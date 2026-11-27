# **80\_waveC-D\_60fps增强播放器\_任意比例RIFE与D2D\_IAT归1终局施工单**

Superpowers 原子终局封板规约（依据 76 号终审裁决《CineRTX Phase 13 74号返工终审验收报告与单EXE极限Modern C++重构演进方案》）

---

## **1\. 施工元数据与终局封板目标**

### **1.1 施工波次与演进阶段**

本施工单覆盖 **Wave C** 与 **Wave D** 两大终局波次，紧接 79\_waveB-A 完成后全面推进，直至 CineRTX Phase 13 终局大封板。

* **Wave C 目标**：落地物理分立单 Profile 任意比例 RIFE 引擎池、PTX 寄存器安全等价核验以及真·60fps AI 实时增强播放器管线；打通从 NVDEC 解码到 DComp 呈现的全链路实时 GPU 渲染。  
* **Wave D 目标**：落地基于无状态 `sys_dynload` 动态调用的 D2D1/DWrite/DComp 极净 UI 体系，实现零 `UiWidgetNode` 类的纯数据驱动绘制；全面收敛单源 `std::vformat` i18n 系统与契约式横向拖拽 DAG 画布；将 `CineRTX.exe` 静态导入表（IAT）依次剥离 `gdi32.dll` (`<= 2`) 与 `user32.dll` (`== 1`)，最终达成静态 IAT 归 1 的终局架构。

### **1.2 宪法级硬红线规约**

* **`[DEC-011]` TRT 单 Profile 铁律**：严禁在单个 TensorRT `.engine` 中配置多个 Optimization Profile（Phase 11.4 实证表明，多 Profile 将引发 `8.89 GB` 显存暴涨与 `< 1fps` 性能坍塌）。必须采用按画幅分桶的物理独立单 Profile `.engine` 池 \+ 面积自适应 `infer_scale` \+ 对称镜像居中填充架构。  
* **否决 `UiWidgetNode` 类膨胀**：彻底废除早期 38 号文档保留的第 35 号 `UiWidgetNode` 类配额。UI 组件必须采用纯数据表驱动与即时绘制模式（控件配置为静态只读表 \+ `std::span`，交互状态存于扁平结构体数组，绘制与命中检测全部写为无状态自由函数），确保具名类型数量零新增且总体净降。  
* **静态 IAT 终局归 1（`Static IAT == 1`）**：  
  * **Wave C 阶段**：控制静态导入库，维持 `Static IAT <= 3`（仅 `kernel32.dll`, `user32.dll`, `gdi32.dll`）。  
  * **Wave D 第一阶段**：彻底清理并剥离 `gdi32.dll` 静态符号依赖，实现 `Static IAT <= 2`。  
  * **Wave D 第二阶段**：将窗口创建、消息循环、DPI 感知与剪贴板等 `user32.dll` API 统一下沉并封装至无状态 `sys_dynload("user32.dll")` 动态解析表中，仅在显式进入桌面 GUI 分支时按需加载。最终实现 **`Static IAT == 1`（仅静态链接 `kernel32.dll`）**。

### **1.3 终局指标矩阵**

| 评估维界 | 基线要求 | 终局封板指标 | 校验机制 |
| :---- | :---- | :---- | :---- |
| **Static IAT 链接数** | `<= 3` | **`== 1`（仅 `kernel32.dll`）** | `AuditImports.cmake` / `dumpbin /imports` |
| **UI 具名类膨胀** | `UiWidgetNode` (13 类) | **`0` 类（纯数据表 \+ 无状态自由函数）** | `AuditTypes.cmake` |
| **TRT Profile 配置** | 多 Profile 动态 Shape | **物理分立单 Profile 桶池 \+ `infer_scale`** | `AuditTrtProfiles.cpp` 断言校验 |
| **代码库 SLOC** | `<= 25,000` | **`<= 23,000`** | `cloc --include-lang=C++,CUDA` |
| **实时播放帧预算** | 16.67 ms (60fps) | **`< 12.5 ms`（GPU 气泡时间 `< 0.8 ms`）** | Nsight Systems 抓轨分析 |
| **Soak 长稳运行** | 0 崩溃 | **2 小时无中断、零显存/内存泄漏** | `rtx_tests.exe --gtest_filter=SoakTest.*` |

---

## **2\. Wave C：任意比例分立桶 RIFE、PTX 等价校验与 60fps AI 实时增强播放器**

### **2.1 Task C-1：任意画幅 RIFE 分立单 Profile 引擎池与几何统一**

#### **2.1.1 物理分立分桶策略**

为了消除多 Profile 带来的显存剧烈波动，构建按图像比例与分辨率划分的 4 组物理独立单 Profile RIFE `.engine` 池：

```
                    [ 输入视频帧 Frame_t0, Frame_t1 ]
                                   │
                         │ 几何分辨率 & 宽高比分析 │
                                   ▼
             ┌───────────────────────────────────────────┐
             │       RIFE Engine Pool (Single-Profile)   │
             ├─────────────┬─────────────┬───────────────┤
             │ Wide (横屏) │ Tall (竖屏) │ Ultra (超宽)  │ Square (方屏) │
             │ 1920x1080   │ 1080x1920   │ 5120x1440     │ 2560x2560     │
             └──────┬──────┴──────┬──────┴──────┬────────┴──────┬────────┘
                    │             │             │               │
                    └─────────────┴──────┬──────┴───────────────┘
                                         ▼
                             [ 面积自适应 infer_scale ]
                                         │ (像素数 > 匹配桶面积上限)
                                         ▼
                             [ 四边对称镜像居中填充/裁剪 ]
                                         │
                                         ▼
                              [ TRT 核心 Tensor 推理 ]
```

1. **Wide (横屏桶)**：基准分辨率 `1920x1080`（支持 16:9 及相近比例）。  
2. **Tall (竖屏桶)**：基准分辨率 `1080x1920`（支持 9:16 短视频源）。  
3. **Ultra (超宽屏桶)**：基准分辨率 `5120x1440`（覆盖 `32:9` 极客双屏及 `5120x2160` `21:9` 5K 带宽流）。  
4. **Square (方屏桶)**：基准分辨率 `2560x2560`（覆盖 `1:1` 及 `4:3` 工业检测/复古帧）。

#### **2.1.2 FG-First 几何流水线与 NV12 中间表面**

系统默认采用 **FG-First**（原生分辨率 RIFE 插帧后再送入 VSR 放大）流水线。当用户强制指定 `vsr_before_fg` 模式时，必须分配独立的 NV12 双槽中间表面对，并执行严格的入口几何硬断言：

```
struct RifeExecutionContext {
    CUstream stream{nullptr};
    nvinfer1::ICudaEngine* engine{nullptr};
    nvinfer1::IExecutionContext* exec_context{nullptr};
    void* bindings[4]{nullptr}; // Input0, Input1, Timestep, Output
    uint32_t bucket_width{0};
    uint32_t bucket_height{0};
};

struct IntermediateNv12SurfacePair {
    ID3D12Resource* y_plane_0{nullptr};
    ID3D12Resource* uv_plane_0{nullptr};
    ID3D12Resource* y_plane_1{nullptr};
    ID3D12Resource* uv_plane_1{nullptr};
    uint32_t width{0};
    uint32_t height{0};
    DXGI_FORMAT format{DXGI_FORMAT_NV12};
};

inline void assert_vsr_before_fg_geometry(
    const IntermediateNv12SurfacePair& surf,
    uint32_t target_w,
    uint32_t target_h
) {
    CINETX_ASSERT(surf.width == target_w && surf.height == target_h,
        "VSR-Before-FG 几何校验失败: 表面尺寸 ({}, {}) 与目标尺寸 ({}, {}) 不匹配",
        surf.width, surf.height, target_w, target_h);
    CINETX_ASSERT(surf.y_plane_0 != nullptr && surf.uv_plane_0 != nullptr,
        "VSR-Before-FG 几何校验失败: NV12 表面未初始化或存在空句柄");
}
```

当输入图像面积超出目标桶的推理上限时，自动计算 `infer_scale = 0.5f` 并应用四边对称镜像居中填充（Symmetric Reflection Padding）与推理后精确裁剪：

\$\$\\text{Pad}*{\\text{left}} \= \\left\\lfloor \\frac{W*{\\text{bucket}} \- W\_{\\text{scaled}}}{2} \\right\\rfloor, \\quad \\text{Pad}*{\\text{top}} \= \\left\\lfloor \\frac{H*{\\text{bucket}} \- H\_{\\text{scaled}}}{2} \\right\\rfloor\$\$

#### **2.1.3 PTX 寄存器安全等价与上下文隔离**

核验 `rife_pack.cu` 与编译后的 `rife_pack.ptx`，确保数值安全与流水线流隔离：

1. **IEEE-754 常数**：PTX 汇编中浮点常数必须全数使用十六进制表示（如 `0F3F800000` 表示 `1.0f`）。  
2. **NaN / Inf 硬件过滤**：调用 `testp.finite.f32` 指令检测输入像素，防止非有限值导致 CUDA Kernel 挂死。  
3. **颜色范围与 Saturate 钳位**：全量补齐 `full_range * 255.0f` 缩放，色彩通道输出使用 `clamp.f32` 锁定于 `[0.0f, 1.0f]`，防止 RGB/YUV 色彩溢出造成"紫闪"现象。  
4. **流与上下文隔离**：使用 `CU_STREAM_NON_BLOCKING` 独立 CUDA 流，并在调用前后通过 `runInRifeContext` 宏对 CUDA Context 进行压栈与弹栈隔离。

```
// rife_pack.cu: 浮点安全打包与 Saturate 钳位 Kernel 示例
extern "C" __global__ void pack_nv12_to_rife_bgr_kernel(
    const uint8_t* __restrict__ src_y,
    const uint8_t* __restrict__ src_uv,
    float* __restrict__ dst_tensor,
    int width, int height, int pitch_y, int pitch_uv,
    int pad_left, int pad_top, int bucket_w, int bucket_h
) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= bucket_w || y >= bucket_h) return;

    // 计算映射回原始 scaled 图像的坐标
    int src_x = x - pad_left;
    int src_y_pos = y - pad_top;

    float r = 0.0f, g = 0.0f, b = 0.0f;

    if (src_x >= 0 && src_x < width && src_y_pos >= 0 && src_y_pos < height) {
        // 读取 NV12 像素
        uint8_t y_val = src_y[src_y_pos * pitch_y + src_x];
        int uv_idx = (src_y_pos >> 1) * pitch_uv + (src_x & ~1);
        uint8_t u_val = src_uv[uv_idx];
        uint8_t v_val = src_uv[uv_idx + 1];

        // BT.709 转换矩阵计算
        float y_f = (float)y_val - 16.0f;
        float u_f = (float)u_val - 128.0f;
        float v_f = (float)v_val - 128.0f;

        r = __saturatef((1.164383f * y_f + 1.792741f * v_f) / 255.0f);
        g = __saturatef((1.164383f * y_f - 0.213249f * u_f - 0.532909f * v_f) / 255.0f);
        b = __saturatef((1.164383f * y_f + 2.112402f * u_f) / 255.0f);
    } else {
        // 四边镜像填充边界处理
        int clamped_x = min(max(src_x, 0), width - 1);
        int clamped_y = min(max(src_y_pos, 0), height - 1);
        uint8_t y_val = src_y[clamped_y * pitch_y + clamped_x];
        r = g = b = __saturatef(((float)y_val - 16.0f) / 219.0f);
    }

    // NCHW 排列写出: dst_tensor [3, bucket_h, bucket_w]
    int plane_stride = bucket_w * bucket_h;
    int pixel_idx = y * bucket_w + x;

    dst_tensor[0 * plane_stride + pixel_idx] = b;
    dst_tensor[1 * plane_stride + pixel_idx] = g;
    dst_tensor[2 * plane_stride + pixel_idx] = r;
}
```

---

### **2.2 Task C-2：真·60fps AI 实时增强播放器与 WebUI 远程控制台定界**

#### **2.2.1 全端到端 GPU 端到端 Pipeline**

彻底废弃 `createHeadlessPreviewSink` 原始帧直通旁路，构建零拷贝的全实时 GPU 管线：

```
 [NVDEC Direct] ──► [Temporal Boundary Analysis] ──► [FRUC (RIFE/NVOFA)] ──► [Viewport VSR Scale]
                                                                                  │
 [DComp SwapChain] ◄── [D3D12 ToneMap] ◄── [A/B Split View] ◄── [TrueHDR + 3D LUT] ◄──┘
```

#### **2.2.2 `<chrono>` 编译期有理数分数插帧与 WASAPI 熔断**

使用 C++20 `<chrono>` 的编译期有理数类型对齐帧率转换相位，计算 `23.976fps` (`1001/24000` s) 到 `60.000fps` (`1/60` s) 的精准内插时间戳：

```
using FPS23976 = std::chrono::duration<int64_t, std::ratio<1001, 24000>>;
using FPS60000 = std::chrono::duration<int64_t, std::ratio<1, 60>>;

class VideoClockSynchronizer {
public:
    enum class ClockState { Normal, AudioDrift, MeltdownBypass };

    struct PresentationInfo {
        int64_t frame_index;
        double  pts_seconds;
        float   rife_timestep; // [0.0f, 1.0f] 内插相位
        bool    is_synthetic;
    };

    PresentationInfo calculate_next_frame(int64_t target_display_ticks_60hz, int64_t audio_clock_us) {
        int64_t video_pts_us = current_video_pts_us_.load(std::memory_order_relaxed);
        int64_t delta_us = audio_clock_us - video_pts_us;

        // WASAPI 音频时钟漂移检测 (阈值 8ms)
        if (std::abs(delta_us) > 8000) {
            state_ = ClockState::AudioDrift;
            if (std::abs(delta_us) > 32000) {
                // 严重卡顿：触发 AI 熔断降级，跳过 RIFE 插帧直接输出硬拷贝
                state_ = ClockState::MeltdownBypass;
                return PresentationInfo{ current_frame_idx_, video_pts_us * 1e-6, 0.0f, false };
            }
        } else {
            state_ = ClockState::Normal;
        }

        // 计算 60Hz 采样下的精确插帧相位
        double source_frame_exact = (double)target_display_ticks_60hz * (24000.0 / 1001.0) / 60.0;
        int64_t base_frame = static_cast<int64_t>(std::floor(source_frame_exact));
        float alpha = static_cast<float>(source_frame_exact - base_frame);

        return PresentationInfo{ base_frame, source_frame_exact * (1001.0 / 24000.0), alpha, alpha > 0.001f };
    }

    void reset_temporal_buffers() {
        // Seek / Pause / StepFrame 瞬时清除历史时域缓存，防止画面残影
        prev_frame_valid_ = false;
        state_ = ClockState::Normal;
    }

private:
    std::atomic<int64_t> current_video_pts_us_{0};
    int64_t current_frame_idx_{0};
    bool prev_frame_valid_{false};
    ClockState state_{ClockState::Normal};
};
```

#### **2.2.3 WebUI 远程控制台架构定界**

将 WebUI 架构明确界定为："远程控制台 \+ 轻量 MJPEG/H.264 代理预览面板"。放弃让浏览器 HTML5 \<video\> 直接解码 10-bit HDR MKV 高码率源视频的伪需求：

```
  ┌─────────────────────────────────────────────────────────────┐
  │                    CineRTX 宿主核心引擎                     │
  │  ┌─────────────────────────────┐   ┌─────────────────────┐  │
  │  │ D3D12/DComp 本地零拷贝渲染链 │   │ NVENC 代理流编码器  │  │
  │  └──────────────┬──────────────┘   └──────────┬──────────┘  │
  └─────────────────┼─────────────────────────────┼─────────────┘
                    │ Direct Display              │ WebRTC / WebSocket
                    ▼                             ▼
           [ 本地 HDR 监视器 ]           [ WebUI 远程控制台 ]
                                         (控制指令 + 720p 代理流)
```

#### **2.2.4 Nsight Systems 帧预算实测验证规约**

在 RTX 5060 目标平台上，通过 Nsight Systems 进行帧预算分析：

* **60fps 帧上限时间**：\$1000 \\text{ ms} / 60 \= 16.67 \\text{ ms}\$。  
* **目标配额 allocated**：NVDEC (1.5ms) \+ RIFE (6.2ms) \+ Viewport VSR (3.1ms) \+ ToneMap/DComp (0.8ms) \= **11.6ms**（保留 5.07ms 盈余）。  
* **CUDA 流气泡硬断言**：同一 CUDA Stream 内上下文切换造成的 Bubble 气泡必须小于 **0.8ms**。  
* **120-rife 校验**：实测 `120-rife` 模式下输出非重复帧 (`dup == false`) 且 NV12 UV 通道符合 DXGI 色域合法性。

---

## **3\. Wave D：D2D1/DWrite 极净 UI（无 UiWidgetNode）、单源 std::vformat i18n、契约式拖拽 DAG 与 Static IAT 归 1**

### **3.1 Task D-1：动态 Direct2D/DirectWrite/DComp 极净自绘体系与剥离 gdi32.dll**

#### **3.1.1 零类膨胀：纯数据表驱动 UI**

彻底废除类继承式的 UI 组件库（取消 `UiWidgetNode` 及其子类），完全采用静态配置只读表、扁平交互状态数组与无状态自由函数：

```
// 扁平交互状态（无任何多态虚表）
struct WidgetState {
    uint32_t id;
    bool is_hovered;
    bool is_pressed;
    bool is_disabled;
    float animation_alpha;
};

// 静态 UI 元素只读配置
struct WidgetLayoutConfig {
    uint32_t id;
    float rel_x, rel_y;
    float width, height;
    const char* i18n_key;
    uint32_t fill_color_rgba;
    uint32_t hover_color_rgba;
};

// UI 渲染与命中检测无状态自由函数集
namespace CineUI {

inline bool hit_test_widget(const WidgetLayoutConfig& config, float mouse_x, float mouse_y) {
    return (mouse_x >= config.rel_x && mouse_x <= config.rel_x + config.width &&
            mouse_y >= config.rel_y && mouse_y <= config.rel_y + config.height);
}

void draw_button_flex(
    ID2D1RenderTarget* target,
    IDWriteTextFormat* text_format,
    const WidgetLayoutConfig& config,
    const WidgetState& state,
    std::wstring_view localized_text
) {
    D2D1_RECT_F rect = D2D1::RectF(config.rel_x, config.rel_y, config.rel_x + config.width, config.rel_y + config.height);
    uint32_t color = state.is_hovered ? config.hover_color_rgba : config.fill_color_rgba;

    ID2D1SolidColorBrush* brush = nullptr;
    D2D1_COLOR_F d2d_color = D2D1::ColorF(
        ((color >> 24) & 0xFF) / 255.0f,
        ((color >> 16) & 0xFF) / 255.0f,
        ((color >> 8) & 0xFF) / 255.0f,
        (color & 0xFF) / 255.0f
    );
    target->CreateSolidColorBrush(d2d_color, &brush);
    if (!brush) return;

    target->FillRoundedRectangle(D2D1::RoundedRect(rect, 4.0f, 4.0f), brush);

    // 绘制内嵌文本
    brush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
    target->DrawTextW(
        localized_text.data(),
        static_cast<UINT32>(localized_text.size()),
        text_format,
        rect,
        brush
    );

    brush->Release();
}

} // namespace CineUI
```

#### **3.1.2 动态 DirectWrite 文本撑开与队列片段交互**

通过 `sys_dynload` 动态载入 `d2d1.dll`, `dwrite.dll`, `dcomp.dll`，利用 IDWriteTextLayout 自动计算中英文混排尺寸，动态撑开 Inspector 垂直滚动区域的高度：

```
struct FlexibleLayoutMetrics {
    float computed_height;
    float content_width;
};

FlexibleLayoutMetrics measure_i18n_text_block(
    IDWriteFactory* dwrite_factory,
    IDWriteTextFormat* text_format,
    std::wstring_view text,
    float max_width
) {
    IDWriteTextLayout* layout = nullptr;
    HRESULT hr = dwrite_factory->CreateTextLayout(
        text.data(),
        static_cast<UINT32>(text.size()),
        text_format,
        max_width,
        10000.0f, // 足够大的高度上限以测算文本折行
        &layout
    );

    if (FAILED(hr) || !layout) return { 0.0f, 0.0f };

    DWRITE_TEXT_METRICS metrics;
    layout->GetMetrics(&metrics);
    layout->Release();

    return { metrics.height + 8.0f, metrics.widthIncludingTrailingWhitespace };
}
```

#### **3.1.3 清理 `gdi32.dll` 静态依赖**

彻底移除对 GDI 绘制与 DC（Device Context）相关函数调用，清除链接器参数中的 `Gdi32.lib`。在构建脚本中通过 `AuditImports.cmake` 断言 IAT 中的 DLL 数量限制：

```
# AuditImports.cmake 阶段校验
execute_process(
    COMMAND dumpbin /imports "${CINETX_EXE_PATH}"
    OUTPUT_VARIABLE IMPORTS_LOG
)

if (IMPORTS_LOG MATCHES "gdi32.dll")
    message(FATAL_ERROR "[硬红线违规] CineRTX.exe 的 Static IAT 仍包含 gdi32.dll！必须实现彻底剥离。")
endif()
```

---

### **3.2 Task D-2：单源 `std::vformat` i18n 与契约式横向可拖拽 DAG 画布**

#### **3.2.1 单源 `std::vformat` i18n 系统**

废除旧系统遗留的 `chromeText()` 函数与孤立文本字典。建立基于编译期枚举 ID \+ `std::vformat` 动态参数插值的统一单源文本库：

```
enum class MessageId : uint32_t {
    StatusProcessing,
    ErrorNodeMismatch,
    FpsCounter,
    BufferAllocation
};

struct I18nMasterTable {
    MessageId id;
    const char* key;
    const char* en_US;
    const char* zh_CN;
};

constexpr I18nMasterTable kSystemDictionary[] = {
    { MessageId::StatusProcessing, "status.processing", "Processing frame {0}/{1} ({2:.2f} fps)", "正在处理第 {0}/{1} 帧 ({2:.2f} fps)" },
    { MessageId::ErrorNodeMismatch, "error.dag_mismatch", "Topology error: Node '{0}' requires SDR, but received {1}", "拓扑错误：节点 '{0}' 需要 SDR 输入，但接收到 {1}" },
    { MessageId::FpsCounter, "ui.fps", "FPS: {0:.1f}", "帧率: {0:.1f}" }
};

class LocalizedFormatEngine {
public:
    enum class Language { EnUS, ZhCN };

    static LocalizedFormatEngine& instance() {
        static LocalizedFormatEngine inst;
        return inst;
    }

    void set_language(Language lang) {
        current_lang_ = lang;
        // 触发 UI 全局 Layout 重测算标脏标记
        relayout_required_.store(true, std::memory_order_release);
    }

    template<typename... Args>
    std::string format(MessageId id, Args&&... args) {
        const auto& entry = kSystemDictionary[static_cast<uint32_t>(id)];
        const char* fmt_pattern = (current_lang_ == Language::ZhCN) ? entry.zh_CN : entry.en_US;
        return std::vformat(fmt_pattern, std::make_format_args(args...));
    }

    bool check_and_clear_relayout_flag() {
        return relayout_required_.exchange(false, std::memory_order_acq_rel);
    }

private:
    Language current_lang_{Language::ZhCN};
    std::atomic<bool> relayout_required_{false};
};
```

#### **3.2.2 契约式横向可拖拽 DAG 算子画布**

设计类似 Premiere 剪辑轨道风格的横向算子卡片流。每个算子输入与输出端均带有显式的**色域与精度契约标签**（如 `SDR_BT709_8BIT` 或 `HDR_BT2020_PQ_10BIT`）。

```
        ┌──────────────────┐      ┌──────────────────┐      ┌──────────────────┐
        │  NVDEC Decoder   │      │    TrueHDR AI    │      │  VSR Upscaler    │
        ├──────────────────┤      ├──────────────────┤      ├──────────────────┤
        │ Out: BT.709 8BIT ├─────►│ In:  BT.709 8BIT │  ┌──►│ In:  BT.709 8BIT │ (拓扑锁警报)
        └──────────────────┘      │ Out: BT.2020 PQ  ├──┘   └──────────────────┘
                                  └──────────────────┘                ▲
                                           │                          │ (自动补全节点)
                                           └────► [ Auto ToneMap ] ───┘
                                                  (BT.2020 -> BT.709)
```

当用户在 GUI 中将一个输出 BT.2020 PQ 色域的节点连入仅支持 SDR 格式的下游算子时：

1. **DAG 校验规则**：计算节点拓扑相容性矩阵。  
2. **UI 警报 / 自动补全**：UI 实时显示拓扑锁提示框，或者自动在连线上插入无损 `Auto ToneMap (BT.2020 -> BT.709)` 色彩转换节点，杜绝任何隐式的颜色空间强制覆写行为。

```
enum class ColorSpaceContract : uint32_t {
    SDR_BT709_8BIT,
    SDR_BT709_10BIT,
    HDR_BT2020_PQ_10BIT,
    HDR_BT2020_HLG_10BIT
};

struct DagNodeContract {
    uint32_t node_id;
    const char* node_name;
    ColorSpaceContract input_contract;
    ColorSpaceContract output_contract;
};

struct DagValidationResult {
    bool is_valid;
    bool auto_fix_available;
    MessageId error_msg_id;
    const char* conflicting_node_name;
};

inline DagValidationResult validate_dag_edge(const DagNodeContract& src, const DagNodeContract& dst) {
    if (src.output_contract == dst.input_contract) {
        return { true, false, MessageId::StatusProcessing, "" };
    }

    // 检测到色彩空间不匹配
    if (src.output_contract == ColorSpaceContract::HDR_BT2020_PQ_10BIT &&
        dst.input_contract == ColorSpaceContract::SDR_BT709_8BIT) {
        // 允许自动插入 ToneMap 节点
        return { false, true, MessageId::ErrorNodeMismatch, dst.node_name };
    }

    return { false, false, MessageId::ErrorNodeMismatch, dst.node_name };
}
```

---

### **3.3 Task D-3：将 user32.dll 窗口 API 下沉至 GUI 按需 sys\_dynload，达成 Static IAT \== 1 终局**

为了使 `CineRTX.exe` 在执行 CLI 无头批处理任务或自动化测试时，进程地址空间中零加载 user32.dll 与 win32u.sys 内核模块，必须将全部 Windowing、Message Processing、DPI-Awareness 与 Clipboard API 彻底解耦，改由无状态 `sys_dynload` 动态按需载入：

```
// user32_dynload.h - 无状态动态解析表
#pragma once
#include <windows.h>

struct User32ApiTable {
    using PFN_CreateWindowExW = HWND(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
    using PFN_DefWindowProcW  = LRESULT(WINAPI*)(HWND, UINT, WPARAM, LPARAM);
    using PFN_DispatchMessageW = LRESULT(WINAPI*)(const MSG*);
    using PFN_GetMessageW     = BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT);
    using PFN_RegisterClassExW = ATOM(WINAPI*)(const WNDCLASSEXW*);
    using PFN_SetProcessDpiAwarenessContext = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);

    PFN_CreateWindowExW CreateWindowExW{nullptr};
    PFN_DefWindowProcW  DefWindowProcW{nullptr};
    PFN_DispatchMessageW DispatchMessageW{nullptr};
    PFN_GetMessageW     GetMessageW{nullptr};
    PFN_RegisterClassExW RegisterClassExW{nullptr};
    PFN_SetProcessDpiAwarenessContext SetProcessDpiAwarenessContext{nullptr};

    bool is_loaded{false};
};

class User32DynLoader {
public:
    static User32ApiTable& get() {
        static User32ApiTable table;
        return table;
    }

    static bool ensure_loaded() {
        User32ApiTable& table = get();
        if (table.is_loaded) return true;

        HMODULE h_user32 = LoadLibraryA("user32.dll");
        if (!h_user32) return false;

        table.CreateWindowExW = reinterpret_cast<User32ApiTable::PFN_CreateWindowExW>(GetProcAddress(h_user32, "CreateWindowExW"));
        table.DefWindowProcW  = reinterpret_cast<User32ApiTable::PFN_DefWindowProcW>(GetProcAddress(h_user32, "DefWindowProcW"));
        table.DispatchMessageW = reinterpret_cast<User32ApiTable::PFN_DispatchMessageW>(GetProcAddress(h_user32, "DispatchMessageW"));
        table.GetMessageW     = reinterpret_cast<User32ApiTable::PFN_GetMessageW>(GetProcAddress(h_user32, "GetMessageW"));
        table.RegisterClassExW = reinterpret_cast<User32ApiTable::PFN_RegisterClassExW>(GetProcAddress(h_user32, "RegisterClassExW"));
        table.SetProcessDpiAwarenessContext = reinterpret_cast<User32ApiTable::PFN_SetProcessDpiAwarenessContext>(GetProcAddress(h_user32, "SetProcessDpiAwarenessContext"));

        table.is_loaded = (table.CreateWindowExW && table.DefWindowProcW && table.DispatchMessageW && table.GetMessageW && table.RegisterClassExW);
        return table.is_loaded;
    }
};
```

入口点分支加载控制流程：

```
// main.cpp - 终局无头与 GUI 分支入口
int main(int argc, char** argv) {
    CommandLineArgs args = parse_cli(argc, argv);

    if (args.is_headless) {
        // 无头 CLI 管道：零加载 user32.dll / win32u.sys
        // 运行自动化测试、后台转码或监控任务
        return run_headless_pipeline(args);
    }

    // GUI 模式：仅在显式进入桌面窗口分支后按需载入 user32.dll
    if (!User32DynLoader::ensure_loaded()) {
        // 使用 kernel32 控制台输出错误信息
        HANDLE h_std_err = GetStdHandle(STD_ERROR_HANDLE);
        const char err_msg[] = "致命错误: 无法动态加载 user32.dll API 表！\n";
        DWORD written = 0;
        WriteFile(h_std_err, err_msg, sizeof(err_msg) - 1, &written, NULL);
        return -1;
    }

    // 初始化 DirectComposition 与桌面 UI 界面
    return run_gui_application(args);
}
```

在剥离 `user32.dll` 之后，`CineRTX.exe` 的 PE 导入表（Import Address Table）仅保留 `kernel32.dll` 一个静态链接库，成功达成 **`Static IAT == 1`** 的极致架构目标。

---

## **4\. Phase 13 终局封板与发布前门禁**

在完成 Wave C 与 Wave D 全部开发任务后，必须严格依次执行以下自动门禁检查，任何一项校验失败均视为拒绝封板：

### **4.1 编译与静态导入表断言**

```
:: build.bat - MSVC 极净严苛编译
@echo off
cl.exe /std:c++20 /O2 /MT /W4 /WX /permissive- /EHsc /Zc:preprocessor ^
    /I.\include /I"%CUDA_PATH%\include" ^
    src\main.cpp src\rife_pool.cpp src\d2d_ui.cpp ^
    /link /SUBSYSTEM:CONSOLE /MACHINE:X64 /NODEFAULTLIB:libcmtd.lib ^
    kernel32.lib ^
    /OUT:bin\CineRTX.exe

if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] 编译失败！退出码非 0。
    exit /b %ERRORLEVEL%
)

:: IAT 归 1 检查
dumpbin /imports bin\CineRTX.exe | findstr /R "\.dll" > iat_imports.txt
for /f %%A in ('type iat_imports.txt ^| find /c /v ""') do set IAT_COUNT=%%A

echo Static IAT Module Count: %IAT_COUNT%
if "%IAT_COUNT%" NEQ "1" (
    echo [FATAL] 静态 IAT 断言失败: 期望 count == 1, 实际 count = %IAT_COUNT%
    exit /b 1
)
```

### **4.2 2 小时 Soak 长稳测试规约**

自动测试集必须包含 `SoakTest` 模块，针对连续插帧、音频时钟漂移补偿与内存/显存分配进行长达 2 小时的压力测试：

```
TEST(Phase13SoakTest, ContinuousPlayback2Hours) {
    VideoClockSynchronizer clock;
    RifeEnginePool pool;
    pool.initialize();

    auto start_time = std::chrono::steady_clock::now();
    constexpr auto duration_target = std::chrono::hours(2);

    uint64_t frame_counter = 0;
    size_t initial_sys_mem = get_process_working_set_bytes();
    size_t initial_gpu_mem = get_cuda_allocated_bytes();

    while (std::chrono::steady_clock::now() - start_time < duration_target) {
        // 模拟 60Hz 实时循环
        int64_t audio_ts = frame_counter * 16666; // 16.666ms
        auto info = clock.calculate_next_frame(frame_counter, audio_ts);

        // 模拟异构合并内存/显存频繁申请与释放
        void* temp_gpu_ptr = nullptr;
        cudaMalloc(&temp_gpu_ptr, 1024 * 1024 * 16); // 16MB
        cudaFree(temp_gpu_ptr);

        frame_counter++;

        // 每 10 分钟检测一次内存漂移
        if (frame_counter % 36000 == 0) {
            size_t current_sys_mem = get_process_working_set_bytes();
            size_t current_gpu_mem = get_cuda_allocated_bytes();

            // 严禁产生渐进式内存泄露 (波动允许范围 < 5MB)
            EXPECT_LE(current_sys_mem, initial_sys_mem + 5 * 1024 * 1024);
            EXPECT_LE(current_gpu_mem, initial_gpu_mem + 5 * 1024 * 1024);
        }
    }

    EXPECT_GT(frame_counter, 400000u); // 2小时应渲染超过 432,000 帧
}
```

### **4.3 终局验收门禁指标汇总表**

```
┌─────────────────────────────────────────────────────────────────────────┐
│                   CineRTX Phase 13 Final Acceptance                     │
├──────────────────────────┬───────────────────┬──────────────────────────┤
│ 检查项 (Check Item)      │ 规约门限 (Target) │ 实际断言机制             │
├──────────────────────────┼───────────────────┼──────────────────────────┤
│ MSVC Build Exit Code     │ == 0              │ /W4 /WX 全警告视为错误   │
│ Static IAT Count         │ == 1              │ 仅保留 kernel32.dll      │
│ 2-Hour Soak Stability    │ 0 Leak / 0 Crash  │ GoogleTest Soak Suite    │
│ Class Count Net Reduction│ AuditTypes 净降   │ 零 UiWidgetNode 类新增   │
│ Source Code SLOC Limit   │ <= 23,000         │ cloc 规约扫描            │
│ Privacy Leakage Scan     │ AuditPrivacy == 0 │ 零绝对路径/个人信息硬编码│
│ SPDX Compliance Header   │ PASS              │ 全量源码许可文件头覆盖   │
└──────────────────────────┴───────────────────┴──────────────────────────┘
```

---

## **5\. 施工单签署与封板状态**

* **施工执行人**：`CineRTX Phase 13 Modern C++ Core Architecture Group`  
* **终审裁决依据**：`76号终审裁决《CineRTX Phase 13 74号返工终审验收报告与单EXE极限Modern C++重构演进方案》`  
* **当前状态**：**APPROVED FOR EXECUTION (已批准执行封板)**

