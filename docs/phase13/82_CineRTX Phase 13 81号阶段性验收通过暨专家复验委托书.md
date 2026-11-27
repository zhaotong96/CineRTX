# 82_CineRTX Phase 13 81号阶段性验收通过暨专家复验委托书

- **基线与状态**：基于 `0beda87` 增量实施 `81_CineRTX Phase 13 77-80号施工单返工汇报.md`（零旧快照整文件覆写，`consumeNvofSceneCut` 与 `isPreviewEnabled()` 完整保留）。
- **委托目标**：请首席领域专家对 `81_` 阶段性成果及宿主机全绿实测结果执行穿透复验，确认是否准予阶段性封存并批复下一波次施工顺序。

---

## 一、宿主机实测结果汇总（协调负责人已逐项实跑）

| 验收门禁 | 实测结果 | 备注 |
| :--- | :--- | :--- |
| **`AuditPrivacy`** | **`PASS: 0 violations across 476 file(s)`** | 全仓无隐私泄露 |
| **`AuditSpdx`** | **`PASS (GPL=115 MIT=39)`** | 许可边界合规 |
| **`ContractGuard`** | **`PASS`** | `consumeNvofSceneCut`、`isPreviewEnabled()` 均完整保留 |
| **`AuditTypes`** | **`34/35`**（新增 `+0` 具名类型） | 严守无新增类型约束 |
| **`AuditSloc`** | **Core `12,585 / 23,000`** | 在受控预算内 |
| **`.\build.bat` (MSVC 19.51 `/W4 /WX`)** | **退出码 `0`**（`0 Error, 0 Warning`） | 已物理移除 `rtx_headless_cli` 并接入前置/后置串行门禁 |
| **`AuditImports` (SINGLE-EXE)** | **`PASS: 4/4 physical DLL(s)`** (`bcrypt.dll, gdi32.dll, kernel32.dll, user32.dll`) | 缓存 FFmpeg 仍含 `bcrypt.dll`，待重编 FFmpeg 后启用 `-DSTRICT_NO_BCRYPT=ON`（`<= 3`） |
| **`rtx_tests.exe`** | **`158 test(s) run, 0 failed (113545 ms)`** | 含新增 `test_merge_concat_homogeneity_classification` 及全部 RIFE/预览测试 |
| **单 EXE 管道重定向** | `CineRTX.exe --help` 退出码 `0`，标准输出管道完整落盘 | 补充观测：`CineRTX.exe --headless --probe` 退出码 `0` 但标准输出为空（待核查 `--headless` 与 `--probe` 组合分支） |

---

## 二、已落地范围 vs 待续余项边界

- **已落地并实测通过**：
  1. `77_` 全部：三道防覆写护栏（`cmake/ContractGuard.cmake` + `CMakeLists.txt` + `build.bat` 强串行审计）、物理拔除 `rtx_headless_cli` 单 EXE 收敛、`/SUBSYSTEM:WINDOWS` 无状态 `bindParentConsoleIfNeeded()` 控制台与管道重定向保护；
  2. `78_` 代码与门禁部分：砍除 `CINERTX_ENABLE_GPL_CODECS` 与 `--enable-gpl` / `libx265` / `libx264` 收敛至纯 LGPL + `libsvtav1`、移除漂移 Zig 分支、`cmake/RtxFfmpegStatic.cmake` 补齐 `HAVE_BCRYPT=0` 补丁、`cmake/AuditImports.cmake` 收紧黑名单（`kernelbase.dll`、`dcomp.dll`、`dwrite.dll`、`d2d1.dll`、`mmdevapi.dll`、`avrt.dll`）及 `-DSTRICT_NO_BCRYPT=ON`（`<= 3`）；
  3. `79_` `MergeConcat` 异构闭环：`areConcatInputsHomogeneous()` + `runNvidiaRemasterRenderGraph()` 异构路由 + `isPreviewEnabled()` 双分支保留 + `test_merge_concat_homogeneity_classification`。
- **诚实保留待续余项**（计划交由工程师在后续波次完成）：
  1. 清理 `.vs/build/external/ffmpeg-static` 缓存重编 FFmpeg（激活 `HAVE_BCRYPT=0` 与静态 `SvtAv1Enc.lib`），并以 `-DSTRICT_NO_BCRYPT=ON` 验证 `CineRTX.exe` 静态 IAT 剥离 `bcrypt.dll`；
  2. `78_` 存量类型物理清退至 `< 34`；
  3. `79_` 余项：`src/io/ffmpeg_io.cpp` MP4 非直通音轨（`SwrContext` + `AVAudioFifo` -> AAC-LC）与字幕 -> `mov_text` 转码闭环、`analyze_temporal_boundary()` 公共切镜总线与 `preview_generation_`；
  4. `80_`（Wave C-D：60fps AI 实时播放器、任意比例 RIFE 4 桶池、纯 D2D1/DWrite 剥离 `gdi32.dll` 与动态加载 `user32.dll` 达成 IAT `== 1`）。

---

## 三、请专家复验与裁决事项

1. **阶段性复验裁决**：核验 `81_` 交付是否符合 `77_`、阶段性 `78_` 与 `79_`（`MergeConcat`）要求，是否准予本地 `git commit` 锁定阶段基线。
2. **CLI 组合参数核查**：针对 `CineRTX.exe --headless --probe` 退出码 `0` 但无标准输出的实测现象，请复核 `src/cli/main.cpp` 中 `--headless` 与 `--probe` 参数分发是否需要补齐输出行为。
3. **下一波次施工顺序确认**：确认是否按 **“重编 FFmpeg 归零 `bcrypt.dll` + `78_` 类型清退 -> `79_` MP4 音轨/字幕与切镜总线 -> `80_` Wave C-D”** 顺序交由执行工程师分波次推进。
