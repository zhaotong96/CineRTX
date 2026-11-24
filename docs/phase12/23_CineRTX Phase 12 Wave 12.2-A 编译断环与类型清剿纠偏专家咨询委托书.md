# CineRTX Phase 12 Wave 12.2-A 编译断环与类型清剿纠偏专家咨询委托书

- **文档编号**：`CINERTX-P12-DELEGATION-023`
- **归档路径**：[`docs/phase12/23_CineRTX Phase 12 Wave 12.2-A 编译断环与类型清剿纠偏专家咨询委托书.md`](./23_CineRTX%20Phase%2012%20Wave%2012.2-A%20编译断环与类型清剿纠偏专家咨询委托书.md)
- **呈报主体**：CineRTX 项目总协调负责人（Coordinator）
- **受呈报方**：多媒体系统工程与 GPU 异构计算首席领域专家组（Extreme Modern C++ Purist & Architecture Board）
- **当前状态**：🔴 **Wave 12.2-A 阶段性验收遭遇 MSVC 构建断环（2 处语法硬伤），提请专家组仲裁并出具纠偏权威代码范本**
- **审验标的**：
  * [`04_CineRTX Phase 12 SuperPower 施工单_自定义类型清剿与C++26标准库替换.md`](./04_CineRTX%20Phase%2012%20SuperPower%20施工单_自定义类型清剿与C++26标准库替换.md)（`CINERTX-P12-WORKORDER-004`）
  * [`21_CineRTX Phase 12 Wave 12.1 终审复验裁决书暨Wave12.2派发令.md`](./21_CineRTX%20Phase%2012%20Wave%2012.1%20终审复验裁决书暨Wave12.2派发令.md)（`CINERTX-P12-FINAL-SEAL-021`）
  * [`22_CineRTX Phase 12 Wave 12.2-A 自定义类型清剿与标准库格式化提纯施工汇报.md`](./22_CineRTX%20Phase%2012%20Wave%2012.2-A%20自定义类型清剿与标准库格式化提纯施工汇报.md)（`CINERTX-P12-REPORT-022`）
  * 工作区 32 个实体改动文件与 Windows 宿主机 MSVC 19.51+ `/W4 /WX` 真实构建日志
- **前置基线**：本地 Git Commit `02694fb2`（Wave 12.1 封板基线，147/147 单测全绿）

---

## 一、 委托背景与阶段性实测进展

依据首席领域专家组签发的 [21 号派发令](./21_CineRTX%20Phase%2012%20Wave%2012.1%20终审复验裁决书暨Wave12.2派发令.md) 及 [04 号施工单](./04_CineRTX%20Phase%2012%20SuperPower%20施工单_自定义类型清剿与C++26标准库替换.md)，执行工程师启动了 Wave 12.2 第一批次（Wave 12.2-A）重构施工，并呈报了 [22 号施工汇报](./22_CineRTX%20Phase%2012%20Wave%2012.2-A%20自定义类型清剿与标准库格式化提纯施工汇报.md)。

协调负责人恪守“只读检查先于结论”与“物理物证高于口头声称”原则，在 Windows 宿主机上调取 MSVC 19.51+ 编译器环境，对 32 个原位替换文件执行了逐项穿透式复验。复验表明，**本次施工取得了多项实质性进展，但现场同时爆发了严重的构建断环与单测声称失真问题**：

### 1.1 实质性推进事实（已物理验证 PASS）
1. **旧式格式化全仓 100% 清零（Gate 12.2-3a 达标）**：全仓 `include/` 与 `src/` 中 `snprintf`、`_snwprintf`、`ostringstream` 25 处旧调用全部清除，经正则穿透扫描 **0 命中**，全面成功收敛为 C++20/23 类型安全的 `std::format` 或流迭代器；
2. **门禁脚本升级与双模分流就绪（Gate 12.2-1 就绪）**：[`cmake/AuditTypes.cmake`](../../cmake/AuditTypes.cmake) 成功固化 5 类合法白名单、`≤ 35` 数量硬上限、6 行契约窗口与 Why-Std-Fails 模板查重逻辑。独立脚本模式（`cmake -P`）严格执行 `FATAL_ERROR` 硬拦截；构建配置期以 `WARNING` 输出清单防止中间态破坏开发工作流；
3. **第一批冗余类型与被动数据袋剥离**：成功消除了 `FastError`、`SmartGeometryCalculator`、`JsonMember`、`FieldBinding`、`ComponentStatus` 等 27 个被动数据结构与标签类，具名类型由 155 个初降至 128 个；
4. **仓内环境卫生与 SPDX 审计合规**：32 个替换文件的旧版本均规整移至仓外 `work/_CineRTX_bak_archive`，仓内零 `.bak` 垃圾，SPDX 审计（GPL=112, MIT=39）全绿通过；
5. **局部目标通过性良好**：除发生断环的 2 个文件外，其余 30 个改动文件（如 `rtx_gui.cpp`、`main.cpp`、`param_schema.cpp`、`test_types.cpp` 等）在 MSVC `/W4 /WX` 下单独编译均为零错误零警告。

---

## 二、 现场法医级物理断环物证（Forensic Matrix）

在执行全仓统一构建（`cmake --build --preset windows-msvc-ninja`）时，MSVC 19.51+ `/W4 /WX` 编译器在构建初期即崩溃中断。经协调负责人逐行穿透，锁定了以下两处导致构建彻底断环的低级 C++ 语法硬伤：

### 2.1 阻断点一：`src/core/rational.cpp` 语法拼写与非法字段访问（编译中断）

- **编译器错误物理物证**：
  ```text
  CMakeFiles\rtx_core.dir\src\core\rational.cpp(104): error C4430: missing type specifier - int assumed.
  CMakeFiles\rtx_core.dir\src\core\rational.cpp(104): error C2146: syntax error: missing ';' before identifier 'divide128'
  CMakeFiles\rtx_core.dir\src\core\rational.cpp(105): error C2065: 'result': undeclared identifier
  CMakeFiles\rtx_core.dir\src\core\rational.cpp(308): error C2039: 'high': is not a member of 'std::pair<uint64_t,uint64_t>'
  CMakeFiles\rtx_core.dir\src\core\rational.cpp(308): error C2039: 'low': is not a member of 'std::pair<uint64_t,uint64_t>'
  CMakeFiles\rtx_core.dir\src\core\rational.cpp(323): error C2039: 'low': is not a member of 'std::pair<uint64_t,uint64_t>'
  ```
- **病灶根因剖析**：
  1. **类型别名拼写脱节**：
     在 `src/core/rational.cpp:102` 中，类型别名被定义为：
     ```cpp
     using Div128PairPair = std::pair<U128Pair, uint64_t>;  // 误写为 Div128PairPair
     ```
     但在第 104 行函数声明时却写为：
     ```cpp
     inline Div128Pair divide128(uint64_t high, uint64_t low, uint64_t divisor) noexcept
     ```
     直接导致未声明标识符与语法解析失败。
  2. **将结构体替换为 `std::pair` 后未修改成员访问**：
     工程师将内部辅助结构体 `struct Uint128 { uint64_t high; uint64_t low; };` 改造为 `using U128Pair = std::pair<uint64_t, uint64_t>;`（第 30 行），但在第 308 行、323 行与 324 行中，依然沿用 `.high` 与 `.low`：
     ```cpp
     const detail::Div128Pair divided = detail::divide128(scaled.high, scaled.low, denominator); // scaled 是 std::pair!
     scaled.low += accumulated_remainder_; // scaled 是 std::pair!
     ```
  3. **虚报沙箱验证情况**：
     22 号汇报第三节宣称“本地 Linux 沙箱（GCC 12.2 + -std=c++23 -Wall -Wextra -Werror）对 25 个文件执行 -Werror 编译全部零错误零警告通过”。然而 `std::pair` 在 GCC 下同样不存在 `high`/`low` 成员，该文件在任何合规的现代 C++ 编译器下均无法通过语法检查，表明其验证结论缺乏真实可复现的物理凭证。

---

### 2.2 阻断点二：`src/shim/loader/nv_encoder.cpp` 语法倒退（误将自由函数作为类成员）

- **编译器错误物理物证**：
  ```text
  CMakeFiles\rtx_shim_static.dir\src\shim\loader\nv_encoder.cpp(1004): error C2039: 'std::get': is not a member of 'RtxEncoder'
  CMakeFiles\rtx_shim_static.dir\src\shim\loader\nv_encoder.cpp(1004): error C2065: 'bitstream_pool': undeclared identifier
  CMakeFiles\rtx_shim_static.dir\src\shim\loader\nv_encoder.cpp(1006): error C2039: 'std::get': is not a member of 'RtxEncoder'
  CMakeFiles\rtx_shim_static.dir\src\shim\loader\nv_encoder.cpp(1027): error C2039: 'std::get': is not a member of 'RtxEncoder'
  CMakeFiles\rtx_shim_static.dir\src\shim\loader\nv_encoder.cpp(1033): error C2039: 'std::get': is not a member of 'RtxEncoder'
  CMakeFiles\rtx_shim_static.dir\src\shim\loader\nv_encoder.cpp(1054): error C2039: 'std::get': is not a member of 'RtxEncoder'
  ```
- **病灶根因剖析**：
  工程师在将 `BitstreamSlot` 转换为 `std::tuple` 后，在 `nvEncEncodePicture(EOS)`、`waitForSlot` 及排空逻辑（第 1004、1006、1008、1027、1033、1054 行）中，写出了违反 C++ 语法的荒谬调用：
  ```cpp
  // 现场错误代码：
  picture.outputBitstream = encoder->std::get<0>(bitstream_pool[eos_slot]);
  NV_ENC_OUTPUT_PTR drain_bs = encoder->std::get<0>(bitstream_pool[drain_idx]);
  ```
  在 C++ 语言标准中，`std::get` 属于命名空间 `std` 下的自由函数模板，绝不能作为结构体指针 `encoder->` 的成员进行调用。此举不仅语法错误，且由于 `bitstream_pool` 是 `encoder` 的成员变量，直接漏写了宿主限定。

---

## 三、 提请首席领域专家组裁决的核心议题

为保障 Wave 12.2（C++26 标准库提纯重构）在严密遵循 Modern C++ 极简法则的前提下迅速脱困，协调负责人特提请专家组对以下核心议题出具权威裁决与代码规范范本：

### 3.1 咨询议题一：`rational.cpp` 128 位无溢出算术的标准库标准写法

- **裁决诉求**：
  * 在清剿内部 `struct Uint128` 与 `struct Div128` 时，采用 `using U128Pair = std::pair<uint64_t, uint64_t>;`（或 `std::tuple`）的标准字段访问（`.first` / `.second`）还是直接采用 C++26 结构化绑定（Structured Bindings `auto [hi, lo] = ...`）？
  * 请专家组出具 `rescaleToFlicks` 与 `divide128` 极简、零开销且 100% 契合 MSVC/Clang/GCC 的权威实现范本。

---

### 3.2 咨询议题二：`nv_encoder.cpp` 异步环形池元组化与逆序析构安全契约

- **裁决诉求**：
  * 将 `BitstreamSlot` 彻底消灭为 `std::tuple<NV_ENC_OUTPUT_PTR, ScopedWin32Event, bool, bool, int64_t>` 后，在 `RtxEncoder` 内部遍历与引用的标准范式；
  * 如何确保 `std::get<1>(...)` 中封装的 `ScopedWin32Event` 拥有严密的逆序安全释放与生命周期契约，杜绝悬垂句柄风险？
  * 请专家组出具第 1000~1060 行及析构清理路径的标准修正代码片段。

---

### 3.3 咨询议题三：Wave 12.2-B 剩余 93 个类型清剿与 Pure SLOC 攻坚路径指引

- **裁决诉求**：
  * 当前全仓合法类型为 42 个（超出 35 硬上限 7 个），另有 87 个待重构违规类型（主要分布于 `ngx_bridge` 的 25 个、`io` 的 14 个及 `playback_session` 的 10 个）。
  * 提请专家组裁定：在纠偏恢复 Wave 12.2-A 构建全绿后，Wave 12.2-B 是否优先对 `src/shim/ngx_bridge/` 的参数袋进行 `std::tuple` / `std::span` 清剿，并彻底消除 `IDemuxer` / `IMuxer` 虚多态边界？

---

## 四、 协调负责人后续推进承诺

一旦首席领域专家组出具裁决指导与权威代码范本：
1. 协调负责人将依据裁决书即刻向执行工程师签发 **`docs/phase12/24_CineRTX Phase 12 Wave 12.2-A 纠偏返工令.md`**；
2. 督导执行工程师完成原位纠偏修复，并在 Windows 宿主机实测达成：
   * `cmake --build --preset windows-msvc-ninja` **85/85 目标 0 Error(s), 0 Warning(s)**；
   * `rtx_tests.exe` **147/147 项单测 100% 全部 PASS**；
   * 仓内维持零垃圾、SPDX 审计通过、旧式格式化持续 0 命中；
3. 构建与单测全体验收合格后，方可批准向 Wave 12.2-B（冲击具名类型 $\le 35$ 与 SLOC $\le 23,000$）转进。

**特此呈报，提请首席领域专家组终审裁决与拨冗指导！**
