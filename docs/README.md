# CineRTX 技术文档全景索引 (Documentation Index)

本项目的所有技术规范、架构决策、阶段报告与历史专家评审记录均归纳整理于本目录，供开发者与协作者快速检索与索引。

> **当前产品契约（2026-10-29）**：`CineRTX.exe` 是唯一目标和产物，GUI 与 CLI
> 的容器输入只有一条像素路径：NVDEC → RTX VSR + TrueHDR → 4K HDR10 HEVC
> Main10。没有透传模式、没有功能开关、没有前端差异；功能是否可用只由运行期
> NVIDIA 组件是否存在决定。Optical Flow SDK 5.0.7 与 TensorRT 10.13.3.9 由 CMake
> 在 configure 阶段按 SHA-256 钉住的归档自动供给（缓存与解压均在
> `build/external`），运行库部署到 `CineRTX.exe` 同级；TensorRT 以
> `GetProcAddress` 在运行期解析，不进入 PE 导入表。本文索引中的历史报告可能保留
> 当时的 `--remaster` 开关、`rtx-transcode*.exe` 目标或容器透传命令，那些记录仅作
> 历史证据，当前行为以根目录 `README.md`、`docs/spec/architecture.md` 与
> `include/rtx/hw_transcode.h` 为准。NvOFFRUC/RIFE 插帧执行尚未接入产品
> 主循环，详见根目录 README 的 Limitations。

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
| **07** | [`spec/testing.md`](spec/testing.md) | 自动化测试体系指南、CTest 运行指引与 137 项测试用例分布说明 | `tests`, `ctest`, `coverage`, `unit-tests` |

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

## 六、 工程交接与维护指南 (`docs/handover/`)

| 序号 | 交付文档 | 核心内容 | 索引关键字 |
| :---: | :--- | :--- | :--- |
| **40** | [`handover/交接文档.md`](handover/交接文档.md) | 跨机环境同步、开发机-测试机映射关系、环境变量配置与日常维护排障手册 | `handover`, `maintenance`, `troubleshooting` |

---

## 七、 演进规划与待办预案 (`docs/TODO/`)

本目录收录正在规划中、待评审或未来演进的架构重构规范与方案设计。

| 序号 | 待办规范 | 核心内容 | 索引关键字 |
| :---: | :--- | :--- | :--- |
| **41** | [`TODO/ui_refactor_and_plugin_status.md`](TODO/ui_refactor_and_plugin_status.md) | Win32 原生 GDI 底部状态栏设计、NGX 插件运行态健康诊断探针与模式联动交互体系 | `status-bar`, `diagnostics`, `plugin-health`, `ui-refactor` |
| **42** | [`TODO/i18n_architecture_and_localization.md`](TODO/i18n_architecture_and_localization.md) | 零依赖、零堆内存分配的 $O(1)$ 强类型枚举国际化 (i18n) 架构与中英双语自适应规范 | `i18n`, `localization`, `zero-allocation`, `bilingual` |
| **43** | [`TODO/ffmpeg7_vs_ffmpeg8_selection_and_evolution.md`](TODO/ffmpeg7_vs_ffmpeg8_selection_and_evolution.md) | FFmpeg 7.1.1 静态内联锁定依据与未来向 FFmpeg 8.x 架构演进决策分析备忘录 | `ffmpeg7-vs-ffmpeg8`, `evolution`, `decision-record`, `todo` |
| **44** | [`TODO/rife_cli_test_fixture.md`](TODO/rife_cli_test_fixture.md) | `cli_rife_command` 覆盖缺口：进程内引擎构建缺少可解析 ONNX 夹具，本轮明确接受并记录候选修复方案 | `rife`, `cli`, `ctest`, `test-gap`, `todo` |

