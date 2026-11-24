# 16_CineRTX Phase 10 WebUI v3 样式重构领域专家专项验收与精修规范

- **文档编号**：`DOC-P10-16`
- **审计标的**：`workspace\work\CineRTX\webui\` 下的 WebUI v3 样式重构产物（[`webui/index.html`](../../webui/index.html)、[`webui/app.css`](../../webui/app.css)、[`webui/app.js`](../../webui/app.js)、[`webui/server.py`](../../webui/server.py)、[`webui/state.json`](../../webui/state.json)、[`webui/README.md`](../../webui/README.md)）
- **关联主干契约**：[`include/rtx/param_schema.h`](../../include/rtx/param_schema.h)（`9,264 B`，20 参数 SSOT 契约）、[`08_CineRTX_Phase10_WebUI_v2重构评审与三端对齐SuperPower规范.md`](08_CineRTX_Phase10_WebUI_v2重构评审与三端对齐SuperPower规范.md)、[`15_CineRTX_Phase10_全栈收官终审驳回裁决暨C++主干合龙SuperPower返工规范.md`](15_CineRTX_Phase10_全栈收官终审驳回裁决暨C++主干合龙SuperPower返工规范.md)
- **签发角色**：多媒体系统工程与 GPU 异构计算首席领域专家（Chief Domain Expert）
- **审计日期**：2026-11-18
- **专项验收裁决**：**CONDITIONAL PASS（有条件通过视觉体系架构与契约对齐验收；确认 20 参数绑定、132 处 i18n 映射与前后端单向队列所有权 100% 完好，但查实 `footer.app-footer` 退化为右侧第 3 列、顶栏 SVG 进度环半径与 JS 周长失配等 5 项样式与几何缺陷 `WUI-S3-01 ~ WUI-S3-05`，须按本规范内置补丁完成收口）**

---

## 0. 首席专家专项验收综述（Executive Audit Summary）

针对前端工程师于 `2026-11-18T04:34Z ~ 08:12Z` 提交的 **CineRTX WebUI v3（Cinematic Glass-Morphism Studio Theme）** 样式重构产物，本领域专家从 **DOM 契约无损性、CSS 布局几何精确度、WCAG 2.1 色彩对比度、GPU 合成器渲染开销（DWM/Viz 零拷贝干扰度）以及离线工业工作站兼容性** 五个维度进行了逐行穿透审计：

1. **已达标并予以肯定的核心工程成果**：
   - **DOM 绑定与 20 参数 SSOT 契约 100% 零损迁移**：在将 [`webui/index.html`](../../webui/index.html) 从 `741` 行精简重构至 `449` 行（`37,307 B`）、将 `.card` 全面升级为 `.glass-panel` 的过程中，原有的 **136 个业务 DOM `id`**、**27 个 `data-bind` 双向绑定节点**、**132 个 `data-i18n` + 2 个 `data-i18n-title` 多语言词条钩子**、**3 个 `data-visible-when` 条件显隐节点**、**7 个 `data-browse` 路径浏览触发器**与 **5 张 `data-preset` 预设卡片** 100% 完整保留，且与更新后的 [`include/rtx/param_schema.h`](../../include/rtx/param_schema.h)（20 项 `kParamSchema`）严格 1:1 对齐。
   - **前轮遗留 P2 缺陷已闭环**：[`webui/app.js:472-481`](../../webui/app.js#L472-L481) 的 `ownedSections()` 已移除 `queue: state.queue`，消除了前端防抖持久化与后端任务状态机的双写竞态；`webui/__pycache__` 缓存污染已从仓库物理清除；[`webui/README.md`](../../webui/README.md) 已同步至 `8,419 B`。
   - **专业影视工作站视觉语言成型**：[`webui/app.css`](../../webui/app.css)（`41,823 B`，`1,215` 行）建立了完整的 Deep Obsidian 6 级表面色阶（`--bg: #050706` 至 `--surface-5: #263026`）与 NVIDIA Signature Green（`#76b900`）辉光 Token 体系，统一了等宽数字对齐（`font-variant-numeric: tabular-nums`）、自定义拨动开关（`.toggle-switch`）、预设激活导条（`.preset-card::before`）、运行态流光进度条（`@keyframes shimmer`）及 `<900px` 窄屏图标化侧边栏响应式折叠。
2. **穿透审计查实的 5 项样式与几何缺陷（`1 P0 + 3 P1 + 1 P2`）**：
   - **`WUI-S3-01` [P0 严重布局错位]**：`<footer class="app-footer">` 作为水平 Flex 容器 `<body>` 的直接子元素且未脱离文档流，退化为屏幕最右侧高达 `100vh`、宽约 `340px` 的**垂直第 3 列**，横向挤压主工作区。
   - **`WUI-S3-02` [P1 SVG 几何失步]**：`index.html:110` 将顶栏全局进度环 `<circle id="gp-circle">` 的半径从 `r="15.5"` 改为 `r="18"`（周长 `113.1`），但未同步修改 `app.js:765` 硬编码的 `const circumference = 97.4;`，导致 `0%` 空闲态常驻 **13.9%（50° 圆心角）幽灵绿弧**。
   - **`WUI-S3-03` [P1 表单失明与对比度不足]**：未声明 `color-scheme: dark` 且未给 `select option` 设深色背景，导致 Windows 浏览器下原生 `<select>` 下拉列表呈现**白底浅白字（对比度 `1.18:1`）**；同时 `--text-muted: #5c685c` 在深色表面上的对比度仅为 **`3.16:1 ~ 3.45:1`**，低于 WCAG AA 级 `4.5:1` 门禁。
   - **`WUI-S3-04` [P1 GPU 合成器抢占与嵌套滚动]**：底层 3 个 `filter: blur(100px)` 的 `.ambient-orb` 无限位移缩放动画叠加 8 层 `backdrop-filter: blur(12px~20px)`，在转码运行期迫使浏览器每帧重算全屏高斯模糊，抢占 GPU 硬件资源；`.inspector-scroll` 与 `.enc-inspector` 存在双层 `overflow-y: auto` 嵌套滚动截断。
   - **`WUI-S3-05` [P2 离线阻塞与文档表错位]**：`index.html:15-17` 引入外网 `fonts.googleapis.com` 同步样式表，在无外网后期机房引发阻塞；`webui/README.md:68-89` 的 20 参数表格列名与 `param_schema.h` 真实 JSON 键名不一致。

---

## 1. 全量审计矩阵（Audit Matrix Across 6 Dimensions）

| 审计维度 | 关联文件与行号 | 实测指标 / 核验物证 | 判定 |
| :--- | :--- | :--- | :---: |
| **1. DOM 契约与双向绑定完整性** | [`webui/index.html:1-449`](../../webui/index.html#L1-L449)<br>[`webui/app.js:529-579`](../../webui/app.js#L529-L579) | `id` 137 个（新增 SVG 渐变 `g1`，原 136 个全保留）、`data-bind` 27 个、`data-i18n` 132 个、`data-visible-when` 3 个、`data-browse` 7 个、`data-preset` 5 个 100% 匹配。 | **PASS** |
| **2. 顶层视口与栅格布局几何** | `webui/index.html:21, 31, 97, 442`<br>`webui/app.css:71-79, 525-540, 1174-1183` | `body { display: flex }` 默认 `row` 排列，导致直接子元素 `<footer class="app-footer">` 变成右侧第 3 列（含 `248px` 左内边距）；`.enc-inspector` 与 `.inspector-scroll` 双重 `overflow-y: auto`。 | **FAIL (`WUI-S3-01`, `WUI-S3-04`)** |
| **3. 动态组件几何与 JS 协同** | [`webui/index.html:108-113`](../../webui/index.html#L108-L113)<br>[`webui/app.js:762-766`](../../webui/app.js#L762-L766) | `#gp-circle` 的 SVG 半径改为 `r="18"`（`stroke-dasharray="113.1"`），但 `app.js:765` 仍按 `r="15.5"` 计算 `circumference = 97.4`，0% 时残留 `15.7px` 绿弧。 | **FAIL (`WUI-S3-02`)** |
| **4. 色彩对比度与原生控件暗色适配** | `webui/app.css:11-46, 374-396` | 主文本 `#e6ece6` 对比度 `16.84:1`（优），次文本 `#a8b4a8` 对比度 `9.39:1`（优），主色 `#76b900` 对比度 `8.38:1`（优）；但 `--text-muted (#5c685c)` 仅 `3.16:1 ~ 3.45:1`（低于 `4.5:1`），且缺失 `color-scheme: dark` 与 `select option` 暗色背景。 | **FAIL (`WUI-S3-03`)** |
| **5. GPU 合成器开销与无障碍动效** | `webui/app.css:91-136, 326-337, 1185-1200` | 3 个 `blur(100px)` 光球常驻位移动画穿透 8 层 `backdrop-filter: blur(16px)`，转码运行期未冻结背景动画，且缺失 `@media (prefers-reduced-motion: reduce)`。 | **FAIL (`WUI-S3-04`)** |
| **6. 离线内网自包含性与文档同步** | [`webui/index.html:15-17`](../../webui/index.html#L15-L17)<br>[`webui/README.md:68-89`](../../webui/README.md#L68-L89) | `index.html` 强引外网 `fonts.googleapis.com`；`webui/README.md` 的 20 参数表仍使用旧版 `truehdr/fgMode` 列名而非 `param_schema.h` 的 `vsrEnabled/fpsMode`。 | **FAIL (`WUI-S3-05`)** |

---

## 2. 缺陷深度解剖与根因取证（Deep-Dive Forensic Analysis）

### 2.1 `WUI-S3-01`（P0）：`body` 水平 Flex 容器下 `<footer class="app-footer">` 退化为右侧垂直列

在 [`webui/index.html:21-447`](../../webui/index.html#L21-L447) 中，文档顶层结构如下：

```html
<body data-status="idle" data-workspace="encoder">
  <div class="ambient-bg" aria-hidden="true">...</div>      <!-- position: fixed (脱离文档流) -->
  <aside class="sidebar" id="sidebar">...</aside>           <!-- Flex Item #1: width: 220px -->
  <main class="main-content">...</main>                     <!-- Flex Item #2: flex: 1 -->
  <div id="fs-modal" class="modal-overlay" hidden>...</div> <!-- position: fixed (脱离文档流) -->
  <footer class="app-footer">...</footer>                   <!-- Flex Item #3: position: relative! -->
</body>
```

而在 [`webui/app.css:71-79`](../../webui/app.css#L71-L79) 与 [`webui/app.css:1174-1183`](../../webui/app.css#L1174-L1183) 中：

```css
body {
  min-height: 100vh;
  display: flex;          /* 默认 flex-direction: row; align-items: stretch */
  background: var(--bg);
  ...
  overflow: hidden;
}
.app-footer {
  padding: 8px 28px 10px;
  padding-left: calc(var(--sidebar-w) + 28px); /* 220px + 28px = 248px */
  ...
  position: relative; z-index: 1;
}
```

- **故障表现**：`<footer class="app-footer">` 并未出现在页面底部，而是作为 `body` 的第 3 个水平 Flex 子项，被排布在 `<main class="main-content">` 的**右侧**！由于 `align-items: stretch` 且左内边距高达 `248px`，它在屏幕右边缘形成了一个宽约 `340px`、高 `100vh` 的巨型空白柱，导致 `.main-content` 可用宽度凭空缩水 `340px`。
- **修复法则**：要么将 `<footer class="app-footer">` 移入 `<main class="main-content">` 内部作为末尾子元素（`flex-direction: column` 自然沉底，且无需 `padding-left: calc(var(--sidebar-w) + 28px)`），要么在 CSS 中将 `.app-footer` 设为 `position: fixed; left: 0; right: 0; bottom: 0;`。推荐**方案 A（移入 `<main class="main-content">` 内部末尾 + CSS 设 `position: fixed; left: var(--sidebar-w); right: 0; bottom: 0; height: 32px;` 雙保險兼容）**，使页面无论是否调整 DOM 位置都严格沉底对齐。

---

### 2.2 `WUI-S3-02`（P1）：顶栏全局进度环 SVG 半径 (`r="18"`) 与 JS 周长 (`97.4`) 失配

在 [`webui/index.html:108-113`](../../webui/index.html#L108-L113) 中，前端工程师将 SVG 进度环的视口从 `36x36` 放大为 `44x44`，并将圆环半径从 `r="15.5"` 放大为 `r="18"`：

```html
<!-- webui/index.html:108-113 -->
<svg viewBox="0 0 44 44" width="44" height="44">
  <circle cx="22" cy="22" r="18" fill="none" stroke="rgba(255,255,255,0.06)" stroke-width="3"/>
  <circle id="gp-circle" cx="22" cy="22" r="18" fill="none" stroke="var(--accent)" stroke-width="3"
          stroke-dasharray="113.1" stroke-dashoffset="113.1" stroke-linecap="round"
          transform="rotate(-90 22 22)"/>
</svg>
```

然而，在 [`webui/app.js:762-766`](../../webui/app.js#L762-L766) 中，`renderStatus()` 仍保留着 v2 时期的硬编码周长 `97.4`（对应 $2\pi \times 15.5$）：

```javascript
// webui/app.js:764-766 (真实在库代码)
const pct = Math.max(0, Math.min(100, prog.pct || 0));
const circumference = 97.4; // 2 * pi * 15.5  <-- 与 r="18" (2 * pi * 18 = 113.097) 严重失配！
if (gpCircle) gpCircle.setAttribute("stroke-dashoffset", String(circumference - (pct / 100) * circumference));
```

- **故障表现**：当 `pct = 0`（初始就绪态）时，`stroke-dashoffset` 被设为 `97.4`，而 `stroke-dasharray` 为 `113.1`，导致圆环顶部常驻一段长 `15.7px`（占圆周 **13.9%**）的绿色弧线，造成「未启动即有进度」的视觉假象。
- **修复法则**：在 [`webui/app.js:765`](../../webui/app.js#L765) 中动态读取 `#gp-circle` 的半径 `r`（或 `stroke-dasharray`）计算周长：`const r = Number(gpCircle && gpCircle.getAttribute("r")) || 18; const circumference = +(2 * Math.PI * r).toFixed(2);`（并同步设置 `stroke-dasharray` 为 `String(circumference)`），彻底实现 HTML 几何与 JS 渲染的自适应解耦。

---

### 2.3 `WUI-S3-03`（P1）：Windows 原生 `<select>` 弹出菜单「白底白字失明」与 `--text-muted` 对比度不达标

1. **原生 `<select>` 弹出菜单失明**：
   - [`webui/app.css:374-386`](../../webui/app.css#L374-L386) 将 `select` 的背景设为极低不透明度的 `rgba(255,255,255,0.03)`，文字颜色设为 `var(--text)`（`#e6ece6`），且 `:root` 未声明 `color-scheme: dark;`。
   - 在 Windows 10/11 的 Chromium / Edge 渲染引擎中，当 `<select>` 的背景 alpha 通道接近透明且未声明 `color-scheme: dark` 时，点击展开的操作系统级 `<option>` 弹出菜单背景色默认为纯白 `#ffffff`，而 `<option>` 继承了父级 `<select>` 的浅白字 `#e6ece6`（对比度 **`1.18:1`**），导致用户在切换编码器、预设、码率控制、分辨率、VSR 档位时看到一片空白！
2. **`--text-muted` 对比度低于 WCAG 2.1 AA 标准**：
   - [`webui/app.css:23`](../../webui/app.css#L23) 将 `--text-muted` 从 `#6b756b` 下调至 `#5c685c`。经实测：
     - `#5c685c` 在 `--bg (#050706)` 上对比度为 **`3.45:1`**；
     - `#5c685c` 在 `--surface-1 (#0a0e0a)` 上对比度为 **`3.32:1`**；
     - `#5c685c` 在 `--surface-2 (#101510)` 上对比度仅为 **`3.16:1`**。
   - 由于 `.param-cell label`（`11px`）、`.queue-table th`（`9px`）、`.log-block`（`11px`）均使用 `--text-muted`，在暗室调色/非编监视器环境下可读性严重受损。将 `--text-muted` 提升至 `#7a887a` 后，其在 `--bg`、`--surface-1`、`--surface-2` 上的对比度分别达到 **`5.48:1`、`5.28:1`、`5.01:1`**，全线跨越 WCAG 2.1 AA 级（`>= 4.5:1`）门禁。

---

### 2.4 `WUI-S3-04`（P1）：转码期全屏高斯模糊动画抢占 GPU 合成器与检查器双重滚动条

1. **GPU 合成器带宽争抢**：
   - [`webui/app.css:99-136`](../../webui/app.css#L99-L136) 的 `.ambient-orb` 使用 `filter: blur(100px)` 并以 `22s~30s` 周期执行 `@keyframes orb-drift` 位移缩放动画。由于其上方覆盖着 `.sidebar`（`backdrop-filter: blur(20px)`）、`.topbar`（`backdrop-filter: blur(12px)`）和多个 `.glass-panel`（`backdrop-filter: blur(16px)`），底层背景的任何像素变化都会击穿 Chromium 的 Backdrop Filter 缓存，迫使 GPU 合成器以 60fps 持续重跑全屏多级高斯模糊着色器。
   - **修复法则**：在 `body[data-status="running"] .ambient-orb` 及 `@media (prefers-reduced-motion: reduce)` 下强制设置 `animation-play-state: paused;`，确保 GPU 在执行 CineRTX 4K VSR + RIFE + NVENC 重负载转码时，浏览器背景层完全静止并命中纹理缓存（0 GPU 重绘开销）。
2. **检查器与主栏嵌套滚动及 `.tele-card` 缺失定义**：
   - `webui/app.css:529, 537, 539`：`.enc-inspector` 设置了 `overflow-y: auto;`，而其内部的 `.inspector-scroll` 又设置了 `max-height: calc(100vh - 200px); overflow-y: auto;`，导致右侧参数面板出现内外两条垂直滚动条；应将 `.enc-inspector` 设为 `display: flex; flex-direction: column; overflow: hidden;`，并令 `.inspector-scroll { flex: 1; min-height: 0; max-height: none; overflow-y: auto; }`。
   - 同时为 [`webui/index.html:246`](../../webui/index.html#L246) 中的 `.tele-card` 补齐样式定义（`display: flex; flex-direction: column; justify-content: center; min-width: 0;`），消除未定义孤立类名。

---

### 2.5 `WUI-S3-05`（P2）：外网 Google Fonts 阻塞与 `webui/README.md` 20 参数表键名错位

1. **移除外网字体同步阻塞**：
   - [`webui/index.html:15-17`](../../webui/index.html#L15-L17) 新增了 `<link rel="preconnect" href="https://fonts.googleapis.com">` 与 `<link href="https://fonts.googleapis.com/css2?..." rel="stylesheet">`。在离线/内网环境下会阻塞首屏渲染。应移除这 3 行外网 `<link>`，依靠 [`webui/app.css:52-53`](../../webui/app.css#L52-L53) 已完备配置的本地字体栈（`"Inter", "Segoe UI", "Microsoft YaHei", system-ui, sans-serif` 与 `"JetBrains Mono", "Cascadia Code", "Fira Code", Consolas, monospace`）实现 100% 离线零延迟渲染。
2. **修正 [`webui/README.md:68-89`](../../webui/README.md#L68-L89) 的 20 参数契约表**：
   - 将表格中的 20 项参数键名与 [`include/rtx/param_schema.h:59-80`](../../include/rtx/param_schema.h#L59-L80) 的 `kParamSchema`（`input`, `output`, `codec`, `preset`, `rateControl`, `bitrateKbps`, `qp`, `resolution`, `geometry`, `vsrEnabled`, `vsrQuality`, `peakNits`, `fpsMode`, `pipelineOrder`, `rifeEngine`, `copyAudio`, `hdr10plus`, `lut`, `gpu`, `frames`）严格对齐。

---

## 3. SuperPower 样式收口施工指令（Tasks `WUI-S3-01 ~ WUI-S3-05`）

前端工程师只需按以下精确代码块合入 4 个文件，即可在 10 分钟内完成 WebUI v3 样式重构的终局收口：

### 3.1 Task `WUI-S3-01` & `WUI-S3-05A`：修正 [`webui/index.html`](../../webui/index.html)（移除外网字体阻塞 + 将 `footer.app-footer` 移入 `main.main-content`）

1. **删除 [`webui/index.html:15-17`](../../webui/index.html#L15-L17)** 的 Google Fonts 外网 `<link>` 标签：
```html
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>CineRTX WebUI</title>
<link rel="stylesheet" href="app.css">
<script src="app.js" defer></script>
</head>
```

2. **调整 [`webui/index.html:428-447`](../../webui/index.html#L428-L447)**，将 `<footer class="app-footer">` 放入 `<main class="main-content">` 内部末尾：
```html
  </section>

  <footer class="app-footer">
    <span data-i18n="WuiFoot">state.json · JSON 通信 (HTTP + SSE)</span>
    · NVIDIA RTX Studio · SSOT
  </footer>

</main>

<!-- ═══════════ FILE BROWSER MODAL ═══════════ -->
<div id="fs-modal" class="modal-overlay" hidden>
  <div class="modal-dialog">
    <div class="modal-head"><strong id="fs-title">…</strong><button id="fs-close" class="btn-icon" aria-label="关闭"><svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2"><path d="M18 6L6 18M6 6l12 12"/></svg></button></div>
    <div class="modal-path"><input id="fs-path" type="text" spellcheck="false" placeholder="输入路径..."><button id="fs-go" class="btn btn-sm" data-i18n="WuiFsGo">跳转</button><button id="fs-up" class="btn btn-sm" data-i18n="WuiFsUp">上级</button><button id="fs-select-all" class="btn btn-sm btn-ghost" data-i18n="WuiFsSelectAll" hidden>全选视频</button></div>
    <div id="fs-list" class="fs-list" role="listbox"></div>
    <div class="modal-foot"><span id="fs-hint" class="hint-text"></span><button id="fs-select" class="btn btn-primary" data-i18n="WuiFsSelect">选择</button></div>
  </div>
</div>

</body>
</html>
```

---

### 3.2 Task `WUI-S3-02`：修正 [`webui/app.js:762-767`](../../webui/app.js#L762-L767) 顶栏进度环自适应周长计算

将 [`webui/app.js:762-767`](../../webui/app.js#L762-L767) 替换为基于 SVG `<circle>` 实际半径 `r` 的自适应周长公式：

```javascript
  const gpCircle = document.getElementById("gp-circle");
  const prog = job.progress || {};
  const pct = Math.max(0, Math.min(100, prog.pct || 0));
  if (gpCircle) {
    const r = Number(gpCircle.getAttribute && gpCircle.getAttribute("r")) || 18;
    const circumference = +(2 * Math.PI * r).toFixed(2); // r=18 -> 113.10
    gpCircle.setAttribute("stroke-dasharray", String(circumference));
    gpCircle.setAttribute("stroke-dashoffset", String(+(circumference - (pct / 100) * circumference).toFixed(2)));
  }
```

---

### 3.3 Task `WUI-S3-03` & `WUI-S3-04`：修正 [`webui/app.css`](../../webui/app.css)（对比度、原生 `<select>` 暗色背景、页脚沉底、单层滚动与 GPU 休眠）

1. **在 [`webui/app.css:11-24`](../../webui/app.css#L11-L24) 声明 `color-scheme: dark` 并提升 `--text-muted` 至 WCAG AA 达标色值 `#7a887a`**：
```css
:root {
  color-scheme: dark;

  /* Deep Obsidian Surface Scale */
  --bg:          #050706;
  --surface-1:   #0a0e0a;
  --surface-2:   #101510;
  --surface-3:   #171d17;
  --surface-4:   #1e261e;
  --surface-5:   #263026;

  /* Typography (WCAG 2.1 AA compliant: --text-muted #7a887a >= 5.01:1 on surface-2) */
  --text:        #e6ece6;
  --text-2:      #a8b4a8;
  --text-muted:  #7a887a;
```

2. **在 [`webui/app.css:130-137`](../../webui/app.css#L130-L137) 增加转码运行期与无障碍减动效的 GPU 合成器休眠规则**：
```css
body[data-status="running"] .ambient-orb {
  animation-play-state: paused;
}

@media (prefers-reduced-motion: reduce) {
  .ambient-orb,
  .engine-status.chip-run .engine-dot,
  body[data-status="running"] .progress-fill::after,
  body[data-status="running"] .btn-primary,
  .glass-panel {
    animation: none !important;
    transition: none !important;
  }
}
```

3. **在 [`webui/app.css:374-394`](../../webui/app.css#L374-L394) 为 `select` 与 `select option` 补齐深色实底背景，根治白底白字失明**：
```css
select {
  appearance: auto;
  cursor: pointer;
  background-color: var(--surface-2);
}

select option,
select optgroup {
  background-color: var(--surface-2);
  color: var(--text);
}
```

4. **在 `webui/app.css:525-540, 691-698, 1174-1184, 1213` 修正三栏高度、消除双重滚动条、补齐 `.tele-card` 并固定 `.app-footer` 沉底**：
```css
/* Encoder 布局与单层滚动 */
.enc-layout {
  display: grid;
  grid-template-columns: 240px minmax(440px, 1fr) 350px;
  gap: 14px;
  height: calc(100vh - var(--topbar-h) - 64px);
}

.enc-presets { overflow-y: auto; }
.enc-center { display: flex; flex-direction: column; gap: 14px; min-width: 0; overflow-y: auto; }
.enc-inspector { display: flex; flex-direction: column; overflow: hidden; }
.inspector-scroll { flex: 1; min-height: 0; overflow-y: auto; padding-right: 2px; }

/* 补齐 .tele-card 定义 */
.tele-card { display: flex; flex-direction: column; justify-content: center; min-width: 0; }

/* Footer 沉底（同时兼容在 main-content 内或 body 下） */
.app-footer {
  flex-shrink: 0;
  padding: 6px 28px 8px;
  color: var(--text-muted);
  font-size: 10px;
  text-align: center;
  background: rgba(8, 12, 8, 0.75);
  border-top: 1px solid var(--border);
  letter-spacing: 0.3px;
  position: relative;
  z-index: 10;
}

body > .app-footer {
  position: fixed;
  left: var(--sidebar-w);
  right: 0;
  bottom: 0;
  padding-left: 28px;
}
```
（同时在 `@media (max-width: 900px)` 中将 `.app-footer { padding-left: 84px; }` 替换为 `body > .app-footer { left: 60px; }`）。

---

### 3.4 Task `WUI-S3-05B`：同步 [`webui/README.md:68-89`](../../webui/README.md#L68-L89) 的 20 参数契约表

将 [`webui/README.md:68-89`](../../webui/README.md#L68-L89) 表格更新为与 [`include/rtx/param_schema.h:59-80`](../../include/rtx/param_schema.h#L59-L80) 及 `webui/index.html` 完全一致的 20 项参数定义：

| 序号 | 参数名 (JSON `data-bind`) | CLI 映射 | 类型 | 默认值 | 允许取值 / 说明 |
| :---: | :--- | :--- | :---: | :---: | :--- |
| 1 | `input` | `--input` | string | `""` | 输入视频源文件路径 |
| 2 | `output` | `--output` | string | `""` | 输出文件路径（空则按 `_remaster_<codec>.mp4` 自动派生） |
| 3 | `codec` | `--codec` | enum | `"av1"` | `"av1"`, `"hevc"`, `"av1_cpu"`, `"hevc_cpu"` |
| 4 | `preset` | `--preset` | int | `5` | NVENC `1..7` (`P1..P7`) / SVT-AV1 `0..13` |
| 5 | `rateControl` | `--rate-control` | enum | `"cq"` | `"cq"`, `"crf"`, `"vbr"`, `"cbr"`, `"qp"` |
| 6 | `bitrateKbps` | `--bitrate` | int | `0` | `0..200000` kbps（`0` 为自动） |
| 7 | `qp` | `--qp` | int | `22` | `0..51`（CQ / QP / CRF 统一目标质量值） |
| 8 | `resolution` | `--resolution` | enum | `"4k"` | `"4k"`, `"2k"`, `"fhd"`, `"native"` |
| 9 | `geometry` | `--pillarbox` | enum | `"auto"` | `"auto"`, `"pillarbox"` |
| 10 | `vsrEnabled` | `--no-vsr` | bool | `true` | 是否启用 RTX VSR 超分辨率 |
| 11 | `vsrQuality` | `--vsr-quality` | int | `4` | `1..4`（默认 Level 4 Ultra） |
| 12 | `peakNits` | `--peak-nits` | int | `1000` | `400..4000` nits（TrueHDR 峰值亮度） |
| 13 | `fpsMode` | `--target-fps` | enum | `"60-nvofa"` | `"original"`, `"60-nvofa"`, `"120-nvofa"`, `"60-rife"`, `"120-rife"` |
| 14 | `pipelineOrder` | `--pipeline-order` | enum | `"vsr-first"` | `"vsr-first"`（默认先超分后补帧）, `"fg-first"`（先补帧后超分） |
| 15 | `rifeEngine` | `--rife-engine` | string | `""` | RIFE TensorRT `.engine` 模型路径 |
| 16 | `copyAudio` | `--no-audio` | bool | `true` | 音轨与内挂字幕无损直通 |
| 17 | `hdr10plus` | `--hdr10plus` | bool | `false` | SMPTE ST 2094-40 动态元数据注入 |
| 18 | `lut` | `--lut` | string | `""` | `.cube` 3D LUT 调色文件路径 |
| 19 | `gpu` | `--gpu` | int | `0` | `0..15`（CUDA/NVENC 设备序号） |
| 20 | `frames` | `--frames` | int | `0` | `0..100000000`（帧数上限，`0` 为全片） |

---

## 4. 样式重构终局核验清单（Checklists）

- [ ] **Gate WUI-S3.1（页脚沉底与三栏满幅门禁）**：浏览器打开 `http://127.0.0.1:8787/`，通过 DevTools 检查 `<footer class="app-footer">` 的 `getBoundingClientRect()`，确认其位于视口底部（`bottom == window.innerHeight`，宽度等于 `window.innerWidth - 220px`），屏幕右侧绝无第 3 列垂直黑条。
- [ ] **Gate WUI-S3.2（进度环 0% 归零门禁）**：在 `idle` 就绪态下检查 `#gp-circle` 的 `stroke-dasharray` 与 `stroke-dashoffset`，两者严格相等（均为 `113.1`），圆环 0% 状态下无任何残留绿弧。
- [ ] **Gate WUI-S3.3（暗色下拉框与 WCAG AA 对比度门禁）**：在 Windows Edge/Chrome 下点击任意 `<select>`，展开菜单为深色背景（`#101510`）配浅白文字（`#e6ece6`）；`--text-muted` 为 `#7a887a`（对比度 `>= 5.0:1`）。
- [ ] **Gate WUI-S3.4（转码期 GPU 零背景重绘与离线加载门禁）**：断开外网连接刷新页面，`Network` 面板零外部域名请求、零失败请求；当 `body[data-status="running"]` 时，`.ambient-orb` 的 `getComputedStyle(el).animationPlayState === "paused"`。
