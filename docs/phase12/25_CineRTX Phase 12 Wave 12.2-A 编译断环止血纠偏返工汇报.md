# CineRTX Phase 12 Wave 12.2-A 编译断环止血纠偏返工汇报

- **汇报编号**：`CINERTX-P12-REPORT-025`
- **归档路径**：`docs/phase12/25_CineRTX Phase 12 Wave 12.2-A 编译断环止血纠偏返工汇报.md`
- **依据裁决书**：[`24_CineRTX Phase 12 Wave 12.2-A 编译断环与类型清剿纠偏终审裁决书.md`](./24_CineRTX%20Phase%2012%20Wave%2012.2-A%20编译断环与类型清剿纠偏终审裁决书.md)（`CINERTX-P12-RULING-024`）
- **前置汇报**：[`22_CineRTX Phase 12 Wave 12.2-A 自定义类型清剿与标准库格式化提纯施工汇报.md`](./22_CineRTX%20Phase%2012%20Wave%2012.2-A%20自定义类型清剿与标准库格式化提纯施工汇报.md)（`CINERTX-P12-REPORT-022`）
- **改动范围**：2 个源码文件原位替换（`src/core/rational.cpp`、`src/shim/loader/nv_encoder.cpp`）；旧版均已移出 `CineRTX/` 至仓外 `work/_CineRTX_bak_archive/`，仓内零 `.bak` 残留。

## 一、 返工修复明细（逐项对标 24 号裁决书）

### 1. 议题一：`src/core/rational.cpp` 128 位无溢出算术标准库重构
- **修复内容**：
  - 彻底清除拼写断层 `Div128PairPair` 与残留 `.high` / `.low` 成员访问；补齐 `<type_traits>` 与 `<utility>` 头文件。
  - 在 `rtx::detail` 中按裁决书 §1.2 落地标准别名 `using U128 = std::pair<uint64_t, uint64_t>;` 与 `using Div128Result = std::pair<U128, uint64_t>;`，实现 `constexpr multiply128`（MSVC `_umul128` / `__int128` / 便携 32×32 回退）与 `constexpr divide128`。
  - 在 `rescaleToFlicks` 与 `FlicksDriftCompensator::rescaleToFlicks` 全面改用 C++17/23 结构化绑定（`auto [scaled_hi, scaled_lo] = ...`、`const auto [quotient, next_remainder] = ...`、`const auto [q_hi, q_lo] = quotient`），保留 64 位商饱和语义与进位传播。
- **负向扫描**：`Div128Pair` / `.high` / `.low` 在 `src/core/rational.cpp` 中 **0 命中**。

### 2. 议题二：`src/shim/loader/nv_encoder.cpp` 异步环形池 4 元组化与确定性三步逆序释放
- **修复内容**：
  - 彻底清除未替换的 `std::vector<BitstreamSlot> bitstream_pool;` 以及 7 处 `encoder->std::get<...>(bitstream_pool[...])` 语法错误，并消灭全文件所有 `std::get<0/1/2/3>` 魔法索引。
  - 按裁决书 §2.2 定义 4 元组别名 `using BitstreamSlotTuple = std::tuple<NV_ENC_OUTPUT_PTR, ScopedWin32Event, bool, int64_t>;`（非 `_WIN32` 分支第二槽为 `void*`），将 `NV_ENC_EVENT_PARAMS` 移至注册/注销调用栈帧局部构造，不再冗余驻留元组。
  - 统一在全部 6 处访问点（`waitForSlot`、编码提交、`nvEncLockBitstream`、`rtx_shim_encoder_create` 槽位初始化、`rtx_shim_encoder_destroy`、`rtx_shim_encoder_flush` EOS 提交与排空循环）使用结构化绑定 `auto& [bitstream, event_handle, async_registered, submit_pts] = ...;`。
  - 在 `rtx_shim_encoder_destroy` 严格固化三步逆序析构：① `nvEncUnregisterAsyncEvent`（局部构造 `NV_ENC_EVENT_PARAMS`）→ ② `nvEncDestroyBitstreamBuffer` → ③ `event_handle.reset()`；各分支未用解包变量均显式 `(void)` 消除 MSVC `/W4 /WX` `C4189` 告警。
- **附带核查**：同步复扫 `src/shim/loader/nv_decoder.cpp`，确认 `->std::get` / `.std::get` / 残留 `Slot` 均为 **0 命中**。
- **负向扫描**：`BitstreamSlot` / `std::get` / `->std::get` 在 `src/shim/loader/nv_encoder.cpp` 中 **0 命中**。

## 二、 实证验证清单（严格区分真实编译与静态审查）

遵守 24 号裁决书 §四.1 验证纪律红线，明确区分本地 Linux 沙箱已实测编译项与仅静态审查项：

| 文件 / 门禁项 | 验证方式 | 验证结果 | 说明 |
| :-- | :-- | :-- | :-- |
| `src/core/rational.cpp` | **Linux GCC 12.2 真编译** (`-std=c++23 -Wall -Wextra -Wpedantic -Werror`) | **PASS（0 错误 0 警告）** | 使用沙箱 `/tmp/gcc12_shim/` 临时 `<format>` 与 `<libavutil/mathematics.h>` 桩头（未入仓） |
| `tests/test_flicks_timebase.cpp` | **Linux GCC 12.2 真编译** (`-std=c++23 -Wall -Wextra -Werror`) | **PASS（0 错误 0 警告）** | 验证与 `rational.cpp` 头文件与符号契约一致 |
| `tests/test_flicks_semantics.cpp` | **Linux GCC 12.2 真编译** (`-std=c++23 -Wall -Wextra -Werror`) | **PASS（0 错误 0 警告）** | 验证与 `rational.cpp` / `flicks.h` 契约一致 |
| `src/shim/loader/nv_encoder.cpp` | **静态语法与负向正则审查**（无 Windows/NVENC SDK 头文件，未执行 MSVC 编译） | **静态核验通过，待 Windows 宿主机 MSVC `/W4 /WX` 实机编译** | 0 处 `BitstreamSlot` / `std::get` / `->std::get`，全部 6 处访问点均已人工逐行核对结构化绑定与 `(void)` 防 `C4189` |
| `OldFormat` (`snprintf`/`_snwprintf`/`ostringstream`) | 全仓 `include/` & `src/` 静态扫描 | **0 命中（PASS）** | 保持 Wave 12.2-A 清零成果 |
| `AuditSpdx` & 仓内 `.bak` 卫生 | 全仓静态扫描 | **0 缺失 / 0 `.bak` 残留（PASS）** | 旧版 `rational.cpp`、`nv_encoder.cpp` 已移至 `work/_CineRTX_bak_archive/` |

## 三、 宿主机下一步复验建议

请宿主机拉取本批两处定点修复后执行：
1. `cmake --preset windows-msvc-ninja && cmake --build --preset windows-msvc-ninja`（验证 85/85 目标 MSVC `/W4 /WX` 零警告通过）
2. `ctest --preset windows-msvc-ninja --output-on-failure`（验证 147/147 单测全绿）
3. 宿主机确认编译与单测全绿后，即可按 24 号裁决书 §三 三大战场顺序启动 Wave 12.2-B（`src/shim/ngx_bridge/` → `src/io/` 与 `playback_session` → `src/core/` & `src/gui/` 收口至 `≤ 35` 类型与 `≤ 23,000` Pure SLOC）。
