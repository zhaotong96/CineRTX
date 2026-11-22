# **CineRTX Phase 11.2-R 治理受挫穿透复核、专家介入裁决暨直通收口方案**

针对项目总协调负责人呈报的 28 号咨询委托书（CINERTX-P11-CONSULT-028），首席领域专家组已完成对 Phase 11.2-R 当前未提交工作区源码、CMake 门禁脚本、注释归档文件及 25–27 号施工验收账本的穿透复核1。专家组正式裁定：**驳回 26、27 号文档关于 Gate R-1、Gate R-6 与 Gate R-8 的完工签收申请，批准保留已兑现的标准库先验替换底座成果（FnDeleter、std::jthread、有理数 Flicks、单线程音频 seek\_epoch 无锁拓扑与 .mkv 实体夹具），并立即启动由专家组直通代建蓝图支撑的纠偏收口**1。针对四项专项请示，终审裁决核心结论为：将全仓删注释归档行为定性为**指标异化驱动的工程契约破坏**，责令无条件回滚注释与逻辑空行并物理删除 phase112r\_governance\_archive.md1；否定仅靠删注释或机械拆文件凑数，裁定对 Gate R-1 采取“真实逻辑消肿（Path A 压减约 600–760 行有效代码）+ 推行纯净有效代码行（SLOC）双轨度量 \+ 核定赤字由 Phase 11.3/11.4 追偿扣减（Path B）”协同路径1；提供基于 Win32 动态缓冲 GetModuleFileNameW 与多级祖先目录无路径泄漏探测的 readFixture 权威标准实现及 CMake 跨平台加固10；下发可直接落盘的补丁规范，同步根治本次穿透审计新检出的隐藏代码与门禁缺陷2。  
尤为关键的是，源码穿透审计表明当前工作区的风险已超出 28 号委托书所列三项表面硬伤1。首先，cmake/AuditTypes.cmake 存在控制流逻辑反转漏洞，导致任何完全省略 @rtx-type-justification 块的类型都会被脚本直接跳过放行2，使 WorkstationApp、Control 与 Group 成为漏网之鱼15，而自动化删注释脚本亦将多处多行 Why-Std-Fails 与 Invariant 契约说明从中间拦腰斩断并倾倒进归档文件7。其次，workstation\_canvas.cpp 的 stopPlaybackSession() 与 WorkstationApp 成员声明序存在**先销毁 playbackSink（含无头桥与 slot\_flags\_ 原子数组）、后等待 playbackSession 工作线程或视口异步退役**的 Use-After-Free（UAF）隐患14。在清零上述隐藏断点并完成 WTS Session 1 完整 100 次 Resize 实呈闭环前，严禁解封 Phase 11.3（19 号施工单）1。

## **一、 现场物证与隐藏缺陷穿透复核**

对工作区现实源码与 26、27、28 号文档账本的逐行交叉比对表明，外部执行工程师在标准库底层原语替换上兑现了真实的架构收益，但在体量治理、类型门禁与异步生命周期细节上出现了“重表面过关、轻结构提纯”的偏差1。下表汇总了八大门禁的物理真相与本次穿透审计新检出的隐藏缺陷1。

| 审查维度 | 26/27 号自述状态 | 穿透核验实情与新增隐藏缺陷（源码锚点） | 专家组终审定性 |
| :---- | :---- | :---- | :---- |
| Gate R-1 体量红线 | 声称 A-2 治理后 38,207 行 PASS4 | 回滚 A-2 删注释后实为 42,334 行（较 37,626 基线净增 \+4,708，相对 39,626 红线高出 2,708 行；A-1 格式清理前按 43,467 − 1,050 推算为 42,417 行，对应 27/28 号文申报的 2,791 缺口）1。归档文件 phase112r\_governance\_archive.md（145 KB / 4,151 行）实录 110 个文件标题删去 3,978 行注释，叠加纯空行剥离合计影响 126 个文件 −4,118 行1。 | REJECT |
| Gate R-2 播放内核 | 声称 266 ≤ 500 行 PASS5 | 机械拆为 playback\_session.cpp（266/225）、\_video.cpp（454/408）、\_audio.cpp（266/244）、\_impl.h（154/133），未删注释前四文件合计 1,140 行（删注释后 1,010 行），拆分开销引入重复头文件与原型声明1。但 0.7056、audio\_state\_mutex 与 8 个冗余包装类确已真实拔除1。 | CONDITIONAL |
| Gate R-3 GUI 边界 | 声称 994 \< 1,000 行 PASS5 | rtx\_gui.cpp 将约 170–242 行 WorkstationApp 与播放/监看胶水迁入 workstation\_canvas.h/.cpp 才压至 994 行4。stopPlaybackSession() 先销毁 playbackSink 再 playbackSession.reset()，存在 P0 级 UAF 隐患14。rtx\_gui.cpp 亦残存重复 CBN\_SELCHANGE 空分支、onBrowse 重复对话框构造及与 workstation\_canvas.h 重复的常量定义15。 | CONDITIONAL |
| Gate R-4 夹具纯洁度 | 物理移除 test\_streams.h PASS5 | test\_streams.h（−1,306 行）确认移除，3 份实体 .mkv（8,916 / 9,589 / 10,999 B）规范就位，源码树零十六进制残留1。 | PASS |
| Gate R-5 类型审计 | AuditTypes.cmake 0 违规 PASS1 | **检出严重脚本漏洞**：AuditTypes.cmake 含 if(NOT \_justified) continue() 反转逻辑，导致未标注的 WorkstationApp、Control 与 Group 直接漏审；部分多行 @rtx-type-justification 注释被删注释脚本拦腰截断；PreviewViewport、NgxRemasterBridge、InspectorRow 批量套用了错误的 Domain-Model 模板2。 | FAIL |
| Gate R-6 单测与包络 | 128/128 PASS，PE 4.29 MB1 | readFixture 硬编码相对路径 "fixtures"，在项目根目录调用 rtx\_tests.exe 时 2 项单测崩溃1；CMakeLists.txt 将 .mkv 拷贝置于 if(WIN32) 内，影响 Linux 测试对等性11。AuditSpdx、AuditImports（4.29 MB）与 .map 8 个核心符号真实达标1。 | REJECT |
| Gate R-7 跨工具链 | MSVC PASS / Zig 限定 PASS5 | MSVC /MT 0 错误 0 警告，如实收敛 libc++ 18 缺失 std::out\_ptr 为两段式回填（callComOut），并如实申报 mingw-w64 dcomp.h 缺失 REFIID 的存量约束5。 | PASS |
| Gate R-8 桌面实呈 | 27 号文申报待用户登录4 | 27 号文于测试机确认驱动已升至 621.24，28 号文确认本机宿主 Session 1 与 621.24 WHQL 已就绪；但脚本 gate\_r8\_desktop\_check.ps1 仅配置 80 次过渡 resize，低于 25 号单与 28 号文规定的 100 次硬门槛1。 | REJECT |

穿透代码核验揭示的四处深层隐患中，前两项直接关系到工程门禁的真实有效性，后两项则直接威胁运行时内存安全。第一，cmake/AuditTypes.cmake 的主扫描循环在检测到 @rtx-type-justification 后置位 \_justified \= TRUE，但紧随其后的分支却写了 if(NOT \_justified) continue() endif()，之后才去匹配 class 或 struct 定义行2。这一顺序反转意味着任何完全不写存续声明的类型都会被直接跳过，只有主动写了 @rtx-type-justification 标记的类型才会被校验类别是否在六类白名单内2。受此漏洞掩护，src/gui/workstation\_canvas.h 中长达近 30 行的 struct WorkstationApp15、src/core/playback\_session\_impl.h 中的嵌套 struct Control17 以及 src/gui/rtx\_gui.cpp 中的局部 const struct Group16，全部未作任何存续声明却顺利通过了审计2。同时，脚本中 (\[^;\]\*)\$ 正则放过了单行内联 struct Foo { int a; };，且从不清理跨过 8 行窗口的孤立声明块，致使 rtx\_gui.cpp 中原属 WorkstationApp 的注释块至今悬停在 constexpr int kTabIds\[\] 上方2。  
第二，26 号与 27 号文档均宣称“许可证头与 @rtx-type-justification 块 100% 保留在原文件”4，但实核源码发现清洗脚本仅保护了以 @rtx-type-justification、Category:、Why-Std-Fails:、Invariant: 打头的 4 行，将多行折行续写的设计契约下半句全部剥离并倾倒进了归档文件7。例如 include/rtx/raii\_owners.h 中 CudaDeviceBuffer 的 Why-Std-Fails 在原文件中被截断为“不可与”三字，下半句“nullptr 比较）……”却落入 phase112r\_governance\_archive.md7；IVideoFrameSink、IAudioMasterClock、MediaPlaybackSession::Impl 与 HeadlessPlaybackSink 均存在同款半截断句6。此外，为完成 142–144 个类型的标注指标，工程现场将属于配置模型 TranscodeProfile 的模板文案“多字段领域模型：具名字段承载序列化/校验/反射契约……”批量套用到了 PreviewViewport（应为 NonTrivial-StateMachine）、NgxRemasterBridge（动态桥接装载器）、InspectorRow、ProbedMedia 与 NleClip/NleTimeline 上1。  
第三，workstation\_canvas.cpp 的 stopPlaybackSession() 与 WorkstationApp 存在双重生命周期 UAF 漏洞14。在 stopPlaybackSession() 中，代码先执行 g\_app.playbackSink.reset() 销毁 HeadlessPlaybackSink（连带销毁无头 RtxNgxBridge\* 与原子数组 slot\_flags\_\[3\]），之后才调用 g\_app.playbackSession.reset() 停止并 join() 仍持有裸指针 IVideoFrameSink\* sink 的 video\_worker 线程14。另一方面，WorkstationApp 的成员声明顺序将 playbackSession 置于 playbackSink 之前、将 previewViewport 置于最后15，且 PreviewViewport::presentShared() 向 GpuRetirementQueue 提交的清理回调捕获了指向 HeadlessPlaybackSink::slot\_flags\_ 的裸指针 ticket.first18。无论是运行时调用 stopPlaybackSession()，还是 WM\_DESTROY / \~WorkstationApp() 在 WAIT\_TIMEOUT 路径下将队列移入进程级 deferRetirement() 延迟池16，一旦 playbackSink 在 GPU Compute Shader 真正退役前被销毁，后续 collect() 或 releaseAll() 执行 flags\[slot\_index\].store(0) 时都将触发野指针写入14。  
第四，rtx\_gui.cpp 与 CMakeLists.txt 存在若干冗余与跨平台漏配11。src/gui/rtx\_gui.cpp 的 WM\_COMMAND 中出现连续重复的 } else if (code \== CBN\_SELCHANGE) { } else if (code \== CBN\_SELCHANGE) { 语法残渣，定义了 runFileDialog() 后却在 onBrowse() 内再次手写重复的 OPENFILENAMEW 结构，且与 workstation\_canvas.h 重复定义了 kBaselineSrcWidth 和 kBaselineSrcHeight15。同时，CMakeLists.txt 将 .mkv 夹具的 POST\_BUILD 同步指令放在 if(WIN32) 分支内部，导致 Linux 环境编译出的 rtx\_tests 可执行目录缺失测试夹具11。

## **二、 请示项 1 终审裁决：全仓注释剥离定性与无损回滚规程**

对于外部执行工程师剥离全仓 126 个文件共计 4,118 行注释与空行并倾倒至 docs/archive/phase112r\_governance\_archive.md 的行为1，首席领域专家组予以如下定性：**该行为属于典型的古德哈特定律（Goodhart's Law）指标投机，同时也是一起由“单维物理总行数门禁”与“无语义保护的自动化清洗脚本”共同诱发的工程事故**1。必须客观指出，外部执行工程师在 27 号请示中曾明确声明“选项 A-2 激进密度治理将删去半数设计契约注释、降低后续可审性，外部执行工程师不推荐”，是在后续复核轮压力下才执行了全量清洗4。即便如此，自动化脚本不仅波及 Phase 1–10 毫无关联的成熟核心文件（抹除了 logger.cpp 的 ABI 异常安全契约、main.cpp 的独显唤醒与 FRUC 故障可见性契约、geometry.cpp 的画幅模式定义、sei\_synthesizer.cpp 的码流语义注释及 wasapi\_audio\_output.cpp 的实时回调红线），还斩断了本阶段刚立下的 @rtx-type-justification 声明块1。用破坏代码自解释性来迎合物理行数统计，从根本上背离了 Phase 11.2-R“消灭生成式 Slop、恢复代码卫生”的初心1。  
专家组正式裁决：**立即无条件撤销 126 个文件的注释与逻辑空行删除，全面恢复核心设计契约注释与 @rtx-type-justification 完整上下文，并在编译与测试验证通过后物理删除 docs/archive/phase112r\_governance\_archive.md（145 KB）**1。  
执行回滚时必须严守局部受控操作原则：**严禁对工作区运行 git checkout \-- . 或整文件覆盖回 Phase 11.2 快照**，因为当前未提交工作区承载了本轮真实有效的标准库重构成果（如 raii\_owners.h 别名收敛、Flicks 有理数化、seek\_epoch 无锁音频拓扑、raw\_errors 空指针修复及 AuditTypes.cmake 骨架）1。由于 phase112r\_governance\_archive.md 精确保存了 110 个文件标题下的 3,978 行被删注释（含文件相对路径、原行号与逐字文本）8，且 26/27 号文显示 Option A-1 良性格式清理（删除 67 行纯装饰性全行分隔线、合并 16 处连续空行与尾随空白清除）合计仅占 −83 行4，回滚规程按三步实施：

> 1. **按锚点定向回植语义注释与段落空行**：以 phase112r\_governance\_archive.md 为回植账本，逐文件将全部 3,978 行注释及恢复可读性所需的合理段落空行按紧邻函数/语句上下文精确回植到对应位置，优先拼全被拦腰截断的 @rtx-type-justification 多行说明7。  
> 2. **保留 A-1 良性格式规范化（−83 行）**：装饰性横线分隔符（−67 行）、连续多余空行合并（−16 行）与尾随空白清除属于正面代码卫生整顿，保留在工作区，不予回退4。  
> 3. **验证与清除投机归档文件**：回植完成后立即运行 MSVC /MT 零警告全量编译与 rtx\_tests.exe 回归，确认未引入语法误植或逻辑退化后，物理删除 docs/archive/phase112r\_governance\_archive.md1。

## **三、 请示项 2 终审裁决：Gate R-1 体量缺口溯源与 A+B 协同消肿方案**

注释回滚后，如何处理当前全仓 42,334 行（A-1 格式清理前按 −1,050 计为 42,417 行）与 39,626 红线之间的缺口，不能靠继续削足适履或简单无限期豁免，而必须基于真实数字账本做结构性拆解1。

### **1\. 精确行数账本还原与缺口根因拆解**

交叉核对 23、24、25、26、27 与 28 号文档的统计口径（包含 src/、include/、tests/ 全部 C++ 源码与头文件、cmake/\*.cmake、根 CMakeLists.txt、schema/ 核心三文件、webui/server.py 与 build.bat），全周期体量变化与差异溯源如下表所示1。

| 度量节点与阶段 | 总物理行数 | 相对 11.1 基线（37,626）净增量 | 相对红线（39,626）偏差 | 账本构成与差异根因核验 |
| :---- | :---- | :---- | :---- | :---- |
| Phase 11.1 封板基线 | 37,626 | 0 | −2,000 | 23、25 号文确立的基准水位3。 |
| Phase 11.2 施工后快照 | 43,450 / 43,467 | \+5,824 / \+5,841 | \+3,824 / \+3,841 | 23、25 号文记 43,450 行；27 号文基于 CineRTX.7z 机读复算为 43,467 行（相差 17 行）3。 |
| 24 号专家方案目标预算 | 39,513 | \+1,887 | −113 | 预期净减 −3,937 行，目标瘦身率 67.6%9。 |
| 实作净减后（A-1 格式清理前） | 42,417 | \+4,791 | \+2,791 | 由 43,467 − 1,050（本轮功能净减）算得，解释了 27 号文选项 B 与 28 号文申报的 2,791 行缺口1。 |
| 实作净减后（A-1 格式清理后） | 42,334 | \+4,708 | \+2,708 | 叠加 A-1 格式清理 −83 行（分隔符 −67、空行 −16），本轮实际净减 −1,133 行；26 号文偏差节曾录中间快照 42,313 行1。 |
| A-2 强删注释后（伪达标终态） | 38,207 | \+581 | −1,419 | 在约 42,325–42,334 行基础上强行剥离 −4,118 行注释与空行，予以驳回1。 |

结合 27 号文给出的目录普查（在 src/、include/、tests/、cmake/、schema/ 合计 41,333 行中，注释占 3,549 行即 8.6%，空行占 3,925 行即 9.5%，纯净有效代码 SLOC 为 33,859 行即 81.9%）4，可将 2,708–2,791 行缺口明确拆解为两类性质截然不同的构成：  
第一类是**预算假设失准导致的不可抗结构偏差（约 811 行）**。24 号方案预估全仓仅约 36 个存续类型（+180 行标注），而实际仓内存在 142–144 个具名类型，叠加 AuditTypes.cmake 脚本（+115 行），合规标注与门禁实测消耗约 675 行，超出预算 495 行1；此外，24 号文误按过期尺寸估算 preview\_viewport.cpp 从 1,029 行压至 730 行（预期贡献 −299 行减量），但开工时该文件实际仅 652 行，致使这 299 行减量空间从一开始就不存在5；再加 27 号文机读快照比 23 号文多出 17 行基线差异3，三项合计约 811 行。  
第二类是**尚未挤干的架构水分与“拆文件搬家”损耗（实存约 600–760 行可直接压减的 SLOC）**。工程师将 playback\_session.cpp 切为 4 个文件（未删注释累计 1,140 行），产生了 154 行 \_impl.h 与四份重复头部及大量重复控制面样板5；rtx\_gui.cpp 将状态胶水搬迁至 workstation\_canvas.h/.cpp（后者在 27 号文账本净增 \+242 行），且内部仍保留大量手写线性预设分支、重复双语控件刷新与非表驱动控件创建样板4。

### **2\. 终审裁决：采纳路径 A 真实消肿与路径 B 度量升维追偿的协同方案**

鉴于当前全仓注释与空行总占比仅 18.1%，在不损害代码可维护性与不逼迫代码揉成单行天书的前提下，于 Phase 11.2-R 强行再硬砍 2,708 行物理行是不现实的，且会重蹈 24 号文过度乐观估算的覆辙4。反之，若采纳纯路径 B 原封不动放行，则会放任约 600–760 行真实可压缩的重复样板流入 Phase 11.314。为此，专家组正式拍板执行**A+B 协同裁决**：

> 1. **执行路径 A 真实消肿（目标净压减约 600–760 SLOC）**：按第五节蓝图对 playback\_session 四文件（预期实减约 250–320 SLOC）、rtx\_gui.cpp 与 workstation\_canvas（预期实减约 280–340 SLOC）以及冗杂小类型与空分支（预期实减约 60–100 SLOC）实施外科压缩，将全仓物理总行数从 42,334 行真实回落至约 **41,570–41,730 行**。  
> 2. **改革 Gate R-1 度量口径（引入纯净有效代码行 SLOC 硬指标）**：自本裁决起，将全仓体量门禁从单一“含注释物理总行数”升级为“纯净有效代码行 SLOC（剔除纯 // 注释、空行以及 @rtx-type-justification 合法合规块）为主红线，物理总行数为参考”。自解释性设计契约注释不再受惩罚，从机制上永久斩断删注释造假的激励。  
> 3. **核销不可抗偏差并锁死 Phase 11.3/11.4 路径 B 追偿红线**：对前述约 811 行预估基线偏差予以核销后，剩余约 **1,140–1,300 行超标赤字**正式登记为在案技术偏差，**强制要求在 Phase 11.3（19 号施工单）与 Phase 11.4（20 号施工单）的工程预算中等额追偿**：其中仅 Phase 11.3 按 19 号单要求彻底拔除 workstation\_canvas.h/.cpp 内残存的 NleTimeline、NleClip、sharedNleTimeline()、timelineXToMs、timelineMsToX 及粗粒度 \*\_ms 胶水逻辑14，即可先期收回 200 余行存量债务，配合全仓 SLOC 负增长硬约束，在 v0.3.0 封板前完成赤字清零1。

## **四、 请示项 3 权威标准实现：readFixture 工业级零路径依赖方案与 CMake 加固**

tests/test\_playback\_session.cpp 第 44 行将样本路径硬编码为当前工作目录相对路径 std::filesystem::path("fixtures") / name，且缺少 tellg() 负值校验与读取字节数校验，导致在项目根目录调用 rtx\_tests.exe 时触发 2 项测试断言崩溃1。  
遵循 CineRTX 在隐私协查与跨工具链审查中确立的双重边界——严禁将包含宿主机用户目录树的 CMAKE\_SOURCE\_DIR 或 \_\_FILE\_\_ 绝对路径编译进可执行文件29，同时必须同时兼容 Windows（MSVC / Zig）与 Linux（linux-gcc CI）并绕过固定 260 字符 MAX\_PATH 限制9——专家组提供 readFixture 的权威工业级标准实现。该函数在 Windows 下利用动态扩容循环调用 GetModuleFileNameW（以 len \< buf.size() 严密防范 ERROR\_INSUFFICIENT\_BUFFER 截断12），在 Linux 下读取 /proc/self/exe19，按“可执行文件同级 fixtures/ → 当前工作目录 fixtures/ 与 tests/fixtures/ → 从可执行目录向上追溯最多 6 层祖先目录的 tests/fixtures/”三级次序寻址。建议将该实现下沉至 tests/test\_framework.h 或置于 tests/test\_playback\_session.cpp 匿名命名空间（需补充 \#if defined(\_WIN32) 引入 \<windows.h\> 及非 Windows 下的 \<unistd.h\>，并确保包含 \<system\_error\>）10：

C++  
namespace {

\[\[nodiscard\]\] std::filesystem::path currentExecutableDir() {  
\#**if** defined(\_WIN32)  
  std::wstring buf(512, L'\\0');  
  for (;;) {  
    const DWORD len \= ::GetModuleFileNameW(  
        nullptr, buf.data(), static\_cast\<DWORD\>(buf.size()));  
    if (len \== 0) {  
      return {};  
    }  
    if (static\_cast\<size\_t\>(len) \< buf.size()) {  
      buf.resize(len);  
      return std::filesystem::path(buf).parent\_path();  
    }  
    if (buf.size() \>= 32768) {  
      return {};  
    }  
    buf.resize(buf.size() \* 2);  
  }  
\#**elif** defined(\_\_linux\_\_)  
  std::string buf(4096, '\\0');  
  const ssize\_t len \= ::readlink("/proc/self/exe", buf.data(), buf.size() \- 1);  
  if (len \<= 0) {  
    return {};  
  }  
  buf.resize(static\_cast\<size\_t\>(len));  
  return std::filesystem::path(buf).parent\_path();  
\#**else**  
  return {};  
\#**endif**  
}

\[\[nodiscard\]\] std::optional\<std::filesystem::path\> resolveFixturePath(const char\* name) {  
  if (name \== nullptr \|\| \*name \== '\\0') {  
    return std::nullopt;  
  }  
  std::error\_code ec;  
  const std::filesystem::path rel\_name(name);

  auto isRegular \= \[\&ec\](const std::filesystem::path& p) {  
    return std::filesystem::exists(p, ec) && std::filesystem::is\_regular\_file(p, ec);  
  };

  // 1\. 首选：可执行文件同级 fixtures/ 目录（CMake POST\_BUILD 标准部署位）  
  const std::filesystem::path exe\_dir \= currentExecutableDir();  
  if (\!exe\_dir.empty()) {  
    const std::filesystem::path alongside \= exe\_dir / "fixtures" / rel\_name;  
    if (isRegular(alongside)) {  
      return alongside;  
    }  
  }

  // 2\. 次选：当前工作目录（兼容从仓库根目录或构建目录直接调用）  
  const std::filesystem::path cwd \= std::filesystem::current\_path(ec);  
  if (\!cwd.empty()) {  
    for (const std::filesystem::path& cand :  
         {cwd / "fixtures" / rel\_name, cwd / "tests" / "fixtures" / rel\_name}) {  
      if (isRegular(cand)) {  
        return cand;  
      }  
    }  
  }

  // 3\. 回退：沿可执行目录向上逐层探测源码树 tests/fixtures/（零宿主路径硬编码）  
  if (\!exe\_dir.empty()) {  
    std::filesystem::path probe \= exe\_dir;  
    for (int depth \= 0; depth \< 6; \++depth) {  
      const std::filesystem::path cand \= probe / "tests" / "fixtures" / rel\_name;  
      if (isRegular(cand)) {  
        return cand;  
      }  
      const std::filesystem::path parent \= probe.parent\_path();  
      if (parent.empty() \|\| parent \== probe) {  
        break;  
      }  
      probe \= parent;  
    }  
  }  
  return std::nullopt;  
}

\[\[nodiscard\]\] std::optional\<std::vector\<uint8\_t\>\> readFixture(const char\* name) {  
  const std::optional\<std::filesystem::path\> resolved \= resolveFixturePath(name);  
  if (\!resolved.has\_value()) {  
    return std::nullopt;  
  }  
  std::ifstream file(\*resolved, std::ios::binary \| std::ios::ate);  
  if (\!file) {  
    return std::nullopt;  
  }  
  const std::streamoff raw\_size \= file.tellg();  
  if (raw\_size \<= 0) {  
    return std::nullopt;  
  }  
  const auto byte\_count \= static\_cast\<std::streamsize\>(raw\_size);  
  file.seekg(0, std::ios::beg);  
  std::vector\<uint8\_t\> buffer(static\_cast\<size\_t\>(raw\_size));  
  if (\!file.read(reinterpret\_cast\<char\*\>(buffer.data()), byte\_count) \|\|  
      file.gcount() \!= byte\_count) {  
    return std::nullopt;  
  }  
  return buffer;  
}

} // namespace

同时，必须在根 CMakeLists.txt 的 rtx\_tests 目标配置段完成两项加固11。首先，将 .mkv 夹具的 POST\_BUILD 复制指令移出 if(WIN32) 块，使 Linux 构建同样能将夹具同步至 \$\<TARGET\_FILE\_DIR:rtx\_tests\>/fixtures/11；其次，在 set\_tests\_properties(rtx\_unit\_tests ...) 中显式绑定工作目录属性，确保通过 ctest 运行时环境一致11：

CMake  
add\_custom\_command(TARGET rtx\_tests POST\_BUILD  
  COMMAND \${CMAKE\_COMMAND} \-E make\_directory  
    "\$\<TARGET\_FILE\_DIR:rtx\_tests\>/fixtures/"  
  COMMAND \${CMAKE\_COMMAND} \-E copy\_if\_different  
    "\${CMAKE\_CURRENT\_SOURCE\_DIR}/tests/fixtures/h264\_25fps\_gop12.mkv"  
    "\${CMAKE\_CURRENT\_SOURCE\_DIR}/tests/fixtures/hevc\_25fps\_gop12.mkv"  
    "\${CMAKE\_CURRENT\_SOURCE\_DIR}/tests/fixtures/av1\_25fps\_gop12.mkv"  
    "\$\<TARGET\_FILE\_DIR:rtx\_tests\>/fixtures/"  
  COMMENT "Deploying physical .mkv playback fixtures beside rtx\_tests"  
  VERBATIM)

set\_tests\_properties(rtx\_unit\_tests PROPERTIES  
  WORKING\_DIRECTORY "\$\<TARGET\_FILE\_DIR:rtx\_tests\>"  
  TIMEOUT 300)

## **五、 请示项 4 专家介入直通代建包与 Phase 11.2-R 战略收口路线**

针对外部工程师与复核机制来回拉锯造成的工期损耗，首席领域专家组正式批准**有限度直通介入（Direct Construction Blueprint & Verification Takeover）**，并严守 17 号总纲的正交分工底线1。以下提供三大核心断点的直接可落盘改造蓝图与 Gate R-8 闭环验收步骤，供协调负责人立即督导或直通合入收口。

### **1\. 直通加固一：修复 AuditTypes.cmake 逻辑漏洞并肃清伪标注**

必须立刻修正 cmake/AuditTypes.cmake 的控制流反转错误，确保无 @rtx-type-justification 块的类型立即报错阻断，并同步捕获带 final : public Base { 的多态继承、带 const/static 修饰的局部结构以及单行花括号结构体 { ... };（同时继续放行前向声明 class Foo; 与枚举 enum class），新增对 Why-Std-Fails: / Invariant: 实质非空的校验及孤立声明块报错2。核心循环改造如下：

CMake  
    if(\_line MATCHES "@rtx-type-justification")  
      if(\_justified)  
        math(EXPR \_prev\_line "\${\_marker\_at} \+ 1")  
        list(APPEND RTX\_TYPE\_VIOLATIONS  
          "\${\_file}:\${\_prev\_line}: orphaned @rtx-type-justification block with no following type")  
      endif()  
      set(\_justified TRUE)  
      set(\_category "")  
      set(\_has\_why FALSE)  
      set(\_has\_inv FALSE)  
      set(\_marker\_at \${\_i})  
      continue()  
    endif()  
    if(\_justified)  
      if(\_line MATCHES "^\[ \\t\]\*//\[ \\t\]\*Category:\[ \\t\]\*(\[A-Za-z-\]+)")  
        set(\_category "\${CMAKE\_MATCH\_1}")  
      elseif(\_line MATCHES "^\[ \\t\]\*//\[ \\t\]\*Why-Std-Fails:\[ \\t\]\*\[^ \\t\].+")  
        set(\_has\_why TRUE)  
      elseif(\_line MATCHES "^\[ \\t\]\*//\[ \\t\]\*Invariant:\[ \\t\]\*\[^ \\t\].+")  
        set(\_has\_inv TRUE)  
      endif()  
    endif()

    set(\_is\_def FALSE)  
    set(\_name "")  
    if(\_line MATCHES "^\[ \\t\]\*((const\|static)\[ \\t\]+)?(class\|struct)\[ \\t\]+(\[A-Za-z\_\]\[A-Za-z0-9\_:\]\*)\[ \\t\]\*((final\[ \\t\]\*)?(:\[ \\t\]\*\[^;{\]+)?\\\\{.\*\|(final\[ \\t\]\*)?(:\[ \\t\]\*\[^;{\]+)?)\$")  
      set(\_is\_def TRUE)  
      set(\_name "\${CMAKE\_MATCH\_4}")  
    endif()

    if(\_is\_def)  
      if(\_name STREQUAL "FnDeleter")  
        set(\_justified FALSE)  
        continue()  
      endif()  
      if(\_justified AND \_category IN\_LIST RTX\_TYPE\_CATEGORIES AND \_has\_why AND \_has\_inv)  
        math(EXPR RTX\_TYPE\_COUNT "\${RTX\_TYPE\_COUNT} \+ 1")  
      else()  
        math(EXPR \_def\_line "\${\_i} \+ 1")  
        list(APPEND RTX\_TYPE\_VIOLATIONS  
          "\${\_file}:\${\_def\_line}: type '\${\_name}' lacks valid @rtx-type-justification (cat='\${\_category}', why=\${\_has\_why}, inv=\${\_has\_inv})")  
      endif()  
      set(\_justified FALSE)  
    elseif(\_justified)  
      math(EXPR \_age "\${\_i} \- \${\_marker\_at}")  
      if(\_age GREATER 8)  
        math(EXPR \_marker\_line "\${\_marker\_at} \+ 1")  
        list(APPEND RTX\_TYPE\_VIOLATIONS  
          "\${\_file}:\${\_marker\_line}: orphaned @rtx-type-justification block expired without matching class/struct")  
        set(\_justified FALSE)  
      endif()  
    endif()

配套的源码清理必须同步落地：删除 src/gui/rtx\_gui.cpp 中 kTabIds\[\] 上方的孤立声明块，将 InspectorRow 收敛为 using InspectorRow \= std::tuple\<int, int, int, int\>;，将 applyWorkspaceLayout 内的 struct Group 替换为 std::tuple\<const int\*, size\_t, bool\>9；为漏审的 WorkstationApp（workstation\_canvas.h）与 Control（playback\_session\_impl.h 或合并后的 Impl 内部控制状态）补齐准确声明，并将误标 Domain-Model 的 PreviewViewport（应为 NonTrivial-StateMachine）与 NgxRemasterBridge（应为 NonTrivial-StateMachine 或 C-ABI 桥接宿主）恢复真实的 Why-Std-Fails 与 Invariant15。

### **2\. 直通加固二：根治 WorkstationApp 析构 UAF 与界面表驱动压缩**

为同时彻底封堵线程竞态与 WAIT\_TIMEOUT GPU 延迟退役路径下的 slot\_flags\_ UAF 漏洞，必须在源码中执行双重加固14。第一，在 workstation\_canvas.h 中将 WorkstationApp 的 playbackSink 成员移至 playbackSession 之前、将 previewViewport 保持在末尾（保证自动逆序析构严格遵循“视口排空 → 会话 join 工作线程 → 释放无头 Sink”）15，并在 stopPlaybackSession() 中严格先调 g\_app.playbackSession.reset() 再排空视口 GPU 队列、最后释放 g\_app.playbackSink14：

C++  
// workstation\_canvas.h 中的 WorkstationApp 声明拓扑（析构严格逆序）：  
std::unique\_ptr\<rtx::IVideoFrameSink\> playbackSink;  
std::unique\_ptr\<rtx::MediaPlaybackSession\> playbackSession;  
// ...  
std::optional\<PreviewViewport\> previewViewport;

C++  
void stopPlaybackSession() {  
  g\_app.playbackBorrow \= nullptr;  
  g\_app.playbackSession.reset(); // 1\. 首要步骤：join video\_worker/audio\_worker，切断一切 sink-\>present()  
  if (g\_app.previewViewport.has\_value()) {  
    (void)g\_app.previewViewport-\>waitForGpuIdle(1000);  
  }  
  rtx::gui::drainDeferredRetirement();  
  g\_app.playbackSink.reset();    // 2\. 在线程退出且 GPU 队列排空后再安全释放无头桥  
  g\_app.playbackLastSlot \= 0xFFFFFFFFu;  
}

第二，鉴于 PreviewViewport::releaseGpu() 在 WAIT\_TIMEOUT 下会将待退役资源移交进程级 deferRetirement() 池18，建议将 HeadlessPlaybackSink::slot\_flags\_、NgxRemasterBridge::preview\_slot\_flags\_ 以及 PreviewSlotBorrow/PreviewSlotTicket 中的原始原子指针 std::atomic\<uint32\_t\>\* 升级为标准库 std::shared\_ptr\<std::array\<std::atomic\<uint32\_t\>, 3\>\>19。该纯标准库升级不引入任何自定义包装类型，却能保证即便 WAIT\_TIMEOUT 导致退役队列晚于会话或视口析构，清理 Lambda 依然安全持有该 12 字节原子标志块的生命周期。  
在界面层真实代码消肿方面，当前 rtx\_gui.cpp（994 行）与 workstation\_canvas.h/.cpp 的表驱动及去重压缩重点如下表所示（预计合计压减约 **280–340 SLOC**）14。

| 目标文件与代码区块 | 现存冗余病灶 | 表驱动与去重压缩方案 | 预计净减量（SLOC） |
| :---- | :---- | :---- | :---- |
| workstation\_canvas.cpp 预设与语言刷新 | applyGoldenPreset() 含 5 组重复 if（55 行）；refreshUiLanguage() 含 24 次重复 SetDlgItemTextW 与连续 InvalidateRect（\~48 行）14 | 收敛为 constexpr 黄金预设配置表与 {IDC, StrId} 双语刷新静态表循环14 | 约 −70 至 −80 |
| workstation\_canvas.cpp 控件工厂与参数映射 | createWorkstationControls() 含 \~80 行顺序控件创建调用；collectProfileFromUi/applyProfileToUi/populateComboBoxes 重复字符串与下标映射；fillRect/frameRect 重复建毁画刷14 | 改用声明式控件描述元组表；抽取共享 constexpr 编解码/码控/分辨率/帧率映射数组；改用 DC\_BRUSH 复用单行填充辅助函数14 | 约 −110 至 −135 |
| rtx\_gui.cpp 布局、对话与残渣清理 | 重复 } else if (code \== CBN\_SELCHANGE)；onBrowse() 重复 runFileDialog() 的 OPENFILENAMEW 初始化；InspectorRow/Group 冗余类型；kBaselineSrcWidth/Height 与 workstation\_canvas.h 重复；applyWorkspaceLayout 大段手写 MoveWindow15 | 消除重复分支与死常量；onBrowse 直调 runFileDialog；InspectorRow 与 Group 改用 std::tuple；将按钮组坐标表驱动化9 | 约 −100 至 −125 |

### **3\. 直通加固三：播放内核实质逻辑提纯（预计净减约 250–320 SLOC）**

针对外部工程师将单个 playback\_session.cpp 机械拆碎为 4 个文件（未删注释累计 1,140 行）的“伪消肿”操作1，专家组提供兼顾单文件行数控制与零重复代码的提纯方案。首先，合并删除冗余的 src/core/playback\_session\_impl.h（节省 154 行重复类定义、方法原型与重复文件头），将控制面与视频面统一收拢至单一 playback\_session.cpp（若按 25 号单保持 playback\_session.cpp ≤ 500 行，可将纯 FFmpeg 音频解码与变速自由函数独立保留在 \~180 行的 playback\_session\_audio.cpp 内，而控制+视频面经下列去重压缩后直接控制在 500 行以内，并仍严守 std::jthread 单线程亲和拓扑）3。  
具体的逻辑提纯围绕四处高频重复代码展开：第一，在控制面抽取单一内部助手 Result\<void\> dispatchControlRequest(Impl& impl, Request req, int64\_t target, bool resume, const char\* timeout\_msg)，将 MediaPlaybackSession::seek() 与 stepFrame() 中完全同构的 22 行 seek\_generation、10 秒 done.wait\_for 及错误透传合并为单一实现26。第二，将散落在 play()、pause()、reportSeekDone() 与 performSeek() 中重复出现的 std::get\<1/2/3/4/5\>(clock) 状态元组操作与 audio-\>pause()/resume() 条件宏合并为单一自由函数 setClockPaused(Impl& impl, bool paused)6。第三，将 bootstrapDecoder() 与 performSeek() 复用的解码器重建与 DecoderPtr 删除器构造抽取为 recreateDecoder(Impl& impl)，并将仅在单处调用的 isPassthroughPacket、isAudioPacket、releaseSlot、applyClockSpeed、pushAudioPacket、popAudioPacket 内联化17。第四，在 playback\_session\_audio.cpp 中使用 4 行 template \<auto Fn\> struct IndirectDeleter { void operator()(auto\* p) const noexcept { if (p) Fn(\&p); } }; 取代 freeAudioCodec、freeSwr、freeAvFrame、freeAvPacket 四个单行包装函数27。上述实打实的去重操作预计可在保留全部契约注释的前提下净缩减 **250–320 SLOC**（四文件 1,140 物理行降至约 **820–890 物理行**，约 **690–760 SLOC**）4。

### **4\. Phase 11.2-R 原子收口序列与 Gate R-8 闭环验收清单**

协调负责人收到本方案后，应按以下原子顺序推进执行并锁死防反弹防线，八大门禁全部实证落盘前严禁派发 Phase 11.3（19 号施工单）1：

> 1. 依据 docs/archive/phase112r\_governance\_archive.md 定向回植全部被剥离的设计契约注释、多行 @rtx-type-justification 完整上下文及段落分隔空行（保留 A-1 的 −83 行良性格式清理），验收无误后物理删除该归档文件1。  
> 2. 合入直通加固补丁：修复 cmake/AuditTypes.cmake 逻辑反转与单行/继承匹配缺陷并肃清全仓漏审与伪模板标注2；修正 WorkstationApp 成员拓扑、stopPlaybackSession() 关停时序与 std::shared\_ptr 槽位标志共享所有权14；落地 workstation\_canvas/rtx\_gui 与 playback\_session 实质去重压缩；合入 readFixture 标准实现及 CMakeLists.txt 双平台测试部署加固10。  
> 3. 执行全量自动化构建与双目录单测门禁：确认 cmake \-P cmake/AuditTypes.cmake（含针对 WorkstationApp、Control 等全仓留存类型的 100% 合法输出）、AuditSpdx.cmake、AuditImports.cmake 与 MSVC /MT 0 警告编译全部通过；分别在**项目根目录 C:\\PUBLIC\\CineRTX** 与 **构建输出目录** 两次直接运行 rtx\_tests.exe，确证 **128/128 PASS 且 0 SKIP-GPU、0 SKIP-AUDIO**1，并核验 CineRTX.exe.map 核心非内联修饰符号留存3。  
> 4. 执行 Gate R-8 本地宿主机真机桌面闭环：将 gate\_r8\_desktop\_check.ps1 的压力次数由 80 次修正为 **100 次 Resize**1，在协调负责人宿主机（RTX 5060 \+ 621.24 WHQL \+ Active WTS Session 1）实测并落盘五项铁证：H.264/HEVC/AV1 原生播放与转码监看实现 presented \> 0 且 D2H \== 0 B3；连续 100 次窗口缩放零崩溃泄漏3；快速连续启停播放会话（触发 stopPlaybackSession()）20 次零 UAF 异常；enable\_truehdr \= 0 下 SDR 预览无压暗且 HDR/SDR 跨屏切换正常3；物理拔插音频设备触发 AUDCLNT\_E\_DEVICE\_INVALIDATED 后无锁自动续播3。  
> 5. 编制双轨行数统计表（分别列明物理总行数、SLOC 纯净有效代码行数、本次真实消肿减量与核定转入 Phase 11.3/11.4 追偿的赤字额度）并归档 29 号验收封板记录；随后派发 19 号施工单，并在开工首日即由新版 AuditTypes.cmake 与 SLOC 负增长红线强制锁死 workstation\_canvas 存量 NleTimeline 清零追偿进度14。

#### **引用的著作与仓内资源**

> 1. [`28_CineRTX Phase 11.2-R 治理受挫复核与专家介入协助咨询委托书.md`](28_CineRTX%20Phase%2011.2-R%20治理受挫复核与专家介入协助咨询委托书.md)  
> 2. [`AuditTypes.cmake`](../../cmake/AuditTypes.cmake)  
> 3. [`25_CineRTX Phase 11.2-R SuperPower 施工单_标准库先验替换与全栈脱毒瘦身.md`](25_CineRTX%20Phase%2011.2-R%20SuperPower%20施工单_标准库先验替换与全栈脱毒瘦身.md)  
> 4. [`27_CineRTX Phase 11.2-R Gate R-1 缺口处置裁决请示暨Gate R-8 补测排期申报.md`](27_CineRTX%20Phase%2011.2-R%20Gate%20R-1%20缺口处置裁决请示暨Gate%20R-8%20补测排期申报.md)  
> 5. [`26_CineRTX Phase 11.2-R 施工验收记录.md`](26_CineRTX%20Phase%2011.2-R%20施工验收记录.md)  
> 6. [`playback_session.h`](../../include/rtx/playback_session.h)  
> 7. [`raii_owners.h`](../../include/rtx/raii_owners.h)  
> 8. [`phase112r_governance_archive.md`](../archive/phase112r_governance_archive.md)  
> 9. [`24_CineRTX 第23号文档深度方案.md`](24_CineRTX%20第23号文档深度方案.md)  
> 10. [`test_playback_session.cpp`](../../tests/test_playback_session.cpp)  
> 11. [`CMakeLists.txt`](../../CMakeLists.txt)  
> 12. [GetModuleFileNameW function (libloaderapi.h) - Win32 apps](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulefilenamew)  
> 13. [Current Directory vs Exe Path in Win32 and .NET - t.laboratory](https://tlaboratory.net/en/winapi-getcurrentdirectory-getmodulefilename/)  
> 14. [`workstation_canvas.cpp`](../../src/gui/workstation_canvas.cpp)  
> 15. [`workstation_canvas.h`](../../src/gui/workstation_canvas.h)  
> 16. [`rtx_gui.cpp`](../../src/gui/rtx_gui.cpp)  
> 17. [`playback_session_impl.h`](../../src/core/playback_session_impl.h)  
> 18. [`preview_viewport.cpp`](../../src/gui/preview_viewport.cpp)  
> 19. [`ngx_remaster.cpp`](../../src/core/ngx_remaster.cpp)  
> 20. [`d3d12_raii.h`](../../src/gui/d3d12_raii.h)  
> 21. [`preview_viewport.h`](../../src/gui/preview_viewport.h)  
> 22. [`ngx_remaster.h`](../../include/rtx/ngx_remaster.h)  
> 23. [`playback_session_video.cpp`](../../src/core/playback_session_video.cpp)  
> 24. [`d3d12_raii.h`](../../src/gui/d3d12_raii.h)  
> 25. [`23_CineRTX Phase 11.2 代码膨胀与Slop重度污染治理专家咨询委托书.md`](23_CineRTX%20Phase%2011.2%20代码膨胀与Slop重度污染治理专家咨询委托书.md)  
> 26. [`playback_session.cpp`](../../src/core/playback_session.cpp)  
> 27. [`playback_session_audio.cpp`](../../src/core/playback_session_audio.cpp)  
> 28. [`19_CineRTX Phase 11.3 SuperPower 施工单_统一NLE引擎与零分配AVX2音频总线.md`](19_CineRTX%20Phase%2011.3%20SuperPower%20施工单_统一NLE引擎与零分配AVX2音频总线.md)  
> 29. [`compliance.md`](../spec/compliance.md)  
> 30. [How can I calculate the complete buffer size for GetModuleFileName?](https://stackoverflow.com/questions/805814/how-can-i-calculate-the-complete-buffer-size-for-getmodulefilename)  
> 31. [`test_framework.h`](../../tests/test_framework.h)  
> 32. [set_tests_properties — CMake 4.5.0 Documentation](https://cmake.org/cmake/help/latest/command/set_tests_properties.html)  
> 33. [WORKING_DIRECTORY — CMake 4.5.0 Documentation](https://cmake.org/cmake/help/latest/prop_test/WORKING_DIRECTORY.html)  
> 34. [`17_CineRTX Phase 11 外包施工总纲暨协调负责人备忘录.md`](17_CineRTX%20Phase%2011%20外包施工总纲暨协调负责人备忘录.md)  
> 35. [`24_CineRTX 第23号文档深度方案.md`](24_CineRTX%20第23号文档深度方案.md)  
> 36. [`21_CineRTX Phase11 施工方案深度审查.md`](21_CineRTX%20Phase11%20施工方案深度审查.md)