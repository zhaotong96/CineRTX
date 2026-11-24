# **CineRTX Phase 12 Wave 12.2-A 异步槽位 Deleter 默认构造性终审裁决书暨 Wave 12.2-B 推进路线**

* **文档编号**：`CINERTX-P12-RULING-027`  
* **归档路径**：`docs/phase12/27_CineRTX Phase 12 Wave 12.2-A 异步槽位Deleter默认构造性终审裁决书暨Wave12.2-B推进路线.md`  
* **审验标的**：  
  * `docs/phase12/26_CineRTX Phase 12 Wave 12.2-A 异步槽位Deleter默认构造性专家咨询委托书.md`（`CINERTX-P12-DELEGATION-026`）  
  * `docs/phase12/25_CineRTX Phase 12 Wave 12.2-A 编译断环止血纠偏返工汇报.md`（`CINERTX-P12-REPORT-025`）  
  * 仓库实体源码核验：`src/shim/loader/nv_encoder.cpp`（第 120–137 行、第 865–915 行及全文件）与 `src/core/rational.cpp`  
* **前序依据**：`04_CineRTX Phase 12 SuperPower 施工单_自定义类型清剿与C++26标准库替换.md`（`CINERTX-P12-WORKORDER-004`）、`24_CineRTX Phase 12 Wave 12.2-A 编译断环与类型清剿纠偏终审裁决书.md`（`CINERTX-P12-RULING-024`）  
* **签发主体**：多媒体系统工程与 GPU 异构计算首席领域专家组（Extreme Modern C++ Purist & Architecture Board）  
* **审验日期**：2026-11-23  
* **终审裁决**：**全面批准方案 A（C++20 无状态 Lambda Deleter），并追加"构造即初始化（Reserve \+ Emplace）"零默认构造改进（APPROVED FOR IMMEDIATE WAVE 12.2-A SEAL & WAVE 12.2-B LAUNCH）**——正式核准 `25/26 号文档` 中已落地的阶段性成果（`src/core/rational.cpp` MSVC `/W4 /WX` 零警告通关、全仓旧式格式化 0 命中、首轮 27 个被动类型清剿、零 `.bak` 卫生）；确认 `nv_encoder.cpp` 中函数指针删除器导致 `std::unique_ptr` 失去默认构造能力的 ISO C++ 标准根因，下达"方案 A（C++20 无状态 Lambda Deleter）+ `reserve()` \+ `emplace_back()` 一步到位构造"双保险标准代码范本，并核发 Wave 12.2-A 封板直通令与 Wave 12.2-B（128 个具名类型 → ≤ 35 个、Pure SLOC ≤ 23,000 行）作战路线。

# **执行摘要（Executive Ruling）**

首席领域专家组对 `26 号咨询委托书` 所呈报的 MSVC 19.51+ 编译报错日志及仓库实体源码 src/shim/loader/nv\_encoder.cpp 进行了逐行穿透复核，签发以下四项终审裁决：

1. **Wave 12.2-A 前序成果复核结论（VERIFIED PASS）**：核准 `src/core/rational.cpp` 128 位算术标准库重构已在 Windows 宿主机 MSVC `/std:c++latest /W4 /WX` 下实现 0 错误 0 警告编译通过；确认 `nv_encoder.cpp` 已将全部 6 处元组槽位访问升级为结构化绑定具名解包，彻底清除了 `encoder->std::get` 语法倒退与魔法索引；确认全仓旧式格式化（`snprintf` / `_snwprintf` / `ostringstream`）保持 0 命中且仓内零 `.bak` 残留。  
2. **核心技术裁决：全面批准方案 A（C++20 无状态 Lambda Deleter），坚决否决方案 B（具名仿函数）**：  
   * **ISO C++ 标准法理依据**：根据 ISO C++ `[unique.ptr.single.ctor]`，当 `Deleter` 为原始函数指针类型 `void(*)(void*) noexcept` 时，`std::unique_ptr<T, D>` 显式禁用默认构造函数（防止空指针删除器引发未定义释放行为）。而 C++20 赋予无捕获 Lambda（Stateless Lambda）平凡默认构造函数与可默认构造的闭包类型（Closure Type），使 `std::unique_ptr<void, decltype(closeWin32Event)>` 天然满足 `std::default_initializable` 约束。  
   * **架构洁癖与门禁契合度**：方案 A 触发零开销空基类优化（EBO，`sizeof(ScopedWin32Event) == sizeof(void*) == 8` 字节），且**不产生任何具名 `struct` / `class`**，零消耗 Wave 12.2 极为宝贵的 `≤ 35` 具名类型配额；方案 B（`struct Win32EventDeleter`）无端增加 1 个具名类型及 4 行契约注释，违背奥卡姆剃刀原则，予以否决。  
3. **现代 C++ 性能与语义加码（`reserve` \+ `emplace_back` 消除冗余默认构造）**：在 `rtx_shim_encoder_create()`（`nv_encoder.cpp:867`）初始化环形缓冲池时，除采用方案 A 使类型本身具备默认构造性外，同步将 `self->bitstream_pool.resize(pool_cap);` 升级为 `self->bitstream_pool.reserve(pool_cap);` \+ 循环内 `emplace_back(...)`，彻底避免先默认构造空槽位再逐字段回填的两阶段初始化反模式（践行 RAII 单步构造铁律）。  
4. **Wave 12.2-A 直通封板与 Wave 12.2-B 推进授权**：协调负责人督导合入上述代码修改、在 Windows 宿主机确认 MSVC `/W4 /WX` 85/85 目标 0 警告通过及 `rtx_tests.exe` 147/147 全绿后，**可直接签署 Wave 12.2-A 结案封板，并立即解封启动 Wave 12.2-B（剩余 93 个类型清剿至 ≤ 35 个、Pure SLOC 压减至 ≤ 23,000 行）**。

# **一、 ISO C++ 标准机制分析与方案对比裁决**

## **1.1 `nv_encoder.cpp` 编译断环的底层类型学根因**

穿透查看仓库实体文件 `src/shim/loader/nv_encoder.cpp` 第 120–137 行与第 865–908 行：

* 第 125 行当前定义：`using ScopedWin32Event = std::unique_ptr<void, decltype(&closeWin32Event)>;`，其第二模板参数为函数指针类型 `void(*)(void*) noexcept`；  
* 第 133 行定义：`using BitstreamSlotTuple = std::tuple<NV_ENC_OUTPUT_PTR, ScopedWin32Event, bool, int64_t>;`；  
* 第 867 行调用：`self->bitstream_pool.resize(pool_cap);` 要求元素类型 `BitstreamSlotTuple` 满足 `DefaultConstructible`（要求每个成员均可默认初始化）。由于 `ScopedWin32Event` 的删除器是函数指针，标准库为防止调用空函数指针而删除了 `std::unique_ptr` 的默认构造函数，进而导致 `std::tuple` 默认构造函数被隐式弃置（deleted），触发 MSVC `error C2672: 'std::construct_at'`。

## **1.2 候选方案多维对比终审表**

| 评估维度 | 方案 A：C++20 无状态 Lambda Deleter（批准采纳） | 方案 B：具名仿函数 struct Win32EventDeleter（否决） |
| :---- | :---- | :---- |
| **默认构造性（`DefaultConstructible`）** | **原生支持**（C++20 `[expr.prim.lambda.closure]` 规定无捕获 Lambda 具备平凡默认构造函数，且为非指针闭包类型） | 原生支持 |
| **内存体积与 ABI 开销** | **8 字节**（触发空基类优化 EBO，与裸指针零额外开销） | 8 字节（EBO） |
| **具名类型配额占用（`AuditTypes`）** | **0 个新增具名类型**（完全不占用 `≤ 35` 稀缺预算） | **\+1 个具名类型**（需额外编写 `@rtx-type-justification` 契约块） |
| **KISS 与代码简洁度** | **极简**（3 行内联常量表达式闭包，声明即定义，无需维护独立结构体） | 冗余（重复造标准库已支持的无状态闭包轮子） |
| **宿主编译实证状态** | 已在 MSVC 19.51+ `/std:c++latest /W4 /WX` 隔离实测 0 警告通过（退出码 0） | 未采纳 |

# **二、 `src/shim/loader/nv_encoder.cpp` 权威标准修复范本**

## **2.1 声明区修改（替换 `src/shim/loader/nv_encoder.cpp` 第 120–137 行）**

将普通自由函数 `closeWin32Event` 升级为 `inline constexpr` 无状态 Lambda，使 `ScopedWin32Event` 天然具备零开销默认构造函数，并保持跨平台 `BitstreamSlotTuple` 声明简洁一致：

```
// src/shim/loader/nv_encoder.cpp (Lines 120-136)
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
using BitstreamSlotTuple = std::tuple<NV_ENC_OUTPUT_PTR, ScopedWin32Event, bool, int64_t>;
#else
using BitstreamSlotTuple = std::tuple<NV_ENC_OUTPUT_PTR, void*, bool, int64_t>;
#endif
```

*(注：同步检查文件中任何残留的 `ScopedWin32Event{nullptr, &closeWin32Event}` 显式双参构造写法；由于 `closeWin32Event` 已非函数指针，默认构造直接使用 `ScopedWin32Event{}` 即可。经核查当前文件中已无多余双参默认构造调用。)*

## **2.2 初始化区优化（升级 `src/shim/loader/nv_encoder.cpp` 第 867–905 行 `resize` 为 `reserve` \+ `emplace_back`）**

为贯彻现代 C++ RAII"构造成熟对象、杜绝先空构造再回填"的设计洁癖，将 `rtx_shim_encoder_create()` 中的环形池构建改写为 `reserve` \+ `emplace_back` 一步到位构造：

```
// src/shim/loader/nv_encoder.cpp (Lines 864-908)
const size_t pool_cap = static_cast<size_t>(
    (std::max)(4, 4 + static_cast<int>(encode_config.frameIntervalP) +
                      static_cast<int>(encode_config.rcParams.lookaheadDepth)));
self->bitstream_pool.clear();
self->bitstream_pool.reserve(pool_cap);
for (size_t i = 0; i < pool_cap; ++i) {
    NV_ENC_CREATE_BITSTREAM_BUFFER buf{};
    buf.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
    status = self->api.nvEncCreateBitstreamBuffer(self->session, &buf);
    if (status != NV_ENC_SUCCESS) {
        self->last_error = self->nvError("NvEncCreateBitstreamBuffer", status);
        rtx_shim_encoder_destroy(self);
        return mapNvencStatus(status);
    }
#if defined(_WIN32)
    ScopedWin32Event slot_event{};
    bool slot_registered = false;
    if (self->asyncRequested()) {
        slot_event = makeScopedWin32Event();
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
}
```

**安全性增强说明**：上述实现中，若在 makeScopedWin32Event() 或 nvEncRegisterAsyncEvent() 失败时提前返回，当前迭代尚未入队的 buf.bitstreamBuffer 会立即调用 `nvEncDestroyBitstreamBuffer` 精准回收，随后 `rtx_shim_encoder_destroy(self)` 清理此前已成功入队的 `0..i-1` 号槽位，实现中途失败路径 **0 显存句柄泄漏**。

# **三、 Wave 12.2-A 收网验收与 Wave 12.2-B 推进路线图**

## **3.1 Wave 12.2-A 直通封板核验步骤**

1. 执行工程师在 `src/shim/loader/nv_encoder.cpp` 合入第二节标准代码（确保旧版仍归档于仓外，仓内 0 个 `.bak` 文件）；  
2. Windows 宿主机执行全量构建与双向单元测试：

```
cmake --build --preset windows-msvc-ninja
.\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe
Push-Location [REDACTED_PATH]; .\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe; Pop-Location
```

   * 确认 MSVC `/std:c++latest /W4 /WX` **85/85 目标 0 错误 0 警告**；  
   * 确认双向运行 `rtx_tests.exe` 均达成 **147/147 PASS**；  
3. 达成上述双绿灯后，协调负责人可直接归档 Wave 12.2-A 验收通过记录，无需再次呈报中间委托，立即转入 Wave 12.2-B 攻坚。

## **3.2 Wave 12.2-B 剩余 93 个类型清剿与 Pure SLOC 攻坚指标分解**

当前全仓尚余 **128 个具名类型**（其中 42 个已标注类型超 `≤ 35` 上限 7 个，86 个待重构违规类型）与 32,360 行 Pure SLOC。Wave 12.2-B 严格按以下三波次压减预算推进，确保最终满足 `04 号施工单` 全部硬门禁：

| 攻坚批次 | 靶向模块范围 | 核心标准库替代动作 | 具名类型预期变化 | Pure SLOC 预期变化 |
| :---- | :---- | :---- | :---- | :---- |
| **12.2-B1** | `src/shim/ngx_bridge/`（约 25 个内部类型） | 内部参数包、纹理视图、光学流/RIFE 配置袋全面替换为 `std::tuple`（结构化绑定）、`std::span<const std::byte>` 与 C++23 `std::mdspan`；仅留存 `ngx_bridge_abi.h` C-ABI 结构体与 `RtxNgxBridge` 状态机。 | `128 → ~103` | `32,360 → ~29,200` |
| **12.2-B2** | `src/io/` 与 `src/core/playback_session*`（约 24 个类型） | 物理拔除无多态必要的 `IDemuxer` / `IMuxer` 虚基类接口壳，直调 `FfmpegDemuxer` / `FfmpegMuxer`；将播放控制请求、帧描述符与音视频状态收敛为 `std::variant` \+ `std::expected` \+ `std::tuple`。 | `103 → ~79` | `29,200 → ~26,000` |
| **12.2-B3** | `src/core/`（NLE/队列/几何）与 `src/gui/` 剩余包装袋（约 45+ 个类型） | 将 7 个多态虚基类编辑命令重构为单一值语义 `std::variant` \+ `std::visit` 撤销/重做执行器；将超配的 `Domain-Model` 数据袋收敛为 `std::tuple` / `std::flat_map`；剪除内部非空引用契约下的冗余 `if (!ptr)` 防御分支与重复辅助逻辑。 | **`79 → ≤ 35`**（硬达标） | **`26,000 → ≤ 23,000`**（硬达标） |

