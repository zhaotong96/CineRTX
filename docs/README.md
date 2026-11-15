# CineRTX 技术文档全景索引 (Documentation Index)

本目录收录 CineRTX 的权威技术规范、阶段定稿文档与演进归档，供开发者与协作者快速检索。全部历史阶段过程单（Phase 1–7）已提炼入单一演进归档，本索引为全仓 `docs/` 的唯一有效目录。

> **当前产品契约（2026-11-15 Phase 9.1 全栈收官封板基线）**：`CineRTX.exe` 是唯一交付目标与单一双模式（Win32 原生 UI / 控制台 CLI）自包含可执行程序。
> 媒体流水线执行确定性纯 GPU 零拷贝路径：NVDEC 硬件解码 → 双路真补帧（路线 A：驱动原生 NVOFA 硬件光流；路线 B：TensorRT 10.13.3.9 + RIFE v4.25 深度学习推理，共存于唯一桥接库 `rtx_ngx_bridge.dll`）→ RTX VSR + TrueHDR 设备显存重制（D3D12/CUDA 零拷贝互操作）→ P010 打包 → NVENC 默认硬件编码为 **AV1 10-bit**（Ada/Blackwell sm_89+ 默认 AV1；旧代架构自动平滑回退 HEVC Main10）。
> 三端同构工作站：Win32 原生 "3+1" 四态专业工作站（`[1] Encoder` 转码矩阵 / `[2] Player` 播放审片 / `[3] NLE` 四轨 `705,600,000 Hz Flicks` 时间轴 / `+1` 文件关联双击无边框全屏动态 VSR 超分快播）、WebUI 浏览器三态工作站（`python webui/server.py --port 8787`）与 CLI，由唯一的 `kParamSchema` 19 项 SSOT 参数契约（`include/rtx/param_schema.h`）与 113 词条中英双语词典（`include/rtx/i18n.h`）驱动，`Ctrl+L` 原地热切。
> 全仓 100% 践行现代 C++26 绝对 RAII（零两段式 initialize/shutdown/destroy），全引擎日志经 `OutputDebugStringW` 以 `[CineRTX]` 前缀实时流出（Sysinternals DebugView 零依赖监看），单 EXE 静态内联手术刀级 FFmpeg，绿色免安装发布包 ~34 MB，`75 tests` 全绿。历史演进脉络见 [`docs/archive/Phase1_to_Phase7_Evolution_Digest.md`](archive/Phase1_to_Phase7_Evolution_Digest.md)。

---

## 一、 核心工程技术规范 (`docs/spec/`，7 篇权威规范)

| 序号 | 规范文档 | 核心内容 |
| :---: | :--- | :--- |
| **01** | [`spec/architecture.md`](spec/architecture.md) | 零拷贝流水线拓扑、双路真补帧（NVOFA + TensorRT RIFE）、三槽 P010 环形显存、三端 SSOT 架构与失败模型 |
| **02** | [`spec/abi.md`](spec/abi.md) | 跨模块纯 C 插件 ABI 标准：`rtx_shim_abi.h`（v2）与 `ngx_bridge_abi.h`（v4，含双路补帧导出）与隔离规约 |
| **03** | [`spec/compliance.md`](spec/compliance.md) | GPLv3 with Plugin-Exception 双许可证物理隔离、FFmpeg LGPLv2.1+ 静态内联法务闭环、静态依赖 0 导入合规 |
| **04** | [`spec/gate1.md`](spec/gate1.md) | 零拷贝编解码基线门禁记录（NVDEC → 显存池 → NVENC；D2H/H2D = 0 实测与复验程序） |
| **05** | [`spec/gate2.md`](spec/gate2.md) | 4K HDR10 工业级重制门禁记录（D3D12 互操作、VSR 超分、TrueHDR 色调映射、128.7 FPS 吞吐与 174 分钟满载 soak） |
| **06** | [`spec/gate3.md`](spec/gate3.md) | 发布验收门禁标准：静态自包含（阻断项）、吞吐、画质、工作站交互、长稳 A/V 同步与全栈合规 |
| **07** | [`spec/testing.md`](spec/testing.md) | 自动化测试体系指南：CTest 运行指引、75 项单测分布、桌面交互门禁（29 项断言）与验收主机矩阵 |

## 二、 Phase 8 权威定稿文档 (`docs/phase8/`，13 篇核心定稿)

| 序号 | 定稿文档 | 核心内容 |
| :---: | :--- | :--- |
| **08** | [`phase8/CineRTX 工程施工规范（第一阶段）：Phase 8 双路真补帧（NVOFA + TensorRT RIFE）落地与桥接层 CUDA 上下文归一.md`](phase8/CineRTX%20工程施工规范（第一阶段）：Phase%208%20双路真补帧（NVOFA%20%2B%20TensorRT%20RIFE）落地与桥接层%20CUDA%20上下文归一.md) | 双路真补帧落地施工规范与桥接层 CUDA 上下文归一 |
| **09** | [`phase8/CineRTX 工程施工规范（第二阶段）：Win32 GUI + CLI + WebUI 三端统一 SSOT 架构与原生接入.md`](phase8/CineRTX%20工程施工规范（第二阶段）：Win32%20GUI%20%2B%20CLI%20%2B%20WebUI%20三端统一%20SSOT%20架构与原生接入.md) | 三端统一 SSOT 架构与原生接入施工规范 |
| **10** | [`phase8/CineRTX 工程施工规范（第三阶段）：全仓文档漂移清零、历史阶段单文档提炼与 CI 契约闭环.md`](phase8/CineRTX%20工程施工规范（第三阶段）：全仓文档漂移清零、历史阶段单文档提炼与%20CI%20契约闭环.md) | 阶段三（收官合拢）施工总规范 |
| **11** | [`phase8/CineRTX_Phase8_TensorRT_RIFE补帧施工方案.md`](phase8/CineRTX_Phase8_TensorRT_RIFE补帧施工方案.md) | 路线 B（TensorRT 10.13.3.9 + RIFE v4.25）深度学习补帧施工文档 |
| **12** | [`phase8/CineRTX Phase 8：TensorRT + RIFE 深度学习补帧外部技术情报与执行参考手册.md`](phase8/CineRTX%20Phase%208：TensorRT%20%2B%20RIFE%20深度学习补帧外部技术情报与执行参考手册.md) | TensorRT + RIFE 外部技术情报与执行参考 |
| **13** | [`phase8/CineRTX_Phase8_参考资料.md`](phase8/CineRTX_Phase8_参考资料.md) | Optical Flow SDK 5.0.7 原生硬件光流与插帧规范速查 |
| **14** | [`phase8/CineRTX Phase 8 全量穿透验收、三端统一架构重塑与全仓文档漂移终审调研报告.md`](phase8/CineRTX%20Phase%208%20全量穿透验收、三端统一架构重塑与全仓文档漂移终审调研报告.md) | Phase 8 全量穿透终审调研报告（29 项漂移终审底稿） |
| **15** | [`phase8/CineRTX WebUI 阶段二首轮验收与第二轮三态工作站（ME转码_VLC播放_PR非线编 + NVIDIA绿）施工规范.md`](phase8/CineRTX%20WebUI%20阶段二首轮验收与第二轮三态工作站（ME转码_VLC播放_PR非线编%20%2B%20NVIDIA绿）施工规范.md) | WebUI 三态工作站施工规范 |
| **16** | [`phase8/CineRTX Win32 Native UI 阶段二施工规范：对齐 WebUI 三态工作站（ME转码_VLC播放_PR非线编 + NVIDIA绿）与 C++26 SSOT 契约.md`](phase8/CineRTX%20Win32%20Native%20UI%20阶段二施工规范：对齐%20WebUI%20三态工作站（ME转码_VLC播放_PR非线编%20%2B%20NVIDIA绿）与%20C%2B%2B26%20SSOT%20契约.md) | Win32 原生工作站施工规范（对齐 WebUI 三态与 C++26 SSOT） |
| **17** | [`phase8/CineRTX Win32 Native UI 阶段二返工验收报告（Track B1-B4）.md`](phase8/CineRTX%20Win32%20Native%20UI%20阶段二返工验收报告（Track%20B1-B4）.md) | Win32 阶段二返工（RW-1~6 / Track B1-B4）验收报告与实证 |
| **18** | [`phase8/CineRTX 阶段二（Track B）返工终审呈报与专家验收委托书.md`](phase8/CineRTX%20阶段二（Track%20B）返工终审呈报与专家验收委托书.md) | 阶段二返工终审呈报与专家验收委托 |
| **19** | [`phase8/CineRTX 阶段二终审通过暨批准阶段3施工指令书.md`](phase8/CineRTX%20阶段二终审通过暨批准阶段3施工指令书.md) | 阶段二终审通过与阶段三施工批准指令（阶段三施工单的总依据） |
| **20** | [`phase8/CineRTX 阶段三（收官合拢）终审验收总报告与专家呈报书.md`](phase8/CineRTX%20阶段三（收官合拢）终审验收总报告与专家呈报书.md) | 阶段三收官合拢终审验收总报告（归并 Track 3A/3B/3C/3D 九份过程单；门禁 3.1~3.6 全绿实证与偏差记录） |

## 三、 Phase 9 / 9.1 权威定稿与封板发布文档 (`docs/phase9/`，11 篇定稿、施工单、裁决书与发布指引)

| 序号 | 定稿文档 | 核心内容 |
| :---: | :--- | :--- |
| **21** | [`phase9/CineRTX_Phase9_终审裁决与全链路真权重闭环SuperPower返工规范.md`](phase9/CineRTX_Phase9_终审裁决与全链路真权重闭环SuperPower返工规范.md) | 领域专家组针对伪 RIFE / NVOFA 偷梁换柱裁定驳回（RW-P9-01~05）与 SuperPower 返工总纲 |
| **22** | [`phase9/CineRTX_Phase9.1_SuperPower返工施工分发总览.md`](phase9/CineRTX_Phase9.1_SuperPower返工施工分发总览.md) | Phase 9.1 四大返工轨道任务矩阵、协同流程与终审复验 6 大门禁指南 |
| **23** | [`phase9/CineRTX_Phase9.1_施工单_Track_9.1A_真权重与纯净RIFE落地.md`](phase9/CineRTX_Phase9.1_施工单_Track_9.1A_真权重与纯净RIFE落地.md) | 拔除 NVOFA 覆写、自动化下载官方 RIFE v4.25 ONNX 并构建 $\ge 5\text{ MiB}$ 真引擎 |
| **24** | [`phase9/CineRTX_Phase9.1_施工单_Track_9.1B_FP16引擎链路与后处理闭环.md`](phase9/CineRTX_Phase9.1_施工单_Track_9.1B_FP16引擎链路与后处理闭环.md) | 修复 `kHALF` 帧对缓存时间步更新、推理后半精度转单精度及 Full-Range 色阶闭环 |
| **25** | [`phase9/CineRTX_Phase9.1_施工单_Track_9.1C_调度防线与单测加固.md`](phase9/CineRTX_Phase9.1_施工单_Track_9.1C_调度防线与单测加固.md) | 消除静态计数器跨会话死锁、补齐插帧全败强熔断检查并加固 C ABI 单测 |
| **26** | [`phase9/CineRTX_Phase9.1_施工单_Track_9.1D_合规归位与端到端实测闭环.md`](phase9/CineRTX_Phase9.1_施工单_Track_9.1D_合规归位与端到端实测闭环.md) | 全仓文档脱敏与归位、零环境变量实机 120fps E2E 转码与连续帧 MAE 互异性复验 |
| **27** | [`phase9/CineRTX_Phase9_终审验收报告_RIFE120fps全链路根治.md`](phase9/CineRTX_Phase9_终审验收报告_RIFE120fps全链路根治.md) | Phase 9 历史验收报告与实机拦截隐蔽缺陷（64位原子加对撞）记录（已脱敏存查） |
| **28** | [`phase9/CineRTX_Phase9.1_终审复验报告_真权重RIFE120fps全链路根治.md`](phase9/CineRTX_Phase9.1_终审复验报告_真权重RIFE120fps全链路根治.md) | Phase 9.1 终审复验：RW-P9-01~05 修复落点、真实 v4.25 权重引擎部署、Gate 9.1~9.6 逐项物证 |
| **29** | [`phase9/CineRTX_Phase9.1_专家终审验收与战略封板委托书.md`](phase9/CineRTX_Phase9.1_专家终审验收与战略封板委托书.md) | Phase 9.1 呈报专家组签署战略封板的委托书（引用复验报告与全部证据） |
| **30** | [`phase9/CineRTX_Phase9.1_终审通过暨全栈收官战略封板裁决书.md`](phase9/CineRTX_Phase9.1_终审通过暨全栈收官战略封板裁决书.md) | 领域专家组正式签署通过 Phase 9.1 终审验收并批准 CineRTX 全栈收官战略封板裁决书 |
| **31** | [`phase9/CineRTX_Phase9.1_开源推送前终检修正与GitHub发布执行指引.md`](phase9/CineRTX_Phase9.1_开源推送前终检修正与GitHub发布执行指引.md) | 开源推送前 CI 阻断项清理实录、云端 TensorRT 头文件供给、Git 提交与 GitHub Release 执行指引 |
| **32** | [`phase9/CineRTX_Phase9.1_推送前仓库卫生完善与终审封板专家委托书.md`](phase9/CineRTX_Phase9.1_推送前仓库卫生完善与终审封板专家委托书.md) | 用户发版前审查提出的四大诉求专项委托（Optical Flow 驱动确证、tools 脚本绝灭与 CMake 替代、tests 瘦身与最终发版大典） |
| **33** | [`phase9/CineRTX_Phase9.1_推送前仓库卫生完善与CMake全原生闭环专家裁决暨SuperPower施工规范.md`](phase9/CineRTX_Phase9.1_推送前仓库卫生完善与CMake全原生闭环专家裁决暨SuperPower施工规范.md) | **CMake 全原生闭环裁决规范 (006)**：消灭 `tools/` 脚本、收敛 `tests/` 10 套 C++26 套件与零 Python 闭环施工规范。 |
| **34** | [`phase9/CineRTX_Phase9.1_专家直通代建完成暨Git索引同步与封板推送指引.md`](phase9/CineRTX_Phase9.1_专家直通代建完成暨Git索引同步与封板推送指引.md) | **专家直通代建指引 (006B)**：专家直通代建落地清单、`Optical_Flow_SDK_5.0.7` 深度解析与 Git 索引同步指引。 |
| **35** | [`phase9/CineRTX_Phase9.1_全栈正式发布与开源交付大典.md`](phase9/CineRTX_Phase9.1_全栈正式发布与开源交付大典.md) | **正式发布与开源交付大典 (007)**：Phase 9.1 终审封板大典、全链路验收矩阵与 `v0.1.0` 正式开源交付宣告。 |

## 四、 演进归档与待办台账

| 序号 | 文档 | 核心内容 |
| :---: | :--- | :--- |
| **36** | [`archive/Phase1_to_Phase7_Evolution_Digest.md`](archive/Phase1_to_Phase7_Evolution_Digest.md) | Phase 1–7 单一权威演进归档：各阶段目标/存活遗产/废除设计黑名单/硬件踩坑全记录（替代原 51 篇阶段文档与 11 篇 TODO 分册） |
| **37** | [`TODO/README.md`](TODO/README.md) | 历史待办全量闭环台账与 v0.3+ 远期展望（奥卡姆剃刀军规） |
| **38** | [`../webui/README.md`](../webui/README.md) | 浏览器端 WebUI（1 html + 1 js + 1 css + 1 json 状态持久化层，前后端仅以 JSON 通信 HTTP+SSE；`server.py` 为零依赖 JSON 桥） |

---

> **归档说明**：Phase 1–7 全部阶段过程文档、交接文档、GUI 演进档案与 `docs/TODO/` 历史分册已于 2026-11-14 阶段三收官时提炼入本索引第 36 项的演进归档后物理清理；Phase 9.1 全项终审复验报告、呈报委托书、专家战略封板裁决书、GitHub 开源发布指引及第 33～35 项 CMake 全原生闭环施工规范与发布大典已完备归档入本索引第 28～35 项。全仓单测基线确立为 74 项。
