# CineRTX 技术文档全景索引 (Documentation Index)

本项目的所有技术规范、架构决策、阶段报告与历史专家评审记录均归纳整理于本目录，供开发者与协作者快速检索与索引。

> **当前产品契约（2026-11-10，Phase 6 权威裁决基线）**：`CineRTX.exe` 是唯一交付目标和单一双模式（Win32 原生 UI / 控制台 CLI）自包含可执行程序。
> 媒体流水线执行确定性纯 GPU 零拷贝路径：NVDEC 硬件解码 → RTX VSR + TrueHDR 设备显存重制（D3D12/CUDA 零拷贝互操作）→ P010 打包 → NVENC 默认硬件编码为 **AV1 10-bit**（Ada/Blackwell sm_89+ 默认 AV1；旧代架构自动平滑回退 HEVC Main10）。
> 彻底推翻 GDI 零子 HWND 自绘教条，换装 Win32 Common Controls v6 原生暗黑交互矩阵（13 项专业参数双向绑定，支持 Ctrl+L 原地热切中英文），拔除磁盘调试日志改用 Windows 原生调试输出。
> 全仓 100% 践行现代 C++26 绝对 RAII（零两段式 initialize/shutdown/destroy），物理隔离 PDB 符号至 symbols/ 目录并彻底切除 RIFE/FRUC/ONNX 冗余死代码，全仓代码由 4.9 万行大幅瘦身，绿色免安装发布包降至 ~34 MB。本文索引中 Phase 1 ~ Phase 5 历史报告均已打上 `[HISTORICAL-SUPERSEDED]` 归档印记，当前权威系统实现一律以根目录 `README.md`、`docs/spec/architecture.md` 与 `docs/TODO/phase6_master_backlog.md` 为准。

---

## 一、 核心工程技术规范 (`docs/spec/`)

本目录包含 CineRTX 开源工程的基础架构设计、插件 ABI 标准、开源合规边界与测试门禁体系。

| 序号 | 规范文档 | 核心内容 | 索引关键字 |
| :---: | :--- | :--- | :--- |
| **01** | [`spec/architecture.md`](spec/architecture.md) | 零拷贝流水线核心拓扑图、VRAM 显存池规划、水印背压算法与阶段契约 | `pipeline`, `vram_pool`, `zero-copy`, `DAG` |
| **02** | [`spec/abi.md`](spec/abi.md) | 跨模块纯 C 插件 ABI 标准（`rtx_shim_abi.h` / `ngx_bridge_abi.h`）与隔离规约 | `abi`, `shim`, `plugin`, `LoadLibraryExW` |
| **03** | [`spec/compliance.md`](spec/compliance.md) | GPLv3 with Plugin-Exception 双许可证物理隔离、静态依赖 0 导入合规声明 | `license`, `spdx`, `GPLv3`, `exception` |
| **04** | [`spec/gate1.md`](spec/gate1.md) | 一阶段零拷贝编解码基线（NVDEC $\to$ VRAM Pool $\to$ NVENC）；现作为历史测量记录与 `verify_gate1.ps1` 复验依据 | `gate1`, `nvdec`, `nvenc`, `throughput` |
| **05** | [`spec/gate2.md`](spec/gate2.md) | 二阶段 4K HDR10 工业级画质重制门禁标准（VSR 超分、TrueHDR 色调映射） | `gate2`, `vsr`, `truehdr`, `hdr10`, `bt2020` |
| **06** | [`spec/gate3.md`](spec/gate3.md) | 阶段三 (v0.2.0) 验收门禁标准：Gate 3.0 静态自包含与单 EXE（阻断项）、3.1 吞吐、3.2 画质、3.3 视口干扰、3.4 长稳 A/V 同步与 3.5 全栈合规 | `gate3`, `v0.2.0`, `static-self-containment`, `acceptance` |
| **07** | [`spec/testing.md`](spec/testing.md) | 自动化测试体系指南、CTest 运行指引与 79 项精炼测试用例分布说明 | `tests`, `ctest`, `coverage`, `unit-tests` |

---

## 二、 原生单 exe GUI 设计与演进 (`docs/gui/`)

本目录记录从多进程方案演进至“Win32 原生 GDI 即时态自绘单 exe”的技术方案决策与专家咨询。

| 序号 | 方案文档 | 核心内容 | 索引关键字 |
| :---: | :--- | :--- | :--- |
| **08** | [`gui/win32_gui_architecture_and_expert_consultation_dossier.md`](gui/win32_gui_architecture_and_expert_consultation_dossier.md) | 331 KB 极简单 exe 架构白皮书、无句柄 GDI 状态机自绘与环形遥测同步机制 | `win32`, `gdi`, `single-exe`, `telemetry` |
| **09** | [`gui/RTX 转码引擎 Win32 极简原生单 exe GUI 演进路线.md`](gui/RTX%20转码引擎%20Win32%20极简原生单%20exe%20GUI%20演进路线.md) | 双专家联合论证纪要：彻底放弃 WebView2/Qt 重型依赖的演进决策分析 | `evolution`, `roadmap`, `expert-review` |
| **10** | [`gui/RTX转码引擎GUI演进方案评估.md`](gui/RTX转码引擎GUI演进方案评估.md) | 8 大 GUI 技术栈深度横向对比与冷启动/内存开销基准评测报告 | `evaluation`, `benchmark`, `footprint` |

---

## 三、 一阶段基准开发与验收归档 (`docs/phase1/`)

本目录归档一阶段（NVDEC $\to$ VRAM Pool $\to$ NVENC 零拷贝转码主干）的开发方案与专家验收记录。

| 序号 | 归档文档 | 核心内容 | 索引关键字 |
| :---: | :--- | :--- | :--- |
| **11** | [`phase1/impl_plan.md`](phase1/impl_plan.md) | 一阶段全量实施计划与模块拆解原始基线 | `phase1`, `plan`, `baseline` |
| **12** | [`phase1/report.md`](phase1/report.md) | 一阶段初始执行汇报 | `phase1`, `report` |
| **13** | [`phase1/phase1_domain_expert_acceptance_report.md`](phase1/phase1_domain_expert_acceptance_report.md) | 一阶段领域专家终审验收报告（确立零拷贝基准达标） | `phase1`, `expert-acceptance` |
| **14** | [`phase1/RTX转码引擎一阶段专家验收.md`](phase1/RTX转码引擎一阶段专家验收.md) | 一阶段专家评审完整多轮对话原始问答实录 | `phase1`, `dialogue`, `transcript` |

---

## 四、 二阶段画质重制、返工与最终交付归档 (`docs/phase2/`)

本目录归档二阶段（D3D12/NGX 互操作、并行流水线、VSR/TrueHDR 集成与压测）施工、返工及全量通过报告。

| 序号 | 归档文档 | 核心内容 | 索引关键字 |
| :---: | :--- | :--- | :--- |
| **15** | [`phase2/phase2_construction_specification.md`](phase2/phase2_construction_specification.md) | 二阶段施工技术规范书 | `phase2`, `specification` |
| **16** | [`phase2/phase2_expert_consultation_dossier.md`](phase2/phase2_expert_consultation_dossier.md) | 二阶段架构评审与专家咨询档案 | `phase2`, `dossier` |
| **17** | [`phase2/phase2_expert_verdict_analysis_and_action_plan.md`](phase2/phase2_expert_verdict_analysis_and_action_plan.md) | 二阶段专家批复分析与整改行动方案 | `phase2`, `action-plan` |
| **18** | [`phase2/rework_order.md`](phase2/rework_order.md) | 二阶段返工令：W4 互操作、信号量规约与同步整改要求 | `phase2`, `rework-order` |
| **19** | [`phase2/rework_order_phase2.md`](phase2/rework_order_phase2.md) | 二阶段深化返工令细节要求 | `phase2`, `rework-details` |
| **20** | [`phase2/rework_phase2_verification.md`](phase2/rework_phase2_verification.md) | 二阶段返工复验核查报告 | `phase2`, `verification` |
| **21** | [`phase2/phase2_rework_acceptance_and_w4_milestone_report.md`](phase2/phase2_rework_acceptance_and_w4_milestone_report.md) | W4 显存互操作里程碑与返工验收报告 | `w4`, `milestone`, `interop` |
| **22** | [`phase2/phase2_w5_w6_and_gui_unified_construction_plan.md`](phase2/phase2_w5_w6_and_gui_unified_construction_plan.md) | W5/W6 专有依赖合流与 GUI 统一推进施工方案 | `w5-w6`, `unified-plan` |
| **23** | [`phase2/phase2_w5_w6_completion_report.md`](phase2/phase2_w5_w6_completion_report.md) | W5/W6 开发完成汇报（双线程生产者-消费者流水线） | `w5-w6`, `completion` |
| **24** | [`phase2/report2.md`](phase2/report2.md) | 开发机完工终审技术汇报（吞吐提升至 128.7 FPS） | `report2`, `128fps`, `performance` |
| **25** | [`phase2/phase2_w5_w6_final_acceptance_report.md`](phase2/phase2_w5_w6_final_acceptance_report.md) | W5/W6 终审全量通过验收报告（4 门禁全达标） | `phase2`, `final-acceptance` |
| **26** | [`phase2/RTX转码引擎二阶段深度验收.md`](phase2/RTX转码引擎二阶段深度验收.md) | 二阶段终审专家问答与全量代码审计评审与审计实录 | `phase2`, `audit-transcript` |
| **27** | [`phase2/CineRTX 代码库深度静态审计.md`](phase2/CineRTX%20代码库深度静态审计.md) | 开源前深度静态代码审计（发现并闭环 12 项系统级缺陷） | `audit`, `defect-analysis`, `system-hardening` |
| **28** | [`phase2/CineRTX_领域专家终审与开源就绪请求书.md`](phase2/CineRTX_领域专家终审与开源就绪请求书.md) | 领域专家终审与开源就绪呈报请求书（DOSSIER-FINAL-SIGNOFF-20261025） | `signoff-request`, `dossier`, `readiness` |
| **29** | [`phase2/CineRTX 开源终审与验收评估.md`](phase2/CineRTX%20开源终审与验收评估.md) | 领域专家委员会全票签署之开源发布令与 A 级工业级就绪裁定 | `final-verdict`, `a-grade`, `open-source-ready` |
| **30** | [`phase2/CineRTX_GitHub开源协议识别异常协助调查文档.md`](phase2/CineRTX_GitHub开源协议识别异常协助调查文档.md) | GitHub 开源许可证识别异常 (License: Other) 深度调查与专家咨询请求书 | `licensee`, `gpl-exception`, `license-other` |
| **31** | [`phase2/CineRTX 开源协议异常解决方案.md`](phase2/CineRTX%20开源协议异常解决方案.md) | 领域专家委员会全票签署之开源协议异常终审裁定与 P0 级标准化施工规程 | `license-solution`, `verbatim-gpl`, `normative-conflict` |

---

## 五、 阶段三 AI 插帧与全 GPU 非编引擎演进 (`docs/phase3/`)

本目录收录三阶段（双路智能补帧合流、奇偶乒乓并发、DirectComposition 实时非编视口与 Flicks 纳秒时基）的架构设计与专家评审档案。

| 序号 | 演进文档 | 核心内容 | 索引关键字 |
| :---: | :--- | :--- | :--- |
| **32** | [`phase3/CineRTX_阶段三架构演进与工程筹备协助调查文档.md`](phase3/CineRTX_阶段三架构演进与工程筹备协助调查文档.md) | 阶段三 (Phase 3) 双路补帧、实时非编视口、Flicks 时基与微内核合流协助调查与专家咨询请求书 | `phase3`, `fruc`, `rife`, `directcomposition`, `flicks`, `nle` |
| **33** | [`phase3/CineRTX 阶段三架构与工程方案.md`](phase3/CineRTX%20阶段三架构与工程方案.md) | 领域专家委员会全票签发之阶段三 (v0.2.0) 架构演进裁定与全栈工程施工规范书 | `verdict-phase3`, `ping-pong`, `three-tier-vram`, `gate3`, `w7-w9` |
| **34** | [`phase3/CineRTX_阶段三全栈工程施工方案.md`](phase3/CineRTX_阶段三全栈工程施工方案.md) | 阶段三 (Phase 3 / v0.2.0) 全栈工程实施方案与深度施工技术规范书（W7–W9 逐行对照施工 SOP） | `construction-spec`, `sop`, `fruc`, `rife`, `directcomposition`, `w7-w9` |
| **35** | [`phase3/CineRTX_FFmpeg全量静态链接与自包含架构协助调查文档.md`](phase3/CineRTX_FFmpeg全量静态链接与自包含架构协助调查文档.md) | 针对媒体层选配宏与动态 DLL 依赖缺陷向专家委员会提请之静态全量内联重构方案请求书 | `ffmpeg`, `static-linking`, `single-exe`, `self-contained`, `spec-hardening` |
| **36** | [`phase3/CineRTX FFmpeg全量静态链接专家方案.md`](phase3/CineRTX%20FFmpeg全量静态链接专家方案.md) | 领域专家委员会全票签发之 FFmpeg 全量静态链接与单 EXE 自包含架构重构终审裁定与全栈施工规范书 | `verdict-static-ffmpeg`, `surgical-strip`, `coff-lib`, `gate3.0`, `single-exe` |
| **37** | [`phase3/CineRTX_FFmpeg全量静态链接全栈施工方案.md`](phase3/CineRTX_FFmpeg全量静态链接全栈施工方案.md) | 阶段三前置 P0 级攻坚工序（ACT-PHASE3-PRE-STATIC-01）全栈实施方案与逐行施工 SOP（Gate 3.0 验收） | `construction-sop`, `msvc-build`, `cmake-refactor`, `memory-avio`, `audit-guard` |
| **38** | [`phase3/CineRTX_阶段三执行工程师环境搭建与实机闭环交付指令书.md`](phase3/CineRTX_阶段三执行工程师环境搭建与实机闭环交付指令书.md) | 阶段三执行工程师轻量编译环境搭建（Zig+Ninja / VS Build Tools）、实机闭环验证与最终合格补丁交付指令书 | `instruction`, `lightweight-env`, `zig-ninja`, `verification-gate`, `patch-standard` |
| **39** | [`phase3/cinertx_unified_zero_assumption_and_single_exe_construction_plan.md`](phase3/cinertx_unified_zero_assumption_and_single_exe_construction_plan.md) | 阶段三终极重构施工方案：宿主编译器优先、CMake 侧 Ninja/Zig 零假设自举、单 EXE 产物收敛与 FFmpeg / nv-codec-headers submodule 供应链净化 | `zero-assumption`, `toolchain-bootstrap`, `cmake-bootstrap`, `single-exe`, `nv-codec-headers` |

---

## 六、 阶段四 Modern C++26 全栈重构 (`docs/phase4/`)

本目录收录阶段四（C++26 全栈重构、全仓反 Slop 极简架构重塑）的核心技术架构与工程施工规范。

| 序号 | 方案与验收文档 | 核心内容 | 索引关键字 |
| :---: | :--- | :--- | :--- |
| **40** | [`phase4/CineRTX C++26 架构重构专家方案.md`](phase4/CineRTX%20C++26%20架构重构专家方案.md) | 阶段四 Modern C++26 架构升级专家技术指南与标准库演化方案 | `cpp26`, `modern-cpp`, `refactoring`, `standard-library` |
| **41** | [`phase4/CineRTX_Phase4_Modern_CPP26_全栈重构工程施工方案.md`](phase4/CineRTX_Phase4_Modern_CPP26_全栈重构工程施工方案.md) | 阶段四 C++26 全栈重构实施 SOP 与逐行施工规范 | `phase4`, `construction-sop`, `modernization` |
| **42** | [`phase4/CineRTX_全仓反Slop与极简架构重塑工程施工方案.md`](phase4/CineRTX_全仓反Slop与极简架构重塑工程施工方案.md) | 全仓反 Slop 治理、死代码切除与极简架构重塑工程施工指南 (PLAN-LEAN-ANTI-SLOP-20261101) | `anti-slop-plan`, `dead-code-elimination`, `occam-razor` |

---

## 七、 工程交接与维护指南 (`docs/handover/`)

| 序号 | 交付文档 | 核心内容 | 索引关键字 |
| :---: | :--- | :--- | :--- |
| **43** | [`handover/交接文档.md`](handover/交接文档.md) | 跨机环境同步、开发机-测试机映射关系、环境变量配置与日常维护排障手册 | `handover`, `maintenance`, `troubleshooting` |

---

## 八、 演进规划与待办预案 (`docs/TODO/`)

本目录收录正在规划中、待评审或未来演进的架构重构规范与方案设计。

| 序号 | 待办规范 | 核心内容 | 索引关键字 |
| :---: | :--- | :--- | :--- |
| **44** | [`TODO/ui_refactor_and_plugin_status.md`](TODO/ui_refactor_and_plugin_status.md) | Win32 原生 GDI 底部状态栏设计、NGX 插件运行态健康诊断探针与模式联动交互体系 | `status-bar`, `diagnostics`, `plugin-health`, `ui-refactor` |
| **45** | [`TODO/i18n_architecture_and_localization.md`](TODO/i18n_architecture_and_localization.md) | 零依赖、零堆内存分配的 $O(1)$ 强类型枚举国际化 (i18n) 架构与中英双语自适应规范 | `i18n`, `localization`, `zero-allocation`, `bilingual` |
| **46** | [`TODO/ffmpeg7_vs_ffmpeg8_selection_and_evolution.md`](TODO/ffmpeg7_vs_ffmpeg8_selection_and_evolution.md) | FFmpeg 7.1.1 静态内联锁定依据与未来向 FFmpeg 8.x 架构演进决策分析备忘录 | `ffmpeg7-vs-ffmpeg8`, `evolution`, `decision-record`, `todo` |
| **47** | [`TODO/rife_cli_test_fixture.md`](TODO/rife_cli_test_fixture.md) | `cli_rife_command` 覆盖缺口：进程内引擎构建缺少可解析 ONNX 夹具，本轮明确接受并记录候选修复方案 | `rife`, `cli`, `ctest`, `test-gap`, `todo` |
| **48** | [`TODO/gui_debug_log_elimination_and_debugview.md`](TODO/gui_debug_log_elimination_and_debugview.md) | 拔除 `RTX_GUI_RESULT_LOG` 环境变量，废除磁盘文件落盘，全面改用 Windows 原生 `OutputDebugString` 与 DebugView 实时监听流 | `debugview`, `occam-razor`, `outputdebugstring`, `todo` |
| **49** | [`TODO/packaging_and_distribution_closure.md`](TODO/packaging_and_distribution_closure.md) | 开源发布归档分发与 NVIDIA 专有运行时再分发权合规闭环方案 (TODO-PKG-DIST-20261102) | `packaging`, `distribution`, `redistributable`, `gpl-compliance` |
| **50** | [`TODO/cli_gui_synchronization_av1_and_pro_parameters.md`](TODO/cli_gui_synchronization_av1_and_pro_parameters.md) | 双端 SSOT 配置引擎（根除双端参数不同步隐患）、默认编码升级为 AV1 10-bit、UI 工业级专业调参矩阵面板 | `ssot`, `dual-frontend-sync`, `av1-default`, `pro-parameters`, `tuning-inspector` |

---

## 九、 阶段五 双平面微内核与 Blackwell 极限特化 (`docs/phase5/`)

本目录收录阶段五（双平面异步微内核 DAG 调度器、Blackwell sm_120 极限硬件特化、AV1 编码器升级及画质修复）的开工预研、专家委托与技术裁决档案。

| 序号 | 规范与调研文档 | 核心内容 | 索引关键字 |
| :---: | :--- | :--- | :--- |
| **51** | [`phase5/CineRTX_Phase5_开工预研与关键问题专家深度调研委托书.md`](phase5/CineRTX_Phase5_开工预研与关键问题专家深度调研委托书.md) | 阶段五开工预研、画质色偏排查、竖屏自适应、AV1 默认选型与文档审计专家深度调研委托书 (DOC-COMMISSION-PHASE5-PRERESEARCH-001) | `phase5-pre-research`, `color-inversion-fix`, `vertical-video`, `av1-default`, `i18n` |
| **52** | [`phase5/CineRTX 阶段五预研专家调研.md`](phase5/CineRTX%20阶段五预研专家调研.md) | 阶段五开工预研与八大关键工程问题领域专家深度调研及技术裁决报告 (VERDICT-COMMISSION-PHASE5-PRERESEARCH-001，全票审定批准八大决议案并下达强制加固指令) | `verdict`, `phase5-pre-research`, `color-hardening`, `black-level`, `smart-geometry`, `av1-default` |
| **53** | [`phase5/CineRTX_Phase5_全媒体可用性与工程化落地施工方案.md`](phase5/CineRTX_Phase5_全媒体可用性与工程化落地施工方案.md) | 阶段五全媒体工业可用性施工方案（PLAN-PHASE5-MEDIA-USABILITY-20261108，音频透传、9:16竖屏自适应、SDR直通与发布闭环） | `phase5-plan`, `audio-mux`, `smart-geometry`, `sdr-passthrough`, `packaging` |
| **54** | [`phase5/CineRTX_Phase5_全媒体可用性与工程化落地实施总结报告.md`](phase5/CineRTX_Phase5_全媒体可用性与工程化落地实施总结报告.md) | 阶段五工程实施终期总结报告（Wave 1~4 全波次交付验收、音画合流全保真、竖屏4K直出、93项单测全绿及绿色发布包闭环） | `phase5-report`, `acceptance`, `audio-verified`, `portrait-4k`, `release-closure` |

---

## 十、 阶段六 架构全面瘦身与专业级产品化 (`docs/phase6/`)

本目录收录阶段六（架构全面瘦身、现代 C++26 纯化、Win32 原生交互控件、AV1 默认及 5 万行代码大刀阔斧精简）的专家委托与核心设计档案。

| 序号 | 规范与调研文档 | 核心内容 | 索引关键字 |
| :---: | :--- | :--- | :--- |
| **55** | [`phase6/CineRTX_Phase6_架构全面瘦身与专业级产品化专家调研委托书.md`](phase6/CineRTX_Phase6_架构全面瘦身与专业级产品化专家调研委托书.md) | 阶段六架构全面瘦身、现代 C++26 纯化与专业级产品化领域专家深度调研委托书（十三大关键议题：原生UI组件、调参矩阵、AV1默认、绝对RAII、types.cpp重构、MSVC向量化实证、拔除冗余削减5万行代码） | `phase6-commission`, `native-ui`, `av1-default`, `designated-initializers`, `strict-raii`, `anti-slop`, `codebase-slimming` |
| **56** | [`phase6/CineRTX_Phase6_架构全面瘦身与专业级产品化专家裁决与技术方案.md`](phase6/CineRTX_Phase6_架构全面瘦身与专业级产品化专家裁决与技术方案.md) | 阶段六十三大核心议题专家终审裁决与一体化工程施工方案（DOC-VERDICT-PHASE6-REFACTOR-001，废除手绘教条、Win32原生标准控件、AV1 10-bit默认、100% RAII重塑、MSVC AVX2实证、外科手术式砍削3.4万行代码） | `phase6-verdict`, `native-controls`, `av1-unlocked`, `strict-raii`, `avx2-ymm`, `codebase-slash` |
| **57** | [`phase6/CineRTX_Phase6_专家取证与交付审查报告.md`](phase6/CineRTX_Phase6_专家取证与交付审查报告.md) | 阶段六全仓源码与55篇历史文档深度取证执行摘要报告（UI停滞根因、AV1五处拦截点、构建文件污染、六大历史文档矛盾及5万行代码膨胀真相） | `phase6-forensics`, `doc-audit`, `slop-diagnosis`, `codebase-truth` |
| **58** | [`phase6/CineRTX_Phase6_第一阶段构建治理与死代码切除施工方案.md`](phase6/CineRTX_Phase6_第一阶段构建治理与死代码切除施工方案.md) | 阶段六第一波次施工实施方案（PLAN-PHASE6-WAVE1-CLEANSE-20261110，阻断增量链接、符号隔离、删除源码树内嵌PTX、切除RIFE/FRUC/DComp死代码、净砍3万行代码） | `phase6-wave1`, `cmake-hygiene`, `ptx-dynamic-gen`, `dead-code-removal`, `slimming-plan` |
| **59** | [`phase6/CineRTX_阶段六收官与系统整体验收及未来演进专家委托书.md`](phase6/CineRTX_阶段六收官与系统整体验收及未来演进专家委托书.md) | 阶段六收官与系统整体验收及未来演进专家委托书（Phase 1～6 实测验收、四大关键问卷：主线选型、实时视口、专业色彩、多GPU与跨平台路线） | `phase6-closure`, `acceptance`, `roadmap-commission` |

---

## 十一、 阶段七 参数闭环、实时监看、色彩科学与云原生演进 (`docs/phase7/`)

本目录收录阶段七（Wave 1~4 全波次：参数闭环微瑕清零、D3D12 FLIP 原生监看视口、融合 3D LUT / HDR10+ 色彩科学、多 GPU 拓扑调度与 Linux 纯 CUDA 无头服务）的专家裁决、施工方案与实证报告。

| 序号 | 规范与调研文档 | 核心内容 | 索引关键字 |
| :---: | :--- | :--- | :--- |
| **60** | [`phase7/CineRTX_Phase7_专家裁决与战略演进报告.md`](phase7/CineRTX_Phase7_专家裁决与战略演进报告.md) | 阶段六收官终审验收、深层瓶颈剖析与 Phase 7–9 战略演进专家裁决报告（全票批准 Phase 6 收官，确立数据面定型优先法则，拆解 Wave 1～4 演进蓝图） | `phase7-verdict`, `roadmap-blueprint`, `viewport-d3d12`, `lut3d-fused`, `multi-gpu`, `linux-headless` |
| **61** | [`phase7/CineRTX_Phase7_Wave1_参数闭环与微瑕清零施工方案.md`](phase7/CineRTX_Phase7_Wave1_参数闭环与微瑕清零施工方案.md) | Wave 1 参数闭环与微瑕清零施工方案（GUI 峰值亮度与画幅绑定、音频直通拦截、VSR 启停透传、聚合体安全与死代码清理） | `phase7-wave1`, `param-closure`, `audio-passthrough`, `vsr-passthrough` |
| **62** | [`phase7/CineRTX_Phase7_Wave2_实时监看与色彩科学施工方案.md`](phase7/CineRTX_Phase7_Wave2_实时监看与色彩科学施工方案.md) | Wave 2 实时监看视口与色彩科学施工方案（独立子 HWND D3D12 FLIP 交换链、零额外带宽融合核函数、3D LUT FP16 纹理采样、HDR10+ 动态元数据） | `phase7-wave2`, `realtime-viewport`, `d3d12-flip`, `fused-pack`, `lut3d`, `hdr10plus` |
| **63** | [`phase7/CineRTX_Phase7_Wave3_多GPU分片调度与长稳施工方案.md`](phase7/CineRTX_Phase7_Wave3_多GPU分片调度与长稳施工方案.md) | Wave 3 多 GPU 拓扑分片调度与长稳鲁棒性施工方案（拓扑探测与设备绑定、GOP 智能切片、动态软解兜底、TDR 注入与弹性恢复） | `phase7-wave3`, `multi-gpu`, `topology`, `gop-split`, `anti-tdr`, `fuzz-resilience` |
| **64** | [`phase7/CineRTX_Phase7_Wave4_Linux纯CUDA与云原生施工方案.md`](phase7/CineRTX_Phase7_Wave4_Linux纯CUDA与云原生施工方案.md) | Wave 4 Linux 纯 CUDA 与云原生施工方案（跨平台 ABI 统一、cuLaunchKernel 动态运行时、Lanczos-3 / CAS 算子后端、无头服务化） | `phase7-wave4`, `linux-cuda`, `headless-engine`, `cloud-native`, `dyn-cuda` |
| **65** | [`phase7/CineRTX_Phase7_验收实证报告.md`](phase7/CineRTX_Phase7_验收实证报告.md) | Phase 7 Wave 1～4 全要素验收实证报告（静态检查、编译门禁、单测全绿、实机 4K HDR10 转码验证与全链路证据） | `phase7-acceptance`, `full-verification`, `evidence` |

---

## 十二、 阶段八 架构深度取证、开箱即用 60 帧与极致瘦身 (`docs/phase8/`)

本目录收录阶段八（全仓架构代码级取证、开箱即用 60 帧重构、零开关极致纯化、双路补帧演进与全栈大刀阔斧瘦身）的深度调研、实施方案与验收实证报告。

| 序号 | 规范与调研文档 | 核心内容 | 索引关键字 |
| :---: | :--- | :--- | :--- |
| **66** | [`phase8/CineRTX_Phase8_关于7个核心问题全仓代码级取证与架构推演.md`](phase8/CineRTX_Phase8_关于7个核心问题全仓代码级取证与架构推演.md) | 关于 7 个核心问题全仓代码级取证与架构推演（控制台日志、--sdr暗坑扫描、补帧现状、双轨UI流水线、ONNX残留、纯GPU播放器/NLE推演、16个DLL极致削减） | `phase8-forensics`, `zero-switch`, `debugview`, `fruc`, `nle-architecture`, `dll-slimming` |
| **67** | [`phase8/CineRTX_Phase8_双路补帧架构工程线索与实机环境证据报告.md`](phase8/CineRTX_Phase8_双路补帧架构工程线索与实机环境证据报告.md) | 双路补帧（NVOFA / RIFE-TensorRT）工程线索与实机环境证据报告（专供大模型高效实操：驱动级光流证据、离线SDK路径、commit:46dbaac历史纯C ABI与核函数秒级复用索引、主流水线1:N改造蓝图） | `phase8-clues`, `nvofa`, `rife-tensorrt`, `fruc-abi`, `hook-points`, `llm-guide` |
| **68** | [`phase8/CineRTX_Phase8_双路补帧架构设计.md`](phase8/CineRTX_Phase8_双路补帧架构设计.md) | 双路补帧（FRUC）顶层架构设计推演方案（纯C ABI隔离、NVDEC与NGX间嵌入拓扑、Flicks时钟驱动滑动窗口、Surface Lease显存租约、4×4网格双线性插值、场景硬切三级保护、Wave 1～4路线图） | `phase8-arch`, `fruc-architecture`, `sliding-window`, `surface-lease`, `nvofa-warp`, `rife-trt`, `abi-firewall` |
| **69** | [`phase8/CineRTX_Phase8_参考资料.md`](phase8/CineRTX_Phase8_参考资料.md) | Phase 8 官方参考资料与接口规范速查（Optical Flow SDK 5.0.7 原生硬件光流与插帧规范、参数契约与最佳实践） | `phase8-refs`, `nvof-sdk`, `specs`, `guidelines` |
| **70** | [`phase8/CineRTX_Phase8_最新代码深度验收报告暨全栈重构方案.md`](phase8/CineRTX_Phase8_最新代码深度验收报告暨全栈重构方案.md) | 最新代码深度验收报告暨“开箱即用 60 帧 + 大刀阔斧极简瘦身”全栈重构方案（四道阻断根因定性、假插帧欺骗性交付溯源、15个死代码文件拔除、三桥归一为 rtx_ngx_bridge、单文件<=1300行硬指标） | `phase8-audit`, `zero-config-60fps`, `radical-slimming`, `single-bridge`, `kiss` |
| **71** | [`phase8/CineRTX_Phase8_返工施工任务书.md`](phase8/CineRTX_Phase8_返工施工任务书.md) | Phase 8 返工施工任务书（Track A～D 施工工单拆解、黄金默认值 SSOT 闭环、驱动级真光流内聚、4 大硬核红线门禁契约） | `phase8-rework-order`, `tasks`, `gates`, `nvof-warp` |
| **72** | [`phase8/CineRTX_Phase8_返工交付验收实证报告.md`](phase8/CineRTX_Phase8_返工交付验收实证报告.md) | Phase 8 返工交付验收实证报告（四大门禁 100% 全绿、零参数 60 帧端到端真机转码实录、前 10 帧非零 SAD 真插帧像素级验证、PE 纯净度 Gate 4 通过） | `phase8-acceptance`, `gates-pass`, `sad-verified`, `60fps-golden` |




