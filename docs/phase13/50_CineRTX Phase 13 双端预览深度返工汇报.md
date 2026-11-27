# 50_CineRTX Phase 13 双端预览深度返工汇报

> **汇报编号**：`CINERTX-P13-REWORK-REPORT-050`  
> **施工依据**：`46_CineRTX Phase 13 双端预览返工(45号)专家穿透复验裁决与真机竣工指令书.md` (`CINERTX-P13-EXPERT-AUDIT-046`)、`47_rework_wave1-A_播放预览环2D写入与DComp合成层级修复施工单.md`、`48_rework_wave1-B_转码预览动态分辨率与HDR10双侧标志闭环施工单.md`、`49_rework_wave2-A_HLSL字节码重编译_宿主机全量构建与双端实机封板施工单.md`  
> **45 号已验收成果**：100% 完整保留（`preview_viewport.h:L31-L36,L115,L131`、`preview_viewport.cpp:L369,L421,L553,L599,L631,L643`、`preview_tone_map_cs.hlsl:L96-L175`、`test_gui_workstation.cpp:L360-L382`）。

---

## 一、 根因穿透发现与代码级返工闭环

### 1.1 硬伤 1 根治（Doc 47 — 播放预览环 2D 写入、PTX 实参错位、动态扩容与 256B 对齐）

对预编译 CUDA PTX (`src/shim/ngx_bridge/p010_pack.ptx:L3616-L3809`) 进行逐指令反汇编核验后，捕获了 Player 模式“仅顶部数像素单线写入、下方全黑”的**真正底层根因**：

* **PTX 入口 `rtx_yuv_to_rgb10_buf` 11 参数真实布局**（`p010_pack.ptx:L3617-L3665, L3803-L3808`）：
  * `param_0..2`：`y_plane`, `uv_plane`, `out_ptr`
  * `param_3..4`：`width`（`setp.ge.s32 %p1, %r1, %r8`）、`height`（`setp.ge.s32 %p2, %r2, %r9`）
  * `param_5..6`：`y_pitch`, `uv_pitch`
  * `param_7..9`：`format`, `full_range`, `matrix`
  * **`param_10`**：`out_pitch_bytes`（`mul.wide.s32 %rd25, %r2, %r34` -> `out_ptr + y * out_pitch_bytes + x * 4`）
* **原启动器实参错位缺陷**（原 `p010_pack_runtime.cpp:L353-L354`）：原代码将 `&out_pitch` 错传在 `param_3`，导致 `param_10`（输出行步长）实际收到了 `matrix`（BT.709 为 `0`，BT.2020 为 `1`），全部 `height` 行像素均以 `y * 0 + x * 4` 反复覆写第 0 行，其余 `height - 1` 行完全保持零值（黑屏）！

**已完成修复与实证坐标**：

1. **修正 PTX 11 参数严格对齐并保持 2D 网格发射**：`src/shim/ngx_bridge/p010_pack_runtime.cpp:L353-L359` 将 `args[]` 修正为 `{&y_plane, &uv_plane, &out_ptr, &width, &height, &y_pitch, &uv_pitch, &format, &full_range, &matrix, &out_pitch}`，并保持 `gx = (width + 15u) / 16u`、`gy = (height + 15u) / 16u`、`block(16, 16)` 2D 网格发射。
2. **256B 对齐行步长与完整三维容量追踪**：
   * `src/shim/ngx_bridge/d3d12_interop.h:L78-L80,L141-L143` 新增原语成员 `preview_ring_width_` / `preview_ring_height_` 与只读访问器 `previewRingWidth()` / `previewRingHeight()`（**+0 具名类型**）。
   * `src/shim/ngx_bridge/d3d12_interop.cpp:L875-L883,L956-L989` 将 `row_bytes` 统一按 `((width * bytes_per_pixel) + 255u) & ~255u` 进行 256 字节对齐，复用判定严格校验 `slot_count`、`row_bytes`、`width`、`height` 四项不变量，析构/释放时同步清零。
3. **动态扩容与流同步**：
   * `src/shim/ngx_bridge/bridge_exports.cpp:L887-L901` 在 `rtx_ngx_bridge_playback_pack()` 中校验当前环容量；当解码帧 `width` / `height` / `width * 4u` 超出已分配环容量时，先执行 `stream_synchronize(stream, &error)`，再调用 `create_shared_buffer_ring(3u, width, height, 4u, &error)` 重建 3 槽共享环并刷新 `preview_ring[0..2]` 与 `preview_pitch`。
   * `src/shim/ngx_bridge/bridge_exports.cpp:L610-L613` 同步修正 `rtx_ngx_bridge_download_preview()`，按 `previewRingHeight()` 动态计算拷贝字节数（消除 `540u` 写死行数），使 1080p/4K 播放环单元测试可完整验证底部行。
4. **TDD 2D 写入完整性回归测试**：`tests/test_playback_session.cpp:L733-L793` 新增 `RTX_TEST(playback_session_2d_preview_ring_integrity)`，以 `64x64` 初始化无头播放环（`reserved[0] = 1`），向其打包 `1920x1080` 非纯黑 NV12 帧，断言动态扩容、256B 对齐 `out_pitch` 及顶部行 `(0, 0)`、中部行 `(W/2, H/2)`、底部行 `(W-1, H-1)` 像素字全部非零（无 GPU 环境自动 `[SKIP-GPU]`）。

---

### 1.2 硬伤 4 根治（Doc 47 — DComp 合成层级置顶修复与 Player 控件隔离）

1. **修正 DComp `topmost` 标记**：`src/gui/preview_viewport.cpp:L510-L512` 将 `dcomp_device_->CreateTargetForHwnd(child_hwnd_.get(), TRUE, &raw_target)` 第二参数由 `TRUE` 修正为 `FALSE`，确保 DirectComposition 视觉树遵守常规 Win32 HWND Z-order，不再遮挡主窗口右键菜单、下拉框与 Toast 提示。
2. **Player 工作区非相关控件全量隔离核验**：全量扫描 `src/gui/workstation_canvas.h:L297-L322` 所有 `IDC_*` 控件 ID，确认无任何遗漏控件；`src/gui/rtx_gui.cpp:L122-L128` 在 `isPlayer == true` 时通过 `showGroup` 对 `kInspectorIds`、`kEncoderIds`、`kNleIds` 全部执行 `SW_HIDE`，仅保留 `kPlayerIds` 与顶栏切换标签。

---

### 1.3 硬伤 3 根治（Doc 48 — 消除 `960u, 540u` 硬编码与 HDR10/HLG 双侧色彩标志闭环）

1. **彻底消除 `960u, 540u` 硬编码**：
   * `src/gui/workstation_canvas.h:L417-L432` 新增纯函数 `derivePreviewDimensions(resource_bytes, pitch, hint_width, hint_height)`，基于借出 `ID3D12Resource::GetDesc().Width` 与 `pitch` 动态推导真实 `(src_w, src_h)`，并对 256B 对齐非 64px 宽画面（如 `720x1280`）保留精确有效宽度，非法几何返回 `{0u, 0u}`。
   * `src/gui/workstation_canvas.cpp:L13,L1122-L1138` 引入仅头文件 `<d3d12.h>`（零新增静态 DLL 依赖），在 `presentTranscodePreview()` 中读取 `static_cast<ID3D12Resource*>(resource)->GetDesc().Width` 并调用 `derivePreviewDimensions(...)`，全文件已**彻底清零 `960u` / `540u` 写死常量**。
2. **输入源 HDR10/HLG 左侧标志与双侧峰值亮度闭环**：
   * `src/gui/rtx_gui.cpp:L320-L329` 在 `applyProbedMedia()` 中通过 `makeFfmpegDemuxer()->open(path)` 同步填充 `app.playbackInfo`，确保 Encoder 工作区在选片、拖拽或入队时即持有真实源视频的 `ColorDescription`（`SMPTE2084` / `HLG` / `BT709`）与 `Hdr10Metadata::cll`（MaxCLL）。
   * `src/gui/workstation_canvas.h:L434-L469` 新增纯函数 `computeTranscodePreviewColorParams(src_info, prof, dst_max_nits)`，返回 `std::tuple<std::uint32_t, float, float>`（**+0 具名类型**），完整覆盖：
     * **HDR10 (`SMPTE2084`) 输入**：左侧置位 `kPreviewLeftPq | kPreviewLeftBt2020`，`left_max_nits = cll.first > 0 ? cll.first : 1000.0f`；
     * **HLG (`TransferFunction::HLG`) 输入**：左侧置位 `kPreviewLeftHlg | kPreviewLeftBt2020`，`left_max_nits = 1000.0f`；
     * **SDR (`BT709`) 输入**：左侧标志为 `0u`，`left_max_nits = 100.0f`；
     * **右侧输出**：TrueHDR/PQ（`prof.peak_nits > 0`）置位 `kPreviewRightPq | kPreviewRightBt2020` 且 `right_max_nits = std::max(peak, 203.0f)`；HDR 直通继承对应 PQ/HLG 标志。
   * `src/gui/workstation_canvas.cpp:L1133-L1138` 将上述双侧 `color_flags`、`left_max_nits`、`right_max_nits` 透传给 `app.previewViewport->presentShared(...)`。
3. **TDD 单元测试覆盖**：`tests/test_gui_workstation.cpp:L384-L437` 新增 `RTX_TEST(gui_transcode_preview_dynamic_resolution_and_hdr10_flags)`，覆盖 4K (`3840x2160`)、9:16 竖屏 (`720x1280`)、21:9 超宽 (`2560x1080`)、零尺寸防御、HDR10+PQ、SDR+TrueHDR (600 nits) 及 HLG 输入全场景断言。

---

### 1.4 硬伤 2 核查说明（Doc 49 — `preview_tone_map_cs.h` 构建机制）

经核查 `CMakeLists.txt:L247-L269` 及工作区 `src/gui/` 目录：
* `preview_tone_map_cs.h` **并非静态签入仓库的源码文件**，而是由 `CMakeLists.txt:L261-L269` 在 Windows 宿主机配置/构建阶段通过 `add_custom_command` 自动调用 `${FXC_EXECUTABLE} /T cs_5_1 /E CSMain /Fh "${RTX_GENERATED_DIR}/preview_tone_map_cs.h" /Vn g_preview_tone_map_cs src/gui/shaders/preview_tone_map_cs.hlsl` 实时生成。
* 只要在 Windows 宿主机执行 `.\build.bat`，CMake 将根据 `DEPENDS src/gui/shaders/preview_tone_map_cs.hlsl` 自动触发 `fxc.exe` 重编译 12-DWORD `ViewportConstants` 字节码头文件。

---

## 二、 施工验收 CheckList 与门禁勾选（含精确坐标实证）

### 2.1 Doc 47（Rework Wave 1-A）验收清单

| 序号 | 验收项目 | 状态 | 实证坐标 (`文件:L行号`) |
| :--: | :--- | :--: | :--- |
| 1 | `rtx_yuv_to_rgb10_buf` 2D 网格与 11 参数对齐修复 | `[x]` | `src/shim/ngx_bridge/p010_pack_runtime.cpp:L353-L359`（对齐 `p010_pack.ptx:L3616-L3809`） |
| 2 | 动态环容量重分配与 `stream_synchronize` | `[x]` | `src/shim/ngx_bridge/bridge_exports.cpp:L887-L901`、`src/shim/ngx_bridge/d3d12_interop.cpp:L875-L883` |
| 3 | `out_pitch == previewRingRowBytes()` 256B 对齐契约 | `[x]` | `src/shim/ngx_bridge/d3d12_interop.cpp:L876`、`src/shim/ngx_bridge/bridge_exports.cpp:L857` |
| 4 | DComp `CreateTargetForHwnd(..., FALSE)` 置顶修复 | `[x]` | `src/gui/preview_viewport.cpp:L511` |
| 5 | Player 工作区非相关控件全量隔离 (`SW_HIDE`) | `[x]` | `src/gui/rtx_gui.cpp:L122-L128`、`src/gui/workstation_canvas.h:L297-L322` |
| 6 | 宪法红线 1：`AuditTypes` 新增具名类型 `+0` | `[x]` | 本轮 10 处文件编辑新增具名 `struct`/`class`/`enum` = `0`（维持基线 `34/35`） |
| 7 | 宪法红线 2：`AuditImports` 零新增静态 DLL | `[x]` | 仅引入 `<d3d12.h>` COM 头文件定义，无新增 `.lib`/静态 DLL 导入（维持 GUI `<= 4` / HEADLESS `== 2`） |
| 8 | 宪法红线 3：预览/播放主链路零 GPU->CPU 回读 | `[x]` | `src/gui/workstation_canvas.cpp:L1108-L1141` 全程 D3D12 共享显存借用，零回读 |
| 9 | TDD 单元测试（顶/中/底三行非零 + 动态扩容）代码落盘 | `[x]` | `tests/test_playback_session.cpp:L733-L793` |

### 2.2 Doc 48（Rework Wave 1-B）验收清单

| 序号 | 验收项目 | 状态 | 实证坐标 (`文件:L行号`) |
| :--: | :--- | :--: | :--- |
| 1 | `workstation_canvas.cpp` 彻底消除 `960u` / `540u` 硬编码 | `[x]` | `src/gui/workstation_canvas.cpp:L1122-L1138`（全文件正则检索 `960`/`540` 命中 `0`） |
| 2 | 动态分辨率推导与零尺寸防御 | `[x]` | `src/gui/workstation_canvas.h:L417-L432`、`src/gui/workstation_canvas.cpp:L1125-L1132` |
| 3 | HDR10/HLG 输入源左侧标志与双侧 Nits 闭环 | `[x]` | `src/gui/rtx_gui.cpp:L320-L329`、`src/gui/workstation_canvas.h:L434-L469`、`src/gui/workstation_canvas.cpp:L1133-L1138` |
| 4 | TDD 单元测试（4K/竖屏/超宽 + HDR10/HLG/TrueHDR）代码落盘 | `[x]` | `tests/test_gui_workstation.cpp:L384-L437` |
| 5 | 静态隐私与 SPDX 合规检查（修改文件） | `[x]` | `AuditPrivacy = 0`、`AuditSpdx = 0`、10 份修改文件括号/花括号平衡全为 `0` |

### 2.3 Doc 49（Rework Wave 2-A）宿主机编译、实跑与真机封板项（严格遵守 `[DEC-004]` / `[DEC-008]` 如实留待）

> **诚实声明**：当前执行环境为离线 Linux 沙箱，无 Windows 11 宿主机、MSVC 19.51、`fxc.exe` 及 RTX 5060 GPU。严格遵循 `[DEC-004]`/`[DEC-008]` 铁律，绝不伪造任何宿主机编译、单测、导入表或实机截图结果，以下宿主机实测项如实保持 `[ ]`，请于 Windows 11 宿主机拉取最新文件后依次执行封板：

- [ ] **Task 1 (`fxc` 字节码重编译)**：在 Windows 宿主机执行 `.\build.bat`（自动由 `CMakeLists.txt:L261-L269` 触发 `fxc.exe /T cs_5_1 /E CSMain /Vn g_preview_tone_map_cs` 重建 `${RTX_GENERATED_DIR}/preview_tone_map_cs.h`），确认 `build\generated\preview_tone_map_cs.h` 时间戳晚于 `preview_tone_map_cs.hlsl`。
- [ ] **Task 2 (全量编译与单元测试实跑)**：执行 `.\build.bat` 与 `.\build\bin\Release\rtx_tests.exe`，确认 `152/152`（原 150 + 本轮新增 2 项：`playback_session_2d_preview_ring_integrity`、`gui_transcode_preview_dynamic_resolution_and_hdr10_flags`）全绿 PASS 且退出码为 `0`。
- [ ] **Task 3 (宿主机五重静态门禁全绿核验)**：执行 `AuditTypes`（`34/35`）、`AuditImports`（GUI `<= 4` / HEADLESS `== 2`）、`AuditPrivacy`（`0`）、`AuditSpdx`（`0`）、`AuditSloc`（`< 25,000`）。
- [ ] **Task 4 (`CineRTX.exe` 双端真机实测取证)**：
  - 场景 A（Player 工作区）：分别播放 16:9、9:16 竖屏、21:9 超宽视频，确认全画面 2D 正常渲染（告别顶部单线黑屏）、居中 Aspect-Fit 零拉伸、缩放不卡死且右键菜单/Toast 不被遮挡。
  - 场景 B（Encoder 工作区）：分别导入 HDR10 (PQ) 与 SDR+TrueHDR 源视频开启预览，确认左侧 HDR10 经 BT.2390 映射无灰白过曝、右侧色彩正常、A/B 分屏拖拽顺滑且画布受 DComp 硬裁剪零溢出。

---

## 三、 本轮修改与归档文件清单

* **修改替换（共 10 个文件，旧版均已归档至 `work/_CineRTX_bak_archive/*.p13r7bak`）**：
  1. `src/shim/ngx_bridge/p010_pack_runtime.cpp`
  2. `src/shim/ngx_bridge/d3d12_interop.h`
  3. `src/shim/ngx_bridge/d3d12_interop.cpp`
  4. `src/shim/ngx_bridge/bridge_exports.cpp`
  5. `src/gui/preview_viewport.cpp`
  6. `src/gui/workstation_canvas.h`
  7. `src/gui/workstation_canvas.cpp`
  8. `src/gui/rtx_gui.cpp`
  9. `tests/test_playback_session.cpp`
  10. `tests/test_gui_workstation.cpp`
* **汇报文档**：`docs/phase13/50_CineRTX Phase 13 双端预览深度返工汇报.md`（纯 `.md` 原生格式落盘，非 `.gdoc`）。
