# CineRTX Phase 9：SuperPower 施工单 (Track 9B)
## TensorRT 推理引擎重构、Profile 尺寸自适应与时序帧对缓存

- **任务编号**：`SP-P9-TRACK-9B`
- **执行责任**：TensorRT 深度学习推理与显存优化执行工程师
- **涉及模块与文件**：
  - `src/shim/ngx_bridge/rife_engine.h`
  - `src/shim/ngx_bridge/rife_engine.cpp`
  - `src/shim/ngx_bridge/bridge_exports.cpp`
- **依赖前置**：Track 9A（依赖其提供的稳定 PTX 内核）

---

## 一、缺陷机理与施工目标

### 1.1 缺陷定位
1. **初始化漏检假阳性**：`loadEngine()` 未调用 `isRifePackAvailable()`，PTX JIT 失败时仍打印引擎加载成功日志，直到首次插帧时才暴雷。
2. **Shape Profile 尺寸死锁**：代码强制将 `1080` 向上对齐 64 像素至 `1088` 并调用 `(void)setInputShape`。若外部预编译引擎的 Profile 最大高度为 `1080`（如标准 `1x7x1080x1920`），`setInputShape` 返回失败被忽略，导致每帧 `enqueueV3` 报错。
3. **120fps 预处理带宽浪费与同步停顿**：从 24fps 插至 120fps 时，同一对输入源帧 $(F_k, F_{k+1})$ 在 $\alpha \in \{0.2, 0.4, 0.6, 0.8\}$ 连续调用 4 次。当前每次均重复执行 NV12 $\to$ RGB 转换与 `cuStreamSynchronize` + `cuMemcpyDtoH` 回传 SAD，造成 75% 显存带宽浪费与 3 次多余的 CPU 阻塞。
4. **缺少模型 $\alpha$ 响应能力自检**：无法识别由旧版代码或缺陷 ONNX 导出的“固定 $0.5$ 占位模型”。

### 1.2 施工目标
- `loadEngine()` 入口强制前置校验 PTX Runtime；
- 动态查询 TensorRT Engine 的优化 Profile，自适应采用真实尺寸（`1080`）或对齐尺寸（`1088`），并对 `setInputShape` 返回值做严格校验；
- 实现 **帧对缓存（Frame-Pair Caching）**：在同一帧对的后续插值点（$\alpha = 0.4, 0.6, 0.8$），直接复用显存中的前 6 通道 RGB 与已计算的 SAD，仅用 `cuMemsetD32Async` 更新第 6 通道 `alpha`；
- 在加载阶段运行 1ms 级 $\alpha$ 时序敏感度探针，断绝“无视 $\alpha$ 静态模型”混入。

---

## 二、逐行施工与代码变更清单

### 2.1 改造 `src/shim/ngx_bridge/rife_engine.h`
在 `RifeEngine` 类中增加前置校验状态与帧对缓存状态接口：
```cpp
class RifeEngine {
public:
    // ... 原有接口 ...
    bool hasValidAlphaResponse() const noexcept;
    void resetFramePairCache() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
```

### 2.2 改造 `src/shim/ngx_bridge/rife_engine.cpp`
1. **`loadEngine` 入口增加 PTX 前置校验**：
   ```cpp
   if (!isRifePackAvailable()) {
       if (error) *error = "RIFE PTX pre/post-processing module failed to JIT compile: " +
                           std::string(rifePackLastError());
       return false;
   }
   ```
2. **自适应 Shape Profile 判定（替换硬编码 64 对齐）**：
   ```cpp
   // 查询模型输入张量的维度限制
   const nvinfer1::Dims tensor_dims = impl_->engine->getTensorShape(impl_->in_name.c_str());
   const int aligned_w = ((width + 63) / 64) * 64;
   const int aligned_h = ((height + 63) / 64) * 64;

   if (tensor_dims.nbDims == 4 && tensor_dims.d[2] > 0 && tensor_dims.d[3] > 0) {
       // 静态尺寸引擎：直接遵从引擎硬编码
       padded_height_ = static_cast<uint32_t>(tensor_dims.d[2]);
       padded_width_  = static_cast<uint32_t>(tensor_dims.d[3]);
   } else if (impl_->engine->getNbOptimizationProfiles() > 0) {
       const nvinfer1::Dims min_d = impl_->engine->getProfileShape(
           impl_->in_name.c_str(), 0, nvinfer1::OptProfileSelector::kMIN);
       const nvinfer1::Dims max_d = impl_->engine->getProfileShape(
           impl_->in_name.c_str(), 0, nvinfer1::OptProfileSelector::kMAX);
       if (min_d.nbDims == 4 && max_d.nbDims == 4) {
           // 若 64 对齐后落在 Profile 内部则采用对齐尺寸；否则采用源尺寸
           padded_height_ = (aligned_h >= min_d.d[2] && aligned_h <= max_d.d[2]) ? aligned_h : height;
           padded_width_  = (aligned_w >= min_d.d[3] && aligned_w <= max_d.d[3]) ? aligned_w : width;
       }
   } else {
       padded_width_  = aligned_w;
       padded_height_ = aligned_h;
   }

   nvinfer1::Dims4 dims{1, 7, static_cast<int32_t>(padded_height_), static_cast<int32_t>(padded_width_)};
   if (!impl_->context->setInputShape(impl_->in_name.c_str(), dims)) {
       if (error) *error = "TensorRT setInputShape failed for shape [1, 7, " +
                           std::to_string(padded_height_) + ", " + std::to_string(padded_width_) + "]";
       return false;
   }
   ```

3. **实现帧对缓存（Frame-Pair Cache）**：
   在 `Impl` 结构体中记录：
   ```cpp
   uint64_t last_prev_nv12 = 0;
   uint64_t last_curr_nv12 = 0;
   uint32_t last_pitch = 0;
   int32_t  last_scene_cut = 0;
   ```
   在 `interpolate(...)` 核心逻辑中：
   ```cpp
   const bool same_pair = (prev_nv12 == impl_->last_prev_nv12 &&
                           curr_nv12 == impl_->last_curr_nv12 &&
                           pitch == impl_->last_pitch);

   if (!same_pair) {
       // 1. 新的一对源帧：重置 SAD，执行全量 7-channel NV12->RGB 预处理
       impl_->last_prev_nv12 = prev_nv12;
       impl_->last_curr_nv12 = curr_nv12;
       impl_->last_pitch = pitch;

       // 启动完整预处理核函数（包含 SAD 规约）
       prepRes = launchRifePreprocess(launch, stream);
       if (prepRes != RTX_OK) return prepRes;

       // 仅在首次同步并判断场景硬切
       if (impl_->d_sad_sum && g_cuda.stream_sync && g_cuda.memcpy_dtoh) {
           g_cuda.stream_sync(stream);
           uint32_t host_sad = 0;
           g_cuda.memcpy_dtoh(&host_sad, impl_->d_sad_sum, sizeof(uint32_t));
           const float total_pixels = static_cast<float>(width_) * static_cast<float>(height_);
           const float diff_norm = (total_pixels > 0.0f) ? (static_cast<float>(host_sad) / (total_pixels * 255.0f)) : 0.0f;
           impl_->last_scene_cut = (diff_norm > 0.35f) ? 1 : 0;
       }
   } else {
       // 2. 同一对源帧（alpha = 0.4, 0.6, 0.8）：
       // 前 6 个通道图像数据已在 GPU 显存，SAD 已经算过，无需再做 NV12->RGB 与 CPU 同步！
       // 仅需将新的 alpha 广播写入第 6 通道平面
       const size_t plane_pixels = static_cast<size_t>(padded_width_) * padded_height_;
       const uint64_t ch6_addr = impl_->d_input_tensor + 6 * plane_pixels * sizeof(float);
       uint32_t alpha_bits = 0;
       std::memcpy(&alpha_bits, &alpha, sizeof(float));
       if (g_cuda.memset_d32_async) {
           g_cuda.memset_d32_async(ch6_addr, alpha_bits, plane_pixels, stream);
       }
   }

   if (scene_cut) *scene_cut = impl_->last_scene_cut;
   if (impl_->last_scene_cut != 0) {
       // 场景硬切就近回退
       const uint64_t src = (alpha < 0.5f) ? prev_nv12 : curr_nv12;
       const size_t total_nv12_bytes = static_cast<size_t>(pitch) * height_ * 3u / 2u;
       if (g_cuda.memcpy_dtod_async) {
           g_cuda.memcpy_dtod_async(out_nv12, src, total_nv12_bytes, stream);
       }
       return RTX_OK;
   }

   // 3. 提交 TensorRT 异步推理并执行 NV12 后处理
   if (!impl_->context->enqueueV3(reinterpret_cast<cudaStream_t>(stream))) {
       return RTX_ERR_INTERNAL;
   }
   return launchRifePostprocess(postLaunch, stream);
   ```

---

## 三、验收标准与自测门禁 (DoD)

1. **Profile 自适应**：分别传入标准 1080p 与对齐后 1088p 引擎，均能正常 `setInputShape` 并完成推理；
2. **缓存复用生效**：在 120fps 插帧测试中，实机断言每对相邻源帧仅触发 1 次 `cuMemcpyDtoH` 与 1 次全量预处理；
3. **时钟平滑**：单帧推理平均耗时显著降低，彻底消除转场帧微卡顿。
