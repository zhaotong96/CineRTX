# **任意画幅 RIFE 四桶引擎集成与 Pack 修复施工单**

## **1\. 元信息与驳回定性**

### **返工基本信息**

* **波次标识**：Rework Wave 2-A（与 Rework Wave 2-B 并行）  
* **核心目标**：修复 `rife_pack.cu` 导致的单测回归，并将四画幅桶分立单 Profile RIFE 引擎真正集成进构建期与运行时执行链路。  
* **依据裁决**：协调负责人裁决文档 `CINERTX-P13-RULING-013`（第 13 号文档）。

### **驳回定性复盘**

原 Wave 2-A（06 号文档）被裁决为 **严重空壳 \+ 回归（REJECTED）**，具体判定依据如下：

1. **真机单测回归**：工程师在修改 `src/shim/ngx_bridge/rife_pack.cu:37` 边界反射逻辑后，直接导致物理真机测试 `rife_four_phase_monotonic_progression`（`tests/test_rife.cpp:442`）断言失败。  
2. **全链路零集成**：原提交仅在 Header `src/shim/ngx_bridge/rife_engine.h:20,49` 声明了 `RifeAspectBucket` 枚举与几何辅助函数，但构建脚本 `cmake/EnsureRifeEngine.cmake`、构建器 `src/shim/ngx_bridge/rife_builder.cpp`、引擎运行时 `src/shim/ngx_bridge/rife_engine.cpp` 以及流水线节点 `src/core/hw_transcode_stages.cpp` 均**未包含任何实际集成代码**，属于典型空壳实现。

### **宪法级硬红线**

* **`[DEC-011]` / `[ISSUE-005]` 约束**：单个 TensorRT `.engine` 文件内**严禁包含多组 `IOptimizationProfile` 动态切换**。必须在编译期严格物理拆分为 4 个独立的单 Profile `.engine` 文件。  
* **架构类型数量限制**：全工程 `AuditTypes <= 34/35`（本波次零新增具名 `struct` / `class`）。

---

## **2\. Task 1：修复 `rife_pack.cu:37` 边界下溢与单测回归**

### **坐标与问题排查**

* **定位坐标**：`src/shim/ngx_bridge/rife_pack.cu:37` 与 `tests/test_rife.cpp:442`。  
* **原因分析**：在旧版 `reflect101i` 边界索引映射实现中，当计算带四边对齐偏移 `(off_x, off_y)` 的源像素坐标时，原公式使用了无符号整数（`uint32_t`）进行减法运算。在负偏移或边界外采样时引发无符号整数下溢（回绕至 `4294967295`），导致 GPU Kernel 读取非法内存或错误反射索引，破坏了中间帧张量的单调递增属性。

### **修复方案与代码实现**

采用有符号 `int32_t` 严格实现 OpenCV 标准 `BORDER_REFLECT_101` 镜像折叠，添加针对 `len == 1` 的防死循环保护。

```
// src/shim/ngx_bridge/rife_pack.cu
__device__ __forceinline__ int reflect101i(int idx, int len) {
    if (len <= 1) return 0;

    while (idx < 0 || idx >= len) {
|---|
            idx = -idx;
        } else if (idx >= len) {
            idx = 2 * len - 2 - idx;
        }
    }
    return idx;
}

__global__ void rife_pack_pad_kernel(
    const float* __restrict__ src,
    float* __restrict__ dst,
    int src_w, int src_h,
    int pad_w, int pad_h,
    int off_x, int off_y,
    int channels
) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    int c = blockIdx.z;

    if (x >= pad_w || y >= pad_h || c >= channels) return;

    // 转换为以原始图像为原点的坐标
    int src_raw_x = x - off_x;
    int src_raw_y = y - off_y;

    // 应用 BORDER_REFLECT_101 映射
    int mapped_x = reflect101i(src_raw_x, src_w);
    int mapped_y = reflect101i(src_raw_y, src_h);

    int src_idx = (c * src_h + mapped_y) * src_w + mapped_x;
    int dst_idx = (c * pad_h + y) * pad_w + x;

    dst[dst_idx] = src[src_idx];
}
```

### **验证步骤**

在宿主机终端执行指令，确保 `rife_four_phase_monotonic_progression` 及全套 RIFE 测试 100% 通过：h  
./bin/rtx\_tests.exe \--gtest\_filter=*rife*

````

---

## 3. Task 2：物理分立四桶单 Profile 引擎集成

### 规格映射表（对齐步长 `align_mod = 64`）

| 桶分类 (Bucket) | 宽高比条件 ($R = W/H$) | 对应 Engine 文件 | 最小分辨率 (MIN) | 推荐分辨率 (OPT) | 最大分辨率 (MAX) |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Ultra** | $R > 2.05$ | `rife_v4.25_ultra.engine` | $640 \times 256$ | $2560 \times 1088$ | $3840 \times 1600$ |
| **Wide** | $1.20 < R \le 2.05$ | `rife_v4.25_wide.engine` | $640 \times 384$ | $1920 \times 1088$ | $3840 \times 2176$ |
| **Square** | $0.80 \le R \le 1.20$ | `rife_v4.25_square.engine` | $512 \times 512$ | $1472 \times 1472$ | $2560 \times 2560$ |
| **Tall** | $R < 0.80$ | `rife_v4.25_tall.engine` | $384 \times 640$ | $1088 \times 1920$ | $2176 \times 3840$ |

*注：Square 桶 MAX 分辨率设为 $2560 \times 2560$，完全覆盖 1:1 高清视频需求，彻底解决超界报错问题。*

### 构建器与 CMake 重构
精确坐标：`src/shim/ngx_bridge/rife_builder.cpp:444-458` 与 `cmake/EnsureRifeEngine.cmake:85-86`。

```cpp
// src/shim/ngx_bridge/rife_builder.cpp
bool buildSingleBucketEngine(const std::string& onnxPath, const std::string& enginePath, RifeAspectBucket bucket) {
    auto builder = std::unique_ptr<nvinfer1::IBuilder>(nvinfer1::createInferBuilder(gLogger));
    auto config = std::unique_ptr<nvinfer1::IBuilderConfig>(builder->createBuilderConfig());
    auto profile = builder->createOptimizationProfile();

    RifeBucketProfile Spec = getBucketSpec(bucket);

    // 严格单个 Profile 配置，驱动 ONNX 动态维度
    profile->setDimensions("input_img0", nvinfer1::OptProfileSelector::kMIN, Spec.min_dims);
    profile->setDimensions("input_img0", nvinfer1::OptProfileSelector::kOPT, Spec.opt_dims);
    profile->setDimensions("input_img0", nvinfer1::OptProfileSelector::kMAX, Spec.max_dims);

    profile->setDimensions("input_img1", nvinfer1::OptProfileSelector::kMIN, Spec.min_dims);
    profile->setDimensions("input_img1", nvinfer1::OptProfileSelector::kOPT, Spec.opt_dims);
    profile->setDimensions("input_img1", nvinfer1::OptProfileSelector::kMAX, Spec.max_dims);

    config->addOptimizationProfile(profile);
    // 构建序列化模型并写入 enginePath ...
    return true;
}
````

```
## cmake/EnsureRifeEngine.cmake
set(RIFE_BUCKETS "ultra" "wide" "square" "tall")
foreach(BUCKET ${RIFE_BUCKETS})
    set(ENGINE_FILE "${ENGINE_OUTPUT_DIR}/rife_v4.25_${BUCKET}.engine")
    if(NOT EXISTS "${ENGINE_FILE}")
        message(STATUS "Generating missing RIFE engine bucket: ${BUCKET}")
        execute_process(
            COMMAND rife_builder --onnx "${RIFE_ONNX_PATH}" --out "${ENGINE_FILE}" --bucket "${BUCKET}"
            RESULT_VARIABLE BUILD_RES
        )
        if(NOT BUILD_RES EQUAL 0)
            message(FATAL_ERROR "Failed to build RIFE engine for bucket: ${BUCKET}")
        endif()
    endif()
endforeach()
```

## **运行时路由与单实例显存守护**

精确坐标：`src/shim/ngx_bridge/rife_engine.cpp:161-162, 366-404`。

```
// src/shim/ngx_bridge/rife_engine.cpp
RifeAspectBucket classifyRifeBucket(int width, int height) {
    float aspect = static_cast<float>(width) / static_cast<float>(height);
    if (aspect > 2.05f) return RifeAspectBucket::ULTRA;
    if (aspect > 1.20f) return RifeAspectBucket::WIDE;
    if (aspect >= 0.80f) return RifeAspectBucket::SQUARE;
    return RifeAspectBucket::TALL;
}

bool RifeEngineManager::switchBucketIfNeeded(RifeAspectBucket targetBucket) {
    if (m_currentBucket == targetBucket && m_engine != nullptr) {
        return true;
    }

    // 显存守护：先释放当前驻留资源，杜绝 OOM
    m_context.reset();
    m_engine.reset();
    m_runtime.reset();

    std::string enginePath = getBucketEnginePath(targetBucket);
    if (!std::filesystem::exists(enginePath)) {
        // 自动按需触发首次编译
        compileEngineOnTheFly(targetBucket, enginePath);
    }

    // 反序列化新桶 Engine
    m_runtime = std::unique_ptr<nvinfer1::IRuntime>(nvinfer1::createInferRuntime(gLogger));
    auto buffer = loadEngineFile(enginePath);
    m_engine = std::unique_ptr<nvinfer1::ICudaEngine>(m_runtime->deserializeCudaEngine(buffer.data(), buffer.size()));
    m_context = std::unique_ptr<nvinfer1::IExecutionContext>(m_engine->createExecutionContext());
    m_currentBucket = targetBucket;

    return m_context != nullptr;
}
```

---

# **4\. Task 3：`infer_scale` 自适应与超限 Auto-Tiling**

## **动态 Scale 自适应**

在 `src/core/hw_transcode_stages.cpp:92-101` 中解除强制 `scale=1.0f` 的限制。当光流计算计算量过大时自适应降采样，而最终 Frame Warp 阶段保持全分辨率合成：

```
// src/core/hw_transcode_stages.cpp
float computeAdaptiveInferScale(int width, int height) {
    uint64_t total_pixels = static_cast<uint64_t>(width) * height;
    // 超过 2560x1440 (约 3.68M 像素) 时启用 0.5x 光流推理
    if (total_pixels > 2560 * 1440) {
        return 0.5f;
    }
    return 1.0f;
}
```

## **超限分辨率 Auto-Tiling 与余弦羽化**

当分辨率超过当前桶 MAX 边界或 VRAM 足迹不足时，自动拆分为 \$2 \\times 2\$ 或 \$1 \\times N\$ 网格 Tile。Tiles 具有 64px 重叠区域，推理完成后调用 CUDA Kernel 进行余弦权重的 Seamless 融合。

\$\$\\text{Weight}(d) \= \\frac{1}{2} \\left( 1 \+ \\cos\\left( \\frac{\\pi \\cdot d}{\\text{Overlap}} \\right) \\right)\$\$

```
// src/shim/ngx_bridge/rife_engine.cpp
bool RifeEngineManager::forwardTiled(const float* img0, const float* img1, float* output, int w, int h) {
    int tile_w = 1920;
    int tile_h = 1080;
    int overlap = 64;

    // 分块计算网格边界，确保每个 Tile 均为 64 的倍数
    auto tiles = calculateGridTiles(w, h, tile_w, tile_h, overlap);

    for (const auto& tile : tiles) {
        // 1. Pack 子块 (带 64px 对齐与边界反射)
        rife_pack_pad_kernel<<<...>>>(...);
        // 2. 执行单 Tile 推理
        m_context->enqueueV3(stream);
        // 3. 裁切并使用余弦权重累加至 Output Buffer
        rife_blend_tile_cosine_kernel<<<...>>>(output, tile_out, tile.x, tile.y, overlap);
    }
    cudaStreamSynchronize(stream);
    return true;
}
```

---

# **5\. 法定验收门禁与测试用例矩阵**

## **真机实测环境**

必须在 **NVIDIA GeForce RTX 5060** 宿主机上完整跑通下列用例：

```
## 执行完整自动化测试套件
./bin/rtx_tests.exe --gtest_filter=*RIFE*
```

## **验证矩阵表**

| 用例编号 | 目标画幅比例 | 分辨率规格 | 匹配 Aspect Bucket | Tiling 状态 | 输出分辨率 | 验收标准 |
| :---- | :---- | :---- | :---- | :---- | :---- | :---- |
| **TC-01** | 21:9 UltraWide | \$2560 \\times 1080\$ | `Bucket::Ultra` | Direct | \$2560 \\times 1080\$ | 0 FAILED / PSNR \> 38dB |
| **TC-02** | 32:9 SuperWide | \$5120 \\times 1440\$ | `Bucket::Ultra` | Auto-Tiling (\$2 \\times 1\$) | \$5120 \\times 1440\$ | 0 FAILED / 无接缝痕迹 |
| **TC-03** | 4:3 Legacy High | \$3072 \\times 2304\$ | `Bucket::Wide` | Auto-Tiling (\$2 \\times 2\$) | \$3072 \\times 2304\$ | 0 FAILED / 无越界崩溃 |
| **TC-04** | 1:1 Square High | \$2560 \\times 2560\$ | `Bucket::Square` | Direct | \$2560 \\times 2560\$ | 0 FAILED / 彻底修复原报错 |
| **TC-05** | 9:16 Vertical | \$1080 \\times 1920\$ | `Bucket::Tall` | Direct | \$1080 \\times 1920\$ | 0 FAILED / 图像单调递增验证 PASS |

```
[  PASSED  ] 5 tests for RIFE Engine Verification.
[  RESULT  ] 0 FAILED TESTS, ALL BUCKETS FULLY FUNCTIONAL.
```

