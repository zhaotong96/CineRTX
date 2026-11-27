# **20\_rework\_wave4-A\_动态D2D\_DirectWrite组件库与全栈i18n施工单**

## **1\. 元信息与驳回定性**

### **1.1 返工波次与并行架构**

* **波次标识**：Rework Wave 4-A（与 Rework Wave 4-B 并行架构）。  
* **核心使命**：彻底重构原 Wave 4-A 严重缩水实现。完整交付动态加载 `d2d1.dll` \+ `dwrite.dll` \+ `d3d11.dll` 组件库、两阶段 DirectWrite 自适应折行布局、垂直滚动检查器容器、队列卡片化交互以及全栈单源命名占位符 i18n 热切换重排系统。

### **1.2 裁决复盘与驳回定性 (`CINERTX-P13-RULING-013`)**

原 Wave 4-A（第 10 号文档）被协调负责人裁决为 **严重缩水（REJECTED）**。复盘技术缺陷如下：

* **伪实现偷工减料**：前次提交仅编写 10 行 GDI `measureWrappedTextHeight` 哑接口与 `detectSystemLanguage()` 壳函数，完全未接入 Direct2D / DirectWrite 渲染管线。  
* **物理动态库缺失**：未实现 `d2d1.dll` / `dwrite.dll` / `d3d11.dll` 的运行时动态绑定机制。  
* **UI 交互断层**：未实现右侧检查器垂直滚动容器与富信息队列卡片列表。  
* **架构违规与硬编码**：未清理 `workstation_canvas.cpp:206-241` 的第二套私藏 `chromeText()` 字典，界面大量散落硬编码英文拼接。  
* **i18n 引擎缺失**：未实现命名占位符动态插值引擎与 WebUI 字典自动化下发机制。

### **1.3 宪法级硬红线约束**

| 规约项目 | 自动化审计脚本 | 宪法约束指标 | 违规惩罚与裁决标准 |
| :---- | :---- | :---- | :---- |
| **PE 静态 IAT 净空铁律** | `AuditImports.cmake` (`[DEC-018]`) | GUI 物理静态导入 DLL **严格 \$\\le 4\$ 个**(仅允许 `kernel32.dll`, `user32.dll`, `gdi32.dll`, `bcrypt.dll`) | **绝对禁止**静态链入 `d2d1.dll`、`dwrite.dll`、`d3d11.dll`、`d3d12.dll`、`dxgi.dll`、`dwmapi.dll`、`comctl32.dll`、`uxtheme.dll`、`ole32.dll`、`shell32.dll`。所有 API **必须**通过 `sys_dynload` 运行时 `LoadLibraryW` \+ `GetProcAddress` 动态绑定。 |
| **具名类型配额铁律** | `AuditTypes.cmake` (`[DEC-009]`) | 全仓 `include/` \+ `src/` 具名 `class`/`struct` 上限 **35 个**(当前基线：`34/35`) | **仅允许使用唯一剩余 1 个名额**定义 POD 结构体 `UiWidgetNode`（达 `35/35` 极限）。其余逻辑全部采用定长数组与无状态自由函数管线，**严禁新增任何第二个 `class`/`struct`**。 |

---

## **2\. Task 1: 动态加载 D2D1/DirectWrite/D3D11on12 互操作组件库与两阶段自适应布局**

### **2.1 精确代码坐标**

* `src/gui/rtx_gui.cpp:186-223`  
* `src/gui/workstation_canvas.cpp:891-964`

### **2.2 TDD 执行步骤**

#### **Step 1.1: `sys_dynload` 零静态导入渲染桥**

在 `src/gui/` 模块内部建立运行时函数指针导出表，通过 `LoadLibraryW` 动态绑定核心入口：

* **`d2d1.dll`**: 解析 `D2D1CreateFactory`  
* **`dwrite.dll`**: 解析 `DWriteCreateFactory`  
* **`d3d11.dll`**: 解析 `D3D11On12CreateDevice`

```
  +-------------------------------------------------------------------+
  |                       DirectX/D2D 动态加载渲染桥                  |
  +-------------------------------------------------------------------+
  | [d3d11.dll]  --> D3D11On12CreateDevice                           |
  | [d2d1.dll]   --> D2D1CreateFactory                                |
  | [dwrite.dll] --> DWriteCreateFactory                              |
  +-------------------------------------------------------------------+
                                   |
                                   v
  +-------------------------------------------------------------------+
  |               DXGI Surface / Direct3D 12 共享资源               |
  +-------------------------------------------------------------------+
  |  +-----------------------+     +-------------------------------+  |
  |  |   D2D1 硬件绘制管线    |     |    DirectWrite 文本测量管线   |  |
  |  | (圆角卡片/平滑滑块/Toggle)|     |  (自适应折行/Subpixel Alignment)|  |
  |  +-----------------------+     +-------------------------------+  |
  +-------------------------------------------------------------------+
                                   |
                                   v
  +-------------------------------------------------------------------+
  |                     原生 HWND / IME 宿主窗口                      |
  |  (浮动 Win32 EDIT 控件，处理 zh-CN / ja-JP IME 文本输入)          |
  +-------------------------------------------------------------------+
```

绘制管线与现有的 `PreviewViewport` D3D12 设备共享 DXGI Surface，提供暗色圆角卡片、矢量平滑滑块、现代 Toggle Switch 与折叠分组绘制。保持按需浮动的原生 Win32 EDIT 控件作为 zh-CN / ja-JP IME 输入法宿主。

#### **Step 1.2: 两阶段 DirectWrite 自适应文本折行测量与垂直滚动容器**

布局管线拆分为 **Measure** 与 **Arrange & Scroll** 两个阶段：

```
  [输入: 容器可用 DIP 宽度 & 字符串]
                  |
                  v
       +---------------------+
       |    Measure 阶段     |
       +---------------------+
                  |
                  +--->  1. 检索 / 创建 IDWriteTextLayout 实例 (宽 + 文本 Hash 缓存)
                  +--->  2. 配置 DWRITE_WORD_WRAPPING_WRAP
                  +--->  3. 配置 DWRITE_WORD_WRAPPING_EMERGENCY_BREAK
                  +--->  4. 执行 GetLineMetrics() 累加精确行高
                  |
                  v
       +---------------------+
       |   Arrange & Scroll  |
       +---------------------+
                  |
                  +--->  1. 动态撑开 Row / Group / Card 高度 (废除 u(40) 截断上限)
                  +--->  2. 接入垂直滚动容器 (Clip Viewport & Offset)
                  +--->  3. 计算 Scrollbar 拖拽块尺寸与 Clamp 边界
                  |
                  v
  [输出: 0 截断 / 0 重叠 UI 布局]
```

##### **Measure 阶段**

1. 传入父容器可用 DIP 宽度；  
2. Label 组件调用 `IDWriteFactory::CreateTextLayout`（根据文本内容与宽度 Hash 值缓存 `IDWriteTextLayout` 句柄，避免逐帧重新创建）；  
3. 开启 `DWRITE_WORD_WRAPPING_WRAP` 与 `DWRITE_WORD_WRAPPING_EMERGENCY_BREAK`（保护超长无空格路径不穿透）；  
4. 通过 `IDWriteTextLayout::GetLineMetrics` 精确累加折行后的总高度，**动态撑开所在行、分组与队列卡片的物理高度**（彻底废除旧版写死的 `dynRowH <= u(40)` 截断硬编码）。

##### **Arrange & Scroll 阶段**

右侧检查器与左侧队列面板接入独立垂直滚动容器：

1. 维护视口偏移量 `viewportOffsetY` 与总内容高度 `contentHeight`；  
2. 支持鼠标滚轮平滑插值、键盘 Tab/Direction 导航以及自绘暗色滚动条；  
3. 在 `800x600` 紧凑窗口与 `200%` DPI 下实现全界面零文字截断、零控件重叠。

#### **Step 1.3: 队列富信息卡片列表升级**

淘汰原 Win32 `LISTBOX` 哑控件，升级为数据驱动的 D2D 自绘队列卡片列表。单张卡片包含：

1. **任务序号徽标**与**输入片段计数器**；  
2. **模式标签**（`独立导出` vs `合并输出`）；  
3. **动态编码参数摘要**（如 `NVENC H.264 · 4K · 60fps · 25Mbps`）；  
4. **独立渲染进度条**（支持 Batch/Item 级百分比与 FPS 显示）；  
5. **内联交互热区**：`[+ 片段]`、`[- 片段]`、`[▲ 上移]`、`[▼ 下移]`、`[✕ 删除]`。

---

## **3\. Task 2: 全栈单源命名占位符 i18n 引擎与语言热切换自动重排**

### **3.1 精确代码坐标**

* `include/rtx/i18n.h`  
* `src/core/i18n.cpp`  
* `src/gui/workstation_canvas.cpp:206-241, 477, 519-531, 591-615`  
* `src/gui/rtx_gui.cpp:814-817`  
* `webui/app.js:21-330`

### **3.2 唯一新增具名类型定义（配额 35/35）**

```
// include/rtx/ui_types.h
// 占用全仓第 35 个（最后一个）具名结构体名额
struct UiWidgetNode {
    uint32_t id;
    uint32_t parentId;
    float x;
    float y;
    float width;
    float height;
    float measuredHeight;
    uint16_t i18nKeyIndex;
    uint8_t widgetType; // 0: Panel, 1: Label, 2: Button, 3: Slider, 4: Card
    uint8_t flags;      // 0x01: Visible, 0x02: Focused, 0x04: WordWrap
};
```

### **3.3 执行步骤**

#### **Step 2.1: 废除所有私藏词典与硬编码拼接**

1. **彻底清理废弃代码**：移除 `workstation_canvas.cpp:206-241` 的 `chromeText()` 映射表及 `OptCodecH264` 残留逻辑；  
2. **文本收敛**：将 `rtx_gui.cpp` 与 `workstation_canvas.cpp` 中散落的英文状态字符串全数抽取至 `include/rtx/i18n.h`。

##### **字符串映射表定义（编译期 `.rdata` 段）**

p  
enum class I18nKey : uint16\_t {  
STATUS\_READY \= 0,  
STATUS\_PROCESSING,  
QUEUE\_CARD\_TITLE,  
LABEL\_CODEC\_PARAM,  
BTN\_ADD\_SEGMENT,  
BTN\_REMOVE\_SEGMENT,  
// ...  
COUNT  
};

static const char\* const g\_I18nStrings\[(size\_t)I18nKey::COUNT\]\[3\] \= {  
// zh-CN                          // en-US                          // ja-JP  
{"就绪",                           "Ready",                          "准备完了"},  
{"处理中: {done}/{total} · {fps} fps", "Processing: {done}/{total} · {fps} fps", "处理中: {done}/{total} · {fps} fps"},  
{"任务 \#{id}: {name}",             "Task \#{id}: {name}",             "タスク \#{id}: {name}"},  
{"合并 {count} 个片段 \-\> {file}",  "Merge {count} segments \-\> {file}","{count} 个のセグメントを結合 \-\> {file}"},  
{"添加片段",                       "Add Segment",                    "セグメント追加"},  
{"移除片段",                       "Remove Segment",                 "セグメント削除"}  
};

````

#### Step 2.2: 零堆分配命名占位符插值引擎
在 `src/core/i18n.cpp` 中实现静态无堆分配（Zero-Heap Allocation）插值函数，使用栈内存缓冲区完成模板解析：

```cpp
struct I18nArg {
    const char* key;
    const char* val;
};

// 零堆分配插值计算自由函数
size_t FormatI18nString(
    I18nKey key, 
    uint8_t langIndex, 
    const I18nArg* args, 
    size_t argCount, 
    char* outBuf, 
    size_t bufCapacity
);
````

#### **替换规则示例**

* 模板：`"合并 {count} 个片段 -> {file}"`  
* 参数：`count="4"`, `file="out.mp4"`  
* 输出：`"合并 4 个片段 -> out.mp4"`

### **Step 2.3: 语言热切换全树重测算与 WebUI 字典统一下发**

```
                 [用户触发语言热切换 (zh-CN <-> en-US <-> ja-JP)]
                                        |
                                        v
                 +----------------------------------------------+
                 |       更新系统 IDWriteTextFormat             |
                 |     (切换 localeName: "zh-CN"/"en-US"/"ja-JP")|
                 +----------------------------------------------+
                                        |
                                        v
                 +----------------------------------------------+
                 |       清空 Layout & LineMetrics 缓存         |
                 |      (InvalidateAllTextLayoutCaches)         |
                 +----------------------------------------------+
                                        |
                                        v
                 +----------------------------------------------+
                 |        触发 GUI 全局 InvalidateLayout()       |
                 +----------------------------------------------+
                                        |
                                        v
                 +----------------------------------------------+
                 |       执行 Task 1 阶段两阶段自适应重排       |
                 |   (Measure 重新计算高度 -> Arrange 重新布局)  |
                 +----------------------------------------------+
                                        |
                                        v
                 +----------------------------------------------+
                 |       WebUI 接口 /api/schema 增量推送         |
                 |    (自动下发 C++ 单源 JSON 词典，替换前端硬编码) |
                 +----------------------------------------------+
```

1. **GUI 语言热切换**：  
   * 当用户切换语言（`zh-CN` \$\\leftrightarrow\$ `en-US` \$\\leftrightarrow\$ `ja-JP`）时，更新 `IDWriteTextFormat` 的 `localeName`，确保系统字体回退与基线对齐正确；  
   * 清空 `IDWriteTextLayout` 缓存并调用 `InvalidateLayout()`；  
   * 触发两阶段高度测算与布局管线，根据新语种文本长度自动重排所有卡片与检查器项。  
2. **WebUI 字典下发**：  
   * 后端通过 `/api/schema` 路由直接输出由 `g_I18nStrings` 自动生成的全量 JSON 字典；  
   * 彻底删除 `webui/app.js:21-330` 冗余的前端硬编码字典，保持 WebUI 与 C++ 原生 UI 的单源同步。

---

# **4\. 法定验收门禁与真机实测规约**

为确保 Wave 4-A 重构完全符合质量红线，必须严格通过以下自动化断言与真机实测：

## **4.1 静态检查与架构约束断言**

```
## 1. 验证具名类型数量 (必须 <= 35)
cmake -P cmake/AuditTypes.cmake

## 2. 验证 PE 静态 IAT 导出表 (GUI 静态物理导入库 strictly <= 4)
cmake -P cmake/AuditImports.cmake
```

#### **导入表白名单校验标准**

编译产物 `rtx_workstation.exe` 仅允许存在以下 4 个物理 DLL 依赖：

1. `kernel32.dll`  
2. `user32.dll`  
3. `gdi32.dll`  
4. `bcrypt.dll`

*注：若扫描到 `d2d1.dll`、`dwrite.dll`、`d3d11.dll` 或任何 COM 动态库静态链入，编译构建流水线直接中断报红。*

## **4.2 运行时 UI / i18n 真机实测矩阵**

```
  +-----------------------------------------------------------------------------------------+
  |                                运行时 UI/i18n 真机实测矩阵                               |
  +--------------------+---------------------+--------------------+-------------------------+
  | 测试场景           | 缩放与分辨率         | 语言切换           | 预期合格指标            |
  +--------------------+---------------------+--------------------+-------------------------+
  | 1. 极限视口测试    | 800x600 窗口        | zh-CN -> en-US     | 检查器显示垂直滚动条，  |
  |                    | (100% DPI)          |                    | 文本完整折行撑开卡片    |
  +--------------------+---------------------+--------------------+-------------------------+
  | 2. 高分屏缩放      | 3840x2160           | en-US -> ja-JP     | 亚像素渲染清晰，文字与  |
  |                    | (200% DPI)          |                    | 图标 0 重叠，卡片重排正常|
  +--------------------+---------------------+--------------------+-------------------------+
  | 3. 动态占位符插值  | 任意分辨率          | 三语种快速轮换     | 占位符正确替换，模板    |
  |                    |                     |                    | 渲染无内存泄漏/截断      |
  +--------------------+---------------------+--------------------+-------------------------+
  | 4. 队列卡片交互    | 800x600 至 4K 缩放   | 全语种             | 卡片热区响应精准，      |
  |                    |                     |                    | 增删/上下移拖拽流畅      |
  +--------------------+---------------------+--------------------+-------------------------+
```

1. **极限视口测试**：在 `800x600` 窗口尺寸、`100%` DPI 下切换至 `en-US`，检查器与队列面板出现自绘滚动条，折行文本自动撑开组件高度，无控件相互重叠；  
2. **高分屏缩放**：在 `200%` DPI 高分屏下切换至 `ja-JP`，DirectWrite 文本基线对齐正确，无模糊与文字裁切现象；  
3. **动态占位符插值**：快速连续切换三语种，验证 `"{done}/{total} · {fps} fps"` 与 `"合并 {count} 个片段 -> {file}"` 等模版字符串输出无内存泄漏、无字符串截断；  
4. **队列卡片交互**：对富信息队列卡片进行「添加片段」、「删除」、「上移」、「下移」操作，布局即时更新并平滑滚动。

