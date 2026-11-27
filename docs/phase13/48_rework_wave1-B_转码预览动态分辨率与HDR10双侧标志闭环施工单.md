# **48\_rework\_wave1-B\_转码预览动态分辨率与HDR10双侧标志闭环施工单**

## **1\. 施工元数据与核心目标**

### **1.1 施工基本信息**

* **施工波次**：`Rework Wave 1 - Stage B`（与 `Rework Wave 1-A` 并行）  
* **靶向硬伤**：46 号专家穿透复验裁决 `CINERTX-P13-EXPERT-AUDIT-046` 硬伤 3  
* **硬伤定位**：`src/gui/workstation_canvas.cpp:1100-1126` 处的 `presentTranscodePreview()` 依然传递写死的 `960u, 540u` 尺寸，且当输入源视频为 HDR10 (PQ) / HLG 时未置位左侧视口色彩标志位 `kPreviewLeftPq` / `kPreviewLeftHlg` / `kPreviewLeftBt2020`，导致左侧 Raw 预览视口色彩全失与纵横比失真。

### **1.2 宪法级硬红线**

| 校验维度 | 宪法红线指标 | 施工约束 |
| :---- | :---- | :---- |
| **具名类型数** | `AuditTypes == 34` / `35` | **零新增具名类型**（`+0` 具名 `struct`/`class`/`enum`） |
| **静态 DLL 依赖** | `AuditImports` GUI `<= 4` / HEADLESS `== 2` | **零新增静态 DLL 链接** |
| **内存总线性能** | `Zero GPU->CPU Readback` | 预览链路全程显存内组装，**零 GPU-\>CPU 像素回读** |

---

## **2\. 靶向手术 1：消除 `workstation_canvas.cpp` 尺寸硬编码**

### **2.1 穿透实证代码缺陷**

在 45 号返工中，src/gui/preview\_viewport.h:39 与 src/gui/shaders/preview\_tone\_map\_cs.hlsl:96-175 已成功实装 Aspect-Fit 居中映射算法。但调用入口 `src/gui/workstation_canvas.cpp` 的 `presentTranscodePreview()` 仍然透传硬编码分辨率：

```
// 现存缺陷代码：src/gui/workstation_canvas.cpp:1100-1126
viewport->presentShared(
    preview_frame.shared_handle,
    960u, 540u, // <--- 硬编码尺寸缺陷！忽视真实 out_width / out_height
    preview_frame.pitch,
    color_flags
);
```

### **2.2 因果危害**

当输入源视频或转码输出尺寸为 `3840x2160` (4K)、`720x1280` (9:16 竖屏) 或 2560x1080 (21:9 超宽屏) 时， Compute Shader 被强制按 960x540 几何边界采样，导致宽高比彻底失真、像素拉伸或采样越界错位。

### **2.3 精确代码修复指令**

#### **修改文件：`src/gui/workstation_canvas.cpp`**

从 `preview_frame` 结构中提取由 `src/shim/ngx_bridge/bridge_exports.cpp` (`borrow_preview`) 导出的真实宽度与高度 `out_width` / `out_height`，并添加防御性边界校验：p  
// 修复后代码：src/gui/workstation\_canvas.cpp  
void WorkstationCanvas::presentTranscodePreview(const TranscodePreviewFrame& preview\_frame, const TranscodeProfile& prof) {  
if (\!preview\_frame.shared\_handle \|\| preview\_frame.width \== 0 \|\| preview\_frame.height \== 0\) {  
return;  
}

```
const uint32_t src_w = preview_frame.width;
const uint32_t src_h = preview_frame.height;

// 防御性校验
if (src_w == 0 || src_h == 0) {
    return;
}

uint32_t color_flags = 0;
float left_max_nits = 100.0f;
float right_max_nits = 100.0f;

// ... 色彩标志计算逻辑（参见靶向手术 2） ...

viewport->presentShared(
    preview_frame.shared_handle,
    src_w, 
    src_h, 
    preview_frame.pitch,
    color_flags,
    left_max_nits,
    right_max_nits
);
```

}

````

---

## 3. 靶向手术 2：补齐输入源 HDR10/HLG 左侧视口标志与双侧峰值亮度

### 3.1 穿透实证代码缺陷
```cpp
// 现存缺陷代码：src/gui/workstation_canvas.cpp:1100-1126
uint32_t color_flags = kPreviewLeftSdr; // 硬编码左侧为 SDR！
if (prof.hdr_mode == TranscodeHdrMode::kPq) {
    color_flags |= kPreviewRightPq | kPreviewRightBt2020;
}
````

## **3.2 因果危害**

系统完全忽略了输入源视频本身的 `color_trc`、`color_primaries` 及 `is_hdr` 属性。当用户输入 HDR10 (PQ) 或 HLG 视频进行转码时，左侧原始帧视口依然按照 `kPreviewLeftSdr`（`0`）渲染。 Compute Shader 跳过了 `pqToNits()` 与 `bt2390ToSdrNits()` 转换，直接将 PQ/HLG 信号当作 SDR Gamma 2.2 处理，导致左侧 Raw 预览画面呈现严重的灰白、对比度缺失（Log 灰片现象）。

## **3.3 精确代码修复指令**

### **修改文件：src/gui/workstation\_canvas.cpp**

解析 `preview_frame.src_color_trc` 与 `preview_frame.src_max_cll` 元数据，闭环组装双侧色彩标志与峰值亮度（Nits）：

```
// 修复后代码：src/gui/workstation_canvas.cpp
uint32_t color_flags = 0;
float left_max_nits = 100.0f;
float right_max_nits = 100.0f;

// 1. 左侧（原始帧）色彩标志与峰值亮度推演
if (preview_frame.src_color_trc == AVCOL_TRC_SMPTE2084) { // HDR10 / PQ
    color_flags |= (kPreviewLeftPq | kPreviewLeftBt2020);
    left_max_nits = (preview_frame.src_max_cll > 0.0f) ? preview_frame.src_max_cll : 1000.0f;
} else if (preview_frame.src_color_trc == AVCOL_TRC_ARIB_STD_B67) { // HLG
    color_flags |= (kPreviewLeftHlg | kPreviewLeftBt2020);
    left_max_nits = 1000.0f;
} else { // SDR (BT.709 / Gamma 2.2)
    color_flags |= kPreviewLeftSdr;
    left_max_nits = 100.0f;
}

// 2. 右侧（处理后帧）色彩标志与峰值亮度推演
if (prof.hdr_mode == TranscodeHdrMode::kPq || prof.enable_true_hdr) {
    color_flags |= (kPreviewRightPq | kPreviewRightBt2020);
    // 右侧峰值亮度严格对齐 RtxNgxRemasterOutput::frame_max_nits
    right_max_nits = (preview_frame.dst_max_nits > 0.0f) ? std::max(preview_frame.dst_max_nits, 203.0f) : 1000.0f;
} else {
    // 右侧为 SDR 输出（包含 HDR->SDR 色调映射）
    color_flags |= kPreviewRightSdr;
    right_max_nits = 100.0f;
}
```

---

# **4\. 靶向手术 3：TDD 单元测试覆盖**

修改文件：`tests/test_gui_workstation.cpp`，补充针对 HDR10/HLG 输入源在 GUI 视口映射中色彩标志与 Nits 值的全覆盖断言。

```
// 新增单元测试用例：tests/test_gui_workstation.cpp
TEST_F(WorkstationCanvasTest, TranscodePreviewHdr10InputFlagsVerification) {
    WorkstationCanvas canvas;
    TranscodePreviewFrame frame{};
    frame.shared_handle = (void*)0x12345678;
    frame.width = 3840;
    frame.height = 2160;
    frame.pitch = 3840 * 4;
    frame.src_color_trc = AVCOL_TRC_SMPTE2084; // PQ HDR10
    frame.src_max_cll = 1000.0f;
    frame.dst_max_nits = 1000.0f;

    TranscodeProfile prof{};
    prof.hdr_mode = TranscodeHdrMode::kPq;

    // 模拟捕获传递给 Viewport 的参数
    uint32_t captured_flags = 0;
    float captured_left_nits = 0.0f;
    float captured_right_nits = 0.0f;
    uint32_t captured_w = 0;
    uint32_t captured_h = 0;

    canvas.setViewportPresentCallback([&](void* handle, uint32_t w, uint32_t h, uint32_t pitch, uint32_t flags, float l_nits, float r_nits) {
        captured_w = w;
        captured_h = h;
        captured_flags = flags;
        captured_left_nits = l_nits;
        captured_right_nits = r_nits;
    });

    canvas.presentTranscodePreview(frame, prof);

    // 断言 1：尺寸不再是 960x540，而是真实 3840x2160
    EXPECT_EQ(captured_w, 3840u);
    EXPECT_EQ(captured_h, 2160u);

    // 断言 2：左侧必须同时包含 PQ 与 BT.2020 标志
    EXPECT_TRUE((captured_flags & kPreviewLeftPq) != 0);
    EXPECT_TRUE((captured_flags & kPreviewLeftBt2020) != 0);
    EXPECT_FLOAT_EQ(captured_left_nits, 1000.0f);

    // 断言 3：右侧必须同时包含 PQ 与 BT.2020 标志
    EXPECT_TRUE((captured_flags & kPreviewRightPq) != 0);
    EXPECT_TRUE((captured_flags & kPreviewRightBt2020) != 0);
    EXPECT_FLOAT_EQ(captured_right_nits, 1000.0f);
}

TEST_F(WorkstationCanvasTest, TranscodePreviewSdrInputTrueHdrOutputVerification) {
    WorkstationCanvas canvas;
    TranscodePreviewFrame frame{};
    frame.shared_handle = (void*)0x12345678;
    frame.width = 1920;
    frame.height = 1080;
    frame.pitch = 1920 * 4;
    frame.src_color_trc = AVCOL_TRC_BT709; // SDR
    frame.src_max_cll = 0.0f;
    frame.dst_max_nits = 600.0f; // TrueHDR 提升至 600 nits

    TranscodeProfile prof{};
    prof.enable_true_hdr = true;

    uint32_t captured_flags = 0;
    float captured_left_nits = 0.0f;
    float captured_right_nits = 0.0f;

    canvas.setViewportPresentCallback([&](void* handle, uint32_t w, uint32_t h, uint32_t pitch, uint32_t flags, float l_nits, float r_nits) {
        captured_flags = flags;
        captured_left_nits = l_nits;
        captured_right_nits = r_nits;
    });

    canvas.presentTranscodePreview(frame, prof);

    // 左侧必须为 SDR
    EXPECT_FALSE((captured_flags & kPreviewLeftPq) != 0);
    EXPECT_FALSE((captured_flags & kPreviewLeftHlg) != 0);
    EXPECT_FLOAT_EQ(captured_left_nits, 100.0f);

    // 右侧必须为 TrueHDR PQ BT.2020 且峰值为 600 nits
    EXPECT_TRUE((captured_flags & kPreviewRightPq) != 0);
    EXPECT_TRUE((captured_flags & kPreviewRightBt2020) != 0);
    EXPECT_FLOAT_EQ(captured_right_nits, 600.0f);
}
```

---

# **5\. 本波次验收 Gate**

| 验收项 | 期望指标 | 状态 | 验证方法 |
| :---- | :---- | :---- | :---- |
| **硬编码消除** | `workstation_canvas.cpp` 完全无 `960u` / `540u` 写死常数 | 待通过 | Cppcheck / Code Search 扫描 |
| **动态分辨率传递** | 预览视口完全按借出的 `out_width` / `out_height` 进行采样 | 待通过 | `test_gui_workstation` 断言 `captured_w/h` |
| **HDR10/HLG 左侧标志** | 输入 HDR10/HLG 时，`color_flags` 准确包含 `kPreviewLeftPq/Hlg` | 待通过 | 自动化单元测试断言 |
| **双侧 Nits 精确匹配** | `left_max_nits` / `right_max_nits` 真实映射 HDR/SDR 阈值 | 待通过 | `test_gui_workstation` |
| **宪法红线 1** | `AuditTypes == 34` 或 `35`（新增类型 `+0`） | 强制约束 | 符号表静态分析器 |
| **宪法红线 2** | `AuditImports` GUI `<= 4` / HEADLESS `== 2` | 强制约束 | PE 文件 Import Table 扫描 |
| **宪法红线 3** | `Zero GPU->CPU Readback` | 强制约束 | D3D12 API Hook 零 Readback 检查 |

