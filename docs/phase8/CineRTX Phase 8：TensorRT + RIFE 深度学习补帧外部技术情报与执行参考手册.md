# CineRTX Phase 8：TensorRT + RIFE 深度学习补帧外部技术情报与执行参考手册

> **文档定位**：本手册面向 CineRTX Phase 8 路线 B（母版级画质补帧）执行工程师，基于仓库 `zhaotong96/CineRTX`（快照 `30fbaaaae904c7d208679fbcdb358bd8aa924e2a`）的现有代码布局与《CineRTX Phase 8：TensorRT + RIFE 深度学习补帧方案施工方案》，补充 TensorRT 10.13.3.9 C++ ABI、Practical-RIFE v4.25 张量拓扑、CUDA Driver API 零依赖 PTX 核函数、混合精度算子隔离及 Windows 动态库装载机制等底层技术细节，消除编码试错成本。
> 
> 

---

## 一、 仓库架构现状对齐与施工落点约束

在动手编写代码前，执行工程师必须严格对齐当前仓库已固化的工程拓扑与构建门禁[cite: 1, 4]：

### 1. 专有 SDK 单一隔离边界（`rtx_ngx_bridge.dll`）

* **现有代码布局**：当前仓库在 `src/shim/ngx_bridge/` 下已建立了完整的专有桥接层，包含 `bridge_exports.cpp`、`ngx_d3d12_bridge.cpp`、`d3d12_interop.cpp`、`p010_pack_runtime.cpp` 与 `ngx_msvc_crt_compat.cpp`。


* **统一收口原则**：所有 TensorRT（`nvinfer_10.dll` / `nvonnxparser_10.dll`）与 RIFE 推理代码必须内聚在 `rtx_ngx_bridge.dll` 内部，仅通过纯 C 头文件 `include/shim/ngx_bridge_abi.h` 向上层 `rtx_core` 暴露接口[cite: 1, 4]。
* **历史残留清理提示**：`src/shim/loader/nv_runtime.cpp` 与 `src/shim/loader/nv_backend_shim.cpp` 中仍遗留有 Phase 3 时期的 `loadTensorRt` / `loadNvOfruc` 探测残余[cite: 1, 3]。执行工程师不应在 `rtx_shim_static` 中重复实现 TensorRT 加载，而应统一走 `ngx_bridge_abi.h` 路线，并在清理阶段移除 `nv_runtime.cpp` 中的失效探测逻辑。



### 2. 零 `cudart` 依赖的 PTX 嵌入式工作流（`rtx_ngx_bridge` 必遵规范）

* **关键发现**：检查仓库 `src/shim/ngx_bridge/` 与 `tools/` 可知，现有的 CUDA 像素重排模块（`p010_pack.cu`）**并未链接 `cudart.lib` 或使用 `<<< >>>` 运行时语法**，而是通过 `tools/gen_p010_ptx.py` 编译为 `p010_pack.ptx`，再由 `tools/embed_ptx.py` 嵌入为 C++ 字节数组，最终在 `p010_pack_runtime.cpp` 中通过 **CUDA Driver API（`cuModuleLoadDataEx` + `cuLaunchKernel`）** 调度执行。


* **施工要求**：新建的 `src/shim/ngx_bridge/rife_pack.cu`（预处理与后处理核函数）必须沿用同一套 `cu` $\to$ `.ptx` $\to$ `embed_ptx.py` $\to$ `cuLaunchKernel` 机制。若误用 CUDA Runtime API，将导致 `rtx_ngx_bridge.dll` 静态依赖外部 `cudart64_*.dll`，破坏绿色单包分发与 `tools/audit_imports.ps1` 导入表审计[cite: 1, 4]。



### 3. 实机 GPU 计算能力校准（Blackwell `sm_120`）

* 开发机搭载的 **NVIDIA GeForce RTX 5060 Laptop GPU** 属于 **Blackwell 架构**，其真实 CUDA Compute Capability 为 **`12.0`（`sm_120`）**（驱动 572.70+，CUDA 13.5 支持）。


* 离线生成 `rife_pack.ptx` 时，`nvcc` 应指定 `-gencode arch=compute_89,code=compute_89`（保留 PTX 虚拟架构字节码供驱动 JIT 编译至 `sm_120`）或使用 CUDA 13.0+ 工具链直接目标化 `compute_120`。



---

## 二、 Track 1：Windows TensorRT 10.13.3.9 安全动态装载与 ABI 规范

### 1. `nvinfer_10.dll` 与 `nvonnxparser_10.dll` 导出符号与版本校验

TensorRT 10.x 的 C++ 头文件（`NvInfer.h`、`NvOnnxParser.h`）将工厂函数定义为 `inline` 包装器，底层调用 DLL 导出的 C 符号。在 `src/shim/ngx_bridge/trt_loader.cpp` 中应解析以下符号：

```cpp
#include <windows.h>
#include <cstdint>
#include "NvInfer.h"
#include "NvOnnxParser.h"

// TensorRT 10.13.3.9 的 NV_TENSORRT_VERSION 编码规则：
// (MAJOR * 10000) + (MINOR * 100) + PATCH = 10 * 10000 + 13 * 100 + 3 = 101303
constexpr int32_t kExpectedTrtVersion = 101303;

using PFN_CreateInferRuntimeInternal = void* (*)(void* logger, int32_t version);
using PFN_CreateInferBuilderInternal = void* (*)(void* logger, int32_t version);
using PFN_GetInferLibVersion         = int32_t (*)();
using PFN_CreateNvOnnxParserInternal = void* (*)(void* network, void* logger, int32_t version);

```

* **版本防劣化门禁**：加载 `nvinfer_10.dll` 后立即调用 `getInferLibVersion()`；若检测到版本 $\ge 101400$（TensorRT 10.14/10.15），必须记录警告或拒绝构建混合精度 RIFE 引擎。经 NVIDIA 官方与社区实证（`pytorch/TensorRT` Issue #4074），TensorRT 10.14+ 在混合精度（FP16 + FP32 `GridSample`）优化器阶段存在回归缺陷，会导致 RIFE 推理显存暴涨（2.7 GB $\to$ 10.0 GB）及输出张量损坏。



### 2. Windows `nvinfer_builder_resource_10.dll` 隐式加载陷阱（关键避坑）

* **问题根因**：在 Windows 平台上，`nvinfer_10.dll` 的 PE 导入表并未静态链接 `nvinfer_builder_resource_10.dll`（Myelin/Tactic 编译资源库），而是在调用 `createInferBuilder_INTERNAL` 或 `buildSerializedNetwork` 时内部执行 `LoadLibraryA("nvinfer_builder_resource_10.dll")`。
* **常见错误**：仅使用 `LoadLibraryExW(trtDllPath, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)` 加载 `nvinfer_10.dll` 时，`LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR` 仅对 `nvinfer_10.dll` 的静态导入依赖生效，后续 `nvinfer_10.dll` 内部调用 `LoadLibraryA` 仍会找不到同目录下的 `nvinfer_builder_resource_10.dll` 并报错退出。
* **标准解法**：在加载任何 TensorRT DLL 之前，调用 `SetDefaultDllDirectories` 与 `AddDllDirectory` 将 TensorRT 目录加入进程安全搜索路径：

```cpp
SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
DLL_DIRECTORY_COOKIE cookie = AddDllDirectory(trtBinDir.c_str());
HMODULE hNvInfer = LoadLibraryExW(
    (trtBinDir / L"nvinfer_10.dll").c_str(),
    nullptr,
    LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
// 仅在需要从 ONNX 编译 .engine 时按需加载 nvonnxparser_10.dll

```

---

## 三、 Track 2：Practical-RIFE v4.25 张量多态适配与 CUDA 核函数实现

### 1. RIFE v4.25 ONNX 输入/输出张量变体兼容设计

社区与官方导出的 Practical-RIFE v4.25 ONNX 模型（基于 `IFNet_HDv3`）在输入签名上存在两种常见封装形态：

| 模型变体类型 | 输入张量签名（Tensor Name & Shape） | 输出张量签名 | 预处理内存布局策略 |
| --- | --- | --- | --- |
| **变体 A：单张量 7 通道拼接**（`Practical-RIFE` 原生导出） | 1 个输入：<br>

<br>`input`：`FP32/FP16 [1, 7, H_pad, W_pad]` | `output`：`[1, 3, H_pad, W_pad]` | `C0..C2` = `img0` RGB<br>

<br>`C3..C5` = `img1` RGB<br>

<br>`C6` = 整平面填充常量 $\alpha$ |
| **变体 B：多张量分离输入**（`vs-mlrt` / 标准化导出） | 3 个输入：<br>

<br>`img0`：`[1, 3, H_pad, W_pad]`<br>

<br>`img1`：`[1, 3, H_pad, W_pad]`<br>

<br>`timestep`：`[1, 1, H_pad, W_pad]` 或 `[1]` | `output`：`[1, 3, H_pad, W_pad]` | `img0` 与 `img1` 各自写入独立的 3 通道缓冲区；`timestep` 按实际维度写入标量或空间平面 |

* **运行时自适应探测**：在 `rtx_ngx_bridge_init_rife` 中，通过 `engine->getNbIOTensors()` 与 `engine->getTensorIOMode(name)` 自动识别当前 `.engine` 属于变体 A 还是变体 B，并查询 `engine->getTensorDataType(name)` 确认输入张量期望 `DataType::kFLOAT` 还是 `DataType::kHALF`，避免硬编码单一模型结构导致兼容失败。



### 2. 空间尺寸对齐与反射填充（Reflection Padding）数学公式

* **对齐粒度选择**：RIFE v4.25 的 `IFNet` 包含 5 级多尺度光流金字塔（`scale_list = [16, 8, 4, 2, 1]`）。常规 `scale = 1.0` 模式下要求宽高为 **`32`（建议 `64`）** 的整数倍；若模型支持 `scale = 0.5`（4K 高分辨率半分辨率光流估计加速模式），则要求宽高为 **`128`** 的整数倍：



$$W_{\text{pad}} = \lceil W / A \rceil \times A, \quad H_{\text{pad}} = \lceil H / A \rceil \times A \quad (A \in \{64, 128\})$$



例如 $1920 \times 1080$ 在 $A = 64$ 时填充为 $1920 \times 1088$（底部填充 8 行）；在 $A = 128$ 时填充为 $1920 \times 1152$（底部填充 72 行）。


* **CUDA `reflect101` 越界镜像映射**：填充区域严禁填 `0`（黑边会在光流金字塔边缘引入虚假运动矢量）。在 `rife_pack.cu` 中对越界坐标 $(x, y)$ 执行 `reflect101` 镜像折叠：



```cpp
__device__ __forceinline__ int reflect101(int idx, int limit) {
    if (limit <= 1) return 0;
    while (idx < 0 || idx >= limit) {
        if (idx < 0) idx = -idx;
        if (idx >= limit) idx = 2 * limit - idx - 2;
    }
    return idx;
}

```

### 3. NV12 $\leftrightarrow$ RGB 色彩矩阵与高精度防溢出保护

* **Studio/Limited Range 色彩还原（防黑位发灰）**：NVDEC 解码输出的 8-bit NV12 视频绝大多数为 **BT.709 Limited Range**（$Y \in [16, 235]$，$Cb, Cr \in [16, 240]$）。预处理核函数应根据输入帧的 `is_full_range` 标志选择对应转换矩阵，将输出严格归一化至 $[0.0f, 1.0f]$：


* **BT.709 Limited Range 转换公式**：

$$Y' = \frac{Y - 16.0f}{219.0f}, \quad U' = \frac{Cb - 128.0f}{224.0f}, \quad V' = \frac{Cr - 128.0f}{224.0f}$$


$$\begin{aligned} R &= \text{clamp}(Y' + 1.5748f \cdot V',\ 0.0f,\ 1.0f) \\ G &= \text{clamp}(Y' - 0.187324f \cdot U' - 0.468124f \cdot V',\ 0.0f,\ 1.0f) \\ B &= \text{clamp}(Y' + 1.8556f \cdot U',\ 0.0f,\ 1.0f) \end{aligned}$$


* **BT.709 Full Range 转换公式**（当 `color_range == AVCOL_RANGE_JPEG` 时）：

$$Y' = \frac{Y}{255.0f}, \quad U' = \frac{Cb - 128.0f}{255.0f}, \quad V' = \frac{Cr - 128.0f}{255.0f}$$




* **后处理 NaN / 溢出安全清洗**：在 RIFE 输出张量转回 NV12（或直接输出 P010）的后处理核函数中，对每个通道分量执行 `isfinite(v) ? fminf(fmaxf(v, 0.0f), 1.0f) : 0.0f` 清洗，彻底消除 FP16 推理在极高光边缘偶尔产生的 `Inf`/`NaN` 黑斑伪影。



---

## 四、 Track 3：TensorRT 10.13 引擎编译与混合精度算子隔离

### 1. 敏感算子 FP32 强制锁定（防 `GridSample` 坐标量化阶梯）

在 `rife_builder.cpp` 构建 TensorRT 引擎时，若全局开启 `BuilderFlag::kFP16` 而不加限制，RIFE 内部的双线性网格采样（`GridSample`）及光流归一化除法层（将像素位移除以 $(W-1)/2$ 与 $(H-1)/2$）会因 FP16 尾数仅 10 位而在 1080p/4K 图像上产生明显网格阶梯撕裂。

在 TensorRT 10.13 中，**必须同时设置层计算精度（`setPrecision`）与层输出张量类型（`setOutputType`）**，并开启 `BuilderFlag::kOBEY_PRECISION_CONSTRAINTS`：

```cpp
config->setFlag(nvinfer1::BuilderFlag::kFP16);
config->setFlag(nvinfer1::BuilderFlag::kOBEY_PRECISION_CONSTRAINTS);
// 为 8GB 显卡设置安全构建工作空间上限（1 GB）
config->setMemoryPoolLimit(nvinfer1::MemoryPoolType::kWORKSPACE, 1ULL << 30);

for (int32_t i = 0; i < network->getNbLayers(); ++i) {
    nvinfer1::ILayer* layer = network->getLayer(i);
    if (!layer) continue;

    bool forceFp32 = false;
    const auto type = layer->getType();

    // 1. 空间变形采样层（ONNX GridSample）
    if (type == nvinfer1::LayerType::kGRID_SAMPLE) {
        forceFp32 = true;
    }
    // 2. 光流归一化除法层（Div）与倒数层（Recip）
    else if (type == nvinfer1::LayerType::kELEMENTWISE) {
        auto* ew = static_cast<nvinfer1::IElementWiseLayer*>(layer);
        if (ew->getOperation() == nvinfer1::ElementWiseOperation::kDIV) {
            forceFp32 = true;
        }
    } else if (type == nvinfer1::LayerType::kUNARY) {
        auto* un = static_cast<nvinfer1::IUnaryLayer*>(layer);
        if (un->getOperation() == nvinfer1::UnaryOperation::kRECIP) {
            forceFp32 = true;
        }
    }
    // 3. 遮挡掩膜归一化层（Softmax / Reduce）
    else if (type == nvinfer1::LayerType::kSOFTMAX || type == nvinfer1::LayerType::kREDUCE) {
        forceFp32 = true;
    }

    if (forceFp32) {
        layer->setPrecision(nvinfer1::DataType::kFLOAT);
        for (int32_t outIdx = 0; outIdx < layer->getNbOutputs(); ++outIdx) {
            layer->setOutputType(outIdx, nvinfer1::DataType::kFLOAT);
        }
    }
}

```

### 2. 动态分辨率 Profile 与 `.engine` 缓存键规约

* **Optimization Profile 显存边界控制**：为兼顾 8GB 显存安全线（全管线总显存 $\le 4.0\text{ GB}$），构建 `.engine` 时应配置合理的 `IOptimizationProfile`：


* `OptProfileSelector::kMIN`：`[1, C, 256, 256]`
* `OptProfileSelector::kOPT`：`[1, C, 1088, 1920]`（1080p 标准常用分辨率）
* `OptProfileSelector::kMAX`：`[1, C, 2176, 3840]`（或针对 8GB 显卡限制在 4K 输入下启用 `scale = 0.5`，避免中间特征图占用超标）


* **硬件绑定缓存文件名格式**：`.engine` 文件与 GPU SM 架构强绑定，建议按以下规范自动命名并复用：
`<onnx_stem>_trt101303_sm<major><minor>_fp16.engine`（例如 `rife_v4.25_trt101303_sm120_fp16.engine`）。

---

## 五、 Track 4：Flicks 滑动窗口时钟、场景切换保护与系统同步

### 1. Flicks（705.6 MHz）任意倍率 `1:N` 调度状态机

为彻底解决任意帧率转换（如 `24fps -> 60fps` 非整数 `2.5x` 插帧、`23.976fps -> 59.94fps`、`30fps -> 60fps`）的音画同步与时间戳漂移问题，主流水线采用系统既有的 `705,600,000 Hz`（Flicks）整型时间基[cite: 1, 2, 4]：

* **精确整型步长**：

$$\Delta_{\text{in}} = \frac{705,600,000 \times \text{src\_fps\_den}}{\text{src\_fps\_num}}, \quad \Delta_{\text{out}} = \frac{705,600,000 \times \text{dst\_fps\_den}}{\text{dst\_fps\_num}}$$


* **相位决策与原始帧零损耗直通**：
维护双帧显存滑动窗口 `[prev_frame (t0), curr_frame (t1)]` 及单调递增的输出光标 `next_out_pts`：
* 当 `next_out_pts == t0`（$\alpha = 0.0$）时，**不调用 RIFE/NVOFA 推理**，直接将原始 `prev_frame` 送入 RTX VSR + TrueHDR 重制管线，100% 保留原片原始帧锐度；


* 当 `t0 < next_out_pts < t1` 时，计算精确归一化相位 $\alpha = \frac{\text{next\_out\_pts} - t_0}{t_1 - t_0}$，调用 `rtx_ngx_bridge` 生成中间插值帧；
* 每输出一帧执行 `next_out_pts += delta_out`（纯 `int64_t` 整数加法，跨数小时转码零误差）。



### 2. GPU 场景切换（Scene Cut）硬切检测实现

门禁 2 要求遥测日志明确输出 `fruc: backend=rife-trt, interpolated=..., scene cuts=...`。在 `rife_pack.cu` 预处理阶段可顺带完成零额外显存拷贝的 **Y 平面归一化 SAD（Sum of Absolute Differences）** 统计：

1. 在 CUDA 核函数中计算 `prev_frame` 与 `curr_frame` 亮度平面 Y 的像素绝对差之和 $\sum \vert{}Y_1(x,y) - Y_0(x,y)\vert{}$，通过 `__shfl_down_sync` 线程束规约与 `atomicAdd` 累加至 4 字节设备端计数器 `d_sad_sum`；
2. 计算归一化帧间差异度：

$$\text{diff}_{\text{norm}} = \frac{\text{sad\_sum}}{W \times H \times 255.0f}$$


3. **硬切旁路裁决**：当 `diff_norm > 0.35f`（镜头发生硬切，两帧内容无空间连续性）时，`scene_cuts++` 并跳过 RIFE 神经网络推理，直接按 $\alpha < 0.5$ 复制 `prev_frame`、$\alpha \ge 0.5$ 复制 `curr_frame`，彻底杜绝转场镜头出现扭曲鬼影。

### 3. CUDA Stream 与 D3D12 NGX 管线零拷贝同步

* RIFE 推理结果写入显存插值缓冲后，需交由 `ngx_d3d12_bridge.cpp` 执行 RTX VSR 超分与 TrueHDR 重制[cite: 1, 4]。
* 利用 `src/shim/ngx_bridge/d3d12_interop.cpp` 中已有的 `ID3D12Fence` / `CUexternalSemaphore` 互操作机制：


1. RIFE 后处理 CUDA 核函数完成后，在 `cuStream` 上执行 `cuSignalExternalSemaphoresAsync` 推进 D3D12 Fence；
2. D3D12 命令队列执行 `ID3D12CommandQueue::Wait(fence, fenceValue)` 后立即执行 NGX VSR + TrueHDR；
3. 全程无需调用 `cudaStreamSynchronize` 阻塞 CPU 主线程，保持 NVDEC $\to$ RIFE $\to$ NGX $\to$ NVENC 流水线满载吞吐。





---

## 六、 CMake 构建集成与交付门禁自检清单

### 1. CMake 目标编排要点（`CMakeLists.txt` & `cmake/RtxNvidiaSdks.cmake`）

* **头文件依赖隔离**：仅为 `rtx_ngx_bridge` 目标添加 `target_include_directories(rtx_ngx_bridge PRIVATE "${TRT_ROOT}/include")`，严禁将 TensorRT 头文件路径暴露给 `rtx_core` 或 `CineRTX` 主可执行文件[cite: 1, 4]。
* **PTX 编译挂接**：将 `src/shim/ngx_bridge/rife_pack.cu` 接入与 `p010_pack.cu` 相同的自定义构建命令，生成 `rife_pack_ptx.h` 供 `rife_pack_runtime.cpp` 编译期内嵌。


* **Staging 校验更新**：确认 `cmake/VerifyNgxStaging.cmake` 不会在未开启 RIFE 时强制要求输出目录必须存在 `nvinfer_10.dll`，确保默认 NVOFA 路线在零外部 TensorRT DLL 环境下依然通过全部门禁测试。



### 2. 提交前自动化门禁核查表

| 门禁编号 | 检查项 | 验证命令 / 检查方式 | 通过标准 |
| --- | --- | --- | --- |
| **Gate 1** | 默认路径零退化 | `CineRTX.exe --input in.mp4 --output out.mp4` | 默认激活 `NVOFA`，不依赖 TensorRT/RIFE 权重即可输出 4K60 AV1 10-bit

 |
| **Gate 2** | RIFE 真实插帧与显存上限 | `CineRTX.exe --input test_24fps.mp4 --output out.mp4 --rife-engine models/rife_v4.25.engine` | 日志输出 `fruc: backend=rife-trt, interpolated=..., scene cuts=...`；峰值显存 $\le 4.0\text{ GB}$；插值帧 SAD $> 0$

 |
| **Gate 3** | PE 导入表纯净度 | `pwsh tools/audit_imports.ps1` | `CineRTX.exe` 与 `rtx_tests.exe` 静态导入表中 `nvinfer`、`nvonnxparser`、`cudart` 计数均为 **0**[cite: 1, 4] |
| **Gate 4** | 许可证、模型防污染与脱敏 | `pwsh tools/check_spdx.ps1` | 新增文件带有效 SPDX 头；单个源文件 $\le 1,200$ 行；无 `.onnx`/`.engine` 入库；无私有 IP 或本机绝对路径[cite: 1, 4] |