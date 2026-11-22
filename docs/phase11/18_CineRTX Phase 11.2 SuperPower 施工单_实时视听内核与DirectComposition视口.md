# CineRTX Phase 11.2 SuperPower 施工单：实时视听内核与 DirectComposition 零回读视口合龙（终审修订版）

- **施工单号**：`CINERTX-P11-WORKORDER-018-REV2`
- **归档路径**：`docs/phase11/18_CineRTX Phase 11.2 SuperPower 施工单_实时视听内核与DirectComposition视口.md`
- **承接角色**：视听多媒体与 D3D12/DComp 图形执行工程师（外部执行工程师）
- **前置基线**：Phase 11.1 封板基线（109/109 单测全绿，测试机驱动升级至 $\ge 621.14$ WHQL，WTS Session ID > 0）
- **依据母本**：[`16_CineRTX Phase 11 演进计划.md`](./16_CineRTX%20Phase%2011%20演进计划.md)、[`21_CineRTX Phase11 施工方案深度审查.md`](./21_CineRTX%20Phase11%20施工方案深度审查.md)
- **建议工期**：3–4 个有效工作日
- **核心目标**：CMake 着色器自动生成与 Linker `/MAP` 开启、D3D12–CUDA 3 槽共享预览环与 `PreviewViewport` 4 项高危边界加固、`nv_decoder.cpp` 非终态 Seek 复位提纯、UI 假播彻底拔除与软编选项清零。

---

## 一、 施工背景与输入资产

在 Phase 11.1 中，全仓已完成五域 RAII 拓扑治理、统一 `ComOwner` 与表驱动 C++26 静态反射参数模型。21 号穿透审查指出，原计划在桥接层共享缓冲、着色器编译路径、视口动态分辨率及软编下拉框等方面存在 4 处硬断层（C-01 至 C-04）。本施工单已将上述隐患全量收敛，作为外部执行工程师施工的权威规范。

### 核心参考与输入文件
- 历史提纯参考源：`archive/phase11-attempt-1` 中的 `src/core/playback_session.cpp`、`src/core/wasapi_audio_output.cpp`、`src/gui/preview_viewport.cpp`
- 架构约束与技术裁决：[`21_CineRTX Phase11 施工方案深度审查.md`](./21_CineRTX%20Phase11%20施工方案深度审查.md) 第三节

---

## 二、 核心开发任务与实施规范

### 任务 1：CMake 构建期着色器生成与 Linker `/MAP` 管道落地（C-01）
修改文件：`CMakeLists.txt`、`src/gui/preview_viewport.cpp`、`tests/test_raii_retirement.cpp`

1. **`fxc.exe` 健壮探测与编译规则**：
   在 `CMakeLists.txt` 中配置自动探测，若环境变量未命中，通过 `file(GLOB ... "C:/Program Files (x86)/Windows Kits/10/bin/*/x64")` 降序探测绝对路径，未命中则 `message(FATAL_ERROR ...)`：
   ```cmake
   find_program(FXC_EXECUTABLE fxc.exe HINTS
       "$ENV{WindowsSdkVerBinPath}/x64"
       "$ENV{WindowsSdkDir}/bin/x64"
   )
   if(NOT FXC_EXECUTABLE)
       file(GLOB WIN10_SDK_BINS "C:/Program Files (x86)/Windows Kits/10/bin/*/x64")
       list(SORT WIN10_SDK_BINS ORDER DESCENDING)
       find_program(FXC_EXECUTABLE fxc.exe HINTS ${WIN10_SDK_BINS})
   endif()
   if(NOT FXC_EXECUTABLE)
       message(FATAL_ERROR "fxc.exe not found! Please install Windows 10/11 SDK.")
   endif()

   set(SHADER_HEADER "${CMAKE_BINARY_DIR}/generated/preview_tone_map_cs.h")
   add_custom_command(
       OUTPUT "${SHADER_HEADER}"
       COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/generated"
       COMMAND ${FXC_EXECUTABLE} /T cs_5_1 /E CSMain /Fh "${SHADER_HEADER}" /Vn g_preview_tone_map_cs "${CMAKE_CURRENT_SOURCE_DIR}/src/gui/shaders/preview_tone_map_cs.hlsl"
       DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/gui/shaders/preview_tone_map_cs.hlsl"
       COMMENT "Compiling preview_tone_map_cs.hlsl -> ${SHADER_HEADER}"
   )
   ```
2. **目标依赖与包含路径解耦**：
   - 将 `${SHADER_HEADER}` 同时追加至 `CineRTX` 与 `rtx_tests` 的 `target_sources`（确立精确构建先后顺序）。
   - 向 `CineRTX` 与 `rtx_tests` 追加 `${CMAKE_BINARY_DIR}/generated` 至 `target_include_directories`。
   - 修改 `src/gui/preview_viewport.cpp` 与 `tests/test_raii_retirement.cpp` 为 `#include "preview_tone_map_cs.h"`。
   - `git rm` 从源码树中彻底移除 `src/gui/shaders/preview_tone_map_cs.h`。
3. **开启 CineRTX `/MAP` 链接选项**：
   在 `CMakeLists.txt` 的 `if(WIN32) if(MSVC)` 块中为 `CineRTX` 目标追加 `/MAP:$<TARGET_FILE_DIR:CineRTX>/CineRTX.exe.map`，保持 `/OPT:REF` 与 `/OPT:ICF` 开启。

---

### 任务 2：D3D12–CUDA 3 槽共享预览环与借用接口（C-02）
修改文件：`src/shim/ngx_bridge/d3d12_interop.h/.cpp`、`src/shim/ngx_bridge/bridge_exports.cpp`、`include/shim/ngx_bridge_abi.h`、`src/core/ngx_remaster.cpp`、`src/core/hw_transcode_stages.cpp`

1. **动态加载 `cuExternalMemoryGetMappedBuffer`**：
   在 `src/shim/ngx_bridge/d3d12_interop.cpp` 中补充加载 CUDA 驱动 API `cuExternalMemoryGetMappedBuffer`（函数签名对齐 `nv_func_tables.cpp`）。
2. **构建 3 槽 D3D12 共享缓冲环（约 5.93 MiB）**：
   - 在 `bridge_exports.cpp` 中将 `preview_ring` 扩充为 3 槽 `D3D12_RESOURCE_DIMENSION_BUFFER`（3 × 960 × 540 × 4 B = 6,220,800 B，行距 3840 B 天然对齐）。
   - 每个槽位以 `D3D12_HEAP_FLAG_SHARED` 与 `D3D12_RESOURCE_STATE_COMMON` 创建，通过 `CreateSharedHandle` 导出 Win32 句柄并由 CUDA `cuImportExternalMemory` 与 `cuExternalMemoryGetMappedBuffer` 映射出 `CUdeviceptr`。
3. **扩展 ABI 借用导出接口**：
   在 `include/shim/ngx_bridge_abi.h`（严守 `MIT OR Apache-2.0` 开源头）追加：
   ```c
   RTX_SHIM_API int32_t RTX_SHIM_CALL rtx_ngx_bridge_borrow_preview(
       RtxNgxBridge* bridge,
       uint32_t slot,
       void** out_d3d12_device,
       void** out_d3d12_resource,
       uint32_t* out_pitch);
   ```
   并在 `bridge_exports.cpp` 与 `ngx_remaster.cpp` 完成实现与静态绑定。
4. **生产端防撕裂防阻塞调度**：
   在 `hw_transcode_stages.cpp` 中，写入槽位前探测原子世代标志，若 3 槽均忙则置 `preview_rgba10_ptr = 0` 直接跳过本次预览拷贝，绝不阻塞转码流水线。

---

### 任务 3：`PreviewViewport` 4 项高危边界加固与动态源参数（C-03）
修改文件：`src/gui/preview_viewport.h/.cpp`、`src/gui/d3d12_raii.h`、`src/gui/shaders/preview_tone_map_cs.hlsl`

1. **封堵 `GetCompletedValue() == UINT64_MAX` 假完成陷阱**：
   在 `waitForGpuIdle()` 与 `GpuRetirementQueue::collect()` 中优先检验 `completed == UINT64_MAX`；命中后调用 `GetDeviceRemovedReason()`，置位 `device_lost_ = true` 并执行 `retirement_.releaseAll()`，返回 `ErrorCode::DeviceLost`。
2. **补全 `releaseGpu()` 全量命令对象延迟退役**：
   当 `WAIT_TIMEOUT` 时，除资源视图外，将 `cmd_allocator_`、`cmd_list_`、`pipeline_`、`root_signature_`、`cmd_queue_` 与旧 `device_` 全量退役；换设备时将 `visual_->SetContent(nullptr)` 与 `dcomp_target_->SetRoot(nullptr)` 置空并 `Commit()`。严禁忽略 `cmd_queue_->Signal()` 返回值。
3. **调换等待序与动态源参数解耦**：
   - 等待序调换为：**先 `waitForGpuIdle(0)`，后探测 `GetFrameLatencyWaitableObject()`**。
   - 扩展 `presentShared()` 签名，接收动态 `uint32_t src_width, uint32_t src_height, bool input_is_pq, float max_nits`（彻底删除 `960x540` 与 `100.0f` 硬编码）。
   - 将 `input_is_pq` 存入 `ViewportConstants::reserved[0]`（保持 12 DWORD 根常量布局不变），着色器据此精确区分 Studio-Swing SDR 与 PQ ST.2084，根治 SDR 预览异常压暗缺陷。
   - 彻底删除 `presentFromHostOrShared` 空桩与 `rtx_gui.cpp` 中的 `previewHost`。

---

### 任务 4：`nv_decoder.cpp` 非终态 Seek 复位与播放内核提纯（C-01）
修改文件：`src/shim/loader/nv_decoder.cpp`、`src/shim/loader/nv_shim_static_table.cpp`、`include/shim/rtx_shim_abi.h`、`include/rtx/nvidia_shim.h`、`src/core/nvidia_shim.cpp`、`include/rtx/playback_session.h`、`src/core/playback_session.cpp`、`src/core/wasapi_audio_output.cpp`、`src/core/gop_seek_scheduler.cpp`

1. **新增 `rtx_shim_decoder_reset`**：
   保留原有 `rtx_shim_decoder_flush`（终态 EOS）供转码使用；新增 `rtx_shim_decoder_reset(RtxDecoder* decoder)`：在 `decoder->stream` 执行同步，清空 ready 队列，复原所有 slots 为 `in_use = false`，清零 `end_of_stream` 与 `flushed`，向 `cuvidParseVideoData` 提交空包 `CUVID_PKT_DISCONTINUITY`（0x04）复位参考帧状态。
2. **`MediaPlaybackSession` 零回读直呈通路**：
   `video_worker` 单线程绑定 CUDA 主上下文，解码表面在 GPU 上轻量打包后写入一套按源尺寸分配的 D3D12–CUDA 3 槽共享 Buffer，直通 `presentShared()`，全流程 D2H 拷贝量恒为 0 B。
3. **WASAPI 音频主时钟与热拔插**：
   以 `IAudioClock` + QPC 外推构建 705.6 MHz Flicks 主时钟；`pause()` 冻结时钟锚点；捕获 `AUDCLNT_E_DEVICE_INVALIDATED` 后控制面无锁切换默认音频端点续播。
4. **测试夹具剥离**：
   将 `tests/test_playback_session.cpp` 中的 864 行十六进制码流剥离至 `tests/fixtures/test_streams.h`。

---

### 任务 5：UI 假播拔除与废弃软编下拉项清零（C-04 / A-05）
修改文件：`src/gui/rtx_gui.cpp`、`src/gui/workstation_canvas.cpp`

1. **彻底拔除 66ms 假播循环**：
   彻底删除 `rtx_gui.cpp` 中的 `WM_TIMER(3, 66ms)`、`runScrubPreviewAsync()` 与 `previewHost`，接入 `MediaPlaybackSession` 与 30 Hz `AtomicTelemetryBus`。
2. **修复宽字符乱码与剔除废弃选项**：
   修复 `workstation_canvas.cpp` 的 `ChromeText::SendEncoder` 乱码；从 `collectProfileFromUi`、`applyProfileToUi` 与 `populateComboBoxes` 彻底删除 `av1_cpu`、`hevc_cpu` 与 `crf` 选项，100% 对齐 `kParamSchema`。

---

## 三、 外部工程师交付验收检查清单（Gates）

- [ ] **Check 1：单测全绿无跳过**：`rtx_tests.exe` 全量执行 100% PASS，新增播放与 Seek 测试。
- [ ] **Check 2：构建期着色器与 Map 正常**：自动生成 `preview_tone_map_cs.h`，`CineRTX.exe.map` 成功输出。
- [ ] **Check 3：负向拔除扫描清零**：
  ```pwsh
  rg "runScrubPreviewAsync|SetTimer\(hwnd, 3, 66" src/gui/rtx_gui.cpp
  ```
  执行输出必须为 **0 命中**。
- [ ] **Check 4：Linker .map 核心修饰符号留存**：
  检索 `CineRTX.exe.map` 的 `Publics by Value` 节区，确认以下修饰名实体留存：
  - `MediaPlaybackSession::create` 与 `seek`
  - `WasapiAudioOutput::create`
  - `GopSeekScheduler`
  - `PreviewViewport::presentShared` 与 `adoptDevice`
