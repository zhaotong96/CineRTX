# CineRTX Phase 11.3-S 48小时应急止血与开源合规净空 SuperPower 施工单

**工单编号**：`CINERTX-P11-WO-047`  
**下达日期**：2026-11-22  
**执行周期**：严格限时 48 小时（倒计时起算）  
**执行人**：外部高级 C++/GPU 实施工程师（External Senior C++/GPU Engineer）  
**验收与负责人**：项目负责人 / Coordinator（接收成果后执行真机物证与终审封板）  
**上位指导文件**：[`docs/phase11/46_CineRTX Phase 11 验收与重构.md`](./46_CineRTX%20Phase%2011%20验收与重构.md)

---

## 一、 施工背景与硬性目标

46 号终审裁决书对 Phase 11.3-S 签发了「**有条件通过并暂缓纯净战略封板（CONDITIONAL PASS / HOLD CLEAN STRATEGIC SEAL）**」。  
在转入 Phase 11.4 决胜冲刺前，外部高级工程师必须在 48 小时内对 46 号裁决指出的核心代码缺陷与合规硬伤实施精准外科手术，实现以下四项硬性目标：

1. **音频管线真合龙**：彻底拔除 `onNleExportSequence()` 中用于应付 `.map` 审计的虚假 `toJson/fromJson` 空转，真正将编译好的 `AudioGraphSegment` 接入 `hw_transcode_stages.cpp` 导出管线，实现 1V+1A 真实混音导出。
2. **WSOLA 算法缺陷修复**：重构 `src/core/wsola_stretch.cpp`，消除多声道（3 声道及以上，如 5.1/7.1）时 `max_tail_frames < 256` 导致直接 `return 0` 静音的致命缺陷；消除推进窗口时的巨量全量内存搬移。
3. **开源隔离红线彻底恢复**：脱毒 [`CMakeLists.txt`](../../CMakeLists.txt)，切断 `rtx_core` 对 `rtx_ngx_bridge_static` 的静态链接，将专有 `nvsdk_ngx_s.lib` 封印回独立的 `rtx_ngx_bridge.dll`；恢复 `src/core/ngx_remaster.cpp` 的动态安全加载，严禁专有库污染 GPL 主程序。
4. **代码卫生与门禁平账**：彻底清理 CMake 5 处冗余 `CXX_STANDARD` 与无效宏，消灭死代码，将 `AuditSloc.cmake` 校准并锁定在 31,064 法定基线，四门禁全绿，135 项单元测试 100% 通过。

---

## 二、 核心外科手术任务分解

### 任务 1：NLE 音频图真实消费与假代码清剿 (Audio Export Pipeline Wiring)

- **涉及源码**：
  - [`src/gui/workstation_canvas.cpp`](../../src/gui/workstation_canvas.cpp)
  - [`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp)
  - [`src/core/hw_transcode_stages.cpp`](../../src/core/hw_transcode_stages.cpp)
  - [`include/rtx/hw_transcode.h`](../../include/rtx/hw_transcode.h)
- **缺陷现状**：
  - `onNleExportSequence()` 内部为了在 `.map` 符号表中体现 `toJson` 和 `fromJson`，强行执行了序列化与反序列化但丢弃了结果，属于欺骗性空转。
  - `exportSelectedClipToEncoder()` 属于废弃历史残余代码。
  - `compileRenderGraph()` 编译生成的 `RenderGraphPlan::audio_segments` 仅被打印到状态栏，根本未传入 `NvidiaTranscodeSession` 进行混音封包。
- **改造指令**：
  1. 物理删除 `onNleExportSequence()` 中的 `toJson` / `fromJson` 空转代码，删除废弃函数 `exportSelectedClipToEncoder()`。
  2. 扩展 `NvidiaTranscodeConfig` 或 `TranscodeSession` 参数，接受 `std::vector<AudioGraphSegment>`。
  3. 在 `hw_transcode_stages.cpp` 中，若存在音频轨道片段，按剪辑入出点与增益参数调用 `mixAvx2()` 与 `wsolaStretch()` 产出混合 PCM 帧，送入音频编码器/复用器，确保导出的 `.mkv` 包含真实的音频流。

### 任务 2：WSOLA 算法多声道支持与环形缓冲纯化 (WSOLA Bugfix & Optimization)

- **涉及源码**：
  - [`src/core/wsola_stretch.cpp`](../../src/core/wsola_stretch.cpp)
  - [`include/rtx/audio_mixer_avx2.h`](../../include/rtx/audio_mixer_avx2.h)
- **缺陷现状**：
  - `WsolaStreamState` 将 `overlap_tail` 固定为 512 个 `float`。在 6 声道（5.1）下，`max_tail_frames = 512 / 6 = 85 < 256`，直接触发安全防御返回 0，导致三声道以上音频彻底静音。
  - 每次消费样本，使用 `std::copy_n` 将环形样本整体左移，0.25x 慢放时带来大量无谓 CPU 搬移。
  - `softLimitSample()` 实际仅是 `std::clamp` 硬削波。
- **改造指令**：
  1. 将 `overlap_tail` 尺寸改为 `kWsolaHalf * channels` 动态匹配（支持 1~8 声道），确保多声道下 `max_tail_frames >= kWsolaHalf` 成立。
  2. 优化移位逻辑，采用 `std::shift_left` 或模运算读写指针避免全量数据拷贝。
  3. 修正混音限幅命名与行为，确保极端叠加时不产生溢出破音。

### 任务 3：开源合规隔离网重建 (Restore NGX Bridge DLL Isolation)

- **涉及源码**：
  - [`CMakeLists.txt`](../../CMakeLists.txt)
  - [`src/core/ngx_remaster.cpp`](../../src/core/ngx_remaster.cpp)
  - [`src/shim/ngx_bridge/bridge_exports.cpp`](../../src/shim/ngx_bridge/bridge_exports.cpp)
  - [`include/shim/ngx_bridge_abi.h`](../../include/shim/ngx_bridge_abi.h)
- **缺陷现状**：
  - Phase 10 Track 10A 将 `rtx_ngx_bridge` 降级为静态库，导致专有 `nvsdk_ngx_s.lib` 静态编入了 `CineRTX.exe`，直接践踏 GPL 开源红线与 [`docs/spec/compliance.md`](../spec/compliance.md)。
- **改造指令**：
  1. **恢复 DLL 目标**：在 `CMakeLists.txt` 中将 `rtx_ngx_bridge` 重新构建为 `SHARED` 动态链接库（`rtx_ngx_bridge.dll`），专有库 `nvsdk_ngx_s.lib` 仅允许被 `rtx_ngx_bridge` 私有链接。
  2. **切断静态直链**：从 `rtx_core` 的链接依赖中彻底移除 `rtx_ngx_bridge_static`。
  3. **恢复安全动态加载**：在 `src/core/ngx_remaster.cpp` 中恢复基于 `LoadLibraryExW` 与 `GetProcAddress` 的纯 C ABI 动态加载机制，并保持未安装 SDK 时的平滑无损降级能力。
  4. **清洗 CMake 遗留负债**：删除 5 处冗余的 `CXX_STANDARD 23` 属性、清除重复的 `SYSTEM PRIVATE` 头路径、删除空 `if(WIN32) endif()` 块及 `rtx_ngx_shadow_archives` 废弃代码。

### 任务 4：代码卫生平账与门禁校验 (Code Baseline Calibration)

- **涉及源码**：
  - [`cmake/AuditSloc.cmake`](../../cmake/AuditSloc.cmake)
  - 全仓清理后的 `.cpp` / `.h`
- **改造指令**：
  1. 通过清理死代码、假引用与冗余代码，将代码行数压回合理区间。
  2. 将 `cmake/AuditSloc.cmake` 中的法定行数基线精准校准为当前实际代码行数，消除倒挂告警。
  3. 本地运行并通过四门禁：`AuditTypes`、`AuditSloc`、`AuditSpdx`、`AuditImports`。

---

## 三、 外部高级工程师交付标准（准出清单）

高级工程师在将控制权移交回负责人之前，必须确保以下交付物全部合格并落盘：

| 交付项 | 验证命令 / 指标 | 必须达成结果 |
| :--- | :--- | :--- |
| **C++ 单元测试** | `ctest --test-dir build -C RelWithDebInfo --output-on-failure` | **135/135 测试全绿 (PASS)** |
| **类型门禁** | `cmake -P cmake/AuditTypes.cmake` | **PASS (153 项以内且无野类型)** |
| **SLOC 门禁** | `cmake -P cmake/AuditSloc.cmake` | **PASS (基线对齐，无倒挂漂移)** |
| **开源许可证门禁**| `cmake -P cmake/AuditSpdx.cmake` | **PASS (100% 符合开源标识)** |
| **PE 导入门禁** | `cmake -P cmake/AuditImports.cmake` | **PASS (`nvsdk_ngx_s` 彻底脱离主程序)** |
| **DLL 隔离验证** | `dumpbin /imports build/RelWithDebInfo/CineRTX.exe` | **绝无任何 `nvsdk_ngx` 导出或专有符号** |

---

## 四、 负责人（Coordinator）后续接手交接规程

在高级工程师完成上述代码级与构建级改造并通过门禁后，控制权即刻移交负责人，由负责人执行以下后续动作：

1. **真实物理 GPU 零拷贝闭环核验**：在 RTX 5060 硬件上执行实机转码压测，验证 D2H == 0 B。
2. **物理桌面监看物证采集**：启动带有 DirectComposition 窗口的播放流程，捕获桌面 `presented > 0` 截屏与交互日志。
3. **多段剪辑切点物证固化**：对导出的包含音视频多段的 `.mkv` 执行 `ffprobe` 逐帧校验，记录切点 IDR 注入及 90 kHz PTS 单调递增日志。
4. **起草 48 号施工汇报与 49 号验收委托函**：向首席领域专家报审，申请正式摘除「有条件通过」标识，完成 Phase 11.3-S 纯净战略封板印章。
5. **签发 Phase 11.4 决胜冲刺总动员**。

---

> [!NOTE]
> 本工单是 46 号裁决书授权的严格限时（48h）止血动作，严禁借机扩大重构范围至 Phase 12 的全局类型擦除工作。

> [!TIP]
> 外部工程师在恢复 `rtx_ngx_bridge.dll` 时，应充分复用 `include/shim/ngx_bridge_abi.h` 既有的纯 C ABI 导出约定，确保 DLL 与 EXE 之间的内存分配与异常边界绝对安全隔离。

> [!IMPORTANT]
> 严守仓库纪律：外部工程师在施工期间**严禁执行 `git push`**，本地每一步修改必须经过 CTest 验证。

> [!WARNING]
> 修复 `wsola_stretch.cpp` 时，必须保留 `alignas(32)` 对齐与 AVX2 矢量指令的前提条件，严禁引入任何堆内存抖动。

> [!CAUTION]
> 坚决禁止再次使用“无返回值消费、仅为应付符号审计”的作弊空转调用；一旦在终审中再次被专家查出此类行为，将直接触发一票否决。
