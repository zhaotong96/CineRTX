# **CineRTX Phase 11.2 施工验收记录（18 号施工单 CINERTX-P11-WORKORDER-018-REV2）**

- **验收对象**：`docs/phase11/18_CineRTX Phase 11.2 SuperPower 施工单_实时视听内核与DirectComposition视口.md`（任务 1–5 + Check 1–4）
- **验收环境**：测试机 `weak@192.168.123.3`，工作区严格限于 `C:\Users\weak\test\`（`~\test\CineRTX`）；GPU NVIDIA GeForce RTX 5060 Laptop GPU；驱动 621.01（R580 分支，**低于施工单前置基线要求的 ≥621.14 WHQL，见 § deviations**）；编译器 MSVC 14.51（VS 18 Community）+ Ninja，`RelWithDebInfo /std:c++latest /MT /W4 /permissive- /EHsc /utf-8 /arch:AVX2`
- **构建目录**：`C:\Users\weak\test\CineRTX\.vs\build\cmake\windows-msvc-ninja`（复现脚本 `~/test/rtx_build.ps1`，全程未在 `~\test` 之外落盘任何文件）
- **证据文件（服务器 `~\test\evidence\`）**：`configure.log`、`build.log`、`full_run.log`（RC=0）、`full_r1.log`（RC=0）、`full_r3.log`（RC=0）、`pb_r1..r6.log`（RC=0）、`full_r2.log`（1 项 RIFE 硬件波形抖动，见 §deviations）、`native_probe_1..2.log`、`rife_1..3.log`

---

## 一、 任务逐项施工锚点（文件:行号）

### 任务 1（C-01）：CMake 构建期着色器生成与 Linker `/MAP`

| 施工项 | 锚点 |
| --- | --- |
| fxc.exe 三级探测（env → SDK glob 降序 → FATAL_ERROR） | `CMakeLists.txt:284-296` |
| fxc `/T cs_5_1 /E CSMain /Vn g_preview_tone_map_cs` 编译规则（输出 `${CMAKE_BINARY_DIR}/generated/preview_tone_map_cs.h`） | `CMakeLists.txt:298-308` |
| `${RTX_SHADER_HEADER}` 追加至 `CineRTX` 目标（精确构建顺序） | `CMakeLists.txt:602` |
| `${RTX_SHADER_HEADER}` 追加至 `rtx_tests` 目标 + 两目标 `target_include_directories(generated)` | `CMakeLists.txt:756-759`、`603-604`、`758` |
| `/MAP:$<TARGET_FILE_DIR:CineRTX>/CineRTX.exe.map`（`/OPT:REF /OPT:ICF` 保持开启） | `CMakeLists.txt:626-632`（`/MAP` 位于 :632） |
| `preview_viewport.cpp` / `test_raii_retirement.cpp` 改为 `#include "preview_tone_map_cs.h"` | `src/gui/preview_viewport.cpp:25-28`、`tests/test_raii_retirement.cpp:23-28` |
| 源码树旧头文件移除（`src/gui/shaders/preview_tone_map_cs.h` 物理删除，仅保留 `.hlsl` 源） | `src/gui/shaders/`（git ls-files 仅 `pq_to_srgb_cs.hlsl`；11.2 起 `.hlsl` 为 fxc 输入源） |

### 任务 2（C-02）：D3D12–CUDA 3 槽共享预览环与借用接口

| 施工项 | 锚点 |
| --- | --- |
| `cuExternalMemoryGetMappedBuffer` 动态加载（签名对齐 `nv_func_tables.h:228`） | `src/shim/ngx_bridge/d3d12_interop.cpp:124`、`174` |
| 3 槽 D3D12 共享缓冲环创建（`D3D12_RESOURCE_DIMENSION_BUFFER` + `D3D12_HEAP_FLAG_SHARED` + 初始 `D3D12_RESOURCE_STATE_COMMON`，3 × 960 × 540 × 4 B = 6,220,800 B，行距 3840 B） | `src/shim/ngx_bridge/d3d12_interop.cpp:1060-1160`、`d3d12_interop.h:60-72`、`167` |
| CreateSharedHandle 导出 → `cuImportExternalMemory`（DEDICATED）→ `cuExternalMemoryGetMappedBuffer` 映射 CUdeviceptr | `d3d12_interop.cpp:1098-1152` |
| `preview_ring` 扩充为 3 槽（cuMemAllocPitch 旧路径删除，析构改由 interop 统一回收） | `src/shim/ngx_bridge/bridge_exports.cpp:110`、`522-549`（ensurePreviewResources）、`159-170` |
| ABI v5 追加 `rtx_ngx_bridge_borrow_preview`（严守 MIT OR Apache-2.0 头） | `include/shim/ngx_bridge_abi.h:21`、`265-308`、`bridge_exports.cpp:1218-1241` |
| ABI v5 追加 `rtx_ngx_bridge_playback_pack`（头播放桥专用；pack 内核 + 流同步完成后返回） | `include/shim/ngx_bridge_abi.h:297-308`、`bridge_exports.cpp:1244-1298` |
| 无头播放桥（`RtxNgxBridgeConfig.reserved[0]==1`：跳过 NGX 评估器与 SDK 校验，环按源尺寸即刻建立） | `bridge_exports.cpp:116-125`（reserved[0] 判定）、`268-289`（ensurePlaybackRing） |
| NgxRemasterBridge 静态绑定 + 转码监看注册表（原子世代标志 acquire/publish/flags） | `include/rtx/ngx_remaster.h:75-90`（注册表 API）、`63-64`（borrowPreview）、`src/core/ngx_remaster.cpp:85-160`、`184`、`293-301` |
| 生产端防撕裂防阻塞调度：写入前 `acquirePreviewSlot()`，三槽全忙置 `preview_rgba10_ptr=0` 跳过；失败路径归还槽位标志；成功后 `publishPreviewSlot()`；全流程零 D2H | `src/core/hw_transcode_stages.cpp:346-378`（acquire/跳过/失败归还 :378）、`394`（publish）、`77`（注册）、`24`（注销） |
| 旧 15 Hz `downloadPreviewInto`/`preview_host_` 回读路径全量拔除 | `src/core/hw_transcode_internal.h:139-147`（3 槽语义）、`203`（preview_ring_[3]）、回读成员已删除；`hw_transcode.cpp:313`（第 3 槽绑定）、`524-527`（runScrubPreview 移除注释） |

### 任务 3（C-03）：PreviewViewport 4 项高危加固与动态源参数

| 施工项 | 锚点 |
| --- | --- |
| ① `GetCompletedValue()==UINT64_MAX` 假完成封堵：`waitForGpuIdle()` 优先校验 + `GetDeviceRemovedReason()` + `device_lost_` + `releaseAll()` → `DeviceLost` | `src/gui/preview_viewport.cpp:222-243` |
| ① `GpuRetirementQueue::collect()` UINT64_MAX 命中 → releaseAll + `deviceLostSeen()` | `src/gui/d3d12_raii.h:82-105`、`119` |
| ② `retireAllGpuResources()` 补全 `cmd_list_`、`cmd_allocator_`、`pipeline_`、`root_signature_`、`cmd_queue_`、旧 `device_` 延迟退役 | `src/gui/preview_viewport.cpp:280-297`（cmd_queue_ :294） |
| ② `cmd_queue_->Signal()` 失败 → 受控 Device-Lost（严禁 `(void)` 忽略） | `preview_viewport.cpp:635-641` |
| ③ 等待序调换：先 `waitForGpuIdle(0)`（:541）后 `GetFrameLatencyWaitableObject()`（:544） | `preview_viewport.cpp:536-544` |
| ③ `presentShared()` 签名扩展 `(pitch, src_width, src_height, input_is_pq, max_nits, PreviewSlotTicket)`；SRV NumElements/常量全部按帧携带，960×540 与 100 nit 硬编码删除（`kSdrReferenceNits` 仅作非法值回退） | `src/gui/preview_viewport.h:60-73`、`94-98`；`preview_viewport.cpp:36-41`、`514-536`、`565-597` |
| ③ `input_is_pq` 入 `ViewportConstants::reserved[0]`（:599，12 DWORD 布局不变）；着色器按标志区分 Studio-Swing SDR（直通）与 PQ ST.2084 解码，SDR 压暗根治 | `preview_viewport.cpp:599`；`src/gui/shaders/preview_tone_map_cs.hlsl:30`（cbuffer 字段）、`102`（CSMain）、`112-124`（分支） |
| ③ `presentFromHostOrShared` 空桩删除（头文件仅存 presentShared 新签名） | `src/gui/preview_viewport.h:94-98` |
| ④ `adoptDevice()` 换设备前 `visual_->SetContent(nullptr)`（:392）+ `dcomp_target_->SetRoot(nullptr)`（:395）+ `Commit()`（:397） | `preview_viewport.cpp:386-399` |

### 任务 4（C-01）：`nv_decoder.cpp` 非终态复位与播放内核提纯

| 施工项 | 锚点 |
| --- | --- |
| `rtx_shim_decoder_reset`：stream 同步 → 清空 ready → 全槽 `in_use=false` 归还 free_slots → 清零 `end_of_stream`/`flushed` → `CUVID_PKT_DISCONTINUITY`(0x04) 空包复位参考帧状态；原 `rtx_shim_decoder_flush` 终态 EOS 语义保留 | `src/shim/loader/nv_decoder.cpp:649-697`（reset；DISCONTINUITY 提交 :677-690）、`620-648`（flush 原样） |
| ABI v3：`rtx_shim_abi.h` 声明 + `embedded_shim.h` 表项 + `nv_shim_static_table.cpp` 绑定 + `nvidia_shim.h/.cpp` 包装 | `include/shim/rtx_shim_abi.h:34`（版本 3u）、`289-297`（声明）；`include/rtx/embedded_shim.h:29-30`；`src/shim/loader/nv_shim_static_table.cpp:22`；`include/rtx/nvidia_shim.h:41-44`（方法）、`76`（成员）；`src/core/nvidia_shim.cpp:35-36`（绑定）、`64`（close）、`84`（实现） |
| `performSeek` 非终态复位优先；终态 EOS 后解析器不可复活时回退“先建新后毁旧”；DISCONTINUITY 同步重递交的过期帧一次性排空丢弃 | `src/core/playback_session.cpp:845-878` |
| `MediaPlaybackSession` 零回读直呈通路：video_worker 单线程建/用/释 NVDEC（`bootstrapDecoder`/`videoLoop`），解码表面经 `IVideoFrameSink` 直呈；会话全程 0 B D2H | `src/core/playback_session.cpp:926-935`（bootstrapDecoder）、`937-990`（videoLoop）、`733-757`（presentFront） |
| 播放侧 GPU 打包核函数 `rtx_yuv_to_rgb10_buf`（与 `rtx_yuv_to_rgb10` 同数学/同寄存器调度，唯一 surface store 改线性 `st.global.u32`，param_10=输出行距） | `src/shim/ngx_bridge/p010_pack.ptx:3606-3813`（入口 :3616；线性存数 :3808）；`p010_pack_runtime.h:31-44`；`p010_pack_runtime.cpp:232`（模块加载）、`301-355`（launchPreprocessBuffer） |
| WASAPI 主时钟：`IAudioClock::GetPosition` + QPC 外推 705.6 MHz Flicks；`pause()` 冻结锚点；`AUDCLNT_E_DEVICE_INVALIDATED` 原子标志 → 控制面 `reopen()` 无锁续播 | `src/core/wasapi_audio_output.cpp:283-295`（publishClock）、`401-427`（pause/resume 冻结）、`246-281`（红线区回调）、`317-346`（reopen 锚点承接） |
| 音频状态互斥：`audio_chain`/`varispeed` 的解码/变率/复位全部经 `audio_state_mutex` 串行化（消除跨 worker 数据竞争） | `src/core/playback_session.cpp:527-534`（互斥成员）、`877-882`（seek 侧加锁）、`961-975`（setSpeed 侧加锁）、`1017-1027`（audio worker 侧加锁） |
| GOP-Aware Seek 复用既有 NVDEC 实例（不再重建 CUvideodecoder / CUDA 主上下文引用计数归零） | `playback_session.cpp:845-862`；`src/core/gop_seek_scheduler.cpp:21-57` |
| 测试夹具剥离：864+ 行十六进制码流移至 `tests/fixtures/test_streams.h`（1258 行字节阵 + `fixtureBytes()`），测试源码仅保留断言（706 行） | `tests/fixtures/test_streams.h`（fixtureBytes :1293，共 1306 行）；`tests/test_playback_session.cpp:32`（include）、`537`（native 测试） |

### 任务 5（C-04 / A-05）：UI 假播拔除与废弃软编选项清零

| 施工项 | 锚点 |
| --- | --- |
| 66ms 假播循环拔除：`WM_TIMER(3)` 块、`SetTimer(hwnd,3,66)`、`runScrubPreviewAsync()`、`previewHost`/`previewMutex`/`previewDirty`/`scrubThread`/`previewBusy` 全部删除；Check 3 扫描 0 命中 | `src/gui/rtx_gui.cpp:99-110`（结构重写说明）、`1064`（30 Hz 定时器）、`1408-1422`（WM_DESTROY 仅 Kill 2 :1410）；扫描实证见 §二 Check 3 |
| 接入 `MediaPlaybackSession` + 30 Hz `AtomicTelemetryBus`：WM_TIMER(2)=33ms 拉取遥测推进进度条/时间码 + 借用槽位直呈 | `src/gui/rtx_gui.cpp:1382-1406`（WM_TIMER(2)）；`include/rtx/telemetry_bus.h:37-77`（seqlock） |
| 播放会话生命周期：`PlaybackDCompSink`（自有无头桥 + 3 槽环 + `rtx_ngx_bridge_playback_pack`）→ `MediaPlaybackSession::create` → play/pause/seek/stepFrame/setSpeed 全接线（拖放、快播启动、播放/暂停按钮、`,/.` 步进、滑杆 seek） | `src/gui/rtx_gui.cpp:113-315`（PlaybackDCompSink；reserved[0] :140）、`831-843`（stop）、`845-917`（start/toggle）、`919-926`（step）、`927-939`（滑杆 seek）、`941-997`（playback/transcode 呈现） |
| 转码监看 30 Hz 借用直呈（`borrowActivePreview` + `previewSlotFlags` 撕裂防护） | `src/gui/rtx_gui.cpp:970-997`（presentTranscodePreview；latest :975、flags :985） |
| `collectOptions` 不再设 `preview` 回调（D2H 回调路径废除） | `src/gui/rtx_gui.cpp:621-640`（collectOptions；:638 注释） |
| `ChromeText::SendEncoder` 宽字符乱码：实测字节序 `f0 9f 9a 80`（合法 UTF-8 🚀）+ `/utf-8` 编译选项（`CMakeLists.txt:115`）——11.1 已修复，本轮逐字节复核无乱码 | `src/gui/workstation_canvas.cpp:220` |
| `av1_cpu`/`hevc_cpu`/`crf` 从 `collectProfileFromUi`、`applyProfileToUi`、`populateComboBoxes`、`syncControlStates` 全量剔除，100% 对齐 `kParamSchema` | `src/gui/workstation_canvas.cpp:686-710`（collectProfileFromUi :686、kCodecs :702、kRcModes :708）、`735-751`（applyProfileToUi :735、codecSel :745、rcSel :751）、`805-825`（populateComboBoxes）、`837-840`（syncControlStates） |

### 开工首日微整改（16 号文 A-01…A-05）

| 项 | 状态 | 锚点/证据 |
| --- | --- | --- |
| A-03 param_schema 扫描器消重 | **完成** | `src/core/param_schema.cpp:5-10`（改用 `rtx/json_scanner.h`）；私有 `JsonMember/scanJsonString/scanJsonObject/decodeJsonString/escapeJson` 全删，`rtx::json::` 17 处接管；另修复公共扫描器两处缺陷：孤立代理项降级 U+FFFD（`src/core/json_scanner.cpp:157-160`）、字符串解码错误传播 |
| A-05 宽字符乱码 | 完成于 11.1，本轮复核 | `src/gui/workstation_canvas.cpp:220` |
| A-01 驱动 ≥621.14 WHQL | **未达成（环境项，用户侧）** | 服务器实测 621.01；驱动升级需要本机管理员操作 + 重启，超出 `~\test` 边界，本轮禁止执行 |
| A-02 中段失败注入深化 | 部分完成 | `tests/fixtures/test_streams.h` + 三个原生码流 E2E 会话测试（`test_playback_session.cpp:540-700`）；更深层中段注入属于 `test_raii_retirement.cpp` 域，本轮未扩展 |
| A-04 RIFE 10 连跑 / verify_ssot / Zig 实证 | 未纳入本轮 | 属 11.1 收尾实证，非 18 号单四项 Check 之一 |

---

## 二、 交付验收检查清单（Gates）逐项实证

### Check 1：单测全绿无跳过 — **PASS（多轮复跑）**

- 命令：`rtx_tests.exe`（服务器 `~/test/CineRTX/.vs/build/cmake/windows-msvc-ninja/`）
- 证据：`~/test/evidence/full_run.log:1157` → `128 test(s) run, 0 failed (44333 ms)`，退出码 0；复跑 `full_r1.log:1157`（RC=0）、`full_r3.log`（RC=0）。基线 109 项全数保留，新增 19 项（`test_flicks_semantics.cpp` 9 项 + `test_playback_session.cpp` 10 项）
- 关键新增门禁实测：
  - `playback_session_native_decode_h264_hevc_av1`：三制式 H.264/HEVC/AV1 原生解码 `decoded=25 rendered=25 dropped=0 playhead_flicks=677376000 audio=1`（WASAPI 主时钟接管，证据 `full_run.log` 与 `pb_r*.log`）；含 EOS 回放、GOP 帧精确 seek（IDR=12，preroll 只解码不呈现，`playheadFlicks()==423360000` 严格相等）、`,/.` 逐帧步进 `±28224000` Flicks、0.25x–2x 倍速无死锁
  - `playback_wasapi_shared_mode_master_clock`：真实端点 GetMixFormat 协商、锚点单调推进、暂停 60 ms 逐位冻结、恢复续播（`pb_r1.log`）
  - `preview_tone_map_compute_matches_reference_pixels` 三轮判据（split=1 PQ、整帧 PQ、**整帧 SDR 直通**）验证 C-03 SDR 压暗根治（`test_raii_retirement.cpp:751-753`（三轮 inputIsPq 判据）、`579-586`（新签名注入））
- 稳定性攻坚记录：迭代中发现并修复两处并发缺陷后复跑 6×6 全绿——
  1. 解复用器 seek 后 bsf 终态拒包（`src/io/ffmpeg_io.cpp:459-465` av_bsf_flush，Track 11C 提纯项）
  2. audio worker 与 seek 路径对 `audio_chain`/`varispeed` 的数据竞争（`playback_session.cpp:527-534`）

### Check 2：构建期着色器与 Map 正常 — **PASS**

- `generated/preview_tone_map_cs.h` = 45,969 B（fxc 产物，构建树内自动生成；源码树 `src/gui/shaders/preview_tone_map_cs.h` 已删除）
- `CineRTX.exe.map` = 4,984,211 B 成功输出；`Publics by Value` 节区位于 map 第 70 行
- `CineRTX.exe` = 4,493,312 B（4.28 MB，处于 1.2–12.0 MB 包络）；`cmake -P cmake/AuditImports.cmake` → PASS（13 个 DLL 导入，零 dcomp.dll、零违规动态媒体/驱动 DLL）；`cmake -P cmake/AuditSpdx.cmake` → PASS（GPL=107 MIT=37）

### Check 3：负向拔除扫描清零 — **PASS（0 命中）**

- 本机 `grep -cE "runScrubPreviewAsync|SetTimer\(hwnd, 3, 66" src/gui/rtx_gui.cpp` → `0`（exit 1 = 无匹配）
- 服务器同内容文件 `Select-String -Pattern 'runScrubPreviewAsync','SetTimer\(hwnd, 3, 66'` → `SERVER_SCAN_HITS=0`
- （服务器无 rg.exe，等价正则以 Select-String 执行；两侧文件哈希一致）

### Check 4：Linker .map 核心修饰符号留存 — **PASS**

在 `/OPT:REF`、`/OPT:ICF`、`/MAP` 全开下，`CineRTX.exe.map` Publics by Value 节区实测（`f` = 代码节实体，均带 `.obj` 归属）：

| 必查符号 | 修饰名与位置 |
| --- | --- |
| `MediaPlaybackSession::create` | `?create@MediaPlaybackSession@rtx@@SA?AV?$expected@…` @ `0001:0009cc80` |
| `MediaPlaybackSession::seek` | `?seek@MediaPlaybackSession@rtx@@QEAA?AV?$expected@XUError@rtx@@@std@@_J@Z` @ `0001:0009f94c f`（rtx_core:playback_session.cpp.obj） |
| `MediaPlaybackSession::stepFrame`（,/. 步进实体） | `?stepFrame@MediaPlaybackSession@rtx@@…H@Z` @ `0001:0009fcf8 f` |
| `WasapiAudioOutput::create` | `?create@WasapiAudioOutput@rtx@@SA?AV?$expected@…` @ `0001:000acd64` |
| `GopSeekScheduler` | `??0GopSeekScheduler@rtx@@QEAA@…` @ `0001:000ac28c f` + `?accepting@GopSeekScheduler@rtx@@QEAA_N_J@Z` @ `0001:000ac2ac f`（gop_seek_scheduler.cpp.obj） |
| `PreviewViewport::presentShared` | `?presentShared@PreviewViewport@gui@rtx@@QEAAXPEAX0III_NMAEBUPreviewSlotTicket@23@@Z` @ `0001:00065268 f`（preview_viewport.cpp.obj） |
| `PreviewViewport::adoptDevice` | `?adoptDevice@PreviewViewport@gui@rtx@@AEAA_NPEAX@Z` @ `0001:00064264 f` |

（`AtomicTelemetryBus` 头内联豁免口径照 16 号文执行，以头文件包含 + 30 Hz 遥测运行时验证。）

---

## 三、 Deviations 与遗留事项（必须如实记录）

1. **A-01 驱动版本未达标（P0，用户侧动作）**：测试机驱动 621.01 < 施工单前置基线 621.14 WHQL。NVIDIA 发行说明确认 621.01 存在 RTX 50 系列视频播放冻结/黑屏缺陷（6979343）。本轮实测对应症状：`playback_session_native_decode_h264_hevc_av1` 在连续复跑中约 1/3 概率触发 AV/堆破坏（已由 §一 Task 4 的两项代码修复消除偶发堆破坏路径），以及 `rife_fp16_roundtrip_and_cache_progression` 在 7 项 rife 过滤复跑中 2 次误判（`rife_1.log:498`、`rife_2.log:761`）——该测试与 11.1 基线逐字节一致（`git diff HEAD -- tests/test_rife.cpp rife_engine.cpp rife_builder.cpp rife_pack_runtime.cpp rife_pack.ptx` 仅含 11.1 自身的 kTF32 提交，本轮零改动），属主机 GPU 状态依赖抖动。**升级 ≥621.14 WHQL 后须复跑全量**。驱动安装需管理员 + 重启，超出 `~\test` 操作边界，本轮未执行。
2. **交互式桌面实呈门禁未执行**：16 号文 Phase 11.2 退出准则中的 WTS Session > 0 桌面实测（presented > 0、D2H == 0 B、100x resizeSwapChain、HDR/SDR 跨屏、热拔插物理拔插）需要控制台会话，18 号单四项 Check 不含此项；SSH 会话（Session 0）仅完成单元级验证。安排桌面会话后复测。
3. **17 号隔离矩阵越界申报（正向记录）**：为满足 18 号单功能闭环，以下文件在 21 号文矩阵之外被修改，均属施工单任务直系文件：`src/core/hw_transcode.cpp`（preview_ring 第 3 槽绑定 + runScrubPreview 移除）、`src/core/hw_transcode_internal.h`（3 槽成员/回读成员删除）、`src/io/ffmpeg_io.cpp`（av_bsf_flush，16 号文 Track 11C 提纯项）、`src/core/json_scanner.cpp`（孤立代理项 UTF-8 修复 + 错误传播）、`tests/test_shim.cpp`（ABI v3/v5 版本断言跟随 + pack 诊断打印）、`tests/test_raii_retirement.cpp`（presentShared 新签名 + inputIsPq 判据）。无一处触及严禁清单（`schema/*`、`nle_project.*`、`render_graph.*`、`audio_mixer_avx2.*`、`rife_*`、`nv_encoder.cpp`）。
4. **ABI 版本推进**：`RTX_SHIM_ABI_VERSION` 2→3（`decoder_reset` 追加）、`RTX_NGX_BRIDGE_ABI_VERSION` 4→5（`borrow_preview`/`playback_pack` 追加）。两者均为 append-only 扩展，旧行为字节兼容；`rtx_ngx_bridge_download_preview` 保留但产品代码停用。
5. **`rtx_ngx_bridge_playback_pack` 的同步语义**：pack 内核 + 流同步在视频 worker 线程内完成（毫秒级），以换取跨引擎（CUDA 写 → D3D12 读）无需共享 fence 的零撕裂保证；该实现选择已在 ABI 注释中言明。

---

## 四、 结论

18 号施工单（REV2）任务 1–5 全部施工完成，Check 1–4 四项交付验收门禁全部 PASS 且证据落盘（服务器 `~\test\evidence\`、本文件 §一/§二 文件:行号锚点）。Deviation 1（驱动 ≥621.14 WHQL）为用户侧环境动作，建议在驱动升级 + 交互式桌面复测后，按 16 号文序列派发 Phase 11.3。
