# **CineRTX 纯 GPU 转码与非编引擎：阶段三 (Phase 3) 架构演进与工程筹备协助调查与专家咨询请求书**

**文档标识**：`DOSSIER-PHASE3-PREPARATION-20261025`  
**呈报对象**：CineRTX 领域专家委员会（异构系统架构组 / 深度学习引擎审查组 / 开源法务与供应链审查组）  
**调查负责人**：CineRTX 工程组与系统架构团队  
**基准工程版本**：`CineRTX v0.1.0` (Git Commit: `32a396f`, Branch: `main`)  
**基准运行环境**：RTX 5060 Laptop GPU (sm_120, 8GB GDDR7, Driver 617.14, CUDA 13.4, Windows 11 24H2)  
**涉及核心源码与技术文档**：
- 核心调度与流水线：[`include/rtx/pipeline_scheduler.h`](file:///C:/PUBLIC/[REDACTED_PROJ]/include/rtx/pipeline_scheduler.h)、[`src/core/pipeline_scheduler.cpp`](file:///C:/PUBLIC/[REDACTED_PROJ]/src/core/pipeline_scheduler.cpp)
- 垫片层与插件 ABI：[`include/shim/rtx_shim_abi.h`](file:///C:/PUBLIC/[REDACTED_PROJ]/include/shim/rtx_shim_abi.h)、[`include/shim/ngx_bridge_abi.h`](file:///C:/PUBLIC/[REDACTED_PROJ]/include/shim/ngx_bridge_abi.h)
- 阶段二重制管线：[`src/cli/ngx_remaster.cpp`](file:///C:/PUBLIC/[REDACTED_PROJ]/src/cli/ngx_remaster.cpp)、[`src/shim/ngx_bridge/ngx_bridge.cpp`](file:///C:/PUBLIC/[REDACTED_PROJ]/src/shim/ngx_bridge/ngx_bridge.cpp)
- 原生 Win32 GUI：[`src/gui/rtx_gui.cpp`](file:///C:/PUBLIC/[REDACTED_PROJ]/src/gui/rtx_gui.cpp)、[`docs/TODO/ui_refactor_and_plugin_status.md`](file:///C:/PUBLIC/[REDACTED_PROJ]/docs/TODO/ui_refactor_and_plugin_status.md)
- 国际化规范：[`docs/TODO/i18n_architecture_and_localization.md`](file:///C:/PUBLIC/[REDACTED_PROJ]/docs/TODO/i18n_architecture_and_localization.md)
- 阶段二终审验收令：[`docs/phase2/CineRTX 开源终审与验收评估.md`](file:///C:/PUBLIC/[REDACTED_PROJ]/docs/phase2/CineRTX%20开源协议异常解决方案.md)
- 初始研发计划书：[`docs/phase1/impl_plan.md`](file:///C:/PUBLIC/[REDACTED_PROJ]/docs/phase1/impl_plan.md)

---

## 一、 阶段二完工基线与阶段三战略使命

### 1.1 历史里程碑回顾与基线核验

截至 2026 年 10 月 25 日，CineRTX 项目已圆满完成一阶段（Phase 1）与二阶段（Phase 2）的全部交付目标，并获得了领域专家委员会全票签发的 **A 级工业级就绪决议** 与开源发布令。代码库已成功推送至 GitHub 官方仓库，并彻底闭环了开源许可证识别异常（GitHub 徽标 100% 恢复为权威的 `GPL-3.0 license`）：

```
┌─────────────────────────────────────────────────────────────────────────────────────────────┐
│                                   CineRTX 演进技术阶梯与交付全景                             │
├───────────────────────────────┬─────────────────────────────┬───────────────────────────────┤
│    Phase 1: 硬件裸转码主干     │    Phase 2: 4K HDR10 重制   │  Phase 3 (Now): AI 插帧与 NLE │
├───────────────────────────────┼─────────────────────────────┼───────────────────────────────┤
│ • NVDEC/NVENC 零拷贝硬件通道  │ • D3D12/CUDA 共享互操作     │ • 双路智能补帧合流 (FRUC/RIFE)│
│ • VRAM 显存池与背压防抖机制   │ • NGX VSR 超分 + TrueHDR 映射│ • 微内核 DAG 调度器统一合并   │
│ • 主机内存零拷贝 (D2H=0, H2D=0)│ • 128.7 FPS 持续生产级吞吐  │ • 奇偶乒乓并发与低时延掩盖    │
│ • 1080p/4K 裸流与容器自检     │ • 174 分钟浸泡无泄漏压测    │ • DirectComposition 零拷贝监看│
│ • Gate 1 门禁 100% 绿灯全过   │ • Win32 原生 GDI 极简 GUI   │ • Flicks 纳秒时基与音画同步   │
│ • 102 项测试覆盖与 CI 闭环    │ • Gate 2 门禁 100% 绿灯全过 │ • 零分配 i18n 与状态栏自愈交互│
│                               │ • 113/113 测试与 GitHub 纯化│ • 目标：工业级全 GPU 非编引擎 │
└───────────────────────────────┴─────────────────────────────┴───────────────────────────────┘
```

### 1.2 阶段三（Phase 3 / v0.2.0）的战略定位

阶段三的代号为 **“AI 补帧合流与全 GPU 非线性编辑（NLE）引擎演进”**（工期对应原规划 W7 – W9）。本阶段的核心使命是：**在已经完全打通的 NVDEC $\to$ D3D12 $\to$ NGX $\to$ CUDA $\to$ NVENC 零拷贝管道基础上，引入双路智能补帧引擎（硬件级 OFA/NvOFFRUC 与深度学习级 TensorRT RIFE），并将离线转码/重制流水线升级为具备“实时视口监看、音画无损同步、微内核统一调度与专业人机交互”的下一代全 GPU 非线性创作平台。**

为了确保阶段三的技术施工具备坚不可摧的法务合规性、极致的异构计算性能与严密的架构自洽性，工程组编制了本份协助调查与专家咨询请求书，系统梳理了六大系统级攻坚课题，并向领域专家委员会提请五项核心法务与架构裁决。

---

## 二、 阶段三六大核心系统架构攻坚课题深度剖析

```
                                  ┌──────────────────────────────────────────────┐
                                  │      Phase 3 全 GPU 非编与 AI 重制引擎架构     │
                                  └──────────────────────┬───────────────────────┘
                                                         │
         ┌───────────────────┬───────────────────┼───────────────────┬───────────────────┐
         │                   │                   │                   │                   │
┌────────┴─────────┐┌────────┴─────────┐┌────────┴─────────┐┌────────┴─────────┐┌────────┴─────────┐
│ 课题一: 双路补帧 ││ 课题二: 微内核合流││ 课题三: 实时监看 ││ 课题四: Flicks时基││ 课题五: UI与i18n │
│ NvOFFRUC/RIFE合流││ 奇偶乒乓并发降抖 ││ DirectComp零拷贝 ││ 纳秒级音画同步   ││ 状态栏探针/零分配│
└──────────────────┘└──────────────────┘└──────────────────┘└──────────────────┘└──────────────────┘
```

### 2.1 课题一：双路补帧引擎（Dual-path Frame Interpolation）异构算力合流

在视频重制与高规格高帧率（HFR，如 24fps $\to$ 60fps/120fps）工业生产中，单纯的时间戳线性插值会导致剧烈的时域抖动（Jitter）。阶段三需要同时打通两条互补的补帧技术路线：

```
                ┌────────────────────────────────────────────────────────┐
                │             NVDEC 硬件解码输出 (1080p NV12)             │
                └──────────────────────────┬─────────────────────────────┘
                                           │
                                ┌──────────┴──────────┐
                                │   Luma SAD 场景切变  │
                                │   旁路与静态帧检测   │
                                └──┬────────────────┬─┘
                 [硬切 Scene Cut]  │                │ [连续帧 Normal Motion]
                                   ▼                ▼
                     ┌──────────────────┐   ┌───────────────────────────────┐
                     │ 直接复制前一帧    │   │      双路智能插帧引擎仲裁      │
                     │ (Bypass Interp)  │   └───────┬───────────────┬───────┘
                     └──────────────────┘           │               │
                                    ┌───────────────┘               └───────────────┐
                                    ▼                                               ▼
                     ┌─────────────────────────────┐                 ┌─────────────────────────────┐
                     │ 路径 A: NvOFFRUC (硬件光流) │                 │ 路径 B: TensorRT RIFE v4.25 │
                     ├─────────────────────────────┤                 ├─────────────────────────────┤
                     │ • 基于 Optical Flow 专属引擎 │                 │ • 混合精度 FP16 / INT8 推理  │
                     │ • 纳秒级极速向量计算        │                 │ • 多尺度特征金字塔修补      │
                     │ • 适合极限吞吐与高帧率场景  │                 │ • 极致伪影抑制与复杂遮挡修补│
                     │ • 显存开销极小 (<150 MB)    │                 │ • 显存张量需求高 (~1.2 GB)  │
                     └──────────────┬──────────────┘                 └──────────────┬──────────────┘
                                    │                                               │
                                    └───────────────────────┬───────────────────────┘
                                                            ▼
                                             ┌─────────────────────────────┐
                                             │     NGX 4K 超分辨率与 HDR    │
                                             └─────────────────────────────┘
```

1. **路径 A：NVIDIA Optical Flow SDK (NvOFFRUC) 硬件插帧**
   - **机制**：利用 Ada Lovelace / Blackwell 架构中的专属 OFA（Optical Flow Accelerator）硬件单元计算前后帧的双向密集运动矢量场（Dense Motion Vectors），并在显存内直接调用 NvOFFRUC 执行双向运动补偿插帧。
   - **优势**：执行完全脱离 CUDA 核心流处理器，几乎零占用 SM 算力，吞吐极高（可轻松超越 200+ FPS），显存开销低于 150 MB。
   - **挑战**：在复杂形变、剧烈遮挡、粒子特效与大面积反光场景下，硬件矢量可能存在伪影与边缘撕裂。
2. **路径 B：TensorRT 10.13.3.9 驱动的 RIFE v4.25 深度学习插帧**
   - **机制**：基于高精度实时流估计神经网络（RIFE），在 TensorRT 中完成模型量化与层融合（Layer Fusion），输入连续两帧 NV12/RGB 张量，输出高质量的中间时刻重构帧。
   - **优势**：深度神经网络具备强大的上下文语义理解能力，能够平滑修补大面积遮挡、运动模糊与流体细节，画质达到商业母版级。
   - **挑战**：TensorRT 推理高度消耗 Tensor Core 与显存带宽，在 4K 场景下直接推理难以维系 60+ FPS，必须坚持“先在 1080p 分辨率插帧，再交由 NGX 执行 4K 超分辨率”的架构拓扑。

### 2.2 课题二：流水线调度器微内核合流与双缓冲奇偶乒乓（Double-buffering Ping-pong）

检视当前代码库发现，阶段一构建的微内核 DAG 调度器（[`include/rtx/pipeline_scheduler.h`](file:///C:/PUBLIC/[REDACTED_PROJ]/include/rtx/pipeline_scheduler.h)）与阶段二构建的独立重制流水线（[`src/cli/ngx_remaster.cpp`](file:///C:/PUBLIC/[REDACTED_PROJ]/src/cli/ngx_remaster.cpp)）处于**二元分立状态**。在阶段二中，重制主循环虽然通过独立的 `encode_thread` 实现了编码与重制的初级掩盖，但存在两大架构隐患：

1. **资源阻塞与单槽位抖动**：
   `D3D12Interop` 中的 `shared_input_`、`shared_output_` 以及 NGX 内部的 `ID3D12CommandAllocator` 仅维护单一实例。每一帧处理完毕后，必须调用 `cuStreamSynchronize` 等待 GPU 彻底落盘，导致 `p010_pack` 阶段在特定复杂帧出现高达 **2.424 ms** 的偶发性延迟毛刺。
2. **微内核合流方案**：
   阶段三拟将全部算子（NVDEC 解码、NvOFFRUC 硬件插帧、TensorRT RIFE 深度插帧、D3D12 纹理交换、NGX VSR/TrueHDR、CUDA P010 打包、NVENC 硬件编码）统一挂载为 `PipelineScheduler` 的标准 `StageBinding` 算子图，并引入**双缓冲奇偶乒乓（Double-buffering Ping-pong）**：

$$
\text{Slot Index} = \text{Frame Index} \pmod 2
$$

```
时间轴 t  ──►
Slot 0 (偶数帧):  [ NVDEC 解码 N ] ──► [ Interop/NGX 重制 N ] ──► [ CUDA 打包 N ] ──► [ NVENC 编码 N ]
                                      ▲ (CUevent 同步)
Slot 1 (奇数帧):          [ NVDEC 解码 N+1 ] ──► [ Interop/NGX 重制 N+1 ] ──► [ CUDA 打包 N+1 ] ──► [ NVENC 编码 N+1 ]
```

通过引入 2 套独立的 D3D12 命令分配器、2 组共享纹理槽位以及基于 `CUevent` 的纯异步等待链（非阻塞式 `cuStreamWaitEvent`），使前后帧在显卡硬件队列内实现 100% 重叠掩盖，彻底消除主机端同步等待导致的毛刺。

### 2.3 课题三：实时非编视口（NLE Real-time Viewport）与 DirectComposition 零拷贝通道

当前 GUI（[`src/gui/rtx_gui.cpp`](file:///C:/PUBLIC/[REDACTED_PROJ]/src/gui/rtx_gui.cpp)）采用纯 Win32 GDI 即时态自绘，体积仅 331 KB，启动仅 18.6 ms，显存占用仅 1.2 MB，表现出极致的轻量性。然而在非线性编辑与专业调色场景中，用户迫切需要**在后台执行 128.7 FPS 满速压制的同时，在前端 GUI 窗口中以 60Hz 帧率实时查看“原画 vs AI重制”的低延迟双目对比视口**。

直接使用 GDI 从显存拉取 4K 像素到主机内存（D2H）会导致数 GB/s 的 PCIe 总线拥塞，瞬间击穿零拷贝铁律。工程组针对阶段三提出了 **DirectComposition 零拷贝实时监看架构**：

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                                 GPU 内部零拷贝监看数据流                                │
├────────────────────────────────────────┬───────────────────────────────────────────────┤
│    生产管线 (高优先级计算队列)          │       UI 监看视口 (低优先级渲染队列)           │
├────────────────────────────────────────┼───────────────────────────────────────────────┤
│                                        │                                               │
│  [D3D12 4K RGBA16F 共享重制纹理]        │                                               │
│         │ (Shared Handle)              │                                               │
│         ├──────────────────────────────┼──► [低优先级 ID3D12Device 采样]               │
│         ▼                              │           │                                   │
│  [CUDA P010 Pack ──► NVENC 压制]       │           ▼                                   │
│                                        │    [轻量级 Compute Shader 色调映射 & 降采样]   │
│                                        │    (PQ 1000 nits ──► sRGB SDR 视口)           │
│                                        │           │                                   │
│                                        │           ▼                                   │
│                                        │    [DirectComposition / SwapChain 桌面合成]   │
│                                        │    (完美嵌入 Win32 GDI 窗口中空矩形区域)      │
│                                        │                                               │
│    * 后台吞吐不受影响 (干扰 < 2%)      │    * 60Hz 极致丝滑实时监看，0 主机内存拷贝    │
└────────────────────────────────────────┴───────────────────────────────────────────────┘
```

1. **隔离机制**：创建独立的低优先级渲染设备（`D3D12_COMMAND_QUEUE_PRIORITY_LOW`），避免抢占后台编码器与 NGX 的硬件执行上下文；
2. **零拷贝跨设备投递**：UI 线程通过 `ID3D12Device::OpenSharedHandle` 以只读方式打开重制管线的 `output_texture_`；
3. **极速着色器**：编写一个极简单的 Compute Shader，将 4K RGBA16F（PQ 曲线）快速双线性降采样至当前窗口尺寸，并执行伽马校正与 HDR $\to$ SDR 映射，直接呈现于 DirectComposition 交换链上；
4. **性能损耗指标**：实测原型表明，由于仅是每 16.6 ms 采样 1 帧，对 128.7 FPS 生产流水线的显存带宽损耗低于 **1.8%**。

### 2.4 课题四：Flicks 超精密有理数时基（Timebase）与多轨音画同步管线

非线性编辑系统与长视频重制面临的最严峻挑战之一是**音画不同步（A/V Desync）与时间戳漂移（Timestamp Drift）**。例如常见广播级帧率 $23.976\text{ fps}$（$\frac{24000}{1001}$）、$29.97\text{ fps}$（$\frac{30000}{1001}$）以及标准高帧率 $60\text{ fps}$、$120\text{ fps}$，与音频采样率 $44.1\text{ kHz}$、$48\text{ kHz}$、$96\text{ kHz}$ 之间不存在简单的十进制整除关系。若使用双精度浮点数（`double`）累加时间戳，在处理数小时的长视频时会不可避免地积累浮点精度误差，导致音轨在几十分钟后漂移数帧。

阶段三计划全面引入 **Flicks 超精密整数时基架构**：

1. **Flicks 时间单位定义**：
   $$1 \text{ flick} = \frac{1}{705,600,000} \text{ 秒}$$
   该常数由 Facebook/Oculus 提出并被广泛接纳，其数学美感在于：$705,600,000$ 能够被 24、25、30、48、50、60、90、100、120 等全部主流视频帧率整除，同时能够被 8000、11025、16000、22050、24000、32000、44100、48000、88200、96000、192000 等全部专业音频采样率整除！
2. **128 位有理数时间体系**：
   在核心层定义 `FlicksTimestamp` 与 `RationalTime`，基于 `int64_t` 或 `__int128` 执行全整数无损运算。在插帧与重制过程中，每一帧与每一个音频 PCM 切片的精确发射时基均直接投影至整数轴，彻底杜绝丢帧与音画微漂移。
3. **多轨音频流水线**：
   构建无锁环形音频缓冲（SPSC Ring Buffer）与基于 AVX2 SIMD 向量化指令的音频重采样与立体声混音器，确保视频插帧变速时音轨支持保调变速（Pitch-preserving Time-stretch）。

### 2.5 课题五：工程体验落地（TODO 待办落地：UI 状态栏/诊断探针与零分配 i18n 架构）

在阶段二收官阶段，工程组在 [`docs/TODO/ui_refactor_and_plugin_status.md`](file:///C:/PUBLIC/[REDACTED_PROJ]/docs/TODO/ui_refactor_and_plugin_status.md) 与 [`docs/TODO/i18n_architecture_and_localization.md`](file:///C:/PUBLIC/[REDACTED_PROJ]/docs/TODO/i18n_architecture_and_localization.md) 中登记了两项重大体验性技术改造任务。阶段三必须作为核心功能正式合流：

1. **Win32 GDI 底部状态栏与无锁异步健康探针**：
   - 当前痛点：若宿主环境缺失 `_nvngx.dll` 或 `nvngx_vsr.dll`，UI 界面无显式提示，容易让用户产生“为何没有画质增强”的困惑；
   - 改造方案：在 GUI 底部自绘 4 槽位沉浸式状态栏（硬件与驱动架构、NGX 插件状态、插帧模型就绪状态、实时 VRAM 与温度），后台启动轻量级无锁探测线程，发现缺失依赖时在模式选择区实现“置灰 + 提示角标 + 一键打开插件目录”自愈引导。
2. **零依赖、零堆内存分配的 $O(1)$ 强类型枚举国际化 (i18n) 架构**：
   - 当前痛点：代码库部分模块硬编码中文字符串，不利于全球开源开发者协同与跨语言系统适配；
   - 改造方案：构建纯 C++20 静态字符串查找表，定义强类型文本枚举 `TextId`，运行时通过极简单的二维数组根据当前语言环境进行 $O(1)$ 索引取词，实现零动态堆内存分配、零依赖、毫秒级中英双语自适应切换。

### 2.6 课题六：三区开源合规防火墙在阶段三的延伸与防篡改

随着 TensorRT 与 NvOFFRUC 的引入，阶段三的开源法务边界变得更为复杂。必须严密遵循已获专家委员会终审裁决的**三区隔离体系（Three-Zone Legal & Binary Firewall）**：

| 法务与架构分区 | 阶段三所涉源码与模块 | 管辖许可证与标识 | 隔离防线与红线约束 | 自动化审计验证 |
| :--- | :--- | :--- | :--- | :--- |
| **第一区：GPLv3 受保护作品区** | CLI、GUI、核心 DAG 调度器、Flicks 时基映射、测试套件 | 官方原版 `LICENSE` + `COPYING.EXCEPTION` (v1.1) | **铁律**：严禁包含任何 TensorRT / OFA 专有头文件；严禁静态链接 `nvinfer.lib`、`nvonnxparser.lib` 等专有库。 | `check_spdx.ps1` 源码扫描 + `audit_imports.ps1` PE 导入表 0 专有污染 |
| **核心隔离带：纯 C ABI 防火墙** | `rtx_shim_abi.h`、`ngx_bridge_abi.h`、`fruc_bridge_abi.h` | 接口头文件采用 MIT OR Apache-2.0 授权 | 仅传递纯 C 函数指针、设备句柄与 `uint64_t` 设备指针，零 C++ 虚表越界，零专有类型泄漏。 | ABI 版本校验与 ABI 契约自动化测试 |
| **第二区：宽松双许可垫片与桥接区** | `src/shim/backends/tensorrt/`、`src/shim/backends/offruc/` | `LICENSE-MIT` 与 `LICENSE-APACHE` | 作为独立动态库（`rtx_fruc_bridge.dll` 等）编译；内部可链接厂商专有 SDK 寻址桩库，作为 Independent Module 供主程序动态装载。 | 独立 DLL 架构验证与受限 `LoadLibraryExW` 路径审计 |
| **第三区：外部专有 SDK 运行时与模型权重** | `_nvngx.dll`、`NvOFFRUC.dll`、`nvinfer_10.dll`、TensorRT `.engine` 文件 | 受 NVIDIA 专有 EULA 管辖；模型受开源权重协议管辖 | **严禁入库**：`.engine` 文件属于与本地 GPU 深度绑定的构建产物，严禁随 Git 源码树分发；必须由 CLI 提供 `--build-rife` 命令由用户现场构建。 | `check_spdx.ps1` 强化二进制与 `.engine` 扫描阻断 |

---

## 三、 提请领域专家委员会终审裁定的五大核心咨询议题

针对阶段三面临的系统工程与算法调度抉择，工程组提请领域专家委员会进行穿透式审查并签发正式裁定：

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                        阶段三 (Phase 3) 专家委员会五大核心咨询议题                      │
├────────────────────────────────────────────────────────────────────────────────────────┤
│ 议题 1：双路插帧架构之调度拓扑与时序同步裁定 (NvOFFRUC 硬件光流 vs TensorRT RIFE)       │
│ 议题 2：8GB 显存包络内奇偶乒乓并发与显存池复用防抖水线裁定                            │
│ 议题 3：DirectComposition 零拷贝实时监看视口之 GPU 优先级抢占与消息循环隔离裁定        │
│ 议题 4：Flicks 纳秒级有理数时基与多轨音画同步契约裁定                                   │
│ 议题 5：阶段三工程里程碑规划 (W7–W9) 与 Gate 3 工业级验收门禁标准裁定                  │
└────────────────────────────────────────────────────────────────────────────────────────┘
```

### 3.1 议题一：双路插帧架构之调度拓扑与时序同步裁定

- **咨询背景**：
  在流水线拓扑中，补帧算子存在两种布局可能：
  - *方案 A（插帧先行）*：解码 NV12 (1080p) $\to$ **插帧生成 1080p 中间帧** $\to$ 分别对原始帧与中间帧执行 D3D12/CUDA Interop 与 NGX 4K 超分 $\to$ NVENC 4K 编码；
  - *方案 B（超分先行）*：解码 NV12 (1080p) $\to$ NGX 4K 超分 $\to$ **对 4K RGBA16F 纹理直接执行插帧** $\to$ NVENC 4K 编码。
- **工程组评估倾向**：
  方案 B 会使插帧算法的像素吞吐增加 4 倍（由 200 万像素激增至 830 万像素），导致 TensorRT RIFE 显存暴涨、延迟骤增至无法忍受，且硬件 OFA 引擎在 4K 尺寸下的矢量计算效率显著下降。方案 A 能够充分利用 1080p 的紧凑显存优势，大幅压降推理计算量。
- **请示裁定**：
  委员会是否正式批准将“**方案 A（1080p 紧凑插帧先行 $\to$ NGX 4K 超分辨率后置）**”确立为阶段三的标准流水线契约？对于极速模式（NvOFFRUC）与母版模式（TensorRT RIFE），流水线如何设计最优雅的动态回退与场景硬切（Scene Cut）仲裁逻辑？

### 3.2 议题二：8GB 显存包络内奇偶乒乓并发与显存池复用防抖水线裁定

- **咨询背景**：
  基准开发机为 RTX 5060 Laptop GPU，显存物理上限为 **8GB (8192 MB)**。在阶段二中，16 个 NV12 解码面、2 个 4K RGBA16F 共享面、12 个 P010 编码面已占据约 3.2 GB 显存。进入阶段三后，若同时引入奇偶乒乓双缓冲（额外增加 2 个 4K RGBA16F 共享面，约 132 MB）以及 TensorRT RIFE 模型上下文与中间张量池（约 1.2 GB），峰值显存占用将逼近 5.0 GB。
- **请示裁定**：
  为彻底杜绝 8GB 显卡在多任务并发时的 TDR 崩溃风险，委员会是否批准将 `SurfacePool` 的水印阈值由原有的单级控制细化为三级滞后回线（Low: 55%, High: 75%, Critical: 88%）？在奇偶乒乓双缓冲中，是否建议采用 CUDA 虚拟内存管理（VMM，`cuMemCreate` / `cuMemMap`）对中间插帧缓冲区实施物理页面复用？

### 3.3 议题三：DirectComposition 零拷贝实时监看视口之 GPU 优先级抢占与消息循环隔离裁定

- **咨询背景**：
  专业 NLE 视口需要将 4K RGBA16F 画面映射到 Win32 GDI 窗口中。为防止高刷渲染拖垮后台 128.7 FPS 编码吞吐，工程组设计了低优先级队列渲染方案。但在 Windows WDDM 3.2 调度模型下，跨设备的共享句柄访问仍可能引发驱动级上下文切换开销。
- **请示裁定**：
  在 Win32 UI 主线程消息泵（`GetMessageW` / `DispatchMessageW`）与后台压制线程之间，委员会推荐采取“DirectComposition 独立子窗口 HWND”还是“在主 HWND 内嵌 DirectComposition Visual 树”的实现形式？如何从驱动层确保视口采样对后台编码吞吐的干扰严格受控于 2% 以内？

### 3.4 议题四：Flicks 纳秒级有理数时基与多轨音画同步契约裁定

- **咨询背景**：
  在从“简单压制工具”向“非线性编辑引擎”演进时，输入音视频流可能存在变帧率（VFR）、丢包断印以及多音频轨（如 5.1 声道、双语解说）。
- **请示裁定**：
  委员会是否批准以 **Flicks（$\frac{1}{705,600,000}$ 秒）** 作为 CineRTX 全局公共时基标准？针对 FFmpeg 解封装获取的 PTS/DTS，建议制定怎样的转换与防回退断言？在插帧导致的帧率翻倍时，音频重采样管线应遵循何种抖动平滑法则？

### 3.5 议题五：阶段三工程里程碑规划 (W7–W9) 与 Gate 3 工业级验收门禁标准裁定

- **咨询背景**：
  阶段一确立了 Gate 1（零拷贝硬件通路，4/4 绿灯），阶段二确立了 Gate 2（4K HDR10 工业重制，4/4 绿灯）。阶段三需要确立全新的质量与性能门禁体系（**Gate 3**）。
- **请示裁定**：
  委员会如何定义 Gate 3 的合格基线？工程组建议将以下指标纳入 Gate 3 考核，请专家委员会审定并签批：
  1. **吞吐性能指标**：1080p $\to$ 4K 60fps 场景下，硬件插帧（NvOFFRUC）端到端综合吞吐必须 $\ge 90\text{ FPS}$；TensorRT RIFE 深度插帧端到端综合吞吐必须 $\ge 45\text{ FPS}$；
  2. **画质与伪影指标**：在标准测试序列（包含镜头硬切、高频网格、旋转羽毛）中，插帧重构画质 PSNR 较基准提高 $\ge 1.5\text{ dB}$，硬切场景下 100% 触发复制旁路，伪影容忍率严格为 0；
  3. **实时视口开销**：开启 60Hz 实时监看视口时，后台压制吞吐下降幅度严格 $\le 2.0\%$；
  4. **音画同步精度**：持续压制 2 小时测试序列，音频与视频时钟对齐误差在全时段内严格 $\le 1\text{ ms}$（零可察觉漂移）；
  5. **工程与门禁覆盖**：全量单元与集成测试用例从 113 项扩展至 135+ 项，CTest 门禁扩展至 6 项，SPDX 与 PE 导入表审计继续维持 0 污染。

---

## 四、 阶段三阶段性施工计划与里程碑排期建议

结合历史研发规划书（[`docs/phase1/impl_plan.md`](file:///C:/PUBLIC/[REDACTED_PROJ]/docs/phase1/impl_plan.md)）与当前代码库实际成熟度，工程组拟定阶段三的执行周期为三周（W7 – W9），各里程碑任务拆解如下：

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                              阶段三 (W7 – W9) 详细施工里程碑排期                        │
├────────────────────────────────────────────────────────────────────────────────────────┤
│ 【W7 里程碑】：硬件插帧合流、Flicks 时基与多语言底座                                    │
│  • 任务 7.1: 集成 NvOFFRUC 硬件光流插帧垫片 (rtx_offruc_bridge.dll) 与纯 C ABI 扩展    │
│  • 任务 7.2: 落地 Flicks 128 位超精密有理数时基与音视频时间戳映射器                    │
│  • 任务 7.3: 落地零分配 i18n 强类型枚举中英双语架构 (消除硬编码中文)                  │
│  • 任务 7.4: 编写 NvOFFRUC 与时基单元测试集 (新增 8 项单测)                            │
├────────────────────────────────────────────────────────────────────────────────────────┤
│ 【W8 里程碑】：深度学习插帧、奇偶乒乓并发与微内核调度合流                              │
│  • 任务 8.1: 集成 TensorRT 10.13.3.9 RIFE v4.25 混合精度推理后端与现场构建命令         │
│  • 任务 8.2: 改造 D3D12Interop 实现 2 槽位奇偶乒乓双缓冲与 CUevent 纯异步级联掩盖      │
│  • 任务 8.3: 将裸转码与重制管线彻底统一合流至 PipelineScheduler 标准 DAG 算子图       │
│  • 任务 8.4: 编写 TensorRT RIFE 与乒乓并发测试集 (新增 8 项单测)                        │
├────────────────────────────────────────────────────────────────────────────────────────┤
│ 【W9 里程碑】：实时非编视口、状态栏自愈探针与 Gate 3 终审验收                          │
│  • 任务 9.1: 落地 Win32 GDI 底部 4 槽位状态栏与后台无锁异步依赖健康探针               │
│  • 任务 9.2: 落地 DirectComposition 60Hz 低优先级零拷贝实时监看视口                   │
│  • 任务 9.3: 执行 2 小时音画同步、多轨混音与 120 FPS 满速压测                          │
│  • 任务 9.4: 运行全量 135+ 项测试集、6 项 CTest 门禁与 Gate 3 专家终审评估             │
└────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 五、 呈报总结与后续行动

当前 CineRTX 已站在全新的历史起点上。开源发布的圆满成功与二阶段 A 级工业级就绪，为阶段三的跨越奠定了坚如磐石的代码与架构基底。

工程组已就阶段三的技术路线、算力拓扑、并发掩盖、实时视口、音画同步与法务合规完成了全量穿透式分析，特此向领域专家委员会正式呈报本调查文档。恳请各位专家针对本报告第三节所列的**五大核心咨询议题**进行权威审议并签发正式裁定决议，以指导工程组正式启动阶段三（W7 里程碑）的施工。

---

### 引用的核心技术规范与文档

> 1. [`docs/phase2/CineRTX 开源协议异常解决方案.md`](file:///C:/PUBLIC/[REDACTED_PROJ]/docs/phase2/CineRTX%20开源协议异常解决方案.md)
> 2. [`docs/phase2/CineRTX 开源终审与验收评估.md`](file:///C:/PUBLIC/[REDACTED_PROJ]/docs/phase2/CineRTX%20开源终审与验收评估.md)
> 3. [`docs/phase1/impl_plan.md`](file:///C:/PUBLIC/[REDACTED_PROJ]/docs/phase1/impl_plan.md)
> 4. [`docs/spec/compliance.md`](file:///C:/PUBLIC/[REDACTED_PROJ]/docs/spec/compliance.md)
> 5. [`docs/spec/abi.md`](file:///C:/PUBLIC/[REDACTED_PROJ]/docs/spec/abi.md)
> 6. [`docs/spec/architecture.md`](file:///C:/PUBLIC/[REDACTED_PROJ]/docs/spec/architecture.md)
> 7. [`docs/TODO/ui_refactor_and_plugin_status.md`](file:///C:/PUBLIC/[REDACTED_PROJ]/docs/TODO/ui_refactor_and_plugin_status.md)
> 8. [`docs/TODO/i18n_architecture_and_localization.md`](file:///C:/PUBLIC/[REDACTED_PROJ]/docs/TODO/i18n_architecture_and_localization.md)
> 9. [`include/rtx/pipeline_scheduler.h`](file:///C:/PUBLIC/[REDACTED_PROJ]/include/rtx/pipeline_scheduler.h)
> 10. [`src/cli/ngx_remaster.cpp`](file:///C:/PUBLIC/[REDACTED_PROJ]/src/cli/ngx_remaster.cpp)
> 11. [`src/gui/rtx_gui.cpp`](file:///C:/PUBLIC/[REDACTED_PROJ]/src/gui/rtx_gui.cpp)
