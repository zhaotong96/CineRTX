# CineRTX 播放器与转码预览异常专家排障委托书

- **基线版本**：Commit `d18b469`（Wave A 本地基线：`150/150` 单测通过、`AuditImports` GUI=4 / HEADLESS=2、`AuditTypes` 34/35）
- **委托目标**：请领域专家针对主程序 GUI 实测暴露的 2 项预览/播放渲染异常进行根因定界，明确修复方案与验收边界（不突破 `AuditTypes <= 35` 与 `AuditImports <= 4` 约束）。

---

## 一、待排障问题清单

### 问题 1（严重）：播放器仅顶部数像素高度有画面（呈单线状）

- **实测现象**：在 Player 工作区加载媒体并播放时，视频预览区仅顶部数像素高度显示画面内容，其余区域不可见或异常。
- **已定位相关代码路径**：
  1. 播放器布局与视口尺寸：`src/gui/rtx_gui.cpp` `applyWorkspaceLayout()`（`isPlayer` 分支 `previewViewport->setBounds(0, bodyTop, width, std::max(u(80), vpH))`）及 `WM_CREATE` 初始化流程。
  2. 无头播放预览写入：`src/shim/ngx_bridge/bridge_exports.cpp` `rtx_ngx_bridge_playback_pack()`（调用 `rtx_ngx::launchPreprocessBuffer` 写入 `previewRingDevicePtr(slot)`，传入 `previewRingRowBytes()`、`width`、`height`）。
  3. 视口借用与着色器采样：`src/gui/workstation_canvas.cpp` `presentPlaybackFrame()` / `presentSlotToViewport()` -> `src/gui/preview_viewport.cpp` `PreviewViewport::presentShared()` -> `src/gui/shaders/preview_tone_map_cs.hlsl`（`gPreview[clamped.y * pitchWords + clamped.x]`）。
- **需专家裁决的排查点**：
  - 核查 `MediaPlaybackSession` 初始化 `rtx_ngx_bridge_create`（`reserved[0] = 1`）时创建的 3-slot 共享预览环尺寸/步长（`previewRingRowBytes()`）与 `launchPreprocessBuffer` 核函数网格/步长、以及 `borrow_preview` 返回的 `out_pitch` 是否一致；
  - 核查 `PreviewViewport::create()` / `createCompositionSwapChain()` / `adoptDevice()` 与 `child_hwnd_` 尺寸同步时序，排除 `height_` 或 DComp Visual/SwapChain 尺寸未随 `WM_SIZE`/工作区切换刷新导致的高度截断。

---

### 问题 2（中度）：转码预览画面呈灰蒙蒙的 SDR，且未随画布缩放（超出画布边界）

- **实测现象**：Encoder 转码时预览可正常播放运动画面，但画面整体发灰（对比度/色域/传递函数异常），且预览区域尺寸未自适应画布，直接超出中间画布边界。
- **已定位相关代码路径**：
  1. 传递函数与峰值亮度硬编码：`src/gui/workstation_canvas.cpp` `presentTranscodePreview()`（第 1108–1111 行）硬编码传入 `960, 540, /*input_is_pq=*/true, 100.0f`：
     - 当 `preview_tone_map_cs.hlsl` 左侧分屏（`leftIsOriginal`）或输入本身为 SDR / 未经 PQ 编码时，`input_is_pq=true` 与 `maxNits=100.0f` 会导致原图侧与调色侧呈现错误曲线（log 灰片感或过暗发灰）；
     - 即使开启 TrueHDR，硬编码 `100.0f` 也未对齐 `RtxNgxRemasterOutput::frame_max_nits` 与当前输出色彩空间状态。
  2. 画布缩放与边界溢出：`src/gui/rtx_gui.cpp` `WM_CREATE`（第 615–626 行）仅创建 `960x540` 视口并调用 `applyWorkspaceLayout(hwnd)`，未同步调用 `commitViewportVsrScale(hwnd)` / `resizeSwapChain()`；且 `PreviewViewport` 使用 `CreateSwapChainForComposition`，当 `swap_chain_` 尺寸（`width_ x height_`）与 `child_hwnd_` 实际矩形不一致、或工作区切换/窗口大小变化后未触发 `resizeSwapChain()` 时，画面会按物理尺寸溢出画布。
- **需专家裁决的排查点**：
  - 明确转码预览环 `preview_ring` 中 `preview_rgba10_ptr` 的真实色彩空间契约（左侧原图 vs 右侧 Remaster/TrueHDR，何时为 SDR、何时为 BT.2020 PQ，以及 `max_nits` 的传递方式）；
  - 明确 `WM_CREATE`、工作区切换（Encoder / Player / NLE）与 `PreviewViewport::adoptDevice()` 后的 `resizeSwapChain()` 同步时机，以及 DComp `child_hwnd_` 裁剪/缩放策略（`WS_CLIPCHILDREN` / SwapChain 尺寸与 `drawSize` 映射）。

---

## 二、期望专家交付内容

1. 上述 2 个问题的**确定性根因定位**（精确到源文件、函数与参数/状态契约）。
2. **最小充分修复方案**（不得新增命名类型以保留 `AuditTypes` 34/35 余量，不得引入新静态导入 DLL）。
3. **可复现的验证标准**（Player 模式满框正常渲染、Encoder 转码预览色彩正常且严格贴合中间画布不溢出）。
