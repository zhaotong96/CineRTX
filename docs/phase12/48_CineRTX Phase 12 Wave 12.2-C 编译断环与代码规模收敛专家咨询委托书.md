# CineRTX Phase 12 Wave 12.2-C 编译断环与代码规模收敛专家咨询委托书

- **文档编号**：`CINERTX-P12-DELEGATION-048`  
- **归档路径**：`docs/phase12/48_CineRTX Phase 12 Wave 12.2-C 编译断环与代码规模收敛专家咨询委托书.md`  
- **呈报主体**：CineRTX 项目总协调负责人（Coordinator）  
- **接收主体**：多媒体系统工程与 GPU 异构计算首席领域专家组（Extreme Modern C++ Purist & Architecture Board）  
- **前置依据**：  
  - [`46_CineRTX Phase 12 Wave 12.2-B2 终审封板裁决书暨Wave12.2-C第三战场作战令.md`](./46_CineRTX%20Phase%2012%20Wave%2012.2-B2%20终审封板裁决书暨Wave12.2-C第三战场作战令.md)（`CINERTX-P12-FINAL-SEAL-046`）  
  - [`47_CineRTX Phase 12 Wave 12.2-C 施工汇报.md`](./47_CineRTX%20Phase%2012%20Wave%2012.2-C%20施工汇报.md)（`CINERTX-P12-W12.2-C-REPORT-047`）  
- **审验标的**：  
  - 语法破损与头文件缺失源文件：`src/io/ffmpeg_io.cpp`、`src/gui/preview_viewport.cpp`  
  - 第三战场全量重构覆盖区（共修改 54 个源码文件，涵盖 `include/rtx/`、`src/core/`、`src/gui/`、`src/shim/loader/` 及测试集）  
- **呈报日期**：2026-11-23  
- **委托主题**：呈报 Wave 12.2-C 第三战场施工成效与宿主机物理实测断环事实，汇报具名自定义类型大幅压减至 47 个（超额达成 $\le 50$ 指标），深度取证 MSVC 宿主机两处编译阻断缺陷（`ffmpeg_io.cpp` 语法崩溃与 `preview_viewport.cpp` 漏引 `<bit>`），并诚实呈报 Pure SLOC（实测 30,105 行，距 $\le 26,500$ 门禁存在 3,605 行缺口）现状，提请专家组签发 49 号终审裁决书以出具定点止血方案并对行数收敛策略定调。

---

## 一、 咨询背景与第三战场战果全貌

依据 46 号作战令部署，施工单位全面打响了 Wave 12.2-C 第三战场攻坚，对 54 个源码文件执行了去结构化包装与 C++23 现代标准库重构，并提交了 47 号施工汇报。协调组在 Windows 宿主机进行了物理穿透对账，基准指标达成情况如下：

### 1.1 全仓门禁物理实测对账总表

| 门禁分类 | 46 号作战令硬性要求 | 施工汇报声称值 | 宿主机物理实测值 | 审核判定 |
| :--- | :--- | :--- | :--- | :--- |
| **Gate 1：具名自定义类型** | $\le 50$ 个（冲刺 $\le 35$） | 47 个 | **47 个**（34 项独立 + 13 项共享样板） | 🟢 **PASS** |
| **Gate 1：非法类型分类** | 0 项 | 0 项 | **0 项**（`Polymorphic` / `Concurrency` 清零） | 🟢 **PASS** |
| **Gate 2：纯 SLOC 行数** | $\le 26,500$ 行 | 30,105 行 | **30,105 行**（净减 277 行，缺口 **3,605 行**） | 🔴 **未达标（诚实披露）** |
| **Gate 3：现代 C++ 卫生** | `std::get<` / `<sstream>` 零命中 | 0 / 0 | **0 / 0**，SPDX PASS，无仓内 `.bak` 残留 | 🟢 **PASS** |
| **Gate 4：宿主 MSVC 构建** | `/std:c++latest /W4 /WX` 零错误零警告 | 仅测 Linux 16 TU | 🔴 **编译中断**（100+ 错误级联，阻断链接） | 🔴 **FAIL（物理阻断）** |
| **Gate 4：单元测试全绿** | 147/147 PASS | 未在 Windows 运行 | **未产生二进制，0/147 无法运行** | 🔴 **FAIL（阻断）** |

---

## 二、 宿主机物理编译阻断缺陷深度取证

### 2.1 阻断 A：`src/io/ffmpeg_io.cpp:220-235` 语法断环与花括号失配（Fatal Error C1003）

#### 物理错误日志
```text
[REDACTED_PATH]\src\io\ffmpeg_io.cpp(221): error C2065: 'hp': undeclared identifier
[REDACTED_PATH]\src\io\ffmpeg_io.cpp(224): error C2065: 'ht': undeclared identifier
[REDACTED_PATH]\src\io\ffmpeg_io.cpp(227): error C2065: 'hm': undeclared identifier
[REDACTED_PATH]\src\io\ffmpeg_io.cpp(234): error C3927: '->': trailing return type is not allowed after a non-function declarator
...
[REDACTED_PATH]\src\io\ffmpeg_io.cpp(360): fatal error C1003: error count exceeds 100; stopping compilation
ninja: build stopped: subcommand failed.
```

#### 根因剖析
在 `src/io/ffmpeg_io.cpp` 的 `mergeHevcBitstreamMetadata` 函数中：
1. 施工单位在重构 `ColorDescription` 为元组形态后，在第 220–228 行引用了从未声明的未具名局部标识符 `hp`、`ht`、`hm`；
2. 在第 227 行末尾非法误插入了一个右闭合花括号 `}`，使 `mergeHevcBitstreamMetadata` 函数体提前闭合；后续第 229–235 行及之后的所有顶层类实现代码（包括 `FfmpegDemuxer`、`FfmpegMuxer`）均被视作全局游离语句，导致 MSVC 抛出 100+ 级联语法错误当场中止。

### 2.2 阻断 B：`src/gui/preview_viewport.cpp:554` 漏引 `<bit>` 头文件（Error C2039）

#### 物理错误日志
```text
preview_viewport.cpp
[REDACTED_PATH]\src\gui\preview_viewport.cpp(554): error C2039: 'bit_cast': is not a member of 'std'
[REDACTED_PATH]\src\gui\preview_viewport.cpp(554): error C2275: 'uint32_t': expected an expression instead of a type
[REDACTED_PATH]\src\gui\preview_viewport.cpp(555): error C2039: 'bit_cast': is not a member of 'std'
```

#### 根因剖析
在消除 `ViewportConstants` 结构体、直接采用 `std::array<uint32_t, 12>` 打包常量缓冲区时，第 554–555 行调用了 `std::bit_cast<std::uint32_t>(split_ratio_)`，但文件头部未包含 C++20 `<bit>` 头文件。

---

## 三、 协调组预研外科手术修复方案

协调组遵循极简闭环原则，已完成以下定点补丁预研，呈报专家组审定：

### 3.1 方案 A：修正 `src/io/ffmpeg_io.cpp:219-235`
对齐 `info.color` 四元组结构化绑定，消除多余闭合花括号，保持与前置解构作用域一致：

```cpp
// 修复方案：src/io/ffmpeg_io.cpp 第 219 行起
    if (info.hdr.enabled) {
        auto& [c_pri, c_trc, c_mat, c_rng] = info.color;
        (void)c_rng;
        if (!has_primaries) {
            c_pri = ColorPrimaries::BT2020;
        }
        if (!has_transfer) {
            c_trc = TransferFunction::SMPTE2084;
        }
        if (!has_matrix) {
            c_mat = MatrixCoefficients::BT2020NC;
        }
        info.hdr.color = info.color;
// HDR10 signals that omit colour metadata still require the PQ
// container description; fill only the gaps so an explicit HLG or
// BT.2020 declaration survives untouched.
    }
    info.hdr_source = info.hdr.enabled;
}
```

### 3.2 方案 B：修正 `src/gui/preview_viewport.cpp`
在 `src/gui/preview_viewport.cpp` 顶部头文件引入区正式补充 `#include <bit>`。

---

## 四、 Pure SLOC 3,605 行缺口深度分析与战略抉择

### 4.1 实测事实
- **实测纯 SLOC**：30,105 行（较基线 30,382 行仅净减 277 行）；
- **门禁要求**：$\le 26,500$ 行；
- **当前差额**：**+3,605 行**。

### 4.2 结构性成因
1. **类型退化为元组的初生代价**：将 26 个具名结构体转换为 `std::tuple`/`std::pair` 别名，虽然使全仓类型锐减至 47 个（超额达成 $\le 50$ 目标），但各处调用点为了兼容解包，引入了大量的结构化绑定声明与自由访问函数，短期内反而产生局部行数微增；
2. **重度代码集中区尚未重组**：目前工程代码行数高度集中于：
   - Win32 GUI 冗长控件创建与事件路由（`src/gui/rtx_gui.cpp`、`src/gui/workstation_canvas.cpp`，共约 1,800 行）；
   - FFmpeg 复杂封装与错误码分支映射（`src/io/ffmpeg_io.cpp`，约 1,100 行）；
   - 多场景全量测试夹具与参数排列（`tests/` 下 24 个文件，约 13,000 行）。
3. **恪守工程诚信**：施工单位未采取压缩单行、合并变量声明或删减合法技术注释的手段伪造数据。

---

## 五、 请求专家委员会裁决与授权事项

为确保 Phase 12 严谨向前推进，协调组提请首席领域专家组签发 **《49 号终审裁决书》**，对以下核心议题出具法定裁定：

1. **审定两处编译阻断补丁**：裁决上述 `ffmpeg_io.cpp` 与 `preview_viewport.cpp` 预研修复方案是否权威有效，授权施工单位立即执行止血修复并恢复 MSVC 构建流水线；
2. **裁定 Pure SLOC 治理路线**：
   - **选项甲（顺延定调）**：鉴于命名类型已超额达成 47 个（逼近终局 35 个红线），正式批准将 Pure SLOC $\le 26,500$ 指标顺延至 Wave 12.3 或作为 Wave 12 全局收口目标；
   - **选项乙（定向瘦身令）**：若坚守 Wave 12.2-C 硬扣 26,500 行，提请专家组下达具体模块重构范式（如 GUI 控件声明全表驱动化、宏与泛型助手折叠测试夹具），指导施工单位完成 3,605 行的实质性消肿。

---
*CineRTX 项目总协调负责人（Coordinator）*  
*呈报：多媒体系统工程与 GPU 异构计算首席领域专家组*
