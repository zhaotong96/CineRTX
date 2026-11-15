# CineRTX Phase 9.1 施工单：Track 9.1-B FP16 引擎链路与后处理闭环

> **工单编号**: `CINERTX-P9.1-TRACK-B`  
> **任务名称**: `FP16 (kHALF) 引擎帧对缓存、敏感探针与后处理转换闭环`  
> **责任小组**: `CUDA 内核与 TRT 推理组`  
> **主导依据**: [`docs/phase9/CineRTX_Phase9_终审裁决与全链路真权重闭环SuperPower返工规范.md`](docs/phase9/CineRTX_Phase9_终审裁决与全链路真权重闭环SuperPower返工规范.md)  
> **关联缺陷**: 🔴 **RW-P9-02 (P0 阻断)**、🟡 **RW-P9-05 (P2 瑕疵)**  
> **核心目标**: 彻底修复 FP16 半精度引擎下帧对缓存漏刷时间步导致 1:4 假死重复帧、输出半精度数据直接被当作单精度解释导致画面乱码的致命硬伤，闭环 Full-Range 色阶适配

---

## 一、 被拒缺陷机理穿透 (RW-P9-02 & RW-P9-05)

### 1. 缺陷 1：`can_reuse_pair` 漏更新 `kHALF` 导致 1:4 假死重复帧 (RW-P9-02)
在 [`src/shim/ngx_bridge/rife_engine.cpp`](src/shim/ngx_bridge/rife_engine.cpp#L747-L756) 中：
```cpp
// 历史被拒代码 (rife_engine.cpp:749)：
if (can_reuse_pair) {
    if (impl_->in_dtype == nvinfer1::DataType::kFLOAT && g_cuda.memset_d32_async) {
        const uint64_t alpha_plane_ptr = impl_->d_input_tensor + 6ULL * plane_pixels * sizeof(float);
        g_cuda.memset_d32_async(alpha_plane_ptr, alpha_bits, plane_pixels, stream);
    }
    // [致命漏洞]：当 in_dtype == kHALF (FP16) 时，代码居然什么都没做直接跳过了！
    effective_scene_cut = impl_->last_scene_cut;
}
```
**严重后果**：
- 当使用 FP16 引擎时，第 1 个插值帧（$\alpha=0.2$）在全量预处理时写入了 $0.2$ 的半精度表示；
- 随后的第 2、3、4 个插值帧（$\alpha \in \{0.4, 0.6, 0.8\}$）命中 `can_reuse_pair == true` 缓存复用分支；
- 由于跳过了更新，显存第 6 通道内的数值永远停留在 $0.2$，神经网络 4 次推理输入了完全相同的张量，**输出了 4 帧完全相同的画面，直接在 FP16 链路上制造了 1:4 假死重复帧！**

### 2. 缺陷 2：后处理张量数据类型错位引发画面崩溃 (RW-P9-02)
在推理完成后调用 `launchRifePostprocess` 时，直接将半精度的 `impl_->d_output_tensor`（数据为 IEEE-754 半精度 16-bit）作为首参数传给了 `launchRifePostprocess`。
而后处理 PTX/CUDA 内核是按 `const float*`（32-bit 单精度）读取输入的！
**严重后果**：两个 16-bit 浮点数被强行拼成一个 32-bit 浮点数解析，颜色和像素空间彻底错乱，画面出现严重噪波、雪花甚至黑屏。

### 3. 缺陷 3：后处理忽略 `full_range` 标志 (RW-P9-05)
后处理 kernel 中 RGB 转 NV12 公式硬编码了 Limited Range（16-235），忽略了传入的 `full_range` 参数，导致全范围素材黑白电平被截断。

---

## 二、 施工目标与详细修改点

### 任务 9.1-B1：修复 `can_reuse_pair` 分支补齐 `kHALF` 通道更新

- **目标文件**：[`src/shim/ngx_bridge/rife_engine.cpp`](src/shim/ngx_bridge/rife_engine.cpp#L747-L756)
- **修改方案**：
  当 `impl_->in_dtype == nvinfer1::DataType::kHALF` 且 `can_reuse_pair` 命中时：
  1. 先用 `memset_d32_async` 将 `alpha_bits` 刷入 FP32 scratch 缓冲区 `impl_->d_input_fp32_scratch` 的第 6 通道；
  2. 仅针对该通道（`plane_pixels` 个元素）调用 `launchFp32ToFp16` 将其同步转为半精度写入 `impl_->d_input_tensor` 的第 6 通道。

**代码补丁（替换原第 747–756 行）**：
```cpp
    if (can_reuse_pair) {
        const size_t plane_pixels = static_cast<size_t>(padded_width_) * static_cast<size_t>(padded_height_);
        const uint32_t alpha_bits = std::bit_cast<uint32_t>(alpha);
        if (impl_->in_dtype == nvinfer1::DataType::kFLOAT && g_cuda.memset_d32_async) {
            const uint64_t alpha_plane_ptr = impl_->d_input_tensor + 6ULL * plane_pixels * sizeof(float);
            g_cuda.memset_d32_async(alpha_plane_ptr, alpha_bits, plane_pixels, stream);
        } else if (impl_->in_dtype == nvinfer1::DataType::kHALF &&
                   impl_->d_input_fp32_scratch != 0 && g_cuda.memset_d32_async) {
            const uint64_t fp32_alpha_ptr = impl_->d_input_fp32_scratch + 6ULL * plane_pixels * sizeof(float);
            const uint64_t fp16_alpha_ptr = impl_->d_input_tensor + 6ULL * plane_pixels * sizeof(uint16_t);
            g_cuda.memset_d32_async(fp32_alpha_ptr, alpha_bits, plane_pixels, stream);
            const int32_t cvt_res = launchFp32ToFp16(fp32_alpha_ptr, fp16_alpha_ptr, plane_pixels, stream);
            if (cvt_res != RTX_OK) return cvt_res;
        }
        effective_scene_cut = impl_->last_scene_cut;
    }
```

---

### 任务 9.1-B2：推理完成后补齐 `kHALF -> kFLOAT` 输出张量转换

- **目标文件**：[`src/shim/ngx_bridge/rife_engine.cpp`](src/shim/ngx_bridge/rife_engine.cpp#L834-L848)
- **修改方案**：
  在 `enqueueV3` 推理完成后：
  判断 `if (impl_->out_dtype == nvinfer1::DataType::kHALF)`，若是，则调用 `launchFp16ToFp32` 将 `impl_->d_output_tensor` 转换为 `impl_->d_output_fp32_scratch`，并将 `postLaunch.rgb_tensor` 指向 `impl_->d_output_fp32_scratch`。

**代码补丁（替换原第 834–848 行）**：
```cpp
    uint64_t post_rgb_tensor = impl_->d_output_tensor;
    if (impl_->out_dtype == nvinfer1::DataType::kHALF) {
        const size_t out_elems = 3ULL * static_cast<size_t>(padded_width_) * static_cast<size_t>(padded_height_);
        const int32_t cvt_res = launchFp16ToFp32(
            impl_->d_output_tensor, impl_->d_output_fp32_scratch, out_elems, stream);
        if (cvt_res != RTX_OK) {
            return cvt_res;
        }
        post_rgb_tensor = impl_->d_output_fp32_scratch;
    }

    // Launch postprocessing kernel with validated single-precision RGB tensor
    const RifePostprocessLaunch postLaunch{
        .rgb_tensor = post_rgb_tensor,
        .y_plane = out_nv12,
        .uv_plane = 0,
        .width = width_,
        .height = height_,
        .y_pitch = pitch,
        .uv_pitch = pitch,
        .padded_width = padded_width_,
        .padded_height = padded_height_,
        .full_range = full_range,
    };

    return launchRifePostprocess(postLaunch, stream);
```

---

### 任务 9.1-B3：修复 `runAlphaSensitivityProbe` 对 `kHALF` 输入/输出的转换适配

- **目标文件**：[`src/shim/ngx_bridge/rife_engine.cpp`](src/shim/ngx_bridge/rife_engine.cpp#L136-L205)
- **修改方案**：
  在动态加载引擎后的探针阶段：
  - 若 `in_dtype == kHALF`：主机填充的 FP32 数据先写入临时缓冲，并调用 `launchFp32ToFp16` 写至 GPU 输入张量；
  - 若 `out_dtype == kHALF`：推理后先调用 `launchFp16ToFp32` 将输出转为 FP32，再 `memcpy_dtoh` 回主机比对；
  - 避免对真实半精度模型误报探针失败。

---

### 任务 9.1-B4：后处理内核适配 `full_range` 参数 (RW-P9-05)

- **目标文件**：`src/shim/ngx_bridge/rife_postprocess_kernel.ptx`
- **修改方案**：
  在 NV12 后处理核函数中，检查 `full_range` 寄存器：
  - 当 `full_range == 1` 时，采用 BT.709/BT.601 Full Range 矩阵：
    $Y = 0.299R + 0.587G + 0.114B$ (范围 0..255)
  - 当 `full_range == 0` 时，采用 Limited Range 矩阵：
    $Y = 16 + (219/255) \times (0.299R + 0.587G + 0.114B)$ (范围 16..235)

---

## 三、 本地独立验证指令与判定标准

1. **编译构建**：
   ```powershell
   cmake --build .vs/build/cmake/windows-msvc-ninja --config RelWithDebInfo
   ```
   **断言**：MSVC `cl.exe` 0 error, 0 warning。

2. **单测运行**：
   ```powershell
   .\.vs\build\cmake\windows-msvc-ninja\tests\rtx_tests.exe --gtest_filter="*rife*"
   ```
   **断言**：全量 RIFE 单测通过，尤其是时序插值四阶段单调性检验 PASS。

---

## 四、 交付验收标准 (DoD)

- [ ] `rife_engine.cpp` 在 `can_reuse_pair` 分支完整闭环 `kHALF` 时间步更新；
- [ ] 推理后半精度输出张量 100% 经 `launchFp16ToFp32` 转为 FP32 后再行色度后处理；
- [ ] `runAlphaSensitivityProbe` 完美支持 FP16 模型，无误杀探针；
- [ ] 后处理正确响应 `full_range` 参数。
