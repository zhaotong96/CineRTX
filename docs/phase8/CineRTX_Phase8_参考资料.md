# CineRTX Phase 8 双路补帧（FRUC）与全仓工程演进外部技术情报与实施参考手册

> **文档定位**：本手册基于仓库 `[[REDACTED_REPO]/CineRTX]([REDACTED_REPO]/CineRTX)` 的《CineRTX Phase 8：双路补帧架构工程线索与实机环境证据报告》 与《关于 7 个核心问题全仓代码级取证与架构推演》，针对执行工程师在落地编码时面临的专有 SDK 接口细节、现代框架 API 变更、硬件微架构特性及 Windows 底层机制，汇总外部权威技术规范与工程避坑指南，供直接检索与编码对照。
> 
> 

---

## 一、 实机硬件与编译工具链基准校准（Blackwell 架构修正）

### 1. NVIDIA GeForce RTX 5060 真实计算能力与微架构特性

* **Compute Capability 修正**：现有工程线索报告中标注 RTX 5060 Laptop GPU 为 `Compute Capability 8.9+`，经 NVIDIA 官方 CUDA GPU Compute Capability 规范核实，**GeForce RTX 5060 / 5060 Ti 属于 Blackwell 架构，真实计算能力为 `Compute Capability 12.0`（`sm_120`）**（`sm_89` 为上一代 Ada Lovelace 架构）。


* **CUDA / PTX 编译与向前兼容红线**：
* 若项目中存在离线编译的 `.ptx`（如 `src/shim/ngx_bridge/p010_pack.ptx` 或新增的双向光流 Warp 核函数），`nvcc` 编译参数必须包含 `-gencode arch=compute_89,code=compute_89`（保留虚拟架构 PTX 以便驱动在加载时 JIT 编译至 `sm_120`）或使用 CUDA 12.8 / 13.0+ 工具链直接生成 `-gencode arch=compute_120,code=sm_120` 目标码。


* 若仅包含旧版 `sm_80`/`sm_86` 的 SASS 二进制而缺失 PTX 字节码，Blackwell 显卡将触发 `CUDA_ERROR_NO_BINARY_FOR_GPU` 或引发数十秒的首次启动 JIT 阻塞。


* **硬件单元并发拓扑**：Blackwell 芯片集成独立的光流加速器（OFA, Optical Flow Accelerator）、第 9 代 NVENC 与第 6 代 NVDEC。NVOFA 推理运行在独立 ASIC 单元上，与 CUDA SM（执行 PTX 像素重排/Warp）及 Tensor Core（执行 RTX VSR / TrueHDR）硬件级并行，互不抢占 SM 算力。



---

## 二、 路线 A（默认极速）：NVOFA 硬件光流引擎技术细则

路线 A 存在两种工程落地子路径，建议优先采用 **子路线 A1（零外部 DLL 增量）**，并以 **子路线 A2（`NvOFFRUC.dll`）** 作为行为对齐参照。

### 1. 子路线 A1：直调驱动原生 `nvofapi64.dll` + 自研 PTX 双向补偿核（推荐主选）

#### (1) 动态加载与函数表初始化（`nvOpticalFlowCommon.h` & `nvOpticalFlowCuda.h`）

Windows 现代 NVIDIA 驱动（572.70+）均在 `C:\Windows\System32\nvofapi64.dll` 内置完整 CUDA 光流接口。加载时应使用 `LOAD_LIBRARY_SEARCH_SYSTEM32` 防止 DLL 劫持：

```cpp
#include <windows.h>
#include <cuda.h>
#include "nvOpticalFlowCommon.h"
#include "nvOpticalFlowCuda.h"

using PFN_NvOFAPICreateInstanceCuda = NV_OF_STATUS(NVOFAPI*)(uint32_t apiVer, NV_OF_CUDA_API_FUNCTION_LIST* functionList);

HMODULE hNvOF = LoadLibraryExW(L"nvofapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
auto pfnCreate = reinterpret_cast<PFN_NvOFAPICreateInstanceCuda>(
    GetProcAddress(hNvOF, "NvOFAPICreateInstanceCuda"));

NV_OF_CUDA_API_FUNCTION_LIST ofFuncs{};
NV_OF_STATUS st = pfnCreate(NV_OF_API_VERSION, &ofFuncs);

```

#### (2) 单次双向光流估计配置（Forward + Backward）

自 NVIDIA Optical Flow SDK 3.0 起，NVOFA 支持在单次 `nvOFExecute` 调用中同时输出前向光流（$F_{t-1} \to F_t$）与后向光流（$F_t \to F_{t-1}$）：

```cpp
NvOFHandle hOfSession = nullptr;
ofFuncs.nvCreateOpticalFlowCuda(cuContext, &hOfSession);
ofFuncs.nvOFSetIOCudaStreams(hOfSession, cuInputStream, cuOutputStream);

NV_OF_INIT_PARAMS initParams{};
initParams.width              = alignedWidth;
initParams.height             = alignedHeight;
initParams.outGridSize        = NV_OF_OUTPUT_VECTOR_GRID_SIZE_4; // 4x4 块输出一个矢量（Turing/Ada/Blackwell 原生最高硬件粒度）
initParams.hintGridSize       = NV_OF_HINT_VECTOR_GRID_SIZE_UNDEFINED;
initParams.mode               = NV_OF_MODE_OPTICALFLOW;
initParams.perfLevel          = NV_OF_PERF_LEVEL_MEDIUM;           // FAST / MEDIUM / SLOW
initParams.enableExternalHints= NV_OF_FALSE;
initParams.enableOutputCost   = NV_OF_TRUE;                        // 必须开启，用于遮挡与场景硬切检测
initParams.hPrivData          = nullptr;
initParams.disparityRange     = NV_OF_STEREO_DISPARITY_RANGE_UNDEFINED;
initParams.enableRoi          = NV_OF_FALSE;
initParams.predDirection      = NV_OF_PRED_DIRECTION_BOTH;         // 单次调用同步计算双向光流
initParams.enableGlobalFlow   = NV_OF_FALSE;
initParams.inputBufferFormat  = NV_OF_BUFFER_FORMAT_NV12;          // 直接复用 NVDEC 解出的 NV12 显存帧，零格式转换

ofFuncs.nvOFInit(hOfSession, &initParams);

```

#### (3) S10.5 定点数矢量格式解码与 CUDA 双向 Warp 核函数公式（核心避坑点）

* **硬件输出缓冲布局**：`outGridSize = NV_OF_OUTPUT_VECTOR_GRID_SIZE_4` 时，输出缓冲区维度为 `gridW = ceil(width / 4.0)`、`gridH = ceil(height / 4.0)`，每个元素类型为 `NV_OF_FLOW_VECTOR`（结构体大小 4 字节：`int16_t flowx; int16_t flowy;`）。
* **S10.5 定点数精度还原**：NVOFA 硬件输出的光流位移采用 **S10.5 定点数格式**（1 位符号位 + 10 位整数位 + 5 位小数位，分辨率为 $\frac{1}{32}$ 像素）。在 CUDA/PTX 核函数中读取 `flowx` / `flowy` 时，**必须乘以 `0.03125f`（即 `/ 32.0f`）** 才能得到真实像素位移：

$$\vec{V}_{0 \to 1}(x, y) = \left( \frac{\text{flowx}_{\text{fwd}}}{32.0f},\ \frac{\text{flowy}_{\text{fwd}}}{32.0f} \right), \quad \vec{V}_{1 \to 0}(x, y) = \left( \frac{\text{flowx}_{\text{bwd}}}{32.0f},\ \frac{\text{flowy}_{\text{bwd}}}{32.0f} \right)$$


* **任意相位 $\alpha \in (0, 1)$ 双向运动补偿与遮挡融合算法**：
对于输出帧像素坐标 $\mathbf{p} = (x, y)$，映射至 `gridX = x >> 2`、`gridY = y >> 2`（建议对周围 2x2 网格做双线性采样消除 4x4 块效应）：
1. **逆向采样坐标**：

$$\mathbf{p}_0 = \mathbf{p} - \alpha \cdot \vec{V}_{0 \to 1}(\mathbf{p}), \quad \mathbf{p}_1 = \mathbf{p} - (1 - \alpha) \cdot \vec{V}_{1 \to 0}(\mathbf{p})$$


2. **双向一致性遮挡检测（Forward-Backward Consistency Check）**：

$$\epsilon_{\text{fb}} = \left\Vert{} \vec{V}_{0 \to 1}(\mathbf{p}) + \vec{V}_{1 \to 0}\left(\mathbf{p} + \vec{V}_{0 \to 1}(\mathbf{p})\right) \right\Vert{}_2$$



同时结合 NVOFA 输出的 `outputCostBuffer`（`uint8_t` 代价值，值越大置信度越低）与亮度残差 $\vert{}I_0(\mathbf{p}_0) - I_1(\mathbf{p}_1)\vert{}$ 计算遮挡权重 $w_{\text{occ}} \in [0, 1]$。
3. **场景切换（Scene Cut）硬切保护**：
统计整帧平均光流代价值 `mean_cost` 或矢量突变比例；当 `mean_cost > threshold`（发生镜头切换）时，将有效插值权重强制归零，直接按 $\alpha < 0.5$ 输出 $F_0$、$\alpha \ge 0.5$ 输出 $F_1$，防止两段不相关画面产生鬼影重影。



### 2. 子路线 A2：官方 `NvOFFRUC.dll` 封装调用参考（`Optical_Flow_SDK_5.0.7`）

若在验证阶段直接调用 `Optical_Flow_SDK_5.0.7/NvOFFRUC/NvOFFRUCSample/bin/win64/NvOFFRUC.dll`：

* **核心 C 导出符号**（仅 4 个）：
1. `NvOFFRUCCreate(const NvOFFRUC_CREATE_PARAM* createParams, NvOFFRUCHandle* hFRUC)`
2. `NvOFFRUCRegisterResource(NvOFFRUCHandle hFRUC, NvOFFRUC_REGISTER_RESOURCE_PARAM* registerParams)`
3. `NvOFFRUCProcess(NvOFFRUCHandle hFRUC, NvOFFRUC_PROCESS_IN_PARAMS* inParams, NvOFFRUC_PROCESS_OUT_PARAMS* outParams)`
4. `NvOFFRUCDestroy(NvOFFRUCHandle hFRUC)`


* **工程约束**：
* 官方 `NvOFFRUC.h` 头文件内部包含 C++ 结构体默认成员初始化语法，无法在纯 C99/C11 模式下直接编译。必须在 `src/shim/` 的 `.cpp` 实现文件内部 `#include "NvOFFRUC.h"`，严格通过纯 C 接口 `include/shim/fruc_bridge_abi.h` 向上暴露。


* `NvOFFRUC` 要求所有参与插帧的显存缓冲区在循环开始前预先通过 `NvOFFRUCRegisterResource` 注册（支持 CUDA Device Pointer 或 D3D11/D3D12 Texture），运行期仅传递已注册的资源索引/指针与归一化时间戳 `nFrameTimeStamp`。



---

## 三、 路线 B（母版画质可选）：TensorRT 10.13.3.9 + RIFE v4.25 接入指南

### 1. TensorRT 10.13 C++ API 强制迁移对照表

`TensorRT 10.13.3.9`（`nvinfer_10.dll` / `nvonnxparser_10.dll`） 彻底移除了 TensorRT 8.x 时代的索引绑定（Binding Index）API。执行工程师编写 `rife_engine.cpp` 时必须严格采用 TensorRT 10.x 张量名称（Tensor Name）接口：

| 废弃的 TensorRT 8.x 接口（编译报错） | TensorRT 10.13.3.9 必须使用的现代接口 | 工程说明 |
| --- | --- | --- |
| `config->setMaxWorkspaceSize(bytes)` | `config->setMemoryPoolLimit(nvinfer1::MemoryPoolType::kWORKSPACE, bytes)` | 建议为 RIFE v4.25 分配 `1ULL << 30`（1 GB）工作空间 |
| `engine->getNbBindings()` | `engine->getNbIOTensors()` | 遍历所有输入/输出张量数量 |
| `engine->getBindingName(i)` | `engine->getIOTensorName(i)` | 获取张量名称（如 `"img0"`, `"img1"`, `"timestep"`, `"output"`） |
| `engine->getBindingDimensions(i)` | `engine->getTensorShape(tensorName)` | 获取动态/静态维度 `nvinfer1::Dims` |
| `context->setBindingDimensions(i, dims)` | `context->setInputShape(tensorName, dims)` | 动态分辨率输入（1080p / 4K）推理前必须设置 |
| `context->executeV2(bindings)` / `enqueueV2` | `context->setTensorAddress(tensorName, devPtr)` + `context->enqueueV3(cuStream)` | 绑定显存地址并异步提交至指定 `CUstream` |

### 2. Practical-RIFE v4.25 ONNX 模型输入输出规范与显存防爆红线

* **标准 ONNX 张量签名**（对应 `hzwer/Practical-RIFE` v4.25 导出规范）：
* **输入 1 (`img0`)**：`float32 [1, 3, H_pad, W_pad]`，RGB 平面格式（CHW），像素值归一化至 `[0.0f, 1.0f]`。
* **输入 2 (`img1`)**：`float32 [1, 3, H_pad, W_pad]`，RGB 平面格式（CHW），像素值归一化至 `[0.0f, 1.0f]`。
* **输入 3 (`timestep`)**：`float32 [1]`（部分模型为 `[1, 1, 1, 1]`，代码需通过 `getTensorShape("timestep")` 动态适配），取值 $\alpha \in (0.0f, 1.0f)$。
* **可选输入 4 (`scale`)**：部分导出版本包含 `float32 [1]` 缩放因子（1080p 传 `1.0f`；4K 超高清源建议传 `0.5f`，可在保持画质的前提下将显存峰值降低 60% 并提速 2 倍）。
* **输出 (`out` / `output`)**：`float32 [1, 3, H_pad, W_pad]`，RGB 平面格式（CHW）。


* **空间尺寸对齐 Padding（硬性约束）**：
* RIFE v4.25 的 IFNet 特征金字塔包含 4~5 级下采样（`scale_list = [8, 4, 2, 1]` 或 `[16, 8, 4, 2, 1]`，当 `scale = 0.5` 时内部还会再除以 2）。
* **输入宽高必须向上 Padding 至 `64`（若支持 `scale = 0.5` 则需对齐至 `128`）的整数倍**。例如 `1920x1080` 必须边缘复制（Replicate Pad）为 `1920x1088`（对齐 64）或 `1920x1152`（对齐 128），推理完成后在 CUDA 核函数中直接裁剪回 `1920x1080`。


* **FP16 混合精度溢出防护**：
* 启用 `BuilderFlag::kFP16` 构建引擎时，RIFE 内部的网格采样（`GridSample`）与光流坐标偏移累加层在 4K 分辨率下（坐标值高达 `3840`）容易因 FP16 仅 10 位尾数产生亚像素量化阶梯伪影。在遍历 `INetworkDefinition` 层时，应对 `LayerType::kGRID_SAMPLE` 及名称含 `flow`/`grid` 的层调用 `layer->setPrecision(DataType::kFLOAT)` 强制锁定 FP32。


* **引擎计划文件（`.engine`）缓存键策略**：
* 为避免每次转码重新解析 ONNX 耗时 20~40 秒，在 `--rife-model` 同级目录或本地缓存目录生成序列化引擎文件，命名格式包含：`<model_stem>_sm<major><minor>_trt1013_<w>x<h>.engine`（配合 `IHostMemory* plan = builder->buildSerializedNetwork(...)`）。



---

## 四、 Flicks (705.6 MHz) 任意倍率补帧时钟状态机数学模型

当前主流水线中 `--fps 60` 仅修改时间基导致 2.5 倍速快进的根本原因，是缺少基于时间轴步进的 `1:N`（含非整数倍 `2:5`）调度器。

### 1. 常见帧率在 Flicks（$705,600,000\text{ Hz}$）下的精确整数周期

$705,600,000 = 2^7 \times 3^2 \times 5^5 \times 7^2 \times 4$，可对所有电影、电视 NTSC（`1000/1001`）及高频显示帧率实现**零舍入误差的 `int64_t` 精确整除**：

| 帧率类型 | 数学分数（`num / den`） | 单帧时长（Flicks 精确整数） | 典型应用场景 |
| --- | --- | --- | --- |
| **23.976 fps (NTSC 电影)** | `24000 / 1001` | `29,429,400` | 美剧、蓝光原盘、动画源 |
| **24 fps (标准电影)** | `24 / 1` | `29,400,000` | 院线母版、CGI 渲染 |
| **25 fps (PAL 制式)** | `25 / 1` | `28,224,000` | 欧洲电视、纪录片 |
| **29.97 fps (NTSC 视频)** | `30000 / 1001` | `23,543,520` | 广播电视、录像带采集 |
| **30 fps (标准视频)** | `30 / 1` | `23,520,000` | 游戏录屏、网络视频 |
| **48 fps (HFR 高帧率)** | `48 / 1` | `14,700,000` | 24fps 整数 2 倍插帧 |
| **59.94 fps (NTSC 高帧率)** | `60000 / 1001` | `11,771,760` | 23.976fps $\to$ 59.94fps（2.5x） |
| **60 fps (标准高帧率)** | `60 / 1` | `11,760,000` | 24fps $\to$ 60fps（2.5x）或 30 $\to$ 60（2x） |
| **120 fps (极致高帧率)** | `120 / 1` | `5,880,000` | 24fps $\to$ 120fps（5x）或 60 $\to$ 120（2x） |

### 2. 任意倍率 `1:N` / `M:N` 滑动窗口调度状态机

针对 `24fps -> 60fps`（输出相位序列为 $\alpha \in \{0.0, 0.4, 0.8\}$ 跨度至下一对帧 $\{0.2, 0.6\}$），执行工程师应在 `src/core/hw_transcode.cpp` 中实现**输出时钟驱动的滑动窗口状态机**：

```cpp
// 设输入源帧步长 delta_in，目标输出帧步长 delta_out
// 维护两帧显存窗口：prev_frame (pts = t0), curr_frame (pts = t1)
// 维护当前待生成输出帧的目标时间戳：next_out_pts（首帧初始化为 first_frame_pts）

while (next_out_pts <= t1) {
    if (next_out_pts == t0) {
        // 相位 alpha == 0.0：直接零拷贝复用原始帧 prev_frame，不调用补帧算子，无损保留原画细节
        remasterAndEncode(prev_frame, next_out_pts);
    } else if (next_out_pts == t1) {
        // 相位 alpha == 1.0：直接复用 curr_frame（注意避免与下一轮 t0 重复编码）
        remasterAndEncode(curr_frame, next_out_pts);
    } else {
        // 计算精确归一化插值相位 alpha in (0.0, 1.0)
        double alpha = static_cast<double>(next_out_pts - t0) / static_cast<double>(t1 - t0);
        frucBridge->interpolate(prev_frame, curr_frame, interp_frame, static_cast<float>(alpha));
        remasterAndEncode(interp_frame, next_out_pts);
    }
    next_out_pts += delta_out; // 精确整型累加，永不累积浮点漂移
}

```

---

## 五、 全仓代码级取证 7 大问题的外部技术支撑参考

对应《关于 7 个核心问题全仓代码级取证与架构推演》，以下为执行工程师在落地各项整改时的关键系统 API 与工程规避细节：

### 1. 日志系统与 Sysinternals DebugView v5.02 / `DbgViewCLI` 联调规范

* **Win32 `OutputDebugStringW` 底层 `DBWIN_BUFFER` 4KB 硬截断限制**：
* Windows 内核通过名为 `DBWIN_BUFFER` 的 4096 字节共享内存段传输调试输出（前 4 字节为 `DWORD ProcessId`，剩余 4092 字节为 ANSI 字符串）。单次 `OutputDebugStringW` 超过 ~4090 字节会被静默截断，且在无调试器监听时会触发宽字符转多字节开销。
* **高性能日志封装准则**：
1. 初始化阶段一次性读取环境变量 `RTX_TRACE=1` 或 CLI `--verbose` 标志并存入 `std::atomic<bool> g_trace_enabled`（替换当前 `nv_trace.h`、`bridge_exports.cpp`、`d3d12_interop.cpp` 中写死的 `return false;`）。


2. 统一日志前缀规范：`[CineRTX][Core]`、`[CineRTX][Shim]`、`[CineRTX][NGX]`、`[CineRTX][FRUC]`、`[CineRTX][GUI]`，单行长度控制在 1024 字符以内。




* **Sysinternals DebugView v5.02（`DbgViewCLI`）自动化联调工作流**：
* 在目标测试机启动 `DbgView.exe /a`（Agent 模式，监听 TCP 端口 `2020–2030`，需放行 Windows 防火墙对应端口）。


* 开发机或自动化测试脚本可使用 Sysinternals DebugView v5.02 附带的零依赖单文件命令行工具 **`dbgviewcli.exe`** 直接拉取过滤后的实时日志：
```powershell
dbgviewcli.exe --connect [REDACTED_IP] --process-filter CineRTX.exe --format csv --no-banner --tail --duration 30

```





### 2. HDR10 / HLG 源视频自动探测与 TrueHDR 自动旁路

为贯彻零开关不变量（移除 `--sdr` 与 `--truehdr`），解封装探测层（`src/io/ffmpeg_io.cpp`[cite: 1, 3]）应根据 FFmpeg `AVCodecParameters` / `AVFrame` 色彩特性自动决策：

* 若 `color_trc == AVCOL_TRC_SMPTE2084`（PQ / HDR10）或 `color_trc == AVCOL_TRC_ARIB_STD_B67`（HLG）：标记 `is_hdr_source = true`，管线自动旁路 TrueHDR 逆色调映射（Inverse Tone Mapping），仅执行 RTX VSR 超分与 P010 编码，并透传原片 `AVMasteringDisplayMetadata` 与 `AVContentLightMetadata`（MaxCLL / MaxFALL）。
* 若为 SDR 源（`AVCOL_TRC_BT709` 等）：无条件执行 RTX VSR + TrueHDR 10-bit P010 重制。



### 3. 双轨 UI 流水线：Daemon 进制安全 IPC 与纯 Win32 原生暗黑模式

* **Windows `--daemon-stdio` 管道二进制模式（防帧头破坏）**：
* 若使用 JSON-RPC 2.0 `Content-Length: <bytes>\r\n\r\n` 帧格式通过标准输入输出与 WebUI 宿主通信，Windows CRT 默认的文本模式（`_O_TEXT`）会自动将 `\n` 转换为 `\r\n`，导致接收端按 `Content-Length` 读取时发生字节偏移死锁。必须在 `main` 入口最前端执行：


```cpp
#include <io.h>
#include <fcntl.h>
_setmode(_fileno(stdin), _O_BINARY);
_setmode(_fileno(stdout), _O_BINARY);

```


* 若改用 Windows 命名管道（`\\.\pipe\CineRTX_Daemon`），直接调用 `kernel32.dll` 中的 `CreateNamedPipeW` / `ConnectNamedPipe`，即可彻底移除 `WS2_32.dll` 导入依赖。




* **纯 Win32 原生暗黑模式完整实现链（`uxtheme.dll` & `dwmapi.dll`）**：
* **进程级暗色菜单与滚动条**（必须在创建任何 `HWND` 之前调用）：动态加载 `uxtheme.dll` 未公开导出序号：
* Ordinal `135`：`SetPreferredAppMode(PreferredAppMode::AllowDark /* 1 */)`
* Ordinal `133`：`AllowDarkModeForWindow(hwnd, true)`
* Ordinal `136`：`FlushMenuThemes()`


* **沉浸式暗色标题栏**：调用 `DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &val, sizeof(val))`。
* **原生通用控件暗色主题**：
* `ListView` / `TreeView`：`SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr);`
* `ComboBox`：除了对主控件调用 `SetWindowTheme(hwndCombo, L"DarkMode_CFD", nullptr)`，还必须调用 `GetComboBoxInfo(hwndCombo, &cbi)` 获取下拉弹出列表句柄 `cbi.hwndList`，并对其执行 `SetWindowTheme(cbi.hwndList, L"DarkMode_Explorer", nullptr)`，否则下拉列表仍为亮白色。
* `Edit` / `Static` / `GroupBox`：拦截父窗口 `WM_CTLCOLOREDIT`、`WM_CTLCOLORSTATIC`、`WM_CTLCOLORBTN`，设置深色背景画刷（如 `RGB(32, 32, 32)`）与浅色文字（`RGB(240, 240, 240)`）。





### 4. 纯 GPU 播放器与 NLE 非线编核心技术栈（Phase 9+ 架构储备）

* **毫秒级稀疏关键帧索引构建（FFmpeg 7/8 API）**：
* 使用 `avformat_index_get_entries_count(AVStream* st)` 与 `avformat_index_get_entry(AVStream* st, int idx)` 遍历容器内置索引表，筛选 `entry->flags & AVINDEX_KEYFRAME` 的条目，将其 `entry->timestamp` 换算为 `int64_t Flicks` 并映射到 `entry->pos`（文件字节偏移），实现无扫包 $O(\log K)$ 随机寻址。




* **WASAPI 硬件音频主时钟（Master Audio Clock）**：
* 初始化 `IAudioClient`（共享模式事件驱动 `AUDCLNT_STREAMFLAGS_EVENTCALLBACK`），通过 `IAudioClock::GetPosition(&devicePos, nullptr)` 与 `IAudioClock::GetFrequency(&freq)` 换算当前声卡物理 DAC 已消耗的精确时间，转换为 `Flicks` 作为全局主时钟。




* **低延迟 DXGI 垂直同步呈现**：
* 创建交换链时指定 `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT`，通过 `IDXGISwapChain3::SetMaximumFrameLatency(1)` 将队列延迟锁死为 1 帧，渲染线程在 `WaitForSingleObjectEx(waitableHandle, 100, TRUE)` 返回后立即提交当帧 P010/SDR 视口，端到端显示延迟 `< 8ms`。




* **Smart Render 闭合 GOP 无损流拷贝**：
* 对未挂载 VSR/TrueHDR/转场特效的完整闭合 GOP 区间，直接复用原始 `AVPacket` 写入输出容器；若源容器为 MP4/MKV（AVCC/HVCC 长度前缀格式）而目标流需要 Annex-B 起始码，需在拼接边界挂载 `av_bsf_get_by_name("h264_mp4toannexb")` 或 `"hevc_mp4toannexb"` 比特流过滤器。





### 5. PE 导入表极限瘦身指南（16 DLL $\to$ 4~8 纯系统原生 DLL）

* **全仓静态 CRT（`/MT`）统一与链接冲突排查**：
* 在顶层 `CMakeLists.txt` 设置 `set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")` 后，必须检查 `tools/build_ffmpeg_static_msvc.sh` 中 FFmpeg 及其静态依赖库（zlib/libopus/dav1d 等）的编译标志，确保全部使用 `-MT`（释放版）或 `-MTd`（调试版）。


* 若存在残留 `/MD` 编译的第三方 `.lib`，链接器会触发 `LNK4098: defaultlib 'MSVCRT' conflicts with use of other libs`；严禁使用 `/NODEFAULTLIB:LIBCMT` 强行掩盖，而应重新用 `/MT` 编译该静态库，从而一次性从导入表中抹除 `MSVCP140.dll`、`VCRUNTIME140.dll`、`VCRUNTIME140_1.dll`、`ucrtbase.dll` 及所有 `api-ms-win-crt-*` 转发器。




* **安全动态加载（Anti-DLL Hijacking）统一封装**：
* 所有按需加载的系统图形与 UI 库（`d3d12.dll`、`dxgi.dll`、`dwmapi.dll`、`uxtheme.dll`、`comdlg32.dll`、`nvofapi64.dll`）均应通过 `LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)` 加载。


* 外部目录按需加载的可选推理插件（`nvinfer_10.dll`、`nvonnxparser_10.dll`）应通过 `LoadLibraryExW(fullPath, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)` 加载，确保 TensorRT 内部依赖的 `nvinfer_builder_resource_10.dll` 与 `cudnn` 组件能从同一目录准确解析，且不污染进程全局搜索路径。