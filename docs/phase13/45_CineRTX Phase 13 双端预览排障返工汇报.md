# 45 CineRTX Phase 13 双端预览排障返工汇报

- **汇报编号**：Phase 13-45（响应 `39` 号委托书、`40` 号专家排障方案及 `41`–`44` 号修复施工单）
- **变更文件数**：6 个（旧版均已外置归档至 `work/_CineRTX_bak_archive/*.p13r6bak`，仓库内零 `.bak` 残留）
- **Git 状态**：严格遵守红线，未执行任何 `git commit` 或 `git push`

---

## 一、逐波次修复清单与精确行号实证（`文件:L行号`）

### Fix Wave 1-A（41 号施工单）：播放器单线垂直截断定界与三段链路防御修复

- [x] **段 2 行字节跨度与 SRV 边界契约防御**：在 `PreviewViewport::presentShared()` 强制校验 `pitch >= src_width * 4u && (pitch % 4u) == 0u`，杜绝字跨度/字节跨度混淆或 SRV `Buffer.NumElements` 不足引发的越界补零截断（`src/gui/preview_viewport.cpp:L631`）。
- [x] **段 3 Win32 窗口裁剪样式与 Z-Order 隔离**：
  - `WM_CREATE` 显式为主窗口附加 `WS_CLIPCHILDREN`，子窗口 `child_hwnd_` 保留 `WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN`（`src/gui/rtx_gui.cpp:L625`、`src/gui/preview_viewport.cpp:L176`）。
  - `PreviewViewport::setBounds()` 统一使用 `SetWindowPos(..., HWND_TOP, ..., SWP_NOACTIVATE)` 确保视口不被同级控件遮挡（`src/gui/preview_viewport.cpp:L540`）。

### Fix Wave 1-B（42 号施工单）：转码预览双侧独立色彩空间与 BT.2390 色调映射

- [x] **零新增命名类型双侧色彩位掩码契约**：在 `preview_viewport.h` 定义 `kPreviewLeftPq` / `kPreviewLeftHlg` / `kPreviewLeftBt2020` / `kPreviewRightPq` / `kPreviewRightHlg` / `kPreviewRightBt2020`（`constexpr std::uint32_t`，+0 具名类型）及双侧重载 `presentShared(..., color_flags, left_max_nits, right_max_nits, ticket)`（`src/gui/preview_viewport.h:L31`、`src/gui/preview_viewport.h:L76`）。
- [x] **废除 `presentTranscodePreview` 硬编码 `input_is_pq=true, 100.0f`**：改为读取当前活跃配置 `guiProfile(app)` 的 `peak_nits`：
  - 关闭 TrueHDR（`peak_nits == 0`）时下发 `color_flags = 0u`、`left_max_nits = right_max_nits = 100.0f`，SDR 双侧 1:1 直通，根治 4.24 倍中灰抬升与发灰起雾；
  - 开启 TrueHDR 时下发 `color_flags = kPreviewRightPq | kPreviewRightBt2020`、`left_max_nits = 100.0f`、`right_max_nits = max(prof.peak_nits, 203)`，实现左侧原生 SDR + 右侧 PQ BT.2020 双侧独立解码（`src/gui/workstation_canvas.cpp:L1123`）。
- [x] **SDR 交换链色彩空间强制对齐**：在 `createCompositionSwapChain()` 当 `!hdr_output_` 时显式调用 `swap_chain_->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709)`（`src/gui/preview_viewport.cpp:L421`）。
- [x] **升级 `preview_tone_map_cs.hlsl`（BT.2390 Hermite EETF + HLG + 双侧独立 EOTF）**：利用既有 12 DWORD 根常量槽位（零 Root Signature 变更）传入 `leftMaxNits`、`colorFlags`、`rightMaxNits`，实装 `pqToNits()`、`hlgToNits()`、`bt2390ToSdrNits()` 与 `bt2020ToBt709()`，分屏左右两侧独立解码并保留 NVIDIA 绿分割线（`src/gui/shaders/preview_tone_map_cs.hlsl:L96`、`src/gui/shaders/preview_tone_map_cs.hlsl:L154`）。

### Fix Wave 2-A（43 号施工单）：组合交换链几何状态机与 DComp 画布零溢出

- [x] **解耦期望尺寸与物理交换链尺寸（零新增类型）**：在 `PreviewViewport` 内新增私有 POD 标量 `target_width_` / `target_height_` 及 `ComPtr<IDCompositionRectangleClip> clip_`（`src/gui/preview_viewport.h:L115`、`src/gui/preview_viewport.h:L131`）。
- [x] **`setBounds()` 与首帧建链尺寸零丢弃**：`setBounds()` 读取真实 `GetClientRect` 刷新 `target_width_`/`target_height_` 并在 `swap_chain_` 已建时立即触发 `resizeSwapChain(target_width_, target_height_)`；`createCompositionSwapChain()` 建链前读取最新 `GetClientRect`，根除首帧锁定 `960x540` 缺陷（`src/gui/preview_viewport.cpp:L369`、`src/gui/preview_viewport.cpp:L553`）。
- [x] **DirectComposition 矩形硬裁剪（零溢出）**：在 `adoptDevice()` 通过 `dcomp_device_->CreateRectangleClip` 绑定 `visual_->SetClip(clip_.get())`，并在 `resizeSwapChain()` 中同步更新 `SetRight`/`SetBottom` 后提交 `dcomp_device_->Commit()`，彻底杜绝物理像素溢出遮挡左右面板（`src/gui/preview_viewport.cpp:L525`、`src/gui/preview_viewport.cpp:L599`）。
- [x] **`presentShared()` 呈现前尺寸兜底对齐**：异步帧到达前若 `target_width_ != width_ || target_height_ != height_` 自动补齐 `resizeSwapChain()`（`src/gui/preview_viewport.cpp:L643`）。
- [x] **`WM_CREATE` 补齐 `commitViewportVsrScale(hwnd)` 与 `WM_SIZE` 最小化保护**：在 `WM_CREATE` 布局完成后紧接调用 `commitViewportVsrScale(hwnd)`，并在 `WM_SIZE` 入口拦截 `SIZE_MINIMIZED`（`0x0`）防 D3D12 设备丢失（`src/gui/rtx_gui.cpp:L628`、`src/gui/rtx_gui.cpp:L645`）。

### Fix Wave 3-A（44 号施工单）：确定性防回归单元测试补充

- [x] **新增 `computeAspectFitDrawRect` 与双侧位掩码单测**：在 `preview_viewport.h:L39` 实装等比居中 `computeAspectFitDrawRect()`，并在 `tests/test_gui_workstation.cpp:L360` 追加 `gui_aspect_fit_draw_rect_and_dual_color_flags` 覆盖 16:9（1000x700）、9:16（720x1280）与 21:9（2560x1080）三类画幅及位掩码契约。

---

## 二、宪法静态门禁复验

- [x] **`AuditTypes` 零新增命名类型**：`34 / 35`（严格 +0 具名 `struct`/`class`）。
- [x] **语法括号与圆括号平衡审计**：全部 6 个修改文件（含 `.hlsl`）`braces=0, parens=0`。
- [x] **`AuditImports` 零新增静态 DLL**：`IDCompositionRectangleClip` 通过既有动态加载的 `dcomp_device_` COM 接口获取，未新增任何静态 `.lib`。
- [x] **`AuditPrivacy` 零违规**：本汇报与所有修改文件不含任何本地路径或外部链接。

---

## 三、待宿主机真机复验项（诚实披露）

以下需在 Windows 11 + MSVC 19.51 + RTX 5060 宿主机真机执行的项如实保持 `[ ]`：

- [ ] **宿主机 `.\build.bat` 编译（含 `fxc` 编译新版 `preview_tone_map_cs.hlsl`）**：确认 `0 Error, 0 Warning`，退出码 `0`。
- [ ] **宿主机 `rtx_tests.exe` 全套实跑（含新增 `gui_aspect_fit_draw_rect_and_dual_color_flags`）**：确认 `151/151 PASS (0 FAILED)` 及五重静态门禁全绿。
- [ ] **双端真机 GUI 肉眼与截屏验收（AC-2 / AC-3 / AC-4）**：验证 Player 多画幅满框居中无顶部单线截断、Encoder 关闭/开启 TrueHDR 双侧无发灰起雾、以及首帧/缩放/最小化还原/DPI 切换下画布零溢出。
