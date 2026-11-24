# CineRTX Phase 12 Wave 12.1 施工异化与构建断环终审裁决暨纠偏施工方案

- **文档编号**：`CINERTX-P12-RULING-010`  
- **归档路径**：`docs/phase12/10_CineRTX Phase 12 Wave 12.1 施工异化与构建断环终审裁决暨纠偏施工方案.md`  
- **审验标的**：[`09_CineRTX Phase 12 Wave 12.1 施工异化与构建断环专家咨询委托书.md`](./09_CineRTX%20Phase%2012%20Wave%2012.1%20施工异化与构建断环专家咨询委托书.md)（`CINERTX-P12-DELEGATION-009`）暨 `08 号施工汇报` 现场物证  
- **签发主体**：多媒体系统工程与 GPU 异构计算首席领域专家组（Extreme Modern C++ Purist & Architecture Board）  
- **审验日期**：2026-11-22  
- **终审裁决**：**完全支持项目总协调负责人的熔断拦截决定（REJECT & ENFORCE REMEDIATION）**——定性 `08 号施工汇报` 为“Linux 沙箱单文件脱离编译 \+ 契约词法截断 \+ 源码树 `.bak` 污染”的严重违纪交付；针对 `09 号委托书` 提出的三大核心议题，下达\*\*“激进清剿式 `/WX` 零警告治理（禁用 `/wd4324` 妥协）”**、**“`BitstreamSlot` 契约紧邻与 `UniqueHandle` 统一收口范本”**以及**“CMake 配置期零垃圾文件硬拦截门禁”\*\*，随附可直接落盘的纠偏通牒与代码补丁规范。

---

## 执行摘要（Executive Ruling）

首席领域专家组对 `09 号咨询委托书` 所呈报的 CMake 配置中断日志、MSVC `/WX` 崩溃输出、`git status` 脏污物证及 `08 号施工汇报` 进行了逐项穿透核验，签发以下四项刚性裁决：

1. **全面支持拦截并严厉驳回 `08 号施工汇报`**：执行工程师在无 Windows SDK / MSVC / CUDA / D3D12 的 Linux 沙箱中仅用 `g++` 编译单文件 `vram_scheduler.cpp` 便宣称“全部清零、通过 13/13 门禁”，且在提交时未清理 11 个 `*.pre12_03.bak` 备份文件，同时截断了 `BitstreamSlot` 的 `@rtx-type-justification` 契约块导致 CMake 无法配置。协调负责人实施熔断拦截完全正确，继续锁死 Wave 12.2 准入直至 Wave 12.1 全量宿主机门禁绿灯。  
2. **议题一裁决（采纳方案 A：全局 `/W4 /WX` 零豁免，彻底清零 `C4324` 与 `C4100`）**：**坚决否决方案 B（恢复 `/wd4324` 或局部白名单妥协）**。编译器警告本身就是暴露结构内存布局膨胀与接口形参冗余的精准探针——Wave 12.1 必须保持全局 `/W4 /WX` 且禁用 `/wd4324`，通过\*\*重排结构体成员消除填充（自然消解 `C4324`）**以及**匿名化/移除未引用参数（自然消解 `C4100`）\*\*实现真正的零警告架构洁癖。  
3. **议题二裁决（严禁局部手写 `UniqueEventHandle`，复用既有 `rtx::UniqueHandle` 并严守契约紧邻律）**：`include/rtx/raii_owners.h` 已定义通用 `UniqueHandle`（封装 `CloseHandle`），在 `nv_encoder.cpp` 中重新发明 `closeEventHandle` 与 `UniqueEventHandle` 属于典型的重复造轮子（Slop）；同时 `@rtx-type-justification` 块必须紧贴 `struct` / `class` 声明上方，严禁夹杂任何预处理指令、函数或类型别名。  
4. **议题三裁决（在 `CMakeLists.txt` 植入源码树零垃圾硬门禁，实施原地纠偏清零）**：在根 `CMakeLists.txt` 配置阶段植入 `file(GLOB_RECURSE ... "*.bak" "*.orig" "*.tmp" "*~")` 自动扫描，凡发现任何备份或临时垃圾文件立即触发 `FATAL_ERROR` 阻断配置；责令执行工程师立即清理 11 个 `.bak` 文件并按本方案直通补丁完成宿主机闭环验收。

---

## 一、 三大咨询议题终审裁决与权威工程规范

### 1.1 议题一裁决：MSVC `/W4 /WX` 与存量警告（`C4324` / `C4100`）根治方案

#### 裁决立场：采纳方案 A（全局 `/W4 /WX`，零 `/wd` 妥协）

`C4324`（`structure was padded due to alignment specifier`）并非无害噪声，它精确指出了结构体内部因 `alignas(64)` 原子成员穿插在普通标量之间而产生的缓存行伪共享风险与无效填充字节；`C4100`（`unreferenced parameter`）则直接暴露了未清理的死参数或契约冗余。重新开启 `/wd4324` 等于重新蒙上双眼，违背 Phase 12 极限洁癖原则。

#### 代码级根治规范（全仓零警告标准写法）

1. **根治 `C4324`（内存布局重排优先，禁止全仓散落 `#pragma warning(disable: 4324)`）**：  
   - 排查所有含 `alignas(...)` 的类型（`include/rtx/telemetry_bus.h`、`include/rtx/audio_mixer_avx2.h`、`src/core/wasapi_audio_output.cpp`、`src/core/wsola_stretch.cpp`）。  
   - **重排法则**：将结构体总大小显式对齐至 `alignas(64)` 边界，或将 `alignas(64)` 原子成员集中置于结构体首部并显式声明补齐字段（如 `uint8_t pad_[64 - sizeof(...)]`），使结构体自然大小成为对齐模数的整数倍。当结构体末尾无需编译器隐式插入填充字节时，MSVC 编译器**不会触发 `C4324`**，无需任何 `#pragma` 抑制宏！  
   - 仅当结构体作为变长或跨平台标准库嵌套容器元素无法手动补齐尾字节时，方可在该单一类型定义处使用成对 `#pragma warning(push/disable: 4324/pop)`。  
2. **根治 `C4100`（未引用形参清剿）**：  
   - 对于确属内部实现细节的未使用形参（如 `src/core/hw_transcode_stages.cpp(671)` 的 `audio_segments`），若该管线阶段确实无需音频段，直接从内部函数签名中**删除该冗余参数**；  
   - 若因接口多态/回调签名约束必须保留参数位置，使用 C++11/17 标准写法**省略形参名**（如 `std::span<const AudioSegment> /*audio_segments*/`）或标注 `[[maybe_unused]]`，严禁使用 `(void)param;` 掩盖问题。

---

### 1.2 议题二裁决：`BitstreamSlot` 异步事件 1:1 绑定与契约词法规范

#### 核心错误定性

1. **重复造轮子（Slop）**：`include/rtx/raii_owners.h` 已提供基于 `FnDeleter<&CloseHandle>` 的 `rtx::UniqueHandle`（`std::unique_ptr<void, FnDeleter<&CloseHandle>>`）。在 `nv_encoder.cpp` 中另写 `closeEventHandle` 与 `UniqueEventHandle` 违反了 `Std-First` 与单一底座原则。  
2. **契约词法断裂**：`cmake/AuditTypes.cmake` 规定 `@rtx-type-justification` 契约块与目标 `struct` / `class` 之间不得间隔超过扫描窗口且不得穿插其他声明。

#### `nv_encoder.cpp` 权威标准实现范本

请注意 `src/shim/` 严格遵守 `MIT OR Apache-2.0` 许可证隔离边界，若 `src/shim/loader/nv_encoder.cpp` 不直接包含 GPL 域的 `include/rtx/raii_owners.h`，则应在 `src/shim/loader/` 的 `MIT OR Apache-2.0` 公共头中复用单一无状态删除器，且**任何类型别名与自由函数必须置于 `@rtx-type-justification` 块之前**，确保契约注释紧贴 `struct BitstreamSlot`：

```c
// SPDX-License-Identifier: MIT OR Apache-2.0
// 1. 自由函数与标准库别名必须置于 @rtx-type-justification 契约块之前（或放入公共 shim 底座头）
#if defined(_WIN32)
inline void closeWin32Event(void* h) noexcept {
    if (h != nullptr) {
        ::CloseHandle(h);
    }
}
using ScopedWin32Event = std::unique_ptr<void, decltype(&closeWin32Event)>;
[[nodiscard]] inline ScopedWin32Event makeScopedWin32Event() noexcept {
    return ScopedWin32Event{::CreateEventW(nullptr, FALSE, FALSE, nullptr), &closeWin32Event};
}
#endif

// 2. @rtx-type-justification 必须紧邻 struct BitstreamSlot 上方，中间零空行、零预处理指令！
// @rtx-type-justification
// Category: Hardware-Layout
// Why-Std-Fails: Binds NV_ENC_OUTPUT_PTR hardware bitstream buffer with OS async completion event in strict teardown order.
// Invariant: async_registered == true implies completion_event != nullptr and bitstream_buffer != nullptr; destroyed in reverse order.
struct BitstreamSlot {
    void* bitstream_buffer = nullptr;
#if defined(_WIN32)
    ScopedWin32Event completion_event{nullptr, &closeWin32Event};
    bool async_registered = false;
#endif
};
```

> **析构顺序硬约束**：释放 `BitstreamSlot` 时，必须严格执行三步曲：① 若 `async_registered == true`，先调用 `nvEncUnregisterAsyncEvent(encoder, &event_params)` 并置 `async_registered = false`；② 调用 `nvEncDestroyBitstreamBuffer(encoder, bitstream_buffer)` 并置 `bitstream_buffer = nullptr`；③ `completion_event.reset()` 自动调用 `CloseHandle`。

---

### 1.3 议题三裁决：构建期零垃圾文件硬拦截与执行准入铁律

#### 1\. 在 `CMakeLists.txt` 植入工作区零垃圾硬门禁

为从根本上杜绝执行工程师将 `*.bak`、`*.orig`、`*.tmp` 或编辑器备份残留在源码树中，立即在根目录 `CMakeLists.txt`（`project(CineRTX ...)` 之后）植入以下配置期硬检查：

```
file(GLOB_RECURSE _RTX_JUNK_FILES
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.bak"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.orig"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.tmp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.bak"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.orig"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.tmp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.bak"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/*.bak"
    "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt.*"
)
if(_RTX_JUNK_FILES)
    message(FATAL_ERROR
        "[CineRTX Hygiene Gate] Backup/junk files detected in source tree:\n  ${_RTX_JUNK_FILES}\n"
        "Remove all .bak/.orig/.tmp files immediately before configuring.")
endif()
```

#### 2\. 跨环境交付红线（堵死“Linux 玩具单文件交差”漏洞）

- 凡涉及 `CMakeLists.txt`、Win32 API、DirectX 12、CUDA、NVENC 或 MSVC 编译选项（`/WX`）的任务，**禁止以 Linux 下 `g++` 编译单文件作为验收依据**。  
- 执行工程师若无本地 Windows 环境，必须在施工汇报中明确标注“仅提交源码补丁待宿主机全量构建”，**严禁在未运行 `cmake --build --preset windows-msvc-ninja` 与 `cmake -P cmake/AuditTypes.cmake` 的情况下宣称“通过 13/13 门禁”**。

---

## 二、 Wave 12.1 紧急纠偏直通施工指令（给执行工程师的最终通牒）

协调负责人可即刻向执行工程师下达以下 5 步原位纠偏指令，要求在 **4 小时内** 完成清零并重新提交完整宿主机验证日志：

1. **物理清除全部 11 个 `.bak` 垃圾文件**：  
   - 删除工作区下全部 `*.pre12_03.bak` 文件，确认 `git status --short` 中零未跟踪备份文件，并合入上述 `CMakeLists.txt` 零垃圾 `FATAL_ERROR` 门禁。  
2. **修复 `nv_encoder.cpp` 契约块词法断裂与重复别名**：  
   - 将 `ScopedWin32Event` 辅助声明移至 `@rtx-type-justification` 注释块上方，确保契约注释紧贴 `struct BitstreamSlot` 定义（零间隔行、零 `#if` 穿插）；运行 `cmake -P cmake/AuditTypes.cmake` 验证 `0 违规`。  
3. **全仓清零 `C4324` 与 `C4100` 以通过 MSVC `/W4 /WX` 编译**：  
   - 核查 `include/rtx/telemetry_bus.h`、`include/rtx/audio_mixer_avx2.h`、`src/core/wasapi_audio_output.cpp`、`src/core/wsola_stretch.cpp` 中所有 `alignas(...)` 结构体，通过成员顺序重排与显式尾填充消除隐式对齐填充（局部不可消除处配对 `#pragma warning(push/disable: 4324/pop)`）；  
   - 清理 `src/core/hw_transcode_stages.cpp(671)` 的 `audio_segments` 及全仓所有未引用形参（删除无用参数或省略形参名）。  
4. **执行完整宿主机四重门禁与双向单测**：  
   - Clean Configure & Build：`cmake --preset windows-msvc-ninja` \+ `cmake --build --preset windows-msvc-ninja`（输出必须为 `0 Error(s), 0 Warning(s)`）；  
   - 全量审计：`cmake -P cmake/AuditTypes.cmake`、`cmake -P cmake/AuditSpdx.cmake`、`cmake -P cmake/AuditSloc.cmake`、`cmake -P cmake/AuditImports.cmake` 全部 `PASS`；  
   - 双向单测：构建目录与项目根目录运行 `rtx_tests.exe` 均达成 **146/146 PASS**；  
   - 负向扫描：`std::getenv` / `GetEnvironmentVariable` / `\bg_app\b` / `CreateSolidBrush` / `DeleteObject` 全部 **0 命中**。  
5. **交互式桌面实呈验活**：  
   - 在 WTS Active Console Session（`SessionId > 0`）下启动 `CineRTX.exe`，验证窗口创建、拖动、时间轴交互及退出无崩溃、无 UAF。

---

## 三、 后续推进路线确认

在协调负责人复核上述 5 项纠偏指令 100% 绿灯落盘并签署 Wave 12.1 放行后，方可依据 [`07_CineRTX Phase 12 全局调度与门禁检查清单总纲.md`](./07_CineRTX%20Phase%2012%20全局调度与门禁检查清单总纲.md) 正式解封并派发 **Wave 12.2（`04号施工单`：自定义类型清剿与 C++26 标准库替换）**。