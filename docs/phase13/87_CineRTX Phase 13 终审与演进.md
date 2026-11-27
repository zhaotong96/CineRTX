# **CineRTX Phase 13 终局封版前专家终审验收报告暨极限现代 C++ 演进方案**

**终审裁决：有条件暂缓封板，驳回当前基线（a1832bc）直接推送 GitHub 的请求（HOLD — DO NOT SEAL OR PUSH）。** 截至 2026-11-27 呈报的基线 a1832bc 在静态导入净化与时域控制上确已取得实质性进展：批 1（84006c2）修复了无头控制台管道输出、内联清除了 WasapiAudioOutput::Impl 使具名类型压降至 33/35，并补齐 MP4 音频/字幕转码与 \+faststart 内存流保护；批 2（a1832bc）落地了基于 \<chrono\> 编译期有理数的 23.976 \-\> 60.000 相位同步、RIFE NV12 偶数几何守卫、user32.dll / gdi32.dll 从 CineRTX.exe 静态导入表（IAT）的彻底剥离，以及 RtxFfmpegStatic.cmake 批处理语法根治1。然而，以极致 Modern C++ 洁癖、奥卡姆剃刀原则与静态零心智负担标准进行穿透审查，当前基线仍存在四项不可豁免的硬性阻断缺陷，必须在封版推送前全部拔除。  
四项硬阻断缺陷直接触及工程底线：第一，AuditImports 仍停留在 \[bcrypt.dll, kernel32.dll\] 的 2/2 过渡态，构建系统中仍保留 \-DSTRICT\_NO\_BCRYPT=ON 这一条件式过渡选项，且委托书拟在清理缓存后同步激活 HAVE\_BCRYPT=0 与 SvtAv1Enc，在缺乏 /MT 无系统库污染实证的情况下极易引入额外 DLL 依赖1；第二，rtx\_tests.exe 全量串行仅录得 164/165 PASS（tests/test\_rife.cpp:444 失败），任何存在前置 GPU 流同步残留的测试集均严禁以“独立运行通过”的名义推上开源主干1；第三，批 2 新引入全局脏标 g\_relayout\_required（src/core/i18n.cpp）与 \_\_d3d12\_h\_\_ 头文件包含顺序依赖（tests/test\_raii\_retirement.cpp），分别构成对 Phase 12 零全局状态准则的架构倒退与典型的隐蔽编译陷阱1；第四，相较 Phase 12（v0.4.0），核心引擎 Pure SLOC 由 12,157 行膨胀至 13,760 行（净增 \+1,603 行，+13.2%），全仓 Pure SLOC 由 31,129 行增至 34,220 行（净增 \+3,091 行，+9.9%），同时 RTX 5060 实机 60fps 帧预算（\< 12.5 ms）尚未取得当期基线实证1。  
在后续演进路线上，CineRTX 应将静态 IAT 严格锁死为 1/1 (kernel32.dll)（对应进程运行时 {kernel32.dll, kernelbase.dll} 及操作系统内核装载基座 ntdll.dll），对未公开清单的 33 个具名类型与 \+1,603 行增量代码实施二次奥卡姆剃刀清查，并在 v0.5.0 至 v1.0.0 周期内严格依据 MSVC 2026（msvc-build-tools-14.51）与 CMake 4.x 的真实成熟度，稳健推进 std::move\_only\_function、std::mdspan、std::flat\_map 与 CUDA Graph 封装，同时主动规避仍处实验状态的 import std; 与未定型特性陷阱1。

## **一、委托书三项核心裁决点穿透批复**

针对 86\_CineRTX Phase 13 终局封版前专家终审委托书.md 第三节提请裁决的三项收口边界，专家组结合 Windows PE 加载机制、FFmpeg/SVT-AV1 构建实证与 TensorRT 异步流模型，下达如下终审裁决。

| 裁决事项 | 委托方呈报现状 | 专家组终审裁决 | 强制前置条件与技术红线 |
| :---- | :---- | :---- | :---- |
| 1\. FFmpeg 重编与静态 IAT 收口 | 拟清缓存重编 FFmpeg，同步开启 HAVE\_BCRYPT=0 与 SvtAv1Enc，配合 \-DSTRICT\_NO\_BCRYPT=ON 收口至 1/1 (kernel32.dll)1 | **必须封版前完成；严禁捆绑 SvtAv1Enc 盲编** | 先单独以 HAVE\_BCRYPT=0 实现 1/1 (kernel32.dll) 并彻底删除 STRICT\_NO\_BCRYPT 编译选项；SvtAv1Enc 须按奥卡姆剃刀剔除或独立审计静态符号1 |
| 2\. RIFE 串行用例失败裁决 | 全量串行 164/165 PASS（tests/test\_rife.cpp:444 失败），单独运行 1/1 PASS（26,976 ms）1 | **驳回豁免；封版前必须源码根治** | 严禁携带已知同步缺陷推送 GitHub；必须根治 test\_rife.cpp 前置用例流同步残留，建议以连续 3 次全量串行 165/165 PASS 作为验收标准1 |
| 3\. 实机性能与长稳压测边界 | 85\_ §4 提及 RTX 5060 Nsight 60fps 帧预算（\< 12.5 ms）与 2 小时 Soak 压测尚未在流水线执行1 | **分层执行：短时帧预算前置，长稳压测归入 Tag 门禁** | Nsight 60fps（\< 12.5 ms）与无头模块快照作为 GitHub 推送前必交物证；2 小时 Soak 归入代码冻结后的正式 Release Tag 前置门禁1 |

### **1.1 FFmpeg 静态重编与 SVT-AV1 接入风险定性**

委托书拟将 AuditImports 收口至 1/1 (kernel32.dll) 的目标完全正确，但「同步激活 HAVE\_BCRYPT=0 与 SvtAv1Enc」的合并变更违反单一职责隔离原则，极易在收口阶段引入新的系统 DLL 污染1。FFmpeg 源码 libavutil/random\_seed.c 在启用 HAVE\_BCRYPT 时引入 \<bcrypt.h\>（BCRYPT\_ALG\_HANDLE）并由 configure 探测 BCryptGenRandom 链接 \-lbcrypt，而早期或特定配置下的回退宏 HAVE\_CRYPTGENRANDOM 则调用 CryptAcquireContext 与 CryptGenRandom（依赖 advapi32.dll）14。在 ./configure 阶段传入 \--disable-bcrypt 使 HAVE\_BCRYPT=0（同时确保关闭 HAVE\_CRYPTGENRANDOM 与 Schannel TLS 通道，防止 advapi32 被连带拉入）后，av\_get\_random\_seed() 将退化至自包含的 get\_generic\_seed() 分支，从而彻底切断 FFmpeg 对 bcrypt.dll 的静态导入12。  
真正的隐患在于同步激活 SvtAv1Enc。当前呈报材料中没有任何实证表明 SvtAv1Enc 能在 /MT 静态链接下保持零额外系统 DLL 导入；恰恰相反，上游 SVT-AV1 的 CMakeLists.txt 默认将 BUILD\_SHARED\_LIBS 设为 ON20，且社区跨平台包管理与打包记录（如 Conan 与 RoboStack 的构建追踪）多次显示 svt-av1 构建链路与 advapi32.lib、user32.lib、bcrypt.lib 以及动态 MSVC 运行库同时出现2。从奥卡姆剃刀与产品定位审视，CineRTX 核心卖点是面向 RTX 5060 等现代 NVIDIA GPU 的全硬件零拷贝 10-bit AV1 编码管线（Phase 12 已实测验证）1；在 Phase 13 临门一脚时往静默纯净的无头计算内核强行捆绑庞大的 CPU 软件 AV1 编码器，既无充分必要场景支撑，又极易摧毁静态导入表的单 DLL 纯洁性。即便业务上确需保留软编降级路径，也必须将 SvtAv1Enc 与本次 HAVE\_BCRYPT=0 收口完全剥离，待使用 dumpbin /directives /imports 实证其 /MT 静态库零系统库残留后方准接入。此外，一旦 1/1 (kernel32.dll) 达成，-DSTRICT\_NO\_BCRYPT=ON 必须从 CMake 中永久删除——构建系统不应存在“允许编译出带 bcrypt.dll 脏版本”的隐藏开关1。

### **1.2 单元测试串行同步泄漏根治标准**

专家组驳回“全量串行失败但独立运行通过即可放行”的降格提议1。将一份在默认全量测试命令下稳定失败（164/165）的仓库推送至 GitHub，既会破坏开源贡献者的第一构建体验，更掩盖了真实的并发/异步资源状态泄漏。  
根据 TensorRT 执行语义，IExecutionContext::enqueueV3(cudaStream) 属于严格的异步提交操作，内核在提交后继续在 GPU 上异步执行，若不配合显式 cudaStreamSynchronize 或 CUDA 事件同步便销毁资源或读取输出，必然产生未定义的数据竞争与错误传播13。委托书亦明确指认该问题涉及 test\_rife.cpp 前置用例遗留的 GPU 默认流同步问题1。在 CUDA 中，若前置测试使用遗留默认流或未在 teardown 阶段等待非默认流执行完毕，后续对时序单调性高度敏感的 rife\_four\_phase\_monotonic\_progression 便会遭遇流间隐式阻塞错序、显存复用踩踏或滞后异步错误码污染1。正确的工程处方不是在测试脚本里搞单独重试，而是在测试夹具与 RIFE 流调度封装中彻底禁用隐式默认流，采用带自定义删除器的 RAII cudaStream\_t 句柄，并在每个用例析构及阶段断言前强制执行流同步与错误寄存器清零检查。专家组建议将“连续三次执行全量串行 rtx\_tests.exe 均稳定达成 165/165 PASS”作为核销该项缺陷的唯一标准1。

### **1.3 硬件性能基线与长稳压测分界**

遵循 Phase 12（v0.4.0）确立的“代码门禁与物理硬件实测双闭环、绝不放过未实证死角”原则4，专家组将 85\_ §4 遗留的两项压测拆分为两道清晰边界1。RTX 5060 宿主机上的 Nsight Systems 60fps 帧预算实测（单帧耗时 \< 12.5 ms，处于 60fps 16.67 ms 帧周期内）与 E2E 零回读（D2H \== 0 B）复核耗时极短，且直接背书本项目开源首页与文档所承诺的实时插帧性能，因此必须作为 **GitHub 推送前的硬性前置门禁**；而耗时 2 小时的 Soak 长稳压测（监测长周期显存水位、系统句柄与音画漂移）则界定为 **源码冻结并推送后的正式 Release Tag（如 v0.5.0）打标门禁**，避免将长周期环境测试与源码定版耦合死锁1。

## **二、极限代码卫生与奥卡姆剃刀反向穿透审计**

将当前基线 a1832bc（2026-11-27）与 Phase 12 终局封板基线 v0.4.0（2026-11-24）进行全维度对账，尽管表面自动化门禁显示为 PASS，深层指标却暴露出体量膨胀、全局状态回潮与头文件隐蔽耦合三项严重卫生退化1。

| 审计维度 | Phase 12 (v0.4.0) 基线 | Phase 13 (a1832bc) 实测 | 演化偏差与洁癖定性 | 强制整改指令 |
| :---- | :---- | :---- | :---- | :---- |
| Core Engine Pure SLOC | 12,157 行4 | 13,760 行1 | **净增 \+1,603 行（+13.2%）**，存在胶水膨胀嫌疑 | 逐文件审计批 1–2 增量，以 C++20/23 泛型折叠重复代码1 |
| 全仓 Pure SLOC | 31,129 行6 | 34,220 行1 | **净增 \+3,091 行（+9.9%）** | 剥离冗余测试桩与宏胶水，收敛至最小必要表达 |
| 全局可变状态 | g\_app 全仓 0 命中4 | 新增 g\_relayout\_required1 | **严重架构倒退**：core/i18n 污染全局重排状态1 | 物理删除 g\_relayout\_required，改为纯返回值或局部纪元比对 |
| 头文件自包含性 | 独立自包含编译 | 须先含 d3d12\_raii.h 再含 raii\_owners.h1 | **典型心智负担隐藏坑**：\_\_d3d12\_h\_\_ 包含顺序耦合1 | 重构 sys\_dynload.h 类型声明隔离，确保任意包含顺序过编 |
| 过渡开关与契约桩 | 零多余开关 | 存在 \-DSTRICT\_NO\_BCRYPT=ON；ContractGuard 锁死 2 个符号1 | 条件开关违反 KISS；须核查被保护符号的生产调用必要性1 | 收口后删除 STRICT\_NO\_BCRYPT；清理僵尸接口守卫 |

### **2.1 拔除全局状态回潮与头文件顺序陷阱**

第 85 号批 2 在 src/core/i18n.cpp 中引入全局脏标 g\_relayout\_required 是一次不可接受的设计妥协1。Phase 12 在 Gate P12-1 中费力清除了全仓的 g\_app 全局变量4，如今核心本地化模块却重新向全局空间抛入一个可变的重排脏标，不仅破坏了多线程单元测试的隔离性，更在 core/ 与 gui/ 之间重建了隐式状态耦合。贯彻纯粹 Modern C++ 与无状态准则，此全局标志必须彻底删除：i18n::setLanguage() 仅需返回 bool（表征语种是否实质切换），或由 i18n 内部维护轻量单调纪元计数器（std::atomic\<std::uint64\_t\>），再由 WorkstationCanvas 持有自身局部成员 last\_lang\_（或局部纪元副本）在布局前完成无副作用比对即可。  
同样违背零心智负担原则的是协调者在 tests/test\_raii\_retirement.cpp 中暴露的“修复”方式：为了规避 \_\_d3d12\_h\_\_ 宏与 sys\_dynload.h 的展开冲突，强行调换 d3d12\_raii.h 与 raii\_owners.h 的 \#include 顺序1。任何需要用户记住“头文件 A 必须写在头文件 B 之前”的工程，本质上都是在埋设未爆弹。根治方案是重构 include/rtx/sys\_dynload.h，严禁其在公开头文件内抢占或模拟 \<windows.h\> / \<d3d12.h\> 的全局宏与结构定义，所有动态加载的句柄与函数指针类型应全部收拢至 namespace rtx::sys::detail 内部，并以不透明句柄（如 void\* 或强类型整数句柄）暴露给上层，仅在 .cpp 实现单元内包含系统 SDK 头文件完成 ABI 转换。修复后须在测试集内以反向 \#include 次序编译通过方算合格。

### **2.2 源码膨胀剖析与物理不变量划界**

由于委托书 86\_ 未提供批 1 与批 2 各源文件的逐文件 SLOC 变更明细1，核心引擎净增的 \+1,603 行必须成为封版前审计的重点标的。首当其冲的疑点位于 sys\_dynload.h、workstation\_canvas.cpp 与 ffmpeg\_io.cpp：当手动剥离 user32.dll / gdi32.dll 静态导入时，极易产出大量手写的函数指针 typedef、空指针判断样板及重复的 GetProcAddress 分派代码；以 C++20 编译期字符串字面量模板或单一无宏函数模板 resolveProc\<Sig\> 配合函数指针缓存表即可将数百行胶水压缩至数十行。其次，鉴于 86\_ 仅摘要描述 src/gui/rtx\_gui.cpp 的 bindParentConsoleIfNeeded() 补充了 SetStdHandle 与 std::cout.clear() / std::cerr.clear()1，源码审查时应实核其仅在标准句柄缺失或指向无效控制台时才接管流状态，避免误覆写父进程已合法重定向的管道句柄。  
在剔除多余防御与冗余逻辑时，必须精准区分“无意义的防御性废话”**与**“底层物理域硬不变量”。批 1 在 src/io/ffmpeg\_io.cpp 中为 \+faststart 补充的 (format\_-\>flags & AVFMT\_FLAG\_CUSTOM\_IO) \== 0 保护，以及批 2 在 src/core/hw\_transcode\_stages.cpp 中针对 RIFE NV12 输入落地的偶数几何硬守卫，均属于绝对必要且不可删除的物理契约——前者防范 movenc 对不可回溯内存流重写索引头部导致必败，后者防止 NV12 半采样色度平面在奇数分辨率下触发越界与张量对齐异常1。相反，任何未在生产链路被真实消费的预留接口、内部私有函数的重复空指针检查、空吞异常的 catch (...) 块，以及 ContractGuard 锁定的 consumeNvofSceneCut 与 isPreviewEnabled（须逐一核验其究竟被正式管线调用，还是仅仅为了照顾历史测试桩而人为保留的僵尸符号）1，一旦确认无生产必要即刻拔除。同时，推送前须依据 Phase 9.1 与 Phase 12 确立的仓库卫生准则，核验仓内除根目录 build.bat 外不存在任何多余的 .ps1 / .py / .sh 脚本碎片，并结合 Gate P12-1 的 getenv 零命中记录，复核 GetEnvironmentVariable 及诸如 CINERTX\_ALLOW\_BUILTIN\_RIFE\_SYNTH 等历史环境变量开关是否已彻底绝灭4。

## **三、自定义类型存废终局清查与标准库先验约束**

本项目确立了近乎苛刻的 Std-First 铁律：凡可直接以 std::tuple、std::variant、std::span、std::array、std::unique\_ptr、std::jthread、std::chrono 等标准库原语表达的结构，一律禁止定义为具名自定义类；凡获准存续的具名类型，必须在其声明上方明确阐述标准库为何失效以及自身锁定的唯一领域不变量26。  
必须坦诚指出一项信息边界：委托书 86\_ 报告了 AuditTypes 已从 34/35 降至 33/35（通过内联移除 WasapiAudioOutput::Impl）并标称 all uniquely justified，同时注明 Task C-1/C-2 为 \+0 具名结构体（PlaybackSyncState 为枚举 enum class），但 **委托书并未披露当前这 33 个存续具名类型的完整清单**1。回溯项目演进史，全仓具名类型经历了 Phase 11.2-R 的 150 个、Phase 12 初的 155 个、Wave 12.2-D 剧减至 34 个（如 MemoryAvioContext 降级为 std::shared\_ptr\<MemoryAvioState\> 别名、MasteringDisplay 降级为 10 元 std::tuple）直至 Phase 13 当前 33 个的连续收拢过程1。由于早期（如 Phase 11.2-R）曾出现过 AuditTypes 仅校验注解格式而放行批量套用模板文案的漏洞26，因此封版前必须导出当前 33 个类型的完整名单并参照下表执行逐一复审，任何在 Phase 12 大裁撤中侥幸留存的伪需求类型仍须就地剥离。

| 历史已知及潜在类型类别 | 标准库先验替代方案 | Phase 13 审查裁定 | 存在必要性显式论证与复核动作 |
| :---- | :---- | :---- | :---- |
| 孤立 Pimpl 包装：WasapiAudioOutput::Impl1 | 编译单元内部自由函数与直持成员 | **已物理拔除（维持零回潮）**1 | 无跨二进制 ABI 隔离需求，此前曾被错误标注，内联移除完全正确1 |
| 独立 C-ABI 删除器：DecoderDeleter、EncoderDeleter、BridgeDeleter 等26 | 单一泛型模板 FnDeleter 或无捕获 Lambda 类型 decltype(\[\](T\* p){...})26 | **若仍存在则强制合并拔除** | 为每个 FFmpeg/CUDA/NGX 句柄单写具名删除器纯属样板膨胀；统一由单模板 FnDeleter 承载，保持 std::unique\_ptr 8 字节零开销26 |
| 扁平数据传输结构：ProbedMedia、FormatMapping、DynamicVsrTarget、PendingEncodeItem、JsonMember、NleWindowState 等26 | std::tuple \+ C++17/20 结构化绑定、std::variant、std::optional26 | **若仍在 33 名单内则逐一清剿** | 上述类型在 Phase 11.2-R 曾被检出套用模板化批注26；除跨核心模块公有接口确有字段歧义风险者外，内部 DTO 应全数降级为匿名聚合或 std::tuple27 |
| 空哨兵标签：SlotTag、DeviceTag、SurfaceTag、StreamTag、EventTag26 | 强类型枚举 enum class 句柄标签 | **若仍在 33 名单内则降级为枚举** | 以空 struct 充当模板标签会无端占用具名类型配额，且易引发无效语义注解；改用 enum class 即可零成本实现强类型区分 |
| 无状态策略与有限状态机：FnDeleter、PlaybackSyncState1 | 无更简练的标准替代 | **批准留存** | FnDeleter 承载编译期非类型函数指针实现零堆开销 C 资源释放；PlaybackSyncState（enum class）精确约束正常、音频漂移（8 ms）与熔断旁路（32 ms）三态边界1 |
| 域不变量与零堆热区：NleProject、EditHistory、CompiledRenderGraph、AudioMixerScratchpad、WsolaStreamState26 | 标准容器无法内建“禁止 GPU 句柄入撤销栈”或“混音热循环 0 堆分配”契约 | **批准留存（限核心不变量）** | 仅当上述类型在当前 33 名单中确切承担不可替代的封装不变量（如 EditHistory 隔离 COM/CUDA 句柄、音频 Scratchpad 锁死 operator new 零分配）时准予存续26 |
| 异步硬件 RAII 主权：D3D12 / CUDA / DirectComposition 视口及延迟退役句柄1 | std::unique\_ptr 无法直接内聚 AddRef 移交与 GPU 围栏析构时序 | **批准留存** | 必须封装不可随意拷贝与不可乱序析构的硬件同步退役边界，从类型系统根除 UAF 野指针风险26 |

## **四、MT 静态链接与系统 DLL 极净边界分析**

实现 Windows 原生桌面与计算应用的极净二进制形态，必须严格区分 **PE 静态导入表（Static IAT）** 与 **OS 加载器运行期模块映射树（Runtime Loaded Modules）**。在 /MT 静态链接 MSVC 运行时（libcmt.lib / libcpmt.lib）并剔除 user32.dll 与 gdi32.dll 后，当前静态 IAT 仅余 \[bcrypt.dll, kernel32.dll\]（2/2），而目标终态是将其收口至 1/1 (kernel32.dll)1。在运行期，Windows 操作系统加载器会自动将 kernel32.dll 中的核心转发调用解析至 kernelbase.dll 与 ntdll.dll，因此静态 IAT 上的单一 kernel32.dll（连同标准库同步原语可能触发的 API Set 转发项）正是对应目标 {kernel32.dll, kernelbase.dll} 极净底座的唯一合法编译期形态4。  
此外，结合 Phase 12 与 Phase 13 的工具链差异，专家组特别提示一项奥卡姆剃刀合并机会：Phase 12 曾维持 CineRTX.exe（桌面 GUI，4 物理 DLL）与 rtx\_headless\_cli.exe（无头节点，2 物理 DLL）双可执行文件并存4；而 Phase 13 已通过 sys\_dynload.h 将 CineRTX.exe 自身的静态图形 DLL 导入清零并实测通过 CineRTX.exe \--headless \--probe1。若当前工程仍同时编译并维护独立的 rtx\_headless\_cli.exe 目标，则应当评估按奥卡姆剃刀将其裁剪合并为单一的 CineRTX.exe（GUI 按需动态加载图形栈，无头参数下永不触发加载），从而从构建源头消除双入口维护成本。全链路组件对静态 IAT 与运行期 DLL 的渗透机理汇总如下表。

| 依赖层与组件 | 触发机制与源码根因 | 静态 IAT 影响 | 运行期模块加载影响 | 极净架构管控红线 |
| :---- | :---- | :---- | :---- | :---- |
| 桌面与界面壳层（sys\_dynload.h） | 剥离 \<windowsx.h\> 宏与静态 user32/gdi32 导入，改用按需动态解析1 | **0 命中**（已清零）1 | 无头模式 0 载入；桌面交互时按需加载1 | 严禁 core/ 或 io/ 反向引用窗口/GDI 符号；无头执行路径永不触发图形栈加载1 |
| FFmpeg libavutil/random\_seed.c | HAVE\_BCRYPT 引用 BCryptGenRandom（-lbcrypt）；HAVE\_CRYPTGENRANDOM 引用 advapi3214 | 导致当前残留 bcrypt.dll（2/2）1 | 运行期映射 bcrypt.dll | 清缓存并以 \--disable-bcrypt（HAVE\_BCRYPT=0 且禁 CryptGenRandom/Schannel）重编，回归 get\_generic\_seed()，收口 1/1 (kernel32.dll)1 |
| SVT-AV1（拟随重编启用） | 默认 BUILD\_SHARED\_LIBS=ON20；社区构建记录中关联 advapi32.lib、user32.lib、bcrypt.lib 及动态 CRT2 | **高危**：极易新增非核心系统 DLL 导入 | 可能引入非预期系统 DLL 或运行库混链风险 | 现阶段严禁打包激活；非必要则剔除，确需保留则须独立验证 /MT 零额外系统库导入 |
| MSVC STL \<chrono\> 时区数据库 | stl/src/tzdb.cpp 显式调用 LoadLibraryExW(L"icu.dll", nullptr, LOAD\_LIBRARY\_SEARCH\_SYSTEM32) 且永不卸载，并读取注册表29 | 无直接静态条目 | **运行期强制映射 icu.dll**，且在旧版 Windows 环境下抛出系统错误30 | **全仓禁绝 C++20 tzdb / 时区查询 API**，仅允许使用无 OS 调用的编译期 duration 与 ratio（如当前 60fps 相位有理数）1 |
| MSVC STL std::random\_device | 历史 MSVC 实现 \_Random\_device() 经由 rand\_s() 调用 advapi32.dll (RtlGenRandom)33；现代 Windows CNG RNG 路由 bcrypt.dll 或 ext-ms-win-cng-rng-l1-1-1 \-\> bcryptprimitives.dll36 | 可能引入加密/RNG API Set | **运行期拖入 bcryptprimitives.dll** 等加密模块 | 核心计算与无头路径禁绝 std::random\_device（当前 2026 STL 源码未直接校验，须以规则封堵）；非密码学随机需求改用确定性 PRNG 与 QueryPerformanceCounter 熵源 |
| MSVC STL std::atomic::wait | stl/src/atomic\_wait.cpp 与 msvcp\_atomic\_wait.src 依赖 api-ms-win-core-synch-l1-2-0.dll 的 WaitOnAddress / WakeByAddress\*28 | 产生 1 项 STL 同步 API Set（Phase 12 已实测记录）4 | OS 解析转发至 kernelbase.dll 与 ntdll.dll39 | 确认 AuditImports 对 api-ms-win-core-synch-\* 前向 API Set 的白名单口径一致，避免误判 |
| OS 运行期基础驻留与 NVIDIA 边界 | Phase 12 无头模块快照除 bcrypt.dll 外曾记录 kernel.appcore.dll 与 msvcrt.dll5；GPU 路径动态加载 nvofapi64.dll（LOAD\_LIBRARY\_SEARCH\_SYSTEM32）与 rtx\_ngx\_bridge.dll1 | 厂商硬件库保持静态 0 导入1 | 核心底层固定驻留 {kernel32, kernelbase, ntdll}，GPU 探针时按需载入桥接 DLL1 | 在 FFmpeg 重编后重新抓取 CineRTX.exe \--headless \--probe 运行期模块快照，确证 bcrypt.dll 清零并核实是否存在可剥离的 msvcrt.dll 残留调用 |

## **五、Phase 13 封板清零施工单与现代 C++ 演进路线图**

只有在以下推送前清零清单 100% 物理闭环后，本项目方具备向 GitHub 推送的工程资格；随后主干应按实测验证过的 C++20/23/26 工具链成熟度阶梯演进，避免盲目追新引入构建碎裂。

### **5.1 推送 GitHub 前强制清零施工单**

> 1. 物理清理 .vs/build/external/ffmpeg-static 后，仅激活 \--disable-bcrypt（确认 HAVE\_BCRYPT=0、HAVE\_CRYPTGENRANDOM=0 且无 Schannel 启用），**禁止同步接入 SvtAv1Enc**，触发 RtxFfmpegStatic.cmake 全量重编，实测验证 AuditImports 收口至 1/1 (kernel32.dll)，随后永久删除 CMakeLists.txt 中的 \-DSTRICT\_NO\_BCRYPT=ON 过渡编译开关1。  
> 2. 若业务确有强制软编需求而不能弃用 SvtAv1Enc，必须将 SvtAv1Enc 拆为独立验证分支，对其静态库执行 dumpbin /directives /imports 检查，确认严格遵循 /MT 且未携带任何 advapi32、user32、bcrypt 或动态 CRT 依赖后方可合入2。  
> 3. 排查 tests/test\_rife.cpp 前置用例及 rife\_engine.cpp 中的 CUDA 流状态，全面改用显式 RAII cudaStream\_t 并在异步 enqueueV3 后与测试夹具销毁前执行 cudaStreamSynchronize 与错误清空1，实现宿主机连续 3 次全量串行执行 rtx\_tests.exe 均达成 165/165 PASS1。  
> 4. 彻底删除 src/core/i18n.cpp 的全局脏标 g\_relayout\_required1，改为 setLanguage() 返回变更布尔值或单调纪元供 WorkstationCanvas 局部比对；重构 include/rtx/sys\_dynload.h 声明边界以拔除 \_\_d3d12\_h\_\_ 包含顺序坑1，在 tests/test\_raii\_retirement.cpp 中验证任意 \#include 顺序均可零警告过编。  
> 5. 导出 AuditTypes 当前 33 个具名类型的完整白名单并按第三节准则完成二次精简（合并单一功能删除器至 FnDeleter、降级内部扁平 DTO、清除非生产僵尸契约桩），同步收束批 1–2 新增的 \+1,603 行核心源码膨胀，并核验 bindParentConsoleIfNeeded() 对重定向管道的安全性及生产代码中不存在多余脚本与隐藏环境变量开关1。  
> 6. 在物理 RTX 5060 宿主机采集 Nsight Systems 60fps 帧预算（实证 \< 12.5 ms）、E2E 零回读日志与 \--headless \--probe 运行期模块快照，确认无 bcrypt.dll 且无图形 DLL 泄漏后方准执行本地封板提交并推送 GitHub；推送后在冻结基线上完成 2 小时 Soak 长稳压测，最终签发正式 Release Tag1。

### **5.2 基于 2026 工具链实况的 C++20 / C++23 / C++26 演进规划**

现代 C++ 特性引入必须以编译器及标准库在目标平台上的真实可用性与构建稳定性为准绳。结合截至 2026 年 MSVC STL（Visual Studio 2026 / msvc-build-tools-14.51）与 CMake 4.x 的官方实现进展9，规划如下分阶段技术演进路线。

| 演进里程碑 | 目标标准与核心特性 | 2026 工具链成熟度依据 | CineRTX 具体重构落地点与工程收益 |
| :---- | :---- | :---- | :---- |
| v0.5.0（近期主干重构） | C++23 std::move\_only\_function | 已在 MSVC STL 合入并完成吞吐优化；2026 年社区实测报告指出 MSVC STL 避免了同类库中的递归堆嵌套退化问题7 | 全面替换任务队列 jobQueue 与异步 GPU 资源退役闭包中的 std::function，直接按值移动捕获 std::unique\_ptr 资源所有权与 std::jthread，消除伪装成 std::shared\_ptr 的逃逸开销7 |
| v0.5.0（近期主干重构） | C++23 std::mdspan | 已在 MSVC STL 合入 P2604R0 与 LWG-4511 推导指引修复7 | 以 std::mdspan\<std::uint8\_t, std::dextents\<std::size\_t, 2\>, std::layout\_stride\> 统一封装 NV12 亮度/色度跨步平面、3D LUT 立方网格与 RIFE 张量视图，根除易越界的手写 pitch 指针偏移算术1 |
| v0.5.0（近期主干重构） | C++23 std::flat\_map / std::flat\_set | P0429R9、P1222R4 及 C++26 修复案 P3567R2 已合入 msvc-build-tools-14.5111 | 替换 i18n 词条索引、编解码格式映射及静态 DAG 拓扑表，以连续内存布局与二分查找消除关联容器零碎节点堆分配1 |
| v0.5.0（异构加速深化） | cudaGraphExec\_t \+ FnDeleter | 原生 CUDA 运行时机制配合 C++20 零开销 RAII 封装26 | 评估将固定分辨率管线中的每帧 GPU 操作（包括颜色转换内核与 TensorRT enqueueV3）固化为单次 cudaGraphLaunch45，压缩 60fps 主循环 CPU 发射开销以稳定裕度运行在 \< 12.5 ms 之下1 |
| v0.6.0（特征宏门控接入） | C++26 std::inplace\_vector 与 std::function\_ref | microsoft/STL\#4766（P0843R14，依赖编译器 P3074R7）与 \#5504（copyable\_function / function\_ref）尚在跟踪实现阶段46 | 待工具链就绪并定义 \_\_cpp\_lib\_inplace\_vector 后，直接替换 AudioMixerScratchpad 等固定上限缓冲区并进一步削减自定义状态类26；待 std::function\_ref 就绪后替代图形命中测试与 DAG 校验中的非拥有型回调1 |
| v0.6.0+（明确暂缓引入） | C++23/26 import std; | CMake 文档明确标注 CXX\_MODULE\_STD 处于实验状态，需在 project() 前硬编码实验 UUID，且 2026 年初微软开发者社区仍存在 VS IntelliSense std.ifc 索引失效与模块格式碎片化问题9 | **现阶段严禁引入**，避免为追求形式主义破坏单一 .\\build.bat 零假设开箱即编体验；待 CMake 移除实验性标签且 IDE 索引完全稳定后再切换替代传统头文件10 |
| v1.0.0（远期自举形态） | C++26 静态反射（Reflection）与契约（Contracts） | C++26 尚处早期编译器落地周期48，生产级跨工具链可用性仍待验证 | 待主流编译器完备支持后，以编译期反射取代 json\_scanner 手写序列化与枚举字符串映射表，并以语言级契约替代外部正则门禁脚本 ContractGuard 与 AuditTypes，实现完全由编译器自举的架构洁净度1 |

综上所述，基线 a1832bc 已打下极净静态 IAT 与无状态界面的坚实底盘，但尚未跨越最后一道代码洁癖与测试确定性红线。请协调负责人立即按第 5.1 节清单逐项消号，待 1/1 (kernel32.dll) 静态导入、连续三次全量串行 165/165 PASS、零全局脏标与头文件乱序编译通过、以及 RTX 5060 短时帧预算实证全部落盘后，再正式签发 Phase 13 终局封板令并推送至 GitHub。

#### **引用的著作**

> 1. 86\_CineRTX Phase 13 终局封版前专家终审委托书.md, [[REDACTED_ID]]([REDACTED_ID])  
> 2. Fresh Windows installation not finding FastDDS implementation. \#6, [https\://github.com/RoboStack/ros-humble/issues/6](https://github.com/RoboStack/ros-humble/issues/6)  
> 3. \[package\] ffmpeg/6.1: Build fails on Windows, missing aom \#23309, [https\://github.com/conan-io/conan-center-index/issues/23309](https://github.com/conan-io/conan-center-index/issues/23309)  
> 4. 69\_CineRTX Phase 12 发版前最后一关穿透终审裁决书暨v0.4.0正式发版核准令.md, [[REDACTED_ID]]([REDACTED_ID])  
> 5. 68\_CineRTX Phase 12 全量实机回归通关暨v0.4.0发版前终审专家咨询委托书.md, [[REDACTED_ID]]([REDACTED_ID])  
> 6. 66\_CineRTX Phase 12 Wave 12.4 终局收官与v0.4.0正式发版终审封板专家咨询委托书.md, [[REDACTED_ID]]([REDACTED_ID])  
> 7. Changelog · microsoft/STL Wiki · GitHub, [https\://github.com/microsoft/STL/wiki/Changelog/244a4de4dce2f80c288e14799bbb4e25eb933e03](https://github.com/microsoft/STL/wiki/Changelog/244a4de4dce2f80c288e14799bbb4e25eb933e03)  
> 8. Compare revisions · microsoft/STL Wiki \- GitHub, [https\://github.com/microsoft/STL/wiki/Changelog/\_compare/a88d51ced71b3f5d47d4ebd5e47ccbe1a459716e](https://github.com/microsoft/STL/wiki/Changelog/_compare/a88d51ced71b3f5d47d4ebd5e47ccbe1a459716e)  
> 9. Importing std with CMake / Visual Studio \- Stack Overflow, [https\://stackoverflow.com/questions/79841075/importing-std-with-cmake-visual-studio](https://stackoverflow.com/questions/79841075/importing-std-with-cmake-visual-studio)  
> 10. \`\`import std\`\` doesn't work on windows \- Usage \- CMake Discourse, [https\://discourse.cmake.org/t/import-std-doesnt-work-on-windows/14268](https://discourse.cmake.org/t/import-std-doesnt-work-on-windows/14268)  
> 11. P0429R9 \`\` · Issue \#2910 · microsoft/STL \- GitHub, [https\://github.com/microsoft/STL/issues/2910](https://github.com/microsoft/STL/issues/2910)  
> 12. win32 version have some bug? · Issue \#25 · sudo-nautilus/FFmpeg, [https\://github.com/sudo-nautilus/FFmpeg-Builds-Win32/issues/25](https://github.com/sudo-nautilus/FFmpeg-Builds-Win32/issues/25)  
> 13. TensorRT与ONNX Runtime本质差异：编译器vs运行时中间件原创, [https\://blog.csdn.net/weixin\_29800471/article/details/170391068](https://blog.csdn.net/weixin_29800471/article/details/170391068)  
> 14. libavutil/random\_seed.c Source File \- FFmpeg, [https\://ffmpeg.org/doxygen/2.5/random\_\_seed\_8c\_source.html](https://ffmpeg.org/doxygen/2.5/random__seed_8c_source.html)  
> 15. libavutil/random\_seed.c Source File \- FFmpeg, [https\://ffmpeg.org/doxygen/8.1/random\_\_seed\_8c\_source.html](https://ffmpeg.org/doxygen/8.1/random__seed_8c_source.html)  
> 16. \[FFmpeg-devel\] \[PATCH v4 2/4\] avutil/random\_seed: add, [https\://ffmpeg.org/pipermail/ffmpeg-devel/2023-July/311650.html](https://ffmpeg.org/pipermail/ffmpeg-devel/2023-July/311650.html)  
> 17. \[FFmpeg-devel\] \[PATCH\] configure: extend the check for bcrypt, [https\://ffmpeg.org/pipermail/ffmpeg-devel/2018-April/228306.html](https://ffmpeg.org/pipermail/ffmpeg-devel/2018-April/228306.html)  
> 18. configure \- chromium/third\_party/ffmpeg \- Git at Google, [https\://chromium.googlesource.com/chromium/third\_party/ffmpeg/+/21d322f670919495bd39917a70d71d85ac3d109c/configure](https://chromium.googlesource.com/chromium/third_party/ffmpeg/+/21d322f670919495bd39917a70d71d85ac3d109c/configure)  
> 19. \[ffmpeg\] Dependencies Secur32.lib and Bcrypt.lib are missing when, [https\://github.com/microsoft/vcpkg/issues/9571](https://github.com/microsoft/vcpkg/issues/9571)  
> 20. SVT-AV1/CMakeLists.txt at main · AOMediaCodec/SVT-AV1 · GitHub, [https\://github.com/AOMediaCodec/SVT-AV1/blob/main/CMakeLists.txt](https://github.com/AOMediaCodec/SVT-AV1/blob/main/CMakeLists.txt)  
> 21. TensorRtSharp：在C\# 世界中释放GPU 推理的极致性能 \- 博客园, [https\://www\.cnblogs.com/guojin-blogs/p/19468745](https://www.cnblogs.com/guojin-blogs/p/19468745)  
> 22. TensorRT与ONNX Runtime对比：GPU模型部署与推理性能优化实战, [https\://blog.csdn.net/weixin\_32181267/article/details/170672891](https://blog.csdn.net/weixin_32181267/article/details/170672891)  
> 23. Check if first enqueue() execution is completed \- TensorRT, [https\://forums.developer.nvidia.com/t/check-if-first-enqueue-execution-is-completed/122044](https://forums.developer.nvidia.com/t/check-if-first-enqueue-execution-is-completed/122044)  
> 24. Is enqueueV2 thread safe? \- TensorRT \- NVIDIA Developer Forums, [https\://forums.developer.nvidia.com/t/is-enqueuev2-thread-safe/246719](https://forums.developer.nvidia.com/t/is-enqueuev2-thread-safe/246719)  
> 25. CineRTX\_Phase9.1\_推送前仓库卫生完善与终审封板专家委托书.md, [[REDACTED_ID]]([REDACTED_ID])  
> 26. CineRTX Phase 11.2-R 终审方案, [[REDACTED_ID]]([REDACTED_ID])  
> 27. 51\_CineRTX Phase 12 Wave 12.2-D 终局类型达成与单点编译止血专家咨询委托书.md, [[REDACTED_ID]]([REDACTED_ID])  
> 28. : Header is missing from this repo · Issue \#613 \- GitHub, [https\://github.com/microsoft/STL/issues/613](https://github.com/microsoft/STL/issues/613)  
> 29. : icu.dll is not unloaded \#2316 \- microsoft/STL \- GitHub, [https\://github.com/microsoft/STL/issues/2316](https://github.com/microsoft/STL/issues/2316)  
> 30. Implement fallback mechanism for chrono's \`\[time.zone\]\` · Issue \#1911, [https\://github.com/microsoft/STL/issues/1911](https://github.com/microsoft/STL/issues/1911)  
> 31. \`\`: Improve exception messages when OS support is, [https\://github.com/microsoft/STL/issues/4331](https://github.com/microsoft/STL/issues/4331)  
> 32. \`\`: \`current\_zone()\` throws on "British Columbia" timezone, [https\://github.com/microsoft/STL/issues/6490](https://github.com/microsoft/STL/issues/6490)  
> 33. The implementation of random\_device in VS2010? \- Stack Overflow, [https\://stackoverflow.com/questions/9549357/the-implementation-of-random-device-in-vs2010](https://stackoverflow.com/questions/9549357/the-implementation-of-random-device-in-vs2010)  
> 34. 当std::random\_device熵未知时如何初始化std：：is 19937？(VS C, [https\://developer.cloud.tencent.com/ask/sof/108005523](https://developer.cloud.tencent.com/ask/sof/108005523)  
> 35. How to initialize std::mt19937 when std::random\_device entropy is, [https\://stackoverflow.com/questions/63727919/how-to-initialize-stdmt19937-when-stdrandom-device-entropy-is-unknown-vs-c](https://stackoverflow.com/questions/63727919/how-to-initialize-stdmt19937-when-stdrandom-device-entropy-is-unknown-vs-c)  
> 36. Modern CRNG on Windows \#123567 \- dotnet runtime \- GitHub, [https\://github.com/dotnet/runtime/discussions/123567](https://github.com/dotnet/runtime/discussions/123567)  
> 37. STL/stl/src/atomic\_wait.cpp at main · microsoft/STL \- GitHub, [https\://github.com/microsoft/STL/blob/main/stl/src/atomic\_wait.cpp](https://github.com/microsoft/STL/blob/main/stl/src/atomic_wait.cpp)  
> 38. atomic waits do not link in ARM64 \#1504 \- microsoft/STL \- GitHub, [https\://github.com/microsoft/STL/issues/1504](https://github.com/microsoft/STL/issues/1504)  
> 39. Is there a reason to use a mutex over a binary\_semaphore ? : r/cpp, [https\://www\.reddit.com/r/cpp/comments/1lcznhy/is\_there\_a\_reason\_to\_use\_a\_mutex\_over\_a\_binary/](https://www.reddit.com/r/cpp/comments/1lcznhy/is_there_a_reason_to_use_a_mutex_over_a_binary/)  
> 40. Visual Studio 2026 release notes \- Microsoft Learn, [https\://learn.microsoft.com/en-us/visualstudio/releases/2026/release-notes](https://learn.microsoft.com/en-us/visualstudio/releases/2026/release-notes)  
> 41. Changelog · microsoft/STL Wiki \- GitHub, [https\://github.com/microsoft/STL/wiki/Changelog/5f4bdb7a17f76a50b6cdb4c736d113ee7e5f5148](https://github.com/microsoft/STL/wiki/Changelog/5f4bdb7a17f76a50b6cdb4c736d113ee7e5f5148)  
> 42. Interconverting std::function and copyable\_function \- daily.dev, [https\://daily.dev/posts/interconverting-std-function-and-copyable-function-az8s7dlex](https://daily.dev/posts/interconverting-std-function-and-copyable-function-az8s7dlex)  
> 43. Your Callback Chain Is Free Until It's 32 Deep \- HFT University, [https\://hftuniversity.com/post/your-callback-chain-is-free-until-it-s-32-deep](https://hftuniversity.com/post/your-callback-chain-is-free-until-it-s-32-deep)  
> 44. Releases · microsoft/STL \- GitHub, [https\://github.com/microsoft/STL/releases](https://github.com/microsoft/STL/releases)  
> 45. dfine-cpp/docs/HANDOFF.md at main \- GitHub, [https\://github.com/PogChamper/dfine-cpp/blob/main/docs/HANDOFF.md](https://github.com/PogChamper/dfine-cpp/blob/main/docs/HANDOFF.md)  
> 46. P0843R14 \`\` · Issue \#4766 · microsoft/STL \- GitHub, [https\://github.com/microsoft/STL/issues/4766](https://github.com/microsoft/STL/issues/4766)  
> 47. \`\`: Avoid double wrapping in \`move\_only\_function, [https\://github.com/microsoft/STL/issues/5504](https://github.com/microsoft/STL/issues/5504)  
> 48. Already end of year 2025, still C++23 import does not work in most, [https\://www\.reddit.com/r/cpp/comments/1p1118u/already\_end\_of\_year\_2025\_still\_c23\_import\_does/](https://www.reddit.com/r/cpp/comments/1p1118u/already_end_of_year_2025_still_c23_import_does/)  
> 49. IntelliSense cannot find the "std" module when building with CMake, [https\://developercommunity.microsoft.com/t/10936508](https://developercommunity.microsoft.com/t/10936508)  
> 50. CXX-Modules: import std does still not work on Linux with clang, [https\://discourse.cmake.org/t/cxx-modules-import-std-does-still-not-work-on-linux-with-clang/15262](https://discourse.cmake.org/t/cxx-modules-import-std-does-still-not-work-on-linux-with-clang/15262)  
> 51. ADSP: Algorithms \+ Data Structures \= Programs \- Buzzsprout, [https\://feeds.buzzsprout.com/1501960.rss](https://feeds.buzzsprout.com/1501960.rss)