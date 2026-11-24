# **CineRTX Phase 12 Wave 12.2-B2 编译断环与链接缺失终审裁决书暨 Wave 12.2-C 推进路线**

**文档编号**：`CINERTX-P12-RULING-043`

---

# **备案与控制信息**

| 属性项目 | 属性值与备案依据 |
| :---- | :---- |
| **归档路径** | `docs/phase12/43_CineRTX Phase 12 Wave 12.2-B2 编译断环与链接缺失终审裁决书暨Wave12.2-C推进路线.md` |
| **审验标的** | `CINERTX-P12-DELEGATION-042CINERTX-P12-W12.2-B2-REPORT-041`实体源码：`src/gui/rtx_gui.cpp`、`include/rtx/io.h`、`src/io/ffmpeg_io.cpp` |
| **前序依据** | `CINERTX-P12-FINAL-SEAL-040CINERTX-P12-WORKORDER-004` |
| **签发主体** | 多媒体系统工程与 GPU 异构计算首席领域专家组（Extreme Modern C++ Purist & Architecture Board） |
| **审验日期** | 2026-11-23 |
| **裁决结论** | **APPROVED FOR IMMEDIATE SURGICAL FIX, WAVE 12.2-B2 SEAL & WAVE 12.2-C LAUNCH** |

---

# **执行摘要（Executive Ruling）**

首席领域专家组穿透核验了 `42 号委托书` 呈报的 Windows MSVC 19.51+ 构建与链接报错日志及仓库最新实体源码，确认 Wave 12.2-B2 已完成第二战场绝大部分结构去骨架与标准库提纯工作，仅剩两处局部语法与符号迁移疏漏。专家组签发如下终审裁决：

1. **Wave 12.2-B2 阶段性战果复核（VERIFIED PASS WITH 2 TARGETED FIXES）**  
   核准全仓具名类型由 `104` 压减至 `73` 个（优于 `≤ 75` 硬上限）、非法类别 `0` 项、`std::get<` 裸索引全仓 `0` 命中、`<sstream>` `0` 引入、`AuditSpdx`（`GPL=112, MIT=39`）全绿以及仓内零 `.bak` 残留。  
2. **议题一（方案 A）裁决**  
   批准 `rtx_gui.cpp:658` 完整 6 元组结构化绑定修复，并精简未使用变量消除：  
   * `BrowseSpec` 定义为 6 元素元组（`int, int, const wchar_t*, DWORD, bool, bool`），解构时必须提供全部 6 个标识符。  
   * **专家精简洁癖加码**：针对 C++26/MSVC `/W4 /WX` 编译环境，若保留具名变量以便日后扩展，5 行 `(void)` 显式抑制满足 `0 Warning` 要求，准予直接合入。  
3. **议题一（方案 B）裁决**  
   批准在 `src/io/ffmpeg_io.cpp` 具象类作用域内补全 `FfmpegDemuxer::seekToKeyframeBefore`：  
   * 去多态化移除 `IDemuxer` 虚基类后，`FfmpegDemuxer` 作为具体类必须完整承载 `seekToKeyframeBefore(int64_t video_stream_pts)` 的关键帧倒回寻道、BSF 状态复位与队列/时间戳守卫清理逻辑。  
   * 核准方案 B 实现代码，确保 `format_ == nullptr || video_stream_ < 0` 时返回 `err(ErrorCode::State, "demuxer is not open")`，寻道成功后调用 `av_bsf_flush`（若 `bitstream_filter_ != nullptr`）、清空 `pending_` 并重置 `drained_`、`bitstream_filter_flushed_`、`video_guard_` 与 `audio_guard_`，彻底消除 `test_fuzz_resilience.cpp`、`hw_transcode.cpp`、`hw_transcode_stages.cpp` 与 `gop_seek_scheduler.cpp` 的 `LNK2019` 未解析符号。  
4. **议题二与议题三裁决**  
   批准 Pure SLOC 顺延至 Wave 12.2-C，签发收网封板直通令：  
   * 坚决反对"拼行、删注或破坏可读性"的伪行数收敛。核准 Wave 12.2-B2 当前实测 `30,356` Pure SLOC 放行，将剩余瘦身配额顺延至 **Wave 12.2-C（Core、NLE 与 GUI 冗余注册机制整合及终局类型/SLOC 收口）**。  
   * 协调负责人督导合入方案 A 与方案 B 后，在 Windows 宿主机确认 MSVC `/std:c++latest /W4 /WX` 全量构建 0 错误 0 警告、双向 `rtx_tests.exe` 147/147 全绿及 `AuditTypes`（73 ≤ 75）通过后，**无需再次呈报中间委托，直接签署 Wave 12.2-B2 封板并即刻启动 Wave 12.2-C**。

---

# **一、 仓库最新代码事实穿透复核与定点修复范本**

## **1.1 阻断 A 修复范本：`src/gui/rtx_gui.cpp` 第 658 行 6 元组结构化绑定**

核查 `BrowseSpec` 6 元组定义（`int` 按钮 ID、`int` 控件 ID、`const wchar_t*` 过滤器、`DWORD` 标志、`bool` 保存模式、`bool` 媒体探测开关），修复循环如下：

```
// src/gui/rtx_gui.cpp (Line 658)
for (const BrowseSpec& b : kBrowseSpecs) {
    const auto& [btn_id, ctrl, filter, flags, save, probe] = b;
    (void)ctrl;
    (void)filter;
    (void)flags;
    (void)save;
    (void)probe;
    if (LOWORD(wParam) == btn_id) {
        onBrowse(hwnd, b);
        return 0;
    }
}
```

## **1.2 阻断 B 修复范本：`src/io/ffmpeg_io.cpp` 补全 `FfmpegDemuxer::seekToKeyframeBefore`**

穿透核验 `src/io/ffmpeg_io.cpp` 中 `FfmpegDemuxer` 成员（`format_`、`video_stream_`、`bitstream_filter_`、`pending_`、`drained_`、`bitstream_filter_flushed_`、`video_guard_`、`audio_guard_`），在 `FfmpegDemuxer` 类定义体或成员函数实现区补齐完整实现：

```
// src/io/ffmpeg_io.cpp: FfmpegDemuxer::seekToKeyframeBefore
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
    if (bitstream_filter_ != nullptr) {
        av_bsf_flush(bitstream_filter_);
    }
    pending_.clear();
    drained_ = false;
    bitstream_filter_flushed_ = false;
    video_guard_.reset();
    audio_guard_.reset();
    return {};
}
```

---

# **二、 Wave 12.2-B2 收网核验与 Wave 12.2-C 攻坚路线图**

## **2.1 Wave 12.2-B2 宿主机封板核验步骤**

1. 合入 `src/gui/rtx_gui.cpp` 与 `src/io/ffmpeg_io.cpp` 两处修复（确认仓内 0 个 `.bak` 文件）；  
2. Windows 宿主机执行全量构建与双向单元测试：

```
cmake --build --preset windows-msvc-ninja
.\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe
Push-Location [REDACTED_PATH]; .\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe; Pop-Location
cmake -P cmake/AuditTypes.cmake
cmake -P cmake/AuditSpdx.cmake
```

3. 确认 MSVC `/std:c++latest /W4 /WX` **0 Error(s), 0 Warning(s)**、`rtx_tests.exe` 双目录 **147/147 PASS**、`AuditTypes`（73 ≤ 75）与 `AuditSpdx` 全绿后，协调负责人直接归档 Wave 12.2-B2 封板记录并提交本地基线快照（严禁 `git push`）。

## **2.2 Wave 12.2-C 阶段指标控制矩阵**

| 评估维度 | Wave 12.2-B2 当前基线 | Wave 12.2-C 阶段目标 | 终局 (Phase 12 Complete) 目标 | 验证与控制手段 |
| :---- | :---- | :---- | :---- | :---- |
| **具名自定义类型数** | 73 个 | **≤ 50 个** | **≤ 35 个** | `cmake -P cmake/AuditTypes.cmake` 严格校验 |
| **非法类型分类** | 0 项 | **0 项** | **0 项** | 严禁 Polymorphic & Concurrency 污染 |
| **Pure SLOC 行数** | 30,356 行 | **≤ 26,500 行** | **≤ 23,000 行** | 剪除 Core/NLE/GUI 冗余注册与契约判空 |
| **构建与警告状态** | 2 处断环待定点修复 | **0 Errors / 0 Warnings** | **0 Errors / 0 Warnings** | MSVC `/std:c++latest /W4 /WX` 强制校验 |
| **单元测试通过率** | 147 / 147 PASS | **100% PASS (147/147)** | **100% PASS (147/147)** | 宿主机双路径执行 `rtx_tests.exe` |

## **2.3 Wave 12.2-C（第三战场：Core / NLE / GUI 整合与终局瘦身）推进目标**

* **核心任务**：整合 Core、NLE 工程与 GUI 模块中的冗余注册/绑定与重复结构，剪除内部非空引用契约下的重复判空防御；  
* **终局硬门禁**：全仓合法具名自定义类型压减至 **`≤ 35` 个**，全仓 Pure SLOC 压减至 **`≤ 23,000` 行**，保持零警告编译与全量单测 100% PASS。

