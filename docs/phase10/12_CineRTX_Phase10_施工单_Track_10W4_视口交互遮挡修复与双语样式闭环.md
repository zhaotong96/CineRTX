# CineRTX Phase 10：WebUI 施工单 10W-4 — 视口交互拖拽、占位遮挡修复与双语样式闭环

- **施工单编号**：`CINERTX-P10-WUI-TASK-10W4`
- **对应评审缺陷**：`WUI-P10-05`（P1 级交互与样式缺陷）
- **母本来源**：[`08_CineRTX_Phase10_WebUI_v2重构评审与三端对齐SuperPower规范.md`](08_CineRTX_Phase10_WebUI_v2重构评审与三端对齐SuperPower规范.md)
- **施工目标**：修复 Player 舞台 A/B 分屏手柄连续拖拽；根除 NLE 与 Encoder 视口内占位文案对真实视频的画面遮挡；打通 NLE 播放头（Playhead）与 `Flicks` 时间基的双向同步；修正 CSS 媒体查询语法错误并补齐双语词条。

---

### 一、 核心病灶与根因分析

1. **Player A/B 拖拽失效**：
   `installPlayer()` 仅监听了 `pointerdown` 事件，未调用 `setPointerCapture`，缺失 `pointermove` 与 `pointerup` 的连续跟踪，导致鼠标拖动分屏线无法实时平滑切分画面。
2. **视口占位层视觉遮挡**：
   在 `webui/app.css` 中，`.nle-viewport .stage-placeholder` 与 `.ab-fallback` 缺少隐藏规则，即便视频元素已成功挂载，遮罩依然覆盖在画面上方；同时 NLE 播放按钮未支持播放/暂停状态翻转，未监听 `timeupdate` 同步时间码。
3. **CSS 语法错误与词条断层**：
   `webui/app.css` 第 1248 行在 `@media (max-width: 900px)` 内裸写 CSS 变量 `--sidebar-w: 60px;`（未声明 `:root` 选择器），导致样式规则被浏览器内核自动丢弃；左侧导航等按钮使用了前端特有但未在双语字典注册的词条。

---

### 二、 施工改造步骤

#### 1. 完善 Player A/B 分屏指针捕获（`webui/app.js:1093-1106`）

使用现代 Pointer Events API 建立连续拖拽闭环：
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

#### 2. 视口占位层隐藏、播放控制与 Flicks 时间轴驱动

- **在 `webui/app.css` 中补充隐藏规则**：
  ```css
  .ab-viewport.has-media .ab-fallback,
  .nle-viewport.has-media .stage-placeholder {
    display: none !important;
  }
  ```
- **在 `webui/app.js` 中联动 `.has-media` 样式类**：
  在挂载视频时执行：
  ```javascript
  document.getElementById("encoder-ab")?.classList.toggle("has-media", Boolean(path));
  document.querySelector(".nle-viewport")?.classList.toggle("has-media", Boolean(path));
  ```
- **修复 NLE 播放控制与时间码步进（`webui/app.js:1347-1350`）**：
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
- 将 `webui/index.html:594` 中的文案修复为 `<label for="nle-frames" data-i18n="WuiParamFrames">帧数上限</label>`。

#### 3. 修复 CSS 媒体查询语法与双语国际化词条

- **修复 `webui/app.css:1248`**：
  ```css
  @media (max-width: 900px) {
    :root {
      --sidebar-w: 60px;
    }
    .sidebar {
      width: 60px;
    }
  }
  ```
- **在 `webui/app.js` 词典中注入前端专属双语项**：
  为 `WuiNavEncoder`、`WuiNavPlayer`、`WuiNavNle` 等界面专属键值提供中英文双语映射，并在切换语言时统一刷新所有带 `[data-i18n]` 的 DOM 节点。

---

### 三、 验收检查点（Gate 10W-4）

- [ ] 在 Player 舞台按住分屏线平滑拖拽，画面切分比例与当前百分比数值跟随变化，释放后自动落盘；
- [ ] 载入视频文件后，预览视口中心不再出现占位文字遮挡；
- [ ] 点击 NLE 播放按钮，视频正常播放，时间轴游标平稳滑动；再次点击能够正常暂停；
- [ ] 点击中英文切换按钮，全界面所有按钮、表单、提示 100% 切换，无任何空白或残留中文。
