# **CineRTX Phase 11.2-R 专家终审穿透复核与 Phase 11.3 解封收口方案**

针对项目总协调负责人呈报的 33 号终审验收委托书（CINERTX-P11-CONSULT-033），专家组对照 29 号专家裁决方案与 30 号施工单，对仓内核心源码、CMake 门禁脚本、注释回植脚本以及 32 号、34 号验收决议记录进行了源码级穿透核验1。针对委托书提请终审的两项核心要项，正式裁定如下：对第一项**项目卫生达标裁决**给予「有条件通过（CONDITIONAL PASS）」——全仓在运行时内存安全（P0 级 UAF 野指针根除）、AuditTypes.cmake 控制流修复、标准库先验替换底座（FnDeleter、std::jthread、有理数 Flicks、单线程音频 seek\_epoch）、readFixture 零硬编码寻址以及真机桌面百次 Resize 压测等关键架构底盘上已达到工业级生产标准1；但源码穿透同时检出四项未在 32 号、34 号文档中暴露的微观卫生与口径尾差，须锁定为 Phase 11.3 开工首日（Day-0）消号任务，预计约 30 分钟即可修改闭环2。  
对第二项**解封 Phase 11.3 施工裁决**给予「批准封板并附条件解封（APPROVED WITH ADDENDUM）」——即刻批准 Phase 11.2-R 结案封板并解除对 Phase 11.3 的熔断冻结令，准予向外部执行工程师派发 19 号施工单1。但鉴于现行 19 号施工单（CINERTX-P11-WORKORDER-019-REV2）定稿于 Phase 11.2-R 治理之前，文档内仍残留已被删除的旧符号名称（VarispeedCursor）、面向 Git 分支的参考指引以及本地绝对路径链接3，协调负责人须连同一份简洁的《Phase 11.3 派发勘误附表》与本文第三节的 Day-0 显微修补清单一并下发，以确保后续 19 号单施工不发生基线错位与体量反弹2。

## **一、 裁决要项 1 源码级穿透复核：实质达标项与遗留盲区**

对 2026-11-21 最新落盘源码树（更新时间涵盖 10:19 至 10:42）的逐文件核验表明，外部执行工程师与协调负责人已真实落地 29 号裁决方案要求的核心架构与安全修复，但自动化脚本回植后的文本精度与部分存续声明的语义保真度尚存死角5。

| 审查维度与核心标的 | 源码物理实证与已兑现成果 | 穿透复核新检出的微观卫生瑕疵 | 终审定性 |
| :---- | :---- | :---- | :---- |
| **1\. AuditTypes.cmake 逻辑与 150 类型合规** | cmake/AuditTypes.cmake 已拔除 if(NOT \_justified) continue() 反转漏洞，引入 alignas(...) 正则预处理，支持继承、单行结构体及 const/static 修饰，强制校验 Why-Std-Fails/Invariant 非空及 8 行超窗孤立块报错；types.h 五个哨兵标签（SlotTag、DeviceTag、SurfaceTag、StreamTag、EventTag）与 EncoderDeleter 已补标，全仓 150 个具名类型扫描 0 违规；InspectorRow/Group/BrowseSpec 已收敛为 std::tuple4。 | 脚本仅能校验语法格式而非语义贴合度：WasapiAudioOutput::Impl 错标为 Domain-Model；FormatMapping 与 NleWindowState 错标为 NonTrivial-StateMachine；DecoderDeleter、BridgeDeleter、DynamicVsrTarget、ProbedMedia、FrameBuffer、PendingEncodeItem 与 JsonMember 等保留了批量套用的模板文案7。 | **结构 PASS / 语义待纠偏** |
| **2\. P0 级 UAF 野指针根除与异步退役** | WorkstationApp 成员严格按 playbackSink 先于 playbackSession 声明、previewViewport 居末重排；stopPlaybackSession() 显式执行先 playbackSession.reset()（join 工作线程）→ waitForGpuIdle(1000) → drainDeferredRetirement() → playbackSink.reset()；slot\_flags\_、preview\_slot\_flags\_、PreviewSlotBorrow 与 PreviewSlotTicket 升级为 std::shared\_ptr\<std::array\<std::atomic\<uint32\_t\>, 3\>\>，并在 presentShared() 中结合 res-\>AddRef() 移交退役闭包7。 | rtx\_gui.cpp 的 WM\_DESTROY 在释放 previewViewport 与发起 PostQuitMessage(0) 前未显式调用 g\_app.jobQueue.skipCurrentJob() 通知队列停机；playback\_session\_video.cpp 中 performSeek() 直接调用 video\_worker.get\_stop\_token()，改为由 videoLoop(stop) 显式透传 stop 令牌更为严谨14。 | **PASS（P0 彻底清零）** |
| **3\. readFixture 与跨平台构建** | tests/test\_playback\_session.cpp 已落地 Win32 动态扩容 GetModuleFileNameW（上限 32,768 宽字符）与 Linux /proc/self/exe 三级寻址（exe 同级 fixtures/ → CWD → 向上 6 层 tests/fixtures/）并校验 tellg()/gcount()；根 CMakeLists.txt 已将 .mkv POST\_BUILD 拷贝移出 if(WIN32) 且为 rtx\_unit\_tests 绑定 WORKING\_DIRECTORY；构建目录与项目根目录双向运行均 128/128 PASS4。 | 无遗留代码缺陷8。 | **PASS** |
| **4\. 注释无损回植与投机产物销毁** | docs/archive/phase112r\_governance\_archive.md（145 KB）与 22 处 \*.pre30.bak 备份已物理清除；31 号汇报 25 处外部私有链接已脱敏归零；raii\_owners.h、playback\_session.h、playback\_session\_impl.h、ngx\_remaster.cpp、logger.cpp 的多行契约完整缝合4。 | scripts/reimplant\_phase112r\_comments.py 的 5 行回溯保护窗口导致 include/rtx/time\_model.h 中两个类型的半截契约续行被逆向顶出至 @rtx-type-justification 上方；d3d12\_raii.h、wasapi\_audio\_output.cpp、json\_scanner.cpp 与 test\_raii\_retirement.cpp 存在多处注释跌入分支或赋值体内部的行号漂移10。 | **需 Day-0 定向对齐** |
| **5\. 标准库先验替换、GUI 消肿与内核提纯** | com\_owner.h 物理删除，raii\_owners.h 收敛为 4 行 FnDeleter \+ std::unique\_ptr 别名；工作线程全面切至 std::jthread；时间基落地有理数 Flicks（0.7056 清零）；音频去锁确立 seek\_epoch 单线程亲和；test\_streams.h 删除（-1,306 行）并就位 3 份实体 .mkv；rtx\_gui.cpp 清零 CBN\_SELCHANGE 残渣与重复常量（920 行），workstation\_canvas.cpp 完成表驱动与 DC\_BRUSH 改造（1,006 行）；playback\_session.cpp 抽取公共控制助手并以 IndirectDeleter 替代 4 个单行包装函数（225 行）1。 | workstation\_canvas.cpp 中 ChromeText::SendEncoder 仍残留 L"ðŸš€ ..." 宽字符乱码（Phase 11.1 A-05 遗留欠账尚未销号）；playback\_session\_impl.h 与 \_video.cpp 未按 29 号方案合回单文件而保持四文件分置（但均已完成实质去重）；rtx\_gui.cpp 的 WM\_TIMER 仍用裸整数乘除换算 playhead\_flicks5。 | **主干 PASS / 须消号 A-05 乱码** |

### **1.1 异步内存安全与三槽退役闭环验证**

在运行时生命周期方面，WorkstationApp 与 PreviewViewport 的双向 UAF 风险已通过结构化所有权拓扑彻底阻断。运行时调用 stopPlaybackSession() 时，程序首先置空 playbackBorrow 并在步骤 1 调用 g\_app.playbackSession.reset()7；由于 MediaPlaybackSession::Impl 将 std::jthread audio\_worker 与 video\_worker 置于成员声明末尾，析构自动触发协作式停止请求并完成 join()，在物理层面切断一切后台线程向 IVideoFrameSink::present() 的写入23。随后步骤 2 执行 waitForGpuIdle(1000) 与 drainDeferredRetirement() 排空视口命令队列，最后释放 g\_app.playbackSink7。  
在极限超时场景下，PreviewViewport::presentShared() 向 GpuRetirementQueue 入队前已对借用的 shared\_resource 调用 res-\>AddRef()，并将携带 ticket.first（std::shared\_ptr\<std::array\<std::atomic\<std::uint32\_t\>, 3\>\>）的清理闭包一并托管9。即便 waitForGpuIdle(1000) 返回 ErrorCode::Timeout 导致 releaseGpu() 将队列转入进程级 deferRetirement() 延迟池9，后续退役回调执行 (\*flags)\[slot\_index\].store(0) 时仍安全持有该 12 字节原子标志块与 D3D12 资源的共享所有权，不会因 HeadlessPlaybackSink 先行销毁而触发野指针解引用9。唯一建议在 Phase 11.3 顺手加固的两处防御细节，是在 rtx\_gui.cpp 的 WM\_DESTROY 中于销毁主窗前显式停机 g\_app.jobQueue（避免依赖全局 g\_app 在进程退出期析构时才收尾工作线程并消除潜在窗口句柄销毁后的 PostMessageW 竞态）14，以及将 playback\_session\_video.cpp 中 performSeek() 内部取用的 video\_worker.get\_stop\_token() 改为由 videoLoop(std::stop\_token stop) 显式传参21。

### **1.2 自动化注释回植漂移与契约截断取证**

逐行核查 scripts/reimplant\_phase112r\_comments.py 的插入逻辑可知，自动化脚本在向剩余 89 个文件回补 1,890 行注释时触发了两种定位偏移4：  
第一类是**契约续行逆向顶出漏洞**。脚本中的 reimplant\_file() 在计算插入位置 idx 后，会检查前 5 行窗口内是否存在 "@rtx-type-justification"，若命中则不断执行 idx \-= 1 以避免把注释插进声明块内部12。然而，当归档中待回补的文本本身就是多行 Why-Std-Fails: 或 Invariant: 被腰斩的后半句（原行号恰好位于 @rtx-type-justification 标记下方第 2–4 行）时，该循环反而把 idx 一直回退到 @rtx-type-justification 标记行的正上方。源码实证显示，include/rtx/time\_model.h 中 TimecodeFormat 的 Why-Std-Fails: 仍截断于 std::pair 丢失字段、Invariant: 截断于 其他帧率一律，其下半句 // 语义，formatSmpteTimecode 的六类白名单时间码契约以具名字段表达。 与 // 渲染为 non-drop。 被顶到了 // @rtx-type-justification 之前；同文件 IncrementalFlicksAccumulator 的 Why-Std-Fails: 亦截断于 std::chrono，下半句 // 时钟与 std::accumulate... 同样孤悬于标记块上方10。  
第二类是**行号基准漂移引发的块内错位**。由于目标源码在注释被剥离后又经历了重构增删行与 A-1 格式清理（-83 行），脚本按旧行号 orig\_ln \- 1 直接插入导致多处功能注释偏离原上下文 1–5 行3。例如 time\_model.h 中解释 advanceOneStep() 与 formatSmpteTimecode() 的函数头注释分别跌入了首个 if 错误提前返回分支内10；wasapi\_audio\_output.cpp 中 // 暂停即冻结... 从 pause() 跌入 resume() 的 if 块尾部，而 // 红线区 落入了 if (FAILED(hr)...) return; 分支内部18；d3d12\_raii.h 中解释 device\_lost\_seen\_ 的注释跌入了 releaseAll() 的 for 循环内部，持久映射上传堆的段标题与说明则插入了 D3D12\_RESOURCE\_DESC desc 字段赋值之间28；json\_scanner.cpp 中的 // High surrogate 与 // 孤立代理项 错位于分支尾部及 default: 返回之后，// Fallback for systems... 亦由 memberDouble 偏入 memberBool29；test\_raii\_retirement.cpp 中的 // 模拟 WAIT\_TIMEOUT 与 // DXGI\_ERROR\_DEVICE\_REMOVED 则错插进相邻测试用例的断言上方27。

### **1.3 类型审计语义盲区与编码卫生残留**

30 号施工单已明确禁令：“恢复真实的 Why-Std-Fails 与 Invariant，禁止盲目复制粘贴”3。虽然 AuditTypes.cmake 修复后实测 150 named types, all justified4，但纯正则脚本无法识别内容真伪，当前源码仍残留三处典型类别错贴与多处模板粘贴：src/core/wasapi\_audio\_output.cpp 中的 WasapiAudioOutput::Impl 封装了 5 个 ComPtr、2 个 UniqueHandle、7 个 alignas(64) 原子游标及 std::jthread worker\_，却被误标为“承载序列化/校验/反射契约”的 Domain-Model（实应为 NonTrivial-StateMachine）18；src/shim/ngx\_bridge/d3d12\_interop.cpp 的纯静态查表行 FormatMapping 与 src/gui/workstation\_canvas.cpp 的 3 字段窗口状态 NleWindowState 均无异步资源生命周期，却套用了“声明序即析构逆序”的 NonTrivial-StateMachine 模板7；src/core/hw\_transcode\_internal.h 中的 DecoderDeleter 与 BridgeDeleter 套用了 CudaDriverApi 的 extern "C" 函数指针表模板（EncoderDeleter 虽定制了 Invariant，但 Why-Std-Fails 仍保留了相同的 extern "C" 误植前缀）15；而 DynamicVsrTarget、ProbedMedia、FrameBuffer、PendingEncodeItem 与 JsonMember 则沿用了 TranscodeProfile 的“schema/测试逐字段锚定；只增不删”模板13。此外，src/gui/workstation\_canvas.cpp 中 ChromeText::SendEncoder 仍保留 L"ðŸš€ 导出序列至转码器" 的 UTF-8 误解码乱码，使 Phase 11.1 审计项 A-05 至今未能彻底销号7。

## **二、 八大门禁复核与双轨体量口径纠偏**

综合 32 号封板记录与源码核查，Gate R-1 至 Gate R-8 的实测状态、文档口径差异与终审定性汇总如下1。

| 门禁编号 | 核验焦点与原始规范 | 32 号记录实测物证 | 专家穿透复核与口径纠偏 | 终审定性 |
| :---- | :---- | :---- | :---- | :---- |
| **Gate R-1** | 全仓体量与去 Slop（红线 \<= 39,626） | Pure SLOC 38,562 行；注释 4,827 行；物理总行 46,256 行（143 个源文件）；核定赤字 \~1,200 行1。 | 32/33 号文存在“Pure SLOC 与物理行基线跨口径相减”及“143 文件列表未透明披露”问题，须在 Day-0 以固定集合脚本重标定基线（见 §2.1）1。 | **CONDITIONAL PASS** |
| **Gate R-2** | 播放内核体量与无锁拓扑（\<= 500 行） | playback\_session.cpp 225 行；0.7056、audio\_state\_mutex 与 8 个冗余包装类 0 残留1。 | 保留 \_impl.h/\_video.cpp/\_audio.cpp 分置未合回单文件，但控制等待、时钟暂停、解码重建与 IndirectDeleter 4 处实质去重已真实兑现5。 | **PASS** |
| **Gate R-3** | GUI 边界与内存安全（\< 1,000 行） | rtx\_gui.cpp 920 行；workstation\_canvas.cpp 1,006 行；P0 UAF 根除1。 | 须在 Day-0 清除 ChromeText::SendEncoder 宽字符乱码（A-05）7。 | **CONDITIONAL PASS** |
| **Gate R-4** | 测试夹具纯洁度 | test\_streams.h（-1,306 行）删除；3 份实体 .mkv（8.9 KB / 9.5 KB / 10.9 KB）就位1。 | 版本库零十六进制码流数组残留，100% 达标4。 | **PASS** |
| **Gate R-5** | 类型系统存在性声明 | AuditTypes.cmake 输出 OK: 150 named types, all justified，0 违规4。 | 语法扫描合规，须在 Day-0 缝合 time\_model.h 断句并修正 3 处类别错贴与模板粘贴7。 | **CONDITIONAL PASS** |
| **Gate R-6** | 双目录单测与二进制包络 | 双目录运行 rtx\_tests.exe 均 128/128 PASS（构建目录 114,266 ms，根目录 2 项夹具用例 6,287 ms）；AuditSpdx PASS（GPL=97, MIT=37）；PE 5.04 MB 零驱动 DLL 泄漏4。 | 纠正 32 号文决议表的笔误：AuditImports.cmake 规范包络为 \[1.2 MB, 12.0 MB\]（5.04 MB 合规），而非表内误写的 PE \<= 4.5 MB4；另 32 号文未附终版 CineRTX.exe.map 修饰符号检索日志，须在 Day-0 补录留档4。 | **PASS（待补 .map 终版日志）** |
| **Gate R-7** | 跨工具链零警告构建 | MSVC /MT 14.51（C++26）30/30 目标 0 错误 0 警告4。 | 32 号终审仅执行了 MSVC 构建；CMakeLists.txt 与 readFixture 的 Linux 路径改造须在 Phase 11.3 首周 CI 中补测 linux-gcc 与 Zig 0.13 兼容性4。 | **PASS（MSVC 实证）** |
| **Gate R-8** | 真实交互式桌面实呈闭环 | RTX 5060 \+ 621.24 WHQL \+ Session 1 实测 100 次 Resize 0 崩溃，presented \> 0，D2H \== 0 B，20 次启停无 UAF，音频热拔插续播正常4。 | 完全补全 25/29 号单规定的物理桌面硬门禁4。 | **PASS** |

### **2.1 双轨体量账本跨口径错配修正**

32 号与 33 号文档中的双轨体量账本存在两处必须指正的数学口径不一致，若不澄清，将在 Phase 11.3/11.4 追偿验收时引发争议。首先，账本用当前全仓的**纯净有效代码行**（38,562 Pure SLOC）直接减去 Phase 11.1 的**物理总行数**基线（37,626 行），由此得出“净增 \+936 行（\<= \+2,000）且较 39,626 物理行红线富余 1,064 行”的结论1。但 23 号、25 号与 29 号文档均确认，37,626 与 39,626 是包含全部注释与空行的**物理总行数**口径；而 27 号文在 5 个主要目录（41,333 物理行）的实测中，Pure SLOC 为 33,859 行（占比 81.9%，注释 3,549 行占 8.6%，空行 3,925 行占 9.5%）5。由于仓库并未单独留存 Phase 11.1 基线的同口径 Pure SLOC 实测值，直接将 Pure SLOC 与物理行红线混算作差在度量上并不成立。  
其次，32 号文对 143 个源文件测得物理总行数 46,256 行（由 38,562 Pure SLOC、4,827 注释行及差额 2,867 空行构成）4，但未给出逐文件统计列表或文件 glob 范围，故无法直接解释其为何较 29 号文在同一阶段复核的 42,334 物理行多出 3,922 行（可能源于统计集合从 23 号文的 120 个核心文件扩至 143 个文件、纳入新增 Python/PS1 脚本，或回植与新增标注的叠加）4。而 32 号、33 号文保留的 **\~1,200 行技术赤字**，实际上严格源自 29 号方案基于物理行账本的推导：在 42,334 行上扣除 Path A 真实消肿（600–760 行）至 41,570–41,730 行，减去 39,626 物理红线后再核销 \~811 行不可抗预估偏差（多出的百余个类型标注与门禁 495 行 \+ 过期 preview\_viewport 基线高估 299 行 \+ 快照差 17 行），即得出 \~1,140–1,300 行的追偿额度1。  
为彻底终结统计口径漂移，专家组确立 Phase 11.3/11.4 的**同口径双轨锁定基准**：不再追溯混算 Phase 11.1 的物理行数，直接以新增脚本 cmake/AuditSloc.cmake 在固定源码集合（include/、src/、tests/、cmake/、根 CMakeLists.txt 及 schema/ 核心三文件）上实测所得的 Pure SLOC 读数作为 Phase 11.3 入场零点水位（以 32 号文口径即锁定为 38,562 Pure SLOC 与 4,827 注释行），后续两阶段所有追偿扣减均在同一固定集合上直接检验 Pure SLOC 的净减少量，确保在 v0.3.0 封板前完成全部 \~1,200 行赤字平账1。

## **三、 Day-0 外科级显微修补蓝图**

以下修补项总计涉及约 60 行局部文本调整与 1 份轻量 CMake 统计脚本，建议在 Phase 11.3 开工第 1 日（Day-0）优先落盘消号，避免带瑕进入 NLE 引擎建设。

### **3.1 缝合 time\_model.h 契约断句与四文件注释归位**

> 1. **修复 include/rtx/time\_model.h 契约块**：将顶出至 // @rtx-type-justification 上方的两段断句续行缝合回字段内部，并把误入 if 返回分支内部的函数说明移回 advanceOneStep() 与 formatSmpteTimecode() 签名紧邻上方（以下为权威修复片段）10：

C++  
// @rtx-type-justification  
// Category: Domain-Model  
// Why-Std-Fails: 帧率 Rational 与 drop-mode 的配对领域模型；std::pair 丢失字段语义，formatSmpteTimecode 的六类白名单时间码契约以具名字段表达。  
// Invariant: drop\_mode 仅对 1000/1001 家族（标称 30/60）有意义，其他帧率一律渲染为 non-drop。  
struct TimecodeFormat {  
    Rational frame\_rate{24, 1};  
    TimecodeDropMode drop\_mode \= TimecodeDropMode::NonDropFrame;  
};

C++  
// @rtx-type-justification  
// Category: NonTrivial-StateMachine  
// Why-Std-Fails: 逐步除法的余数必须跨调用进位（Bresenham 载体），std::chrono 时钟与 std::accumulate 均无法表达「余数进位 \+ 饱和防回绕」的持续状态机。  
// Invariant: 每步推进的漂移永不越过 1 Flicks tick；非法步长为 no-op（不除零）。  
class IncrementalFlicksAccumulator {

> 2. **对齐四份漂移源码注释**：在 src/core/wasapi\_audio\_output.cpp 中，将 // 暂停即冻结... 从 resume() 移回 pause() 的 client\_-\>Stop() 之前，将 // 红线区... 移出 if (FAILED(hr)...) return; 分支至 pumpDevice() 拷贝区，将 // 声明顺序即析构逆序... 移至 worker\_ 声明上方，将 // 停止请求... 移至 std::stop\_callback 上方18；在 src/gui/d3d12\_raii.h 中，将 // collect() 命中 UINT64\_MAX... 移回 deviceLostSeen()，将 // \---- 持久映射上传堆 \---- 及工厂注释移至 createMappedUploadResource() 函数声明上方，将 // 进程级延迟退役池... 移至 deferredPool() 上方28；在 src/core/json\_scanner.cpp 中，将 // High surrogate 与 // 孤立代理项... 归位至代理对分支与 0xFFFD 降级赋值处，将 // Fallback for systems... 移回 memberDouble() 的 std::strtod 回退支路29；在 tests/test\_raii\_retirement.cpp 中，将 // 模拟 WAIT\_TIMEOUT 与 // DXGI\_ERROR\_DEVICE\_REMOVED 注释移回对应的退役超时与设备丢失测试段27。

### **3.2 清除 SendEncoder 乱码与重写失真存续声明**

> 1. **消号 Phase 11.1 A-05 乱码**：在 src/gui/workstation\_canvas.cpp 中将 ChromeText::SendEncoder 的 UTF-8 误解码字面量替换为标准 C++ 通用字符名（或纯净文本），彻底消除乱码7：

C++  
case ChromeText::SendEncoder:  
    return zh ? L"\\U0001F680 导出序列至转码器" : L"\\U0001F680 Export to Encoder";

> 2. **重写错贴类型的 @rtx-type-justification**：将 WasapiAudioOutput::Impl（src/core/wasapi\_audio\_output.cpp）的 Category 修正为 NonTrivial-StateMachine，精确说明其管理 WASAPI 多 COM 对象、事件句柄、缓存行对齐环形游标及 std::jthread worker\_ 末位 join 析构序18；将 FormatMapping（src/shim/ngx\_bridge/d3d12\_interop.cpp）修正为 Domain-Model 或直接收敛为 std::tuple\<int32\_t, DXGI\_FORMAT, CUarray\_format, unsigned int, uint32\_t\> 静态查找表（免除自定义类型）17；在 src/core/hw\_transcode\_internal.h 中修正 DecoderDeleter、EncoderDeleter 与 BridgeDeleter 的 Why-Std-Fails，说明其携带 NvidiaShim\* / NgxRemasterBridge\* 实例指针（FnDeleter 仅适配无捕获自由函数）而非 extern "C" 函数表15；在移除 NleWindowState 前若暂存过渡期，将其标注从 NonTrivial-StateMachine 修正为 Domain-Model（说明绑定于 GWLP\_USERDATA 且随 WM\_NCDESTROY 回收）7；并顺带刷新 DynamicVsrTarget、ProbedMedia、FrameBuffer 与 PendingEncodeItem 的真实契约描述13。

### **3.3 固化同口径 cmake/AuditSloc.cmake 与补录 .map 核验**

在 cmake/ 新增纯 CMake 原生脚本 cmake/AuditSloc.cmake，硬编码锁定扫描范围（include/、src/、tests/、cmake/、根 CMakeLists.txt 及 schema/schema\_model.hpp、schema/reflection\_codegen.cpp、schema/CMakeLists.txt），利用正则剔除纯空行、纯 ///\# 注释行与 @rtx-type-justification 块后输出 Pure SLOC、语义注释行与物理总行三项读数，消除人工统计与跨工具脚本的口径分歧5。同步在宿主机重新运行一次 CineRTX.exe.map Publics by Value 检索，确证 Module 4/5 提纯后的非内联符号（MediaPlaybackSession::create/seek、WasapiAudioOutput::create、GopSeekScheduler、PreviewViewport::presentShared/adoptDevice）完整留存4。

## **四、 裁决要项 2 落地路径：Phase 11.3 解封与 19 号施工单纠偏约束**

首席领域专家组正式批准 **Phase 11.2-R 结案封板**，并批准立即解除冻结、派发 **19 号施工单**启动 Phase 11.3（统一 NLE 引擎与零分配 AVX2 音频总线）1。但协调负责人派单时必须随附勘误说明，修正 19 号单定稿于 Phase 11.2-R 之前导致的四处过时表述：第一，19 号单任务 4 中的“全面替换变调失真的 VarispeedCursor”须修正为“替换 src/core/playback\_session\_audio.cpp 中的自由函数 applyLinearVarispeed()”（VarispeedCursor 已在 Phase 11.2-R 中被彻底消灭）11；第二，19 号单引用的 archive/phase11-attempt-1 历史参考文件需由协调负责人按脱毒清单提纯后直接随物理文件树交付，遵守 30 号单确立的“外部执行工程师不可访问 Git 版本库”边界3；第三，19 号单引用的 ../../... 宿主机路径直链须脱敏为仓内相对路径4；第四，将 Phase 11.2-R 确立的 Std-First、AuditTypes.cmake 与 AuditSloc.cmake 负增长追偿门禁正式并入 19 号单验收项2。  
为防止 Phase 11.3 重蹈“单测孤岛”或“Slop 回潮”覆辙，外部执行工程师必须无条件落实以下五道硬性施工防线：

> 1. **开工首日（Day 1）全量拔旧清债并同步消号 Day-0 尾差**：在 src/gui/workstation\_canvas.h 与 src/gui/workstation\_canvas.cpp 中物理删除 NleClip、NleTimeline、sharedNleTimeline()、kFlicksPerMs、msToFlicks()、formatFlicksTimecode()、timelineXToMs()、timelineMsToX() 与 onNleRazor() 中的 100 ms 硬编码偏移（预计先期收回约 200 SLOC）2，同步改写 rtx\_gui.cpp 中 WM\_TIMER 的毫秒换算为 std::chrono::duration\_cast\<std::chrono::milliseconds\> 并在 WM\_DESTROY 补入 jobQueue 显式停机通知14，同时落地第三节全部 Day-0 显微修补7。验收时对 src/gui/workstation\_canvas.\* 执行正则扫描 NleClip\|NleTimeline\|sharedNleTimeline\|kFlicksPerMs\|msToFlicks\|formatFlicksTimecode\|timelineXToMs\|timelineMsToX\|\_ms 必须返回 0 命中2。  
> 2. **复用 json\_scanner.h 并严守 EditHistory 零句柄铁律**：NleProject::toJson() / fromJson() 必须 100% 复用既有 include/rtx/json\_scanner.h，严禁重演 archive/phase11-attempt-1 中在 nle\_project.cpp 复制上千行私有 JSON 扫描器与 UTF-8 代理对解码器的缺陷11。7 类可逆编辑命令（含 RelinkAssetCommand）仅记录工程元数据 Delta，严禁持有任何 GPU / COM / CUDA 句柄11。  
> 3. **贯彻 Std-First 与 \-400 至 \-600 Pure SLOC 负增长追偿预算**：Phase 11.3 新增的 MediaAsset、Clip、Track、Sequence、NleProject、EditHistory、CompiledRenderGraph、AudioMixerScratchpad 与 WsolaStreamState 等具名类型，凡可用 std::tuple/std::variant/std::span 替代的局部辅助结构一律不准建类；必须存续者须在声明上方 8 行内编写贴合自身不变量的 @rtx-type-justification 声明，严禁复制粘贴模板文案2。借助拔除旧时间轴胶水与复用基础设施，Phase 11.3 结项时全仓 Pure SLOC 必须在 AuditSloc.cmake 锁定的当前基准（38,562 行）上净压减至少 400–600 行，且严禁删减语义注释或压缩空行凑数1。  
> 4. **打通 C-05 硬切 IDR 链路与音频热循环零堆分配**：在 src/core/hw\_transcode\_stages.cpp 的 encodeWorkerBody() 中补全 encode\_frame.keyframe \= item.force\_keyframe ? 1 : 0;，确保 CompiledRenderGraph 驱动多段硬切导出时保持单调连续 90 kHz PTS 不断流，并在切点下发 force\_reset\_temporal \= true 与强制 IDR 关键帧，最终以导出 MP4/MKV 的容器解复用总帧数及切点 IDR 位置逐帧一致性校验作为退出铁证11。音频链路必须消除每帧构造 std::vector\<VoicePrep\> 的堆分配，改用预分配 AudioMixerScratchpad 或栈上分块处理，在 tests/test\_audio\_mixer\_avx2.cpp 中通过 Debug 重载 operator new 断言混音热循环 0 次堆分配，并以预分配环缓冲的 WsolaStreamState 替换 applyLinearVarispeed()，验证 0.25x–2.0x 调速下基频漂移 \< 0.05%11。  
> 5. **三层主干集成与跨工具链全闭环验收**：交付验收必须同时满足 CineRTX.exe.map（在 /OPT:REF /OPT:ICF 下留存 NleProject::toJson/fromJson、CompiledRenderGraph::compile、mixAvx2、wsolaStretch 符号）、四件套构建门禁（AuditTypes / AuditSpdx / AuditImports / AuditSloc 全绿）、WTS Session \> 0 交互式桌面 1V+1A 剪辑/撤销/重链接/多段导出全流程实测，并在 CI 中补跑 linux-gcc（与 Zig/Clang 工具链检查），彻底收口 Phase 11.2-R 仅覆盖 MSVC 的跨平台验证尾巴2。

**终审签发结论**：批准 **CineRTX Phase 11.2-R 结案封板**，即刻**解封 Phase 11.3（19 号施工单）**；协调负责人须将本文第三节 Day-0 显微修补清单、《Phase 11.3 派发勘误附表》与 cmake/AuditSloc.cmake 基准读数纳入 Phase 11.3 首日开工交底，在绝对纯净、零 Slop、同口径受控的基线上推进统一 NLE 引擎与零分配音频总线合龙1。

#### **引用的著作**

> 1. 33\_CineRTX Phase 11.2-R 项目卫生达标与整改收口专家终审验收委托书.md [受控工程档案]  
> 2. 34\_CineRTX Phase 11.2-R 项目卫生达标与整改收口专家终审验收裁决书.md [受控工程档案]  
> 3. 30\_CineRTX Phase 11.2-R 施工单\_注释无损回植与专家直通架构提纯.md [受控工程档案]  
> 4. 32\_CineRTX Phase 11.2-R 最终终审验收与封板决议记录.md [受控工程档案]  
> 5. 29\_CineRTX 11.2-R 专家裁决方案.md [受控工程档案]  
> 6. AuditTypes.cmake [受控工程档案]  
> 7. workstation\_canvas.cpp [受控工程档案]  
> 8. test\_playback\_session.cpp [受控工程档案]  
> 9. preview\_viewport.cpp [受控工程档案]  
> 10. time\_model.h [受控工程档案]  
> 11. 19\_CineRTX Phase 11.3 SuperPower 施工单\_统一NLE引擎与零分配AVX2音频总线.md [受控工程档案]  
> 12. reimplant\_phase112r\_comments.py [受控工程档案]  
> 13. types.h [受控工程档案]  
> 14. rtx\_gui.cpp [受控工程档案]  
> 15. hw\_transcode\_internal.h [受控工程档案]  
> 16. workstation\_canvas.h [受控工程档案]  
> 17. d3d12\_interop.cpp [受控工程档案]  
> 18. wasapi\_audio\_output.cpp [受控工程档案]  
> 19. json\_scanner.h [受控工程档案]  
> 20. ngx\_remaster.cpp [受控工程档案]  
> 21. playback\_session\_video.cpp [受控工程档案]  
> 22. CMakeLists.txt [受控工程档案]  
> 23. playback\_session\_impl.h [受控工程档案]  
> 24. raii\_owners.h [受控工程档案]  
> 25. playback\_session.h [受控工程档案]  
> 26. logger.cpp [受控工程档案]  
> 27. test\_raii\_retirement.cpp [受控工程档案]  
> 28. d3d12\_raii.h [受控工程档案]  
> 29. json\_scanner.cpp [受控工程档案]  
> 30. playback\_session.cpp [受控工程档案]  
> 31. playback\_session\_audio.cpp [受控工程档案]  
> 32. CineRTX Phase 11 演进计划 [受控工程档案]  
> 33. CineRTX 11.2-R 专家裁决方案 [受控工程档案]  
> 34. gate3.md [受控工程档案]  
> 35. 25\_CineRTX Phase 11.2-R SuperPower 施工单\_标准库先验替换与全栈脱毒瘦身.md [受控工程档案]  
> 36. 27\_CineRTX Phase 11.2-R Gate R-1 缺口处置裁决请示暨Gate R-8 补测排期申报.md [受控工程档案]  
> 37. 23\_CineRTX Phase 11.2 代码膨胀与Slop重度污染治理专家咨询委托书.md [受控工程档案]  
> 38. 17\_CineRTX Phase 11 外包施工总纲暨协调负责人备忘录.md [受控工程档案]  
> 39. 26\_CineRTX Phase 11.2-R 施工验收记录.md [受控工程档案]  
> 40. CineRTX Phase 11 复盘与演进 [受控工程档案]  
> 41. CineRTX全仓隐私与敏感资产深度协查报告 [受控工程档案]