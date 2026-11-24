# **CineRTX Phase 12 Wave 12.2-A 无状态 Deleter 常量性剥离终审裁决书暨 Wave 12.2-B 启动令**

| 属性 Key | 元数据明细 Content Metadata |
| :---- | :---- |
| **文档编号** | `CINERTX-P12-RULING-030` |
| **归档路径** | `docs/phase12/30_CineRTX Phase 12 Wave 12.2-A 无状态Deleter常量性剥离终审裁决书暨Wave12.2-B启动令.md` |
| **审验标的** | `docs/phase12/29_CineRTX Phase 12 Wave 12.2-A 无状态Deleter常量性剥离与全量封板专家咨询委托书.md` (`CINERTX-P12-DELEGATION-029`)`docs/phase12/28_CineRTX Phase 12 Wave 12.2-A 异步槽位无状态Deleter返工汇报.md` (`CINERTX-P12-REPORT-028`)仓库实体源码核验：`src/shim/loader/nv_encoder.cpp`（第 120–136 行、第 875–905 行） |
| **前序依据** | `27_CineRTX Phase 12 Wave 12.2-A 异步槽位Deleter默认构造性终审裁决书暨Wave12.2-B推进路线.md` (`CINERTX-P12-RULING-027`)`04_CineRTX Phase 12 SuperPower 施工单_自定义类型清剿与C++26标准库替换.md` (`CINERTX-P12-WORKORDER-004`) |
| **签发主体** | 多媒体系统工程与 GPU 异构计算首席领域专家组（Extreme Modern C++ Purist & Architecture Board） |
| **审验日期** | 2026-11-23 |
| **终审裁决** | **全票批准三大终审裁决议题并核发 Wave 12.2-A 封板直通令暨 Wave 12.2-B 启动令（APPROVED FOR WAVE 12.2-A FINAL SEAL & WAVE 12.2-B1 LAUNCH）**——正式核准 `std::remove_cvref_t<decltype(closeWin32Event)>` 剥离 `constexpr` 隐式 `const` 闭包限定（恢复 `std::unique_ptr` 完整可移动构造与可移动赋值能力）、核准单步三元条件就地构造消除冗余赋值、并直接授权宿主机双绿灯验证后无需再报中间委托即刻封板 Wave 12.2-A、全速开启 Wave 12.2-B1。 |

# **执行摘要（Executive Ruling）**

首席领域专家组审阅了 `29 号咨询委托书` 呈报的 MSVC 19.51+ `error C2280` 物理报错日志及类型学根因分析，确认 `C2672` 默认构造错误已彻底消灭，剩余的 `C2280` 源于 ISO C++ 对 `constexpr` 变量的隐式 `const` 限定传播至 `decltype(closeWin32Event)`。专家组签发如下三项终审裁决：

1. **议题一裁决（全面批准 `std::remove_cvref_t<decltype(closeWin32Event)>` 剥离常量性）**：  
   * **ISO C++ 法理机制**：在 C++17/20/23 中，`inline constexpr auto f = [](...) noexcept { ... };` 声明的变量具备 `const` 限定，故 `decltype(f)` 为 `const <closure_type>`。将其直接传入 `std::unique_ptr<T, D>` 会使内部存储的删除器成员成为 `const D`，从而导致 `std::is_move_assignable_v<D>` 为 `false`，SFINAE 弃置 `std::unique_ptr::operator=(unique_ptr&&)`。使用 C++20 标准特征 **`std::remove_cvref_t<decltype(closeWin32Event)>`** 精准剥离顶层 `const`，既保留 `inline constexpr` 闭包的零开销与默认可构造性（EBO，8 字节，0 新增具名类型），又完整恢复 `std::is_move_assignable_v<ScopedWin32Event> == true`。  
   * **架构延展约束**：同步将此模式确立为全仓无状态 Lambda 删除器的标准规范（未来在 `include/rtx/raii_owners.h` 或其他模块使用 Lambda 删除器时，一律配合 `std::remove_cvref_t<decltype(fn)>`）。  
2. **议题二裁决（全面批准单步三元条件就地构造，践行零冗余赋值 RAII 洁癖）**：  
   * 批准将 `nv_encoder.cpp:876–880` 改写为单步三元初始化 `ScopedWin32Event slot_event = self->asyncRequested() ? makeScopedWin32Event() : ScopedWin32Event{};`，在源码结构上彻底消灭两阶段默认构造后再赋值的反模式，保持中途失败路径 `nvEncDestroyBitstreamBuffer` 的 0 句柄泄漏回收闭环。  
3. **议题三裁决（直接核发 Wave 12.2-A 结案封板授权暨 Wave 12.2-B1 启动令）**：  
   * 协调负责人督导合入上述两处定型代码后，在 Windows 宿主机实测确认 MSVC `/std:c++latest /W4 /WX` 85/85 目标 0 错误 0 警告编译通过，且双向运行 `rtx_tests.exe` 达成 **147/147 PASS**，**无需再次呈报中间委托，直接签署 Wave 12.2-A 结案封板，并即刻下发 Wave 12.2-B1（`src/shim/ngx_bridge/` 类型清剿）施工任务**！

# **一、 `src/shim/loader/nv_encoder.cpp` 终审定型代码范本**

## **1.1 声明区定型代码（替换 `src/shim/loader/nv_encoder.cpp` 第 120–135 行）**

```
// src/shim/loader/nv_encoder.cpp (Lines 120-135)
#if defined(_WIN32)
inline constexpr auto closeWin32Event = [](void* h) noexcept {
    if (h != nullptr) {
        ::CloseHandle(h);
    }
};
using ScopedWin32Event = std::unique_ptr<void, std::remove_cvref_t<decltype(closeWin32Event)>>;
static_assert(std::is_default_constructible_v<ScopedWin32Event>);
static_assert(std::is_move_assignable_v<ScopedWin32Event>);
static_assert(sizeof(ScopedWin32Event) == sizeof(void*));

[[nodiscard]] inline ScopedWin32Event makeScopedWin32Event() noexcept {
    return ScopedWin32Event{::CreateEventW(nullptr, FALSE, FALSE, nullptr)};
}
using BitstreamSlotTuple = std::tuple<NV_ENC_OUTPUT_PTR, ScopedWin32Event, bool, int64_t>;
#else
using BitstreamSlotTuple = std::tuple<NV_ENC_OUTPUT_PTR, void*, bool, int64_t>;
#endif
```

* **编译期自证**：三行 `static_assert` 直接锁死 `ScopedWin32Event` 的可默认构造性、可移动赋值性与 8 字节空基类优化（EBO）布局不变量，零运行时开销。

## **1.2 初始化区定型代码（替换 `src/shim/loader/nv_encoder.cpp` 第 875–905 行）**

```
// src/shim/loader/nv_encoder.cpp (Lines 875-905)
#if defined(_WIN32)
    const bool want_async = self->asyncRequested();
    ScopedWin32Event slot_event = want_async ? makeScopedWin32Event() : ScopedWin32Event{};
    bool slot_registered = false;
    if (want_async) {
        if (slot_event == nullptr) {
            (void)self->api.nvEncDestroyBitstreamBuffer(self->session, buf.bitstreamBuffer);
            self->last_error = "CreateEventW failed for NVENC bitstream slot";
            rtx_shim_encoder_destroy(self);
            return RTX_ERR_INTERNAL;
        }
        NV_ENC_EVENT_PARAMS ev{};
        ev.version = NV_ENC_EVENT_PARAMS_VER;
        ev.completionEvent = slot_event.get();
        status = self->api.nvEncRegisterAsyncEvent(self->session, &ev);
        if (status != NV_ENC_SUCCESS) {
            (void)self->api.nvEncDestroyBitstreamBuffer(self->session, buf.bitstreamBuffer);
            self->last_error = self->nvError("NvEncRegisterAsyncEvent", status);
            rtx_shim_encoder_destroy(self);
            return mapNvencStatus(status);
        }
        slot_registered = true;
    }
    self->bitstream_pool.emplace_back(buf.bitstreamBuffer, std::move(slot_event), slot_registered, 0);
#else
    self->bitstream_pool.emplace_back(buf.bitstreamBuffer, nullptr, false, 0);
#endif
```

# **二、 Wave 12.2-A 收官核验与 Wave 12.2-B1 立即派发清单**

## **2.1 Wave 12.2-A 最终收网步骤（宿主机执行）**

1. 合入第一节两处定型代码（确认仓内 0 个 `.bak` 文件，旧版本统一归档于仓外 `work/_CineRTX_bak_archive/`）；  
2. 在 Windows 宿主机执行全量编译与双向单元测试：

```
cmake --build --preset windows-msvc-ninja
.\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe
Push-Location [REDACTED_PATH]; .\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe; Pop-Location
```

3. 核验 **85/85 目标 `0 Error(s), 0 Warning(s)`** 且双目录 **147/147 PASS** 后，协调负责人直接归档 Wave 12.2-A 封板记录。

## **2.2 Wave 12.2-B1（第一战场：`src/shim/ngx_bridge/`）立即启动指令**

Wave 12.2-A 封板后，无需等待额外请示，协调负责人立即向执行工程师下达 **Wave 12.2-B1 作战指令**：

* **靶向范围**：`src/shim/ngx_bridge/`（当前约 25 个内部自定义类型）及 `include/shim/ngx_bridge_abi.h`（严守 `MIT OR Apache-2.0` 许可证头）；  
* **核心动作**：  
  1. 物理清剿 `ngx_bridge` 内部所有被动配置袋、缓冲区描述符、纹理视图与光流/RIFE 内部参数传递结构体，全面替换为 **`std::tuple`**（结构化绑定解包）、**`std::span<const std::byte>`** 与 C++23 **`std::mdspan`**（二维步幅平面视图）；  
  2. 仅允许跨 `extern "C"` 二进制接口的 `ngx_bridge_abi.h` 结构体（`C-ABI`）与核心桥接状态机 `RtxNgxBridge`（`NonTrivial-StateMachine`）存续；  
  3. 预期指标收敛：全仓具名自定义类型从 `128` 个压减至 **`~103` 个**，Pure SLOC 从 `32,360` 行压减至 **`~29,200` 行**，随后依次推进 `12.2-B2`（`src/io/` 拔除虚接口壳 \+ `playback_session`）与 `12.2-B3`（`core`/`gui` 收口至 `≤ 35` 类型与 `≤ 23,000` Pure SLOC）。

