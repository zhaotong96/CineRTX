# CineRTX Phase 12 Wave 12.2-B2 编译断环与链接缺失修复汇报

- **文档编号**：`CINERTX-P12-W12.2-B2-FIX-REPORT-044`
- **对应终审裁决书**：`docs/phase12/43_CineRTX Phase 12 Wave 12.2-B2 编译断环与链接缺失终审裁决书暨Wave12.2-C推进路线.md`（`CINERTX-P12-RULING-043`）
- **关联委托书**：`docs/phase12/42_CineRTX Phase 12 Wave 12.2-B2 编译断环与链接缺失专家咨询委托书.md`（`CINERTX-P12-DELEGATION-042`）
- **汇报日期**：2026-11-23
- **执行范围**：严格按 43 号终审裁决书方案 A 与方案 B，完成 `src/gui/rtx_gui.cpp` 与 `src/io/ffmpeg_io.cpp` 两处外科手术式定点修复。

---

## 1. 定点修复明细

### 1.1 阻断 A 修复（方案 A）：`src/gui/rtx_gui.cpp` 第 658 行 6 元组结构化绑定（消除 MSVC C3449）
- **问题根因**：`BrowseSpec` 定义为 6 元素元组 `std::tuple<int, int, const wchar_t*, DWORD, bool, bool>`，原循环仅解构 4 个标识符触发 `error C3449`。
- **修复落实**：按 43 号裁决书 §1.1 补全全部 6 个结构化绑定标识符 `[btn_id, ctrl, filter, flags, save, probe]`，并对未使用的 5 个绑定显式执行 `(void)` 抑制，保持 `/W4 /WX` 零告警：
  ```cpp
  for (const BrowseSpec& b : kBrowseSpecs) {
      const auto& [btn_id, ctrl, filter, flags, save, probe] = b;
      (void)ctrl;
      (void)filter;
      (void)flags;
      (void)save;
      (void)probe;
      if (id == btn_id) {
          onBrowse(hwnd, b);
          return 0;
      }
  }
  ```

### 1.2 阻断 B 修复（方案 B）：`src/io/ffmpeg_io.cpp` 补全 `FfmpegDemuxer::seekToKeyframeBefore`（消除 LNK2019 / LNK1120）
- **问题根因**：`include/rtx/io.h` 去多态化移除 `IDemuxer` 后，具象类 `FfmpegDemuxer` 声明了 `seekToKeyframeBefore(int64_t video_stream_pts)`，但实现未迁入 `src/io/ffmpeg_io.cpp`，导致 `test_fuzz_resilience.cpp`、`hw_transcode.cpp`、`hw_transcode_stages.cpp` 与 `gop_seek_scheduler.cpp` 链接期报 `LNK2019`。
- **修复落实**：按 43 号裁决书 §1.2 在 `src/io/ffmpeg_io.cpp` 第 408 行补全 `FfmpegDemuxer::seekToKeyframeBefore(int64_t video_stream_pts)`：
  - 未打开或无视频流时返回 `err(ErrorCode::State, "demuxer is not open")`；
  - 将 `std::numeric_limits<int64_t>::min()` 归零后调用 `av_seek_frame(format_, video_stream_, target, AVSEEK_FLAG_BACKWARD)`，失败返回 `err(ErrorCode::Io, ..., avErrorString(status))`；
  - 寻道成功后若 `bitstream_filter_ != nullptr` 则调用 `av_bsf_flush(bitstream_filter_)`，清空 `pending_`，复位 `drained_`、`bitstream_filter_flushed_`、`video_guard_` 与 `audio_guard_`。

---

## 2. 门禁与静态审计复核

| 审计维度 | 修复前（41 号文） | 修复后实测值 | 门禁要求 | 状态 |
| :--- | :---: | :---: | :---: | :---: |
| 全仓命名类型总量（`AuditTypes`） | 73 | **73**（`<= 75`） | `<= 75` | **PASS** |
| 非法分类 `Polymorphic-Boundary` / `Concurrency-Invariant` | 0 / 0 | **0 / 0** | `0 / 0` | **PASS** |
| 全仓 `std::get<` / `#include <sstream>` | 0 / 0 | **0 / 0** | `0 / 0` | **PASS** |
| 全仓 `OldFormat` / `AuditSpdx` 缺失 | 0 / 0 | **0 / 0** | `0 / 0` | **PASS** |
| 全仓 Pure SLOC（`AuditSloc`） | 30,356 | **30,382**（+26 行修复体） | 顺延至 Wave 12.2-C（`<= 26,500`） | **已核准顺延** |
| `CineRTX/` 仓内 `.bak` 残留 | 0 | **0**（2 份旧文件已迁至外部归档目录） | `= 0` | **PASS** |

---

## 3. 文件替换与验证边界声明

- **外部归档替换（2 个文件）**：
  - `src/gui/rtx_gui.cpp`（旧版本已移至 `work/_CineRTX_bak_archive/rtx_gui.cpp.b2fixbak`）
  - `src/io/ffmpeg_io.cpp`（旧版本已移至 `work/_CineRTX_bak_archive/ffmpeg_io.cpp.b2fixbak`）
- **验证边界诚实声明**：上述 2 个文件分别依赖 Windows Win32 SDK 与 FFmpeg libavformat/libavcodec 头文件，在当前 Linux 沙箱环境中**仅执行了静态语法/成员/签名核验与门禁脚本审计**，尚未运行 Windows MSVC 真实编译。

---

## 4. Windows 宿主机终审封板复测命令（按 43 号文 §2.1）

```powershell
cmake --build --preset windows-msvc-ninja
.\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe
Push-Location [REDACTED_PATH]; .\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe; Pop-Location
cmake -P cmake/AuditTypes.cmake
cmake -P cmake/AuditSpdx.cmake
```

确认 MSVC `/std:c++latest /W4 /WX` 0 Error 0 Warning、双目录 `rtx_tests.exe` 147/147 PASS 后，即可按 43 号终审裁决书直接完成 Wave 12.2-B2 封板并进入 Wave 12.2-C。
