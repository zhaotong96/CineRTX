# **Wave 4-A 合规无 DLL 污染 DirectWrite 组件库与全栈 i18n 施工单**

本施工单定义 **Wave 4 \- Stage A** 阶段的具体实施路径，重点完成现代化 Win32 无污染 UI 重构、亚像素 DirectWrite 动态渲染管线、自适应两阶段折行布局引擎以及全栈单源国际化 (i18n) 体系。工程实施必须严格遵守项目宪法红线。

---

## **1\. 元信息与不可逾越的宪法红线**

### **1.1 波次定位**

* **施工阶段**：`Wave 4 - Stage A`（最终交互整合波次）  
* **依赖关系**：前置依赖 `Wave 1-A`、`Wave 3-A`、`Wave 3-B`；与 `Wave 4-B` 并行构建。  
* **主要目标**：构建零 DLL 污染的高性能 UI 渲染管线，消除界面字体裁切与硬编码英文，提供全栈动态多语言热切换支持。

### **1.2 宪法红线与静态约束检查**

| 宪法条款 | 约束指标 / 审计脚本 | 物理限制与强制标准 | 违宪后果 / 自动化拦截机制 |
| :---- | :---- | :---- | :---- |
| **PE 静态 IAT 净空铁律** | `cmake/AuditImports.cmake[DEC-018]` | 静态导入物理 DLL **严格 ≤ 4 个**（仅限 `kernel32.dll`, `user32.dll`, `gdi32.dll`, `bcrypt.dll`）。**严禁静态链入** `d2d1.dll`, `dwrite.dll`, `d3d11.dll`, `d3d12.dll`, `dxgi.dll`, `dwmapi.dll`, `comctl32.dll`, `uxtheme.dll`, `ole32.dll`, `shell32.dll`。 | CI 物理阻断：`AuditImports` 脚本直接报 Error 并中断编译。必须采用 `sys_dynload` 进行 `LoadLibraryW` \+ `GetProcAddress` 运行时绑定。 |
| **具名类型配额铁律** | `cmake/AuditTypes.cmake[DEC-009]` | 全仓 `include/` \+ `src/` 具名 `struct`/`class` 上限 **35 个**（当前配额 `34/35`，仅存 1 个名额）。**绝对禁止**为控件新建类（严禁 `class Button`, `class Slider`）。 | CI 物理阻断：`AuditTypes` 解析 AST 超标即失败。本 Wave 必须采用**数据驱动 POD 描述符 \+ 无状态自由函数绘制管线**。 |
| **Win32 组合框防裁切铁律** | 布局防裁切规范`[DEC-015]` | 原生宿主控件（编辑框/下拉列表）必须遵循显式上下文传递，严格保护包络高度，阻止高 DPI 及多语言下边界截断。 | 交互测试不通过：出现文字裁切或控件边缘挤压即判定构建不合格。 |

---

## **2\. Task 1：Windows 原生复用 vs 自研 D2D/DirectWrite 组件边界落地**

### **2.1 事实代码定位与存在缺陷**

1. `src/gui/rtx_gui.cpp:186-218`：写死了固定尺寸 `lblW = u(120), rowH = u(26), gap = u(2)` 的扁平循环，在多语言扩展时导致标签右截断。  
2. `src/gui/workstation_canvas.cpp:891-964`：标准原生 `STATIC`/`EDIT`/`BUTTON`/`COMBOBOX`/`LISTBOX` 混用拼装，缺少 `WORDBREAK` 动态高度测算，且无垂直滚动容器。

### **2.2 详细执行步骤与技术实现**

```
               [ Direct2D / DirectWrite 动态加载渲染管线 ]
                                  │
    ┌─────────────────────────────┼─────────────────────────────┐
    ▼                             ▼                             ▼
【D2D/DWrite 自由函数】     【原生 EDIT 宿主】         【两阶段弹性布局引擎】
 ├─ 圆角卡片 / 矢量滑块      ├─ 透明 Overlay IME 输入     ├─ Phase 1: Measure (DWrite)
 ├─ 现代 Toggle Switch      └─ 暗色模式 Sync            │   └─ TextLayout 高度测算
 └─ 亚像素文本与热区绘制                                └─ Phase 2: Arrange & Scroll
                                                            └─ Dynamic Inspector Viewport
```

#### **Step 1.1：`sys_dynload` 动态加载 `d2d1.dll` \+ `dwrite.dll` \+ `d3d11.dll` 互操作桥**

在 `src/gui/` 内部实现 `dwrite_d2d_bridge.cpp`，彻底摒弃导入库链接：

* 使用 `sys_dynload` 动态解析 `D2D1CreateFactory`、`DWriteCreateFactory` 以及 `D3D11On12CreateDevice`。  
* 与现有 `PreviewViewport` 的 D3D12 设备共享 DXGI 表面，绘制硬件加速圆角卡片、矢量 Toggle Switch 及亚像素 DirectWrite 文本。  
* 保持系统对话框（动态加载 `ole32`/`shell32` 调取 `IFileOpenDialog`）与暗色标题栏（动态 `dwmapi` 传递 `DWMWA_USE_IMMERSIVE_DARK_MODE`）。  
* 保留按需浮动的原生 `EDIT` 控件作为 IME 中/日文输入法宿主，获得无缝原生输入体验。

#### **Step 1.2：两阶段自适应文本折行测量与弹性滚动布局引擎**

利用唯一剩余的 1 个类型名额定义紧凑型 `UiWidgetNode` POD 结构体，配合无状态自由函数：

```
// 唯一剩余具名类型名额（配额 35/35）
struct UiWidgetNode {
    uint32_t id;
    uint32_t type; // 0:Label, 1:Button, 2:Slider, 3:Toggle, 4:Card
    float x, y, width, height;
    float measured_height;
    uint32_t text_id; // 关联 i18n 词条 ID
    uint32_t flags;
};
```

1. **Measure 阶段**：  
   * 传入宿主可用 DIP 宽度。针对长路径与多语言文本，调用 `IDWriteFactory::CreateTextLayout`。  
   * 采用文本内容与宽度哈希（`hash64`）建立 `IDWriteTextLayout` 缓存池，避免每帧频繁重建。  
   * 开启 `DWRITE_WORD_WRAPPING_WRAP` 与 `DWRITE_WORD_WRAPPING_EMERGENCY_BREAK`（适用于无空格超长路径）。  
   * 调用 `GetLineMetrics` 计算折行后准确行高，动态展开所在行、卡片与分组的 `measured_height`。  
2. **Arrange & Scroll 阶段**：  
   * 右侧 Inspector 与 任务队列接入独立垂直滚动容器，计算 `ContentHeight` 与 `ViewportHeight`。  
   * 支持鼠标滚轮平滑插值、键盘 Tab/Direction 导航与自定义暗色滚动条绘制，解决小分辨率窗口（如 800x600）下的溢出问题。

#### **Step 1.3：队列卡片化交互升级**

将 `LISTBOX` 升级为数据驱动的富信息队列卡片列表（Card List）：

```
┌─────────────────────────────────────────────────────────────────────────┐
│ [01] 🎬 Segment_001.mp4                 [独立导出] [Preset: H.265 4K]   │
│ 片段数: 12  │  进度: [████████████████████░░░░] 68%                      │
│ 🛠️  [添加文件]   [删减片段]   [▲ 移上]   [▼ 移下]   [✖ 移除]           │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## **3\. Task 2：全栈单源国际化 (i18n) 与运行时热切换重排引擎**

### **3.1 事实代码定位与重构目标**

1. **清理代码垃圾**：彻底废除 `src/gui/workstation_canvas.cpp:206-241` 的 `chromeText()` 第二套私藏 27 词条表，清除废弃的 `OptCodecH264` 残留字符串。  
2. **清理硬编码拼接**：清理 `rtx_gui.cpp:814-817` 与 `workstation_canvas.cpp:477, 519-531, 591-615` 中的硬编码英文字符串拼接。  
3. **消除 WebUI 孤岛**：废除 `webui/app.js:21-330` 中的硬编码 WebUI 字典，收敛至 C++ 单源表。

### **3.2 详细执行步骤与技术实现**

```
                             [ 单源 i18n `.rdata` 数据表 ]
                                           │
                    ┌──────────────────────┴──────────────────────┐
                    ▼                                             ▼
       【C++ Win32 纯桌面端】                             【WebUI 浏览器前端】
        ├─ 编译期 ID 枚举: I18N_KEY_...                    └─ GET /api/schema 接口
        ├─ 零堆分配占位符插值引擎                              └─ 全量 JSON 动态下发
        └─ TextFormat 重新绑定 & Layout 缓存刷新
```

#### **Step 2.1：统一 `.rdata` 编译期词条表**

所有词条统一在 `include/rtx/i18n.h` 与 `src/core/i18n.cpp` 中维护。词条映射表放置于静态数据区（`.rdata`），首批全面覆盖 `zh-CN`（简体中文）、`en-US`（美式英文）、`ja-JP`（日语）。

```
enum class I18nKey : uint16_t {
    STATUS_READY,
    STATUS_PROCESSING,
    LABEL_INPUT_SEGMENTS,
    TASK_MODE_MERGE,
    TASK_MODE_SPLIT,
    ERR_FILE_NOT_FOUND,
    // ...
    COUNT
};
```

#### **Step 2.2：零堆分配 / 小缓冲栈格式化命名占位符插值引擎**

实现自由函数 `i18n_format_to()`，直接使用栈上 `std::array<wchar_t, 256>` 进行格式化解析，避免运行时堆内存分配：

```
// 示例：解析 "{done}/{total} · {fps} fps"
void i18n_format_to(wchar_t* out_buf, size_t capacity, I18nKey key, const I18nArg* args, size_t arg_count);
```

此举可彻底解决不同语种间主谓宾语序差异带来的拼接生硬与翻译错位问题。

#### **Step 2.3：运行时热切换全树重测算联动与 WebUI 动态下发**

1. **Win32 端响应热切换**：  
   * 变更当前 `Locale` 标识。  
   * 重新创建/更新 `IDWriteTextFormat` 的 `localeName` 参数（确保正确触发系统字体回退链，如中文字体回退至 *Microsoft YaHei UI*，日文字体回退至 *Yu Gothic UI*）。  
   * 刷新并清空 `IDWriteTextLayout` 缓存池，触发全局 `InvalidateLayout()`。  
   * 布局引擎执行 Step 1.2 的两阶段测算，自动重排所有卡片与控件尺寸。  
2. **WebUI 字典同步**：  
   * 后端 `/api/schema` 路由直接将当前语言全量词典序列化为 JSON 下发给 WebUI。  
   * 前端 JavaScript 接收 Schema 后实时更新 DOM 节点，不再自建或硬编码字典。

---

## **4\. 验收门禁与验证指标**

为保证施工质量，必须通过以下两条硬性门禁测试：

```
                              [ 自动化与手动验收门禁 ]
                                          │
                   ┌──────────────────────┴──────────────────────┐
                   ▼                                             ▼
         【门禁 A: 静态物理审计】                       【门禁 B: 交互与渲染验证】
          ├─ AuditImports <= 4                         ├─ 4 种 DPI 缩放比例测试
          └─ AuditTypes <= 35                          ├─ 800x600 紧凑布局滚动测试
                                                       └─ 动态语言热切换零截断验证
```

### **4.1 门禁 A：静态物理审计（自动化 CI）**

* 运行 `cmake/AuditImports.cmake`：  
  * GUI 可执行文件静态导入物理 DLL **严格 ≤ 4**（仅允许 `kernel32.dll`, `user32.dll`, `gdi32.dll`, `bcrypt.dll`）。  
  * 包含 `d2d1`, `dwrite`, `d3d11`, `d3d12`, `dxgi`, `dwmapi`, `comctl32`, `uxtheme`, `ole32`, `shell32` 在内的黑名单 DLL 静态导入数必须为 **0**。  
* 运行 `cmake/AuditTypes.cmake`：  
  * 全仓具名类型数 **严格 ≤ 35/35**（包含本次新增的唯一 POD `UiWidgetNode`）。

### **4.2 门禁 B：交互与渲染验证（手动 / 视觉 Regression）**

* **DPI 自适应测试**：在 `100%` / `125%` / `150%` / `200%` 系统 DPI 下分别启动程序，界面元素无失真，DirectWrite 文本保持亚像素清晰。  
* **紧凑窗口与滚动测试**：将窗口调整为 `800x600` 极限尺寸，展开全部高级参数与卡片队列，控件无重叠溢出，垂直滚动条交互响应流畅。  
* **热切换与 UTF-8 编码测试**：在运行状态下动态切换 `zh-CN <-> en-US <-> ja-JP`，全部组件无文字截断，多行文本自动重新折行并推开行高，包含中/日文特殊字符的文件路径全程以 UTF-8 呈现且无乱码。

