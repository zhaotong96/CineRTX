# **71\_wave1-B\_GUI拖放即入队\_Pending靶向联动与120-rife防回落施工单**

Superpowers 原子施工规约 — 同步 `a6451b7` 基线与 56 号外部专家统一状态机

## **1\. 最新状态同步与宪法硬红线**

### **1.1 最新状态同步**

* **基线确认与回滚恢复**：`54_` 改动已全部回滚清除，代码库完全回退并对齐至 `a6451b7` 基线（依据 `55` 号委托书）。  
* **缺陷根因与方案裁决**：  
  * `51` 号委托书指出的关键问题：用户拖拽/入队冻结默认的 `"60-nvofa"` 参数后，在 UI 改选 `120-rife` 并点击 Start，系统未将更改同步给已处于 `Pending` 状态的队列任务，导致最终仍执行旧的 `60-nvofa` 参数。  
  * `54_` 尝试修补却破坏了单文件拖放入队与队列自动刷新，导致 `55` 号回归测试中拖放功能完全失效。  
  * `56` 号外部专家方案裁决：采用\*\*"即拖即入队 \+ `active_sync_ids` 靶向联动 \+ 路径感知启动同步"\*\*统一状态机模型。该模型既能保证拖放立即可见、支持"先拖放再改选 `120-rife`"实时生效，又能实现多任务异构参数的精确隔离。  
* **增量落地定位**：`50` 号汇报已修改 `src/gui/rtx_gui.cpp:L320-L329`，确保在 `applyProbedMedia()` 中同步填充 `app.playbackInfo`；本波次在 `a6451b7` 当前工作区增量落地完整的 GUI 拖放即入队与 `120-rife` 同步闭环。

### **1.2 宪法硬红线**

| 检查项 | 规约标准与执行要求 | 判定状态 |
| :---- | :---- | :---- |
| **AuditTypes** | `AuditTypes == 34/35`（零新增具名类型，仅在既有 `app` 状态结构体中追加 `selected_job_id` 与 `std::set<uint64_t> active_sync_ids` 字段）。 | **PASS** |
| **AuditImports** | GUI `<= 4` / HEADLESS `== 2`（`shell32.dll` 经 `sys_dynload` 动态加载，且必须确保在所有异常/提前退出路径中严格执行 `DragFinish(hDrop)`）。 | **PASS** |
| **AuditPrivacy** | `AuditPrivacy = 0`（严禁泄露本地文件路径、用户隐私数据或无授权外发日志）。 | **PASS** |
| **AuditSpdx** | `AuditSpdx = PASS`（所有源文件头部保持合规开源许可声明）。 | **PASS** |

---

## **2\. Task 1：落地统一交互状态机**

必须在 `src/gui/rtx_gui.cpp` 相关代码坐标精确落地"即拖即入队 \+ `active_sync_ids` 靶向联动 \+ 路径感知启动同步"统一交互状态机。

### **2.1 精确代码坐标**

* `WM_DROPFILES`（`src/gui/rtx_gui.cpp:L676-L695`）  
* `onQueueAdd()`（`src/gui/rtx_gui.cpp:L530-L546`）  
* `onStartTranscode()`（`src/gui/rtx_gui.cpp:L358-L398`）  
* `CBN_SELCHANGE`（`src/gui/rtx_gui.cpp:L799`）  
* `populateComboBoxes()`  
* `applyProbedMedia()`（`src/gui/rtx_gui.cpp:L320-L329`）

### **2.2 七类交互流转规则**

```
[拖入单/多文件 / 点击 + 追加]
       │
       ▼
   探测媒体信息
       │
       ▼
 按 current_profile 立即入队 (Pending)
       │
       ├─► 设 selected_job_id & active_sync_ids
       └─► 刷新 UI 队列列表并高亮选中
       │
 [切换 Combobox (如 120-rife) / 调整参数]
       │
       ▼
  CBN_SELCHANGE 触发 updatePendingJob()
       │
       ▼
 仅靶向更新 active_sync_ids 且处于 Pending 的任务 (队列实时显示 120-rife)
       │
 [点击 Start Transcode]
       │
       ▼
  路径感知校验 ──► 匹配现有 Pending？ ──► 是: 最终快照同步 ──► 启动 Worker
                        │
                        └─────────────► 否: 自动补入队列 ──► 启动 Worker
```

#### **规则 1：单文件拖入 Encoder 窗体（`WM_DROPFILES`）**

1. 读取拖拽句柄，探测媒体属性。  
2. 按照当前 UI 配置（`collectProfileFromUi`）**立即入队**生成 `Pending` 任务 \$J\_k\$。  
3. 更新状态：`selected_job_id = J_k`，`active_sync_ids = {J_k}`。  
4. 刷新队列列表并高亮选中 \$J\_k\$。  
5. 将文件路径写入 `IDC_EDT_INPUT`，执行非破坏性 `applyProbedMedia`，状态栏显示 `Queued J_k: filename`。  
6. **隔离约束**：绝不改写此前已有队列项的参数。

#### **规则 2：多文件批量拖入 Encoder 窗体**

1. 循环解析句柄中的全部文件，逐一探测并立即入队为 `Pending` 任务组 \${J\_1, J\_2, \\dots, J\_m}\$。  
2. 更新状态：`active_sync_ids = {J_1, J_2, ..., J_m}`，`selected_job_id = J_1`。  
3. 队列列表立即全量显示 \$m\$ 个待办项，焦点高亮选中首项 \$J\_1\$。

#### **规则 3：拖入后切换 `IDC_CMB_FPS`（如 `120-rife`）或调节任意参数**

1. 控制台/控件触发 `CBN_SELCHANGE` 或 `EN_CHANGE` 等通知，更新 `guiProfile(app)`。  
2. 遍历 `active_sync_ids` 集合，对其中状态仍为 `JobStatus::Pending` 的任务调用 `updatePendingJob(id, current_profile)`。  
3. 实时刷新队列列表对应行摘要（清楚标出 `120-rife`）。  
4. **隔离约束**：不在 `active_sync_ids` 内的既有任务以及已运行/已完成的任务绝不被改写。

#### **规则 4：队列列表中主动选中某任务 \$J\_i\$**

1. 设置 `selected_job_id = J_i`。  
2. 更新同步联动集合：若 \$J\_i\$ 处于 `Pending` 状态，则收窄 `active_sync_ids = {J_i}`；若 \$J\_i\$ 非 `Pending`（已完成或正在运行），则清空 `active_sync_ids = {}`。  
3. 将 \$J\_i\$ 保存的独立 `TranscodeProfile` 与输入路径反向回填至检查器控件与 `IDC_EDT_INPUT`。

#### **规则 5：点击 `+`（`onQueueAdd`）主动追加任务**

1. 根据当前 `guiProfile(app)` 新建独立的 `Pending` 任务 \$J\_{\\text{new}}\$。  
2. 重置 `selected_job_id = J_new` 并且 `active_sync_ids = {J_new}`。  
3. 队列列表追加该行并高亮选中。

#### **规则 6：浏览替换 `IDC_EDT_INPUT` 后点击 `Start Transcode`**

1. `onStartTranscode()` 首先调用 `collectProfileFromUi()` 获取当前界面最新快照。  
2. **路径感知分支**：  
   * 若 `selected_job_id` 对应的任务处于 `Pending` 且其输入路径与 `IDC_EDT_INPUT` 一致：再次同步执行一次该任务的配置快照覆盖；  
   * 若 `IDC_EDT_INPUT` 指向了一个未在 `Pending` 队列中出现的新文件路径：在启动前自动按 `current_profile` 将其补入队列，再调用 `startWorker()`。

#### **规则 7：非 Encoder 工作区（如 Player）拖放**

1. 捕获拖拽目标句柄区域。  
2. 直接交由播放器媒体加载入口处理，禁止触发 Encoder 队列更新逻辑，不污染 Encoder 任务队列。

---

## **3\. Task 2：Win32 下拉框同步防踩坑与 120-rife 防静默回落**

### **3.1 Win32 下拉框索引同步防踩坑**

在 `populateComboBoxes()`（包括语言切换重构条目时）或队列行切换反向回填时，必须严格执行索引校对流程：

```
// 查找 target fps_mode 在 kFpsModes 中的精确索引
int target_idx = -1;
for (int i = 0; i < 5; ++i) {
    if (kFpsModes[i] == profile.fps_mode) {
        target_idx = i;
        break;
    }
}
if (target_idx != -1) {
    SendDlgItemMessage(hDlg, IDC_CMB_FPS, CB_SETCURSEL, (WPARAM)target_idx, 0);
    // 关键防踩坑：CB_SETCURSEL 不会触发 CBN_SELCHANGE 通知
    // 必须手动校对并强制更新内部状态，防止复位回 60-nvofa (索引 1)
    LRESULT real_idx = SendDlgItemMessage(hDlg, IDC_CMB_FPS, CB_GETCURSEL, 0, 0);
    if (real_idx != CB_ERR) {
        app.profile.fps_mode = kFpsModes[real_idx];
    }
}
```

### **3.2 `applyProbedMedia()` 非破坏性原则**

`applyProbedMedia()` 仅更新从文件探测到的媒体元数据（如原始分辨率、帧率、时长、视频/音频编码格式），**严格禁止**覆盖用户在 UI 上已经手动选择或切换的 fps\_mode、encoder、vsr\_mode 或 quality\_preset 参数。

### **3.3 `120-rife` 防静默回落校验机制**

在 `onStartTranscode()` 启动转码引擎前，必须增加硬性参数校验逻辑：

```
if (app.profile.fps_mode == "60-rife" || app.profile.fps_mode == "120-rife") {
    // 1. 强校验目标帧率结构体
    const uint32_t expected_fps = (app.profile.fps_mode == "120-rife") ? 120 : 60;
    if (app.profile.target_fps.num != expected_fps || app.profile.target_fps.den != 1) {
        app.profile.target_fps = { expected_fps, 1 };
    }

    // 2. 校验 RIFE 模型与分桶族路径有效性
    if (!validateRifeModelPath(app.profile)) {
        SetDlgItemText(hDlg, IDC_ST_STATUS, L"Error: RIFE model path invalid. Aborting start.");
        return; // 严禁静默回落为 nvof 60fps
    }

    // 3. 状态栏显示真实生效路由
    wchar_t status_buf[256];
    swprintf_s(status_buf, L"Engine Starting: %s via RIFE Pipeline", 
              (app.profile.fps_mode == "120-rife") ? L"120fps" : L"60fps");
    SetDlgItemText(hDlg, IDC_ST_STATUS, status_buf);
}
```

---

## **4\. 验收 Gate**

施工完成后，必须通过以下断言测试与静态审计：

| 序号 | 验收场景 | 预估结果 / 判定标准 |
| :---- | :---- | :---- |
| **G-1** | 单文件拖放测试 | 拖入单文件后，UI 队列**立即可见** \$J\_1\$ 任务，状态为 `Pending`。 |
| **G-2** | 批量拖放测试 | 一次性拖入 3 个文件，队列立即显示 3 个 `Pending` 项，首项高亮选中。 |
| **G-3** | 先拖后切 `120-rife` | 拖放后将 UI 下拉框切至 `120-rife`，队列摘要**实时同步标出** `120-rife`。 |
| **G-4** | 参数隔离校验 | 选中 \$J\_1\$ 改为 `120-rife`，新建/拖入 \$J\_2\$ 改为 `60-nvofa`，再次点击 \$J\_1\$，UI 正确回填 `120-rife`，两任务互不干扰。 |
| **G-5** | `120-rife` 运行校验 | 点击 Start Transcode，日志与底层 Pipeline 输出的目标帧率为 `120/1`，**无静默回落 `60-nvofa`** 现象。 |
| **G-6** | `AuditTypes` 校验 | `AuditTypes` 数值在 `34/35` 范围内，无额外具名结构体新增。 |
| **G-7** | `AuditImports` 校验 | GUI 导入表依赖 `<= 4`（`shell32.dll` 动态加载），HEADLESS `== 2`，异常路径均包含 `DragFinish`。 |

