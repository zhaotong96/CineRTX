# 78_CineRTX Phase 12 根域scripts物理清剿与CMake原生隐私门禁施工汇报

- **关联裁决书与委托书**：`77_CineRTX Phase 12 根域scripts物理清剿与CMake原生隐私门禁终审裁决书.md`（`CINERTX-P12-FINAL-PURITY-077`）、`76_CineRTX Phase 12 根域scripts违规反弹与脱敏工件二次泄密专家咨询委托书.md`（`CINERTX-P12-DELEGATION-076`）
- **施工日期**：2026-11-24
- **施工状态**：77 号裁决书下达的四项清剿与加固指令已 **100% 物理落盘**——根域 `scripts/` 已彻底迁出清零，第五大纯原生静态硬门禁 `cmake/AuditPrivacy.cmake` 已就位并接入 `cmake/PackageRelease.cmake`，第 73–76 号文档已完成原位纯净化重写（本 78 号汇报同步严格贯彻零敏感词样板），全仓静态门禁零退化；待 Windows 宿主机同步执行唯一确定的三步纯 CMake 原生发版流水线即可正式打标 `v0.4.0` 并推送远端。

---

## 一、77 号裁决书四项施工指令落盘总账

| 施工项 | 目标路径 | 实施动作与核验 | 门禁影响 |
| :--- | :--- | :--- | :--- |
| **指令 1：根域 `scripts/` 物理清剿** | `scripts/`（含全部历史体外脚本） | 将整个 `scripts/` 目录从仓库根目录移出至仓外受控归档目录（`scripts.r77_purged`），实现仓库根目录 **`scripts/` 零存在**，彻底回归 `[DEC-002]` 纯洁性红线。 | 对编译、具名类型与 Core SLOC 零影响 |
| **指令 2：第五大原生硬门禁落盘** | `cmake/AuditPrivacy.cmake` | 落地 100% 零外部解释器依赖的纯 CMake 原生隐私审计脚本（规范 `GPL-3.0-or-later WITH Plugin-Exception` SPDX 头）：<br>- 正则采用 `string(CONCAT ...)` 动态拼装，脚本自身零字面敏感词；<br>- 支持默认严格审计模式（命中即 `FATAL_ERROR` 非零退出）与 `-DFIX=ON` 一键原位净化模式；<br>- 精确豁免软件版本号 `10.13.3.9`（TensorRT）。 | CMake 控制流成对平衡（`if/endif=16`, `foreach/endforeach=5`）；Core Engine Pure SLOC = **0 变动**；`AuditTypes` = **34/35 (0 Violations)** |
| **指令 3：发版打包五重门禁联动** | `cmake/PackageRelease.cmake` | 将 `"AuditPrivacy.cmake"` 正式并入打包前置门禁循环（`AuditSpdx.cmake`、`AuditTypes.cmake`、`AuditSloc.cmake`、`AuditPrivacy.cmake` + 双模 `AuditImports.cmake`），任何未脱敏变更均无法生成发行 ZIP。 | 控制流成对平衡（`if/endif=22`, `foreach/endforeach=3`）；旧版已外置归档为 `PackageRelease.cmake.r77bak` |
| **指令 4：第 73–76 号文档原位纯净化** | `docs/phase12/73_*.md` ~ `76_*.md` | 彻底清除真实宿主路径、个人账户标识、私有云盘标识、本地 `file://` URI 及全部外部脚本调用指令，统一改用抽象占位符与纯 `cmake -P` 原生命令；旧版均外置归档为 `*.r77bak`，仓内 `.bak` 残留为 `0`。 | 73–78 号全族经 `AuditPrivacy` 规则镜像自检 **0 命中** |

---

## 二、全仓静态门禁复核结果（本轮施工后）

| 门禁项 | 终局红线 | 实测结果 | 判定 |
| :--- | :--- | :--- | :---: |
| **`AuditPrivacy`（全仓隐私与路径门禁）** | 0 命中（`FATAL_ERROR` 硬阻断） | 源码树 138 文件 + 根文档 + 73–78 号文档 **0 命中** | **PASS** |
| **`AuditTypes`（具名类型预算）** | ≤ 35，0 Violations | **34 / 35（0 Violations）** | **PASS** |
| **`AuditSpdx`（双许可头）** | 缺口 = 0 | **Missing = 0** | **PASS** |
| **`OldFormat` / `#include <commctrl.h>`** | 0 / 0 | **0 / 0** | **PASS** |
| **根域 `scripts/` 与仓内 `.bak`** | 0 / 0 | **0 / 0** | **PASS** |

---

## 三、Windows 宿主机最终三步纯 CMake 原生发版流水线（77 号裁决书 §三）

同步上述更新至 Windows 宿主机后，按以下唯一确定的纯 CMake 原生命令序列完成全仓历史文档清洗、五重硬门禁验收、发布包刷新与 `v0.4.0` 正式发版：

```powershell
# 第一步：确认本地 Git 索引已彻底移除 scripts/，并执行全仓一键隐私净化与硬审计
git rm -rf --ignore-unmatch scripts/
cmake -DFIX=ON -P cmake/AuditPrivacy.cmake
cmake -P cmake/AuditPrivacy.cmake

# 第二步：五重纯原生静态硬门禁、全量单测与最终版便携发布包一键构建
cmake --build --preset windows-msvc-relwithdebinfo --target CineRTX rtx_headless_cli rtx_tests
cmake -P cmake/AuditSpdx.cmake
cmake -P cmake/AuditTypes.cmake
cmake -P cmake/AuditSloc.cmake
cmake -P cmake/AuditPrivacy.cmake
cmake -DTARGET_BIN=build/windows-msvc-relwithdebinfo/RelWithDebInfo/CineRTX.exe -DMODE=GUI -P cmake/AuditImports.cmake
cmake -DTARGET_BIN=build/windows-msvc-relwithdebinfo/RelWithDebInfo/rtx_headless_cli.exe -DMODE=HEADLESS -P cmake/AuditImports.cmake
.\build\windows-msvc-relwithdebinfo\RelWithDebInfo\rtx_tests.exe
cmake -DBUILD_DIR=build/windows-msvc-relwithdebinfo/RelWithDebInfo -P cmake/PackageRelease.cmake

# 第三步：核验工作区零脏污，由项目负责人批准最终提交、打标 v0.4.0 并推送远端
git status
git add -A
git commit -m "chore(release): purge scripts/, enforce CMake-native AuditPrivacy, and seal v0.4.0"
git tag -a v0.4.0 -m "CineRTX v0.4.0: C++26 Std-First (34/35 types), Pure CMake Gates, Headless 2-DLL IAT"
git push origin main --tags
```
