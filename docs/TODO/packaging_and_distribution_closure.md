# CineRTX 发布打包分发与运行时闭包演进待办规范

---

**任务编号**：`TASK-TODO-DISTRIBUTION-CLOSURE-004`  
**任务性质**：开源发行物打包分发、首方运行时依赖闭包与宿主驱动前置规约设计待办备忘录 (Release Packaging & Distribution Closure Specification)  
**关联合规报告**：`docs/phase4/audit.md` (发现项 F-01, F-08)  
**当前状态**：`PROPOSED (待功能完善后于发布阶段实施)`  
**起草日期**：2026 年 11 月 2 日  

---

## 一、 待办立项背景与问题定义

在阶段四独立同行评议审计报告（`docs/phase4/audit.md`，发现项 F-01 与 F-08）中，审计工程师指出当前发布打包脚本 `tools/package.ps1` 存在以下分发闭包与定义不一致问题：

1. **第一方运行时模块遗漏**：当前 `tools/package.ps1` 仅将主可执行文件 `CineRTX.exe` 及文档、头文件打入 ZIP 归档。然而在 Windows 生产路径下，RTX VSR 与 TrueHDR 重制功能依赖由本项目源码编译出的开源桥接动态库 `rtx_ngx_bridge.dll`（位于 `src/shim/ngx_bridge/`）。若发布 ZIP 中缺少该 DLL，用户在无开发构建环境的干净目标机上解压运行将无法开启重制管线；
2. **专有驱动与应用的分发边界模糊**：审计报告将“单 EXE 闭包”与“包含所有专有 NVIDIA 运行库”混淆。在开源法务与系统架构上，NVIDIA 专有运行时（`nvcuda.dll`、`nvngx_vsr.dll`、`nvngx_truehdr.dll` 等）属于闭源专有驱动栈，**严禁随 GPLv3 开源发布包直接重新分发**（否则构成违反 NVIDIA SDK EULA 与版权侵权）；
3. **分发合同界定不清**：文档中关于“绝对自包含单 EXE”的表述易使外部审计与终端用户误解为“完全无需安装任何驱动的独立二进制”，需要对“第一方代码自包含（静态内联 FFmpeg，免外部 `av*.dll`）”与“底层硬件加速依赖（需宿主安装合规 NVIDIA 显卡驱动）”作出正交解耦定义。

鉴于当前项目处于底层音视频管线与核心功能演进阶段，尚未进入正式对外分发打包周期，且当前源码架构在 GPLv3 WITH Plugin-Exception 许可下完全合法合规，工程团队决定本轮暂不执行重型打包改造，立此 TODO 文档，作为后续发布阶段的实施依据。

---

## 二、 演进方案设计与实施准则

待项目核心转码功能与 GUI 完全收敛并进入发布前夕时，执行以下三项标准改造：

### 2.1 改造 `tools/package.ps1` 纳入第一方构建物

在打包脚本中，显式将构建目录生成的内部动态库加入发布清单：
- 复制 `build/cmake/windows-msvc-ninja/rtx_ngx_bridge.dll` 至发布归档根目录（与 `CineRTX.exe` 同级）；
- 对生成的 ZIP 归档执行解压后完整性校验，确认第一方桥接库 100% 存在。

### 2.2 确立清晰的双层分发契约 (Distribution Contract)

在用户文档、`README.md` 及发布 Release Notes 中明确以下分发契约：

| 分发层级 | 交付主体与包含内容 | 用户前置条件 (Prerequisites) | 法务与开源许可边界 |
| :--- | :--- | :--- | :--- |
| **第一方自包含交付层 (First-Party Envelope)** | `CineRTX.exe`（内联静态 FFmpeg）+ `rtx_ngx_bridge.dll`（首方开源桥接库） | Windows 10/11 x64 操作系统，已安装 MSVC 2026/UCRT 运行库 | 严格遵循 GPL-3.0-or-later WITH Plugin-Exception，源码 100% 开放 |
| **底层硬件驱动层 (Host Runtime Environment)** | NVIDIA Display Driver（包含 `nvcuda.dll`、`nvcuvid.dll`、`nvEncodeAPI64.dll` 及 NGX 运行时） | 用户宿主机需具备 Turing (sm_75) 或 Blackwell (sm_120) 及以上显卡，安装驱动版本 >= 617.14 | 专有闭源驱动，受 NVIDIA EULA 约束，由用户官方渠道安装，不得随本仓库二次分发 |

### 2.3 升级自动化审计脚本 `tools/audit_imports.ps1`

- 拆分审计报告中的检查项：
  - 门禁一：`CineRTX.exe` 静态导入表零 NVIDIA 专有符号导入（PASS）；
  - 门禁二：发布目录中第一方必要组件（`CineRTX.exe` 与 `rtx_ngx_bridge.dll`）完整存在；
  - 门禁三：显式将第三方专有运行库（`nv*.dll`）列为“系统/驱动前置依赖白名单”，而非发布包必需项。

---

## 三、 验收标准 (Acceptance Criteria)

1. `tools/package.ps1` 产出的 Windows ZIP 包含 `CineRTX.exe` 与 `rtx_ngx_bridge.dll`；
2. 干净的测试机在安装官方 NVIDIA 显卡驱动后，解压该 ZIP 即可正常执行转码与 VSR/TrueHDR 重制；
3. 全仓文档不再使用“完全无外部前置条件的单 EXE”等绝对化词汇，统一规范为“第一方静态自包含单 EXE，专有显卡功能依赖宿主官方显卡驱动”。
