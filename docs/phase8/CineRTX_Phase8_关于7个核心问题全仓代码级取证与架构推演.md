结合最新同步的 [CineRTX\_Phase7\_验收实证报告.md] 与 2026-11-12 最新代码库快照，我对你提出的 7 个核心问题进行了全仓代码级取证与架构推演，以下是逐项详尽报告：

### **1\. 控制台日志与 Remote DebugView：到底实装了吗？**

**结论：仅实装了“半截（本地内核调试流 \+ 父控制台挂载）”，真正的底层硬件追踪被硬编码锁死为 false，且并未实装独立网络级的 Remote DebugView 服务。**  
具体代码级取证如下：

* **GUI 层本地 OutputDebugStringW 已实装（需外部工具配合才支持远程）**：在 [src/gui/rtx\_gui.cpp](../../src/gui/rtx\_gui.cpp)（第 114–121 行）中，实装了 guiLog(const wchar\_t\* fmt, ...)，内部通过 \_vsnwprintf\_s 格式化 \[CineRTX-GUI\] 前缀后调用 Win32 API OutputDebugStringW(buf)。它只向本机 Windows 内核 DBWIN\_BUFFER 写入调试流——要实现“Remote DebugView”，必须在目标机额外运行微软 Sysinternals 的 DbgView.exe /a（Agent 模式，监听 TCP 2020 端口），由开发机连过去看，程序自身并没有内置网络日志广播器。  
* **雙模 CLI/GUI 控制台挂载已实装**：在 [src/gui/rtx\_gui.cpp](../../src/gui/rtx\_gui.cpp)（第 792–809 行）的 attachParentConsole() 中，由于 [CMakeLists.txt](../../CMakeLists.txt) 将 CineRTX.exe 编译为 /SUBSYSTEM:WINDOWS，当带命令行参数启动时，它会调用 AttachConsole(ATTACH\_PARENT\_PROCESS) 并用 freopen\_s 将 CONOUT\$ 重定向到 stdout/stderr，配合 [src/core/runtime\_env.cpp](../../src/core/runtime\_env.cpp) 中的 SetConsoleOutputCP(CP\_UTF8) 输出控制台日志。但当你直接双击启动 GUI 时，没有父控制台，std::cout 和 std::cerr 会直接丢入黑洞。  
* **核心转码层与 Shim/Bridge 底层日志处于“断联/假死”状态**：  
  * **核心层不进 DebugView**：[src/core/hw\_transcode.cpp](../../src/core/hw\_transcode.cpp)（第 1278–1280 行）仅在 options.verbose 为 true 时向 std::cerr 打印 \[remaster\] 耗时，完全没有调用 OutputDebugStringW，导致 GUI 模式下转码核心的报错与帧耗时在 DebugView 里完全看不见。  
  * **Shim 与 NGX Bridge 的追踪开关被硬编码写死为 false**：在 [src/shim/loader/nv\_trace.h](../../src/shim/loader/nv\_trace.h)（第 16 行）、[src/shim/ngx\_bridge/bridge\_exports.cpp](../../src/shim/ngx\_bridge/bridge\_exports.cpp)（第 52 行）以及 [src/shim/ngx\_bridge/d3d12\_interop.cpp](../../src/shim/ngx\_bridge/d3d12\_interop.cpp)（第 30 行）中，虽然注释写着设置 RTX\_SHIM\_TRACE=1 或 RTX\_NGX\_TRACE=1 可开启追踪，但代码实际写成了 inline bool traceEnabled() { return false; }！这意味着底层 NVDEC/NVENC/NGX 的所有 RTX\_TRACE 宏在编译期就是死代码。

### **2\. \--sdr 开关与全仓隐藏开关/心智负担大扫描**

**结论：--sdr 不仅违背了 [docs/spec/architecture.md](../spec/architecture.md) 第 1.3 节的“奥卡姆剃刀与零开关不变量（Zero-Switch Invariant）”，而且在当前 [src/cli/main.cpp](../../src/cli/main.cpp) 中还造成了自相矛盾的逻辑陷阱。**  
在 [src/cli/main.cpp](../../src/cli/main.cpp) 第 131–132 行中，帮助文本写着 \--sdr: Force SDR native color space (default for SDR sources)（声称 SDR 源默认走 SDR），但第 70 行 Options::truehdr 默认值却是 true，第 424 行 hardware.hdr \= options.hdr\_set ? options.truehdr : true; 也是默认强开 HDR10！用户看到 \--truehdr 和 \--sdr 两个开关同时存在，且说明文字与实际默认行为相反，心智负担极大。  
我对全仓（[src/cli/main.cpp](../../src/cli/main.cpp)、[include/rtx/hw\_transcode.h](../../include/rtx/hw\_transcode.h)、[src/gui/rtx\_gui.cpp](../../src/gui/rtx\_gui.cpp)、[CMakeLists.txt](../../CMakeLists.txt)）进行了彻底扫描，清点出所有隐藏开关、死参数与增加心智负担的暗坑：

| 隐藏开关 / 暗坑位置 | 具体参数或代码状态 | 违背原则与心智负担分析 | 清洗裁决建议 |
| :---- | :---- | :---- | :---- |
| [src/cli/main.cpp](../../src/cli/main.cpp) (第 131–132, 307–312 行) | \--sdr 与 \--truehdr 双开关并存 | 违背 [docs/spec/architecture.md](../spec/architecture.md) 唯一确定性管线（SDR 输入 → RTX VSR \+ TrueHDR → 4K HDR10），且帮助文档与默认值自相矛盾 | **彻底拔除 \--sdr 和 \--truehdr**，管线无条件执行 TrueHDR 10-bit P010 重制（若输入本身已是 HDR10，由探测层自动旁路 TrueHDR 逆色调映射，无需人工开关） |
| [src/cli/main.cpp](../../src/cli/main.cpp) (第 68, 305 行) | \--4k-profile (options.four\_k\_profile) | **纯僵尸开关（Dead Flag）**：解析进了 Options 结构体，但在 makeHwOptions() 中根本没有读取和传递，用户传了毫无作用 | **立即删除** four\_k\_profile 字段及 \--4k-profile 分支 |
| [src/cli/main.cpp](../../src/cli/main.cpp) (第 69, 314 行) | \--no-cuda-graph (options.no\_cuda\_graph) | **纯僵尸开关（Dead Flag）**：解析进了 Options，但全仓无任何地方读取该布尔值，CUDA Graph 在桥接层也是空壳 | **立即删除** no\_cuda\_graph 字段及 \--no-cuda-graph 分支 |
| [src/cli/main.cpp](../../src/cli/main.cpp) (第 52–53, 108–109 行) | \--width 1920 / \--height 1080 | **误导性输入参数**：容器转码模式下输入宽高完全由 [src/io/ffmpeg\_io.cpp](../../src/io/ffmpeg\_io.cpp) 解封装自动探测，暴露给用户反而让人误以为需要手动填源视频分辨率 | **从 CLI 移除 \--width / \--height**，仅保留输出端 \--target-width / \--target-height（或统一为输出分辨率预设） |
| [src/cli/main.cpp](../../src/cli/main.cpp) (第 87–88, 286–298 行) | \--test-gop-split 与 \--fault-inject-frame | 将内部单元测试桩（模拟 GPU TDR 崩溃、强制 GOP 切分测试）直接暴露在生产 CLI 参数表中，污染用户界面 | **从生产 CLI 中剥离**，测试直接通过 C++ 测试二进制 rtx\_tests 调用 HwTranscodeOptions 内部结构体 |
| [src/cli/main.cpp](../../src/cli/main.cpp) vs [src/gui/rtx\_gui.cpp](../../src/gui/rtx\_gui.cpp) | \--vsr-quality 默认值为 2，而 GUI 下拉框默认选 4 (Ultra) | CLI 与 GUI 默认画质档位不一致，用户用 CLI 跑出的画质比 GUI 差两档 | **全栈统一默认值为 4（Ultra）** |
| [src/core/nvidia\_shim.cpp](../../src/core/nvidia\_shim.cpp) & [nv\_runtime.cpp] | 报错提示 set RTX\_SHIM\_PATH / set RTX\_NVIDIA\_DIR | 代码早已移除 std::getenv 读取，但报错文案仍诱导用户去配这两个已经失效的环境变量 | **清理所有失效环境变量提示文案** |

### **3\. 补帧功能（FRUC）默认没开启？24 帧进去出来还是 24 帧？**

**结论：是的，目前 24 帧进去出来依然是 24 帧，代码里没有任何真正的补帧（Frame Rate Up-Conversion）发生。**  
原因与源码真相如下：

* **当前代码行为**：在 [src/core/hw\_transcode.cpp](../../src/core/hw\_transcode.cpp)（第 421–435 行）的 chooseFrameRate() 中，当未指定 \--fps 时，直接沿用源视频的 info.frame\_rate（24 帧进 → 24 帧出）。即便你在 CLI 传了 \--fps 60，它也只是改写了编码器时间基与容器头部帧率标记，把原有的 24 帧画面按 60fps 时间戳硬塞进去（导致画面 2.5 倍速快进），**并不会生成任何中间插值帧**。  
* **历史渊源**：在 Phase 3 时期，仓库曾尝试过两条补帧路线——一条是 TensorRT \+ RIFE v4.25（需要携带 1.2 GB 的 DLL），另一条是 NvOFFRUC.dll（NVIDIA 光流补帧库）。但这两条路线当时都只写了外围加载器，从未真正接进 [src/core/hw\_transcode.cpp](../../src/core/hw\_transcode.cpp) 的主循环 runNvidiaRemasterTranscode 中；到了 Phase 6 Wave 1 大瘦身时，为了砍掉 1.2 GB 的冗余依赖，这两个未接线模块被一并拔除。  
* **零体积膨胀的正确补帧架构方案**：如果要实装真正的硬件补帧（例如 24fps → 48fps/60fps），**绝对不要把 ONNX/TensorRT 捡回来**，而是直接调用 **NVIDIA 显卡驱动自带的硬件光流引擎 nvofapi64.dll（NVIDIA Optical Flow API，Turing/Ampere/Ada/Blackwell 驱动原生内置在 System32 中，安装包增量 0 MB）**：  
  1. 在 rtx\_ngx\_bridge 中动态加载 nvofapi64.dll（NvOFAPICreateInstanceCuda），利用显卡上的独立硬件单元 **NVOFA（Optical Flow Accelerator）** 计算相邻两帧 ${F}_{t}$ 与 ${F}_{t+1}$ 之间的双向 4×4 光流运动矢量场（不占用 CUDA 核心算力）。  
  2. 在现有的 [src/shim/ngx\_bridge/p010\_pack\_runtime.cpp](../../src/shim/ngx\_bridge/p010\_pack\_runtime.cpp) PTX 核函数中增加一个 120 行左右的双向运动补偿插值核（Bidirectional Motion-Compensated Warp \+ Occlusion Mask），直接在显存内合成 ${F}_{t+\alpha }$ 中间帧并送入 NVENC。

### **4\. 组件库与双轨 UI 工程流水线（负责人定接口 → 工程师 1 出 WebUI → 工程师 2 对齐纯 Win32）**

这个决策非常老道！直接手写 Win32 GDI/Common Controls 去试错 UI 布局和交互，改一个间距或状态联动都要重新编译调坐标，效率极低；而先由项目负责人锁死后端接口契约，再由工程师 1 用 WebUI 快速建立**组件库规范与视觉交互母版（Single Source of Truth）**，最后让工程师 2 照着现成的后端契约和 WebUI 视觉母版做 1:1 纯 Win32 像素级还原，既保证了架构解耦，又彻底消除了两个前端各自发挥导致的返工。  
建议将该双轨研发机制固化为以下 4 个严格串行/并行交接阶段：

**1.阶段一：项目负责人定义并冻结后端核心契约：**前置阻塞门禁 · 产出 rtx\_engine\_api.h 与 JSON-RPC Schema。  
由项目负责人（Architect/Lead）独立完成后端状态机与接口定义，严禁任何 UI 概念渗透进核心层：

> 1. **统一配置与状态数据模型**：基于 [include/rtx/hw\_transcode.h](../../include/rtx/hw\_transcode.h) 与 [src/cli/daemon\_rpc.cpp](../../src/cli/daemon\_rpc.cpp)，冻结 5 类核心契约：GetCapabilities（硬件与编码器能力枚举）、ProbeMedia（源媒体元数据）、JobConfig（编码参数、VSR/峰值亮度、LUT、多卡调度参数）、JobQueueCommand（增删改查、启停、取消）、TelemetryEvent（帧率、进度、显存水位、Preview 帧共享句柄）。  
> 2. **双通道同构暴露**：同一套数据结构既通过 C ABI 函数表供进程内 Win32 直接调用，又通过 [src/cli/daemon\_rpc.cpp](../../src/cli/daemon\_rpc.cpp) 的 \--daemon-stdio / \--daemon-http 序列化为 JSON-RPC 供 WebUI 联调。

**2.阶段二：工程师 1 构建 WebUI 组件库与交互母版：**对接 Daemon 接口 · 产出可交互组件库与设计 Token 表。  
工程师 1 基于冻结的后端接口，开发轻量级 WebUI（连接 CineRTX \--daemon-http）：

> 1. **原子组件库（Component Library）抽离**：将所有控件收敛为 7 个标准原子组件：SelectBox（下拉框）、NumberInput（带步进与范围校验的输入框）、CheckBox（复选框）、PathPicker（路径选择器）、SplitViewport（Before/After 对比视口）、BatchQueueTable（批处理队列列表）、ProgressBar（底部状态与进度条）。  
> 2. **状态联动与参数边界固化**：在 WebUI 中把诸如“选择 Constant QP 时禁用 Bitrate 输入框并激活 QP 输入框”、“选择 H.264 时限制 10-bit 选项”等表单联动规则及配色字号 Token（背景色、边框色、行高、间距）全部跑通并固化为可视化规范。

**3.阶段三：项目负责人主持接口与视觉母版冻结评审：**双轨对齐门禁 · 零模糊度交接。  
项目负责人验收工程师 1 的 WebUI 成果：确认组件数量克制、无冗余开关、所有参数 100% 映射到后端契约字段，随后冻结 UI 布局网格尺寸、色彩 Token 与交互状态机，作为工程师 2 的唯一施工图纸。

**4.阶段四：工程师 2 开展纯 Win32 原生 1:1 映射实装：**零第三方 GUI 依赖 · 复用同一套后端 C++ 接口。  
工程师 2 在 [src/gui/rtx\_gui.cpp](../../src/gui/rtx\_gui.cpp) 中，将 WebUI 的 7 个原子组件 1:1 映射为 Win32 原生组件封装类（WC\_COMBOBOXW、WC\_EDITW、BS\_AUTOCHECKBOX、WC\_LISTVIEWW、msctls\_progress32 及 [src/gui/preview\_viewport.h](../../src/gui/preview\_viewport.h)），直接调用阶段一冻结的后端 C++ 接口，确保 WebUI 与纯 Win32 版本在参数能力、交互联动和视觉布局上分毫不差。

### **5\. 为什么曾经要有 ONNX？现在代码里还有残留吗？**

* **为什么曾经会有 ONNX**：  
  在项目的 Phase 3 阶段（详见历史遗留文档 [docs/TODO/rife\_cli\_test\_fixture.md](../TODO/rife\_cli\_test\_fixture.md)），当时的设计试图用深度学习模型 **RIFE v4.25** 做 AI 补帧。为了不在仓库里存放针对不同显卡架构预编译的数百兆 .engine 二进制文件，当时的设计是提供 .onnx 模型权重，在用户电脑上首次运行 CineRTX \--build-rife 时，动态加载 nvonnxparser\_10.dll（ONNX 解析器）和 nvinfer\_10.dll（TensorRT 10.13.3.9），现场把 ONNX 图编译成 TensorRT 引擎。这套方案不仅让运行库膨胀了 1.2 GB，而且违反了“免折腾、单文件即开即用”的产品原则，因此在 Phase 6 Wave 1 中被裁撤。  
* **现在代码里还有残留吗？—— 有！虽然 .onnx 解析代码已删，但全仓仍有 5 处 TensorRT / RIFE / NvOFFRUC 的残余代码与文档尚未拔干净**：  
  1. **每次运行 \--probe 仍在探测 TensorRT 和 NvOFFRUC**：在 [src/shim/loader/nv\_backend\_shim.cpp](../../src/shim/loader/nv\_backend\_shim.cpp)（第 84–85 行）中，rtx\_shim\_probe 依然在调用 state.runtime.loadNvOfruc(\&ignored) 和 state.runtime.loadTensorRt(\&ignored)！  
  2. **运行时加载器里仍有整段 RIFE/TensorRT 探测逻辑**：在 [src/shim/loader/nv\_runtime.cpp](../../src/shim/loader/nv\_runtime.cpp)（第 189–266 行）中，完整保留了 NvidiaRuntime::loadNvOfruc 和 NvidiaRuntime::loadTensorRt（还在找 nvinfer\_10.dll / nvinfer\_11.dll，并在找不到时输出 "TensorRT 10.13.3.9 (nvinfer\_10.dll) not found beside CineRTX.exe; RIFE disabled (set RTX\_NVIDIA\_DIR)"）。  
  3. **函数表仍有 TensorRT 符号解析**：[src/shim/loader/nv\_func\_tables.h](../../src/shim/loader/nv\_func\_tables.h) 与 [src/shim/loader/nv\_func\_tables.cpp](../../src/shim/loader/nv\_func\_tables.cpp) 中仍保留着 namespace tensorrt 的 getInferLibVersion 动态解析表。  
  4. **ABI 头文件与错误码残留**：[include/shim/rtx\_shim\_abi.h](../../include/shim/rtx\_shim\_abi.h)（第 43、67–68 行）中仍定义了 RTX\_ERR\_TENSORRT\_VERSION \= 11、RTX\_CAP\_NVOFA、RTX\_CAP\_TENSORRT；[src/core/hw\_transcode.cpp](../../src/core/hw\_transcode.cpp)（第 73、105 行）和 [include/rtx/status.h](../../include/rtx/status.h) 中也仍保留着 ErrorCode::TensorRtVersion。  
  5. **遗留文档残留**：[docs/TODO/rife\_cli\_test\_fixture.md](../TODO/rife\_cli\_test\_fixture.md) 仍在目录中。以上 5 处残留可在下一次清洗中彻底归零。

### **6\. 下一阶段：纯 GPU 播放器（Player）与非线性编辑（NLE）架构设计（纯架构探讨）**

当前 CineRTX 是一条**单向推流式离线转码管线（Push-based Linear Pipeline）**：解封装 → 顺序解码 → 单流 VSR/TrueHDR → 顺序编码落盘。  
要演进为专业级**纯 GPU 播放器 \+ 非线性编辑系统（NLE）**，核心范式必须从“单向顺序推流”升级为“以 705.6 MHz Flicks 时间轴驱动的拉取式显存帧图（Pull-based VRAM Frame Graph）”。播放器只是该引擎“单轨单片段”的退化特例，千万不要写两套内核！

SVG  
整个纯 GPU 播放器与非线编架构应当由以下 **5 层解耦子系统** 构成：

> 1. **精准时间轴与无损工程模型层（Timeline & OTIO Model Layer）**：  
   * **绝对时间基准**：直接复用 Phase 8 已经落地的 705,600,000 Hz（int64\_t Flicks）时间基，彻底杜绝 23.976 / 29.97 / 59.94 fps 与 48kHz 音频采样在跨数小时剪辑时的浮点数累积漂移。  
   * **数据结构（Timeline \-\> Track \-\> Clip / Transition / EffectStack）**：所有剪辑操作（Split、Trim、Slip、Slide、Cross-Dissolve）本质上只是对不可变（Immutable）时间轴树结构的轻量级指针变换，天然支持 $O(1)$ 的 Undo/Redo 历史栈，工程文件直接存为紧凑 JSON。  
> 2. **GOP 级随机寻址与双队列解码缓存层（Random-Access GOP Index & Dual-NVDEC Cache）**：  
   * **为什么转码器不能直接当播放器/NLE 用？** 因为视频流是 GOP（关键帧 I 帧 \+ 预测帧 P/B 帧）压缩的。当用户在时间轴上拖拽进度条（Scrubbing）到第 150 帧时，如果最近的关键帧在第 120 帧，解码器必须从第 120 帧开始解码。  
   * **稀疏关键帧索引表（Sparse GOP Index）**：媒体导入时，利用 [src/io/ffmpeg\_io.cpp](../../src/io/ffmpeg\_io.cpp) 只扫容器索引头（AVIndexEntry），几毫秒内建立 PTS (Flicks) \-\> Keyframe Byte Offset 映射表。  
   * **双通道 NVDEC 调度 \+ 显存 LRU 帧环（VRAM Frame Cache）**：基于现有的 [src/core/vram\_ring\_pool.cpp](../../src/core/vram\_ring\_pool.cpp) 升级为带 PTS 标签的显存 LRU 缓存池（例如驻留 32 帧 P010 纹理）。同时维护两个逻辑解码队列：**Playback 预读队列**（顺序向前解码填满缓存）与 **Scrubbing 交互队列**（鼠标拖动时立即丢弃过期非参考帧，跳到目标最近关键帧极速解出目标帧）。  
> 3. **显存算子有向无环图合成层（GPU Render Graph / Compositor DAG）**：  
   * 当时间轴游标停在时间 $T$ 时，评估器收集 $T$ 时刻所有激活轨道的片段：  
     * **单轨播放态（零拷贝直通）**：如果当前只有单条素材且未开特效，直接把 VRAM Frame Cache 中的 P010 显存指针送往显示视口（或按需经过 NGX VSR/TrueHDR）。  
     * **多轨合成与转场态（单一 Uber-Kernel 融合）**：如果 $T$ 时刻存在两段素材交叉叠化（Cross-Dissolve）、画中画（Transform/Scale/Crop）或色彩调色（3D LUT \+ Lift/Gamma/Gain），在 [src/shim/ngx\_bridge/p010\_pack\_runtime.cpp](../../src/shim/ngx\_bridge/p010\_pack\_runtime.cpp) 的 CUDA 模块中将 **多轨 Alpha 混合 \+ 几何缩放裁剪 \+ NGX VSR/TrueHDR \+ 3D LUT** 融合在同一个 CUDA Stream 中一次完成，全程数据不离开显存。  
> 4. **音频主时钟驱动的实时播放引擎（Master Audio Clock & DXGI Waitable Present）**：  
   * **音画同步铁律（Audio Master Clock）**：专业播放器和非线编绝不能用系统 sleep 当时钟，必须以 **Windows 原生 WASAPI 音频硬件消费时钟** 为主时钟（Master Clock），视频渲染线程查询当前音频播放到了哪个 Flicks 时间戳，去 VRAM Frame Cache 取出对应 PTS 的画面，绑定 [src/gui/preview\_viewport.cpp](../../src/gui/preview\_viewport.cpp) 的 IDXGISwapChain3 Waitable Object 在显示器 VSync 垂直同步信号到来时零撕裂上屏。  
   * **交互式性能自适应（Interactive Proxy Mode）**：当用户按住鼠标高速来回拖动时间轴（Scrubbing）时，自动旁路耗时的 NGX VSR 4K 超分，直接以源分辨率 1080p P010 极速上屏（延迟小于 8ms）；当鼠标松开暂停或正常 1x 播放时，立即无缝切回 4K VSR \+ TrueHDR 精渲染。  
> 5. **智能无损秒级导出层（Smart Render / GOP Stream-Copy \+ NVENC）**：  
   * 非线编导出时，评估器扫描整条时间轴：对于用户**未加特效、未开启超分、仅做粗剪拼接**的完整闭合 GOP 区间，直接走 [src/io/ffmpeg\_io.cpp](../../src/io/ffmpeg\_io.cpp) 的 Packet 级无损流拷贝（Stream Copy，速度高达数千 FPS）；仅对跨越剪辑点的残缺 GOP 或挂载了 VSR/TrueHDR/转场特效的区间启动 NVDEC \+ GPU Render Graph \+ NVENC 重编码，并在拼接处由 [src/core/sei\_synthesizer.cpp](../../src/core/sei\_synthesizer.cpp) 保证序列头与色彩元数据一致。

### **7\. 目前这 16 个 DLL 依赖正常吗？如何精准削减到极致？**

**结论：这 16 个 DLL 中，有 6 个是不合理的“架构泄漏与编译配置冗余”（完全可以立即砍掉），有 4 个是 Win32 原生 GUI 必须的系统库，还有 6 个是 Windows 进程运行的基础内核库。通过 3 处构建与加载改造，你可以把 CineRTX.exe 的导入表从 16 个直接砍到 8～10 个纯系统原生 DLL，且彻底告别用户电脑缺少 VC++ 运行库报错的问题！**  
以下是你列出的 16 个 DLL 的逐一取证与去留裁决表：

| 导入的 DLL 名称 | 归属分类 | 是谁把它拉进 CineRTX.exe 的？（代码定位） | 是否正常？ | 削减与根治方案 |
| :---- | :---- | :---- | :---- | :---- |
| MSVCP140.dll VCRUNTIME140.dll VCRUNTIME140\_1.dll | **异常冗余（动态 CRT 泄漏）** | [CMakeLists.txt](../../CMakeLists.txt) 第 296 行只给 rtx\_ngx\_bridge 设置了 /MT（静态 CRT），而 CineRTX、rtx\_core、rtx\_shim\_static 默认走了 /MD（动态 CRT） | **不正常**（裸机未装 VC++ Redist 会直接弹窗报错无法启动） | 在 [CMakeLists.txt](../../CMakeLists.txt) 顶层设置 set(CMAKE\_MSVC\_RUNTIME\_LIBRARY "MultiThreaded$<$\<CONFIG:Debug\>:Debug\>")，**一次性消灭这 3 个 DLL** |
| d3d12.dll dxgi.dll | **异常冗余（静态导入表污染）** | [CMakeLists.txt](../../CMakeLists.txt) 第 433 行写死了 target\_link\_libraries(CineRTX PRIVATE ... d3d12 dxgi)，因为 [src/gui/preview\_viewport.cpp](../../src/gui/preview\_viewport.cpp) 直接调用了 D3D12CreateDevice 和 CreateDXGIFactory1 | **不正常**（违背了全仓通过 LoadLibraryExW 动态加载硬件驱动的隔离原则） | 仿照 [src/shim/ngx\_bridge/d3d12\_interop.cpp](../../src/shim/ngx\_bridge/d3d12\_interop.cpp)，在 preview\_viewport.cpp 中用 LoadLibraryExW \+ GetProcAddress 动态解析这 2 个函数，从 [CMakeLists.txt](../../CMakeLists.txt) 移除 d3d12 dxgi，**消灭这 2 个导入项** |
| WS2\_32.dll | **可裁减项（网络套接字库）** | [CMakeLists.txt](../../CMakeLists.txt) 第 430 行显式链接了 ws2\_32，供 [src/cli/daemon\_rpc.cpp](../../src/cli/daemon\_rpc.cpp) 的 \--daemon-http 监听本地 TCP 端口使用 | **视需求而定**（纯本地转码/GUI 根本不需要 Winsock） | 若与 WebUI 宿主（如 WebView2/Electron）仅通过 \--daemon-stdio（标准输入输出管道）或 Windows Named Pipe（在 kernel32.dll 内）通信，即可删掉 daemon\_rpc.cpp 的 TCP Socket 分支，**消灭 WS2\_32.dll** |
| COMDLG32.dll UxTheme.dll dwmapi.dll COMCTL32.dll | **Win32 GUI 系统组件库** | [src/gui/rtx\_gui.cpp](../../src/gui/rtx\_gui.cpp) 调用了 GetOpenFileNameW（文件框）、SetWindowTheme（暗色滚动条）、DwmSetWindowAttribute（沉浸式暗色标题栏）、InitCommonControlsEx（原生控件） | **正常**（Windows 自带系统库，无外部依赖风险） | 若想进一步精简导入表，dwmapi.dll、UxTheme.dll、COMDLG32.dll 均可改为在 GUI 启动时按需 GetProcAddress（わずか 20 行辅助函数），只留 COMCTL32.dll |
| bcrypt.dll | **系统密码学随机数源** | 静态链接的 libavutil.a（FFmpeg）内部调用 BCryptGenRandom 生成容器 GUID/随机种子 | **正常**（Win10/11 System32 原生自带） | 保留即可（无需任何外部安装） |
| kernel32.dll user32.dll gdi32.dll SHELL32.dll ucrtbase.dll | **Windows 核心基座 DLL** | 进程/线程管理（kernel32）、窗口消息循环（user32）、字体画刷（gdi32）、拖拽文件与 CommandLineToArgvW（SHELL32）、Win10/11 内置通用 C 运行库（ucrtbase） | **完全正常**（任何原生 Win32 GUI 程序的最小集） | 若开启 /MT 静态链接 CRT，连 ucrtbase.dll 及其下方一整串 api-ms-win-crt-\* 转发器也会被一并静态化消除，最终只剩 4 个最纯粹的 Win32 核心 DLL（kernel32、user32、gdi32、SHELL32）！ |

总结来说：只要在 [CMakeLists.txt](../../CMakeLists.txt) 中全局启用 /MT（MultiThreaded），就会立刻把 **MSVCP140.dll、VCRUNTIME140.dll、VCRUNTIME140\_1.dll、ucrtbase.dll 以及那一长串 api-ms-win-crt-\* 全部从导入表中抹除**；再把 preview\_viewport.cpp 的 d3d12/dxgi 改为动态加载，整个 CineRTX.exe 的导入表就会变得极其干净纯粹。