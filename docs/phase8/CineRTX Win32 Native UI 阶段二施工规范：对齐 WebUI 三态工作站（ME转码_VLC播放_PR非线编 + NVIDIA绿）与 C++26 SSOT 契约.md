# CineRTX Win32 Native UI 阶段二施工规范：对齐 WebUI 三态工作站（ME转码 / VLC播放+全屏快播 / PR非线编 + NVIDIA绿）与 C++26 SSOT 契约

**文档标识**：`DOC-SUPERPOWER-STAGE2-TRACKB-WIN32-FOUR-MODES-NVGREEN-006`  
**演进阶段**：Stage 2 · Track B（WebUI 第二轮三态工作站已圆满验收冻结 $\to$ **正式启动纯 Win32 Native UI 对齐施工，含第 4 形态“双击 MP4 无边框全屏快速超分播放器”**）  
**遵循方法论**：`SuperPower` 工程施工范式（`Plan` + `Tasks` + `Checklists` 三位一体，遵循 SOLID 六大准则、KISS、奥卡姆剃刀、高可读高可维护原则）  
**关联基准文件**：
- **WebUI 视觉与交互黄金参照（已冻结）**：[`webui/index.html`](../../webui/index.html)、[`webui/app.css`](../../webui/app.css)、[`webui/app.js`](../../webui/app.js)、[`webui/state.json`](../../webui/state.json)、`webui/ACCEPTANCE_STAGE2.md`
- **C++26 后端 SSOT 契约（已冻结）**：[`include/rtx/param_schema.h`](../../include/rtx/param_schema.h)、[`src/core/param_schema.cpp`](../../src/core/param_schema.cpp)、[`include/rtx/i18n.h`](../../include/rtx/i18n.h)、[`src/core/i18n.cpp`](../../src/core/i18n.cpp)、[`include/rtx/logger.h`](../../include/rtx/logger.h)、[`src/core/logger.cpp`](../../src/core/logger.cpp)
- **Win32 施工目标文件**：[`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp)、[`src/gui/preview_viewport.h`](../../src/gui/preview_viewport.h)、[`src/gui/preview_viewport.cpp`](../../src/gui/preview_viewport.cpp)
**适用角色**：执行工程师 2（Win32 原生 GUI 泳道）

---

## 第一部分：WebUI 第二轮返工专家终审裁决（WebUI Sign-Off Verdict）

### 1.1 终审结论：**WebUI 第二轮返工全项达标（PASS），无需第三轮返工，正式冻结并移交 Win32 阶段施工**

经对 `webui/report.md`、`webui/ACCEPTANCE_STAGE2.md` 及 `webui/` 全目录代码的穿透式复核，四道第二轮门禁（`R2.1–R2.4`）与首轮底层门禁（`2.1–2.3`）全部高标准通过：

| 门禁编号 | 核验项 | 代码级实证与复核结果 | 终审判定 |
| :--- | :--- | :--- | :---: |
| **R2.1** | **NVIDIA 绿（`#76B900`）工业视觉体系** | [`webui/app.css`](../../webui/app.css)（第 10–40、74–86、258–269、315–321 行）完整落地 `--accent: #76b900`、`--bg: #0b0d0b`、`--panel: #131613` 碳黑绿标体系；旧蓝色系在全目录 22 个文本文件中 **0 命中**。 | ✅ **PASS** |
| **R2.2** | **ME / VLC / PR 三大工作站形态与 HTTP 206 媒体流** | [`webui/index.html`](../../webui/index.html)（562 行）与 [`webui/app.js`](../../webui/app.js)（1,612 行）实现单状态树驱动的三态切换（`Alt+1/2/3`），`verify_round2.mjs` 34/34 断言全绿；[`webui/server.py`](../../webui/server.py)（第 333–431 行）实装 `GET /api/media`（支持 `206 Partial Content`、`Accept-Ranges: bytes`、越界 `416`）。 | ✅ **PASS** |
| **R2.3** | **SSOT 19 项参数、113 词条与协作取消零退化** | `GET /api/schema` 唯一词典与参数源零重复；协作取消在进度 32.4% 触发 `stdin` `"cancel\n"`，子进程优雅退出且输出 MP4 含完整 `moov` box。 | ✅ **PASS** |
| **R2.4** | **目录卫生与 PEP 8 多行可读性** | 已物理删除 `webui/report.md.txt`，将旧 `ACCEPTANCE.md` 并入 `webui/ACCEPTANCE_STAGE2.md` 附录 A；[`webui/server.py`](../../webui/server.py)（547 行）彻底清除了单行分号挤压（tokenizer 扫描 0 命中）；`webui/index.html:319` `ctl-gpu` 静态属性已修正为 `max="15"`。 | ✅ **PASS** |

---

## 第二部分：Plan（Win32 Native UI 顶层架构与“三态工作站 + 第 4 形态全屏快播”规范）

### 2.1 本阶段核心目标
当前全仓唯一尚未对齐 SSOT 契约与多工作站形态的模块为 [`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp)（1,116 行）。本阶段由 **Win32 工程师（工程师 2）** 在不引入任何外部 GUI 框架（零 Qt、零 ImGui、零 WebView2、零 `dcomp.dll`）的前提下，使用 **纯 Win32 原生控件 + GDI 双缓冲自绘 + 既有 D3D12 `PreviewViewport`（`R10G10B10A2_UNORM` 零阻塞交换链）**：
1. **彻底根除 `rtx_gui.cpp` 现存的 6 大契约漂移与隐形控件 Bug**（100% 接入 `include/rtx/param_schema.h`、`include/rtx/i18n.h`、`include/rtx/logger.h`，闭合门禁 2.1 与 2.4）；
2. **对齐 WebUI 的 NVIDIA Studio Carbon-Black + Signature Green（`#76B900` / `RGB(118, 185, 0)`）工业暗黑主题**；
3. **在原生 Win32 窗口内实现完整的“3 + 1”四大工作形态**：
   - **形态一（`WorkspaceMode::Encoder`）**：`[1] 视频转码器 · Media Encoder [CORE]`（默认无参启动形态）；
   - **形态二（`WorkspaceMode::Player` 窗口态）**：`[2] 超分播放器 · RTX Player`（带顶栏与底部 VLC 播控栏的窗口化审看播放器）；
   - **形态三（`WorkspaceMode::Nle`）**：`[3] 非线性编辑 · RTX NLE Studio`（PR 四象限 + GDI 双缓冲四轨 `Flicks` 时间轴）；
   - **形态四（`WorkspaceMode::QuickFullscreenPlayer` — 即 `[2] 播放器形态` 的无边框全屏快速播放形态）**：
     - **触发入口**：当用户将 Windows 视频文件（`.mp4` / `.mkv` / `.mov` / `.ts` 等）的默认打开方式设为 `CineRTX.exe` 并**双击视频文件启动**时（即命令行传入单个非 `-` 开头的文件路径 `CineRTX.exe "D:\video.mp4"`），或者在 `[2] RTX Player` 中按 `F11` / `Alt+Enter` / 双击画面时，直接进入**无标题栏、无边框的全屏快速播放形态（Borderless Fullscreen）**并立即开始播放！
     - **视口分辨率驱动的动态自适应超分倍率（Viewport-Driven Dynamic VSR Scaling）**：
       超分的目标分辨率与倍率严格取决于**当前播放视口（窗口/全屏）的物理像素分辨率**与**源视频宽高比的内切缩放比**：
       $$\text{scale} = \min\!\left(\frac{W_{\text{viewport}}}{W_{\text{src}}},\; \frac{H_{\text{viewport}}}{H_{\text{src}}}\right)$$
       * 例如在 `3840 × 2160`（4K）显示器全屏播放 `1920 × 1080` 横屏视频时：$\text{scale} = \min(3840/1920,\; 2160/1080) = \mathbf{2.0\times}$（目标超分输出 `3840 × 2160`）；
       * 在 `3840 × 2160` 全屏播放 `720 × 1280` 竖屏视频时：$\text{scale} = \min(3840/720,\; 2160/1280) = \min(5.3333,\; 1.6875) = \mathbf{1.6875\times}$（目标超分输出 `1216 × 2160`，保持竖屏比例零拉伸）；
       * 当按 `Esc` / `F11` / 双击**退出全屏回到普通窗口**时，立即根据缩小后的窗口视口分辨率动态降低超分倍率；
       * 当用户用鼠标**拖拽窗口边框改变窗口大小时**，拖拽过程中（`WM_SIZE` 期间）仅做交换链平滑拉伸、**严禁每像素重建 NGX VSR 纹理**；必须在**鼠标左键释放完成拖拽的瞬间（Win32 `WM_EXITSIZEMOVE` 消息，即等价于 `MouseReleaseEvent`）**才重新计算新的超分倍率并切换 VSR 目标分辨率！

### 2.2 架构红线与强制不变量（Architectural Invariants）
1. **SSOT 零重复定律（Zero Duplication）**：
   - `rtx_gui.cpp` **严禁**再手写 `std::clamp` 参数范围校验、**严禁**手写 `_remaster_av1.mp4` 后缀拼接、**严禁**手写 `"models/rife_v4.25.engine"` 路径探测、**严禁**手写 `isZh ? L"..." : L"..."` 双语分支！
   - 界面参数收集必须统一构造 `rtx::TranscodeProfile`，调用 `rtx::validateAndNormalizeProfile(profile, false)` 与 `rtx::profileToHwOptions(profile)`；自动输出路径必须调用 `rtx::deriveOutputPath()`；下拉框文案必须 100% 调用 `rtx::trW(rtx::StrId::Opt...)`。
2. **彻底拔除 8-bit SDR H.264 后门**：
   - 从 `rtx_gui.cpp` 中彻底删除 `H.264 High (8-bit SDR)` 下拉项、`isH264` 禁用分支与 `_remaster_h264.mp4` 分支，输出编码器仅保留 `AV1 10-bit`（索引 0）与 `HEVC Main10`（索引 1）。
3. **单文件行数上限红线（$\le 1,300$ 行）与模块化**：
   - 全仓单文件行数红线为 **所有 `.cpp` / `.h` 文件严格 $\le 1,300$ 行**。
   - 为避免加入四态工作站与 GDI 自绘时间轴后 `src/gui/rtx_gui.cpp` 突破 1,300 行，**推荐将纯 Win32 GDI 绿标顶栏、自绘按钮、动态超分倍率计算器与 NLE 四轨 Flicks 时间轴绘制逻辑拆分为独立内部头文件 `src/gui/workstation_canvas.h`（或 `src/gui/workstation_canvas.cpp`）**，确保每个源文件均控制在 `750 ~ 1,150` 行。
4. **PE 导入表与 `/MT` 静态 CRT 纯净度红线**：
   - 严禁引入任何新系统 DLL 或第三方库；`CineRTX.exe` 的 PE 导入表必须继续保持 `0` 个 `MSVCP140.dll` / `VCRUNTIME140*.dll` / `WS2_32.dll` / `dcomp.dll`，通过 `pwsh tools/audit_imports.ps1` 门禁。
5. **构建目录约定**：
   - 仓库外部依赖包位于 `/.vs/build/external/`（已在 [`CMakeLists.txt`](../../CMakeLists.txt) 第 16 行配置为 `${CMAKE_SOURCE_DIR}/.vs/build/external`），严禁将硬编码绝对路径带入任何源文件或 `CMakeLists.txt`。

### 2.3 文件所有权矩阵（File Ownership Matrix）

| 泳道角色 | 允许新建 / 修改文件 | 严禁触碰区域 |
| :--- | :--- | :--- |
| **工程师 2（Win32 Native UI 泳道）** | - 修改 [`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp)<br>- 新建 `src/gui/workstation_canvas.h`（若新建 `.cpp` 则同步在 `CMakeLists.txt` 的 `CineRTX` 目标追加该源文件）<br>- 修改 [`src/gui/preview_viewport.h`](../../src/gui/preview_viewport.h) / [`src/gui/preview_viewport.cpp`](../../src/gui/preview_viewport.cpp)（支持动态 `resizeSwapChain(int w, int h)`）<br>- 在 `tests/` 新增针对动态超分倍率计算的单元测试 | **严禁修改** `include/rtx/*`、`src/core/*`、`src/cli/*`、`src/shim/*` 及已冻结的 `webui/*` |

---

## 第三部分：Tasks（Win32 Native UI 原子级 TDD 施工任务集）

### Task 1：根治 `src/gui/rtx_gui.cpp` 现有 6 大契约缺陷，实现 19 项 SSOT 参数 1:1 全绑定

#### 1.1 现存 6 大缺陷精确定位（必改清单）

| 缺陷编号 | [`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp) 行号 | 现状病理描述 | 修复规范 |
| :--- | :--- | :--- | :--- |
| **Bug 1** | `:761-762`、`:304-401`、`:480` | `WM_CREATE` 创建了 `IDC_LBL_PEAK_NITS` 与 `IDC_EDT_PEAK_NITS`，但 `layoutControls()` 完全漏掉了它们的 `MoveWindow`，导致控件在 `(0,0)` 尺寸为 `0x0` 隐形！且 `:480` 直接写死 `opt.peak_nits = 1000;`，从未读取输入框！ | 补齐 `IDC_LBL_PEAK_NITS` 与 `IDC_EDT_PEAK_NITS` 的 `MoveWindow` 布局，并通过 `GetDlgItemTextW` 读取真实用户输入送入 `TranscodeProfile::peak_nits`。 |
| **Bug 2** | `:170`、`:188`、`:286-295`、`:415`、`:582` | 残留已被废弃的 `H.264 High (8-bit SDR)` 选项及 `_remaster_h264.mp4` 后缀分支，导致 Stage 2 门禁 `2.1` 全三端校验不通过。 | 彻底删除所有 `H.264` 分支；`IDC_CMB_CODEC` 仅保留索引 `0`（`AV1`）与索引 `1`（`HEVC`）。 |
| **Bug 3** | `:56-95`、`:459-478` | 缺少 WebUI 已具备的 3 项 SSOT 控件：`rifeEngine`（RIFE 引擎路径输入框+浏览按钮）、`gpu`（GPU 序号 `0..15`）、`frames`（帧数上限 `0=全流`）。 | 新增 `IDC_LBL_RIFE_ENGINE`、`IDC_EDT_RIFE_ENGINE`、`IDC_BTN_BROWSE_RIFE`、`IDC_LBL_GPU`、`IDC_EDT_GPU`、`IDC_LBL_FRAMES`、`IDC_EDT_FRAMES`，实现 19 项 SSOT 参数 100% 满配对齐。 |
| **Bug 4** | `:159-176`、`:405-507` | `autoFillOutputPath` 与 `collectOptions` 手写文件名拼接与 `std::clamp`，绕过了 `param_schema.h`。 | 改为构造 `rtx::TranscodeProfile`，调用 `rtx::deriveOutputPath()`、`rtx::validateAndNormalizeProfile()` 与 `rtx::profileToHwOptions()`。 |
| **Bug 5** | `:178-257` | `populateComboBoxes()` 手写了 20 多处 `isZh ? L"..." : L"..."` 字符串字面量。 | 全部替换为 `rtx::trW(rtx::StrId::OptCodecAv1)` ～ `rtx::trW(rtx::StrId::OptFps120Rife)`。 |
| **Bug 6** | `:134-141`、`:550`、`:594` | 私自维护 `guiLog()` 仅调 `OutputDebugStringW`，缺乏统一 `[CineRTX][P:pid/T:tid][LEVEL]` 格式。 | 删除 `guiLog()`，全部改调 `rtx::logInfo()` / `rtx::logWarn()` / `rtx::logError()`。 |

#### 1.2 标准 SSOT `collectProfileFromUi` 与 `applyProfileToUi` 实现范式
在 `src/gui/rtx_gui.cpp` 中引入 `#include "rtx/param_schema.h"`，维护全局 `rtx::TranscodeProfile g_profile{}`：
```cpp
rtx::TranscodeProfile collectProfileFromUi(HWND hwnd) {
    rtx::TranscodeProfile p{};
    wchar_t buf[MAX_PATH] = {};

    GetDlgItemTextW(hwnd, IDC_EDT_INPUT, buf, MAX_PATH);
    p.input_path = buf;
    GetDlgItemTextW(hwnd, IDC_EDT_OUTPUT, buf, MAX_PATH);
    p.output_path = buf;

    p.codec = (ComboBox_GetCurSel(GetDlgItem(hwnd, IDC_CMB_CODEC)) == 1) ? "hevc" : "av1";
    p.preset = ComboBox_GetCurSel(GetDlgItem(hwnd, IDC_CMB_PRESET)) + 1;

    const int rcIdx = ComboBox_GetCurSel(GetDlgItem(hwnd, IDC_CMB_RC));
    p.rate_control = (rcIdx == 1) ? "vbr" : (rcIdx == 2) ? "qp" : "cbr";

    GetDlgItemTextW(hwnd, IDC_EDT_BITRATE, buf, 32);
    p.bitrate_kbps = _wtoi(buf);
    GetDlgItemTextW(hwnd, IDC_EDT_QP, buf, 32);
    p.qp = _wtoi(buf);

    const int resIdx = ComboBox_GetCurSel(GetDlgItem(hwnd, IDC_CMB_RES));
    p.resolution = (resIdx == 1) ? "2k" : (resIdx == 2) ? "fhd" : (resIdx == 3) ? "native" : "4k";
    p.geometry = (ComboBox_GetCurSel(GetDlgItem(hwnd, IDC_CMB_GEOM)) == 1) ? "pillarbox" : "auto";

    p.vsr_enabled = (Button_GetCheck(GetDlgItem(hwnd, IDC_CHK_VSR)) == BST_CHECKED);
    p.vsr_quality = ComboBox_GetCurSel(GetDlgItem(hwnd, IDC_CMB_VSR_QUAL)) + 1;

    GetDlgItemTextW(hwnd, IDC_EDT_PEAK_NITS, buf, 32);
    p.peak_nits = (buf[0] != L'\0') ? _wtoi(buf) : 1000;

    const int fpsIdx = ComboBox_GetCurSel(GetDlgItem(hwnd, IDC_CMB_FPS));
    static constexpr const char* kFpsModes[] = {
        "original", "60-nvofa", "120-nvofa", "60-rife", "120-rife"
    };
    p.fps_mode = (fpsIdx >= 0 && fpsIdx < 5) ? kFpsModes[fpsIdx] : "60-nvofa";

    GetDlgItemTextW(hwnd, IDC_EDT_RIFE_ENGINE, buf, MAX_PATH);
    p.rife_engine = wideToUtf8(buf);

    p.copy_audio = (Button_GetCheck(GetDlgItem(hwnd, IDC_CHK_AUDIO)) == BST_CHECKED);
    p.hdr10plus = (Button_GetCheck(GetDlgItem(hwnd, IDC_CHK_HDR10PLUS)) == BST_CHECKED);

    GetDlgItemTextW(hwnd, IDC_EDT_LUT_PATH, buf, MAX_PATH);
    p.lut_path = buf;

    GetDlgItemTextW(hwnd, IDC_EDT_GPU, buf, 32);
    p.gpu_id = _wtoi(buf);
    GetDlgItemTextW(hwnd, IDC_EDT_FRAMES, buf, 32);
    p.max_frames = _wtoi(buf);

    (void)rtx::validateAndNormalizeProfile(p, false);
    return p;
}
```
同时实现逆向同步函数 `applyProfileToUi(HWND hwnd, const rtx::TranscodeProfile& p)`，使得点击 **RTX 黄金预设按钮**、点击 **批处理队列项** 或在 **播放器 / NLE 工作站** 修改参数时，能够一键写回所有 Win32 控件！

---

### Task 2：打造 NVIDIA Signature Green（`#76B900`）工业级暗黑 GDI 主题与顶部三态导航栏

#### 2.1 对齐 WebUI 的 Win32 GDI 色彩令牌（Color Tokens）
废除原先单调的 `RGB(24, 24, 24)` / `RGB(36, 36, 36)`，在 Win32 GDI 层严格复刻 [`webui/app.css`](../../webui/app.css)（第 10–40 行）的 NVIDIA Studio 色板：

| 令牌名称 | RGB 宏定义 | 对应 WebUI Hex | 用途 |
| :--- | :--- | :--- | :--- |
| `kClrBg` | `RGB(11, 13, 11)` | `#0b0d0b` | 主窗口底层碳黑背景（`g_hbrBg`） |
| `kClrPanel` | `RGB(19, 22, 19)` | `#131613` | 面板容器底色（`g_hbrPanel`） |
| `kClrPanel2` | `RGB(26, 30, 26)` | `#1a1e1a` | 预设卡 / 磁贴 / 轨道头背景（`g_hbrPanel2`） |
| `kClrCtrl` | `RGB(34, 39, 34)` | `#222722` | 输入框 / 下拉框 / 列表框填充色（`g_hbrCtrl`） |
| `kClrLine` | `RGB(44, 51, 44)` | `#2c332c` | 常规面板与轨道分隔线 |
| `kClrLineStrong` | `RGB(61, 71, 61)` | `#3d473d` | 控件外框与顶栏底边线 |
| `kClrText` | `RGB(238, 242, 238)` | `#eef2ee` | 主正文与数值高亮 |
| `kClrTextDim` | `RGB(146, 156, 146)` | `#929c92` | 标签与次级说明文字 |
| `kClrNvGreen` | **`RGB(118, 185, 0)`** | **`#76b900`** | **NVIDIA 标志绿主强调色**（Logo 徽标、激活 Tab 下划线、主按钮、播放头、A/B 分割线） |
| `kClrNvGreenHover` | `RGB(138, 208, 0)` | `#8ad000` | 主按钮悬停高亮绿 |
| `kClrNvGreenDim` | `RGB(78, 122, 0)` | `#4e7a00` | 徽章描边与次级绿框 |
| `kClrOnAccent` | `RGB(7, 13, 0)` | `#070d00` | 绿底按钮与 `RTX` 徽标上的深黑粗体文字 |
| `kClrHdrGold` | `RGB(255, 209, 102)` | `#ffd166` | HDR10 / Flicks 时码金色徽章 |

#### 2.2 顶部三态工作站导航栏（Topbar & Workspace Switcher）
在窗口顶部固定预留 `48px`（DPI 缩放后 `MulDiv(48, dpi, 96)`）的 Topbar 区域（当处于第 4 形态无边框全屏播放时自动隐藏该 Topbar）：
1. **左侧品牌区（GDI 双缓冲自绘）**：
   - 使用 GDI `Polygon()` 绘制平行四边形斜切绿色徽标（对应 `webui/app.css:74-86` 的 `clip-path: polygon(0 0, 100% 0, 82% 100%, 0 100%)`），填充 `kClrNvGreen`，居中绘制深黑粗体 `"RTX"`；
   - 右侧绘制主标题 `rtx::trW(rtx::StrId::AppTitle)` 与副标题 `"NVIDIA RTX Studio Native Workstation · C++26 SSOT"`。
2. **中部三态工作区切换按钮组（Owner-Draw Tabs + `Alt+1/2/3` & `F11` 快捷键）**：
   - `IDC_TAB_WS_ENCODER`：`[1] 视频转码器 · Media Encoder [CORE]`（无参启动默认激活）
   - `IDC_TAB_WS_PLAYER`：`[2] 超分播放器 · RTX Player (F11 全屏快播)`
   - `IDC_TAB_WS_NLE`：`[3] 非线性编辑 · RTX NLE Studio`
   - 采用 `BS_OWNERDRAW`（在 `WM_DRAWITEM` 中绘制）：激活态文字为 `kClrNvGreen`、底部绘制 `3px` 实心 `kClrNvGreen` 下划线、背景为暗绿填充 `RGB(24, 38, 8)`；支持 `Alt+1`、`Alt+2`、`Alt+3` 秒级切换工作站形态，支持 `F11` / `Alt+Enter` 切换第 4 形态无边框全屏快播！
3. **右侧全局工具按钮**：
   - `IDC_BTN_PROBE`（`运行探针` / `Run Probe`）；
   - `IDC_BTN_LANG`（`EN / 中文`，点击等同于 `Ctrl+L`，立即切换双语并刷新全部 113 词条控件）。

---

### Task 3：原生实现“3 + 1”四大工作站形态（ME 转码器 / VLC 播放器 / 全屏快播形态 / PR 非线编）

定义工作区形态与全屏状态：
```cpp
enum class WorkspaceMode : int {
    Encoder = 0,  // [1] 视频转码器 (Media Encoder)
    Player  = 1,  // [2] 超分播放器 (RTX Player 窗口态 / 全屏快播态)
    Nle     = 2,  // [3] 非线性编辑 (RTX NLE Studio)
};
```
并维护 `bool g_isBorderlessFullscreen = false;`、`bool g_inResizeDrag = false;`、`WINDOWPLACEMENT g_prevPlacement{sizeof(WINDOWPLACEMENT)};` 与 `DWORD g_prevStyle = 0;`。

#### 3.1 形态一：视频转码器工作站（`WorkspaceMode::Encoder` — 对标 Adobe Media Encoder，核心默认形态）
采用与 `webui/index.html:55-327` 对应的 **左-中-右三栏专业工作站布局**：
1. **左栏（宽度 `220px`）：RTX 黄金预设浏览器（Preset Browser）**
   - 垂直排列 5 个预设按钮（`BS_OWNERDRAW` 左侧带 `3px` 绿色激活条）：
     - `IDC_BTN_PRESET_MASTER`：`★ 4K 60fps AV1 HDR10 母版`（`av1, P5, 4k, vsr=true, L4, 60-nvofa, 1000 nits`）
     - `IDC_BTN_PRESET_RIFE`：`◆ 4K 60fps RIFE 深度学习臻彩`（`av1, P6, 4k, vsr=true, L4, 60-rife, 1000 nits`）
     - `IDC_BTN_PRESET_ESPORTS`：`⚡ 4K 120fps NVOFA 电竞高帧率`（`av1, P4, 4k, vsr=true, L4, 120-nvofa, 1000 nits`）
     - `IDC_BTN_PRESET_COMPAT`：`🎬 2K 60fps HEVC Main10 广泛兼容`（`hevc, P5, 2k, vsr=true, L3, 60-nvofa, 1000 nits`）
     - `IDC_BTN_PRESET_NATIVE`：`🎞 原生分辨率 TrueHDR 无损重制`（`av1, P6, native, vsr=false, L4, original, 1000 nits`）
   - 点击任一预设按钮，立即更新 `g_profile` 并调用 `applyProfileToUi(hwnd, g_profile)` 同步右侧 19 项检查器。
2. **中栏（自适应弹性宽度）：多任务批处理队列 + D3D12 A/B 实时监看与遥测舱**
   - **上半区（批处理队列）**：
     - 工具栏：`IDC_BTN_QUEUE_ADD`（`+ 添加当前配置到队列`）、`IDC_BTN_QUEUE_REMOVE`（`移除选中`）、`IDC_BTN_QUEUE_CLEAR`（`清空队列`）、右侧主动作按钮 `IDC_BTN_START`（`BS_OWNERDRAW` 实心 NVIDIA 绿底粗体字 `开始硬件转码`）与 `IDC_BTN_STOP`（红框 `停止转码`）；
     - 队列表 `IDC_LST_QUEUE`、进度条 `IDC_PROGRESS` 与状态行 `IDC_LBL_STATUS`。
   - **下半区（D3D12 A/B 监看视口 + 4 项实时遥测）**：
     - 挂载 `g_previewViewport`（16:9 比例，支持鼠标拖拽 A/B 分割线）与遥测标签 `IDC_LBL_TELEMETRY`。
3. **右栏（宽度 `390px`）：19 项 SSOT 参数检查器（Parameter Inspector）**
   - 完整排列全部 19 项控件（含补齐后的 `IDC_EDT_PEAK_NITS`、`IDC_EDT_RIFE_ENGINE` + `IDC_BTN_BROWSE_RIFE`、`IDC_EDT_GPU`、`IDC_EDT_FRAMES`）。

#### 3.2 形态二（窗口态）与形态四（无边框全屏快速播放器形态 — 本次重点新增核心特性）

##### （A）Windows「打开方式」双击视频文件直入全屏快播（`wWinMain` 路由改造）
在 [`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp) 第 1041–1058 行中，原先只要 `argc > 1` 就一律转发给 `rtxCliMain()`，导致用户在 Windows 资源管理器中将 `.mp4` 的默认打开方式设为 `CineRTX.exe` 并双击视频（此时系统传入 `CineRTX.exe "D:\movie.mp4"`，`argc == 2` 且 `argv[1]` 不以 `-` 开头）时，被当成未知 CLI 参数报错退出！
**必须按以下规则重构 `wWinMain` 命令行分流**：
```cpp
std::wstring startupQuickPlayFile;
int argc = 0;
LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
if (argv != nullptr && argc > 1) {
    // 若仅传入 1 个参数且不以 '-' 开头（Windows Shell 双击关联打开或拖拽文件到 exe 图标）：
    // 直接进入 GUI 的【形态四：无边框全屏快速超分播放器形态】，不走 CLI！
    if (argc == 2 && argv[1][0] != L'-' && std::filesystem::exists(argv[1])) {
        startupQuickPlayFile = argv[1];
    } else {
        std::vector<std::string> cliArgs;
        cliArgs.reserve(static_cast<size_t>(argc));
        for (int i = 0; i < argc; ++i) {
            cliArgs.push_back(wideToUtf8(argv[i]));
        }
        LocalFree(argv);
        std::vector<char*> cliPtrs;
        cliPtrs.reserve(cliArgs.size() + 1);
        for (auto& s : cliArgs) cliPtrs.push_back(s.data());
        cliPtrs.push_back(nullptr);
        return rtxCliMain(static_cast<int>(cliArgs.size()), cliPtrs.data());
    }
}
if (argv != nullptr) {
    LocalFree(argv);
}
```
当 `startupQuickPlayFile` 非空时：
1. 主窗口创建后立即切换至 `WorkspaceMode::Player`；
2. 立即调用 `enterBorderlessFullscreen(hwnd, true)` 将窗口设为无标题栏、无边框的全屏状态（`WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN`），铺满当前显示器物理分辨率（如 `3840 × 2160`）；
3. 自动加载 `startupQuickPlayFile`，探测源视频宽高 `(src_width, src_height)`，按下方 **（B）动态超分倍率公式** 计算全屏超分倍率并立即启动硬件超分播放！

##### （B）视口分辨率驱动的动态超分倍率数学模型（Viewport-Driven Dynamic VSR Scaling）
无论是全屏状态还是普通窗口状态，在播放器形态下，VSR 超分的目标分辨率与超分倍率**由当前播放视口（`viewport_w × viewport_h`）与源视频分辨率（`src_w × src_h`）动态决定**：

```cpp
struct DynamicVsrTarget {
    int target_width  = 0;
    int target_height = 0;
    double scale      = 1.0;
    bool vsr_active   = false;
};

// 纯函数：根据源视频宽高与当前窗口播放视口宽高，计算等比内切（Letterbox/Pillarbox）超分目标与倍率
inline DynamicVsrTarget computeViewportVsrTarget(int src_w, int src_h, int viewport_w, int viewport_h) noexcept {
    if (src_w <= 0 || src_h <= 0 || viewport_w <= 0 || viewport_h <= 0) {
        return {src_w, src_h, 1.0, false};
    }
    const double scale_x = static_cast<double>(viewport_w) / static_cast<double>(src_w);
    const double scale_y = static_cast<double>(viewport_h) / static_cast<double>(src_h);
    const double scale   = std::min(scale_x, scale_y);

    // 若视口小于或等于源分辨率（scale <= 1.0），无需启动 VSR 放大，保持 1.0x 原生或由交换链下采样
    if (scale <= 1.0) {
        return {src_w, src_h, scale, false};
    }
    // 对齐至偶数像素（NV12/P010 4:2:0 要求 2 像素对齐）
    const int out_w = (static_cast<int>(std::lround(src_w * scale)) + 1) & ~1;
    const int out_h = (static_cast<int>(std::lround(src_h * scale)) + 1) & ~1;
    return {out_w, out_h, scale, true};
}
```
**典型场景精确验算（必须写入单元测试验证）**：
1. **`3840 × 2160` 全屏打开 `1920 × 1080` 横屏视频**：
   $$\text{scale} = \min\!\left(\frac{3840}{1920},\; \frac{2160}{1080}\right) = \min(2.0,\; 2.0) = \mathbf{2.0\times} \implies \text{输出 } 3840 \times 2160$$
2. **`3840 × 2160` 全屏打开 `720 × 1280` 竖屏视频**：
   $$\text{scale} = \min\!\left(\frac{3840}{720},\; \frac{2160}{1280}\right) = \min(5.333333,\; 1.6875) = \mathbf{1.6875\times} \implies \text{输出 } 1216 \times 2160 \text{（720}\times\text{1.6875 = 1215，偶数对齐 1216）}$$
3. **按 `Esc` / `F11` 退出全屏回到普通窗口（如视口变为 `1440 × 810`）**：
   - 打开 `1920 × 1080` 视频时：$\text{scale} = \min(1440/1920,\; 810/1080) = 0.75\times \le 1.0\times$，自动旁路 VSR 放大；
   - 打开 `720 × 1280` 竖屏视频时：$\text{scale} = \min(1440/720,\; 810/1280) = \mathbf{0.6328125\times}$（若窗口拉大到视口高 `1600`，则倍率为 $1600/1280 = \mathbf{1.25\times}$）。

##### （C）无边框全屏切换与窗口拖拽 `WM_EXITSIZEMOVE`（MouseReleaseEvent）延迟倍率切换机制
1. **无边框全屏状态机 `setBorderlessFullscreen(HWND hwnd, bool fullscreen)`**：
   - **进入全屏（`fullscreen == true`）**：
     - 记录当前窗口样式 `g_prevStyle = GetWindowLongW(hwnd, GWL_STYLE)` 与位置 `GetWindowPlacement(hwnd, &g_prevPlacement)`；
     - 剥离标题栏与边框：`SetWindowLongW(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN)`；
     - 获取所在显示器完整屏幕矩形 `MONITORINFO mi{sizeof(mi)}; GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)`；
     - 调用 `SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW)`；
     - 隐藏顶栏与底部播控栏，`g_previewViewport` 占满 `100%` 屏幕（`0, 0, monitorW, monitorH`），立即调用 `commitViewportVsrScale(hwnd)` 切换至全屏超分倍率（如 `2.0x` 或 `1.6875x`）。
   - **退出全屏（`fullscreen == false`，响应 `VK_ESCAPE`、`VK_F11`、`Alt+Enter` 或双击视口）**：
     - 恢复样式 `SetWindowLongW(hwnd, GWL_STYLE, g_prevStyle | WS_VISIBLE)` 与 `SetWindowPlacement(hwnd, &g_prevPlacement)`；
     - 恢复顶栏与底部 VLC 播控栏显示；
     - **立即调用 `commitViewportVsrScale(hwnd)`**，按恢复后的窗口视口尺寸动态降低超分倍率！
2. **鼠标拖拽缩放窗口时的 `WM_ENTERSIZEMOVE` / `WM_EXITSIZEMOVE` 两阶段机制**：
   - 当用户按住窗口边缘拖拽缩放时，Windows 发送 `WM_ENTERSIZEMOVE`：此时置 `g_inResizeDrag = true`。在拖拽期间连续触发的数十次 `WM_SIZE` 中，仅调用 `layoutControls(hwnd)` 移动子窗口位置，**绝不触发 NGX VSR 重初始化或重算超分倍率**（避免拖拽卡顿与显存抖动）。
   - 当用户**松开鼠标左键结束窗口大小拖拽时（即 Win32 的 `WM_EXITSIZEMOVE`，对应 `MouseReleaseEvent`）**：
     - 置 `g_inResizeDrag = false`；
     - **在 `WM_EXITSIZEMOVE` 分支中立即调用 `commitViewportVsrScale(hwnd)`**：读取当前 `g_previewViewport` 的最新宽高 `(vp_w, vp_h)`，调用 `computeViewportVsrTarget(src_w, src_h, vp_w, vp_h)` 算出新的 `scale` 与 `(target_width, target_height)`，更新 `g_previewViewport->resizeSwapChain(vp_w, vp_h)`、OSD 浮层文案（如 `VSR Scale: 1.6875x (720x1280 -> 1216x2160)`）并记录 `rtx::logInfo`！
   - 若用户点击窗口最大化/还原按钮（`WM_SIZE` 且 `wParam == SIZE_MAXIMIZED` 或 `wParam == SIZE_RESTORED` 且 `!g_inResizeDrag`），同样立即调用 `commitViewportVsrScale(hwnd)`。

#### 3.3 形态三：非线性编辑工作站（`WorkspaceMode::Nle` — 对标 Adobe Premiere Pro）
当切换至 `WorkspaceMode::Nle` 时，呈现经典 **PR 四象限布局**：
1. **左上象限（Project Media & 3D LUT Bin）**：
   - 包含 `IDC_LST_NLE_BIN` 列表框（预置 3 款内置 `.cube` LUT 标识 + 用户导入的媒体素材）、`IDC_BTN_NLE_IMPORT`（`+ 导入素材`）与 `IDC_BTN_NLE_TO_TRACK`（`+ 插入 V1 主轨`）。
2. **中上象限（Program Monitor + `705,600,000 Hz Flicks` 时码显示）**：
   - 顶部显示序列名称与精确时码栏 `IDC_LBL_NLE_TIMECODE`：`SMPTE 00:00:00:00  |  0 Flicks (705,600,000 Hz · DRIFT 0)`（使用 `int64_t flicks = static_cast<int64_t>(playhead_ms) * 705600LL` 精确计算）；
   - 中部挂载 `g_previewViewport` 节目监视器；
   - 底部剪辑工具条：`IDC_BTN_NLE_IN`（`[ 标记入点 (I)`）、`IDC_BTN_NLE_OUT`（`标记出点 (O) ]`）、`IDC_BTN_NLE_RAZOR`（`✂ 在播放头分割 (C)`）。
3. **右上象限（Effect Controls / Lumetri & RTX HDR 调色台）**：
   - 直接展示并绑定入点 `IDC_EDT_NLE_IN_MS`、出点 `IDC_EDT_NLE_OUT_MS`、帧数上限 `IDC_EDT_FRAMES`、HDR 峰值亮度 `IDC_EDT_PEAK_NITS`、HDR10+ `IDC_CHK_HDR10PLUS`、3D LUT `IDC_EDT_LUT_PATH`、VSR 档位与 FRUC 补帧模式；
   - 底部配备绿色主按钮 `IDC_BTN_NLE_EXPORT`（`🚀 导出序列至转码器渲染`）。
4. **底部通栏象限（GDI 双缓冲自绘四轨 Flicks 时间轴 — Multi-Track Timeline）**：
   - 在底部区域通过 GDI 内存 DC（`CreateCompatibleDC` + `CreateCompatibleBitmap` 零闪烁双缓冲）绘制 4 条专业轨道（与 `webui/state.json:54-123` 完全对齐：`V2 调色调整层`、`V1 视频主轨`、`A1 音轨`、`S1 字幕轨`），支持鼠标点击/拖拽时间轴移动播放头、`I/O` 标记入出点与 `C` 剃刀分割。

---

## 第四部分：Checklists（Win32 Native UI 量化验收门禁清单）

工程师 2 完成施工后，必须逐项执行以下门禁并在汇报中附上实测输出：

- [ ] **门禁 W32.1（全三端 SSOT 19 项参数与默认值 100% 闭环 — 闭合原门禁 2.1）**：
  - 验证方法：运行 `python webui/test/verify_stage2.py` 及静态扫描 `src/gui/rtx_gui.cpp`
  - 通过标准：
    1. `src/gui/rtx_gui.cpp` 中 `H.264` / `h264` / `opt.peak_nits = 1000` 硬编码命中数为 **0**；
    2. `IDC_EDT_PEAK_NITS`、`IDC_EDT_RIFE_ENGINE`、`IDC_BTN_BROWSE_RIFE`、`IDC_EDT_GPU`、`IDC_EDT_FRAMES` 全部具备有效非零 `MoveWindow` 几何坐标并接入 `rtx::TranscodeProfile`；
    3. `verify_stage2.py` 中 `gates["2.1"].PASS`（全三端 `Core + CLI + WebUI + Win32 GUI`）正式从 `false` 变为 **`true`**！
- [ ] **门禁 W32.2（NVIDIA 绿 `#76B900` 工业主题与零硬编码双语词条）**：
  - 验证方法：扫描 `src/gui/` 色板定义与 `populateComboBoxes()`
  - 通过标准：
    1. 实装 `RGB(118, 185, 0)`（`#76B900`）NVIDIA 标志绿主色与 `RGB(11, 13, 11)` 碳黑背景、斜切 `RTX` 绿色徽标及绿底主动作按钮；
    2. `populateComboBoxes()` 中手写三元运算符 `isZh ?` 计数为 **0**，全部下拉项通过 `rtx::trW(rtx::StrId::Opt...)` 从 `.rdata` 读取，按 `Ctrl+L` 或点击顶栏 `EN/中文` 按钮全界面即时无闪烁切换中英文。
- [ ] **门禁 W32.3（Win32 三大工作站形态 `Encoder / Player / NLE` 切换与 D3D12 视口联动）**：
  - 验证方法：点击顶栏 `[1] Media Encoder`、`[2] RTX Player`、`[3] RTX NLE Studio` 或按 `Alt+1` / `Alt+2` / `Alt+3`
  - 通过标准：
    1. **形态一（Encoder）**：5 个黄金预设按钮一键写入 19 项检查器，多任务队列增删与拖放正常；
    2. **形态二（Player）**：D3D12 `PreviewViewport` 自动最大化，支持鼠标拖拽 A/B 分割线、`-1f/+1f` 逐帧步进、时间轴拖拽单帧超分预览与「送入转码器」一键跳转；
    3. **形态三（NLE）**：四象限布局完整，显示 `705,600,000 Hz Flicks` 整数时码，底部 GDI 双缓冲绘制 `V2/V1/A1/S1` 四条轨道，支持点击时间轴定位播放头、`I`/`O` 标记入出点、`C` 剃刀分割片段及「导出序列至转码器渲染」。
- [ ] **门禁 W32.4（第 4 形态：双击 MP4 无边框全屏快播 + 视口动态超分倍率 + `WM_EXITSIZEMOVE` 鼠标释放重配验证）**：
  - 验证方法：
    1. 单元测试断言 `computeViewportVsrTarget(1920, 1080, 3840, 2160)` 返回 `scale == 2.0` (`3840x2160`)，且 `computeViewportVsrTarget(720, 1280, 3840, 2160)` 返回 `scale == 1.6875` (`1216x2160`)；
    2. 执行 `CineRTX.exe sample.mp4`（模拟 Windows 打开方式双击启动）：程序不进入 CLI 报错，而是直接以 `WS_POPUP` 无标题栏全屏启动 `[2] 播放器` 并自动播放；
    3. 按 `Esc` / `F11` 退出全屏至窗口态，或拖拽窗口边框并在松开鼠标左键（`WM_EXITSIZEMOVE`）时，DebugView 与 OSD 实时打印更新后的超分倍率（拖拽过程中 `WM_SIZE` 不重复重建 VSR）。
- [ ] **门禁 W32.5（统一 DebugView 日志接入 — 闭合原门禁 2.4）**：
  - 验证方法：扫描 `src/gui/rtx_gui.cpp` 并运行单元测试 / 启动 GUI
  - 通过标准：私自定义的 `guiLog()` 已删除，GUI 启动、预设切换、工作站切换、全屏倍率切换、转码启停全部调用 `rtx::logInfo()` / `rtx::logError()`，向内核调试流输出标准 `[CineRTX][P:.../T:...][INFO]` 日志。
- [ ] **门禁 W32.6（单文件行数红线 $\le 1,300$ 行、编译零错误与 PE 导入表纯净 — 闭合原门禁 2.5）**：
  - 验证方法：
    1. 检查 `src/gui/` 下所有 `.cpp` / `.h` 文件行数均严格 $\le 1,300$ 行；
    2. 在 MSVC / Ninja 下完整编译 `CineRTX.exe` 与 `rtx_tests.exe` 零错误通过，`ctest` 100% 全绿；
    3. 运行 `pwsh tools/audit_imports.ps1`，确认 `CineRTX.exe` 静态导入表中 `NVIDIA static imports: 0`、`MSVCP140.dll: 0`、`VCRUNTIME140*.dll: 0`、`WS2_32.dll: 0`。
