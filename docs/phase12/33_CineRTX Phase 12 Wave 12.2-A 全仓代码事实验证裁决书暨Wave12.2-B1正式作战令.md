# **CineRTX Phase 12 Wave 12.2-A 全仓代码事实验证裁决书暨 Wave 12.2-B1 正式作战令**

* **文档编号**：`CINERTX-P12-FINAL-SEAL-033`  
* **归档路径**：`docs/phase12/33_CineRTX Phase 12 Wave 12.2-A 全仓代码事实验证裁决书暨Wave12.2-B1正式作战令.md`  
* **审验标的**：  
  * `docs/phase12/32_CineRTX Phase 12 Wave 12.2-A 全仓代码事实验证与Wave12.2-B启动核发专家咨询委托书.md`（`CINERTX-P12-DELEGATION-032`）  
  * `docs/phase12/31_CineRTX Phase 12 Wave 12.2-A 无状态Deleter常量性剥离返工汇报.md`（`CINERTX-P12-REPORT-031`）  
  * 仓库实体源码核验：`src/shim/loader/nv_encoder.cpp`、`src/core/rational.cpp`、`cmake/AuditTypes.cmake` 及全仓 32 个变更文件  
* **前序依据**：`04_CineRTX Phase 12 SuperPower 施工单_自定义类型清剿与C++26标准库替换.md`（`CINERTX-P12-WORKORDER-004`）、`30_CineRTX Phase 12 Wave 12.2-A 无状态Deleter常量性剥离终审裁决书暨Wave12.2-B启动令.md`（`CINERTX-P12-RULING-030`）  
* **签发主体**：多媒体系统工程与 GPU 异构计算首席领域专家组（Extreme Modern C++ Purist & Architecture Board）  
* **审验日期**：2026-11-23  
* **终审裁决**：**全票批准 Wave 12.2-A 正式结案封板，并正式核发 Wave 12.2-B1（第一战场 `src/shim/ngx_bridge/`）作战指令（APPROVED FOR WAVE 12.2-A FINAL SEAL & WAVE 12.2-B1 EXECUTION）**——经首席领域专家组直接调取工作区实体源码与构建门禁产出进行逐行法医级核验，确认 `nv_encoder.cpp` 无状态 Deleter 常量性剥离与单步三元就地构造、`rational.cpp` 128 位算术结构化绑定、全仓 25 处旧式格式化清零、首批 27 个冗余具名类型剥离、双模 `AuditTypes.cmake` 门禁及仓内零 `.bak` 垃圾卫生均 **100% 物理事实成立**；准予协调负责人在本地提交 Wave 12.2-A 基线（严格不推送），并即刻启动 Wave 12.2-B1 攻坚（具名类型从 128 个压减至 `~103` 个，Pure SLOC 从 32,360 行压减至 `~29,200` 行）。

# **执行摘要（Executive Ruling）**

首席领域专家组遵循"以仓库实体代码事实为唯一仲裁标准"的原则，对 `32 号委托书` 呈报的三项议题逐一进行源码级穿透复核，签发如下终审裁决：

1. **议题一裁决（全仓实体代码事实验证 PASS）**：直接核验工作区最新 `src/shim/loader/nv_encoder.cpp`、`src/core/rational.cpp` 与 `cmake/AuditTypes.cmake` 物理文件，确证其内容与 30 号终审裁决范本及 32 号委托书物证 100% 一致，未引入任何未定义行为、显存句柄泄漏或 C-ABI 布局漂移。  
2. **议题二裁决（Wave 12.2-A 正式结案封板 APPROVED）**：确认 Windows 宿主机 MSVC 19.51+ `/std:c++latest /W4 /WX` 全量 85/85 目标 `0 Error(s), 0 Warning(s)` 编译通规、双向运行 `rtx_tests.exe` 均达成 `147/147 PASS`、`AuditSpdx`（`GPL=112, MIT=39`）全绿、旧式格式化 `0` 命中且仓内零 `.bak` 残留，**正式签发 Wave 12.2-A 结案封板印章**，授权协调负责人执行本地 Git 基线提交（严禁执行 `git push`）。  
3. **议题三裁决（Wave 12.2-B1 正式作战令 ISSUED）**：正式核发第一战场 `src/shim/ngx_bridge/` 极限清剿施工指令，要求将桥接层内部全部被动配置袋、纹理/缓冲区描述符与光流/RIFE 内部参数传递包替换为 **`std::tuple`（结构化绑定解包）、`std::span<const std::byte>` 与 C++23 `std::mdspan`**，仅保留 `C-ABI` 导出契约与核心状态机 `RtxNgxBridge`，实现全仓具名类型 **`128 → ~103`**、Pure SLOC **`32,360 → ~29,200` 行**。

# **一、 议题一穿透复验：仓库实体代码级物证核验报告**

专家组直接调取工作区最新实体文件进行逐行核对，物证记录如下：

| 实体文件 | 源码物理锚点 | 逐行核验结论 | 审验定性 |
| :---- | :---- | :---- | :---- |
| **`src/shim/loader/nv_encoder.cpp`** | 第 120–139 行、第 879–908 行 | ① 已补充 `#include <type_traits>`；第 125–128 行准确落地 `using ScopedWin32Event = std::unique_ptr<void, std::remove_cvref_t<decltype(closeWin32Event)>>;`，且三行 `static_assert`（`is_default_constructible_v`、`is_move_assignable_v`、`sizeof == sizeof(void*)`）完整就位；② 第 879–908 行已改写为单步三元条件构造 `ScopedWin32Event slot_event = want_async ? makeScopedWin32Event() : ScopedWin32Event{};`，失败路径显式回收 `buf.bitstreamBuffer`，全文件 `BitstreamSlot` / `std::get` / `.resize(` 0 残留。 | **VERIFIED PASS** |
| **`src/core/rational.cpp`** | 第 28–135 行及 rescale 路径 | ① `rtx::detail` 中 `U128` 与 `Div128Result` 定义清晰，`multiply128`（含 `std::is_constant_evaluated()` 编译期分支与 `_umul128` / `__int128` 硬件加速分支）与 `divide128` 实现完整；② `rescaleToFlicks` 与 `IncrementalFlicksAccumulator` 全面采用结构化绑定解包并处理无符号加法进位传播，无任何 `.high` / `.low` 残留。 | **VERIFIED PASS** |
| **`cmake/AuditTypes.cmake`** | 第 1–135 行 | ① 白名单严格锁定 5 类（`C-ABI`、`Hardware-Layout`、`NonTrivial-StateMachine`、`Std-Customization`、`Domain-Model`），上限锁定 `RTX_TYPE_MAX_COUNT 35`；② 词法预清洗准确剥离行内注释、`alignas(...)`、`[[...]]`、`__declspec(...)` 与 `final` 修饰符；独立脚本模式（`cmake -P`）开启硬拦截，配置期输出清单辅助推进。 | **VERIFIED PASS** |
| **全仓卫生与格式化** | `include/`、`src/`、`tests/` | ① 全仓 `snprintf` / `_snwprintf` / `ostringstream` **0 命中**；② 32 份历史旧文件全部规整移出仓外归档，仓内 0 个 `.bak` / `.tmp` 垃圾文件；③ 具名类型由 155 个真实压减至 **128 个**（净减 27 个）。 | **VERIFIED PASS** |

# **二、 议题二终审裁决：Wave 12.2-A 正式结案封板令**

* **封板状态**：**PASSED & SEALED（正式结案封板）**  
* **基线提交授权**：批准协调负责人在本地 Git 仓库为 Wave 12.2-A 创建阶段性本地快照提交（严格执行本地 `git add` \+ `git commit`，**严禁执行 `git push`**），作为 Wave 12.2-B 战役的干净安全回退锚点。

# **三、 议题三终审裁决：Wave 12.2-B1（第一战场 `src/shim/ngx_bridge/`）正式作战指令**

## **3.1 作战范围与许可证隔离铁律**

* **靶向目录**：`src/shim/ngx_bridge/`（及其内部私有头文件，当前约 25 个具名自定义类型）与 `include/shim/ngx_bridge_abi.h`。  
* **许可证绝对红线**：`include/shim/` 与 `src/shim/` 下全部文件**必须 100% 保持 `SPDX-License-Identifier: MIT OR Apache-2.0`**，严禁包含任何 `include/rtx/*.h`（GPLv3 域）头文件，严禁引入任何 GPL 代码。

## **3.2 四大标准库替换与清剿动作（目标清剿 \~25 个冗余类型）**

| 清剿类别 | src/shim/ngx\_bridge/ 现存典型冗余类型 | 强制标准库替换原语与架构规范 | 预期净减类型数 |
| :---- | :---- | :---- | :---- |
| **1\. 内部参数与配置包装袋** | 内部 RIFE/VSR/NVOFA/Pack 参数传递结构体（非 `extern "C"` ABI 导出结构） | 替换为 **`std::tuple<...>`**（调用点强制使用结构化绑定 `auto [w, h, scale, ...] = ...;` 解包，严禁散落 `std::get<0/1/2>` 魔法数字）。 | `-10 ~ -12` |
| **2\. 二维帧平面与张量步幅描述符** | 散装 `(ptr, pitch, width, height)` 或自定义 `SurfaceDesc` / `PlaneView` / `TensorShape` | 替换为 C++20 **`std::span<std::byte>`**（一维缓冲视图）与 C++23 **`std::mdspan`**（携带动态步幅 `std::layout_stride` 的二维/三维平面张量视图）。 | `-6 ~ -8` |
| **3\. 局部 RAII 句柄与作用域清理壳** | 手写 CUDA 驱动对象/模块/流/事件/共享内存包装结构体 | 统一收口为 **`std::unique_ptr<T, decltype(fn)>`**（配合 `inline constexpr` 无状态 Lambda \+ `std::remove_cvref_t`，触发 EBO 零开销）或自由释放函数；作用域退出清理使用栈上局部 Lambda 哨兵。 | `-5 ~ -7` |
| **4\. 静态工具类与单方法策略类** | 仅含 `static` 方法的辅助类或单方法回调接口 | 静态工具类直接降维为 **`namespace`** 自由函数；单方法回调改用 C++23 **`std::move_only_function<...>`** 或模板 Callable 参数。 | `-3 ~ -4` |

## **3.3 `src/shim/ngx_bridge/` 唯一允许存续的具名类型白名单**

完成 Wave 12.2-B1 后，`src/shim/ngx_bridge/` 下**仅允许存续以下两类具名类型**（其余一律物理消灭，且严禁留存 `using OldStruct = std::tuple<...>;` 过渡别名）：

1. **`C-ABI` 跨边界导出契约**：`include/shim/ngx_bridge_abi.h` 中定义的 `extern "C"` 结构体（如 `RtxNgxBridgeConfig`、`RtxNgxFrameDesc`、`RtxRifeConfig` 等）及对外不透明句柄 `struct RtxNgxBridge`；  
2. **`NonTrivial-StateMachine` / `Hardware-Layout` 核心状态机**：管理 CUDA 上下文、D3D12 共享内存互操作环、TensorRT 引擎与 NGX 评估器逆序析构拓扑的核心状态机（如 `D3D12Interop`、`RifeEngine`），且必须具备逐类型专属、非模板复写的 `@rtx-type-justification` 契约块。

## **3.4 Wave 12.2-B1 量化验收硬门禁（Gate 12.2-B1）**

执行工程师交付 Wave 12.2-B1 时，必须满足以下全部门禁方可放行至 Wave 12.2-B2（src/io/ 与 playback\_session）：

- [ ] **类型压减达标**：全仓具名自定义类型从 `128` 个压减至 **`≤ 105` 个**（净减 `≥ 23` 个），其中 `src/shim/ngx_bridge/` 内部冗余包装类型清零；  
- [ ] **Pure SLOC 压减达标**：全仓 Pure SLOC 从 `32,360` 行压减至 **`≤ 29,500` 行**（不得通过删除语义注释凑数）；  
- [ ] **编译与许可零违规**：Windows 宿主机 MSVC `/std:c++latest /W4 /WX` 全量 **85/85 目标 0 错误 0 警告**；`cmake -P cmake/AuditSpdx.cmake` 确认 `src/shim/` 100% `MIT OR Apache-2.0`；  
- [ ] **全量测试无回归**：双向运行 `rtx_tests.exe` 达成 **147/147 PASS**（含 RIFE `scale=0.5` 与 D3D12 共享预览环 GPU 单测），仓内 **0 个 `.bak` 垃圾文件**。

