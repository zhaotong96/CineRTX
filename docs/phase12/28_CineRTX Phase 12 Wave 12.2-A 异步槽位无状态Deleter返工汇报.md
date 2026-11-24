# CineRTX Phase 12 Wave 12.2-A 异步槽位无状态 Deleter 返工汇报

- **汇报编号**：`CINERTX-P12-REPORT-028`
- **归档路径**：`docs/phase12/28_CineRTX Phase 12 Wave 12.2-A 异步槽位无状态Deleter返工汇报.md`
- **依据裁决书**：[`27_CineRTX Phase 12 Wave 12.2-A 异步槽位Deleter默认构造性终审裁决书暨Wave12.2-B推进路线.md`](./27_CineRTX%20Phase%2012%20Wave%2012.2-A%20异步槽位Deleter默认构造性终审裁决书暨Wave12.2-B推进路线.md)（`CINERTX-P12-RULING-027`）
- **前置汇报**：[`25_CineRTX Phase 12 Wave 12.2-A 编译断环止血纠偏返工汇报.md`](./25_CineRTX%20Phase%2012%20Wave%2012.2-A%20编译断环止血纠偏返工汇报.md)（`CINERTX-P12-REPORT-025`）
- **改动范围**：1 个源码文件原位替换（`src/shim/loader/nv_encoder.cpp`）；旧版已移出 `CineRTX/` 至仓外 `work/_CineRTX_bak_archive/nv_encoder.cpp.pre12_28.bak`，仓内零 `.bak` 残留。

## 一、 返工修复明细（逐项对标 27 号裁决书 §二）

### 1. 方案 A 落地：C++20 无状态 Lambda Deleter（`src/shim/loader/nv_encoder.cpp` 第 119–133 行）
- 将 `closeWin32Event` 由普通自由函数指针升级为 `inline constexpr` 无状态 Lambda 闭包：
  ```cpp
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
  ```
- **工程效果**：满足 ISO C++20 `std::default_initializable<ScopedWin32Event>` 与 `std::default_initializable<BitstreamSlotTuple>`，触发空基类优化（EBO，`sizeof(ScopedWin32Event) == sizeof(void*) == 8` 字节），且新增 **0 个具名类型**（不消耗 `AuditTypes ≤ 35` 配额）。

### 2. 单步构造与零泄漏收口：`reserve` + `emplace_back`（`src/shim/loader/nv_encoder.cpp` 第 864–904 行）
- 在 `rtx_shim_encoder_create()` 中废除 `self->bitstream_pool.resize(pool_cap);` 两阶段回填写法，升级为 `self->bitstream_pool.clear(); self->bitstream_pool.reserve(pool_cap);` 与循环内 `emplace_back(buf.bitstreamBuffer, std::move(slot_event), slot_registered, 0)`（非 `_WIN32` 分支同步使用 `emplace_back(buf.bitstreamBuffer, nullptr, false, 0)`）。
- 在 `makeScopedWin32Event()` 或 `nvEncRegisterAsyncEvent()` 失败提前返回路径上，于调用 `rtx_shim_encoder_destroy(self)` 前立即执行 `(void)self->api.nvEncDestroyBitstreamBuffer(self->session, buf.bitstreamBuffer);`，彻底消除中途失败路径的显存比特流句柄泄漏。

## 二、 验证实证与边界声明

严格遵守验证纪律红线，明确区分已实测验证项与仅静态审查项：

| 验证项 | 验证方式 | 验证结果 | 说明 |
| :-- | :-- | :-- | :-- |
| 无状态 Lambda Deleter 类型特征与 EBO | **Linux GCC 12.2 真编译** (`-std=c++23 -Wall -Wextra -Wpedantic -Werror`) | **PASS（0 错误 0 警告）** | 隔离编译验证 `std::default_initializable<ScopedWin32Event>`、`std::default_initializable<BitstreamSlotTuple>`、`sizeof(ScopedWin32Event) == sizeof(void*)` 及 `reserve()` + `emplace_back()` 全通 |
| `src/shim/loader/nv_encoder.cpp` 完整单元 | **静态审查 + 负向正则扫描**（沙箱无 Windows/NVENC SDK，未运行 MSVC 编译） | **静态核验通过，待宿主机 MSVC `/W4 /WX` 实机验证** | `&closeWin32Event`、`bitstream_pool.resize`、`BitstreamSlot`、`std::get` 均 **0 命中** |
| 全仓 `OldFormat` / `AuditSpdx` / 仓内 `.bak` 卫生 | 全仓静态扫描 | **PASS（0 命中 / 0 缺失 / 0 `.bak` 残留）** | 具名类型数维持 128（0 新增） |

## 三、 宿主机 Wave 12.2-A 直通封板核验命令

请 Windows 宿主机拉取更新后按 27 号裁决书 §3.1 执行：
1. `cmake --build --preset windows-msvc-ninja`（核验 85/85 目标 MSVC `/std:c++latest /W4 /WX` 0 错误 0 警告）
2. `.\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe` 与 `Push-Location [REDACTED_PATH]; .\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe; Pop-Location`（核验双向 147/147 PASS）
3. 确认双绿灯封板 Wave 12.2-A 后，按 27 号裁决书 §3.2 三波次（`12.2-B1` `ngx_bridge` → `12.2-B2` `io` & `playback_session` → `12.2-B3` `core` & `gui`）推进 Wave 12.2-B。
