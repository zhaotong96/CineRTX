# **42\_fix\_wave1-B\_转码预览双侧独立色彩空间与BT2390色调映射施工单**

**Superpowers 原子执行规约（依据 39 号委托书与 40 号专家方案，与 Fix Wave 1-A 并行推进）**

---

## **1\. 施工元数据与基线硬红线**

### **1.1 施工元数据**

* **施工波次**：Fix Wave 1 \- Stage B（与 Fix Wave 1-A 并行推进）。  
* **核心目标**：根治问题 2A——Encoder 转码预览画面整体发灰、对比度异常、Log 灰片感与分屏色彩失真。  
* **代码基线锁定**：Commit `d18b469`（系统类型总数约束 `AuditTypes = 34/35`，动态导入符号数约束 `AuditImports` GUI `<= 4` / HEADLESS `== 2`）。

### **1.2 宪法硬红线 (Constitutional Red Lines)**

* **零新增具名类型 (Zero Named Types)**：`AuditTypes` 必须严格保持在 34（或 HEADLESS 下的 35），禁止在任何 C++ 或 HLSL 头文件中添加新的 `enum`、`struct` 或 `class` 定义。  
* **零类型传递机制**：新增色彩空间元数据全部通过 `uint32_t` 位掩码常量、`borrow_preview` 标量指针出参以及 `std::array<uint32_t, 16>` 根常量（Root Constants）直接传递。  
* **零新增静态 DLL 依赖 (Zero New Static Imports)**：禁止链接额外的 DXGI 或 Direct3D 静态库，DirectX 交互必须通过既有 `Microsoft::WRL::ComPtr` 的 `QueryInterface` 运行时获取接口。

---

## **2\. 色度学确定性定因分析**

定位精确代码坐标：`src/gui/workstation_canvas.cpp:1108-1111`。

原始代码逻辑如下：

```
// src/gui/workstation_canvas.cpp:1108-1111 (BEFORE FIX)
preview_viewport_->presentShared(
    preview_ptr,
    960, 540,
    /*input_is_pq=*/true,
    /*max_nits=*/100.0f
);
```

上述硬编码逻辑触发了以下三重色彩畸变：

### **2.1 SDR 信号误入 ST.2084 EOTF 导致抬升中灰与灰雾**

当关闭 TrueHDR 转码 SDR 素材，或开启 TrueHDR 时分屏左侧原图（leftIsOriginal）为 BT.709 Gamma 2.2 信号时，Gamma 2.2 规范下的中灰数值 \$V \= 0.50\$（代表真实显示亮度 \$21.764\\text{ nits}\$）被 `input_is_pq=true` 错误送入 SMPTE ST.2084 EOTF 变换：

\$\$L \= 10000 \\cdot \\left( \\frac{\\max(V^{1/m\_2} \- c\_1, 0)}{c\_2 \- c\_3 \\cdot V^{1/m\_2}} \\right)^{1/m\_1} \\text{nits}\$\$

其中 \$m\_1 \= \\frac{2610}{16384} \\approx 0.15930176\$, \$m\_2 \= \\frac{2523}{32} \= 78.84375\$, \$c\_1 \= \\frac{3424}{4096} \\approx 0.8359375\$, \$c\_2 \= \\frac{2413}{128} \\approx 18.8515625\$, \$c\_3 \= \\frac{2392}{128} \\approx 18.6875\$。

经过计算，中灰 \$V \= 0.50\$ 被虚高拉伸至 \$92.246\\text{ nits}\$（暴增 4.24 倍）；高光区域 \$V \= 0.70\$（原 \$45.626\\text{ nits}\$）飙升至 \$620.879\\text{ nits}\$。随后，该虚高亮度直接撞击 `max_nits = 100.0f` 的硬裁切逻辑，导致亮部层次全部丢失，画面呈现低反差发灰与暗部发白。

### **2.2 TrueHDR 输出未对齐 frame\_max\_nits**

在 TrueHDR 开启时，算法输出的真实峰值亮度由 `RtxNgxRemasterOutput::frame_max_nits` 动态指定。硬编码 max\_nits \= 100.0f 强制使 BT.2390 EETF 曲线的源峰值与目标 SDR 峰值比退化为 \$1.0\$。所有介于 \$100\\text{ nits}\$ 至 \$600\\text{ nits}\$ 之间的动态高光被无差别裁切或压缩，使画面极易出现高光过暴或严重失真的 Log 灰片感及退饱和现象。

### **2.3 单一布尔量无法表达分屏异构色彩空间**

开启 TrueHDR 转码 SDR 时，分屏左侧为 BT.709 Gamma 2.2 线性空间，右侧为 BT.2020 ST.2084 PQ 空间。单一 `input_is_pq` 布尔传递参数在逻辑上无法兼顾左右两侧不同的 EOTF、色域（Color Gamut）及峰值亮度需求。

---

## **3\. Task 1：双侧原生色彩写入与位掩码契约**

### **Step 1.1：零类型开销 `uint32_t` 色彩标志位定义**

在内部共享头文件（如 `src/core/preview_constants.h`）中定义 `constexpr uint32_t` 位掩码常量，不引入任何 `enum` 或 `struct`：p  
// 零类型开销位掩码定义 (Strictly No New Types)  
constexpr uint32\_t kPreviewLeftPq      \= 1u \<\< 0;  
constexpr uint32\_t kPreviewLeftHlg     \= 1u \<\< 1;  
constexpr uint32\_t kPreviewLeftBt2020  \= 1u \<\< 2;  
constexpr uint32\_t kPreviewRightPq     \= 1u \<\< 3;  
constexpr uint32\_t kPreviewRightHlg    \= 1u \<\< 4;  
constexpr uint32\_t kPreviewRightBt2020 \= 1u \<\< 5;

````

### Step 1.2：CUDA 打包核与 `borrow_preview` 契约升级
在 `src/shim/ngx_bridge/bridge_exports.cpp` 及 `src/core/ngx_remaster.cpp` 中，维护 `preview_rgba10_ptr` 的写入逻辑：

1. **保持分屏两侧原生传递函数与基色**：CUDA 打包核禁止将 SDR 原图预转码为 PQ，避免因 10-bit 码率分配不均导致暗部产生色带 (Color Banding)。
2. **Full-Range 展开**：将 10-bit narrow-range（`64..940`）数值无损展开至 full-range（`0..1023`），消除黑位抬升引起的灰雾感：

$$V_{\text{full}} = \text{clamp}\left(\frac{V_{\text{narrow}} - 64}{940 - 64} \times 1023.0\text{f}, 0.0\text{f}, 1023.0\text{f}\right)$$

3. **`borrow_preview` 标量出参导出**：在 `borrow_preview` 函数签名中增加轻量标量出参指针，并在预览槽位提取时按四类工况精准填充：

```cpp
// C-API / C++ 接口扩展（使用原生指针，零类型新增）
bool borrow_preview(
    void** out_rgba10_ptr,
    uint32_t* out_width,
    uint32_t* out_height,
    uint32_t* out_pitch,
    uint32_t* out_color_flags,
    float* out_left_max_nits,
    float* out_right_max_nits
);
````

### **四类标准工况填充规则**

| 工况分类 | 条件描述 | out\_color\_flags 组合 | out\_left\_max\_nits | out\_right\_max\_nits |
| :---- | :---- | :---- | :---- | :---- |
| **工况 1** | SDR 输入 \+ 关闭 TrueHDR | `0` | `100.0f` | `100.0f` |
| **工况 2** | SDR 输入 \+ 开启 TrueHDR | `kPreviewRightPq | kPreviewRightBt2020` | `100.0f` | `std::max(out.frame_max_nits, 203.0f)` |
| **工况 3** | HDR10 输入 (BT.2020 PQ) | `kPreviewLeftPq | kPreviewLeftBt2020 | kPreviewRightPq | kPreviewRightBt2020` | 源容器 `MaxCLL`（若缺失回落 `1000.0f`） | `std::max(out.frame_max_nits, 100.0f)` |
| **工况 4** | HLG 输入 (BT.2100 HLG) | `kPreviewLeftHlg | kPreviewLeftBt2020 | [Right Flags]` | `1000.0f` | 对齐目标输出峰值亮度与色彩空间 |

---

# **4\. Task 2：`preview_tone_map_cs.hlsl` 与 DXGI 交换链升级**

## **Step 2.1：消除 `workstation_canvas.cpp` 接口硬编码**

修改 `src/gui/workstation_canvas.cpp`，彻底废除硬编码实参：

```
// src/gui/workstation_canvas.cpp (AFTER FIX)
void* preview_ptr = nullptr;
uint32_t width = 0, height = 0, pitch = 0, color_flags = 0;
float left_max_nits = 100.0f, right_max_nits = 100.0f;

if (borrow_preview(&preview_ptr, &width, &height, &pitch, &color_flags, &left_max_nits, &right_max_nits)) {
    preview_viewport_->presentShared(
        preview_ptr,
        width, height, pitch,
        color_flags,
        left_max_nits, right_max_nits
    );
}
```

## **Step 2.2：交换链色彩空间强制对齐**

在 `PreviewViewport` 初始化或窗口尺寸变更重建 SwapChain 时，利用既有 `ComPtr::As` 执行 QueryInterface，设定 DXGI 交换链色彩空间：

```
// 零新增静态 DLL 绑定
Microsoft::WRL::ComPtr<IDXGISwapChain3> swap_chain3;
if (SUCCEEDED(swap_chain_.As(&swap_chain3))) {
    swap_chain3->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
}
```

## **Step 2.3：`preview_tone_map_cs.hlsl` 核心实现**

修改 `src/gui/shaders/preview_tone_map_cs.hlsl`，利用 `std::array<uint32_t, 16>` 根常量映射解包参数，并实现分屏双侧独立 EOTF、BT.2390 Hermite 色调映射与 BT.2020 到 BT.709 转换矩阵。

```
// src/gui/shaders/preview_tone_map_cs.hlsl

cbuffer PreviewParams : register(b0)
{
    uint   g_ColorFlags;     // 偏移 0: 位掩码
    float  g_LeftMaxNits;    // 偏移 1: 左侧峰值亮度
    float  g_RightMaxNits;   // 偏移 2: 右侧峰值亮度
    uint   g_SplitX;         // 偏移 3: 分屏线 X 坐标
    uint4  g_Reserved[3];    // 补齐 16 个 uint32_t (64 字节) 根常量
};

Texture2D<float4>   g_InputBuffer  : register(t0);
RWTexture2D<float4> g_OutputBuffer : register(u0);

// BT.2020 到 BT.709 线性色域转换矩阵
static const float3x3 M_2020_to_709 = float3x3(
     1.6605f, -0.5876f, -0.0728f,
    -0.1246f,  1.1329f, -0.0083f,
    -0.0182f, -0.1006f,  1.1187f
);

// SMPTE ST.2084 EOTF: 还原绝对亮度 (Nits)
float3 ApplyST2084_EOTF(float3 N)
{
    const float m1 = 0.1593017578125f;
    const float m2 = 78.84375f;
    const float c1 = 0.8359375f;
    const float c2 = 18.8515625f;
    const float c3 = 18.6875f;

    float3 N_inv = pow(max(N, 0.0f), 1.0f / m2);
    float3 num = max(N_inv - c1, 0.0f);
    float3 den = c2 - c3 * N_inv;
    float3 rem = pow(num / den, 1.0f / m1);
    return rem * 10000.0f; // 得到绝对亮度 Nits
}

// HLG EOTF: ARIB STD-B67
float3 ApplyHLG_EOTF(float3 E)
{
    const float a = 0.17883277f;
    const float b = 0.28466892f;
    const float c = 0.55991073f;
    
    float3 N = max(E, 0.0f);
    float3 linear_rgb;
    [unroll]
    for(int i = 0; i < 3; ++i)
    {
        if (N[i] <= 0.5f)
            linear_rgb[i] = (N[i] * N[i]) / 3.0f;
        else
            linear_rgb[i] = (exp((N[i] - c) / a) + b) / 12.0f;
    }
    return linear_rgb * 1000.0f; // 标定峰值 1000 nits
}

// ITU-R BT.2390 EETF: 三次 Hermite 样条高光平滑滚降
float3 ApplyBT2390_EETF(float3 color_nits, float max_lum)
{
    float max_rgb = max(color_nits.r, max(color_nits.g, color_nits.b));
    if (max_rgb <= 10.0f || max_lum <= 100.0f)
    {
        return min(color_nits, 100.0f);
    }

    // 映射空间参数归一化
    float E = max_rgb / max_lum;
    float KS = 10.0f / max_lum; // 10 nits 拐点
    
    float E_mapped = E;
    if (E > KS)
    {
        // Hermite 样条平滑插值
        float T = (E - KS) / (1.0f - KS);
        float T2 = T * T;
        float T3 = T2 * T;
        float P = (2.0f * T3 - 3.0f * T2 + 1.0f) * KS + 
                  (T3 - 2.0f * T2 + T) * (1.0f - KS) + 
                  (-2.0f * T3 + 3.0f * T2) * (100.0f / max_lum);
        E_mapped = P;
    }

    float scale = E_mapped / max(max_rgb, 1e-5f);
    return min(color_nits * scale, 100.0f);
}

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 coord = dispatchThreadID.xy;
    float4 raw_sample = g_InputBuffer[coord];
    float3 rgb = raw_sample.rgb;

    // 解析分屏参数与双侧标志位
    bool is_right = (coord.x >= g_SplitX);
    uint flags = g_ColorFlags;
    
    bool is_pq     = is_right ? ((flags & 8u) != 0u) : ((flags & 1u) != 0u);
    bool is_hlg    = is_right ? ((flags & 16u) != 0u) : ((flags & 2u) != 0u);
    bool is_bt2020 = is_right ? ((flags & 32u) != 0u) : ((flags & 4u) != 0u);
    float src_max_nits = is_right ? g_RightMaxNits : g_LeftMaxNits;

    float3 linear_nits;

    // 步骤 1: 执行分屏独立 EOTF
    if (is_pq)
    {
        linear_nits = ApplyST2084_EOTF(rgb);
    }
    else if (is_hlg)
    {
        linear_nits = ApplyHLG_EOTF(rgb);
    }
    else
    {
        // SDR BT.709 Gamma 2.2 解码至线性绝对亮度 (按 100 nits 标定)
        linear_nits = pow(max(rgb, 0.0f), 2.2f) * 100.0f;
    }

    // 步骤 2: BT.2390 色调映射 (Tone Mapping)
    float3 mapped_nits = ApplyBT2390_EETF(linear_nits, src_max_nits);
    
    // 归一化至 [0.0, 1.0] 域 (以 100 nits 为 SDR 满度)
    float3 linear_rgb = mapped_nits / 100.0f;

    // 步骤 3: 色域转换 (BT.2020 -> BT.709)
    if (is_bt2020)
    {
        linear_rgb = mul(M_2020_to_709, linear_rgb);
    }

    // 步骤 4: 安全 Clamp 与 Gamma 2.2 编码输出
    linear_rgb = clamp(linear_rgb, 0.0f, 1.0f);
    float3 final_srgb = pow(linear_rgb, 1.0f / 2.2f);

    g_OutputBuffer[coord] = float4(final_srgb, raw_sample.a);
}
```

---

# **5\. 验收 Gate（本波次退出判据）**

施工完成后，必须按顺序通过以下 4 项静态与动态自动化验收检查：

```
[验收检验工作流]
  │
  ├── 1. 静态代码审计 (Audit Baseline)
  │      ├── AuditTypes == 34/35 (零新增 C++ 类型)
  │      └── AuditImports: GUI <= 4, HEADLESS == 2 (零静态 DLL)
  │
  ├── 2. 测试场景 A: SDR 关闭 TrueHDR
  │      └── 结果比对: 左右分屏 RGB 差异幅度 < 1/1024，无黑位抬升或灰雾
  │
  ├── 3. 测试场景 B: SDR 开启 TrueHDR
  │      └── 结果比对: 左侧为标准 SDR 观感，右侧高光滚降且 MaxNits == frame_max_nits
  │
  └── 4. 测试场景 C: HDR10 (BT.2020 PQ) 输入
         └── 结果比对: 左侧依据 MaxCLL 映射，无 Log 灰片、退饱和与色偏
```

## **5.1 验收标准清单**

1. **关闭 TrueHDR 转码 SDR**：分屏左右两侧画面在亮度、对比度与饱和度上保持 \$1:1\$ 完全一致，消除黑位抬升引起的灰雾感。  
2. **开启 TrueHDR 转码 SDR**：左侧原图精确维持标准 SDR BT.709 观感，右侧转码结果的高光呈现柔和平滑滚降，且 `right_max_nits` 严格等于 `RtxNgxRemasterOutput::frame_max_nits`。  
3. **转码 HDR10 PQ 素材**：左侧原图精准按照源容器 `MaxCLL` 进行 BT.2390 色调映射，不存在 Log 灰片感及偏色现象。  
4. **编译与类型约束**：系统构建日志中 `AuditTypes` 计数维持在 `34`（GUI）或 `35`（HEADLESS），GUI 模块动态导入库数量 `AuditImports <= 4`，HEADLESS 模块 `AuditImports == 2`。

