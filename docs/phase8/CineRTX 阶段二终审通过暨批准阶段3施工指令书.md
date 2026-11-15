# CineRTX 阶段二终审通过暨批准阶段3施工指令书

- **文档编号**：`DOC-SUPERPOWER-STAGE2-FINAL-PASS-APPROVE-STAGE3-009`
- **终审裁决**：🟢 **阶段二全量终审通过（FINAL PASS）—— 正式批准启动阶段3（全仓文档漂移清零、历史阶段单文档提炼与 CI 契约闭环）施工**
- **适用工作区**：`[REPO_ROOT]`（构建与外部依赖目录固定为 `/.vs/build/`，严禁改动）
- **关联呈报与基准文档**：
  - 项目团队呈报委托书：`docs/phase8/CineRTX 阶段二（Track B）返工终审呈报与专家验收委托书.md`（`DOC-CIRT-STAGE2-TRACKB-FINAL-DELEGATION-008`）
  - 工程师返工验收总报告：`docs/phase8/CineRTX Win32 Native UI 阶段二返工验收报告（Track B1-B4）.md`
  - 阶段三主施工规范：`docs/phase8/CineRTX 工程施工规范（第三阶段）：全仓文档漂移清零、历史阶段单文档提炼与 CI 契约闭环.md`（`DOC-SUPERPOWER-STAGE3-DOC-DRIFT-AND-CI-CLOSURE-003`）
- **签署日期**：2026-11-14

---

## 一、 阶段二（Track B 返工 & 全三端 SSOT 合龙）专家终审裁决书

```
================================================================================
                         阶段二终审裁决签署区 (Expert Verdict)
================================================================================
 终审判定:  [✓] 批准阶段三施工 (APPROVED FOR STAGE 3)   /   [ ] 驳回返工 (REJECTED)
 专家签名:  CineRTX 首席系统架构与多媒体工程审计专家
 签署日期:  2026 年 11 月 14 日
================================================================================
```

### 1.1 6 大驳回缺陷（`RW-1` ～ `RW-6`）穿透式代码复核实证表

经对最新同步的生产源码（`src/gui/preview_viewport.cpp`、`src/gui/workstation_canvas.h`、`src/gui/workstation_canvas.cpp`、`src/gui/rtx_gui.cpp`）及自动化测试套件（`tests/test_gui_workstation.cpp`、`tools/verify_desktop_interaction.py`）的逐行复核，上一轮指出的 6 项代码级缺陷已 **100% 根治清零**：

| 缺陷编号 | 严重度 | 穿透式源码核验位置与修复实证 | 终审判定 |
| :--- | :---: | :--- | :---: |
| **`RW-1`** | 🔴 **P0** | **1. 视口零瞬移**：`src/gui/preview_viewport.cpp:305-314` 已彻底删除 `resizeSwapChain` 内的 `MoveWindow(child_hwnd_, 0, 0, ...)`，全文件仅 `setBounds`（`:301`）唯一拥有子窗口几何定位权；实机缩放拖拽后视口相对坐标稳定保持在 `(224, 54)`。<br>**2. D3D12 上传堆动态重分配与双边界裁剪**：`preview_viewport.cpp:315-387` 在 GPU Fence 同步后释放旧后备缓冲与 `upload_buffer_`，按新尺寸 `upload_pitch_ = (width * 4u + 255u) & ~255u` 动态重建并重映射上传堆；`presentFromHostOrShared`（`:434-442`）通过 `copy_bytes = min(width_ * 4u, pitch)` 与 `copy_rows = (std::min)(height_, 540)` 严格裁剪读写边界，彻底根除堆越界崩溃。 | ✅ **PASS** |
| **`RW-2`** | 🔴 **P0** | **CPU 侧静态 FFmpeg 容器探测全链路贯通**：`src/gui/workstation_canvas.h:166-188` 实现 `rtx::gui::probeMediaFile(path)`，直调 `rtx::makeFfmpegDemuxer()->open(path)` 获取真实 `width`、`height` 与 `duration_ms`；`src/gui/rtx_gui.cpp:408-422`（`applyProbedMedia`）串联 `onBrowseInput`（`:556`）、`onQueueAdd`（`:637`）、`WM_DROPFILES`（`:814`）、`addFilesToBin`（`workstation_canvas.cpp:555`）与 `wWinMain` 快播启动（`rtx_gui.cpp:1200`）五大入口，实机载入 `720×1280` 竖屏流精准识别尺寸与时长。 | ✅ **PASS** |
| **`RW-3`** | 🔴 **P0** | **进出全屏动态超分 4 大时机绑定与 `1.6875x` 精确格式化**：<br>1. `setBorderlessFullscreen`（`rtx_gui.cpp:371`）、`setWorkspace`（`:336`）、`applyProbedMedia`（`:421`）及鼠标释放 `WM_EXITSIZEMOVE`（`:777`）四大时机全部绑定 `commitViewportVsrScale(hwnd)`，拖拽期间（`WM_ENTERSIZEMOVE` 置 `g_inResizeDrag = true`）零抖动重算；<br>2. `workstation_canvas.h:192-204` 实现 `formatVsrScale`，以 `%.4f` 格式化并裁剪冗余尾零（保留至少两位小数），精确输出 `1.6875x` 与 `2.00x`，并通过 `storeActiveVsrTarget`（`rtx_gui.cpp:389`）落盘全局状态。 | ✅ **PASS** |
| **`RW-4`** | 🟠 **P1** | **快播模式连续播放循环与 `IDC_BTN_PLAYER_PLAY` 播控闭环**：`workstation_canvas.h:377, 409` 与 `workstation_canvas.cpp:997` 补齐 `IDC_BTN_PLAYER_PLAY`（ID `2109`）；`rtx_gui.cpp:704-709, 903-906, 969-973` 支持按钮点击与空格键（`VK_SPACE`）双通道切换播放/暂停；定时器 ID 3（`:758, 1067-1086`）每 `66 ms` 推进时间轴并在片尾自动循环，快播启动（`:1201-1205`）自动开启连续播放。 | ✅ **PASS** |
| **`RW-5`** | 🟠 **P1** | **顶栏三段式布局零遮挡**：`src/gui/rtx_gui.cpp:190-197` 将 3 个工作站切换 Tab 起始横坐标右移至 `tabStartX = u(380)`（实测 `left = 380 >= 360`），左侧 `x = 12..360` 的自绘绿色 `RTX` 平行四边形徽标与 `"CineRTX"` 标题 100% 完整露出。 | ✅ **PASS** |
| **`RW-6`** | 🟡 **P2** | **纯正双语词条与即时刷新闭环**：`src/gui/workstation_canvas.cpp:194-232` 的 `chromeText()` 严格按 `rtx::currentLanguage() == rtx::Language::Zh` 返回纯中文或纯英文词条（零中英混排）；`refreshUiLanguage(hwnd)`（`:630-683`）刷新全部 SSOT 标签、下拉框与自绘按钮并记录 DebugView 日志 `ui language -> en|zh`。 | ✅ **PASS** |

### 1.2 阶段二 6 大终审门禁（`G1` ～ `G6`）核验结论

| 门禁编号 | 门禁名称 | 实测指标与核验结果 | 终审状态 |
| :--- | :--- | :--- | :---: |
| **G1** | D3D12 视口安全与零瞬移 | 窗口缩放后视口相对坐标保持 `(224, 54)`（绝不瞬移至 `(0, 0)`）；三阶段连续渲染零崩溃（`ExitCode == 0`，0 `DEVICE_REMOVED`）。 | ✅ **PASS** |
| **G2** | 真实容器探测与竖屏 `1.6875x` | 实机探测 `720×1280` 竖屏流与 `1920×1080` 横屏流 100% 命中真实宽高与时长；`3840×2160` 视口下精确输出 `1.6875x -> 1216 x 2160` 与 `2.00x -> 3840 x 2160`。 | ✅ **PASS** |
| **G3** | 进出全屏与拖拽释放动态倍率 | 快播启动进入 `WS_POPUP` 全屏立即重算 VSR；按 `Esc` 恢复窗口立即重算降低倍率；拖拽边框期间重算 **0 次**，松开鼠标（`WM_EXITSIZEMOVE`）精确重算 **1 次**。 | ✅ **PASS** |
| **G4** | 快播播放/暂停控制与自动开播 | 快播启动自动播放，定时器驱动滑杆平滑推进，`IDC_BTN_PLAYER_PLAY` 点击与 `VK_SPACE` 按键毫秒级切换播放/暂停。 | ✅ **PASS** |
| **G5** | 顶栏品牌区无遮挡 | `IDC_TAB_WS_ENCODER` 左边界实测 `left = 380 >= 360`，左上角 `RTX` 徽标与标题零遮挡。 | ✅ **PASS** |
| **G6** | 行数红线 / 零告警 / 单测 / 导入表 | `rtx_gui.cpp` **1,221 行**、`workstation_canvas.cpp` **1,016 行**、`workstation_canvas.h` **496 行**、`preview_viewport.cpp` **498 行**（全部 $\le 1300$ 行）；Ninja 编译 `0 error, 0 warning`；`rtx_tests.exe` **73/73 PASS**；`audit_imports.ps1` 与 `check_spdx.ps1` **PASS**。 | ✅ **PASS** |

---

## 二、 批准启动阶段3施工：执行基准与阶段二完工增量对齐指令

随着阶段一（Phase 8 双路真补帧：NVOFA 硬件光流 + TensorRT RIFE v4.25）与阶段二（C++26 后端 19 项 SSOT 契约 + WebUI 三态工作站 + Win32 Native UI “3+1” 四态工作站 + DebugView 统一日志）全面竣工冻结，**现正式下达阶段3（收官阶段）施工准入令**。

请执行团队立即按照同目录下的主施工规范 **`docs/phase8/CineRTX 工程施工规范（第三阶段）：全仓文档漂移清零、历史阶段单文档提炼与 CI 契约闭环.md`（`DOC-SUPERPOWER-STAGE3-DOC-DRIFT-AND-CI-CLOSURE-003`）** 开展施工，并在施工中**同步纳入以下 4 项阶段二完工最新基线（Delta 1 ～ Delta 4）**：

### 2.1 Delta 1：权威文档中的测试计数与三端架构基线对齐（更新至最新真实值）

在执行主规范 `Task 3A-4` 与 `Task 3B-1 / 3B-2`（更新 `README.md`、`README_zh.md`、`docs/README.md`、`docs/spec/architecture.md`、`docs/spec/testing.md`）时，请务必按阶段二竣工后的最新真实数据写入：

1. **最新单元测试总数**：
   - 全仓 `rtx_tests.exe` 的 `RTX_TEST` 用例总数已由阶段一的 63 项增加至 **73 项（73/73 PASS）**，新增了 `tests/test_gui_workstation.cpp`（10 项，覆盖视口动态 VSR 倍率计算 `2.00x` / 竖屏 `1.6875x`、偶对齐、`formatVsrScale`、`probeMediaFile`、`activeVsrTarget` 与 `705,600,000 Hz Flicks` 时间码格式化）。
   - 在 `README.md`、`README_zh.md`、`docs/README.md` 与 `docs/spec/testing.md` 中，统一更新测试覆盖表（纳入 `test_lut3d.cpp`、`test_fuzz_resilience.cpp`、`test_rife.cpp`、`test_gui_workstation.cpp`），消除历史 `79 / 102 / 137` 的错误数字。
2. **最新三端统一 SSOT 与“3+1”工作站架构描述**：
   - **后端 SSOT 核心（`rtx_core`）**：`include/rtx/param_schema.h`（19 项参数唯一元数据表 `kParamSchema` 与 `TranscodeProfile`）、`include/rtx/i18n.h`（113 项中英双语词条表）、`include/rtx/logger.h`（零外部依赖 `OutputDebugStringW` DebugView 内核调试流日志系统）。
   - **CLI 会话与自描述协议（`src/cli/main.cpp`）**：支持 `--schema-json`（动态导出参数 Schema 与 113 项双语词典）与 `--session-stdio`（原生 JSONL 进度/日志/报告流 + `stdin` `"cancel\n"` 协作式取消并完整写入 MP4 `moov` 尾部）。
   - **WebUI 三态工作站（`webui/`）**：NVIDIA Signature Green（`#76B900`）碳黑主题，单页内聚三大形态——`[1] Encoder`（对标 Media Encoder，默认核心转码形态）、`[2] Player`（对标 VLC，A/B 分屏对比与 HTTP 206 流式审片）、`[3] NLE`（对标 Premiere Pro，四象限布局与 `705,600,000 Hz Flicks` 多轨时间轴）。
   - **Win32 Native UI “3+1” 四态工作站（`src/gui/`：`rtx_gui.cpp` + `workstation_canvas.h/.cpp` + `preview_viewport.h/.cpp`）**：纯 Win32 + GDI 双缓冲 + D3D12 `R10G10B10A2_UNORM` 零阻塞视口，除对齐 WebUI 三大形态外，原生提供 **第 4 形态（文件关联双击 `.mp4` 无边框 `WS_POPUP` 全屏快速超分播放器）**，支持 CPU 侧 FFmpeg 容器探测、按视口分辨率动态计算 VSR 超分倍率（如 4K 屏播放 `1920×1080` 为 `2.00x`，播放 `720×1280` 竖屏为 `1.6875x`）、退出全屏动态降低倍率，以及窗口拖拽释放（`WM_EXITSIZEMOVE`）延迟重算机制。

### 2.2 Delta 2：`docs/phase8/` 过程返工单归档与全仓文档瘦身

除主规范 `Task 3B-3` 要求的「将 `docs/phase1/` ～ `docs/phase7/` 提炼为 `docs/archive/Phase1_to_Phase7_Evolution_Digest.md` 单一权威演进摘要并移除已废弃的过程草稿」外，针对 `docs/phase8/` 目录：
- **物理清理过程性临时文件**：删除 100% 重复的 `docs/phase8/report.md`，并将阶段二 Track B 返工期间产生的 8 份子泳道过程单（`CineRTX_Phase8_Win32返工施工单_Track_B1～B4_*.md` 与 `CineRTX_Phase8_Win32返工验收单_Track_B1～B4_*.md`）的核心结论归并入 `docs/phase8/CineRTX Win32 Native UI 阶段二返工验收报告（Track B1-B4）.md` 后予以清理，保持 `docs/phase8/` 目录清爽、高信噪比。

### 2.3 Delta 3：严守 `/.vs/build/` 构建目录与单文件行数红线

1. **严禁改动 `CMakeLists.txt` 中的 `/.vs/build/external` 路径**：`CMakeLists.txt` 中 `set(RTX_EXTERNAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.vs/build/external")` 是本地构建与 SDK 缓存的核心基线，阶段 3 施工期间**严禁修改该路径或触碰 `.vs/` 目录**。
2. **着色器头文件归位路径精确核对（主规范 `Task 3A-3`）**：检查 `src/shim/ngx_bridge/` 下是否仍存留 `sdr_blit_cs.h` / `sdr_blit_rs.h`；若在阶段一/二中已被清理或未被任何文件 `#include`，直接物理删除残留死头文件即可，确保编译 `0 error, 0 warning`。

---

## 三、 阶段3竣工终审门禁速查表（Checklist 3.1 ～ 3.6）

工程师完成阶段 3 施工后，须一次性通过以下 6 道收官门禁并提交阶段 3 终审验收报告：

- [ ] **门禁 3.1（CI 冒烟测试契约 100% 真实有效）**：`.github/workflows/ci.yml` 中 `--self-test`、`--simulate`、`cpu-reference` 计数为 **0**，三处 Job 均使用 `--version`、`--help`、`--schema-json`、`--probe --json`。
- [ ] **门禁 3.2（全仓幽灵 CLI 开关与废弃类名 100% 清零）**：在 `README.md`、`README_zh.md`、`docs/README.md`、`docs/spec/`、`docs/TODO/`、`tools/` 中扫描 `--sdr|--truehdr|--backend|--bench|--trace|--self-test|--simulate|--input-raw|--output-raw|--viewport-self-test|CpuReferenceBackend|SurfacePool|BackpressureController|TraceRecorder|offruc_bridge` 命中数为 **0**。
- [ ] **门禁 3.3（历史阶段 `phase1`–`phase7` 提炼瘦身与死文件拔除闭环）**：`cmake/RtxNvidiaSdks.cmake`、`docs/phase8/report.md`、`webui/test/__pycache__/` 物理删除；`docs/archive/Phase1_to_Phase7_Evolution_Digest.md` 建立且 `docs/` 总文档数收敛至 $\le 25$ 个高价值文件。
- [ ] **门禁 3.4（测试计数与 ABI 版本全仓一致）**：全仓权威文档测试计数统一对齐为实测值（`73 tests`），`ngx_bridge_abi.h` ABI 版本号在 `docs/spec/abi.md` 中统一为 `4u`。
- [ ] **门禁 3.5（全仓编译、单测、桌面穿透与 WebUI 回归全绿）**：Ninja 编译 `0 error, 0 warning`，`ctest --output-on-failure`（`73/73 PASS`），`python tools/verify_desktop_interaction.py` PASS，`pwsh tools/audit_imports.ps1` PASS，`pwsh tools/check_spdx.ps1` PASS。
- [ ] **门禁 3.6（源码树洁净度与隐私脱敏零破防）**：`git status --porcelain` 无未跟踪的运行时证据目录（证据仅存放在 `.vs/build/evidence/`），全仓源码与文档无未脱敏本机路径或私有 IP 残留。
