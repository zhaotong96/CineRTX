# **CineRTX Phase 11.3 止血与竖屏修复终审验收裁决书暨 Phase 12 极限现代 C++ 重构与 DLL 净空总方案**

针对项目总协调负责人呈报的 **52 号终审封板委托书（CINERTX-P11-DELEGATION-052）**，首席领域专家组对照 **46 号终审裁决（CINERTX-P11-AUDIT-046）**、**47/48 号应急止血工单与汇报**、**49 号竖屏法医排查（CINERTX-P11-FORENSIC-049）** 及 **50/51 号竖屏修复工单与汇报** 完成了全链路源码与物证穿透复审，正式签发如下双轨终审裁决：

> 1. **Phase 11.3 阶段性封板裁决：正式解除「CONDITIONAL PASS / HOLD CLEAN STRATEGIC SEAL」限制，授予「有条件战略封板通过（STRATEGIC SEAL GRANTED WITH PHASE 12 CARRY-OVER DEBT）」**。全部 11 项阻断性缺陷（46 号裁决指出的 5 项合规与音视频硬伤 \+ 49 号排查锁定的 RC-PORTRAIT-01 \~ RC-PORTRAIT-06 共 6 项竖屏阻断）均已形成可复现的源码与 RTX 5060 真机物理闭环：CineRTX.exe 剥离 nvsdk\_ngx\_s.lib 恢复独立 rtx\_ngx\_bridge.dll 后体积净降 901,120 字节（收敛至 4,803,584 字节，静态 IAT 零专有符号），141 项单元测试 100% PASS，6 大横/竖屏硬件转码场景（含 2160x3840 4K 竖屏直出与 1080x1920 60fps RIFE 插帧）实证 D2H \== 0 B，准予按 **20 号施工单** 启动 Phase 11.4 决胜冲刺以收官 v0.3.0。  
> 2. **代码卫生与技术赤字定性：52 号委托书及全仓源码仍残留 4 项结构性违背“极致现代 C++、奥卡姆剃刀与代码卫生洁癖”的结转债务**——Pure SLOC 攀升至 **32,658 行**（较 749fe0d 法定基准 31,064 行净增 \+1,594 行，未实现净减平账）、52 号委托书正文内仍泄露 <local_absolute_path> 本地路径直链、全仓 153 个具名类型中仍寄生着未清剿的伪包装结构体与模板化虚假契约，以及静态链接库中仍传递引入了多余的 Windows 系统 DLL。上述债务全部锁定为 **Phase 12 极限重构与 DLL 净空** 的刚性清零标的。

## **一、 52 号封板委托书（CINERTX-P11-DELEGATION-052）逐项穿透核验**

### **1.1 十一项核心缺陷闭环物证复核**

| 验收维度 | 52 号委托书申报状态 | 专家组穿透复核事实与物证校验 | 终审定性 |
| :---- | :---- | :---- | :---- |
| **1\. GPL 开源隔离净空** | rtx\_ngx\_bridge 恢复为 SHARED DLL；CineRTX.exe 体积降 901,120 字节 | CMakeLists.txt 彻底切断 rtx\_core 对 rtx\_ngx\_bridge\_static 的直链，专有 nvsdk\_ngx\_s.lib 仅由 rtx\_ngx\_bridge.dll 私有链接；ngx\_remaster.cpp 恢复纯 C ABI LoadLibraryExW \+ GetProcAddress 动态加载；AuditImports.cmake 确认 CineRTX.exe（4,803,584 B）静态 IAT 零专有符号。远端历史（方案 B）锁定于 Phase 11.4 终审授权时原子化 rebase 清洗。 | **PASS** |
| **2\. NLE 音频合龙与假调用肃清** | 拔除假 toJson/fromJson 空转与废弃 exportSelectedClipToEncoder()；真实消费 AudioGraphSegment | workstation\_canvas.cpp 欺骗性空转符号清零，onNleExportSequence() 补齐 validateAndNormalizeProfile() 并透传 AudioGraphSegment 至 hw\_transcode\_stages.cpp，经 mixAvx2 \+ wsolaStretch 重基准封装进单 Muxer。 | **PASS** |
| **3\. WSOLA 多声道与真软限幅** | 支持 1\~8 声道，消除全量左移，改 tanh 软限幅 | WsolaStreamState 扩容至 kWsolaRingMaxFrames \* 8 与 kWsolaOverlapFrames \* 8，逻辑 ring\_head 配合惰性 std::shift\_left 消除无效搬移，tanh 软拐点限幅消除削波，5.1/7.1 单测全绿。 | **PASS** |
| **4\. 竖屏全链路自适应（RC-PORTRAIT-01\~06）** | 全档位 9:16 翻转、Native 直通、Pillarbox 黑边、RIFE 2176x2176、DisplayMatrix 透传、NVENC 3840 High Tier 全部闭环 | RTX 5060（Driver 621.24 WHQL）实测通过 1080x1920 \-\> 1080x1920 (FHD)、1440x2560 (2K)、2160x3840 (4K High Tier)、Native 及 60-rife（2176x2176 TRT 引擎）；ffprobe 确证输出为 1080x1920 \[SAR 1:1 DAR 9:16\] 60fps 10-bit PQ HDR10，全程 D2H \== 0 B。 | **PASS** |
| **5\. 门禁、体量与文档卫生核验** | 141/141 单测 PASS；AuditTypes 153 类型；Pure SLOC 32,658 行 | **检出 2 项遗留瑕疵**：① Pure SLOC 较 749fe0d 基线（31,064）净增 **\+1,594 行**（52 号文仅写“平账通过”而未披露超支额度）；② 52 号文第一节、第四节、第五节仍残留 4 处本地 <local_absolute_path> 路径直链，须立即替换为相对路径。 | **PASS（附 Phase 12 减债契约）** |

## **二、 Phase 12 极限现代 C++20/23/26 重构与“标准库第一铁律（Std-First）”**

Phase 12 以 **“能用标准库表达的，绝不允许自定义类/结构体存在”** 为最高铁律，实施源码级清剿与现代化升维。

### **2.1 编译器特性实证边界（MSVC 19.51+ / C++20/23/26）**

> 1. **全仓强制底座（C++20/23）**：全面使用 std::expected、std::optional、std::variant、std::tuple、std::span、std::mdspan、std::jthread、std::stop\_token、std::move\_only\_function、std::flat\_map、std::format、std::from\_chars、std::shift\_left、std::numbers、C++23 Deducing this 与 static operator()。  
> 2. **明确规避的未落地/不可移植边界**：截至 2026 年 11 月，MSVC 尚未公开提供 P2996 静态反射（^^、std::meta::info）与 \#embed，且 std::out\_ptr 代理对象不兼容 Win32 IID\_PPV\_ARGS 宏。因此坚持 **单链 C++23 constexpr 成员指针内生反射表（kParamDescriptors）** 搭配 consteval 编译期断言，绝不复辟体外代码生成器；COM 出参统一采用自由函数两段式收养或显式裸指针 reset。

### **2.2 全仓自定义类型零容忍清剿表（目标：153 类型 $\rightarrow \leq 115$ 类型）**

对 AuditTypes.cmake 在册的 153 个具名类型实施逐个甄别，凡被动数据袋、伪状态机及重复底层绑定壳一律物理消灭：

| 待清剿自定义类型及锚点 | 违背现代 C++ 洁癖的根因 | Phase 12 处置动作 | 标准库精确替代原语 |
| :---- | :---- | :---- | :---- |
| DynamicVsrTarget & ViewportVsrTarget (workstation\_canvas.h) | 仅含 4 个标量，且布尔字段 vsr\_active 与 scale \> 1.0 完全冗余；附带全局静态访问器 | **物理删除（-2 类型）** | std::optional\<std::tuple\<int, int, double\>\>（std::nullopt 即未激活） |
| ProbedMedia (workstation\_canvas.h) | 3 个标量加 bool ok，典型 C 风格状态袋 | **物理删除（-1 类型）** | std::optional\<std::tuple\<int, int, int64\_t\>\> |
| PingPongSlot (d3d12\_interop.h) | 全仓零引用的死类型，仅靠复制粘贴注释占位 | **物理删除（-1 类型）** | 整段清除 |
| InteropTexture & SharedBufferSlot (d3d12\_interop.h) | 无析构函数与状态机方法，却套用虚假状态机注释；裸持 void\*/HANDLE | **降维合并（-2 类型）** | 改用 FnDeleter / UniqueHandle 组成的 std::tuple 托管生命周期 |
| D3D12Api、CudaApi、CudaDriverApi (d3d12\_interop.cpp / nvof\_engine.cpp) | 内部匿名命名空间结构体，却谎称 Category: C-ABI，且重复绑定 nvcuda.dll 函数 | **清退统合（-3 类型）** | 统一为无状态函数指针加载器模板 \+ std::tuple 绑定表 |
| RenderGraphSegment & AudioGraphSegment (render\_graph.h) | 前 5 个时间线/源区间字段完全同构，仅末尾附加负载不同 | **合并降维（-1 类型）** | 统一为单一时间线切片结构，负载使用 std::variant\<std::pair\<bool, bool\>, float\> |
| Options (src/cli/main.cpp) | 将互斥的 CLI 模式与各模式专有字段平铺，制造非法组合状态 | **物理删除（-1 类型）** | 改用 std::variant 和类型（每个子模式仅携带自身合法参数） |
| 历史旧包装残余（MappedResource 遗留包装、Control、Group 等） | 局部传参或控件表定义包装 | **逐个清退** | std::tuple / std::array\<std::tuple\<...\>, N\> 常量表 |

### **2.3 存续自定义类型的合法性白名单与强制存在证明**

Phase 12 全仓仅允许以下 **六类不可被标准库替代的自定义类型** 存续，且每一项必须在 AuditTypes.cmake（升级为 file(READ) 词法前端）下提供独立、非模板化的真实存在证明：

> 1. **二进制 ABI 契约边界（C-ABI）**：仅限 ngx\_bridge\_abi.h、rtx\_shim\_abi.h、EmbeddedShimApi 及驱动结构体（如 CUDA\_MEMCPY3D）。  
   * *存在必要性*：跨越 DLL / C 语言边界与 NVIDIA 驱动接口，必须保证字段偏移、内存填充与 extern "C" 布局确定性，标准库 std::tuple 不保证跨编译器标准布局（Standard-Layout）与 ABI 稳定性。  
> 2. **硬件寄存器与显存物理对齐布局（Hardware-Layout）**：仅限 CudaDeviceBuffer、TelemetrySnapshot 及着色器根常量。  
   * *存在必要性*：CUdeviceptr（uint64\_t）为非主机指针整数，不满足 std::unique\_ptr 的 NullablePointer 约束且需隔离 CPU/GPU 地址空间；跨线程无锁采样快照需 alignas(64) 独占缓存行防止伪共享（False Sharing）。  
> 3. **多方法动态虚契约（Polymorphic-Boundary）**：仅限真正包含纯虚函数的抽象基类 IVideoFrameSink、IDemuxer、IMuxer、IAudioMasterClock。  
   * *存在必要性*：需要多方法接口分组与无头单元测试桩（Mock/Fake）动态多态替换，单一 std::move\_only\_function 无法表达多方法状态契约。  
> 4. **严格逆序析构与异步硬件退役状态机（NonTrivial-StateMachine）**：仅限 PreviewViewport、GpuRetirementQueue、MediaPlaybackSession、WasapiAudioOutput、D3D12Interop、NgxD3D12Bridge、NvofEngine、VramAdaptiveScheduler、WsolaStreamState。  
   * *存在必要性*：必须通过类成员声明拓扑在编译期锁死析构逆序（先 join std::jthread 工作线程 $\rightarrow$ 等待 GPU Fence 完成/登记延迟退役池 $\rightarrow$ 释放硬件上下文/桥接句柄），防止 Use-After-Free（UAF）与硬件句柄泄漏。  
> 5. **无锁内存序并发原语（Concurrency-Invariant）**：仅限 AtomicTelemetryBus 等依赖 memory\_order\_acquire/release 世代校验（Seqlock）的无锁读取器。  
   * *存在必要性*：封装跨线程世代奇偶校验与内存屏障不变量，避免调用方手写原子序出错。  
> 6. **核心领域持久化与反射锚点模型（Domain-Model）**：仅限 NleMediaAsset、TimelineClip、NleProject、TranscodeProfile、MediaInfo。  
   * *存在必要性*：TranscodeProfile 承载 20 项参数的成员指针编译期反射锚点（\&TranscodeProfile::\*）；NleProject 维护按 (start\_flicks, lane) 有序的编辑不变量及 6 支 std::variant（NleEditDelta）可逆变换。

## **三、 奥卡姆剃刀全仓除冗：拔除额外防御、隐藏开关、编译选项与心智坑**

> 1. **构建系统与编译选项归一化（CMakeLists.txt & cmake/RtxFfmpegStatic.cmake）**：  
   * 彻底删除全仓散落的重复 CXX\_STANDARD 属性、冗余 \-Wno-unused-function、历史迁移残留（RTX\_ZIG\_EXECUTABLE、-fexperimental-library），将标准版本、/MT、/W4、/permissive-、/ARCH:AVX2 100% 收拢至单一接口目标 rtx\_project\_options。  
   * 拔除全局 /wd4324 警告抑制：收缩 WasapiAudioOutput::Impl 中过度分散的 7 处独立 alignas(64) 原子变量，仅在跨读写线程边界的两组游标间保留单道缓存行隔离，从根源消灭对齐填充警告。  
   * 清除配置阶段静默执行外部脚本的 execute\_process(EmbedPtx.cmake)，仅保留构建期按文件依赖触发的 add\_custom\_command；为任何外部权重/归档拉取增加默认 OFF 的显式开关 RTX\_ALLOW\_NETWORK\_PROVISION，杜绝配置期静默联网。  
   * 统一 FFmpeg 静态库与主工程的 CRT 编译开关，消除 Debug 期 /NODEFAULTLIB:msvcrt.lib 补丁，并启用 /WARNUNUSEDLIBS 审计死库。  
> 2. **源码层全局状态、死开关与心智坑清零**：  
   * **全局可变状态清零**：彻底消灭 workstation\_canvas.cpp 中的全局可变单例 g\_app、active\_vsr\_target() 及其 7 个全局包装函数，将 WorkstationApp 生命周期严格绑定至主窗 GWLP\_USERDATA 托管的 std::unique\_ptr（WM\_NCCREATE 创建、WM\_NCDESTROY 销毁）。  
   * **全栈 UI/CLI 单源驱动**：彻底拔除 workstation\_canvas.cpp 中平行维护的第 4 份 20 项参数映射（collectProfileFromUi / applyProfileToUi）及 main.cpp 中的重复标量 CLI 参数分支，全部改为遍历 kParamDescriptors 表驱动自动生成与绑定。  
   * **隐藏死开关与伪防御剔除**：删除 d3d12\_interop.cpp 中的死开关 traceEnabled() { return false; }、main.cpp 中的空语句块 if (target\_fps\_set && target\_fps.num \> 0\) {} 与错位注释；用无异常零分配 std::from\_chars 替换 try/catch std::stoi 控制流；内部私有函数默认采用不可空引用与 std::span，消除函数入口层层叠叠的冗余判空防御。  
   * **NLE 编辑原子性修复**：将 addFilesToBin() 中分两次插入 V1 与 A1 的操作合并为单条复合 NleEditDelta 事务，修复单次 Ctrl+Z 仅撤销音轨而撕裂音画的心智坑。

## **四、 13 项系统 DLL 依赖极限剥离矩阵与演进目标**

针对现状列出的 {shell32.dll, dwmapi.dll, gdi32.dll, user32.dll, comdlg32.dll, comctl32.dll, uxtheme.dll, d3d12.dll, dxgi.dll, bcrypt.dll, ole32.dll, kernel32.dll, kernelbase.dll} 共 13 项系统 DLL，严格遵循 **“静态导入表（IAT）零多余依赖 \+ 运行时最小按需加载 \+ 拒绝为删轻库而引重库”** 原则实施外科手术剥离：

| DLL 名称 | 当前引入源头 | 静态 IAT 剥离 | 运行时剥离 | Phase 12 极限剥离实施机制与架构权衡 |
| :---- | :---- | :---- | :---- | :---- |
| **comctl32.dll** | workstation\_canvas.cpp 的 InitCommonControlsEx 与 PROGRESS\_CLASSW / TRACKBAR\_CLASSW | **永久移除** | **永久移除** | 删除样式割裂的旧式通用控件，直接复用画布现有双缓冲绘制原语实现暗色自绘进度条与滑块；永久删除 InitCommonControlsEx 与 comctl32.lib（标准按钮/文本框/下拉框属于 user32.dll）。 |
| **uxtheme.dll** | workstation\_canvas.cpp 对子控件调用 SetWindowTheme(L"DarkMode\_Explorer") | **永久移除** | **彻底根除** | 从 CMakeLists.txt 移除 uxtheme.lib；全自绘控件或将 GUI 解耦剥离后彻底零调用、零加载。 |
| **d3d12.dll** | CMakeLists.txt 静态链接 d3d12.lib 供 preview\_viewport.cpp 调用 | **100% 移除** | 仅视口呈现时加载 | 仿照 d3d12\_interop.cpp，在 preview\_viewport.cpp 改用 LoadLibraryExW(L"d3d12.dll", nullptr, LOAD\_LIBRARY\_SEARCH\_SYSTEM32) 动态解析入口，删除 d3d12.lib 链接。 |
| **dxgi.dll** | CMakeLists.txt 静态链接 dxgi.lib 供 preview\_viewport.cpp 创建工厂 | **100% 移除** | 仅视口呈现时加载 | 在 preview\_viewport.cpp 动态解析 CreateDXGIFactory2，删除 dxgi.lib 链接，使 CLI/无头进程零 DXGI 启动加载。 |
| **dwmapi.dll** | rtx\_gui.cpp 调用 DwmSetWindowAttribute 开启暗色标题栏 | **100% 移除** | 可选弱依赖 | 删除 dwmapi.lib；窗体创建时用 LoadLibraryExW \+ GetProcAddress 弱探测 DWMWA\_USE\_IMMERSIVE\_DARK\_MODE，无 DWM（如 Session 0）自动静默跳过。 |
| **comdlg32.dll** | rtx\_gui.cpp 点击浏览时调用 GetOpenFileNameW / GetSaveFileNameW | **100% 移除** | 仅点击浏览时加载 | 删除 comdlg32.lib；在 runFileDialog() 内按需懒加载 comdlg32.dll，避免进程启动期拉入整套 Shell 对话框组件树。 |
| **shell32.dll** | main.cpp 的 CommandLineToArgvW 与 GUI 拖拽 DragQueryFileW | **100% 移除** | CLI 零加载 / GUI 按需 | main.cpp 直接消费 MSVC /MT 原生提供的 \_\_argc / \_\_wargv（零 API 调用）；GUI 仅在收到 WM\_DROPFILES 时懒加载 shell32.dll；删除 shell32.lib。 |
| **ole32.dll** | wasapi\_audio\_output.cpp 的 COM 初始化与分配；RtxFfmpegStatic.cmake 误传依赖 | **100% 移除** | 仅 WASAPI 播放期 | 先从 RtxFfmpegStatic.cmake 拔除冗余 ole32、strmiids、mfuuid、shlwapi、secur32、ws2\_32；再于 wasapi\_audio\_output.cpp 懒加载 CoInitializeEx 等 4 个函数，使转码/探测零 COM 加载。 |
| **gdi32.dll** | workstation\_canvas.cpp 与 rtx\_gui.cpp 基础窗口绘制 | **Core/GUI 解耦后移除** | GUI 内保留 | **遵循 KISS**：GUI 内若强行替换 gdi32.dll 必须引入 d2d1.dll \+ dwrite.dll \+ ole32.dll 重型 COM 栈，属典型反优化。Phase 12 将 GUI 剥离至可选 rtx\_gui.dll / 独立前端后，核心 CineRTX.exe 零 gdi32.dll。 |
| **user32.dll** | Win32 窗口消息循环；RtxFfmpegStatic.cmake 误传链接 | **Core/GUI 解耦后移除** | GUI 内保留 | 立即从 RtxFfmpegStatic.cmake 剔除无用的 user32；Phase 12 核心/界面解耦后，纯无头 CineRTX.exe 彻底摆脱 user32.dll 与桌面会话耦合。 |
| **bcrypt.dll** | 静态链接 FFmpeg (libavutil) 调用 BCryptGenRandom | **保留** | **保留** | 无 GUI/COM 寄生的极简系统密码学原语库，强行魔改 FFmpeg 源码剔除会制造上游维护债与熵源退化，保留最符合奥卡姆剃刀。 |
| **kernel32.dll** | MSVC /MT 静态 CRT、线程/文件/内存与 LoadLibraryExW 基座 | **OS 铁底不可移除** | **不可移除** | Win32 用户态最小运行基座；严禁绕过 kernel32.dll 直调未公开 ntdll.dll 原生系统调用。 |
| **kernelbase.dll** | Windows 内核转发底座（API Set 路由目标） | **顶层 IAT 零直接导入** | **OS 加载期必然映射** | 只要不链接 onecore.lib/mincore.lib，CineRTX.exe 顶层 PE 导入表可保持零 KERNELBASE.dll 条目；但进程创建时 Windows 加载器（LdrpInitializeProcess）必然将其映射进地址空间承接 kernel32.dll 转发，属操作系统物理定律。 |

**最终 IAT 收敛成果**：

* **单 EXE 混合过渡态**：静态导入表由 13 项压缩至 **kernel32.dll、user32.dll、gdi32.dll、bcrypt.dll 4 项**（剥离 9 项）。  
* **Phase 12 核心/界面解耦终态**：无头核心引擎 CineRTX.exe（Pure CLI \+ \--session-stdio JSONL 引擎）静态 IAT 压榨至物理极限——**仅剩 kernel32.dll \+ bcrypt.dll 2 项极简底座**（剥离 11 项）。

## **五、 Phase 11.4 收官至 Phase 12 演进执行路线与硬门禁**

> 1. **即刻动作（文档卫生闭环）**：在 docs/phase11/52\_CineRTX Phase 11.3 止血与竖屏修复终审封板委托书.md 中清除残留的 4 处本地 <local_absolute_path> 绝对路径直链，统一改为仓内相对路径，完成 Phase 11.3 归档净空。  
> 2. **Phase 11.4 决胜冲刺（按 20 号施工单封板 v0.3.0）**：落地 RIFE scale=0.5（$A=128$ 对齐 \+ reflect101 边缘填充与保守 PTX ISA 同步）、基于 7,300 MiB 实际 DXGI 预算的滞后显存自适应调度器（82% 降级 / 68% 恢复 / 120 帧最小驻留）及 Video Codec SDK 13.1 双模协商与环形多缓冲池（输出 AV1 Default / 7B / 15B / 31B \+ UHQ 实测矩阵）；封板确认后经用户最高授权一次性原子化执行 git rebase 清洗远端静态链接历史（方案 B）。  
> 3. **Phase 12 极限现代 C++ 重构与 DLL 净空（迈向 v0.4.0）**：  
   * **类型与体量硬门禁**：升级 AuditTypes.cmake 为 file(READ) 词法扫描器，全仓具名类型压减至 $\leq 115$ 个且 100% 具备真实不可替代性契约；AuditSloc.cmake 锁定负增长平账，将 Pure SLOC 压回 $\leq 30,500$ 行以内。  
   * **二进制导入表硬门禁**：收紧 AuditImports.cmake 白名单，强制核心 CineRTX.exe 静态 IAT 仅允许 kernel32.dll 与 bcrypt.dll（双底座），PE 体积锁定在 \[1.2, 12.0\] MiB 区间，全部单元测试与 RTX 5060 真机零拷贝（D2H \== 0 B）回归 100% PASS。