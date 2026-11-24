# CineRTX Phase 12 Wave 12.2-A 无状态 Deleter 常量性剥离返工汇报

- **汇报编号**：`CINERTX-P12-REPORT-031`（注：因 `30_` 编号已由终审裁决书占用，为避免重名覆盖，本汇报顺延归档为 31 号文档）
- **归档路径**：`docs/phase12/31_CineRTX Phase 12 Wave 12.2-A 无状态Deleter常量性剥离返工汇报.md`
- **依据裁决书**：[`30_CineRTX Phase 12 Wave 12.2-A 无状态Deleter常量性剥离终审裁决书暨Wave12.2-B启动令.md`](./30_CineRTX%20Phase%2012%20Wave%2012.2-A%20无状态Deleter常量性剥离终审裁决书暨Wave12.2-B启动令.md)（`CINERTX-P12-RULING-030`）
- **前置汇报**：[`28_CineRTX Phase 12 Wave 12.2-A 异步槽位无状态Deleter返工汇报.md`](./28_CineRTX%20Phase%2012%20Wave%2012.2-A%20异步槽位无状态Deleter返工汇报.md)（`CINERTX-P12-REPORT-028`）
- **改动范围**：1 个源码文件原位替换（`src/shim/loader/nv_encoder.cpp`）；旧版已移出 `CineRTX/` 至仓外 `work/_CineRTX_bak_archive/nv_encoder.cpp.pre12_31.bak`，仓内零 `.bak` 残留。

## 一、 返工修复明细（逐项对标 30 号裁决书 §一）

1. **剥离 `constexpr` 隐式顶层 `const` 限定与三项编译期自证（`src/shim/loader/nv_encoder.cpp` 第 120–139 行）**：
   - 补齐 `#include <type_traits>`，将 `ScopedWin32Event` 更新为：
     ```cpp
     using ScopedWin32Event = std::unique_ptr<void, std::remove_cvref_t<decltype(closeWin32Event)>>;
     static_assert(std::is_default_constructible_v<ScopedWin32Event>);
     static_assert(std::is_move_assignable_v<ScopedWin32Event>);
     static_assert(sizeof(ScopedWin32Event) == sizeof(void*));
     ```
   - 彻底消除 MSVC `error C2280`，保持 8 字节空基类优化（EBO）且新增 **0 个具名类型**。
2. **单步三元条件就地构造（`src/shim/loader/nv_encoder.cpp` 第 879–908 行）**：
   - 将 `rtx_shim_encoder_create()` 中的事件句柄初始化改写为单步三元初始化：
     `const bool want_async = self->asyncRequested();`
     `ScopedWin32Event slot_event = want_async ? makeScopedWin32Event() : ScopedWin32Event{};`
   - 保留中途失败路径立即调用 `nvEncDestroyBitstreamBuffer` 的 0 句柄泄漏回收闭环。

## 二、 验证实证与边界声明

| 验证项 | 验证方式 | 结果 | 说明 |
| :-- | :-- | :-- | :-- |
| `remove_cvref_t` + 3 项 `static_assert` + 移动赋值 + 三元构造 | **Linux GCC 12.2 真编译** (`-std=c++23 -Wall -Wextra -Wpedantic -Werror`) | **PASS（0 错误 0 警告）** | 隔离编译实证 `ScopedWin32Event` 与 `BitstreamSlotTuple` 均满足 `is_default_constructible_v`、`is_move_assignable_v` 与 EBO 8B 布局 |
| `src/shim/loader/nv_encoder.cpp` 全文件 | **静态核验**（沙箱无 Windows/NVENC SDK，未运行 MSVC 编译） | **待 Windows 宿主机 MSVC `/W4 /WX` 实机验证** | 0 处 `std::get`、`BitstreamSlot`、`&closeWin32Event` 或 `.resize(` 残留；仓内 0 个 `.bak` 文件 |

## 三、 宿主机 Wave 12.2-A 封板核验命令

请 Windows 宿主机拉取后执行：
1. `cmake --build --preset windows-msvc-ninja`（核验 85/85 目标 MSVC `/std:c++latest /W4 /WX` 0 错误 0 警告）
2. `.\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe` 与 `Push-Location [REDACTED_PATH]; .\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe; Pop-Location`（核验双向 147/147 PASS）
3. 确认双绿灯后即可封板 Wave 12.2-A，转入 Wave 12.2-B1（`src/shim/ngx_bridge/` 类型清剿）。
