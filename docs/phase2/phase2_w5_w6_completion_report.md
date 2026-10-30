# 二阶段 W4/W5/W6 施工与 Gate 2 状态报告

- 日期：2026-10-24
- 施工计划：`docs/phase2_expert_verdict_analysis_and_action_plan.md`
- 验收主机：[REDACTED_HOST] / NVIDIA GeForce RTX 5060 Laptop GPU，8151 MiB，驱动 617.14，compute capability 12.0
- 源码一致性：验收机 `C:\Users\[REDACTED_USER]\rtx\repo5` 与本地工作区的 10 个关键源文件 SHA-256 前 16 位完全一致
- 结论：工序 1（W4）与工序 3（W6）已完成并在 RTX 5060 实机取得证据；工序 2（W5）因缺少 NVIDIA RTX Video SDK 阻塞；工序 4（Gate 2.1–2.4）依赖 W5 与 nsys，未执行。

## 1. 总览

| 施工工序 | 状态 | 关键证据 |
| --- | --- | --- |
| 工序 1：W4 尾项加固 | 完成 | 跨线程二次发射 `TLS_REBIND_MATCH`；自测 trace `external semaphore ready` / `cuda signalled the shared fence`；caps 逻辑与探测输出 |
| 工序 2：W5 专有桥接 | 阻塞（SDK 缺失） | 4K 中间纹理、隔离加载路径、自适应曝光律、逻辑与探测已就绪；VSR/TrueHDR 无法运行 |
| 工序 3：W6 管线合流 | 完成 | sm_120 上 Y=8192 / UV=2048 样点逐码值零失配；111/111 单测；四项防 TDR 机制实机自测 |
| 工序 4：Gate 2 终审 | 未执行 | 依赖 W5 全管线与 nsys；本机与验收机均无 `nsys` |

## 2. 工序 1：W4 三项强制加固

### 2.1 P010 打包运行时跨线程 TLS 上下文重绑

实现：`src/shim/ngx_bridge/p010_pack_runtime.cpp:126` 的 `bindPrimaryContext()` 在每次
`rtx_ngx_pack_launch` 进入 `ensureLoaded` 之后、`cuLaunchKernel` 之前显式执行
`cuCtxSetCurrent(pack.primary_context)`（调用点 `src/shim/ngx_bridge/p010_pack_runtime.cpp:229`）。
主上下文在加载路径中持久化保存（`p010_pack_runtime.cpp:174`），不依赖线程本地状态。

实机证据（RTX 5060，`pack_verify.exe`）：主线程首次发射后，由从未绑定过 CUDA 上下文的工作线程
再次发射同一内核，输出与 CPU 参考逐字节一致：

```
pack PTX loaded and kernel found on this device
device buffers ready, launching kernel
samples: Y=8192 UV=2048, mismatches: Y=0 UV=0
PACK_KERNEL_MATCH
TLS_REBIND_MATCH
```

加固前该路径会在第二线程返回 `CUDA_ERROR_INVALID_CONTEXT (201)`；本次未出现。

### 2.2 D3D12 围栏的 CUDA 外部信号量导入

实现：`src/shim/ngx_bridge/d3d12_interop.cpp:499` `import_fence_semaphore()` 通过
`CreateSharedHandle(fence_)` 导出句柄，以 `CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE` 导入；
`fence_signal_cuda()`（`:529`）与 `fence_wait_cuda()`（`:551`）分别封装
`cuSignalExternalSemaphoresAsync` / `cuWaitExternalSemaphoresAsync`。自测中的写回路径不再
`cuStreamSynchronize` CPU 阻塞，而是 CUDA 流发信号、D3D12 队列按时间线值等待。

实机 trace（`RTX_BRIDGE_TRACE=1`）：

```
[rtx_ngx] create: external semaphore ready
[rtx_ngx] selftest: cuda->d3d12 copy
[rtx_ngx] selftest: cuda signalled the shared fence
[rtx_ngx] selftest: download
```

### 2.3 `bridge_exports.cpp` 逻辑或 → 严格逻辑与

实现：`src/shim/ngx_bridge/bridge_exports.cpp:74` 起，`vsrAvailable = host && vsr`，
`truehdrAvailable = host && truehdr`；缺失项由 `missingComponents()` 精确列出。
设备/队列失败先轮询 `pollDeviceLost()`，映射为 `RTX_ERR_DEVICE_LOST`（`:201`、`:272`、`:290`）。

实机 caps 输出（SDK 未安装时）：

```
result=0 vsr=0 truehdr=0 d3d12=0 cuda_interop=1 shared_in=0 shared_out=0 ngx_version=0
diagnostics=rtx_ngx_bridge: the D3D12/CUDA interop layer is available, but the RTX Video SDK
(nvsdk_ngx_d.dll / nvngx_vsr.dll / nvngx_truehdr.dll) is not installed, so VSR and TrueHDR
cannot run (W5 suspended: RTX_ERR_NGX). Missing: nvsdk_ngx_d.dll, nvngx_vsr.dll, nvngx_truehdr.dll.
```

即：互操作能力独立上报为可用，VSR/TrueHDR 严格为 0，不再被单个 DLL 误判为就绪。

### 2.4 W4 自测

`rtx_ngx_bridge_selftest(1920, 1080)` 在 RTX 5060 返回：

```
result=0 detail=round trip OK: 1920x1080 RGBA16F, D3D12 -> CUDA -> D3D12;
queue depth cap 2, pack stream priority high, adapter removal declared
```

trace 同时显示 `create: mid 4K texture ready` 与 `selftest: upscale mid texture verified`。

## 3. 工序 3：W6 管线合流

### 3.1 P010 融合内核

`src/shim/ngx_bridge/p010_pack.cu` 已落实专家算法终审：

- IEEE 就近偶数舍入 `__float2int_rn`（`:118` 起），再钳位 Y 64..940 / UV 64..960；
- Jiménez IGN 抖动（`:52`）分别叠加到 Y 与 UV，寄存器运算、无额外带宽；
- 以 p00 为中心的 3x3 可分离 `[1,2,1]x[1,2,1]/4` 色度低通（`:136`），相位与 VUI `topleft` 对齐；
- U/V 合并为一次 32 位合并写；每线程 2x2 亮度块；
- 内嵌 PTX：`.version 8.8`、`.target sm_75`、7643 行，由驱动 `cuModuleLoadData` JIT。

### 3.2 sm_120 实机数值验证

`out/tools/pack_verify.cpp` 构造 128x64 RGBA16F CUDA 数组（PQ 编码输入），通过桥接导出的
`rtx_ngx_pack_available` / `rtx_ngx_pack_launch` 发射内核，并用独立 CPU 参考逐码值比较。
RTX 5060 结果：

```
samples: Y=8192 UV=2048, mismatches: Y=0 UV=0
PACK_KERNEL_MATCH
```

本地 GTX 970M（sm_52）上同一探针按预期返回退出码 3（PTX JIT 拒绝），证明该结果不是无条件通过。

### 3.3 显存池合一与 65% 背压

- `src/core/vram_ring_pool.cpp:219` `SurfacePool::initializeDevice()` 为每个槽位建立一次真实设备
  分配（256 字节对齐，`kDeviceAlignment`），槽位先发布后推进失败点，避免双重分配；
- `include/rtx/vram_pool.h:186` 高水位 `high = 0.65`，低水位 0.25、临界 0.90；
- 单测 `vram_surface_pool_device_backed`、`vram_watermarks_65_percent`（12 槽位第 8 槽位触发，
  第 11 槽位临界）覆盖。

### 3.4 四重防 TDR

| 机制 | 实现 | 实机验证 |
| --- | --- | --- |
| 单帧单次提交 | `PipelineConfig::single_frame_submission = true`（`include/rtx/scheduler.h:99`），每帧独立命令列表与 flush（`src/core/pipeline_scheduler.cpp:260`） | `scheduler_single_frame_submission` |
| D3D12 队列深度 ≤ 2 | `submitted - completed` 退休计数与 `wait_for_queue_depth()`（`d3d12_interop.cpp:611`/`:622`）；调度器 35 ms 排空超时（`pipeline_scheduler.cpp:196`） | 自测 queue depth cap 2；`scheduler_queue_depth_gate` / `_errors` |
| CUDA 流优先级倒置 | `cuCtxGetStreamPriorityRange` + `cuStreamCreateWithPriority(CU_STREAM_NON_BLOCKING, greatest)`（`d3d12_interop.cpp:695`） | trace `high-priority pack stream ready`；detail `pack stream priority high` |
| 适配器移除声明与面包屑 | `DXGIDeclareAdapterRemovalSupport` 在 `cuInit` 之前由 `D3D12Api::load()` 调用一次（`d3d12_interop.cpp:124`）；`GetDeviceRemovedReason` 锁存并映射 `RTX_ERR_DEVICE_LOST` | trace `adapter removal declared`；`test_shim.cpp` 断言 |

### 3.5 自动化测试

- 本地：`rtx_tests.exe` **111/111 通过**（931 ms）；`ctest` 4/4 通过。
- RTX 5060：`rtx_tests.exe` **111/111 通过**（492 ms）；`ctest` 4/4 通过。
- 新增覆盖：`vram_tdr_queue_depth_cap`、`vram_surface_pool_device_backed`、
  `vram_watermarks_65_percent`、`scheduler_queue_depth_gate`、`scheduler_queue_depth_errors`、
  `scheduler_single_frame_submission`。

## 4. 工序 2：W5 专有桥接（阻塞）

### 4.1 阻塞事实

RTX Video SDK 的宿主库与特征库在本地与验收机均不存在：

- `nvsdk_ngx_d.dll`、`nvngx_vsr.dll`、`nvngx_truehdr.dll`：对 `C:\Program Files`、
  `C:\Program Files (x86)`、`C:\Windows\System32`、`C:\Users\[REDACTED_USER]` 递归检索，零命中；
- SDK 头文件与导入库不在本源码 drop 中；当时 CMake 在 `RTX_WITH_NGX_SDK=ON` 时按设计直接报错
  （`CMakeLists.txt:45`），因为 `src/shim/ngx_bridge/ngx_d3d12_bridge.cpp` 属于未随附的专有源码。
  2026-10-29 更新：该开关已删除，Windows configure 始终自动下载 SHA-256 锁定的 RTX Video SDK
  并编译 `ngx_d3d12_bridge.cpp`；
- 公开的 NVIDIA DLSS SDK 头文件仅定义 `NVSDK_NGX_Feature_VideoSuperResolution = 5`
  （已核对 `nvsdk_ngx_defs.h`），**没有** TrueHDR 特性/参数定义；且公开仓库不含
  `nvngx_vsr.dll` / `nvngx_truehdr.dll` 运行时；
- RTX Video SDK 官方下载要求 NVIDIA Developer 账号登录，本次未提供该账号。

因此 VSR、TrueHDR、双 Handle/双 Parameter/双 Scratch、三段式屏障与 1080p SDR → 4K HDR10
端到端均无法实现或验证。未以猜测 API 编写不可编译的桥接源码，也未提交任何 NVIDIA 专有二进制。

### 4.2 已就绪的 W5 基础设施

- 隔离加载：`load_plugin_library()` 仅搜索 `<bridge>/plugins/nvidia_rtx` 或
  `RTX_NGX_PLUGIN_DIR`（`d3d12_interop.cpp:1277`），满足“部署在源码树外”的约束；
- `mid_4k_` 中间纹理：`create_mid_texture()`（`d3d12_interop.cpp:883`），自测 trace 已验证；
- 自适应防过曝律：`TrueHdrAdapter` 默认平滑系数 0.92、切镜头瞬时跳变
  （`src/core/scene_detect.cpp:150-161`，`include/rtx/scene_detect.h:165`），有单测覆盖；
- 逻辑与探测与缺失项诊断已按 2.3 实机验证。

## 5. 工序 4：Gate 2 状态

| 门禁 | 判据 | 状态 |
| --- | --- | --- |
| Gate 2.1 | 1000 帧、≥60 fps、显存 ≤5.8 GB | 未执行：依赖 W5 全管线 |
| Gate 2.2 | Nsight：D2H=0、H2D=0、内核 1000 次、单帧 ≤1.8 ms | 未执行：`nsys` 在本地与验收机均不存在；且依赖 W5 |
| Gate 2.3 | 物理 HDR10 徽标点亮 | 未执行：依赖 W5 TrueHDR |
| Gate 2.4 | 2 小时满载零 TDR | 未执行：依赖 W5；四项防 TDR 机制已实现并通过单测/自测 |

## 6. 质量门禁与合规

- SPDX/边界审计：`PASS`（GPL=59、MIT=29、二进制=0）。
- 静态导入审计：`PASS`，检查 5 个 PE（`rtx-transcode.exe`、`rtx-transcode-base.exe`、
  `rtx_tests.exe`、`rtx_shim.dll`、`rtx_ngx_bridge.dll`），全部无
  `nvcuda/nvcuvid/nvEncodeAPI/nvsdk_ngx/nvngx/nvrtc` 静态导入。
- 未向源码树提交任何 NVIDIA 专有 DLL/头文件/导入库；插件目录约定为 `plugins/nvidia_rtx/`。

## 7. 解除阻塞所需输入

1. **RTX Video SDK 安装包**（NVIDIA Developer 账号）：提供
   `nvsdk_ngx_d.dll`、`nvngx_vsr.dll`、`nvngx_truehdr.dll`、NGX 头文件与导入库。
   部署到源码树外的 `<app_root>/plugins/nvidia_rtx/`（可用 `RTX_NGX_PLUGIN_DIR` 覆盖）。
2. **专有桥接源码** `src/shim/ngx_bridge/ngx_d3d12_bridge.cpp`，实现双 Handle/双 Parameter/
   双 Scratch、`COMMON → SRV/UAV → COMMON` 三段式屏障与 `mid_4k_` UAV 屏障。
3. **Nsight Systems CLI**（公开下载）安装到验收机，用于 Gate 2.2 的 1000 帧采集。

## 8. 复现命令

```powershell
# 单测与集成测试（本地/验收机）
build\zig\rtx_tests.exe
build\zig\rtx-transcode.exe --self-test

# P010 内核数值验证（需 sm_75+；验收机已跑）
build\pack_verify.exe

# 桥接自测与 trace（验收机）
$env:RTX_BRIDGE_TRACE='1'
powershell -File selftest_probe.ps1

# 能力探测
powershell -File caps_probe.ps1
```

---

## 更正与补充（2026-10-25，P0-1 施工后）

本报告正文的结论 **“W5 被 SDK 缺失阻塞”已被实机证据推翻**，特此更正：

1. **SDK 齐备性**：`RTX_Video_SDK_v1.1.0` 按 NVIDIA 架构**故意不提供**
   `nvsdk_ngx_d.dll`；其静态 stub `nvsdk_ngx_s.lib` 编入桥接后由驱动侧
   `_nvngx.dll`（DriverStore，已实证存在）寻址加载。项目负责人的终审
   裁决见
   [`phase2_w5_w6_and_gui_unified_construction_plan.md`](phase2_w5_w6_and_gui_unified_construction_plan.md) §1.2。
2. **W5 实机状态**（2026-10-24）：桥接层在 RTX 5060 上以合成帧验证
   VSR + TrueHDR 可运行（远端 `C:\Users\[REDACTED_USER]\rtx_w5\eval.log`：
   `vsr=1 truehdr=1`、1920×1080 R10G10B10A2 输入、3840×2160 RGBA16F
   输出、两帧评估、`RESULT: PASS`）。当时**生产转码管线尚未接入桥接**，
   因此正文所载 Gate 2 未执行的状态在当时是准确的。
3. **2026-10-25 状态**：P0-1 生产级 GPU-only remaster 链路已实现并
   贯通（NVDEC → 预处理 kernel → VSR → TrueHDR → pack v2 → NVENC，
   全程零主机像素拷贝），Gate 2.1/2.2/2.3 与 Gate GUI 已执行并签署，
   详见 [`gate2.md`](gate2.md) §8 与
   [`phase2_w5_w6_final_acceptance_report.md`](phase2_w5_w6_final_acceptance_report.md)。
   本报告正文的其余历史内容（W4/W6 记录、112→111 项测试清单等）保持
   原样作为历史；当前测试计数为 **113/113**（新增 remaster ABI 测试）。

交接文档（`交接文档.md`）中“不得把合成帧桥接自测当成端到端转码”的
告诫在本轮全部执行中得到遵守：所有门禁数字均来自
`rtx-transcode.exe` 的真实容器转码，命令与二进制哈希见 gate2.md §8。
