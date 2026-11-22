# **CineRTX Phase 11.3-R 极限现代 C++ 重构与 NLE 全栈合龙终审验收报告及项目演进方案**

针对项目总协调负责人呈报的 41 号终审验收委托书（CINERTX-P11-DELEGATION-041），首席领域专家组对照 38 号极限重构审计、39 号施工单、40 号施工汇报以及当前工作区实际落盘的源码、构建脚本与门禁输出完成了源码级穿透复审1。针对委托书第六部分提请裁决的两项核心议题，专家组签发如下终审结论：第一，**Phase 11.3-R 核心重构代码品质裁定为「有条件通过（CONDITIONAL PASS）」**——7 大重构任务在语言层与算法层彻底清除了初级外包留下的伪面向对象包装（Wrapper Slop）、全局 operator new 运行库劫持以及体外代码生成器，在编译期参数反射、代数和编辑模型、Deducing this、单遍离散事件扫描线、AVX2+FMA 零堆分配矢量混音与 std::mdspan 多维视图上达到了高标准的现代 C++20/23 工程洁癖1；第二，**NLE 全栈合龙与战略封板裁定为「暂缓封板（HOLD STRATEGIC SEAL）」**——源码穿透揭示出若干尚未闭环的产品级断链与门禁口径缺口，团队须先以一个约 2–3 个工作日的 **Phase 11.3-S（全栈接线与真机闭环收口）** 短冲刺完成产品集成与实机复测，方可核准 NLE 核心战略封板并转入 Phase 11.4（v0.3.0）攻坚1。  
之所以作出“语言重构高度肯定、全栈合龙严格卡点”的差异化裁决，源于五项可复现的源码与实测物证。首先，新建与定向改造的 param\_schema.h、raii\_owners.h、nle\_project.h/.cpp、render\_graph.cpp、audio\_mixer\_avx2.cpp 与 test\_nle\_vertical\_slice.cpp 均严格兑现了标准库第一铁律（Std-First），152 个具名类型通过静态语法审计且 133 项 CTest 在 Release 下全绿1。但与此同时，受限于 39 号施工单的离线文件修改边界，核心新模块尚未完全接入 CineRTX.exe 产品执行链路：RemasterPipeline::run() 仍只支持单文件输入而未消费 compileRenderGraph()，playback\_session\_audio.cpp 仍在调用会引起变调失真的 applyLinearVarispeed() 而未接入流式 WSOLA 与 mixAvx2()，rtx\_gui.cpp 也未将 onNleExportSequence、onNleUndo、onNleRedo 与 onNleDeleteSelected 接入界面交互，致使这些核心非内联符号在 /OPT:REF /OPT:ICF 链接期极易被判定为不可达死代码而遭到剥离10。此外，AuditSloc.cmake 实测 Pure SLOC 为 31,770 行（较 749fe0d 基线净增 \+706 行，超出 39 号单约定的 \<= 30,664 上限 1,106 行），CMakeLists.txt 仍残留多项未清构建杂质，100x 视口压测出现 presented=0 skipped=100 指向无 DWM 的 Session 0 运行环境，且委托书缺失 AuditImports.cmake、CineRTX.exe.map 符号留存及 Debug 模式 \_CrtSetAllocHook 零分配实测日志，同时 39 号与 41 号文档内部仍残留本地绝对路径直链1。

| 终审裁决维度 | 41 号委托书呈报状态 | 专家组源码穿透复核实情 | 终审定性结论 |
| :---- | :---- | :---- | :---- |
| 议题一：7 大任务现代 C++ 洁癖合规性 | 声称全面落实 Std-First 与奥卡姆剃刀原则1 | 核心算法与数据结构高度契合现代 C++20/23 准则5；仅存量文件遗留 std::function、FormatMapping、std::get 残余与失真类型注解16 | **CONDITIONAL PASS** |
| 议题二：NLE 全栈合龙与战略封板 | 提请核准 NLE 统一核心完成合龙并封板1 | render\_graph、mixAvx2、wsolaStretch 与 NleProject 序列化未接入 CineRTX.exe 运行主路径，onNleExportSequence 仅输出状态字符串而未触发导出10 | **HOLD（暂缓封板，转入 11.3-S）** |
| 声学蜂鸣与 Win32 队列治理 | 声称 440 Hz 静音与非阻塞消息泵已根治1 | 0.0f 全静音缓冲与 PeekMessageW 抽水循环已实体落盘19；但 presented=0 skipped=100 表明本次尚未在 WTS Session \> 0 桌面完成真实合成呈现1 | **源码 PASS / 待 Session \> 0 复测** |
| 构建、门禁与双轨 SLOC 账本 | 声称 35 目标零警告、152 类型合规、31,770 Pure SLOC 抹平债务1 | 35 目标零警告、AuditTypes 152/152、AuditSpdx（GPL=107, MIT=37）属实1；但 Pure SLOC 超出 30,664 红线 1,106 行，AuditImports、.map 与 Debug 分配钩子实测缺失1 | **PARTIAL PASS（需平账与补证）** |
| 脱敏审计与仓库主权纪律 | 声明全仓私有直链 100% 相对化且未执行 git push1 | 严格遵守未提交与不推送纪律1；但 41 号文第五节与 39 号文仍存在 本地绝对URI 本地绝对路径直链1 | **发现残余泄露，须脱敏** |

## **一、 7 大核心重构任务源码级穿透审计**

逐文件核查显示，Phase 11.3-R 在底层核心模块上基本兑现了 38 号审计与 39 号施工单的语言设计意图，但在模块跨层耦合、边缘防御一致性以及产品集成端存在需要靶向收口的断点1。  
在编译期内生反射与智能指针基础设施方面（任务 1 与任务 2），include/rtx/param\_schema.h 成功拔除了体外 schema/generated/\* 包含与 GCC 16 代码生成器，甚至比 41 号委托书所记的“自定义 ParamDescriptor”更加贯彻 Std-First 原则——直接以原生 std::array\<std::pair\<ParamMeta, ProfileMemberPtr\>, 20\> 绑定 20 项参数元数据与成员指针，并通过 consteval validateParamDescriptors() 在编译期拦截键名冲突、CLI 标志重复及整型默认值越界1。不过 validateParamDescriptors() 尚未覆盖 ParamKind::Enum 的 default\_str 是否合法落入 enum\_csv（该规则目前仅在单测运行期校验），kCliFlags 仍采用手工硬编码索引（如 10、11、13）而缺乏与 kParamDescriptors 下标的编译期一致性断言，且 src/core/param\_schema.cpp 匿名命名空间内仍重复实现了 toLowerAscii、trimAscii、pathToUtf8 与 utf8ToPath，未复用既有的 include/rtx/string\_utils.h9。在资源与时钟层，include/rtx/raii\_owners.h 的 FnDeleter 已升级为 C++23 static operator() 与 C++20 requires { Fn(p); } 编译期单/二级指针自动分派，物理消灭了 src/core/playback\_session\_audio.cpp 中的 IndirectDeleter；PlaybackClockState 也如期压缩为含 std::optional\<steady\_clock::time\_point\> 的 4 元组并在主循环改用结构化绑定11。但 38 号文明确要求以 C++23 std::move\_only\_function 替代内部 std::function 的改造尚未落盘——raii\_owners.h（CudaDeviceBuffer 与 CudaPinnedHostBuffer）、d3d12\_raii.h、playback\_session\_impl.h 与 hw\_transcode\_internal.h 仍在使用支持拷贝装箱的 std::function4，且 playback\_session.cpp 尚余两处 std::get(impl.clock) 与 std::get(impl.clock) 未统一至结构化绑定20。  
在 NLE 领域模型与单遍执行图方面（任务 3 与任务 4），include/rtx/nle\_project.h 与 src/core/nle\_project.cpp 的代码纯度极为突出：7 个 OOP 命令类、NleEditCommand 与 EditHistory 已被彻底销毁，取而代之的是 6 支纯标准库代数和类型 NleEditDelta 与双端队列对 EditStacks5。NleProject::clip 与 asset 采用 C++23 Deducing this 单模板彻底消除了 const\_cast 与重复重载，toJson() 采用前置逗号保证严格 RFC 8259 零尾逗号输出，fromJson() 全链路以 Result\<NleProject\> 无异常传递错误5。但细节核查发现三项边界隐患：其一，NleProject::fromJson() 按顺序检查 project.asset(\*asset\_id)，若标准 JSON 文件中 "clips" 键排在 "assets" 之前，反序列化将误判为未知资产而失败；其二，nleRegisterAsset 与 nleAddClip 在调用方显式指定非零 id 时未检查 ID 是否与既有项冲突（若 nleAddClip 传入重复 clip.id，applyDelta 会静默跳过插入却仍压入 Undo 栈，导致后续撤销误删原片段），nleRelinkAsset 也未在素材时长缩短时校验或截断已引用片段的 out\_flicks；其三，render\_graph.cpp 的单遍事件扫描线在算法层虽已彻底消除 VideoClaim 与冗余向量拷贝，但仅过滤 NleTrackKind::Video 而完全忽略 A1（lane \== 2）音频编辑切片，且在遇到 asset-\>offline 片段时选择静默 continue 而非返回离线错误6。更为关键的断链在于 src/core/hw\_transcode\_stages.cpp：尽管 encodeWorkerBody() 已补齐 encode\_frame.keyframe \= item.force\_keyframe ? 1 : 0; 闭合 C-05，但 RemasterPipeline::run() 仍围绕单一 options\_.input\_path 读取单段素材，既未接收 RenderGraphSegment 序列，也未在切点实现源切换与时域复位驱动，导致扫描线输出尚未进入真实导出管线10。  
在矢量音频总线与 GUI 集成方面（任务 5、任务 6 与任务 7），include/rtx/audio\_mixer\_avx2.h 与 src/core/audio\_mixer\_avx2.cpp 彻底清退了 VoicePrep 与 AudioMixerScratchpad，以 std::span\<const AudioVoice\> 配合 \_mm256\_fmadd\_ps 实现零堆分配混音（仅需注意当前混音实现为纯粹线性累加而未附带 Gate 11D-2 约定的 \[-1.0, 1.0\] 总线软限幅保护）8。src/core/wsola\_stretch.cpp 成功引入进程级 alignas(32) 只读 kHannTable 与 C++23 std::mdspan \[frame, channel\] 视图，但存在两处实际约束：一方面它仅为无状态单块函数，缺乏跨缓冲块保持重叠相加尾迹进位的 WsolaStreamState，且 512 样本分析窗（48 kHz 下约 10.67 ms）配合单声道非归一化点积在低频音频调速时易受能量偏置影响7；另一方面，src/core/playback\_session\_audio.cpp 至今仍在调用旧的 applyLinearVarispeed() 自由函数，根本未接入 wsolaStretch 或 mixAvx211。同理，在 GUI 层虽然 src/gui/workstation\_canvas.h/.cpp 已按值持有 NleProject 与 EditStacks、清退了 15 处冗余判空并将后台导出状态改用 std::atomic\<bool\> 与 std::jthread12，但 src/gui/rtx\_gui.cpp 从未在按钮或快捷键中调用 onNleExportSequence、onNleUndo、onNleRedo 与 onNleDeleteSelected（IDC\_BTN\_NLE\_EXPORT 仅回跳调用将单个素材路径填入输入框的 exportSelectedClipToEncoder）12；同时 onNleExportSequence 自身也只运行了 compileRenderGraph() 并将段数打印到状态栏，并未启动 RemasterPipeline 执行导出，timelineCanvasProc 亦未实现按 (lane, x) 命中选中或拖拽移动片段，addFilesToBin 也仅向 V1 轨道插入视频片段而留空 A1 音轨12。由此造成的结果是：NleProject::toJson/fromJson、compileRenderGraph、mixAvx2 与 wsolaStretch 在整个 CineRTX.exe 调用图中均处于未引用状态，在开启 /OPT:REF 与 /OPT:ICF 的生产编译下极可能被链接器剥离4。

| 核心重构任务 | 物理交付文件范围 | 现代 C++ 达标亮点 | 实证遗留缺口（产品级 / 边界级） | 审查判定 |
| :---- | :---- | :---- | :---- | :---- |
| 任务 1：内生参数反射 | param\_schema.h、param\_schema.cpp、test\_param\_schema\_reflection.cpp2 | constexpr 描述符表 \+ consteval 自检 \+ forEachProfileField，彻底删除 schema/ 生成器2 | validateParamDescriptors 未校验 Enum 默认值与 kCliFlags 下标一致性；param\_schema.cpp 未复用 string\_utils.h 且残留 crf 死分支9 | **PASS（微瑕待净）** |
| 任务 2：释放器与时钟 | raii\_owners.h、playback\_session.h/.cpp/\_impl.h/\_video.cpp/\_audio.cpp2 | FnDeleter static operator() \+ requires 合并指针/二级指针；4 元组 optional 时钟 \+ 结构化绑定16 | 多处析构回调仍用 std::function 未切 std::move\_only\_function16；playback\_session.cpp 仍残余 2 处 std::get20 | **CONDITIONAL PASS** |
| 任务 3：NLE 代数模型 | nle\_project.h、nle\_project.cpp2 | 6 支 std::variant \+ EditStacks；Deducing this 零 const\_cast；RFC 8259 零尾逗号；无异常 Result\<T\>5 | fromJson 依赖 "assets" 先于 "clips" 出现；显式非零重复 ID 与 Relink 缩短时长未拦截34 | **PASS（边界加固）** |
| 任务 4：扫描线执行图 | render\_graph.h、render\_graph.cpp、hw\_transcode\_stages.cpp2 | 单遍离散事件扫描线消除 VideoClaim 与向量拷贝；GPU 编码分支补齐 encode\_frame.keyframe（C-05）6 | RemasterPipeline::run() 未接入多段 RenderGraphSegment 导出；compileRenderGraph 未处理 A1 音轨且静默跳过离线资产6 | **算法 PASS / 集成 FAIL** |
| 任务 5：AVX2 与 WSOLA | audio\_mixer\_avx2.h/.cpp、wsola\_stretch.cpp2 | \_mm256\_fmadd\_ps 零堆分配混音；alignas(32) 静态只读汉宁窗 \+ std::mdspan 二维切片7 | playback\_session\_audio.cpp 仍调用变调的 applyLinearVarispeed()11；缺跨块 WsolaStreamState 与总线软限幅7 | **算子 PASS / 接入 FAIL** |
| 任务 6：GUI 与防竞态 | workstation\_canvas.h/.cpp、rtx\_gui.cpp2 | 旧符号 0 残留；按值持有消除 15 处判空；原子忙闲标志 \+ std::jthread request\_stop() 安全停机12 | rtx\_gui.cpp 未接线导出/Undo/Redo/Delete；onNleExportSequence 只做图计数未真导出；画布不支持按轨选段与拖拽12 | **并发 PASS / UI 断链** |
| 任务 7：隔离单元测试 | test\_audio\_mixer\_avx2.cpp、test\_nle\_vertical\_slice.cpp2 | 全局 operator new 彻底清零；std::array\<AudioVoice, 32\> \+ 局部 \_CrtSetAllocHook；7 类操作全覆测15 | 41 号文仅在 Release 跑测，\_DEBUG 分配钩子未实测；缺 WSOLA \< 0.05% 基频漂移与多段真实容器导出断言1 | **隔离 PASS / 覆盖待补** |

## **二、 声学治理、门禁物证核验与双轨 SLOC 账本复算**

### **2.1 声学蜂鸣根治与 Session 0 运行边界确认**

源码取证确认，Phase 11.3 早期测试期间触发宿主机扬声器发声与系统警报的双重物理根因已被准确消除。在 tests/test\_playback\_session.cpp 的 playback\_wasapi\_shared\_mode\_master\_clock 测试中，原有以 0.2f \* std::sin(...) 合成的 440 Hz 正弦波循环已被替换为 std::vector\<float\> interleaved(frames \* channels, 0.0f) 全静音缓冲，在确保物理声卡绝对零分贝输出的前提下完整保留了 IAudioClock::GetPosition() 时钟演进与暂停冻结校验25。同时，tests/test\_raii\_retirement.cpp 的 preview\_viewport\_100x\_create\_destroy\_zero\_leak 在每次轮询排空后补充了非阻塞 PeekMessageW(\&pump\_msg, nullptr, 0, 0, PM\_REMOVE) 消息泵循环，消除了百次窗口建毁对测试线程消息队列的堵塞以及由此诱发的 win32k.sys 输入缓冲区溢出警报19。  
但必须指出，41 号文第 3.2 节记录的实测输出 created=100 presented=0 skipped=100 直接揭示出本轮 133 项测试的运行环境局限1。核对 src/gui/preview\_viewport.cpp 与 tests/test\_raii\_retirement.cpp 实现可知，PreviewViewport::create() 仅创建子窗口句柄与加载 dcomp.dll，而 DirectComposition 交换链的创建是在首次 presentShared() 调用 adoptDevice() 时触发的27。在无 DWM 桌面合成器的 Session 0 无头会话下，CreateSwapChainForComposition 会稳定返回 DXGI\_ERROR\_NOT\_CURRENTLY\_AVAILABLE 导致所有帧呈现被安全跳过（即 presented=0, skipped=100）；而在具备真实桌面合成的 WTS Session \> 0 会话中，Gate R-8 明确要求实证 presented \> 0 与 D2H \== 0 B19。因此，本次 133 项 CTest 通过仅能证明无头路径、内存退役及基础逻辑无误，尚不足以替代交互式桌面实呈门禁的最终签认1。

### **2.2 四大门禁、缺失证据与脱敏合规审查**

按 35、38 与 39 号文件确立的准出标准核对 41 号文第四部分实测台账，多项关键硬门禁在交付记录中存在缺口或偏差，具体复核如下。

| 门禁检验项 | 权威契约要求（35 / 38 / 39 号文） | 41 号文实际呈现读数 | 复核结论与缺口说明 |
| :---- | :---- | :---- | :---- |
| 全量构建与零警告 | MSVC 19.51 /std:c++latest /arch:AVX2 0 错误 0 警告；拔除 /Gs- 与 if(TRUE)1 | 35/35 目标全绿，0 错误 0 警告；已移除 /Gs- 与 if(TRUE)1 | **PASS**（但 CMakeLists.txt 尚留其余 38 号文列明的构建冗余项待清）4 |
| 具名类型门禁 (AuditTypes.cmake) | 全仓具名类型 100% 附带合法契约并严格落入六类白名单3 | 实测 152 具名类型 0 违规1 | **语法 PASS / 语义未净**：d3d12\_interop.cpp 仍留 FormatMapping23；flicks.h、nvidia\_shim.h、ffmpeg\_io.cpp 等仍存批量套用的失真说明24 |
| 许可证边界 (AuditSpdx.cmake) | 三区物理隔离合规，全部源文件含合法 SPDX 头2 | PASS（GPL=107, MIT=37）1 | **PASS**（较 749fe0d 基线 GPL=98, MIT=37 净增 9 份新建 GPL 源码/单测）2 |
| 二进制闭包 (AuditImports.cmake) | CineRTX.exe 体积处于 1.2–12.0 MB 包络且零驱动/媒体 DLL 泄漏4 | **未列示执行记录**1 | **MISSING（须补跑归档）** |
| Linker .map 符号留存 | /OPT:REF /OPT:ICF 下 CineRTX.exe.map 物理留存 toJson/fromJson、compileRenderGraph、mixAvx2、wsolaStretch4 | **未列示检索记录**1 | **HIGH RISK / MISSING**：因主程序尚未调用这四组函数，链接器极可能已执行死代码剥离10 |
| Debug 零分配与桌面实测 | Debug 构建下 \_CrtSetAllocHook 断言 0 次堆分配；WTS Session \> 0 实呈 presented \> 018 | 仅运行 \-C Release（133/133 PASS），100x 视口 presented=01 | **INCOMPLETE**：test\_audio\_mixer\_avx2.cpp 的 \_CrtSetAllocHook 在 Release 下被 \#ifdef \_DEBUG 编译剔除，零分配仅靠 Release 不崩无法替代1 |
| 全仓隐私与路径脱敏 | 文档与代码零本地绝对路径直链1 | 41 号文注记自述 0 命中1 | **FAILED**：41 号文第五节保留 <local_absolute_path> .gemini/antigravity/brain 本地路径）1，39 号文亦保留 <public_root>/CineRTX3 |

### **2.3 双轨 SLOC 账本口径校准与 CMake 未尽清洗项**

41 号文第 四部分将本次实测 Pure SLOC: 31,770 行 描述为“远低于 39,626 行基线约束上限，彻底抹平历史虚高债务”，这一结论混淆了两套互不可比的度量口径1。历史文档核验表明，39,626 行是 Phase 11.2-R 初期基于“物理总行数（含空行、注释及早期 webui/ 等更宽统计口径）”设立的红线；而在 Commit 749fe0d 进场时，固定扫描脚本 cmake/AuditSloc.cmake 已正式将 Phase 11.3 的同口径 Pure SLOC 基线锁定为 **31,064 行**（规范集合 135 个文件），并由 39 号施工单 Gate 11.3-R4 规定结项 Pure SLOC 必须达到 **30,664 行或以下**（净减 400–600 行）3。

| 度量节点与样本口径 | 扫描文件数 | 物理总行数 | 空白行 | 语义注释行 | Pure SLOC | 相对 749fe0d Pure SLOC 净变化 | 相对 39 号单红线（\<= 30,664）偏差 |
| :---- | :---- | :---- | :---- | :---- | :---- | :---- | :---- |
| 749fe0d 入场法定基准（含 schema/ 核心 3 文件） | 1354 | 38,3244 | 2,528 *(推算)* | 4,7324 | **31,064**4 | 0 | \+400 |
| Phase 11.3 初次驳回版（含 schema/ 3 文件） | 141+ | 未单列 | 未单列 | 未单列 | **32,380**4 | **\+1,316**4 | \+1,716（严重倒挂） |
| Phase 11.3-R 当前交付（拔除 schema/ 3 文件，新增 9 文件） | 1411 | 39,2711 | 2,6441 | 4,8571 | **31,770**1 | **\+706**1 | **\+1,106（未平账）** |

按 135 \- 3 \+ 9 \= 141 个文件的精确集合变动还原账本，出现 1,106 行缺口的原因非常清晰：首先，cmake/AuditSloc.cmake 自身输出模板中仍硬编码打印过时的 Phase 11.2-R Locked Entry Baseline: 38,562 与 Benchmark target \<= 39,626 字样45，客观上导致了核验比对时的基准漂移；其次，在删除原有 schema/ 3 文件（约占 480–520 Pure SLOC）后4，新增的 9 份高质量 NLE/DSP 源码与严密单元测试带来了约 1,200 行纯净代码增量，高出 38 号文预先估算的 420–520 行增量1；再次，宿主机合龙阶段仅从 CMakeLists.txt 剔除了 /Gs-、if(TRUE) 与 schema/ 规则，38 号审计第三部分要求清理的其余 CMakeLists.txt 构建杂质仍原封不动驻留于当前工作区源码中1：

> 1. **配置期重复与变量冗余**：set(RTX\_EXTERNAL\_DIR ...) 在 CMakeLists.txt 前后段重复赋值两次；EmbedPtx.cmake 仍在配置阶段执行两次 execute\_process 生成 PTX 头文件，又在构建阶段以 add\_custom\_command 重复定义生成规则4。  
> 2. **属性复制与镜像分支**：CXX\_STANDARD 23、CXX\_STANDARD\_REQUIRED ON、CXX\_EXTENSIONS OFF 属性块仍在 rtx\_core、rtx\_shim\_static、rtx\_ngx\_bridge\_static、CineRTX 与 rtx\_tests 五个目标上重复硬编码；rtx::ngx\_bridge 别名及公共链接仍在 if(WIN32) 与 else() 分支内书写了两份完全一致的代码；CineRTX 与 rtx\_tests 下仍存在相邻重复的 if(WIN32) 块，且文末 install 区仍残留空 if(WIN32) endif() 僵尸块4。  
> 3. **全局优化标志篡改与路径遮蔽**：string(REPLACE "/O2" "/O1" CMAKE\_CXX\_FLAGS\_RELWITHDEBINFO ...) 仍在全局作用域篡改所有子库优化级别而非限定于 CineRTX 单目标；rtx\_ngx\_bridge\_static 同时对 \${RTX\_NGX\_INCLUDE\_DIR} 配置 PRIVATE 与 SYSTEM PRIVATE 导致系统头警告抑制失效；rtx\_project\_options 全局挂接 /wd4324 抑制缓存行对齐告警而非收窄至具名对齐边界4。  
> 4. **未收口的实验开关与网络拉取**：configure 阶段仍默认静默尝试联网下载 RIFE ONNX 权重而未提供显式离线/缓存开关，且保留了专用于迁就旧版 Zig 0.13 LLVM 18（缺少原生 jthread/stop\_token 完整支持）的 \-fexperimental-library 与空归档阴影库 rtx\_ngx\_shadow\_archives4。

此外，41 号文在附注中披露了一项极具价值的门禁脆弱点：AuditTypes.cmake 使用 file(STRINGS ... ENCODING UTF-8) 读取源文件行列表，导致注释中含半开区间方括号（如 \[0, N)）或特定分隔字符时发生行截断解析异常1。比对 AuditSloc.cmake 可见，后者采用 file(READ) 配合 \\ 与 ; 字符转义后再拆分的机制（从而彻底免疫 ; 与 \[\] 词法干扰）45。因此，后续演进应直接升级 AuditTypes.cmake 的文件读取前端，从根源上消除词法解析陷阱，而不仅依赖人工规避注释标点。

## **三、 面向 v0.3.0 封板与 v0.4.0 的分阶段演进方案**

基于工作区当前真实代码基准，建议摒弃推倒重来，保留 Phase 11.3-R 已经锤炼成熟的现代 C++23 核心资产，沿着 **Phase 11.3-S（产品合龙收口）→ Phase 11.4（**v0.3.0 **高阶算子与编码封板）→ Wave 4（**v0.4.0 **架构扩展）** 的三步垂直切片路径稳步演进4。

### **3.1 Phase 11.3-S：NLE 最后一公里合龙与门禁收口（建议 2–3 个工作日）**

本阶段目标是打通核心库向 CineRTX.exe 终端产品的最后一公里接线、消除残留构建/代码债并补齐真机实证，达标后即执行受控提交并正式批准 NLE 核心战略封板。该阶段拆解为三个并行推进的工作包：  
**工作包 S-1：渲染图全链路导出、流式 WSOLA 与 GUI 交互闭环**。在离线导出端，修改 src/core/hw\_transcode\_stages.cpp（及 hw\_transcode\_internal.h），为 RemasterPipeline 实现接收 std::span\<const RenderGraphSegment\> 的多段执行循环：整个导出过程保持单一 encoder\_ 与 muxer\_ 实例不断流并按时间线 Flicks 换算单调递增的 90 kHz PTS；当段间切换素材源路径或发生时间跳转时复用或重建 demuxer\_ 与 decoder\_，在每个片段首帧严格置位 force\_reset\_temporal \= true（驱动 NVSDK\_NGX\_Parameter\_Reset 重置 VSR 时域历史）与 force\_keyframe \= segment.force\_keyframe（利用已闭合的 C-05 链路强制输出 IDR 关键帧）10。同步修正 src/core/render\_graph.cpp 在 asset-\>offline 时的静默跳过逻辑，改为明确返回 ErrorCode::NotFound 防止导出静默黑帧，并扩展扫描线或音轨编译入口同步提取 A1（lane \== 2）音频区间与增益参数，形成完整的 1V \+ 1A 导出执行图6。在实时音频端，围绕 wsolaStretch 在 src/core/wsola\_stretch.cpp 中建立带环形分析缓冲、归一化互相关与重叠相加尾迹进位的 WsolaStreamState（附合法 NonTrivial-StateMachine 契约，扩展分析窗以适配 48 kHz 基频，并在主混音链按需接入软限幅），随后在 src/core/playback\_session\_audio.cpp 中彻底物理删除会引起音高畸变的 applyLinearVarispeed()，全面切换为流式 WSOLA 与单轨 mixAvx2 栈切片7。在前端控制端，为 src/gui/rtx\_gui.cpp 与 src/gui/workstation\_canvas.cpp 补齐 Ctrl+Z（onNleUndo）、Ctrl+Y（onNleRedo）与 Delete（onNleDeleteSelected）响应，将 onNleExportSequence 真正接线至 RemasterPipeline 多段导出并由 NLE 界面按钮触发，接入工程保存/读取（实际消费 NleProject::toJson() 与 fromJson()），同时完善 timelineCanvasProc 按 (lane, x) 坐标联合命中选中与拖拽移动、addFilesToBin 同步挂载 A1 音轨，并补全 NleProject::fromJson() 对 JSON 键乱序（"clips" 在 "assets" 之前）及 nleRegisterAsset/nleAddClip 重复非零 ID 的校验5。  
**工作包 S-2：构建系统深度脱毒、Std-First 尾差清零与文档脱敏**。对照 38 号审计清单清洗 CMakeLists.txt 中前文定位的全部冗余项，收口为单点 rtx\_project\_options 管控，移除 rtx\_ngx\_shadow\_archives 与重复分支，同时更新 cmake/AuditSloc.cmake 提示横幅以匹配 31,064 基线并全面删除 schema 旧字样4。升级 cmake/AuditTypes.cmake 采用 file(READ) \+ ; 转义按行切分，根治 file(STRINGS) 遇到半开区间方括号或分号时的解析脆弱性1。在源码洁癖收尾上，将 d3d12\_interop.cpp 中的 FormatMapping 替换为 constexpr std::array\<std::tuple\<...\>, 3\>4；将 raii\_owners.h、d3d12\_raii.h、playback\_session\_impl.h 与 hw\_transcode\_internal.h 中的 std::function 全面升级为 C++23 std::move\_only\_function 并以 std::erase\_if 精简 GpuRetirementQueue::collect()4；消除 playback\_session.cpp 剩余 2 处 std::get20；让 param\_schema.cpp 复用 string\_utils.h 并在 param\_schema.h 中增加 consteval 对 Enum 默认值与 kCliFlags 下标的校验（同时清理 param\_schema.cpp 无用 crf 死分支）9；刷新 flicks.h、nvidia\_shim.h、ffmpeg\_io.cpp 中批量套用的失真 @rtx-type-justification 文本24。最后，物理清除 39 号文与 41 号文中的宿主机 本地绝对URI 绝对路径（将 \[DEC-037\] 原文同步纳入仓内受控文档管理，消除对外部本地脑图路径的引用）1。  
**工作包 S-3：全口径真机复测与静态闭环验证**。在 MSVC 19.51 下同时跑通 Debug 与 Release 构建，落实五项强制物证：在 Debug 模式下运行 test\_audio\_mixer\_avx2 验证局部 \_CrtSetAllocHook 计数严格等于 0，并补入 WSOLA 0.25x–2.0x 调速基频漂移 \< 0.05% 单测及多段 .mkv 导出容器解复用总帧数/切点 IDR 逐帧一致性测试18；执行 cmake \-P cmake/AuditImports.cmake 确认 CineRTX.exe 保持在 1.2–12.0 MB 包络且零动态 DLL 泄漏42；在 /OPT:REF /OPT:ICF 全开下检索 CineRTX.exe.map 的 Publics by Value 节区，验证 NleProject::toJson/fromJson、compileRenderGraph、mixAvx2 与 wsolaStretch 全部实体留存4；运行 cmake \-P cmake/AuditSloc.cmake 核验经 CMake 清洗与冗余剔除后的 Pure SLOC 达标落位（目标压回 \<= 30,664 或在审定新增单测透明豁免后重锁基准）3；在 **WTS Session \> 0** 真实交互式桌面会话（NVIDIA 驱动 \>= 621.24 WHQL）下全量跑通 rtx\_tests.exe（preview\_viewport\_100x 实测 presented \> 0、D2H \== 0 B）并完成一次 1V+1A 剪辑、撤销重做、Relink 与多段导出实测1。

### **3.2 Phase 11.4：RIFE 高阶算子、滞后显存调度与 SDK 13.1 封板 v0.3.0（建议 4–5 个工作日）**

经核查现行源码，rife\_builder.cpp、rife\_engine.cpp 仍停留在 kAlign \= 64 与 1080p 上限配置，ngx\_bridge\_abi.h 尚无 RtxRifeConfig，nv\_encoder.cpp 仍为单比特流输出缓冲与 frameIntervalP \= 1 且无 VramAdaptiveScheduler50。待 Phase 11.3-S 完成 NLE 合龙封板后，应立即按 20 号施工单与 38 号审计规划全面启动 Phase 11.44：  
在 **RIFE** scale=0.5 **与管线拓扑闭环**方面，保持 include/shim/ngx\_bridge\_abi.h 的 MIT OR Apache-2.0 开源边界并追加 ABI 结构体 RtxRifeConfig 与 rtx\_ngx\_bridge\_init\_rife\_ex 导出入口14。在 rife\_engine.cpp 与 rife\_builder.cpp 中针对 flow\_scale \== 0.5f 将张量对齐模数从 64 升级至 A \= 128（保证两级光流降采样后维度整除对齐），将动态 Optimization Profile 上限拓宽至 4K 对齐分辨率 3840x2176（同步保留 clearFlag(kTF32) 与坐标/算术敏感层 FP32 锁定）35；修改 rife\_pack.cu 实现 reflect101 边界镜像填充与输出回写裁剪核函数，采用保守兼容的 PTX ISA 版本重新编译同步 rife\_pack.ptx，防止低版本驱动发生 JIT 拒载35；在 hw\_transcode\_stages.cpp 中按 TopologyTier 传入真实输入尺寸（修正当前写死源分辨率调用 initRife 的断点），并将已预分配却长期闲置的 anchor\_p010\_ 真正接入 vsr-first 超分后补帧流转，同时以新建的 tests/test\_rife\_scale05.cpp 验证无边缘黑边且实测记录 trt\_peak\_allocation\_bytes10。  
在 **基于 WDDM 实际预算的滞后防抖显存调度**方面，在 8 GB GDDR7 显存的 RTX 5060 Laptop GPU 上，扣除操作系统与桌面合成占用后，DXGI 报告的进程可用本地显存预算 dxgi\_budget\_bytes 落在约 7,200–7,600 MiB 区间（按 7,300 MiB 建模，82% 降级高水位线约为 5,986 MiB，68% 恢复低水位线约为 4,964 MiB）35。落盘具备 NonTrivial-StateMachine 声明的 VramAdaptiveScheduler（最小驻留周期 120 帧），以 dxgi\_budget\_bytes 为核心分母联合 dxgi\_current\_usage\_bytes、cuMemGetInfo、真实 trt\_peak\_allocation\_bytes 与编解码参考池，按 VsrFirst\_RifeFull \-\> VsrFirst\_RifeHalf \-\> VsrFirst\_Nvofa \-\> FgFirst\_RifeFull \-\> PassThrough\_NoFruc 五级梯队平滑降级，保持 NVOFA 与 RIFE 互斥按需生命周期，实证 8 GB 显卡 4K 压制全程显存受控于 82% 阈值下且零 OOM14。  
在 **Video Codec SDK 13.1 双模协商与多缓冲池重构**方面，继续维持 .gitmodules 中 nv-codec-headers n13.0.19.1 的 R580 兼容生产基线，而在 src/shim/loader/nv\_encoder.cpp 初始化前调用 NvEncodeAPIGetMaxSupportedVersion 执行运行时双模版本探测14。当驱动支持 \>= 13.1（对应驱动 \>= 610.0）时通过局部隔离常量补充分层参考枚举与 UHQ 参数并调用 NvEncGetEncodeCaps 探测能力，低版本驱动或硬件不支持则优雅回退至 Default 并输出告警日志14。同步废除单一 NV\_ENC\_OUTPUT\_PTR bitstream 状态机，重构为容量不低于 4 \+ B帧数 \+ Lookahead深度 的 std::vector\<BitstreamSlot\> 异步完成事件环形池，将 NV\_ENC\_ERR\_NEED\_MORE\_INPUT 合法识别为缓冲蓄水信号而非内部错误，并重写 rtx\_shim\_encoder\_flush() 在提交 EOS 后循环锁定排空所有延迟重排包35。最终在 RTX 5060（驱动 \>= 621.24 WHQL）上如实记录测试口径，交付覆盖 Default vs HighQuality-7B vs Master-UHQ-15B/31B 的 FPS、编码器占用、VRAM 峰值与 PSNR/SSIM/VMAF 全表后，正式打标发布 v0.3.01。

### **3.3 Wave 4：v0.4.0 架构扩展与工具链前瞻演进**

在 v0.3.0 稳固底盘之上，Wave 4 将秉持同样的极限现代 C++ 与零重型依赖哲学推进五项演进4：

> 1. **工具链升级与 C++26 渐进落盘**：待主力编译器升至 MSVC 14.52+（cl 19.52 修复了 19.51 下 /arch:AVX2 /O2 对 std::hash\<float\> \+0.0f/-0.0f 的哈希缺陷）后，可将全局 /std:c++latest 收敛为稳定 /std:c++23；在检测到 \_\_cpp\_lib\_inplace\_vector 就绪后，将局部栈定容数组平滑替换为 C++26 std::inplace\_vector，并逐步卸载面向旧版 Zig 0.13 缺失标准库的实验性编译选项4。  
> 2. **零外部运行时** .otio **工程互导**：利用 NleProject（UUID 资产、相对路径、offline 标记与整数 Flicks 区间）与 OpenTimelineIO 数据原语的高度同构性，直接基于 json\_scanner.h 编写约百余行纯 C++ 解析映射函数实现 .otio 粗剪时间轴无损导入导出，不引入 Python 或庞大第三方运行时4。  
> 3. **RIFE CUDA Graph 发射加速与多轨转场合成**：在 rife\_engine.cpp 中面向固定的 A \= 128 分辨率分桶引入 cudaStreamBeginCapture / cudaGraphLaunch 图捕获消除频繁内核发射开销（析构严格执行 RAII 释放）4；在 compileRenderGraph() 与着色器层叠加多轨交叉叠化（Cross-Dissolve）节点，并探索为 MediaPlaybackSession 挂载轻量动态 VSR 实时预览及 WebUI v3 双向状态同步4。  
> 4. **进程级 NGX 单例主从让渡契约（Primary-Yield Policy）**：遵循社区实证揭示的 RTX VideoEnhancement 进程级全局单例约束，确立当后台批处理转码或离线导出启用 NGX VSR 时，前台预览视口自动让渡 NGX 实例而仅通过 6,220,800 B（约 5.93 MiB）三槽 D3D12 共享缓冲进行轻量色调映射呈现的调度契约4。  
> 5. **CPU AV1 编码器插件化独立评估**：在确保 v0.3.0 维持纯正 LGPL 与 \<= 12.0 MB 单二进制包络的前提下，于 v0.4.0 周期独立评估通过可选外挂模块引入 libsvtav1 的可行性（规避将约 23 MB 静态归档 libSvtAv1Enc.a 直接编入主 EXE 击穿体积红线）26。

| 演进波次 | 建议工期 | 核心聚焦领域 | 决定性退出门禁与交付里程碑 |
| :---- | :---- | :---- | :---- |
| Phase 11.3-S（当前即刻启动） | 2–3 工作日 | NLE 全链路接线、流式 WSOLA、CMakeLists.txt 深度脱毒与真机复测 | RemasterPipeline 多段硬切容器解复用逐帧吻合；applyLinearVarispeed 物理删除且 WSOLA 漂移 \< 0.05%；Pure SLOC 账本平账；AuditImports、.map 符号留存、Debug \_CrtSetAllocHook \== 0 与 WTS Session \> 0 presented \> 0 全绿1 |
| Phase 11.4（v0.3.0 收官） | 4–5 工作日 | RIFE scale=0.5（A=128 \+ reflect101）、真实 DXGI 滞后调度与 SDK 13.1 多缓冲池 | test\_rife\_scale05 零黑边；8 GB RTX 5060 4K 压制全程显存 \< 82%（\~5,986 MiB）零 OOM；AV1 Default / 7B / 15B/31B+UHQ 全口径实测矩阵落盘，正式打标发布 v0.3.014 |
| Wave 4（v0.4.0 演进） | 独立立项周期 | MSVC 14.52+ / C++26 演进、纯 C++ .otio 互导、CUDA Graph、Cross-Dissolve 与 NGX 让渡 | 零 Python 实现 .otio 互导；多轨叠化实时预览与导出稳定；主程序严守单 EXE 包络与三区开源合规边界4 |

## **四、 即时签发执行指令清单**

在签署 Phase 11.3 最终封板决议及执行任何 git commit 之前，项目总协调负责人须严格按以下顺序落实收口操作，并继续恪守严禁未经授权执行 git push 的开源主权铁律1：

> 1. **立即完成文档路径脱敏与黄金现场只读保护**：清除 docs/phase11/39 与 docs/phase11/41 中的宿主机 本地绝对URI 绝对路径直链，将 \[DEC-037\] 工程军规同步收归仓内受控规范体系，在 Phase 11.3-S 未合龙前保持工作树受控且不提交半成品1。  
> 2. **派发 Phase 11.3-S 工作包 S-1（产品接线闭环）**：在 hw\_transcode\_stages.cpp 落地 RemasterPipeline::runRenderGraph 多段硬切导出及 A1 音轨对齐；在 wsola\_stretch.cpp 与 playback\_session\_audio.cpp 落地 WsolaStreamState 并彻底删除 applyLinearVarispeed()，接入 mixAvx2；在 rtx\_gui.cpp、workstation\_canvas.cpp 与 nle\_project.cpp 补全序列导出、Undo/Redo/Delete、画布按轨选段拖拽、JSON 键乱序解析及重复 ID 防护6。  
> 3. **执行工作包 S-2（构建脱毒与 Std-First 尾差清零）**：消除 CMakeLists.txt 剩余冗余配置，升级 AuditTypes.cmake 为 file(READ) 解析并修正 AuditSloc.cmake 提示口径，拔除 FormatMapping，升级 std::move\_only\_function 与 std::erase\_if，复用 string\_utils.h 清理 param\_schema.cpp，刷新三份存量文件的失真类型注解4。  
> 4. **在真实桌面环境下完成工作包 S-3 全口径复测**：切换至 WTS Session \> 0 交互式 Windows 桌面（驱动 \>= 621.24 WHQL），同时跑通 MSVC Debug（验证 \_CrtSetAllocHook \== 0 与 WSOLA \< 0.05% 漂移）与 Release 全量构建，补全 AuditImports.cmake、CineRTX.exe.map 核心符号检索、AuditSloc.cmake 平账读数、presented \> 0 视口实呈以及 1V+1A 多段导出容器逐帧验收铁证1。  
> 5. **提请复核封板并转入 Phase 11.4**：在以上四步全部交齐可复现物证后，由协调负责人呈交极简补充验收单，专家组即时签发 NLE 核心战略封板令并授权派发 Phase 11.4（Track 11E）施工单，全速推进 v0.3.0 正式发行14。

#### **引用的著作**

> 1. 41\_CineRTX Phase 11.3-R 极限现代C++重构与NLE全栈合龙专家终审验收委托书.md,   
> 2. 40\_CineRTX Phase 11.3-R 施工汇报\_极限现代C++重构与NLE全栈合龙.md,   
> 3. 39\_CineRTX Phase 11.3-R SuperPower 施工单\_极限现代C++重构与NLE全栈合龙.md,   
> 4. 38\_CineRTX 现代C++ 极限重构审计.md,   
> 5. nle\_project.h,   
> 6. render\_graph.cpp,   
> 7. wsola\_stretch.cpp,   
> 8. audio\_mixer\_avx2.cpp,   
> 9. param\_schema.h,   
> 10. hw\_transcode\_stages.cpp,   
> 11. playback\_session\_audio.cpp,   
> 12. workstation\_canvas.cpp,   
> 13. rtx\_gui.cpp,   
> 14. 10\_CineRTX\_Phase11\_施工单\_Track\_11E\_VSR\_FRUC显存感知自适应调度与SDK13.1分级接入.md,   
> 15. test\_nle\_vertical\_slice.cpp,   
> 16. raii\_owners.h,   
> 17. CMakeLists.txt,   
> 18. test\_audio\_mixer\_avx2.cpp,   
> 19. test\_raii\_retirement.cpp,   
> 20. playback\_session.cpp,   
> 21. playback\_session\_impl.h,   
> 22. d3d12\_raii.h,   
> 23. d3d12\_interop.cpp,   
> 24. flicks.h,   
> 25. test\_playback\_session.cpp,   
> 26. 14\_CineRTX Phase 11 复盘与演进.md,   
> 27. preview\_viewport.cpp,   
> 28. param\_schema.cpp,   
> 29. test\_param\_schema\_reflection.cpp,   
> 30. string\_utils.h,   
> 31. playback\_session\_video.cpp,   
> 32. playback\_session.h,   
> 33. hw\_transcode\_internal.h,   
> 34. nle\_project.cpp,   
> 35. CineRTX Phase11 施工方案深度审查,   
> 36. 09\_CineRTX\_Phase11\_施工单\_Track\_11D\_DirectComposition零回读视口与统一NLE引擎及AVX2音频矩阵.md,   
> 37. workstation\_canvas.h,   
> 38. ffmpeg\_io.cpp,   
> 39. nvidia\_shim.h,   
> 40. RTX超分与HDR转码实施方案,   
> 41. 36\_CineRTX Phase 11.3 SuperPower 施工单\_统一NLE引擎与零分配AVX2音频总线.md,   
> 42. AuditImports.cmake,   
> 43. CineRTX Phase 11.2-R 终审方案,   
> 44. 37\_CineRTX Phase 11.3 施工验收与极限现代C++重构专家咨询委托书.md,   
> 45. AuditSloc.cmake,   
> 46. Why is jthread not presented in libc++ (using clang 18, c++26), [https\://stackoverflow.com/questions/78170391/why-is-jthread-not-presented-in-libc-using-clang-18-c26](https://stackoverflow.com/questions/78170391/why-is-jthread-not-presented-in-libc-using-clang-18-c26)  
> 47. AuditTypes.cmake,   
> 48. 20\_CineRTX Phase 11.4 SuperPower 施工单\_RIFE高阶算子与SDK13.1分级实证.md,   
> 49. ngx\_d3d12\_bridge.cpp,   
> 50. rife\_builder.cpp,   
> 51. rife\_engine.cpp,   
> 52. nv\_encoder.cpp,   
> 53. ngx\_bridge\_abi.h, 