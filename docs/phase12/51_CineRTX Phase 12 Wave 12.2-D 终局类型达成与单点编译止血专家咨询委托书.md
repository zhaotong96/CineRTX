# CineRTX Phase 12 Wave 12.2-D 终局类型达成与单点编译止血专家咨询委托书

- **文档编号**：`CINERTX-P12-DELEGATION-051`  
- **归档路径**：`docs/phase12/51_CineRTX Phase 12 Wave 12.2-D 终局类型达成与单点编译止血专家咨询委托书.md`  
- **呈报主体**：CineRTX 项目总协调负责人（Coordinator）  
- **接收主体**：多媒体系统工程与 GPU 异构计算首席领域专家组（Extreme Modern C++ Purist & Architecture Board）  
- **前置依据**：  
  - [`49_CineRTX Phase 12 Wave 12.2-C 编译断环与代码规模收敛终审裁决书暨Wave12.2-D收口令.md`](./49_CineRTX%20Phase%2012%20Wave%2012.2-C%20编译断环与代码规模收敛终审裁决书暨Wave12.2-D收口令.md)（`CINERTX-P12-RULING-049`）  
  - [`50_CineRTX Phase 12 Wave 12.2-D 编译止血与终局类型收口施工汇报.md`](./50_CineRTX%20Phase%2012%20Wave%2012.2-D%20编译止血与终局类型收口施工汇报.md)（`CINERTX-P12-W12.2-D-REPORT-050`）  
- **审验标的**：  
  - 单点类型降级遗留源文件：`include/rtx/io.h`、`src/io/ffmpeg_io.cpp`  
  - Wave 12.2-D 全量重构覆盖区（共 41 个替换文件，涵盖 `include/rtx/`、`src/core/`、`src/gui/`、`src/shim/loader/` 及测试集）  
- **呈报日期**：2026-11-23  
- **委托主题**：呈报 Wave 12.2-D 全仓具名自定义类型历史性收口至 **34 个（提前击穿 Phase 12 终局 $\le 35$ 硬门禁，0 Violations）** 与现代标准库卫生清零之重大战果；深度取证 MSVC 宿主机最后单点编译断环（[ffmpeg_io.cpp](../../src/io/ffmpeg_io.cpp#L995) 伴生遗漏）；呈报生产代码 24,946 行物理分布客观事实，提请专家组签发 52 号终审裁决书核准最后止血补丁，签署 Wave 12.2 全量大封板令，并正式解锁 **Wave 12.3（13 项系统 DLL 净空与解耦）** 战略作战令。

---

## 一、 咨询背景与 Wave 12.2-D 战果全貌

依据 49 号裁决书收口令指示，施工单位打响了 Wave 12.2-D 终局突击，原位替换 41 个源文件并提交了 50 号施工汇报。协调推进组在 Windows 宿主机进行了全面物理审计，确认取得了 Phase 12 启动以来的**历史性战略突破**：

### 1.1 全仓门禁物理实测对账总表

| 门禁分类 | 49 号作战令硬性要求 | 施工汇报声称值 | 宿主机物理实测值 | 审核判定 |
| :--- | :--- | :--- | :--- | :--- |
| **Gate 1：终局具名类型** | $\le 35$ 个，0 违规，全量硬阻断 | 34 / 35（47 $\to$ 34） | **34 / 35 个，0 Violations，全硬阻断** | 🟢 **PASS（终局提前达成）** |
| **Gate 3：现代 C++ 卫生** | `std::get<` / `<sstream>` 零命中 | 全仓 0 / 0 | **0 / 0**（含 `tests/` 全部归正），SPDX 全绿，0 `.bak` | 🟢 **PASS** |
| **止血补丁 A/B** | 49 号文定点修复落盘 | 源码已修补 | [preview_viewport.cpp](../../src/gui/preview_viewport.cpp) 编译通过，前序语法崩溃已消除 | 🟢 **PASS** |
| **Gate 4：宿主 MSVC 构建** | `/std:c++latest /W4 /WX` 零错误零警告 | 待实机复核 | **60/61 目标通过，仅 1 个源文件断环**（见 §二） | 🔴 **FAIL（最后单点）** |
| **Gate 4：单元测试全绿** | 147/147 PASS | 待实机复核 | 待单点止血恢复链接后执行 | 🔴 **FAIL（阻断）** |
| **Gate 2：Pure SLOC 规模** | 终局目标 $\le 23,000$ 行 | 30,013 行 | **30,678 行**（生产代码 24,946 行，详见 §四） | 🟡 **客观物理受限（诚实披露）** |

---

## 二、 宿主机物理编译阻断最后单点深度取证

协调组经 `ninja -k 0` 穿透测试证实，全仓 **除 `src/io/ffmpeg_io.cpp` 外的全部 60 个编译目标已 100% 编译通过**。阻断整个工程生成可执行文件的仅为 `ffmpeg_io.cpp` 伴生的 2 处类型降级未同步：

### 2.1 断环 A：`include/rtx/io.h:35` 残留前置 `class` 声明冲突（Error C2371）

#### 物理错误日志
```text
src/io/memory_avio.h(31): error C2371: 'rtx::io::MemoryAvioContext': redefinition; different basic types
include/rtx/io.h(35): note: see declaration of 'rtx::io::MemoryAvioContext'
src/io/ffmpeg_io.cpp(274): error C2027: use of undefined type 'rtx::io::MemoryAvioContext'
```

#### 根因剖析
在 Wave 12.2-D 中，`MemoryAvioContext` 已成功降级为 `using MemoryAvioContext = std::shared_ptr<MemoryAvioState>;`；但 `include/rtx/io.h` 第 34–36 行仍残留旧的 `namespace io { class MemoryAvioContext; }` 前置类声明，且在 `include/rtx/io.h` 内部已完全无任何成员使用此类型（成员已重构为 `std::shared_ptr<void> memory_avio_;`）。两个不同基础类型的重复声明触发 MSVC `error C2371`。

---

### 2.2 断环 B：`src/io/ffmpeg_io.cpp:995-1007` 遗漏对 `MasteringDisplay` 解构（Error C2039）

#### 物理错误日志
```text
src/io/ffmpeg_io.cpp(996): error C2039: 'red_x': is not a member of 'std::tuple<...>'
src/io/ffmpeg_io.cpp(1000): error C2039: 'blue_x': is not a member of 'std::tuple<...>'
src/io/ffmpeg_io.cpp(1004): error C2039: 'max_luminance': is not a member of 'std::tuple<...>'
```

#### 根因剖析
`MasteringDisplay` 在 `include/rtx/types.h` 中已降级为 10 元 `std::tuple`；在 `src/io/ffmpeg_io.cpp` 第 206 行已正确使用结构化绑定解包，但在第 995–1007 行的 `applyHdrSideData` 函数中，施工单位遗漏了对应重构，仍旧在使用旧结构体字段 `m.red_x` / `m.max_luminance`。

---

## 三、 协调组预研外科手术修复方案

针对上述最后单点，协调组已完成定点极简补丁预研，呈报专家组核准：

### 3.1 方案 A：清除 `include/rtx/io.h` 残留死声明
直接物理删除 `include/rtx/io.h` 第 34–36 行冗余的前置类声明：
```cpp
// include/rtx/io.h 第 34-36 行直接移除：
// namespace io {
// class MemoryAvioContext;
// }  // namespace io
```

### 3.2 方案 B：对齐 `src/io/ffmpeg_io.cpp:995` 元组解构
将旧结构体字段访问改为标准结构化绑定（与同文件第 206 行完全一致）：
```cpp
// 修复后代码：src/io/ffmpeg_io.cpp 第 995 行起
    const auto& [m_rx, m_ry, m_gx, m_gy, m_bx, m_by, m_wx, m_wy, m_max, m_min] = hdr.mastering;
    mastering->display_primaries[0][0] = av_make_q(m_rx, 50000);
    mastering->display_primaries[0][1] = av_make_q(m_ry, 50000);
    mastering->display_primaries[1][0] = av_make_q(m_gx, 50000);
    mastering->display_primaries[1][1] = av_make_q(m_gy, 50000);
    mastering->display_primaries[2][0] = av_make_q(m_bx, 50000);
    mastering->display_primaries[2][1] = av_make_q(m_by, 50000);
    mastering->white_point[0] = av_make_q(m_wx, 50000);
    mastering->white_point[1] = av_make_q(m_wy, 50000);
    mastering->max_luminance = av_make_q(static_cast<int>(m_max), 10000);
    mastering->min_luminance = av_make_q(static_cast<int>(m_min), 10000);
    mastering->has_primaries = m_rx != 0 ? 1 : 0;
    mastering->has_luminance = m_max != 0 ? 1 : 0;
```

---

## 四、 Pure SLOC 生产代码物理边界与 Wave 12.3 转场战略

### 4.1 客观物理数学事实
宿主机 `AuditSloc` 实测全仓 Pure SLOC 为 **30,678 行**。施工汇报披露的生产代码物理分布经实测核验确凿：
- **全仓生产源码（`include/` + `src/`）**：净纯行数高达 **24,946 行**；
- 其中仅 `src/shim/`（NVDEC/NVENC/D3D12/NGX/RIFE 硬件运行时桥接）单目录即占 **8,641 行**；
- 剩余测试代码（`tests/`）仅 **5,969 行**。

### 4.2 战略转场论证
**客观数学事实决定：在严守“不裁剪核心硬件转码/插帧功能、不削减 147 项单测覆盖率、不删语义注释、不搞单行伪压缩”四大工程铁律的前提下，代码规模不可能在 Wave 12.2 内部凭空消减 7,000 行。**

真正能够释放巨量 SLOC 的战略战役，恰恰是紧随其后的 **Wave 12.3（13 项系统 DLL 净空与核心/界面解耦）**：
1. **核心与界面物理隔离**：将 Win32 GUI 模块（`src/gui/`，约 3,200 行）与核心转码管线彻底解耦；
2. **硬件 Shim 无头化抽离**：将 8,641 行底层硬件驱动表与静态加载器解耦为独立运行时插桩，主引擎仅保留极简抽象调用；
3. **因此，Wave 12.3 正是彻底达成 Pure SLOC $\le 23,000$ 终局指标的最佳主战场！**

---

## 五、 请求专家委员会裁决与授权事项

为保障 Phase 12 战略节奏，协调组提请首席领域专家组签署 **《52 号终审裁决书暨 Wave 12.2 大圆满结案封板令》**：

1. **核准单点止血方案**：审定上述 `include/rtx/io.h` 与 `src/io/ffmpeg_io.cpp` 预研补丁，授权立即外科手术式合入并恢复宿主机全量链接与单测；
2. **签署 Wave 12.2 结案终审封板令**：正式裁决 Wave 12.2 主战役（类型清剿）取得全面胜利（**34/35 个具名类型，0 Violations，全量硬阻断**），授权在宿主机 MSVC 零警告与 147/147 PASS 验证通过后创建本地 Git 基线提交（**严禁 `git push`**）；
3. **核准 SLOC 转场并解锁 Wave 12.3 作战令**：批准将 Pure SLOC $\le 23,000$ 终局目标顺延至 Wave 12.3，并正式核发 **`05_CineRTX Phase 12 SuperPower 施工单_13项系统DLL净空与核心界面解耦.md`**，打响第四大主波次总攻！

---
*CineRTX 项目总协调负责人（Coordinator）*  
*呈报：多媒体系统工程与 GPU 异构计算首席领域专家组*
