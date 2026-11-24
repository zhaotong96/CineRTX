# CineRTX Phase 12 Wave 12.2-A 异步槽位Deleter默认构造性专家咨询委托书

- **文档编号**：`CINERTX-P12-DELEGATION-026`
- **归档路径**：[`docs/phase12/26_CineRTX Phase 12 Wave 12.2-A 异步槽位Deleter默认构造性专家咨询委托书.md`](./26_CineRTX%20Phase%2012%20Wave%2012.2-A%20异步槽位Deleter默认构造性专家咨询委托书.md)
- **呈报主体**：CineRTX 项目总协调负责人（Coordinator）
- **受呈报方**：多媒体系统工程与 GPU 异构计算首席领域专家组（Extreme Modern C++ Purist & Architecture Board）
- **当前状态**：🟡 **Wave 12.2-A 攻坚达到 98%（`rational.cpp` 100% 绿灯），提请专家组对 `ScopedWin32Event` Deleter 默认构造性裁决（FINAL DELETER CLOSURE）**
- **审验标的**：
  * [`24_CineRTX Phase 12 Wave 12.2-A 编译断环与类型清剿纠偏终审裁决书.md`](./24_CineRTX%20Phase%2012%20Wave%2012.2-A%20编译断环与类型清剿纠偏终审裁决书.md)（`CINERTX-P12-RULING-024`）
  * [`25_CineRTX Phase 12 Wave 12.2-A 编译断环止血纠偏返工汇报.md`](./25_CineRTX%20Phase%2012%20Wave%2012.2-A%20编译断环止血纠偏返工汇报.md)（`CINERTX-P12-REPORT-025`）
  * 宿主机 MSVC 19.51+ `/W4 /WX` 真实编译器日志与 `src/shim/loader/nv_encoder.cpp`
- **基线标的**：Commit `02694fb2`（Wave 12.1 封板基线）

---

## 一、 委托背景与重大纠偏突破

在首席领域专家组下发 [24 号裁决书](./24_CineRTX%20Phase%2012%20Wave%2012.2-A%20编译断环与类型清剿纠偏终审裁决书.md) 后，执行工程师针对两处构建断环实施了原位返工，并呈报了 [25 号施工汇报](./25_CineRTX%20Phase%2012%20Wave%2012.2-A%20编译断环止血纠偏返工汇报.md)。

协调负责人在 Windows 宿主机上执行了穿透式编译复验，**前序阻断已取得决定性突破**：
1. **`src/core/rational.cpp` 完美通关**：彻底消除了 `Div128PairPair` 拼写错位与 `.high`/`.low` 残留，结构化绑定（`auto [hi, lo]`）完全落地。在宿主机 MSVC 19.51+ `/std:c++latest /W4 /WX` 实机编译中，**达成 0 Error, 0 Warning 纯净通过**；
2. **验证纪律显著改善**：执行工程师吸取了 24 号裁决纪律警示，在 25 号汇报中严格区分了沙箱真实编译与静态审查待测项，杜绝了虚假声称；
3. **元组解包语法倒退彻底消除**：`nv_encoder.cpp` 中散落的 `encoder->std::get` 与 `std::get<0/1/2>` 魔法索引已全量按裁决书改写为结构化绑定具名解包；
4. **格式化清零与环境卫生 100% 保持**：`snprintf` 保持 0 命中，全仓无 `.bak` 垃圾，SPDX 全绿。

全仓统一构建推进至 `[3/35]`，目前仅被 `nv_encoder.cpp` 中最后 1 处微观标准库约束拦截，特呈请专家组终审定夺。

---

## 二、 现场法医级物理断环物证

### 2.1 物理报错日志（`src/shim/loader/nv_encoder.cpp:867`）

```text
C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\include\xmemory(730): error C2672: 'std::construct_at': no matching overloaded function found
C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\include\xutility(633): note: 'std::tuple<NV_ENC_OUTPUT_PTR,ScopedWin32Event,bool,int64_t>::tuple': no appropriate default constructor available
[REDACTED_PATH]\src\shim\loader\nv_encoder.cpp(867): note: see reference to 'std::vector<BitstreamSlotTuple>::resize'
```

### 2.2 ISO C++ 标准病灶剖析

1. **问题成因**：
   在 `src/shim/loader/nv_encoder.cpp:125` 中，自定义智能指针定义为：
   ```cpp
   inline void closeWin32Event(void* h) noexcept { if (h) ::CloseHandle(h); }
   using ScopedWin32Event = std::unique_ptr<void, decltype(&closeWin32Event)>;
   ```
2. **标准禁令**：
   根据 ISO C++ 标准 `[unique.ptr.single.ctor]`，`std::unique_ptr<T, Deleter>` 仅在 `is_default_constructible_v<Deleter>` 为 true 且 `Deleter` **非指针类型** 时，才提供默认构造函数 `constexpr unique_ptr() noexcept`。
   由于其 Deleter 属于函数指针类型 `void(*)(void*) noexcept`，标准显式禁止其默认构造。
3. **连锁反应**：
   `ScopedWin32Event` 无法默认初始化 $\rightarrow$ `BitstreamSlotTuple` 无法默认初始化 $\rightarrow$ 第 867 行 `self->bitstream_pool.resize(pool_cap);` 无法进行值构造，触发 MSVC `error C2672` 拦截。

---

## 三、 提请首席领域专家组终审裁决议题

提请专家组针对 `ScopedWin32Event` 的 Deleter 默认构造性方案进行技术终审：

### 方案 A：C++20 无状态 Lambda Deleter（协调负责人推荐）
直接将普通函数改写为 C++20 内联无状态 Lambda：
```cpp
#if defined(_WIN32)
inline constexpr auto closeWin32Event = [](void* h) noexcept {
    if (h != nullptr) {
        ::CloseHandle(h);
    }
};
using ScopedWin32Event = std::unique_ptr<void, decltype(closeWin32Event)>;
[[nodiscard]] inline ScopedWin32Event makeScopedWin32Event() noexcept {
    return ScopedWin32Event{::CreateEventW(nullptr, FALSE, FALSE, nullptr)};
}
#endif
```
- **核心优势**：
  * C++20 标准规定无状态 Lambda 具备平凡默认构造函数，且其类型属于类类型（Closure Type）而非指针类型，`std::unique_ptr` 天然恢复默认构造；
  * 零开销空基类优化（EBO），`sizeof(ScopedWin32Event) == sizeof(void*)`（8 字节）；
  * **零新增命名类型**，绝对不触发 `cmake/AuditTypes.cmake` 具名类型审计告警；
  * **宿主机实证**：协调负责人已在 MSVC 19.51+ `/std:c++latest /W4 /WX` 下对该代码执行隔离编译测试，`vector<BitstreamSlotTuple>::resize` 100% 成功通过，退出码为 0。

### 方案 B：具名仿函数结构体（Functor Deleter）
```cpp
struct Win32EventDeleter {
    void operator()(void* h) const noexcept {
        if (h != nullptr) { ::CloseHandle(h); }
    }
};
using ScopedWin32Event = std::unique_ptr<void, Win32EventDeleter>;
```
- **考量**：虽然天然可默认构造，但会引入一个新的具名结构体 `struct Win32EventDeleter`，需要补写 `@rtx-type-justification` 契约（Category: `Std-Customization`），增加类型预算负担。

---

## 四、 协调负责人直通放行承诺

一旦首席领域专家组对上述方案出具终审裁决令：
1. 协调负责人将督导执行工程师原位落盘专家范本；
2. 宿主机立刻执行：
   * `cmake --build --preset windows-msvc-ninja`（验证 85/85 目标 MSVC `/W4 /WX` 零警告通过）；
   * `rtx_tests.exe`（验证 147/147 单元测试 100% 全绿）；
3. 构建与单测双绿灯后，立即正式签署 **Wave 12.2-A 封板结案令**，并即刻依据 24 号裁决三大战场规划，下发 Wave 12.2-B 攻坚作战任务！

**特此呈报，提请首席领域专家组出具终局技术裁决！**
