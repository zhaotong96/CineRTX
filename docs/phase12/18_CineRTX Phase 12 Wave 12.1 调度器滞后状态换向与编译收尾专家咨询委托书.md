# CineRTX Phase 12 Wave 12.1 调度器滞后状态换向与编译收尾专家咨询委托书

- **文档编号**：`CINERTX-P12-DELEGATION-018`
- **归档路径**：[`docs/phase12/18_CineRTX Phase 12 Wave 12.1 调度器滞后状态换向与编译收尾专家咨询委托书.md`](./18_CineRTX%20Phase%2012%20Wave%2012.1%20调度器滞后状态换向与编译收尾专家咨询委托书.md)
- **呈报主体**：CineRTX 项目总协调负责人（Coordinator）
- **受呈报方**：多媒体系统工程与 GPU 异构计算首席领域专家组（Extreme Modern C++ Purist & Architecture Board）
- **当前状态**：🟡 **Wave 12.1 达到 99%（146/147 单测通过），提请状态机换向滞后不变量与最终封板裁决（FINAL INVARIANT CLOSURE）**
- **审验标的**：
  * [`16_CineRTX Phase 12 Wave 12.1 GUI上下文传递与ABI错误码终审裁决书.md`](./16_CineRTX%20Phase%2012%20Wave%2012.1%20GUI上下文传递与ABI错误码终审裁决书.md)（`CINERTX-P12-RULING-016`）
  * [`17_CineRTX Phase 12 Wave 12.1 终局收敛施工汇报_GUI上下文显式传参与ABI错误码修正.md`](./17_CineRTX%20Phase%2012%20Wave%2012.1%20终局收敛施工汇报_GUI上下文显式传参与ABI错误码修正.md)（`CINERTX-P12-REPORT-017`）
- **基线上下文**：Windows 宿主机实测，四重 CMake 静态审计全绿，全仓 `getenv` 0 命中，MSVC 零警告编译打通，147 项单测中 146 项通过，仅 1 项调度器滞后单测红灯。

---

## 一、 委托背景与重大突破进展

在首席领域专家组签发 [16 号裁决书](./16_CineRTX%20Phase%2012%20Wave%2012.1%20GUI上下文传递与ABI错误码终审裁决书.md) 后，执行工程师呈报了 [17 号施工汇报](./17_CineRTX%20Phase%2012%20Wave%2012.1%20终局收敛施工汇报_GUI上下文显式传参与ABI错误码修正.md)。

协调负责人在 Windows 宿主机上执行了法医级穿透实测，**取得重大突破**：
1. **GUI 语法断环彻底消除**：[workstation_canvas.h](../../src/gui/workstation_canvas.h) 与 [workstation_canvas.cpp](../../src/gui/workstation_canvas.cpp) 全量规范为显式非空引用 `WorkstationApp&` 传参，顺利生成目标；
2. **四重静态审计 100% 全绿**：`AuditTypes`（157 类型全部具备合法证明）、`AuditSpdx`、`AuditSloc`、`AuditImports` 全部 PASS；
3. **单元测试 146/147 通过**：绝大部分管线、NGX、编解码与算子回归全部绿灯。

但实测现场锁定了最后 3 处实体残余，尤其是状态机逻辑断环，特呈请专家组出具终审修复代码范本。

---

## 二、 现场法医级物理报错物证

### 2.1 阻断点一：`vram_scheduler` 单计数器换向漏重置（逻辑断环）

- **测试报错物证**：
  ```
  [ RUN      ] vram_scheduler_five_tier_degradation_and_120f_hysteresis
  [  FAILED  ] vram_scheduler_five_tier_degradation_and_120f_hysteresis
               tests\test_vram_scheduler.cpp:50: CHECK failed: !sched.observeFrame(4000ull * kMiB, 500ull * kMiB)
               tests\test_vram_scheduler.cpp:52: CHECK failed: sched.observeFrame(4000ull * kMiB, 500ull * kMiB)
  [==========] 147 test(s) run, 1 failed (81201 ms)
  ```
- **病灶根因**：
  在 `src/core/vram_scheduler.cpp` 中，工程师将双计数器简化为单一 `dwell_frames_` 时，写成了：
  ```cpp
  if (dwell_frames_ < (std::numeric_limits<uint32_t>::max)()) {
      ++dwell_frames_;
  }
  if (dwell_frames_ < min_dwell_frames_) {
      return false;
  }
  // 达到 120 帧且高压时降档并置 0；达到 120 帧且低压时升档并置 0
  ```
  该实现导致前序已处于最低档（`PassThrough_NoFruc`）的高压残留帧数（如 5 帧）未在状态换向时被清零，直接跨越计入了后续低压恢复的 120 帧计数中，导致在第 114 帧就提前发生了恢复升档，破坏了严格的 120 帧滞后防抖不变量。

---

### 2.2 阻断点二：磁盘实体未真实落盘与新增未引用变量告警

1. **`nv_encoder.cpp(542)` 虚报落盘**：17 号汇报声称已修正为 `RTX_ERR_INTERNAL`，但磁盘实体文件行仍为 `return fail(RTX_ERR_STATE, ...);`，文件 mtime 停留在前期，导致重新构建时立刻报 `C2065`；
2. **`rtx_gui.cpp(516)` 未引用变量**：声明 `WorkstationApp& app = *app_ptr;` 后未在后续分支引用，触发 MSVC `/WX` 门禁拦截（`warning C4189: 'app': local variable is initialized but not referenced`）。

---

## 三、 提请首席领域专家组裁决的核心议题

### 3.1 咨询议题一：`VramScheduler` 单计数器防抖状态机权威规范

- **求解诉求**：
  * 在严格遵守“消除双计数器冗余（HYG-P12-03）”的前提下，如何最简、最高效地表达高低压双向滞后？
  * 是否引入一个极简枚举 `enum class PressureDirection : uint8_t { None, Degrading, Recovering };`，当压力落入死区（既非高压又非低压）或方向发生反转时，强制重置 `dwell_frames_ = 0`？
  * 请专家组出具经严格推演的 `VramScheduler::observeFrame` 权威代码范本。

---

### 3.2 咨询议题二：最后两处源码补丁标准收口

- **求解诉求**：
  * 确认将 [nv_encoder.cpp:542](../../src/shim/loader/nv_encoder.cpp#L542) 物理覆写落盘为 `RTX_ERR_INTERNAL`；
  * 确认在 [rtx_gui.cpp:516](../../src/gui/rtx_gui.cpp#L516) 标注 `[[maybe_unused]]` 或规范收敛其内部调用。

---

## 四、 协调负责人最终收网与直通放行承诺

一旦专家组出具权威修复代码：
1. 协调负责人将督导执行工程师完成物理落盘；
2. 在 Windows 宿主机实测达成 **MSVC `/W4 /WX` 85/85 零警告全量构建** 与 **147/147 单元测试 100% PASS**；
3. 立即签发 Wave 12.1 封板结案令，并正式下发 **Wave 12.2（`04_CineRTX Phase 12 SuperPower 施工单_自定义类型清剿与C++26标准库替换.md`）**！

**提请首席领域专家组签发最终技术规范！**
