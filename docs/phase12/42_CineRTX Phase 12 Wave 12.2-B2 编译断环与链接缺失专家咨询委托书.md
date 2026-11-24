# CineRTX Phase 12 Wave 12.2-B2 编译断环与链接缺失专家咨询委托书

- **文件编号**: `CINERTX-P12-DELEGATION-042`
- **当前状态**: `PROPOSED / PENDING_EXPERT_RULING`
- **委托方**: CineRTX 工程协调推进组 (Coordinator)
- **受托方**: 首席架构师与领域专家委员会 (Chief Architecture & Domain Expert Board)
- **基线版本**: `Wave 12.2-B1` 封板基线 `f25a7f1`
- **提交日期**: 2026-11-23

---

## 1. 委托背景与物理核验概貌

在《Wave 12.2-B1 终审封板裁决书暨Wave12.2-B2第二战场作战令》（`CINERTX-P12-FINAL-SEAL-040`）的规范下，施工单位推进了第二战场的去骨架重构并提交了《Wave 12.2-B2 施工汇报》（`CINERTX-P12-W12.2-B2-REPORT-041`）。

协调推进组在 Windows MSVC 宿主环境对工作区（共涉及 45 个改动源文件）执行了端到端物理穿透审计，基准指标达成情况如下表所示：

| 审计维度 | 门禁约束要求 | 物理实测数据 | 判定结论 | 说明 |
| :--- | :--- | :--- | :--- | :--- |
| **命名类型总数** | $\le 75$ | **73** | 🟢 **超额达标** | 由基线 104 个净减 31 个，类型下潜与内聚达标 |
| **非法类型分类** | 0 项 | **0** | 🟢 **合格** | `Polymorphic-Boundary` 与 `Concurrency-Invariant` 已清零 |
| **裸索引访问** | `std::get<` 清零 | **0** | 🟢 **合格** | 由基线 75 处违规全部归正为命名投射 |
| **禁止头文件** | `<sstream>` 零引入 | **0** | 🟢 **合格** | 由基线 2 处清除，字符串拼接契约规范 |
| **SPDX 许可证** | 100% 覆盖 | **GPL=112, MIT=39** | 🟢 **合格** | `AuditTypes.cmake` 判定 `PASS` |
| **工程编译与链接** | 0 Error, 0 Warning | **1 C3449, 1 LNK2019** | 🔴 **物理阻断** | 存在 1 处语法断环与 1 处符号缺失断环 |
| **纯 SLOC 规模** | 目标 $\le 26,500$ | **30,356** | 🟡 **阶段性达标** | 净减 594 行，恪守诚实守则（无拼行/无删注） |

经协调组物理构建验证，当前工作区存在 **2 处阻断性缺陷**，导致 `ninja` 编译失败与测试驱动二进制 `rtx_tests.exe` 链接中断。现特呈报专家委员会，请求出具权威技术定论与标准补丁。

---

## 2. 物理阻断问题深度取证

### 2.1 阻断 A：`src/gui/rtx_gui.cpp:658` 结构化绑定元组大小失配（MSVC C3449）

#### 物理错误日志
```text
[REDACTED_PATH]\src\gui\rtx_gui.cpp(658): error C3449: the number of identifiers must match the value of 'std::tuple_size<const 'anonymous-namespace'::BrowseSpec>::value'
[12/13] Building CXX object CMakeFiles\rtx_gui.dir\src\gui\rtx_gui.cpp.obj
ninja: build stopped: subcommand failed.
```

#### 根因剖析
在 `src/gui/rtx_gui.cpp` 第 387 行，`BrowseSpec` 定义为 6 元素元组：
```cpp
using BrowseSpec = std::tuple<int, int, const wchar_t*, DWORD, bool, bool>;
```
但在第 658 行解构迭代时，代码仅声明了 4 个标识符：
```cpp
// src/gui/rtx_gui.cpp:658
for (const BrowseSpec& b : kBrowseSpecs) {
    const auto& [btn_id, target_edit, is_save, filter] = b; // 错误：6 元组仅解构 4 项
    // ...
}
```
违反 C++17 结构化绑定语法契约，导致 MSVC 编译器直接抛出 `error C3449` 并中止构建。

---

### 2.2 阻断 B：`rtx_tests.exe` 缺失 `seekToKeyframeBefore` 符号定义（LNK2019 / LNK1120）

#### 物理错误日志
```text
[13/13] Linking CXX executable bin\rtx_tests.exe
FAILED: bin/rtx_tests.exe
cmd.exe /C "cd . && "C:\Program Files\CMake\bin\cmake.exe" -E vs_link_exe ... /out:bin\rtx_tests.exe ...
test_fuzz_resilience.cpp.obj : error LNK2019: unresolved external symbol "public: class std::expected<void,struct rtx::Error> __cdecl rtx::FfmpegDemuxer::seekToKeyframeBefore(__int64)" (?seekToKeyframeBefore@FfmpegDemuxer@rtx@@QEAA?AV?$expected@XUError@rtx@@@std@@_J@Z) referenced in function ...
hw_transcode.cpp.obj : error LNK2019: unresolved external symbol ... referenced in function ...
hw_transcode_stages.cpp.obj : error LNK2019: unresolved external symbol ... referenced in function ...
gop_seek_scheduler.cpp.obj : error LNK2019: unresolved external symbol ... referenced in function ...
bin\rtx_tests.exe : fatal error LNK1120: 1 unresolved externals
ninja: build stopped: subcommand failed.
```

#### 根因剖析
在 `include/rtx/io.h` 第 81 行，去多态化后的具象类 `FfmpegDemuxer` 声明了接口：
```cpp
[[nodiscard]] Result<void> seekToKeyframeBefore(int64_t video_stream_pts);
```
但在重构 `src/io/ffmpeg_io.cpp` 剔除 `IDemuxer` 接口实现的过程中，未将 `FfmpegDemuxer::seekToKeyframeBefore` 的函数体实体迁入具象类中，导致所有调用该接口的测试用例与转码调度单元在链接期触发 `LNK2019`。

---

## 3. 协调组预研修复方案与代码契约

协调组遵循**最小受控变更**（Minimally Sufficient Mechanism）原则，形成如下预研方案供专家委员会审定：

### 3.1 方案 A：修正 `src/gui/rtx_gui.cpp:658` 结构化绑定
完整对齐 6 元组签名，未使用的元组字段显式使用 `(void)` 消除 C4100 警告，行为与同文件 415 行的 `onBrowse` 保持一致：

```cpp
// 修复后代码：src/gui/rtx_gui.cpp:658
for (const BrowseSpec& b : kBrowseSpecs) {
    const auto& [btn_id, ctrl, filter, flags, save, probe] = b;
    (void)ctrl;
    (void)filter;
    (void)flags;
    (void)save;
    (void)probe;
    if (LOWORD(w_param) == btn_id) {
        onBrowse(hwnd, b);
        return;
    }
}
```

### 3.2 方案 B：在 `src/io/ffmpeg_io.cpp` 补充 `seekToKeyframeBefore` 实现
在 `src/io/ffmpeg_io.cpp` 末尾具象类作用域内注入标准实现，恢复 FFmpeg 关键帧倒回寻道与码流过滤器（BSF）排空逻辑：

```cpp
Result<void> FfmpegDemuxer::seekToKeyframeBefore(int64_t video_stream_pts) {
    if (format_ == nullptr || video_stream_ < 0) {
        return err(ErrorCode::State, "demuxer is not open");
    }
    const int64_t target =
        video_stream_pts == std::numeric_limits<int64_t>::min()
            ? 0
            : video_stream_pts;
    const int status = av_seek_frame(format_, video_stream_, target,
                                     AVSEEK_FLAG_BACKWARD);
    if (status < 0) {
        return err(ErrorCode::Io, "cannot seek to the requested key frame",
                   avErrorString(status));
    }
    drainBitstreamFilter();
    return ok();
}
```

---

## 4. SLOC 治理诚实披露与 Wave 12.2-C 顺延请示

- **实测纯 SLOC**: 30,356 行（对比 Wave 12.2-B1 的 30,950 行，净减 594 行）。
- **合规事实**: 施工单位未采用拼接行、压缩空白行、删除合法注释或破坏命名可读性的手段伪造行数收敛，保持了代码工程诚实度。
- **顺延请示**: Wave 12.2-B2 的核心使命是消除骨架空转（Named Types、Tuple 解构、多态接口扁平化）。对于剩余约 3,800 行向 $\le 26,500$ 行的收敛，请专家委员会裁定顺延至 **Wave 12.2-C**（涵盖 Core、NLE 与 GUI 模块冗余注册机制整合），不在 Wave 12.2-B2 闭环阶段作激进行数裁减。

---

## 5. 请求专家委员会裁决与授权事项

协调推进组特请专家委员会审议并签发《Wave 12.2-B2 修复裁决书暨终审封板令》（43 号文）：

1. **审定修复代码**: 裁定上述 `rtx_gui.cpp` 与 `ffmpeg_io.cpp` 修复代码是否具备权威有效性，或提供更优的极简实现。
2. **批准 SLOC 顺延**: 正式批准将 SLOC $\le 26,500$ 绝对目标顺延至 Wave 12.2-C 战场。
3. **签发修复作战令**: 授权施工单位立即开展两处外科手术式代码修复，并在宿主环境执行全量编译、类型审计与 147 项全量单元测试物理闭环。

---
*CineRTX 工程协调推进组 (Coordinator)*  
*报告呈送：首席架构师与领域专家委员会*
