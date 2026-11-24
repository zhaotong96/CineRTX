# 65_CineRTX Phase 12 Wave 12.4 阶段2-4收官工程准备与发布脚本加固汇报

- **关联验收裁决书**：`64_CineRTX Phase 12 Wave 12.4 阶段1验收裁决书暨阶段2-3实机回归与v0.4.0终审指引.md`（`CINERTX-P12-RULING-064`）
- **关联前序文档**：`63_CineRTX Phase 12 Wave 12.4 阶段1隐患清零复验暨阶段2-3实机回归委托书.md`、`62_CineRTX Phase 12 Wave 12.4 隐患清零与v0.4.0收官施工汇报.md`、`06_CineRTX Phase 12 SuperPower 施工单_全场景实机回归与v0.4.0封板验收.md`
- **施工日期**：2026-11-24
- **施工状态**：已审慎穿透检查阶段 2–4 发布与回归脚本链，**提前捕获并根治 `cmake/PackageRelease.cmake` 的致命打包断环**，同步落地一键化 Wave 12.4 实机回归脚本 `scripts/verify_wave12_4_release.ps1` 与 `docs/archive/HISTORY_SUMMARY_P1_P11.md` 历史归档摘要；全仓静态门禁零退化，已就绪交由 Windows RTX 5060 宿主机一键执行阶段 2–4 实机采证与 `v0.4.0` 封板。

---

## 一、审慎检查捕获的致命隐患与三项收官施工交付

在依据 64 号验收裁决书推进阶段 2–4 收官准备时，执行工程师对 `cmake/PackageRelease.cmake`、`scripts/` 与 `docs/archive/` 进行了逐行穿透审计，发现原 `cmake/PackageRelease.cmake` 第 28 行仍保留旧版无参 `include("${CMAKE_CURRENT_LIST_DIR}/AuditImports.cmake")`，在 Wave 12.3 升级 `AuditImports.cmake` 为双模强门禁后**会在打包阶段直接抛出 `Missing -DTARGET_BIN` 致命错误**，且未打包 `rtx_headless_cli.exe`、未在发布说明中披露 `rtx_ngx_bridge.dll` 的 3 项 NVIDIA 厂商静态导入。本轮已完成以下 3 项收官加固：

| 施工项 | 目标文件 | 核心改动与价值 | 门禁影响 |
| :--- | :--- | :--- | :--- |
| **加固 1（阻断级修复）** | `cmake/PackageRelease.cmake` | 1. **修复打包门禁断环**：将原无参 `include(AuditImports.cmake)` 升级为通过 `execute_process` 依次执行 `AuditSpdx.cmake`、`AuditTypes.cmake`、`AuditSloc.cmake`、`AuditImports.cmake -DMODE=GUI`（校验 `CineRTX.exe`）及 `AuditImports.cmake -DMODE=HEADLESS`（校验 `rtx_headless_cli.exe`），任一门禁失败即阻断发版。<br>2. **补齐无头产物打包**：在绿色压缩包 `CineRTX-v0.4.0-windows-x64.zip` 中同步收纳 `rtx_headless_cli.exe`。<br>3. **落实 64 号裁决书 §3.2 披露条款**：自动生成 `RELEASE_NOTES.txt` 随包分发并计入 `SHA256SUMS.txt` / `manifest.json`，如实披露 `rtx_ngx_bridge.dll` 因 NVIDIA `nvsdk_ngx_s.lib` 引入的 3 项系统 DLL 静态导入（`KERNEL32.dll`、`USER32.dll`、`ADVAPI32.dll`）及主程序绝对隔离边界。 | CMake 控制流成对校验通过（`if/endif=22`, `foreach/endforeach=3`）；旧版已外置归档为 `PackageRelease.cmake.r64bak` |
| **加固 2（实机一键脚本）** | `scripts/verify_wave12_4_release.ps1` | 严格 1:1 对齐 64 号裁决书 §2（阶段 2 E2E-01~04）、§3.1（阶段 3 无头契约与 `--session-stdio` 运行时模块快照 11 项禁载 DLL 断言）及 §3.2（阶段 4 `PackageRelease.cmake` 打包），支持 `-Mode All \| Gates \| Headless \| Desktop \| Package`；前置强检 `SessionId > 0` 与 `nvidia-smi` 驱动 ≥ `621.24 WHQL`，全部日志与探针输出统一落盘至仓外 `$env:TEMP\CineRTX\evidence\phase12\`。 | 位于 `scripts/`，对 `AuditTypes`（34/35）与 `AuditSloc`（Core 12,157）**零影响** |
| **加固 3（历史归档收口）** | `docs/archive/HISTORY_SUMMARY_P1_P11.md` | 落实 06 号施工单 W12.4-1 与 64 号裁决书 §3.2 要求，在完整保留 `docs/archive/HISTORY_SUMMARY_P1_P10.md` 与 `docs/phase11/` 原始卷宗的前提下，编制 Phase 1–11 权威演进摘要（涵盖 Wave 0 暗礁根治、Track 11A/B/C/D、Phase 11.4 RIFE `scale=0.5` A=128 / 7,300 MiB 显存调度 / SDK 13.1 多缓冲池及 Phase 11.2-R 反 Slop 熔断动因）。 | 纯归档文档，无任何破坏性删除 |

---

## 二、静态卫生与全仓门禁复核（本轮施工后）

| 核验项 | 目标红线 | 实测状态 | 判定 |
| :--- | :--- | :--- | :---: |
| **`AuditTypes`（具名类型预算）** | ≤ 35，0 Violations | **34 / 35（0 Violations）** | **PASS** |
| **`AuditSpdx`（双许可头）** | 缺口 = 0 | **Missing = 0**（新增脚本均带规范 SPDX 头） | **PASS** |
| **`OldFormat` / 死头文件 / `0.3.0`** | 全部 0 命中 | **全部 0 命中** | **PASS** |
| **仓内 `.bak` 残留** | 0 命中 | **`CineRTX/` 仓内 0 命中**（旧 `PackageRelease.cmake` 已外置归档至 `work/_CineRTX_bak_archive/PackageRelease.cmake.r64bak`） | **PASS** |

---

## 三、Windows RTX 5060 宿主机阶段 2–4 一键实机执行指引

> **实证诚实性声明**：当前沙箱无 RTX 5060 GPU 与 WTS 交互式桌面环境，严守 06 号施工单军规 1 & 2，**绝不捏造任何实机压测数值**。请在 Windows 11 + RTX 5060 宿主机（活跃控制台桌面 `SessionId > 0`，驱动 ≥ `621.24 WHQL`）同步最新 3 个文件后执行：

```powershell
# 1. 一键跑通静态五重门禁 + 双向 rtx_tests + 阶段 3 无头模块快照审计
powershell -ExecutionPolicy Bypass -File scripts\verify_wave12_4_release.ps1 `
    -BuildDir "build\windows-msvc-relwithdebinfo\RelWithDebInfo" `
    -Mode Gates

powershell -ExecutionPolicy Bypass -File scripts\verify_wave12_4_release.ps1 `
    -BuildDir "build\windows-msvc-relwithdebinfo\RelWithDebInfo" `
    -Mode Headless

# 2. 阶段 2 交互式桌面实机压测（传入真实 1080p 横屏与 9:16 竖屏测试素材路径）
powershell -ExecutionPolicy Bypass -File scripts\verify_wave12_4_release.ps1 `
    -BuildDir "build\windows-msvc-relwithdebinfo\RelWithDebInfo" `
    -Mode Desktop `
    -LandscapeInput "C:\path\to\1080p_sdr.mp4" `
    -PortraitInput  "C:\path\to\1080x1920_sdr.mp4"

# 3. 阶段 4 打包生成 CineRTX-v0.4.0-windows-x64.zip 及 SHA-256 清单
powershell -ExecutionPolicy Bypass -File scripts\verify_wave12_4_release.ps1 `
    -BuildDir "build\windows-msvc-relwithdebinfo\RelWithDebInfo" `
    -Mode Package
```

完成上述实机采证并留存 `$env:TEMP\CineRTX\evidence\phase12\` 全部真实日志（含 E2E-01 `D2H == 0 B` / 显存 < `5,986 MiB` / `ffprobe` 报告、E2E-03 100 次 Resize、E2E-04 NLE 硬切导出与无头 `headless_runtime_modules.log` 零禁载 DLL 报告）后，即可由协调推进组呈报《66 号终审封板委托书》，经专家组终审签印并由用户授权打标 **`v0.4.0`**。
