# CineRTX Phase 12 SuperPower 施工单（Wave 12.2）：自定义类型清剿与 C++26 标准库替换

- **施工单编号**：`CINERTX-P12-WORKORDER-004`  
- **归档路径**：`docs/phase12/04_CineRTX Phase 12 SuperPower 施工单_自定义类型清剿与C++26标准库替换.md`  
- **承接角色**：现代 C++26 核心架构重构与标准库提纯执行工程师（External Execution Engineer）  
- **签发主管**：多媒体系统工程与 GPU 异构计算首席领域专家组 / 项目总协调负责人  
- **前置基线**：Wave 12.1 封板基线（`/WX` 0 警告编译通过，全仓 `std::getenv` / `g_app` / 裸 GDI 句柄 0 命中，146/146 单测全绿）  
- **依据母本**：[`02_CineRTX Phase 12 极限现代 C++26 重构与无头 DLL 净空终审裁决书.md`](./02_CineRTX%20Phase%2012%20极限现代%20C++26%20重构与无头%20DLL%20净空终审裁决书.md)（`CINERTX-P12-RULING-002` §二）  
- **建议工期**：2–3 个有效工作日  
- **核心目标**：贯彻“标准库第一铁律（Std-First Law）”与奥卡姆剃刀原则，升级 `cmake/AuditTypes.cmake` 执行严格词法与白名单拦截；全面以 C++20/23/26 标准库原语替换全仓冗余类型与自定义包装器，**将全仓具名自定义类型从 155 个压减至 ≤ 35 个**，同时剪除内部冗余防御分支，**将全仓 Pure SLOC 从 33,143 行压减至 ≤ 23,000 行（净瘦身 ≥ 30%）**，146/146 单测保持 100% PASS。

---

## 一、 最高架构军规与“零 Slop”判定准则

1. **标准库等价替换律（Std-First Replacement Law）**：  
   - 凡 C++20/23/26 标准库（MSVC 19.51+ `/std:c++latest` 原生支持特性）能够等价或更优表达的语法结构，**严禁手写自定义 `class` 或 `struct`**。  
   - 任何类型清剿不得采用“删除合理语义注释或机械合并成超长单行”的伪消肿手段（Gate 12.2 严格考核 `AuditSloc` 纯净有效代码行 \+ `AuditTypes` 具名类型数）。  
2. **自定义类型存续五类特许白名单（唯一合法存在理由）**：  
   - 仅以下 5 类具有物理或协议不可替代性的类型允许存续，且声明紧邻上方必须附带完整的 `@rtx-type-justification` 契约块：  
     1. `C-ABI`：跨 `extern "C"`、COM、Win32 SDK、CUDA、NVENC 或 NGX 二进制接口的结构体（布局受 ABI 约束）；  
     2. `Hardware-Layout`：封装 GPU 设备虚拟地址（`uint64_t` 不满足 `NullablePointer`）、硬件描述符或 `alignas(64)` 缓存行隔离的原子数据结构；  
     3. `NonTrivial-StateMachine`：管理多阶段异构资源依赖拓扑、具备严格成员声明逆序析构约束的核心引擎会话（如 `NvidiaTranscodeSession`、`MediaPlaybackSession`、`PreviewViewport`）；  
     4. `Std-Customization`：标准库模板特化点（无状态删除器 `FnDeleter`、自定义哈希器等）；  
     5. `Domain-Model`：承载跨端序列化、可逆编辑 Delta 或编译期 GCC 16 P2996 静态反射契约的核心领域聚合根（如 `TranscodeProfile`、`NleProject`）。  
3. **禁止新增任何冗余防御与过渡兼容层**：  
   - 内部模块接口一律使用非空引用 `T&` 或 `std::span` 表达契约，移除层层重复的 `if (!ptr)` 防御；禁止保留旧结构体 `using OldType = ...` 过渡别名，全仓一步到位替换。

---

## 二、 核心重构任务（Tasks W12.2-1 至 W12.2-5）

### 任务 W12.2-1：升级 `cmake/AuditTypes.cmake` 词法与白名单硬门禁

**涉及文件**：`cmake/AuditTypes.cmake`

1. **锁定 5 类白名单与总量硬上限**：  
   - 在 `cmake/AuditTypes.cmake` 中将 `RTX_TYPE_CATEGORIES` 严格锁定为 `"C-ABI;Hardware-Layout;NonTrivial-StateMachine;Std-Customization;Domain-Model"`。  
   - 新增硬数量上限门禁：全仓统计到的合法具名类型总数 `RTX_TYPE_COUNT` 超过 **35 个** 时，脚本直接输出 `FATAL_ERROR` 阻断配置与构建。  
2. **补全词法边界识别**：  
   - 预处理剥离行内注释与 `alignas(...)`、`[[nodiscard]]`、`__declspec(...)`、`final` 等修饰符后再匹配 `class` / `struct` 定义，杜绝任何形式的类型定义绕过审查；  
   - 严格校验 `@rtx-type-justification` 块与紧随类型定义的行距（超过 6 行视作孤立声明块报错），且 `Why-Std-Fails:` 与 `Invariant:` 必须包含实质性非空说明（禁止批量复制占位模板文案）。

---

### 任务 W12.2-2：被动数据袋、包装器与辅助状态机全量清剿（155 → ≤ 35）

**涉及文件**：`include/rtx/*.h`、`src/core/*.h/.cpp`、`src/io/*.h/.cpp`、`src/gui/*.h/.cpp`、`src/shim/*.h/.cpp`

按以下 6 项标准库原语映射表，对现有 155 个具名类型逐一执行物理销毁与替换：

| 清剿类别 | 现存典型 Slop 类型形态 | 强制标准库替换原语 | 工程收益与约束 |
| :---- | :---- | :---- | :---- |
| **1\. 错误与结果包裹类** | `RtxResult`、`StatusOr<T>`、带 `ok` 布尔值的返回结构体 | **C++23 `std::expected<T, rtx::Error>`**（支持 `.and_then()` / `.transform()` / `.or_else()`） | 统一全仓错误流，消灭手动 `if (!res.ok)` 样板代码。 |
| **2\. 多态指令与分支状态** | 含 `enum Kind` \+ 联合字段的命令/事件结构体（CLI 子命令、队列事件、NLE 编辑 Delta） | **C++17/23 `std::variant<...>` \+ `std::visit`**（配合 `Overloaded{lambdas...}`） | 编译期穷尽性检查，消灭标签联合越界访问隐患。 |
| **3\. 被动数据袋与多返回值** | 2–4 字段的临时传参结构体、队列槽位条目、探测结果包 | **C++17 `std::tuple` / `std::pair` \+ 结构化绑定** `auto [a, b, c] = ...` | 零额外具名类型，调用点语义清晰。 |
| **4\. 缓冲视图与多维帧描述** | 散装 `(ptr, pitch, width, height)` 或自定义 `PlaneView` / `AudioBufferView` | **C++20 `std::span<std::byte>` \+ C++23 `std::mdspan`** | 统一步幅张量与平面内存视图，消除越界与步幅算错。 |
| **5\. 策略回调与延迟清理** | 单方法虚基类接口、自定义 `ScopeGuard` / `CleanupAction` 类 | **C++23 `std::move_only_function<void() noexcept>`** 与泛型 Lambda | 消除虚表与 `std::function` 可拷贝约束带来的堆分配。 |
| **6\. 关联查找表与常量重载** | 线性遍历的键值对数组结构体、CRTP 静态多态基类、`const`/非`const` 重复 Getter | **C++23 `std::flat_map` / `std::flat_set`** 与 **C++23 Deducing `this`** | 连续内存缓存命中率最大化，代码量减半。 |

---

### 任务 W12.2-3：范围管道（`std::ranges`）与编译期格式化（`std::format`）全仓提纯

**涉及文件**：`src/core/*.cpp`、`src/io/*.cpp`、`src/cli/main.cpp`、`src/gui/*.cpp`

1. **以 `std::ranges` 管道替代手写嵌套循环**：  
   - 将时间线片段过滤、轨道筛选、参数校验与队列统计中的手写多层 `for` 循环与临时中间 `std::vector`，重构为 `std::views::filter`、`std::views::transform`、`std::views::chunk` 与 `std::ranges::fold_left` / `std::ranges::to` 零中间分配管道。  
2. **以 `std::format` / `std::format_to_n` 替代旧式格式化**：  
   - 彻底清零全仓 `snprintf`、`_snwprintf` 与 `std::ostringstream` 拼接，统一采用类型安全的 `std::format`（及热路径栈缓冲 `std::format_to_n`）。

---

### 任务 W12.2-4：P2996/P3394 静态反射范围扩展至 `i18n` 与 CLI 帮助契约

**涉及文件**：`schema/schema_model.hpp`、`schema/reflection_codegen.cpp`、`schema/generated/*`、`include/rtx/i18n.h`、`src/cli/main.cpp`

1. **消除参数说明与双语标识重复表**：  
   - 利用 GCC 16（`-std=c++26 -freflection`）的 `^^` 与 `std::meta::annotations_of` 反射生成管道，自动产出参数 CLI 帮助文本、范围校验表与 `i18n` 绑定索引，删除 `src/cli/main.cpp` 与 `src/core/param_schema.cpp` 中残留的任何手工映射表。  
2. **保持 MSVC 消费链零反射语法依赖**：  
   - 确认重新生成后的 `schema/generated/` 产物在 MSVC `/std:c++latest /WX` 下零警告编译通过。

---

### 任务 W12.2-5：内部冗余防御剪枝与 Pure SLOC 瘦身收口（≤ 23,000 行）

**涉及文件**：全仓 `include/` 与 `src/`

1. **契约式编程（Design by Contract）减枝**：  
   - 将所有内部模块间必非空指针参数替换为引用 `T&` 或非空 `std::span`，删除内部函数体首部重复的 `if (!p) return ...` 防御分支；  
   - 合并 `src/core/` 与 `src/gui/` 中高度同构的重复辅助函数（如重复的弹窗构建、重复的 JSON 键值读取、重复的矩形布局计算）。  
2. **体量收口核验**：  
   - 运行 `cmake -P cmake/AuditSloc.cmake`，确认全仓纯净有效代码行（Pure SLOC）从 33,143 行真实压减至 **≤ 23,000 行**（净瘦身率 ≥ 30%），同时保留 100% 必要的契约与设计不变量注释。

---

## 三、 Wave 12.2 自动化验收门禁清单（Hard Gates）

外部执行工程师交付前，必须在本地宿主机完成以下全部门禁验证并留存实证日志：

- [ ] **Gate 12.2-1（具名类型数量与契约门禁）**：`cmake -P cmake/AuditTypes.cmake` 输出合法具名类型总数 **≤ 35 个**，0 违规、0 孤立声明、0 模板复制糊弄。  
- [ ] **Gate 12.2-2（Pure SLOC 瘦身门禁）**：`cmake -P cmake/AuditSloc.cmake` 实测全仓 Pure SLOC **≤ 23,000 行**（较 33,143 基线压减 ≥ 30%）。  
- [ ] **Gate 12.2-3（旧式构造清零扫描）**：  
  - `rg "snprintf|_snwprintf|ostringstream" include/ src/` → **0 命中**；  
  - `cmake --build --preset windows-msvc-ninja`（`/WX` 开启）全量目标 **0 错误 0 警告**；  
  - `cmake -P cmake/AuditSpdx.cmake` → **PASS**。  
- [ ] **Gate 12.2-4（单元测试适配与全绿回归）**：同步适配重构后标准库接口的单元测试，运行 `rtx_tests.exe` 实现 **100% PASS（0 失败，0 跳过）**，并在交互式桌面验证 GUI 与转码管线无功能退化。