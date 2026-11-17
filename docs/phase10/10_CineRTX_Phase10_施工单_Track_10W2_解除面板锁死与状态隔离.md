# CineRTX Phase 10：WebUI 施工单 10W-2 — 解除运行期锁死、根治越权篡改与持久化竞态

- **施工单编号**：`CINERTX-P10-WUI-TASK-10W2`
- **对应评审缺陷**：`WUI-P10-02` 与 `WUI-P10-03`（P0 级阻断缺陷）
- **母本来源**：[`08_CineRTX_Phase10_WebUI_v2重构评审与三端对齐SuperPower规范.md`](08_CineRTX_Phase10_WebUI_v2重构评审与三端对齐SuperPower规范.md)
- **施工目标**：彻底废除右侧参数检查器的运行期 `data-lock` 锁死，建立“草稿态”与“排队任务检视态”双模式隔离；根除 `syncProfileToQueue` 对运行中/已完成任务的跨任务越权篡改，消除 `flushPersist` 空操作竞态与运行中任务误删。

---

### 一、 核心病灶与根因分析

1. **`data-lock` 运行期全局瘫痪**：
   `runEffects()` 在检测到 `state.runtime.job.status === "running"` 时，将所有带 `[data-lock]` 的参数控件及浏览按钮设为 `disabled = true`。这导致后台转码期间用户无法配置新参数、无法浏览文件、无法添加新任务入队。
2. **`syncProfileToQueue` 跨任务越权覆写**：
   `syncProfileToQueue()` 盲目依据 `selectedQueueId` 覆写队列项，未断言 `items[index].status === "pending"`。当用户点击过 Job #1 导致其被选中，后续 Job #2 启动或在其他面板修改 `profile` 时，会强行将新参数篡改到已完成或正在跑的 Job #1 上。
3. **`flushPersist` 成为 No-op 空操作**：
   `startQueueItem()` 触发修改后未调用 `schedulePersist()`，导致内部 `persistTimer === null`，随后的 `await flushPersist()` 没有任何写入动作，内存状态未落盘。
4. **运行中任务孤儿化**：
   `- 移除任务` 按钮未拦截正在运行的 `status === "running"` 任务，导致后台执行与前端队列失去锚定。

---

### 二、 施工改造步骤

#### 1. 物理拔除 `webui/index.html` 中的 `data-lock`（`index.html:292-450`）

- 彻底移除右侧参数面板 `#panel-params` 中所有控件标签上的 `data-lock` 属性；
- 在参数检查器顶部挂载当前上下文指示条：
  ```html
  <div id="inspector-context-bar" class="context-bar">
    <span id="inspector-mode-text" class="sub">草稿模式 · 修改后点击「+ 加入队列」</span>
    <button id="btn-queue-deselect" class="btn btn-xs btn-ghost" hidden>返回新任务草稿</button>
  </div>
  ```

#### 2. 重构 `selectQueueItem`、`syncProfileToQueue` 与取消选中机制（`webui/app.js`）

- **实现双模式隔离与安全同步**：
  ```javascript
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
    // 铁律：仅允许对处于 pending 状态的排队任务进行实时参数回写
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

#### 3. 根治 `flushPersist` 立即刷盘与拦截删除正在运行任务

- 修正 `flushPersist`：无论 `persistTimer` 是否已激活，均无条件直接向 `/api/state` 提交最新状态树：
  ```javascript
  async function flushPersist() {
    if (persistTimer) {
      clearTimeout(persistTimer);
      persistTimer = null;
    }
    await api("/api/state", "PUT", ownedSections()).catch(() => {});
  }
  ```
- 移除任务按钮 `#btn-queue-remove` 严格增加运行态校验：
  ```javascript
  const target = (state.queue || []).find((j) => j.id === selectedQueueId);
  if (target && target.status === "running") {
    toast("无法直接删除正在运行的任务，请使用「跳过」或「停止」", "warn");
    return;
  }
  ```

---

### 三、 验收检查点（Gate 10W-2）

- [ ] `#panel-params` 中所有 `data-lock` 物理清零，执行转码期间输入框、下拉框与浏览按钮均保持可用；
- [ ] 当队列中 Job #1 处于 `running` 或 `done` 状态且已被选中时，修改右侧参数检查器中的参数，断言 Job #1 的所有字段不受任何污染篡改；
- [ ] 仅当点击选中 `pending` 状态任务时，上下文提示条变为“正在编辑排队任务”，且参数变更能实时同步写入该任务的快照并落盘。
