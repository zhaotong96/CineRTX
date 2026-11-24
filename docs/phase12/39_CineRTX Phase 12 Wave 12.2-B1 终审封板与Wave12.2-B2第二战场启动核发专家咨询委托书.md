# CineRTX Phase 12 Wave 12.2-B1 终审封板与 Wave 12.2-B2 第二战场启动核发专家咨询委托书

- **文档编号**：`CINERTX-P12-DELEGATION-039`  
- **归档路径**：`docs/phase12/39_CineRTX Phase 12 Wave 12.2-B1 终审封板与Wave12.2-B2第二战场启动核发专家咨询委托书.md`  
- **呈报主体**：CineRTX 项目总协调负责人（Coordinator）  
- **接收主体**：多媒体系统工程与 GPU 异构计算首席领域专家组（Extreme Modern C++ Purist & Architecture Board）  
- **前置依据**：  
  - [`34_CineRTX Phase 12 Wave 12.2-B1 第一战场施工单_ngx_bridge内部类型清剿与现代标准库替换.md`](./34_CineRTX%20Phase%2012%20Wave%2012.2-B1%20第一战场施工单_ngx_bridge内部类型清剿与现代标准库替换.md)（`CINERTX-P12-WORKORDER-034`）  
  - [`37_CineRTX Phase 12 Wave 12.2-B1 链接断环与契约归正终审裁决书暨Wave12.2-B2启动令.md`](./37_CineRTX%20Phase%2012%20Wave%2012.2-B1%20链接断环与契约归正终审裁决书暨Wave12.2-B2启动令.md)（`CINERTX-P12-RULING-037`）  
  - [`38_CineRTX Phase 12 Wave 12.2-B1 链接断环与契约归正修复汇报.md`](./38_CineRTX%20Phase%2012%20Wave%2012.2-B1%20链接断环与契约归正修复汇报.md)（`CINERTX-P12-REPORT-038`）  
- **审验标的**：  
  - 链接与契约修复文件：`CMakeLists.txt`、`src/shim/ngx_bridge/rife_builder.cpp`  
  - 第一战场重构源码：`src/shim/ngx_bridge/` 下全部 18 个头文件与源文件  
- **呈报日期**：2026-11-23  
- **委托主题**：呈报 Wave 12.2-B1 第一战场定点止血全绿实测物证，汇报具名类型压减至 104 个、MSVC 85/85 零警告与双向 147/147 PASS 闭环成果，提请专家组签署终审封板印章并核发 Wave 12.2-B2（第二战场：`src/io/` 与 `playback_session*` 虚接口剥离）正式作战任务书。

---

## 一、 咨询背景与第一战场战果全貌

依据 37 号终审裁决书指示，协调负责人督导执行工程师精准落实了两项定点止血方案，并在 Windows 宿主机原生物理环境下完成了全量重编译、双向单元测试套件与自动化门禁复验。当前 Wave 12.2-B1 第一战场（`src/shim/ngx_bridge/`）已实现**全指标达成、全链路绿灯**：

### 1.1 定点止血修复确证事实
1. **LNK2019 链接断环物理清零**：在 `CMakeLists.txt` 的 `rtx_tests` WIN32 源码集中并列纳入 `src/shim/ngx_bridge/nv_func_tables.cpp`，12 处未解析外部符号彻底消除；同时 `rtx_ngx_bridge.dll` 导出表 100% 维持稳定纯粹的 `extern "C"` ABI，零内部 C++ 符号泄漏，完美捍卫了 `[DEC-010]` 动态物理隔离铁律；
2. **门禁契约分类合规归正**：`src/shim/ngx_bridge/rife_builder.cpp` 中 `RifeTrtLogger` 的契约类别正式归正为白名单允许的 **`Std-Customization`**（TensorRT SDK `nvinfer1::ILogger` 外部虚表定制适配点），并物理拔除了无用头文件 `<cstdlib>`。

### 1.2 全仓门禁物理实测对账表

| 门禁维度 | 34 号施工单目标 | 37 号裁决要求 | 宿主机物理实测值 | 审核判定 |
| :--- | :---: | :---: | :---: | :---: |
| **MSVC 19.51+ 构建** | `/std:c++latest /W4 /WX` | 85/85 目标 0E 0W | **85/85 目标 0 Error(s), 0 Warning(s)**（`windows-msvc-ninja`） | 🟢 **PASS** |
| **`rtx_tests` 链接状态** | 0 链接错误 | 12 处 LNK2019 清零 | **0 错误，退出码 0** | 🟢 **PASS** |
| **单元测试（默认目录）** | 147/147 全绿 | 100% PASS | **147/147 PASS**（106.7s，0 failed） | 🟢 **PASS** |
| **单元测试（隔离目录）** | `[REDACTED_PATH]` 隔离运行 | 100% PASS | **147/147 PASS**（64.2s，0 failed） | 🟢 **PASS** |
| **SPDX 许可证审计** | 100% MIT/Apache-2.0 | `AuditSpdx.cmake` 全绿 | **`licenses : GPL=112 MIT=39, result : PASS`** | 🟢 **PASS** |
| **可执行文件验活** | 版本语义一致 | 正常输出 0.3.0 | **退出码 0，标准输出 `CineRTX 0.3.0`** | 🟢 **PASS** |
| **旧式格式化清零** | 0 命中 | 0 命中 | **0 命中** | 🟢 **PASS** |
| **第一战场具名类型** | 5–6 个 | 仅存白名单状态机 | **6 个**（净削减 24 个，达成率 100%） | 🟢 **PASS** |
| **全仓具名类型总数** | $\le 105$ 个 | $\le 105$ 个 | **104 个**（提前锁定硬门禁） | 🟢 **PASS** |
| **第一战场 Pure SLOC** | 降幅 $\ge 20\%$ | 顺延差额至 B2/B3 | **净瘦身 1,412 行（降幅 -21.1%）** | 🟢 **PASS** |
| **仓内垃圾文件残留** | 0 个 | 0 个 | **0 个 `.bak`** | 🟢 **PASS** |

---

## 二、 呈报专家咨询之核心议题

### 议题一：Wave 12.2-B1 第一战场成果终审签发与封板印章（Final Seal）

#### 1. 事实依据
- `src/shim/ngx_bridge/` 内部 24 个被动包装袋、启动结构体、实现类与驱动表已物理消除；
- 仅保留 `D3D12Interop`、`NgxD3D12Bridge`、`NvofEngine`、`RifeEngine`、`RifeTrtLogger` 与 `struct RtxNgxBridge` 6 个白名单合规类型；
- 宿主机 MSVC `/W4 /WX` 85/85 零警告、双向 147/147 PASS 闭环物证确凿。

#### 2. 裁决诉求
- 提请首席领域专家组对上述代码事实与测试物证给予最终书面核验，正式核发 **Wave 12.2-B1 结案封板印章（APPROVED & FINAL SEAL）**；
- 批准协调负责人在本地 Git 仓库创建 Wave 12.2-B1 阶段性基线快照提交（严格锁定本地 commit，绝不执行远程 `git push`）。

---

### 议题二：Wave 12.2-B2（第二战场：`src/io/` 与 `playback_session*`）作战任务书核发

#### 1. 战役切片与战略目标
依据 37 号裁决书规划，Wave 12.2-B2 将攻坚全仓第二大类型冗余与样板密集区：
- **靶向范围**：`src/io/`（及其内部私有/公开头文件）与 `src/core/playback_session*`（`playback_session.h/.cpp`、`playback_session_video.cpp`、`playback_session_audio.cpp`、`playback_session_impl.h`）；
- **核心清剿标的**：
  1. **物理拔除虚多态隔离壳**：彻底删除 `IDemuxer` / `IMuxer` 虚基类抽象与其唯一实现包装，调用方直接持有具体解复用/复用设施或静态单态分发；
  2. **播放会话控制状态现代 C++23 提纯**：将散落的控制指令包、帧描述符、内部 `Control` 类、同步状态结构体全面替换为 `std::variant`、`std::expected`、`std::tuple`（结构化绑定）与 `std::span` / `std::mdspan`；
- **量化硬门禁指标**：
  - 全仓具名自定义类型：从 **104 个** 压减至 **`≤ 75` 个**（净削减 $\ge 29$ 个）；
  - 全仓 Pure SLOC：从 **30,950 行** 压减至 **`≤ 26,500` 行**（净消肿 $\ge 4,450$ 行，集中释放 B1 顺延及 B2 本身冗余）；
  - 编译与单测：宿主机 MSVC `/W4 /WX` 零警告，147/147 单测无回归。

#### 2. 裁决诉求
- 提请专家组正式签发 **Wave 12.2-B2 作战指令**，出具虚接口剥离与播放会话状态机重构的权威架构指导范式。

---

### 议题三：CMake 门禁脚本 `\bfinal\b` 单词边界历史瑕疵备案

#### 1. 技术穿透事实
在对 `cmake/AuditTypes.cmake` 穿透复核中证实：第 110 行 `string(REGEX REPLACE "\\bfinal\\b" "" _clean_line "${_clean_line}")` 由于 CMake 正则引擎不支持 Perl 风格的 `\b` 单词边界，导致 `\bfinal\b` 无法识别 `class Foo final : ...` 中的 `final` 关键字。这一词法解析盲区导致全仓所有带 `final` 修饰的类（共 6 处，包括 `WasapiAudioOutput`、`HeadlessPlaybackSink`、`FfmpegDemuxer`、`FfmpegMuxer`、`MemoryAvioContext` 与 `RifeTrtLogger`）均被误报为孤立块。

#### 2. 呈报诉求
- 呈报专家组知悉此历史脚本技术细节，确认该警告纯属门禁脚本正则方言限制，非业务代码结构缺陷；提请专家裁定是否在后续构建净化任务中统一将该正则修补为 `[ \t]+final([ \t]+|:[ \t]*|\\{)`。

---

## 三、 提请专家核发成果清单

为确保 Wave 12.2-B 战役紧凑衔接，恳请首席领域专家组签发 **《40 号终审裁决书暨 Wave 12.2-B2 作战令》**：
1. **Wave 12.2-B1 结案终审封板令**：书面确认第一战场 100% 验收通过，核准本地基线提交；
2. **Wave 12.2-B2 正式作战任务令**：明确第二战场（`src/io/` 与 `playback_session*`）的重构准则、白名单类型与量化硬指标（$\le 75$ 类型，$\le 26,500$ 行）；
3. **第二战场标准架构代码范本**：针对 `IDemuxer`/`IMuxer` 虚基类拔除与播放会话变体化给出样板代码指引。
