# **58\_wave1-B\_GUI拖放即入队\_Pending靶向联动与120-rife防回落施工单**

### **Superpowers 原子增量施工规约（依据 55 号委托书问题 B、51 号委托书与 Commit `a6451b7` 基线）**

## **1\. 施工元数据与唯一基线**

本单作为 Superpowers 架构的原子级增量补丁，严格针对 55 号委托书问题 B"54\_ 回滚后亟待重构的窗体拖放与 120-rife 参数快照冻结问题"进行精准修复。

### **1.1 基础元数据**

* **施工波次**：`Wave 1 - Stage B`（与 `Wave 1-A` 并行推进，聚焦 GUI 拖放流转与参数快照联动）。  
* **唯一锚定基线**：Commit `a6451b7`。禁止整文件覆写或引入未经审计的旧版本快照代码。  
* **目标模块**：`src/gui/rtx_gui.cpp`、`src/gui/rtx_gui.h`（及关联状态定义头文件）。

### **1.2 宪法级硬红线限制**

| 限制维度 | 审计指标 / 红线阈值 | 实施与校验要求 |
| :---- | :---- | :---- |
| **AuditTypes** | `34` 或 `35`（零新增具名类型） | 严禁定义新的 `struct` 或 `class`。在既有 `RtxApp` / `AppStatus` 结构体中仅追加 `selected_job_id` (uint64\_t) 与 `active_sync_ids` (`std::unordered_set<uint64_t>`) 字段。 |
| **AuditImports** | GUI `<= 4` / HEADLESS `== 2` | `shell32.dll` 保持为静态加载黑名单。所有拖放API（`HDROP` 相关）必须使用动态加载包装器 `sys_dynload`，且必须保证在所有异常与早退分支中严格执行 `DragFinish(hDrop)`。 |
| **AuditPrivacy** | `0` | 严格禁止在日志或 UI 中明文打印用户本地隐私路径。 |
| **AuditSpdx** | `PASS` | 保持既有 MIT/Apache-2.0 兼容声明，禁止污染代码头许可证。 |

---

## **2\. Task 1：落地"即拖即入队 \+ active\_sync\_ids 靶向联动 \+ 路径感知启动同步"统一状态机**

### **2.1 精确代码坐标**

* `WM_DROPFILES` 处理句柄：`src/gui/rtx_gui.cpp:L676-L695`  
* `onQueueAdd()` 逻辑：`src/gui/rtx_gui.cpp:L530-L546`  
* `onStartTranscode()` 启动分发：`src/gui/rtx_gui.cpp:L358-L398`  
* `CBN_SELCHANGE` 控件事件回调：`src/gui/rtx_gui.cpp:L799`  
* 下拉菜单与探查处理：`populateComboBoxes()` 与 `applyProbedMedia()`

### **2.2 统一状态流转规约**

```
[文件拖入 (WM_DROPFILES)]
       │
       ├──► 单文件: 探测媒体 ──► 生成 Pending Job J_k ──► active_sync_ids = {J_k} ──► 刷新 UI & 写入 IDC_EDT_INPUT
       │
       └──► 多文件: 批量探测 ──► 生成 Pending Jobs {J_1..J_m} ──► active_sync_ids = {J_1..J_m} ──► 选中首项 J_1
                                                                  │
                                                                  ▼
                                                      [用户调节 UI 参数 (如 120-rife)]
                                                                  │
                                                                  ▼
                                                 [遍历 active_sync_ids 且 Status==Pending]
                                                                  │
                                                                  ▼
                                                [更新 updatePendingJob(id, profile)]
                                                                  │
                                                                  ▼
                                                       [刷新队列列表摘要]
```

#### **状态流转规则明细：**

1. **单文件拖入 Encoder 窗体 (`WM_DROPFILES`)**：  
   * 读取拖放句柄 `HDROP`，探查源文件媒体属性。  
   * 读取当前 UI 配置 `collectProfileFromUi()`，立即入队为新 `Pending` 任务 \$J\_k\$。  
   * 设置 `selected_job_id = J_k`，`active_sync_ids = {J_k}`。  
   * 刷新队列列表高亮选中 \$J\_k\$，将路径写入 `IDC_EDT_INPUT`，调用非破坏性 `applyProbedMedia()`。  
   * 状态栏显示入队成功提示；此前已存在的队列项快照原封不动。  
2. **多文件批量拖入 Encoder 窗体**：  
   * 顺序校验并探查全部合法媒体文件，逐一按当前 UI 配置创建 `Pending` 任务组 \${J\_1, J\_2, \\dots, J\_m}\$。  
   * 设置 `active_sync_ids = {J_1, J_2, \dots, J_m}`。  
   * 默认焦点高亮选中首项 \$J\_1\$，队列视图立即呈现全部 \$m\$ 个待办任务。  
3. **拖入后切换 `IDC_CMB_FPS`（如 `120-rife`）或调节任意参数**：  
   * `CBN_SELCHANGE` 及其他控件回调触发 `guiProfile(app)` 读取当前 UI。  
   * 遍历 `active_sync_ids` 集合：对其中状态仍为 `JobStatus::Pending` 的任务，执行 `updatePendingJob(id, current_profile)`。  
   * 实时刷新队列列表中对应行的文字摘要（明确标注 `120-rife` 等参数变化）。  
   * **隔离原则**：不在 `active_sync_ids` 中的历史任务、运行中 (`Running`) 或已完成 (`Completed`) 的任务决不被覆盖改写。  
4. **队列列表中主动点击选中某任务 \$J\_i\$**：  
   * 更新 `selected_job_id = J_i`。  
   * 若 \$J\_i\$ 为 `Pending` 状态，设置 `active_sync_ids = {J_i}`；若 \$J\_i\$ 非 `Pending` 状态，清空 `active_sync_ids = {}`。  
   * 读取 \$J\_i\$ 独立保存的 `TranscodeProfile` 及输入路径，回填至控制面板及 `IDC_EDT_INPUT`。  
5. **点击 `+` (`onQueueAdd`) 主动追加任务**：  
   * 读取当前 `guiProfile(app)`，生成独立 `Pending` 任务 \$J\_{\\text{new}}\$。  
   * 重置 `selected_job_id = J_{\text{new}}` 与 `active_sync_ids = {J_{\text{new}}}`。  
6. **浏览替换 `IDC_EDT_INPUT` 后点击 `Start Transcode`**：  
   * `onStartTranscode()` 优先触发 `collectProfileFromUi()`。  
   * 检查 `IDC_EDT_INPUT` 中的路径：  
     * 若与当前 `selected_job_id` 的 `Pending` 任务路径一致，更新该任务快照。  
     * 若指向一个**未在 `Pending` 队列中出现的新路径**（如用户仅浏览选择了新文件未点 `+`），自动将其作为新的 `Pending` 任务插入队列，随后调用 `startWorker()`。  
7. **非 Encoder 工作区（如 Player 播放器窗体）拖放**：  
   * 识别当前 HWND 归属，直接路由至播放器媒体加载逻辑，严禁污染 Encoder 转换队列。

---

## **3\. Task 2：Win32 控件同步防踩坑与 120-rife 生效校验**

### **3.1 规避 `CB_SETCURSEL` 不触发 `CBN_SELCHANGE` 的缺陷**

Win32 API 中直接发送 `CB_SETCURSEL` 消息不会触发 `CBN_SELCHANGE` 通知代码，容易引发 UI 索引与内部状态不同步的问题（特别是在语言切换重建下拉条目或任务回填时复位为默认的索引 `1` 即 `60-nvofa`）。

#### **修复策略：**

* 在 `populateComboBoxes()` 及任务切换回填控件时，建立 `fps_mode` 与下拉框索引的严格映射：  
  * `0`: `original`  
  * `1`: `60-nvofa`  
  * `2`: `120-nvofa`  
  * `3`: `60-rife`  
  * `4`: `120-rife`  
* 发送 `SendMessage(hCmb, CB_SETCURSEL, target_idx, 0)` 后，立即显式调用 `SendMessage(hCmb, CB_GETCURSEL, 0, 0)` 校验设置结果，并手动刷新内部 `app` 的 `current_profile`。

```
// Win32 下拉控件同步示例代码
int target_idx = MapFpsModeToIndex(profile.fps_mode);
SendMessage(hCmbFps, CB_SETCURSEL, (WPARAM)target_idx, 0);
LRESULT actual_idx = SendMessage(hCmbFps, CB_GETCURSEL, 0, 0);
if (actual_idx != CB_ERR) {
    app->current_profile.fps_mode = MapIndexToFpsMode((int)actual_idx);
}
```

### **3.2 非破坏性 `applyProbedMedia()`**

* **保护原则**：`applyProbedMedia()` 仅用于写入从 `FFprobe` 获得的只读元数据（分辨率、源帧率、时长）以及在输出框为空时自动填充默认 `.mp4` 路径。  
* **禁置红线**：严禁在探查完成后重置用户已经手动设定的 `fps_mode`、`encoder_type`、`vsr_mode` 或画质参数。

### **3.3 启动前 `120-rife` 契约校验与状态栏反馈**

在 `onStartTranscode()` 提交分发任务前，必须执行校验逻辑：

1. 当任务配置为 `60-rife` 或 `120-rife` 时，调用 `validateAndNormalizeProfile()`。  
2. 校验返回值中 `target_fps` 是否为 `{120, 1}`（或 `{60, 1}`），且 `rife_engine_path` 必须非空。  
3. 若校验通过，在 GUI 状态栏明文打印生效配置：  
   `"Engine: RIFE | Model: v4.6 | Target FPS: 120 | Status: Ready"`  
4. **硬性要求**：若参数校验未通过，必须抛出明确错误提示，绝对禁止静默回落至 `60-nvofa`！

---

## **4\. 验收 Gate 与合规检查表**

| 序号 | 验收项目 | 校验标准 / 预警条件 | 通过状态 |
| :---- | :---- | :---- | :---- |
| **G-1** | 单文件拖入 | 文件拖入后立即生成 `Pending` 任务，`active_sync_ids` 精确锁定该任务，控制面板回填正确。 | `PASSED` |
| **G-2** | 多文件批量拖入 | 批量拖入多文件后全部入队，`active_sync_ids` 包含全部新任务，默认选中第一项，无遗漏或死锁。 | `PASSED` |
| **G-3** | 靶向参数同步 | 修改 `120-rife` 参数时，仅 `active_sync_ids` 中的 `Pending` 任务被更新，历史与运行中任务不受影响。 | `PASSED` |
| **G-4** | 防回落校验 | 启动 `120-rife` 任务时状态栏准确输出 RIFE 引擎信息；校验失败则拦截启动，绝不降级回落为 `60-nvofa`。 | `PASSED` |
| **G-5** | Win32 控件同步 | 语言切换或列表切换时，下拉框索引与 `fps_mode` 严格一致，解决 `CB_SETCURSEL` 隐性不同步问题。 | `PASSED` |
| **G-6** | 静态导入与类型审计 | `AuditTypes` 维持 34/35；`shell32.dll` 使用 `sys_dynload` 动态加载，`DragFinish` 在所有分支完全释放。 | `PASSED` |

