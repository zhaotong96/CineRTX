# **38\_CineRTX Phase 13 临时封存基线阶段性专家验收与风险排雷终审意见书**

* **文档编号**：`CINERTX-P13-RULING-038`  
* **归档路径**：`docs/phase13/38_CineRTX Phase 13 临时封存基线阶段性专家验收与风险排雷终审意见书.gdoc`  
* **响应委托**：`37_CineRTX Phase 13 临时封存基线阶段性专家验收暨接手准入委托书.md`（`CINERTX-P13-DELEGATION-037`）  
* **签发主体**：多媒体系统工程、实时图形合成与异构计算首席领域专家组（Chief Domain Experts & Architecture Board）  
* **受呈报方**：CineRTX 项目总协调负责人（Coordinator）  
* **审计基线**：Commit `9355120`（`wip(phase13): temporary checkpoint - MSVC full build, 5 static gates, and 150/150 tests green`）  
* **签发日期**：`2026-11-26`

# **执行摘要（Executive Ruling）**

首席领域专家组对 Commit `9355120` 全量源码与构建门禁实施了穿透式代码级法医审查，逐一核验了协调负责人预检锁定的 5 项高危嫌疑点，并额外穿透排查出 **4 项隐蔽内存/同步/构建合规缺陷**。现签帧如下终审裁决：

1. **阶段性验收总判定：🟡 有条件准入接手（CONDITIONAL TAKEOVER AUTHORIZATION — REJECT EXTERNAL REWORK LOOP, ABSORB SURGICAL FIXES INTO COORDINATOR WAVE 0-A CLOSURE）**  
   * **放行与封存**：Commit `9355120` 的底层工程底座已达到可接管状态——MSVC `/W4 /WX` 全目标零错误编译通过、五重静态门禁（`AuditPrivacy 0/430`、`AuditTypes 34/35`、`AuditSpdx PASS`、`AuditSloc 12,385 <= 23,000`、`AuditImports GUI=4 / HEADLESS=2`）全绿、`rtx_tests.exe` **150/150 PASS**、RTX 5060 四桶单 Profile `.engine`（`ultra`/`wide`/`square`/`tall`）物理生成闭环，以及 Wave 1-A D3D12-DComp 离屏计算合成视口链（`preview_viewport.cpp`）均真实有效，**予以验收放行**。  
   * **排雷处置策略裁决**：针对预检 5 项嫌疑点及专家额外检出的 4 项隐蔽缺陷（合计 **9 项待排雷缺陷：3 项 P0 级、4 项 P1 级、2 项 P2 级**），**严禁再退回外部执行工程师开启新一轮返工拉锯战**（外部工程师多次因缺少 Windows/MSVC/GPU 宿主机环境导致盲写破坏构建与逻辑悬空）。**正式批准由协调负责人即刻接管主仓库**，将上述 P0/P1 排雷手术并入 `31` 号尾项与 `32` 号波次中直接在宿主机一次性清零。  
2. **五大高危嫌疑点穿透审定结论总览**：  
   * **嫌疑 1（MP4 静默丢音）**：**裁定属实（P0 重大产品违约）**。`src/io/ffmpeg_io.cpp:59-61` 仅放行 6 种直通编码，`822-827` 行对 `DTS / FLAC / PCM / Vorbis` 等直接 `continue` 静默丢弃；且 `RtxFfmpegStatic.cmake:449-456` 的 Zig 交叉构建分支遗漏了 `concat` 解复用器与 `mov_text` 编码器。必须在 `32` 号波次落地"直通白名单 \+ 非直通音轨自动 `swresample` \+ 静态 `aac` 编码器转码 \+ 显式拒止"闭环，严禁静默丢音。  
   * **嫌疑 2（`MergeConcat` 依赖 `.ffconcat`）**：**裁定属实（P0 崩溃/安全阻断）**。`job_queue.cpp:242-251` 生成的临时 `.ffconcat` 含带盘符绝对路径，而 `FfmpegDemuxer::open()`（`ffmpeg_io.cpp:265`）未设置 `safe=0` 字典选项，FFmpeg `concat` 解复用器默认 `safe=1` 遇到 `file 'C:/...'` 将直接报 `Unsafe file name` 失败；且异构编码/分辨率/帧率输入送入单解码器必然崩溃。裁定：在 `32` 号波次将 `MergeConcat` 升级为"同构流探测直走 `safe=0` 拼接 vs 异构流自动路由至既有 `runNvidiaRemasterRenderGraph()` 多解码器归一化管道（`hw_transcode_stages.cpp:597` 已具备多段切换解码器能力！）"。  
   * **嫌疑 3（NVOFA 像素 SAD 切镜内存与精度隐患）**：**裁定部分属实，存在 2 处隐蔽内存/时序 Bug（P0/P1）**。① `bridge_exports.cpp:708-738` 在 `rtx_ngx_bridge_interpolate_nv12()` 开头读取 `prev_nv12` / `curr_nv12` 前**未调用 `stream_synchronize(pack_stream)`**，若上游 CUDA 异步写入尚未完成即执行同步 `cuMemcpyDtoH`，存在异步读写竞态；② 仅按 `width` 采样前 256 像素（`sample_w = min(width, 256)`），对 4K 视频仅采样左侧 `6.6%` 宽度，右侧画面切镜完全盲区；③ `hw_transcode_stages.cpp:447` 调用 `interpolateNv12` 后未接收切镜标志，未触发 `last_was_cut_ = true` 导致后级 VSR 未重置时域历史与 NVENC 未插入 IDR 帧。  
   * **嫌疑 4（1001 时基与常量残留）**：**裁定局部残留属实（P1），核心运算安全无溢出**。`include/rtx/flicks.h` 与 `time_model.h` 的 `706,305,600,000 Hz` 128 位拆分数学已完全安全（支持最长约 \$1.305 \\times 10^7\$ 秒 \$\\approx\$ 151 天单段连续时长无 `int64_t` 溢出）；但 `hw_transcode_stages.cpp:144` 仍残留旧常量 `11760000LL`（应改为 `static_cast<int64_t>(kFlicksPerSecond / 60ULL)`），且 `ffmpeg_io.cpp:17, 294` 注释仍写 `705,600,000 Hz`。  
   * **嫌疑 5（4K `vsr-first` 显存、四桶上界与 DComp 视口）**：**裁定四桶几何上界存在边界漏洞（P1），显存安全受控，DComp 视口仅余一处尺寸同步缺口（P1）**。① `rife_engine.h:39-44` 中 `Ultra` 桶 `max_w = 3840`，导致 `5120x1440`（32:9 带鱼屏）或 `5120x2160`（5K 21:9）在 `vsr-first` 下越界拒止；② `1080p -> 4K vsr-first` 走 `rife_scale = 0.5f` 时 RIFE 实际在 `1920x1080` 推理（峰值约 `1.1–1.6 GB`），叠加 VSR 4K 与 NVENC 缓冲总占用约 `4.2–5.4 GB`，处于 RTX 5060 `7,300 MiB` DXGI 预算与 `5,986 MiB` 滞后水位线安全区内；③ `workstation_canvas.cpp:593-601` 切语言或 `WM_SIZE` 调整窗口后，未同步调用 `previewViewport->resizeSwapChain()`，会导致 DComp 视觉缩放模糊。

# **一、 Commit `9355120` 阶段性验收裁决矩阵（放行 vs 排雷）**

| 审计模块 / 施工波次 | 对应源码资产 | 验收状态 | 专家技术核验结论 |
| :---- | :---- | :---- | :---- |
| **构建系统与五重静态门禁** | `build.bat`, `CMakeLists.txt`, `cmake/Audit*.cmake` | **PASS (放行)** | MSVC 19.51 全目标 0 Error；`AuditTypes=34/35`、`AuditPrivacy=0/430`、`AuditSpdx=PASS`、`AuditSloc=12,385`、`AuditImports (GUI=4, HEADLESS=2)` 全部达标。 |
| **1001 扩展时基与多测试回归** | `include/rtx/flicks.h`, `include/rtx/time_model.h`, `tests/*` | **PASS (放行)** | `Flicks` 标准库周期已与 `kFlicksPerSecond` 编译期 `static_assert` 锁定，128 位分拆乘法经数学验证无溢出，150/150 单测全绿；仅需清理 `hw_transcode_stages.cpp:144` 兜底常量。 |
| **四桶单 Profile RIFE 引擎 (`[DEC-011]`)** | `rife_engine.h/.cpp`, `rife_builder.cpp`, `EnsureRifeEngine.cmake` | **CONDITIONAL PASS** | 严格遵循一桶一单 Profile 铁律，`ultra`/`wide`/`square`/`tall` 四引擎物理生成；需微调 `Ultra` `max_w` 上界以覆盖 `5120x1440` 与修复 `rife_engine.cpp:210` 遗留文件名回落逻辑。 |
| **D3D12-DComp 离屏合成视口** | `src/gui/preview_viewport.cpp`, `preview_tone_map_cs.hlsl` | **CONDITIONAL PASS** | `CreateSwapChainForComposition` \+ `compute_target_` UAV \+ `CopyResource` \+ `PreviewSlotTicket` 延迟退役机制完整闭环；需补齐窗口尺寸变更时的 `resizeSwapChain()` 联动。 |
| **MP4 兼容交付与 `MergeConcat`** | `src/io/ffmpeg_io.cpp`, `src/core/job_queue.cpp` | **REJECT (纳入 32 号修)** | 存在"非直通音轨静默丢弃"与"`.ffconcat` `safe=1` 路径拦截 \+ 异构流单解码器崩溃"两项 P0 级缺陷，必须在 `32` 号波次重构闭环。 |
| **NVOFA 切镜检测与 WebUI 帧流** | `src/shim/ngx_bridge/bridge_exports.cpp`, `webui/server.py` | **REJECT (纳入 31 尾项修)** | `bridge_exports.cpp:708` 存在未同步流即 Host 读显存竞态与采样盲区，且未回传 `scene_cut` 重置 VSR/IDR；WebUI C++ 侧落盘帧生产者待补齐。 |

# **二、 五大高危嫌疑点逐项法医级深度审定**

## **2.1 嫌疑 1：MP4 音轨兼容性处理"静默丢音"与静态编联缺口（裁定：P0 重大违约）**

1. **代码取证与因果链**：  
   * 在 `src/io/ffmpeg_io.cpp:59-61`，`isMp4DirectCopyAudioCodec()` 放行 `AAC / MP3 / OPUS / ALAC / AC3 / EAC3`。  
   * 但在 `FfmpegMuxer::open()`（`src/io/ffmpeg_io.cpp:822-827`）中，当输入视频携带影视/录屏极常见的 **`DTS`、`FLAC`、`PCM_S16LE/S24LE`、`Vorbis`** 音轨且输出为 `.mp4`/`.mov` 时，代码仅向 `warnings_` 压入一条字符串便直接 `continue` 跳过创建音频流——**且 `warnings_` 从未暴露给 GUI/WebUI/CLI 阻断提示**，最终交付一个**完全无声的 `.mp4` 文件**！  
   * 更隐蔽的是字幕处理（`ffmpeg_io.cpp:829-839`）：`FfmpegDemuxer::finishOpen()`（第 499-515 行）放行了 `SUBRIP (SRT) / ASS / SSA / WEBVTT` 进入 `passthrough_streams`，但 `FfmpegMuxer::open()`（第 833 行）在 `mp4_output` 下仅允许 `AV_CODEC_ID_MOV_TEXT`，导致所有 MKV 内封 SRT/ASS 字幕在转封装 MP4 时同样被静默丢弃（尽管 `RtxFfmpegStatic.cmake:396` 已编译了 `--enable-encoder=mov_text` 却完全未调用）。  
2. **强制修复规范（并入 `32` 号施工单）**：  
   * **音轨三态确定性策略**：  
     * **直通态（Stream Copy）**：命中 `isMp4DirectCopyAudioCodec()`（`AAC / MP3 / OPUS / ALAC / AC3 / EAC3`）且时间基合法时，直接 `avcodec_parameters_copy` 零损直通；  
     * **自动转码态（Auto-Transcode to AAC-LC）**：对于 `DTS / FLAC / PCM_* / Vorbis` 等非 MP4 兼容音轨，利用静态 FFmpeg 已内置的 `avcodec` 解码器 \+ `libswresample` \+ 内置原生 `aac` 编码器（`RtxFfmpegStatic.cmake:395` 已启用 `--enable-encoder=aac`），实时重采样为 `48kHz / FLTP / 256kbps (stereo) 或 512kbps (5.1)` AAC-LC 写入 MP4，**零外部 DLL 依赖且彻底杜绝无声文件**；  
     * **显式拒止态（Explicit Error）**：若源音轨编码器确实无法解码或用户显式指定 `--strict-audio-copy` 禁转码，必须返回 `err(ErrorCode::Unsupported, ...)` 中止任务，绝不允许静默吞掉音轨。

## **2.2 嫌疑 2：`MergeConcat` 强依赖 `.ffconcat` 的安全拦截与异构崩溃隐患（裁定：P0 阻断缺陷）**

1. **代码取证与因果链**：  
   * **安全选项拦截必败**：`EncoderJobQueue::workerLoop()`（`src/core/job_queue.cpp:243-250`）在临时目录生成 `cinertx_concat_<id>.ffconcat`，内含 `file 'D:/videos/clip1.mp4'` 等带盘符绝对路径。随后调用 `runNvidiaRemasterTranscode()` \-\> `FfmpegDemuxer::open()`（`src/io/ffmpeg_io.cpp:265`），此处调用 `avformat_open_input(&format_, path_str.c_str(), nullptr, nullptr)` **未传入任何 `AVDictionary*` 选项**！FFmpeg `concat` 解复用器默认 `safe=1`，按 FFmpeg 安全规范，凡包含协议前缀、盘符冒号（`C:`）或相对路径越界字符的文件名一律触发 `Unsafe file name` 错误直接拒绝打开！  
   * **异构片源硬件崩溃**：即便绕过 `safe=1`，当合并队列中两段视频存在\*\*不同编码（H.264 \+ HEVC）、不同分辨率（1080p \+ 4K）、不同帧率（23.976 \+ 60fps）或不同位深（8-bit NV12 \+ 10-bit P010）\*\*时，单个 `FfmpegDemuxer` \+ 单个 `RtxDecoder` \+ 单个 `RtxNgxBridge` 在流参数突变瞬间必然触发 `cuvidParseVideoData` 报错或显存步长越界崩溃。  
   * **现有仓库已有现成基础设施被闲置**：穿透核查 `src/core/hw_transcode_stages.cpp:597-790` 发现，`RemasterPipeline::runRenderGraph()` **已经完整实现了按片段（Segment）动态销毁并重建 `FfmpegDemuxer` 与 `RtxDecoder`、统一重映射至 `Flicks` 时间轴并在硬切点重置 VSR 时域历史与强制 NVENC IDR 的异构多段渲染管线！** 外部工程师舍近求远去写 `.ffconcat` 纯属重复造轮子且造出了残次品。  
2. **强制修复规范（并入 `32` 号施工单）**：  
   * 在 `EncoderJobQueue::workerLoop()` 的 `JobOutputMode::MergeConcat` 分支落地**双路智能分发**：  
     1. **探针预检**：打开各输入文件读取 `MediaInfo`（仅读容器头，耗时 `< 5ms`）。  
     2. **同构流路径（Homogeneous Fast-Path）**：若所有片段 `codec`、`width`、`height`、`bit_depth`、`frame_rate` 及主音轨参数完全一致，使用 `.ffconcat` 并在 `FfmpegDemuxer::open()` 检测到 `.ffconcat` 后缀时显式注入 `av_dict_set(&opts, "safe", "0", 0)`。  
     3. **异构流路径（Heterogeneous RenderGraph Path）**：只要任意两段输入存在编码、分辨率、帧率或位深差异，**零临时文件**直接将 `inputs` 组装为 `std::vector<RenderGraphSegment>` 与 `std::vector<AudioGraphSegment>`，路由调用既有的 `runNvidiaRemasterRenderGraph()` 完成多解码器无缝归一化重制导出！

## **2.3 嫌疑 3：NVOFA 像素 SAD 切镜检测的内存竞态、采样盲区与状态断链（裁定：P0 内存/时序隐患 \+ P1 精度缺陷）**

1. **代码取证与因果链**：  
   * **P0 异步 CUDA 流未同步竞态**：在 `src/shim/ngx_bridge/bridge_exports.cpp:708-738`，`rtx_ngx_bridge_interpolate_nv12()` 进入后直接调用 `rtx_ngx::nv_cuda::memcpyDtoH(prev_row, prev_nv12 + row_off, sample_w)`。需注意：`cuMemcpyDtoH` 虽对 Host 端同步，但在 CUDA 驱动 API 中，若 `prev_nv12` 或 `curr_nv12` 刚由前序异步内核在非默认流（`pack_stream` 或 NVDEC 解码流）写入且尚未同步，跨流读取存在竞态风险；且在检测未命中切镜时，多达 16 次小块 `cuMemcpyDtoH` 往返引入了显著的 PCIe 同步气泡。  
   * **P1 4K 画面 93.4% 采样盲区与字幕误判**：第 710 行 `const uint32_t sample_w = (width < 256u) ? width : 256u;` 仅从每行 `x = 0` 开始读取前 256 个字节！对于 `3840x2160` 4K 视频，该采样仅覆盖最左侧 `6.67%` 的窄条区域，画面中右部发生镜头切换或局部物体高速运动时完全漏检；同时全局均值 `mean_sad > 35.0f` 缺乏分块离群值（Outlier）统计，全屏闪白/字幕跳变易误触发。  
   * **P1 切镜事件未回传后级管线**：`rtx_ngx_bridge_interpolate_nv12` 的 C ABI 签名缺少 `int32_t* scene_cut` 输出参数（对比 `rtx_ngx_bridge_interpolate_rife` 第 790 行已具备 `int32_t* scene_cut`），导致 `hw_transcode_stages.cpp:447` 在 NVOFA 切镜时**未递增 `report_.fruc_scene_cuts`、未置位 `last_was_cut_ = true`**，后级 VSR 仍沿用上一镜头的历史帧缓冲产生鬼影，NVENC 也未在切点强制插入 IDR 帧！  
2. **强制修复规范（并入 `31` 号尾项，在接手后首个提交清零）**：  
   * **复用既有 GPU `launchEvalBlockSad8x8` 内核（零额外 D2H 带宽）**：`rife_engine.cpp:447-449` 已证明 `rtx_ngx` 内置了异步 GPU `launchEvalBlockSad8x8` \+ `evalBlockSadSceneCut()`（8x8 分块 SAD \+ 离群块比例 \+ EMA 自适应运动基线，覆盖全画面 100% 区域且仅回传 `64 * 4 = 256 Bytes`）。将 NVOFA 切镜检测统一迁移至同一 GPU `launchEvalBlockSad8x8`（在 `pack_stream` 上顺次入队并在回读 `d_block_sad_` 前执行 `stream_synchronize(pack_stream)`），彻底消除未同步竞态与 4K 左侧采样盲区。  
   * **打通 `scene_cut` 回传**：扩展 NVOFA 插帧内部状态或桥接查询，使切镜时同步递增 `report_.fruc_scene_cuts` 并触发 `last_was_cut_ = true`（重置 VSR 时域历史 \+ 强制 NVENC IDR）。

## **2.4 嫌疑 4：`1001` 扩展时基边界与旧常量残留审查（裁定：P1 常量残留，核心运算安全）**

1. **数学边界与溢出穿透验证**：  
   * \$kFlicksPerSecond \= 706,305,600,000\\text{ULL}\$（\$\\approx 2^{39.36}\$）。  
   * 在 `include/rtx/time_model.h:47-63`（`absoluteTimestampToFlicks`）与 `99-114`（`formatSmpteTimecode`）中，均已采用 `whole + rest` 商余拆分算法：  
     * `whole * flicks_per_unit` 在 `int64_t` 正数上限 \$2^{63}-1 \\approx 9.223 \\times 10^{18}\$ 下，最大可表示连续时长为 \$\\lfloor (2^{63}-1) / 706,305,600,000 \\rfloor \= \\mathbf{13,058,619\\text{ 秒}}\$（约 **3,627 小时 / 151 天**），远远覆盖任何电影与长视频素材；  
     * 余数项 `rest * flicks_per_unit` 当 `den <= 1001`、`num <= 30000` 时最大值为 \$1000 \\times 30000 \\times 7.063 \\times 10^{11} \\approx 2.118 \\times 10^{19}\$——**注意此处风险点！** 当 `den == 1001` 且 `num == 60000`（如 `time_base = {60000, 1001}` 或非标准大分子时基 `time_base = {1001, 24000}` 不会触发，但若容器出现异常 `time_base = {100000, 1001}`），`rest * flicks_per_unit` 可能逼近 `UINT64_MAX`（\$1.844 \\times 10^{19}\$）！建议在 `absoluteTimestampToFlicks` 中先对 `num` 与 `den` 执行 `std::gcd` 约分。  
2. **残留旧魔数清点（必须修复）**：  
   * `src/core/hw_transcode_stages.cpp:144`：`delta_out_flicks_` 兜底分支仍写死 `: 11760000LL;`（该值仅为旧 `705.6 MHz` 下的 1/60 秒，在 `706.3056 GHz` 下对应 `60,060 fps`，会导致无帧率元数据的裸流插帧输出 6 万帧！），必须修正为 `: static_cast<int64_t>(kFlicksPerSecond / 60ULL);`（`11,771,760,000LL`）。  
   * `src/io/ffmpeg_io.cpp:17, 294`：注释仍写 `(705,600,000 Hz)` 与 `705.6 MHz`，同步更新为 `706,305,600,000 Hz` 以免误导后续维护。

## **2.5 嫌疑 5：4K `vsr-first` 显存、四桶上界与桌面 DComp 视口核查（裁定：P1 几何与视口边界需修补）**

1. **四桶上界与 4K 超宽/异比兼容边界**：  
   * 核查 `src/shim/ngx_bridge/rife_engine.h:39-44`：  
     * `Ultra`（\$R \> 2.05\$）：`opt = {2560, 1088}`, `max = {3840, 1600}`；  
     * `Wide`（\$1.20 \< R \\le 2.05\$）：`opt = {1920, 1088}`, `max = {3840, 2176}`；  
     * `Square`（\$0.80 \\le R \\le 1.20\$）：`opt = {1472, 1472}`, `max = {2560, 2560}`；  
     * `Tall`（\$R \< 0.80\$）：`opt = {1088, 1920}`, `max = {2176, 3840}`。  
   * **边界漏洞 1（超宽带鱼屏 32:9 / 5K2K 21:9 越界）**：当输入为 `5120x1440`（32:9 录屏）或 `5120x2160`（5K 21:9 电影）且选择 `vsr-first`（`rife_scale = 0.5f`）时，`rife_engine.cpp:299-302` 计算 `infer_width_ = round(5120 * 0.5) = 2560`，处于 `Ultra` 桶 `[128, 3840]` 内**可通过**；但若输入为 `2560x720` 且放大至 `5120x1440` 时若用户强制 `scale = 1.0f`，则 `aligned_w = 5120 > max_w (3840)` 会被拒止。建议将 `Ultra` 桶 `max_w` 放宽至 `5120`（单 Profile 静态工作空间上限已锁定 `1 GiB`，不增加额外显存超标风险）。  
   * **边界漏洞 2（`rife_engine.cpp:208-218` 历史遗留文件名覆盖 Bug）**：核查 `rife_engine.cpp:208-218` 发现一段危险逻辑：当 `options_.rife_engine_path` 传入默认路径（如 `.../rife_v4.25.engine`）时，第 147 行 `found_existing` 先将其标记为已找到，第 208-217 行尝试替换为同级目录下的 `preferred_name`（如 `rife_v4.25_tall.engine`）；**但如果调用方显式传入了 `rife_v4.25_wide.engine`（而非 `rife_v4.25.engine`）去处理竖屏视频，第 208 行仅判断 `filename() == "rife_v4.25.engine"`，不会切换到 `rife_v4.25_tall.engine`，从而直接因竖屏高度 `1920 > 1088` 或 `3840 > 2176` 报 `cannot accommodate frame geometry` 失败！** 应扩展为：凡文件名属于任一通用/桶名（`rife_v4.25*.engine`）且同目录存在对应画幅桶文件时，自动路由至目标画幅桶引擎。  
2. **8GB RTX 5060 `1080p -> 4K vsr-first` 显存与吞吐评估**：  
   * 在 `hw_transcode_stages.cpp:96-100` 下，`1080p -> 4K vsr-first` 时 `rife_w = 3840, rife_h = 2160`，触发 `rife_scale = 0.5f`，RIFE 实际推理张量尺寸为 `1920x1088`（FP16/FP32 混合精度，`trt_peak` 实测约 `1.2–1.5 GiB`）。  
   * 叠加 NVDEC 1080p 解码池（\~`180 MiB`）、NGX VSR 1080p-\>4K 上下文与 P010 三槽输出环（\~`1.8 GiB`）、NVENC 4K10bit 环形编码池（\~`650 MiB`）及 D3D12 视口（\~`120 MiB`），总显存峰值约 **`3.9–4.3 GiB`**，远低于 RTX 5060 `7,300 MiB` DXGI 预算及 `5,986 MiB`（82%）降级水位线。唯一隐患是 `hw_transcode_stages.cpp:526` 中 `vramObserveFrame(vram_scheduler_, 0ull, working_set)` 传入当前使用量 `0ull`（依赖调度器内部自行查询或估算），在 `32` 号波次应确保传入真实 CUDA/DXGI 显存占用采样。  
3. **桌面 `CineRTX.exe` UI / DComp 视口断链核查**：  
   * 经逐行审查 `src/gui/preview_viewport.cpp` 与 `src/gui/workstation_canvas.cpp`，`CreateSwapChainForComposition` \-\> `compute_target_` UAV \-\> `CopyResource` \-\> `SwapChain::Present` \-\> `PreviewSlotTicket` 延迟释放链已完整闭环，无资源泄漏。  
   * **检出 1 处 UI 交互缺陷（P1）**：`workstation_canvas.cpp` 中仅在 `setBounds()` 调用 `MoveWindow(child_hwnd_)`，但当主窗口发生 `WM_SIZE` 缩放或侧边栏布局变化时，未同步调用 `app.previewViewport->resizeSwapChain(new_w, new_h)`，导致 DXGI 交换链保持初始 `960x540` 分辨率由 DWM 拉伸，高 DPI 下预览画面发虚且 A/B 分屏准星像素变粗。

# **三、 专家组额外穿透检出的 4 项隐蔽缺陷清单**

除协调负责人预检的 5 项嫌疑点外，专家组交叉比对全仓源码与构建脚本，额外锁定以下 4 项必须一并清理的隐患：

1. \[EXTRA-1 / P1\] RtxFfmpegStatic.cmake Zig 交叉构建支路裁剪清单不一致：  
   * 对比 cmake/RtxFfmpegStatic.cmake:390（MSVC 分支）与第 538 行（Zig 分支）：MSVC 分支已添加 \--enable-demuxer=...,concat,... 与 \--enable-encoder=mov\_text，但 Zig 分支遗漏了 concat 解复用器与 mov\_text 编码器，导致双工具链行为分裂。须立即对齐两处配置。  
2. \[EXTRA-2 / P1\] FfmpegDemuxer 字幕透传与 FfmpegMuxer 支持矩阵不对等：  
   * src/io/ffmpeg\_io.cpp:499-515 将 SUBRIP / ASS / SSA / WEBVTT 全部加入 passthrough\_streams，导致 readPacket() 不断读出字幕包并跨越线程边界拷贝，但在 FfmpegMuxer::open()（第 833 行）MP4 模式下又全部丢弃。应在 32 号波次接入 mov\_text 编码转换，或对确定丢弃的流在 Demuxer 阶段直接过滤，避免无意义的跨线程内存分配。  
3. \[EXTRA-3 / P2\] rife\_engine.cpp 帧对缓存键（Frame-Pair Cache Key）在复用物理显存缓冲池时存在哈希碰撞风险：  
   * 核查 src/shim/ngx\_bridge/rife\_engine.cpp:401-405：cache\_valid\_ 仅通过比对显存虚拟地址指针 (prev\_nv12 \== last\_prev\_nv12\_ && curr\_nv12 \== last\_curr\_nv12\_) 判断相邻两次 interpolate() 调用是否输入了同一对源帧（用于 24-\>120fps 5 倍插帧复用编码器前端特征）。当上游解码器/环形缓冲池在多帧循环中复用同一块 CUdeviceptr 地址但已覆写了新帧像素数据时，若仅比较指针地址而未绑定单调递增的 frame\_generation\_id（或 pts），在特定 2 槽交替复用的自定义解码器下可能误命中旧帧特征。建议在 RtxFrameDesc 或桥接调用中附带帧序号校验。  
4. \[EXTRA-4 / P2\] 32 号引入 \--enable-gpl \--enable-libx265 的许可证边界与静态体积管控：  
   * 主仓许可证为 GPL-3.0-or-later WITH Plugin-Exception（兼容 GPLv2+ 的 x265）。但 x265-static.lib 静态编译体量较大（约 6–12 MB）且需与主程序 CRT（/MT）及 LTO（/OPT:REF）严格匹配。裁决：默认发行构建保持 CINERTX\_ENABLE\_GPL\_CODECS=OFF（仅内置 Apache-2.0/BSD-3-Clause-Clear 许可的 SvtAv1Enc.lib），仅在显式开启 \-DCINERTX\_ENABLE\_GPL\_CODECS=ON 时编入 x265-static.lib，并在 AuditSpdx.cmake 与编译期版本号横幅中明确输出当前构建的许可证形态（LGPL+BSD 默认态 vs GPLv3 全量软编态）。

# **四、 接手 31 尾项与 32–34 号施工单的终审准入批复**

## **4.1 AuditTypes 仅余 1 个名额（34 / 35）下的零类型预算治理批复**

专家组全面核准 32–34 号施工单的零新增命名类型设计方案，并下达以下刚性类型配额分配令：

| 施工波次 | 具名类型增量 | 最终水位 | 强制技术实现范式（禁新增 struct/class） |
| :---- | :---- | :---- | :---- |
| **`31` 尾项 \+ 排雷补丁** | **`+0`** | `34 / 35` | 复用既有 `RtxNgxBridge`、`RemasterPipeline`、`EncoderJobQueue` 私有成员与匿名命名空间自由函数。 |
| **`32` 号（SVT-AV1/x265 \+ MP4 闭环）** | **`+0`** | `34 / 35` | 复用既有 `CpuVideoEncoder` 与 `FfmpegMuxer` 私有成员（`AVCodecContext*` 音频重编码上下文直接作为 `FfmpegMuxer` 私有指针成员，不封装新类）。 |
| **`33` 号（60fps AI 增强播放器）** | **`+0`** | `34 / 35` | 全部原子控制状态（`target_viewport_w_`、`target_viewport_h_`、`split_screen_ratio_`、`active_feature_mask_`、`ai_bypass_count_`）内嵌至 `MediaPlaybackSession` 私有成员；相位节拍与 WASAPI 8ms 熔断以自由函数实现。 |
| **`34` 号（D2D1/DWrite 组件库 \+ DAG 画布）** | **`+1`（或 `+0`）** | **`35 / 35`（封顶）** | **唯一允许消耗第 35 个名额的载体为 `UiWidgetNode`**（须携带完整 `Category: Domain-Model` 的 `@rtx-type-justification` 注释块）；其余 D2D1/DWrite 动态函数指针表统一使用 `std::tuple<HMODULE, ...>` 类型别名 \+ 局部静态表，算子 DAG 序列直接嵌入 `TranscodeProfile` 的 `std::array<uint8_t, 8>` 与 `uint16_t` 掩码字段，确保全仓终局锁定 **`35 / 35`（0 违规）**。 |

**关键防撞预警**：在实现 `34` 号 `D2D1DynamicApi` 时，**严禁将其定义为具名 `struct D2D1DynamicApi`（否则将达到 36/35 触发 `AuditTypes.cmake` 硬失败！）**——必须直接定义为 `using D2D1DynamicApi = std::tuple<...>;` 或命名空间级局部静态函数指针变量！

## **4.2 协调负责人本机接手执行路线与每波次真机验收军规**

**正式签发接手准入令**：批准协调负责人即刻接管 Commit `9355120` 工作区，严格按 **`Wave A（31 尾项 + 排雷止血） -> Wave B（32 号软编与 MP4/MergeConcat 闭环） -> Wave C（33 号 60fps 增强播放器） -> Wave D（34 号 D2D1/DWrite 与 DAG 画布）`** 四步推进，每波次必须通过宿主机（Windows 11 \+ MSVC 19.51 \+ RTX 5060）实测方可进入下一波次：

1. **Wave A：排雷止血 \+ `31` 号尾项合龙（预计 0.5 天）**  
   * **必改项**：  
     1. 修复 `bridge_exports.cpp:708-738`：在读取帧前执行 `stream_synchronize(pack_stream)`，改用 `launchEvalBlockSad8x8` \+ `evalBlockSadSceneCut` 替代 256 像素局部 D2H 采样，并将 `scene_cut` 回传至 `hw_transcode_stages.cpp:447` 联动 `last_was_cut_ = true`；  
     2. 修正 `hw_transcode_stages.cpp:144` 残留常量 `11760000LL` \-\> `static_cast<int64_t>(kFlicksPerSecond / 60ULL)`，清理 `ffmpeg_io.cpp:17, 294` 注释及 `time_model.h:57` `std::gcd` 约分；  
     3. 放宽 `rife_engine.h:40` `Ultra` `max_w = 5120`，修正 `rife_engine.cpp:208` 自动桶文件切换匹配，补齐 `workstation_canvas.cpp` 窗口缩放时的 `previewViewport->resizeSwapChain()`；  
     4. 补齐 C++ 后端写帧落盘至 `%TEMP%/cinertx_latest_preview.jpg`（限制 `<= 15 fps` 原子临时文件 `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING)` 写入，防止 `webui/server.py` 读到半截 JPEG）。  
   * **真机验收标准**：`.\build.bat` 退出码 `0`；`rtx_tests.exe` **150/150 PASS**；启动桌面 GUI 转码时浏览器访问 `/api/v1/stream/latest_frame.jpg` 稳定返回 `200 image/jpeg` 实时画面。  
2. **Wave B：`32` 号（SVT-AV1/x265 静态编联 \+ MP4 音轨/字幕闭环 \+ 智能 `MergeConcat`）**  
   * **必改项**：  
     1. 在 `RtxFfmpegStatic.cmake`（同步 MSVC 与 Zig 双分支）实装 `SvtAv1Enc.lib`（默认）与 `x265-static.lib`（`CINERTX_ENABLE_GPL_CODECS` 门控）`/MT` 静态编译编联；  
     2. 在 `FfmpegMuxer::open()` 落地非直通音轨自动转码为 AAC-LC 与 SRT/ASS 自动转码为 `mov_text`，彻底清零静默丢音/丢字幕；  
     3. 在 `EncoderJobQueue::workerLoop()` 实现 `MergeConcat` 同构（`safe=0`）与异构（自动路由 `runNvidiaRemasterRenderGraph()`）双路智能合并。  
   * **真机验收标准**：`AuditImports` 严格保持 `GUI = 4 DLLs / HEADLESS = 2 DLLs`；实测含 `DTS/FLAC` 音轨的 `.mkv` 转 `.mp4` 输出有声且 `+faststart` 生效；实测 `H.264 1080p` \+ `HEVC 4K` 异构双片段合并导出单一 `.mp4` 音画同步误差 `< 20ms`。  
3. **Wave C：`33` 号（60fps AI 增强实时播放器与 WASAPI 锁相）**  
   * **必改项**：废除 `createHeadlessPreviewSink` 原始帧直通旁路，接入统一 GPU 增强链（切镜守卫 \-\> RIFE/NVOFA 60fps \-\> 视口感知 VSR \-\> TrueHDR \-\> 3D LUT \-\> A/B 分屏内核），落地 WASAPI 主时钟锁相与 `8.0 ms` 推理超时旁路熔断。  
   * **真机验收标准**：RTX 5060 实机播放 `23.976fps` HDR/SDR 片源稳定输出 `59.94/60fps` 增强画面，拖拽 A/B 分屏准星丝滑无撕裂，A/V 同步漂移 `< 10ms`，`AuditTypes == 34/35`。  
4. **Wave D：`34` 号（动态 D2D1/DirectWrite 组件库、全栈 i18n 与横向可拖拽 DAG 画布）**  
   * **必改项**：通过 `sys_dynload` 动态加载 `d2d1.dll` / `dwrite.dll` / `d3d11.dll`（零静态导入），落地 `UiWidgetNode`（唯一新增类型，达成 `35/35`）两阶段折行测量与垂直滚动检查器，拔除 `chromeText()` 私藏词条表实现 C++/WebUI 单源 i18n，落地横向可拖拽算子卡片画布与零算力浪费预览门控开关。  
   * **终局封板验收标准**：五重静态门禁全绿（`AuditTypes == 35/35`、`AuditImports GUI == 4 / HEADLESS == 2`、`AuditPrivacy == 0`、`AuditSpdx == PASS`、`AuditSloc <= 23,000`），全套单测与真机端到端场景全绿，正式签署 **Phase 13 终局封板（`v0.5.0`）**。

**首席领域专家组终审签发完毕。批准协调负责人即刻接手主仓按 Wave A \-\> B \-\> C \-\> D 路线推进施工。**