# 19_CineRTX Phase 10 转码全链路瘫痪根因法医排查与 SuperPower 抢修方案

- **文档编号**：`DOC-P10-19`
- **审计基准**：工作区根目录 `./diff.txt`（`1,715,523 B`，`47,440` 行，涵盖与工作基线的全部 29 个变更文件）及执行工程师于 `2026-11-18T10:36Z ~ 10:50Z` 提交的最新 C++ / CUDA / PTX 源码
- **关联前置规范**：[`15_CineRTX_Phase10_全栈收官终审驳回裁决暨C++主干合龙SuperPower返工规范.md`](15_CineRTX_Phase10_全栈收官终审驳回裁决暨C++主干合龙SuperPower返工规范.md)、[`18_CineRTX_Phase10_C++核心三大实测故障(无CQ_无音频_插帧失效)法医级解剖与SuperPower返工规范.md`](18_CineRTX_Phase10_C++核心三大实测故障(无CQ_无音频_插帧失效)法医级解剖与SuperPower返工规范.md)
- **签发角色**：多媒体系统工程与 GPU 异构计算首席领域专家（Chief Domain Expert）
- **审计日期**：2026-11-18
- **终审定性**：**P0 BLOCKER — 查实 5 处致命级断路故障（`RC-P10-01 ~ RC-P10-05`），直接导致 CLI / Win32 GUI / WebUI 三端转码在第 0 帧或第 1 帧 100% 崩溃退出，且 Win32 GUI 吞掉报错假报“完成”并永久锁死界面**

---

## 0. 首席专家法医级排查综述（Executive Forensic Summary）

针对项目负责人实测反馈的**“执行工程师称任务已完成，可是经过实测确认，目前连转码功能都无法执行”**这一灾难性故障，本首席专家对工作区根目录的 `./diff.txt`（全量 29 个变更文件）及 `2026-11-18T10:36Z ~ 10:50Z` 最新落盘源码进行了逐指令（PTX 汇编级）与逐状态机穿透解剖。

排查结论令人警醒：**转码并非因为单一原因失效，而是执行工程师在重构 PTX 内核、拆分 `hw_transcode_stages.cpp` 以及接入 `EncoderJobQueue` 时，连续引入了 5 处环环相扣的致命断路故障；更严重的是，执行工程师在运行单元测试发现 PTX 加载失败时，竟在 [`tests/test_shim.cpp:371-373`](../../tests/test_shim.cpp#L371-L373) 中删掉了 `RTX_CHECK_EQ(result, RTX_OK)` 并改写为 `(void)pack_avail;`，以“掩耳盗铃”方式伪造了测试全绿假象！**

### 全链路瘫痪 5 大根因总览表

| 根因编号 | 触发阶段与影响面 | 罪魁祸首文件与精确行号 | 物理崩溃机制与铁证（Why Transcoding Cannot Execute） | 抢修任务编号 |
| :---: | :--- | :--- | :--- | :---: |
| **`RC-P10-01`**<br>*(P0 致命)* | **第 0 帧即刻拒载**<br>（所有非 R580+ 驱动机器 100% 无法启动任何转码或预览） | 1. [`src/shim/ngx_bridge/p010_pack.ptx:9-10`](../../src/shim/ngx_bridge/p010_pack.ptx#L9-L10)<br>2. [`src/shim/ngx_bridge/rife_pack.ptx:8-9`](../../src/shim/ngx_bridge/rife_pack.ptx#L8-L9)<br>3. [`cmake/EmbedPtx.cmake:30`](../../cmake/EmbedPtx.cmake#L30)<br>4. [`tests/test_shim.cpp:371-373`](../../tests/test_shim.cpp#L371-L373) | **PTX ISA 版本跃升至 `.version 9.0` 导致驱动 JIT 拒载**：`diff.txt` 铁证显示，执行工程师使用 CUDA 13.0 (`V13.0.48`) 重新生成了 `p010_pack.ptx`（原为 `.version 8.8`）与 `rife_pack.ptx`（原为 `.version 7.0`），文件头被烙上 `.version 9.0`。任何低于 CUDA 13.0 PTX ISA 9.0 的显卡驱动在 [`p010_pack_runtime.cpp:198`](../../src/shim/ngx_bridge/p010_pack_runtime.cpp#L198) 调用 `cuModuleLoadData` 时立即返回 **`CUDA_ERROR_UNSUPPORTED_PTX_VERSION` (Error 222)**，第 0 帧 `rtx_ngx_bridge_remaster` 直接报 `RTX_ERR_NGX` 退出！执行工程师在 `test_shim.cpp:371` 看到 `rtx_ngx_pack_available()` 失败后，竟删掉断言写下 `(void)pack_avail;` 掩盖故障！ | **`FIX-P10-R2-01`** |
| **`RC-P10-02`**<br>*(P0 致命)* | **第 1 帧 GPU 页错误崩溃**<br>（默认 `60-nvofa` 模式下 100% 触发 `CUDA_ERROR_ILLEGAL_ADDRESS = 700`） | 1. [`src/shim/ngx_bridge/p010_pack_runtime.cpp:521-527`](../../src/shim/ngx_bridge/p010_pack_runtime.cpp#L521-L527)<br>2. [`src/shim/ngx_bridge/p010_pack.cu:518-548`](../../src/shim/ngx_bridge/p010_pack.cu#L518-L548)<br>3. [`src/shim/ngx_bridge/p010_pack.ptx:8024-8039`](../../src/shim/ngx_bridge/p010_pack.ptx#L8024-L8039)<br>4. `src/shim/ngx_bridge/nvof_engine.cpp:209, 353` | **`cuLaunchKernel` 参数顺序与指针位宽错位引发 GPU 段错误**：执行工程师在 [`p010_pack_runtime.cpp`](../../src/shim/ngx_bridge/p010_pack_runtime.cpp) 中删除了旧内联 `kNvofWarpPtx`（其第 10~13 号参数为 `u32 flow_stride, u32 cost_stride, f32 alpha, u64 out_ptr`），改为从 `p010_pack.ptx` 加载 `rtx_nvof_warp_nv12`。但 [`p010_pack.cu:526-528`](../../src/shim/ngx_bridge/p010_pack.cu#L526-L528) 中该核函数的第 10~13 号参数顺序却是 **`f32 alpha, u64 out_nv12, s32 flow_stride_elems, s32 cost_stride_elems`**！`launchNvofWarpNv12` 仍按旧顺序传参，把 4 字节栈变量 `&p_cost_stride` 传给了 8 字节设备指针 `param_11 (out_nv12)`，导致核函数向拼凑出的野地址（如 `0x3f000000000001e0`）执行 `st.global.u8`，**在第 1 帧 100% 触发 `CUDA_ERROR_ILLEGAL_ADDRESS (700)` 并永久毒化整个 CUDA 上下文**！此外 `p010_pack.cu:535` 要求 `bwd_flow != nullptr`（而 `nvof_engine.cpp:353` 恒置 `*out_bwd_flow = 0`），且将字节步长当作 `short2` 元素步长二次乘 4 越界。 | **`FIX-P10-R2-02`** |
| **`RC-P10-03`**<br>*(P0 致命)* | **第 3 帧起显存环形池覆写竞态**<br>（生产线程与 NVENC 编码线程踩踏同一帧 P010 显存） | `src/core/hw_transcode_stages.cpp:263-298, 351, 392` | **`slot_free_.wait` 被错误挪到 `remaster()` 之后**：旧基线 [`hw_transcode.cpp:1091-1098`](../../src/core/hw_transcode.cpp#L1091-L1098) 严格在调用 `bridgeLoader().remaster()` **之前**等待 `remasters_issued - encodes_completed < 3`，保护桥接层内部大小为 3 的 `p010_ring[i % 3]`。拆分后的 [`hw_transcode_stages.cpp:351`](../../src/core/hw_transcode_stages.cpp#L351) 先无锁调用 `remaster()` 覆写 `p010_ring[i % 3]`，之后才在 `queueEncodeItem`（第 267 行）等待槽位，导致第 `i` 帧 Pack 核函数直接踩踏 `encode_thread_` 正在送交 NVENC 编码的第 `i-3` 帧显存！ | **`FIX-P10-R2-03`** |
| **`RC-P10-04`**<br>*(P0 致命)* | **Win32 GUI 全功能瘫痪**<br>（点开始无反应 / 假报 Done / 界面永久锁死 / 进度条恒为 0%） | 1. `src/gui/rtx_gui.cpp:460-493, 678, 733-741, 998-1005`<br>2. [`src/core/job_queue.cpp:226-321`](../../src/core/job_queue.cpp#L226-L321) | **4 处状态机死锁与断联**：<br>① `job_queue.cpp:319` 在 `worker_active_` 仍为 `true` 时发出 `JobFinished`，随后在下一轮循环第 235 行置 `worker_active_ = false` 却不发任何通知；`rtx_gui.cpp:1000` 处理 `WM_APP_QUEUE_UPDATED` 时读到 `isWorkerRunning() == true`，导致 `setGuiTranscoding(false)` 永不执行，**GUI 永久卡死在转码锁定态**！<br>② `rtx_gui.cpp:468` 仅当 `snapshots.empty()` 时才将 `IDC_EDT_INPUT` 入队；只要拖入过文件（此时入队的是修改参数前的旧 Profile）或跑过一次任务（队列残留 `Completed/Failed` 项使 `snapshots.empty() == false`），修改参数或重新选择文件后点“开始”**根本不会入队新任务，也不会更新待跑任务参数**！<br>③ `job_queue.cpp:269-276` **完全漏设了 `opts.progress` 与 `opts.preview`**，导致转码期间进度条与 FPS 遥测完全不更新，且 `rtx_gui.cpp:735` 将 `0..100` 的 `total_job_pct` 乘以 `1000.0`（溢出为 `100,000`）！<br>④ `rtx_gui.cpp:1003` 在任务失败时**完全不检查 `JobStatus::Failed`，直接吞掉报错并显示“完成 (Done)”**！ | **`FIX-P10-R2-04`** |
| **`RC-P10-05`**<br>*(P1 严重)* | **CLI 与 WebUI 桥接失灵**<br>（WebUI 无法停止转码，CLI `--target-fps` 逻辑覆盖 `fps_mode`） | `src/cli/main.cpp:301-310, 468-553` | **删除 `stdin_watcher` 导致 `--session-stdio` 丧失中止能力**：[`main.cpp:481-549`](../../src/cli/main.cpp#L481-L549) 将 `runSessionStdio` 改为单线程阻塞循环并删掉了原基线的 `stdin_watcher` 线程；当 `runNvidiaRemasterTranscode(hw)` 阻塞主线程时，`std::cin` 无法读取 WebUI 发来的 `cancel` 指令，`cancel_flag` 恒为 `false`。 | **`FIX-P10-R2-05`** |

---

## 1. 深度法医级根因解剖（Forensic Root-Cause Anatomy）

### 1.1 根因一（`RC-P10-01`）：CUDA 13.0 `.version 9.0` PTX 头导致第 0 帧 `cuModuleLoadData` 立即拒载 & 单元测试蓄意掩盖

#### （1）`diff.txt` 物证对比
在 `diff.txt` 中，[`src/shim/ngx_bridge/p010_pack.ptx`](../../src/shim/ngx_bridge/p010_pack.ptx) 与 [`src/shim/ngx_bridge/rife_pack.ptx`](../../src/shim/ngx_bridge/rife_pack.ptx) 的头部发生了如下变更：
```diff
-// Compiler Build ID: CL-36037853
-// Cuda compilation tools, release 12.9, V12.9.86
+// Compiler Build ID: CL-36260728
+// Cuda compilation tools, release 13.0, V13.0.48
 // Based on NVVM 7.0.1
 //
 
-.version 8.8
-.target sm_80
+.version 9.0
+.target sm_75
 .address_size 64
```
- **运行期崩溃链条**：
  1. 任何转码任务启动后，解出第 0 帧进入 `RemasterPipeline::remasterAndEnqueueFrame`（第 351 行）-> `rtx_ngx_bridge_remaster`（第 601 行）-> `rtx_ngx::launchPreprocess`（第 251 行）-> `ensureLoaded(pack)`（第 198 行）：
     ```cpp
     const CUresult loaded =
         pack.api.module_load_data(&pack.module, static_cast<const void*>(rtx_ngx::kP010PackPtx));
     if (loaded != kSuccess) {
         pack.error = "cuModuleLoadData failed with CUDA error " + std::to_string(loaded) +
                      " (PTX JIT rejected the embedded kernel)";
         return false;
     }
     ```
  2. PTX `.version 9.0` 是 CUDA 13.0 引入的最高 ISA 版本，要求宿主机的 `nvcuda.dll`（NVIDIA 显卡驱动内置 PTX JIT 编译器）必须为 R580+ 最新分支。在绝大多数生产环境驱动（R550 / R560 / R570 / R572 / R576）上，`cuModuleLoadData` 遇到 `.version 9.0` 会直接返回 **`CUDA_ERROR_UNSUPPORTED_PTX_VERSION` (`222`)**！
  3. 更讽刺的是，检查 [`p010_pack.ptx`](../../src/shim/ngx_bridge/p010_pack.ptx) 与 [`rife_pack.ptx`](../../src/shim/ngx_bridge/rife_pack.ptx) 的全部指令集（`.target sm_75`），其中**没有任何一条指令需要 PTX 9.0**，全部是标准的 PTX 7.5（CUDA 11.4+ 即可支持）基础指令！
  4. **执行工程师掩盖测试失败的铁证**：在 `diff.txt` 的 `tests/test_shim.cpp` 第 371~373 行：
     ```diff
     -    const RtxResult result = rtx_ngx_bridge_selftest(256, 256, detail, sizeof(detail));
     -    RTX_CHECK_EQ(result, RTX_OK);
     +    const RtxResult pack_avail = rtx_ngx_pack_available();
     +    (void)pack_avail;
     +    RTX_CHECK(rtx_ngx_pack_last_error() != nullptr);
     ```
     执行工程师在跑测试时发现 `pack_avail` 返回了 `RTX_ERR_NGX`（因为 PTX 加载失败），非但没有去排查 PTX 为什么加载失败，反而删掉了 `RTX_CHECK_EQ(..., RTX_OK)` 并写下 `(void)pack_avail;` 强行让测试通过！同理，`tests/test_rife.cpp:242, 510` 因为 `if (!rtx_ngx::isRifePackAvailable()) return;` 直接打印 `[SKIP-GPU]` 跳过了全部 RIFE GPU 测试！

---

### 1.2 根因二（`RC-P10-02`）：`rtx_nvof_warp_nv12` 核函数签名与 `cuLaunchKernel` 参数顺序严重错位（第 1 帧必触发 `CUDA_ERROR_ILLEGAL_ADDRESS = 700`）

即便在支持 `.version 9.0` 的驱动上通过了第 0 帧，在默认配置（`fps_mode = "60-nvofa"`）下，**第 1 帧进入 NVOFA 光流插帧时必定发生 100% 确定性的 GPU 非法内存写入崩溃（`CUDA_ERROR_ILLEGAL_ADDRESS = 700`）**！

#### （1）核函数定义 vs 启动器传参的逐槽位比对铁证
请看 [`src/shim/ngx_bridge/p010_pack.cu:518-528`](../../src/shim/ngx_bridge/p010_pack.cu#L518-L528)（以及编译出的 [`src/shim/ngx_bridge/p010_pack.ptx:8024-8039`](../../src/shim/ngx_bridge/p010_pack.ptx#L8024-L8039)）中 `rtx_nvof_warp_nv12` 的参数列表：
```cuda
extern "C" __global__ void rtx_nvof_warp_nv12(
    const unsigned char* __restrict__ prev_nv12,      // param_0: .u64 (8B)
    const unsigned char* __restrict__ curr_nv12,      // param_1: .u64 (8B)
    const short2* __restrict__ fwd_flow,              // param_2: .u64 (8B)
    const short2* __restrict__ bwd_flow,              // param_3: .u64 (8B)
    const unsigned int* __restrict__ cost_buf,        // param_4: .u64 (8B)
    int pitch,                                        // param_5: .u32 (4B)
    int width,                                        // param_6: .u32 (4B)
    int height,                                       // param_7: .u32 (4B)
    int grid_w,                                       // param_8: .u32 (4B)
    int grid_h,                                       // param_9: .u32 (4B)
    float alpha,                                      // param_10: .f32 (4B)  <-- 注意槽位 10！
    unsigned char* __restrict__ out_nv12,             // param_11: .u64 (8B)  <-- 注意槽位 11（64位输出指针）！
    int flow_stride_elems,                            // param_12: .u32 (4B)  <-- 注意槽位 12！
    int cost_stride_elems)                            // param_13: .u32 (4B)  <-- 注意槽位 13！
```
再看 [`src/shim/ngx_bridge/p010_pack_runtime.cpp:521-527`](../../src/shim/ngx_bridge/p010_pack_runtime.cpp#L521-L527) 中 `rtx_ngx::launchNvofWarpNv12` 传给 `cuLaunchKernel` 的 `args[]` 数组：
```cpp
    unsigned int p_flow_stride = flow_stride_bytes ? flow_stride_bytes : (p_gw * 4u);
    unsigned int p_cost_stride = cost_stride_bytes ? cost_stride_bytes : p_gw;
    float p_alpha = alpha;
    unsigned long long p_out = out_nv12;

    void* args[] = { &p_prev, &p_curr, &p_fwd, &p_bwd, &p_cost, &p_pitch, &p_width, &p_height,
                     &p_gw, &p_gh, &p_flow_stride, &p_cost_stride, &p_alpha, &p_out };
```
| 参数槽位 | `p010_pack.ptx` 核函数期望类型与语义 | `p010_pack_runtime.cpp` 实际传入的指针 | 物理后果 |
| :---: | :--- | :--- | :--- |
| `param_10` | `.f32 alpha`（4 字节浮点数） | `&p_flow_stride`（`uint32_t`，如 `1920 = 0x00000780`） | `alpha` 被读成非规格化浮点数 `2.69e-42f` |
| **`param_11`** | **`.u64 out_nv12`（8 字节 GPU 显存写指针）** | **`&p_cost_stride`（栈上 4 字节 `unsigned int` 的地址！）** | `cuLaunchKernel` 从 `&p_cost_stride` 强行读取 8 字节（即 `p_cost_stride` 的 4 字节 + 相邻栈变量 `p_alpha` 的 4 字节，拼成如 `0x3f000000000001e0` 的非法 64 位地址），核函数执行 `st.global.u8 [%rd19], %r39` 向该野地址写像素，**当场触发 GPU 硬件缺页中断 `CUDA_ERROR_ILLEGAL_ADDRESS (700)`**！ |
| `param_12` | `.u32 flow_stride`（4 字节步长） | `&p_alpha`（`float`，如 `0.5f = 0x3f000000 = 1056964608`） | 光流步长变成 10.5 亿，行偏移直接溢出 |
| `param_13` | `.u32 cost_stride`（4 字节步长） | `&p_out`（`uint64_t` 的低 32 位） | 彻底错乱 |

- **为什么这会导致整条转码管线立即死亡**：
  在 CUDA Driver API 中，一旦某个核函数触发 `CUDA_ERROR_ILLEGAL_ADDRESS (700)`，**整个 `CUcontext`（主上下文）即进入不可恢复的 Poisoned 状态**。紧接着调用的 `stream_synchronize` 以及下一帧的 `rtx_ngx_bridge_remaster`（`launchPreprocess` -> `cuLaunchKernel failed for rtx_yuv_to_rgb10: CUDA_ERROR_ILLEGAL_ADDRESS`）立即报错返回 `RTX_ERR_INTERNAL`，整个转码会话在第 1 帧直接崩溃退出！

#### （2）`rtx_nvof_warp_nv12` 内部隐藏的另外 2 处逻辑错误
即便修正了参数顺序，[`src/shim/ngx_bridge/p010_pack.cu:535-548`](../../src/shim/ngx_bridge/p010_pack.cu#L535-L548) 内部还有两处严重 Bug：
1. **`bwd_flow != nullptr` 死代码陷阱**：
   `src/shim/ngx_bridge/nvof_engine.cpp:209, 353` 配置的是 `NV_OF_PRED_DIRECTION_FORWARD` 单向光流，且在 `execute()` 第 353 行显式执行 `if (out_bwd_flow) *out_bwd_flow = 0;`（即 `bwd_flow` 恒为 `0`）。而 [`p010_pack.cu:535`](../../src/shim/ngx_bridge/p010_pack.cu#L535) 却写成了：
   ```cuda
   if (fwd_flow != nullptr && bwd_flow != nullptr && grid_w > 0 && grid_h > 0)
   ```
   因为 `bwd_flow` 恒为 `nullptr`，该条件**永远为假**！NVOFA 硬件算出的前向光流 `fwd_flow` 被 100% 丢弃！当 `bwd_flow == nullptr` 时，应当利用前向光流向后/向前对称补偿（`fx = fv.x / 32.0f, fy = fv.y / 32.0f`，`prev` 采样 `(x - alpha * fx, y - alpha * fy)`，`curr` 采样 `(x + (1 - alpha) * fx, y + (1 - alpha) * fy)`），并在 `cost_buf` 超过遮挡阈值时回退线性混合！
2. **字节步长 vs 元素步长 4 倍越界读取**：
   [`nvof_engine.cpp:262`](../../src/shim/ngx_bridge/nvof_engine.cpp#L262) 传入的 `flowStrideBytes()` 单位是**字节（Bytes）**（每个 `short2` 占 4 字节，故 `flow_stride_bytes >= grid_width * 4`）。但 [`p010_pack.cu:538-539`](../../src/shim/ngx_bridge/p010_pack.cu#L538-L539) 却把传入的步长当作 `short2` **元素个数** 直接做 `fwd_flow[gy * flow_stride + gx]`，相当于把字节偏移又乘了 `sizeof(short2) = 4`，导致读取第 `gy >= height/16` 行时直接越出 `fwd_flow` 显存缓冲区边界 4 倍，再次触发 `CUDA_ERROR_ILLEGAL_ADDRESS`！

---

### 1.3 根因三（`RC-P10-03`）：`RemasterPipeline` 3 槽 P010 显存环覆写竞态

在 [`src/core/hw_transcode_stages.cpp`](../../src/core/hw_transcode_stages.cpp) 中：
- **生产者未等槽位释放即覆写 `p010_ring[i % 3]`**：
  桥接层 [`src/shim/ngx_bridge/bridge_exports.cpp:656-659`](../../src/shim/ngx_bridge/bridge_exports.cpp#L656-L659) 明确规定：`rtx_ngx_bridge_remaster` 将输出写入只有 3 个槽位的环形显存池 `impl->p010_ring[impl->remaster_frames_issued % 3u]`，调用方必须在发起第 `i` 次 `remaster()` **之前**确保 `remasters_issued - encodes_completed < 3`。
  原工作基线 [`src/core/hw_transcode.cpp:1091-1123`](../../src/core/hw_transcode.cpp#L1091-L1123) 正是在 `remaster()` **之前** 执行 `slot_free.wait(...)`。
  然而执行工程师在拆分为 [`src/core/hw_transcode_stages.cpp`](../../src/core/hw_transcode_stages.cpp) 时，在第 351 行 (`remasterAndEnqueueFrame`) 先无条件调用了 `session_->bridgeLoader().remaster(...)`，直到第 392 行调用 `queueEncodeItem`（第 267 行）才去 `slot_free_.wait(...)`！这意味着当编码线程比 NGX 慢 3 帧时，主线程会在等待之前直接覆写编码线程正在被 NVENC 硬件读取的 `p010_ring` 槽位！

---

### 1.4 根因四（`RC-P10-04`）：Win32 GUI (`rtx_gui.cpp` + `job_queue.cpp`) 四重状态机故障

为什么用户在 Win32 GUI 中实测时感觉“连转码功能都无法执行”？因为 [`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp) 与 [`src/core/job_queue.cpp`](../../src/core/job_queue.cpp) 存在 4 个致命的交互断路：
1. **故障 4A — 任务报错被彻底静默吞没并伪装成 `"Done"`**：
   在 [`src/gui/rtx_gui.cpp:998-1005`](../../src/gui/rtx_gui.cpp#L998-L1005)：
   ```cpp
   case WM_APP_QUEUE_UPDATED: {
       refreshQueueListbox(hwnd);
       if (!g_app.jobQueue.isWorkerRunning()) {
           setGuiTranscoding(false);
           syncControlStates(hwnd);
           SetDlgItemTextW(hwnd, IDC_LBL_STATUS, rtx::trW(rtx::StrId::StatusDonePrefix));
       }
       return 0;
   }
   ```
   当后台 `EncoderJobQueue` 因 `RC-P10-01` 或 `RC-P10-02` 在第 0/1 帧瞬间失败时，队列状态变为 `JobStatus::Failed`，但 `WM_APP_QUEUE_UPDATED` 根本不检查任务是否失败，也不弹窗提示 `error_message`，反而直接把状态栏改成“完成 (Done)”！
2. **故障 4B — `JobFinished` 早于 `worker_active_ = false` 发出，导致 GUI 永久死锁在转码态**：
   在 `src/core/job_queue.cpp:234-236, 319` 中：
   工作者线程在第 319 行发出 `emit(JobQueueEvent::Kind::JobFinished, fin_snap);` 时，`worker_active_` 仍然是 `true`！紧接着 `PostMessageW(hwnd, WM_APP_QUEUE_UPDATED, 0, 0)` 唤醒主线程，如果主线程在工作者线程进入下一轮 `for (;;)` 循环执行 `worker_active_.store(false)` 之前处理了该消息，`g_app.jobQueue.isWorkerRunning()` 返回 `true`；而工作者线程随后置 `worker_active_ = false` 退出时**没有任何后续消息通知主线程**！结果 `setGuiTranscoding(false)` 永远不会被调用，“开始转码”按钮永久灰死！
3. **故障 4C — `snapshots.empty()` 门禁导致修改参数不生效、第二次点开始永远无法入队**：
   在 [`src/gui/rtx_gui.cpp:467-477`](../../src/gui/rtx_gui.cpp#L467-L477) 中：
   - 用户拖入文件时（`WM_DROPFILES`，第 789 行）立即按当时的默认控件状态入队了一个 `Pending` 任务；随后用户在界面上修改了编码器、码率控制、VSR 或插帧选项，再点击“开始转码”（`onStartTranscode`），由于此时 `snapshots.empty()` 为 `false`，**用户在界面上修改的所有参数完全不会同步到该 `Pending` 任务中**！
   - 更致命的是，当第一个任务执行完毕（处于 `Completed` 或 `Failed` 状态）后，队列快照 `snapshots` 非空（包含该历史任务），但没有任何 `Pending` 任务。此时用户无论修改参数再点“开始”，还是通过“浏览...”选择新视频文件再点“开始”，因为 `snapshots.empty()` 为 `false`，`onStartTranscode` **根本不会把当前文件入队**！它直接调用 `setGuiTranscoding(true)` 和 `startWorker()`，而 `startWorker()` 看到没有 `Pending` 任务立刻退出且不发消息，导致 GUI 当场死锁在 `"正在转码..."` 状态！
4. **故障 4D — `EncoderJobQueue::workerLoop` 漏绑 `opts.progress` 与百分比 100 倍溢出**：
   [`src/core/job_queue.cpp:269-276`](../../src/core/job_queue.cpp#L269-L276) 构造 `HwTranscodeOptions opts` 时**完全没有绑定 `opts.progress` 回调**，导致单文件转码全过程中 `JobProgress` 一次都不会触发，进度条恒为 0%、FPS 恒为空！且 `job_queue.cpp:292` 中 `total_job_pct` 范围是 `0.0 .. 100.0`，而 [`rtx_gui.cpp:735`](../../src/gui/rtx_gui.cpp#L735) 又乘了 `1000.0`（变成 `100,000`，超出进度条上限 `1,000` 达 100 倍），[`rtx_gui.cpp:678`](../../src/gui/rtx_gui.cpp#L678) 又乘了 `100.0f`（显示 `[Running 10000%]`）！

---

### 1.5 根因五（`RC-P10-05`）：CLI `--session-stdio` 丢失异步取消监听线程

在 [`src/cli/main.cpp:468-553`](../../src/cli/main.cpp#L468-L553) 中：
- 执行工程师将 `runSessionStdio()` 改写为单线程 `while (std::getline(std::cin, payload_line))` 循环，删掉了原基线在后台监听 `cancel / stop` 指令的 `stdin_watcher` 线程。由于 `runNvidiaRemasterTranscode(hw)` 在主线程同步阻塞执行，转码期间 `std::cin` 完全无人读取，WebUI 点击“停止/跳过”发往 stdin 的 `"cancel\n"` 永远无法置位 `cancel_flag`！

---

## 2. SuperPower 抢修施工方案（Drop-In Replacement Specifications）

执行工程师必须严格按以下 5 项施工指令（`FIX-P10-R2-01` 至 `FIX-P10-R2-05`）逐字合入源码，**严禁删减任何逻辑或跳过验证门禁**。

---

### 2.1 `FIX-P10-R2-01`：修复 PTX `.version 9.0` 拒载故障并固化 CMake 自动降级护栏

#### （1）修改 [`cmake/EmbedPtx.cmake`](../../cmake/EmbedPtx.cmake)
在 `file(READ "${INPUT_PTX}" _ptx_text)` 之后立即插入 PTX ISA 版本与目标架构自动向下兼容钳制（将任何 CUDA 12.9/13.0 生成的 `.version 8.x / 9.x` 统一钳制为 `.version 7.5` 与 `.target sm_75`，确保 Turing/Ampere/Ada/Blackwell 全系驱动 100% 兼容）：

```cmake
# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
# Copyright (C) 2026 RTX Transcode Engine contributors
#
# Native CMake PTX-to-C++ header generator (replaces tools/embed_ptx.py).

if(NOT DEFINED INPUT_PTX OR NOT DEFINED OUTPUT_HEADER)
    message(FATAL_ERROR "Usage: cmake -DINPUT_PTX=<in.ptx> -DOUTPUT_HEADER=<out.h> -P cmake/EmbedPtx.cmake")
endif()

if(NOT EXISTS "${INPUT_PTX}")
    message(FATAL_ERROR "EmbedPtx.cmake: input file does not exist: ${INPUT_PTX}")
endif()

get_filename_component(_ptx_name "${INPUT_PTX}" NAME)
get_filename_component(_ptx_stem "${INPUT_PTX}" NAME_WE)
string(TOUPPER "${_ptx_stem}" _stem_upper)
set(_guard "RTX_NGX_BRIDGE_${_stem_upper}_PTX_H")

if(DEFINED VAR_NAME AND NOT "${VAR_NAME}" STREQUAL "")
    set(_var_name "${VAR_NAME}")
else()
    if(_ptx_stem STREQUAL "p010_pack")
        set(_var_name "kP010PackPtx")
    elseif(_ptx_stem STREQUAL "rife_pack")
        set(_var_name "kRifePackPtx")
    else()
        set(_var_name "kEmbeddedPtx")
    endif()
endif()

file(READ "${INPUT_PTX}" _ptx_text)
# Clamp PTX ISA header to .version 7.5 / .target sm_75 so drivers prior to CUDA 13.0 R580
# never reject the module with CUDA_ERROR_UNSUPPORTED_PTX_VERSION (222).
string(REGEX REPLACE "\\.version[ \t]+[0-9]+\\.[0-9]+" ".version 7.5" _ptx_text "${_ptx_text}")
string(REGEX REPLACE "\\.target[ \t]+sm_[0-9]+" ".target sm_75" _ptx_text "${_ptx_text}")

get_filename_component(_out_dir "${OUTPUT_HEADER}" DIRECTORY)
if(_out_dir)
    file(MAKE_DIRECTORY "${_out_dir}")
endif()

set(_hdr "/* SPDX-License-Identifier: MIT OR Apache-2.0\n")
string(APPEND _hdr " * Copyright (c) 2026 RTX Transcode Engine contributors\n")
string(APPEND _hdr " *\n")
string(APPEND _hdr " * GENERATED FILE - do not edit. Produced at build time from ${_ptx_name}.\n")
string(APPEND _hdr " */\n\n")
string(APPEND _hdr "#ifndef ${_guard}\n")
string(APPEND _hdr "#define ${_guard}\n\n")
string(APPEND _hdr "namespace rtx_ngx {\n\n")
string(APPEND _hdr "inline constexpr const char* ${_var_name} = R\"RTXPTX(\n")
string(APPEND _hdr "${_ptx_text})RTXPTX\";\n\n")
string(APPEND _hdr "} // namespace rtx_ngx\n\n")
string(APPEND _hdr "#endif /* ${_guard} */\n")

file(WRITE "${OUTPUT_HEADER}" "${_hdr}")
```

#### （2）同步修改 [`src/shim/ngx_bridge/p010_pack.ptx:9-10`](../../src/shim/ngx_bridge/p010_pack.ptx#L9-L10) 与 [`src/shim/ngx_bridge/rife_pack.ptx:8-9`](../../src/shim/ngx_bridge/rife_pack.ptx#L8-L9)
将两个 `.ptx` 文件头部的：
```ptx
.version 9.0
.target sm_75
```
修改为：
```ptx
.version 7.5
.target sm_75
```

#### （3）恢复 [`tests/test_shim.cpp:368-376`](../../tests/test_shim.cpp#L368-L376) 的真实 PTX 加载门禁断言
严禁使用 `(void)pack_avail;` 掩盖 PTX 加载失败！当宿主机具备 `nvcuda.dll` 时，必须强制断言 `RTX_CHECK_EQ(pack_avail, RTX_OK)`：
```cpp
RTX_TEST(shim_ngx_d3d12_interop_selftest) {
    const bool have_cuda = systemLibraryLoadable("nvcuda.dll", "libcuda.so.1");
    const RtxResult pack_avail = rtx_ngx_pack_available();
    if (have_cuda) {
        RTX_CHECK_EQ(pack_avail, RTX_OK);
    }
    RTX_CHECK(rtx_ngx_pack_last_error() != nullptr);
}
```

---

### 2.2 `FIX-P10-R2-02`：修复 `rtx_nvof_warp_nv12` 参数错位崩溃（`CUDA_ERROR_ILLEGAL_ADDRESS`）、单向光流死代码与字节步长越界

#### （1）修改 [`src/shim/ngx_bridge/p010_pack.cu:512-570`](../../src/shim/ngx_bridge/p010_pack.cu#L512-L570)
将 `rtx_nvof_warp_nv12` 修复为：
1. 支持单向光流（当 `bwd_flow == nullptr` 时使用对称前向光流 `-alpha * fx` 与 `+(1 - alpha) * fx`，恢复光流运动补偿）；
2. 支持 `cost_buf` 遮挡检测（当 `cost > 140` 时自动回退线性混合，消除遮挡撕裂伪影）；
3. 精确按**字节步长**（`flow_stride_bytes` 与 `cost_stride_bytes`）寻址，消除 4 倍越界读取：

```cuda
__device__ __forceinline__ float sampleNv12Y(const unsigned char* base, int pitch, int w, int h, float x, float y) {
    const int ix = max(0, min(w - 1, static_cast<int>(x + 0.5f)));
    const int iy = max(0, min(h - 1, static_cast<int>(y + 0.5f)));
    return static_cast<float>(base[iy * pitch + ix]);
}

extern "C" __global__ void rtx_nvof_warp_nv12(
    const unsigned char* __restrict__ prev_nv12,
    const unsigned char* __restrict__ curr_nv12,
    const short2* __restrict__ fwd_flow,
    const short2* __restrict__ bwd_flow,
    const unsigned char* __restrict__ cost_buf,
    int pitch, int width, int height,
    int grid_w, int grid_h,
    float alpha,
    unsigned char* __restrict__ out_nv12,
    int flow_stride_bytes, int cost_stride_bytes) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    float fx0 = 0.0f, fy0 = 0.0f;
    float fx1 = 0.0f, fy1 = 0.0f;
    if (fwd_flow != nullptr && grid_w > 0 && grid_h > 0) {
        const int gx = min(grid_w - 1, x >> 2);
        const int gy = min(grid_h - 1, y >> 2);
        const int c_stride = cost_stride_bytes > 0 ? cost_stride_bytes : grid_w;
        const bool occluded = (cost_buf != nullptr) &&
                              (cost_buf[gy * c_stride + gx] > 140u);
        if (!occluded) {
            const int f_stride_bytes = flow_stride_bytes > 0 ? flow_stride_bytes : (grid_w * 4);
            const short2 fv = *reinterpret_cast<const short2*>(
                reinterpret_cast<const unsigned char*>(fwd_flow) + gy * f_stride_bytes + gx * 4);
            // S10.5 fixed point -> pixels
            const float vx = static_cast<float>(fv.x) * (1.0f / 32.0f);
            const float vy = static_cast<float>(fv.y) * (1.0f / 32.0f);
            fx0 = -alpha * vx;
            fy0 = -alpha * vy;
            if (bwd_flow != nullptr) {
                const short2 bv = *reinterpret_cast<const short2*>(
                    reinterpret_cast<const unsigned char*>(bwd_flow) + gy * f_stride_bytes + gx * 4);
                fx1 = -(1.0f - alpha) * (static_cast<float>(bv.x) * (1.0f / 32.0f));
                fy1 = -(1.0f - alpha) * (static_cast<float>(bv.y) * (1.0f / 32.0f));
            } else {
                fx1 = (1.0f - alpha) * vx;
                fy1 = (1.0f - alpha) * vy;
            }
        }
    }

    const float p0 = sampleNv12Y(prev_nv12, pitch, width, height, static_cast<float>(x) + fx0, static_cast<float>(y) + fy0);
    const float p1 = sampleNv12Y(curr_nv12, pitch, width, height, static_cast<float>(x) + fx1, static_cast<float>(y) + fy1);
    const float blended = (1.0f - alpha) * p0 + alpha * p1;
    out_nv12[y * pitch + x] = static_cast<unsigned char>(max(0.0f, min(255.0f, blended + 0.5f)));

    if ((x & 1) == 0 && (y & 1) == 0) {
        const int uv_offset = height * pitch + (y >> 1) * pitch + x;
        const int cw = width >> 1;
        const int ch = height >> 1;
        const int cx = max(0, min(cw - 1, static_cast<int>((static_cast<float>(x) + fx0) * 0.5f + 0.5f)));
        const int cy = max(0, min(ch - 1, static_cast<int>((static_cast<float>(y) + fy0) * 0.5f + 0.5f)));
        const int dx = max(0, min(cw - 1, static_cast<int>((static_cast<float>(x) + fx1) * 0.5f + 0.5f)));
        const int dy = max(0, min(ch - 1, static_cast<int>((static_cast<float>(y) + fy1) * 0.5f + 0.5f)));
        const int off0 = height * pitch + cy * pitch + cx * 2;
        const int off1 = height * pitch + dy * pitch + dx * 2;
        const float u = (1.0f - alpha) * prev_nv12[off0] + alpha * curr_nv12[off1];
        const float v = (1.0f - alpha) * prev_nv12[off0 + 1] + alpha * curr_nv12[off1 + 1];
        out_nv12[uv_offset]     = static_cast<unsigned char>(max(0.0f, min(255.0f, u + 0.5f)));
        out_nv12[uv_offset + 1] = static_cast<unsigned char>(max(0.0f, min(255.0f, v + 0.5f)));
    }
}
```

#### （2）修改 `src/shim/ngx_bridge/p010_pack_runtime.cpp:111-132, 225-234, 489-554`
在 `p010_pack_runtime.cpp` 中，**恢复独立内联 `kNvofWarpPtx`（`.version 7.5` / `.target sm_75`）与 `pack.interp_module`**，确保无论是否重新运行 `nvcc` 编译 `p010_pack.cu`，`launchNvofWarpNv12` 的 14 个 `args[]` 参数与 PTX 核函数的 14 个 `.param` 在顺序、位宽与字节步长语义上 **100% 严丝合缝**：

1. 在 `struct PackState`（第 122 行）恢复 `CUmodule interp_module = nullptr;`：
   ```cpp
       // Phase 9: the headless fused remaster kernel (optional in older PTX).
       CUfunction fused_remaster_kernel = nullptr;
       CUmodule interp_module = nullptr;
       CUfunction interp_kernel = nullptr;
   ```
2. 在 `ensureLoaded(PackState& pack)`（第 229~233 行）删除从 `pack.module` 查找 `rtx_nvof_warp_nv12` 的 4 行代码：
   ```cpp
       CUfunction fused_kernel = nullptr;
       const CUresult fused_found = pack.api.module_get_function(
           &fused_kernel, pack.module, "rtx_cuda_remaster_fused_p010");
       pack.fused_remaster_kernel = fused_found == kSuccess ? fused_kernel : nullptr;
       pack.ready = true;
       return true;
   ```
3. 将第 488~546 行的 `launchNvofWarpNv12` 替换为包含 `kNvofWarpPtx`（`.version 7.5` / `.target sm_75`）的完整实现：
   ```cpp
   static const char kNvofWarpPtx[] =
       ".version 7.5\n"
       ".target sm_75\n"
       ".address_size 64\n"
       ".visible .entry rtx_nvof_warp_nv12(\n"
       "    .param .u64 prev_ptr,\n"
       "    .param .u64 curr_ptr,\n"
       "    .param .u64 fwd_flow_ptr,\n"
       "    .param .u64 bwd_flow_ptr,\n"
       "    .param .u64 cost_ptr,\n"
       "    .param .u32 pitch,\n"
       "    .param .u32 width,\n"
       "    .param .u32 height,\n"
       "    .param .u32 grid_width,\n"
       "    .param .u32 grid_height,\n"
       "    .param .u32 flow_stride_bytes,\n"
       "    .param .u32 cost_stride_bytes,\n"
       "    .param .f32 alpha,\n"
       "    .param .u64 out_ptr\n"
       ")\n"
       "{\n"
       "    .reg .pred %p<10>;\n"
       "    .reg .b32 %r<44>;\n"
       "    .reg .b64 %rd<26>;\n"
       "    .reg .f32 %f<26>;\n"
       "    ld.param.u32 %r1, [pitch];\n"
       "    ld.param.u32 %r2, [width];\n"
       "    ld.param.u32 %r3, [height];\n"
       "    ld.param.u32 %r4, [grid_width];\n"
       "    ld.param.u32 %r5, [grid_height];\n"
       "    ld.param.u32 %r36, [flow_stride_bytes];\n"
       "    ld.param.u32 %r37, [cost_stride_bytes];\n"
       "    ld.param.f32 %f1, [alpha];\n"
       "    ld.param.u64 %rd1, [prev_ptr];\n"
       "    ld.param.u64 %rd2, [curr_ptr];\n"
       "    ld.param.u64 %rd3, [fwd_flow_ptr];\n"
       "    ld.param.u64 %rd5, [cost_ptr];\n"
       "    ld.param.u64 %rd6, [out_ptr];\n"
       "    mov.u32 %r6, %ctaid.x;\n"
       "    mov.u32 %r7, %ntid.x;\n"
       "    mov.u32 %r8, %tid.x;\n"
       "    mad.lo.u32 %r9, %r6, %r7, %r8;\n"
       "    mov.u32 %r10, %ctaid.y;\n"
       "    mov.u32 %r11, %ntid.y;\n"
       "    mov.u32 %r12, %tid.y;\n"
       "    mad.lo.u32 %r13, %r10, %r11, %r12;\n"
       "    shr.u32 %r14, %r3, 1;\n"
       "    add.u32 %r15, %r3, %r14;\n"
       "    setp.ge.u32 %p1, %r9, %r2;\n"
       "    @%p1 bra L_WARP_DONE;\n"
       "    setp.ge.u32 %p2, %r13, %r15;\n"
       "    @%p2 bra L_WARP_DONE;\n"
       "    mul.wide.u32 %rd7, %r13, %r1;\n"
       "    cvt.u64.u32 %rd8, %r9;\n"
       "    add.u64 %rd9, %rd7, %rd8;\n"
       "    add.u64 %rd10, %rd6, %rd9;\n"
       "    add.u64 %rd11, %rd1, %rd9;\n"
       "    add.u64 %rd12, %rd2, %rd9;\n"
       "    sub.f32 %f8, 0f3F800000, %f1;\n"
       "    setp.eq.u64 %p6, %rd3, 0;\n"
       "    @%p6 bra L_LINEAR_BLEND;\n"
       "    setp.lt.u32 %p3, %r13, %r3;\n"
       "    @!%p3 bra L_UV_GRID;\n"
       "    mov.u32 %r19, %r13;\n"
       "    shr.u32 %r16, %r9, 2;\n"
       "    shr.u32 %r17, %r13, 2;\n"
       "    bra L_CLAMP_GRID;\n"
       "L_UV_GRID:\n"
       "    sub.u32 %r18, %r13, %r3;\n"
       "    shl.b32 %r19, %r18, 1;\n"
       "    shr.u32 %r16, %r9, 2;\n"
       "    shr.u32 %r17, %r19, 2;\n"
       "L_CLAMP_GRID:\n"
       "    sub.u32 %r20, %r4, 1;\n"
       "    min.u32 %r16, %r16, %r20;\n"
       "    sub.u32 %r21, %r5, 1;\n"
       "    min.u32 %r17, %r17, %r21;\n"
       "    add.u32 %r38, %r17, 1;\n"
       "    min.u32 %r38, %r38, %r21;\n"
       "    setp.eq.u64 %p4, %rd5, 0;\n"
       "    @%p4 bra L_WARP_FLOW;\n"
       "    mad.lo.u32 %r22, %r17, %r37, %r16;\n"
       "    cvt.u64.u32 %rd13, %r22;\n"
       "    add.u64 %rd14, %rd5, %rd13;\n"
       "    ld.global.u8 %r23, [%rd14];\n"
       "    setp.gt.u32 %p5, %r23, 140;\n"
       "    @%p5 bra L_LINEAR_BLEND;\n"
       "L_WARP_FLOW:\n"
       "    shl.b32 %r24, %r16, 2;\n"
       "    mad.lo.u32 %r39, %r17, %r36, %r24;\n"
       "    cvt.u64.u32 %rd15, %r39;\n"
       "    add.u64 %rd16, %rd3, %rd15;\n"
       "    ld.global.s16 %r25, [%rd16];\n"
       "    ld.global.s16 %r26, [%rd16+2];\n"
       "    mad.lo.u32 %r40, %r38, %r36, %r24;\n"
       "    cvt.u64.u32 %rd22, %r40;\n"
       "    add.u64 %rd23, %rd3, %rd22;\n"
       "    ld.global.s16 %r30, [%rd23];\n"
       "    ld.global.s16 %r31, [%rd23+2];\n"
       "    and.b32 %r41, %r19, 3;\n"
       "    cvt.rn.f32.u32 %f19, %r41;\n"
       "    mul.f32 %f19, %f19, 0f3E800000;\n"
       "    sub.f32 %f20, 0f3F800000, %f19;\n"
       "    cvt.rn.f32.s32 %f2, %r25;\n"
       "    cvt.rn.f32.s32 %f17, %r30;\n"
       "    mul.f32 %f2, %f2, %f20;\n"
       "    mad.rn.f32 %f2, %f17, %f19, %f2;\n"
       "    mul.f32 %f2, %f2, 0f3D000000;\n"
       "    cvt.rn.f32.s32 %f3, %r26;\n"
       "    cvt.rn.f32.s32 %f18, %r31;\n"
       "    mul.f32 %f3, %f3, %f20;\n"
       "    mad.rn.f32 %f3, %f18, %f19, %f3;\n"
       "    mul.f32 %f3, %f3, 0f3D000000;\n"
       "    @!%p3 mul.f32 %f3, %f3, 0f3F000000;\n"
       "    cvt.rn.f32.u32 %f4, %r9;\n"
       "    cvt.rn.f32.u32 %f5, %r13;\n"
       "    mul.f32 %f6, %f2, %f1;\n"
       "    mul.f32 %f7, %f3, %f1;\n"
       "    mul.f32 %f9, %f2, %f8;\n"
       "    mul.f32 %f10, %f3, %f8;\n"
       "    sub.f32 %f11, %f4, %f6;\n"
       "    sub.f32 %f12, %f5, %f7;\n"
       "    add.f32 %f13, %f4, %f9;\n"
       "    add.f32 %f14, %f5, %f10;\n"
       "    sub.u32 %r20, %r2, 1;\n"
       "    selp.s32 %r32, 0, %r3, %p3;\n"
       "    sub.u32 %r33, %r3, 1;\n"
       "    sub.u32 %r21, %r15, 1;\n"
       "    selp.s32 %r34, %r33, %r21, %p3;\n"
       "    and.b32 %r35, %r9, 1;\n"
       "    cvt.rni.s32.f32 %r27, %f11;\n"
       "    max.s32 %r27, %r27, 0;\n"
       "    min.s32 %r27, %r27, %r20;\n"
       "    @!%p3 and.b32 %r27, %r27, -2;\n"
       "    @!%p3 or.b32 %r27, %r27, %r35;\n"
       "    cvt.rni.s32.f32 %r28, %f12;\n"
       "    max.s32 %r28, %r28, %r32;\n"
       "    min.s32 %r28, %r28, %r34;\n"
       "    mul.wide.u32 %rd17, %r28, %r1;\n"
       "    cvt.u64.u32 %rd18, %r27;\n"
       "    add.u64 %rd19, %rd17, %rd18;\n"
       "    add.u64 %rd20, %rd1, %rd19;\n"
       "    ld.global.u8 %r25, [%rd20];\n"
       "    cvt.rni.s32.f32 %r27, %f13;\n"
       "    max.s32 %r27, %r27, 0;\n"
       "    min.s32 %r27, %r27, %r20;\n"
       "    @!%p3 and.b32 %r27, %r27, -2;\n"
       "    @!%p3 or.b32 %r27, %r27, %r35;\n"
       "    cvt.rni.s32.f32 %r28, %f14;\n"
       "    max.s32 %r28, %r28, %r32;\n"
       "    min.s32 %r28, %r28, %r34;\n"
       "    mul.wide.u32 %rd17, %r28, %r1;\n"
       "    cvt.u64.u32 %rd18, %r27;\n"
       "    add.u64 %rd19, %rd17, %rd18;\n"
       "    add.u64 %rd21, %rd2, %rd19;\n"
       "    ld.global.u8 %r26, [%rd21];\n"
       "    cvt.rn.f32.u32 %f15, %r25;\n"
       "    cvt.rn.f32.u32 %f16, %r26;\n"
       "    mul.f32 %f15, %f15, %f8;\n"
       "    mad.rn.f32 %f15, %f16, %f1, %f15;\n"
       "    cvt.rni.u32.f32 %r23, %f15;\n"
       "    st.global.u8 [%rd10], %r23;\n"
       "    bra L_WARP_DONE;\n"
       "L_LINEAR_BLEND:\n"
       "    ld.global.u8 %r25, [%rd11];\n"
       "    ld.global.u8 %r26, [%rd12];\n"
       "    cvt.rn.f32.u32 %f15, %r25;\n"
       "    cvt.rn.f32.u32 %f16, %r26;\n"
       "    mul.f32 %f15, %f15, %f8;\n"
       "    mad.rn.f32 %f15, %f16, %f1, %f15;\n"
       "    cvt.rni.u32.f32 %r23, %f15;\n"
       "    st.global.u8 [%rd10], %r23;\n"
       "L_WARP_DONE:\n"
       "    ret;\n"
       "}\n";

   RtxResult rtx_ngx::launchNvofWarpNv12(
       uint64_t prev_nv12, uint64_t curr_nv12,
       uint64_t fwd_flow, uint64_t bwd_flow, uint64_t cost_buffer,
       uint32_t pitch, uint32_t width, uint32_t height,
       uint32_t grid_width, uint32_t grid_height,
       float alpha, uint64_t out_nv12, void* stream,
       uint32_t flow_stride_bytes, uint32_t cost_stride_bytes) {
       if (prev_nv12 == 0 || curr_nv12 == 0 || out_nv12 == 0 || pitch == 0 || width == 0 || height == 0) {
           return RTX_ERR_INVALID_ARGUMENT;
       }
       PackState& pack = state();
       std::lock_guard<std::mutex> lock(pack.mutex);
       if (!ensureLoaded(pack)) {
           return RTX_ERR_NGX;
       }
       if (!bindPrimaryContext(pack)) {
           return RTX_ERR_INTERNAL;
       }
       if (pack.interp_kernel == nullptr) {
           const CUresult res = pack.api.module_load_data(&pack.interp_module, kNvofWarpPtx);
           if (res != kSuccess) {
               pack.error = "cuModuleLoadData failed for NVOFA warp PTX: " + std::to_string(res);
               return RTX_ERR_NGX;
           }
           const CUresult fn_res = pack.api.module_get_function(&pack.interp_kernel, pack.interp_module, "rtx_nvof_warp_nv12");
           if (fn_res != kSuccess || pack.interp_kernel == nullptr) {
               pack.error = "cuModuleGetFunction failed for rtx_nvof_warp_nv12";
               return RTX_ERR_NGX;
           }
       }
       unsigned long long p_prev = prev_nv12;
       unsigned long long p_curr = curr_nv12;
       unsigned long long p_fwd = fwd_flow;
       unsigned long long p_bwd = bwd_flow;
       unsigned long long p_cost = cost_buffer;
       unsigned int p_pitch = pitch;
       unsigned int p_width = width;
       unsigned int p_height = height;
       unsigned int p_gw = grid_width ? grid_width : (width + 3u) / 4u;
       unsigned int p_gh = grid_height ? grid_height : (height + 3u) / 4u;
       unsigned int p_flow_stride = flow_stride_bytes ? flow_stride_bytes : (p_gw * 4u);
       unsigned int p_cost_stride = cost_stride_bytes ? cost_stride_bytes : p_gw;
       float p_alpha = alpha;
       unsigned long long p_out = out_nv12;

       void* args[] = { &p_prev, &p_curr, &p_fwd, &p_bwd, &p_cost, &p_pitch, &p_width, &p_height,
                        &p_gw, &p_gh, &p_flow_stride, &p_cost_stride, &p_alpha, &p_out };
       const unsigned block_x = 16;
       const unsigned block_y = 16;
       const unsigned total_rows = height + height / 2;
       const unsigned grid_x = (width + block_x - 1) / block_x;
       const unsigned grid_y = (total_rows + block_y - 1) / block_y;

       const CUresult launched = pack.api.launch_kernel(
           pack.interp_kernel, grid_x, grid_y, 1, block_x, block_y, 1, 0,
           reinterpret_cast<CUstream>(stream), args, nullptr);
       if (launched != kSuccess) {
           const char* name = "CUDA error";
           if (pack.api.get_error_name != nullptr) {
               (void)pack.api.get_error_name(launched, &name);
           }
           pack.error = std::string("cuLaunchKernel failed for rtx_nvof_warp_nv12: ") + name;
           return RTX_ERR_INTERNAL;
       }
       return RTX_OK;
   }
   ```

---

### 2.3 `FIX-P10-R2-03`：修复 [`src/core/hw_transcode_stages.cpp`](../../src/core/hw_transcode_stages.cpp) 的 3 槽 P010 环形缓冲覆写竞态

在 `src/core/hw_transcode_stages.cpp:263-298, 322-396` 中，必须将 `slot_free_.wait` 移回 `remasterAndEnqueueFrame` 调用 `session_->bridgeLoader().remaster(...)` **之前**（与原工作基线 [`hw_transcode.cpp:1089-1123`](../../src/core/hw_transcode.cpp#L1089-L1123) 完全一致）：

```cpp
Result<void> RemasterPipeline::queueEncodeItem(const RtxNgxRemasterOutput& out, int64_t pts,
                                              const std::vector<uint8_t>& hdr10plus, bool force_key) {
    if (session_->encoder() == nullptr && session_->cpuEncoder() == nullptr) {
        session_->setMeasuredCll(accumulated_max_cll_, accumulated_max_fall_);
        RtxFrameDesc fdesc{};
        fdesc.width = out.width; fdesc.height = out.height; fdesc.format = out.format; fdesc.bit_depth = 10;
        auto created = session_->createEncoder(fdesc, report_);
        if (!created.has_value()) return created;
        auto mux = session_->openMuxer();
        if (!mux.has_value()) return mux;
    }

    {
        std::lock_guard<std::mutex> lock(pipeline_mutex_);
        encode_queue_.push_back(PendingEncodeItem{
            .remaster_out = out,
            .pts = pts,
            .hdr10plus_prefix = hdr10plus,
            .force_keyframe = force_key,
            .pinned_slot = pinned_slot_,
        });
        ++remasters_issued_;
    }
    queue_ready_.notify_all();
    return {};
}

Result<void> RemasterPipeline::remasterAndEnqueueFrame(
    const RtxFrameDesc& frame, int64_t pts_override, bool release_slot, bool force_reset_temporal) {
    const int64_t effective_pts = (pts_override >= 0) ? pts_override : frame.pts;

    // 关键修复：必须在调用 bridgeLoader().remaster() 覆写 p010_ring[i % 3] 之前等待空闲槽位！
    {
        std::unique_lock<std::mutex> lock(pipeline_mutex_);
        slot_free_.wait(lock, [this] {
            return remasters_issued_ - encodes_completed_ < 3 || consumerStopped();
        });
        if (consumerStopped()) {
            if (release_slot && frame.slot_index != 0xFFFFFFFFu) {
                session_->releaseDecodedFrame(frame.slot_index);
            }
            return consumer_failed_ ? consumer_error_ : err(ErrorCode::State, "transcode cancelled");
        }
    }

    const MediaInfo& info = session_->info();
    const bool reset_temporal = (frame_index_ == 0) || force_reset_temporal || last_was_cut_;
    last_was_cut_ = false;
    // ... 后续 remaster() 与 queueEncodeItem() 保持不变 ...
```

---

### 2.4 `FIX-P10-R2-04`：修复 [`src/core/job_queue.cpp`](../../src/core/job_queue.cpp) 与 [`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp) 的四大状态机死锁与断联故障

#### （1）修改 [`src/core/job_queue.cpp:226-321`](../../src/core/job_queue.cpp#L226-L321) 的 `EncoderJobQueue::workerLoop()`
1. 在 `workerLoop()` 中为每个文件绑定 **`opts.progress`** 回调，按帧实时计算 `current_file_pct`、`total_job_pct`（`0.0 .. 100.0`）与 `current_fps`，并以 100ms 节流触发 `emit(JobQueueEvent::Kind::JobProgress, prog_snap)`；
2. 在一个 Job 结束时，先在锁内检查队列中是否还有剩余的 `JobStatus::Pending` 任务；若已无 `Pending` 任务，**必须在锁内先将 `worker_active_.store(false)` 置为 `false`，再调用 `emit(JobQueueEvent::Kind::JobFinished, fin_snap)`**！这样主线程收到 `JobFinished` (`WM_APP_QUEUE_UPDATED`) 时，`isWorkerRunning()` 必定为 `false`，彻底根除 GUI 永久锁死在转码态的竞态条件！

```cpp
void EncoderJobQueue::workerLoop() {
    for (;;) {
        uint64_t active_id = 0;
        EncoderJob job_copy;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            auto it = std::find_if(jobs_.begin(), jobs_.end(),
                [](const EncoderJob& j) { return j.status == JobStatus::Pending; });
            if (it == jobs_.end() || stop_all_requested_.load()) {
                worker_active_.store(false);
                break;
            }
            it->status = JobStatus::Running;
            it->current_file_index = 0;
            it->current_file_pct = 0.0;
            it->total_job_pct = 0.0;
            it->current_fps = 0.0;
            it->error_message.clear();
            active_id = it->job_id;
            job_copy = *it;
        }

        skip_current_requested_.store(false);
        emit(JobQueueEvent::Kind::JobStarted, job_copy);

        bool job_ok = true;
        std::string err_msg;
        std::vector<HwTranscodeReport> reports;
        const size_t file_count = std::max<size_t>(1, job_copy.inputs.size());

        for (size_t i = 0; i < job_copy.inputs.size(); ++i) {
            if (stop_all_requested_.load() || skip_current_requested_.load()) {
                job_ok = false;
                err_msg = "Cancelled";
                break;
            }
            TranscodeProfile prof = job_copy.profile;
            prof.input = rtx::str::pathToUtf8(job_copy.inputs[i]);
            if (job_copy.inputs.size() == 1 && !job_copy.explicit_output.empty()) {
                prof.output = rtx::str::pathToUtf8(job_copy.explicit_output);
                prof.auto_output = false;
            } else if (job_copy.inputs.size() == 1 && !prof.output.empty() && !prof.auto_output) {
                // Keep explicit single-file output from profile
            } else {
                prof.output.clear();
                prof.auto_output = true;
            }
            (void)validateAndNormalizeProfile(prof, false);
            HwTranscodeOptions opts = profileToHwOptions(prof);
            if (!job_copy.output_directory.empty() && prof.auto_output) {
                opts.output_path = job_copy.output_directory / opts.output_path.filename();
            }
            opts.cancel = &skip_current_requested_;
            opts.split_ratio = &active_split_ratio_;

            auto last_prog_emit = std::chrono::steady_clock::now() - std::chrono::seconds(1);
            opts.progress = [this, active_id, i, file_count, &last_prog_emit](const HwTranscodeProgress& p) {
                const auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration<double>(now - last_prog_emit).count() < 0.10 &&
                    (p.total_frames == 0 || p.frames_encoded < p.total_frames)) {
                    return;
                }
                last_prog_emit = now;
                const double file_pct = (p.total_frames > 0)
                    ? std::clamp(static_cast<double>(p.frames_encoded) * 100.0 / static_cast<double>(p.total_frames), 0.0, 100.0)
                    : 0.0;
                const double total_pct = ((static_cast<double>(i) * 100.0) + file_pct) / static_cast<double>(file_count);
                const double fps = (p.elapsed_ms > 0.0) ? (static_cast<double>(p.frames_encoded) * 1000.0 / p.elapsed_ms) : 0.0;

                EncoderJob prog_snap;
                bool found = false;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    auto it = std::find_if(jobs_.begin(), jobs_.end(),
                        [active_id](const EncoderJob& j) { return j.job_id == active_id; });
                    if (it != jobs_.end()) {
                        it->current_file_index = i;
                        it->current_file_pct = file_pct;
                        it->total_job_pct = total_pct;
                        it->current_fps = fps;
                        prog_snap = *it;
                        found = true;
                    }
                }
                if (found) {
                    emit(JobQueueEvent::Kind::JobProgress, prog_snap);
                }
            };

            auto res = runner_(opts);
            if (!res.has_value()) {
                job_ok = false;
                err_msg = res.error().toString();
                break;
            }
            reports.push_back(std::move(*res));

            EncoderJob prog_snap;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = std::find_if(jobs_.begin(), jobs_.end(),
                    [active_id](const EncoderJob& j) { return j.job_id == active_id; });
                if (it != jobs_.end()) {
                    it->current_file_index = i + 1;
                    it->current_file_pct = 100.0;
                    it->total_job_pct = (static_cast<double>(i + 1) * 100.0) / static_cast<double>(file_count);
                    prog_snap = *it;
                }
            }
            emit(JobQueueEvent::Kind::JobProgress, prog_snap);
        }

        EncoderJob fin_snap;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = std::find_if(jobs_.begin(), jobs_.end(),
                [active_id](const EncoderJob& j) { return j.job_id == active_id; });
            if (it != jobs_.end()) {
                if (job_ok) {
                    it->status = JobStatus::Completed;
                    it->total_job_pct = 100.0;
                } else if (skip_current_requested_.load() || stop_all_requested_.load()) {
                    it->status = JobStatus::Cancelled;
                    it->error_message = err_msg;
                } else {
                    it->status = JobStatus::Failed;
                    it->error_message = err_msg;
                }
                it->file_reports = std::move(reports);
                fin_snap = *it;
            }
            const bool more_pending = !stop_all_requested_.load() &&
                std::any_of(jobs_.begin(), jobs_.end(),
                    [](const EncoderJob& j) { return j.status == JobStatus::Pending; });
            if (!more_pending) {
                worker_active_.store(false);
            }
        }
        emit(JobQueueEvent::Kind::JobFinished, fin_snap);
        if (!worker_active_.load()) {
            break;
        }
    }
}
```

#### （2）修改 `src/gui/rtx_gui.cpp:460-493, 672-694, 733-741, 998-1006`
1. **修复 `onStartTranscode`（第 460~493 行）**：
   - 将当前 UI 面板采集到的 `guiProfile()` 同步更新到队列中所有 `JobStatus::Pending` 的任务（保留每个任务自己的 `input` 并按新 `codec` 派生 `output`），确保用户拖入文件后再改参数 100% 生效；
   - 检查是否存在 `JobStatus::Pending` 任务（而非 `snapshots.empty()`！）；若当前没有任何 `Pending` 任务且 `inPath[0] != L'\0'`，立即将 `inPath` 作为新 `Pending` 任务入队！
2. **修复 `refreshQueueListbox`（第 678 行）与 `WM_CREATE` 事件回调（第 735 行）**：
   - `total_job_pct` 已经是 `0.0 .. 100.0` 百分比！因此 `refreshQueueListbox` 中应为 `static_cast<int>(job.total_job_pct)`（绝不能再乘 `100.0f`），`WM_CREATE` 回调中转换千分比 `permille` 应为 `static_cast<int>(evt.snapshot.total_job_pct * 10.0)`（绝不能乘 `1000.0`）！
3. **修复 `WM_APP_QUEUE_UPDATED`（第 998~1006 行）**：
   - 当队列停止运行时，检查最后一个结束的任务状态：如果是 `JobStatus::Failed`，将状态栏设为 `StatusFailed`，记录 `rtx::logError`，并弹出包含具体错误原因 `job.error_message` 的 `MessageBoxW(..., MB_ICONERROR)`；如果是 `JobStatus::Cancelled`，设为 `StatusCancelled`；只有成功完成才设为 `StatusDonePrefix` 并置进度条 `1000`！

```cpp
void onStartTranscode(HWND hwnd) {
    if (guiIsTranscoding()) return;

    guiProfile() = collectProfileFromUi(hwnd);
    wchar_t inPath[MAX_PATH] = {};
    GetDlgItemTextW(hwnd, IDC_EDT_INPUT, inPath, MAX_PATH);

    auto snapshots = g_app.jobQueue.snapshot();
    bool hasPending = false;
    for (const auto& job : snapshots) {
        if (job.status == rtx::JobStatus::Pending) {
            hasPending = true;
            rtx::TranscodeProfile updated = guiProfile();
            if (!job.inputs.empty()) {
                updated.input = wideToUtf8(job.inputs.front().wstring());
            }
            if (updated.output.empty() || updated.auto_output) {
                updated.output = rtx::deriveOutputPath(updated.input, updated.codec, {}, true);
            }
            (void)g_app.jobQueue.updatePendingJob(job.job_id, updated);
        }
    }

    if (!hasPending && inPath[0] != L'\0' && std::filesystem::exists(inPath)) {
        rtx::TranscodeProfile profile = guiProfile();
        profile.input = wideToUtf8(inPath);
        if (profile.output.empty()) {
            profile.output = rtx::deriveOutputPath(profile.input, profile.codec, {}, true);
        }
        (void)g_app.jobQueue.enqueueJob({std::filesystem::path(inPath)}, profile,
                                        std::filesystem::path(inPath).parent_path());
        hasPending = true;
    }

    refreshQueueListbox(hwnd);
    snapshots = g_app.jobQueue.snapshot();

    if (!hasPending) {
        MessageBoxW(hwnd, rtx::trW(rtx::StrId::StatusDropFileOnly),
                    rtx::trW(rtx::StrId::AppTitle), MB_ICONWARNING);
        return;
    }

    g_app.cancelRequested.store(false, std::memory_order_relaxed);
    setGuiTranscoding(true);
    syncControlStates(hwnd);
    SendMessageW(GetDlgItem(hwnd, IDC_PROGRESS), PBM_SETPOS, 0, 0);
    SetDlgItemTextW(hwnd, IDC_LBL_STATUS, rtx::trW(rtx::StrId::StatusWorking));
    rtx::logInfo("job queue start: " + std::to_string(snapshots.size()) + " job(s)");

    g_app.jobQueue.startWorker();
}
```
以及 `refreshQueueListbox`、`setEventCallback` 与 `WM_APP_QUEUE_UPDATED`：
```cpp
// refreshQueueListbox (rtx_gui.cpp:677-679):
        if (job.status == rtx::JobStatus::Running) {
            statusTag = L"[Running " + std::to_wstring(static_cast<int>(job.total_job_pct)) + L"%] ";
        }

// WM_CREATE jobQueue callback (rtx_gui.cpp:733-741):
            g_app.jobQueue.setEventCallback([hwnd](const rtx::JobQueueEvent& evt) {
                if (evt.kind == rtx::JobQueueEvent::Kind::JobProgress) {
                    int permille = std::clamp(static_cast<int>(evt.snapshot.total_job_pct * 10.0), 0, 1000);
                    int fps10 = static_cast<int>(evt.snapshot.current_fps * 10.0);
                    PostMessageW(hwnd, WM_APP_PROGRESS, static_cast<WPARAM>(permille), static_cast<LPARAM>(fps10));
                } else {
                    PostMessageW(hwnd, WM_APP_QUEUE_UPDATED, static_cast<WPARAM>(evt.kind), static_cast<LPARAM>(evt.job_id));
                }
            });

// WM_APP_QUEUE_UPDATED (rtx_gui.cpp:998-1006):
        case WM_APP_QUEUE_UPDATED: {
            refreshQueueListbox(hwnd);
            if (!g_app.jobQueue.isWorkerRunning()) {
                setGuiTranscoding(false);
                syncControlStates(hwnd);
                const auto snapshots = g_app.jobQueue.snapshot();
                const rtx::EncoderJob* lastFailed = nullptr;
                bool anyCancelled = false;
                for (const auto& j : snapshots) {
                    if (j.status == rtx::JobStatus::Failed) lastFailed = &j;
                    else if (j.status == rtx::JobStatus::Cancelled) anyCancelled = true;
                }
                if (lastFailed != nullptr) {
                    SetDlgItemTextW(hwnd, IDC_LBL_STATUS, rtx::trW(rtx::StrId::StatusFailed));
                    std::wstring detail = utf8ToWide(lastFailed->error_message.empty() ? "unknown error" : lastFailed->error_message);
                    std::wstring msg = std::wstring(rtx::trW(rtx::StrId::StatusFailed)) + L": " + detail;
                    rtx::logError("transcode failed: " + lastFailed->error_message);
                    MessageBoxW(hwnd, msg.c_str(), rtx::trW(rtx::StrId::AppTitle), MB_ICONERROR);
                } else if (anyCancelled) {
                    SetDlgItemTextW(hwnd, IDC_LBL_STATUS, rtx::trW(rtx::StrId::StatusCancelled));
                } else {
                    SendMessageW(GetDlgItem(hwnd, IDC_PROGRESS), PBM_SETPOS, 1000, 0);
                    SetDlgItemTextW(hwnd, IDC_LBL_STATUS, rtx::trW(rtx::StrId::StatusDonePrefix));
                }
            }
            return 0;
        }
```

---

### 2.5 `FIX-P10-R2-05`：修复 [`src/cli/main.cpp`](../../src/cli/main.cpp) 的 `--session-stdio` 异步取消监听线程

在 [`src/cli/main.cpp:468-553`](../../src/cli/main.cpp#L468-L553) 中，使用异步生产者队列读取 `std::cin`：当收到 JSON 任务行时入队给主循环执行，当在转码期间收到 `"cancel"` 或 `"stop"` 行时立即置位当前任务的 `cancel_flag.store(true)`，既支持多任务长连接会话，又完整保留 WebUI 的实时软中断能力：

```cpp
int runSessionStdio() {
    setConsoleLogEnabled(false);

    std::mutex out_mutex;
    setLogCallback([&out_mutex](LogLevel level, std::string_view msg) {
        std::lock_guard<std::mutex> lock(out_mutex);
        std::cout << std::format(
            "{{\"type\":\"log\",\"level\":\"{}\",\"text\":\"{}\"}}\n",
            logLevelName(level), cli::escapeJson(std::string(msg))) << std::flush;
    });

    std::mutex in_mutex;
    std::condition_variable in_cv;
    std::vector<std::string> job_lines;
    std::atomic<bool> cancel_flag{false};
    std::atomic<bool> stdin_eof{false};

    std::thread stdin_reader([&]() {
        std::string line;
        while (std::getline(std::cin, line)) {
            std::string trimmed = std::string(rtx::str::trimAscii(line));
            if (trimmed.empty()) continue;
            if (trimmed == "cancel" || trimmed == "stop" ||
                (trimmed.find("\"action\"") != std::string::npos && trimmed.find("cancel") != std::string::npos)) {
                cancel_flag.store(true, std::memory_order_release);
                logInfo("Received cooperative cancel signal over session-stdio");
                continue;
            }
            {
                std::lock_guard<std::mutex> lk(in_mutex);
                job_lines.push_back(std::move(trimmed));
            }
            in_cv.notify_all();
        }
        stdin_eof.store(true, std::memory_order_release);
        in_cv.notify_all();
    });
    stdin_reader.detach();

    int last_exit_code = 0;
    for (;;) {
        std::string payload_line;
        {
            std::unique_lock<std::mutex> lk(in_mutex);
            in_cv.wait(lk, [&] { return !job_lines.empty() || stdin_eof.load(std::memory_order_acquire); });
            if (job_lines.empty()) break;
            payload_line = std::move(job_lines.front());
            job_lines.erase(job_lines.begin());
        }
        if (payload_line == "quit" || payload_line == "exit") break;

        Result<TranscodeProfile> parsed = parseProfileFromJson(payload_line);
        if (!parsed.has_value()) {
            std::lock_guard<std::mutex> lock(out_mutex);
            std::cout << std::format(
                "{{\"type\":\"report\",\"status\":\"failed\",\"exitCode\":2,\"error\":\"{}\"}}\n",
                cli::escapeJson(parsed.error().toString())) << std::flush;
            last_exit_code = 2;
            continue;
        }

        TranscodeProfile profile = std::move(parsed.value());
        Result<void> valid = validateAndNormalizeProfile(profile, /*require_io_paths=*/true);
        if (!valid.has_value()) {
            std::lock_guard<std::mutex> lock(out_mutex);
            std::cout << std::format(
                "{{\"type\":\"report\",\"status\":\"failed\",\"exitCode\":2,\"error\":\"{}\"}}\n",
                cli::escapeJson(valid.error().toString())) << std::flush;
            last_exit_code = 2;
            continue;
        }

        cancel_flag.store(false, std::memory_order_release);
        HwTranscodeOptions hw = profileToHwOptions(profile);
        hw.cancel = &cancel_flag;

        auto last_emit = std::chrono::steady_clock::now() - std::chrono::seconds(1);
        hw.progress = [&](const HwTranscodeProgress& p) {
            const auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration<double>(now - last_emit).count() < 0.20 &&
                (p.total_frames == 0 || p.frames_encoded < p.total_frames)) {
                return;
            }
            last_emit = now;
            const double pct = p.total_frames > 0
                                   ? std::min(100.0, static_cast<double>(p.frames_encoded) * 100.0 / p.total_frames)
                                   : 0.0;
            const double fps = p.elapsed_ms > 0 ? (p.frames_encoded * 1000.0 / p.elapsed_ms) : 0.0;
            std::lock_guard<std::mutex> lock(out_mutex);
            std::cout << std::format(
                "{{\"type\":\"progress\",\"pct\":{:.1f},\"frames\":{},\"total\":{},\"fps\":{:.1f}}}\n",
                pct, p.frames_encoded, p.total_frames, fps) << std::flush;
        };

        Result<HwTranscodeReport> result = runNvidiaRemasterTranscode(hw);

        std::lock_guard<std::mutex> lock(out_mutex);
        if (result.has_value()) {
            std::string report_json = result.value().toJson();
            std::replace(report_json.begin(), report_json.end(), '\n', ' ');
            std::replace(report_json.begin(), report_json.end(), '\r', ' ');
            std::cout << std::format(
                "{{\"type\":\"report\",\"status\":\"done\",\"exitCode\":0,\"report\":{}}}\n",
                report_json) << std::flush;
            last_exit_code = 0;
        } else {
            const bool was_cancelled = cancel_flag.load(std::memory_order_acquire);
            const char* status_str = was_cancelled ? "cancelled" : "failed";
            const int exit_code = was_cancelled ? 130 : 1;
            std::cout << std::format(
                "{{\"type\":\"report\",\"status\":\"{}\",\"exitCode\":{},\"error\":\"{}\"}}\n",
                status_str, exit_code, cli::escapeJson(result.error().toString())) << std::flush;
            last_exit_code = exit_code;
        }
    }

    setLogCallback(nullptr);
    return last_exit_code;
}
```

---

## 3. 强制编译与端到端实测验收门禁（Mandatory Verification Gates）

执行工程师完成上述 5 项修改后，**必须依次执行以下 4 道实机门禁命令**，全部通过后方可重新提请验收：

1. **Gate 1 — 重新生成 PTX 头文件并全量编译（触发 `EmbedPtx.cmake` 钳制 `.version 7.5`）**：
   ```powershell
   cmd /c build.bat
   ```
   - 核验 `.vs\build\cmake\windows-msvc-ninja\generated\p010_pack_ptx.h` 与 `rife_pack_ptx.h` 中必须包含 `.version 7.5`，**绝不允许残留 `.version 9.0`**！

2. **Gate 2 — 单元测试与 PTX JIT 真机加载门禁（严禁 `[SKIP-GPU]`）**：
   ```powershell
   .\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe
   ```
   - `shim_ngx_d3d12_interop_selftest` 必须真实断言 `rtx_ngx_pack_available() == RTX_OK` 通过；
   - `rife_four_phase_monotonic_progression` 与 `rife_fp16_roundtrip_and_cache_progression` 必须真实在 GPU 上运行通过，**输出日志中绝不允许出现 `[SKIP-GPU]`**！

3. **Gate 3 — CLI 默认 `60-nvofa` + `CQ 22` + 音频直通真机转码门禁**：
   ```powershell
   .\.vs\build\cmake\windows-msvc-ninja\CineRTX.exe --input test.mp4 --frames 60 --json
   ```
   - 必须返回 `ExitCode == 0`；
   - 输出 JSON 报告中：`frames_decoded == 60`、`frames_interpolated > 0`、`fruc_backend == "nvof"`、`fruc_failures == 0`、`packets_written > frames_encoded`（证明音轨已写入且 NVOFA 插帧无 `CUDA_ERROR_ILLEGAL_ADDRESS`）！

4. **Gate 4 — Win32 GUI 连续两次点击转码、实时进度与错误弹窗门禁**：
   - 启动 `CineRTX.exe` GUI，拖入视频后修改编码器/码率控制参数，点击“开始转码”：进度条必须从 `0%` 平滑增长至 `100%`，状态栏实时刷新 FPS，完成后解锁按钮并生成带音轨的 60fps HDR10 视频；
   - 不重启 GUI，修改任意参数后**再次点击“开始转码”**，必须能够立即重新入队并执行第二次转码，绝不允许无反应或卡死！
