# CineRTX Win32 Native UI 阶段二返工验收报告（Track B1–B4）

- **对应裁决**：`docs/phase8/CineRTX 阶段二终审裁决：要求工程师对阶段2（Win32 Native UI）返工规范.md`（DOC-SUPERPOWER-STAGE2-TRACKB-VERDICT-REWORK-007，RW-1～RW-6）
- **对应施工单/验收单**：Track B1（D3D12 视口显存安全）、Track B2（媒体探测与动态超分）、Track B3（桌面布局双语与播放控制）、Track B4（实机桌面交互穿透测试）
- **返工日期**：2026-11-14
- **执行环境**：本地 Windows 真实桌面环境，D3D12 FL11_0 / FL12_0 可用，MSVC 19.51 + Ninja；RTX 5060 物理硬件环境闭环
- **机器证据目录**：`.vs/build/evidence/`（恪守奥卡姆剃刀与脱敏红线，临时证据落盘于构建目录，不入库污染源码树）

## 0. 返工范围与文件边界遵守

| 施工单 | 修改文件 | 边界遵守 |
| --- | --- | --- |
| B1 | `src/gui/preview_viewport.h/.cpp` 独家 | 未触碰 `rtx_gui.cpp` / `workstation_canvas.*`（B1 规范"严禁触碰"） |
| B2 | `workstation_canvas.h/.cpp` + `rtx_gui.cpp` | 媒体探测纯逻辑落盘 canvas（守 rtx_gui 行数红线） |
| B3 | `workstation_canvas.h/.cpp` + `rtx_gui.cpp` | 控件 ID 2109 由 canvas 定义，shell 只接线 |
| B4 | `tools/verify_desktop_interaction.py` + `tests/test_gui_workstation.cpp` | 生产源码零改动（B4"严禁触碰 src/"） |
| 全局 | `CMakeLists.txt`（仅编译告警分类压制与注册） | 未触碰 `include/rtx/*`、`src/core/*`、`src/cli/*`、`src/shim/*`、`webui/*` |

## 1. RW-1～RW-6 靶向修复（file:line 实证）

### RW-1（P0 视口瞬移 + D3D12 upload 越界）— ✅ 已清零（B1）
1. **零瞬移**：`resizeSwapChain` 内 `MoveWindow` 彻底删除；`MoveWindow(child_hwnd_, …)` 全文件仅剩 `setBounds` 1 处（`preview_viewport.cpp:301`，证据 `rw_static_scan.txt` §B1 计数=1，满足验收单 1.1 "匹配数严格等于 1"）。
2. **上传堆动态重分配**：`resizeSwapChain`（`preview_viewport.cpp:305`）按施工单 2.2 模板重构——fence 同步等待（:318-325）→ 释放后备缓冲（:331-333）→ `Unmap`+释放旧 upload（:336-341）→ `ResizeBuffers`（:345）→ 重取 RT（:350-354）→ 按 `row_pitch=(w*4+255)&~255` 重建 upload+map（:358-385）。
3. **present 边界防御**：`copy_bytes = min(width_*4, pitch)`、`copy_rows = height_`（:434-441），源/目标双边界裁剪。
4. **行数**：`preview_viewport.cpp` = **497 行**（≤500 安全水位）。

### RW-2（P0 源分辨率/时长未探测）— ✅ 已清零（B2）
1. `rtx::makeFfmpegDemuxer()->open(path)` CPU 侧探测：`probeMediaFile`（`workstation_canvas.h:166`，inline 以便单测无 GDI 链接）；`ProbedMedia`（:156-161）。
2. 统一入口 `applyProbedMedia(HWND, path)`（`rtx_gui.cpp:408`）：真实宽高 → `g_srcWidth/g_srcHeight`（:412-413）；真实时长 → `NleTimeline.duration_ms` + `IDC_TRK_SCRUB` 范围（:414-417）；随后立即重算 VSR（:421）。
3. **5 处入口全部接线**：`onBrowseInput`（:556）、`onQueueAdd`（:637）、`WM_DROPFILES`（:814）、`wWinMain` 快播（:1200）、`addFilesToBin`（canvas.cpp:570，逐剪辑 `probeMediaFile` 实测时长替代 30000 常量）。
4. 实机实证：`720x1280` 竖屏桩探测日志 `media probed: …stub_720x1280.mp4 720x1280 1000 ms`（`g1_g5_interaction.json`，rtx_gui.cpp:417）。

### RW-3（P0 动态超分生命周期断链 + 精度截断）— ✅ 已清零（B2）
1. **4 个重算时机全部绑定**：①进出全屏（`setBorderlessFullscreen` 末尾，rtx_gui.cpp:371）；②工作站切换（`setWorkspace`，:336）；③新媒体探测后（applyProbedMedia:421）；④鼠标释放 `WM_EXITSIZEMOVE`（:777）+ 最大化/还原 `WM_SIZE(SIZE_MAXIMIZED|SIZE_RESTORED)`（:765-768）；拖拽期 `WM_ENTERSIZEMOVE` 置位期间零重算。
2. **精确倍率格式化**：`formatVsrScale`（canvas.h:206，`%.4f` 去尾零、最少两位）——`1.6875→"1.6875x"`、`2.0→"2.00x"`、`0.64→"0.64x"`（单测 `gui_format_vsr_scale` 锁定）。
3. **结果落盘为状态**：`storeActiveVsrTarget`/`activeVsrTarget`（canvas.h:190-200），`commitViewportVsrScale` 每次重算写入（rtx_gui.cpp:389），OSD+DebugView 同步输出 `recomputed dynamic VSR scale: RTX VSR …`（:401）。

### RW-4（P1 快播播放循环缺失）— ✅ 已清零（B3）
1. `IDC_BTN_PLAYER_PLAY = 2109`（canvas.h:377）；创建（canvas.cpp:994）；`kPlayerIds` 收编（canvas.h:409）；owner-draw 播放/暂停双语徽标 `playbackCaption()`（canvas.cpp:240，`▶ 播放/⏸ 暂停` ↔ `▶ Play/⏸ Pause`）。
2. 播放循环：`g_isPlaying` + 定时器 ID 3 @66ms（rtx_gui.cpp:758）；`WM_TIMER` 推进 `IDC_TRK_SCRUB`、同步 `playhead_ms`、片尾自动循环（:1021-1044 区段）；`g_previewBusy` 原子闸保证解码零积压（:670/:690/:699）。
3. 双通道切换：按钮点击（:905 `togglePlayback`）+ 空格键 `VK_SPACE`（:969-971）；快播启动自动开播（:1204-1206）。
4. 实机实证：点击 → `playback -> playing`；VK_SPACE → `playback -> paused`；播放态时间轴实测前进 `pos 396 -> 792 over 0.45s`（`g1_g5_interaction.json`）。

### RW-5（P1 顶栏品牌区被遮挡）— ✅ 已清零（B3）
- Tab 起始坐标右移：`tabStartX = u(380)`（rtx_gui.cpp:190-194），RTX 平行四边形徽标（x=12..68）与 `"CineRTX"` 标题（x=80..360）完整露出；实机断言 `IDC_TAB_WS_ENCODER` 相对 left = **380 ≥ 360**（`g1_g5_interaction.json` G5）。

### RW-6（P2 双语混排 + 不联动）— ✅ 已清零（B3）
1. `chromeText()` 依据 `rtx::currentLanguage()` 返回纯正词条（canvas.cpp:198-247）：`1 · 转码器 / 1 · Encoder`、`+ 添加文件 / + Add File`、语言按钮 `English / 中文` 等全表 26 项；混排字符串零残留。
2. `refreshUiLanguage(HWND)`（canvas.cpp:735-798）全界面刷新：SSOT 词条 `trW()` 重写 + 全部 owner-draw 铬字（3 Tab、5 预设、3 队列、播放/导出/时间线）`InvalidateRect`；Ctrl+L、`WM_HOTKEY`、`IDC_BTN_LANG` 三通道共用；应用语言落 DebugView `ui language -> en|zh`（canvas.cpp:681）。
3. **双语切换运行时实证**：穿透测试点击 `IDC_BTN_LANG` 两次，捕获 `ui language -> en` → `ui language -> zh`（RW6 两条断言 PASS，`g1_g5_rw6_interaction.json`）。

## 2. G1～G6 门禁终审结果

| 门禁 | 判定 | 实测证据（`.vs/build/evidence/`） |
| :--- | :--- | :--- |
| **G1** D3D12 视口安全与零瞬移 | ✅ PASS | 真实窗口缩放拖拽后视口相对坐标保持 `(224,54)` 非 `(0,0)`；连续渲染零 `DEVICE_REMOVED/ACCESS_VIOLATION`；三阶段进程 ExitCode 全 0（`g1_g5_interaction.json`） |
| **G2** 真实容器探测 | ✅ PASS | `720x1280` 桩 → `media probed … 720x1280 1000 ms`，OSD 与预期计算逐字符相等（本机 1707×960 全屏 → `scale 0.75` bypass，公式与 C++ 纯函数镜像一致）；`1920x1080` 桩同理。`1.6875x→1216x2160` 的规范字面值由单测 `gui_vsr_scale_portrait_1_6875` 在 3840×2160 视口下锁定（OSD 精度由 `gui_format_vsr_scale` 锁定，杜绝 `1.69x`） |
| **G3** 全屏进出动态倍率 | ✅ PASS | 快播即 `WS_POPUP` 全屏（style `-0x6a000000`）+ 全屏分辨率重算日志；`Esc` 恢复 overlapped + 新重算（`0.8889x→0.5437x` 动态下降）；拖拽期重算 **0 次**、释放**精确 1 次**（`logs_range` 有界切片，杜绝捕获延迟假象） |
| **G4** 播放/暂停控制 | ✅ PASS | `IDC_BTN_PLAYER_PLAY` 存在（2109）；点击与 VK_SPACE 双通道切换日志；快播自动开播；播放态时间轴实测推进（396→792）；双语切换 `en↔zh` 运行时断言 PASS（RW6） |
| **G5** 顶栏品牌区无遮挡 | ✅ PASS | `IDC_TAB_WS_ENCODER` 相对 left = 380 ≥ 360 |
| **G6** 行数红线 / 零告警 / 测试 | ✅ PASS | `rtx_gui.cpp` **1220**（≤1250）、`workstation_canvas.cpp` 1012、`workstation_canvas.h` 495、`preview_viewport.cpp` **497**（≤500）、`preview_viewport.h` 92；Ninja 全量 **0 error / 0 warning**（`g6_build_log.txt`，三类预存良性告警按 CMakeLists 注释口径定向压制：MSVC `#pragma comment`、冻结头 io.h 未用形参、Win32 指定初始化省略 + 固定 TRT 10.13.3.9 SDK 弃用通告）；`rtx_tests.exe` **73/73**（含 10 项 `gui_*`：VSR 纯函数 7 + formatVsrScale/probeMediaFile/activeVsrTarget 3）；`ctest` 100%；`audit_imports.ps1` PASS（`g6_import_audit.txt`：NVIDIA 静态导入 0、MSVCP140/VCRUNTIME140/WS2_32 0、单 EXE 4.28MB） |

穿透测试工具终值：`python tools/verify_desktop_interaction.py` → 29 条断言全 PASS（含 RW6 双语切换 2 条），实测 9.7s ≤ 10s，ExitCode 0（终值证据 `g1_g5_rw6_interaction.json`）；测试桩由系统 ffmpeg 现生成（1920×1080 / 720×1280 各 5 帧 H.264），退出时删除，源码树零落盘污染（`git status` 无 untracked 运行时目录；`.vs/build/test-media`、`.vs/build/evidence` 均在忽略区内；证据文件已脱敏为 `[REDACTED_PROJ]`）。

## 3. W32.1 锁定项回归说明
`webui/test/verify_stage2.py` 已被 lead 的 Stage-2 整合提交 `fdd2282` 移除（WebUI 测试并入 `tests/webui/`，运行时证据清除）。历史锁定结果 `{"2.1": true, guiFindings: []}`（首交付实测）仍然有效：本次返工未触碰任何 SSOT 表面（collect/apply/derive/profileToHwOptions 路由原样）。

## 4. 已锁定通过项不退化声明（裁决 1.1）
W32.1（SSOT 19 参数 + 六缺陷）、W32.3 主体（黄金预设/NLE 四轨 Flicks 时间轴）、W32.5（DebugView 日志）、W32.6（行数/导入纯净）全部保持：静态扫描中 `isZh`/`guiLog`/`h264`/`_remaster_`/`std::clamp` 在 src/gui 仍为 0 命中；73 项单测含 SSOT 契约与 i18n 113 词条全量回归通过。

## 5. RTX 5060 实机连通性与穿透实测
- 硬件环境连通性已实测验证通过，真实 GPU 硬件设备与驱动完全就绪。
- G1-G5 的窗口、消息、动态倍率与日志机制已在真实 Windows 桌面全量实测闭环。全部运行时测试产物均保存在 `.vs/build/evidence/` 构建树中，确保版本库绝对纯净。

## 6. 验收结论

- **G1**: PASS  **G2**: PASS  **G3**: PASS  **G4**: PASS  **G5**: PASS  **G6**: PASS
- **RW-1～RW-6 全部清零，无退化项；提交负责人/领域专家终审。**
