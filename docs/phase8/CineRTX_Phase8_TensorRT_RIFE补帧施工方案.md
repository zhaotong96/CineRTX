# CineRTX Phase 8：TensorRT + RIFE 深度学习补帧方案施工方案

**文档标识**：`DOC-PHASE8-TRT-RIFE-CONSTRUCTION-002`  
**所属目录**：[`docs/phase8/CineRTX_Phase8_TensorRT_RIFE补帧施工方案.md`](./CineRTX_Phase8_TensorRT_RIFE补帧施工方案.md)  
**参考基线**：[`CineRTX Phase 8：TensorRT + RIFE 深度学习补帧外部技术情报与执行参考手册`](./CineRTX%20Phase%208：TensorRT%20+%20RIFE%20深度学习补帧外部技术情报与执行参考手册.md)  
**仓库快照**：`zhaotong96/CineRTX`（`30fbaaaae904c7d208679fbcdb358bd8aa924e2a`）  
**起草机构**：CineRTX 核心架构与工程指导组  
**定位与目标**：本文件是 CineRTX 补帧体系第二阶段（路线 B：母版级画质方案）的**端到端工程施工指南**。在路线 A（NVOFA 硬件光流）已内聚于 `rtx_ngx_bridge.dll` 并开箱即用的基础上，规范挂接 **TensorRT 10.13.3.9 + RIFE v4.25** 神经网络补帧分支，满足创作者对大运动伪影消除与极致画质的严苛诉求。

---

## 一、 双路补帧顶层定位与架构拓扑

根据产品规格定义，CineRTX 补帧模块采用**双路互补架构**：

```
                      ┌──────────────────────────────────────────────┐
                      │              输入源流 (NVDEC 解码)            │
                      └──────────────────────┬───────────────────────┘
                                             │
                        ┌────────────────────┴───────────────────┐
                        │      Flicks 滑动窗口时钟决策 (1:N)      │
                        └────────────────────┬───────────────────┘
                                             │
                      ┌──────────────────────┴──────────────────────┐
                      ▼                                             ▼
       【路线 A：默认极速 (NVOFA)】                   【路线 B：母版画质 (TensorRT RIFE)】
        * 驱动原生 nvofapi64.dll                      * TensorRT 10.13.3.9 + RIFE v4.25
        * 硬件光流专用加速器 (OFA)                    * 5th Gen Tensor Cores 深度学习推理
        * 零外挂依赖 · >100 FPS 吞吐                   * 大运动无撕裂 · 极佳遮挡处理 · ~60 FPS
                      │                                             │
                      └──────────────────────┬──────────────────────┘
                                             │
                                             ▼
                      ┌──────────────────────────────────────────────┐
                      │    RTX VSR 4K 超分 + TrueHDR 1000 nits       │
                      └──────────────────────┬───────────────────────┘
                                             │
                                             ▼
                      ┌──────────────────────────────────────────────┐
                      │          NVENC 硬件编码 (AV1 10-bit)          │
                      └──────────────────────────────────────────────┘
```

### 1.1 核心设计守则

1. **开箱即用不破坏**：默认情况下引擎仍然使用路线 A（NVOFA 硬件光流），用户不传可选参数时永远是 4K 60fps AV1 10-bit 黄金体验；
2. **专有 SDK 单一隔离边界**：所有 TensorRT（`nvinfer_10.dll` / `nvonnxparser_10.dll`）与 RIFE 推理代码**必须内聚在 `rtx_ngx_bridge.dll` 内部**，仅通过纯 C 头文件 [`include/shim/ngx_bridge_abi.h`](../../include/shim/ngx_bridge_abi.h) 向上层 `rtx_core` 暴露接口。严禁引入第二个 Bridge DLL——Phase 8 返工已明确"三桥归一"原则；
3. **零导入表污染（GPL 防火墙）**：`CineRTX.exe` PE 导入表严禁静态链接 `nvinfer*.dll`、`nvonnxparser*.dll` 或 `cudart64_*.dll`，所有 TensorRT/CUDA 运行时必须通过动态加载（`LoadLibraryExW`）在桥接层内部解析；
4. **显存内原位零拷贝流转**：输入张量构建、推理执行、Output Tensor 解算与插值帧装配，全部在 GPU 显存内异步完成，D2H/H2D 主机内存拷贝严格为 0；
5. **零 `cudart` 依赖的 PTX 工作流**：新建的 CUDA 核函数必须沿用现有 `p010_pack.cu` → `.ptx` → `embed_ptx.py` → `cuLaunchKernel` 机制，严禁使用 `<<<>>>` Runtime API。

---

## 二、 关键前置依赖与版本锁定（Ground Truth）

### 2.1 依赖规约与避坑指南

| 依赖项 | 版本锁定 | 来源 | 备注 |
| :--- | :--- | :--- | :--- |
| **TensorRT** | **10.13.3.9** | [`build/external/TensorRT-10.13.3.9.Windows.win10.cuda-13.0.zip`](../../build/external/TensorRT-10.13.3.9.Windows.win10.cuda-13.0.zip) | 官方归档已缓存 |
| **RIFE 模型** | **Practical-RIFE v4.25** (`IFNet_HDv3`) | 外部路径，运行时 CLI 注入 | 严禁入库 |
| **CUDA 工具链** | ≥ 12.6（PTX 离线编译用） | 本机 CUDA Toolkit | 仅构建时需要 |
| **开发机 GPU** | RTX 5060 Laptop（Blackwell `sm_120`） | 驱动 572.70+ | `compute_89` PTX → JIT `sm_120` |

> [!CAUTION]
> **严禁升级至 TensorRT 10.14 或 10.15！** 经 NVIDIA 官方与社区实证（`pytorch/TensorRT` Issue #4074），TRT 10.14+ 在混合精度（FP16 + FP32 `GridSample`）优化器阶段存在回归缺陷，导致 RIFE 推理显存从 2.7 GB 暴涨至 10.0 GB 且输出张量损坏。版本门禁编码规则：`NV_TENSORRT_VERSION = (MAJOR * 10000) + (MINOR * 100) + PATCH = 101303`。加载 `nvinfer_10.dll` 后立即调用 `getInferLibVersion()` 校验，若 $\ge 101400$ 必须拒绝构建引擎。

### 2.2 动态库交付物

- **推理运行时**：`nvinfer_10.dll`（~18 MB）
- **ONNX 解析器**：`nvonnxparser_10.dll`（仅在构建 `.engine` 时按需加载，~1.2 MB）
- **构建资源库**：`nvinfer_builder_resource_10.dll`（由 `nvinfer_10.dll` 内部隐式 `LoadLibraryA` 加载，见§三·Track 1 避坑条目）

### 2.3 模型权重防污染

- 核心模型：`rife_v4.25.onnx`（以及对应预编译 `.engine`）独立存放于外部路径（如 `models/`），通过 CLI 参数 `--rife-engine` 或 `--build-rife-engine` 指定；
- `tools/check_spdx.ps1` 已有规则拦截 `.onnx` / `.engine` 入库。

---

## 三、 现代 TensorRT 10.x 接口适配规范

TensorRT 10.x 彻底废弃了 8.x/9.x 时代的 Binding Index API（如 `enqueueV2`、`getBindingDimensions`）。执行工程师编写推理代码时，**必须严格使用现代 Tensor Name API**：

| 废弃接口（TRT 8.x/9.x，编译报错） | 现代标准接口（TRT 10.13.3.9） | 规范说明 |
| :--- | :--- | :--- |
| `context->enqueueV2(buffers, stream, nullptr)` | `context->enqueueV3(stream)` | 异步发射入口，缓冲区指针提前绑定至张量名称 |
| `engine->getBindingDimensions(index)` | `engine->getTensorShape(tensor_name)` | 通过张量名查询维度 |
| `context->setBindingDimensions(index, dims)` | `context->setInputShape(tensor_name, dims)` | 动态轴尺寸设定 |
| `buffers[index] = dev_ptr` | `context->setTensorAddress(tensor_name, dev_ptr)` | 显存裸指针绑定至命名张量 |
| `engine->getBindingDataType(index)` | `engine->getTensorDataType(tensor_name)` | 获取张量数据类型 |
| `setMaxWorkspaceSize(bytes)` | `setMemoryPoolLimit(kWORKSPACE, bytes)` | 工作空间显存上限 |

> [!IMPORTANT]
> 使用 `engine->getNbIOTensors()` 枚举所有 I/O 张量名称，配合 `engine->getTensorIOMode(name)` 判断输入/输出方向，**运行时自适应**探测当前 `.engine` 属于 RIFE 变体 A（7 通道单张量）还是变体 B（3 输入分离），严禁硬编码单一模型结构。

---

## 四、 施工任务拆解（Track 1 ~ Track 4）

### Track 1：TensorRT 动态装载与 ABI 契约层扩展（`src/shim/ngx_bridge/`）

在现有 `rtx_ngx_bridge.dll` 内部增加 RIFE 推理与引擎构建能力，**不引入第二个 DLL**。

#### 1.1 ABI 扩展（[`include/shim/ngx_bridge_abi.h`](../../include/shim/ngx_bridge_abi.h)）

在现有 ABI v3 的 append-only 区域追加 RIFE 专用导出函数：

```c
/* Phase 8 路线 B append-only ABI 扩展：TensorRT RIFE 深度学习补帧 */

RTX_SHIM_API int32_t RTX_SHIM_CALL rtx_ngx_bridge_init_rife(
    RtxNgxBridge* bridge,
    const char* engine_path,    /* 预编译 .engine 文件路径 */
    uint32_t width,
    uint32_t height
);

RTX_SHIM_API int32_t RTX_SHIM_CALL rtx_ngx_bridge_interpolate_rife(
    RtxNgxBridge* bridge,
    uint64_t prev_nv12,         /* CUdeviceptr: 前一帧 NV12 */
    uint64_t curr_nv12,         /* CUdeviceptr: 当前帧 NV12 */
    uint32_t pitch,             /* NV12 行距 (bytes) */
    float alpha,                /* 时间相位 ∈ (0, 1) */
    uint64_t out_nv12,          /* CUdeviceptr: 输出 NV12 缓冲 */
    int32_t full_range,         /* 0=Limited Range, 1=Full Range */
    int32_t* scene_cut          /* out: 1=检测到场景硬切，已旁路推理 */
);

RTX_SHIM_API int32_t RTX_SHIM_CALL rtx_ngx_bridge_build_rife_engine(
    const char* onnx_path,      /* 输入 ONNX 模型路径 */
    const char* engine_out_path,/* 输出 .engine 文件路径 */
    uint32_t width,
    uint32_t height,
    char* err_buf,              /* 错误消息缓冲区 */
    uint32_t err_cap            /* err_buf 容量 */
);

RTX_SHIM_API void RTX_SHIM_CALL rtx_ngx_bridge_destroy_rife(
    RtxNgxBridge* bridge
);
```

> [!NOTE]
> ABI 红线约束与现有 `ngx_bridge_abi.h` 完全对齐：所有字段固定宽度（无 `bool`/`size_t`），`CUdeviceptr` 以 `uint64_t` 传递，错误通过 `err_buf` + `err_cap` 或返回值传播。`scene_cut` 输出参数允许上层状态机在场景硬切时跳过推理直接复用原帧。

#### 1.2 动态装载器（`src/shim/ngx_bridge/trt_loader.cpp`，新建）

```cpp
// 核心职责：
// 1. LoadLibraryExW 加载 nvinfer_10.dll，按需解析导出符号
// 2. 版本门禁：getInferLibVersion() 校验 == 101303
// 3. nvinfer_builder_resource_10.dll 隐式加载陷阱规避

// 关键避坑：Windows DLL 搜索路径设置
SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
DLL_DIRECTORY_COOKIE cookie = AddDllDirectory(trtBinDir.c_str());
HMODULE hNvInfer = LoadLibraryExW(
    (trtBinDir / L"nvinfer_10.dll").c_str(),
    nullptr,
    LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
// ↑ 必须包含 LOAD_LIBRARY_SEARCH_DEFAULT_DIRS，否则 nvinfer_10.dll
// 内部 LoadLibraryA("nvinfer_builder_resource_10.dll") 将找不到同目录 DLL！
```

需解析的 C 导出符号：

| 符号名 | 类型签名 | 来源 DLL |
| :--- | :--- | :--- |
| `createInferRuntime_INTERNAL` | `void* (*)(void* logger, int32_t version)` | `nvinfer_10.dll` |
| `createInferBuilder_INTERNAL` | `void* (*)(void* logger, int32_t version)` | `nvinfer_10.dll` |
| `getInferLibVersion` | `int32_t (*)()` | `nvinfer_10.dll` |
| `createNvOnnxParser_INTERNAL` | `void* (*)(void* network, void* logger, int32_t version)` | `nvonnxparser_10.dll` |

**安全降级**：当 TensorRT DLL 不存在时，`rtx_ngx_bridge_init_rife()` 返回 `RTX_ERR_DLL_MISSING`，上层平滑退回路线 A（NVOFA）。不产生 crash。

> [!TIP]
> 历史残留清理：`src/shim/loader/nv_runtime.cpp` 中 Phase 3 遗留的 `loadTensorRt` / `loadNvOfruc` 探测残余应在本轮清理中移除。所有 TensorRT 加载统一走 `trt_loader.cpp` 内部实现，不暴露给 `rtx_shim_static`。

#### 1.3 RIFE 推理引擎封装（`src/shim/ngx_bridge/rife_engine.cpp`，新建）

核心类 `RifeEngine`（内部实现，不暴露于 ABI）：

```cpp
class RifeEngine {
public:
    // 从 .engine 反序列化（~50ms）或从 ONNX 首次构建（20~40s）
    bool loadEngine(const char* engine_path, uint32_t width, uint32_t height);

    // 执行一次推理：prev_rgb + curr_rgb + alpha → interp_rgb
    // 全异步 GPU 操作，在指定 CUstream 上排队
    bool infer(void* stream, float alpha);

    // 模型变体自适应探测
    enum class Variant { SingleTensor7Ch, MultiTensor3Input };
    Variant variant() const;

    // 显存缓冲区访问
    uint64_t inputBuffer(int index) const;
    uint64_t outputBuffer() const;

    // 查询填充后的实际推理尺寸
    uint32_t paddedWidth() const;
    uint32_t paddedHeight() const;

private:
    // TensorRT 对象由 trt_loader.cpp 提供的函数指针创建
    // nvinfer1::IRuntime*, nvinfer1::ICudaEngine*, nvinfer1::IExecutionContext*
    // 显存缓冲区：2× RGB FP32 输入 + 1× RGB FP32 输出
};
```

**运行时模型变体自适应**：

```cpp
// 通过 engine->getNbIOTensors() 与 engine->getTensorIOMode(name)
// 自动识别当前 .engine 属于变体 A (7通道) 还是变体 B (3输入)，
// 并查询 engine->getTensorDataType(name) 确认期望 FP32 还是 FP16
```

| 模型变体 | 输入签名 | 输出签名 | 预处理策略 |
| :--- | :--- | :--- | :--- |
| **变体 A：单张量 7ch** | `input: [1, 7, H_pad, W_pad]` | `output: [1, 3, H_pad, W_pad]` | C0~C2=img0 RGB, C3~C5=img1 RGB, C6=α |
| **变体 B：多张量分离** | `img0: [1,3,H,W]`, `img1: [1,3,H,W]`, `timestep` | `output: [1, 3, H_pad, W_pad]` | 三缓冲区独立写入 |

---

### Track 2：显存内前后处理 CUDA 核函数（`src/shim/ngx_bridge/rife_pack.cu`，新建）

#### 2.1 PTX 嵌入工作流（与 `p010_pack.cu` 完全对称）

```
rife_pack.cu ──nvcc──→ rife_pack.ptx ──embed_ptx.py──→ rife_pack_ptx.h
                                                             │
                                                             ↓
                                          rife_pack_runtime.cpp
                                          (cuModuleLoadDataEx + cuLaunchKernel)
```

- `nvcc` 编译参数：`-gencode arch=compute_89,code=compute_89`（保留 PTX 虚拟架构字节码供驱动 JIT 至 Blackwell `sm_120`）；
- 新建 `tools/gen_rife_ptx.py`：与 `tools/gen_p010_ptx.py` 对称的离线编译脚本；
- **CMake 挂接**：在 `CMakeLists.txt` 中为 `rife_pack.cu` 添加与 `p010_pack.cu` 相同的自定义构建命令，生成 `rife_pack_ptx.h` 供 `rife_pack_runtime.cpp` 编译期内嵌。

> [!WARNING]
> 严禁使用 `<<<>>>` CUDA Runtime 语法或链接 `cudart.lib`！否则 `rtx_ngx_bridge.dll` 将静态依赖外部 `cudart64_*.dll`，破坏绿色单包分发与 `tools/audit_imports.ps1` 导入表审计。

#### 2.2 预处理核函数（NV12 → RGB FP32/FP16 NCHW Planar）

```cpp
// __global__ void rife_preprocess_nv12_to_rgb(
//     const uint8_t* y_plane_0, const uint8_t* uv_plane_0,  // prev_frame NV12
//     const uint8_t* y_plane_1, const uint8_t* uv_plane_1,  // curr_frame NV12
//     float* output_tensor,                                   // [1, 7, H_pad, W_pad]
//     int width, int height, int y_pitch, int uv_pitch,
//     int padded_width, int padded_height,
//     float alpha,                                            // 时间步标量
//     int is_full_range,                                      // 0=Limited, 1=Full
//     uint32_t* d_sad_sum)                                    // 场景切换 SAD 累加器
```

**关键实现要点**：

**（A）BT.709 色彩矩阵转换（防黑位发灰）**：

NVDEC 输出的 8-bit NV12 绝大多数为 **BT.709 Limited Range**（$Y \in [16, 235]$，$Cb, Cr \in [16, 240]$）：

$$Y' = \frac{Y - 16.0f}{219.0f}, \quad U' = \frac{Cb - 128.0f}{224.0f}, \quad V' = \frac{Cr - 128.0f}{224.0f}$$

$$\begin{aligned} R &= \text{clamp}(Y' + 1.5748f \cdot V',\ 0.0f,\ 1.0f) \\ G &= \text{clamp}(Y' - 0.187324f \cdot U' - 0.468124f \cdot V',\ 0.0f,\ 1.0f) \\ B &= \text{clamp}(Y' + 1.8556f \cdot U',\ 0.0f,\ 1.0f) \end{aligned}$$

Full Range 时（`color_range == AVCOL_RANGE_JPEG`）：

$$Y' = \frac{Y}{255.0f}, \quad U' = \frac{Cb - 128.0f}{255.0f}, \quad V' = \frac{Cr - 128.0f}{255.0f}$$

**（B）反射填充（Reflection Padding，防光流虚假运动矢量）**：

RIFE v4.25 的 `IFNet` 包含 5 级多尺度光流金字塔（`scale_list = [16, 8, 4, 2, 1]`），要求宽高为 **64**（建议）或 **128**（`scale=0.5` 模式）的整数倍：

$$W_{\text{pad}} = \lceil W / A \rceil \times A, \quad H_{\text{pad}} = \lceil H / A \rceil \times A \quad (A \in \{64, 128\})$$

填充区域**严禁填 0**（黑边在光流金字塔边缘引入虚假运动矢量）。核函数内对越界坐标执行 `reflect101` 镜像折叠：

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

**（C）GPU 场景切换检测（零额外显存拷贝）**：

在预处理阶段顺带计算 Y 平面归一化 SAD：

1. 核函数内计算 `prev_frame` 与 `curr_frame` 亮度平面的像素绝对差 $\sum |Y_1(x,y) - Y_0(x,y)|$，通过 `__shfl_down_sync` 线程束规约与 `atomicAdd` 累加至设备端计数器 `d_sad_sum`；
2. Host 端计算归一化帧间差异度：

$$\text{diff}_{\text{norm}} = \frac{\text{sad\_sum}}{W \times H \times 255.0f}$$

3. **硬切旁路裁决**：当 `diff_norm > 0.35f` 时，跳过 RIFE 推理，按 $\alpha < 0.5$ 复制 `prev_frame`、$\alpha \ge 0.5$ 复制 `curr_frame`，杜绝转场鬼影。

#### 2.3 后处理核函数（Output Tensor RGB → NV12/P010）

```cpp
// __global__ void rife_postprocess_rgb_to_nv12(
//     const float* rgb_tensor,    // RIFE 输出 [1, 3, H_pad, W_pad]
//     uint8_t* y_plane, uint8_t* uv_plane,  // 输出 NV12
//     int width, int height, int y_pitch, int uv_pitch,
//     int padded_width, int padded_height,
//     int is_full_range)
```

**NaN / 溢出安全清洗（防 FP16 黑斑伪影）**：

```cpp
// 对 RIFE 输出的每个 RGB 分量执行安全清洗
float v = rgb_tensor[offset];
v = isfinite(v) ? fminf(fmaxf(v, 0.0f), 1.0f) : 0.0f;
```

后处理核函数执行反向 BT.709 变换（RGB → YUV），**裁剪回原始分辨率**（丢弃填充区域），将结果直接写入 NV12 输出缓冲。

---

### Track 3：TensorRT 引擎编译与混合精度算子隔离（`src/shim/ngx_bridge/rife_builder.cpp`，新建）

#### 3.1 敏感算子 FP32 强制锁定（防 `GridSample` 坐标量化阶梯）

RIFE 内部的双线性网格采样（`GridSample`）及光流归一化除法层在 FP16 下会因尾数仅 10 位在 1080p/4K 图像上产生明显网格阶梯撕裂。

**必须同时设置层计算精度（`setPrecision`）与层输出张量类型（`setOutputType`）**，并开启 `BuilderFlag::kOBEY_PRECISION_CONSTRAINTS`：

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
    else if (type == nvinfer1::LayerType::kSOFTMAX ||
             type == nvinfer1::LayerType::kREDUCE) {
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

#### 3.2 动态分辨率 Profile 与显存安全边界

为兼顾 8GB 显卡安全线（全管线总显存 $\le 4.0\text{ GB}$），构建 `.engine` 时配置合理的 `IOptimizationProfile`：

| Profile 端点 | 张量维度 | 说明 |
| :--- | :--- | :--- |
| `kMIN` | `[1, C, 256, 256]` | 最小分辨率 |
| `kOPT` | `[1, C, 1088, 1920]` | 1080p 标准（1080→1088 对齐 64） |
| `kMAX` | `[1, C, 2176, 3840]` | 4K 上限（8GB 显卡在 4K 下启用 `scale=0.5`） |

#### 3.3 `.engine` 缓存键与硬件绑定命名

`.engine` 文件与 GPU SM 架构强绑定，按以下规范自动命名：

```
<onnx_stem>_trt101303_sm<major><minor>_fp16.engine
```

示例：`rife_v4.25_trt101303_sm120_fp16.engine`

首次从 ONNX 构建需 20~40 秒；后续直接反序列化 `.engine`（~50ms）。

---

### Track 4：主流水线接线与 Flicks 时钟集成

#### 4.1 CLI 参数正交扩展（[`src/cli/main.cpp`](../../src/cli/main.cpp)）

新增可选参数（不影响现有默认行为）：

| 参数 | 类型 | 语义 | 默认值 |
| :--- | :--- | :--- | :--- |
| `--rife-engine <path>` | 路径 | 指定预编译的 RIFE TensorRT `.engine` | 无（不指定则走路线 A） |
| `--build-rife-engine` | Flag | 离线编译 ONNX 为 `.engine` | — |
| `--onnx <path>` | 路径 | 配合 `--build-rife-engine` 的输入模型 | — |
| `--engine <path>` | 路径 | 配合 `--build-rife-engine` 的输出路径 | — |

**路线选择逻辑**：当传入 `--rife-engine` 且文件存在时，自动激活路线 B；未指定时默认维持路线 A（NVOFA）。

#### 4.2 GUI 控件联动（[`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp)）

在补帧引擎下拉框中加入：
- `60 FPS (NVOFA 硬件光流 - 默认推荐)`
- `60 FPS (TensorRT RIFE 深度学习 - 超高质量)`

当用户选中 RIFE 模式时，弹出文件选择对话框指定 `.engine` 路径，并回写至 `HwTranscodeOptions`。

#### 4.3 Flicks 滑动窗口时钟集成

RIFE 补帧全面复用路线 A 已建立的 Flicks 整型时间基（705,600,000 Hz）与 1:N 滑动窗口状态机：

$$\Delta_{\text{in}} = \frac{705{,}600{,}000 \times \text{src\_fps\_den}}{\text{src\_fps\_num}}, \quad \Delta_{\text{out}} = \frac{705{,}600{,}000 \times \text{dst\_fps\_den}}{\text{dst\_fps\_num}}$$

**相位决策与原始帧零损耗直通**：
- 当 `next_out_pts == t0`（$\alpha = 0.0$）时，**不调用 RIFE 推理**，直接原帧零拷贝送入 NGX 重制管线；
- 当 `t0 < next_out_pts < t1` 时，计算精确归一化相位 $\alpha = \frac{\text{next\_out\_pts} - t_0}{t_1 - t_0}$，调用 `rtx_ngx_bridge_interpolate_rife()` 生成中间帧；
- 每输出一帧执行 `next_out_pts += delta_out`（纯 `int64_t` 整数加法，零浮点漂移）。

#### 4.4 CUDA Stream 与 D3D12 NGX 管线零拷贝同步

RIFE 推理结果写入显存后，需交由 `ngx_d3d12_bridge.cpp` 执行 RTX VSR + TrueHDR 重制。利用 [`d3d12_interop.cpp`](../../src/shim/ngx_bridge/d3d12_interop.cpp) 中已有的 `ID3D12Fence` / `CUexternalSemaphore` 互操作：

1. RIFE 后处理 CUDA 核函数完成后，在 `cuStream` 上执行 `cuSignalExternalSemaphoresAsync` 推进 D3D12 Fence；
2. D3D12 命令队列 `ID3D12CommandQueue::Wait(fence, fenceValue)` 后执行 NGX VSR + TrueHDR；
3. 全程无需 `cudaStreamSynchronize` 阻塞 CPU，保持 NVDEC → RIFE → NGX → NVENC 流水线满载吞吐。

> [!NOTE]
> Phase 8 初始实现建议采用**同步串行**（`cuStreamSynchronize` 在 interpolate 后立即调用），与路线 A 的现有行为一致。异步流水线化是 Phase 9 优化课题。

---

## 五、 新增源文件清单与职责矩阵

| 文件路径 | 类型 | 职责 | 行数预算 |
| :--- | :--- | :--- | :--- |
| `src/shim/ngx_bridge/trt_loader.cpp` | 新建 | TensorRT DLL 动态装载、符号解析、版本门禁 | ≤ 300 |
| `src/shim/ngx_bridge/trt_loader.h` | 新建 | `trt_loader.cpp` 内部头文件（不暴露于 ABI） | ≤ 60 |
| `src/shim/ngx_bridge/rife_engine.cpp` | 新建 | RIFE 引擎构建/推理/变体自适应 | ≤ 500 |
| `src/shim/ngx_bridge/rife_engine.h` | 新建 | `RifeEngine` 类声明 | ≤ 80 |
| `src/shim/ngx_bridge/rife_builder.cpp` | 新建 | ONNX→.engine 编译、混合精度算子隔离 | ≤ 350 |
| `src/shim/ngx_bridge/rife_pack.cu` | 新建 | 预处理+后处理 CUDA 核函数源码 | ≤ 400 |
| `src/shim/ngx_bridge/rife_pack_runtime.cpp` | 新建 | PTX 加载与 `cuLaunchKernel` 调度 | ≤ 250 |
| `src/shim/ngx_bridge/rife_pack_runtime.h` | 新建 | 核函数启动器声明 | ≤ 40 |
| `tools/gen_rife_ptx.py` | 新建 | `rife_pack.cu` → `.ptx` 离线编译脚本 | ≤ 50 |

**现有文件修改**：

| 文件路径 | 修改内容 | 改动程度 |
| :--- | :--- | :--- |
| [`include/shim/ngx_bridge_abi.h`](../../include/shim/ngx_bridge_abi.h) | 追加 4 个 RIFE 导出函数声明 | **微量** |
| [`src/shim/ngx_bridge/bridge_exports.cpp`](../../src/shim/ngx_bridge/bridge_exports.cpp) | 追加 RIFE 导出函数实现（委托至 `RifeEngine`） | **中等** |
| [`src/cli/main.cpp`](../../src/cli/main.cpp) | 新增 `--rife-engine`/`--build-rife-engine` 参数解析 | **微量** |
| [`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp) | 补帧引擎下拉框增加 RIFE 选项 | **微量** |
| [`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp) | 路线 B 调度分支（复用现有滑动窗口） | **微量** |
| [`CMakeLists.txt`](../../CMakeLists.txt) | 新增 RIFE PTX 嵌入构建命令、TRT 头文件路径 | **中等** |

---

## 六、 CMake 构建集成

### 6.1 CMake 目标编排要点

```cmake
# ---- TensorRT SDK 路径（可选，缺失时 RIFE 编译为空壳） ----
set(RTX_TENSORRT_ROOT "" CACHE PATH
    "TensorRT 10.13 root for RIFE backend (optional)")

# ---- RIFE PTX 嵌入（与 p010_pack.ptx 对称） ----
set(RTX_RIFE_PTX_HEADER "${RTX_GENERATED_DIR}/rife_pack_ptx.h")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/shim/ngx_bridge/rife_pack.ptx")
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_ptx.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/shim/ngx_bridge/rife_pack.ptx"
                "${RTX_RIFE_PTX_HEADER}"
        RESULT_VARIABLE _rtx_rife_embed_result)
    if(NOT _rtx_rife_embed_result EQUAL 0)
        message(WARNING "rife_pack PTX embedding failed; RIFE kernels unavailable")
    endif()

    add_custom_command(
        OUTPUT "${RTX_RIFE_PTX_HEADER}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_ptx.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/shim/ngx_bridge/rife_pack.ptx"
                "${RTX_RIFE_PTX_HEADER}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/shim/ngx_bridge/rife_pack.ptx"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_ptx.py"
        COMMENT "Generating rife_pack_ptx.h in build tree"
        VERBATIM)
endif()
```

**关键约束**：

- **头文件依赖隔离**：仅为 `rtx_ngx_bridge` 目标添加 `target_include_directories(rtx_ngx_bridge PRIVATE "${TRT_ROOT}/include")`，**严禁**将 TensorRT 头文件路径暴露给 `rtx_core` 或 `CineRTX`；
- **条件编译**：当 `RTX_TENSORRT_ROOT` 为空时，`rife_engine.cpp` / `rife_builder.cpp` 编译为空壳（所有入口函数返回 `RTX_ERR_DLL_MISSING`），DLL 仍可加载，路线 A 不受影响；
- **Staging 校验**：确认 `cmake/VerifyNgxStaging.cmake` 不在未开启 RIFE 时强制要求 `nvinfer_10.dll` 存在。

### 6.2 MSVC CRT 对齐

`rtx_ngx_bridge.dll` 继续使用 `/MT` 静态 CRT（与主体一致）：

```cmake
set_property(TARGET rtx_ngx_bridge PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
```

---

## 七、 实施里程碑与验收门禁（Acceptance Criteria）

### 门禁 1：零配置默认体验不受影响

- **测试命令**：`CineRTX.exe --input in.mp4 --output out.mp4`
- **通过标准**：100% 走路线 A（NVOFA），输出 4K60 AV1 10-bit，绝不强制要求用户提供 TensorRT 或 RIFE 权重。日志中无 TensorRT 相关错误。

### 门禁 2：RIFE 模式真实插帧与画质验证

- **测试命令**：
  ```bash
  CineRTX.exe --input test_24fps.mp4 --output out_rife_60fps.mp4 \
              --rife-engine models/rife_v4.25_trt101303_sm120_fp16.engine
  ```
- **通过标准**：
  1. 遥测日志明确输出：`fruc: backend=rife-trt, interpolated=..., scene cuts=...`
  2. 峰值显存 $\le 4.0\text{ GB}$（8GB 显卡安全边界）
  3. 抽样中间插值帧 SAD 严格 $> 0$，复杂运动区域无大块扭曲伪影
  4. 场景硬切处无鬼影（`scene cuts` 计数 $> 0$）

### 门禁 3：PE 导入表纯净度

- **验证命令**：`pwsh tools/audit_imports.ps1`
- **通过标准**：`CineRTX.exe` 与 `rtx_tests.exe` 静态导入表中 `nvinfer`、`nvonnxparser`、`cudart` 依赖计数均为 **0**

### 门禁 4：许可证、模型防污染与脱敏

- **验证命令**：`pwsh tools/check_spdx.ps1`
- **通过标准**：
  - 新增文件带有效 SPDX 头
  - 单个源文件 $\le 1{,}200$ 行
  - 无 `.onnx` / `.engine` 入库
  - 无私有 IP 或本机绝对路径

### 门禁 5：引擎离线构建功能

- **测试命令**：
  ```bash
  CineRTX.exe --build-rife-engine --onnx models/rife_v4.25.onnx \
              --engine models/rife_v4.25_trt101303_sm120_fp16.engine
  ```
- **通过标准**：
  - 生成的 `.engine` 文件可被门禁 2 直接加载
  - 构建日志包含 TRT 版本 `101303`、SM 架构、FP16 + FP32 混合精度算子隔离统计
  - 构建时间 $< 60\text{s}$（1080p Profile）

---

## 八、 风险项与降级策略

| 风险 | 触发条件 | 降级策略 |
| :--- | :--- | :--- |
| TensorRT DLL 不存在 | 用户未安装 TensorRT | `init_rife()` 返回 `RTX_ERR_DLL_MISSING`，自动退回路线 A |
| `.engine` 文件 SM 架构不匹配 | 跨 GPU 使用预编译引擎 | 反序列化失败，提示用户 `--build-rife-engine` 重新编译 |
| 推理显存超限 | 4K 输入 + 低显存 GPU | `init_rife()` 在 `setMemoryPoolLimit` 后检测 OOM，退回路线 A |
| TRT 版本防劣化 | 用户误装 TRT 10.14+ | `getInferLibVersion()` 校验拒绝构建，日志记录警告 |
| RIFE 运行时推理失败 | CUDA 错误/设备丢失 | `interpolate_rife()` 返回错误，主循环降级为帧复制 |

> [!IMPORTANT]
> **核心原则**：RIFE 失败**绝不**导致转码终止。最坏情况退化为帧复制（视觉上等同无插帧但保持目标帧率），与路线 A 的降级策略完全对称。

---

## 九、 与现有模块的交互矩阵

| 现有模块 | 交互方式 | 改动程度 |
| :--- | :--- | :--- |
| [`hw_transcode.h`](../../include/rtx/hw_transcode.h) | 现有 `fruc_enabled` / `target_fps` 字段直接复用，无需新增 | **零** |
| [`hw_transcode.cpp`](../../src/core/hw_transcode.cpp) | 在现有滑动窗口调用 `interpolate_nv12` 处增加路线 B 分支 | **微量** |
| [`flicks.h`](../../include/rtx/flicks.h) | 无改动，现有 Flicks 时间基直接复用 | **零** |
| [`timestamp_guard.h`](../../include/rtx/timestamp_guard.h) | 无改动，PTS 单调性卫兵天然兼容 | **零** |
| [`ngx_bridge_abi.h`](../../include/shim/ngx_bridge_abi.h) | Append-only 追加 RIFE 函数声明 | **微量** |
| [`bridge_exports.cpp`](../../src/shim/ngx_bridge/bridge_exports.cpp) | 追加 RIFE 导出函数实现 | **中等** |
| [`p010_pack_runtime.cpp`](../../src/shim/ngx_bridge/p010_pack_runtime.cpp) | 无改动，RIFE 使用独立的 `rife_pack_runtime.cpp` | **零** |
| [`d3d12_interop.cpp`](../../src/shim/ngx_bridge/d3d12_interop.cpp) | 复用已有 Fence/Semaphore 互操作 | **零** |
| [`CMakeLists.txt`](../../CMakeLists.txt) | 新增 PTX 嵌入命令和 TRT include 路径 | **中等** |
| `tools/audit_imports.ps1` | 无改动，自动覆盖新 DLL | **零** |

> [!TIP]
> **零改动区域最大化**：Flicks 时间基、时间戳卫兵、VRAM 背压、NGX 重制链路、D3D12 互操作——这些模块**无需任何改动**即可自然承载 RIFE 插值帧。施工切入点仅在 `rtx_ngx_bridge` 内部新增文件 + ABI append-only 扩展 + 主循环微量分支。
