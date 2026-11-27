# 85_CineRTX Phase 13 第二批增量返工汇报（Wave C-D 架构统一与 Static IAT 归 1 收口）

- **执行基线**：`84006c2`（批 1 验收通过 + 协调者内存流 `AVFMT_FLAG_CUSTOM_IO` `+faststart` 保护基线）
- **依据施工单**：`docs/phase13/80_waveC-D_60fps增强播放器_任意比例RIFE与D2D_IAT归1终局施工单.md`
- **增量原则**：全部 16 处修改均直接基于 `84006c2` 实时最新文件做最小增量补丁，零旧快照覆盖，完整保留 `src/gui/rtx_gui.cpp` 控制台双句柄同步、`src/io/ffmpeg_io.cpp` `AVFMT_FLAG_CUSTOM_IO` 保护、`WasapiAudioOutput` 内联、`analyze_temporal_boundary`、`preview_generation_`、`consumeNvofSceneCut`、`isPreviewEnabled()` 与批 1 新增 3 项单测。

---

## 1. Wave C：核心架构、RIFE 4-Bucket 几何护栏与 60fps 实时增强播放器

- [x] **Task C-1：4-Bucket 单 Profile RIFE 池校验与 NV12 入口几何硬断言**
  - **存量核验**：`src/shim/ngx_bridge/rife_engine.h:L20-L68` 已完整落盘 `RifeAspectBucket`（`Ultra`/`Wide`/`Square`/`Tall`）、`classifyRifeBucket()`、`rifeBucketEngineFilename()`、`rifeBucketProfileDims()` 与对称居中填充 `calculateRifeGeometry()`；`src/shim/ngx_bridge/rife_engine.cpp:L363-L386` 已具备 `getNbOptimizationProfiles() != 1` 拒绝断言与 `CU_STREAM_NON_BLOCKING` 独立流；`src/shim/ngx_bridge/rife_pack.ptx` 已具备 12 处 `testp.finite.f32` 非有限值防护。
  - **本次增量**：
    - `src/core/hw_transcode_stages.cpp:L188-L193`：在 RIFE 初始化前增加偶数维度与非零表面几何硬守卫（`RIFE NV12 geometry guard failed`），杜绝非法 NV12 表面尺寸静默透传。
    - `tests/test_rife.cpp:L19,L1109-L1130`：引入 `rife_engine.h` 并新增确定性单元测试 `rife_bucket_pool_and_geometry_contract`，覆盖 `1920x1080 (Wide)`、`1080x1920 (Tall)`、`5120x1440 (Ultra)`、`2560x2560 (Square)` 四桶分类、物理引擎文件名及 `align=128` 对称填充坐标。

- [x] **Task C-2：`<chrono>` 编译期有理数 23.976->60.000 分数相位与 WASAPI 熔断旁路（+0 具名类型）**
  - `include/rtx/playback_session.h:L262-L296`：以纯 `using`/`enum class`/`std::tuple` 零类型膨胀方式新增 `Fps23976Duration` (`std::ratio<1001, 24000>`)、`Fps60000Duration` (`std::ratio<1, 60>`)、`enum class PlaybackClockState { Normal, AudioDrift, MeltdownBypass }`、`FractionalPresentationInfo` 与无状态函数 `computeFractionalFramePhase()`（WASAPI 漂移 `> 8000 us` 标记 `AudioDrift`，`> 32000 us` 触发 `MeltdownBypass` 跳过 RIFE 插帧直接硬拷贝原帧）。
  - `src/core/playback_session_impl.h:L81-L82,L121-L124`：在既有 `MediaPlaybackSession::Impl` 原位新增 `clock_state`、`temporal_prev_valid` 与 `resetTemporalBuffers()`（`+0` 具名类型）。
  - `src/core/playback_session_video.cpp:L194-L200,L256,L358`：在 `publishTelemetry()` 实时计算 `av_drift_us` 并同步 `clock_state`（8ms 漂移 / 32ms 熔断）；在 `performSeek()` 与 `StepForward` 入口瞬时调用 `resetTemporalBuffers()` 清除历史时域状态，杜绝 Seek/单步残影。
  - `tests/test_gui_workstation.cpp:L493-L517`：新增 `playback_fractional_60fps_phase_and_wasapi_meltdown` 单元测试，验证 `23.976->60.000` 相位递进、8ms `AudioDrift` 与 32ms `MeltdownBypass`。

---

## 2. Wave D：零 `UiWidgetNode`、单源 `std::vformat` i18n、契约式 DAG 与 `Static IAT == 1`

- [x] **Task D-1 & D-3：全量动态载入 `d2d1.dll`/`dwrite.dll`/`gdi32.dll`/`user32.dll`，达成 `Static IAT == 1`**
  - `include/rtx/sys_dynload.h:L244-L996`：严格遵循 `LOAD_LIBRARY_SEARCH_SYSTEM32` 安全不变量与 `+0` 具名类型铁律，补齐 `d2d1.dll`（`d2d1CreateFactory`）、`dwrite.dll`（`dwriteCreateFactory`）、`gdi32.dll`（16 个按需 GDI 包装与 `ensureGdi32Loaded()`）以及 `user32.dll`（48 个按需窗口/消息/DPI 包装与 `ensureUser32Loaded()`）。
  - `include/rtx/raii_owners.h:L40,L65-L72`：将 `UniqueHwnd`、`UniqueBrush`、`UniqueFont`、`UniqueBitmap` 与 `makeSolidBrush()` 统一切换至 `rtx::sys::destroyWindow`、`rtx::sys::deleteObject` 与 `rtx::sys::createSolidBrush`，切断头文件级静态 `user32`/`gdi32` 符号引用。
  - `src/gui/rtx_gui.cpp:L1171-L1175`（及全文件调用改写）：完整保留 `bindParentConsoleIfNeeded()`（`L1097-L1142`）的 `SetStdHandle` 与 `std::cout.clear()`/`std::cerr.clear()` 修复；仅在无头 CLI 分支（`args.size() > 1`）返回之后调用 `rtx::sys::ensureUser32Loaded()` 与 `rtx::sys::ensureGdi32Loaded()`，确保 `--headless --probe` 运行期间进程地址空间零加载 `user32.dll` / `gdi32.dll`；全文件 `user32`/`gdi32` 调用全部收敛至 `rtx::sys::*`。
  - `src/gui/workstation_canvas.h`、`src/gui/workstation_canvas.cpp`、`src/gui/preview_viewport.cpp`：彻底替换 `workstation_canvas.cpp` 中暗含静态 `user32!SendMessageW` 展开的 6 组 `<windowsx.h>` 宏（`Button_GetCheck`、`Button_SetCheck`、`ComboBox_AddString`、`ComboBox_GetCurSel`、`ComboBox_ResetContent`、`ComboBox_SetCurSel`）为显式 `rtx::sys::sendMessageW(...)`，并将上述三文件全部 `user32`/`gdi32` 调用切换至 `rtx::sys::*`。
  - `CMakeLists.txt:L535`：移除 `target_link_libraries(CineRTX PRIVATE gdi32 user32)`，主程序 `CineRTX.exe` 静态链接收敛至仅 `kernel32.dll`。
  - `cmake/AuditImports.cmake:L47-L59`：将 `gdi32.dll` 与 `user32.dll` 纳入零容忍静态 IAT 黑名单；`-DSTRICT_NO_BCRYPT=ON` 模式下强制 `_ALLOWED_PHYSICAL = "kernel32.dll"` 且 `_MAX_PHYSICAL = 1`（过渡期 `_MAX_PHYSICAL = 2`）。

- [x] **Task D-2：零 `UiWidgetNode` 无状态命中检测、单源 `std::vformat` i18n 与契约式 DAG 校验**
  - `include/rtx/i18n.h:L15,L204-L216` & `src/core/i18n.cpp:L513-L523`：保持 `kStrIdCount == 125` 与现有 `test_i18n.cpp` 契约完全兼容，新增单源 `std::vformat` 接口 `formatI18nVFormat()` / `formatI18n(StrId, Args&&...)`，并在 `setLanguage()` 写入 `g_relayout_required` 原子标脏位（通过 `consumeI18nRelayoutRequired()` 消费）。
  - `src/gui/workstation_canvas.h:L531-L566`：不新增任何 `UiWidgetNode` 或具名 `struct`/`class`，以纯无状态自由函数与 `enum class` / `std::tuple` 落地 `hitTestRect()`、`enum class DagColorContract`、`DagNodeContract`、`DagEdgeValidation` 与 `validateDagEdge()`（检测 `HDR BT.2020 PQ/HLG -> SDR BT.709` 色彩空间不匹配并标记 `Auto ToneMap` 自动插入能力）。
  - `tests/test_gui_workstation.cpp:L519-L549`：新增 `dag_contract_validation_and_auto_tonemap` 与 `i18n_vformat_and_relayout_flag` 单元测试。

---

## 3. 本地静态门禁自检实测输出

1. **括号 / 圆括号平衡检查（全部 14 个 C/C++ 修改文件）**：
   - `hw_transcode_stages.cpp` (1000L)、`i18n.cpp` (657L)、`i18n.h` (217L)、`playback_session.h` (298L)、`playback_session_impl.h` (139L)、`playback_session_video.cpp` (372L)、`preview_viewport.cpp` (752L)、`raii_owners.h` (142L)、`rtx_gui.cpp` (1237L)、`sys_dynload.h` (998L)、`test_gui_workstation.cpp` (549L)、`test_rife.cpp` (1130L)、`workstation_canvas.cpp` (1135L)、`workstation_canvas.h` (568L) — 全部 `braces=0, parens=0`。
2. **`user32.dll` / `gdi32.dll` 静态符号零残留扫描**：
   - `raii_owners.h`、`rtx_gui.cpp`、`workstation_canvas.h`、`workstation_canvas.cpp`、`preview_viewport.cpp` 静态 `user32`/`gdi32` 符号与 `<windowsx.h>` 宏残留均为 `0`；剩余裸 Win32 调用均为 `kernel32.dll` 符号（`AttachConsole`、`CreateFileW`、`GetCommandLineW`、`GetFileType`、`GetStdHandle`、`MulDiv`、`SetConsoleOutputCP`、`SetStdHandle`、`MultiByteToWideChar`、`WideCharToMultiByte`、`GetLastError`、`GetModuleHandleW`、`CreateEventW`、`GetProcAddress`、`LoadLibraryExW`、`WaitForSingleObject`）。
3. **`AuditTypes` 零膨胀核验**：
   - 全部 12 个修改的 `include/` 与 `src/` 源码文件新增具名 `class`/`struct` 数量均为 `0`，全仓具名类型计数严格维持 `33/35`（远低于 `35` 上限，零 `UiWidgetNode`、零冗余 `Impl`）。
4. **`ContractGuard` 与 `84006c2` 零回退核验**：
   - `include/rtx/ngx_remaster.h` 完整保留 `consumeNvofSceneCut` 与 `preview_generation_`；`src/core/job_queue.cpp` 完整保留 `isPreviewEnabled()`；`src/io/ffmpeg_io.cpp` 完整保留 `AVFMT_FLAG_CUSTOM_IO` 保护（本批未改动）。
5. **`AuditPrivacy` 与 `AuditSpdx` 自检**：
   - 全部 16 个改动文件及本汇报文档通过 `AuditPrivacy.cmake` 正则扫描，`0` 违规；全部改动文件保留完整 SPDX 许可头。

---

## 4. 宿主机待执行终局验收清单与未决风险

因当前执行环境为无 MSVC / Windows 11 / RTX 5060 GPU 的沙盒，以下宿主机实测门禁如实保持未勾选状态，交由宿主机统一验收：

- [ ] `.\build.bat`（MSVC `/std:c++latest /W4 /WX` 0 Error、0 Warning，且 `ContractGuard` / `AuditTypes` / `AuditPrivacy` / `AuditSpdx` / `AuditSloc <= 23000` 全部 PASS）
- [ ] `cmake -DTARGET_BIN=<build>/CineRTX.exe -DSTRICT_NO_BCRYPT=ON -P cmake/AuditImports.cmake` 实测 `Static IAT == 1/1 (kernel32.dll)`（无 `gdi32.dll`、`user32.dll`、`bcrypt.dll`）
- [ ] `CineRTX.exe --headless --probe` 终端直出与管道重定向验证通过，且进程不加载 `user32.dll` / `gdi32.dll`
- [ ] `rtx_tests.exe` 全量通过（预计 `165/165 PASS`，含本次新增 4 项单测），若全量串行出现 `rife_four_phase_monotonic_progression` 偶发波动，单跑 `rtx_tests.exe --filter=rife_four_phase_monotonic_progression` 复验
- [ ] RTX 5060 实机 Nsight Systems 60fps 帧预算（`< 12.5 ms`、GPU 气泡 `< 0.8 ms`）与 2 小时 Soak 长稳压测

### 未决风险提示
1. **CRT/静态库间接符号风险**：已彻底清零 `CineRTX.exe` 自身源码及 `raii_owners.h` 对 `user32.dll` 与 `gdi32.dll` 的直接引用；若宿主机链接器报告来自第三方静态库（如旧缓存 FFmpeg）的极个别间接 `user32`/`gdi32`/`bcrypt` 符号，请直接贴出 `dumpbin /imports CineRTX.exe` 对应函数名即可定向剥离。
2. **MSVC `/W4 /WX` 严格类型转换风险**：`rtx::sys` 动态包装均已按 Win32 SDK 精确原型编写，若宿主机某个 Win32 SDK 版本的 `LONG_PTR`/`DPI_AWARENESS_CONTEXT`/`LPMONITORINFO` 出现 `C4191`/`C4244` 告警，贴出报错行号即可 1 分钟内定点修正。
