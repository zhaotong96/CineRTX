# **Wave 2 \- Stage A: 任意画幅 RIFE 分立桶引擎与自适应分块施工单**

## **1\. 元信息与核心宪法红线**

### **1.1 施工波次定位**

* **波次标识**：`Wave 2 - Stage A`  
* **执行依赖**：必须在 `Wave 0-A` 基础设施及硬件流水线打通后启动；与 `Wave 2-B`（超分与色彩引擎解耦）并行推进。  
* **业务目标**：彻底打破原有 RIFE 模型对 `16:9` 尺寸与标准电视分辨率的硬性锚定，全面支持 `21:9`、`32:9`、`4:3`、`1:1`、`4:5`、`9:16`、`9:19.5` 等任意画面比例，以及最高达 `8K (7680x4320)` 的超高清/大画幅视频高帧率插帧。

### **1.2 不可逾越的宪法红线**

#### **`[DEC-011]` / `[ISSUE-005]` 单 Profile 铁律**

严禁在单个 TensorRT `.engine` 文件内构建多个 `IOptimizationProfile` 并企图在运行时动态切换。

* **反面教训**：历史 Phase 11.4 架构演进实证表明，在单个 TRT 引擎中包含 3 个及以上 `IOptimizationProfile` 会触发 Myelin 优化器的图展开膨胀，导致编译生成的 `.engine` 内存占用达到 `8.89 GB`，且运行时 context 绑定引发严重显存抖动，推理帧率暴跌至 `< 1 fps`。  
* **硬性约束**：每一个编译产出的 `.engine` 物理文件只允许且必须包含 **1 个 `IOptimizationProfile`**。

#### **几何真实性保全律**

严禁在 preprocessing / postprocessing 流程中使用非等比拉伸缩放（Dynamic Stretching）将非标输入强制拟合至标准画幅。

* **硬性约束**：输入图像缩放必须严格保持原始光流矢量的物理比例，防止光流场重投影算法产生非线性扭曲与伪影。

#### **具名类型配额控制**

根据架构规约，系统必须严格维持 `AuditTypes <= 34/35`。

* **实施细节**：在本施工单范围内，**严禁新增任何全局 `struct` 或 `class` 定义**。所有画幅桶路由算法、向上对齐尺寸计算、Tiling 切片网格计算均必须采用 `constexpr` 静态常量查找表与无状态自由函数（Stateless Free Functions）实现。

---

## **2\. 核心架构设计与工程改造方案**

### **2.1 Task 1：四画幅桶物理分立单 Profile Engine 池**

#### **物理代码定位**

* `cmake/EnsureRifeEngine.cmake:85-86`  
* `src/shim/ngx_bridge/rife_builder.cpp:444-458`  
* `src/shim/ngx_bridge/rife_engine.cpp:161-162, 366-404`

#### **画幅桶分类与 Profile 边界定义**

所有画幅桶的维度上界与下界均严格执行 `align_mod = 64` 对齐（适用 RIFE v4.25）。修正向下取整缺陷，强制统一使用向上取整公式：`padded_dim = ((dim + 63) / 64) * 64`。

| 画幅桶 ID (RifeBucketId) | 覆盖纵横比范围 \$R \= W / H\$ | 代表画幅 | MIN (Min Profile) | OPT (Opt Profile) | MAX (Max Profile) |
| :---- | :---- | :---- | :---- | :---- | :---- |
| **BUCKET\_ULTRA** | \$R \> 2.05\$ | 21:9, 32:9 | `640 x 256` | `2560 x 1088` | `3840 x 1600` |
| **BUCKET\_WIDE** | \$1.20 \< R \\le 2.05\$ | 16:9, 16:10, 3:2, 4:3 | `640 x 384` | `1920 x 1088` | `3840 x 2176` |
| **BUCKET\_SQUARE** | \$0.80 \\le R \\le 1.20\$ | 1:1, 4:5, 5:4 | `512 x 512` | `1472 x 1472` | `2560 x 2560` |
| **BUCKET\_TALL** | \$R \< 0.80\$ | 9:16, 9:19.5, 3:4 | `384 x 640` | `1088 x 1920` | `2176 x 3840` |

*注：`BUCKET_SQUARE` 的 MAX 设置为 `2560x2560`，彻底解决了原架构中 1:1 视频在 `2560x2560` 分辨率下触发 `2176` 硬性上界截断报错的问题。*

#### **引擎驻留与 LRU 内存/显存治理规约**

为了彻底杜绝多个引擎同时驻留显存导致的 OOM，设计轻量级单句柄驻留机制（LRU Cache with Capacity \= 1）：

```
// 在 src/shim/ngx_bridge/rife_engine.cpp 中实现的无状态桶判定与句柄切换逻辑
constexpr inline uint32_t align_up_64(uint32_t v) {
    return ((v + 63U) / 64U) * 64U;
}

enum class RifeBucketId : uint8_t {
    ULTRA = 0,
    WIDE = 1,
    SQUARE = 2,
    TALL = 3
};

inline RifeBucketId classify_aspect_ratio(uint32_t width, uint32_t height) {
    const float ratio = static_cast<float>(width) / static_cast<float>(height);
    if (ratio > 2.05f) return RifeBucketId::ULTRA;
    if (ratio > 1.20f) return RifeBucketId::WIDE;
    if (ratio >= 0.80f) return RifeBucketId::SQUARE;
    return RifeBucketId::TALL;
}
```

当输入帧的画幅桶发生改变（例如从 `16:9` 切换至 `9:16`）时：

1. **显式销毁**：立即调用 `nvinfer1::IExecutionContext::destroy()` 与 `nvinfer1::ICudaEngine::destroy()` 释放当前驻留的 GPU 资源。  
2. **CUDA Flush**：执行 `cudaStreamSynchronize` 与 `cudaFree(0)` 强行回收 Myelin 临时工作空间。  
3. **按需加载**：从本地磁盘反序列化目标桶 `.engine` 文件（若本地缺失，则触发 `rife_builder` 即时离线生成）。

---

### **2.2 Task 2：CUDA 对称镜像边缘反射填充（Symmetric Reflect101 Padding）与精准裁剪**

#### **物理代码定位**

* `src/shim/ngx_bridge/rife_engine.cpp:366-404`

#### **传统方案痛点**

原代码直接在右侧与下侧填充零像素（Zero-Padding）或重复边界，且写死 `align_mod = 128`。填充纯黑边界在 RIFE 光流估计网络中会产生极高的高频边缘梯度，致使边缘画面向内严重坍缩或拉扯失真。

#### **改造算法与 CUDA Kernel 规范**

采用 4 边对称居中反射填充（Reflect101, 镜面反射不重复边缘像素）。

```
        +---------------------------------------------------+  (pad_w, pad_h)
        |                 Reflect Top Region                |
        +------------+-------------------------+------------+
        |            |                         |            |
        |  Reflect   |     Original Image      |  Reflect   |
        |    Left    |  (in_w x in_h placed)   |   Right    |
        |            |   at (off_x, off_y)     |            |
        +------------+-------------------------+------------+
        |                Reflect Bottom Region              |
        (0,0)-----------------------------------------------+
```

##### **偏移量计算公式**

\$\$\\text{pad\_w} \= \\left\\lceil \\frac{\\text{in\_w}}{\\text{align}} \\right\\rceil \\times \\text{align}, \\quad \\text{pad\_h} \= \\left\\lceil \\frac{\\text{in\_h}}{\\text{align}} \\right\\rceil \\times \\text{align}\$\$

\$\$\\text{off\_x} \= \\frac{\\text{pad\_w} \- \\text{in\_w}}{2}, \\quad \\text{off\_y} \= \\frac{\\text{pad\_h} \- \\text{in\_h}}{2}\$\$

##### **CUDA 算子实现规范 (Symmetric Reflect101 Kernel)**

a  
**host** **device** **forceinline** int reflect101\_coord(int p, int len) {  
if (len \<= 1\) return 0;  
while (p \< 0 \|\| p \>= len) {  
if (p \< 0\) p \= \-p;  
else if (p \>= len) p \= 2 \* len \- 2 \- p;  
}  
return p;  
}

**global** void launchReflectPadSymmetricKernel(  
const float\* restrict src,  
float\* restrict dst,  
int in\_w, int in\_h,  
int pad\_w, int pad\_h,  
int off\_x, int off\_y,  
int channels)  
{  
int x \= blockIdx.x \* blockDim.x \+ threadIdx.x;  
int y \= blockIdx.y \* blockDim.y \+ threadIdx.y;  
int c \= blockIdx.z;

```
if (x >= pad_w || y >= pad_h || c >= channels) return;

int src_x = reflect101_coord(x - off_x, in_w);
int src_y = reflect101_coord(y - off_y, in_h);

size_t src_idx = (static_cast<size_t>(c) * in_h + src_y) * in_w + src_x;
size_t dst_idx = (static_cast<size_t>(c) * pad_h + y) * pad_w + x;

ds
```

t\[dst\_idx\] \= src\[src\_idx\];  
}

````

##### 精准裁剪 (Crop Output Kernel)
推理结束后，直接从 TensorRT 输出 Buffer 的 `[off_x : off_x + in_w, off_y : off_y + in_h]` 区域提取数据写回目标 GPU 显存，确保像素级零偏移。

---

### 2.3 Task 3：超大画幅内部降采样倍率自适应（`infer_scale`）与自动网格分块（Auto-Tiling）

#### 物理代码定位
* `src/core/hw_transcode_stages.cpp:92-101`
* `src/shim/ngx_bridge/rife_engine.cpp`

#### 改造策略 A：内部光流尺度 `infer_scale` (1.0f / 0.5f) 解耦
彻底移除 `hw_transcode_stages.cpp:92-101` 中在特定分辨率下强行覆盖 `scale = 1.0f` 的历史硬编码。

```cpp
// 内部光流计算尺度自适应决断函数
inline float calculate_rife_infer_scale(uint32_t width, uint32_t height) {
    const uint64_t total_pixels = static_cast<uint64_t>(width) * height;
    // 面积超过 2560x1440 (3.68M 像素) 自动启用 0.5x 光流金字塔降采样
    if (total_pixels > 3686400ULL) {
        return 0.5f;
    }
    return 1.0f;
}
````

* **执行特征**：光流估计网络内部运行在 `0.5x` 空间采样率下，获得大移动矢量的更广感受野；而最终的 Feature Warp 与 RGB Blend 阶段依然在 `1.0x` 原生分辨率执行。  
* **状态暴露**：将当前命中的 `infer_scale` 实时写入 Pipeline DAG 状态表，并在日志与 UI 摘要信息中显式展示 `Scale: 0.5x`。

### **改造策略 B：超安全水位与极值画幅 Auto-Tiling 网格分块**

当输入尺寸超过所在桶的 `MAX` 边界，或当前 GPU 可用显存低于安全水位（`Safe VRAM Threshold = 4.5 GB`）时，自动激活 Auto-Tiling 引擎。

```
+--------------------------+--------------------------+
| Tile (0,0)               | Tile (1,0)               |
| [Overlap 64px]           | [Overlap 64px]           |
|                     +----+---------------------+    |
|                     | Cosine Feathering Blend  |    |
|                     +----+---------------------+    |
+--------------------------+--------------------------+
| Tile (0,1)               | Tile (1,1)               |
+--------------------------+--------------------------+
```

#### **Tiling 参数约束与划分规则**

* **Tile 基础维度**：从目标桶的 `OPT` 分辨率作为基准划分（如 `1920x1088` 或 `1472x1472`）。  
* **Overlap 重叠带**：每个 Tile 边缘向外扩展  `overlap = 64` 像素（满足 64-bit 对齐）。  
* **混合算法 (Cosine Feathering Blend)**：在重叠区域（Overlap Region），使用一维/二维余弦加权函数进行 Smooth Step 融合：

\$\$W(d) \= 0.5 \\times \\left(1.0 \- \\cos\\left(\\frac{\\pi \\cdot d}{\\text{overlap}}\\right)\\right)\$\$

其中 \$d \\in \[0, \\text{overlap}\]\$ 为相对于重叠边界的距离。该方法可彻底消除 Tile 缝隙处的拼接断层与光流撕裂现象。

---

# **3\. 验收门禁与测试用例**

为确保施工质量，必须通过以下 6 组典型画幅的端到端单测与显存基准测试。

```
                       WAVE 2 - STAGE A VALIDATION MATRIX
+-----------------+-------------------+---------------+------------------+-----------------+
| Test Target     | Resolution (WxH)  | Aspect Ratio  | Assigned Bucket  | Tiling Status   |
+-----------------+-------------------+---------------+------------------+-----------------+
| 21:9 UltraWide  | 2560 x 1080       | 2.37 (Ultra)  | BUCKET_ULTRA     | Native Pass     |
| 32:9 SuperWide  | 5120 x 1440       | 3.55 (Ultra)  | BUCKET_ULTRA     | Auto-Tiled 2x1  |
| 4:3 Legacy      | 3072 x 2304       | 1.33 (Wide)   | BUCKET_WIDE      | Auto-Tiled 2x2  |
| 1:1 Square      | 2560 x 2560       | 1.00 (Square) | BUCKET_SQUARE    | Native Pass     |
| 9:16 Portrait   | 1080 x 1920       | 0.56 (Tall)   | BUCKET_TALL      | Native Pass     |
| 8K Extreme      | 7680 x 4320       | 1.77 (Wide)   | BUCKET_WIDE      | Auto-Tiled 4x2  |
+-----------------+-------------------+---------------+------------------+-----------------+
```

## **验收门禁清单**

1. **尺寸精确性**：所有测试用例输出帧的 \$W \\times H\$ 必须与输入原片 \$1:1\$ 严格一致，无任何像素级的几何拉伸或坐标偏移。  
2. **Profile 独占性**：运行时通过 Nsight Systems 抓取 TRT 引擎实例，断言任意时刻显存中只存在 `1` 个 `IOptimizationProfile`，严格遵守 `[DEC-011]`。  
3. **显存安全红线**：在 `6GB` 显存设备（如 RTX 2060 / 3060 Laptop）上跑通 `8K (7680x4320)` 帧率插帧测试，峰值显存占用低于 `4.2 GB`，无 OOM 崩溃。  
4. **边缘质量**：4 边对称居中镜像反射填充在边缘 64px 区域内无黑边拉扯、无光流坍缩伪影。

