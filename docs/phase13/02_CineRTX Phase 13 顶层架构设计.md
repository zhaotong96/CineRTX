# **CineRTX Phase 13 顶层架构重塑与全栈能力演进领域专家解决方案**

针对 CineRTX Phase 13 面临的 13 项功能演进需求与系统性故障，本方案以“最高层级底层技术调研—顶层软件架构重塑—分模块工程实现与验收基线”三段式纵深展开。核心架构结论先行：当前暴露出的转码预览黑屏、播放器长期空转、测试期系统连续蜂鸣、光流切镜果冻、RIFE 比例锁死、Encoder 多任务队列僵化及编译开关失控，本质上源于早期以“单输入固定规格流”为隐含前提的紧耦合实现已彻底无法承载现代异构媒体管线。Phase 13 必须放弃局部补丁思维，将系统重塑为**硬件互操作与计算内核（HAL & Media Core）—有向图编译与双模会话调度（Graph Compiler & Orchestrator）—原生排版与自绘混合交互层（DirectWrite \+ 声明式管线画布）—确定性构建与合规清单**四层正交分离架构，使播放器与转码器共享同一套零拷贝 GPU 媒体流水线。  
在展开具体设计前须先界定一项客观工程边界：当前检视环境的工作目录为空，未挂载本地 CineRTX 仓库，因而无法直接读取 Phase 13 委托书原文、Phase 10/11 历史代码差异及 docs/TODO 原始文件1。为确保方案的严谨性与可落地性，下文所有涉及 TensorRT 动态尺寸、NVOFA 光流引擎、D3D12–CUDA 互操作、Windows 蜂鸣触发路径、DirectWrite 排版机制及 FFmpeg 静态编译拼接的论断均严格立足于底层技术文档与工程实证；而针对仓库内部隐蔽开关、TODO 文本清理及 Phase 10/11 断层定位，则提供零猜测的确定性代码审计与迁移规约。

## **一、最高层级技术调研与底层边界确立**

在启动架构设计与编码前，必须先厘清制约 Phase 13 六大核心问题域的底层技术机理与真实性能边界，明确哪些属于硬件/API 硬约束，哪些属于算法与工程选型错误。

### **1.1 AI 推理动态尺寸、对齐模数与显存边界**

RIFE（IFNet）基于粗到细的多尺度光流金字塔进行双向中间流估计与掩膜融合2，其网络下采样结构决定了输入图像的空间宽高必须满足严格的整除对齐约束。从开源生态的实测规约来看，不同代际模型对填充模数（Alignment Modulus）的要求并不统一：PotionUI 技术文档指出 rife4.21–4.26（四级 8/4/2/1 或五级 16/8/4/2/1 尺度块配合多尺度 Head 编码器）要求画幅填充至 64 像素的整数倍，而 rife4.6–4.9（带双层特征编码器）则要求填充至 128 像素的整数倍6；Hugging Face 上的 RIFE-v4.9 MLX 移植版同样声明需按 64 的倍数填充4，而一项医学影像预印本研究则记录了将所需的 128 像素误设为 32 像素填充而触发推理缺陷的案例7。更早期的 RIFE 官方推理脚本还暴露过向下取整陷阱（340 // 32 \* 32 \= 320 导致填充尺寸反而小于原图宽度）8。在运行时性能与显存方面，开源项目 TheAnimeScripter 的发行说明在 RTX 3090 上报告了 rife4.26 720p FP16 TensorRT 达 332 fps（约合 3.01 ms/帧）、1080p 2× CUDA 达 277 fps（约合 3.61 ms/帧）的参考表现2，但 SmoothVideo Project（SVP）针对 TensorRT 的用户指南也警示，1440p 及以下视频在优化与运行时通常需要 6–8 GB 显存，而 4K 分辨率往往要求 10 GB 以上显存9。  
若要彻底摆脱“两个 RIFE 模型锁死 16:9”的困境，NVIDIA TensorRT 官方给出的标准技术路径是**运行时动态维度（输入张量使用 \-1 占位符）配合多优化配置（Multi-Optimization Profile）**10。一个 Engine 允许在构建期注入多个互相交叠或互斥的 IOptimizationProfile，从而分别覆盖横屏、竖屏与方屏等不同长宽比包络（Aspect-Ratio Envelope），配合命名维度或 IAssertionLayer 向引擎传达空间维相等约束10。自 TensorRT 8.4 起，NVIDIA 亦针对动态形状 Profile 切换开销做了专门优化12。不过工程上必须严防两大官方风险：其一，每个 IExecutionContext 必须绑定独立 Profile，且不同 Profile 的 MIN/OPT/MAX 参数会令显存占用产生剧烈波动10；其二，SVP 文档的实测反馈表明，若在播放现场为每个首次遇到的新分辨率触发 TensorRT 自动调优编译，等待时间往往长达 5–10 分钟9，这对实时播放器而言完全不可接受。

### **1.2 光流插帧 vs. 神经插帧的切镜果冻成因**

针对用户观测到的“RIFE 在切镜与黑场下干净利落，而光流模式出现严重果冻效应”现象，技术调研揭示了清晰的机理分叉。RIFE 本身通过特权蒸馏训练联合输出中间流与融合掩膜（flow and mask），不依赖外部预训练光流2；此外，工程级 RIFE 管线（如 PotionUI 管线）默认启用场景切换检测，即对降采样至 32×32 的连续灰度帧计算均值 SSIM，当 SSIM \< 0.2 时判定为切镜并直接跳过 RIFE 推理，中间时间槽位仅显示最近源帧（t \< 0.5 取前帧，否则取后帧），仅在未开启切镜检测的盲目倍率模式下才会强行混合所有帧对6。节点式推理工作流中也普遍内置类似 scene\_change\_threshold（如默认 0.24）用于切镜重置13。  
反观 NVIDIA 硬件光流方案（NVOFA 与 NvOFFRUC），尽管 NVIDIA 官方博客与文档显示 Ada Lovelace 架构的 NVOFA 吞吐较 Ampere 提升约 2 倍、可提供 4×4 块粒度流场与外部提示支持14，并暴露了用于过滤异常矢量的 cost 缓冲区以及在 NV\_OF\_EXECUTE\_INPUT\_PARAMS 中用于使历史时间提示失效的重置标志16；然而 NVIDIA 开发者社区的实测（如 VapourSynth nvof-test 深度测试）明确指出两项致命短板：第一，**NVOFA 输出的 cost 缓冲区无法充当可靠的像素级置信度场**，即便在完全静止的画面中也会输出错误流矢量，且单纯对 cost 设阈值无法剥离坏点与好点；第二，**NVOFA 本身缺乏内建的场景切换检测能力**18。在无独立切镜检测、无历史提示重置、无前后向流一致性检验的裸奔状态下，光流模式一旦跨越硬切镜或黑场过渡，必然将两个无关联镜头的纹理按错误矢量强制拉扯变形，产生典型的“果冻/融化”伪影。

### **1.3 D3D12、CUDA 互操作断层与 DXGI 帧步调机制**

Phase 10/11 预览可用而当前预览黑屏、播放器全面失效，直接指向 D3D12 与 CUDA 互操作契约及 DXGI 呈现同步的破坏。与 D3D11 拥有传统的 cudaGraphicsD3D11RegisterResource 不同，**CUDA 至今未提供 cudaGraphicsD3D12RegisterResource 接口**，现代 D3D12–CUDA 零拷贝共享必须基于外部资源导出与导入机制（CreateSharedHandle \+ cudaImportExternalMemory \+ 外部信号量）构建19。综合微软与 NVIDIA 官方文档及开发者问题报告，该链路存在四处极易引发“不报错但黑屏/花屏/卡死”的隐蔽断层，归纳如下表：

| 故障维度 | 底层 API 机制与硬性前提 | 典型错误引发的症状 | 依据属性与来源 |
| :---- | :---- | :---- | :---- |
| 共享堆与资源大小 | 调用 CreateCommittedResource 时堆标志必须包含 D3D12\_HEAP\_FLAG\_SHARED，且资源尺寸需满足 GPU 页对齐（小于 64 KB 的小资源在 cuMemMap 下会报 CUDA\_ERROR\_NOT\_SUPPORTED） | 共享句柄创建失败或映射报错，导致预览管线静默跳过上屏 | NVIDIA 开发者论坛实证22 |
| 2D 纹理内存布局 | D3D12 Texture2D 为适配空间局部性采用平铺或莫顿序（Morton Order）存储而非线性行主序；且 RTV 等图形视图期望 4 通道格式（如 R32G32B32A32float 而非 3 通道） | 若误用 cudaExternalMemoryGetMappedBuffer 当线性数组直接读写纹理，将出现全黑、撕裂或严重偏色；应改用 cudaExternalMemoryGetMappedMipmappedArray 或经线性共享缓冲中转 | 图形互操作技术分析24 |
| 跨引擎状态与同步 | D3D12 与 CUDA 处于异步执行队列，组件间共享资源须遵守明确的资源状态契约（起始为默认态、结束恢复默认态）并配合同步原语；同时高频调用 cudaFree 会引发隐式全局设备同步 | 无同步导致竞态花屏或 TDR 设备重置，频繁申请释放造成帧率骤降 | Microsoft D3D12 互操作文档与开发者论坛21 |
| DXGI 可等待交换链 | 启用了 DXGI\_SWAP\_CHAIN\_FLAG\_FRAME\_LATENCY\_WAITABLE\_OBJECT 的 Flip Model 交换链，渲染每一帧（含首帧）前都必须等待 GetFrameLatencyWaitableObject() 返回的句柄 | 遗漏首帧或每帧等待、或交换链缓冲过少时，引发帧队列阻塞、撕裂或后台呈现停滞 | Microsoft IDXGISwapChain2 文档与帧步调分析25 |

### **1.4 测试触发系统 嘟嘟嘟 蜂鸣的底层根因图谱**

用户在 \[ RUN \] playback\_session\_native\_decode\_h264\_hevc\_av1 等测试附近听到的持续 嘟嘟嘟 蜂鸣，在 Windows 机制上高度吻合三条已知的并发或交织诱因，且均能解释为何其听感宛如“内核输入缓冲区溢出”：  
第一，**消息泵饥饿引发的 HID 输入缓冲区溢出蜂鸣**。Windows 技术社区与开发者排障实证显示，当操作系统或某个持有窗口消息/低级输入钩子（Input Hook）的线程因高负载或同步阻塞而未能及时读取输入队列时，底层 HID 驱动在输入缓冲区满溢后会发出一连串主板/系统蜂鸣声（即便系统扬声器被静音，部分路径下 BIOS/主板蜂鸣器仍会发声），并常伴随鼠标滞涩或 dwm.exe 响应迟滞28。如果测试夹具在运行阻塞式 GPU/FFmpeg 解码的线程上顺带创建了宿主窗口，或者测试进程内驻留了未泵消息的输入钩子，就会精确复现这种连续蜂鸣。  
第二，**DefWindowProc 对未消费消息与 WM\_SETCURSOR 的默认蜂鸣惩罚**。依据微软 WM\_NCHITTEST 文档及 Win32 开发实证，当窗口处于禁用状态（EnableWindow(hwnd, FALSE)）或命中测试返回 HTERROR (-2) 时，系统会将 WM\_SETCURSOR **直接派发给窗口过程 WindowProc（绕过 GetMessage/PeekMessage 循环）**，此时若窗口过程将其交给 DefWindowProc，系统便会直接调用 MessageBeep 发出错误提示音34；同样，向未处理的默认控件按键也可能在框架分支中直接触发 MessageBeep((UINT)-1)36。  
第三，**控制台 BEL (0x07) 字符泄漏与路径 \\a 转义陷阱**。向标准输出或控制台终端发送 ASCII 0x07（即 \\a 响铃控制符）会请求终端模拟器发出系统警报铃声38。尤为隐蔽的是 Windows 路径字符串未做原始字面量或双反斜杠转义时，形如 "...\\av1\_test.mp4" 中的 \\a 在 Python 与 C/C++ 字符串字面量中均属合法转义字符，会被编译器静默替换为 0x07（BEL）38；而将含二进制帧头（如未初始化日志或原始流切片）的字符串打印到控制台，同样会在测试输出中夹带大量 0x07 引发连续蜂鸣。

### **1.5 Windows 原生组件库边界与 DirectWrite 排版能力**

在决定“哪些复用 Windows UI、哪些自研”之前，必须认清 Win32 原生视觉栈的演进断层。Windows 上通过 DwmSetWindowAttribute 设置 DWMWA\_USE\_IMMERSIVE\_DARK\_MODE 仅能将窗口标题栏切换为暗色模式；标准通用控件、属性页与传统对话框（如 CDialog）在暗黑主题下原生支持极不完整，即使辅以 SetWindowTheme("DarkMode\_Explorer") 仍有大量控件与滚动条无法适配，开源跨平台框架往往被迫依赖社区逆向 Hook 补丁维持暗色外观41。  
与之形成鲜明对比的是微软在字体排印与自动换行方面的底层资产——**DirectWrite**。DirectWrite 是原生硬件加速（配合 Direct2D）且与 GPU 设备无关（Device-Independent，交换链或设备丢失重建时工厂与排版对象均无需销毁）的现代文本引擎44。其中 IDWriteTextFormat 原生封装了字体族、字号（DIP 逻辑像素）、语言区域（localeName）、行距、裁剪（Trimming）及自动换行（Word Wrapping）规则；而 IDWriteTextLayout 在给定布局框宽高的前提下，提供 GetLineMetrics（获取折行后的逐行精确尺寸）、DetermineMinWidth（计算无词内强行断行的最小宽度）、GetOverhangMetrics、区域混合样式以及从右至左（RTL）多语种布局能力，并可通过 ID2D1RenderTarget::DrawTextLayout 与 D3D12（通过 D3D11 on 12 与 D2D 互操作通道）高效合批渲染21。因此，“复用原生系统文件选择弹窗、暗色标题栏及 DirectWrite 排版引擎，同时废弃 Win32 控件外壳改用基于 D2D 的自绘组件库与自定义管线画布”是最具性价比的路线。

### **1.6 FFmpeg 多容器支持、异构拼接、静态编译与许可合规边界**

针对编码器队列重塑、软编码 CRF、容器收敛与工程卫生，调研得出四项关键制约：

> 1. **异构合并（Concat）与音频漂移铁律**：FFmpeg 文档与实战工程一致确认，concat demuxer（流复制无损合并）严格要求所有片段具备相同的编码格式、像素分辨率、帧率及容器时间基（tbn，例如 MP4 需统一 \-video\_track\_timescale）；此外若未使用 \-safe 0，含空格或日文/中文等非 ASCII 字符的路径会直接报错 Unsafe file name49。一旦遇到编码、宽高比或帧率不同步的片段，Demuxer 会静默产出卡死或浏览器不可播的坏文件，必须切入基于规格化滤镜的连续转码通路（统一 scale 保持原比例缩放 \+ pad \+ setsar=1 \+ fps \+ format，并以 anullsrc 为静音视频注入兼容占位音轨、对齐各段音视频轨长度以消除衔接后音画漂移）49。  
> 2. **MP4 固化封装规范**：输出锁定 MP4 时，必须显式启用 movflags=+faststart 将 moov 索引原子前移至文件开头，确保视频可即点即播与快速拖动49；在音频编码选择上，vcpkg 规范提醒除非明确需要专利受限且属 nonfree 的 HE-AAC（libfdk-aac），否则应优先采用 FFmpeg 内置 AAC 编码器以维持分发合规56。  
> 3. **软编码器许可证分界**：libx264 与 libx265 均为 **GPL** 许可（如 x264 为 GPL v2），静态链接进主程序会触发整包开源传染56；而 **SVT-AV1** 采用宽松的 BSD 系许可（据不同版本与清单记载为 BSD-2-Clause-Patent 或 BSD-3-Clause-Clear）58，适合作为内置开源软编码主力（若使用 SVT-AV1-Essential 分支须注意其仅支持 10-bit yuv420p10le 输入57），解码侧 dav1d 亦兼具高性能与宽松授权59。通过 libavutil/opt.h 与 AVCodecContext 编程接口可统一配置各编码器的 CRF 与质量参数50。  
> 4. **Windows 静态链接与构建卫生陷阱**：FFmpeg 的 \--enable-static 开关并不自动强制静态链入外部编解码器库（若路径存在动态库链接器仍会优先选用，需配合 \--disable-shared 与显式静态库路径）64；在 Windows 上静态链接 FFmpeg 不会自动带入系统 SDK 依赖，工程文件必须显式声明 dxva2.lib、evr.lib、mf.lib、mfplat.lib、mfplay.lib、mfreadwrite.lib、mfuuid.lib、bcrypt.lib、ws2\_32.lib、Secur32.lib、Strmiids.lib 等系统导入库65。同时，FFmpeg 官方 Windows 编译指南指出 MSVC 工具链下 FFmpeg 默认按 \-MT 构建，若混用 \-MT 与 \-MD 编译的第三方库会直接导致 CRT 多重定义冲突，必须全依赖树严格统一同一 CRT 运行库模式（全部 \-MT 或全部 /MD）66。若 CineRTX 仅处理本地媒体，应当通过 \--disable-network \--disable-schannel \--disable-protocols \--enable-protocol=file,pipe \--disable-autodetect \--disable-programs 彻底裁剪网络栈与自动探测漂移68。

## **二、CineRTX Phase 13 顶层程序架构设计**

基于前述调研，Phase 13 的顶层设计目标是将 CineRTX 从“面向单一文件过程式转码”重构为**以 GPU 帧总线为脊柱、计算与表现彻底解耦、任务与管线声明式可编排的四层正交架构**。

### **2.1 四层正交模块拓扑与依赖边界**

全系统严格遵循单向依赖铁律（上层仅依赖下层稳定接口，下层绝对禁止反向包含上层类型；同时将 FFmpeg、CUDA、TensorRT 原生重型头文件以及易拖慢编译速度的重模板如 std::ranges73 完全隔离在编译单元内部，仅通过轻量抽象接口暴露契约）：

| 架构层级 | 核心模块组成 | 职责边界与资源所有权 | 对外交互契约 |
| :---- | :---- | :---- | :---- |
| L4 交互与表现层（UI & Shell） | WindowHost、LayoutEngine、Direct2DWidgets、PipelineCardCanvas、I18nCatalog | 管理无边框暗色宿主窗口、DirectWrite 两阶段自适应排版、横向可拖拽管线卡片画布、异构作业队列视图及只读预览视口 | 仅向 L3 提交不可变控制指令（增删改队列、拖放重排、启停预览、Seek）并消费只读 UI 状态快照 |
| L3 会话与编排层（Orchestrator） | PlaybackSession、EncodeQueueManager、GraphCompiler、MediaClock | 负责播放主时钟同步状态机、多文件作业调度、将卡片链编译校验为合法的执行计划及显存预算评估 | 持有工作线程生命周期与取消令牌，驱动 L2 图节点运行并对 UI 节流推送状态与进度 |
| L2 媒体与计算内核层（Media Core） | DemuxDecodeStage、SceneCutGuard、RifeEngineRunner、NvofRunner、VsrRunner、NormalizeConcatStage、MuxEncodeStage | 在统一 GPU 帧总线（GpuFrame）上完成解复用、硬/软解码、动态填充与裁剪、切镜守卫、AI 插帧/超分、多段拼接规格化及 MP4 编码封装 | 纯异步 CUDA Stream 流水线执行单元，内部自管理临时显存池（Scratch Pool）与可控预览分接点 |
| L1 硬件抽象与互操作层（HAL / Interop） | CudaDeviceCtx、TrtEnginePool、ExternalMemoryBridge、D3D12Presenter、WasapiAudioSink | 独占管理 D3D12 设备、DXGI 可等待交换链、CUDA–D3D12 NT 共享内存与外部信号量、TensorRT 多 Profile 引擎池与 WASAPI 音频时钟 | 屏蔽驱动与同步细节，确保跨队列资源状态转换与锁步释放零差错 |

### **2.2 统一数据总线与“播放/转码双模合流”机制**

Phase 13 彻底废除“播放器一套假代码、转码器一套真实代码”的双轨分裂状态，让二者在 L2 层共享同一条基于 **GpuFrame 显存句柄** 的零拷贝流水线。每一帧从解码器产出后即封装为 GpuFrame（包含 GPU 纹理/设备指针、精确有理数时间戳 pts、有效画面矩形 valid\_rect、采样纵横比 SAR、色彩空间描述以及前置切镜标识 is\_scene\_cut），后续由 GraphCompiler 将用户在横向工作流中定义的动作卡片序列实例化为串联的 IComputeNode 节点并在同一个 CUDA Stream 上依次调度。任何模型所需的尺寸对齐（如 64 或 128 像素边界填充）都在对应节点的独立显存草稿池内闭环完成并在输出时精确裁回 valid\_rect，避免中间过程回落 CPU 或产生临时磁盘文件。  
在该统一执行图的末端，通过策略模式的 Sink 注入实现双模分流：当运行**转码会话**时，图末端直连硬件/软件编码封装器（MuxEncodeStage）锁定输出 faststart MP4，并在编码前挂载一个受 std::atomic\<bool\> preview\_enabled 严格门控的轻量分接点（Preview Tap），仅在勾选预览时通过无锁单槽覆盖邮箱（Latest-Frame Mailbox）向 D3D12Presenter 节流投递副本；当运行**播放会话**时，编码器被替换为由 **WASAPI 音频渲染时钟** 驱动的 PacedRingBuffer（有界预读环），直接消费经过 AI 管线处理的帧并通过 ExternalMemoryBridge 与 IDXGISwapChain2 按显示器 VSync 精确步调上屏25。这样一来，只要转码核心通过测试，播放器底层渲染与 AI 管线即天然具备同源可靠性。

## **三、Phase 13 全栈能力演进与 13 项需求分模块实现方案**

以下按顺序逐一给出针对用户全部 13 项需求的深度工程实现细则。

### **3.1 需求 1：打破 16:9 限制——RIFE 多比例动态引擎、边缘反射对齐与显存分块**

针对“现有两个 RIFE 模型均固定 16:9，如何兼容竖屏（9:16）、超宽屏（21:9）、方屏（1:1/4:3）等任意比例视频”的问题，Phase 13 从“模型构建改造”和“旧静态模型向后兼容”两个层面给出确定性解决方案，并建立如下两级路由策略：

| 路由分支 | 适用前提 | 空间对齐与几何保真机制 | 算力与显存影响 |
| :---- | :---- | :---- | :---- |
| 路径 A：动态维度 \+ 多比例 Profile 桶（Phase 13 标准方案） | 持有 RIFE ONNX 源码，可重新导出并构建动态 TensorRT Engine | 导出 ONNX 时将空间高宽设为命名动态维 \[1, 3, \-1, \-1\] 并附加等维约束（如 IAssertionLayer）10；在单一 Engine 中构建多组 IOptimizationProfile 分别覆盖横屏、竖屏与方屏包络10，各 IExecutionContext 绑定对应 Profile10 | 彻底消除拉伸失真与无效黑边计算，TensorRT 8.4+ 已优化 Profile 切换效率12 |
| 路径 B：静态 16:9 模型无失真兼容适配（过渡兜底方案） | 仅持有既有固定 16:9 .engine 文件且暂无法重导 ONNX | 严禁非等比缩放挤压图像！在 GPU 上将输入帧等比适配至模型有效区域内，四周采用**边缘像素复制/镜像反射填充**补满 16:9 画布后推理，输出再精确裁出 valid\_rect | 100% 保持几何比例与运动轨迹真实性，但填充区域会产生额外的无效推理 FLOPs |

在动态引擎的运行时预处理阶段，无论原始输入宽高 (W, H) 比例如何，RIFE 节点必须根据当前加载模型的实际代际选择对齐模数 align（针对 rife4.21–4.26 等架构取 64，针对 rife4.6–4.9 等需更高对齐要求的架构取 1284），并严格使用向上取整公式 pad\_w \= ((W \+ align \- 1\) / align) \* align 与 pad\_h \= ((H \+ align \- 1\) / align) \* align，根绝向下整除导致的画幅缩水 Bug8。在填补空白像素时，绝对禁止填入纯黑 0（纯黑会在图像边缘制造尖锐人工阶跃边，严重扭曲 IFNet 最外层卷积块的光流方向），而应当通过自定义 CUDA 预处理 Kernel 执行边缘像素复制或镜像反射填充，推理结束后按原始 \[0:W, 0:H\] 切片还原。同时，考虑到 SVP 文档指出的 4K RIFE 推理往往要求 10 GB 以上显存9，当检测到大画幅超出当前显卡安全显存阈值时，自动触发**网格分块推理（Auto-Tiling）**：将帧划分为满足 align 倍数的若干图像块（设计上建议保留 64 px 对齐重叠带并在拼接缝做线性渐变羽化），串行或受控并发执行推理，消除 6–8 GB 显存显卡跑高分辨率视频时的 OOM 崩溃。

### **3.2 需求 2：定位并根治 \[ RUN \] playback\_session\_native\_decode\_h264\_hevc\_av1 附近的系统蜂鸣**

结合第 1.4 节排查出的 Windows 三大系统蜂鸣机理，工程团队在接入本地仓库后须按以下无遗漏的顺序核查步骤立即彻底排雷：

> 1. **隔离解码单元测试的窗口创建与消息泵阻塞**：核查 playback\_session\_native\_decode\_\* 测试夹具在初始化呈现/解码上下文时是否顺带创建了真实的 Win32 HWND。若工作线程在执行同步高耗时解码或等待 GPU 时未高频调用 PeekMessageW \+ DispatchMessageW，任何窗口/键鼠输入都会迅速塞满队列，直接导致 Windows 低级 HID 驱动持续发出类似输入缓冲区溢出的高频蜂鸣28。根本解法是将单元测试彻底迁移至纯无头离屏模式（仅测试解码至 CUDA/ID3D12Resource，严禁创建桌面窗口）；对于确实需要验证交换链上屏的集成测试，必须由专用 UI 线程维护独立消息循环。  
> 2. **拦截 WindowProc 里的 WM\_SETCURSOR 隐式 MessageBeep**：在任何测试宿主窗口或主程序窗口过程中显式接管 WM\_SETCURSOR。鉴于该消息可绕过 GetMessage/PeekMessage 直接送达 WindowProc，当检测到窗口被禁用或 LOWORD(lParam) \== HTERROR 时必须直接返回 TRUE 拦截，绝不可透传给 DefWindowProc 触发 MessageBeep34。  
> 3. **清查进程期全局输入 Hook 与 dwm.exe 拥塞源**：检查测试启动夹具及 UI 框架是否提前注册了 WH\_MOUSE\_LL / WH\_KEYBOARD\_LL 等全局或线程级输入钩子。在进程陷入密集阻塞期间，此类钩子会引发全局指针顿挫与滚轮蜂鸣29。所有单元测试与后台无头转码过程均应禁止挂接输入钩子。  
> 4. **全量检索 \\a 路径转义与 0x07（BEL）控制台字符泄漏**：针对错误精准集中在 \_av1 测试附近的显著特征，第一时间排查测试资产路径字面量是否存在未用原始字符串或 \\\\ 转义的 "\\av1..." 写法——\\a 在字符串解析中会被静默转化为响铃符 0x0738；同时排查 FFmpeg 日志回调函数或测试用例打印逻辑，强制为所有控制台输出增加控制字符过滤器（剥离或十六进制转义除 \\r\\n\\t 外的 \< 0x20 字节），彻底阻断 0x07 触发终端响铃39。

### **3.3 需求 3：修复转码预览黑屏与重建稳定呈现引擎（HAL Interop）**

针对 Phase 10/11 尚可预览而当前预览与播放全黑的回归故障，Phase 13 不再依靠临时拼凑修复旧代码，而是在 L1 层重塑符合 D3D12–CUDA 现代规范的 **ExternalMemoryBridge 与 D3D12Presenter 呈现核心**，同时消除两者间常见的资源布局与同步陷阱：  
在内存共享层，所有用于向呈现管线传递图像的缓冲区均在 D3D12 侧以堆标志 D3D12\_HEAP\_FLAG\_SHARED 创建23，并确保最小分配尺寸按 64 KB 页对齐（避免小于 64 KB 的资源引发 cuMemMap 返回 CUDA\_ERROR\_NOT\_SUPPORTED22），随后通过 CreateSharedHandle 导出 NT 句柄并用 cudaImportExternalMemory 一次性导入复用19（绝对禁止在逐帧渲染循环中重复导入与调用引发隐式全局同步的 cudaFree22）。在纹理格式与布局上，切忌将 3 通道 RGB 或线性指针直接硬套到 D3D12 2D 平铺纹理视图上24：统一采用 **4 通道 RGBA 对齐布局**，优先方案是 CUDA 侧向导入的线性共享缓冲（cudaExternalMemoryGetMappedBuffer20）写入经过行距（Pitch）对齐的 4 通道像素，再由 D3D12 命令列表复制到交换链后台缓冲（若直接绑定 2D 纹理则须严格通过 cudaExternalMemoryGetMappedMipmappedArray 映射 CUDA Array 并保持格式与图形视图绝对一致24）。  
在同步与步调层，遵循微软 D3D12 跨引擎互操作的状态契约（将跨组件交接的资源状态明确约定在默认状态并严格执行栅栏同步21），把一对共享 ID3D12Fence 导入 CUDA 为外部信号量20：仅当 D3D12 发出“上一帧已复制/读取完毕”信号后，CUDA Stream 才执行下一帧像素写入并在完成后触发“写入就绪”信号。同时，呈现器使用 DXGI Flip Model 与 DXGI\_SWAP\_CHAIN\_FLAG\_FRAME\_LATENCY\_WAITABLE\_OBJECT 配置多缓冲交换链，严格遵守微软 IDXGISwapChain2 规范——在渲染**包括第一帧在内的每一帧前**均先等待 GetFrameLatencyWaitableObject() 返回句柄25，并配合窗口大小变更防抖重建，从根源上消除黑屏、死锁与延迟漂移。

### **3.4 需求 4：正式落地 60 fps 实时播放器与固定尺寸 RIFE 适配策略**

让长期“假装完成”的播放器真正跑起来，关键在于将非实时的重计算 AI 推理纳入刚性的 **16.67 ms（60 fps）实时呈现步调**，并兼容目前受限于固定比例的旧模型过渡状态与不同显卡算力上限。  
在时间轴映射上，播放器彻底抛弃整数倍粗暴插帧，采用基于精确有理数（如 24000/1001 对齐 60/1）的**分数帧率节拍器**6。输出第 k 帧对应源时间轴位置 pos \= k × src\_fps / 60：若 pos 距源帧整数位置的偏差小于 1/1000 帧，直接跳过 AI 推理原样送出该源帧；其余中间时刻将分数部分 timestep \= frac(pos) ∈ (0, 1\) 送入 RIFE4。以常见的 24 \-\> 60 fps 为例，每 5 个输出帧周期（即每 2 个源帧跨度）产生 1 帧原帧与 0.4、0.8、0.2、0.6 共 4 个插帧时刻6，意味着每秒需要 48 次 RIFE 推理（平均每帧推理预算 20.83 ms，而连续出帧窗口仍受限于 16.67 ms/帧呈现节拍）；而在 30 \-\> 60 fps 下为 1 原帧加 1 个 0.5 插帧循环（30 次推理/秒，平均预算 33.33 ms）。  
为确保在各类分辨率与既有固定尺寸 RIFE 下稳定运行于 60 fps 而不音画卡死，播放会话实施三级实时护栏。首先，**播放前严禁触发现场编译**——仅允许加载预构建的多比例动态 Engine 缓存或预计算 Timing Cache，彻底避开 SVP 文档所述的 5–10 分钟新分辨率编译卡顿9。其次，引入**视口感知代理降载（Viewport-Aware Proxy Scaling）**：在播放模式下，若原片分辨率超越当前播放器窗口物理显示尺寸或超出显卡单帧实时预算（例如 4K 片源），在解码后进 RIFE 前先于 GPU 上等比下采样至适配窗口/GPU 能力的代理尺寸（参照 TheAnimeScripter 实测，720p FP16 TensorRT 在高端卡上可达数百帧/秒级极低单帧耗时2）；若在 Phase 13 早期过渡阶段仍需挂载既有的固定 16:9 模型，亦自动采用第 3.1 节的等比缩放加边缘复制填充贴入固定画布后再裁回显示，保证画面永不畸变。最后，确立**WASAPI 音频主时钟主导的有界旁路熔断**：当预读环检测到某目标帧交付相对音频播放时间戳滞后超出安全容限（设计建议阈值约 8 ms）时，调度器立即执行单帧 AI 旁路，直接推送已解码完成的最近原帧上屏并记录一次可观测的 Bypass Count，绝不允许视频推理反压阻塞音频流或拖垮整体播放流畅度。

### **3.5 需求 5：组件库重构（DirectWrite 自动换行排版与混合 UI 架构）**

Windows 原生控件体系在暗黑视觉一致性与复杂自定义交互上存在硬伤，但在字体度量与国际排版上拥有顶尖底座42。Phase 13 对 UI 组件库确立明确的“复用与自建分界线”：系统级文件拾取弹窗（支持路径浏览与扩展名过滤）、剪切板以及通过 DwmSetWindowAttribute(..., DWMWA\_USE\_IMMERSIVE\_DARK\_MODE, ...) 实现的暗色标题栏41 全数复用 Windows 原生基础设施；全部文本测量、断行与绘制能力全数复用硬件加速且设备无关的 **DirectWrite** 引擎45；而所有交互控件（Label、Button、CheckBox、Slider、ScrollView、JobQueueList）则摒弃粗糙的 Win32 CDialog/ComCtl32 外壳42，采用基于 D3D12 交换链互操作通道（Direct3D 11 on 12 \+ Direct2D21）的统一暗色自绘组件库，并配合全新设计的拖拽管线画布共同构建一致体验。  
针对此前突出暴露的 **Label 文字自动换行截断或布局崩坏问题**，Phase 13 在 LayoutEngine 与 DirectWrite 之间强制实施以下两阶段测量与排列协议：

> 1. **Measure 测量阶段（宽度约束下行高自适应）**：父布局容器根据当前 DPI 缩放因子换算可用 DIP 宽度并下发给子 Label74。Label 仅在文案、字体属性或可用宽度实质发生变化时，以该可用宽度为盒宽、无限大高度创建一个与 IDWriteTextFormat 关联的 IDWriteTextLayout 实例（利用 IDWriteTextLayout 文本不可变特性进行结果缓存，避免每帧重复构造）48。随后开启文字自动换行（以及应对超长无空格文件名时的词内紧急断行或末尾省略号裁剪策略），调用 IDWriteTextLayout::GetLineMetrics 汇总折行后的全部行高之和，并参考 DetermineMinWidth 与 GetOverhangMetrics 计算真实所需包围盒高度，向上报告给父容器重新撑开卡片或列表行高46。  
> 2. **Arrange 与 Draw 绘制阶段（亚像素硬件输出）**：父容器依各控件回传的实测高度完成最终纵坐标重排并分配矩形，确保多行长文本、错误堆栈或国际语言折行绝不被底部边界截断；渲染阶段直接将缓存好的 IDWriteTextLayout 交由 ID2D1RenderTarget::DrawTextLayout 完成亚像素抗锯齿硬件光栅化45。

### **3.6 需求 6：全链路国际化（i18n）与排印弹性基座**

Phase 13 将国际化作为横切关注点写入应用内核与布局引擎，而非事后机械替换字符串。架构层面确立三条刚性规范：其一，业务核心、媒体路径与任务序列序列化**全栈统一使用 UTF-8 (std::string)**，绝不依赖系统本地 ANSI 代码页（避免日文、中文或含空格路径送入 FFmpeg 解复用器时失败50），仅在跨入 Windows 原生 API 与 DirectWrite 边界的瞬间通过标准转换转为 UTF-16 std::wstring。其二，所有本地化语言包外置为分域 JSON 词典（如 zh-CN、en-US、ja-JP），界面文案格式化严禁使用依赖参数顺序的位置拼接（如 printf 风格），一律使用带命名占位符的模板（例如 "{done} / {total} 帧 · {fps} fps"），以便兼容不同语系的语序重构。其三，切换语言包时除了替换词典，必须将对应的 localeName（以及必要时的文本阅读方向设置44）同步写入 DirectWrite 文本格式与布局对象以触发系统字体回退与基线校准，同时广播全 UI 树重算布局（InvalidateMeasure），利用第 3.5 节的两阶段动态高度流彻底吸纳英语、德语等长文本语种的扩展体积，避免按钮文本溢出或控件重叠。

### **3.7 需求 7：全容器输入、固化 MP4 输出、软编码 CRF 与许可证隔离**

针对输入格式扩展、软编码参数开放以及输出封装锁定 MP4 的需求，Phase 13 统一梳理媒体格式边界与开源合规防线：  
在输入端，彻底移除扩展名白名单限制，完整开放 FFmpeg 主流容器解复用矩阵（涵盖 MKV、MP4、MOV、WebM、AVI、TS/M2TS、FLV、MXF 等），依托 avformat\_open\_input 与 avformat\_find\_stream\_info 自动识别封装内的任意像素格式（8-bit yuv420p 及 10-bit yuv420p10le52）、动态分辨率及 VFR 时间戳，并优先利用 NVDEC 硬解与高性能开源解码器 libdav1d 处理 AV1 输入56。  
在输出端，封装格式强制锁定为标准 MP4，并在写入文件头时默认附加字典选项 movflags=+faststart 将 moov 原子置于头部以支持全平台秒开与顺畅拖放49。考虑到 MP4 容器对编码流的兼容性要求，音频管线实行“兼容直通、不兼容自动转 AAC”原则——当源音轨兼容 MP4 且未触发剪辑拼合时直接流复制；若输入为 PCM、FLAC、Vorbis 或多段异构拼接，则自动统一重编码为 FFmpeg 内置 AAC 编码器产出，规避专利受限且 nonfree 的 libfdk-aac56。视频软/硬编码参数则通过 FFmpeg 统一的 libavutil/opt.h 与 AVCodecContext 参数设置接口对外暴露63，具体能力矩阵与许可证分发策略如下表所示（具体编码预设档位与可调数值范围应随构建期实际集成的编码器库版本动态查询暴露，避免写死版本耦合常量）：

| 编码后端类型 | 核心质量与控制参数（通过 libavutil/opt.h 暴露） | 位深与像素格式支撑 | 开源许可属性与合规分发策略 |
| :---- | :---- | :---- | :---- |
| SVT-AV1 (libsvtav1) 软编码 | crf 恒定质量因子与速度/质量权衡 preset | 8-bit yuv420p；若启用 SVT-AV1-Essential 变体须注意其仅接受 10-bit yuv420p10le57 | **BSD-2-Clause-Patent / BSD-3-Clause-Clear**58，对闭源与商业分发友好，定位为 Phase 13 默认内置高压缩软编码核心 |
| NVENC（H.264 / HEVC / AV1）硬件编码 | 基于恒定质量模式（CQ）与多档位预设 | 硬件显存零拷贝直通（8-bit / 10-bit） | 运行时加载显卡驱动侧 NVENC 能力，闭源友好，定位为高速主力 |
| libx264 / libx265 软编码 | crf（如常见高质量与标准区间 18 / 23 示例50）、preset、tune、profile | 8-bit 与 10-bit | 属于 **GPL（如 x264 为 GPL v2）**56！绝对禁止直接静态链入闭源商业构建；必须通过独立构建 Profile 隔离或封装为独立 GPL 子进程边界按需调用 |

### **3.8 需求 8：面向多文件、独立参数与动态增删的全新 Encoder 队列引擎**

原 Encoder 无法兼容异构多任务与任意合并需求，症结在于调度层把单个输入文件等同于队列单元。Phase 13 将编码器的最小调度原子重构为自包含的 **EncodeJob（作业包）**，同时重设计支持异构拼接的三路执行内核。  
在数据模型与队列交互上，队列中的每一个条目都是一个完整的 EncodeJobSpec，包含唯一任务 ID、有序的片段数组 clips\[\]（每个片段可指向不同路径且可带独立入出点裁剪）、布尔标志 merge\_outputs、独立绑定的 **PipelineGraphSpec**（用户在该任务上拖拽排布的动作卡片链）、独立 **EncodeProfileSpec**（编码器选择、CRF/CQ 值、输出分辨率约束与音频策略）以及目标 MP4 路径。调度器采用“等待区可随时可变编辑 \+ 执行区运行期快照隔离”机制：当任务 A 正在后台编码时，用户仍可在队列中任意追加新作业、拖动重排排队中任务的优先级、修改尚未执行作业的 CRF/管线/文件列表、或删除等待中任务；一旦某作业被工作线程拾取执行，其 EncodeJobSpec 立即锁定为不可变快照，用户仅能通过绑定的 std::stop\_token 触发安全中断与清理。  
在多文件处理与合并执行上，面对同一个作业下多达数个甚至数十个编码、分辨率、帧率与音轨各异的片段，EncodeJobRunner 摒弃盲目调用 concat demuxer 的脆弱做法49，按以下三路决策路由精准分发：

| 执行路径 | 触发判定条件 | 核心实现机制与防踩坑保障 |
| :---- | :---- | :---- |
| 路径一：批处理独立输出 | merge\_outputs \== false（无论单文件还是多文件） | 作业内依次或受控并发对 clips\[\] 中的每个片段独立执行同一套 AI 管线与编码预设，生成各自对应的 .mp4 文件 |
| 路径二：全等极速无损直拼 | merge\_outputs \== true、**未启用任何 AI/色彩/缩放滤镜**，且预探测证实所有片段编码、分辨率、SAR、帧率、像素格式、音频采样率/声道数及 **容器时间基（tbn）** 完全相同49 | 启用 concat demuxer 级流复制拼接，自动注入安全路径保护（等效 \-safe 0 以兼容空格及多语种非 ASCII 路径50），秒级无损封装为 faststart MP4 |
| 路径三：GPU 统一规格化连续拼接（通用核心路径） | merge\_outputs \== true，且片段存在分辨率/横竖屏/帧率/编码差异，或存在音轨缺失/声道不统一，或挂载了 RIFE/VSR 管线49 | **全过程不生成中间大文件！** 任务启动前先确定唯一 CanonicalOutputSpec（统一目标宽高、SAR 1:1、统一帧率及立体声 48 kHz 音频规约）。逐片段解码并独立走完 AI 管线后，在 GPU 上执行保比例缩放加黑边对齐（等效 scale=...decrease \+ pad \+ setsar=152）；对静音视频段按片段精确时长生成静音占位帧（等效 anullsrc52）并修齐单段内音视频流尾差以防止累计漂移55，将各段 PTS 首尾连续平移累加后持续送入**同一个连续编码器与 MP4 Muxer 实例** |

### **3.9 需求 9：真正节省算力与显存的转码预览门控 CheckBox**

若仅在界面隐藏预览画布或停止 UI 重绘，底层显卡仍会徒劳消耗显存带宽与计算单元执行 RGBA 转换及共享表面复制。Phase 13 将预览开关 CheckBox 打造为贯通 L4 到 L1 的**真实零开销链路熔断开关**。  
当用户在转码期间取消勾选“开启预览”时，绑定于转码会话上的原子变量 preview\_enabled 即刻置为 false（以 std::memory\_order\_release 发布）。L2 转码流水线的 Preview Tap 在每帧完成编码输入后第一时间原子读取该开关，一旦检测到 false，立即短路跳过高耗时的 YUV \-\> RGBA 预览色彩空间转换 Kernel，跳过 ExternalMemoryBridge 共享内存复制及跨队列信号量同步20，并通知 L1 释放非必需的临时预览 RGBA 显存缓冲，把全部有限 VRAM 优先让渡给高开销的 4K AI 模型。与此同时，UI 视口保留最后一帧静态画面并覆盖轻量化“预览已暂停以释放 GPU 资源”提示遮罩，彻底停止交换链提交。  
当用户重新勾选“开启预览”后，核心流水线恢复向 Latest-Frame Mailbox（仅保留最新一帧的单槽无锁覆盖邮箱）投递画面，按预设节流周期（例如设计为约 33 ms 或 66 ms 上限，即 15–30 fps 预览上限）在 GPU 上顺便按当前 UI 预览窗尺寸完成轻量化降采样后再写入共享呈现缓冲区。这种设计保证“预览永远只服务于人眼观察，慢速窗口绘制或窗口最小化绝不反过来卡住后台高速编码器”。

### **3.10 需求 10：可拖拽排序的横排卡片式工作流与管线算力编译器**

类似专业非编软件（如 Premiere 效果控件管线）的横向可拖拽方块卡片交互，不仅关乎前端操作灵活性，更直接牵动整个 GPU 管线的内存峰值与计算量级——因为“先插帧后超分”与“先超分后插帧”在算力成本上呈指数/平方级分叉。  
在 UI 交互层面，L4 的 PipelineCardCanvas 采用自左向右的有向流式画布：最左端锁定为不可拖动的 输入源（Source） 锚点卡片，最右端锁定为 MP4 封装输出（Encoder） 锚点卡片；中间为可动态增删、横向拖拽重排的动作卡片区（包括 AI 插帧（RIFE / NVOFA）、AI 超分（VSR）、切镜保护、色彩/降噪 等功能方块），卡片间由动态 SVG/D2D 方向矢量线相连。交互状态机支持指针捕获拖拽排序（拖动期间其余卡片平滑让位并亮起目标插入虚线槽）、卡片角标“一键旁路（Bypass）”开关（灰显保留参数但不参与执行）以及单击选中卡片后在画布下方展开专属参数配置抽屉。  
在系统算力建模层面，每次卡片排序落地时，L3 的 GraphCompiler 立即执行一次静态图类型与开销推导。设源视频单帧像素量为 P \= W × H，输入帧率为 F\_in，目标帧率为 F\_out（倍率 m \= F\_out / F\_in），每秒需合成的新帧数为 F\_interp（如 24 \-\> 60 时为 48 帧/秒，30 \-\> 60 时为 30 帧/秒），超分倍率为 s（放大后像素量增至 s²P，例如 2× 超分使单帧像素面积扩大至 4P）。两种核心排布的架构代价对比十分鲜明：

| 工作流排列方式 | 插帧阶段像素量与推理规模 | 超分阶段像素量与推理规模 | 画质物理规律与显存风险 | 专家推荐默认策略 |
| :---- | :---- | :---- | :---- | :---- |
| 排序 A：插帧 \-\> 超分 | 在原始 1× 像素量 P 上运行 F\_interp 次/秒，单次算力小、显存极可控 | 须在升帧后的 F\_out 帧上运行超分（调用次数较排序 B 多出倍率 m） | 先在无缩放锐利原画上追踪运动并插帧，再统一超分放大，可有效抑制超分微弱时域闪烁被插帧进一步放大 | 面向 1080p 升 4K、或显存 ≤ 8–10 GB9 机器上的**默认安全排序** |
| 排序 B：超分 \-\> 插帧 | 须在放大后的 s²P 像素量上运行 F\_interp 次/秒（2× 超分即 4× 图像面积，4K RIFE 显存需求迅速冲破 10 GB9） | 仅需对 F\_in 源帧运行超分（可节省 (1 \- 1/m) 的超分调用量，例如 24 \-\> 60 时可省去 60% 超分计算！） | 若超分算子存在帧间抖动，后置插帧容易连带生成局部扭曲；但若超分模型极其沉重且显存充裕则总耗时可能反超 | 适用于低分辨率老片（如 480p 升 1080p）且挂载了极其重型耗时超分模型、同时显存余量极充足的工作站 |

GraphCompiler 在用户拖拽放下卡片的瞬间即根据当前选中作业的最高源分辨率与目标倍率完成逐节点显存与计算量推导，在卡片链顶部的状态栏实时亮出直观的资源预算提示与风险预警（如“当前顺序将在 4K 分辨率下运行 RIFE，显存需求较高，建议改为先插帧后超分或开启自动分块”），防止用户盲目连线导致运行数分钟后突发 OOM 崩溃。

### **3.11 需求 11：彻底根治光流模式在切镜/黑场下的严重果冻效应**

立足第 1.2 节调研证实的 NVOFA 两大技术现实——**cost 缓冲不可用作可靠置信度掩膜**且 **NVOFA 自身不感知场景切换**18——Phase 13 在 L2 层设计了贯穿光流模式与 RIFE 模式的 **SceneCutGuard 三层切镜与遮挡防线**，在不依赖 CPU 回读像素的前提下把光流模式的切镜表现拉升至与 RIFE 同一水准。  
第一层防线为 **CUDA 全显存前置切镜与黑场探测**。在把连续源帧对 (I0, I1) 提交给 NVOF / NvOFFRUC 或 RIFE 之前，先在当前 CUDA Stream 上把两帧高速降采样至与业界切镜检测一致的 32×32 低频缩略图（零 PCIe 传输）6，在几十微秒内并行计算三项联合判据：其一是 32×32 局部均值 SSIM（参照实用化 RIFE 管线 SSIM \< 0.2 的切镜准则6 或节点管线可配置阈值 0.24 体系13）；其二是归一化颜色直方图差异度；其三是**低方差/黑场状态监测**，即当任一帧平均亮度与像素方差跌入黑场阈值区间时（如淡入淡出黑场交替处），显式禁止生成跨黑场的大位移拉伸光流矢量。  
第二层防线为 **切镜即时旁路与 NVOFA 时域提示硬件复位**。一旦 SceneCutGuard 判定 (I0, I1) 为硬切镜或跨黑场转场，首先触发计算旁路：绝对不启动光流双向扭曲混合（若为 RIFE 模式亦跳过推理），对于二者之间所有插帧时间点 t ∈ (0, 1)，严格按最近源帧保持策略输出（t \< 0.5 输出 I0，t \>= 0.5 输出 I16），以硬切还原替代跨镜头果冻融化。紧接着，在对转场后的新镜头第一组非切镜帧对调用 NVOFA 时，必须按照 NVIDIA Optical Flow SDK 编程指南要求，在 NV\_OF\_EXECUTE\_INPUT\_PARAMS（或 NV\_OF\_EXECUTE\_INPUT\_PARAMS\_D3D12）中**显式置位无效化历史时间提示标志（Flag to invalidate past temporal hints）**17，强制硬件清零上一镜头的时域矢量记忆，彻底截断残余运动向量污染新镜头的前几帧。在转码输出端，切镜信号同步透传至编码封装层，强制在新镜头首帧插入完整关键帧（IDR），既锁住 GOP 边界又大幅改善切镜后的码率质量分配。  
第三层防线为 **镜头内前后向流一致性校验（反局部果冻）**。针对非切镜镜头内部高速前景遮挡背景时单纯依靠 NVOFA cost 缓冲仍会误判的痛点18，可在光流模式下提供“标准极速”与“高保真抗果冻”双子模式：高保真模式下分别请求 NVOFA 计算前向流与后向流，在合成 Warp CUDA Kernel 中逐像素核验前向矢量指向位置处的后向矢量对称闭环残差与采样像素亮度一致度；凡是前后向闭环失效的遮挡/新暴露区域，立即压低插值混合权重并回退取可见侧单向像素，从物理上消灭高速物体边缘拉丝与果冻气泡。

### **3.12 需求 12：编译开关收敛、隐藏开关清理与确定性构建卫生规范**

鉴于当前环境未能读取原始仓库的 CMake 脚本与源代码宏定义1，Phase 13 对编码卫生与构建系统执行“自顶向下确定性声明式重构”标准，本地合入阶段须严格依以下清单完成清理：  
在构建矩阵与依赖治理层面，废除历史散落的批处理脚本与手改属性表，将全部构建入口收敛至单一 CMakePresets.json 配合版本锁定的依赖清单（如声明式 vcpkg.json 或受控构建配方）56。对外输出预设严格区分为不含任何 GPL 依赖的商业闭源发布配置（集成 NVENC \+ SVT-AV1 \+ dav1d）与可选开源完整配置（显式开启 GPL 下的 libx264/libx265）56。按照第 1.6 节调研铁律，对 FFmpeg 构建参数实施刚性收口：在确定 CineRTX 仅服务本地媒体文件的定位后，启用 \--disable-network \--disable-schannel \--disable-protocols \--enable-protocol=file,pipe \--disable-autodetect \--disable-programs \--disable-shared \--enable-static64；同时**严格强制主程序与所有第三方静态库编译选项完全对齐同一 CRT 运行库模式**（若沿用 FFmpeg MSVC 工具链默认的 \-MT，则全部依赖按 \-MT 静态编译；若选用动态 CRT 变体如 static-md，则全栈统一 /MD）66，禁止使用强制忽略特定默认库的链接器黑魔法掩盖混用冲突66，并在 CMake Target 中显式且完整地声明依赖的 Windows 系统库（ws2\_32.lib、bcrypt.lib、Secur32.lib、mfplat.lib、dxva2.lib 等）65。  
在源码与开关卫生层面，接入本地仓库后立即执行全库正则审计，扫描所有条件编译宏（\#ifdef / \#if）、运行时环境变量读取（std::getenv / GetEnvironmentVariable）及非标调试全局标志：一切服务于早期阶段绕过缺失模块或硬编码测试参数的临时开关必须彻底拔除；确有运行期调节价值的控制量（例如日志级别、预览最大节流帧率、切镜 SSIM 阈值、光流高保真校验开关、显存分块安全线），全部收归单一强类型配置对象 AppRuntimeConfig，由显式配置文件与 UI 高级设置面板唯一控制，并在程序启动日志第一屏完整打印生效配置哈希与关键字段快照，从此杜绝任何不可见分支引发的“幽灵行为”。

### **3.13 需求 13：docs/TODO 残留待办项的本地审计与 Phase 13 归口闭环机制**

受限于当前工作区未能访问 docs/TODO 原始文档与委托书文本1，本方案严守工程实事求是准则，不对不可见文件内可能包含的具体待办条目做任何臆测或编造，而是向协调负责人交付一套可于本地仓库直接落地的 **docs/TODO 四象限审计收敛规约**，保证进入实操阶段后半小时内完成历史 TODO 的精准清零与归档：

| TODO 分流象限 | 本地审计判定标准 | 在 Phase 13 中的处置规则 | 与本架构方案的挂接归口 |
| :---- | :---- | :---- | :---- |
| Q1：稳定性与回归阻断项（P0 Blocker） | 涉及测试期系统蜂鸣、预览/播放器全黑、资源泄漏、死锁、路径编码崩溃或 CRT 链接冲突 | 直接提升为 Phase 13 第一里程碑强制门禁项，阻断项未全绿前禁止合入后续 UI 特性 | 归口需求 2、需求 3 与需求 12 |
| Q2：Phase 13 核心交付对齐项（P1 Scope） | 涉及非 16:9 模型比例、容器扩展、CRF 参数开放、Encoder 多文件合并、预览 CheckBox、横向卡片 UI、光流切镜优化与 i18n | 直接并入对应模块的 Phase 13 正式验收单，废除原有的零碎临时修补提案 | 归口需求 1、4、5、6、7、8、9、10、11 |
| Q3：已被重塑架构自然消灭的过时项（Obsolete） | 围绕废弃的“单任务队列模型”、Win32 CDialog 控件补丁或旧静态 16:9 临时脚本而立的 TODO | 经负责人核对确认无遗留边角价值后，在原文标注 Superseded by Phase 13 Architecture 并整体移入历史归档目录 | 归口顶层四层分离重构与需求 5、8、12 |
| Q4：非核心主路径的远期演进项（Defer to Phase 14） | 非本阶段必需的扩展需求（如额外非主流实验滤镜、额外动画特效或远端协作） | 统一摘录并迁移至 Phase 14 路线池，正式清空 Phase 13 docs/TODO 中的历史积压列表 | 归口项目卫生与迭代治理（需求 12） |

## **四、Phase 13 分阶段演进路线图与验收基线**

为防止大规模架构演进滑向“长期大改而无法运行验证”的风险，整个 Phase 13 严格拆分为四个顺次递进、每阶段均可独立运行与自动化验收的工程里程碑：

> 1. **里程碑 M1（卫生治理、排雷与呈现通路点亮）**：接入本地仓库后先执行需求 12 的构建矩阵与开关扫描、以及需求 13 的 docs/TODO 四象限清零；同步落地需求 2（解耦无消息泵测试窗口、拦截 WM\_SETCURSOR 隐式 MessageBeep 与清零 0x07/\\a 泄漏，确认测试套全程零蜂鸣）和需求 3（重建基于 D3D12\_HEAP\_FLAG\_SHARED、共享 Fence 与 DXGI Waitable Object 的呈现桥，恢复转码预览上屏），实现基础运行底座止血。  
> 2. **里程碑 M2（核心媒体计算内核进化）**：交付支持横屏/竖屏/方屏多 Profile 动态维度与 64/128 边缘镜像对齐填充及防 OOM 自动分块的 RIFE 推理引擎（需求 1）；上线 SceneCutGuard CUDA 前置切镜探测、NVOFA 历史提示复位与前后向一致性校准（需求 11），经高反差硬切与黑场素材盲测验证光流果冻伪影清零；完成全主流容器解复用、固定 faststart MP4 封装及 SVT-AV1/NVENC/合规隔离 GPL 软编码的 CRF 接口贯通（需求 7）。  
> 3. **里程碑 M3（真 60 fps 播放器与异构编码队列合流）**：在 M2 同一 GPU 流水线底座之上，正式上线由 WASAPI 音频时钟牵引、集成有理数帧率节拍、视口代理降载与过期旁路熔断的 60 fps PlaybackSession（需求 4）；同期上线以 EncodeJobSpec 为中心、支持运行期动态安全编辑与三路零漂移拼接合并的 EncodeQueueManager（需求 8），并挂接真正零拷贝短路省电的 preview\_enabled CheckBox 门控（需求 9）。  
> 4. **里程碑 M4（DirectWrite 组件库、i18n 与拖拽管线终态合龙）**：将全界面静态与动态文本切换至 DirectWrite IDWriteTextLayout 两阶段自适应布局（需求 5），接入 UTF-8 多语种语言包热切换并完成含空格与多语种路径全链路压力测试（需求 6），交付带显存/计算量静态推导提示的横排拖拽式管线工作流编辑器（需求 10）。当工程在端到端覆盖“异构多格式混拼”、“竖屏/超宽屏 RIFE”、“光流硬切零果冻”、“60 fps 持续播放不降音画同步且零测试蜂鸣”以及“关闭预览 0% 额外 GPU 开销”五项基线回归中全部达标后，即可完成 CineRTX Phase 13 正式封版交付。

#### **引用的著作**

> 1. [unknown\_url]([REDACTED_ID])  
> 2. Releases · NevermindNilas/TheAnimeScripter \- GitHub, [https\://github.com/NevermindNilas/TheAnimeScripter/releases/](https://github.com/NevermindNilas/TheAnimeScripter/releases/)  
> 3. Daily Papers \- Hugging Face, [https\://huggingface.co/papers?q=Realtime-Bench](https://huggingface.co/papers?q=Realtime-Bench)  
> 4. leonardoventurini/RIFE-v4.9-mlx \- Hugging Face, [https\://huggingface.co/leonardoventurini/RIFE-v4.9-mlx](https://huggingface.co/leonardoventurini/RIFE-v4.9-mlx)  
> 5. Mirror RIFE v4.25 from Practical-RIFE · daydreamlive/RIFE at 52c1970, [https\://huggingface.co/daydreamlive/RIFE/commit/52c19702a5c82337d4dad5410c323964605cbb8e](https://huggingface.co/daydreamlive/RIFE/commit/52c19702a5c82337d4dad5410c323964605cbb8e)  
> 6. RIFE — PotionUI documentation, [https\://potionui.com/docs/models/rife](https://potionui.com/docs/models/rife)  
> 7. Phase-Resolved Isotropic Subtraction Mapping for Automated Multi, [https\://www\.medrxiv.org/content/10.64898/2026.09.10.26362169.full.pdf](https://www.medrxiv.org/content/10.64898/2026.09.10.26362169.full.pdf)  
> 8. replicating benchmarks · Issue \#1 · hzwer/ECCV2022-RIFE \- GitHub, [https\://github.com/hzwer/ECCV2022-RIFE/issues/1](https://github.com/hzwer/ECCV2022-RIFE/issues/1)  
> 9. RIFE AI interpolation \- SVP – SmoothVideo Project, [https\://www\.svp-team.com/docs/rife-ai/](https://www.svp-team.com/docs/rife-ai/)  
> 10. Dynamic Shapes: Core Concepts — NVIDIA TensorRT for RTX, [https\://docs.nvidia.com/deeplearning/tensorrt-rtx/latest/inference-library/dynamic-shapes-basics.html](https://docs.nvidia.com/deeplearning/tensorrt-rtx/latest/inference-library/dynamic-shapes-basics.html)  
> 11. Working with Dynamic Shapes — NVIDIA TensorRT, [https\://docs.nvidia.com/deeplearning/tensorrt/latest/inference-library/work-with-dynamic-shapes.html](https://docs.nvidia.com/deeplearning/tensorrt/latest/inference-library/work-with-dynamic-shapes.html)  
> 12. TensorRT set\_optimization\_profile\_async takes a lot of time \#1943, [https\://github.com/NVIDIA/TensorRT/issues/1943](https://github.com/NVIDIA/TensorRT/issues/1943)  
> 13. ComfyUI-RH-DLSS5 detailed guide \- RunComfy, [https\://www\.runcomfy.com/comfyui-nodes/ComfyUI-RH-DLSS5](https://www.runcomfy.com/comfyui-nodes/ComfyUI-RH-DLSS5)  
> 14. SU14 and Reprojection Mode: What's really going on?, [https\://forums.flightsimulator.com/t/su14-and-reprojection-mode-whats-really-going-on/627048](https://forums.flightsimulator.com/t/su14-and-reprojection-mode-whats-really-going-on/627048)  
> 15. Harnessing the NVIDIA Ada Architecture for Frame-Rate Up, [https\://developer.nvidia.com/blog/harnessing-the-nvidia-ada-architecture-for-frame-rate-up-conversion-in-the-nvidia-optical-flow-sdk/](https://developer.nvidia.com/blog/harnessing-the-nvidia-ada-architecture-for-frame-rate-up-conversion-in-the-nvidia-optical-flow-sdk/)  
> 16. An Introduction to the NVIDIA Optical Flow SDK, [https\://developer.nvidia.com/blog/an-introduction-to-the-nvidia-optical-flow-sdk/](https://developer.nvidia.com/blog/an-introduction-to-the-nvidia-optical-flow-sdk/)  
> 17. NVOFA Programming Guide \- NVIDIA Docs, [https\://docs.nvidia.com/video-technologies/optical-flow-sdk/nvofa-programming-guide/index.html](https://docs.nvidia.com/video-technologies/optical-flow-sdk/nvofa-programming-guide/index.html)  
> 18. NVIDIA Optical Flow \- VapourSynth plugin (Motion compensation), [https\://forums.developer.nvidia.com/t/nvidia-optical-flow-vapoursynth-plugin-motion-compensation/218981](https://forums.developer.nvidia.com/t/nvidia-optical-flow-vapoursynth-plugin-motion-compensation/218981)  
> 19. CUDA interop with D3D12 possible? \- NVIDIA Developer Forums, [https\://forums.developer.nvidia.com/t/cuda-interop-with-d3d12-possible/54671](https://forums.developer.nvidia.com/t/cuda-interop-with-d3d12-possible/54671)  
> 20. Graphics API Interoperability \| NVIDIA/cuda-samples \- DeepWiki, [https\://deepwiki.com/NVIDIA/cuda-samples/8-graphics-api-interoperability](https://deepwiki.com/NVIDIA/cuda-samples/8-graphics-api-interoperability)  
> 21. Direct3D 12 interop \- Win32 apps \- Microsoft Learn, [https\://learn.microsoft.com/en-us/windows/win32/direct3d12/direct3d-12-with-direct3d-11--direct-2d-and-gdi](https://learn.microsoft.com/en-us/windows/win32/direct3d12/direct3d-12-with-direct3d-11--direct-2d-and-gdi)  
> 22. Problem importing small committed resources from D3D12, [https\://forums.developer.nvidia.com/t/problem-importing-small-committed-resources-from-d3d12/297867](https://forums.developer.nvidia.com/t/problem-importing-small-committed-resources-from-d3d12/297867)  
> 23. Driver Crash when creating Root Signature for D3D12 resource with, [https\://forums.developer.nvidia.com/t/driver-crash-when-creating-root-signature-for-d3d12-resource-with-shared-heap-in-unreal-engine-4/79582](https://forums.developer.nvidia.com/t/driver-crash-when-creating-root-signature-for-d3d12-resource-with-shared-heap-in-unreal-engine-4/79582)  
> 24. cuda \- directx 12 texture2D (in 1D array) interop \- Stack Overflow, [https\://stackoverflow.com/questions/58626926/cuda-directx-12-texture2d-in-1d-array-interop](https://stackoverflow.com/questions/58626926/cuda-directx-12-texture2d-in-1d-array-interop)  
> 25. IDXGISwapChain2::GetFrameLatencyWaitableObject (dxgi1\_3.h), [https\://learn.microsoft.com/en-us/windows/win32/api/dxgi1\_3/nf-dxgi1\_3-idxgiswapchain2-getframelatencywaitableobject](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_3/nf-dxgi1_3-idxgiswapchain2-getframelatencywaitableobject)  
> 26. DXGI Waitable SwapChain not waiting (D3D11) \- Stack Overflow, [https\://stackoverflow.com/questions/66676804/dxgi-waitable-swapchain-not-waiting-d3d11](https://stackoverflow.com/questions/66676804/dxgi-waitable-swapchain-not-waiting-d3d11)  
> 27. Swapchains and frame pacing \| Raph Levien's blog, [https\://raphlinus.github.io/ui/graphics/gpu/2021/10/22/swapchain-frame-pacing.html](https://raphlinus.github.io/ui/graphics/gpu/2021/10/22/swapchain-frame-pacing.html)  
> 28. Unknown beeps \| Windows 11 Forum, [https\://www\.elevenforum.com/t/unknown-beeps.51175/](https://www.elevenforum.com/t/unknown-beeps.51175/)  
> 29. \[Windows\] ChatGPT/Codex desktop app causes system-wide mouse, [https\://github.com/openai/codex/issues/38546](https://github.com/openai/codex/issues/38546)  
> 30. Why is my mouse malfunction with a beeping noise? \- Super User, [https\://superuser.com/questions/849517/why-is-my-mouse-malfunction-with-a-beeping-noise](https://superuser.com/questions/849517/why-is-my-mouse-malfunction-with-a-beeping-noise)  
> 31. Why is my PC doing this "boop" sound when I scroll very quickly with, [https\://www\.reddit.com/r/G502MasterRace/comments/dr24v8/why\_is\_my\_pc\_doing\_this\_boop\_sound\_when\_i\_scroll/](https://www.reddit.com/r/G502MasterRace/comments/dr24v8/why_is_my_pc_doing_this_boop_sound_when_i_scroll/)  
> 32. Very fast mouse scrolling causes a beeping sound and mouse lag \#8, [https\://github.com/suurjaak/InputScope/issues/8](https://github.com/suurjaak/InputScope/issues/8)  
> 33. Random unidentifiable beeping sound without any particular action, [https\://www\.reddit.com/r/techsupport/comments/1twpntj/random\_unidentifiable\_beeping\_sound\_without\_any/](https://www.reddit.com/r/techsupport/comments/1twpntj/random_unidentifiable_beeping_sound_without_any/)  
> 34. How to programatically hinder Windows from playing "Default Beep, [https\://stackoverflow.com/questions/64041296/how-to-programatically-hinder-windows-from-playing-default-beep-sound-when-use](https://stackoverflow.com/questions/64041296/how-to-programatically-hinder-windows-from-playing-default-beep-sound-when-use)  
> 35. WM\_NCHITTEST message (Winuser.h) \- Win32 apps \| Microsoft Learn, [https\://learn.microsoft.com/en-us/windows/win32/inputdev/wm-nchittest](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-nchittest)  
> 36. Using Keyboard Input \- Win32 apps \| Microsoft Learn, [https\://learn.microsoft.com/en-us/windows/win32/inputdev/using-keyboard-input](https://learn.microsoft.com/en-us/windows/win32/inputdev/using-keyboard-input)  
> 37. Windows 10 Error Sound: Change, Mute or Stop Random Dings, [https\://hypestkey.com/windows-10-error-sound/](https://hypestkey.com/windows-10-error-sound/)  
> 38. \\a in Python: ASCII Bell (Alert) Character \| MojoAuth Dev Guides, [https\://mojoauth.com/dev-guides/alert-bell-a-in-python](https://mojoauth.com/dev-guides/alert-bell-a-in-python)  
> 39. Alert Bell (\\a) Escape Character in C\# \| MojoAuth Dev Guides, [https\://mojoauth.com/dev-guides/alert-bell-a-in-c](https://mojoauth.com/dev-guides/alert-bell-a-in-c)  
> 40. Beep sound in C : r/learnprogramming \- Reddit, [https\://www\.reddit.com/r/learnprogramming/comments/1njjh59/beep\_sound\_in\_c/](https://www.reddit.com/r/learnprogramming/comments/1njjh59/beep_sound_in_c/)  
> 41. Support Dark and Light themes in Win32 apps \- Microsoft Learn, [https\://learn.microsoft.com/en-us/windows/apps/desktop/modernize/ui/apply-windows-themes](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/ui/apply-windows-themes)  
> 42. Supporting dark mode for MFC dialog application ... \- Stack Overflow, [https\://stackoverflow.com/questions/75087553/supporting-dark-mode-for-mfc-dialog-application-dwmsetwindowattribute](https://stackoverflow.com/questions/75087553/supporting-dark-mode-for-mfc-dialog-application-dwmsetwindowattribute)  
> 43. [https\://wxwidgets.org/wxWidgets/src/msw/darkmode.cpp](https://wxwidgets.org/wxWidgets/src/msw/darkmode.cpp)  
> 44. Introduction to DirectWrite \| CodeGuru, [https\://www\.codeguru.com/cplusplus/introduction-to-directwrite/](https://www.codeguru.com/cplusplus/introduction-to-directwrite/)  
> 45. Introduction to Direct 2D: DirectWrite \- j2i.net, [https\://blog.j2i.net/2020/04/25/introduction-to-direct-2d-directwrite/](https://blog.j2i.net/2020/04/25/introduction-to-direct-2d-directwrite/)  
> 46. IDWriteTextLayout interface (dwrite.h) \- Win32 apps \- Microsoft Learn, [https\://learn.microsoft.com/en-us/windows/win32/api/dwrite/nn-dwrite-idwritetextlayout](https://learn.microsoft.com/en-us/windows/win32/api/dwrite/nn-dwrite-idwritetextlayout)  
> 47. Introducing DirectWrite \- Win32 apps \| Microsoft Learn, [https\://learn.microsoft.com/en-us/windows/win32/directwrite/introducing-directwrite](https://learn.microsoft.com/en-us/windows/win32/directwrite/introducing-directwrite)  
> 48. win32/desktop-src/DirectWrite/text-formatting-and-layout.md at docs, [https\://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/DirectWrite/text-formatting-and-layout.md](https://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/DirectWrite/text-formatting-and-layout.md)  
> 49. Merge Videos with FFmpeg: Concat Demuxer, Filter & Protocol, [https\://www\.ffmpeg-micro.com/blog/ffmpeg-concat-merge-videos](https://www.ffmpeg-micro.com/blog/ffmpeg-concat-merge-videos)  
> 50. FFmpeg Join Videos — concat Demuxer vs Filter, [https\://ffmpeg-cookbook.com/en/articles/concat-video/](https://ffmpeg-cookbook.com/en/articles/concat-video/)  
> 51. r/ffmpeg on Reddit: How to Merge / Joining Multiple Videos with, [https\://www\.reddit.com/r/ffmpeg/comments/1bhkc6u/how\_to\_merge\_joining\_multiple\_videos\_with/](https://www.reddit.com/r/ffmpeg/comments/1bhkc6u/how_to_merge_joining_multiple_videos_with/)  
> 52. How to concatenate videos in ffmpeg with different attributes?, [https\://stackoverflow.com/questions/57366845/how-to-concatenate-videos-in-ffmpeg-with-different-attributes](https://stackoverflow.com/questions/57366845/how-to-concatenate-videos-in-ffmpeg-with-different-attributes)  
> 53. FFmpeg Concat Made Easy: 3 Ways to Combine Videos with FFmpeg, [https\://cloudinary.com/guides/video-effects/ffmpeg-concat](https://cloudinary.com/guides/video-effects/ffmpeg-concat)  
> 54. How to concatenate two or more videos with different dimensions in, [https\://superuser.com/questions/1136822/how-to-concatenate-two-or-more-videos-with-different-dimensions-in-ffmpeg](https://superuser.com/questions/1136822/how-to-concatenate-two-or-more-videos-with-different-dimensions-in-ffmpeg)  
> 55. Match audio and video duration before ffmpeg-concat \#93 \- GitHub, [https\://github.com/transitive-bullshit/ffmpeg-concat/issues/93](https://github.com/transitive-bullshit/ffmpeg-concat/issues/93)  
> 56. ffmpeg \- vcpkg package, [https\://vcpkg.io/en/package/ffmpeg.html](https://vcpkg.io/en/package/ffmpeg.html)  
> 57. FFmpeg-Builds-SVT-AV1-Essential \- nekotrix \- GitHub, [https\://github.com/nekotrix/FFmpeg-Builds-SVT-AV1-Essential](https://github.com/nekotrix/FFmpeg-Builds-SVT-AV1-Essential)  
> 58. Licenses — System Stacks for Linux\* OS documentation, [https\://intel.github.io/stacks/mers/LICENSES.html](https://intel.github.io/stacks/mers/LICENSES.html)  
> 59. selkies/docs/licensing.md at main \- GitHub, [https\://github.com/selkies-project/selkies/blob/main/docs/licensing.md](https://github.com/selkies-project/selkies/blob/main/docs/licensing.md)  
> 60. Download \- Hyundai Worldwide, [https\://www\.hyundai.com/wsvc/ww/download.file.do?id=/content/hyundai/ww/data/opensource/data/BJ1-2026/licenseCode/info](https://www.hyundai.com/wsvc/ww/download.file.do?id=/content/hyundai/ww/data/opensource/data/BJ1-2026/licenseCode/info)  
> 61. NR-SW6100, [http\://www\.mitsubishielectric.co.jp/nwcamera/support/pdf/operate/NR-SW6100\_operate.pdf](http://www.mitsubishielectric.co.jp/nwcamera/support/pdf/operate/NR-SW6100_operate.pdf)  
> 62. Build FFmpeg from Source with SVT-AV1, EVC, x265, fdk-aac and, [https\://ercan.dev/blog/notes/build-ffmpeg-from-source-on-macos](https://ercan.dev/blog/notes/build-ffmpeg-from-source-on-macos)  
> 63. FFmpeg Codecs Documentation, [https\://ffmpeg.org/ffmpeg-codecs.html](https://ffmpeg.org/ffmpeg-codecs.html)  
> 64. \[Libav-user\] statically linking x264 and x265 into ffmpeg, [https\://libav-user.ffmpeg.narkive.com/7kX7gm8f/statically-linking-x264-and-x265-into-ffmpeg](https://libav-user.ffmpeg.narkive.com/7kX7gm8f/statically-linking-x264-and-x265-into-ffmpeg)  
> 65. \[ffmpeg\] Dependencies Secur32.lib and Bcrypt.lib are missing when, [https\://github.com/microsoft/vcpkg/issues/9571](https://github.com/microsoft/vcpkg/issues/9571)  
> 66. Can I statically link ws2\_32.lib in windows? c++ MFC \- Stack Overflow, [https\://stackoverflow.com/questions/35503185/can-i-statically-link-ws2-32-lib-in-windows-c-mfc](https://stackoverflow.com/questions/35503185/can-i-statically-link-ws2-32-lib-in-windows-c-mfc)  
> 67. Platform Specific Information \- FFmpeg, [https\://www\.ffmpeg.org/platform.html](https://www.ffmpeg.org/platform.html)  
> 68. FFmpeg/configure at master \- GitHub, [https\://github.com/FFmpeg/FFmpeg/blob/master/configure](https://github.com/FFmpeg/FFmpeg/blob/master/configure)  
> 69. A list of all the configuration options available when ... \- GitHub Gist, [https\://gist.github.com/omegdadi/6904512c0a948225c81114b1c5acb875](https://gist.github.com/omegdadi/6904512c0a948225c81114b1c5acb875)  
> 70. configure \- platform/external/ffmpeg \- Git at Google, [https\://android.googlesource.com/platform/external/ffmpeg/+/refs/heads/upstream-release-3.2/configure](https://android.googlesource.com/platform/external/ffmpeg/+/refs/heads/upstream-release-3.2/configure)  
> 71. ffmpeg编译时configure选项(Options) \- kaisawind's blog, [https\://www\.kaisawind.com/post/2019-11-19-ffmpeg-options/](https://www.kaisawind.com/post/2019-11-19-ffmpeg-options/)  
> 72. How do you disable FFmpeg from accessing remote files over HTTP, [https\://stackoverflow.com/questions/69681550/how-do-you-disable-ffmpeg-from-accessing-remote-files-over-http-https](https://stackoverflow.com/questions/69681550/how-do-you-disable-ffmpeg-from-accessing-remote-files-over-http-https)  
> 73. Show HN: Beautiful Type Erasure with C++26 Reflection, [https\://news.ycombinator.com/item?id=48905914](https://news.ycombinator.com/item?id=48905914)  
> 74. Tutorial Getting Started with DirectWrite \- Win32 apps \- Microsoft Learn, [https\://learn.microsoft.com/en-us/windows/win32/directwrite/getting-started-with-directwrite](https://learn.microsoft.com/en-us/windows/win32/directwrite/getting-started-with-directwrite)  
> 75. Build Portable FFmpeg Binaries with ffmpeg-builder \| Alfred Gutierrez, [https\://alfg.dev/blog/ffmpeg-builder](https://alfg.dev/blog/ffmpeg-builder)