# CineRTX Phase 12 SuperPower 施工单（Wave 12.4）：全场景实机回归与 v0.4.0 封板验收

- **施工单编号**：`CINERTX-P12-WORKORDER-006`  
- **归档路径**：`docs/phase12/06_CineRTX Phase 12 SuperPower 施工单_全场景实机回归与v0.4.0封板验收.md`  
- **承接角色**：全栈集成测试、硬件实证与版本发布治理执行工程师（External Execution Engineer）  
- **签发主管**：多媒体系统工程与 GPU 异构计算首席领域专家组 / 项目总协调负责人  
- **前置基线**：Wave 12.3 封板基线（`AuditTypes` 合法具名类型 ≤ 35 个，`AuditSloc` Pure SLOC ≤ 23,000 行，无头引擎静态 IAT \= 2 项 / 混合发行版静态 IAT ≤ 4 项，`/WX` 0 警告）  
- **依据母本**：[`02_CineRTX Phase 12 极限现代 C++26 重构与无头 DLL 净空终审裁决书.md`](./02_CineRTX%20Phase%2012%20极限现代%20C++26%20重构与无头%20DLL%20净空终审裁决书.md)（`CINERTX-P12-RULING-002` §四）  
- **建议工期**：1.5–2 个有效工作日  
- **核心目标**：在物理硬件宿主机（RTX 5060 Laptop GPU \+ Driver ≥ 621.24 WHQL）的**交互式桌面会话（WTS Session \> 0）与无头服务器/无显示器模拟环境**下完成全场景端到端实机回归；同步升级版本元数据至 `0.4.0`，清理全仓构建与测试残留，汇聚五重自动化门禁与实机物证，正式签署 **`v0.4.0`** 里程碑封板。

---

## 一、 施工军规与实证诚实性铁律

1. **禁止“伪绿色单测”与数据捏造**：所有性能、显存与吞吐指标必须源于真实硬件运行日志，严禁估算、推算或复用 Phase 11 旧数据；每项实测必须附带完整测试口径（GPU 型号、驱动版本、源素材分辨率/帧率/色深、编码参数、预热帧数与统计窗口）。  
2. **禁止在无头 Session 0 下蒙混桌面验收**：DirectComposition 合成视口、HDR/SDR 跨屏切换与交互式 Resize 压测必须在活跃交互式桌面会话（`quser` 显示 `Active` 且 `SessionId > 0`）中执行；无头环境专项用于验证“2 项静态 IAT 引擎冷启动与优雅降级”。  
3. **版本控制边界守则**：所有临时测试证据（日志、输出视频）统一落盘至工作区外的指定证据目录（如 `$env:TEMP\CineRTX\evidence\phase12\`），严禁污染源码树；未获用户明确授权前严禁执行 `git commit` 或 `git push`。

---

## 二、 核心外科手术任务（Tasks W12.4-1 至 W12.4-5）

### 任务 W12.4-1：版本元数据与双语文档同步至 `v0.4.0`

**涉及文件**：`CMakeLists.txt`、`README.md`、`README_zh.md`、`webui/README.md`、`docs/archive/HISTORY_SUMMARY_P1_P11.md`

1. **升级构建版本号**：  
   - 将根目录 `CMakeLists.txt` 中的 `project(CineRTX VERSION 0.4.0 ...)` 升级至 `0.4.0`，确保 `CineRTX.exe --version` 与 `--schema-json` 准确输出 `0.4.0`。  
2. **同步项目文档与历史归档**：  
   - 更新 `README.md` 与 `README_zh.md`，如实反映 Phase 12 架构跃迁成果：C++26 现代标准库零冗余架构（具名类型 ≤ 35 个、Pure SLOC ≤ 23,000 行）、双形态极简系统 DLL 依赖（无头引擎仅 `kernel32.dll` \+ `bcrypt.dll` 2 项；桌面发行版 ≤ 4 项）、705.6 MHz Flicks 无损时基与零回读 DirectComposition 渲染管线。  
   - 将 Phase 11 过程施工单浓缩并入 `docs/archive/HISTORY_SUMMARY_P1_P11.md`，保持 `docs/` 导航清爽。

---

### 任务 W12.4-2：交互式桌面全链路实机压测（WTS Session \> 0）

**实机环境**：Windows 11 \+ NVIDIA GeForce RTX 5060 Laptop GPU（Driver ≥ 621.24 WHQL）+ WTS Active Console Session（`SessionId > 0`）

1. **4K AV1 HDR10 全链路零拷贝重制压测**：  
   - 输入 1080p SDR 素材，启用 `vsr-first`（VSR Level 4 \+ TrueHDR 1000 nits \+ RIFE `scale=0.5` A=128 \+ AV1 10-bit CQ22 Spatial AQ），实测输出 4K 60fps HDR10 容器。  
   - 核验全程预览回读带宽 **`D2H == 0 B`**、显存峰值严格受控于 7,300 MiB DXGI 预算的 82%（5,986 MiB）高水位以下、零 OOM、零帧撕裂。  
2. **DirectComposition 视口与 WASAPI 实时播放稳定性压测**：  
   - 启动 `CineRTX.exe` 连续播放 H.264 / HEVC / AV1 测试流，执行 **100 次连续剧烈窗口缩放（Resize）**、全屏切换、播放/暂停/GOP-Aware 拖拽 Seek 以及音频输出设备切换，验证 `presented > 0`、零崩溃、零 UAF、零 GDI/D3D12 资源泄漏。  
3. **NLE 多段硬切导出与零分配音频总线核验**：  
   - 执行多段 `1V+1A` 剪辑序列硬切导出，通过解复用校验导出文件总帧数与理论 705.6 MHz Flicks 换算帧数逐帧一致，切点处包含合法 IDR 帧；验证 Debug 构建下 `mixAvx2` 与流式 WSOLA 热循环堆分配次数严格为 **0**。

---

### 任务 W12.4-3：无头（Headless / 无显示器 / Session 0）环境双模降级实测

**测试环境**：脱离交互式桌面的无头命令行会话（模拟 Windows Server Core / 算力节点）

1. **冷启动与 CLI / JSON 契约验证**：  
   - 运行无头核心引擎（静态 IAT 仅 `kernel32.dll` \+ `bcrypt.dll`），执行 `--version`、`--schema-json` 及离线纯 CPU 参数校验与转码任务。  
   - 确认进程冷启动零缺失 DLL 弹窗、零错误码异常退出；当显式请求桌面专属 DirectComposition 视口或文件对话框时，按契约优雅返回 `Error::Unsupported` 并 cleanly 释放资源。  
2. **动态加载隔离审计**：  
   - 核验无头纯 CPU 批处理任务全程未加载 `user32.dll`、`gdi32.dll`、`d3d12.dll`、`dxgi.dll`、`ole32.dll`、`dwmapi.dll`、`uxtheme.dll`、`shell32.dll`、`comdlg32.dll`、`comctl32.dll`。

---

### 任务 W12.4-4：五重自动化静态与动态门禁终审扫描

**涉及脚本**：`cmake/AuditTypes.cmake`、`cmake/AuditSloc.cmake`、`cmake/AuditImports.cmake`、`cmake/AuditSpdx.cmake`、`rtx_tests.exe`

在 Clean Build 目录下依次执行并存档完整输出日志：

1. `cmake --build --preset windows-msvc-ninja`（MSVC 19.51+ `/std:c++latest /MT /WX /W4`）→ 确认 **0 错误 0 警告**；  
2. `cmake -P cmake/AuditTypes.cmake` → 确认合法具名类型总数 **≤ 35 个**，0 违规；  
3. `cmake -P cmake/AuditSloc.cmake` → 确认全仓 Pure SLOC **≤ 23,000 行**（较 33,143 基线瘦身 ≥ 30%）；  
4. `cmake -P cmake/AuditImports.cmake`（联动 `dumpbin /imports`）→ 确认无头引擎静态 IAT **\= 2 项**（`kernel32.dll`, `bcrypt.dll`），桌面发行版 `CineRTX.exe` 静态 IAT **≤ 4 项**（`kernel32.dll`, `user32.dll`, `gdi32.dll`, `bcrypt.dll`），零黑名单 DLL 泄漏；  
5. `cmake -P cmake/AuditSpdx.cmake` → 确认许可证边界 100% 合规（`src/shim/` 严格 `MIT OR Apache-2.0`，其余 `GPL-3.0-or-later WITH Plugin-Exception`）；  
6. 从项目根目录与构建目录双向运行 `rtx_tests.exe` → 确认全量单元测试 **100% PASS（0 失败，0 跳过）**。

---

### 任务 W12.4-5：编制 `v0.4.0` 终审验收报告与发布清单

**涉及文件**：`docs/phase12/07_CineRTX Phase 12 终审验收报告暨v0.4.0封板决议记录.md`

汇总上述五重门禁日志、双环境实测指标矩阵、Linker `.map` 核心非内联符号留存记录以及完整文件树校验结果，呈报首席领域专家组与用户签发 **`v0.4.0` 正式封板令**。

---

## 三、 Wave 12.4 最终封板硬门禁总表（Release Sign-Off Gates）

| 门禁编号 | 验收维度 | 硬性量化指标 | 必须提交物证 |
| :---- | :---- | :---- | :---- |
| **Gate 12.4-1** | **编译与静态卫生** | MSVC `/WX` **0 错误 0 警告**；`std::getenv` / `g_app` / 裸 GDI / `snprintf` / `#pragma comment(lib)` 全仓 **0 命中**。 | `build_w12_4.log`、`hygiene_scan.log` |
| **Gate 12.4-2** | **现代 C++26 纯度** | `AuditTypes` 合法具名类型 **≤ 35 个**；`AuditSloc` Pure SLOC **≤ 23,000 行**（净瘦身 ≥ 30%）。 | `audit_types.log`、`audit_sloc.log` |
| **Gate 12.4-3** | **系统 DLL 净空度** | 无头核心静态 IAT **\= 2 项**；桌面 `CineRTX.exe` 静态 IAT **≤ 4 项**；`AuditSpdx` **PASS**。 | `dumpbin_imports.log`、`audit_imports.log`、`audit_spdx.log` |
| **Gate 12.4-4** | **单元测试全绿** | 构建目录与项目根目录双向运行 `rtx_tests.exe` 均 **100% PASS**（0 失败，0 跳过）。 | `rtx_tests_build.log`、`rtx_tests_root.log` |
| **Gate 12.4-5** | **双环境实机闭环** | WTS Session \> 0 桌面实测 `presented > 0`、`D2H == 0 B`、100 次 Resize 零崩溃；无头会话冷启动零缺库弹窗。 | `desktop_e2e.log`、`headless_e2e.log` |

