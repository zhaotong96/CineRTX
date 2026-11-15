# CineRTX 工程施工规范（第二阶段）：Win32 GUI + CLI + WebUI 三端统一 SSOT 架构与原生接入

**文档标识**：`DOC-SUPERPOWER-STAGE2-UNIFIED-THREE-FRONTENDS-002`  
**演进阶段**：Stage 2 / 共 3 阶段（**后端接口已由领域专家完成并冻结**；当前交付给**工程师 1（WebUI）**与**工程师 2（Win32 GUI）**执行双端 UI 对齐）  
**遵循方法论**：`SuperPower` 工程施工范式（`Plan` + `Tasks` + `Checklists` 三位一体，严格遵循软件设计六大准则 SOLID + 迪米特法则 + KISS + 奥卡姆剃刀）  
**关联已落地后端契约源码**：
- [`include/rtx/param_schema.h`](../../include/rtx/param_schema.h) / [`src/core/param_schema.cpp`](../../src/core/param_schema.cpp)（三端唯一参数 Schema、默认值与 JSON 转换器）
- [`include/rtx/logger.h`](../../include/rtx/logger.h) / [`src/core/logger.cpp`](../../src/core/logger.cpp)（零依赖 C++26 `OutputDebugStringW` DebugView + 控制台 + 回调统一日志系统）
- [`include/rtx/i18n.h`](../../include/rtx/i18n.h) / [`src/core/i18n.cpp`](../../src/core/i18n.cpp)（113 项 `.rdata` 双语词条与 `exportI18nJson()`）
- [`include/rtx/hw_transcode.h`](../../include/rtx/hw_transcode.h) / [`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp)（已清除 `hdr`/`bit_depth`/`width`/`height`/`fps` 死字段，接入 DebugView 日志与协作取消 MP4 `moov` 写尾）
- [`src/cli/main.cpp`](../../src/cli/main.cpp)（已实装 `--schema-json` 与 `--session-stdio`，并拔除 `--min-w`/`--max-w`/`--fp32` 等 6 个伪开关）
- [`CMakeLists.txt`](../../CMakeLists.txt)（已拔除 `RTX_BUILD_TESTS`、`RTX_BUILD_GUI`、`RTX_WARNINGS_AS_ERRORS` 冗余开关，并将 `logger.cpp`、`param_schema.cpp` 编入 `rtx_core`）

---

## 一、 Plan（顶层规划与已冻结后端契约）

### 1.1 阶段目标与当前进度
1. **Track A（后端核心 SSOT 契约、DebugView 日志与 CLI 会话引擎）—— ✅ 已由领域专家完成并冻结**：
   - `rtx_core` 已内置唯一编译期参数元数据表 `rtx::kParamSchema`（19 项）与唯一参数聚合体 `rtx::TranscodeProfile`，统一三端默认值为：`codec="av1"`, `preset=5`, `rateControl="cbr"`, `qp=24`, `resolution="4k"`, `geometry="auto"`, `vsrEnabled=true`, `vsrQuality=4`, `peakNits=1000`, `fpsMode="60-nvofa"`, `copyAudio=true`；
   - 已彻底废除违反 10-bit HDR10 产品定位的 `H.264 (8-bit SDR)` 输出编码后门，输出编码器仅保留 **`AV1 10-bit`** 与 **`HEVC Main10`**；
   - `CineRTX.exe --schema-json` 原生输出完整 JSON（含 `defaults`、`params` 范围枚举定义与 `i18n` 113 项中英双语字典）；
   - `CineRTX.exe --session-stdio` 原生支持从 `stdin` 读取 `TranscodeProfile` JSON、后台监听 `stdin` `"cancel\n"` 信号优雅写完 MP4 `moov` 头后退出，并通过 `stdout` 实时流式推送 `{"type":"progress",...}`、`{"type":"log",...}`、`{"type":"report",...}` 单行 JSONL 事件流；
   - 全仓已实装零外部依赖 C++26 统一日志系统（`rtx::logInfo` / `rtx::logWarn` / `rtx::logError` / `rtx::logDebug`），自动向 Windows 内核调试器（Sysinternals DebugView / Remote DebugView `dbgv.exe`）输出带 `[CineRTX][P:pid/T:tid][LEVEL][file:line]` 前缀的结构化日志。
2. **接下来由两名执行工程师按序推进双端 UI 施工**：
   - **第一步：工程师 1（WebUI 泳道 — Track C）** 对接 `--schema-json` 与 `--session-stdio`，先出 WebUI 样式与交互闭环；
   - **第二步：工程师 2（Win32 原生 GUI 泳道 — Track B）** 对齐 WebUI 视觉与 `param_schema.h` 后端接口，完成纯 Win32 版本。

### 1.2 已冻结的后端 C++26 / JSON 接口速查表（供工程师 1 & 2 直接调用）

| 接口层 | 调用方式 / 函数签名 | 功能契约说明 |
| :--- | :--- | :--- |
| **C++ SSOT 参数表与校验 (`include/rtx/param_schema.h`)** | `rtx::TranscodeProfile profile{};`<br>`rtx::validateAndNormalizeProfile(profile, true);`<br>`rtx::HwTranscodeOptions hw = rtx::profileToHwOptions(profile);`<br>`rtx::deriveOutputPath(in, codec, out, auto_out);`<br>`rtx::resolveDefaultRifeEnginePath(explicit_path);` | Win32 GUI 与 CLI 直接调用，严禁在 UI 层私自手写范围 `clamp`、文件名后缀拼接或 RIFE 引擎路径查找。 |
| **C++ 双语词条表 (`include/rtx/i18n.h`)** | `rtx::trW(rtx::StrId::OptCodecAv1)` ... `rtx::trW(rtx::StrId::OptFps120Rife)` | 全部 113 项中英文词条（含所有下拉框选项 `Opt*` 与 WebUI 标签 `Wui*`）均已编译进 `.rdata`。 |
| **C++ DebugView 统一日志 (`include/rtx/logger.h`)** | `rtx::logInfo(msg)` / `rtx::logWarn(msg)` / `rtx::logError(msg)` / `rtx::logDebug(msg)` | 自动写入 `OutputDebugStringW`（供 DebugView 过滤 `[CineRTX]`）并同步触发日志回调。 |
| **WebUI Schema & 词典导出 (`CLI`)** | `CineRTX.exe --schema-json` | 输出 `{"version":1,"defaults":{...},"params":[...],"i18n":{"en":{...},"zh":{...}}}`。 |
| **WebUI 交互式转码会话 (`CLI`)** | `CineRTX.exe --session-stdio` | `stdin` 第 1 行传入 `state.json` 或 `profile` JSON；运行期 `stdin` 传入 `"cancel\n"` 触发优雅收尾；`stdout` 输出 `progress` / `log` / `report` 单行 JSONL。 |

### 1.3 多工程师防撞文件矩阵（File Ownership Matrix）

| 泳道角色 | 状态 | 独占修改文件清单 | 职责边界 |
| :--- | :---: | :--- | :--- |
| **首席架构师 / 领域专家（后端 SSOT 与 CLI 会话层）** | ✅ **已完工冻结** | `include/rtx/param_schema.h`, `src/core/param_schema.cpp`, `include/rtx/logger.h`, `src/core/logger.cpp`, `include/rtx/i18n.h`, `src/core/i18n.cpp`, `include/rtx/hw_transcode.h`, `src/core/hw_transcode.cpp`, `src/cli/main.cpp`, `CMakeLists.txt`, `tests/test_i18n.cpp` | 后端契约已锁定，UI 工程师**严禁**修改上述后端核心文件 |
| **工程师 1（WebUI 极简去重与样式定标泳道 — Track C）** | ⏳ 待执行（先做） | `webui/server.py`, `webui/app.js`, `webui/index.html`, `webui/state.json` | 消费 `CineRTX.exe --schema-json` 与 `--session-stdio`，确定组件样式基准 |
| **工程师 2（Win32 原生 GUI 对齐泳道 — Track B）** | ⏳ 待执行（后做） | `src/gui/rtx_gui.cpp` | 消费 `rtx/param_schema.h`、`rtx/i18n.h`、`rtx/logger.h`，与 WebUI 参数矩阵 1:1 对齐 |

---

## 二、 Tasks（双端 UI 工程师原子级施工任务集）

### Track C（工程师 1 — 先行施工）：WebUI 极简去重、UTF-8 乱码修复与 `--session-stdio` 对接（`webui/`）

#### Task 2C-1：重构 `webui/server.py` 为零业务知识的纯 JSON 管道桥（≤ 200 行）
- **目标位置**：`webui/server.py`
- **施工动作**：
  1. **彻底删除以下重复代码（共砍除 ~450 行）**：
     - 删除 `PROFILE_LIMITS`、`PROFILE_NONNEG`、`PROFILE_ENUMS`、`RESOLUTIONS`（第 47–70 行）及 `validate_section` 中的手工字段校验（第 127–168 行）；
     - 删除 `PROGRESS_FULL`、`PROGRESS_PLAIN`、`_REPORT_INT_SUFFIXES`、`REPORT_RES`（10 条正则表达式）及 `parse_report`（第 289–349 行）；
     - 删除 `JobRunner.build_argv`（第 402–449 行）。
  2. **新增 `GET /api/schema` 路由**：
     调用 `subprocess.run([engine_path, "--schema-json"], capture_output=True, text=True)`，将 C++ 核心导出的 `defaults`、`params` 与双语 `i18n` 字典直接返回给浏览器。
  3. **将 `JobRunner.start()` 与 `cancel()` 切换至 `--session-stdio`**：
     - 启动子进程：`Popen([engine_path, "--session-stdio"], stdin=PIPE, stdout=PIPE, stderr=PIPE, text=True, encoding="utf-8")`，将 `json.dumps(doc) + "\n"` 写入 `proc.stdin` 并 `flush()`；
     - 读取 `proc.stdout`：每一行本身就是合法的 JSONL 事件（`type == "progress"` 更新 `runtime.job.progress`；`type == "log"` 追加至 `runtime.job.logTail`；`type == "report"` 更新终态 `status`、`exitCode` 与 `report`），直接 `json.loads(line)` 更新 `HOUSE` 并通过 SSE 推送！
     - **协作式取消 `JobRunner.cancel()`**：向 `proc.stdin` 写入 `"cancel\n"` 并 `flush()`，等待最多 5 秒让引擎优雅写完 MP4 `moov` 容器尾（仅当 5 秒超时未退出时才兜底 `terminate()`）。

#### Task 2C-2：清理 `webui/app.js` 260 行重复词典、修复第 813 行乱码并对齐 `index.html` / `state.json`
- **目标位置**：`webui/app.js`、`webui/index.html`、`webui/state.json`
- **施工动作**：
  1. 在 `webui/app.js` 中：
     - 删除第 25–260 行硬编码的 `const I18N = { en: {...}, zh: {...} };`，改为在 `boot()` 时请求 `GET /api/schema` 动态加载 `schema.i18n` 与 `schema.defaults`。
     - **修复第 813 行 UTF-8 乱码（Mojibake）**：
       将第 813 行的乱码字符串 `"ð\x9f\x93\x81 "` / `"ð\x9f\x93\x84 "` 修正为干净的 Unicode 转义 `"\uD83D\uDCC1 "` / `"\uD83D\uDCC4 "`。
     - **使用后端同源推导规则**：输出文件名自动推导仅针对 `_remaster_(av1|hevc).mp4`。
  2. 在 `webui/index.html` 中：
     - 从 `<select id="ctl-codec">` 中**删除 `<option value="h264" data-i18n="OptCodecH264">`（8-bit SDR 已废弃）**，并移除其他控件上冗余的 `data-disable-when="profile.codec=h264"`；
     - 在第 92 行 VSR 质量档位旁补齐 `profile.vsrEnabled` 复选框（`data-bind="profile.vsrEnabled"`，与 Win32 GUI `IDC_CHK_VSR` 对齐）；
     - 在第 112–116 行的 RIFE 引擎路径输入框（`id="ctl-rife"`）旁补齐 `<button class="btn" data-browse="profile.rifeEngine" data-lock data-i18n="BtnBrowse">浏览...</button>`。
  3. 在 `webui/state.json` 中：
     - 将 `"qp": 26` 修正为 `"qp": 24`（与 Core / CLI / GUI 100% 统一），并补齐 `"vsrEnabled": true`。

---

### Track B（工程师 2 — 待 WebUI 定标后施工）：Win32 原生 GUI 重构与漏布局缺陷修复（`src/gui/rtx_gui.cpp`）

#### Task 2B-1：修复 `IDC_EDT_PEAK_NITS` 隐形 Bug、新增 RIFE 引擎路径选择行并全面接入 `param_schema.h`
- **目标位置**：`src/gui/rtx_gui.cpp`
- **缺陷定位**：
  1. `IDC_LBL_PEAK_NITS` 与 `IDC_EDT_PEAK_NITS` 在 `WM_CREATE` 创建后，在 `layoutControls()` 中**完全漏掉了 `MoveWindow` 调用**，导致其在窗口左上角 `(0, 0)` 处尺寸为 `0x0` 不可见！
  2. `collectOptions()` 直接硬编码 `opt.peak_nits = 1000;`，从未调用 `GetDlgItemTextW(hwnd, IDC_EDT_PEAK_NITS, ...)`！
  3. 选中 `60/120 FPS (RIFE)` 时没有 `.engine` 路径输入框与浏览按钮，也未调用 `rtx::resolveDefaultRifeEnginePath()`。
  4. `populateComboBoxes()` 仍包含 `"H.264 High (8-bit SDR)"` 选项以及大量手写 `isZh ? L"..." : L"..."` 硬编码字符串，未复用 `rtx::trW(rtx::StrId::Opt...)`。
- **施工动作**：
  1. 在 `src/gui/rtx_gui.cpp` 顶部引入 `#include "rtx/param_schema.h"` 与 `#include "rtx/logger.h"`，删除局部 `guiLog` 函数，统一改为调用 `rtx::logInfo(...)` / `rtx::logError(...)`。
  2. 在 `ControlId` 枚举新增 `IDC_LBL_RIFE_ENGINE`、`IDC_EDT_RIFE_ENGINE`、`IDC_BTN_BROWSE_RIFE`。
  3. 在 `populateComboBoxes()` 中：
     - **移除 `H.264 High (8-bit SDR)` 选项**，`IDC_CMB_CODEC` 仅保留索引 0（`rtx::trW(rtx::StrId::OptCodecAv1)`）与索引 1（`rtx::trW(rtx::StrId::OptCodecHevc)`），并移除 `syncControlStates()` 中冗余的 `isH264` 分支；
     - 将 Preset、Rate Control、Resolution、Geometry、VSR Quality、Target FPS 下拉框的全部 `ComboBox_AddString` 替换为 `rtx::trW(rtx::StrId::Opt...)`，实现零硬编码双语切换。
  4. 在 `layoutControls()` 的双列参数网格中：
     - **Grid Row 6**：左侧放置 `IDC_LBL_PEAK_NITS` + `IDC_EDT_PEAK_NITS`，右侧放置 `IDC_CHK_HDR10PLUS`；
     - **Grid Row 7**：左侧放置 `IDC_LBL_RIFE_ENGINE`（`rtx::trW(rtx::StrId::WuiParamRifeEngine)`），右侧放置 `IDC_EDT_RIFE_ENGINE` + `IDC_BTN_BROWSE_RIFE`（当 `IDC_CMB_FPS` 选中 RIFE 模式即索引 3 或 4 时启用，否则禁用）；
     - **Grid Row 8**：放置 3D LUT 路径与浏览按钮（`IDC_EDT_LUT_PATH` + `IDC_BTN_BROWSE_LUT`）。
  5. 在 `collectOptions()` 与 `autoFillOutputPath()` 中：
     - 直接构造 `rtx::TranscodeProfile profile{}`，读取界面控件值填入 `profile`，调用 `rtx::validateAndNormalizeProfile(profile, false)` 与 `rtx::profileToHwOptions(profile)` 生成 `HwTranscodeOptions`，彻底消灭 GUI 侧的手写参数转换与文件名后缀拼接！

---

## 三、 Checklists（第二阶段量化验收门禁清单）

- [x] **门禁 2.0（后端 SSOT 契约、DebugView 日志与单元测试门禁 — 领域专家已验收）**：
  - 执行命令：`ctest --test-dir build/cmake/windows-msvc-ninja --output-on-failure` 与 `CineRTX.exe --schema-json`
  - 通过标准：`i18n_table_bounds`（113 项）、`param_schema_ssot_contract` 100% PASS；`CMakeLists.txt` 零冗余 `option()` 开关；`main.cpp` 零伪开关残留。
- [ ] **门禁 2.1（三端默认参数 100% 零偏差校验）**：
  - 执行命令：`.\build\cmake\windows-msvc-ninja\CineRTX.exe --schema-json` 并与 `webui/state.json` 及 `rtx_gui.cpp` 初始状态比对
  - 通过标准：`codec="av1"`, `preset=5`, `rateControl="cbr"`, `qp=24`, `resolution="4k"`, `vsrEnabled=true`, `vsrQuality=4`, `peakNits=1000`, `fpsMode="60-nvofa"`, `copyAudio=true` 在三端 100% 完全一致，且三端均无 8-bit SDR H.264 输出选项。
- [ ] **门禁 2.2（WebUI 零重复词典、零正则刮削与零乱码验证 — 工程师 1 门禁）**：
  - 执行命令：`rg -n "(REPORT_RES|PROGRESS_FULL|build_argv|PROFILE_LIMITS)" webui/server.py` 与 `rg -n "ð" webui/app.js`
  - 通过标准：两项扫描均为 **0 命中**；`webui/server.py` 总行数 $\le 220$ 行，`node --check webui/app.js` 与 `python -m py_compile webui/server.py` 零警告通过。
- [ ] **门禁 2.3（WebUI 协作取消输出容器完整性验证 — 工程师 1 门禁）**：
  - 执行命令：通过 WebUI 启动转码并在进度 30% 时点击“停止转码”，随后对已生成的输出文件执行 `ffprobe out.mp4`
  - 通过标准：子进程通过 `stdin` `"cancel\n"` 优雅退出，`out.mp4` 包含完整 `moov` box，可被播放器正常打开播放。
- [ ] **门禁 2.4（Win32 GUI 控件完整可见性、真绑定与 DebugView 监看验证 — 工程师 2 门禁）**：
  - 执行命令：打开 Sysinternals DebugView（过滤 `[CineRTX]`），无参启动 `CineRTX.exe`，修改 `Peak Nits` 为 `1500`、切换 `FPS` 至 `60 FPS (RIFE)`
  - 通过标准：`Peak Nits` 输入框清晰可见且传入 `HwTranscodeOptions.peak_nits == 1500`；选中 RIFE 时 `IDC_EDT_RIFE_ENGINE` 与浏览按钮可用；DebugView 实时捕获 `[CineRTX][P:.../T:...][INFO]` 日志流。
- [ ] **门禁 2.5（`CineRTX.exe` 零网络库与零外部 CRT 导入验证）**：
  - 执行命令：`pwsh tools/audit_imports.ps1`
  - 通过标准：`CineRTX.exe` 静态导入表中 `WS2_32.dll` 与 `MSVCP140.dll`/`VCRUNTIME140.dll` 计数严格为 **0**。
