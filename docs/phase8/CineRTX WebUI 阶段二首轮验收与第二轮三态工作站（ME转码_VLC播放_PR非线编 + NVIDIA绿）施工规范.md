# CineRTX WebUI 阶段二首轮验收与第二轮三态工作站（ME 转码器 / VLC 播放器 / PR 非线编 + NVIDIA 绿）施工规范

**文档标识**：`DOC-SUPERPOWER-STAGE2-ROUND2-WEBUI-THREE-MODES-NVGREEN-004`  
**演进阶段**：Stage 2 · WebUI 第二轮迭代（首轮 SSOT 管道化验收通过 $\to$ 本轮升级 **NVIDIA 绿工业视觉体系 + ME/VLC/PR 三大工作站形态**）  
**遵循方法论**：`SuperPower` 工程施工范式（`Plan` + `Tasks` + `Checklists` 三位一体，遵循 KISS、奥卡姆剃刀、高可读高可维护原则）  
**关联基准文件**：
- [`webui/server.py`](../../webui/server.py)
- [`webui/index.html`](../../webui/index.html)
- [`webui/app.js`](../../webui/app.js)
- [`webui/app.css`](../../webui/app.css)
- [`webui/state.json`](../../webui/state.json)
- `webui/ACCEPTANCE_STAGE2.md`
**适用角色**：WebUI 前端工程师（独占 `webui/` 泳道；C++ 后端接口已冻结，Win32 GUI 与 Stage 3 待 WebUI 多轮定型后再启动）

---

## 第一部分：WebUI 阶段二首轮交付专家验收裁决（Audit Verdict）

### 1.1 首轮底层契约对齐验收结论：**底层管道与 SSOT 对接通过（PASS），工程卫生与视觉形态进入第二轮迭代**

| 验收维度 | 首轮实测表现（代码级证据） | 裁决结论 |
| :--- | :--- | :---: |
| **1. C++ SSOT 零重复对接 (`--schema-json`)** | [`webui/app.js`](../../webui/app.js)（第 28–60、754 行）已彻底删除原 236 行硬编码 `I18N` 字典，通过 `GET /api/schema` 动态拉取 113 项双语词条、19 项参数默认值与 `[min, max]` 范围；[`webui/state.json`](../../webui/state.json) 已统一 `qp: 24` 与 `vsrEnabled: true`。 | ✅ **PASS** |
| **2. 原生 JSON 会话与协作取消 (`--session-stdio`)** | [`webui/server.py`](../../webui/server.py)（第 77–130 行）已彻底拔除原 10 条正则刮削与 `build_argv`，改用 `Popen([engine, "--session-stdio"])` 发送 `state.json` 并按行消费 `progress` / `log` / `report` JSONL；`cancel()`（第 103–114 行）通过 `stdin` 写入 `"cancel\n"` 并等待 5 秒完成 MP4 `moov` 优雅写尾。 | ✅ **PASS** |
| **3. 8-bit SDR H.264 拔除与乱码修复** | [`webui/index.html`](../../webui/index.html)（第 53–56、115、135 行）已删除 `h264` 选项与 3 处 `data-disable-when="profile.codec=h264"`，补齐了 `vsrEnabled` 复选框与 RIFE 浏览按钮；`app.js:650` 乱码已修复为 `\uD83D\uDCC1` / `\uD83D\uDCC4`。 | ✅ **PASS** |
| **4. 代码可读性与目录卫生（需在本轮修正）** | ① **目录脏文件残留**：`webui/` 根目录下混入了工程师对话过程文本 `webui/report.md.txt`，且并存新旧两份验收文档（`ACCEPTANCE.md` 与 `ACCEPTANCE_STAGE2.md`）；<br>② **为凑行数牺牲可读性**：`server.py` 为了强压到 220 行，使用了大量单行分号拼接（如第 17、26、31、50、60、81、96、100、128 行）并删掉了外部 `state.json` 文件监听，违背了项目“高可读、高可维护”的核心约定；<br>③ **HTML 静态属性微瑕**：`index.html:169` 的 `ctl-gpu` 静态属性仍写着 `max="31"`（应改为 `max="15"`）。 | ⚠️ **本轮修正** |

> [!IMPORTANT]
> **给前端工程师的代码风格纠偏指令**：
> 绝不要为了迎合机械的“行数上限”而把多条语句用分号 `;` 挤在一行！我们追求的精简是**架构级消除重复逻辑（不写重复字典、不写正则刮削、不重复造轮子）**，而不是压缩换行符的“代码高尔夫（Code Golf）”。`webui/server.py` 请恢复标准、清晰、易读的 PEP 8 多行排版（控制在 260～340 行以内完全合理）。

---

## 第二部分：Plan（第二轮顶层规划：NVIDIA 绿工业主题 + 三大工作站形态架构）

### 2.1 核心产品愿景与定位
`CineRTX` 的内核是一套基于 NVIDIA RTX GPU（NVDEC + NVOFA/TensorRT RIFE + NGX VSR + TrueHDR + NVENC 10-bit）的超分重制与非编引擎。WebUI 作为先行的视觉与交互基准（定型后再向纯 Win32 GUI 对齐），必须具备纯正的 **NVIDIA 工业级专业软件质感**，并在同一个单页应用（SPA）中通过顶部 **工作区形态切换器（Workspace Switcher）** 原生提供三套对标行业标杆的工作形态：

1. **形态一（核心基本盘 / 默认主形态）：视频转码器形态 — 对标 Adobe Media Encoder (ME) (`data-workspace="encoder"`)**
   - **定位**：本项目的最根本、最核心形态，也是整个引擎的算力内核。无论是单文件重制、批量队列转码，还是非线编形态最终的渲染交付，全部汇聚于此形态执行。
   - **标杆特征**：左侧 **RTX 母版预设库（Preset Browser）** + 中上部 **多任务批处理队列（Batch Queue Table）** + 中下部 **实时编码遥测与 A/B 对比监看舱（Encoding Monitor & Telemetry）** + 右侧 **19 项 SSOT 深度参数检查器（Parameter Inspector）**。
2. **形态二：AI 超分重制播放器形态 — 对标 VLC Media Player (`data-workspace="player"`)**
   - **定位**：面向原片审看、重制前后画质实时对比（A/B Split Wipe）与多轨音字幕检视的沉浸式影院播放器。
   - **标杆特征**：全幅沉浸式 **16:9 / 9:16 自适应视频舞台（含可拖拽 A/B 分屏对比滑杆与左上角 GPU 实时 OSD 遥测浮层）** + 底部 **VLC 风格播控栏（逐帧进退 `-1f/+1f`、Flicks 精确时间轴拖拽、实时 VSR/TrueHDR/FRUC 档位快捷胶囊、多音轨/内挂字幕切换器、一键送入转码队列按钮）**。
3. **形态三：非线性编辑软件形态 — 对标 Adobe Premiere Pro (PR) (`data-workspace="nle"`)**
   - **定位**：面向多镜头剪辑、705,600,000 Hz `Flicks` 零漂移时间轴编排、3D `.cube` LUT 调色与 HDR10/HDR10+ 母版精修的专业非线编工作站。
   - **标杆特征**：经典 **PR 四象限工业布局**——左上 **项目素材箱与 LUT 特效库（Project Media Bin & Effects）**、中上 **源/节目双模式监视器（Source / Program Monitor）**、右上 **特效控制台与 Lumetri/RTX 调色面板（Effect Controls & RTX HDR Inspector）**、底部通栏 **多轨 Flicks 时间轴与剪辑工具栏（Multi-Track Timeline：`V2 调色层` + `V1 视频主轨` + `A1/A2 多轨音频` + `S1 字幕轨`，支持入点 `I` / 出点 `O` / 剃刀分割 `C` 及一键发送至 ME 队列渲染）**。

### 2.2 架构铁律：继续保持“4 文件前端 + 1 个 JSON 状态源 + 零外部依赖”
三套工作形态**绝不是拆成三套互不相通的网页**，而是共用同一套 `state.json` 状态树、同一套 `kParamSchema` 参数契约与同一套 DOM 双向绑定引擎：
- **唯一顶层视图切换属性**：在 `<body data-status="idle" data-workspace="encoder">` 上通过 `state.ui.workspace`（`"encoder"` | `"player"` | `"nle"`）驱动顶栏 Tab 高亮与主工作区面板显隐；
- **数据流无缝贯通**：
  - 在 **播放器形态（VLC）** 中打开并审看的视频，点击“送入转码器”即可直接填入 `profile.input` 或追加至 `queue` 并切回 **转码器形态（ME）**；
  - 在 **非线编形态（PR）** 中剪辑、打入出点（In/Out）、挂载 `.cube` 3D LUT 与设定 Peak Nits 后的时间轴序列，点击“导出至队列（Queue to Media Encoder）”即可将时间轴片段参数转化为标准转码作业送入 **转码器形态（ME）** 渲染输出！

---

## 第三部分：Tasks（前端工程师原子级施工任务集）

### Task 1：全面升级 NVIDIA 标志性绿色（`#76B900`）工业暗黑主题（`webui/app.css`）
- **目标位置**：[`webui/app.css`](../../webui/app.css)
- **施工动作**：
  1. **彻底替换 `:root` 中的蓝色系变量（`#4aa3ff` / `#2f7fd6` / `#10233c`），确立纯正 NVIDIA GeForce / Studio 工业绿暗色令牌体系**：
     ```css
     :root {
       /* Carbon-Black NVIDIA Studio Workstation Surfaces */
       --bg: #0b0d0b;
       --panel: #131613;
       --panel-2: #1a1e1a;
       --panel-header: #161a16;
       --ctrl: #222722;
       --ctrl-hover: #2b322b;
       --line: #2c332c;
       --line-strong: #3d473d;

       /* Typography */
       --text: #eef2ee;
       --text-dim: #929c92;
       --text-muted: #646e64;

       /* NVIDIA Signature Green Brand Palette */
       --accent: #76b900;
       --accent-hover: #8ad000;
       --accent-dim: #4e7a00;
       --accent-soft: rgba(118, 185, 0, 0.16);
       --accent-glow: rgba(118, 185, 0, 0.35);
       --on-accent: #070d00;

       /* Semantic Telemetry Tokens */
       --ok: #76b900;
       --warn: #f5a623;
       --err: #ff4d4f;
       --hdr-badge: #ffd166;
       --mono: "Cascadia Mono", Consolas, "JetBrains Mono", monospace;
       --radius: 6px;
     }
     ```
  2. **升级主按钮、进度条、模式切换胶囊与焦点的视觉质感**：
     - `.btn-primary`：背景 `var(--accent)` (`#76b900`)，文字颜色 `var(--on-accent)` (`#070d00`)，字重 `700`，悬停背景 `var(--accent-hover)` (`#8ad000`) 并带微光 `box-shadow: 0 0 12px var(--accent-glow)`；
     - `.progress-fill` 与 `body[data-status="running"] .progress-fill`：改为 `linear-gradient(90deg, var(--accent-dim), var(--accent), var(--accent-hover))`；
     - 顶部导航栏 `.topbar`：左侧增加 NVIDIA 风格斜切绿色几何徽标（纯 CSS/内联 SVG 构成的 `RTX` 绿色角标），中央放置三态工作站切换器 `.workspace-tabs`（激活态底部 `2px solid var(--accent)` + 绿色高亮标签）。

---

### Task 2：扩展 `state.json` 与 `server.py` 本地媒体流路由（支撑三态协同与视频流预览）
- **目标位置**：[`webui/state.json`](../../webui/state.json)、[`webui/server.py`](../../webui/server.py)
- **施工动作**：
  1. **清理 `webui/` 目录垃圾文件**：
     - 物理删除 `webui/report.md.txt`；
     - 将旧 `webui/ACCEPTANCE.md` 合并入 `webui/ACCEPTANCE_STAGE2.md`（只保留一份最新验收文档）。
  2. **恢复 `webui/server.py` 的 PEP 8 高可读性排版，并新增 `GET /api/media` 本地视频流式预览接口**：
     - 展开 `server.py` 中所有用分号 `;` 挤压在一行的代码，恢复标准 Python 缩进与空行；
     - 在 `SECTIONS` 中保留 `("ui", "profile", "engine", "queue", "player", "nle")` 持久化支持；
     - 新增 `GET /api/media?path=<urlencoded_path>` 路由（约 30 行标准库代码）：支持 HTTP `Range: bytes=start-end`（`206 Partial Content`，`Content-Type: video/mp4` 或 `video/x-matroska`），使 **播放器形态（VLC）**、**非线编形态（PR）** 与 **转码器监看视口（ME）** 中的 HTML5 `<video>` / `<canvas>` 能够直接流式加载并逐帧拖拽本机磁盘上的原片与转码成品视频！
  3. **扩展 `webui/state.json` 结构**：
     - 在 `ui` 下新增 `"workspace": "encoder"`（可选 `"encoder"` | `"player"` | `"nle"`）；
     - 新增顶层 `"queue": []`（ME 批处理作业队列，每项含 `{id, input, output, presetName, codec, resolution, fpsMode, peakNits, status, pct, fps}`）；
     - 新增顶层 `"player": {"input": "", "abMode": "split", "splitPos": 50, "osd": true, "loop": true, "speed": 1.0, "audioTrack": 0, "subTrack": -1}`；
     - 新增顶层 `"nle": {"sequenceName": "4K_60FPS_HDR10_Master_Seq", "playheadMs": 0, "inMs": 0, "outMs": 10000, "zoom": 100, "tool": "select", "selectedClipId": "clip-1", "tracks": [...]}`（内置一组默认演示轨道结构 `V2 调色层` / `V1 视频主轨` / `A1 主音轨` / `S1 字幕轨`，打开非线编模式即可立即看到完整多轨时间轴并可交互操作）。

---

### Task 3：构建三大工作站形态 DOM 布局与交互闭环（`webui/index.html` + `webui/app.js`）

#### 3.1 顶部全局工作站导航栏（Global Workstation Topbar）
在 `index.html` 的 `<header class="topbar">` 中布局三大区域：
- **左区（Brand & GPU Chip）**：`NVIDIA RTX` 绿标徽章 + `CineRTX` 标题 + 后端引擎在线胶囊 `#engine-chip`；
- **中区（3-Mode Workspace Switcher）**：三个工业级切换 Tab（支持快捷键 `Alt+1` / `Alt+2` / `Alt+3`）：
  1. `[1] 视频转码器 · Media Encoder`（`data-workspace-tab="encoder"`，带 `CORE` 核心徽章）
  2. `[2] 超分播放器 · RTX Player`（`data-workspace-tab="player"`）
  3. `[3] 非线性编辑 · RTX NLE Studio`（`data-workspace-tab="nle"`）
- **右区（Global Actions）**：硬件探针快捷按钮 `#btn-probe` + 语言切换 `#btn-lang` (`EN/中文`)。

#### 3.2 形态一（核心形态）：视频转码器工作站 — 对标 Adobe Media Encoder (`#ws-encoder`)
作为项目的根基与默认视图，采用 **ME 专业三栏四区布局**：
1. **左栏：RTX 黄金预设浏览器（Preset Browser，宽 `240px`）**：
   - 提供 5 个一键应用预设卡片（点击后自动设置 `state.profile` 对应参数并触发 `runEffects()`）：
     - `★ 4K 60fps AV1 HDR10 母版 (默认开箱最优)`：`codec="av1", preset=5, resolution="4k", vsrEnabled=true, vsrQuality=4, fpsMode="60-nvofa", peakNits=1000`
     - `◆ 4K 60fps RIFE v4.25 深度学习臻彩`：`codec="av1", preset=6, resolution="4k", vsrEnabled=true, vsrQuality=4, fpsMode="60-rife", peakNits=1000`
     - `⚡ 4K 120fps NVOFA 电竞高帧率`：`codec="av1", preset=4, resolution="4k", vsrEnabled=true, vsrQuality=4, fpsMode="120-nvofa", peakNits=1000`
     - `🎬 2K 60fps HEVC Main10 广泛兼容`：`codec="hevc", preset=5, resolution="2k", vsrEnabled=true, vsrQuality=3, fpsMode="60-nvofa", peakNits=1000`
     - `🎞 原生分辨率 TrueHDR 无损重制`：`codec="av1", preset=6, resolution="native", vsrEnabled=false, fpsMode="original", peakNits=1000`
2. **中栏上区：ME 多任务批处理队列表（Batch Encoding Queue Table）**：
   - 队列工具栏：`+ 添加当前配置到队列`、`📂 批量导入文件`、`移除选中`、`清空已完成`、右侧醒目的 NVIDIA 绿主按钮 **`▶ 开始硬件转码 / 启动队列 (#btn-start)`** 与 **`■ 停止转码 (#btn-stop)`**；
   - 队列表格列：`状态图标` | `输入源文件名` | `编码预设 (AV1/HEVC · 4K · 60fps)` | `FRUC 引擎 (NVOFA/RIFE)` | `HDR 峰值 (nits)` | `输出路径` | `实时进度条`；
   - 点击队列中的任意行，右侧参数检查器即同步展示/编辑该任务的 `profile`；若队列为空时直接点击“开始硬件转码”，则直接以当前 `state.profile` 单任务立即启动（保持 100% 向后兼容！）。
3. **中栏下区：实时编码监看与 WDDM 零拷贝遥测舱（Encoding Monitor & Telemetry）**：
   - 包含 **实时 A/B 对比预览小视口**（显示原画 SDR vs 重制 HDR10 示意/视频帧）+ **4 块 NVIDIA 绿遥测数字磁贴（`#tile-fps`、`#tile-frames`、`#tile-elapsed`、`#tile-zero`）** + **任务进度条 `#progress-fill`** + **硬件探针详情 `#probe-view` / 任务报告 `#report-view` / 实时引擎日志 `#log-view`**。
4. **右栏：SSOT 19 项转码参数检查器（Parameter Inspector，宽 `380px`）**：
   - 完整容纳现有的 19 个 `[data-bind="profile.*"]` 控件（分组为：`输入/输出路径`、`编码器与码率控制`、`超分画幅与帧率补帧`、`TrueHDR 色彩科学与硬件绑定`），并将 `ctl-gpu` 的 HTML `max` 属性修正为 `"15"`。

#### 3.3 形态二：AI 超分重制播放器 — 对标 VLC Media Player (`#ws-player`)
采用 **沉浸式黑曜石影院布局**：
1. **中央主舞台：交互式 A/B 蝴蝶分屏对比视口（A/B Split Remaster Stage）**：
   - 底层挂载双层同步视图（或 `<video id="player-video">` + 实时 CSS/Canvas HDR10 & VSR 锐化对比层），中间有一条带 NVIDIA 绿手柄的可左右拖拽分屏线（`data-bind="player.splitPos"`，`0%..100%`）；
   - 左侧角标 `ORIGINAL · SDR BT.709`，右侧角标 `RTX REMASTER · 4K HDR10 PQ + VSR L4 + 60FPS`；
   - 左上角悬浮 **VLC 工业级 GPU OSD 遥测浮层**（显示当前媒体路径、分辨率、目标帧率、VSR 档位、TrueHDR 峰值亮度 Nits、零拷贝状态，可按 `O` 键或按钮开关）。
2. **底部双层 VLC 播控台（Transport & Real-Time RTX Shader Bar）**：
   - **第一层（全宽时间轴 Scrubber）**：高精度进度滑轨 + 当前时间码 `00:00:00.000` / 总时长 + 帧号显示；
   - **第二层（播控按钮与实时增强拨盘）**：
     - 左组：`📂 打开媒体`（复用 `/api/fs` 选择器并自动挂载至 `/api/media?path=...`）、`⏮ 跳至片头`、`⏪ -1 帧`、`▶/⏸ 播放/暂停`、`⏩ +1 帧`、`⏹ 停止`；
     - 中组（实时 RTX 增强快速切换）：分屏模式切换（`A/B 分屏` | `仅看重制` | `仅看原片`）、实时 VSR 档位胶囊（`L1..L4`）、补帧模式切换（`NVOFA 60` | `RIFE 60` | `120 FPS`）、倍速选择（`0.25x` .. `2.0x`）、音轨/字幕轨下拉框；
     - 右组：**`⚡ 将当前媒体送入转码器 (Send to Media Encoder)`** 按钮（点击后自动将当前播放文件路径写入 `state.profile.input`，推导 `profile.output`，并一键切换至 `encoder` 形态）。

#### 3.4 形态三：非线性编辑软件工作站 — 对标 Adobe Premiere Pro (`#ws-nle`)
采用 **Premiere Pro 经典四象限专业工作站网格布局**：
1. **左上象限（Project Bin & LUT Library · 项目素材箱与特效库）**：
   - 顶部 Tab：`项目素材 (Media Bin)` | `3D LUT 调色预设 (.cube)` | `RTX 算子特效`；
   - 支持点击 `+ 导入素材`（调用 `/api/fs`）将视频加入素材箱，点击素材旁 `+ 上轨` 即可追加到时间轴 `V1` 轨道。
2. **中上象限（Source / Program Monitor · 源与节目监视器）**：
   - 顶部显示 `节目: 4K_60FPS_HDR10_Master_Seq` 及精确到 **Flicks (`705,600,000 Hz`)** 的双制式工业时间码（如 `00:00:04:12 · 2,963,520,000 Flicks`）；
   - 视频画面预览舞台 + 监视器下方标记栏：`[ 标记入点 (I)`、`标记出点 (O) ]`、`⏪ -1f`、`▶ 播放`、`⏩ +1f`、`✂ 在播放头处分割 (Razor)`。
3. **右上象限（Effect Controls & Lumetri / RTX Remaster Inspector · 特效控制台与调色面板）**：
   - 选中时间轴片段时，实时显示并编辑该片段的：
     - **时间裁剪（Timing）**：入点 `In (ms)`、出点 `Out (ms)`、帧率上限 `profile.frames`；
     - **RTX AI 超分与补帧（RTX VSR & FRUC）**：绑定 `profile.vsrEnabled`、`profile.vsrQuality`、`profile.fpsMode`、`profile.rifeEngine`；
     - **Lumetri / TrueHDR 色彩科学（Color & HDR10）**：绑定 `profile.peakNits`（`400..4000 nits` 滑块+数值）、`profile.hdr10plus`、`profile.lut`（`.cube` 3D LUT 路径选择）；
   - 底部常驻 NVIDIA 绿核心动作按钮：**`🚀 导出序列至转码器渲染 (Export to Media Encoder)`**（点击后将当前时间轴主素材与调色/超分参数同步至 `state.profile` 并加入 ME 队列，自动切至 `encoder` 形态启动渲染）。
4. **底部通栏象限（Multi-Track Flicks Timeline · PR 多轨时间轴）**：
   - **左侧垂直工具坞（Tool Dock）**：选择工具 (`V`)、剃刀切割工具 (`C`)、吸附开关 (`Snap`)、时间轴缩放滑杆 (`Zoom`)；
   - **右侧多轨时间轴画布（Timeline Tracks）**：
     - 顶部时间标尺（Time Ruler，带入出点高亮区间条与可拖拽的 **NVIDIA 绿播放头 Playhead**）；
     - 4 条标准专业轨道（含轨道头静音 `M` / 锁定 `L` / 可见性 `👁` 开关与彩色轨道块）：
       - `V2 · LUT & HDR10+ 调色调整层 (Adjustment Layer)`
       - `V1 · 4K VSR + FRUC 视频主轨 (Video Master)`
       - `A1 · 多声道无损直通音轨 (Audio Stream Copy)`
       - `S1 · 内挂字幕直通轨 (Subtitle Passthrough)`
     - 支持点击/拖拽时间轴任意位置移动播放头、点击剃刀工具把选中 Clip 一分为二、拖拽 Clip 边缘调整入出点。

---

## 第四部分：Checklists（第二轮量化验收门禁清单）

前端工程师完成本轮施工后，必须逐项通过以下门禁验证：

- [ ] **门禁 R2.1（NVIDIA 绿品牌视觉体系纯净度门禁）**：
  - 验证方法：扫描 `webui/app.css` 与 `webui/index.html`
  - 通过标准：`--accent` 严格为 `#76b900`（NVIDIA Green），原蓝色系（`#4aa3ff`、`#2f7fd6`、`#6ab5ff`、`#10233c`）在全目录 **0 命中**。
- [ ] **门禁 R2.2（三大工作站形态完整结构与切换闭环门禁）**：
  - 验证方法：启动 `server.py` 并在 Headless / 浏览器中依次切换 `encoder`（ME 转码器）、`player`（VLC 播放器）、`nle`（PR 非线编）三种形态
  - 通过标准：
    1. `encoder` 形态具备：5 项预设浏览器、多任务队列表、实时遥测面板、19 项 SSOT 参数检查器；
    2. `player` 形态具备：可拖拽 A/B 分屏对比视口、GPU OSD 浮层、逐帧步进播控栏、`/api/media` 流式播放与“送入转码器”联动按钮；
    3. `nle` 形态具备：素材箱、源/节目监视器（含 `Flicks` 时间码）、Lumetri/RTX 调色控制台、4 轨（`V2/V1/A1/S1`）可交互时间轴（支持播放头定位与剃刀分割）及“导出序列至转码器”联动按钮。
- [ ] **门禁 R2.3（阶段二底层 SSOT 与协作取消零退化门禁）**：
  - 验证方法：运行更新后的自动化验收脚本 `python webui/test/verify_stage2.py`
  - 通过标准：原有门禁 2.1（19 项参数与 113 项双语词条同源）、门禁 2.2（零硬编码字典、零正则刮削、零乱码）、门禁 2.3（`--session-stdio` 协作取消生成含完整 `moov` 的 MP4）**100% 继续保持 PASS**。
- [ ] **门禁 R2.4（工程卫生与代码可读性门禁）**：
  - 验证方法：检查 `webui/` 目录文件列表与 `webui/server.py` 代码风格
  - 通过标准：`webui/report.md.txt` 已物理删除；`webui/server.py` 恢复标准多行 PEP 8 格式（无多语句单行分号挤压）；`index.html` 中 `ctl-gpu` 的 `max="15"` 与后端严格一致。
