# CineRTX WebUI (`v0.5.0`)

浏览器端的专业转码控制台，与统一单入口 `CineRTX.exe`（Win32 原生 GUI / 无头 CLI 一体化单 EXE，物理静态 IAT 仅 `kernel32.dll` 1 项）并存的前端形态，面向远程与无桌面算力节点场景。

## 文件边界

**WebUI 本体 = 恰好 4 个文件**（本目录下的其他文件都是后端适配器或文档/测试，不属于 WebUI 本体）：

| 文件 | 角色 |
| --- | --- |
| `index.html` | 唯一界面文档；每个控件用 `data-bind` 声明状态路径、`data-i18n` 声明词条 |
| `app.js` | 唯一脚本：响应式 store + 双向绑定引擎 + i18n 表（与 `include/rtx/i18n.h` 129 项 `TrKey` 词条 SSOT 对齐）+ SSE 客户端 |
| `app.css` | 唯一样式：暗色专业工作站主题（色板对齐原生 GUI 的碳黑 `#121212` 与 NVIDIA Signature Green `#76B900`） |
| `state.json` | **前端状态持久化层** = 单一 JSON 状态文档（SSOT），UI 与它 1:1 双向绑定 |

**后端侧适配器（非 WebUI 本体）**：`server.py` — 零依赖 stdlib JSON 桥。前端与后端的全部通信是它承载的 JSON；argv 翻译与 `--session-stdio` 引擎交互细节只存在于这一层，前端对引擎底层零知识。

## 运行

```bash
# 真实引擎（自动按 state.json 的 engine.path / CINERTX_ENGINE / exe 同级目录解析）
python webui/server.py --port 8787
# 显式指定引擎与状态文档
python webui/server.py --engine .vs/build/cmake/windows-msvc-ninja/CineRTX.exe --state webui/state.json
# 无 RTX 硬件的机器上端到端联调（夹具引擎，模拟 CLI 输出契约）
python webui/server.py --engine webui/test/mock_cinertx.bat
```

浏览器打开 `http://127.0.0.1:8787/`。

## JSON 通信契约（前端 ↔ 桥）

| 路由 | 方向 | 载荷 | 说明 |
| --- | --- | --- | --- |
| `GET /api/state` | 拉取 | 完整状态文档（`ui/profile/engine/queue/player/nle` + 服务器所有的 `runtime`） | 页面初始化与状态轮询 |
| `PUT /api/state` | 持久化 | `{ui, profile, engine}` | 服务端校验范围/枚举后原子写盘；`queue/runtime` 由服务端独占，消除双写竞态 |
| `GET /api/schema` | 查询 | 20 项参数元数据与取值范围 | C++ SSOT 参数契约反射 |
| `POST /api/probe` | 动作 | 执行 `--probe --json`，原生 JSON 原样入库 `runtime.probe` | 媒体探针探测元数据与流结构 |
| `POST /api/job` | 动作 | `{action: "enqueue"\|"remove"\|"clear_completed"\|"retry"\|"move"\|"start"\|"cancel"}` | 7 类动作全生命周期队列调度引擎 |
| `GET /api/events` | 订阅 | SSE；每个事件都是一个 JSON 文档：`hello` / `runtime`（进度、报告、日志，≤4Hz）/ `state-file`（外部改盘推送） | 实时遥测事件流 |
| `GET /api/fs?path=` | 浏览 | 通用 JSON 目录列表（路径选择弹窗） | 服务端跨盘符目录树浏览 |
| `GET /api/media` | 媒体 | HTTP 206 断点续传视频流 | 原生 HTML5 `<video>` 低延迟视频播放与时间轴拖拽 |
| `GET /api/health` | 探活 | 引擎解析结果/版本（`0.5.0`）、作业状态 | 服务端心跳与健康探测 |

## 状态文档与所有权

```
state.json
├── version  文档版本
├── ui       浏览器所有（语言/自动命名/日志显隐）
├── engine   浏览器所有（引擎路径/工作目录）
├── profile  浏览器所有（转码参数矩阵，对齐 HwTranscodeOptions）
├── queue    服务器所有（异构任务排队列表，前端只通过 /api/job 驱动变更）
├── player / nle 前端所有（播放器对比与非线编时间轴工作区状态）
└── runtime  服务器所有（探针结果、活动作业、逐帧遥测、日志尾）
```

**双向绑定的三条通道**（全部以 JSON 为介质）：

1. **DOM → state → 磁盘**：控件 `input/change` → `setPath()` → 250ms 防抖 `PUT /api/state`；前端 `ownedSections()` 仅回写 `["profile", "ui"]`，绝不回写 `queue` 与 `activeJob`，彻底杜绝双写竞态。
2. **磁盘 → state → DOM**：`server.py` 监视 `state.json`（过滤自身写入，防回写竞态）→ SSE `state-file` → 客户端应用 → 控件重渲染；
3. **引擎 → state → DOM**：`runtime` 事件（≤4Hz 节流发布、≥1Hz 落盘）→ 合并进 `state.runtime` → 进度条/遥测/报告/日志自动更新。

## C++ 20 参数 SSOT 契约规范

CineRTX 全栈以 `include/rtx/param_schema.h` 的 `kParamSchema` 为单一真实信源（SSOT），严格对齐 20 项参数元数据（当前 `v0.4.0` 核心优先承载 20 项纯 GPU 零拷贝 SSOT，CPU 软编与 CRF 码控已收录至 `docs/TODO/README.md` 远期路线）：

| 序号 | 参数名 (JSON `data-bind`) | CLI 映射 | 类型 | 默认值 | 允许取值 / 说明 |
| :---: | :--- | :--- | :---: | :---: | :--- |
| 1 | `input` | `--input` | string | `""` | 输入视频源文件路径 |
| 2 | `output` | `--output` | string | `""` | 输出文件路径（空则按 `_remaster_<codec>.mp4` 自动派生） |
| 3 | `codec` | `--codec` | enum | `"av1"` | `"av1"`, `"hevc"`；`"av1_cpu"`, `"hevc_cpu"` *(Roadmap / 规划路线)* |
| 4 | `preset` | `--preset` | int | `5` | NVENC `1..7` (`P1..P7`) / SVT-AV1 `0..13` |
| 5 | `rateControl` | `--rate-control` | enum | `"cq"` | `"cq"`, `"vbr"`, `"cbr"`, `"qp"`；`"crf"` *(Roadmap / 规划路线)* |
| 6 | `bitrateKbps` | `--bitrate` | int | `0` | `0..200000` kbps（`0` 为自动） |
| 7 | `qp` | `--qp` | int | `22` | `0..51`（CQ / QP / CRF 统一目标质量值） |
| 8 | `resolution` | `--resolution` | enum | `"4k"` | `"4k"`, `"2k"`, `"fhd"`, `"native"` |
| 9 | `geometry` | `--pillarbox` | enum | `"auto"` | `"auto"`, `"pillarbox"` |
| 10 | `vsrEnabled` | `--no-vsr` | bool | `true` | 是否启用 RTX VSR 超分辨率 |
| 11 | `vsrQuality` | `--vsr-quality` | int | `4` | `1..4`（默认 Level 4 Ultra） |
| 12 | `peakNits` | `--peak-nits` | int | `1000` | `400..4000` nits（TrueHDR 峰值亮度） |
| 13 | `fpsMode` | `--target-fps` | enum | `"60-nvofa"` | `"original"`, `"60-nvofa"`, `"120-nvofa"`, `"60-rife"`, `"120-rife"` |
| 14 | `pipelineOrder` | `--pipeline-order` | enum | `"fg-first"` | `"fg-first"`（默认先补帧后超分）, `"vsr-first"`（先超分后补帧） |
| 15 | `rifeEngine` | `--rife-engine` | string | `""` | RIFE TensorRT `.engine` 模型路径 |
| 16 | `copyAudio` | `--no-audio` | bool | `true` | 音轨与内挂字幕无损直通 |
| 17 | `hdr10plus` | `--hdr10plus` | bool | `false` | SMPTE ST 2094-40 动态元数据注入 |
| 18 | `lut` | `--lut` | string | `""` | `.cube` 3D LUT 调色文件路径 |
| 19 | `gpu` | `--gpu` | int | `0` | `0..15`（CUDA/NVENC 设备序号） |
| 20 | `frames` | `--frames` | int | `0` | `0..100000000`（帧数上限，`0` 为全片） |

## CPU / GPU 双轨编码架构与管线拓扑

1. **GPU 硬件编码轨道 (NVENC)**：
   - 编码器：`av1`, `hevc`；
   - 码控模式：解锁 `RTX_RC_CQ`（0=CBR, 1=VBR, 2=QP, 3=CQ），实现广播级感知恒定质量；
   - 显存全零拷贝闭环：NVDEC 解码 -> D3D12/CUDA Interop -> NGX VSR / RIFE 帧生成 -> NVENC 硬件编码，全程驻留显存。

2. **CPU 广播级编码轨道 (SVT-AV1 / x265)**：
   - 编码器：`av1_cpu` (SVT-AV1), `hevc_cpu` (x265)；
   - 码控模式：原生支持 `crf` 恒定质量因子与 `qp`；
   - 异步双锁页回传流水线：通过 2 槽位 `PinnedP010RingBuffer` 锁页内存，在 GPU 渲染后通过 `cuMemcpyDtoHAsync_v2` 异步拷贝至系统内存；
   - 原生 HDR10+ 注入：每帧调用 `av_frame_remove_side_data` 清理旧元数据，并注入合规的 `AV_FRAME_DATA_DYNAMIC_HDR_PLUS` SEI 载荷。

3. **`vsr-first` 4K P010 锚点环拓扑**：
   - 当 `pipelineOrder == "vsr-first"` 时：
     `Decode -> ShotGroupSegmenter (切镜判定) -> NGX VSR/TrueHDR (4K P010 锚点帧) -> RIFE/NVOFA (高分辨率过渡帧插值) -> Encode`；
   - 若检测到 Hard-Cut 切镜：向 NGX 下发 `NVSDK_NGX_Parameter_Reset` 重置时域积累，并禁止跨镜头插值，直接输出副本保护画面纯净度；
   - 当 `pipelineOrder == "fg-first"` 时：保留低分辨率插帧后整体超分辨率放大的吞吐优先管线。

## 常驻流通信机制 (`--session-stdio`)

`server.py` 通过长效常驻子进程（`CineRTX.exe --session-stdio`）实现高性能无缝调度：
- 采用多行 JSON Lines (JSONL) 标准输入/输出流通信；
- 每次分发任务向 stdin 写入单行标准 JSON 字典；
- 引擎内部循环监听并执行转码，零跨进程反复冷启动及显存上下文重建开销；
- 逐帧以 `[Transcode] {"frame":..., "pct":..., "fps":...}` 行推回，实现亚秒级遥测流式响应。
