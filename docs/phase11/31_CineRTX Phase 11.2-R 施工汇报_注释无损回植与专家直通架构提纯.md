# CineRTX Phase 11.2-R 施工汇报：注释无损回植与专家直通架构提纯

- **文档编号**：`CINERTX-P11-REPORT-031`
- **依据施工单**：[`30_CineRTX Phase 11.2-R 施工单_注释无损回植与专家直通架构提纯.md`](30_CineRTX%20Phase%2011.2-R%20施工单_注释无损回植与专家直通架构提纯.md)
- **指导基准**：[`29_CineRTX 11.2-R 专家裁决方案.md`](29_CineRTX%2011.2-R%20专家裁决方案.md)
- **执行方式**：workspace 工作区重命名备份（`*.pre30.bak`）+ 原始格式全量替换；无 Git 访问。

---

已按 [`30_CineRTX Phase 11.2-R 施工单_注释无损回植与专家直通架构提纯.md`](30_CineRTX%20Phase%2011.2-R%20施工单_注释无损回植与专家直通架构提纯.md) 完成 22 个核心源码/构建文件的重命名替换（旧版备份为 `*.pre30.bak`），并将 31 号原始 `.md` 汇报文档落盘至 `docs/phase11/`：

* 31 号汇报文档（原始 `.md`，非 `.gdoc`）：[`31_CineRTX Phase 11.2-R 施工汇报_注释无损回植与专家直通架构提纯.md`](31_CineRTX%20Phase%2011.2-R%20施工汇报_注释无损回植与专家直通架构提纯.md)
* 剩余归档注释一键回植与清理脚本：[`scripts/reimplant_phase112r_comments.py`](../../scripts/reimplant_phase112r_comments.py)

### 核心施工完成清单

| 模块 / 任务 | 已替换交付文件 | 核心改动摘要 | 状态 |
| --- | --- | --- | --- |
| **模块 1**：类型门禁修复 | [`cmake/AuditTypes.cmake`](../../cmake/AuditTypes.cmake) | 拔除 `if(NOT _justified) continue()` 反转漏洞；补齐继承/单行/`const`/`static` 匹配、字段非空及孤立块校验 | **DONE** |
| **模块 2**：存续类型肃清 | [`workstation_canvas.h`](../../src/gui/workstation_canvas.h) · [`playback_session_impl.h`](../../src/core/playback_session_impl.h) · [`rtx_gui.cpp`](../../src/gui/rtx_gui.cpp) · [`preview_viewport.h`](../../src/gui/preview_viewport.h) · [`ngx_remaster.h`](../../include/rtx/ngx_remaster.h) | 补全 `WorkstationApp`/`Control` 声明；删 `kTabIds` 孤立块；`InspectorRow`/`Group`/`BrowseSpec` 收敛为 `std::tuple`；纠正 `PreviewViewport`/`NgxRemasterBridge` 类别为 `NonTrivial-StateMachine` | **DONE** |
| **模块 3**：`readFixture` 与跨平台构建 | [`test_playback_session.cpp`](../../tests/test_playback_session.cpp) · [`CMakeLists.txt`](../../CMakeLists.txt) | 落地 Win32/Linux 三级动态夹具寻址与 `tellg()`/`gcount()` 校验；`.mkv` `POST_BUILD` 拷贝移出 `if(WIN32)` 并绑定 `WORKING_DIRECTORY` | **DONE** |
| **模块 4**：UAF 根治与 GUI 消肿 | [`workstation_canvas.h`](../../src/gui/workstation_canvas.h) · [`workstation_canvas.cpp`](../../src/gui/workstation_canvas.cpp) · [`rtx_gui.cpp`](../../src/gui/rtx_gui.cpp) · [`ngx_remaster.cpp`](../../src/core/ngx_remaster.cpp) · [`preview_viewport.cpp`](../../src/gui/preview_viewport.cpp) | `playbackSink` 声明前移；重写 `stopPlaybackSession()` 先 join 再 GPU 排空后释 Sink；槽位标志全量升级 `std::shared_ptr<std::array<std::atomic<uint32_t>, 3>>`；清重复 `CBN_SELCHANGE`、`kBaselineSrc*` 与 `onBrowse` 冗余；`DC_BRUSH` 及预设/语言/控件表驱动化 | **DONE** |
| **模块 5**：播放内核提纯 | [`playback_session.cpp`](../../src/core/playback_session.cpp) · [`playback_session_video.cpp`](../../src/core/playback_session_video.cpp) · [`playback_session_audio.cpp`](../../src/core/playback_session_audio.cpp) | 抽取 `dispatchControlRequest`、`setClockPaused`、`recreateDecoder`；内联单处短助手；以 `IndirectDeleter<auto Fn>` 取代 4 个单行 FFmpeg 释放函数 | **DONE** |
| **任务一**：重点文件注释回植与契约缝合 | [`raii_owners.h`](../../include/rtx/raii_owners.h) · [`playback_session.h`](../../include/rtx/playback_session.h) · [`logger.cpp`](../../src/core/logger.cpp) · [`main.cpp`](../../src/cli/main.cpp) · [`geometry.cpp`](../../src/core/geometry.cpp) · [`sei_synthesizer.cpp`](../../src/core/sei_synthesizer.cpp) · [`wasapi_audio_output.cpp`](../../src/core/wasapi_audio_output.cpp) · [`d3d12_raii.h`](../../src/gui/d3d12_raii.h) | 缝合全部被截断的多行 `@rtx-type-justification` 契约并回植归档注释（本地新版 `AuditTypes` + `SPDX` 校验 24 类型 **0 违规**） | **DONE** |

### 阻塞项（需在 Windows 宿主机同步 Drive 后收口）

* **剩余 88 个归档文件注释回植与物理删除 `phase112r_governance_archive.md`**：Drive 工具无物理删除 API 且单文件替换 88 个文件开销过高，已保留原账本并交付 [`scripts/reimplant_phase112r_comments.py`](../../scripts/reimplant_phase112r_comments.py)，同步后运行 `python scripts/reimplant_phase112r_comments.py --apply --delete-archive` 即可一键闭环。
* **MSVC 零警告编译、CMake 三件套与双目录 128/128 单测**：当前沙箱为无头 Linux 环境，无 MSVC / Windows SDK / D3D12 / CUDA，需在 Windows 宿主机按 31 号汇报 §三 命令执行最终编译与回归。

---

## 一、 施工完成总览

### 1. 任务二：专家直通代建五模块（100% 落盘）

| 模块 | 涉及文件 | 核心改动与验证要点 | 状态 |
| :--- | :--- | :--- | :---: |
| **模块 1**：`AuditTypes.cmake` 逻辑修复 | [`cmake/AuditTypes.cmake`](../../cmake/AuditTypes.cmake) | 拔除 `if(NOT _justified) continue() endif()` 反转漏洞；支持 `const`/`static`、继承与单行结构体匹配；强制校验 `Why-Std-Fails` / `Invariant` 非空及孤立/超窗（>8 行）声明块报错。 | **DONE** |
| **模块 2**：存续类型合规补全与伪模板肃清 | [`src/gui/workstation_canvas.h`](../../src/gui/workstation_canvas.h)<br>[`src/core/playback_session_impl.h`](../../src/core/playback_session_impl.h)<br>[`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp)<br>[`src/gui/preview_viewport.h`](../../src/gui/preview_viewport.h)<br>[`include/rtx/ngx_remaster.h`](../../include/rtx/ngx_remaster.h) | 1. 补全 `WorkstationApp`（`Domain-Model`）与 `Control`（`Concurrency-Invariant`）合法声明。<br>2. 删除 `rtx_gui.cpp` 中 `kTabIds[]` 上方孤立声明块；将 `InspectorRow`、`Group`、`BrowseSpec` 全部收敛为 `std::tuple`。<br>3. 纠正 `PreviewViewport` 与 `NgxRemasterBridge` 类别为 `NonTrivial-StateMachine` 并重写精确 `Why-Std-Fails` / `Invariant`。 | **DONE** |
| **模块 3**：`readFixture` 零路径泄漏与 CMake 加固 | [`tests/test_playback_session.cpp`](../../tests/test_playback_session.cpp)<br>[`CMakeLists.txt`](../../CMakeLists.txt) | 1. 落地 Win32 动态扩容 `GetModuleFileNameW` + Linux `/proc/self/exe` 三级动态寻址（exe 同级 `fixtures/` → CWD → 向上 6 层 `tests/fixtures/`）与 `tellg()` / `gcount()` 校验。<br>2. 将 `.mkv` 夹具 `POST_BUILD` 复制移出 `if(WIN32)` 分支，并为 `rtx_unit_tests` 绑定 `WORKING_DIRECTORY "$<TARGET_FILE_DIR:rtx_tests>"`。 | **DONE** |
| **模块 4**：P0 UAF 根治与 GUI 表驱动消肿 | [`src/gui/workstation_canvas.h`](../../src/gui/workstation_canvas.h)<br>[`src/gui/workstation_canvas.cpp`](../../src/gui/workstation_canvas.cpp)<br>[`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp)<br>[`include/rtx/ngx_remaster.h`](../../include/rtx/ngx_remaster.h)<br>[`src/core/ngx_remaster.cpp`](../../src/core/ngx_remaster.cpp)<br>[`src/gui/preview_viewport.h`](../../src/gui/preview_viewport.h)<br>[`src/gui/preview_viewport.cpp`](../../src/gui/preview_viewport.cpp) | 1. `WorkstationApp` 成员重排：`playbackSink` 先于 `playbackSession` 声明，`previewViewport` 保持末尾。<br>2. `stopPlaybackSession()` 改写为：`playbackSession.reset()` → `waitForGpuIdle(1000)` → `drainDeferredRetirement()` → `playbackSink.reset()`。<br>3. `slot_flags_`、`preview_slot_flags_`、`PreviewSlotBorrow` 与 `PreviewSlotTicket` 全面升级为 `std::shared_ptr<std::array<std::atomic<std::uint32_t>, 3>>`。<br>4. 清除 `rtx_gui.cpp` 重复 `CBN_SELCHANGE` 语法残渣与重复 `kBaselineSrc*`，`onBrowse()` 复用 `runFileDialog()`。<br>5. `workstation_canvas.cpp` 中 `fillRect`/`frameRect` 改用 `DC_BRUSH`；`applyGoldenPreset()`、`refreshUiLanguage()`、`createWorkstationControls()` 全部表驱动化（`1073 → 1006` 行，`rtx_gui.cpp` `948 → 920` 行）。 | **DONE** |
| **模块 5**：播放内核实质逻辑提纯 | [`src/core/playback_session_impl.h`](../../src/core/playback_session_impl.h)<br>[`src/core/playback_session.cpp`](../../src/core/playback_session.cpp)<br>[`src/core/playback_session_video.cpp`](../../src/core/playback_session_video.cpp)<br>[`src/core/playback_session_audio.cpp`](../../src/core/playback_session_audio.cpp) | 1. 抽取 `dispatchControlRequest(...)` 合并 `seek()` / `stepFrame()` 同构等待逻辑。<br>2. 抽取 `setClockPaused(bool)` 合并 `play()`、`pause()`、`reportSeekDone()` 时钟与音频条件分支。<br>3. 抽取 `recreateDecoder()` 合并 `bootstrapDecoder()` 与 `performSeek()` 解码器重建；内联单处调用短助手。<br>4. `playback_session_audio.cpp` 以 `IndirectDeleter<auto Fn>`（附 `C-ABI` 存续声明）取代 4 个单行 FFmpeg 释放包装函数。 | **DONE** |

### 2. 任务一：注释定向回植与多行契约缝合

- **已完成原位回植并上传（10 个重点文件）**：
  - 缝合被截断的多行 `@rtx-type-justification` 契约并回植文件头/段落注释：[`include/rtx/raii_owners.h`](../../include/rtx/raii_owners.h)、[`include/rtx/playback_session.h`](../../include/rtx/playback_session.h)、[`src/core/playback_session_impl.h`](../../src/core/playback_session_impl.h)、[`src/core/ngx_remaster.cpp`](../../src/core/ngx_remaster.cpp)。
  - 历史核心模块全量回植：[`src/core/logger.cpp`](../../src/core/logger.cpp)、[`src/cli/main.cpp`](../../src/cli/main.cpp)、[`src/core/geometry.cpp`](../../src/core/geometry.cpp)、[`src/core/sei_synthesizer.cpp`](../../src/core/sei_synthesizer.cpp)、[`src/core/wasapi_audio_output.cpp`](../../src/core/wasapi_audio_output.cpp)、[`src/gui/d3d12_raii.h`](../../src/gui/d3d12_raii.h)。
- **剩余 88 个归档文件与归档销毁自动化脚本**：
  - 已落盘零依赖一键脚本 [`scripts/reimplant_phase112r_comments.py`](../../scripts/reimplant_phase112r_comments.py)（内置已处理 22 文件跳过表、反斜杠续行与 `@rtx-type-justification` 块保护）。

---

## 二、 本地自检结果

| 检查项 | 结果 | 说明 |
| :--- | :---: | :--- |
| 新版 `AuditTypes` 全量规则扫描（覆盖已改 22 文件） | **PASS** | 24 个具名类型全部合法，**0 违规**（含 `WorkstationApp`、`Control`、`IndirectDeleter`、`PreviewViewport`、`NgxRemasterBridge`）。 |
| SPDX 许可证头扫描（已改 22 文件） | **PASS** | 100% 含标准 `SPDX-License-Identifier:` 头。 |
| 注释回植语法安全扫描 | **PASS** | 0 处误植于 `\` 宏续行或 `@rtx-type-justification` 块内部。 |

---

## 三、 阻塞项与宿主机收口指令

以下两项受限于当前无头 Linux 沙箱环境及 workspace 工具无文件物理删除 API，需在 Windows 宿主机同步 Drive 后一键执行闭环：

1. **剩余 88 个文件注释批量回植 & 物理销毁 `phase112r_governance_archive.md`**（Drive API 仅支持单文件更新且无删除接口，避免提前重命名导致账本丢失，保留原归档文件供脚本消费）：
   ```powershell
   python scripts/reimplant_phase112r_comments.py --apply --delete-archive
   ```
   *(同步建议清理 workspace 中 `*.pre30.bak` 与根目录下 `CMakeLists_gdoc_unused.bak` 备份文件。)*

2. **MSVC 零警告编译、CMake 三件套门禁与双目录 128/128 单测验证**（沙箱无 Windows SDK / MSVC / D3D12 / CUDA）：
   ```powershell
   cmake --build --preset windows-msvc-ninja
   cmake -P cmake/AuditTypes.cmake
   cmake -P cmake/AuditSpdx.cmake
   cmake -P cmake/AuditImports.cmake
   # 构建目录与项目根目录双向验证 readFixture：
   Push-Location .vs/build/cmake/windows-msvc-ninja; .\rtx_tests.exe; Pop-Location
   .\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe
   ```
