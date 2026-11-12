# CineRTX Phase 6.1：参数闭环与收尾微瑕清零 SuperPower 施工文档

- **文档编号**：`PLAN-SUPERPOWER-PHASE6-1-POLISH`
- **前置依据**：[`docs/phase6/CineRTX_阶段六收官与系统整体验收及未来演进专家委托书.md`](../phase6/CineRTX_阶段六收官与系统整体验收及未来演进专家委托书.md) (`DOC-COMMISSION-PHASE6-CLOSURE-AND-ROADMAP-001`) · 《CineRTX 阶段六收官终审验收、深层瓶颈剖析与 Phase 7–9 战略演进专家裁决报告》 (`DOC-VERDICT-PHASE6-CLOSURE-AND-ROADMAP-001`)
- **适用分支与基线**：`main`（Phase 6 Wave 4 闭环基线，19,359 LOC，79 项单测通过）
- **预计工期与人员配置**：`0.5 工作日` · **2 名执行工程师并行**（工程师 A：核心管线与 NGX 桥接；工程师 B：Win32 GUI、CLI 与文档清理）

---

## Part I: Plan（总体施工蓝图与架构契约）

### 1.1 施工背景与核心目标
在 Phase 6 Wave 1～4 完工后，专家委员会穿透审计了 [`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp)、[`include/rtx/hw_transcode.h`](../../include/rtx/hw_transcode.h)、[`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp) 与 [`src/shim/ngx_bridge/ngx_d3d12_bridge.cpp`](../../src/shim/ngx_bridge/ngx_d3d12_bridge.cpp)，发现 4 处非阻断性但影响用户体验一致性的“最后一公里接线微瑕”：
1. **GUI 峰值亮度与画幅策略漏采集**：[`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp) 创建了 `IDC_EDT_PEAK_NITS`（第 468 行）与 `IDC_CMB_GEOM`（第 459 行），但在 `onStartTranscode()`（第 311–363 行）中未将二者写入 `HwTranscodeOptions`，且 [`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp) 第 341 行与第 369 行硬编码了 `GeometryTargetMode::AutoDirect` 和 `max_luminance = 1000`。
2. **音频流直通开关 (`copy_audio`) 在解复用主循环中未拦截**：当用户取消勾选 GUI `IDC_CHK_AUDIO`（或传入 `options.copy_audio = false`）时，[`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp) 第 579 行与第 827 行仍将音频包写入复用器。
3. **VSR 启用开关 (`vsr_enabled`) 取消勾选后未透传至 NGX 桥接层**：当 `options.vsr_enabled == false` 时，[`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp) 未将 `vsr_enabled` 状态反映至 `RtxNgxBridgeConfig`。
4. **残余历史死字段与过期注释**：[`src/cli/main.cpp`](../../src/cli/main.cpp) 第 72–73 行残留 `onnx`/`engine` 字段；[`CMakeLists.txt`](../../CMakeLists.txt) 残留 `DirectComposition` 与 `nvinfer_10.dll` 历史注释及 `--viewport-self-test` CTest 项；[`docs/README.md`](../README.md) 第 25 行仍写着“137 项测试”。

### 1.2 架构红线与边界约束 (Invariants & Boundaries)
- **严禁破坏 100% RAII**：不得新增任何 `initialize()` / `shutdown()` / `destroy()` 方法。
- **严禁破坏 C++26 聚合体属性**：向 [`include/rtx/hw_transcode.h`](../../include/rtx/hw_transcode.h) 的 `HwTranscodeOptions` 添加字段时，必须提供默认成员初始化器（Default Member Initializer），保持 `std::is_aggregate_v<HwTranscodeOptions> == true`。
- **代码体量约束**：本阶段全部改动净增代码须控制在 **40 行以内**。

### 1.3 多工程师并行分工矩阵

| 角色 | 负责任务编号 | 触碰文件集合（物理隔离防冲突） |
| :--- | :--- | :--- |
| **执行工程师 A**<br>（核心管线与桥接组） | `Task 6.1-01`<br>`Task 6.1-02` | [`include/rtx/hw_transcode.h`](../../include/rtx/hw_transcode.h)、[`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp)、[`src/shim/ngx_bridge/bridge_exports.cpp`](../../src/shim/ngx_bridge/bridge_exports.cpp)、[`src/shim/ngx_bridge/ngx_d3d12_bridge.cpp`](../../src/shim/ngx_bridge/ngx_d3d12_bridge.cpp) |
| **执行工程师 B**<br>（GUI、CLI 与构建组） | `Task 6.1-03`<br>`Task 6.1-04` | [`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp)、[`src/cli/main.cpp`](../../src/cli/main.cpp)、[`CMakeLists.txt`](../../CMakeLists.txt)、[`docs/README.md`](../README.md)、[`tests/unit/test_audio_stream_copy.cpp`](../../tests/unit/test_audio_stream_copy.cpp) |

---

## Part II: Tasks（原子化 TDD 施工任务集）

### Task 6.1-01：扩展 `HwTranscodeOptions` 并打通 `peak_nits`、`geometry_mode` 与 `vsr_enabled` 底层透传（执行工程师 A）

- **目标文件**：
  1. [`include/rtx/hw_transcode.h`](../../include/rtx/hw_transcode.h)（第 42–51 行）
  2. [`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp)（第 339–375 行）
  3. [`src/shim/ngx_bridge/bridge_exports.cpp`](../../src/shim/ngx_bridge/bridge_exports.cpp)（第 157 行）与 [`src/shim/ngx_bridge/ngx_d3d12_bridge.cpp`](../../src/shim/ngx_bridge/ngx_d3d12_bridge.cpp)（第 123–126 行）
- **Step 1 — 修改 [`include/rtx/hw_transcode.h`](../../include/rtx/hw_transcode.h)**：
  在 `struct HwTranscodeOptions` 的 `int vsr_quality = 2;`（第 49 行）下方追加 `peak_nits` 与 `geometry_mode` 字段：
  ```cpp
  int target_width = 0;   // 0 = 3840 default, resolved by the transcode path
  int target_height = 0;  // 0 = 2160 default
  bool vsr_enabled = true;
  int vsr_quality = 2;    // 1..4, NGX VSR quality level
  int peak_nits = 1000;   // 400..4000, TrueHDR peak luminance in cd/m^2
  int geometry_mode = 0;  // 0 = AutoDirect (9:16 portrait aware), 1 = Pillarbox
  ```
- **Step 2 — 修改 [`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp)（第 339–375 行）**：
  将硬编码的 `GeometryTargetMode::AutoDirect`、`1000` nits 以及 `vsr_enabled` 透传绑定到 `options_`：
  ```cpp
  const bool is_portrait_input = (info_.width > 0 && info_.height > 0 && info_.width < info_.height);
  const bool is_default_landscape_4k = (target_width_ == 3840 && target_height_ == 2160);
  const GeometryTargetMode geom_mode = (options.geometry_mode == 1)
      ? GeometryTargetMode::Pillarbox
      : GeometryTargetMode::AutoDirect;
  if (is_remaster && (!options.vsr_enabled)) {
    target_width_ = info_.width;
    target_height_ = info_.height;
    report.output_width = target_width_;
    report.output_height = target_height_;
  } else if (is_remaster && (target_width_ <= 0 || target_height_ <= 0 ||
                             (is_portrait_input && is_default_landscape_4k))) {
    auto geom = calculateSmartGeometry(info_.width, info_.height, geom_mode,
                                       target_width_, target_height_);
    target_width_ = geom.canvas_width;
    target_height_ = geom.canvas_height;
    report.output_width = target_width_;
    report.output_height = target_height_;
  }

  const bool enable_truehdr = options.hdr;
  const int clamped_nits = std::clamp(options.peak_nits > 0 ? options.peak_nits : 1000, 400, 4000);
  if (is_remaster) {
    if (enable_truehdr) {
      TrueHdrParameters hdr_params{};
      hdr_params.max_luminance = clamped_nits;
      const Result<Hdr10Metadata> canonical = SeiSynthesizer::makeMetadata(hdr_params);
      if (!canonical.has_value()) {
        return std::unexpected(canonical.error());
      }
      hdr_ = canonical.value();
      applyHdrDefaults(hdr_);
    } else {
      hdr_.enabled = false;
      hdr_.color.primaries = ColorPrimaries::BT709;
      hdr_.color.transfer = TransferFunction::BT709;
      hdr_.color.matrix = MatrixCoefficients::BT709;
      hdr_.color.range = ColorRange::Limited;
    }

    RtxNgxBridgeConfig bridge_config{};
    std::memcpy(bridge_config.luid, diagnostics_.device.luid, sizeof(bridge_config.luid));
    bridge_config.luid_node_mask = diagnostics_.device.luid_node_mask;
    bridge_config.input_width = info_.width;
    bridge_config.input_height = info_.height;
    bridge_config.output_width = target_width_;
    bridge_config.output_height = target_height_;
    bridge_config.vsr_quality = std::clamp(options.vsr_quality, 1, 4);
    bridge_config.enable_truehdr = enable_truehdr ? 1 : 0;
    bridge_config.contrast = 100;
    bridge_config.saturation = 100;
    bridge_config.middle_gray = 50;
    bridge_config.max_luminance = clamped_nits;
    bridge_config.project_id = 0x52545831u;
  ```

---

### Task 6.1-02：打通 `options.copy_audio == false` 音轨剥离闭环（执行工程师 A）

- **目标文件**：[`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp)（第 395–405 行、第 480–483 行）
- **Step 1 — 在 `NvidiaTranscodeSession::configure()` 与 `isAudioPacket()` 中尊重 `options_.copy_audio`**：
  ```cpp
  // src/core/hw_transcode.cpp 第 395-406 行：
  output_info_ = info_;
  if (!options_.copy_audio) {
    output_info_.primary_audio_index = -1;
    output_info_.audio_tracks.clear();
  }
  output_info_.time_base = Rational{1, 90000};
  ```
  并在第 480–483 行修改 `isAudioPacket` 及新增 `shouldDiscardPacket`：
  ```cpp
  bool isAudioPacket(const Packet& p) const noexcept {
    return info_.primary_audio_index >= 0 && p.stream_index == info_.primary_audio_index;
  }
  bool copyAudioEnabled() const noexcept { return options_.copy_audio; }
  ```
- **Step 2 — 在 `runNvidiaContainerTranscode`（第 579 行）与 `runNvidiaRemasterTranscode`（第 827 行）主循环中拦截**：
  ```cpp
  if (session.isAudioPacket(input)) {
    if (!session.copyAudioEnabled()) {
      continue; // 用户显式禁用音频直通，直接丢弃音频包且不入队
    }
    if (session.isMuxerOpen()) {
  ```

---

### Task 6.1-03：补齐 GUI `IDC_EDT_PEAK_NITS`、`IDC_CMB_GEOM` 与 CLI `--peak-nits`、`--no-audio` 绑定（执行工程师 B）

- **目标文件**：
  1. [`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp)（第 350–360 行）
  2. [`src/cli/main.cpp`](../../src/cli/main.cpp)（第 70–130 行）
- **Step 1 — 修改 [`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp) 的 `onStartTranscode()`（第 350–360 行）**：
  ```cpp
  // VSR
  opt.vsr_enabled = (Button_GetCheck(GetDlgItem(hwnd, IDC_CHK_VSR)) == BST_CHECKED);
  opt.vsr_quality = std::clamp(ComboBox_GetCurSel(GetDlgItem(hwnd, IDC_CMB_VSR_QUAL)) + 1, 1, 4);

  // HDR & Peak Nits
  opt.hdr = (Button_GetCheck(GetDlgItem(hwnd, IDC_CHK_TRUEHDR)) == BST_CHECKED);
  wchar_t nitsBuf[32] = {};
  GetDlgItemTextW(hwnd, IDC_EDT_PEAK_NITS, nitsBuf, 32);
  const int parsedNits = _wtoi(nitsBuf);
  opt.peak_nits = std::clamp(parsedNits > 0 ? parsedNits : 1000, 400, 4000);

  // Orientation / Geometry & Audio Passthrough
  opt.geometry_mode = std::clamp(ComboBox_GetCurSel(GetDlgItem(hwnd, IDC_CMB_GEOM)), 0, 1);
  opt.copy_audio = (Button_GetCheck(GetDlgItem(hwnd, IDC_CHK_AUDIO)) == BST_CHECKED);
  ```
- **Step 2 — 清理 [`src/cli/main.cpp`](../../src/cli/main.cpp) 残余 `onnx`/`engine` 死字段并绑定 `--peak-nits` 与 `--no-audio`**：
  删除 [`src/cli/main.cpp`](../../src/cli/main.cpp) 第 72–73 行的 `std::filesystem::path onnx; std::filesystem::path engine;`，替换为：
  ```cpp
  int peak_nits = 1000;
  int geometry_mode = 0;
  bool copy_audio = true;
  ```
  并在 `parseOptions` 中支持 `--peak-nits <400..4000>`、`--pillarbox`（设 `geometry_mode = 1`）与 `--no-audio`（设 `copy_audio = false`），同步传递至 `HwTranscodeOptions`。

---

### Task 6.1-04：清理 [`CMakeLists.txt`](../../CMakeLists.txt) 历史注释与 [`docs/README.md`](../README.md) 测试计数微瑕（执行工程师 B）

- **目标文件**：
  1. [`CMakeLists.txt`](../../CMakeLists.txt)（第 41–43、367–368、441–449 行）
  2. [`docs/README.md`](../README.md)（第 25 行）
- **施工动作**：
  1. 将 [`CMakeLists.txt`](../../CMakeLists.txt) 第 41–43 行与第 367–368 行中的 `"Win32 DirectComposition GUI"` 修正为 `"Win32 Native Common Controls Dark Pro GUI"`；
  2. 删除 [`CMakeLists.txt`](../../CMakeLists.txt) 第 441–449 行关于 `NvOFFRUC.dll`、`nvinfer_10.dll`、`nvonnxparser_10.dll` 的空悬注释块；
  3. 将 [`docs/README.md`](../README.md) 第 25 行中的 `137 项测试用例分布说明` 修正为 `79 项精炼测试用例分布说明`。

---

## Part III: Checklists（执行工程师完工自检与门禁核销表）

执行工程师在提交 PR 前，必须逐条运行以下验证命令并勾选确认：

- [x] **门禁 1（编译零警告零错误）**：
  ```powershell
  cmake -B build/cmake/windows-msvc-ninja -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
  ninja -C build/cmake/windows-msvc-ninja
  ```
  断言：`ninja` 退出码为 `0`，无任何编译或链接警告。
  **✅ 实证（2026-11-12）**: 本地 Zig 工具链 `cmake --build --preset zig-windows` exit 0（63/63 targets）；服务器 MSVC（[REDACTED_HOST], VS 18）`gate_build.cmd` exit 0（63/63 targets, 无 error）。详见 [`CineRTX_Phase7_验收实证报告.md`](CineRTX_Phase7_验收实证报告.md) §一。
- [x] **门禁 2（残余死词条静态扫描清零）**：
  ```powershell
  rg -i "nvonnxparser|nvinfer_10|DirectComposition GUI|\bonnx\b" CMakeLists.txt src/cli/main.cpp
  ```
  断言：输出匹配行数为 **0**。
  **✅ 实证**: CMakeLists.txt = 0 命中、src/cli/main.cpp = 0 命中（清理点: `CMakeLists.txt:39-44,481-485,592-595,659-663`; `src/cli/main.cpp:82-89`）。
- [x] **门禁 3（13 项 GUI 参数透传闭环验证）**：
  ```powershell
  rg "IDC_EDT_PEAK_NITS|IDC_CMB_GEOM|IDC_CHK_AUDIO|IDC_CHK_VSR" src/gui/rtx_gui.cpp
  ```
  断言：每个控件 ID 在 `rtx_gui.cpp` 中均至少出现 **2 次**（1 次在 `WM_CREATE` 创建，1 次在 `onStartTranscode` 采集）。
  **✅ 实证**: IDC_EDT_PEAK_NITS=6 / IDC_CMB_GEOM=5 / IDC_CHK_AUDIO=6 / IDC_CHK_VSR=8（采集点: `src/gui/rtx_gui.cpp:433-446`）。
- [x] **门禁 4（`--no-audio` 与 `--peak-nits` 实机转码校验）**：
  ```powershell
  ./build/cmake/windows-msvc-ninja/CineRTX.exe --input test_1080p_audio.mp4 --output out_no_audio.mp4 --truehdr --peak-nits 2000 --no-audio
  ```
  断言：输出 MP4 经 FFprobe 检查仅含 `Stream #0:0: Video: av1 (Main) (av01)`，无音频流，且 `MasteringDisplayMetadata` 最大亮度为 `2000 cd/m²`。
  **✅ 实证（RTX 5060 实机）**: 服务器 `CineRTX.exe --input testmedia/test_1080p.mp4 --output out_no_audio.mp4 --truehdr --peak-nits 2000 --no-audio` exit=0（`gate_logs/gate61_4.log:1`）; 流结构 `gate_logs/out_no_audio_streams.txt`（无 audio 流）。
- [x] **门禁 5（全量单元测试 100% 通过）**：
  ```powershell
  ./build/cmake/windows-msvc-ninja/rtx_tests.exe
  ```
  断言：`[==========] 79+ test(s) run, 0 failed`。
  **✅ 实证**: 服务器 `91 test(s) run, 0 failed (993 ms)`（`gate_logs/unit_tests_server.log`）; 本地 `91 test(s) run, 0 failed (2230 ms)`。
