# CineRTX Phase 12 Wave 12.2-A 无状态Deleter常量性剥离与全量封板专家咨询委托书

- **文档编号**：`CINERTX-P12-DELEGATION-029`
- **归档路径**：[`docs/phase12/29_CineRTX Phase 12 Wave 12.2-A 无状态Deleter常量性剥离与全量封板专家咨询委托书.md`](./29_CineRTX%20Phase%2012%20Wave%2012.2-A%20无状态Deleter常量性剥离与全量封板专家咨询委托书.md)
- **呈报主体**：CineRTX 项目总协调负责人（Coordinator）
- **受呈报方**：多媒体系统工程与 GPU 异构计算首席领域专家组（Extreme Modern C++ Purist & Architecture Board）
- **当前状态**：🟡 **Wave 12.2-A 攻坚达成 99%（`C2672` 默认构造错误已彻底消灭），提请专家组对 `const` 闭包常量性剥离与最终封板裁决（FINAL CONST-CLOSURE & SEAL）**
- **审验标的**：
  * [`27_CineRTX Phase 12 Wave 12.2-A 异步槽位Deleter默认构造性终审裁决书暨Wave12.2-B推进路线.md`](./27_CineRTX%20Phase%2012%20Wave%2012.2-A%20异步槽位Deleter默认构造性终审裁决书暨Wave12.2-B推进路线.md)（`CINERTX-P12-RULING-027`）
  * [`28_CineRTX Phase 12 Wave 12.2-A 异步槽位无状态Deleter返工汇报.md`](./28_CineRTX%20Phase%2012%20Wave%2012.2-A%20异步槽位无状态Deleter返工汇报.md)（`CINERTX-P12-REPORT-028`）
  * 宿主机 MSVC 19.51+ `/W4 /WX` 真实编译器日志与 `src/shim/loader/nv_encoder.cpp`
- **基线标的**：Commit `02694fb2`（Wave 12.1 封板基线）

---

## 一、 委托背景与重大攻坚进展

依据首席领域专家组签发的 [27 号终审裁决书](./27_CineRTX%20Phase%2012%20Wave%2012.2-A%20异步槽位Deleter默认构造性终审裁决书暨Wave12.2-B推进路线.md)，执行工程师实施了针对 [nv_encoder.cpp](../../src/shim/loader/nv_encoder.cpp) 的定点返工，并呈报了 [28 号施工汇报](./28_CineRTX%20Phase%2012%20Wave%2012.2-A%20异步槽位无状态Deleter返工汇报.md)。

协调负责人在 Windows 宿主机上执行了穿透式 MSVC 实机编译复验，**前序核心阻断已取得根本性消除**：
1. **原 `C2672` 默认构造断环彻底根除**：落地方案 A（C++20 无状态 Lambda）与 `reserve()` + `emplace_back()` 单步就地构造后，原先阻断在第 867 行的 `std::construct_at` 模板实例化失败完全清零；
2. **显存句柄零泄漏闭环落地**：在 `makeScopedWin32Event` 与 `nvEncRegisterAsyncEvent` 失败路径上，成功落地 `nvEncDestroyBitstreamBuffer` 抢先回收，实现 0 句柄泄漏；
3. **`rational.cpp` 与全仓格式化持续绿灯**：128 位无溢出算术在 MSVC 19.51+ `/W4 /WX` 下保持 0 错误 0 警告；全仓旧式格式化（`snprintf` / `_snwprintf` / `ostringstream`）持续 0 命中；仓内零 `.bak` 垃圾，SPDX 全绿。

全仓构建目前推进至 `[2/34]`，仅在 `nv_encoder.cpp:879` 遭遇因 `constexpr` 带来的隐式 `const` 闭包导致的移动赋值语义阻断，特呈请专家组进行最终技术裁决。

---

## 二、 现场法医级物理断环物证

### 2.1 物理报错日志（`src/shim/loader/nv_encoder.cpp:879`）

```text
CMakeFiles\rtx_shim_static.dir\src\shim\loader\nv_encoder.cpp(879): error C2280: 'std::unique_ptr<void,const <lambda>@closeWin32Event> &std::unique_ptr<void,const <lambda>@closeWin32Event>::operator =(const std::unique_ptr<void,const <lambda>@closeWin32Event> &)': attempting to reference a deleted function
C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\include\memory(3512): note: see declaration of 'std::unique_ptr<void,const <lambda>@closeWin32Event>::operator ='
C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\include\memory(3512): note: function was explicitly deleted
```

### 2.2 ISO C++ 标准病灶与类型学根因剖析

1. **病灶代码定位**：
   - 声明区（第 120–125 行）：
     ```cpp
     inline constexpr auto closeWin32Event = [](void* h) noexcept { if (h) ::CloseHandle(h); };
     using ScopedWin32Event = std::unique_ptr<void, decltype(closeWin32Event)>;
     ```
   - 使用区（第 876–879 行）：
     ```cpp
     ScopedWin32Event slot_event{};
     bool slot_registered = false;
     if (self->asyncRequested()) {
         slot_event = makeScopedWin32Event(); // <-- 触发 C2280
     ```
2. **C++ 语言标准层面的深层机理**：
   - 根据 ISO C++ 标准，`constexpr` 修饰的变量默认具备常量性（`const` 限定）。因此 `decltype(closeWin32Event)` 推导出的实际闭包类型为 **`const <lambda_closure_type>`**。
   - 在 `std::unique_ptr<void, const Deleter>` 中，删除器成员具备 `const` 限定，由于常量对象在 C++ 中不可被赋值（`is_move_assignable_v<const Deleter>` 为 `false`），标准库 SFINAE 禁用了移动赋值运算符 `operator=(unique_ptr&&)`。
   - 当第 879 行执行赋值 `slot_event = makeScopedWin32Event();` 时，无法匹配移动赋值，回退匹配被弃置（`= delete`）的拷贝赋值运算符，引发 MSVC `C2280` 报错。

---

## 三、 提请首席领域专家组终审裁决议题

提请专家组对彻底消灭该类型学阻断的双保险方案出具终审技术裁决：

### 3.1 议题一：类型别名剥离常量性（`std::remove_cvref_t`）
在声明区第 125 行，显式剥离推导类型的 `const` 限定：
```cpp
#if defined(_WIN32)
inline constexpr auto closeWin32Event = [](void* h) noexcept {
    if (h != nullptr) {
        ::CloseHandle(h);
    }
};
using ScopedWin32Event = std::unique_ptr<void, std::remove_cvref_t<decltype(closeWin32Event)>>;
[[nodiscard]] inline ScopedWin32Event makeScopedWin32Event() noexcept {
    return ScopedWin32Event{::CreateEventW(nullptr, FALSE, FALSE, nullptr)};
}
using BitstreamSlotTuple = std::tuple<NV_ENC_OUTPUT_PTR, ScopedWin32Event, bool, int64_t>;
#else
using BitstreamSlotTuple = std::tuple<NV_ENC_OUTPUT_PTR, void*, bool, int64_t>;
#endif
```
- **实测物证**：协调负责人在宿主机隔离环境下实机验证，`static_assert(std::is_move_assignable_v<ScopedWin32Event>)` 100% 成立，编译退出码为 `0`。

### 3.2 议题二：初始化单步三元就地构造（消除不必要的赋值操作）
在初始化区第 876–880 行，废除先默认构造再赋值的两步写法，改为单步条件构造：
```cpp
#if defined(_WIN32)
ScopedWin32Event slot_event = self->asyncRequested() ? makeScopedWin32Event() : nullptr;
bool slot_registered = false;
if (self->asyncRequested()) {
    if (slot_event == nullptr) {
        (void)self->api.nvEncDestroyBitstreamBuffer(self->session, buf.bitstreamBuffer);
        self->last_error = "CreateEventW failed for NVENC bitstream slot";
        rtx_shim_encoder_destroy(self);
        return RTX_ERR_INTERNAL;
    }
    // ... 后续逻辑无缝衔接 ...
```
- **架构收益**：贯彻 RAII 声明即初始化，从源码结构上彻底消除对赋值运算符的调用。

### 3.3 议题三：Wave 12.2-A 正式结案封板令与 Wave 12.2-B 派发令
提请专家组在核准上述 2 处修正范本的同时，直接核发 **Wave 12.2-A 封板结案令**，并批准即刻启动 **Wave 12.2-B（按 27 号裁决三大战场执行 93 个类型清剿至 ≤ 35 个、Pure SLOC 压减至 ≤ 23,000 行）**。

---

## 四、 协调负责人直通放行承诺

一旦首席领域专家组下发终审裁决令：
1. 协调负责人将督导执行工程师原位落盘上述修正；
2. 宿主机现场执行：
   * `cmake --build --preset windows-msvc-ninja`（验证 85/85 目标 MSVC `/W4 /WX` 零警告零错误通关）；
   * `rtx_tests.exe`（验证 147/147 单元测试 100% PASS）；
3. 达成全仓全绿后，即刻正式宣布 Wave 12.2-A 圆满收官，并全速派发 Wave 12.2-B1 作战任务！

**特此呈报，提请首席领域专家组终审裁决！**
