# CineRTX Phase 12（v0.4.0）极限现代 C++26 重构、代码卫生清剿与系统 DLL 净空终审验收暨演进裁决书

- **文档编号**：`CINERTX-P12-RULING-002`  
- **归档路径**：`docs/phase12/02_CineRTX Phase 12 极限现代 C++26 重构与无头 DLL 净空终审裁决书.md`  
- **审验标的**：[`01_CineRTX Phase 12 极限现代 C++26 重构与无头 DLL 净空专家咨询委托书.md`](./01_CineRTX%20Phase%2012%20极限现代%20C++26%20重构与无头%20DLL%20净空专家咨询委托书.md)（`CINERTX-P12-DELEGATION-001`）暨当前 `v0.3.0`（Commit `530a9a6`）源码树与构建脚本  
- **签发主体**：多媒体系统工程与 GPU 异构计算首席领域专家组（Extreme Modern C++ Purist & Architecture Board）  
- **审验日期**：2026-11-22  
- **终审裁决**：**全面批准启动 Phase 12（v0.4.0）极限重构战役（APPROVED WITH MANDATORY SURGICAL SPECIFICATIONS）**——正式核准基线验收（`v0.3.0` 历史原子化 Rebase 清洗、四门禁全绿与 146/146 单测闭环真实有效），并下达\*\*“标准库第一铁律（Std-First）与自定义类型零容忍清剿令”**、**“微观冗余/隐式开关/全局状态拔除清单”**、**“13 项系统 DLL 净空与 IAT 极限压减矩阵（混合态 ≤ 4 项 / 无头引擎态 \= 2 项）”**以及**“四波次垂直切片施工蓝图”\*\*。

---

## 执行摘要（Executive Ruling）

面对 `v0.3.0` 基线留存的 **33,143 行 Pure SLOC**、**155 个具名自定义类型**、**13 项 Windows 系统 DLL 静态链接依赖**以及 Phase 11 遗留的微观代码卫生欠账，首席领域专家组遵循\*\*奥卡姆剃刀（如无必要，勿增实体）**与**极简完备机制（KISS / Minimally Sufficient Mechanism）\*\*作出如下终审裁决：

1. **基线准入验收结论（PASS）**：确认 `v0.3.0`（`530a9a6`）已完成历史提交原子化清洗，严格捍卫了 `GPL-3.0-or-later WITH Plugin-Exception` 与 `MIT OR Apache-2.0` 的动态桥接物理隔离边界，146/146 单测与四项自动化门禁全部达标，具备实施大刀阔斧外科手术式重构的安全底座。  
2. **自定义类型零容忍清剿令（155 → ≤ 35 个具名类型）**：任何可通过 C++20/23/26 标准库原语（`std::expected`、`std::variant`、`std::optional`、`std::tuple`、`std::span`、`std::mdspan`、`std::move_only_function`、`std::flat_map`、`std::jthread`、`std::chrono`、`std::ranges` 及 Deducing `this`）等价或更优表达的自定义 `class` / `struct` **一律物理消灭**。仅允许五类不可替代实体（C-ABI 跨边界布局、硬件寄存器/显存布局、异步资源生命周期非平凡状态机、标准库特化点删除器/哈希器、反射单一事实源契约模型）存续，且须经升级后的 `cmake/AuditTypes.cmake` 执行逐行词法与契约合法性拦截。  
3. **微观卫生、隐式开关与编译债零容忍拔除**：彻底清除 `nv_encoder.cpp` 单缓冲残余字段与同步事件混用隐患、`rife_engine.cpp` 重复 CUDA 符号表与 `std::getenv` 噪声抑制、`VramAdaptiveScheduler` 双计数器浮点误差、`CMakeLists.txt` 配置期静默联网拉取与全局 `/wd4324` 告警屏蔽，全仓 Pure SLOC 强力压减 **≥ 30%（目标 ≤ 23,000 行）**。  
4. **13 项系统 DLL 极限净空路线**：针对 `{shell32, dwmapi, gdi32, user32, comdlg32, comctl32, uxtheme, d3d12, dxgi, bcrypt, ole32, kernel32, kernelbase}` 实施架构解耦与按需延迟解析：  
   - **纯无头 CLI / 核心引擎态（`CineRTX.exe` 引擎核心）**：静态导入表（IAT）净空至 **仅 2 项底层底座（`kernel32.dll` \+ `bcrypt.dll`）**，实现在 Windows Server Core / 无显示器 / Session 0 无头节点上的零图形子系统开销独立运行；  
   - **Win32 GUI 混合宿主态（或独立前端壳）**：剔除全部非必要壳层库，静态 IAT 压缩至 **≤ 4 项（`kernel32.dll`、`user32.dll`、`gdi32.dll`、`bcrypt.dll`）**，其余图形与系统交互按需受控加载并自带降级回退。

---

## 一、 基线与微观代码卫生法医级穿透验收报告

在启动重构前，专家组对当前 `v0.3.0`（`530a9a6`）源码树与构建脚本进行了逐行穿透复核，锁定以下 **7 项必须在本阶段连根拔除的微观卫生病灶与额外防御隐患**：

| 审计编号 | 病灶精确落点（File : Line） | 违背原则与物理/逻辑根因 | Phase 12 强制外科手术指令 |
| :---- | :---- | :---- | :---- |
| **HYG-P12-01** | `src/shim/loader/nv_encoder.cpp`（单缓冲旧成员与 `completion_event`） | C-07 升级为 `std::vector<BitstreamSlot>` 环形池后，类内仍残留旧版单缓冲成员 `bitstream` 与未对齐 slot 的 `completion_event`；同步事件创建与多槽异步重排存在潜在状态错配与死代码负担。 | **物理删除**单缓冲旧成员；将每槽事件严格封装入 `BitstreamSlot` 的 `UniqueHandle` 成员中，与 `NV_ENC_EVENT_PARAMS` 1:1 生命周期绑定。 |
| **HYG-P12-02** | `src/shim/ngx_bridge/rife_engine.cpp`（局部 `CudaDriverApi` 与 `std::getenv` 抑制） | 与 `nv_func_tables.h/.cpp` 重复定义局部 CUDA 驱动函数指针表（违反 DRY 与 SSOT）；且使用 `std::getenv` 抑制环境警告，引入线程不安全 C 标准库调用与隐式环境耦合。 | 统一复用 `nv_func_tables.h` 单一驱动符号表；**彻底删除** `std::getenv` 隐式分支，严禁任何未在 `TranscodeProfile` 显式声明的环境变量隐藏开关。 |
| **HYG-P12-03** | `include/rtx/vram_scheduler.h` & `src/core/vram_scheduler.cpp` | 同时维护 `frames_in_tier_` 与冗余步进计数器，且水位线计算混用浮点乘法舍入，增加状态空间复杂度。 | 收敛为单一整数帧驻留计数器；水位线判定改为纯整数交叉乘积比较（`usage * 100 >= budget * degrade_pct`），彻底消灭浮点误差与冗余状态。 |
| **HYG-P12-04** | `CMakeLists.txt`（配置期静默 `git submodule update` 与全局 `/wd4324`） | 配置阶段暗中执行 `execute_process(COMMAND git submodule update ...)` 违反离线确定性构建与最小惊讶原则；在 `rtx_project_options` 全局追加 `/wd4324` 掩盖了对齐填充潜在问题。 | **拔除**配置期静默联网拉取（缺失子模块直接 `FATAL_ERROR` 提示）；**移除**全局 `/wd4324`，仅在明确需要 `alignas(64)` 缓存行隔离的原子结构局部使用 `#pragma warning(suppress: 4324)`。 |
| **HYG-P12-05** | `src/gui/rtx_gui.cpp` & `src/gui/workstation_canvas.cpp`（全局可变状态 `g_app`） | 仍依赖文件级可变全局单例 `g_app` 贯穿所有 Win32 窗口过程与回调，造成状态隐式耦合与生命周期不可预测。 | 全面收敛为窗口实例绑定的唯一所有者指针（`GWLP_USERDATA` \+ RAII 生命周期绑定），实现全局可变状态 **0 残留**。 |
| **HYG-P12-06** | `src/gui/workstation_canvas.cpp`（手写 `CreateSolidBrush` / `SelectObject` / `DeleteObject`） | `drawBrandMark` 等局部绘制函数仍手写裸 GDI 句柄申请与释放（非异常安全且冗余）。 | 全部替换为 `rtx::UniqueBrush` 或无分配 `SetDCBrushColor` \+ `GetStockObject(DC_BRUSH)`，清零裸 `DeleteObject` 调用。 |
| **HYG-P12-07** | 全仓 `include/` 与 `src/`（155 个具名类型与冗余防御分支） | 大量 2–4 字段被动数据袋、单函数适配器类、重复 `nullptr` 内部防御分支（违背契约式编程）拉高至 33,143 Pure SLOC。 | 执行标准库代数类型替换与冗余防御剪枝，将内部不变量收敛至类型系统与编译期断言。 |

---

## 二、 现代 C++20 / C++23 / C++26 极致重构规范与类型清剿铁律

### 2.1 编译器现实边界与特性锁定（MSVC 19.51+ `/std:c++latest` \+ GCC 16 P2996）

为兼顾极致现代语法表达力与 Windows 生产链零阻断编译，Phase 12 确立**双层语言特性基线**：

1. **生产主链（MSVC 19.51+ `/std:c++latest`）强制全面落地特性**：  
   - **代数和类型与单轨错误流**：全面采用 C++23 `std::expected<T, rtx::Error>`（支持 `.and_then()`、`.transform()`、`.or_else()` 单子化链式组合）取代所有 `RtxResult`、`bool` \+ out-param 与错误码分支；CLI 子命令分发与管线拓扑状态彻底重构为 `std::variant<...>` \+ `std::visit`（配合重载集 `Overloaded{lambdas...}`）。  
   - **零开销视图与多维张量**：所有帧缓冲、YUV 平面、PCM 音频块与序列化只读切片一律使用 `std::span<const std::byte>` / `std::span<float>` 与 C++23 `std::mdspan`（表达 `(Plane, Height, Width)` 步幅张量布局），彻底消灭 `(ptr, pitch, width, height)` 散装四元组传参。  
   - **轻量可调用与零模板膨胀**：一次性回调与延迟清理动作全面使用 C++23 `std::move_only_function<void() noexcept>`，替代自定义虚基类接口与沉重的 `std::function`。  
   - **扁平关联容器与范围管道**：配置查找表与小型映射表全面迁移至 C++23 `std::flat_map` / `std::flat_set`（连续内存缓存友好）；集合变换、过滤与切片优先使用 `std::views::transform`、`std::views::filter`、`std::views::chunk` 与 `std::ranges::to`。  
   - **显式对象参数（Deducing `this`）**：消除所有 CRTP 静态多态基类与 `const` / 非 `const` 双重重载样板函数，统一使用 `template <typename Self> auto&& get(this Self&& self)`。  
   - **编译期安全格式化**：全仓日志、错误消息与时间码生成统一使用 `std::format` / `std::format_to_n`（零堆分配栈缓冲格式化），严禁 `snprintf` 与流式 `std::ostringstream`。  
2. **静态反射边界（GCC 16 `-std=c++26 -freflection`）**：  
   - `schema/` 独立反射代码生成轨继续作为全仓参数契约的**唯一事实源（SSOT）**，进一步将 `i18n` 词条与 CLI 帮助文本纳入 P2996/P3394 编译期反射生成，消除任何手工同步表。

---

### 2.2 自定义类型“零容忍”清剿清单（5 大类立即销毁，仅 5 类特许存续）

#### 立即物理销毁的 5 类伪抽象（一律视为 Slop 垃圾代码）

1. **被动数据袋结构体（Data-Bag Structs → `std::tuple` / `std::pair`）**：凡无独立不变量、仅用于函数多返回值或局部队列条目的 `struct`（如 `InspectorRow`、`FrameEntry`、`ProbeResult` 等），全部销毁并替换为 `std::tuple` / `std::pair` \+ 结构化绑定 `auto [a, b, c] = ...`。  
2. **自定义 RAII 句柄包装类（Custom Wrappers → `std::unique_ptr<T, FnDeleter<Fn>>`）**：严禁任何手写构造/移动/析构的句柄包装器，统一复用 `FnDeleter<auto Fn>` \+ 类型别名；作用域退出清理统一使用极简 `ScopeExit` Lambda 哨兵。  
3. **无状态或单方法策略类（Functor / Strategy Classes → Lambda 闭包）**：凡仅包含 `operator()` 或单一虚方法的辅助类，全部内联为泛型 Lambda 或 `std::move_only_function`。  
4. **手动循环线程类（Worker Classes → `std::jthread` \+ `std::stop_token`）**：所有后台线程控制循环直接由 `std::jthread` 持有 Lambda 闭包的局部状态，严禁为线程生命周期单独建类。  
5. **自定义时间/区间/有理数包装（Custom Time Types → `std::chrono::duration`）**：全仓时间算术严格锚定 `using Flicks = std::chrono::duration<int64_t, std::ratio<1, 705600000>>`，利用标准库 `duration_cast` 与 `std::ratio` 编译期约分。

#### 仅允许存续的 5 类特许类型白名单（`AuditTypes.cmake` 硬性校验）

任何存续的 `class` / `struct` 必须严格属于以下五类之一，并在定义上方附带完整 `@rtx-type-justification` 契约块：

1. **`C-ABI`**：跨越 `extern "C"` 或 COM / Windows SDK / CUDA / NVENC / NGX 二进制接口的结构体（布局受 ABI 约束）。  
2. **`Hardware-Layout`**：映射 GPU 显存地址（`uint64_t` 不满足 `NullablePointer`）、硬件描述符或 `alignas(64)` 缓存行隔离的原子数据结构。  
3. **`NonTrivial-StateMachine`**：管理多阶段异构资源依赖拓扑、具备严格成员声明逆序析构约束的核心引擎会话（如 `NvidiaTranscodeSession`、`MediaPlaybackSession`、`PreviewViewport`）。  
4. **`Std-Customization`**：标准库模板特化点（无状态删除器 `FnDeleter`、自定义哈希器等）。  
5. **`Domain-Model`**：承载跨端序列化、可逆编辑 Delta 或编译期 P2996 静态反射契约的核心领域聚合根（如 `TranscodeProfile`、`NleProject`）。

---

### 2.3 奥卡姆剃刀与“零心智负担”负面清单（全仓拔除）

- **拔除冗余内部防御（No Defensive Clutter）**：内部模块间传递非空引用 `T&` 或 `std::span` 替代 `T*`；消除入口处层层嵌套的 `if (!ptr) return Error;` 重复判空，仅在外部输入（CLI / JSON / 文件 I/O / 硬件驱动返回）边界执行一次性严格校验。  
- **拔除一切隐藏开关（Zero Hidden Switches）**：全仓清零 `std::getenv`、`GetEnvironmentVariableW`、未在 `TranscodeProfile` 公开的注册表/文件配置旁路以及秘密调试宏，确保程序行为 100% 由显式输入参数决定。  
- **拔除多余编译选项（Minimal Build Flags）**：精简 `CMakeLists.txt`，去除冗余预处理器宏与 warning 抑制项，保持纯粹的 `/std:c++latest /W4 /WX /permissive- /EHsc /utf-8 /arch:AVX2` 与静态运行时 `/MT`。

---

## 三、 13 项 Windows 系统 DLL 极限净空与 IAT 压减裁决

针对当前构建中涉及的 13 项系统动态库：`{shell32.dll, dwmapi.dll, gdi32.dll, user32.dll, comdlg32.dll, comctl32.dll, uxtheme.dll, d3d12.dll, dxgi.dll, bcrypt.dll, ole32.dll, kernel32.dll, kernelbase.dll}`，专家组逐一穿透调用栈并作出如下物理剥离与架构解耦裁决：

### 3.1 13 项系统 DLL 逐项去留与替代路径矩阵

| 序号 | 系统 DLL | 当前引入来源与历史用途 | 剥离可行性与替代实现方案 | 无头核心态 IAT | GUI 混合态 IAT |
| :---: | :---- | :---- | :---- | :---: | :---: |
| 1 | **`kernel32.dll`** | 进程/线程、同步原语、虚拟内存、文件 I/O、动态库加载基础。 | **不可移除（核心底座）**：Windows Win32 子系统根基，作为动态解析其他可选模块（`LoadLibraryExW(..., LOAD_LIBRARY_SEARCH_SYSTEM32)`）的唯一入口。 | **保留 (1)** | **保留 (1)** |
| 2 | **`kernelbase.dll`** | 现代 Windows 10/11 API 转发层（部分 SDK 导入库直接引用）。 | **物理剥离 IAT**：统一链接标准 `kernel32.lib` umbrella 导入库，禁止直接链接 `kernelbase.lib` 或 `onecore.lib`，消除冗余 IAT 条目。 | **移除** | **移除** |
| 3 | **`bcrypt.dll`** | C++ 标准库 `std::random_device` 与加密安全随机数/SHA-256 哈希。 | **保留（安全基石）**：提供零状态系统熵源与原生硬件加速哈希，体积极轻且全版本 Windows 标配，避免手写伪随机与自研哈希轮子。 | **保留 (2)** | **保留 (2)** |
| 4 | **`user32.dll`** | Win32 窗口消息循环、控件创建与显示器几何查询。 | **核心解耦剥离**：将 `rtx_core` 与 CLI 彻底剥离 `user32` 依赖（无头转码/批处理零 GUI 导入）；仅在交互式 Win32 GUI 宿主模块中静态保留。 | **移除** | **保留 (3)** |
| 5 | **`gdi32.dll`** | Win32 自定义绘制（时间轴画布、字体、双缓冲位图）。 | **核心解耦剥离**：同 `user32.dll`，纯无头模式下零引用；GUI 宿主态下保留并统一使用 `DC_BRUSH` / `DC_PEN` 消除句柄泄漏。 | **移除** | **保留 (4)** |
| 6 | **`d3d12.dll`** | D3D12 设备、命令队列、Compute Shader 视口与 NGX D3D12 互操作。 | **移除静态 IAT（改延迟解析）**：移出静态链接库列表！通过 `LoadLibraryExW(L"d3d12.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)` 动态解析 `D3D12CreateDevice` 与 `D3D12SerializeRootSignature`，无显卡或纯 CPU 软编模式下实现零加载启动。 | **移除** | **移除 (动态)** |
| 7 | **`dxgi.dll`** | DXGI 适配器枚举、本地显存预算查询（`QueryVideoMemoryInfo`）与合成交换链。 | **移除静态 IAT（改延迟解析）**：移出静态链接库列表！动态解析 `CreateDXGIFactory1` / `CreateDXGIFactory2`，在无头服务器环境下优雅降级。 | **移除** | **移除 (动态)** |
| 8 | **`ole32.dll`** | COM 套间初始化（`CoInitializeEx`）、`CoTaskMemFree`（WASAPI 混音格式释放）与文件对话框。 | **移除静态 IAT（改延迟解析）**：WASAPI 音频播放与文件对话框仅在交互式 GUI/播放模式按需触发，动态解析 `CoInitializeEx` / `CoUninitialize` / `CoTaskMemFree`，无头批处理转码零 COM 依赖。 | **移除** | **移除 (动态)** |
| 9 | **`shell32.dll`** | 拖放文件接收（`DragAcceptFiles` / `DragQueryFileW`）与目录浏览。 | **彻底移除静态链接**：消除 `#pragma comment(lib, "shell32.lib")`；拖放功能若保留仅需 3 个导出符号动态解析，或由现代对话框统一承接，IAT 零出现。 | **移除** | **移除** |
| 10 | **`comdlg32.dll`** | 传统 `GetOpenFileNameW` / `GetSaveFileNameW` 文件选择框。 | **彻底物理删除**：传统阻塞式模态对话框样式陈旧且拉入重型壳层依赖；统一使用轻量动态 COM `IFileOpenDialog` / `IFileSaveDialog`（复用动态 `ole32`）或拖拽/路径输入，彻底消灭 `comdlg32.dll`。 | **移除** | **移除** |
| 11 | **`comctl32.dll`** | Win32 通用控件（TrackBar 进度条、ListView 队列列表）与视觉样式清单。 | **物理净空移除**：使用原生 `user32` 窗口类与 `gdi32` 自绘控件（或已有 WebUI 界面）完全替代重型通用控件，移除 `#pragma comment(lib, "comctl32.lib")` 与清单依赖。 | **移除** | **移除** |
| 12 | **`uxtheme.dll`** | 暗色模式窗口标题栏/滚动条主题（`SetWindowTheme` / `AllowDarkModeForWindow`）。 | **移除静态 IAT（可选延迟探测）**：移除 `#pragma comment(lib, "uxtheme.lib")`；暗色主题增强通过 `GetModuleHandleW` / `LoadLibraryExW` 可选探测，缺失时静默回退，零静态导入。 | **移除** | **移除** |
| 13 | **`dwmapi.dll`** | DWM 暗色标题栏属性（`DwmSetWindowAttribute` `DWMWA_USE_IMMERSIVE_DARK_MODE`）与合成同步。 | **移除静态 IAT（可选延迟探测）**：移除 `#pragma comment(lib, "dwmapi.lib")`；通过动态获取 `DwmSetWindowAttribute` 设置沉浸式暗色边框，在 Windows Server / 无 DWM 会话下零报错安全跳过。 | **移除** | **移除** |

---

### 3.2 架构解耦裁决：双形态 IAT 极简包络

为彻底根治“启动纯无头转码任务却被迫加载全套桌面窗口与主题 DLL”的架构耦合，Phase 12 将构建产物与导入表门禁（`cmake/AuditImports.cmake`）升级为双形态严苛包络：

1. **核心引擎与无头 CLI 模式（`rtx_core` 零 GUI/DWM/Shell 污染）**：  
   - `rtx_core` 与无头执行入口仅允许静态导入 **`kernel32.dll`** 与 **`bcrypt.dll`**（**共计 2 项**）。  
   - 所有 GPU 计算（CUDA / NVDEC / NVENC / TensorRT / NGX / D3D12 / DXGI）与音频输出（WASAPI / `ole32`）全部采用 `LOAD_LIBRARY_SEARCH_SYSTEM32` 延迟按需解析，确保在任何 Windows Server Core、Docker 容器或 Session 0 算力节点上秒级冷启动且零缺失 DLL 报错。  
2. **Win32 GUI 混合单可执行形态（`CineRTX.exe` 桌面发行版）**：  
   - 静态导入表（IAT）硬上限压缩至 **4 项：`kernel32.dll`、`user32.dll`、`gdi32.dll`、`bcrypt.dll`**。  
   - `dumpbin /imports CineRTX.exe` 自动化审计脚本（`AuditImports.cmake`）将把 `shell32.dll`、`dwmapi.dll`、`comdlg32.dll`、`comctl32.dll`、`uxtheme.dll`、`d3d12.dll`、`dxgi.dll`、`ole32.dll`、`kernelbase.dll` 全部列入**静态 IAT 零容忍黑名单**（发现任一静态导入条目即判构建失败）。

---

## 四、 Phase 12（v0.4.0）四波次垂直切片实施路线与验收门禁

为防止重演 Phase 11 早期因单阶段范围过载导致的失控，Phase 12 严格划分为 **4 个串行垂直切片波次（Wave 12.1 – Wave 12.4）**，每波次合龙后必须立即通过全量 编译 \+ `AuditTypes` \+ `AuditSloc` \+ `AuditImports` \+ `rtx_tests.exe` 门禁方可进入下一波次：

| 波次编号 | 战役主题 | 核心外科手术任务与交付物 | 量化硬验收门禁（Hard Gates） |
| :---- | :---- | :---- | :---- |
| **Wave 12.1** | **构建净化与微观病灶止血** | 1\. 清理 `CMakeLists.txt`：拔除配置期静默 `git submodule update` 与全局 `/wd4324`，升级 `/WX`（警告即错误）； 2\. 根治 `HYG-P12-01 ~ 06`：清除 `nv_encoder.cpp` 单缓冲残余与事件错配、统一 `rife_engine.cpp` 驱动符号表并删除 `std::getenv`、整数化 `VramAdaptiveScheduler`、拔除 `g_app` 全局状态与裸 GDI 句柄。 | • 全仓 **0 警告**编译通过； • 全仓 `std::getenv` / `GetEnvironmentVariable` **0 命中**； • 146/146 单测 100% PASS。 |
| **Wave 12.2** | **自定义类型清剿与 C++26 标准库替代** | 1\. 升级 `cmake/AuditTypes.cmake`：强制限制具名类型仅属 5 类白名单，新增对单行内联 `struct`、`alignas` 与契约语义的词法拦截； 2\. 执行类型清剿：以 `std::expected`、`std::variant`、`std::tuple`、`std::span`、`std::mdspan`、`std::move_only_function`、`std::flat_map` 与 Deducing `this` 替换全仓冗余类； 3\. 剪除内部冗余防御分支与重复校验代码。 | • 具名自定义类型从 155 个压减至 **≤ 35 个**（100% 合法 `@rtx-type-justification`）； • 全仓 Pure SLOC 从 33,143 行压减至 **≤ 23,000 行**（净瘦身 ≥ 30%）。 |
| **Wave 12.3** | **13 项系统 DLL 净空与核心/界面解耦** | 1\. 彻底隔离 `rtx_core` 与 Win32 GUI：确保核心静态库零引用 `user32` / `gdi32` / `ole32`； 2\. 实施延迟解析替换：将 `d3d12`、`dxgi`、`ole32`、`dwmapi`、`uxtheme` 移出静态链接表，物理删除 `comdlg32`、`comctl32`、`shell32`、`kernelbase` 依赖； 3\. 升级 `cmake/AuditImports.cmake` 执行双形态 IAT 审计。 | • `dumpbin /imports` 实证：   \- 无头引擎核心静态 IAT \= **2 项**（`kernel32.dll`, `bcrypt.dll`）；   \- GUI 混合 `CineRTX.exe` 静态 IAT **≤ 4 项**（`kernel32.dll`, `user32.dll`, `gdi32.dll`, `bcrypt.dll`）。 |
| **Wave 12.4** | **全场景实机回归与 `v0.4.0` 封板** | 1\. 在 RTX 5060（Driver ≥ 621.24 WHQL）交互式桌面与模拟无头（无显示器/Session 0）双环境下执行端到端压力测试； 2\. 验证 4K AV1 HDR10 重制（`D2H == 0 B`）、DirectComposition 100 次 Resize、NLE 剪辑导出与全量单元测试闭环。 | • 双环境 100% 稳定运行零崩溃； • 全部门禁（`AuditTypes` / `AuditSloc` / `AuditImports` / `AuditSpdx` / `rtx_tests`）全绿，正式签发 **`v0.4.0`**。 |

---

## 五、 立即执行指令（Wave 12.1 施工准入令）

项目总协调负责人可即刻依据本裁决书，生成并派发 **`CINERTX-P12-WORKORDER-003`（Wave 12.1 构建净化与微观卫生清剿施工单）**。所有执行工程师进场前须逐条签署“标准库第一铁律”与“零隐式开关/零冗余防御”军令状，凡新增任何可用 `std::*` 替代的自定义类或引入任何黑名单 DLL 静态导入者，门禁系统将在编译与链接期予以无条件自动拦截驳回。