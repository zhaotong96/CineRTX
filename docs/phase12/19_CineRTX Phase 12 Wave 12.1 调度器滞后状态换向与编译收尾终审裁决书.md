# CineRTX Phase 12 Wave 12.1 调度器滞后状态换向与编译收尾终审裁决书

- **文档编号**：`CINERTX-P12-RULING-019`  
- **归档路径**：`docs/phase12/19_CineRTX Phase 12 Wave 12.1 调度器滞后状态换向与编译收尾终审裁决书.md`  
- **审验标的**：[`18_CineRTX Phase 12 Wave 12.1 调度器滞后状态换向与编译收尾专家咨询委托书.md`](./18_CineRTX%20Phase%2012%20Wave%2012.1%20调度器滞后状态换向与编译收尾专家咨询委托书.md)（`CINERTX-P12-DELEGATION-018`）  
- **签发主体**：多媒体系统工程与 GPU 异构计算首席领域专家组（Extreme Modern C++ Purist & Architecture Board）  
- **审验日期**：2026-11-22  
- **终审裁决**：**全面签发终局修复规范与 Wave 12.1 最终封板放行令（FINAL CLOSURE & WAVE 12.2 RELEASE AUTHORIZATION）**——以奥卡姆剃刀原则确立 `VramScheduler` **“有符号单计数器（Signed `int32_t` Dwell Counter）”** 极简无枚举实现（零新增类型），同时下达 `nv_encoder.cpp(542)` 物理覆写与 `rtx_gui.cpp(516)` 死局部变量移除指令，合入跑通 147/147 单测后直接签署 Wave 12.1 封板并解封 Wave 12.2。

---

## 执行摘要（Executive Ruling）

首席领域专家组审阅了 `18 号咨询委托书` 所呈报的 `test_vram_scheduler` 失败日志及剩余两处代码断点，作出如下终审裁决：

1. **议题一裁决（否决新增 `PressureDirection` 枚举，采用有符号单计数器 `int32_t dwell_frames_` 极简状态机）**：  
   - **奥卡姆剃刀批判**：为表达“高压降档计数 / 死区归零 / 低压升档计数”三态而额外发明 `enum class PressureDirection` 违背“如无必要，勿增实体”。  
   - **最优标准解法**：将 `dwell_frames_` 设为**有符号 32 位整数 `int32_t dwell_frames_ = 0`**——正数表示连续处于高压降级区的帧数（`+1, +2, ..., +120`），负数表示连续处于低压恢复区的帧数（`-1, -2, ..., -120`），处于死区（`recover_pct < usage < degrade_pct`）或压力方向发生反转时直接归零 `dwell_frames_ = 0`！既零新增自定义类型，又以单一标量在编译期和运行期 100% 杜绝高压残留帧跨入低压恢复计数的语义漏洞。  
2. **议题二裁决（`nv_encoder.cpp(542)` 物理落地与 `rtx_gui.cpp(516)` 死代码删除，禁止 `[[maybe_unused]]` 掩盖）**：  
   - 严厉批评执行工程师未核对磁盘文件 mtime 即声称修复 `nv_encoder.cpp(542)` 的行为，责令立即覆写为 `RTX_ERR_INTERNAL` 并验证文件落盘；  
   - 对于 `rtx_gui.cpp(516)` 声明后未使用的 `WorkstationApp& app = *app_ptr;`，**严禁使用 `[[maybe_unused]]` 保留无用死变量**，直接物理删除该未引用局部变量声明（若仅需判空检查，直接 `if (!app_ptr) return ...;` 即可）。  
3. **Wave 12.1 最终收网授权**：合入上述三处代码修改并在 Windows 宿主机确认 MSVC `/W4 /WX` 85/85 目标 0 警告编译通过与 `rtx_tests.exe` **147/147 PASS** 后，协调负责人直接归档 Wave 12.1 封板记录并解封派发 **Wave 12.2（`04号施工单`）**。

---

## 一、 议题一终审裁决：`VramScheduler` 有符号单计数器防抖状态机权威代码

### 1.1 数学与状态不变量证明

设当前显存预算为 \$B \= \\text{budget\_bytes}\$，当前占用为 \$U \= \\text{used\_bytes}\$，高水位百分比阈值为 \$P\_{\\text{high}}\$（默认 \$82\$），低水位百分比阈值为 \$P\_{\\text{low}}\$（默认 \$68\$），最小驻留周期为 \$N \= \\text{min\_dwell\_frames}\$（默认 \$120\$）。

定义单一有符号状态变量 \$d \= \\text{dwell\_frames\_} \\in \[-N, \+N\]\$：

- **高压区间（\$U \\times 100 \\ge B \\times P\_{\\text{high}}\$）**：  
  - 若当前已是最低降级档（`PassThrough_NoFruc`），无法继续降档，保持 \$d \= 0\$（绝不累积正向计数，防止切入低压后产生状态污染）；  
  - 否则，若 \$d \< 0\$（此前处于低压累积），方向发生反转，先重置 \$d \= 0\$ 再步进为 \$d \= 1\$（即 \$d \= \\max(d, 0\) \+ 1\$）；  
  - 当 \$d \\ge N\$ 时，触发向下一级平滑降档，并立即重置 \$d \= 0\$，返回 `true`。  
- **低压区间（\$U \\times 100 \\le B \\times P\_{\\text{low}}\$）**：  
  - 若当前已是最高画质档（`VsrFirst_RifeFull`），无法继续升档，保持 \$d \= 0\$；  
  - 否则，若 \$d \> 0\$（此前处于高压累积），方向发生反转，先重置 \$d \= 0\$ 再步进为 \$d \= \-1\$（即 \$d \= \\min(d, 0\) \- 1\$）；  
  - 当 \$d \\le \-N\$ 时，触发向上一级平滑恢复，并立即重置 \$d \= 0\$，返回 `true`。  
- **中间滞后死区（\$B \\times P\_{\\text{low}} \< U \\times 100 \< B \\times P\_{\\text{high}}\$）或无效预算（\$B \== 0\$）**：  
  - 立即清零 \$d \= 0\$，返回 `false`（彻底消除临界点抖动累积）。

### 1.2 权威标准代码范本（`include/rtx/vram_scheduler.h` & `src/core/vram_scheduler.cpp`）

```c
// src/core/vram_scheduler.cpp
#include "rtx/vram_scheduler.h"
#include <algorithm>
#include <cstdint>

namespace rtx {

namespace {
[[nodiscard]] constexpr bool exceedsPercent(uint64_t used, uint64_t budget, uint32_t pct) noexcept {
#if defined(__SIZEOF_INT128__)
    return static_cast<__uint128_t>(used) * 100u >= static_cast<__uint128_t>(budget) * pct;
#else
    const uint64_t q = budget / 100u;
    const uint64_t r = budget % 100u;
    return used >= q * pct + (r * pct) / 100u;
#endif
}

[[nodiscard]] constexpr bool belowOrEqualPercent(uint64_t used, uint64_t budget, uint32_t pct) noexcept {
#if defined(__SIZEOF_INT128__)
    return static_cast<__uint128_t>(used) * 100u <= static_cast<__uint128_t>(budget) * pct;
#else
    const uint64_t q = budget / 100u;
    const uint64_t r = budget % 100u;
    return used <= q * pct + (r * pct) / 100u;
#endif
}
} // namespace

bool VramAdaptiveScheduler::observeFrame(uint64_t budget_bytes, uint64_t used_bytes) noexcept {
    if (budget_bytes == 0 || min_dwell_frames_ == 0) {
        dwell_frames_ = 0;
        return false;
    }

    const int32_t target_dwell = static_cast<int32_t>(
        std::min<uint32_t>(min_dwell_frames_, 1'000'000u));

    // 1. 高压降级判定（> high watermark）
    if (exceedsPercent(used_bytes, budget_bytes, degrade_pct_)) {
        if (current_tier_ == TopologyTier::PassThrough_NoFruc) {
            dwell_frames_ = 0;
            return false;
        }
        dwell_frames_ = (dwell_frames_ > 0) ? (dwell_frames_ + 1) : 1;
        if (dwell_frames_ >= target_dwell) {
            current_tier_ = static_cast<TopologyTier>(
                static_cast<uint8_t>(current_tier_) + 1u);
            dwell_frames_ = 0;
            return true;
        }
        return false;
    }

    // 2. 低压恢复判定（<= low watermark）
    if (belowOrEqualPercent(used_bytes, budget_bytes, recover_pct_)) {
        if (current_tier_ == TopologyTier::VsrFirst_RifeFull) {
            dwell_frames_ = 0;
            return false;
        }
        dwell_frames_ = (dwell_frames_ < 0) ? (dwell_frames_ - 1) : -1;
        if (dwell_frames_ <= -target_dwell) {
            current_tier_ = static_cast<TopologyTier>(
                static_cast<uint8_t>(current_tier_) - 1u);
            dwell_frames_ = 0;
            return true;
        }
        return false;
    }

    // 3. 滞后死区（recover_pct_ < usage < degrade_pct_）：严格重置计数器防跨区污染
    dwell_frames_ = 0;
    return false;
}

} // namespace rtx
```

*(注：`include/rtx/vram_scheduler.h` 中同步将 `dwell_frames_` 类型声明为 `int32_t dwell_frames_ = 0;`，且在 `reset()` 或 `selectInitialTier()` 切换层级时同步执行 `dwell_frames_ = 0;`。)*

---

## 二、 议题二终审裁决：最后两处源码物理落盘与死代码清除

### 2.1 `src/shim/loader/nv_encoder.cpp(542)` 物理覆写核验

- 打开磁盘实体文件 `src/shim/loader/nv_encoder.cpp` 第 542 行，将残留的 `RTX_ERR_STATE` 物理替换为 `RTX_ERR_INTERNAL`：

```c
if (bitstream_pool.empty()) {
    return fail(RTX_ERR_INTERNAL, "NVENC bitstream ring pool is empty");
}
```

- 保存后通过 `Get-Item src/shim/loader/nv_encoder.cpp | Select-Object LastWriteTime` 核验时间戳已更新，且全仓 `rg "RTX_ERR_STATE" include/ src/` 返回 **0 命中**。

### 2.2 `src/gui/rtx_gui.cpp(516)` 物理删除死局部变量（禁用 `[[maybe_unused]]`）

- `WorkstationApp& app = *app_ptr;` 在当前分支中未被引用，属于纯粹冗余变量声明。**严禁**添加 `[[maybe_unused]]` 保留死代码，直接删除 `WorkstationApp& app = *app_ptr;` 这一行（保留 `if (!app_ptr) return ...;` 边界守护即可）。

---

## 三、 Wave 12.1 最终收网与 Wave 12.2 派发指令

1. **执行工程师立即合入以上三处修复**（`vram_scheduler.h/.cpp`、`nv_encoder.cpp:542`、`rtx_gui.cpp:516`）；  
2. **宿主机一键构建与双向单测终验**：

```
cmake --build --preset windows-msvc-ninja
.\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe
Push-Location [REDACTED_PATH]; .\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe; Pop-Location
```

   - 确认 MSVC `/W4 /WX` **85/85 目标 0 错误 0 警告**；  
   - 确认双向运行 `rtx_tests.exe` 均达成 **147/147 PASS**（`vram_scheduler_five_tier_degradation_and_120f_hysteresis` 绿灯通过）；  
3. **签署 Wave 12.1 封板并即刻启动 Wave 12.2**：协调负责人归档 Wave 12.1 验收记录后，立即下发 [`04_CineRTX Phase 12 SuperPower 施工单_自定义类型清剿与C++26标准库替换.md`](./04_CineRTX%20Phase%2012%20SuperPower%20施工单_自定义类型清剿与C++26标准库替换.md)，全面推进具名类型清剿（157 → ≤ 35 个）与 Pure SLOC 瘦身（≤ 23,000 行）！