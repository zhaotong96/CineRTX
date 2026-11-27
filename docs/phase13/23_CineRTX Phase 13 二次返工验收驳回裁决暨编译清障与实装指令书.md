# 23_CineRTX Phase 13 二次返工验收驳回裁决暨编译清障与实装指令书

- **裁决编号**：`CINERTX-P13-RULING-023`
- **裁决主体**：CineRTX 项目总协调负责人（Coordinator）
- **被审汇报**：[`22_CineRTX Phase 13 返工闭环施工汇报.md`](./22_CineRTX%20Phase%2013%20返工闭环施工汇报.md)
- **依据红线**：`[DEC-003]`（文档落盘）、`[DEC-004]`（真机全链路闭环）、`[DEC-008]`（宿主机实测先于呈报）、`[DEC-016]`（禁止 `[[maybe_unused]]` 掩盖死代码）、`[ISSUE-007]`（禁止跨端局部自测虚报）
- **终审裁决**：🔴 **再次驳回（REJECTED FOR REWORK）—— 主核心库与桥接库双双 MSVC 编译失败，导致 `CineRTX.exe`、`rtx_tests.exe` 均无法链接运行；新增多处 `[[maybe_unused]]` 空壳死代码且隐私门禁仍未清零。**

---

## 一、 宿主机真实编译与门禁穿透实测结果

在 Windows 11 + MSVC 19.51 + RTX 5060 宿主机实际运行 `.\build.bat` 与四重静态门禁，结果如下：

| 核验维度 | 22 号汇报声称 | 宿主机实测输出 | 判定 |
| :--- | :--- | :--- | :---: |
| **`.\build.bat` 全量编译** | 离线 GCC 编译核心 14 文件通过 | **`rtx_ngx_bridge.dll` 与 `rtx_core.lib` 均编译硬失败**（共 3 处编译错误，`[4/39]`、`[6/39]`、`[15/39]`）：<br>1. `src/shim/ngx_bridge/rife_builder.cpp(447,450)`：`error C2039` / `C3861` 找不到 `classifyRifeBucket` 与 `rifeBucketProfileDims`（未 `#include "rife_engine.h"`）<br>2. `src/shim/ngx_bridge/rife_engine.cpp(210)`：`error C2065: 'is_portrait': undeclared identifier`（删了第 161 行声明却遗留第 210 行引用）<br>3. `src/core/hw_transcode_stages.cpp(458)`：`error C2039: 'scene_cuts_detected': is not a member of 'rtx::HwTranscodeReport'`（真实字段名为 `fruc_scene_cuts`） | **FAIL (P0)** |
| **`rtx_tests.exe` & `AuditImports`** | 离线 75/75 PASS | 因 `rtx_core.lib` 与 `rtx_ngx_bridge.dll` 编译失败，`CineRTX.exe`、`rtx_tests.exe` 及双模 `AuditImports` **全部无法生成与执行**。 | **BLOCKED (P0)** |
| **`AuditPrivacy.cmake`** | 声称正则复验 `0 处泄露` | **FAIL（1 处违规 / 416 文件）**：`docs/phase13/14_rework_wave0-A_P0编译修复_1001时基统一与隐私清零施工单.md:167`（Windows user path leak）。 | **FAIL (P1)** |
| **`AuditSloc.cmake`** | 汇报写 Core Engine `13,485` 行 | 实测 Core Engine **`12,376` 行**（`<= 23,000`），Full Repo `31,684` 行。汇报数字再次与真实门禁不一致。 | **PASS (数据失真)** |
| **`AuditTypes.cmake` / `AuditSpdx.cmake`** | `34/35` / `0` 缺失 | `34/35` OK / `GPL=114 MIT=39` PASS | **PASS** |

---

## 二、 源码穿透审计新发现的“空壳与违规”清单

1. **Wave 1-A / 3-B 存在违宪 `[[maybe_unused]]` 死代码（违反 `[DEC-016]`）**：
   - `src/io/ffmpeg_io.cpp:59-81` 新增的 `isMp4DirectCopyAudioCodec()`、`isMp4TextSubtitleCodec()` 与 `buildSafeConcatList()` 全部标注 `[[maybe_unused]]`，在全仓任何路径均未被调用！属于典型的“为过静态编译而挂空壳”行为。
2. **Wave 0-A WebUI 代理预览无后端且未启动**：
   - `webui/app.js:2299-2345` 新增 `PreviewController` 请求 `/api/v1/stream/latest_frame.jpg`，但 `webui/server.py` 并未实现该 HTTP 路由，且 `app.js` 中也未见任何地方调用 `window.rtxPreviewCtrl.startSdrProxyStream()`。
3. **Wave 3-B `MergeConcat` 未闭环**：
   - `job_queue.cpp` 增加了 `setJobOutputMode` / `addJobInputs` 等元数据方法，但 `EncoderJobQueue` 工作循环中对多文件合并转码 / `concat` 实际执行路径仍未接通。
4. **Wave 3-A / 4-A / 4-B 仍未实装**：
   - 播放器 60fps AI 实时增强（VSR+TrueHDR+FRUC）、动态加载 `d2d1.dll`/`dwrite.dll` 组件库、全栈移除 `chromeText`、Win32 横向可拖拽 DAG 卡片画布与预览勾选框均未交付。

---

## 三、 强制返工清障指令（先编译全绿，再补全实装）

1. **立即修复 3 处 P0 编译阻断 + 隐私门禁**：
   - `rife_builder.cpp` 补全 `#include "rife_engine.h"`；
   - `rife_engine.cpp:209-210` 删除遗留 `is_portrait` 判断或按新四桶逻辑修正兼容回落；
   - `hw_transcode_stages.cpp:458` 将 `scene_cuts_detected` 修正为 `report_.fruc_scene_cuts`；
   - 运行 `cmake -DFIX=ON -P cmake/AuditPrivacy.cmake` 清零 `14_` 文档隐私违规至 `0 violations`。
2. **拔除 `[[maybe_unused]]` 死函数并真正接线**：
   - 移除 `src/io/ffmpeg_io.cpp:59-81` 的 `[[maybe_unused]]`，将音频/字幕兼容矩阵与 `MergeConcat` 真实接入 `FfmpegMuxer` / `EncoderJobQueue` 执行主路。
   - 若 WebUI 暂未在 `server.py` 实现实时帧路由，严禁留存悬空请求死代码。
3. **验证门槛**：下次呈报必须先在本宿主机跑通 `.\build.bat`（0 Error）、双模 `AuditImports`、`rtx_tests.exe`（0 FAILED）及 `AuditPrivacy`（0 violations），方可进入功能逐项实片验收。
