# CineRTX Phase 11.3-P SuperPower 施工单：竖屏视频全链路自适应与 DisplayMatrix 透传修复

**工单编号**：`CINERTX-P11-WO-050`  
**项目阶段**：`Phase 11.3-P (Portrait Fix & Full Pipeline Adaptive Repair)`  
**下达日期**：2026-11-22  
**执行周期**：严格限时 24 小时（倒计时起算）  
**执行人**：外部高级多媒体系统与 GPU 计算实施工程师（External Senior Multimedia & GPU Engineer）  
**协调负责人 / 门禁审查人**：项目 Coordinator（接收成果后执行编译自愈、门禁平账、RTX 5060 实机物证与封板委托）  
**上位指导与排查依据**：
- [`docs/phase11/49_CineRTX_Phase11_竖屏视频无法转码根因法医排查与修复方案.md`](./49_CineRTX_Phase11_竖屏视频无法转码根因法医排查与修复方案.md)（`CINERTX-P11-FORENSIC-049`）
- [`docs/phase11/46_CineRTX Phase 11 验收与重构.md`](./46_CineRTX%20Phase%2011%20验收与重构.md)（`CINERTX-P11-AUDIT-046`）
- [`docs/phase11/47_CineRTX Phase 11.3-S 48小时应急止血与开源合规净空SuperPower施工单.md`](./47_CineRTX%20Phase%2011.3-S%2048小时应急止血与开源合规净空SuperPower施工单.md)（`CINERTX-P11-WO-047`）
- [`DECISIONS.md`](DECISIONS.md)（`[DEC-008]`, `[DEC-009]`, `[DEC-010]`）
- [`ISSUES.md`](ISSUES.md)（`[ISSUE-008]`）

---

## 一、 施工背景与硬性目标

根据首席多媒体与 GPU 计算领域专家签发的 [49号法医排查与修复方案](./49_CineRTX_Phase11_竖屏视频无法转码根因法医排查与修复方案.md)，当前版本在竖屏视频转码链路中暴露出 6 项交织的阻断性与回归性缺陷（`RC-PORTRAIT-01` ~ `RC-PORTRAIT-06`）。这不仅导致用户实拍竖屏视频 100% 出现转码报错、尺寸倒挂硬熔断、画面暴力拉扁、下半屏预览截断以及手机实拍横躺等严重故障，更是阻碍 Phase 11.3 获得最终战略封板印章的直接阻碍。

外部高级工程师必须在 24 小时内，基于已经完成 `rtx_ngx_bridge.dll` 隔离净空的当前工作区树，实施精准外科手术式修复，达成以下六项硬性目标：

1. **全档位智能几何翻转与原尺寸语义直通**：重构尺寸解析与几何契约，消除切换预设时的旧尺寸脏残留；实现 4K/2K/FHD 全档位对竖屏输入的 9:16 等比自适应翻转；保障 `native` 模式原画直通，杜绝强行插值放大。
2. **RIFE TensorRT Dynamic Profile 高度扩展**：将 `rife_builder.cpp` 动态 Profile 上限拓展至 `2176x2176`，使动态引擎原生承载常见竖屏视频（`1080x1920` 等），彻底消除 TRT 拒载崩溃。
3. **容器旋转矩阵（`AV_PKT_DATA_DISPLAYMATRIX`）全链路透传**：在 `MediaInfo`、Demuxer 与 Muxer 中打通 36 字节 DisplayMatrix 的无损读取与原样透传，手机竖屏实拍不再横躺。
4. **Pillarbox 黑边对齐与 NLE 参数归一化**：修正 Pillarbox 丢弃偏移量导致的横向压扁；在 NLE 序列导出前强制执行 Profile 归一化。
5. **零回读预览环等比自适应与 NVENC 边界防护**：CUDA 核函数改写为等比居中自适应缩放，消灭竖屏下半屏裁切；NVENC 初始化前校验高度 Caps，并在超标时自动启用 High Tier。
6. **现代 C++ 规范与全量单测全绿**：严守 `[DEC-009]` 零自定义类与标准库优先军规；新增全档位竖屏单元测试矩阵，确保 136+ 项单元测试与四门禁 100% PASS。

---

## 二、 核心外科手术任务分解

### 任务 1：全档位竖屏几何契约与预设覆写修复 (RC-PORTRAIT-01 & RC-PORTRAIT-04)

- **涉及源码**：
  - [`src/core/param_schema.cpp`](../../src/core/param_schema.cpp)（约 256~270 行）
  - [`src/core/geometry.cpp`](../../src/core/geometry.cpp)（约 39~51 行）
  - [`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp)（约 218~235 行）
  - [`src/core/hw_transcode_stages.cpp`](../../src/core/hw_transcode_stages.cpp)（约 64~66 行）
- **缺陷现状**：
  - `param_schema.cpp` 中 `profile.resolution == "4k"` 仅在 `target_width <= 0 && target_height <= 0` 时赋值 `3840x2160`，导致从 2K/FHD 切回 4K 时残留旧尺寸。
  - `hw_transcode.cpp` 仅对 `target_width_ == 3840 && target_height_ == 2160` 触发 `calculateSmartGeometry`，竖屏选择 2K（`2560x1440`）或 FHD（`1920x1080`）时未翻转，直接触发 `target_h < info.height` 硬报错 `"remaster target geometry must not be smaller than the source"`。
  - `native` 模式因 `target_width <= 0` 强行进入 `calculateSmartGeometry`，被错误放大为 4K。
  - `Pillarbox` 模式仅取 `canvas_width / canvas_height`，丢弃黑边偏移，造成 9:16 压扁为 16:9。
- **改造指令**：
  1. **无条件按预设覆写**：在 `param_schema.cpp` 中，无论是 4K、2K、FHD 还是 native，无条件按预设重置 `target_width` 与 `target_height`，切断旧残留。
  2. **`native` 语义锁定**：在 `hw_transcode.cpp` 中，若 `target_width_ == 0 && target_height_ == 0`，直接赋值为 `info_.width` 与 `info_.height`，直通原生尺寸。
  3. **AutoDirect 全档位等比竖屏翻转**：当 `is_portrait_input && geom_mode == GeometryTargetMode::AutoDirect && target_width_ > target_height_` 时，对 4K（`3840x2160 -> 2160x3840`）、2K（`2560x1440 -> 1440x2560`）、FHD（`1920x1080 -> 1080x1920`）统一按源纵横比翻转目标尺寸，向 2 对齐，且保底 `target_w >= info_.width && target_h >= info_.height`。
  4. **Pillarbox 黑边保护**：传递真实的 `scaled_width` 与 `offset_x` 至桥接配置，对输入高度大于画布高度的竖屏源禁止反向挤压并进行清晰防御。

---

### 任务 2：RIFE TensorRT Profile 动态维度扩展 (RC-PORTRAIT-02)

- **涉及源码**：
  - [`src/shim/ngx_bridge/rife_builder.cpp`](../../src/shim/ngx_bridge/rife_builder.cpp)（约 594~609 行）
  - [`src/shim/ngx_bridge/rife_engine.cpp`](../../src/shim/ngx_bridge/rife_engine.cpp)（约 529~546 行）
- **缺陷现状**：
  - TensorRT 动态优化 Profile 的最大高度写死为横屏上限 `max_h = (std::max)(1088, opt_h)`。
  - 竖屏 `1080x1920` 的输入高度 `1920 > 1088`，导致 `fits()` 判定失败，TensorRT 执行 `setInputShape` 直接拒绝并崩溃。
- **改造指令**：
  1. 将 `rife_builder.cpp` 中的最大宽高对称放开：
     ```cpp
     // 64 字节对齐，覆盖 1920x1088 与 1088x1920，对称支持横竖屏 2K（2176）
     const int max_w = (std::max)(2176, opt_w);
     const int max_h = (std::max)(2176, opt_h);
     ```
  2. 保持 `rife_engine.cpp` 中的 `fits` 逻辑与显存校验契约一致，允许竖屏尺寸安全命中动态 Profile。

---

### 任务 3：FFmpeg 容器旋转侧数据 DisplayMatrix 透传 (RC-PORTRAIT-03)

- **涉及源码**：
  - [`include/rtx/io.h`](../../include/rtx/io.h)
  - [`src/io/ffmpeg_io.cpp`](../../src/io/ffmpeg_io.cpp)（约 498~502 行与 842~883 行）
- **缺陷现状**：
  - 解复用器只提取了常规宽高，丢弃了 `AV_PKT_DATA_DISPLAYMATRIX`。实拍竖屏视频（横屏物理编码 + 旋转标签）被当作横屏处理，导出后更彻底丢失旋转信息，导致播放器横躺呈现。
- **改造指令**：
  1. 在 `MediaInfo` 结构体中新增旋转元数据容器：
     ```cpp
     std::array<int32_t, 9> display_matrix{};
     bool has_display_matrix{false};
     ```
  2. 在 `FfmpegDemuxer::open()` 中，调用 `av_packet_side_data_get()` 或遍历 `stream->codecpar->coded_side_data` 提取 `AV_PKT_DATA_DISPLAYMATRIX`，存入 `info.display_matrix` 并标记 `info.has_display_matrix = true`。
  3. 在 `FfmpegMuxer::open()` 中，若输入流带有 `has_display_matrix`，使用 `av_stream_new_side_data(stream_, AV_PKT_DATA_DISPLAYMATRIX, sizeof(int32_t) * 9)` 或 `av_packet_side_data_add()` 将该 36 字节矩阵完整注入到输出流的侧数据中。

---

### 任务 4：NLE 导出预归一化与 CUDA 预览核函数自适应 (RC-PORTRAIT-05)

- **涉及源码**：
  - [`src/gui/workstation_canvas.cpp`](../../src/gui/workstation_canvas.cpp)（约 570~592 行）
  - [`src/shim/ngx_bridge/p010_pack.cu`](../../src/shim/ngx_bridge/p010_pack.cu)（约 462~466 行）
- **缺陷现状**：
  - `onNleExportSequence()` 漏调 `validateAndNormalizeProfile()`，将未规范化的 profile 送入硬件转码管线。
  - 3 槽零回读预览环尺寸固定为横屏 `960x540`，CUDA 核函数以硬编码 `x0 >> 2, y0 >> 2` 取样写入。竖屏 `2160x3840` 时 `py` 超出 540，下半屏 43.75% 画面被强制裁剪丢弃。
- **改造指令**：
  1. 在 `onNleExportSequence()` 调用 `profileToHwOptions(profile)` 前，显式执行 `(void)rtx::validateAndNormalizeProfile(profile, false);`。
  2. 重构 `p010_pack.cu` 内部的预览采样坐标映射逻辑，依据源图宽高比计算等比居中缩放或动态步长映射，保证竖屏画面在 `960x540` 视口中完整呈现且不发生越界与截断。

---

### 任务 5：NVENC 4K 竖屏高度 Caps 探测与 High Tier 提升 (RC-PORTRAIT-06)

- **涉及源码**：
  - [`src/shim/loader/nv_encoder.cpp`](../../src/shim/loader/nv_encoder.cpp)（约 331 行与 721~735 行）
  - [`src/shim/ngx_bridge/ngx_d3d12_bridge.cpp`](../../src/shim/ngx_bridge/ngx_d3d12_bridge.cpp)
- **缺陷现状**：
  - 驱动 `621.24 WHQL` 下，4K 竖屏高度 `3840 > 2160` 超越常规 Level 5.1 上限，若未配置 High Tier 或超出硬件 Caps 会导致 `NvEncInitializeEncoder` 静默报错。
- **改造指令**：
  1. 在初始化前调用 `nvEncGetEncodeCaps` 查询 `NV_ENC_CAPS_HEIGHT_MAX`，确保输出高度合法。
  2. 当输出高度大于 2160 时，对 HEVC / AV1 编码参数显式开放 `tier = 1 (High)`。
  3. 捕获编码器与 NGX 初始化错误，在失败日志中打印包含分辨率尺寸与 API 原始返回值的明确诊断。

---

### 任务 6：全档位竖屏单元测试矩阵构建 (Unit Test Matrix)

- **涉及源码**：
  - [`tests/test_geometry.cpp`](../../tests/test_geometry.cpp) 或专项测试文件
- **改造指令**：
  1. 编写包含 `1080x1920` 与 `720x1280` 竖屏源的自动化单测，验证 `4k`、`2k`、`fhd`、`native` 以及预设来回切换下的输出分辨率。
  2. 断言所有输出尺寸严格满足：保持 9:16 纵横比、`target_w >= src_w && target_h >= src_h`、且向偶数对齐。
  3. 编写 `DisplayMatrix` 36 字节读写往返测试，断言旋转矩阵数据完全无损一致。

---

## 三、 外部高级工程师交付标准（准出硬性门禁）

高级工程师在将控制权交回负责人之前，必须确保以下交付物全部合格并落盘：

| 交付门禁项 | 验证命令 / 指标 | 必须达成结果 |
| :--- | :--- | :--- |
| **C++ 单元测试** | `ninja -C build rtx_tests && .\build\rtx_tests.exe` | **136+ 项单元测试 100% 全部通过 (PASS)** |
| **类型门禁** | `cmake -P cmake/AuditTypes.cmake` | **PASS (严格遵守标准库优先，0 纯包装类)** |
| **SLOC 门禁** | `cmake -P cmake/AuditSloc.cmake` | **PASS (无样板代码膨胀，注释完整保留)** |
| **SPDX 开源门禁** | `cmake -P cmake/AuditSpdx.cmake` | **PASS (全仓 100% 具备开源合规标识)** |
| **动态隔离门禁** | `cmake -P cmake/AuditImports.cmake` | **PASS (主程序 0 专有静态符号，0 隔离倒退)** |
| **工件物理落盘** | `docs/phase11/50_CineRTX Phase 11.3-P 施工汇报_竖屏视频全链路自适应与DisplayMatrix透传修复.md` | **100% 实体落盘，包含详细修改清单与本地测试日志** |

---

## 四、 协调负责人（Coordinator）后续接手流程

外部工程师提交 50 号施工汇报并交接后，协调负责人立即执行以下交接与实机验收闭环：

1. **环境与跨平台编译自愈**：在 Windows MSVC 环境下验证重编，确保编译警告清零、无 PDB 句柄冲突与增量链接污染。
2. **TRT 动态引擎物理重建**：在宿主机 RTX 5060 上运行引擎重构指令，固化支持 `2176x2176` 的新版 `rife_v4.25.engine`。
3. **真实竖屏实机转码穿透验证**：
   - 使用包含 `DisplayMatrix` 的物理竖屏视频（1080x1920），分别跑通 `4k`、`2k`、`fhd`、`native` 以及 `60-rife` 竖屏插帧。
   - 使用 `ffprobe` 验证输出视频流旋转角、分辨率单调性与播放方向。
   - 抓取转码控制台，验证 `D2H == 0 B`，显存零回读。
4. **终审封板委托书落盘**：起草并落盘 `docs/phase11/51_CineRTX Phase 11.3 止血与竖屏修复终审封板委托书.md`（`CINERTX-P11-DELEGATION-051`），正式提请首席专家解除「CONDITIONAL PASS」限制，完成 Phase 11.3 终审印章。
5. **Phase 11.4 启动与方案 B 历史重写准备**：衔接 [20号施工单](./20_CineRTX%20Phase%2011.4%20SuperPower%20施工单_RIFE高阶算子与SDK13.1分级实证.md)，待 Phase 11.4 最终封板时原子化执行 Git 远端历史重写。

---

> [!NOTE]
> 本工单基于首席领域专家的 [49号法医排查与修复方案](./49_CineRTX_Phase11_竖屏视频无法转码根因法医排查与修复方案.md) 制定，任务边界高度收敛于竖屏几何、TRT 动态尺寸、DisplayMatrix 与预览自适应，严禁引入无关功能变更。

> [!TIP]
> 在修改 `param_schema.cpp` 与 `hw_transcode.cpp` 的几何翻转逻辑时，建议复用 `std::clamp` 与纯算术位运算（如 `(dim + 1) & ~1`）保证偶数对齐，杜绝动态内存分配。

> [!IMPORTANT]
> 外部工程师在实施过程中必须严格贯彻 [DEC-009](DECISIONS.md)“现代 C++ 极限重构全栈工程军规”：禁止新增任何自定义包装结构体，纯数据传递一律优先使用 `std::pair`、`std::tuple` 或标准 `std::array`，守死 153 个命名类型的上限红线。

> [!WARNING]
> 严防合规倒退：外部工程师在修改 `rife_builder.cpp` 时，必须确保专有头文件与符号严格限定在 `rtx_ngx_bridge.dll` 内部，绝对禁止再次向 `CineRTX.exe` 泄露任何专有静态符号，守死 [DEC-010](DECISIONS.md)。

> [!CAUTION]
> 绝对禁止在未获用户明确批准的情况下执行 `git commit` 或 `git push`；所有产物一律在本地验证，严格防范版本库污染。
