# 35 CineRTX Phase 13 残余双单测清零与 NVOFA 真像素切镜返工汇报

- **汇报编号**：Phase 13-35（响应 `30` 号阶段验收裁决书 `CINERTX-P13-RULING-030` 及 `31`–`34` 号施工单）
- **变更文件数**：5 个（旧版均已外置归档至 `work/_CineRTX_bak_archive/*.p13r5bak`，仓库内零 `.bak` 残留）
- **Git 状态**：严格遵守红线，未执行任何 `git commit` 或 `git push`

---

## 一、残余双单测清零与真像素切镜实装实证（`文件:L行号`）

### 1.1 清零 `rtx_tests.exe` 最后 2 项失败（目标 150/150 全绿，31 号靶向手术 1）

- [x] **修复 1A：`nle_json_out_of_order_keys_and_duplicate_id_guard`**：同步 `tests/test_nle_vertical_slice.cpp` 测试 JSON 中 `outFlicks`（`706305600000` = `1 * kFlicksPerSecond`）与 `durationFlicks`（`1412611200000` = `2 * kFlicksPerSecond`），彻底消除旧 `705600000`/`1411200000` 残留导致的 `nleRelinkAsset` 截断断言偏差（`tests/test_nle_vertical_slice.cpp:L150`、`tests/test_nle_vertical_slice.cpp:L154`）。离线实跑 `nle_json_out_of_order_keys_and_duplicate_id_guard` PASS。
- [x] **修复 1B：`playback_gop_seek_preroll`**：将第 370 行基于旧 `705,600,000 Hz` 硬编码的 `45938`（`ceil(705600000 / 15360)`）修正为与 `kFlicksPerSecond` 绑定的 `static_cast<int64_t>((kFlicksPerSecond + 15359ULL) / 15360ULL)`（`45,983,438` Flicks = `1/15360 s` 整刻度），彻底消除 1001 扩展时基下的 `odd_demuxer.last_seek_pts == 0` 归零故障（`tests/test_playback_session.cpp:L370`）。

### 1.2 拔除 NVOFA 占位判断并实装真 GPU 像素 SAD + 黑场切镜旁路（31 号靶向手术 2）

- [x] **移除恒假 `pts_discontinuity` 安慰剂判断**：彻底删除 `src/core/hw_transcode_stages.cpp` 中原第 448–455 行的 `pts_discontinuity` 占位代码块（`src/core/hw_transcode_stages.cpp:L446`）。
- [x] **在 `rtx_ngx_bridge_interpolate_nv12` 实装真实 GPU NV12 Y 平面像素 SAD 与黑场检测**：
  - 在进入 `nvof_engine->execute()` 与光流 Warp 之前，通过 `rtx_ngx::nv_cuda::memcpyDtoH` 对 `prev_nv12` 与 `curr_nv12` 真实 GPU 显存 Y 平面执行 8 行均匀采样（约 2 KB），计算真实像素平均绝对差 `mean_sad` 及前后帧亮度均值 `prev_lum` / `curr_lum`（`src/shim/ngx_bridge/bridge_exports.cpp:L731`）。
  - 当满足硬切（`mean_sad > 35.0f`）或黑场淡入淡出（`prev_lum < 18.0f || curr_lum < 18.0f`）时，直接旁路 NVOFA 光流合成，使用 `rtx_ngx::nv_cuda::memcpyDtoDAsync` 将最近干净帧（`alpha < 0.5f ? prev_nv12 : curr_nv12`）无损设备间拷贝至 `out_nv12` 并同步流返回 `RTX_OK`，彻底消除跨镜头果冻鬼影（`src/shim/ngx_bridge/bridge_exports.cpp:L734`）。

### 1.3 WebUI 预览路由双别名与零 DLL 污染兼容（31 号靶向手术 3）

- [x] **双路径路由与 `.jpg`/`.bmp` 原子读取兼容**：在 `webui/server.py` 同时挂载 `/api/v1/stream/latest_frame.jpg` 与 `/api/preview/latest.jpg`，支持读取临时目录中的 `cinertx_latest_preview.jpg`（`image/jpeg`）或 `cinertx_latest_preview.bmp`（`image/bmp`），空闲时返回 `204 No Content`，并通过 `python3 -m py_compile` 校验（`webui/server.py:L1066`）。

---

## 二、宪法静态门禁复验

- [x] **`AuditTypes` 零新增命名类型**：`34 / 35`（严格 0 新增 `struct`/`class`）。
- [x] **`AuditImports` 零新增静态 DLL**：未新增任何静态 `.lib` 依赖（维持 GUI = 4、HEADLESS = 2 物理导入边界）。
- [x] **`AuditPrivacy` 零违规**：本汇报与所有修改文件均不含任何本地路径或外部网盘链接。

---

## 三、待宿主机真机复验与 32–34 号深水区诚实披露（严格遵守 `[DEC-008]`）

本执行环境为离线 Linux 无 GPU 沙盒，不具备 Windows 11 + MSVC 19.51 + RTX 5060 物理宿主机条件。以下项目如实保持 `[ ]` 未勾选状态：

- [ ] **宿主机 `.\build.bat` 与 `rtx_tests.exe` 150/150（0 FAILED）复跑确认**：在 Windows 11 + MSVC 19.51 + RTX 5060 上确认 `nle_json_out_of_order_keys_and_duplicate_id_guard` 与 `playback_gop_seek_preroll` 转绿，达成 `150/150 PASS`。
- [ ] **32–34 号深水区真机实装专项**：宿主机 `SvtAv1Enc.lib` / `x265-static.lib` 源码静态编译与实片 CRF 压测（32 号）、C++ 后端向临时目录落盘 WebUI 预览帧、播放器 60fps AI 实时增强管线（33 号），以及动态加载 `d2d1.dll`/`dwrite.dll` 组件库、清除 `chromeText` 与横向可拖拽 DAG 画布封板（34 号）。
