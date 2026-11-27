# **《26\_target\_wave2-A\_四桶单Profile引擎与NVOFA切镜黑场真接线施工单》**

本工程单为针对 **23 号驳回裁决书** 中 Wave 2-A / Wave 2-B 遗留缺陷的最高优先级执行规约。旨在完成四桶单 Profile RIFE 引擎物理分立构建与运行时接线、修复 CUDA 核边界反射计算，以及彻底盘活 NVOFA 光流旁路、切镜黑场检测与 ShotGroupSegmenter 状态流水线。

---

## **1\. 工程背景与宪法级限制红线**

### **1.1 前置依赖条件**

* **编译与门禁锁死**：必须全量继承 `24_target_wave0-A` 产出的宿主工程结构，保证静态分析（Linter）、单元测试框架、显存分配器与四重安全门禁均为绿灯状态。  
* **物理目标环境**：以 NVIDIA GeForce RTX 5060 为基准测试与验证目标，所有工程交存前必须在 RTX 5060 宿主机运行并通过测试。

### **1.2 驳回靶点定位（23号裁决追溯）**

1. `rife_builder.cpp` / `rife_engine.cpp` / `EnsureRifeEngine.cmake` 未真正接入四桶单 Profile 编译逻辑，导致多画幅输入时 TensorRT 产生频繁的动态尺寸重构开销或内存越界。  
2. `rife_pack.cu:37` 边界反射算法缺陷，直接引发 `rife_four_phase_monotonic_progression`（`test_rife.cpp:442`）断言失败。  
3. `hw_transcode_stages.cpp:446-459` 与 `bridge_exports.cpp:674-723` 中的 `nvof_engine` 未接入 `ShotGroupSegmenter` 探测结果，导致场景切镜与黑场过渡时产生剧烈的果冻撕裂拖影，且未打通历史 Temporal 提示复位。

### **1.3 宪法级工程红线**

* **`[DEC-011]` 严禁单 `.engine` 内嵌多 `IOptimizationProfile`**：必须物理编译并生成 4 个独立的 `.engine` 文件。  
* **`AuditTypes <= 34/35` 极简类型限制**：严禁在 C++ 接口层中新增具名 `struct` 或 `class`。必须复用现有 POD 结构或标准 C API 句柄传递。  
* **零整帧 CPU 像素回读**：切镜、黑场探测、直方图计算及 RIFE/NVOFA 合成必须全程在 GPU 显存内闭环，严禁将图像数据拉回 Host 侧 CPU 内存。

---

## **2\. Task 1: 修复 `rife_pack.cu:37` 边界反射与时域单调性回归**

### **2.1 边界反射 `BORDER_REFLECT_101` 修复规约**

在 `rife_pack.cu` 第 37 行及相关补帧预处理/后处理 Kernel 中，针对对称居中偏移 (`off_x`, `off_y`) 导致索引越界问题，强制引入带符号整型 `int` 折叠计算机制，并增加 `len == 1` 退化保底逻辑。

```
// CUDA Kernel 内部索引折叠公式实现
__device__ __forceinline__ int reflect_101(int p, int len) {
    if (len <= 1) return 0;

    while (p < 0 || p >= len) {
|---|0) {
            p = -p;
        } else {
            p = 2 * len - 2 - p;
        }
    }
    return p;
}

// 作用于 rife_pack.cu:37 附近 Padding 取样
__global__ void rife_pack_pad_kernel(
    const float* __restrict__ src,
    float* __restrict__ dst,
    int src_w, int src_h,
    int dst_w, int dst_h,
    int off_x, int off_y) 
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= dst_w || y >= dst_h) return;

    // 计算映射回原始图像的物理坐标
    int raw_x = x - off_x;
    int raw_y = y - off_y;

    // 执行 BORDER_REFLECT_101 折叠
    int mapped_x = reflect_101(raw_x, src_w);
    int mapped_y = reflect_101(raw_y, src_h);

    // 写入目标 Tensor 空间
    dst[(y * dst_w + x)] = src[mapped_y * src_w + mapped_x];
}
```

### **2.2 验证与断言目标**

在 RTX 5060 宿主机上编译并运行 `rtx_tests.exe`，确保 `test_rife.cpp:442` 中的 `rife_four_phase_monotonic_progression` 测试用例通过，验证插帧权重在 \$t \\in \[0.0, 1.0\]\$ 范围内的输出连续性与像素单调渐变性，实现 0 FAILED。

---

## **3\. Task 2: 四画幅桶物理分立单 Profile 引擎全面接入**

根据宽高比 \$R \= \\frac{\\text{Width}}{\\text{Height}}\$ 将任意输入分辨率映射至 4 个物理画幅桶中。所有桶采用 `align_mod = 64` 强制向上对齐：`dim_aligned = ((dim + 63) / 64) * 64`。

### **3.1 四桶物理规格配置**

| 画幅桶标识 | 宽高比范围 \$R\$ | 引擎物理文件名 | MIN (W×H) | OPT (W×H) | MAX (W×H) |
| :---- | :---- | :---- | :---- | :---- | :---- |
| **Ultra** | \$R \> 2.05\$ | `rife_v4.25_ultra.engine` | 640×256 | 2560×1088 | 3840×1600 |
| **Wide** | \$1.20 \< R \\le 2.05\$ | `rife_v4.25_wide.engine` | 640×384 | 1920×1088 | 3840×2176 |
| **Square** | \$0.80 \\le R \\le 1.20\$ | `rife_v4.25_square.engine` | 512×512 | 1472×1472 | 2560×2560 |
| **Tall** | \$R \< 0.80\$ | `rife_v4.25_tall.engine` | 384×640 | 1088×1920 | 2176×3840 |

### **3.2 `EnsureRifeEngine.cmake` 物理编译规则构建**

修改 CMake 编译规则，确立 4 个独立目标构建指令，严禁合并 Profile。

```
# EnsureRifeEngine.cmake 关键构建逻辑
set(RIFE_BUCKET_NAMES "ultra" "wide" "square" "tall")
set(RIFE_SPECS_ultra  "MIN=640x256;OPT=2560x1088;MAX=3840x1600")
set(RIFE_SPECS_wide   "MIN=640x384;OPT=1920x1088;MAX=3840x2176")
set(RIFE_SPECS_square "MIN=512x512;OPT=1472x1472;MAX=2560x2560")
set(RIFE_SPECS_tall   "MIN=384x640;OPT=1088x1920;MAX=2176x3840")

foreach(bucket ${RIFE_BUCKET_NAMES})
    set(ENGINE_FILE "${CMAKE_BINARY_DIR}/engines/rife_v4.25_${bucket}.engine")
    add_custom_command(
        OUTPUT ${ENGINE_FILE}
        COMMAND rife_builder --bucket=${bucket} --specs=${RIFE_SPECS_${bucket}} --output=${ENGINE_FILE}
        DEPENDS rife_builder
        COMMENT "Building Single-Profile TensorRT Engine for RIFE Bucket: ${bucket}"
    )
    list(APPEND RIFE_ENGINE_TARGETS ${ENGINE_FILE})
endforeach()
add_custom_target(EnsureRifeEngines ALL DEPENDS ${RIFE_ENGINE_TARGETS})
```

### **3.3 `rife_builder.cpp` 严格单 Profile 导出**

构建器在调用 TensorRT Builder API 时，为每个 ONNX 模型单独创建一个 `IOptimizationProfile` 并绑定至 `IBuilderConfig`。

```
// rife_builder.cpp 核心配置片段
nvinfer1::IOptimizationProfile* profile = builder->createOptimizationProfile();
profile->setDimensions("input_img0", nvinfer1::OptProfileSelector::kMIN, nvinfer1::Dims4{1, 3, min_h, min_w});
profile->setDimensions("input_img0", nvinfer1::OptProfileSelector::kOPT, nvinfer1::Dims4{1, 3, opt_h, opt_w});
profile->setDimensions("input_img0", nvinfer1::OptProfileSelector::kMAX, nvinfer1::Dims4{1, 3, max_h, max_w});

// 对 input_img1 应用相同 Profile 规则
profile->setDimensions("input_img1", nvinfer1::OptProfileSelector::kMIN, nvinfer1::Dims4{1, 3, min_h, min_w});
profile->setDimensions("input_img1", nvinfer1::OptProfileSelector::kOPT, nvinfer1::Dims4{1, 3, opt_h, opt_w});
profile->setDimensions("input_img1", nvinfer1::OptProfileSelector::kMAX, nvinfer1::Dims4{1, 3, max_h, max_w});

config->addOptimizationProfile(profile); // 严格仅添加 1 个 Profile
```

### **3.4 `rife_engine.cpp` 分类器与物理单实例反序列化**

实现 `classifyRifeBucket(int w, int h)` 选桶逻辑。若画幅发生桶间切换，严格按"先释放旧 `IExecutionContext`/`ICudaEngine`，再反序列化新桶"的顺序执行，确保单实例显存驻留。

```
enum class RifeBucket { ULTRA = 0, WIDE = 1, SQUARE = 2, TALL = 3 };

RifeBucket classifyRifeBucket(int width, int height) {
    float aspect_ratio = static_cast<float>(width) / static_cast<float>(height);
    if (aspect_ratio > 2.05f) return RifeBucket::ULTRA;
    if (aspect_ratio > 1.20f) return RifeBucket::WIDE;
    if (aspect_ratio >= 0.80f) return RifeBucket::SQUARE;
    return RifeBucket::TALL;
}

void RifeEngineContext::switchBucketIfNeeded(RifeBucket target_bucket) {
    if (current_bucket_ == target_bucket && engine_instance_ != nullptr) return;

    // 1. 物理释放当前引擎与上下文
    context_.reset();
    engine_instance_.reset();

    // 2. 加载目标桶对应的物理 `.engine` 文件
    std::string engine_path = getEnginePathForBucket(target_bucket);
    std::vector<char> engine_data = readBinaryFile(engine_path);

    // 3. 反序列化新引擎
    runtime_.reset(nvinfer1::createInferRuntime(g_logger));
    engine_instance_.reset(runtime_->deserializeCudaEngine(engine_data.data(), engine_data.size()));
    context_.reset(engine_instance_->createExecutionContext());

    current_bucket_ = target_bucket;
}
```

### **3.5 填充推理、自动分块 (Auto-Tiling) 与羽化融合**

推理前按照 \$64\\text{px}\$ 步长四边对称 Padding，推理完成后裁回 `[off_x : off_x + w, off_y : off_y + h]`。若图像超过对应桶 `MAX` 规格或显存超限，触发 `64px` 重叠余弦羽化 Auto-Tiling；对于超大画幅图像，自动激活 `infer_scale = 0.5f`。

\$\$\\text{Weight}(x) \= 0.5 \\times \\left(1.0 \- \\cos\\left(\\frac{\\pi \\cdot x}{\\text{overlap\_px}}\\right)\\right)\$\$

---

## **4\. Task 3: `ShotGroupSegmenter` 盘活与 NVOFA 切镜黑场硬切旁路**

### **4.1 GPU 时域探测原语与状态更新**

统一 GPU 侧的轻量级探测计算，输出 `sad_ema_ratio`（用于判断场景硬切 `HardCut`）、`luma_mean` / `luma_var`（用于判断黑场及渐变 `BlackFade`）以及 16-bin 亮度直方图的 L1 距离。`ShotGroupSegmenter` 接收上述原语并更新全局 `shot_id` 与帧边界状态。

```
// 硬件转码阶段/桥接层状态流转逻辑
struct TemporalDetectorOutput {
    float sad_ema_ratio;
    float luma_mean;
    float luma_var;
    float hist_l1_dist;
};

// 状态判定逻辑
bool is_hard_cut = (det.sad_ema_ratio > 3.5f) && (det.hist_l1_dist > 0.45f);
bool is_black_fade = (det.luma_mean < 0.03f) || (det.luma_var < 0.001f);

if (is_hard_cut || is_black_fade) {
    segmenter.markNewShot(is_black_fade ? FrameType::BLACK_FADE : FrameType::HARD_CUT);
}
```

### **4.2 NVOFA 光流旁路与硬切策略 (在 `hw_transcode_stages.cpp` 与 `bridge_exports.cpp`)**

在 NVOFA 执行入口阶段接入检测标记。若检测到场景变更或黑场，绕过 NVOFA 密集光流计算，降低开销并杜绝交叉污染。p  
// hw\_transcode\_stages.cpp:446-459 & bridge\_exports.cpp:674-723 接线逻辑  
if (current\_frame\_meta.is\_scene\_change \|\| current\_frame\_meta.is\_black\_frame) {  
// 1\. 旁路 NVOFA 光流计算，直接执行硬切合成策略  
// 严格按时间切片分配：t \< 0.5 取 I0 帧，t \>= 0.5 取 I1 帧  
cudaMemcpyAsync(  
output\_interpolated\_ptr,  
(target\_t \< 0.5f) ? input\_i0\_ptr : input\_i1\_ptr,  
frame\_size\_bytes,  
cudaMemcpyDeviceToDevice,  
stream\_  
);

```
// 2. 显式置位 NV_OF_EXECUTE_INPUT_PARAMS 历史 temporal 提示无效化
nv_of_params.disableTemporalHints = NV_TRUE;
```

} else {  
// 标准 NVOFA 光流推理与合成路径  
nv\_of\_params.disableTemporalHints \= NV\_FALSE;  
nvof\_engine-\>execute(\&nv\_of\_params, stream\_);  
launchSynthesisKernel(input\_i0\_ptr, input\_i1\_ptr, nvof\_flow\_vector, output\_interpolated\_ptr, stream\_);  
}

```

### 4.3 历史提示复位与下游联动机制
当新镜头首帧确定时，触发下游依赖系统的状态复位：
1. **NV_OF 历史提示**：`disableTemporalHints = NV_TRUE`。
2. **前后向一致性校验**：在光流合成 Kernel 中开启动态 Warp 遮挡掩码，避免果冻拉伸。
3. **VSR 超分引擎**：下发 `force_reset_temporal = true`，清空历史帧 Buffer。
4. **编码器联动**：强制当前帧编码类型为 `IDR` 关键帧，彻底阻断跨场景 P/B 帧参考残留。

---

## 5. 宿主机验收门禁与测试指标

测试需在配置为 NVIDIA GeForce RTX 5060 的宿主机器上全量通过。
```

```
                ┌──────────────────────────────────────────┐
                │  RTX 5060 Host Construction & Test Flow  │
                └────────────────────┬─────────────────────┘
                                     │
                                     ▼
                ┌──────────────────────────────────────────┐
                │      Build rife_builder & CMake Target   │
                └────────────────────┬─────────────────────┘
                                     │
                                     ▼
                ┌──────────────────────────────────────────┐
                │ Dynamic Frame Ratio Multi-Bucket Inference│
                │   (21:9 / 32:9 / 4:3 / 1:1 / 9:16)       │
                └────────────────────┬─────────────────────┘
                                     │
                                     ▼
                ┌──────────────────────────────────────────┐
                │ Verify rtx_tests.exe & rife_monotonic    │
                └────────────────────┬─────────────────────┘
                                     │
                     [ 0 FAILED / PSNR > 45 dB ]
                                     │
                                     ▼
                              ┌─────────────┐
                              │ PASS / DONE │
                              └─────────────┘
```

```

### 5.1 画幅桶覆盖性测试（测试套件：`rtx_tests.exe`）

| 测试分辨率 | 宽高比 $R$ | 对应选中桶 | 期待动作 / 结果 |
| :--- | :--- | :--- | :--- |
| **3840×1600** | $2.40$ | `rife_v4.25_ultra.engine` | 无 Padding，单 Profile 直接推理 |
| **5120×1440** | $3.55$ | `rife_v4.25_ultra.engine` | 触发 Auto-Tiling + 64px 融合 |
| **1920×1080** | $1.77$ | `rife_v4.25_wide.engine` | Padding 至 1920×1088，裁回原图 |
| **1080×1080** | $1.00$ | `rife_v4.25_square.engine` | Padding 至 1088×1088，正常推理 |
| **1080×1920** | $0.56$ | `rife_v4.25_tall.engine` | Padding 至 1088×1920，正常推理 |

### 5.2 质量与性能验收标准
* **用例全绿**：`test_rife.cpp` 内所有测试（特别是 `rife_four_phase_monotonic_progression`）测试通过，报表输出 `0 FAILED`。
* **场景切换图像质量**：对于硬切帧与黑场渐变帧，NVOFA 旁路输出与近端源帧的 PSNR 图像相似度指标满足：

$$\text{PSNR} > 45\text{ dB}$$

* **无视觉瑕疵**：场景切换画面无任何交错果冻、拉伸撕裂或历史残影现象。
```

