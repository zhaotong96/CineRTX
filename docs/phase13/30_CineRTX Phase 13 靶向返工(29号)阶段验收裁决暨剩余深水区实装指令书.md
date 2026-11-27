# 30_CineRTX Phase 13 靶向返工（29号）阶段验收裁决暨剩余深水区实装指令书

- **裁决编号**：`CINERTX-P13-RULING-030`
- **裁决主体**：CineRTX 项目总协调负责人（Coordinator）
- **被审汇报**：[`29_CineRTX Phase 13 靶向编译清障与真接线返工汇报.md`](./29_CineRTX%20Phase%2013%20靶向编译清障与真接线返工汇报.md)
- **依据红线**：`[DEC-003]`（文档落盘）、`[DEC-004]`（真机全链路闭环）、`[DEC-008]`（宿主机实测先于呈报）、`[DEC-011]`（单 Profile 四桶引擎）
- **阶段裁决**：🟡 **P0 编译与门禁闭环有条件放行（24 号清障主体达成）；整阶段仍暂缓封板（CONDITIONAL HOLD）—— `rtx_tests` 尚余 2 项失败，NVOFA 切镜与 WebUI 帧流存在逻辑悬空，25–28 号深水区待实装。**

---

## 一、 宿主机真机全量实测结果（Windows 11 + MSVC 19.51 + RTX 5060）

| 核验维度 | 实测命令与输出证据 | 判定 |
| :--- | :--- | :---: |
| **`.\build.bat` 全量编译与四桶引擎** | `CineRTX.exe`、`rtx_headless_cli.exe`、`rtx_tests.exe`、`rtx_ngx_bridge.dll` 全目标编译通过（退出码 `0`）；RTX 5060 构建成功 4 个独立单 Profile 引擎：`rife_v4.25_ultra.engine`（24,796,036 B）、`wide`（24,960,436 B）、`square`（25,170,356 B）、`tall`（25,561,172 B）。 | **PASS** |
| **五重静态门禁 & 双模 IAT** | • `AuditPrivacy.cmake`：**`PASS: 0 violations across 423 file(s)`**<br>• `AuditTypes.cmake`：**`OK: 34/35 named types`**<br>• `AuditSpdx.cmake`：**`PASS (GPL=114 MIT=39)`**<br>• `AuditSloc.cmake`：**Core Engine = `12,399`（`<= 23,000`），Full Repo = `31,707`**<br>• `AuditImports.cmake (GUI)`：**`PASS: 4 physical DLL(s) = [bcrypt.dll, gdi32.dll, kernel32.dll, user32.dll]`**<br>• `AuditImports.cmake (HEADLESS)`：**`PASS: 2 physical DLL(s) = [bcrypt.dll, kernel32.dll]`** | **PASS** |
| **全套单元测试 (`rtx_tests.exe`)** | 实跑 **`150 test(s) run, 2 failed (59232 ms)`**：<br>• 原 P0 崩溃项 `rife_four_phase_monotonic_progression`、`playback_session_native_decode_h264_hevc_av1`、`playback_clock_software_master`、`playback_session_fence_gated_slot_retirement`、`gui_flicks_timebase`、`gui_flicks_timecode_format` 与新增 `p13r4_merge_concat_executes_ffconcat_manifest` 等 **148 项全部 PASS**。<br>• **残留 2 项失败**：<br>  1. `nle_json_out_of_order_keys_and_duplicate_id_guard`（`tests/test_nle_vertical_slice.cpp:174: actual 705600000, expected 353152800000`）<br>  2. `playback_gop_seek_preroll`（`tests/test_playback_session.cpp:371: actual 0, expected 1`） | **PARTIAL FAIL (148/150)** |

---

## 二、 代码穿透复核发现的“逻辑悬空”清单

1. **Wave 2-B NVOFA 切镜检测实际处于恒假空转状态**：
   - `src/core/hw_transcode_stages.cpp:448-455` 以 `(kFlicksPerSecond / 90000ULL) * 9000LL` 判断 90kHz `pts_discontinuity`，该阈值约等价于 78.4 万秒，且正常视频硬切帧间 PTS 连续；导致 `nvof_metrics` 恒为全 `0.0f`，**未调用任何像素/8x8 SAD 切镜内核（`rtx_eval_block_sad_8x8`）**，NVOFA 切镜果冻效应仍未真实解决。
2. **WebUI SDR 代理预览缺少后端帧生产者**：
   - `webui/server.py:1066-1081` 读取临时目录中的 `cinertx_latest_preview.jpg`，但目前转码/播放后端并无任何模块向该路径写入 JPEG 帧，导致接口恒返回 `204 No Content`。
3. **25–28 号靶向深水区仍待实装**（与 29 号汇报第三节诚实披露一致）：
   - `SvtAv1Enc.lib` / `x265-static.lib` 宿主机源码编译与实片 CRF 转码；
   - 播放器 60fps AI 实时增强管线（`MediaPlaybackSession` 接入 VSR/TrueHDR/FRUC）；
   - 动态加载 `d2d1.dll` / `dwrite.dll` 组件库、清除 `chromeText`、Win32 横向可拖拽 DAG 卡片画布与预览勾选框。

---

## 三、 下一步收口与深水区推进指令

1. **P0 残留双测试清零**：修复 `tests/test_nle_vertical_slice.cpp:174` 遗留的旧 `705600000` 字面量及 `tests/test_playback_session.cpp:371` `odd_demuxer.last_seek_pts` 边界，达成 `rtx_tests.exe` **150/150 (0 FAILED)**。
2. **Wave 2-B 真像素切镜接线（26 号施工单）**：移除 `pts_discontinuity` 安慰剂判断，将真实 CUDA 8x8 分块 SAD（及黑场/淡入淡出亮度统计）接入 NVOFA 前置路径并联动 `force_reset_temporal=true`。
3. **按 `25` → `28` 号靶向施工单依次闭环剩余深水区**：补齐 WebUI JPEG 帧写入（或预览代理闭环）、静态软编验证、60fps 增强播放器、D2D1/DirectWrite 组件库及可拖拽 DAG 画布。
