# CineRTX Phase 11.2 代码膨胀与 Slop 重度污染治理专家咨询委托书

- **文档编号**：`CINERTX-P11-CONSULT-023`
- **归档路径**：[`23_CineRTX Phase 11.2 代码膨胀与Slop重度污染治理专家咨询委托书.md`](./23_CineRTX%20Phase%2011.2%20代码膨胀与Slop重度污染治理专家咨询委托书.md)
- **呈报角色**：项目总协调负责人（遵照用户最高整改指令）
- **呈送对象**：多媒体系统工程与 GPU 异构计算首席领域专家组
- **基线版本**：Phase 11.2 施工后基线（128/128 测试全绿，驱动 621.24 WHQL，但全仓手写源码暴增至 43,450 行）
- **核心触发**：用户在审阅 Phase 11.2 施工统计（单阶段物理新增 6,927 行、净增 5,824 行）后严厉喝止推进，明确裁决：
  > **“无法进行下一步了，就目前情况来说项目卫生严重不达标，必须整改。说明项目中存在过多的 slop，抽象和复用严重不足。”**
  > **“我的期望是连这些 raii_owner 都视作无意义的 slop。所有的类都应该用 std 标准库先替换，实在没办法必须写的类才应该存在，对任何一个自定义类/结构体都无容忍，任何一个自定义类/结构体都必须显式声明存在意义，把这一点记到请求文档里。”**

---

## 一、 紧急熔断背景与用户最高裁决：标准库至上与自定义类零容忍

在 Phase 11.2 施工验收中，虽然外部执行工程师交付了 128/128 项单元测试全绿并满足了 Linker `.map` 修饰名留存等表面技术门禁，但在统计全仓代码体量时暴露出触目惊心的**代码恶性膨胀与生成式 Slop 污染**：

| 度量指标 | Phase 11.2 施工前基线 | Phase 11.2 施工后现状 | 阶段绝对变化量 | 严重程度判定 |
| :--- | :--- | :--- | :--- | :--- |
| **手写核心源文件数** | 111 个 | 120 个 | **+9 个新文件** | 🔴 结构失控 |
| **手写源码总行数 (`src/`+`tests/`+`include/`+`cmake/`)** | 37,626 行 | 43,450 行 | **物理新增 6,927 行（净增 +5,824 行）** | 🔴 恶性膨胀 |
| **单个核心业务实现文件行数 (`playback_session.cpp`)** | 0 行（不存在） | 1,254 行 | **+1,254 行单文件巨石** | 🔴 严重违背 KISS |
| **界面主入口文件行数 (`rtx_gui.cpp`)** | 1,289 行 | 1,539 行 | **+250 行（内联图形底层 Sink）** | 🔴 领域职责越界 |
| **测试夹具文件行数 (`test_streams.h`)** | 0 行 | 1,306 行 | **+1,306 行十六进制字节码流** | 🔴 工程卫生严重污染 |
| **全仓自定义包装类/结构体数量** | 泛滥 | 进一步恶化 | **随意手写私有类与冗余 Wrapper** | 🔴 伪面向对象 Slop |

用户在审查该统计后，对当前代码库充斥的 AI 代码生成恶习和虚胖体量进行了极其深刻的批判，并下达了**根本性的架构军规**：

1. **重新定义 Slop：手写包装器（`raii_owner`）等同于无意义的 Slop**
   - 过去团队盲目追求手写所谓的 `raii_owner`（如 [`include/rtx/raii_owners.h`](../../include/rtx/raii_owners.h) 中长达 385 行的 `UniqueHandle`、`UniqueModule`、`UniqueHwnd`、`UniqueThread`、`UniqueBrush`、`UniqueFont` 以及各种自定义 deleter 包装器）。
   - 用户深刻指出：**现代 C++ 标准库（尤其 CineRTX 已开启 C++26）拥有完备的原语。手写几十个包含数十行样板构造/析构/拷贝删除/移动赋值的自定义结构体，本质上是彻头彻尾的机械样板堆砌与无意义的 AI Slop！**
2. **标准库先验替换律 (Law of Standard Library Primacy)**
   - **“所有的类都应该用 std 标准库先替换，实在没办法必须写的类才应该存在。”**
   - 所有资源所有权表达，一律强制优先使用 `std::unique_ptr<T, Deleter>`、`std::shared_ptr`；
   - 所有工作线程管理，一律强制优先使用 C++20 原生自带自动 RAII 协同与析构安全加入的 `std::jthread`，彻底淘汰手写的 `UniqueThread`；
   - 所有的元组、键值对、变体状态与跨线程数据传递，一律强制优先使用 `std::pair`、`std::tuple`、`std::variant`、`std::optional`、`std::span`，严禁随意为了传参随手捏造一次性 `struct`。
3. **自定义类/结构体零容忍与显式声明存在意义 (Zero Tolerance & Explicit Existence Justification)**
   - **“对任何一个自定义类/结构体都无容忍，任何一个自定义类/结构体都必须显式声明存在意义。”**
   - 任何在代码中保留或新增的 `class` 或 `struct`，都必须承担极其苛刻的举证责任；
   - 任何存续的自定义类型，其代码声明上方必须包含标准形式的显式声明，明确回答：
     1. **为什么标准库机制（`std::*`）绝对无法替代该类型？**（例如：特定硬件 SDK 的 C-ABI 内存布局强绑定、必须持有虚表供外部 C 导出注入等）；
     2. **该类型的核心不变量与唯一存在理由是什么？**
   - 凡未显式声明存在意义、或理由不成立的自定义类/结构体，全部按 Slop 垃圾代码定性并在构建门禁中予以拦截驳回！

---

## 二、 代码 Slop 膨胀、伪包装器与抽象缺失的法医级穿透取证

协调负责人对当前工作区展开地毯式源码核查，锁定以下 6 大典型“Slop 污染、伪包装器与无节制手写类”重灾区：

### 1. `include/rtx/raii_owners.h`（385 行）与 `include/rtx/com_owner.h`（94 行）—— 包装器 Slop 的源头毒灶
- **源码断点**：[`include/rtx/raii_owners.h`](../../include/rtx/raii_owners.h#L39-L170)、[`include/rtx/com_owner.h`](../../include/rtx/com_owner.h#L24-L87)
- **事实取证**：
  1. [`raii_owners.h`](../../include/rtx/raii_owners.h) 中充斥着对操作系统与 CUDA 原生句柄的冗余包装：
     - `UniqueHandle`（22 行）、`UniqueModule`（21 行）、`UniqueHwnd`（21 行）、`UniqueGdiObject`（28 行）……每个结构体都机械重复手写 `operator=`、`reset()`、`release()`、`operator bool()`。而在标准库中，这些完全可以用 `std::unique_ptr<void, Win32CloseHandleDeleter>`、`std::unique_ptr<HMODULE__, Win32FreeLibDeleter>` 等最简类型别名（Type Alias）在 2 行内解决，根本无需发明自定义 struct！
     - `UniqueThread`（30 行）：内部仅仅包了一个 `std::thread` 并在析构时调用 `join()`！现代 C++20 已正式引入 `std::jthread`，标准库原生具备退出时自动 `request_stop()` 与 `join()` 语义，手写 `UniqueThread` 是完全背离现代 C++ 标准的无知表现。
  2. [`com_owner.h`](../../include/rtx/com_owner.h)（94 行）：重新造了一个劣质的 COM 智能指针轮子，其 API 风格和行为与标准库或已有设施割裂。
  3. **下游毒化效应**：正是因为公共头文件带头滥造自定义 Wrapper 类，下游执行工程师在写业务代码时更是肆无忌惮，疯狂手写私有类！

### 2. `src/core/playback_session.cpp`（1,254 行）—— 单文件私有类泛滥与无视标准库
- **源码断点**：[`src/core/playback_session.cpp`](../../src/core/playback_session.cpp#L1-L1254)
- **事实取证**：
  1. 工程师在单个 `.cpp` 内部一口气堆砌了 4 个庞大的私有类定义与完整实现：
     - `PlaybackClock`（约 80 行）：自己写类维护时钟与倍速，内部硬编码 `0.7056` 浮点数，完全无视 C++ 标准库 `std::chrono` 与已有的 `time_model.h` 严格分数。
     - `FrameDeadlineScheduler`（约 120 行）：手写调度状态机。
     - `DecodeAheadQueue`（约 150 行）：手写类包装 `std::deque`、`std::mutex`、`std::condition_variable`。
     - `DecoderOwner`（约 60 行）：又手写了一套私有的解码器 RAII 移动语义！
  2. **完全无视标准库与复用**：单文件聚集解码工作线程、音频工作线程、重采样器、同步时钟、死锁边缘的多互斥锁竞争。整个文件 1,254 行充斥着样板胶水，缺乏任何标准库的高阶表达。

### 3. `src/gui/rtx_gui.cpp`（1,539 行）—— 底层图形 Sink 暴力内联进 Win32 消息循环
- **源码断点**：[`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp#L113-L315)
- **事实取证**：
  1. 在负责 UI 界面主窗口和消息循环的 [`rtx_gui.cpp`](../../src/gui/rtx_gui.cpp) 中，从第 113 行到 315 行赫然内联了一个超过 200 行的自定义类 `PlaybackDCompSink`！
  2. 包含了 D3D12 设备访问、无头桥交互、三槽预览环管理、Fence 等待与槽位释放逻辑。底层图形硬件同步逻辑直接裸奔在 UI 消息循环文件中，完全违背单一职责原则，属于典型的 AI 生成式 Slop 粘贴操作。

### 4. `tests/fixtures/test_streams.h`（1,306 行）—— 十六进制机器码直接污染 C++ 头文件
- **源码断点**：[`tests/fixtures/test_streams.h`](../../tests/fixtures/test_streams.h#L48-L1306)
- **事实取证**：
  1. 外部工程师为了所谓的“自包含单测”，竟然将 1,300 余行十六进制字节数组（`kFixtureH264`、`kFixtureHEVC`、`kFixtureAV1`）硬编码贴入 C++ 源代码头文件中。
  2. 文件中甚至包含连续几十行由纯 `0x00, 0x00, 0x00...` 构成的无效填充。这种简单粗暴将二进制素材伪装成 C++ 代码的做法，是对项目工程卫生的粗暴践踏。

### 5. `src/gui/preview_viewport.cpp`（1,029 行）—— Win32/COM 样板代码无节制膨胀
- **源码断点**：[`src/gui/preview_viewport.cpp`](../../src/gui/preview_viewport.cpp)
- **事实取证**：
  视口逻辑在加入 4 项安全加固后，代码膨胀至 1,029 行。交换链创建、DComp 目标绑定、根签名与管线状态构建充斥着大量的 Win32/COM 样板代码，缺乏基于标准库特化的轻量级辅助工具，导致每次增补边界检查都需要膨胀数百行重复代码。

### 6. `src/core/ngx_remaster.cpp`（新增 88 行）—— 全局静态状态与胶水穿透
- **源码断点**：[`src/core/ngx_remaster.cpp`](../../src/core/ngx_remaster.cpp#L85-L160)
- **事实取证**：
  为支撑转码监看槽位借用，直接在文件中引入了全局静态状态与原子标志集合，模块间依赖通过隐式全局变量穿透传递，破坏了纯净的架构依赖拓扑。

---

## 三、 五大专项请示议题（Consultation Inquiries）

针对上述由于“包装器伪抽象导致 Slop 恶习”、“严重无视 C++ 标准库”以及“缺乏高阶抽象与复用”导致的工程质量危机，特向首席领域专家组紧急呈递以下 5 项请示议题：

### 请示项 1：制定《CineRTX 标准库先验替换与自定义类清零规范》
> **请示**：
> 1. 请专家组为 CineRTX 制定一套严格的**“标准库先验替换法则（Std-First Rule）”**，明确指导工程团队：
>    - 如何全面使用 `std::unique_ptr<T, CustomDeleter>` 替换所有自定义 `raii_owner` 结构体（将 385 行代码压缩为数个 2 行别名声明）？
>    - 如何全面使用 C++20 `std::jthread` 彻底绝灭全仓的 `UniqueThread`？
>    - 如何使用标准库的 `std::tuple`、`std::pair`、`std::variant`、`std::optional`、`std::span` 淘汰业务层随意创建的一次性数据包裹结构体？
> 2. **存续类型显式存在意义规范**：请专家组提供一套标准的“自定义类存续声明模板（Class Existence Justification Schema）”，规定凡是保留或新增的自定义类/结构体，必须强制标注其为何无法用 `std` 替换。请审定该契约的标准格式与合规红线。

### 请示项 2：`src/core/playback_session.cpp` 的标准库化脱毒与消肿方案
> **请示**：
> 1. 如何对长达 1,254 行的 [`playback_session.cpp`](../../src/core/playback_session.cpp) 实施外科手术？
> 2. 请专家组指导如何**彻底消灭**该文件内部私自定义的 `PlaybackClock`、`FrameDeadlineScheduler`、`DecodeAheadQueue` 与 `DecoderOwner` 四个私有类：
>    - 时钟与调度：如何改用标准库 `std::chrono` 与 `time_model.h` 的无锁原生表达？
>    - 解码前瞻队列：如何用极简的 `std::queue<std::pair<...>>` 或无锁环形队列替代自定义队列类？
>    - 线程模型：如何直接使用 `std::jthread` 组织数据面与控制面，消除所有胶水互斥锁？
> 3. 目标应将该文件从 1,254 行压缩至 400–500 行精锐形态，请专家组出具高阶抽象架构图。

### 请示项 3：视口域解耦与 `rtx_gui.cpp` 瘦身方案
> **请示**：
> 1. 如何将暴力内联在 [`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp#L113-L315) 中的 200 余行 `PlaybackDCompSink` 彻底剔除？
> 2. 专家组推荐将其重构归位至视口管理域（`src/gui/preview_viewport`）还是设计为纯粹的标准库智能指针管理组件？
> 3. 如何使 `rtx_gui.cpp` 剥离全部图形硬件细节，回归纯粹的 Win32 消息分发控制器，使其代码体量回落至 1,000 行以内？

### 请示项 4：测试码流与二进制夹具的 CMake 自动化隔离标准
> **请示**：
> 1. 针对 [`tests/fixtures/test_streams.h`](../../tests/fixtures/test_streams.h) 中高达 1,306 行的硬编码十六进制数组，专家组推荐的工业级最佳实践为何？
> 2. 是否应当将真实的微型 `.mkv` 码流样本作为物理测试资源存放于 `tests/fixtures/`，并在 CMake 构建期通过小型工具（或 CMake 原生脚本）将其生成至构建输出目录（如 `.vs/build/.../generated_fixtures/`），从而彻底从版本库源码树中清除所有十六进制机器码？

### 请示项 5：设立 Phase 11.2-R（标准库消肿与 Slop 拔除）专项整改波次
> **请示**：
> 鉴于当前代码库被大量包装器 Slop 和未抽象代码严重污染，项目团队建议：
> **在正式开工 Phase 11.3 之前，设立为期 1–2 个工作日的专项整改波次「Phase 11.2-R（Refactor & De-slop）」**。
> - **硬性准出指标**：
>   1. 全仓手写源码净增量从当前的 +5,824 行断崖式压缩至 **+2,000 行以内**（单阶段脱毒瘦身率 $\ge 60\%$）；
>   2. 彻底清零 `playback_session.cpp` 中的私有类，文件行数降至 500 行以内；
>   3. 彻底从 `rtx_gui.cpp` 剔除 `PlaybackDCompSink`；
>   4. 彻底从头文件中拔除 1,306 行硬编码十六进制机器码；
>   5. 全仓所有保留的自定义类/结构体 100% 具备显式存在意义声明并通过静态扫描；
>   6. 128/128 项单元测试继续保持 100% PASS，PE 导入表与 Linker .map 审计 100% PASS。
> 
> 请专家组对 Phase 11.2-R 的整改目标、实施边界与准出验收规范给予权威批复！

---

## 四、 协调负责人整改态度与立行立改承诺

协调负责人深刻领会并完全认同用户关于“标准库至上、手写包装类等同于 Slop、对自定义类零容忍”的战略远见：

1. **反思根因**：过去在推行 RAII 时陷入了“过度面向对象”的本本主义陷阱，误以为给每个 Win32 句柄或 CUDA 资源手写一个独立的 `struct FooOwner` 就是正规工程，反而在版本库中引入了数百行毫无营养的样板类。这种坏品味直接导致后续外包工程师层层效仿，最终酿成了 5,824 行的体量灾难与 AI Slop 狂欢！
2. **立行立改**：坚决摒弃虚伪的代码繁荣，确立**“能用 std 替换的绝不自己写类，必须保留的类必须宣誓其不可替代性”**的铁律。在专家组方案出炉后，将严格按照 Phase 11.2-R 规范督导执行工程师实施靶向脱毒，不挤干最后一滴 Slop 水分，绝不放行下一阶段！

特此呈报，静候专家组权威裁决与脱毒瘦身方案！
