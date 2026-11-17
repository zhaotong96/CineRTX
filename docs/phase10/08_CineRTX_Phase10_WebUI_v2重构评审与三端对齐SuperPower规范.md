# CineRTX Phase 10：WebUI v2 重构深度评审裁决书暨三端对齐 SuperPower 返工施工规范

- **文档编号**：`CINERTX-P10-WEBUI-V2-AUDIT-REWORK-008`
- **工作区路径**：`docs/phase10/08_CineRTX_Phase10_WebUI_v2重构评审与三端对齐SuperPower规范.md`
- **评审角色**：多媒体系统工程与异构计算首席领域专家
- **评审对象**：
  - [`webui/index.html`](../../webui/index.html)（`712` 行，`39,460 B`）
  - [`webui/app.js`](../../webui/app.js)（`1,623` 行，`58,818 B`）
  - [`webui/app.css`](../../webui/app.css)（`1,250` 行，`36,346 B`）
  - [`webui/server.py`](../../webui/server.py)（`547` 行，`20,086 B`）
  - [`webui/state.json`](../../webui/state.json)（`134` 行，`2,679 B`）
  - [`webui/README.md`](../../webui/README.md)（`92` 行，`4,720 B`）
- **关联架构基线**：
  - [`01_CineRTX_Phase10_架构提纯与核心算法演进SuperPower总纲.md`](01_CineRTX_Phase10_架构提纯与核心算法演进SuperPower总纲.md)
  - [`05_CineRTX_Phase10_施工单_Track_10D_异构多文件动态任务队列与三端重构.md`](05_CineRTX_Phase10_施工单_Track_10D_异构多文件动态任务队列与三端重构.md)
  - [`07_CineRTX_Phase10_专家驳回裁决暨全链路接线闭环SuperPower返工规范.md`](07_CineRTX_Phase10_专家驳回裁决暨全链路接线闭环SuperPower返工规范.md)
  - [`include/rtx/param_schema.h`](../../include/rtx/param_schema.h) / [`include/rtx/i18n.h`](../../include/rtx/i18n.h) / [`include/rtx/job_queue.h`](../../include/rtx/job_queue.h)
- **终审裁决**：**REJECT（有条件驳回验收 —— 肯定视觉与纯原生响应式底座跃迁，拦截 5 项核心架构断链与状态越权缺陷，下达 `WUI-P10-01 ~ WUI-P10-05` 强制返工规范）**

---

## 第一部分：领域专家穿透式技术评审裁决（Audit Verdict & Causal Forensics）

### 1.1 予以确认保留的工程资产（PASS 维度）

前端工程师在本轮 WebUI v2 重构中展现了扎实的原生前端工程功底，以下三项架构成果**予以正式通过并锁定为后续基线**：

| 审计维度 | 实证代码落点 | 专业评价与裁决 |
| :--- | :--- | :---: |
| **1. Obsidian + NVIDIA Signature Green (`#76b900`) 工业暗色设计系统** | [`webui/app.css:10-54`](../../webui/app.css)、[`webui/index.html:23-108`](../../webui/index.html) | 彻底清零旧版蓝色系残留，建立 6 级黑曜石表面色阶（`--bg: #080a08` 至 `--surface-5: #2e362e`）与 `--accent: #76b900` 令牌体系；升级为左侧 `220px` 导航轨（`#sidebar`）+ 顶部 `60px` 状态栏（含 SVG 环形进度指示器 `#gp-ring`），视觉层次与工业质感显著跃升。**PASS** |
| **2. 零依赖响应式 Store 与声明式双向绑定引擎** | [`webui/app.js:60-226`](../../webui/app.js) | 严守 4 文件前端边界（`index.html` / `app.js` / `app.css` / `state.json`），零第三方框架依赖；通过 `getPath` / `setPath` 与前缀匹配 `subscribe(path, fn)` 驱动 `[data-bind]`、`[data-visible-when]`、`[data-disable-when]`；`server.py` 恢复标准 PEP 8 多行可读排版并实现 `/api/media` HTTP 206 Range 流式预览。**PASS** |
| **3. 三态工作站（ME / VLC / PR）单页同构视图骨架** | [`webui/index.html:113-679`](../../webui/index.html)、[`webui/app.js:628-1428`](../../webui/app.js) | 在同一棵 `state` 状态树上贯通 `[1] Encoder`（预设库 + 队列表 + 遥测舱 + 参数检查器）、`[2] Player`（双 `<video>` A/B 分屏舞台 + OSD + 送入转码器）与 `[3] NLE`（四象限素材箱 + `705,600,000 Hz Flicks` 监视器 + Lumetri/RTX 调色台 + `V2/V1/A1/S1` 四轨时间轴）。**PASS** |

---

### 1.2 实证捕获的五大阻断级缺陷（REJECT 根因穿透取证总表）

经逐行交叉核对 [`webui/app.js`](../../webui/app.js)、[`webui/index.html`](../../webui/index.html)、[`webui/app.css`](../../webui/app.css)、[`webui/server.py`](../../webui/server.py) 与 Phase 10 C++26 核心契约（[`05_Track_10D`](05_CineRTX_Phase10_施工单_Track_10D_异构多文件动态任务队列与三端重构.md) / [`07_返工规范`](07_CineRTX_Phase10_专家驳回裁决暨全链路接线闭环SuperPower返工规范.md)），当前 WebUI v2 存在 **4 项 P0 级架构/数据完整性阻断缺陷** 与 **1 项 P1 级交互/语法缺陷**，严禁带病封板：

| 返工编号 | 严重度 | 病灶精确落点（File : Line） | 物理/逻辑根因推演（Causal Chain） | 违背的设计契约 |
| :--- | :---: | :--- | :--- | :--- |
| **WUI-P10-01** | **P0** | 1. `webui/index.html:282, 315-348, 354-408`<br>2. `webui/app.js:252, 281, 656-660`<br>3. `webui/state.json:15-17` | **Phase 10 SSOT 20 项参数与全模态编码契约缺位**：<br>① `include/rtx/i18n.h:114-125` 已新增 `OptRcCq`、`OptRcCrf`、`OptCodecAv1Cpu`、`OptCodecHevcCpu`、`ParamPipelineOrder`、`OptPipeVsrFirst`、`OptPipeFgFirst` 等 12 项词条（共 125 项），但 `index.html:282` 仍标 `19 · SSOT`；<br>② `#ctl-codec`（`index.html:315`）缺失 `av1_cpu` / `hevc_cpu`；`#ctl-rc`（`index.html:334`）缺失 `cq`（Phase 10 默认）与 `crf`；<br>③ `#ctl-qp`（`index.html:345`）显隐条件写死 `data-visible-when="profile.rateControl=qp"`，当选 `cq` 或 `crf` 时质量输入框直接消失；<br>④ 完全缺失 `pipelineOrder`（`vsr-first` / `fg-first`）下拉框；<br>⑤ `app.js:252, 281` 的 `OUTPUT_SUFFIX` 与 `looksAutoFor` 正则未处理 `av1_cpu` / `hevc_cpu`，且 `PRESET_FIELDS`（`app.js:656`）丢弃了 `rateControl`、`qp`、`pipelineOrder`。 | Track 10B / 10C / 10D 三端 SSOT 1:1 对齐铁律 |
| **WUI-P10-02** | **P0** | 1. `webui/app.js:305-308`<br>2. `webui/index.html:292-450` | **`data-lock` 运行期全局锁死参数检查器，阻断动态追加异构任务**：<br>`runEffects()` 在 `state.runtime.job.status === "running"` 时将所有带 `[data-lock]` 的 19 个参数控件及文件浏览按钮全部设为 `disabled = true`。这导致转码执行期间用户根本无法调整右侧参数或选择新文件入队，点击「添加到队列」只能重复克隆当前锁定配置。 | `05_Track_10D` §1 & §3.1：*“转码运行途中，右侧参数面板保持完全可操作，绝不锁死”* |
| **WUI-P10-03** | **P0** | 1. `webui/app.js:739-764`<br>2. `webui/app.js:796-825` | **`syncProfileToQueue` 状态越权篡改、`flushPersist` 空操作竞态与运行中误删**：<br>① **跨任务越权污染**：`syncProfileToQueue()`（`app.js:747`）未检查 `items[index].status === "pending"`。一旦用户点击过某行（如 Job #1，`selectedQueueId` 永久滞留且无取消选中机制），当 `startQueueItem()` 启动 Job #2 并执行 `setPath("profile", profile)`（`app.js:804`），或用户在预设卡/Player/NLE 修改 `state.profile` 时，`subscribe("profile")` 立即触发 `syncProfileToQueue()`，**把 Job #2 的参数和输入路径强行覆写到正在运行或已完成的 Job #1 上**！<br>② **持久化丢失竞态**：`startQueueItem()`（`app.js:802-805`）调用 `setPath("queue", items)` 和 `setPath("profile", profile)` 后**未调用 `schedulePersist()`**，此时 `persistTimer === null`，导致第 805 行 `await flushPersist()` 成为 100% 空操作（No-op），磁盘 `state.json` 的 `queue` 状态未落盘；<br>③ **运行中任务孤儿化**：`#btn-queue-remove`（`app.js:820`）未拦截 `status === "running"`，允许直接删除正在后台跑的队列项。 | 状态机隔离不变量与持久化原子性契约 |
| **WUI-P10-04** | **P0** | 1. `webui/app.js:690-704, 776-811`<br>2. `webui/server.py:208-293, 445-457`<br>3. `webui/index.html:179-204` | **浏览器端 `setTimeout` 脆弱 `queuePump` 断链 & 缺失多文件异构任务队列**：<br>① **无头断链**：队列连续调度依赖浏览器端 `queuePump()`（`app.js:776`）捕获 `lastJobStatus === "running" -> current !== "running"` 边沿并 `setTimeout(..., 350)` 发起下一次 HTTP 请求。若远程转码途中关闭网页、刷新浏览器（`lastJobStatus` 重置为 `"idle"`）或 SSE 闪断，队列在当前文件跑完后永久停摆；<br>② **粒度与动作缺失**：前端队列项仅存单文件 `input`，不支持单任务 `1..N` 个输入文件（`inputs: string[]`）；工具栏缺失 `↑ 上移`、`↓ 下移`、`⏭ 跳过当前`、`↻ 重试`；`server.py` 的 `POST /api/job` 仅支持单次 `start | cancel`，缺乏服务端常驻队列推进器。 | `05_Track_10D` §2 & §3.2 异构多文件动态任务队列契约 |
| **WUI-P10-05** | **P1** | 1. `webui/app.js:1093-1106, 1347-1350`<br>2. `webui/index.html:42-56, 228-233, 561-564, 594`<br>3. `webui/app.css:858, 1022-1029, 1248` | **Player/NLE 视口交互断链、未注册 i18n 词条与 CSS 语法错误**：<br>① **Player A/B 分屏线无法拖拽**：`installPlayer()`（`app.js:1093-1106`）仅监听 `pointerdown`，缺失 `pointermove` / `pointerup` / `setPointerCapture`，鼠标按住 `#ab-handle` 无法连续拖动；<br>② **NLE 与 Encoder 视频视口遮挡 & 播放头脱节**：`app.css:1022` 中 `.nle-viewport .stage-placeholder` 位于 `<video id="nle-video">` 之后且无 `.has-media` 隐藏规则，加载视频后仍被 `"PROGRAM MONITOR"` 永久遮盖（`#encoder-ab .ab-fallback` 同理）；`#btn-nle-play`（`app.js:1347`）不支持暂停切换，且 `#nle-video` 未绑定 `timeupdate` 更新 `state.nle.playheadMs`；`index.html:594` 误将“帧数上限”写为“帧率上限”；<br>③ **i18n 半截失效**：`index.html:42, 49, 56` 使用了 C++ `i18n.h` 中不存在的 `WuiNavEncoder` / `WuiNavPlayer` / `WuiNavNle`，切至英文时退回中文 `FALLBACK`；<br>④ **CSS 语法错误**：`app.css:1248` 在 `@media (max-width: 900px)` 内裸写 `--sidebar-w: 60px;`（无 `:root` 选择器包裹），被浏览器直接忽略。 | 阶段二 R2.2 / R2.4 交互完备性与代码质量门禁 |

---

## 第二部分：Plan（WebUI v2 返工架构蓝图与三端协同不变量）

### 2.1 核心架构不变量（Architectural Invariants）

1. **SSOT 20 项参数零漂移对齐**：
   - 配合 [`07_返工规范`](07_CineRTX_Phase10_专家驳回裁决暨全链路接线闭环SuperPower返工规范.md) 的 `RW-P10-02`，`kParamSchema` 正式锁定为 **20 项**（新增 `pipelineOrder: "vsr-first" | "fg-first"`，扩展 `codec: "av1,hevc,av1_cpu,hevc_cpu"` 与 `rateControl: "cq,vbr,cbr,qp,crf"`，默认 `rateControl="cq"`, `qp=22`, `pipelineOrder="vsr-first"`）。
   - WebUI 通过 `GET /api/schema` 动态消费全部 20 项参数元数据与 125 项双语词典，前端零硬编码范围。
2. **运行期非阻塞工作台与双模式参数检查器（Inspector Dual-Context Mode）**：
   - **彻底废除参数控件上的 `data-lock`**：后台队列处于 `running` 时，右侧参数检查器保持 **100% 可编辑**。
   - **明确区分「草稿配置态（Draft Profile Mode）」与「排队任务编辑态（Pending Job Inspect Mode）」**：
     - 当 `selectedQueueId === null`（或选中的是 `running`/`done`/`failed`/`cancelled` 任务，设为只读检视或仅回显不反向覆写）时，右侧检查器编辑的是顶层 `state.profile`（用于随时点击「+ 加入队列」追加新任务）；
     - 仅当用户显式选中状态为 `"pending"` 的队列行时，`syncProfileToQueue()` 才将右侧修改同步写入该 `Pending` 任务的 `job.profile` 快照，并在检查器顶部显示 `正在编辑排队任务 #N (点击空白处或「取消选中」返回新任务草稿)`。
3. **服务端常驻多文件异构队列调度器（Server-Owned Queue Pump）**：
   - 彻底拔除浏览器端脆弱的 `setTimeout` `queuePump()`，将队列推进下沉至 `webui/server.py` 的 `JobRunner` 后台线程：
     - 每个队列任务 `EncoderJob` 支持 `inputs: string[]`（`1..N` 个输入文件）与独立 `profile` 快照；
     - `POST /api/job` 原生支持 `action: "enqueue" | "update" | "remove" | "move" | "skip" | "retry" | "clear" | "start" | "stop"`；
     - 一旦启动队列（或向空闲队列 `enqueue` 并自动/手动启动），`server.py` 的后台工作线程自动按序遍历所有 `pending` 任务及其下辖的 `1..N` 个输入文件，实时更新 `state.json` 的 `queue` 与 `runtime.job` 并通过 SSE `runtime` / `state-file` 广播——即使关闭全部浏览器窗口，后端转码队列依然 100% 跑完！

### 2.2 返工文件触碰矩阵（File Isolation Matrix）

| 返工任务 | 触碰文件 | 协同依赖说明 |
| :--- | :--- | :--- |
| **WUI-P10-01** | `webui/index.html`, `webui/app.js`, `webui/state.json` | 对接 `include/rtx/param_schema.h`（20 项 SSOT）与 `include/rtx/i18n.h`（125 项词条） |
| **WUI-P10-02** | `webui/index.html`, `webui/app.js` | 移除参数控件 `data-lock`，实现运行期非阻塞追加与 `Pending` 任务安全编辑 |
| **WUI-P10-03** | `webui/app.js` | 修复 `syncProfileToQueue` 越权篡改、`flushPersist` 立即写盘与选中态解绑 |
| **WUI-P10-04** | `webui/server.py`, `webui/app.js`, `webui/index.html` | 服务端多文件异构队列调度器 + 前端 `↑/↓/Skip/Retry` 与多文件导入闭环 |
| **WUI-P10-05** | `webui/app.js`, `webui/index.html`, `webui/app.css` | 修复 Player A/B 拖拽、NLE/Encoder 视口占位遮挡、NLE 播放头同步、CSS `:root` 与双语词条 |

---

## 第三部分：Tasks（SuperPower 原子级返工施工单：`WUI-P10-01 ~ WUI-P10-05`）

### WUI-P10-01：补齐 Phase 10 20 项 SSOT 参数契约、全模态编码选项与预设映射

#### 1. 修改 `webui/index.html`（参数检查器 & 预设卡片 & NLE 面板）
- **位置 1（`webui/index.html:122-168`，左栏 5 张预设卡片）**：补充 `data-rate-control="cq"`、`data-qp="22"`、`data-pipeline-order="vsr-first"`：
```html
        <div class="preset-list" id="preset-list">
          <button type="button" class="preset-card active" data-preset="master"
                  data-codec="av1" data-preset-level="5" data-rate-control="cq" data-qp="22"
                  data-resolution="4k" data-vsr-enabled="true" data-vsr-quality="4"
                  data-fps-mode="60-nvofa" data-pipeline-order="vsr-first" data-peak-nits="1000">
            <span class="preset-icon">★</span>
            <div class="preset-text">
              <span class="preset-name">4K 60fps AV1 HDR10 母版</span>
              <span class="preset-meta">默认最优 · AV1 CQ22 / 4K / NVOFA 60</span>
            </div>
          </button>
          <button type="button" class="preset-card" data-preset="rife"
                  data-codec="av1" data-preset-level="6" data-rate-control="cq" data-qp="20"
                  data-resolution="4k" data-vsr-enabled="true" data-vsr-quality="4"
                  data-fps-mode="60-rife" data-pipeline-order="vsr-first" data-peak-nits="1000">
            <span class="preset-icon">◆</span>
            <div class="preset-text">
              <span class="preset-name">4K 60fps RIFE 深度学习臻彩</span>
              <span class="preset-meta">RIFE v4.25 · VSR->FG · CQ20</span>
            </div>
          </button>
          <button type="button" class="preset-card" data-preset="esports"
                  data-codec="av1" data-preset-level="4" data-rate-control="cq" data-qp="22"
                  data-resolution="4k" data-vsr-enabled="true" data-vsr-quality="4"
                  data-fps-mode="120-nvofa" data-pipeline-order="vsr-first" data-peak-nits="1000">
            <span class="preset-icon">⚡</span>
            <div class="preset-text">
              <span class="preset-name">4K 120fps 电竞高帧率</span>
              <span class="preset-meta">120 FPS · VSR->FG 极速拓扑</span>
            </div>
          </button>
          <button type="button" class="preset-card" data-preset="compat"
                  data-codec="hevc" data-preset-level="5" data-rate-control="cq" data-qp="22"
                  data-resolution="2k" data-vsr-enabled="true" data-vsr-quality="3"
                  data-fps-mode="60-nvofa" data-pipeline-order="vsr-first" data-peak-nits="1000">
            <span class="preset-icon">🎬</span>
            <div class="preset-text">
              <span class="preset-name">2K 60fps HEVC 广泛兼容</span>
              <span class="preset-meta">HEVC Main10 CQ22 / 2K / VSR L3</span>
            </div>
          </button>
          <button type="button" class="preset-card" data-preset="cpu-master"
                  data-codec="av1_cpu" data-preset-level="5" data-rate-control="crf" data-qp="20"
                  data-resolution="native" data-vsr-enabled="false" data-vsr-quality="4"
                  data-fps-mode="original" data-pipeline-order="vsr-first" data-peak-nits="1000">
            <span class="preset-icon">🎞</span>
            <div class="preset-text">
              <span class="preset-name">原生分辨率 CPU CRF 母版</span>
              <span class="preset-meta">libsvtav1 CRF20 · TrueHDR 无损</span>
            </div>
          </button>
        </div>
```
- **位置 2（`webui/index.html:280-408`，右栏参数检查器）**：
  1. 将 `282` 行徽章改为 `<span class="badge-accent" id="inspector-badge">20 · SSOT</span>`；
  2. 在 `#ctl-codec`（`315` 行）补齐 `av1_cpu` 与 `hevc_cpu`：
     ```html
     <select id="ctl-codec" data-bind="profile.codec">
       <option value="av1" data-i18n="OptCodecAv1">AV1 10-bit (NVENC)</option>
       <option value="hevc" data-i18n="OptCodecHevc">HEVC Main10 (NVENC)</option>
       <option value="av1_cpu" data-i18n="OptCodecAv1Cpu">AV1 10-bit CPU (libsvtav1)</option>
       <option value="hevc_cpu" data-i18n="OptCodecHevcCpu">HEVC 10-bit CPU (libx265)</option>
     </select>
     ```
  3. 在 `#ctl-rc`（`334` 行）补齐 `cq` 与 `crf`，并将 `#ctl-qp` 的 `data-visible-when` 扩展为 `cq || qp || crf`：
     ```html
     <select id="ctl-rc" data-bind="profile.rateControl">
       <option value="cq" data-i18n="OptRcCq">CQ 恒定感知画质 (Spatial AQ)</option>
       <option value="vbr" data-i18n="OptRcVbr">VBR 动态码率</option>
       <option value="cbr" data-i18n="OptRcCbr">CBR 恒定码率</option>
       <option value="qp" data-i18n="OptRcQp">Constant QP (I/P/B 阶梯)</option>
       <option value="crf" data-i18n="OptRcCrf">CRF 纯 CPU 母版软编</option>
     </select>
     ```
     ```html
     <div class="param-cell" data-visible-when="profile.rateControl=cq||profile.rateControl=qp||profile.rateControl=crf">
       <label for="ctl-qp" data-i18n="ParamCq">CQ / QP / CRF 值</label>
       <input id="ctl-qp" type="number" min="0" max="51" data-bind="profile.qp" data-type="number">
     </div>
     ```
  4. 在「超分与补帧」分组（`396` 行 `#ctl-fps` 下方）新增第 20 项参数 `pipelineOrder`（并在 `#ws-nle` 的 `619` 行同步新增）：
     ```html
     <div class="param-cell full">
       <label for="ctl-pipeline-order" data-i18n="ParamPipelineOrder">管线拓扑顺序</label>
       <select id="ctl-pipeline-order" data-bind="profile.pipelineOrder">
         <option value="vsr-first" data-i18n="OptPipeVsrFirst">VSR -> FG (先超分后补帧 - 默认)</option>
         <option value="fg-first" data-i18n="OptPipeFgFirst">FG -> VSR (先补帧后超分)</option>
       </select>
     </div>
     ```

#### 2. 修改 `webui/app.js` 与 `webui/state.json`
- 在 `webui/app.js:252-283` 升级 `OUTPUT_SUFFIX` 与 `looksAutoFor`，支持 `av1_cpu` / `hevc_cpu` 归一化推导：
```javascript
const OUTPUT_SUFFIX = {
  av1: "_remaster_av1.mp4",
  av1_cpu: "_remaster_av1.mp4",
  hevc: "_remaster_hevc.mp4",
  hevc_cpu: "_remaster_hevc.mp4",
};
```
- 在 `webui/app.js:656-660` 扩展 `PRESET_FIELDS`：
```javascript
const PRESET_FIELDS = {
  codec: "codec", presetLevel: "preset", rateControl: "rateControl", qp: "qp",
  resolution: "resolution", vsrEnabled: "vsrEnabled", vsrQuality: "vsrQuality",
  fpsMode: "fpsMode", pipelineOrder: "pipelineOrder", peakNits: "peakNits",
};
```
- 在 `webui/state.json:11-31` 更新默认 `profile`：
```json
  "profile": {
    "input": "",
    "output": "",
    "codec": "av1",
    "preset": 5,
    "rateControl": "cq",
    "bitrateKbps": 0,
    "qp": 22,
    "resolution": "4k",
    "geometry": "auto",
    "vsrEnabled": true,
    "vsrQuality": 4,
    "peakNits": 1000,
    "fpsMode": "60-nvofa",
    "pipelineOrder": "vsr-first",
    "rifeEngine": "",
    "copyAudio": true,
    "hdr10plus": false,
    "lut": "",
    "gpu": 0,
    "frames": 0
  },
```

---

### WUI-P10-02 & WUI-P10-03：解除运行期面板锁死、根治 `syncProfileToQueue` 越权篡改与持久化竞态

#### 1. 拔除 `#panel-params` 上的 `data-lock` 属性（`webui/index.html:292-450`）
- 删除 `webui/index.html` 右侧参数检查器中所有 `input`、`select`、`button` 上的 `data-lock` 属性，确保后台队列 `running` 期间用户可随时配置新参数并点击「添加到队列」或「批量导入」动态追加新任务。
- 在 `#panel-params` 头部增加当前编辑上下文指示条（`#inspector-context-bar`）：
  - 当 `selectedQueueId === null` 时显示：`草稿模式 · 修改参数后可点击「+ 加入队列」追加新任务`；
  - 当选中 `Pending` 队列任务时显示：`正在编辑排队任务 #ID` + `<button id="btn-queue-deselect" class="btn btn-sm btn-ghost">返回新任务草稿</button>`；
  - 当选中 `Running / Done / Failed / Cancelled` 任务时，仅做只读回显或提示该任务已锁定，严禁反向覆写。

#### 2. 重写 `selectQueueItem`、`syncProfileToQueue` 与 `flushPersist`（`webui/app.js:150-156, 739-765`）
```javascript
async function flushPersist() {
  if (persistTimer) {
    clearTimeout(persistTimer);
    persistTimer = null;
  }
  await api("/api/state", "PUT", ownedSections()).catch(() => {});
}

function deselectQueueItem() {
  if (!selectedQueueId) return;
  selectedQueueId = null;
  renderQueue();
  renderInspectorContext();
}

function selectQueueItem(id) {
  if (selectedQueueId === id) {
    deselectQueueItem();
    return;
  }
  const item = (state.queue || []).find((entry) => entry.id === id);
  if (!item) return;
  selectedQueueId = id;
  applyingRemote = true;
  try {
    setPath("profile", Object.assign({}, DEFAULTS, item.profile || {}));
  } finally {
    applyingRemote = false;
  }
  runEffects();
  renderQueue();
  renderInspectorContext();
}

function syncProfileToQueue() {
  if (!selectedQueueId || applyingRemote || !state.profile || !Object.keys(state.profile).length) return;
  const items = (state.queue || []).slice();
  const index = items.findIndex((entry) => entry.id === selectedQueueId);
  if (index < 0) {
    selectedQueueId = null;
    renderInspectorContext();
    return;
  }
  // CRITICAL INVARIANT: Only "pending" jobs may be live-edited!
  if (items[index].status !== "pending") return;

  const p = Object.assign({}, state.profile);
  const inputs = Array.isArray(items[index].inputs) && items[index].inputs.length
    ? items[index].inputs.slice()
    : (p.input ? [p.input] : []);
  if (inputs.length <= 1 && p.input) inputs[0] = p.input;

  items[index] = Object.assign({}, items[index], {
    profile: p,
    inputs,
    input: inputs[0] || p.input || "",
    output: p.output || autoNameFor(inputs[0] || "", p.codec || "av1"),
    codec: p.codec || "av1",
    rateControl: p.rateControl || "cq",
    qp: p.qp ?? 22,
    resolution: p.resolution || "4k",
    fpsMode: p.fpsMode || "60-nvofa",
    pipelineOrder: p.pipelineOrder || "vsr-first",
    peakNits: p.peakNits || 1000,
  });
  setPath("queue", items);
  schedulePersist();
  api("/api/job", "POST", {
    action: "update",
    jobId: selectedQueueId,
    profile: p,
    inputs,
  }).catch(() => {});
}
```

---

### WUI-P10-04：下沉服务端常驻多文件异构队列调度器（`webui/server.py` + `webui/app.js` + `webui/index.html`）

#### 1. 升级 `webui/server.py` 的 `JobRunner` 为服务端多文件异构队列引擎
在 `webui/server.py` 中重构 `JobRunner` 与 `_job()`，彻底接管队列生命周期（浏览器仅发命令并通过 SSE 订阅状态）：
- **支持动作集合**：
  - `enqueue`：接收 `{action: "enqueue", inputs: [...], profile: {...}, presetName: "..."}`，生成唯一 `jobId`，支持单任务 `1..N` 个输入文件，原子写入 `state.json["queue"]` 并广播 SSE `state-file`；若当前队列已在运行，工作线程自动在后续轮次消费该新任务；
  - `update`：仅允许修改 `status == "pending"` 的任务 `profile` 与 `inputs`；
  - `remove`：拒绝删除 `status == "running"` 的任务（返回 `409 Cannot remove a running job; use skip instead`），删除非运行任务并持久化；
  - `move`：接收 `{action: "move", jobId: "...", delta: -1 | 1}`，在 `pending` 任务间上下移动优先级；
  - `skip`：向当前正在运行的 `CineRTX.exe --session-stdio` 发送 `"cancel\n"`，将该任务标记为 `"cancelled"`，**但工作线程不退出，立即自动推进至下一个 `"pending"` 任务**；
  - `retry`：将 `"failed"` 或 `"cancelled"` 任务重置为 `"pending"`（`pct = 0`, `currentFileIndex = 0`）；
  - `clear`：清除所有非 `"running"`（或已完成 `"done"`）的任务；
  - `start`：若 `queue` 中存在 `"pending"` 任务则启动常驻队列消费循环 `_queue_worker()`；若 `queue` 为空且 `profile.input` 非空，则以单任务模式启动；
  - `stop` / `cancel`：设置 `stop_all = True` 并终止当前子进程，暂停整个队列。
- **多文件任务执行循环（`inputs: 1..N`）**：
  对于每个 `EncoderJob`，依次遍历 `inputs[0 .. N-1]`：
  - 当 `len(inputs) > 1` 时，自动按该任务专属 `profile.codec` 为每个子文件派生输出路径 `<stem>_remaster_<codec>.mp4`；
  - 实时计算 `currentFileIndex`（`k/N`）、`currentFilePct` 与 `totalJobPct = ((i * 100.0) + pct) / len(inputs)`，写入 `queue[idx]` 并通过 SSE 推送给前端。

#### 2. 升级 `webui/index.html` 队列工具栏与表格列（`179-204` 行）
- 在 `.queue-toolbar` 中补齐动态编排按钮（绑定 `i18n.h` 已定义的 `BtnQueueAdd` / `BtnQueueRemove` / `BtnQueueUp` / `BtnQueueDown` / `BtnQueueSkip`）：
```html
          <div class="queue-toolbar">
            <button id="btn-queue-add" class="btn btn-sm btn-ghost" data-i18n="BtnQueueAdd">+ 加入队列</button>
            <button id="btn-queue-import" class="btn btn-sm btn-ghost" data-browse="queue.pendingImport" data-browse-mode="queue-import" data-i18n="WuiBtnBatchImport">批量多选导入</button>
            <button id="btn-queue-up" class="btn btn-sm btn-ghost" data-i18n="BtnQueueUp">↑ 上移</button>
            <button id="btn-queue-down" class="btn btn-sm btn-ghost" data-i18n="BtnQueueDown">↓ 下移</button>
            <button id="btn-queue-remove" class="btn btn-sm btn-ghost" data-i18n="BtnQueueRemove">- 移除任务</button>
            <button id="btn-queue-retry" class="btn btn-sm btn-ghost">↻ 重试</button>
            <button id="btn-queue-clear" class="btn btn-sm btn-ghost">清空已完成</button>
            <span class="spacer"></span>
            <button id="btn-skip" class="btn btn-sm btn-ghost" data-i18n="BtnQueueSkip" disabled>⏭ 跳过当前</button>
            <button id="btn-stop" class="btn btn-sm btn-danger" data-i18n="ButtonStop" disabled>⏹ 停止队列</button>
            <button id="btn-start" class="btn btn-primary" data-i18n="ButtonStart">▶ 开始转码</button>
          </div>
```
- 在 `#fs-modal`（文件浏览器弹窗）支持 `queue-import` 模式下的**多文件勾选（Multi-Select）**：当 `fsCtx.mode === "queue-import"` 时，允许用户在当前目录勾选 `1..N` 个视频文件（或一键「全选本目录视频」），打包为单个含 `N` 个文件的 `EncoderJob`（显示 `clip1.mp4 (+3 files)` 徽章）压入队列！
- 删除 `webui/app.js` 中脆弱的浏览器端 `queuePump()` 与 `startQueueItem()`，改为直接调用 `POST /api/job` 对应动作。

---

### WUI-P10-05：修复 Player A/B 指针捕获拖拽、NLE 视口遮挡与播放头同步、CSS 作用域及双语完备性

#### 1. 修复 Player A/B 分屏手柄连续拖拽（`webui/app.js:1093-1106`）
使用 Pointer Events 标准 `setPointerCapture` 实现丝滑拖拽：
```javascript
  const stage = document.getElementById("player-stage");
  const handle = document.getElementById("ab-handle");
  let draggingSplit = false;
  const setSplitFromEvent = (event) => {
    if (!stage || !stage.getBoundingClientRect) return;
    const rect = stage.getBoundingClientRect();
    if (rect.width <= 0) return;
    const pct = Math.max(0, Math.min(100, ((event.clientX - rect.left) / rect.width) * 100));
    setPath("player.splitPos", Math.round(pct));
  };
  if (stage && stage.addEventListener) {
    stage.addEventListener("pointerdown", (event) => {
      if (event.target === handle || event.target === stage || event.target.tagName === "VIDEO") {
        draggingSplit = true;
        if (stage.setPointerCapture && event.pointerId != null) {
          try { stage.setPointerCapture(event.pointerId); } catch {}
        }
        setSplitFromEvent(event);
      }
    });
    stage.addEventListener("pointermove", (event) => {
      if (!draggingSplit) return;
      setSplitFromEvent(event);
    });
    const endDrag = (event) => {
      if (!draggingSplit) return;
      draggingSplit = false;
      if (stage.releasePointerCapture && event.pointerId != null) {
        try { stage.releasePointerCapture(event.pointerId); } catch {}
      }
      schedulePersist();
    };
    stage.addEventListener("pointerup", endDrag);
    stage.addEventListener("pointercancel", endDrag);
  }
```

#### 2. 修复 NLE 节目监视器与 Encoder A/B 视口占位遮挡及播放头实时同步（`webui/app.js` & `webui/app.css`）
- 在 `webui/app.css:672` 与 `1022` 增加 `.has-media` 占位层隐藏规则：
```css
.ab-viewport.has-media .ab-fallback,
.nle-viewport.has-media .stage-placeholder {
  display: none;
}
```
- 在 `webui/app.js` 的 `attachMedia()` 与 `mountNleVideo(path)` 中同步切换 `.has-media` 类名：
  - `document.getElementById("encoder-ab")?.classList.toggle("has-media", Boolean(path));`
  - `document.querySelector(".nle-viewport")?.classList.toggle("has-media", Boolean(path));`
- 在 `webui/app.js:1347-1350` 修复 `#btn-nle-play` 为播放/暂停双态切换，并监听 `#nle-video` 的 `timeupdate` 实时驱动 `state.nle.playheadMs` 与 `705,600,000 Hz Flicks` 时间码：
```javascript
  const nleVideo = document.getElementById("nle-video");
  if (nleVideo && nleVideo.addEventListener) {
    nleVideo.addEventListener("timeupdate", () => {
      if (!nleVideo.paused) {
        setPath("nle.playheadMs", Math.round((Number(nleVideo.currentTime) || 0) * 1000));
      }
    });
  }
  document.getElementById("btn-nle-play").addEventListener("click", () => {
    if (!nleVideo) return;
    if (nleVideo.paused) {
      if (typeof nleVideo.play === "function") nleVideo.play().catch(() => {});
    } else if (typeof nleVideo.pause === "function") {
      nleVideo.pause();
    }
  });
```
- 将 `webui/index.html:594` 的 `<label for="nle-frames">帧率上限</label>` 修正为 `<label for="nle-frames" data-i18n="WuiParamFrames">帧数上限</label>`。

#### 3. 修复 `webui/app.css:1248` 媒体查询语法错误与前端 UI 词条兜底
- 在 `webui/app.css:1238-1249` 将裸写的 `--sidebar-w: 60px;` 包裹进 `:root`：
```css
@media (max-width: 900px) {
  :root { --sidebar-w: 60px; }
  .sidebar { width: 60px; }
  ...
}
```
- 在 `webui/app.js` 的 `I18N` 初始化字典中提供前端专属布局标签的双语映射（`WuiNavEncoder` / `WuiNavPlayer` / `WuiNavNle` 等，在 `loadSchema()` 合并时保留前端导航与分区标题的中英词条），确保 `Ctrl+L` 或点击语言按钮时全界面 100% 切换无中文残留。

---

## 第四部分：Checklists（WebUI v2 终审复验量化门禁）

前端工程师完成 `WUI-P10-01 ~ WUI-P10-05` 返工后，必须同时通过以下 5 道硬门禁方可申请终审封板：

- [ ] **Gate WUI-10.1（Phase 10 20 项 SSOT 参数与枚举完整性门禁）**：
  - 静态扫描断言：
    - `rg "av1_cpu|hevc_cpu" webui/index.html webui/app.js` 命中 $\ge 4$ 处；
    - `rg "OptRcCq|OptRcCrf|ParamPipelineOrder|OptPipeVsrFirst|OptPipeFgFirst" webui/index.html` 全部命中；
    - `rg 'profile\.rateControl=cq\|\|profile\.rateControl=qp\|\|profile\.rateControl=crf' webui/index.html` 命中；
    - `webui/state.json` 中 `profile.rateControl == "cq"`、`profile.qp == 22`、`profile.pipelineOrder == "vsr-first"`。
- [ ] **Gate WUI-10.2（运行期零锁死与 `syncProfileToQueue` 状态隔离门禁）**：
  - 静态扫描断言：`rg "data-lock" webui/index.html` 在 `#panel-params` 参数检查器内返回 **0 匹配**；
  - 行为验证：当 Job #1 处于 `running` 或 `done` 且曾被点击选中时，修改右侧 `profile` 或启动 Job #2，断言 Job #1 的 `profile` / `input` / `output` **零篡改**；僅当显式选中 `status === "pending"` 的任务时才允许原地更新其参数。
- [ ] **Gate WUI-10.3（服务端常驻多文件异构队列调度门禁）**：
  - 静态扫描断言：`rg "function queuePump" webui/app.js` 返回 **0 匹配**（浏览器端 `setTimeout` 泵彻底拔除）；
  - 端到端验证：通过 `POST /api/job` 连续 `enqueue` Job #1（2 个输入文件，`av1 + cq 22 + vsr-first`）与 Job #2（1 个输入文件，`hevc_cpu + crf 20 + fg-first`），在 Job #1 运行期间动态 `enqueue` Job #3 并调用 `move(job3, -1)` 提至 Job #2 之前，不依赖任何浏览器轮询即可由 `server.py` 后台线程按 `Job #1 (2 files) -> Job #3 -> Job #2` 顺序自动跑完并持久化至 `state.json`。
- [ ] **Gate WUI-10.4（Player / NLE 交互闭环门禁）**：
  - Player 舞台按住 `#ab-handle` 左右拖动时，`--split` 与 `state.player.splitPos` 实时连续跟随；
  - NLE 与 Encoder 视口挂载视频后，`.stage-placeholder` / `.ab-fallback` 立即隐藏不遮挡画面；点击 `#btn-nle-play` 可正常切换播放/暂停，且播放期间时间轴 Playhead 与 `Flicks` 时间码随视频进度实时推进。
- [ ] **Gate WUI-10.5（CSS 语法纯净度与双语切换零残留门禁）**：
  - `webui/app.css` 中 `@media (max-width: 900px)` 内的 `--sidebar-w: 60px;` 置于 `:root { ... }` 选择器内；
  - 切换 `ui.lang` 至 `"en"` 时，左侧导航栏（`Encoder` / `Player` / `NLE`）、队列工具栏及检查器分组标题 100% 切换为英文，零中文硬编码残留。
