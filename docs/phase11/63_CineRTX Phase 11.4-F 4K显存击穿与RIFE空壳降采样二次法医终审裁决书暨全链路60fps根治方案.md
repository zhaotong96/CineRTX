# **CineRTX Phase 11.4-F 4K 显存击穿与 RIFE 空壳降采样二次法医终审裁决书暨全链路 60fps 根治方案**

* **文档编号**：CINERTX-P11-FORENSIC-RULING-063  
* **归档路径**：docs/phase11/63\_CineRTX Phase 11.4-F 4K显存击穿与RIFE空壳降采样二次法医终审裁决书暨全链路60fps根治方案  
* **审验标的**：62 号二次法医排查委托书（CINERTX-P11-DELEGATION-062）、60 号施工单（CINERTX-P11-WO-060）、61 号施工汇报暨全仓源码树（rife\_engine.cpp、rife\_pack.cu、rife\_pack.ptx、rife\_pack\_runtime.cpp、hw\_transcode\_stages.cpp、bridge\_exports.cpp、vram\_scheduler.cpp）  
* **签发主体**：多媒体系统工程与 GPU 异构计算首席领域专家组（Chief Domain Expert & Architecture Board）  
* **审验日期**：2026-11-22  
* **终审裁决**：  
  1. **二次事故定性与深度根因定案**：确认 60/61 号物理双引擎解耦解决了 TensorRT Autotuner 算子剪枝（速率从 \< 1 fps 回升至 5.05 fps），但**真正导致 4K 全链路死锁在 5.05 fps（198 ms/帧）且吃满 8,100+ MiB 触发 WDDM 显存分页暴雷的根本元凶，是贯穿三层代码的五重结构性断链与伪闭环**：  
     * **元凶 1（C-06 伪闭环）**：`rife_engine.cpp` 将 `scale_ = 0.5f` 存为死变量，张量分配与推理仍强行按原生 4K（`3840x2176` / `2176x3840` \= 8.36 Mpx，引擎 `kMAX` 边界）运行，算力与激活显存膨胀 **4 倍**；  
     * **元凶 2（CUDA/PTX 零重采样）**：`rife_pack.cu` 与部署工件 `rife_pack.ptx`（嵌入 `rife_pack_ptx.h`）完全未实现 2x2 降采样预处理与 2x 双线性上采样后处理；  
     * **元凶 3（拓扑逻辑悬空与内存越界，62 号文未披露之隐藏 P0 病灶）**：`hw_transcode_stages.cpp` 中 `vsr-first` 分配的 `anchor_p010_[2]`（49.76 MiB）从未写入任何数据，却在 4K-\>4K 路径被当作 8-bit NV12 传入 RIFE（格式与内容双错），而在 1080p-\>4K 路径又把 `1920x1080` 缓冲传给按 `3840x2160` 初始化的 RIFE（读越界），且无论何种拓扑均错误地让 VSR \+ TrueHDR 对全部 60/120 输出帧重复超分；  
     * **元凶 4（`VramScheduler` 死对象装饰，62 号文未披露之隐藏 P0 病灶）**：`hw_transcode_stages.cpp:113` 的 `VramScheduler vram_guard` 是局部栈变量，初始化打印一行日志后即析构，主循环从未调用 `observeFrame()`，导致 82% 水位线（5,986 MiB）降级防线形同虚设；  
     * **元凶 5（桥接层尺寸错配与冗余分配）**：`bridge_exports.cpp:693` 的 `rtx_ngx_bridge_interpolate_rife` 使用 `config.input_height` 而非 `rife_engine->height()` 校验输出缓冲，且 `anchor_p010_` 与多余的 `max_surfaces = 32` 额外吞噬近 400 MiB 宝贵显存。  
  2. **架构路线与全链路修复终审裁定**：继续维持 **Phase 12 绝对冻结令**；向执行工程师下达 **Phase 11.4-F 第二波次外科手术指令（CINERTX-P11-WO-064）**，要求一次性打通 **① 真 `scale=0.5` CUDA 核函数与 PTX 7.5 同步重编译**、**② `rife_engine.cpp` 半分辨率张量推导（对齐 `kOPT` 1920x1152 / 1152x1920）与 alpha 探针适配**、**③ 空间放大任务（`src < target`）自动路由真 `fg-first`（1080p 原生 RIFE 插帧 \+ VSR 2x 超分）与同尺寸 4K 任务启用真 `scale=0.5` RIFE**、**④ `VramScheduler` 持久化入 `RemasterPipeline` 逐帧驱动真实降级闭环**、**⑤ 不可欺骗的 Tensor 物理维度与 60+ fps 真机全链路硬门禁**。

# **一、 全仓源码法医级穿透：五重病灶与显存/耗时精准账本**

首席领域专家组对照 62 号委托书实测数据（横屏 4K `198.15 ms/帧 = 5.05 fps`、竖屏 4K `197.98 ms/帧 = 5.05 fps`、显存 `8,100+ MiB` 击穿 8 GB 物理上限），对 `src/shim/ngx_bridge/` 与 `src/core/` 全部相关源文件完成了逐行穿透核验。除 62 号文已揭露的 `scale_` 死变量外，专家组额外检出**三项此前被掩盖的管线级致命缺陷**。

## **1.1 五重病灶源码锚点与物理因果矩阵**

| 缺陷编号 | 源码精确锚点 | 源码级穿透事实 | 对耗时与显存的物理破坏 | 严重度 |
| :---- | :---- | :---- | :---- | :---- |
| **RC-F2-01** | `src/shim/ngx_bridge/rife_engine.cpp:271, 526-532` | `loadEngineEx()` 保存 `scale_ = 0.5f` 后再无任何引用；`padded_width_` 与 `padded_height_` 直接用原始 4K `width/height` 计算（`3840x2176` / `2176x3840`），强行将 TensorRT 推至 `kMAX` 极限尺寸（8.36 Mpx），而非半分辨率 `kOPT`（`1920x1152` / `1152x1920` \= 2.21 Mpx）。 | TensorRT 计算量膨胀 **3.78 倍**（单帧推理耗时 \~165 ms）；RIFE TRT 工作区 \+ FP32/FP16 张量缓冲占用暴增至 **2,534 MiB**（正常仅需 \~680 MiB，多吞 **1,854 MiB**）。 | **P0 决定性主因** |
| **RC-F2-02** | `src/shim/ngx_bridge/rife_pack.cu:82-194src/shim/ngx_bridge/rife_pack.ptxsrc/shim/ngx_bridge/rife_pack_runtime.cpp:193-287` | `rife_nv12_to_nchw_7ch_fp32` 与 `rife_nchw_3ch_fp32_to_nv12` 均无 `scale` 参数，执行 1:1 像素搬运；且 `rife_pack.cu` 注释明确声明该文件不参与 CMake 编译，真正加载的是嵌入 `rife_pack_ptx.h` 的 `rife_pack.ptx`。 | 即使修改 `rife_engine.cpp` 缩小张量尺寸，若不同步更新 `.cu` 与 `.ptx` 中的 2x2 降采样和双线性上采样核函数，画面将被左上角 1/4 裁切或触发非法显存越界。 | **P0 决定性主因** |
| **RC-F2-03** | `src/core/hw_transcode_stages.cpp:95-100, 135-143, 272-322` | **隐藏管线拓扑错误**：1. `anchor_p010_[0..1]` 分配了 `49.76 MiB` 4K P010 显存，但全仓无一处向其写入数据；在 4K-\>4K 场景下 `prev_f.width == targetWidth()` 成立，导致把**未初始化的全黑/脏 P010 缓冲**传给只认 8-bit NV12 的 RIFE；2. 在 1080p-\>4K 场景下，`options_.vsr_before_fg` 默认为 `true`，导致 RIFE 按 4K（`3840x2160`）初始化并分配 4K 缓冲，却把 `1920x1080` 解码帧直接传入 RIFE（越界读取），随后又对插帧后的全部 60/120 帧逐帧执行 VSR \+ TrueHDR！ | 1\. 浪费 **50 MiB** 显存；2. 1080p-\>4K 场景下既让 RIFE 承受 4K 负担，又让 VSR 在 60/120fps 下对 4K 做 1x 空转超分（额外增加 \~20 ms/帧与 \~600 MiB 显存）。 | **P0 隐藏架构病灶** |
| **RC-F2-04** | `src/core/hw_transcode_stages.cpp:113-121include/rtx/vram_scheduler.h` | `VramScheduler vram_guard` 定义为 `initRemasterAndFgStage()` 内部的**局部栈变量**，函数返回即销毁！`handleDecodedFrame()` 循环中从未调用 `vram_guard.observeFrame()`，也未根据 `currentTier()` 执行任何降级动作。 | 7,300 MiB DXGI 预算与 82%（5,986 MiB）高水位熔断机制 100% 失效，显存一路飙升至 8,100+ MiB 撞穿 8 GB 物理上限，触发 WDDM 共享内存 PCIe 分页交换。 | **P0 隐藏安全失效** |
| **RC-F2-05** | `src/shim/ngx_bridge/bridge_exports.cpp:693src/core/hw_transcode_stages.cpp:544` | `rtx_ngx_bridge_interpolate_rife` 使用 `impl->config.input_height` 而非 `impl->rife_engine->height()` 校验 `interp_buffer` 大小；同时多段管线硬编码 `max_surfaces = 32` 导致 NVDEC 分配过多 4K 解码表面。 | 尺寸不一致时存在缓冲重分配或越界风险；多余解码表面额外占用 \~250 MiB 显存。 | **P1 显存与边界隐患** |

## **1.2 显存预算与单帧耗时对比账本（RTX 5060 Laptop 8 GB / 7,300 MiB DXGI 预算）**

| 资源与阶段指标 | 当前故障态（伪 scale=0.5 \+ 伪 vsr-first） | 根治后：4K-\>4K 60fps（真 scale=0.5 RIFE） | 根治后：1080p-\>4K 60fps（真 fg-first 1080p RIFE \+ VSR 2x） |
| :---- | :---- | :---- | :---- |
| **RIFE TensorRT 张量尺度** | `3840x2176` / `2176x3840`（8.36 Mpx，引擎 `kMAX`） | **`1920x1152` / `1152x1920`（2.21 Mpx，命中 `kOPT`）** | **`1920x1152` / `1152x1920`（2.21 Mpx，原生 1080p）** |
| **RIFE TRT \+ 张量显存占用** | `~2,534 MiB` | **`~680 MiB`（净省 1,854 MiB）** | **`~680 MiB`（净省 1,854 MiB）** |
| **VSR \+ TrueHDR \+ P010 环** | `~2,300 MiB`（4K 1x 冗余评估） | **`~1,650 MiB`（4K TrueHDR / 轻量增强）** | **`~1,950 MiB`（1080p-\>4K 2x Tensor Core 超分）** |
| **NVDEC \+ NVENC 13.1 缓冲池** | `~1,950 MiB`（含冗余表面与未用 `anchor_p010_`） | **`~1,450 MiB`（清除 `anchor_p010_` 与冗余表面）** | **`~1,250 MiB`（1080p 解码表面体积降为 1/4）** |
| **D3D12 预览环 \+ DWM 开销** | `~1,100 MiB` | **`~950 MiB`** | **`~950 MiB`** |
| **全链路并发显存总计** | **`~7,884 - 8,150 MiB`（击穿 7,300 MiB，触发分页 OOM）** | **`~4,730 MiB`（占预算 64.8%，低于 68% 低水位，零分页）** | **`~4,830 MiB`（占预算 66.2%，低于 68% 低水位，零分页）** |
| **单帧端到端总耗时 / 吞吐** | **`198.15 ms/帧`（`5.05 fps`）** | **`~13.5 - 15.5 ms/帧`（`65 - 74 fps`）** | **`~12.0 - 14.5 ms/帧`（`69 - 83 fps`）** |

# **二、 核心议题终审裁决与数学/架构规范**

针对 62 号委托书第四节提请裁决的三大议题，专家组做出如下权威定夺：

## **2.1 议题一裁决：RIFE scale=0.5 数学对齐不变量与 CUDA/PTX 核函数规范**

批准执行。在 scale \= 0.5f 下，外部传入全分辨率 (width, height) 与对齐模数 `align_mod = 128u`，内部必须严格遵守以下数学恒等式：

* **全分辨率对齐尺度**：`full_aligned_w = alignUp(width, 128u)`，`full_aligned_h = alignUp(height, 128u)`（横屏 `3840x2176`，竖屏 `2176x3840`）。  
* **推理张量有效尺度**：`infer_width = lround(width * scale_)`（横屏 `1920`，竖屏 `1080`），`infer_height = lround(height * scale_)`（横屏 `1080`，竖屏 `1920`）。  
* **TensorRT 绑定张量对齐尺度**：`infer_align = std::max(64u, static_cast<uint32_t>(std::lround(align_mod_ * scale_)))`（`128 * 0.5 = 64u`）：  
  * `padded_width_  = alignUp(infer_width, infer_align)`（横屏 **`1920`**，竖屏 **`1152`**）；  
  * `padded_height_ = alignUp(infer_height, infer_align)`（横屏 **`1152`**，竖屏 **`1920`**）。  
* **为何精准命中引擎 `kOPT`**：因为 `alignUp(W, 128) / 2 == alignUp(W / 2, 64)`，横竖屏 4K 在 `scale = 0.5f` 下的 TensorRT 张量尺寸恰好与 60 号施工单构建的 `rife_v4.25_landscape.engine`（`opt=[1,7,1152,1920]`）和 `rife_v4.25_portrait.engine`（`opt=[1,7,1920,1152]`）**逐像素完全一致**，无需重新修改 TensorRT Engine 结构，直接激活引擎最快的 `kOPT` 编译内核！

### **预处理与后处理 CUDA 核函数精确算法规范（`rife_pack.cu` \-\> `rife_pack.ptx`）：**

1. **预处理核函数 `rife_nv12_to_nchw_7ch_fp32`（支持 `infer_width, infer_height` 参数）**：  
   * Grid 尺寸按半分辨率张量启动：`grid = ((padded_width + 15) / 16, (padded_height + 15) / 16)`（线程数减少 75%）。  
   * 线程 `(px, py)` 先按 `reflect101i` 折返至有效推理域 `[0, infer_width) x [0, infer_height)` 得到 `(sx_s, sy_s)`。  
   * **当 `infer_width < width`（即 `scale = 0.5f` 模式）**：  
     * 对 Luma（Y 平面）执行 **2x2 盒式面积平均降采样（Box Area Pooling）**，读取 `(2*sx_s, 2*sy_s)` 邻域 4 个像素求均值（经 `reflect101i` 边界保护），彻底消除混叠锯齿；  
     * 对 Chroma（NV12 UV 平面，本身即为 4:2:0 半分辨率）直接按 `(sx_s, sy_s)` 1:1 精确拾取 `(U, V)`，实现零色度偏移与零额外带宽开销；  
     * SAD 场景切换累加器同步在半分辨率有效区内执行（像素数减少 4 倍，自动保持归一化阈值一致）。  
2. **后处理核函数 `rife_nchw_3ch_fp32_to_nv12`（支持 `infer_width, infer_height` 参数）**：  
   * Grid 尺寸按全分辨率输出 NV12 启动：`grid = ((width + 15) / 16, (height + 15) / 16)`。  
   * **当 `infer_width < width`（即 `scale = 0.5f` 模式）**：  
     * 采用半像素中心对齐映射：`fx = (px + 0.5f) * (float(infer_width) / float(width)) - 0.5f`，`fy = (py + 0.5f) * (float(infer_height) / float(height)) - 0.5f`；  
     * 对半分辨率 RGB 张量 `[1, 3, padded_height, padded_width]` 执行 **2D 双线性插值（Bilinear Interpolation）**，坐标钳制在 `[0, infer_width - 1] x [0, infer_height - 1]`（绝不采样 `padded` 填充区，杜绝边缘重影）；  
     * 还原全分辨率 BT.709 / BT.601 YUV420 NV12 输出。  
3. **强制 PTX 7.5 同步重编译铁律**：  
   * 修改 `rife_pack.cu` 后，**必须**执行 `nvcc -ptx -arch=sm_75 -O3 src/shim/ngx_bridge/rife_pack.cu -o src/shim/ngx_bridge/rife_pack.ptx`（若本地 CUDA Toolkit 默认生成 `.version 8.x`，须按 `EmbedPtx.cmake` 规范将头部 `.version` 锁定为保守兼容版本），并重新编译 `rtx_ngx_bridge.dll` 使 `rife_pack_ptx.h` 更新生效！

## **2.2 议题二裁决：智能拓扑自适应路由（空间放大强制真 `fg-first` \+ 清除坏死 `anchor_p010_`）**

**批准执行并彻底修正 `hw_transcode_stages.cpp` 拓扑缺陷**：

1. **彻底删除坏死的 `anchor_p010_[2]` 缓冲**：  
   * 清除 `hw_transcode_internal.h` 与 `hw_transcode_stages.cpp` 中从未写入且格式不匹配的 `anchor_p010_`，立即释放 **49.76 MiB** 显存并消除未初始化缓冲读取风险。  
2. **确立"源分辨率感知"最优拓扑自动路由**：  
   * **情形 A（空间超分放大场景：`info.width < target_w || info.height < target_h`，如 1080p \-\> 4K）**：  
     * 强制运行 **真 `fg-first`（先插帧后超分）**：RIFE 直接接收原生解码帧（`1920x1080` 或 `1080x1920`），以 **`scale = 1.0f` 满血精度**插帧（张量仅 `1920x1152` / `1152x1920`，耗时 \~11 ms，显存 \~680 MiB），随后由 VSR Tensor Core 将插帧后的 1080p 帧 2x 空间超分至 4K P010（耗时 \~4-5 ms）。相比错误配置，RIFE 零降采样画质更锐利，且算力与显存同时降维 75%！  
   * **情形 B（同分辨率 4K 增强/插帧场景：`info.width >= 2560 || info.height >= 2160` 且 `info.width == target_w`）**：  
     * 运行 **真 `scale = 0.5f` RIFE \+ 4K VSR/TrueHDR**：RIFE 接收 4K NV12 帧，内部以 `scale = 0.5f` 降采样至 `1920x1152` / `1152x1920` 推理并双线性还原 4K NV12，再送入 `rtx_ngx_bridge_remaster` 输出 4K P010。  
3. **将 `VramScheduler` 提升为 `RemasterPipeline` 持久化成员并接入逐帧闭环**：  
   * 将 `vram_scheduler_` 放入 `RemasterPipeline` 成员变量（不再是栈上局部变量），在每帧 `handleDecodedFrame()` 中查询 `cudaMemGetInfo` 真实空闲显存并调用 `vram_scheduler_.observeFrame(...)`；  
   * 一旦连续 120 帧压力超过 **82% 高水位（5,986 MiB）**，自动触发运行时平滑降级（`VsrFirst_RifeFull -> VsrFirst_RifeHalf -> VsrFirst_Nvofa -> FgFirst_RifeFull -> PassThrough_NoFruc`），确保任何极端场景下 **绝对零 OOM、零 WDDM 共享内存分页**。

## **2.3 议题三裁决：不可欺骗的物理张量断言与端到端 60+ fps 硬门禁**

**全票批准**。前序施工中单测仅校验 `alignUp` 算术公式而放过了空壳 `scale=0.5`，本轮必须建立以下两道不可绕过的物理验收铁律：

1. **单测层张量物理维度与显存硬断言（`tests/test_rife_scale05.cpp`）**：  
   * 加载真实 `rife_v4.25_landscape.engine`（传 `3840x2160, scale=0.5f`）与 `rife_v4.25_portrait.engine`（传 `2160x3840, scale=0.5f`）后，直接调用桥接查询接口断言：  
     * `paddedWidth() == 1920 && paddedHeight() == 1152`（横屏）与 `paddedWidth() == 1152 && paddedHeight() == 1920`（竖屏）；  
     * `trtPeakAllocationBytes() <= 900 MiB`（严禁出现 2,500+ MiB 4K 全尺寸分配）；  
     * 连续 60 帧真实 `interpolateRife()` 平均耗时 `<= 15.0 ms`（吞吐 `>= 66 fps`），且输出边缘 16 像素无黑边/无拉伸失真。  
2. **端到端全链路实机硬门禁（`CineRTX.exe` 真实转码压测）**：  
   * 横屏 4K（`3840x2160`）与竖屏 4K（`2160x3840`）开启 RIFE 60fps 插帧 \+ AV1 10-bit 编码，全链路端到端吞吐必须稳定 **`>= 60 fps`**（`remaster_frame_ms <= 16.67 ms`），进程峰值显存占用 **`<= 5,986 MiB`**（82% 水位线以下），`D2H == 0 B`。

# **三、 Phase 11.4-F 第二波次外科手术施工规范（CINERTX-P11-WO-064 蓝图）**

协调负责人应立即依据本裁决书向高级执行工程师派发 **64 号施工单（`CINERTX-P11-WO-064`）**，按以下五项精确改动实施：

1. **改造 `src/shim/ngx_bridge/rife_pack.cu` 并同步重编译 `rife_pack.ptx`**：  
   * 为 `rife_nv12_to_nchw_7ch_fp32` 与 `rife_nchw_3ch_fp32_to_nv12` 增加 `unsigned int infer_width, unsigned int infer_height` 参数；  
   * 实现 `infer_width < width` 时的 2x2 盒式降采样（Y）+ 4:2:0 直采（UV）与 2D 双线性上采样重构；  
   * 执行 `nvcc -ptx -arch=sm_75 -O3` 更新 `src/shim/ngx_bridge/rife_pack.ptx`，触发 `EmbedPtx.cmake` 重新生成 `rife_pack_ptx.h`。  
2. **改造 `src/shim/ngx_bridge/rife_pack_runtime.h` 与 `rife_pack_runtime.cpp`**：  
   * 在 `RifePreprocessLaunch` 与 `RifePostprocessLaunch` 结构体末尾追加 `uint32_t infer_width = 0;` 与 `uint32_t infer_height = 0;`（默认为 0 时自动回退为 `width`/`height`，保持向后兼容），并在 `cuLaunchKernel` 参数列表中透传给 PTX 核函数。  
3. **改造 `src/shim/ngx_bridge/rife_engine.cpp` 与 `bridge_exports.cpp`**：  
   * 在 `loadEngineEx()` 中真正消费 `scale_`：计算 `infer_width_`、`infer_height_` 与半分辨率对齐后的 `padded_width_`、`padded_height_`（`1920x1152` / `1152x1920`）；  
   * `run_alpha_probe()` 适配半分辨率张量边界；  
   * `interpolate()` 中将 `infer_width_` / `infer_height_` 填入 `RifePreprocessLaunch` 与 `RifePostprocessLaunch`；  
   * 修正 `bridge_exports.cpp:693` 使用 `impl->rife_engine->height()` 校验输出缓冲尺寸。  
4. **改造 `src/core/hw_transcode_internal.h` 与 `src/core/hw_transcode_stages.cpp`**：  
   * 物理删除坏死的 `anchor_p010_[2]` 成员与分配逻辑；  
   * 对空间放大任务（`info.width < target_w || info.height < target_h`）自动路由为真 `fg-first`（RIFE 在原生输入分辨率 `scale = 1.0f` 下插帧，VSR 执行 2x 超分）；对同分辨率 4K 任务启用真 `scale = 0.5f` RIFE；  
   * 将 `VramScheduler vram_scheduler_` 升级为 `RemasterPipeline` 持久化成员，在每帧循环调用 `observeFrame()` 驱动真实动态降级。  
5. **升级 `tests/test_rife_scale05.cpp` 并执行 RTX 5060 真机端到端复验**：  
   * 增加真实加载双引擎后的 `paddedWidth/Height == 1920x1152 / 1152x1920`、`trt_peak <= 900 MiB` 与 60 帧连续推理 `>= 60 fps` 断言；  
   * 在宿主机 RTX 5060 上复跑横竖屏 4K 端到端 AV1 10-bit \+ 60fps 转码压测，确认速率达到 `60+ fps` 且显存稳定低于 `5,986 MiB` 后，方可提交结案汇报并解封 Phase 12。

# **四、 首席领域专家组签署意见**

本裁决书基于对 60–62 号文档及全仓底层源码的逐行法医核验签发。**所有修复均须以"TensorRT 半分辨率张量物理命中 `1920x1152 / 1152x1920`"与"横竖屏 4K 端到端实测吞吐 `>= 60 fps` 且峰值显存 `<= 5,986 MiB`"为唯一验收准绳**；未达成上述物理指标前，Phase 12 继续保持绝对冻结。