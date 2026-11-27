# **CineRTX Phase 13 基线 0beda87 极限重构验收报告与 Wave B-D 演进方案**

基于极端 Modern C++（C++20/23/26）工程洁癖、奥卡姆剃刀原则与单可执行文件架构铁律，对《69\_CineRTX Phase 13 基于最新基线(0beda87)重编 Wave B-D 指导书委托书》及基线 0beda87 状态的最终验收裁决为：**核心音视频与渲染底座通过，结构卫生与分发形态门禁驳回，必须先行实施 Wave 0 架构清洗后再按重编方案推进 Wave B 至 Wave D**。基线 0beda87 已在 MSVC 19.51 /W4 /WX 下达成零错误零警告编译与 rtx\_tests.exe 157/157 PASS，并完成了 12-DWORD 双侧色彩契约、DirectComposition 矩形硬裁剪、rife\_stream\_ 非阻塞流隔离、RIFE FG-First 几何锁定、PTX param\_10 对齐及即拖即入队等高难度底层收口1。然而在架构卫生层面，现状仍存在不可容忍的违规项：残存违背单二进制铁律的 rtx\_headless\_cli 独立产物、静态链接可剔除的系统与加密 DLL（涉及 {gdi32.dll, user32.dll, bcrypt.dll, kernel32.dll, kernelbase.dll} 五元清单）、cmake/RtxFfmpegStatic.cmake 夹带预留探测与额外编译选项及失步的 Zig 交叉分支、UI 层残留 chromeText() 私藏词条表，以及 AuditTypes 占用至 34/35 却仍拟将最后 1 个名额浪费在保留模式面向对象节点类 UiWidgetNode 上1。  
本报告的所有基线实测指标（编译状态、门禁计数、代码行号与缺口描述）均严格采信《69\_指导书委托书》披露的宿主机只读核验事实1，编译器与标准库特性依据 Microsoft 官方 C++ 团队博客及 microsoft/STL 变更日志（MSVC Build Tools 14.51 / Visual Studio 2026 18.6）核定2。由于工作区底层 .cpp/.h 完整源码与宿主机实际 dumpbin /imports 符号表在当前审阅环境下不可直接遍历6，针对存量 34 个自定义类型的逐一清退及各目标二进制内部触发特定 DLL 导入的具体符号调用点，本方案均给出宿主机确定性核验清单与零猜测的根除路径。

## **一、 基线 0beda87 与委托书红线验收裁决**

0beda87 的工程现状呈现出显著的“核心算子高度精良、外围构建与 C++ 抽象层历史包袱沉重”的非对称特征。原委托书敏锐识别了 32\_–34\_ 旧工单因接口演进而产生的假设过期问题1，却对多 .exe 并存、冗余 DLL、条件开关与类型配额耗尽等问题选择了顺从妥协，未能从架构源头落实“能用标准库则绝不自定义类、能无条件直连则绝不设开关”的极简设计哲学。下表给出各维度的正式验收裁决与强制整改动作。

| 验收维度 | 0beda87 现状与原委托书假设 | 极限重构验收结论 | 强制整改动作 |
| :---- | :---- | :---- | :---- |
| 可执行文件形态 | 存在 rtx\_headless\_cli（对应 AuditImports 独立 HEADLESS 产物）与 GUI 主程序1 | 驳回（违反绝对红线） | 彻底拔除 rtx\_headless\_cli 构建目标与发布脚本，收拢为单一 /SUBSYSTEM:WINDOWS 主 .exe |
| /MT 静态导入 DLL | AuditImports 报 GUI 4 个物理 DLL / HEADLESS 2 个物理 DLL1；现状集合为 {gdi32, user32, bcrypt, kernel32, kernelbase} | 驳回（含可移除导入） | 彻底根除 bcrypt.dll 与 gdi32.dll；从 PE 静态导入表剥离 user32.dll；清查显式 kernelbase.dll 诱因 |
| CMake 与额外选项 | RtxFfmpegStatic.cmake 含 if \[\[ \-f ... \]\] 条件探测、CINERTX\_ENABLE\_GPL\_CODECS 门控与遗漏 mov\_text 的 Zig 分支1 | 驳回（隐蔽开关与死代码） | 拔除预留探测、门控开关与未受测 Zig 分支；统一无条件确定性静态构建清单 |
| 自定义类型配额 | AuditTypes 已占 34/35，拟将第 35 个名额用于 Wave D 的 UiWidgetNode1 | 驳回（面向类型膨胀） | 否决 UiWidgetNode；Wave B–D 严格保持 \+0 具名类型；Wave 0 启动存量 34 类全面清退 |
| 词条表与门控设计 | workstation\_canvas.cpp:L207-L282 保留 chromeText() 私藏词条表；拟增零算力预览门控开关1 | 驳回 chromeText() | 整段删除 chromeText() 并内联编译期字面量；禁止隐藏门控开关，改为显式管线旁路状态 |
| 核心渲染与并发底座 | 157/157 PASS、AuditPrivacy 0 违规、AuditSpdx PASS (GPL=114 MIT=39)、12-DWORD 色彩契约、rife\_stream\_、FG-First1 | 通过（锁定为不可回退基线） | Wave B–D 严格对接 0beda87 新契约推进，严禁接口回退 |

## **二、 绝对红线拔除方案：单可执行文件归位与 /MT 极限去 DLL**

### **2.1 彻底拔除 rtx\_headless\_cli 并收拢为单可执行文件**

独立无头可执行文件 rtx\_headless\_cli 的存在是项目不可妥协的规格违约：它不仅造成双份二进制分发体积与双重 AuditImports 审计成本，更在无形中诱导业务层分化出两套初始化与调度状态机。在极限重构下，必须从 CMakeLists.txt、打包流程与测试前置依赖中彻底删除 rtx\_headless\_cli 目标，而 rtx\_tests.exe 明确仅作为构建期与 CI 验证夹具，不进入任何交付制品目录。  
将无头批处理与交互界面统一收拢至单一 /SUBSYSTEM:WINDOWS 可执行文件后，程序入口 wWinMain 在触及任何 DirectComposition、窗口消息或图形组件动态加载前，先以 std::span 承载命令行参数视图完成模式分流：当检测到 \--headless 或批处理子命令时，直接进入纯计算与转码管线并跳过全部 GUI 初始化。针对 /SUBSYSTEM:WINDOWS 程序在终端或 CI 管道中执行无头任务的输出需求8，严禁额外生成 .com 控制台伴随文件（这会变相恢复双二进制），也严禁粗暴调用 AllocConsole() 闪烁弹出多余黑框9。唯一正统的 Win32 处置流程是在无头分流后先调用 GetStdHandle(STD\_OUTPUT\_HANDLE) 与 GetStdHandle(STD\_ERROR\_HANDLE) 并通过 GetFileType() 探测句柄属性：若父进程或 Shell 已通过管道或文件重定向传入有效句柄（FILE\_TYPE\_PIPE 或 FILE\_TYPE\_DISK），则保留该有效重定向句柄而不破坏管道数据流10；仅当标准句柄无效（如 FILE\_TYPE\_UNKNOWN）且存在交互式父控制台时，才调用 AttachConsole(ATTACH\_PARENT\_PROCESS) 并以 freopen\_s 将标准流重绑至 "CONOUT$"/"CONIN$"10。需如实向使用者声明一项 Windows 内核固有行为：在交互式 cmd.exe 中直接运行 /SUBSYSTEM:WINDOWS 程序时，cmd.exe 默认不等待进程退出即刻返回提示符，脚本自动化与 CI 调用须通过管道重定向或 start /wait 同步等待，绝不为此引入多余进程包装10。

### **2.2 五大 DLL 逐项手术级剖析与剥离路线**

0beda87 的 AuditImports 数据显示 GUI 产物引入 4 个物理 DLL，HEADLESS 产物引入 2 个物理 DLL1。由于当前审阅环境无法直接读取宿主机 dumpbin /imports 原始节表6，各 DLL 在两个原有产物中的精确分布比例尚待宿主机二次确认；但结合用户盘点的五元集合 {gdi32.dll, user32.dll, bcrypt.dll, kernel32.dll, kernelbase.dll} 以及 Windows /MT 静态运行时链接规律，这五项 DLL 的技术归因与剥离边界已完全清晰，必须逐项执行不同烈度的外科手术。

| 目标 DLL | 典型静态引入诱因 | 是否可移除 | 极限清理方案与边界界定 |
| :---- | :---- | :---- | :---- |
| bcrypt.dll | FFmpeg 在 Windows 默认激活 HAVE\_BCRYPT 并于 libavutil/random\_seed.c 调用 BCryptGenRandom14；或 MSVC STL std::random\_device 触发 BCryptGenRandom17 | 完全可移除 | 清除 FFmpeg HAVE\_BCRYPT 与 bcrypt 系统库链接14；清理非密码学 std::random\_device 调用，实现静态与动态双 0 加载 |
| gdi32.dll | 旧版窗口绘制、字体位图操作或 BMP 导出路径静态引用 GDI 接口 | 完全可移除 | 移除 gdi32.lib；全量改用按需加载的 D2D/DWrite 与纯标准流写 BMP 头，实现静态与动态双 0 加载 |
| user32.dll | GUI 创建窗口、处理消息循环与剪贴板/拖放逻辑静态链接 user32.lib20 | 静态表可清零 | 移除 user32.lib；在进入交互 GUI 分支时以 LoadLibraryExW（带 LOAD\_LIBRARY\_SEARCH\_SYSTEM32）按需解析，无头模式 0 加载 |
| kernelbase.dll | 显式调用 PathCch\*（Pathcch.lib）21、WaitOnAddress（api-ms-win-core-synch-l1-2-0.dll）22，或 /MT UCRT API-Set 内核映射22 | 条件可移除 | 彻底清除 PathCch\* 与显式同步库引用；若 dumpbin 验证仅存 UCRT API-Set 转发，则如实界定为 OS 不可减地板 |
| kernel32.dll | /MT 静态 CRT 线程、堆、文件 I/O、控制台重定向与模块加载根基20 | 不可移除 | 保留作为 Standard C++ 与 Win32 最低运行时底座；绝不采用 /NODEFAULTLIB 裸调 ntdll 等高危反 KISS 方案 |

针对 bcrypt.dll，必须同时从 FFmpeg 第三方静态库与 C++ 主工程两路封堵。根据公开 FFmpeg 源码镜像与 OSS-Fuzz 覆盖分析，libavutil/random\_seed.c 长期通过 \#if HAVE\_BCRYPT 调用 BCryptOpenAlgorithmProvider 与 BCryptGenRandom15，且社区 Windows 编译实录显示 FFmpeg 的 configure 脚本在历史版本中会自动将 bcrypt 注入 SYSTEM\_LIBRARIES 而未必提供开箱即用的关闭选项14。因此在 cmake/RtxFfmpegStatic.cmake 中切忌盲目假设单一参数有效：应先核查仓库锁定的 FFmpeg configure 是否原生支持关闭开关，若不支持则直接在构建期对 configure 的 SYSTEM\_LIBRARIES 与生成的 config.h 实施确定性补丁，强制锁定 HAVE\_BCRYPT=0 并剔除 bcrypt 链接库，从源头切断 FFmpeg 静态库的 bcrypt.dll 依赖14。与此同时，社区对现代 MSVC STL std::random\_device 的实测反馈指出其在 Windows 上通常调用 BCryptGenRandom（早期曾用 SystemFunction036）17；鉴于视频处理调度与 UI 状态机不存在密码学熵源需求，凡工程内为生成任务流水号、唯一令牌或采样抖动而引入 std::random\_device 之处，应全部替换为 std::atomic\<std::uint64\_t\> 单调序列或以 std::chrono::steady\_clock 纳秒时间戳、线程哈希与 GetCurrentProcessId() 混合定种的 std::mt19937\_64，并由 dumpbin /imports 逐符号验收零 BCryptGenRandom 残留24。  
针对 gdi32.dll 与 user32.dll，坚决否决 /DELAYLOAD 折中方案。尽管在 MSVC 中配合 delayimp.lib 传递 /DELAYLOAD:user32.dll 可把引用从常规导入表迁移至 IMAGE\_DIRECTORY\_ENTRY\_DELAY\_IMPORT27，但这实质上只是把静态依赖转入延迟解析目录，既违反“禁止额外链接/编译选项”的铁律，也未能清除冗余辅助代码。在极致方案中，gdi32.dll 通过 Wave D 全量切换至 Direct2D / DirectWrite 并重构 writeWebuiPreviewBmpIfDue()（使用 std::array\<std::byte, 54\> 在栈上无分配组装标准 BMP 文件头并经 std::ofstream 写入磁盘）实现彻底消亡，直接从链接列表移除 gdi32.lib。对于 GUI 运行态必需的 user32.dll20，同样从链接依赖移除 user32.lib，仅在单可执行文件确认为 GUI 交互启动且非 \--headless 时，在唯一窗口启动入口通过 LoadLibraryExW(L"user32.dll", nullptr, LOAD\_LIBRARY\_SEARCH\_SYSTEM32) 一次性取得所需十余个窗口函数指针，再交由 std::unique\_ptr 自定义删除器托管 HMODULE。此举无需任何额外链接选项与自定义封装类，既使 PE 静态导入表中 user32.dll 彻底清零，又确保无头路径绝不污染进程地址空间。同理，Wave C 所需的 WASAPI 组件（ole32.dll/mmdevapi.dll/avrt.dll）及 Wave D 所需的 d2d1.dll/dwrite.dll 全部强制遵循同一 LoadLibraryExW(..., LOAD\_LIBRARY\_SEARCH\_SYSTEM32) 规范，杜绝任何静态导入反弹。  
针对 kernelbase.dll，必须在宿主机通过 dumpbin /imports 辨明其进入 PE 导入表的真实路径24。Windows 开发者文档与技术资料表明，显式使用 PathCchAppend 等函数（链接 Pathcch.lib）21 或显式引用 WaitOnAddress / WakeByAddressAll（经由 api-ms-win-core-synch-l1-2-0.dll 同步扩展集）22，均会直接诱发 kernelbase.dll 导入。凡属此类显式 API 引用，全部无条件替换为标准库 std::filesystem::path 与基于 std::mutex/std::condition\_variable 的标准同步原语。若清除一切显式引用后，AuditImports 仍因 Windows 10/11 底层将 UCRT 与内核 API-Set 重定向至系统核心宿主 kernelbase.dll（构成 kernel32.dll \-\> kernelbase.dll \-\> ntdll.dll 系统分层22）而计入 1 个物理映射，则绝不允许为追求表面数字“1”而绕行 /MT 标准库手写 ntdll 原生系统调用；此时静态导入物理地板客观收敛为纯 kernel32.dll（1 个）或 kernel32.dll 与内核 API-Set 宿主 kernelbase.dll（2 个）。

## **三、 极致的 Modern C++ 架构洁癖与冗余铲除**

### **3.1 MSVC 19.51 工具链实证能力边界与编译选项收敛**

基线已明确锁定使用 **MSVC 19.51** 编译器1，对应 Visual Studio 2026 18.6 所搭载的 **MSVC Build Tools 14.51**（如 cl 19.51.36260.0 / 工具集 14.51.36231）4。全仓构建策略必须收紧为唯一全局标准标志 /std:c++latest（注：据 Microsoft 官方说明，MSVC 14.51 的正式 /std:c++23 开关需待 14.52 补齐 P2564R3 consteval 传播与 P0533R9 constexpr \<cmath\> 后方才启用，当前 /std:c++23preview 仅覆盖至 C++23 预览，而 /std:c++latest 已囊括 C++23 特性与最新实验特性2），严禁按目标或源文件私设第二套语言标准或宏开关。  
在语言与标准库能力应用上，必须严格区分“已由 14.51 官方实证的特性”与“须按标准特性测试宏核验的特性”。Microsoft C++ 团队博客与 microsoft/STL 官方变更记录明确证实 MSVC Build Tools 14.51 已全面实装：C++23 有序扁平关联容器 \<flat\_map\>（P0429R9）与 \<flat\_set\>（P1222R4、P3567R2）2、可移植优化提示属性 \[\[assume\]\]（P1774R8）、constexpr 函数局部 static constexpr（P2647R1）、放宽 constexpr 约束（P2448R2）、显式生命周期管理 std::start\_lifetime\_as / std::start\_lifetime\_as\_array（P2590R2/P2679R2）、std::is\_implicit\_lifetime（P2674R1）及 4 项作为缺陷报告合入的 C++26 库提案（P3016R6 初始化列表 begin/end 一致性、P3349R1 连续迭代器转指针、P3503R3 promise/packaged\_task 分配器类型擦除、P3612R1 代理引用操作统一）2；同时 14.51 彻底删除了 TR1、\<hash\_map\>、\<experimental/filesystem\> 与 stdext::checked\_array\_iterator 等历史遗留，并对 \<regex\> 与 STL SIMD 矢量化算法完成底层重构2。另一方面，针对 std::move\_only\_function（开发博客指出自 MSVC 19.32 起已可用33）以及 std::expected、std::mdspan、std::print、std::ranges::to 和显式 this 参数等 C++23 核心设施34，因其未出现在 14.51 增量更新清单中且不同工具链曾存在分批落地历史2，工程头文件必须遵循业界推荐实践，直接采用标准特性测试宏（\_\_cpp\_lib\_expected、\_\_cpp\_lib\_mdspan、\_\_cpp\_lib\_flat\_map、\_\_cpp\_lib\_move\_only\_function、\_\_cpp\_lib\_print、\_\_cpp\_explicit\_this\_parameter）实施编译期 static\_assert 核验35，绝不依据编译器内部版本号编排条件宏分支。  
必须同时划清 C++26 实装边界：截至 2026 年秋，Microsoft Developer Community 上的 C++26 跟踪议题显示，诸如 \#embed、静态反射（Reflection）、包索引（Pack Indexing）、展开语句（Expansion Statements）及平凡可重定位（Trivial Relocatability）等 C++26 核心语言特性在 MSVC 中尚无公开路线图或官方实装公告31。因此在本项目重构方案中，不依赖尚未落地的 C++26 语法幻想；着色器字节码与 PTX 内核继续使用确定性的构建期脚本生成 constexpr std::array\<std::byte, N\> 头或标准 Win32 RCDATA 资源，杜绝运行时磁盘散落文件依赖。

### **3.2 自定义类全面清算与必要性显式论证**

0beda87 当前 AuditTypes 已达 34/35，而原委托书竟将最后 1 个稀缺配额留给 Wave D 的 UiWidgetNode1。从极限 Modern C++ 准则审视，类型上限 35 绝不是等待填满的预算额度，而是被历史面向过程/面向对象习惯过度消耗的警报线。一切能够用标准库原语、无状态自由函数与非捕获或轻量捕获 Lambda 表达式实现的逻辑，绝对禁止以自定义命名类或成员函数形式存在；任何保留的自定义类都必须能够经受“若拆解为标准库组合会直接破坏跨操作原子性或硬件/并发生命周期不变式”的严苛检验。下表给出对《69\_指导书委托书》所涉具名类型及存量 34 类典型结构的强制清算裁决。

| 类型 / 类别 | 0beda87 定位与原案规划 | 存在必要性显式论证 | 极限 Modern C++ 重构归宿 |
| :---- | :---- | :---- | :---- |
| UiWidgetNode | Wave D 拟占用第 35 个名额构建 UI 树节点 | **无必要（彻底否决）**；UI 控件树是典型保留模式 OOP 过度设计，节点交互与绘制不具备独立跨线程资源所有权不变式 | 采用数据导向设计：由无方法、无虚表的纯聚合位置与拓扑关系存入主画布 std::vector，由无状态 Lambda 与 std::span 驱动 D2D 即刻绘制；净增类型 \+0 |
| C API 句柄与资源包装器（存在于 34 类中者） | 封装 AVFormatContext、AVCodecContext、SwrContext、HANDLE、HMODULE、CUstream 析构 | **无必要（彻底清零）**；纯单一指针析构托管无需新建具名 RAII 类 | 统一替换为 using 类型别名：std::unique\_ptr\<T, decltype(\[\](T\* p) static noexcept { ... })\>（结合 C++20 非求值上下文 Lambda 与 C++23 static operator()），零额外类型污染 |
| 视图/回调/错误码/小字典封装（存在于 34 类中者） | 图像步长描述类、回调函数对象类、状态枚举结果包装、有序键值对小类 | **无必要（彻底清零）**；标准库已提供零开销一线设施 | 步长与多维平面视图迁移至 std::mdspan / std::span34；错误流迁移至 std::expected34；只移动回调迁移至 std::move\_only\_function33；小表查找迁移至 \<flat\_map\>2 |
| FfmpegDemuxer / FfmpegMuxer | src/io/ffmpeg\_io.cpp 持有格式上下文、passthrough\_streams 与 MP4 封装流状态1 | **高危待审（建议拆解 \-2）**；若调用呈单线程顺序流式打开-读写-关闭，完全可由纯自由函数与 std::unique\_ptr 句柄组合取代；仅当跨异步批次维持 DTS 单调性与延迟 extradata 注入不变式时方有存在基础 | 在 Wave 0 实施源码审计：优先拆解为自由函数与聚合上下文；仅在确认存在不可剥离的多包交叉状态机不变式时保留，且严禁在内部再套自定义缓冲包装类 |
| EncoderJobQueue | src/core/job\_queue.cpp 驱动 workerLoop()、activeSyncIds 与多片段队列调度1 | **明确必要（条件保留）**；必须在 UI 拖放生产线程与后台编码消费线程之间强制锁定互斥队列、停机令牌与工作线程析构汇合顺序，防止悬垂回调 | 剥除一切自研线程或信号量类，内核严格收敛为 std::jthread \+ std::stop\_token \+ std::mutex \+ std::condition\_variable\_any \+ 任务 std::vector |
| PreviewViewport | src/gui/preview\_viewport.h/.cpp 绑定 12-DWORD 色彩契约、DComp 硬裁剪与共享纹理呈递1 | **明确必要（保留）**；必须将 IDCompositionRectangleClip、CreateTargetForHwnd(..., FALSE) 目标、DXGI 交换链与 presentShared 跨流票据同步强绑定于窗口重建/销毁同一原子生命周期 | 保留并确保内部 presentShared(..., color\_flags, left\_max\_nits, right\_max\_nits, ticket) 为单一契约入口，清除任何旧版单比特色彩兼容字段 |
| RifeEngine | rtx\_ngx\_bridge 管理光流插帧推理会话、FG-First 张量与 rife\_stream\_ 非阻塞流1 | **明确必要（保留）**；必须确保专属非阻塞流 rife\_stream\_ 在推理核未完成或未同步前绝不释放底层 FG-First 原生分辨率显存池，属于硬件异步安全硬不变式 | 保留作为最小硬件并发锚点，算子参数与平面指针以 std::span/std::mdspan 传递，禁止再衍生下级子引擎类 |
| 其余未具名存量类型（34 类之余部） | 委托书未逐一列名的约 28–29 个类型 | **待 AST 全量核验清退** | 在 Wave 0 由构建脚本扫出全部具名类型清单，逐一签核“硬件并发不变式声明”；无正当声明者一律降维删退 |

### **3.3 铲除额外防御、预留逻辑、隐蔽开关与私藏词条**

按照“任何额外的防御、任何预留的接口与逻辑、任何无意义的冗余代码、任何其他隐藏的开关、任何额外的编译选项、任何违反 KISS 与奥卡姆剃刀原则以及增加使用者心智负担的隐藏坑一律拔除”的绝对洁癖要求，对 0beda87 及原方案中的四类污染源执行永久性清除。  
首先，彻底铲除 cmake/RtxFfmpegStatic.cmake 第 297–306 行的 if \[\[ \-f ... \]\] 预留条件探测与 CINERTX\_ENABLE\_GPL\_CODECS 额外编译选项1。在构建期根据磁盘文件“碰巧存在与否”静默决定是否编入 SvtAv1Enc.lib 或 x265-static.lib，会让同一源码在不同宿主机产出功能截然不同的静默残缺二进制，属于典型隐藏坑；同样，全仓 AuditSpdx 已显式表明当前代码库处于 PASS (GPL=114 MIT=39) 许可结构下1，保留一个 CINERTX\_ENABLE\_GPL\_CODECS 门控开关既撕裂构建矩阵，又令 cpu\_video\_encoder.cpp 中预置的 "libx265" 映射存在空悬失效风险1。遵循奥卡姆剃刀原则，必须直接移除该门控开关并将 SvtAv1Enc.lib 与 x265-static.lib 固化为无条件静态依赖（若未来法务策略转向纯 MIT/宽松许可，则应从全仓永久删除全部 114 个 GPL 文件与 x265 支线，绝不允许靠构建开关维持灰色双轨制）。  
其次，立刻删除 cmake/RtxFfmpegStatic.cmake 第 643–648 行的 Zig 交叉编译分支1。事实已证实，该分支早已与主分支严重漂移——不仅遗漏 \--enable-encoder=mov\_text，也未对齐 libsvtav1/libx2651，且 CineRTX 核心渲染链（DirectComposition、DXGI、WASAPI、MSVC 19.51 /MT）全盘锚定 Windows 原生宿主机。一段既不受 CI 门禁看护又持续失步的平行脚本是不折不扣的冗余死代码，必须立即拔除；若未来确实立项支持第二套受 CI 验证的交叉工具链，亦必须强制与 MSVC 主线共享同一份 CMake 编解码器清单变量，从物理上断绝双份清单分裂。  
再次，整段删去 src/gui/workstation\_canvas.cpp:L207-L282 的 chromeText() 私藏词条表1。将本应一目了然的界面常量文本隐藏在 76 行自定义映射表后端，既没有真实国际化资源体系依托，又强迫维护者在调试界面元素时进行二次跳转索引，纯属无端拉高认知负载；所有文案直接内联为 constexpr std::wstring\_view 或原地字面量即可。同时，针对 Wave D 拟引入的“零算力预览门控开关”1，严禁以隐藏配置文件键、环境变量或内部暗桩开关的形式实现，而必须直白建模为渲染管线状态结构体中的唯一显式旁路标识，与 UI 界面操作按钮 1:1 严格映射。  
最后，全面清扫源码内部的“过度防御性编程”与冗余兜底分支。例如在内部算子与预览链路中（PreviewViewport::presentShared、RifeEngine 推理调度及 FfmpegMuxer 内部调用链），凡经过边界入口校验或类型系统约束（如非空引用、std::span 固定宽度视图、枚举全覆盖匹配）的数据流，严禁在下游每层函数头部重复堆砌防御性 if (\!ptr) return false; 静默吞错、禁止为兼容已淘汰的旧版单比特 input\_is\_pq 契约保留任何兼容适配器，也禁止在 GPU 管线失败时悄然静默回退至未授权的 CPU 慢速路径；应以 \[\[assume(...)\]\]（C++23 P1774R8，MSVC 14.51 已支持2）向优化器传递清晰不变量，或通过 std::expected 在单一故障源头显式返回确定性错误值。

## **四、 基于 0beda87 极简底座的 Wave 0 至 Wave D 演进方案**

新编 Phase 13 演进路线拒绝带病冲刺，采用“**Wave 0 架构拆弹 \-\> Wave B 静态编解码闭环 \-\> Wave C 实时 AI 预览锁相 \-\> Wave D 零 GDI 数据驱动画布**”的四步走增量施工单。全过程严格恪守两项铁律：所有修改均以局部增量补丁落实，严禁整文件覆写；Wave B、Wave C 与 Wave D 全程实现 AuditTypes 增量 \+0（彻底否决 UiWidgetNode1），确保每个波次收口全绿后再推进下一波次，直至 v0.5.0 正式封板。

| 演进波次 | 核心工程目标 | 增量补丁目标文件 | 类型与 DLL 硬约束 | 宿主机实测验收门禁 |
| :---- | :---- | :---- | :---- | :---- |
| Wave 0（前置清洗） | 拔除 rtx\_headless\_cli、切断 bcrypt.dll、清除 CMake 开关与 Zig 分支、清洗存量 34 类 | CMakeLists.txt、cmake/RtxFfmpegStatic.cmake、单程序入口、存量冗余类文件、审计脚本 | 类型大幅压减（显著低于 34）；零 rtx\_headless\_cli；零 bcrypt.dll | 单 .exe \--headless 终端/管道输出正常；157/157 PASS 零回归 |
| Wave B（编解码与智能合并） | 无条件静态编译 SVT-AV1/x265、MP4 音轨/字幕转码闭环、同构/异构双路智能合并 | cmake/RtxFfmpegStatic.cmake、src/io/ffmpeg\_io.cpp、src/core/job\_queue.cpp | 新增 \+0 具名类型；新增 0 静态 DLL（零 bcrypt.dll） | libsvtav1/libx265 实编码、DTS/SRT 转 AAC/mov\_text、双路合并单测全绿 |
| Wave C（统一 GPU 预览与锁相） | 实时 AI 增强管线接入 12-DWORD 视口契约、WASAPI 时钟锁相与 8.0 ms 熔断 | src/core/playback\_session.cpp、rtx\_ngx\_bridge、WASAPI 动态装载单元 | 新增 \+0 具名类型；ole32/mmdevapi/avrt 严禁静态导入 | 12-DWORD 掩码直达 presentShared、切镜/流隔离及 \>8.0 ms 熔断单测全绿 |
| Wave D（零 GDI 画布与封板） | 删除 chromeText()、清零 gdi32.dll 与静态 user32.dll、DOD 横向拖拽 DAG 画布 | src/gui/workstation\_canvas.h/.cpp、src/gui/rtx\_gui.cpp、writeWebuiPreviewBmpIfDue 所在文件 | 否决 UiWidgetNode（新增 \+0）；清零 gdi32.dll 与静态 user32.dll | 单 .exe 静态表收敛至 kernel32.dll（及不可回避 API-Set 宿主）；v0.5.0 封板 |

### **4.1 Wave 0：单 EXE 收拢、DLL 清洗与类型瘦身前置门禁**

Wave 0 是后续波次展开的先决条件，目标是在不改变核心算子功能的前提下，一次性排空结构性债务。

> 1. 在 CMakeLists.txt 彻底删除 rtx\_headless\_cli 可执行目标与对应测试引用，并在唯一主程序入口前端以 std::span 完成 \--headless 分发与基于 GetStdHandle \+ GetFileType 守卫的 AttachConsole(ATTACH\_PARENT\_PROCESS) / freopen\_s 输出流对接8。  
> 2. 在 cmake/RtxFfmpegStatic.cmake 删除第 297–306 行的文件存在探测、删除 CINERTX\_ENABLE\_GPL\_CODECS 编译选项以及第 643–648 行的 Zig 交叉分支1；在 FFmpeg 构建步骤中确定性剔除 HAVE\_BCRYPT 与 bcrypt 系统库14，并用 dumpbin /imports 清查并拔除工程中非密码学 std::random\_device、PathCch\* 与显式同步 API 导入17。  
> 3. 对当前 AuditTypes 中的 34 个类型产出全量静态清单，凡纯句柄 RAII 包装、简单映射表与无不变式状态对象全部降维拆解为 std::unique\_ptr 自定义 Lambda 删除器、std::expected、std::mdspan、std::move\_only\_function 与 \<flat\_map\>，同时整块删除 chromeText() 私藏词条表1。  
> 4. 重写本地与 CI AuditImports 验收脚本，废除双 .exe 配额分支，锁定单一业务 .exe \+ 零 bcrypt.dll 底线，并复跑全量 157/157 单元测试确认零回归1。

### **4.2 Wave B 重编指导书：全静态编解码器、MP4 转码闭环与智能合并**

Wave B 对应原 32\_ 范围，前置依赖为 Wave 0 完成全绿收口，重点补齐 RtxFfmpegStatic.cmake、src/io/ffmpeg\_io.cpp 与 src/core/job\_queue.cpp 的实作缺口，严格保持新增具名类型 \+0 且直接适配 src/io/cpu\_video\_encoder.cpp 既有的 "libsvtav1" / "libx265" 与 preset 参数映射1。  
在 cmake/RtxFfmpegStatic.cmake 增量补丁中，放弃条件文件探测，改为通过确定性构建步骤将 SvtAv1Enc.lib 与 x265-static.lib 直接纳入源码静态编译链路。参考 Windows/MSVC 静态运行库工程实践，配置 x265 时显式传递 \-DSTATIC\_LINK\_CRT=ON \-DENABLE\_SHARED=OFF \-DENABLE\_CLI=OFF37，配置 SVT-AV1 时关闭共享库生成并保证全静态 CRT /MT 一致性38；随后在 FFmpeg 配置期无条件开启 \--enable-libsvtav1 \--enable-gpl \--enable-libx265 \--enable-demuxer=concat \--enable-encoder=aac,mov\_text 并同步确保 HAVE\_BCRYPT=01。  
在 src/io/ffmpeg\_io.cpp 增量补丁中，针对 FfmpegMuxer::open() 第 834 行在 MP4 模式下仅直通 AV\_CODEC\_ID\_MOV\_TEXT 的历史缺口1，以返回 std::expected\<void, ...\> 的自由函数构建音频与字幕转码闭环（无需新增转码类）：对于 FfmpegDemuxer 置于 passthrough\_streams 中且不被 MP4 容器兼容的音轨（如 DTS、FLAC 等1），通过 std::unique\_ptr\<SwrContext, decltype(\[\](SwrContext\* s) static noexcept { swr\_free(\&s); })\> 与 AVAudioFifo 句柄组合完成采样率/声道布局重采样并编码为标准 AAC-LC；对于 SUBRIP、ASS 与 WEBVTT 等非 MOV\_TEXT 字幕流1，经 libavcodec 解码为 AVSubtitle 并用 RAII 守卫确保 avsubtitle\_free 释放，再重新编码为 AV\_CODEC\_ID\_MOV\_TEXT 封入 MP4 轨道。  
在 src/core/job\_queue.cpp 增量补丁中，升级 EncoderJobQueue::workerLoop() 第 242–251 行的 JobOutputMode::MergeConcat 分支1。将待合并片段的容器/流探测结果组织为无方法纯数据聚合集合，直接基于 std::span 与 std::ranges::all\_of 执行一行严格同构校验（对齐视频编码器、分辨率、像素格式、SAR、帧率时基及音频采样率/声道布局）：全部片段严格同构时沿用既有高效率 .ffconcat 直出路径；一旦任一输入流出现异构特征，自动切换路由至 runNvidiaRemasterRenderGraph() 执行统一 GPU 管线重制与无拼接裂缝合并1。  
Wave B 宿主机实测验收门禁如下：

> 1. build.bat（MSVC 19.51 /std:c++latest /W4 /WX）0 Error, 0 Warning。  
> 2. AuditTypes 增量严格为 \+0，dumpbin /imports 确认零 bcrypt.dll、零新增静态 DLL24。  
> 3. 新增单元测试验证不依赖任何外部共享文件，端到端产出合法的 libsvtav1 与 libx265（含 preset 透传）编码码流。  
> 4. 新增单元测试验证含 DTS/FLAC 音轨及 SRT/ASS 字幕的源媒体经转码复用至 MP4 后，输出文件音轨为 AAC-LC、字幕轨为 mov\_text 且时间戳连续可读。  
> 5. 新增单元测试验证同构多片段进入 .ffconcat 快速合并、异构分辨率或帧率多片段自动路由至 runNvidiaRemasterRenderGraph()。

### **4.3 Wave C 重编指导书：统一 GPU AI 增强预览环、WASAPI 锁相与 8.0 ms 熔断**

Wave C 对应原 33\_ 范围，核心是在 src/core/playback\_session.cpp 与 rtx\_ngx\_bridge 中淘汰原始解码帧直通逻辑1，将预览环升级为与离线渲染等效的实时 GPU AI 增强管线，并全面锚定 0beda87 已建成的渲染与异步边界1。严禁回退使用已废弃的单比特 input\_is\_pq 签名，所有提交必须唯一经由 PreviewViewport::presentShared(..., color\_flags, left\_max\_nits, right\_max\_nits, ticket)，根据预览模式与源/目标动态范围精确组装 kPreviewLeftPq/Hlg/Bt2020 与 kPreviewRightPq/Hlg/Bt2020 双侧 12-DWORD 色彩掩码及左右峰值亮度值，无缝激发 BT.2390 Hermite EETF 与 IDCompositionRectangleClip 硬件视口契约1。  
实时预览增强链严格保持 RifeEngine 的 rife\_stream\_ 非阻塞 CUDA 流隔离与 FG-First 原生分辨率锁定（配合已修复的 PTX 11 参数对齐及 playback\_pack 动态扩容机制）1，并将函数命名由易引起误解的 createHeadlessPreviewSink() 规范重命名为 createRealtimePreviewSink()。管线逐级串联“切镜守卫 \-\> RIFE/NVOFA 60fps \-\> VSR \-\> TrueHDR \-\> 3D LUT \-\> A/B 分屏”1，各阶段显存步长与纹理平面一律通过 std::mdspan / std::span 零拷贝传递，阶段连接采用编译期固定拓扑或 std::move\_only\_function 表达，拒绝为滤镜级联定义任何抽象基类或策略节点类（AuditTypes 增量严格 \+0）。  
对于 WASAPI 音频时钟锁相与 8.0 ms 推理超时熔断机制，实施零静态库污染与无线程冗余设计：WASAPI 运行时所需的 ole32.dll、mmdevapi.dll 与线程优先级提升所需的 avrt.dll 严禁静态链接，一律封装在播放会话启动路径的局部作用域内通过 LoadLibraryExW(..., LOAD\_LIBRARY\_SEARCH\_SYSTEM32) 按需解析并交由 std::unique\_ptr 自定义删除器托管；8.0 ms 熔断判定在每帧GPU调度点直接利用 std::chrono::steady\_clock 计算帧间推理延迟窗并与 CUDA 流非阻塞事件状态耦合，一旦检测单帧 AI 耗时超越 8.0 ms 红线，立即通过无锁 std::atomic\<bool\> 原子状态切换至直通旁路，确保将原始解码帧无延迟推入 playback\_pack，从而死守 WASAPI 主时钟锁相节拍不发生爆音或缓冲堆积。  
Wave C 宿主机实测验收门禁如下：

> 1. AuditTypes 增量严格为 \+0，AuditImports 验证零 ole32.dll、mmdevapi.dll 与 avrt.dll 静态导入，且 \--headless 启动链路不触发上述音频 DLL 动态加载。  
> 2. 新增单元测试验证左右眼/对比侧不同色彩空间组合下，12-DWORD 双侧掩码与 left\_max\_nits/right\_max\_nits 正确送达 PreviewViewport::presentShared()。  
> 3. 新增单元测试验证切镜守卫触发及动态切换增强参数时，rife\_stream\_ 与 FG-First 锁定张量不发生跨流竞争与显存越界。  
> 4. 新增单元测试通过人工注入 \> 8.0 ms 模拟算子延迟，验证熔断器在单帧边界内平稳旁路增强链并保持 WASAPI 时钟周期锁相稳定。

### **4.4 Wave D 重编指导书：零 GDI 纯 D2D/DWrite 数据驱动画布与 Phase 13 封板**

Wave D 对应原 34\_ 范围，是 Phase 13（v0.5.0）收官交付波次。本波次全面承接 0beda87 在 src/gui/workstation\_canvas.h/.cpp 与 src/gui/rtx\_gui.cpp 中已完成的 activeSyncIds 靶向联动、selectedJobId、即拖即入队、fpsModeStringFromIndex 单一映射、WS\_CLIPCHILDREN 与 commitViewportVsrScale()1，同时坚持**彻底弃用 UiWidgetNode 第 35 个类型配额，保持类型增量 \+0**，推进四项收官改造。

> 1. **清零 gdi32.dll 并彻底移除 chromeText()**：确认 workstation\_canvas.cpp:L207-L282 的 chromeText() 已于 Wave 0 铲除或在此收口清零1；画布渲染、节点连线贝塞尔曲线与字体排印全面由按需加载的 d2d1.dll 与 dwrite.dll 承载，并将 writeWebuiPreviewBmpIfDue()1 核查重构为纯标准 C++ 文件流按小端字节序构造 54 字节 BMP 文件头后落盘像素，在 CMakeLists.txt 永久摘除 gdi32.lib。  
> 2. **完成 user32.dll 静态导入清零**：移除 CMakeLists.txt 中的 user32.lib，将全部窗口创建、消息派发、光标命中与拖放 API 集中至 GUI 模式启动阶段一次性通过 LoadLibraryExW(L"user32.dll", nullptr, LOAD\_LIBRARY\_SEARCH\_SYSTEM32) 取回函数表，确保主程序静态导入表零图形 DLL、--headless 路径零 GUI 动态加载。  
> 3. **数据导向横向可拖拽 DAG 画布（无 UiWidgetNode）**：以纯 POD 数据聚合体记录横向 DAG 节点表（含节点标识、二维布局坐标、端口连接索引数组及关联 jobId）并连续存储于画布主 std::vector 缓冲区中；画布平移缩放、鼠标命中检测与 D2D 绘制全部采用自由函数与 std::span 顺序遍历配合无状态 Lambda 完成，不仅将新类型数量锁死为 \+0，且具备远优于指针控件树的 CPU 缓存局部性。  
> 4. **显式零算力预览旁路控制**：将零算力直通控制直接关联至 Wave C 管线状态的唯一显式布尔标志位及界面“旁路增强”开关，禁止任何潜伏隐藏阈值或双重分支，关闭增强算子时立即保持零 GPU 算力消耗并直通原片预览。

Phase 13（v0.5.0）正式封板须在宿主机一次性通过以下终极门禁：

> 1. 分发目录中仅存在单一业务 .exe，彻底绝迹 rtx\_headless\_cli；同一 .exe 在 \--headless 下管道/控制台输出完好，在交互启动下秒开纯 D2D/DWrite/DComp 工作站。  
> 2. build.bat（MSVC 19.51 /std:c++latest /W4 /WX）0 Error, 0 Warning。  
> 3. AuditImports 与 dumpbin /imports 实测静态导入表：零 bcrypt.dll、零 gdi32.dll、零 user32.dll、零 d2d1.dll/dwrite.dll/ole32.dll，物理静态导入收敛至不可削减的 kernel32.dll（及 UCRT API-Set 不可回避映射的宿主 kernelbase.dll，且已核验绝无 PathCch\* 或显式 WaitOnAddress 等人为引入点）。  
> 4. AuditTypes 严格低于 34（核心仅存有明确生命周期不变式辩护的实体，无 UiWidgetNode），AuditSloc（Core 远低于 23,000 红线）、AuditPrivacy（0 violations）、AuditSpdx（PASS）及 Wave B–D 全部新增与既有单元测试 100% PASS，正式批准 v0.5.0 封板1。

#### **引用的著作**

> 1. 69\_CineRTX Phase 13 基于最新基线(0beda87)重编Wave BD 指导书委托书.md, [[REDACTED_ID]]([REDACTED_ID])  
> 2. C++23 Support in MSVC Build Tools 14.51 \- C++ Team Blog, [https\://devblogs.microsoft.com/cppblog/c23-support-in-msvc-build-tools-14-51/](https://devblogs.microsoft.com/cppblog/c23-support-in-msvc-build-tools-14-51/)  
> 3. Changelog · microsoft/STL Wiki \- GitHub, [https\://github.com/microsoft/STL/wiki/Changelog/911f24b80597e09d96790a600fd5365c15a8a56c](https://github.com/microsoft/STL/wiki/Changelog/911f24b80597e09d96790a600fd5365c15a8a56c)  
> 4. MSVC Build Tools Version 14.51 Release Candidate Now Available, [https\://devblogs.microsoft.com/cppblog/msvc-build-tools-version-14-51-release-candidate-now-available/](https://devblogs.microsoft.com/cppblog/msvc-build-tools-version-14-51-release-candidate-now-available/)  
> 5. Changelog · microsoft/STL Wiki \- GitHub, [https\://github.com/microsoft/STL/wiki/Changelog](https://github.com/microsoft/STL/wiki/Changelog)  
> 6. [[REDACTED_ID]]([REDACTED_ID])  
> 7. workspace\_resource://CksiRwohMU1Kcjd1RGY2RGxZR2JtbXJxalpzc3BjdlBQajE1ZGZzGiJhcHBsaWNhdGlvbi92bmQuZ29vZ2xlLWFwcHMuZm9sZGVyMAM  
> 8. AttachConsole function \- Windows Console \- Microsoft Learn, [https\://learn.microsoft.com/en-us/windows/console/attachconsole](https://learn.microsoft.com/en-us/windows/console/attachconsole)  
> 9. How to attach a console to your GUI app in C++ \- Bobobobo's Weblog, [https\://bobobobo.wordpress.com/2009/03/01/how-to-attach-a-console-to-your-gui-app-in-c/](https://bobobobo.wordpress.com/2009/03/01/how-to-attach-a-console-to-your-gui-app-in-c/)  
> 10. Where do writes to stdout go when launched from a cygwin shell, no, [https\://stackoverflow.com/questions/4028353/where-do-writes-to-stdout-go-when-launched-from-a-cygwin-shell-no-redirection](https://stackoverflow.com/questions/4028353/where-do-writes-to-stdout-go-when-launched-from-a-cygwin-shell-no-redirection)  
> 11. GitHub \- wesmar/CmdT: The world's smallest TrustedInstaller launcher, [https\://github.com/wesmar/cmdt](https://github.com/wesmar/cmdt)  
> 12. Console documentation, [https\://canvas.pieas.edu.pk/files/151439/download?download\_frd=1](https://canvas.pieas.edu.pk/files/151439/download?download_frd=1)  
> 13. How do I get console output in C++ with a Windows program?, [https\://stackoverflow.com/questions/191842/how-do-i-get-console-output-in-c-with-a-windows-program](https://stackoverflow.com/questions/191842/how-do-i-get-console-output-in-c-with-a-windows-program)  
> 14. Backporting ffmpeg 5.1.2 to Microsoft Windows XP / XP x64, [http\://wp.xin.at/archives/5851](http://wp.xin.at/archives/5851)  
> 15. libavutil \- MIRRORS/AOSP-VANILLA/android\_external\_ffmpeg.git, [https\://repos.it-feel.com/MIRRORS/AOSP-VANILLA/android\_external\_ffmpeg.git/tree/libavutil/random\_seed.c?id=32582a4e2a8e631a9195050a2005862ee7ad7aec](https://repos.it-feel.com/MIRRORS/AOSP-VANILLA/android_external_ffmpeg.git/tree/libavutil/random_seed.c?id=32582a4e2a8e631a9195050a2005862ee7ad7aec)  
> 16. Coverage Report \- Googleapis.com, [https\://storage.googleapis.com/oss-fuzz-coverage/ffmpeg/reports-by-target/20261122/ffmpeg\_dem\_ASS\_fuzzer/linux/src/ffmpeg/libavutil/random\_seed.c.html](https://storage.googleapis.com/oss-fuzz-coverage/ffmpeg/reports-by-target/20261122/ffmpeg_dem_ASS_fuzzer/linux/src/ffmpeg/libavutil/random_seed.c.html)  
> 17. Windows C++串口DLL封装规范：生产级二进制契约设计 \- CSDN博客, [https\://blog.csdn.net/weixin\_29189003/article/details/171226620](https://blog.csdn.net/weixin_29189003/article/details/171226620)  
> 18. C++ Show and Tell \- December 2025 : r/cpp \- Reddit, [https\://www\.reddit.com/r/cpp/comments/1pbglr2/c\_show\_and\_tell\_december\_2025/](https://www.reddit.com/r/cpp/comments/1pbglr2/c_show_and_tell_december_2025/)  
> 19. Windows XP compatible non-SSE2 FFmpeg binaries \- Doom9's Forum, [https\://forum.doom9.org/showthread.php?t=181802](https://forum.doom9.org/showthread.php?t=181802)  
> 20. Windows 10 System Programming, Part 1 1196068259, 2147483647, [https\://dokumen.pub/windows-10-system-programming-part-1-1196068259-2147483647.html](https://dokumen.pub/windows-10-system-programming-part-1-1196068259-2147483647.html)  
> 21. Which DLL has PathCchAppend? \- c++ \- Stack Overflow, [https\://stackoverflow.com/questions/57354840/which-dll-has-pathcchappend](https://stackoverflow.com/questions/57354840/which-dll-has-pathcchappend)  
> 22. 可通过rundll32 直接调用的实用工具型DLL（高频运维） \- suv789, [https\://www\.cnblogs.com/suv789/p/18815796](https://www.cnblogs.com/suv789/p/18815796)  
> 23. Search existing C++ feedback \- Visual Studio Developer Community, [https\://developercommunity.visualstudio.com/cpp?moreInfo=true\&viewtype=all\&q=runs+linked](https://developercommunity.visualstudio.com/cpp?moreInfo=true&viewtype=all&q=runs+linked)  
> 24. .NET Native AOT 在Windows 7 启动崩溃的根因与替代方案 \- 社区, [https\://bbs.csdn.net/weixin\_26833205/article/details/100732911](https://bbs.csdn.net/weixin_26833205/article/details/100732911)  
> 25. The implementation of random\_device in VS2010? \- Stack Overflow, [https\://stackoverflow.com/questions/9549357/the-implementation-of-random-device-in-vs2010](https://stackoverflow.com/questions/9549357/the-implementation-of-random-device-in-vs2010)  
> 26. 简单几步教你如何更换文件夹背景图片原创 \- CSDN博客, [https\://blog.csdn.net/2303\_79299383/article/details/138260699](https://blog.csdn.net/2303_79299383/article/details/138260699)  
> 27. Windows PE绑定导入表原理与实战指南 \- CSDN博客, [https\://blog.csdn.net/weixin\_33404102/article/details/171168310](https://blog.csdn.net/weixin_33404102/article/details/171168310)  
> 28. Accelerated Windows API for Software Diagnostics: With Category, [https\://dokumen.pub/accelerated-windows-api-for-software-diagnostics-with-category-theory-in-view-1nbsped-1912636638-9781912636631.html](https://dokumen.pub/accelerated-windows-api-for-software-diagnostics-with-category-theory-in-view-1nbsped-1912636638-9781912636631.html)  
> 29. DLL延迟加载\| ZONE.CI 全球网, [https\://security.zone.ci/secarticles/wx/481207.html](https://security.zone.ci/secarticles/wx/481207.html)  
> 30. Windows 10 System Programming, Part 1 \- Pavel Yosifovich, [https\://edu.anarcho-copy.org/other/Windows/Windows%2010%20System%20Programming.pdf](https://edu.anarcho-copy.org/other/Windows/Windows%2010%20System%20Programming.pdf)  
> 31. Implement C++26 Standard features in MSVC \- Developer Community, [https\://developercommunity.visualstudio.com/t/Implement-C26-Standard-features-in-MSV/10777423](https://developercommunity.visualstudio.com/t/Implement-C26-Standard-features-in-MSV/10777423)  
> 32. MSVC should add /std:c++26preview option to distinguish C++26, [https\://developercommunity.microsoft.com/t/MSVC-should-add-std:c26preview-option/11108533](https://developercommunity.microsoft.com/t/MSVC-should-add-std:c26preview-option/11108533)  
> 33. C++23 likes to move it\! \- Sandor Dargo's Blog, [https\://www\.sandordargo.com/blog/2024/01/31/cpp23-likes-to-move-it](https://www.sandordargo.com/blog/2024/01/31/cpp23-likes-to-move-it)  
> 34. Chapter 11: C++23 \- Modern C++ Tutorial \- Changkun Ou, [https\://changkun.de/modern-cpp/en-us/11-cpp23/](https://changkun.de/modern-cpp/en-us/11-cpp23/)  
> 35. C++23 Features: Complete Guide with Tested Code Examples, [https\://www\.mycplus.com/programming/cpp/cplusplus23-exploring-the-new-features/](https://www.mycplus.com/programming/cpp/cplusplus23-exploring-the-new-features/)  
> 36. C++23: This and That – MC++ BLOG \- Modernes C++, [https\://www\.modernescpp.com/index.php/c23-this-and-that/](https://www.modernescpp.com/index.php/c23-this-and-that/)  
> 37. x265ビルド VC2017版 \- rigayaの日記兼メモ帳 \- FC2, [https\://rigaya34589.blog.fc2.com/blog-entry-943.html](https://rigaya34589.blog.fc2.com/blog-entry-943.html)  
> 38. General Documentation \- FFmpeg, [https\://www\.ffmpeg.org/general.html](https://www.ffmpeg.org/general.html)