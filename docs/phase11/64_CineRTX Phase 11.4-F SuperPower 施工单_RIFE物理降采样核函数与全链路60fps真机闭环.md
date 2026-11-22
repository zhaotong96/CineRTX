# CineRTX Phase 11.4-F SuperPower 施工单：RIFE 物理降采样核函数与全链路 60fps 真机闭环

- **文档编号**：`CINERTX-P11-WO-064`
- **归档路径**：[`docs/phase11/64_CineRTX Phase 11.4-F SuperPower 施工单_RIFE物理降采样核函数与全链路60fps真机闭环.md`](./64_CineRTX%20Phase%2011.4-F%20SuperPower%20施工单_RIFE物理降采样核函数与全链路60fps真机闭环.md)
- **签发主体**：CineRTX 项目总协调负责人（Coordinator）
- **受指派方**：外部高级执行工程师（Senior Implementation Engineer）
- **当前状态**：🟡 **Phase 11.4-F 第二波次深度根治施工单已签发，提请执行工程师进场施工（ASSIGNED TO IMPLEMENTATION ENGINEER）**
- **执行基线**：Phase 11.4-F 抢险分支（Windows MSVC 19.51 / Ninja / RTX 5060 Laptop GPU 8GB / Driver `621.24 WHQL`）
- **输入与法定依据**：
  * [`docs/phase11/63_CineRTX Phase 11.4-F 4K显存击穿与RIFE空壳降采样二次法医终审裁决书暨全链路60fps根治方案.md`](./63_CineRTX%20Phase%2011.4-F%204K显存击穿与RIFE空壳降采样二次法医终审裁决书暨全链路60fps根治方案.md)（`CINERTX-P11-FORENSIC-RULING-063`）
  * [`docs/phase11/62_CineRTX Phase 11.4-F 4K全链路显存击穿与转码速率持续低迷二次法医排查专家咨询委托书.md`](./62_CineRTX%20Phase%2011.4-F%204K全链路显存击穿与转码速率持续低迷二次法医排查专家咨询委托书.md)（`CINERTX-P11-DELEGATION-062`）
  * [`DECISIONS.md`](../../DECISIONS.md)（`[DEC-008]`, `[DEC-013]`, `[DEC-014]`）
  * [`ISSUES.md`](../../ISSUES.md)（`[ISSUE-010]`）

---

## 一、 施工背景与战略攻坚目标

在前序施工中，物理双引擎解耦消除了 TensorRT Autotuner 的算子剪枝，但由于 `scale=0.5` 在底层 CUDA 算子与桥接层中仅声明了接口参数、**未实现任何物理下采样与上采样核函数（空壳参数伪闭环）**，导致系统仍以 4K 原生全尺寸（$8.36\text{ Mpx}$）硬算，使得全链路单帧耗时高达 **`198.15 ms`（仅 5.05 fps）**，并发显存突破 **8,100+ MiB** 造成硬件爆显存。

首席领域专家组在 [63 号法医终审裁决书](./63_CineRTX%20Phase%2011.4-F%204K显存击穿与RIFE空壳降采样二次法医终审裁决书暨全链路60fps根治方案.md) 中正式签发了全链路根治方案，确认：
**60 号施工单构建的横竖屏物理双引擎无需重新编译，其 `kOPT`（`1920x1152` / `1152x1920`）在数学上与 `scale=0.5` 逐像素完全一致！**

依据 `[DEC-008]` 铁律，协调负责人坚守门禁审计与物证终验职责，严禁代写业务代码。本工单对专家裁决进行工业级细化拆解，指派高级执行工程师执行第二波次外科手术，**目标达成端到端 60+ fps 满血实时吞吐，且并发显存严格低于 5,986 MiB（82% 水位线）**。

---

## 二、 任务包拆解与物理文件隔离矩阵

本次施工划分为 5 个高度正交的执行任务包：

| 任务包编号 | 任务包名称 | 核心改造目标与交付物 | 涉及物理文件（仓库相对路径） | 预计工期 |
| :---: | :--- | :--- | :--- | :---: |
| **PKG-G1** | **CUDA 降采样/上采样核函数落地与 PTX 编译** | 实现 2x2 盒式降采样预处理（Y）+ 4:2:0 直采（UV）与 2D 双线性上采样后处理；编译生成 `rife_pack.ptx` 并触发 `rife_pack_ptx.h`。 | [`src/shim/ngx_bridge/rife_pack.cu`](../../src/shim/ngx_bridge/rife_pack.cu)<br>[`src/shim/ngx_bridge/rife_pack.ptx`](../../src/shim/ngx_bridge/rife_pack.ptx) | 60 min |
| **PKG-G2** | **运行时结构体透传与 PTX 参数绑定** | `RifePreprocessLaunch` 与 `RifePostprocessLaunch` 追加 `infer_width` / `infer_height`；`rife_pack_runtime.cpp` 透传至 `cuLaunchKernel`。 | [`src/shim/ngx_bridge/rife_pack_runtime.h`](../../src/shim/ngx_bridge/rife_pack_runtime.h)<br>[`src/shim/ngx_bridge/rife_pack_runtime.cpp`](../../src/shim/ngx_bridge/rife_pack_runtime.cpp) | 30 min |
| **PKG-G3** | **桥接层张量尺度推导与命中 `kOPT`** | 真正消费 `scale_`；张量尺寸严格对齐半分辨率（横屏 `1920x1152`，竖屏 `1152x1920`）；适配 alpha 探针；修正输出高度校验。 | [`src/shim/ngx_bridge/rife_engine.cpp`](../../src/shim/ngx_bridge/rife_engine.cpp)<br>[`src/shim/ngx_bridge/bridge_exports.cpp`](../../src/shim/ngx_bridge/bridge_exports.cpp) | 40 min |
| **PKG-G4** | **双拓扑自适应路由与显存调度器持久化** | 删除坏死 `anchor_p010_[2]`；空间放大强制真 `fg-first`（1080p 原生插帧 + VSR 2x 超分）；`VramScheduler` 持久化入类并逐帧驱动。 | [`src/core/hw_transcode_internal.h`](../../src/core/hw_transcode_internal.h)<br>[`src/core/hw_transcode_stages.cpp`](../../src/core/hw_transcode_stages.cpp) | 50 min |
| **PKG-G5** | **物理张量尺寸强断言与 60+ fps 复验** | `test_rife_scale05.cpp` 增加对张量物理维度的硬断言；在 RTX 5060 实机复核端到端 60+ fps 与显存达标。 | [`tests/test_rife_scale05.cpp`](../../tests/test_rife_scale05.cpp) | 40 min |

---

## 三、 细化代码级重构细则

### 3.1 PKG-G1：CUDA 重采样核函数与 PTX 编译规范（`rife_pack.cu`）

1. **预处理核函数（`rife_nv12_to_nchw_7ch_fp32`）**：
   - 增加参数：`unsigned int infer_width, unsigned int infer_height`；
   - 启动 Grid 尺寸：按半分辨率张量启动：`grid = ((padded_width + 15) / 16, (padded_height + 15) / 16)`；
   - 线程 `(px, py)` 映射：
     ```cuda
     int sx_s = reflect101i(px, infer_width);
     int sy_s = reflect101i(py, infer_height);
     ```
   - **当 `infer_width < width`（即 `scale = 0.5f`）**：
     * **Luma Y 平面**：执行 **2x2 盒式面积均值降采样**：
       ```cuda
       int src_x0 = sx_s * 2;
       int src_y0 = sy_s * 2;
       int x0 = reflect101i(src_x0, width);
       int x1 = reflect101i(src_x0 + 1, width);
       int y0 = reflect101i(src_y0, height);
       int y1 = reflect101i(src_y0 + 1, height);
       float y_avg = (read_luma(y0, x0) + read_luma(y0, x1) + 
                      read_luma(y1, x0) + read_luma(y1, x1)) * 0.25f;
       ```
     * **Chroma UV 平面**：由于 NV12 色度本身即为 4:2:0 半分辨率，直接按 `(sx_s, sy_s)` 1:1 读取对应的 `(U, V)` 对，零色度偏移，零多余带宽消耗；
   - **当 `infer_width == width`（即 `scale = 1.0f`）**：维持既有 1:1 点对点读取。

2. **后处理核函数（`rife_nchw_3ch_fp32_to_nv12`）**：
   - 增加参数：`unsigned int infer_width, unsigned int infer_height`；
   - 启动 Grid 尺寸：按全分辨率输出 NV12 启动：`grid = ((width + 15) / 16, (height + 15) / 16)`；
   - **当 `infer_width < width`（即 `scale = 0.5f`）**：
     * 半像素中心对齐映射：
       ```cuda
       float fx = (float(px) + 0.5f) * (float(infer_width) / float(width)) - 0.5f;
       float fy = (float(py) + 0.5f) * (float(infer_height) / float(height)) - 0.5f;
       ```
     * 对半分辨率 RGB 张量 `[1, 3, padded_height, padded_width]` 执行 **2D 双线性插值**，坐标严格钳制在 `[0, infer_width - 1] x [0, infer_height - 1]`，**绝不采样 `padded` 填充区**；
     * 还原为满血 4K NV12 输出。

3. **PTX 编译执行规范**：
   - 编写完成后，执行 NVCC 编译更新 PTX：
     ```powershell
     nvcc -ptx -arch=sm_75 -O3 "src\shim\ngx_bridge\rife_pack.cu" -o "src\shim\ngx_bridge\rife_pack.ptx"
     ```
   - 确保生成的 PTX 头部 `.version` 与 `.target` 满足保守兼容性，触发 `EmbedPtx.cmake` 重新生成 `rife_pack_ptx.h`。

---

### 3.2 PKG-G2：运行时结构体透传（`rife_pack_runtime.h/.cpp`）

1. 在 `RifePreprocessLaunch` 与 `RifePostprocessLaunch` 末尾追加：
   ```cpp
   uint32_t infer_width = 0;
   uint32_t infer_height = 0;
   ```
2. 在 `rife_pack_runtime.cpp` 的 `launchRifePreprocess` 与 `launchRifePostprocess` 中，将 `infer_width` 与 `infer_height`（若为 0 则自动回退为 `width` 与 `height`）作为入参追加至 `kernel_params` 数组，正确传递给 `g_cuda.launch_kernel`。

---

### 3.3 PKG-G3：桥接层张量尺寸计算与命中 `kOPT`（`rife_engine.cpp`）

1. **数学对齐与张量尺度推导**：
   在 `RifeEngine::loadEngineEx()` 中：
   ```cpp
   const float eff_scale = (scale_ > 0.0f && scale_ <= 1.0f) ? scale_ : 1.0f;
   infer_width_ = static_cast<uint32_t>(std::lround(static_cast<float>(width) * eff_scale));
   infer_height_ = static_cast<uint32_t>(std::lround(static_cast<float>(height) * eff_scale));

   const uint32_t eff_align_mod = (align_mod_ > 0u) ? align_mod_ : 128u;
   const uint32_t infer_align = (eff_scale < 1.0f)
       ? std::max(64u, static_cast<uint32_t>(std::lround(static_cast<float>(eff_align_mod) * eff_scale)))
       : eff_align_mod;

   padded_width_ = ((infer_width_ + infer_align - 1u) / infer_align) * infer_align;
   padded_height_ = ((infer_height_ + infer_align - 1u) / infer_align) * infer_align;
   ```
   * **核验点**：
     - 横屏 4K（$3840 \times 2160$）：$3840 \times 0.5 = 1920$，$2160 \times 0.5 = 1080 \xrightarrow{\text{align 64}} \mathbf{1152}$；张量为 **`1920x1152`**；
     - 竖屏 4K（$2160 \times 3840$）：$2160 \times 0.5 = 1080 \xrightarrow{\text{align 64}} \mathbf{1152}$，$3840 \times 0.5 = 1920$；张量为 **`1152x1920`**；
     - **精准命中 60 号施工单生成的独立单 Profile 引擎的 `kOPT` 锚点！**
2. **张量缓冲分配与参数透传**：
   - `plane_pixels = padded_width_ * padded_height_`；
   - `impl_->input_bytes` 与 `output_bytes` 仅按 `plane_pixels` 分配（显存从 334 MB 降至 88 MB！）；
   - 在 `interpolate()` 中将 `infer_width_` 与 `infer_height_` 赋给 `launch` 结构体。
3. **输出尺寸校验纠偏**：
   - 在 `src/shim/ngx_bridge/bridge_exports.cpp:693` 中，将输出缓冲高度从 `impl->config.input_height` 改为使用 `impl->rife_engine->height()`。

---

### 3.4 PKG-G4：双拓扑自适应路由与显存调度器闭环（`hw_transcode_stages.cpp`）

1. **彻底物理删除 `anchor_p010_[2]`**：
   - 从 `src/core/hw_transcode_internal.h` 与 `src/core/hw_transcode_stages.cpp` 中删除 `anchor_p010_` 成员及其分配逻辑，**净释放 49.76 MiB 显存**；
   - 插帧分支一律使用合法可读的 `prev_f.planes[0]` 与 `curr_f.planes[0]`（8-bit NV12）。
2. **双拓扑自动自适应路由**：
   在 `initRemasterAndFgStage()` 中：
   ```cpp
   const bool is_spatial_upscale = (session_->info().width < session_->targetWidth() ||
                                    session_->info().height < session_->targetHeight());
   ```
   - **空间放大任务（如 1080p $\rightarrow$ 4K）**：
     * 强制路由为 **真 `fg-first`（先插帧后超分）**：
       - `rife_w = session_->info().width`（1920），`rife_h = session_->info().height`（1080）；
       - `rife_scale = 1.0f`（1080p 无需降采样，满血画质插帧，耗时仅 ~11ms）；
       - 插帧后的每一帧再送入 VSR 硬件 Tensor Core 2x 超分至 4K；
   - **同分辨率 4K 任务（如 4K $\rightarrow$ 4K）**：
     * 启用 **真 `scale=0.5f` RIFE**：
       - `rife_w = 3840`，`rife_h = 2160`，`rife_scale = 0.5f`；
       - RIFE 内部通过 PKG-G1 核函数自动降采样至 1080p 推理并双线性重构 4K。
3. **显存调度器持久化接入逐帧闭环**：
   - 将 `VramScheduler vram_scheduler_` 提升为 `RemasterPipeline` 的持久化成员变量（消除局部栈变量装饰缺陷）；
   - 在 `handleDecodedFrame()` 每帧循环中查询空闲显存并调用 `vram_scheduler_.observeFrame()`；
   - 超过 82% 水位线触发 5 级平滑自适应降级，彻底阻断物理爆显存。

---

### 3.5 PKG-G5：物理张量尺寸强断言（`tests/test_rife_scale05.cpp`）

在 `tests/test_rife_scale05.cpp` 中升级 `rife_trt_throughput_dual_engine_gate`：
1. **张量物理尺寸硬断言**：
   - 横屏引擎在 `scale=0.5f` 下，断言张量 `paddedWidth() == 1920 && paddedHeight() == 1152`；
   - 竖屏引擎在 `scale=0.5f` 下，断言张量 `paddedWidth() == 1152 && paddedHeight() == 1920`；
2. **TRT 峰值显存硬断言**：
   - 断言 `trtPeakAllocationBytes() <= 900 MiB`（严禁出现 4K 全尺寸的 2,500+ MiB 分配）；
3. **真实 GPU 推理吞吐硬断言**：
   - 连续 60 帧单帧平均耗时 $\le 15.0\text{ ms}$（吞吐 $\ge 66\text{ fps}$）。

---

## 四、 协调负责人（Coordinator）验收硬指标（Gates）

| 门禁编号 | 门禁名称 | 穿透检验方式与合格准则 | 一票否决红线 |
| :---: | :--- | :--- | :--- |
| **GATE-G1** | **CUDA/PTX 同步更新门禁** | `rife_pack.cu` 具备 2x2 降采样与双线性上采样；`rife_pack.ptx` 与 `rife_pack_ptx.h` 均已更新。 | 存在任何 1:1 点对点硬套直接否决。 |
| **GATE-G2** | **物理张量半尺寸门禁** | `test_rife_scale05.cpp` 验证张量尺寸严格为 **`1920x1152`（横）/ `1152x1920`（竖）**。 | 张量尺寸仍为 4K 全尺寸直接否决。 |
| **GATE-G3** | **TRT 显存瘦身门禁** | RIFE 峰值显存由 2,534 MiB 降至 **$\le 900\text{ MiB}$**；`anchor_p010_` 彻底移除。 | RIFE 显存 $> 1,200\text{ MiB}$ 直接否决。 |
| **GATE-G4** | **端到端 60+ fps 实机门禁** | RTX 5060 实机转码横竖屏 4K，`main_loop` 平均吞吐 **$\ge 60\text{ fps}$**（`remaster_frame_ms <= 16.67 ms`）。 | 转码速率 $< 60\text{ fps}$ 直接否决。 |
| **GATE-G5** | **82% 水位线安全门禁** | 4K 端到端转码期间，系统峰值显存稳定受控在 **$\le 5,986\text{ MiB}$**，零 WDDM 内存分页。 | 发生任何显存 OOM 或崩溃直接否决。 |

---

## 五、 执行工程师接单实施指令

请执行工程师严格遵照本工单 5 个任务包进场施工。

**严守安全底线**：严禁伪造参数，严禁空壳声明，必须确保每一行 CUDA 降采样与上采样逻辑在 PTX 中物理落地。

施工完毕后，请提交《Phase 11.4-F 第二波次施工汇报》，附带 PTX 编译日志、单测张量尺寸物证与端到端 60+ fps 实测数据，提请协调负责人执行真机穿透验收！

---
*(正文完)*
