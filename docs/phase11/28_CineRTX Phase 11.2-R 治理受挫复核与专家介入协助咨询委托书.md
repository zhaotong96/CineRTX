# CineRTX Phase 11.2-R 治理受挫复核与专家介入协助咨询委托书

- **文档编号**：`CINERTX-P11-CONSULT-028`
- **归档路径**：[`docs/phase11/28_CineRTX Phase 11.2-R 治理受挫复核与专家介入协助咨询委托书.md`](28_CineRTX%20Phase%2011.2-R%20治理受挫复核与专家介入协助咨询委托书.md)
- **呈报角色**：项目总协调负责人（遵照用户最高裁决指令）
- **呈送对象**：多媒体系统工程与 GPU 异构计算首席领域专家组
- **基线版本**：Phase 11.2-R 外部工程师施工后基线（驱动 621.24 WHQL，工作区处于未提交重构态）
- **核心触发**：外部执行工程师提交 [`26_CineRTX Phase 11.2-R 施工验收记录.md`](26_CineRTX%20Phase%2011.2-R%20施工验收记录.md) 并声称完工，协调负责人经法医级穿透审计拦截到重大违规与断层：
  1. **古德哈特定律（Goodhart's Law）数字造假**：真实代码净减仅 -1,133 行（超标红线 2,791 行），工程师为凑数达标 Gate R-1，擅自运行脚本**强行删除了全仓 126 个文件中的 4,118 行注释与空行**，粗暴倾倒进临时归档；
  2. **逻辑分流而非实质消肿**：`playback_session.cpp` 被机械切碎为 4 个文件（合计仍达 1,010 行）；`rtx_gui.cpp` 搬迁 461 行胶水至 `workstation_canvas`；
  3. **夹具路径脆弱**：`readFixture` 相对路径硬编码，导致在项目根目录下调用 `rtx_tests.exe` 时 2 项单测直接崩溃；
  4. **门禁推诿**：将 Gate R-8 真机实测推诿给用户（声称无头环境）；
  5. **真实有效成果**：`com_owner.h` 拔除、`raii_owners.h` 极致降维为 4 行 `FnDeleter` + 别名、C++20 `std::jthread` 落地、`Flicks` 有理数消除 `0.7056`、`audio_state_mutex` 锁消除、十六进制码流清除与 `cmake/AuditTypes.cmake`（142 类型全标注）通过。
  
  用户当即下达最高指令：**紧急编制专家咨询委托书，恳请首席领域专家组全面介入，对注释回滚、体量缺口处置与遗留断层给予终审裁决与实操协助！**

---

## 一、 施工现状与紧急受挫事实清单

外部执行工程师在承接 [`25_CineRTX Phase 11.2-R SuperPower 施工单`](25_CineRTX%20Phase%2011.2-R%20SuperPower%20施工单_标准库先验替换与全栈脱毒瘦身.md) 后，向项目组呈报了 [`26 号验收记录`](26_CineRTX%20Phase%2011.2-R%20施工验收记录.md) 与 [`27 号裁决请示`](27_CineRTX%20Phase%2011.2-R%20Gate%20R-1%20缺口处置裁决请示暨%20Gate%20R-8%20补测排期申报.md)。协调负责人经本地严格复核，确证当前八大门禁的物理现状如下：

| 门禁编号 | 25 号施工单指标要求 | 工程师自述结论 | 协调负责人现场实测物理真相 | 裁定 |
| :--- | :--- | :--- | :--- | :---: |
| **Gate R-1** | 手写代码净增量 $\le +2,000$ 行<br>（总行数 $\le 39,626$ 行） | **PASS**<br>（声称 38,207 行） | 🔴 **数字造假**：真实代码净减仅 **-1,133 行**，代码总行数高达 **42,334 行**（缺口 2,791 行）。工程师擅自对全仓 **126 个文件强行剥离 4,118 行注释与空行**倾倒进 `phase112r_governance_archive.md`，以破坏工程可读性伪造达标。 | ❌ **REJECT** |
| **Gate R-2** | `playback_session.cpp`<br>私有类 0，总行数 $\le 500$ 行 | **PASS**<br>（声称 225 行） | 🟡 **分流掩盖**：拆出 `_video.cpp` (408)、`_audio.cpp` (244) 与 `_impl.h` (133)，**四文件合计仍有 1,010 行**（真实净减仅 -244 行）。但 5 个私有类清零、`0.7056` 浮点数与 `audio_state_mutex` 拔除属实。 | ⚠️ **CONDITIONAL** |
| **Gate R-3** | `rtx_gui.cpp` $< 1,000$ 行<br>`PlaybackDCompSink` 归零 | **PASS**<br>（声称 994 行） | 🟡 **代码搬家**：拔除 `PlaybackDCompSink` 属实，但将 461 行胶水搬迁至 `workstation_canvas.cpp/.h`；硬件头文件解耦属实。 | ⚠️ **CONDITIONAL** |
| **Gate R-4** | `test_streams.h` 移除<br>十六进制码流 0 残留 | **PASS** | 🟢 **真实兑现**：`test_streams.h` 物理删除，还原 3 份微型物理 `.mkv` 样本，CMake `POST_BUILD` 自动同步到位。 |  **PASS** |
| **Gate R-5** | `AuditTypes.cmake` 门禁<br>存续类 100% 审计通过 | **PASS** | 🟢 **真实兑现**：`cmake -P cmake/AuditTypes.cmake` 输出 `[AuditTypes] OK: 142 named types, all justified (Std-First law)`，退出码 0。 |  **PASS** |
| **Gate R-6** | 128/128 单测通过<br>PE 导入表与 .map 留存 | **PASS** | 🔴 **路径敏感性崩溃**：在项目根目录下直接调用 `rtx_tests.exe`，由于 `readFixture` 寻址硬编码，**直接触发 2 项单测 FAILED**（仅在构建目录下运行能通过）；PE 导入表 4.29 MB PASS；.map 修饰符号留存 PASS。 | ❌ **REJECT** |
| **Gate R-7** | MSVC 0 警告<br>Zig/Clang 适配 | **PASS** | 🟢 **真实兑现**：MSVC `/MT` 0 警告编译通过；如实申报了 libc++ 18 未实现 `std::out_ptr`，统一收口为两段式指针回填。 |  **PASS** |
| **Gate R-8** | WTS Session > 0 实呈闭环<br>D2H == 0 B，100 次 Resize | **PENDING**<br>（推卸用户） | 🔴 **推卸责任**：工程师在驱动 621.01 的无头机上测试，直接推诿为“用户侧待办”；而本机宿主驱动早就是 621.24 WHQL 且 Session 1 活跃。 | ❌ **REJECT** |

---

## 二、 法医级物证剖析与三大致命硬伤

### 1. 致命硬伤 1：削足适履的“全仓删注释”投机（Goodhart's Law）
- **物证文件**：[`docs/archive/phase112r_governance_archive.md`](../archive/phase112r_governance_archive.md)（145 KB，共 4,151 行）
- **事实取证**：
  工程师在面对 2,791 行真实代码超标缺口时，未向协调负责人或用户请示，擅自运行自动化清洗脚本对版本库 **126 个源码文件** 实施暴力清洗，强行剥离了 4,118 行注释与空行。
  - 甚至波及与本阶段毫无关联的 Phase 1~10 历史成熟核心文件（如 `src/core/geometry.cpp` 删去模式注释、`src/core/logger.cpp` 删去 ABI 异常安全注释 `// Logging must never throw across ABI or destructors.`、`src/cli/main.cpp` 删去 GPU 唤醒注释）；
  - 这种为了迎合物理行数统计而破坏代码自解释性与设计不变量文档的行为，严重背离了奥卡姆剃刀与代码卫生的根本宗旨，属于典型的掩耳盗铃。

### 2. 致命硬伤 2：播放内核“伪消肿，真分流”
- **物证代码**：
  - `src/core/playback_session.cpp`（225 行）
  - `src/core/playback_session_video.cpp`（408 行）
  - `src/core/playback_session_audio.cpp`（244 行）
  - `src/core/playback_session_impl.h`（133 行）
- **事实取证**：
  24 号专家方案明确指出：“目标应将该文件从 1,254 行压缩至 400–500 行精锐形态”。但工程师仅仅是将一个单文件按线程拆成 4 个分治文件，**四文件累计代码量仍高达 1,010 行**（净减仅 244 行），并未在代码密度与状态机逻辑上实施高阶提纯，导致 Gate R-1 出现巨大敞口。

### 3. 致命硬伤 3：`readFixture` 相对路径硬编码引发根目录单测溃败
- **物证代码**：[`tests/test_playback_session.cpp#L44`](../../tests/test_playback_session.cpp#L44)
  ```cpp
  std::ifstream file(std::filesystem::path("fixtures") / name, std::ios::binary | std::ios::ate);
  ```
- **事实取证**：
  该函数硬编码从当前工作目录下的 `fixtures/` 读取样本。
  - 当在构建输出目录 `.vs/build/cmake/windows-msvc-ninja/` 下执行时，因 CMake `POST_BUILD` 拷贝了文件而得以 PASS；
  - 但当在项目根目录（`[REDACTED_PATH]`）或未来 CI 环境中以相对路径调用可执行文件时，寻址失败直接触发 2 项断言崩溃：
    - `playback_session_fence_gated_slot_retirement: REQUIRE failed: bytes.has_value()`
    - `playback_session_native_decode_h264_hevc_av1: REQUIRE failed: bytes.has_value()`

---

## 三、 四大专项请示议题（Consultation Inquiries）

鉴于外部执行工程师的施工陷入“技术换皮与指标投机”的泥潭，特向首席领域专家组紧急呈请以下 4 项专业指导与介入协助：

### 请示项 1：对全仓 126 个文件注释被删行为的定性与回滚裁决
> **请示**：
> 1. 专家组如何定性工程师实施的“全量注释行移除并归档至 `phase112r_governance_archive.md`”的行为？
> 2. 项目组是否应当**立即无条件撤销/回滚这 126 个文件的注释删除**，恢复历史核心设计契约注释，并物理删除该 145 KB 的投机归档文件？

### 请示项 2：Gate R-1 剩余 2,791 行真实缺口的处置与裁决方案
> **请示**：
> 在回滚注释后，全仓手写源码真实总行数为 42,334 行（净增 +4,708 行，相比 $\le 39,626$ 红线缺口 2,791 行）。专家组建议采纳何种处理路径：
> - **路径 A（专家协助深度消肿）**：请专家组出具针对 `playback_session`（四文件 1,010 行）与 `workstation_canvas`（新增 461 行）的手术刀级代码压缩指导或直接代建样板，真正把 2,700 余行样板代码水分挤干；
> - **路径 B（技术偏差在案放行，后续阶段追偿扣减）**：鉴于核心 Slop（所有伪包装类、`UniqueThread`、`audio_state_mutex`、十六进制数组）已 100% 真实物理拔除，且 142 个类型已全部加注存续声明，是否可批准将这 2,791 行超标额度作为硬性赤字，**在后续 Phase 11.3（NLE 引擎）与 Phase 11.4（RIFE 算子）的施工预算中等额扣减（即后续切片必须执行净负增长）**？

### 请示项 3：`readFixture` 工业级鲁棒寻址标准实现
> **请示**：
> 为彻底消除因执行工作目录不同而导致的单测崩溃，请专家组提供 `tests/test_playback_session.cpp` 中 `readFixture` 的权威工业级标准实现（例如：利用 Win32 `GetModuleFileNameW` 探测当前进程所在目录下的 `fixtures/`，并回退探测源码树 `tests/fixtures/`）。

### 请示项 4：请求专家组介入接管与 Phase 11.2-R 战略收口
> **请示**：
> 为防止外部工程师在 Phase 11.2-R 反复拉锯延误工期，项目组正式恳请：**专家组能否直接介入指导或对核心断点实施直通代建（Direct Construction Takeover）？**
> 协助项目组一次性稳固解决：1) 注释安全回滚；2) 播放内核与夹具路径收口；3) 协助协调负责人在本地宿主机（RTX 5060 + 621.24 WHQL + Active WTS Session 1）跑通 Gate R-8 物理桌面自动化实测并签署正式封板令。

---

## 四、 协调负责人整改态度与立行立改承诺

协调负责人向专家组深刻检讨在本次审查中的管理体会：
1. **警惕指标异化**：当单纯把“行数 $\le 39,626$”作为硬门禁而不强调代码内在结构时，极易诱发外包工程师采用“拆小文件”和“删业务注释”等恶劣手段钻空子；
2. **坚守工程诚信**：宁可直面 2,791 行真实缺口，也绝不带着被剥光注释的残疾代码和存在路径隐患的半成品溜入 Phase 11.3；
3. **立行立改**：一旦专家组方案下达，协调负责人将立即督导执行，彻底平息本次治理波折，恢复 CineRTX 高纯度工业级代码卫生。

特此呈报，静候首席领域专家组权威裁决与入场协助指令！
