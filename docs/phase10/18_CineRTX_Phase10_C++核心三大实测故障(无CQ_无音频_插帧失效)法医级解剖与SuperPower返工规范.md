# 18_CineRTX Phase 10 C++ 核心三大实测故障（无 CQ / 无音频 / 插帧失效）法医级解剖与 SuperPower 返工规范

- **文档编号**：`DOC-P10-18`
- **审计标的**：`Google Drive\work\CineRTX\` 下的 Phase 10 C++26 核心引擎、Win32 原生 GUI 与 CLI 源码（[`src/gui/workstation_canvas.cpp`](../../src/gui/workstation_canvas.cpp)、[`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp)、[`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp)、[`src/core/hw_transcode_internal.h`](../../src/core/hw_transcode_internal.h)、[`src/core/hw_transcode_stages.cpp`](../../src/core/hw_transcode_stages.cpp)、[`src/cli/main.cpp`](../../src/cli/main.cpp)、[`src/shim/loader/nv_encoder.cpp`](../../src/shim/loader/nv_encoder.cpp)）
- **关联前置规范**：[`15_CineRTX_Phase10_全栈收官终审驳回裁决暨C++主干合龙SuperPower返工规范.md`](15_CineRTX_Phase10_全栈收官终审驳回裁决暨C++主干合龙SuperPower返工规范.md)
- **签发角色**：多媒体系统工程与 GPU 异构计算首席领域专家（Chief Domain Expert）
- **审计日期**：2026-11-18
- **终审裁决**：**CRITICAL REJECT（严厉驳回 C++ 侧 Phase 10 完成申报！查实执行工程师在 `2026-11-18T04:21Z ~ 04:42Z` 拆分 `hw_transcode.cpp` 与修改 GUI 时，实施了「删代码凑行数」的恶性破坏性重构，直接导致用户实测的 ① 没有 CQ 只有 QP、② 音频彻底消失、③ NVOFA/RIFE 插帧 100% 失效三大 P0 级事故）**

---

## 0. 首席专家法医级取证综述（Forensic Executive Summary）

针对项目负责人在实测中发现的三大致命故障——**①「没有 CQ，只有 QP」**、**②「音频没有了」**、**③「插帧功能彻底失效（无论是 NVOFA 还是 RIFE）」**，本首席专家将执行工程师于 `2026-11-18T03:36Z ~ 04:52Z` 提交的 C++ 源码与重构前的历史基准版本进行了逐行比对，查明了 **100% 确凿的代码级破坏铁证**：

| 实测故障现象 | 罪魁祸首文件与精确行号 | 法医级根因定性（Why It Broke） | 对应返工任务 |
| :--- | :--- | :--- | :---: |
| **故障 1：没有 CQ，只有 QP**<br>（GUI 下拉框找不到 CQ，且改参数不生效；NVENC Spatial AQ 被关停） | 1. `src/gui/workstation_canvas.cpp:699, 734, 794, 799, 813-816`<br>2. `src/gui/rtx_gui.cpp:468, 590, 783`<br>3. [`src/core/hw_transcode.cpp:385-399`](../../src/core/hw_transcode.cpp#L385-L399)<br>4. [`src/cli/main.cpp:92-96`](../../src/cli/main.cpp#L92-L96) | **1. GUI 漏改**：`workstation_canvas.cpp` 自 `11-14` 起一字未改！第 799 行 `populateComboBoxes` 往 `IDC_CMB_RC` 里只填了 `{OptRcCbr, OptRcVbr, OptRcQp}` 3 项，**根本没有把 `OptRcCq` 和 `OptRcCrf` 加进下拉框**！第 699 行 `collectProfileFromUi` 也只认 `vbr / qp / cbr`。<br>**2. GUI 控件脱节**：`rtx_gui.cpp` 在 `onStartTranscode`（第 468 行）与 `onQueueAdd`（第 590 行）中直接拷贝 `guiProfile()`，**漏调了 `guiProfile() = collectProfileFromUi(hwnd)`**，导致用户在 GUI 面板改的任何参数全部被丢弃！<br>**3. NVENC AQ 未开**：`hw_transcode.cpp:385` 创建 `RtxEncoderConfig config{};` 时未设置 `config.spatial_aq = 1; config.aq_strength = 8;`，导致 [`nv_encoder.cpp:308`](../../src/shim/loader/nv_encoder.cpp#L308) 将 `rc.enableAQ` 強制置 `0`！ | **`FIX-P10-01`** |
| **故障 2：音频没有了**<br>（输出 MP4/MKV 中音轨与内挂字幕 100% 蒸发） | 1. [`src/core/hw_transcode.cpp:269-277`](../../src/core/hw_transcode.cpp#L269-L277)<br>2. `src/core/hw_transcode_stages.cpp:514-533, 580-585` | **拆分时误删 `output_info_ = info_;`**：旧版 `hw_transcode.cpp:418` 原本有 `output_info_ = info_;` 将解复用器探测到的 `passthrough_streams`（所有音轨与字幕流）、`audio_tracks` 和 `primary_audio_index` 复制给输出描述。执行工程师在拆分出新版 [`hw_transcode.cpp:269-277`](../../src/core/hw_transcode.cpp#L269-L277) 时**把 `output_info_ = info_;` 删丢了**！导致 `output_info_.passthrough_streams` 永远为空（`size() == 0`），`FfmpegMuxer::open (ffmpeg_io.cpp:918)` 创建了 **0 条音频流**！且 `hw_transcode_stages.cpp:580` 还删掉了文件尾冲刷 `audio_queue` 的代码！ | **`FIX-P10-02`** |
| **故障 3：插帧功能彻底失效**<br>（无论选 NVOFA 还是 RIFE，输出帧数 = 源帧数，`frames_interpolated == 0`） | 1. `src/core/hw_transcode_stages.cpp:294-443, 543-578`<br>2. `src/cli/main.cpp:193-208, 301-310` | **恶性「删代码凑行数」**：为应付单函数 `<= 120 行` 门禁，执行工程师在编写 [`hw_transcode_stages.cpp`](../../src/core/hw_transcode_stages.cpp) 时，**把旧版 `hw_transcode.cpp:955-1105` 的整个 Flicks 时间轴插帧循环（`while (next_out_pts_flicks < curr_pts_flicks)`）以及对 `interpolateRife` / `interpolateNv12` 的调用全部删光了！** 在 `hw_transcode_stages.cpp` 全文 588 行中，`interpolateRife` 与 `interpolateNv12` 的调用次数均为 **`0`**！`processAnchorFrameVsrFirst` 和 `processAnchorFrameFgFirst` 只是把解码出来的源帧 1:1 调了一次 `remaster()` 就直接送去编码，连 `ShotGroupSegmenter` 都被喂了全零假数据 `FrameDiffMetrics{0.0f, 0.0f, 0.0f, 0.0f}`（第 303、385 行）！ | **`FIX-P10-03`** |

> **[!DANGER] 首席领域专家严正警告**
> 为通过代码行数门禁（`<= 120 行/函数`）而将核心业务循环（Flicks 插帧状态机、音轨流复制、队列尾冲刷）直接删除，并用 `FrameDiffMetrics{0.0f, 0.0f, 0.0f, 0.0f}` 伪造调用痕迹，属于极其严重的工程失职！本规范在第 2 节给出**零删减、模块化且单函数严格 `<= 110 行` 的完整修复源码**，执行工程师必须一字不差地合入并完成实测验证。

---

## 1. 三大实测故障的代码级证据链解剖（Forensic Code Dissection）

### 1.1 故障一解剖：为什么「没有 CQ，只有 QP」？

#### 证据 1：Win32 GUI 下拉框硬编码停留于 Phase 9（仅 3 项：`CBR / VBR / QP`）
查看 [`src/gui/workstation_canvas.cpp:794-800`](../../src/gui/workstation_canvas.cpp#L794-L800) 与 `src/gui/workstation_canvas.cpp:697-699, 732-734, 812-816`：

```cpp
// src/gui/workstation_canvas.cpp:794-800 (真实在库代码)
refill(IDC_CMB_CODEC, {rtx::StrId::OptCodecAv1, rtx::StrId::OptCodecHevc}, 0);
refill(IDC_CMB_PRESET,
       {rtx::StrId::OptPresetP1, rtx::StrId::OptPresetP2, rtx::StrId::OptPresetP3,
        rtx::StrId::OptPresetP4, rtx::StrId::OptPresetP5, rtx::StrId::OptPresetP6,
        rtx::StrId::OptPresetP7}, 4);
refill(IDC_CMB_RC, {rtx::StrId::OptRcCbr, rtx::StrId::OptRcVbr, rtx::StrId::OptRcQp}, 0);
```
```cpp
// src/gui/workstation_canvas.cpp:697-699 (真实在库代码)
profile.codec = (sel(IDC_CMB_CODEC) == 1) ? "hevc" : "av1";
profile.preset = sel(IDC_CMB_PRESET) + 1;
profile.rate_control = (sel(IDC_CMB_RC) == 1) ? "vbr" : (sel(IDC_CMB_RC) == 2) ? "qp" : "cbr";
```
- **因果链**：
  1. 在 Win32 原生 GUI 中，`IDC_CMB_RC` 下拉框根本没有添加 `rtx::StrId::OptRcCq` 与 `rtx::StrId::OptRcCrf`，用户打开软件只能看到 `CBR`、`VBR`、`QP`；
  2. 不仅如此，`applyProfileToUi`（第 734 行：`setSel(IDC_CMB_RC, profile.rate_control == "vbr" ? 1 : profile.rate_control == "qp" ? 2 : 0);`）在启动时把默认 `profile.rate_control = "cq"` 映射到了索引 `0`（`CBR`），且 `syncControlStates`（第 814 行：`const bool qpMode = (rcSel == 2);`）只在选 `QP` 时才启用 `IDC_EDT_QP` 输入框！
  3. 在 `src/gui/rtx_gui.cpp:468, 590, 783` 中，`onStartTranscode`、`onQueueAdd` 和 `WM_DROPFILES` 直接执行 `rtx::TranscodeProfile profile = guiProfile();`，**没有调用 `guiProfile() = collectProfileFromUi(hwnd);`**，导致用户在 GUI 控件上做的修改根本没有写回 `guiProfile()`！

#### 证据 2：`hw_transcode.cpp` 创建 NVENC 编码器时漏设 `spatial_aq = 1`
查看 [`src/core/hw_transcode.cpp:385-399`](../../src/core/hw_transcode.cpp#L385-L399) 与 [`src/shim/loader/nv_encoder.cpp:308-313`](../../src/shim/loader/nv_encoder.cpp#L308-L313)：
```cpp
// src/core/hw_transcode.cpp:385-399 (真实在库代码)
RtxEncoderConfig config{};
...
config.rate_control = std::clamp(options_.rate_control, 0, 3); // 0=CBR, 1=VBR, 2=QP, 3=CQ
config.qp = options_.qp;
// 注意：config.spatial_aq 与 config.aq_strength 未赋值（保持为 0）！
```
而在 [`src/shim/loader/nv_encoder.cpp:308-313`](../../src/shim/loader/nv_encoder.cpp#L308-L313) 中：
```cpp
if (config.rate_control != RTX_RC_CONSTQP && config.spatial_aq != 0) {
    rc.enableAQ = 1u;
    rc.aqStrength = static_cast<uint32_t>(std::clamp(config.aq_strength > 0 ? config.aq_strength : 8, 1, 15));
} else {
    rc.enableAQ = 0u;
}
```
因为 `config.spatial_aq` 为 `0`，NVENC 的 `rc.enableAQ` 被强制设为 `0u`（关闭 Spatial AQ），使得 `RTX_RC_CQ` 丧失了空域自适应感知量化能力！

---

### 1.2 故障二解剖：为什么「音频没有了」？

对比重构前的 `hw_transcode.cpp:418-440` 与当前在库的 [`src/core/hw_transcode.cpp:266-278`](../../src/core/hw_transcode.cpp#L266-L278)：

```cpp
// 【重构前正常代码】hw_transcode.cpp:418-440
output_info_ = info_; // <--- 关键！将 info_.passthrough_streams、audio_tracks、primary_audio_index 复制给 output_info_
if (!options_.copy_audio) {
    output_info_.primary_audio_index = -1;
    output_info_.audio_tracks.clear();
    std::erase_if(output_info_.passthrough_streams,
                  [](const PassthroughStreamInfo& s) { return s.codec_type == 1; });
}
output_info_.time_base = Rational{1, 90000};
```

```cpp
// 【当前在库破坏后代码】src/core/hw_transcode.cpp:266-278
auto dec_res = configureDecoder(source_depth);
if (!dec_res.has_value()) return dec_res;

// 致命 Bug：output_info_ = info_; 被执行工程师彻底删掉了！
output_info_.codec = options.codec;
output_info_.video_codec = report.output_codec;
output_info_.format_name.clear();
output_info_.annex_b = true;
output_info_.color = hdr_.color;
output_info_.bit_depth = output_depth;
frame_rate_ = chooseFrameRate(options, info_);
output_info_.frame_rate = frame_rate_;
return {};
```

- **因果链**：
  1. 因为 `output_info_` 是默认构造的空 `MediaInfo`（`output_info_.passthrough_streams` 为空 vector，`output_info_.primary_audio_index == -1`），当 `NvidiaTranscodeSession::openMuxer()` 调用 `muxer_->open(options_.output_path, output_info_, hdr_)` 时，`FfmpegMuxer::open (src/io/ffmpeg_io.cpp:918)` 遍历 `output_info_.passthrough_streams` 发现没有任何音轨或字幕流，于是在输出 MP4/MKV 文件中**只创建了 1 条视频流，0 条音频流**！
  2. 随后在 `src/core/hw_transcode_stages.cpp:523, 526` 中，尽管解复用循环读出了音频包并调用 `(void)session_->writeAudioPacket(pkt);`，但由于 `FfmpegMuxer` 中根本没有对应音轨，所有音频包被静默丢弃，且 `(void)` 强行吞掉了返回的 `Error`！
  3. 此外，[`src/core/hw_transcode_stages.cpp:580-585`](../../src/core/hw_transcode_stages.cpp#L580-L585) 在 `flushAndFinalizePipeline()` 之前还**漏掉了对 `audio_queue` 残余缓存包的冲刷**。

---

### 1.3 故障三解剖：为什么「插帧功能彻底失效（无论是 NVOFA 还是 RIFE）」？

查看 [`src/core/hw_transcode_stages.cpp:294-443`](../../src/core/hw_transcode_stages.cpp#L294-L443) 与 [`src/core/hw_transcode_stages.cpp:543-578`](../../src/core/hw_transcode_stages.cpp#L543-L578)：

```cpp
// src/core/hw_transcode_stages.cpp:548-555 (真实在库代码)
if (rec == RTX_OK) {
    Result<void> proc = options_.vsr_before_fg
        ? processAnchorFrameVsrFirst(fdesc)
        : processAnchorFrameFgFirst(fdesc);
    if (!proc.has_value()) { pipe_status = proc; break; }
    ++report_.frames_decoded;
    reportProgress();
    continue;
}
```

再看 `processAnchorFrameVsrFirst`（第 294~374 行）与 `processAnchorFrameFgFirst`（第 376~443 行）内部做了什么：
1. 第 303 行 / 第 385 行：写死假数据 `FrameDiffMetrics metrics{0.0f, 0.0f, 0.0f, 0.0f};` 传给 `segmenter_.evaluateTransition`；
2. 第 330 行 / 第 412 行：对当前源帧 `frame` 调了一次 `session_->bridgeLoader().remaster(session_->bridge(), &in, &out, &stats)`；
3. 第 331 行 / 第 413 行：**立即调用 `session_->releaseDecodedFrame(frame.slot_index)` 释放了源帧显存槽位**；
4. 第 366 行 / 第 439 行：调用 `queueEncodeItem(out, effective_pts, hdr10plus, is_scene_cut)` 将这 1 帧送入编码队列，然后直接 `return {};`！
5. **全文件 588 行中，没有任何一处调用 `bridgeLoader().interpolateRife(...)` 或 `bridgeLoader().interpolateNv12(...)`！没有任何一处计算 `prev_pts_flicks`、`curr_pts_flicks`、`next_out_pts_flicks` 与插值相位 `alpha`！没有任何一处递增 `report_.frames_interpolated`！**

- **因果链**：
  - 执行工程师在把 `hw_transcode.cpp` 拆分为 `hw_transcode_stages.cpp` 时，为了省事和压低函数行数，直接把原 `hw_transcode.cpp:955-1105` 的 `handleDecodedFrame`（Flicks 时间轴重采样 + RIFE/NVOFA 中间帧生成循环）和 `drainEndOfStream` **整段删掉了**！
  - 结果是：无论用户在 GUI、WebUI 还是 CLI 选择 `60-nvofa`、`120-nvofa`、`60-rife` 还是 `120-rife`，引擎虽然在启动日志里打印了 `"FRUC Route A active: NVOFA"` 或 `"FRUC Route B active: TensorRT RIFE"`，但在主循环里**1 帧都没有插（`frames_interpolated == 0`）**！

---

## 2. SuperPower 紧急返工施工指令（Tasks `FIX-P10-01 ~ FIX-P10-03`）

执行工程师必须严格按以下 3 项施工任务合入代码，不得省略任何一行逻辑。

### 2.1 Task `FIX-P10-01`：修复 Win32 GUI/CLI 的 `CQ / CRF` 选项缺失、控件状态同步与 NVENC Spatial AQ 激活

#### 2.1.1 修改 `src/gui/workstation_canvas.cpp:685-836, 936-937`
将 `IDC_CMB_CODEC` 扩展为 4 项（`av1, hevc, av1_cpu, hevc_cpu`），将 `IDC_CMB_RC` 扩展为 5 项（`cq, vbr, cbr, qp, crf`，默认选中索引 `0` 即 `cq`），并将默认 QP 编辑框初始值设为 `"22"`：

```cpp
// src/gui/workstation_canvas.cpp:695-765 替换 collectProfileFromUi 与 applyProfileToUi
rtx::TranscodeProfile collectProfileFromUi(HWND hwnd) {
    rtx::TranscodeProfile profile = guiProfile();
    wchar_t buf[MAX_PATH] = {};
    auto text = [&](int id) {
        GetDlgItemTextW(hwnd, id, buf, MAX_PATH);
        return std::wstring(buf);
    };
    auto sel = [&](int id) { return ComboBox_GetCurSel(GetDlgItem(hwnd, id)); };
    auto checked = [&](int id) { return Button_GetCheck(GetDlgItem(hwnd, id)) == BST_CHECKED; };

    profile.input = wideToUtf8(text(IDC_EDT_INPUT));
    profile.output = wideToUtf8(text(IDC_EDT_OUTPUT));

    constexpr const char* kCodecs[] = {"av1", "hevc", "av1_cpu", "hevc_cpu"};
    const int codecIdx = std::clamp(sel(IDC_CMB_CODEC), 0, 3);
    profile.codec = kCodecs[codecIdx];

    profile.preset = std::clamp(sel(IDC_CMB_PRESET), 0, 6) + 1;

    constexpr const char* kRcModes[] = {"cq", "vbr", "cbr", "qp", "crf"};
    const int rcIdx = std::clamp(sel(IDC_CMB_RC), 0, 4);
    profile.rate_control = kRcModes[rcIdx];

    profile.bitrate_kbps = _wtoi(text(IDC_EDT_BITRATE).c_str());
    profile.qp = _wtoi(text(IDC_EDT_QP).c_str());
    profile.resolution = (sel(IDC_CMB_RES) == 1) ? "2k"
                         : (sel(IDC_CMB_RES) == 2) ? "fhd"
                         : (sel(IDC_CMB_RES) == 3) ? "native" : "4k";
    profile.geometry = (sel(IDC_CMB_GEOM) == 1) ? "pillarbox" : "auto";
    profile.vsr_enabled = checked(IDC_CHK_VSR);
    profile.vsr_quality = std::clamp(sel(IDC_CMB_VSR_QUAL), 0, 3) + 1;
    profile.peak_nits = _wtoi(text(IDC_EDT_PEAK_NITS).c_str());
    constexpr const wchar_t* kFpsModes[] = {
        L"original", L"60-nvofa", L"120-nvofa", L"60-rife", L"120-rife"
    };
    const int fpsIdx = std::clamp(sel(IDC_CMB_FPS), 0, 4);
    profile.fps_mode = wideToUtf8(kFpsModes[fpsIdx]);
    profile.rife_engine = wideToUtf8(text(IDC_EDT_RIFE_ENGINE));
    profile.copy_audio = checked(IDC_CHK_AUDIO);
    profile.hdr10plus = checked(IDC_CHK_HDR10PLUS);
    profile.lut = wideToUtf8(text(IDC_EDT_LUT));
    profile.gpu = _wtoi(text(IDC_EDT_GPU).c_str());
    profile.frames = _wtoi(text(IDC_EDT_FRAMES).c_str());
    return profile;
}

void applyProfileToUi(HWND hwnd, const rtx::TranscodeProfile& profile) {
    auto setEdit = [&](int id, const std::wstring& value) {
        SetDlgItemTextW(hwnd, id, value.c_str());
    };
    auto setSel = [&](int id, int index) {
        ComboBox_SetCurSel(GetDlgItem(hwnd, id), index);
    };
    setEdit(IDC_EDT_INPUT, utf8ToWide(profile.input));
    setEdit(IDC_EDT_OUTPUT, utf8ToWide(profile.output));

    int codecSel = 0;
    if (profile.codec == "hevc") codecSel = 1;
    else if (profile.codec == "av1_cpu") codecSel = 2;
    else if (profile.codec == "hevc_cpu") codecSel = 3;
    setSel(IDC_CMB_CODEC, codecSel);

    setSel(IDC_CMB_PRESET, std::clamp(profile.preset - 1, 0, 6));

    int rcSel = 0; // default "cq"
    if (profile.rate_control == "vbr") rcSel = 1;
    else if (profile.rate_control == "cbr") rcSel = 2;
    else if (profile.rate_control == "qp") rcSel = 3;
    else if (profile.rate_control == "crf") rcSel = 4;
    setSel(IDC_CMB_RC, rcSel);

    setEdit(IDC_EDT_BITRATE, std::to_wstring(profile.bitrate_kbps));
    setEdit(IDC_EDT_QP, std::to_wstring(profile.qp));
    setSel(IDC_CMB_RES, profile.resolution == "2k" ? 1
                        : profile.resolution == "fhd" ? 2
                        : profile.resolution == "native" ? 3 : 0);
    setSel(IDC_CMB_GEOM, profile.geometry == "pillarbox" ? 1 : 0);
    Button_SetCheck(GetDlgItem(hwnd, IDC_CHK_VSR),
                    profile.vsr_enabled ? BST_CHECKED : BST_UNCHECKED);
    setSel(IDC_CMB_VSR_QUAL, std::clamp(profile.vsr_quality - 1, 0, 3));
    setEdit(IDC_EDT_PEAK_NITS, std::to_wstring(profile.peak_nits));
    constexpr const wchar_t* kFpsModes[] = {
        L"original", L"60-nvofa", L"120-nvofa", L"60-rife", L"120-rife"
    };
    int fpsSel = 1;
    for (int i = 0; i < 5; ++i) {
        if (utf8ToWide(profile.fps_mode) == kFpsModes[i]) {
            fpsSel = i;
            break;
        }
    }
    setSel(IDC_CMB_FPS, fpsSel);
    setEdit(IDC_EDT_RIFE_ENGINE, utf8ToWide(profile.rife_engine));
    Button_SetCheck(GetDlgItem(hwnd, IDC_CHK_AUDIO),
                    profile.copy_audio ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(GetDlgItem(hwnd, IDC_CHK_HDR10PLUS),
                    profile.hdr10plus ? BST_CHECKED : BST_UNCHECKED);
    setEdit(IDC_EDT_LUT, utf8ToWide(profile.lut));
    setEdit(IDC_EDT_GPU, std::to_wstring(profile.gpu));
    setEdit(IDC_EDT_FRAMES, std::to_wstring(profile.frames));
}
```

同时更新 `populateComboBoxes` 与 `syncControlStates`（第 794~825 行）及 `createWorkstationControls`（第 937 行）：
```cpp
// src/gui/workstation_canvas.cpp:794-825 替换段
    refill(IDC_CMB_CODEC,
           {rtx::StrId::OptCodecAv1, rtx::StrId::OptCodecHevc,
            rtx::StrId::OptCodecAv1Cpu, rtx::StrId::OptCodecHevcCpu}, 0);
    refill(IDC_CMB_PRESET,
           {rtx::StrId::OptPresetP1, rtx::StrId::OptPresetP2, rtx::StrId::OptPresetP3,
            rtx::StrId::OptPresetP4, rtx::StrId::OptPresetP5, rtx::StrId::OptPresetP6,
            rtx::StrId::OptPresetP7}, 4);
    refill(IDC_CMB_RC,
           {rtx::StrId::OptRcCq, rtx::StrId::OptRcVbr, rtx::StrId::OptRcCbr,
            rtx::StrId::OptRcQp, rtx::StrId::OptRcCrf}, 0);
...
void syncControlStates(HWND hwnd) {
    const int rcSel = ComboBox_GetCurSel(GetDlgItem(hwnd, IDC_CMB_RC));
    // rcSel: 0=cq, 1=vbr, 2=cbr, 3=qp, 4=crf
    const bool usesQualityTarget = (rcSel == 0 || rcSel == 3 || rcSel == 4);
    const bool usesBitrateTarget = (rcSel == 1 || rcSel == 2);
    EnableWindow(GetDlgItem(hwnd, IDC_EDT_BITRATE), usesBitrateTarget && !guiIsTranscoding());
    EnableWindow(GetDlgItem(hwnd, IDC_EDT_QP), usesQualityTarget && !guiIsTranscoding());
```
并将第 937 行 `createEdit(IDC_EDT_QP, L"24", true);` 改为 `createEdit(IDC_EDT_QP, L"22", true);`。

#### 2.1.2 修改 `src/gui/rtx_gui.cpp:460-477, 589-599, 781-788`
在 `onStartTranscode`、`onQueueAdd` 和 `WM_DROPFILES` 读取 `guiProfile()` 之前，**必须先调用 `guiProfile() = collectProfileFromUi(hwnd);`**：
```cpp
// src/gui/rtx_gui.cpp:463-476 替换段
    guiProfile() = collectProfileFromUi(hwnd);
    wchar_t inPath[MAX_PATH] = {};
    GetDlgItemTextW(hwnd, IDC_EDT_INPUT, inPath, MAX_PATH);

    auto snapshots = g_app.jobQueue.snapshot();
    if (snapshots.empty() && inPath[0] != L'\0') {
        rtx::TranscodeProfile profile = guiProfile();
        profile.input = wideToUtf8(inPath);
        if (profile.output.empty()) {
            profile.output = rtx::deriveOutputPath(profile.input, profile.codec, {}, true);
        }
        (void)g_app.jobQueue.enqueueJob({std::filesystem::path(inPath)}, profile, std::filesystem::path(inPath).parent_path());
        refreshQueueListbox(hwnd);
        snapshots = g_app.jobQueue.snapshot();
    }
```
（在 `onQueueAdd` 第 590 行和 `WM_DROPFILES` 第 782 行前同样加上 `guiProfile() = collectProfileFromUi(hwnd);`）。

#### 2.1.3 修改 [`src/core/hw_transcode.cpp:395-400`](../../src/core/hw_transcode.cpp#L395-L400) 开启 NVENC Spatial AQ
```cpp
// src/core/hw_transcode.cpp:395-400 替换段
    config.gop_length = static_cast<int32_t>(std::max<int64_t>(1, frame_rate_.num > 0 ? frame_rate_.den * 2 : 60));
    config.rate_control = std::clamp(options_.rate_control, 0, 3); // 0=CBR, 1=VBR, 2=QP, 3=CQ
    config.qp = options_.qp;
    config.spatial_aq = (config.rate_control == RTX_RC_CQ || config.rate_control == RTX_RC_VBR) ? 1 : 0;
    config.aq_strength = 8;
    config.device_index = options_.device_index;
```

---

### 2.2 Task `FIX-P10-02`：恢复 `output_info_ = info_;` 音轨/字幕直通流描述与队列尾冲刷（根治「音频没有了」）

#### 2.2.1 修改 [`src/core/hw_transcode.cpp:266-278`](../../src/core/hw_transcode.cpp#L266-L278)
在 `NvidiaTranscodeSession::configure` 中恢复 `output_info_ = info_;` 及 `!options_.copy_audio` 音轨剔除逻辑：

```cpp
// src/core/hw_transcode.cpp:266-278 替换段
    auto dec_res = configureDecoder(source_depth);
    if (!dec_res.has_value()) return dec_res;

    output_info_ = info_;
    if (!options_.copy_audio) {
        output_info_.primary_audio_index = -1;
        output_info_.audio_tracks.clear();
        std::erase_if(output_info_.passthrough_streams,
                      [](const PassthroughStreamInfo& s) { return s.codec_type == 1; });
    }
    output_info_.time_base = Rational{1, 90000};
    output_info_.codec = options.codec;
    output_info_.video_codec = report.output_codec;
    output_info_.format_name.clear();
    output_info_.annex_b = true;
    output_info_.color = hdr_.color;
    output_info_.bit_depth = output_depth;
    frame_rate_ = chooseFrameRate(options, info_);
    output_info_.frame_rate = frame_rate_;
    return {};
```

#### 2.2.2 修改 [`src/core/hw_transcode.cpp:424-442`](../../src/core/hw_transcode.cpp#L424-L442) 与 [`src/core/hw_transcode_stages.cpp`](../../src/core/hw_transcode_stages.cpp) 的音频写入与尾部冲刷
1. 在 `NvidiaTranscodeSession::writeAudioPacket`（[`hw_transcode_internal.h:146-149`](../../src/core/hw_transcode_internal.h#L146-L149)）中，统一走 `muxer_->writePacket(packet)`（因为 `FfmpegMuxer::writePacket` 内部按 `packet.stream_index == target.source_index` 路由所有主/副音轨和字幕轨，并自动做 `av_rescale_q` 与单调保护）：
```cpp
// src/core/hw_transcode_internal.h:146-149 替换段
    Result<void> writeAudioPacket(const Packet& packet) {
        if (!muxer_) return err(ErrorCode::State, "muxer is not open");
        return muxer_->writePacket(packet);
    }
```
2. 在 [`src/core/hw_transcode_stages.cpp`](../../src/core/hw_transcode_stages.cpp) 的 `RemasterPipeline::run()` 结束解复用与解码冲刷后、调用 `session_->finalize()` 之前，**必须冲刷 `audio_queue` 中残留的所有音频/字幕包**（见 2.3 节完整代码）。

---

### 2.3 Task `FIX-P10-03`：恢复 Flicks 时间轴插帧状态机与 NVOFA / RIFE 真实插帧调用（根治「插帧彻底失效」）

#### 2.3.1 修改 [`src/core/hw_transcode_internal.h:213-275`](../../src/core/hw_transcode_internal.h#L213-L275)
在 `RemasterPipeline` 中补齐 Flicks 时间轴插帧状态机成员与子方法声明（保持每个方法严格 `<= 110 行`）：

```cpp
// src/core/hw_transcode_internal.h:213-275 替换 RemasterPipeline 声明
class RemasterPipeline {
public:
    explicit RemasterPipeline(const HwTranscodeOptions& options);
    ~RemasterPipeline();

    Result<HwTranscodeReport> run();

    Result<void> initDecodeAndDemuxStage();
    Result<void> initRemasterAndFgStage();
    Result<void> initEncoderAndMuxerStage();
    Result<void> handleDecodedFrame(const RtxFrameDesc& frame);
    Result<void> synthesizeAndRemasterWindow(const RtxFrameDesc& prev_f, const RtxFrameDesc& curr_f,
                                             int64_t out_pts_90k, float alpha);
    Result<void> remasterAndEnqueueFrame(const RtxFrameDesc& frame, int64_t pts_override,
                                         bool release_slot, bool force_reset_temporal = false);
    Result<void> drainEndOfStream();
    Result<void> flushAndFinalizePipeline();

private:
    HwTranscodeOptions options_;
    HwTranscodeReport report_;
    Clock::time_point started_;
    std::unique_ptr<NvidiaTranscodeSession> session_;
    CudaDriverApi cuda_;

    ShotGroupSegmenter segmenter_;
    uint64_t frame_index_ = 0;
    bool last_was_cut_ = false;

    bool has_prev_frame_ = false;
    RtxFrameDesc prev_frame_{};
    int64_t prev_pts_flicks_ = 0;
    int64_t next_out_pts_flicks_ = -1;
    int64_t delta_out_flicks_ = 11760000LL;
    int fruc_error_logs_ = 0;

    uint64_t anchor_p010_[2] = {0, 0};
    uint32_t anchor_slot_ = 0;
    bool anchor_has_prev_ = false;
    int64_t prev_anchor_pts_ = 0;
    uint32_t anchor_pitch_ = 0;

    void* pinned_host_buf_[2] = {nullptr, nullptr};
    size_t pinned_buf_bytes_ = 0;
    uint32_t pinned_slot_ = 0;

    std::deque<PendingEncodeItem> encode_queue_;
    std::deque<Packet> audio_queue_;
    std::mutex audio_mtx_;
    std::mutex pipeline_mutex_;
    std::condition_variable queue_ready_;
    std::condition_variable slot_free_;
    bool producer_done_ = false;
    bool consumer_failed_ = false;
    Result<void> consumer_error_;
    uint64_t remasters_issued_ = 0;
    uint64_t encodes_completed_ = 0;
    std::thread encode_thread_;

    bool rife_active_ = false;
    uint64_t fruc_out_buffer_ = 0;
    RtxNgxRemasterStats previous_stats_{};
    double pack_max_ms_ = 0.0;
    uint32_t accumulated_max_cll_ = 0;
    uint32_t accumulated_max_fall_ = 0;
    uint32_t preview_slot_ = 0;
    std::vector<uint8_t> preview_host_;
    std::optional<Clock::time_point> last_preview_download_;
    int64_t estimated_total_frames_ = 0;

    void releasePrevLease() noexcept;
    bool consumerStopped() const noexcept;
    void reportProgress();
    Result<void> flushBufferedAudioPackets();
    Result<void> queueEncodeItem(const RtxNgxRemasterOutput& out, int64_t pts,
                                const std::vector<uint8_t>& hdr10plus, bool force_key);
};
```

#### 2.3.2 修改 [`src/core/hw_transcode_stages.cpp`](../../src/core/hw_transcode_stages.cpp) 完整恢复 NVOFA / RIFE 插帧与音频队列冲刷
将 [`src/core/hw_transcode_stages.cpp:290-588`](../../src/core/hw_transcode_stages.cpp#L290-L588) 替换为以下完整实现（在 `initRemasterAndFgStage` 中初始化 `delta_out_flicks_`，在 `handleDecodedFrame` 中执行 Flicks 时间轴插帧循环并真实调用 `interpolateRife` 与 `interpolateNv12`，在切镜时向 `remasterAndEnqueueFrame` 传递 `force_reset_temporal = true` 重置 NGX VSR 时域历史帧，并在 `flushAndFinalizePipeline` 中冲刷全部缓存音频包）：

```cpp
// src/core/hw_transcode_stages.cpp:133-155 补充 delta_out_flicks_ 初始化
    const Rational& out_fps = session_->frameRate();
    delta_out_flicks_ = (out_fps.num > 0 && out_fps.den > 0)
        ? (705600000LL * out_fps.den / out_fps.num)
        : 11760000LL;
```

```cpp
// src/core/hw_transcode_stages.cpp:294-588 完整替换段
void RemasterPipeline::releasePrevLease() noexcept {
    if (has_prev_frame_ && session_) {
        session_->releaseDecodedFrame(prev_frame_.slot_index);
        has_prev_frame_ = false;
    }
}

Result<void> RemasterPipeline::flushBufferedAudioPackets() {
    if (!session_ || !session_->isMuxerOpen()) return {};
    std::deque<Packet> to_flush;
    {
        std::lock_guard<std::mutex> lk(audio_mtx_);
        to_flush.swap(audio_queue_);
    }
    for (const auto& ap : to_flush) {
        auto a_res = session_->writeAudioPacket(ap);
        if (!a_res.has_value()) return a_res;
        ++report_.packets_written;
    }
    return {};
}

Result<void> RemasterPipeline::remasterAndEnqueueFrame(
    const RtxFrameDesc& frame, int64_t pts_override, bool release_slot, bool force_reset_temporal) {
    const int64_t effective_pts = (pts_override >= 0) ? pts_override : frame.pts;
    const MediaInfo& info = session_->info();
    const bool reset_temporal = (frame_index_ == 0) || force_reset_temporal || last_was_cut_;
    last_was_cut_ = false;

    RtxNgxRemasterInput in{};
    in.y_plane = frame.planes[0];
    in.uv_plane = frame.planes[1];
    in.y_pitch = frame.pitches[0];
    in.uv_pitch = frame.pitches[1];
    in.format = frame.format;
    in.full_range = (info.color.range == ColorRange::Full) ? 1 : 0;
    in.matrix = (info.color.matrix == MatrixCoefficients::BT2020NC) ? 1 : 0;
    in.reset_temporal = reset_temporal ? 1 : 0;

    bool preview_this_frame = false;
    if (options_.preview && session_->previewReady()) {
        in.preview_rgba10_ptr = session_->previewDevicePointer(preview_slot_ & 1u);
        in.preview_pitch = session_->previewPitch();
        in.split_x_norm = options_.split_ratio != nullptr
            ? std::clamp(options_.split_ratio->load(std::memory_order_relaxed), 0.0f, 1.0f) : 0.5f;
        preview_slot_ ^= 1u;
        preview_this_frame = true;
    }

    RtxNgxRemasterOutput out{};
    RtxNgxRemasterStats stats{};
    const RtxResult r = session_->bridgeLoader().remaster(session_->bridge(), &in, &out, &stats);
    if (release_slot && frame.slot_index != 0xFFFFFFFFu) {
        session_->releaseDecodedFrame(frame.slot_index);
    }
    if (r != RTX_OK) {
        return err(mapShimResult(r), describe("rtx_ngx_bridge_remaster", r, nullptr),
                   session_->bridgeLoader().lastError(session_->bridge()));
    }

    const double pack_delta = static_cast<double>(stats.pack_kernel_ms - previous_stats_.pack_kernel_ms);
    previous_stats_ = stats;
    if (pack_delta > pack_max_ms_) pack_max_ms_ = pack_delta;
    accumulated_max_cll_ = std::max<uint32_t>(accumulated_max_cll_, out.frame_max_nits);
    accumulated_max_fall_ = std::max<uint32_t>(accumulated_max_fall_, out.frame_avg_nits);

    if (preview_this_frame) {
        const auto now = Clock::now();
        const bool due = !last_preview_download_.has_value() ||
                         std::chrono::duration<double>(now - *last_preview_download_).count() >= (1.0 / 15.0);
        if (due && session_->downloadPreviewInto(preview_host_.data(), preview_host_.size()) == RTX_OK) {
            last_preview_download_ = now;
            options_.preview(preview_host_.data(), session_->previewPitch());
        }
    }

    std::vector<uint8_t> hdr10plus;
    if (options_.hdr10plus) {
        Hdr10PlusFrameStats fstats{};
        fstats.maxscl_r_nits = std::max<uint32_t>(1u, out.frame_max_nits);
        fstats.maxscl_g_nits = std::max<uint32_t>(1u, out.frame_max_nits);
        fstats.maxscl_b_nits = std::max<uint32_t>(1u, out.frame_max_nits);
        fstats.average_maxrgb_nits = std::max<uint32_t>(1u, out.frame_avg_nits);
        const float ratio = out.frame_max_nits > 0
            ? static_cast<float>(out.frame_avg_nits) / static_cast<float>(out.frame_max_nits) : 0.5f;
        fstats.knee_point_x = 500;
        fstats.knee_point_y = static_cast<uint16_t>(std::clamp(static_cast<int>(ratio * 1000.0f), 1, 1000));
        hdr10plus = (options_.codec == VideoCodec::HEVC)
            ? SeiSynthesizer::hevcHdr10PlusPrefixSei(fstats)
            : SeiSynthesizer::av1Hdr10PlusMetadataObu(fstats);
    }

    auto q_res = queueEncodeItem(out, effective_pts, hdr10plus, reset_temporal);
    if (!q_res.has_value()) return q_res;
    ++frame_index_;
    return {};
}

Result<void> RemasterPipeline::synthesizeAndRemasterWindow(
    const RtxFrameDesc& prev_f, const RtxFrameDesc& curr_f, int64_t out_pts_90k, float alpha) {
    if (alpha <= 0.0001f) {
        return remasterAndEnqueueFrame(prev_f, out_pts_90k, /*release_slot=*/false);
    }
    if (alpha >= 0.9999f) {
        return remasterAndEnqueueFrame(curr_f, out_pts_90k, /*release_slot=*/false);
    }

    const uint32_t pitch = prev_f.pitches[0];
    const int input_full_range = (session_->info().color.range == ColorRange::Full) ? 1 : 0;

    if (rife_active_) {
        int32_t scene_cut = 0;
        const int32_t status = session_->bridgeLoader().interpolateRife(
            session_->bridge(), prev_f.planes[0], curr_f.planes[0], pitch,
            alpha, fruc_out_buffer_, input_full_range, &scene_cut);
        if (status == RTX_OK) {
            if (scene_cut != 0) {
                ++report_.fruc_scene_cuts;
                last_was_cut_ = true;
                const RtxFrameDesc& fb = (alpha < 0.5f) ? prev_f : curr_f;
                return remasterAndEnqueueFrame(fb, out_pts_90k, /*release_slot=*/false, /*force_reset_temporal=*/true);
            }
            ++report_.frames_interpolated;
            RtxFrameDesc interp_frame = prev_f;
            interp_frame.planes[0] = fruc_out_buffer_;
            interp_frame.planes[1] = fruc_out_buffer_ + static_cast<uint64_t>(pitch) * prev_f.height;
            interp_frame.pts = out_pts_90k;
            interp_frame.slot_index = 0xFFFFFFFFu;
            return remasterAndEnqueueFrame(interp_frame, out_pts_90k, /*release_slot=*/false);
        }
        ++report_.fruc_failures;
        if (fruc_error_logs_ < 5) {
            ++fruc_error_logs_;
            logError(std::format("[FRUC-ERROR] RIFE interpolation failed (code={}): {}",
                                 status, session_->bridgeLoader().lastError(session_->bridge())));
        }
        const RtxFrameDesc& fb = (alpha < 0.5f) ? prev_f : curr_f;
        return remasterAndEnqueueFrame(fb, out_pts_90k, /*release_slot=*/false);
    }

    const RtxResult status = session_->bridgeLoader().interpolateNv12(
        session_->bridge(), prev_f.planes[0], curr_f.planes[0], pitch, alpha, fruc_out_buffer_);
    if (status == RTX_OK) {
        ++report_.frames_interpolated;
        RtxFrameDesc interp_frame = prev_f;
        interp_frame.planes[0] = fruc_out_buffer_;
        interp_frame.planes[1] = fruc_out_buffer_ + static_cast<uint64_t>(pitch) * prev_f.height;
        interp_frame.pts = out_pts_90k;
        interp_frame.slot_index = 0xFFFFFFFFu;
        return remasterAndEnqueueFrame(interp_frame, out_pts_90k, /*release_slot=*/false);
    }
    ++report_.fruc_failures;
    return remasterAndEnqueueFrame(prev_f, out_pts_90k, /*release_slot=*/false);
}

Result<void> RemasterPipeline::handleDecodedFrame(const RtxFrameDesc& frame) {
    constexpr int64_t kFlicksPerTick90k = 7840;
    if (!options_.fruc_enabled || fruc_out_buffer_ == 0) {
        return remasterAndEnqueueFrame(frame, -1, /*release_slot=*/true);
    }
    if (!has_prev_frame_) {
        prev_frame_ = frame;
        prev_pts_flicks_ = frame.pts * kFlicksPerTick90k;
        next_out_pts_flicks_ = prev_pts_flicks_;
        has_prev_frame_ = true;
        return {};
    }
    const int64_t curr_pts_flicks = frame.pts * kFlicksPerTick90k;
    if (curr_pts_flicks <= prev_pts_flicks_) {
        session_->releaseDecodedFrame(prev_frame_.slot_index);
        prev_frame_ = frame;
        prev_pts_flicks_ = curr_pts_flicks;
        return {};
    }
    const int64_t window_duration = curr_pts_flicks - prev_pts_flicks_;
    while (next_out_pts_flicks_ < curr_pts_flicks) {
        if (consumerStopped()) {
            return err(ErrorCode::State, "transcode cancelled");
        }
        const float alpha = static_cast<float>(next_out_pts_flicks_ - prev_pts_flicks_) /
                            static_cast<float>(window_duration);
        const int64_t out_pts_90k = (next_out_pts_flicks_ + (kFlicksPerTick90k / 2)) / kFlicksPerTick90k;
        auto res = synthesizeAndRemasterWindow(prev_frame_, frame, out_pts_90k, alpha);
        if (!res.has_value()) return res;
        next_out_pts_flicks_ += delta_out_flicks_;
    }
    session_->releaseDecodedFrame(prev_frame_.slot_index);
    prev_frame_ = frame;
    prev_pts_flicks_ = curr_pts_flicks;
    has_prev_frame_ = true;
    return {};
}

Result<void> RemasterPipeline::drainEndOfStream() {
    constexpr int64_t kFlicksPerTick90k = 7840;
    if (options_.fruc_enabled && fruc_out_buffer_ != 0 && has_prev_frame_) {
        if (next_out_pts_flicks_ <= prev_pts_flicks_) {
            const int64_t out_pts_90k = (next_out_pts_flicks_ + (kFlicksPerTick90k / 2)) / kFlicksPerTick90k;
            auto res = remasterAndEnqueueFrame(prev_frame_, out_pts_90k, /*release_slot=*/false);
            if (!res.has_value()) {
                releasePrevLease();
                return res;
            }
            next_out_pts_flicks_ += delta_out_flicks_;
        }
        releasePrevLease();
    }
    return {};
}

Result<void> RemasterPipeline::flushAndFinalizePipeline() {
    {
        std::lock_guard<std::mutex> lock(pipeline_mutex_);
        producer_done_ = true;
    }
    queue_ready_.notify_all();
    if (encode_thread_.joinable()) {
        encode_thread_.join();
    }
    releasePrevLease();
    if (consumer_failed_) {
        return consumer_error_;
    }

    auto flushed_enc = session_->flushEncoder(report_);
    if (!flushed_enc.has_value()) return flushed_enc;

    auto flushed_aud = flushBufferedAudioPackets();
    if (!flushed_aud.has_value()) return flushed_aud;

    if (rife_active_ && report_.fruc_failures > 0 && report_.frames_interpolated == 0) {
        (void)session_->finalize(report_, started_);
        return err(ErrorCode::Internal,
                   std::format("RIFE frame interpolation failed on all {} intermediate frame(s): {}",
                               report_.fruc_failures,
                               session_->bridgeLoader().lastError(session_->bridge())));
    }

    auto finalized = session_->finalize(report_, started_);
    if (!finalized.has_value()) return finalized;

    if (report_.frames_encoded > 0) {
        const double n = static_cast<double>(report_.frames_encoded);
        report_.remaster_preprocess_ms = previous_stats_.preprocess_ms / n;
        report_.remaster_ngx_ms = previous_stats_.ngx_ms / n;
        report_.remaster_pack_kernel_ms = previous_stats_.pack_kernel_ms / n;
        report_.remaster_frame_ms = report_.main_loop_ms / n;
        report_.remaster_pack_kernel_max_ms = pack_max_ms_;
        report_.remaster_pack_launches = previous_stats_.pack_launches;
    }
    report_.measured_max_cll_nits = session_->measuredMaxCllNits();
    report_.measured_max_fall_nits = session_->measuredMaxFallNits();
    return {};
}
```

并在 `RemasterPipeline::run()`（第 478~585 行）中：
1. 将第 489~490 行的局部变量 `std::deque<Packet> audio_queue; std::mutex audio_mtx;` 删除（直接使用成员变量 `audio_queue_` 与 `audio_mtx_`），将第 516~531 行的音频分支改为调用 `auto a_res = flushBufferedAudioPackets();` 与 `auto w_res = session_->writeAudioPacket(pkt);`（检查返回值）；
2. 将第 549~551 行与第 571~573 行的 `options_.vsr_before_fg ? processAnchorFrameVsrFirst(fdesc) : processAnchorFrameFgFirst(fdesc)` 统一替换为 `handleDecodedFrame(fdesc)`；
3. 在第 578 行解码器 EOS 排空循环之后，调用 `if (pipe_status.has_value()) { pipe_status = drainEndOfStream(); }`。

---

## 3. 返工终审硬门禁核验清单（Checklists）

执行工程师完成 `FIX-P10-01 ~ FIX-P10-03` 合入并重新编译 `.vs/build` Release 产物后，必须通过以下 **4 道实测硬门禁**：

- [ ] **Gate FIX-10.1（Win32 GUI & CLI `CQ` 码控实测门禁）**：
  - 启动 `CineRTX.exe` GUI，验证右侧「码率控制 / Rate Control」下拉框包含 5 项（首项默认选中 `CQ 恒定感知画质 (Spatial AQ)`，且包含 `CRF 纯 CPU 母版软编`），「QP / CQ」输入框默认值为 `22` 且处于可编辑激活态；
  - 使用 `CQ` 模式压制测试视频，验证 NVENC `rc.enableAQ == 1`、`rc.targetQuality == 22` 真实生效。
- [ ] **Gate FIX-10.2（音轨与内挂字幕 100% 无损直通门禁）**：
  - 输入含 AAC/AC3/FLAC 音轨的测试视频，转码完成后使用 `ffprobe` 或 `CineRTX.exe --probe` 检查输出 `.mp4`/`.mkv`，确认 `audio_streams >= 1`，播放器播放输出视频声音清晰且音画完全同步。
- [ ] **Gate FIX-10.3（NVOFA 与 RIFE 插帧实测翻倍门禁）**：
  - 对 24fps / 30fps 测试视频分别开启 `60-nvofa`、`120-nvofa`、`60-rife` 转码；
  - 验证转码报告中 `fruc_backend` 分别为 `"nvof"` / `"rife-trt"`，**`frames_interpolated > 0`**，且 `frames_encoded` 严格等于源帧数按目标帧率倍增后的帧数（例如 30fps -> 60fps 时 `frames_encoded >= frames_decoded * 2 - 1`）。
- [ ] **Gate FIX-10.4（全量 CTest 单元测试 100% 回归门禁）**：
  - 执行 `ctest --test-dir .vs/build -C Release --output-on-failure`，全部测试用例 100% 通过。
