# CineRTX Phase 11 竖屏视频无法正常转码根因法医排查与修复方案（Doc 49）

- **文档编号**：`CINERTX-P11-FORENSIC-049`
- **归档路径**：`docs/phase11/49_CineRTX_Phase11_竖屏视频无法转码根因法医排查与修复方案.md`
- **排查基线**：Phase 11.3-S 应急止血后最新代码树（含 Commit `10c29aa` + 48 号施工汇报 改动，2026-11-22，RTX 5060 Laptop / 驱动 `621.24 WHQL`）
- **排查角色**：多媒体系统工程与 GPU 异构计算首席领域专家（Chief Domain Expert）
- **排查日期**：2026-11-22

---

## 一、 执行摘要（Executive Summary）

针对用户反馈的**“目前项目竖屏视频无法正常转码，之前还是好的”**回归故障，本专家对比了 `v0.2.0` 正常基线与 Phase 11.2 / 11.3-S 最新源码树（覆盖 `param_schema.cpp`、`geometry.cpp`、`hw_transcode.cpp`、`hw_transcode_stages.cpp`、`rife_builder.cpp`、`rife_engine.cpp`、`ffmpeg_io.cpp`、`bridge_exports.cpp`、`p010_pack.cu`、`ngx_d3d12_bridge.cpp`、`nv_encoder.cpp`、`workstation_canvas.cpp` 与 `rtx_gui.cpp`），共锁定 **6 项交织的竖屏转码阻断与回归根因（`RC-PORTRAIT-01` ~ `RC-PORTRAIT-06`）**：

| 根因编号 | 严重度 | 故障模块与精确行号 | 核心机理与触发场景 | 为什么“之前好、现在坏” / 触发条件 |
| :---: | :---: | :--- | :--- | :--- |
| **RC-PORTRAIT-01** | **P0 阻断** | `src/core/param_schema.cpp:256-270`<br>`src/core/hw_transcode.cpp:218-235`<br>`src/core/hw_transcode_stages.cpp:64-66` | `hw_transcode.cpp` 仅对 `3840x2160` 触发 `calculateSmartGeometry` 竖屏翻转；且 `param_schema.cpp` 在 `resolution=="4k"` 时仅当 `target_width<=0 && target_height<=0` 才重置为 `3840x2160`。竖屏（如 `1080x1920`）选 `2K (2560x1440)`、`FHD (1920x1080)` 或切回 `4K` 残留旧尺寸时，直接触发 `target_h < info.height` 硬报错熔断。 | 只要切换过任意非 4K 预设或使用 2K/FHD/NLE 导出，即 100% 必现 `"remaster target geometry must not be smaller than the source"`。 |
| **RC-PORTRAIT-02** | **P0 阻断** | `src/shim/ngx_bridge/rife_builder.cpp:594-609`<br>`src/shim/ngx_bridge/rife_engine.cpp:529-546` | `rife_builder.cpp` 构建 TensorRT 动态 Profile 时将 `kMAX` 高度硬编码为横屏上限 `max_h = max(1088, opt_h)`。竖屏 `1080x1920` 输入高度 `1920 > 1088`，`setInputShape({1, 7, 1920, 1088})` 直接被 TensorRT 拒载失败！ | **2026-11-22 09:12 重建的 `rife_v4.25.engine`** 按默认横屏 `1920x1088` 构建，导致所有 `height > 1088` 的竖屏源开启 RIFE 即刻崩溃。 |
| **RC-PORTRAIT-03** | **P0 阻断** | `src/io/ffmpeg_io.cpp:498-502, 842-883` | Demuxer 与 Muxer 仅处理 HDR10 Side Data，**完全未读取也未透传 `AV_PKT_DATA_DISPLAYMATRIX`（±90° 旋转矩阵）**。手机拍摄的“横屏编码 + 旋转标志”竖屏视频既无法触发 `is_portrait_input`，导出后又彻底丢失旋转元数据导致横躺播放。 | 手机实拍竖屏（iPhone / Android MOV/MP4）100% 触发旋转丢失与方向误判。 |
| **RC-PORTRAIT-04** | **P1 失真/阻断** | `src/core/geometry.cpp:39-51`<br>`src/core/hw_transcode.cpp:230-234` | `Pillarbox` 模式（Mode B）虽算出 `scaled_width` 与 `offset_x`，但 `hw_transcode.cpp` **只取 `canvas_width (3840)` / `canvas_height (2160)` 并丢弃左右黑边偏移**，将 9:16 竖屏直接横向拉扁成 16:9；且 4K 竖屏源（`2160x3840`）选 `Pillarbox` 会因 `2160 < 3840` 直接报错。 | 竖屏选 `Pillarbox` 必然拉伸变形或对 4K 竖屏源硬报错。 |
| **RC-PORTRAIT-05** | **P1 新增回归** | `src/gui/workstation_canvas.cpp:570-592`<br>`src/shim/ngx_bridge/bridge_exports.cpp:512-514, 690-693`<br>`src/shim/ngx_bridge/p010_pack.cu:462-466` | ① Phase 11.3 新增的 `onNleExportSequence()` 漏调 `validateAndNormalizeProfile()`，导致 NLE 导出分辨率字段错乱；② Phase 11.2 新增的 3 槽零回读预览环固定为横屏 `960x540`，`p010_pack.cu` 以 `x0>>2, y0>>2` 写入，竖屏 `2160x3840` 下半屏 43.75% 被裁切、右侧 420 列为空白/脏显存。 | **Phase 11.2 / 11.3 全新引入的回归**，直接破坏竖屏监看与 NLE 导出。 |
| **RC-PORTRAIT-06** | **P1 驱动/编码边界** | `src/shim/ngx_bridge/ngx_d3d12_bridge.cpp:425-440`<br>`src/shim/loader/nv_encoder.cpp:331, 721-735` | 宿主机在 Phase 11.2 刚将驱动升级至 **`621.24 WHQL`**，同时 4K 竖屏输出高度 `3840 > 2160` 超出常规 16:9 4K 高度上限（AV1 Level 5.1 最大高度 2160，需抬升至 Level 5.2+/6.0；部分 NGX VSR 驱动版本对 `OutputSubrectSize.height > 2160` 存在参数校验收紧）。 | 若默认 4K 竖屏直出 `2160x3840` 在新驱动 `621.24` 下直接报 `NGX_D3D12_EVALUATE_VSR_EXT` 或 `NvEncInitializeEncoder` 失败，即为本根因命中。 |

---

## 二、 逐项法医级根因取证

### 2.1 `RC-PORTRAIT-01`：竖屏分辨率解析与 `SmartGeometry` 触发条件双重缺陷

在 `src/core/param_schema.cpp:256-270` 中：
```cpp
    if (profile.resolution == "4k") {
        if (profile.target_width <= 0 && profile.target_height <= 0) {
            profile.target_width = 3840;
            profile.target_height = 2160;
        }
    } else if (profile.resolution == "2k") {
        profile.target_width = 2560;
        profile.target_height = 1440;
    } else if (profile.resolution == "fhd") {
        profile.target_width = 1920;
        profile.target_height = 1080;
    } else if (profile.resolution == "native") {
        profile.target_width = 0;
        profile.target_height = 0;
    }
```
随后在 `src/core/hw_transcode.cpp:218-235` 中：
```cpp
    const bool is_portrait_input = (info_.width > 0 && info_.height > 0 && info_.width < info_.height);
    const bool is_default_landscape_4k = (target_width_ == 3840 && target_height_ == 2160);
    ...
    } else if (is_remaster && (target_width_ <= 0 || target_height_ <= 0 ||
                               (is_portrait_input && is_default_landscape_4k))) {
        auto geom = calculateSmartGeometry(info_.width, info_.height, geom_mode, target_width_, target_height_);
```
**三条直接死路**：
1. **2K / FHD 竖屏必崩**：竖屏 `1080x1920` 选 `2k`（`2560x1440`）或 `fhd`（`1920x1080`）时，`is_default_landscape_4k` 为 `false`，未翻转成 `1440x2560` / `1080x1920`，紧接着在 `hw_transcode_stages.cpp:64-66` 因 `target_h (1440/1080) < info.height (1920)` 直接报错 `"remaster target geometry must not be smaller than the source"`！
2. **切回 4K 残留死锁**：`guiProfile()` 持久保存了上一次转码的 `target_width`/`target_height`。若上次选了 `2k`（`2560x1440`）或 `fhd`（`1920x1080`），再切回 `4k` 时，`param_schema.cpp:257` 的 `if (target_width <= 0 && target_height <= 0)` 不成立，导致 `target_width/height` 永久残留 `2560x1440`，再跑竖屏 4K 同样直接报错！
3. **`native` 语义被劫持**：选 `resolution == "native"` 时 `target_width = 0, target_height = 0`，结果 `hw_transcode.cpp:228` 因 `target_width_ <= 0` 强行进入 `calculateSmartGeometry`，把 `native` 劫持放大为 `2160x3840` 4K！

---

### 2.2 `RC-PORTRAIT-02`：RIFE TensorRT Profile 最大高度写死 `1088`，竖屏 `1080x1920` 必败

在 `src/shim/ngx_bridge/rife_builder.cpp:594-609` 中：
```cpp
    const int opt_w = (width > 0) ? ((static_cast<int>(width) + kAlign - 1) / kAlign * kAlign) : 1920;
    const int opt_h = (height > 0) ? ((static_cast<int>(height) + kAlign - 1) / kAlign * kAlign) : 1088;
    const int max_w = (std::max)(1920, opt_w);
    const int max_h = (std::max)(1088, opt_h);
```
当 `build/models/rife_v4.25.engine`（修改于 `2026-11-22 09:12:49Z`）按默认横屏构建时，其 TensorRT `kMAX` 维度为 `{1, 7, 1088, 1920}`。
对于任何 `1080x1920` 竖屏视频，`initRemasterAndFgStage()` 传入 `in_w = 1080, in_h = 1920`，`rife_engine.cpp:543-546` 的 `fits(128, 1088, 1920)` 为 `false`，回退后仍传入 `padded_height_ = 1920 > 1088`，`setInputShape({1, 7, 1920, 1088})` 直接返回失败！

---

### 2.3 `RC-PORTRAIT-03`：容器旋转矩阵 `AV_PKT_DATA_DISPLAYMATRIX` 零透传

在 `src/io/ffmpeg_io.cpp:498-502` 与 `src/io/ffmpeg_io.cpp:842-883` 中：
- `MediaInfo` 结构体未记录输入视频流的 `display_matrix`（3×3 `int32_t[9]` 旋转矩阵）；
- 手机拍摄的绝大多数竖屏 MP4/MOV 实际编码宽高为 `1920x1080` + `-90°` 旋转侧数据；
- `FfmpegMuxer::open()` 仅写入 HDR10 Side Data，未将原始 `AV_PKT_DATA_DISPLAYMATRIX` 拷贝至输出流，导致输出竖屏视频横躺 90°。

---

### 2.4 `RC-PORTRAIT-04`：`Pillarbox` 仅传画布尺寸导致暴力拉扁与 4K 竖屏报错

`src/core/geometry.cpp:39-51` 正确计算了 `scaled_width`、`offset_x` 与左右黑边宽度，但 `src/core/hw_transcode.cpp:230-234` 仅提取 `canvas_width`/`canvas_height` 传给 `RtxNgxBridgeConfig`，而 `NgxD3D12Bridge` 始终以 `{0, 0} -> {output_width_, output_height_}` 全画布评估 VSR，导致：
- `1080x1920` 选 `Pillarbox` $\rightarrow$ VSR 强制从 `1080x1920` 拉伸为 `3840x2160`（9:16 压扁为 16:9）；
- `2160x3840` 选 `Pillarbox` $\rightarrow$ `output_height (2160) < input_height (3840)` 直接触发 NGX 与 Pipeline 报错。

---

### 2.5 `RC-PORTRAIT-05`：Phase 11.2 / 11.3 新增回归（NLE 漏校验 + 3 槽预览环横屏硬编码）

1. **NLE 导出漏调校验（`src/gui/workstation_canvas.cpp:570-592`）**：`onNleExportSequence()` 未调用 `validateAndNormalizeProfile(profile, false)`，直接将未归一化的 `profile` 送入 `profileToHwOptions()`。
2. **零回读 3 槽预览环竖屏截断（`bridge_exports.cpp:512-514, 690-693` + `p010_pack.cu:462-466`）**：预览环尺寸硬编码为 `960x540`，核函数固定右移 2 位（1/4 采样），竖屏 `2160x3840` 的 `py = y0 >> 2` 最大达 `959`，超出 `preview_height = 540` 的下半屏整块丢弃。

---

### 2.6 `RC-PORTRAIT-06`：新驱动 `621.24 WHQL` 下 `2160x3840` VSR / NVENC 高度边界排查

若用户使用默认 4K AutoDirect + NVOFA 跑原生 `1080x1920 -> 2160x3840` 仍然直接报错，**首要在宿主机执行以下一条 CLI 诊断命令以瞬间定位精确报错节点**：
```powershell
.\build\CineRTX.exe --input <竖屏视频路径> --frames 30 --json
```
- 若返回 `rtx_ngx_bridge_create failed` / `NGX_D3D12_EVALUATE_VSR_EXT failed`：说明新驱动 `621.24` 下 NGX VSR 对 `output_height = 3840` 存在上限约束，需在 `NgxD3D12Bridge` 探测或按比例限幅至最大合法高度；
- 若返回 `rtx_shim_encoder_create failed` / `NvEncInitializeEncoder`：说明 NVENC AV1/HEVC 在 `2160x3840` 下需显式设置 `tier = 1 (High)` 或校验 `NV_ENC_CAPS_HEIGHT_MAX`；
- 若返回 `remaster target geometry must not be smaller than the source`：直接确认命中 **`RC-PORTRAIT-01`**；
- 若返回 `rtx_ngx_bridge_init_rife failed`：直接确认命中 **`RC-PORTRAIT-02`**。

---

## 三、 切实可行的全链路修复方案（按优先级落地）

### 修复 1（对应 `RC-PORTRAIT-01` & `RC-PORTRAIT-04`）：重构竖屏自适应几何契约（`param_schema.cpp` + `geometry.cpp` + `hw_transcode.cpp`）

1. **`src/core/param_schema.cpp:256-270`**：无条件按预设覆写 `target_width`/`target_height`（消除切回 `4k` 的旧尺寸残留）：
   ```cpp
   if (profile.resolution == "4k") {
       profile.target_width = 3840;
       profile.target_height = 2160;
   } else if (profile.resolution == "2k") {
       profile.target_width = 2560;
       profile.target_height = 1440;
   } else if (profile.resolution == "fhd") {
       profile.target_width = 1920;
       profile.target_height = 1080;
   } else if (profile.resolution == "native") {
       profile.target_width = 0;
       profile.target_height = 0;
   }
   ```
2. **`src/core/geometry.cpp` + `src/core/hw_transcode.cpp:218-235`**：
   - **`native` 语义保护**：当 `target_width_ == 0 && target_height_ == 0` 时，直接锁定 `target_width_ = info_.width; target_height_ = info_.height;`（原分辨率 TrueHDR/FRUC，绝不强行放大 4K）；
   - **全档位竖屏自动翻转（AutoDirect）**：当 `is_portrait_input && geom_mode == GeometryTargetMode::AutoDirect && target_width_ > target_height_` 时，**统一对 `4K (3840x2160 -> 2160x3840)`、`2K (2560x1440 -> 1440x2560)`、`FHD (1920x1080 -> 1080x1920)` 全部按源宽高比等比计算竖屏目标尺寸**（并对齐至 2 的倍数，且保底 `target_w >= info_.width && target_h >= info_.height` 防止反向降采样触碰 VSR 下限）；
   - **`Pillarbox` 真黑边接入**：在 `RtxNgxBridgeConfig` 或 `NgxD3D12Bridge::process()` 中传递 `OutputSubrectBase = {geom.offset_x, 0}` 与 `OutputSubrectSize = {geom.scaled_width, geom.scaled_height}`（其余画布清零为黑色），并对 `info_.height > canvas_height` 的 4K 竖屏源在 `Pillarbox` 模式下明确禁用 VSR 或返回清晰提示，避免拉伸与静默崩溃。

### 修复 2（对应 `RC-PORTRAIT-02`）：扩展 RIFE TensorRT 动态 Profile 高度上限（`rife_builder.cpp`）

- 修改 `src/shim/ngx_bridge/rife_builder.cpp:598-599`：
  ```cpp
  // 对称支持横屏 (1920x1080) 与竖屏 (1080x1920) 乃至 2K 边长（64 像素对齐 = 1920 / 2176）
  const int max_w = (std::max)(2176, opt_w);
  const int max_h = (std::max)(2176, opt_h);
  ```
- 重建 `models/rife_v4.25.engine`（`CineRTX.exe --build-rife-engine ...`），即可原生支持 `1080x1920`、`1440x2160` 等全部常见竖屏视频的 RIFE 插帧。

### 修复 3（对应 `RC-PORTRAIT-03`）：透传 `AV_PKT_DATA_DISPLAYMATRIX` 旋转侧数据（`include/rtx/io.h` + `src/io/ffmpeg_io.cpp`）

- 在 `MediaInfo` 增加 `std::array<int32_t, 9> display_matrix{}; bool has_display_matrix = false;`；
- 在 `FfmpegDemuxer::open()` 中扫描 `stream->codecpar->coded_side_data` 查找 `AV_PKT_DATA_DISPLAYMATRIX` 并保存；
- 在 `FfmpegMuxer::open()` 中若 `info.has_display_matrix` 为真，通过 `av_packet_side_data_add(..., AV_PKT_DATA_DISPLAYMATRIX, ...)` 原样写回输出视频流，彻底解决手机竖屏视频导出后横躺问题。

### 修复 4（对应 `RC-PORTRAIT-05`）：补齐 NLE 校验与竖屏预览自适应缩放

1. 在 `src/gui/workstation_canvas.cpp:588` 的 `onNleExportSequence()` 调用 `profileToHwOptions(profile)` 之前补上 `(void)rtx::validateAndNormalizeProfile(profile, false);`；
2. 在 `p010_pack.cu:462-466` 中，将写死的 `x0 >> 2, y0 >> 2` 改为按 `scale = max(ceil(width / preview_width), ceil(height / preview_height))`（或动态保持纵横比映射到 `960x540` 居中区域），确保 `2160x3840` 竖屏在零回读预览环中完整显示且不裁切下半屏。

### 修复 5（对应 `RC-PORTRAIT-06`）：NVENC 能力查询与 `2160x3840` 明确诊断

- 在 `src/shim/loader/nv_encoder.cpp` 初始化前调用 `nvEncGetEncodeCaps` 探测 `NV_ENC_CAPS_WIDTH_MAX` 与 `NV_ENC_CAPS_HEIGHT_MAX`；对 `height > 2160` 的 AV1/HEVC 4K 竖屏自动放开 `tier = 1 (High)` 并在失败日志中显式打印 `encodeWidth x encodeHeight` 与 `nvEncGetLastErrorString` 原文。

---

## 四、 回归验收门禁 Checklist

- [ ] **单测 1（全档位竖屏几何矩阵）**：新增单元测试覆盖 `1080x1920` 与 `720x1280` 在 `4k`、`2k`、`fhd`、`native` 及 `2k -> 4k` 切回场景下的输出尺寸，断言全部 `target_w >= src_w && target_h >= src_h` 且保持 9:16 比例；
- [ ] **单测 2（DisplayMatrix 透传）**：验证含 90° `AV_PKT_DATA_DISPLAYMATRIX` 的输入文件经 `FfmpegDemuxer` $\rightarrow$ `FfmpegMuxer` 后输出流保留完全一致的 36 字节旋转矩阵；
- [ ] **真机 1（竖屏 NVOFA + 4K/2K/FHD/Native 转码）**：在 RTX 5060 宿主机上用真实 `1080x1920` 竖屏视频分别跑通 `4k (2160x3840)`、`2k (1440x2560)`、`fhd (1080x1920)` 与 `native`，均 ExitCode `0` 且 `ffprobe` 校验分辨率、旋转角与播放方向正确；
- [ ] **真机 2（竖屏 RIFE 插帧）**：重建 `rife_v4.25.engine`（`max_w = max_h = 2176`）后实测 `1080x1920` 竖屏 `60-rife` 转码 100% 通过且无黑边。
