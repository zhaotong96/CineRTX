# CineRTX Phase 12 全量文档与代码隐私脱敏暨 GPL 合规治理施工单（净版）

**工程协调推进组 · 受控任务派发工件**

---

## 基础元数据

| 属性项 | 内容详细 |
| :--- | :--- |
| **施工单编号** | `CINERTX-P12-WORKORDER-073`（已依 `CINERTX-P12-FINAL-PURITY-077` 原位净化） |
| **归档路径** | `docs/phase12/73_CineRTX Phase 12 全量文档与代码隐私脱敏暨GPL合规治理施工单.md` |
| **派发主体** | 工程协调推进组 (Engineering Coordination Group) |
| **受令主体** | 外部执行工程师 (Execution Engineer) |
| **派发依据** | 项目负责人在 Windows 桌面实测通过下拉列表修复后签发的隐私脱敏与 GPLv3 合规治理指令 |
| **前序依据** | `71_CineRTX Phase 12 UI下拉列表展开失效法医级根因裁决书暨修复施工令.md`、`72_CineRTX Phase 12 UI下拉列表展开失效修复施工汇报.md`、本地提交 `02d8322` |
| **治理范围** | 全仓文档族（`docs/`、`README.md`、`README_zh.md`）、构建与门禁脚本（`cmake/`）、源码及头文件（`src/`、`include/`、`tests/`） |
| **日期** | 2026-11-24 |

---

## 一、 任务背景与核心目标

在真机实测 ComboBox 修复通过、本地提交 `02d8322` 封板后，Phase 12 正式进入 **`v0.4.0` 发版前最终合规净化阶段**。要求对全仓实施双轨净化：

1. **隐私与宿主路径 100% 深度脱敏**：消除全仓任何位置遗留的本地盘符用户目录绝对路径、个人账户名、私有云盘标识及内网私有 IP；
2. **开源许可与 GPLv3 合规 100% 审计闭环**：消除任何许可头缺失、专有依赖混淆与合规盲区，确保交付工件为纯粹开源软件。

---

## 二、 施工任务详细规范

### 任务一（Track A）：全仓隐私信息与宿主绝对路径深度脱敏

#### 1.1 明确清洗与替换标的

| 敏感特征项 | 抽象违规形态 | 标准化脱敏替换规范 |
| :--- | :--- | :--- |
| **本地盘符用户目录** | `[REDACTED_PATH]:/Users/<user>/...` | 规整为相对路径 `./...`、占位符 `[REDACTED_PATH]/...` 或 `[REDACTED_TEMP_DIR]/CineRTX/...` |
| **私有云盘目录名与分享链接** | 个人云盘同步目录标识及外部云盘分享 URL | 统一清理为 `workspace` 或 `[internal-reference]` |
| **个人用户账户名** | 路径或文本中的真实个人账户标识 | 替换为通用占位符 `[REDACTED_PATH]` / `<dev>` |
| **内网私有地址** | RFC1918 私网 IPv4 段（精确豁免软件版本号 `10.13.3.9`） | 统一替换为标准回环 `127.0.0.1` 或 `localhost` |
| **本地文件协议链接** | 指向本地磁盘的 `file://` URI | 规整为相对链接 `./...` 或 `../../...` |

#### 1.2 固化纯 CMake 原生隐私门禁
- 依据 `[DEC-002]` 资产物理隔离铁律，全仓门禁一律原生收拢于 `cmake/`，**严禁在根域设立 `scripts/` 体外脚本**；
- 落地纯原生 `cmake/AuditPrivacy.cmake`（支持 `cmake -DFIX=ON -P cmake/AuditPrivacy.cmake` 一键原位净化与无参 `cmake -P cmake/AuditPrivacy.cmake` `FATAL_ERROR` 硬阻断），并接入 `cmake/PackageRelease.cmake` 前置门禁循环。

---

### 任务二（Track B）：开源许可与 GPLv3 自由软件合规净化

#### 2.1 SPDX 双许可头全覆盖审查
运行 `cmake -P cmake/AuditSpdx.cmake` 并逐文件审计，确保：
1. **主程序及核心层**（`include/rtx/`、`src/core/`、`src/gui/`、`src/io/`、`src/cli/`、`tests/`、`cmake/`、`CMakeLists.txt`）：全部包含 `SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception`；
2. **桥接隔离层**（`src/shim/`、`include/shim/`）：全部包含 `SPDX-License-Identifier: MIT OR Apache-2.0`。

#### 2.2 专有 SDK 物理隔离合规终审
1. 确认 `CineRTX.exe` 与 `rtx_headless_cli.exe` 仅静态依赖自由软件与 Windows 基础系统组件；
2. 确认 NVIDIA 专有库均通过独立动态库 `rtx_ngx_bridge.dll` 动态加载，主二进制静态导入表零专有符号；
3. 根目录 `LICENSE`、`COPYING.EXCEPTION`、`LICENSE-MIT`、`LICENSE-APACHE` 完整自洽。

---

## 三、 硬性工程纪律与验收要求

1. **具名类型与 SLOC 铁律**：保持 `AuditTypes = 34 / 35 (0 Violations)`，Core Engine Pure SLOC 保持 `12,157` 行（`<= 23,000`）；
2. **环境卫生铁律**：仓内零 `.bak` / `.orig` / `.tmp` 残留，根域零 `scripts/` 体外脚本；
3. **版本库主权铁律**：严禁擅自执行 `git push`，所有验证均通过纯 CMake 原生命令序列完成。
