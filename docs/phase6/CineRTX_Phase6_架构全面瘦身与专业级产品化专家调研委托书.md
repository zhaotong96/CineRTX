# CineRTX 阶段六：架构全面瘦身、现代 C++26 纯化与专业级产品化领域专家调研委托书

> **文档标识**：`DOC-COMMISSION-PHASE6-REFACTOR-001`  
> **归档位置**：`docs/phase6/CineRTX_Phase6_架构全面瘦身与专业级产品化专家调研委托书.md`  
> **签发日期**：2026-11-09  
> **委托方**：CineRTX 系统架构委员会暨项目总监  
> **受托方**：CineRTX 领域专家评审委员会（高性能计算、现代 C++、编译器优化及媒体工程专家组）  
> **当前版本**：v1.0 (正式下发)

---

## 1. 委托背景与核心指导思想 (Executive Mandate)

CineRTX 在经历 Phase 1 至 Phase 5 的高强度攻坚后，已顺利攻克全硬件链路打通、全栈静态自包含单 exe、Modern C++26 基础重构以及全媒体工业级可用性（零拷贝音频流透传、9:16 竖屏自适应 4K、D3D12 SDR Blit 色彩保真）。当前全仓 93 项单元测试全绿，免安装绿色分发包已实现自动化构建闭环。

然而，在对全仓进行深度全景静态审计后，系统暴露出若干违背软件工程核心准则与现代 C++ 哲学的设计病灶：
1. **代码体量过度膨胀**：C/C++ 代码行数已达 **49,042 行**，充斥着大量过度分层的类包装、为“未来预期”保留的冗余接口以及历史防御性兼容垫片；
2. **两段式初始化残余**：部分核心中枢依然残留 `initialize()` / `shutdown()` 等反 RAII 范式；
3. **C 风格 Slop 函数横行**：如 [`src/core/types.cpp`](../../src/core/types.cpp) 等底层模块充斥裸指针出参、手写逐字段深拷贝及手动内存覆盖等不现代写法；
4. **UI 极简至功能缺失**：前端仅能拖拽执行硬编码参数，缺乏工业级转码非编软件必需的参数调节矩阵（下拉框、输入框、复选框）；
5. **过早优化的臆想倾向**：存在未经 Profiler 验证的臆想优化担忧，对 C++26 现代特性的向量化能力缺乏实证依据；
6. **历史文档与阶段定义出现演化漂移**。

基于此，架构委员会决定正式立项 **Phase 6（架构全面瘦身、现代 C++26 纯化与专业级产品化）**，特向领域专家组下达本调研委托书，要求对十三大核心议题展开穷尽推导，出具权威技术裁决与一体化工程施工方案。

---

## 2. 十三大关键工程议题调研清单 (Thirteen Commissioned Topics)

### 议题一：UI 架构演进与轻量级原生组件体系选型 (Topic 1: UI Component Architecture)
* **现状分析**：
  * 当前 [`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp) 采用纯 GDI Owner-Draw 自绘模式，无任何子窗口控件，交互形态近乎空白；
  * 早期专家意见为保证单 exe 极轻量（Sub-10MB）排除了 Qt、Electron、WPF、Flutter 等重型外部框架。
* **专家调研要点**：
  1. 评估轻量级 UI 组件范式：是基于 Windows 原生标准子控件（通过 Win32 API 创建 `BUTTON`、`COMBOBOX`、`EDIT`、`TRACKBAR`、`CHECKBOX` 并注入现代暗黑主题样式），还是引入零外部链接依赖、单头文件现代 C++ 立即模式微型 GUI（如轻量级自包含 Immediate Mode GUI 桥接 DirectComposition）？
  2. 确立“功能完整、原生轻量、代码行数最小化”的组件选型铁律。

### 议题二：工业级参数调节矩阵规格与交互解绑 (Topic 2: Professional Tuning Controls)
* **现状分析**：
  * 目前 UI 缺乏参数调节能力，编码器参数、画幅策略与画质算法被写死，不符合专业转码/非编生产力工具定位。
* **专家调研要点**：
  1. 规划完整的扩展参数面板（Inspector / 折叠抽屉），涵盖：
     * **下拉选择框 (ComboBox)**：编码格式（AV1 / HEVC / H.264）、速率控制模式（CBR / VBR / Const-QP）、画幅适配模式（Auto Direct / Pillarbox 黑边 / 自定义）；
     * **数值输入/滑块框 (Edit/Trackbar)**：目标码率（Mbps）、VSR 质量等级（1..4）、TrueHDR 五维色调映射曲线（对比度、饱和度、中灰、峰值亮度 nit、平滑系数）；
     * **复选框 (CheckBox)**：TrueHDR 逆色调映射开关、音频流处理策略（Stream Copy 透传 / 剥离音轨）。
  2. 设计数据双向绑定机制，确保控件操作无缝驱动底层 `HwTranscodeOptions`。

### 议题三：极简实用主义界面布局设计规约 (Topic 3: Pragmatic Minimalist UI Layout)
* **现状分析**：
  * 过度华丽的动效与复杂的图形层叠违背系统初衷，但当前 UI 过于简陋原始。
* **专家调研要点**：
  1. 制定“绝不增加复杂装饰性样式，专注高能效布局”的 Win32 布局规约；
  2. 规定 Per-Monitor V2 高 DPI 自动缩放与网格/流式排列算法，保证高 DPI 切换时不模糊、不破损。

### 议题四：默认媒体格式全面升级为 AV1 10-bit (Topic 4: AV1 10-bit Default Standard)
* **现状分析**：
  * 当前产品默认硬编码或推导为 HEVC Main10；面向 RTX 40/50 系列 Blackwell/Ada 架构，AV1 具有更高的码率收益与开源免版税优势。
* **专家调研要点**：
  1. 制定将全局默认编码格式由 HEVC 切换为 AV1 10-bit（Main Profile）的落地细节；
  2. 设计硬件探测自适应降级防线：当运行于不支持 AV1 NVENC 的 GPU 时，自动透明降级为 HEVC Main10 并输出诊断报告。

### 议题五：构建输出与插件目录临时文件清理规范 (Topic 5: Build Artifact Hygiene)
* **现状分析**：
  * `build/external/plugins/nvidia_rtx` 目录中混存着动态库（`.dll`）以及增量链接缓存（`.ilk`）和调试符号（`.pdb`）。
* **专家调研要点**：
  1. 明确构建资产分层：CMake/Ninja 编译期中间产物、开发调试符号与生产分发包镜像的目录边界；
  2. 在构建脚本与清理规则中明确 `.ilk` / `.pdb` 的隔离策略，防止调试废料混入分发包。

### 议题六：全仓既有技术文档一致性审计与前后矛盾勘误 (Topic 6: Documentation Consistency)
* **现状分析**：
  * 随着技术方案的快速迭代，早期阶段的技术设想（如 `docs/TODO/phase5_roadmap_and_spec.md` 将 DAG 调度器与 Blackwell 硬件特化标为 Phase 5，而实际实施的 Phase 5 交付的是全媒体可用性）与最终实现事实存在命名漂移与前后表述脱节。
* **专家调研要点**：
  1. 对 `docs/` 下累计 50 余篇文档进行交叉审计；
  2. 建立正本清源的《全案文档前后矛盾与状态对齐勘误表》，明确历史演化脉络。

### 议题七：软件 GUI 运行时国际化 (i18n) 动态响应体系 (Topic 7: GUI Runtime Localization)
* **现状分析**：
  * 目前 GUI 中存在部分硬编码宽字符文本，国际化切换未与 UI 控件生命周期完全联动。
* **专家调研要点**：
  1. 统一接入基于强类型枚举与 `constexpr` 字典的 `rtx::trW` 国际化中枢；
  2. 实现语言切换事件（`ToggleLanguageEvent`）广播与界面控件文本即时动态刷新。

### 议题八：根目录 README 文档标准化中英双语架构 (Topic 8: Global README Bilingual Structure)
* **现状分析**：
  * 根目录 [`README.md`](../../README.md) 目前采用单篇中英文穿插混杂编排，且仍保留了“默认 4K HEVC”的陈旧描述。
* **专家调研要点**：
  1. 按照顶级开源项目规范重构 README 架构：建立标准英文主文档 [`README.md`](../../README.md) 与中文全镜像文档 `README_zh.md`，顶部互设语言导航锚点；
  2. 全面更新技术指标矩阵与功能状态表（体现音视频零拷贝透传、9:16 竖屏自适应及 AV1 默认）。

### 议题九：C++26 指定初始化器（Designated Initializers）全仓强制规范 (Topic 9: Universal Designated Initializers)
* **现状分析**：
  * 部分结构体实例化仍采用传统位置传参或繁琐的显式构造函数，容易引发参数位置倒置或隐式类型收缩转换。
* **专家调研要点**：
  1. 确立全仓所有结构体实例化的现代 C++26 铁律——一律采用指定初始化器语法：
     ```cpp
     MyStruct stru{
         .id = userId,
         .name = userName,
     };
     ```
  2. 全面拔除结构体中无必要的显式样板构造函数，回归聚合体（Aggregate）本质。

### 议题十：彻底拔除两段式初始化，践行 100% 绝对 RAII (Topic 10: Elimination of Two-Phase Init)
* **现状分析**：
  * [`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp) 与 [`include/rtx/vram_pool.h`](../../include/rtx/vram_pool.h) 中依然存在 `initialize()`、`initializeDevice()` 与 `shutdown()` 等典型的两段式初始化与手动销毁。
* **专家调研要点**：
  1. 拔除全仓所有类的 `initialize` 和 `shutdown` / `destroy` 方法；
  2. 全面推行基于 `std::expected<T, Status>` 的静态工厂模式（如 `Class::create(...)`），构造即可用，生命周期终结时通过析构函数自动释放，任何特例必须提交物理不可抗力豁免报告。

### 议题十一：深挖并重构 C 风格 Slop 函数与非现代残留 (Topic 11: Core Slop Refactoring)
* **典型病灶案例**：以 [`src/core/types.cpp`](../../src/core/types.cpp) 为首批解剖样本：
  * 裸指针作为多个出参：`size_t frameByteSize(..., int* pitches, int* chroma_width, int* chroma_height)`；
  * 手工逐字段复制深拷贝：`void Frame::copyMetadataFrom(const Frame& other)`；
  * 手写位运算宏式对齐函数与裸 `const char*` / C 风格数组传递；
  * `FrameBuffer::allocate` 手动两阶段内存覆盖。
* **专家调研要点**：
  1. 出具 [`src/core/types.cpp`](../../src/core/types.cpp) 及类似底层模块的 Modern C++26 重构范式；
  2. 采用结构体值返回、编译器合成的默认拷贝/移动语义、`std::string_view` / `std::span` 及标准库对齐机制全面替换 Slop 代码。

### 议题十二：破除“过早优化”臆想，确立 MSVC 2026 汇编实证金标准 (Topic 12: Pragmatic Optimization & SIMD Verification)
* **现状分析**：
  * 严禁臆想“C++26 标准库或新特性会导致 YMM/XMM 向量化回退”而拒绝采用标准库，甚至自己造轮子。
* **专家调研要点**：
  1. 确立“过早优化是万恶之源”的开发准则：优先使用标准库（`std::expected`、`std::span`、`<bit>`、`<algorithm>`），直到真实硬件 Profiling 证明产生性能瓶颈；
  2. 确立编译器物理实证原则：一律以 Visual Studio 2026 (MSVC v18.11+) 实际编译生成的汇编指令为唯一裁决金标准，通过调试器/反汇编验证指令是否保持 YMM 向量化；
  3. 寻求行数最少、表达最现代的 C++26 语法，坚决杜绝为了假想性能而堆砌样板代码。

### 议题十三：大刀阔斧削减 5 万行代码：拔除冗余类与虚构兼容接口 (Topic 13: Aggressive Codebase Slimming)
* **现状分析**：
  * 全仓 C/C++ 代码已达 **49,042 行**，存在过度分层、虚设的抽象基类与为了“未来扩展”保留的未引用接口。
* **专家调研要点**：
  1. 普查全仓所有类与接口，坚决切除所有“非必要且可拔除”的包装类；
  2. 彻底拔除一切向前兼容垫片和为了预期保留的死接口；
  3. 制定将全仓有效代码行数大幅削减、实现极致精简与高内聚的专项瘦身工程路线图。

---

## 3. 专家裁决与方案交付规格 (Deliverables Requirement)

专家组需在调研后输出正式报告：《CineRTX 阶段六：架构全面瘦身与专业级产品化专家裁决与技术方案》，报告必须包含：
1. **针对十三大议题的逐项技术论证与明确裁决**（给出支持/修改/否决意见及第一性原理解析）；
2. **轻量级原生 Win32 UI 控件架构与参数绑定详细设计**；
3. **`types.cpp` 等核心模块 Modern C++26 瘦身对比代码清单**；
4. **全案文档前后矛盾勘误与新阶段定义对齐表**；
5. **Phase 6 多波次工程施工分解计划（WBS）与代码行数削减目标**。
