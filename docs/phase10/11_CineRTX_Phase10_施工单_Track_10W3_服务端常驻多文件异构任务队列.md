# CineRTX Phase 10：WebUI 施工单 10W-3 — 服务端常驻多文件异构任务队列与无头调度引擎

- **施工单编号**：`CINERTX-P10-WUI-TASK-10W3`
- **对应评审缺陷**：`WUI-P10-04`（P0 级阻断缺陷）
- **母本来源**：[`08_CineRTX_Phase10_WebUI_v2重构评审与三端对齐SuperPower规范.md`](08_CineRTX_Phase10_WebUI_v2重构评审与三端对齐SuperPower规范.md)
- **施工目标**：彻底拔除浏览器端脆弱的 `setTimeout` `queuePump`，将队列调度全权下沉至 `webui/server.py` 的常驻后台工作线程；支持单任务 `1..N` 个输入文件（多文件批处理）；支持队列动态编排操作（上移、下移、跳过、重试、清空、启停）。

---

### 一、 核心病灶与根因分析

1. **浏览器依赖型断链调度**：
   原有设计中，整个连续队列调度由前端 JavaScript 监听 `lastJobStatus === "running" -> current !== "running"`，通过 `setTimeout(..., 350)` 发起下一次任务的 HTTP 请求。若用户关闭浏览器、刷新页面或发生网络断连，后端进程跑完当前单文件后立即彻底停摆。
2. **多文件输入能力缺失**：
   前端队列项仅支持单个 `input` 字符串，不支持单任务绑定多个文件输入；同时文件浏览模态框不支持多选勾选。
3. **调度动作匮乏**：
   缺少服务端原生提供的 `move`（优先级调整）、`skip`（优雅跳过当前任务并继续推进）、`retry`（重试失败任务）等完整队列动作。

---

### 二、 施工改造步骤

#### 1. 重写 `webui/server.py` 服务端多文件队列引擎

在 `webui/server.py` 中升级 `JobRunner` 与 `_job()` 处理流程：
- **动作协议集合**：
  - `enqueue`：接收 `{action: "enqueue", inputs: [...], profile: {...}, presetName: "..."}`，生成全局唯一 ID，将任务追加至 `queue` 并持久化到 `state.json`；若工作线程正在循环中，该任务将被自动消费；
  - `update`：对处于 `pending` 状态的任务更新其 `profile` 与 `inputs`；
  - `remove`：拦截运行中任务，删除已完成/排队任务并回写状态；
  - `move`：接收 `{action: "move", jobId: "...", delta: -1 | 1}`，在 `pending` 列表中调整执行次序；
  - `skip`：向正在运行的子进程写入取消指令，将当前任务标记为 `"cancelled"`，**后台工作线程保持激活，自动弹出下一个 `pending` 任务继续转码**；
  - `retry`：将 `"failed"` 或 `"cancelled"` 任务的进度与状态重置回 `"pending"`；
  - `clear`：清理所有已完成任务；
  - `start` / `stop`：控制常驻工作线程的主循环。
- **多文件迭代逻辑（`inputs: 1..N`）**：
  对单个任务内的 `inputs` 数组进行顺序转码，自动根据任务的 `codec` 派生独立输出文件名，计算子进度与整体总进度 `totalJobPct = ((i * 100.0) + pct) / len(inputs)`，通过 SSE 实时向前端广播。

#### 2. 升级 `webui/index.html` 队列工具栏与多文件浏览模态框

- **在 `.queue-toolbar` 中部署完整编排按钮组（`index.html:179-204`）**：
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
- **在文件浏览器模态框 `#fs-modal` 中实现多文件多选能力**：
  当以 `data-browse-mode="queue-import"` 呼起时，列表渲染多选复选框，并提供“全选视频文件”快捷按钮，确定后将勾选的多个路径作为 `inputs` 数组一次性生成异构任务入队。

#### 3. 清理 `webui/app.js` 浏览器端泵逻辑

- 彻底删除 `function queuePump` 与 `startQueueItem` 逻辑；
- 前端按钮仅作为纯粹的 Command 触发者向 `POST /api/job` 发送动作，渲染逻辑完全基于 SSE 广播的 `state` 与 `runtime` 状态驱动。

---

### 三、 验收检查点（Gate 10W-3）

- [ ] `rg "function queuePump" webui/app.js` 结果为 0 匹配；
- [ ] 批量导入包含 3 个视频文件的任务，启动转码后即使完全关闭浏览器窗口，服务端依然能持续将 3 个文件连续跑完；
- [ ] 点击「⏭ 跳过当前」，正在运行的任务被标记为 `cancelled`，后端工作线程立即无缝切入下一个 `pending` 任务。
