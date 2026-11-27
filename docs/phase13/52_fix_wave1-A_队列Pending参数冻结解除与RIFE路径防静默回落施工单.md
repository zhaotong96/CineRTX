# **52\_fix\_wave1-A\_队列Pending参数冻结解除与RIFE路径防静默回落施工单**

Superpowers 原子靶向施工规约 \| 依据 51 号排障委托书与 Commit `a6451b7` 基线

---

## **1\. 元信息与基线宪法红线**

### **1.1 工程定位与波次协同**

* **施工波次**：`Fix Wave 1 - Stage A`（与 `Fix Wave 1-B` 并行推进）。  
* **核心目标**：彻底根除"GUI `IDC_CMB_FPS` 选 `120-rife` 却仍执行 `60-nvofa`"的致命缺陷，修复队列 `Pending` 任务快照冻结陷阱，并打通 RIFE 路径动态解析与显式错误中断机制。  
* **基线锚定**：基于 Commit `a6451b7`（测试验证全量通过：`153/153 PASS`、`AuditTypes = 34/35`、`AuditImports` GUI `<= 4` / HEADLESS `== 2`）。

### **1.2 宪法硬红线（Imperative Constraints）**

施工过程必须严格遵循以下三项不可逾越的红线：

| 红线编号 | 红线名称 | 严格限定指标 / 规则 | 违规后果 |
| :---- | :---- | :---- | :---- |
| **RED-LINE-01** | **零新增具名类型** | 严格保持 `AuditTypes == 34/35`（`+0` 具名 `struct` / `class` / `enum` / `typedef`） | 编译/审计直接拦截 |
| **RED-LINE-02** | **零新增静态 DLL** | 严格保持 `AuditImports` GUI `<= 4` / HEADLESS `== 2` | 架构合规校验中断 |
| **RED-LINE-03** | **零静默回落** | 当用户显式选择 `60-rife` 或 `120-rife` 时，严禁在无明确诊断报错的情况下静默退化为 `nvof 60fps` | 视为严重功能故障 |

---

## **2\. 靶向手术 1：解除队列 Pending 任务参数冻结陷阱（主根因闭环）**

### **2.1 精确代码坐标**

* `src/gui/rtx_gui.cpp:L676-L695` (`WM_DROPFILES` 消息处理)  
* `src/gui/rtx_gui.cpp:L530-L546` (`onQueueAdd()` 队列添加函数)  
* `src/gui/rtx_gui.cpp:L358-L398` (`onStartTranscode()` 启动转码逻辑)

### **2.2 因果缺陷剖析**

1. **快照过早冻结**：当用户通过拖拽（`WM_DROPFILES`）或点击按键（`onQueueAdd()`）添加视频文件时，系统立即提取当前 UI 参数（默认为 `fps_mode = "60-nvofa"`）并生成 `JobStatus::Pending` 任务存入 `app.jobQueue`。  
2. **UI 与队列状态脱节**：用户随后在 GUI 下拉菜单 `IDC_CMB_FPS` 中修改参数为 `120-rife`，并点击 `Start Transcode`。  
3. **关键逻辑陷阱**：在 `onStartTranscode()` 中，虽然调用了 `collectProfileFromUi(hwnd)` 更新全局 `guiProfile(app)`，但代码判定 `hasPending == true` 后，跳过了新任务的创建，且**未将更新后的 UI 配置同步至队列中已存在的 `Pending` 任务**。`startWorker()` 随后直接提取队列中原先冻结的旧快照（`60-nvofa`）进行转码，造成"配置选了 120-rife 但实际跑 60-nvofa"的现象。

### **2.3 双层对齐确定性契约方案**

为兼顾"单文件选片直觉"与"多任务异构隔离"，实施以下双层改动：

```
[用户操作]
  │
  ├─► 单文件拖入 (WM_DROPFILES count == 1)
  │     └─► 仅填充 IDC_EDT_INPUT 并更新预览 (不创建 Pending 任务)
  │
  ├─► 多文件拖入 / 点击 '+' 按钮 (WM_DROPFILES count >= 2 / onQueueAdd)
  │     └─► 按照当前 UI Profile 生成 JobStatus::Pending 任务入队
  │
  └─► 修改 UI 参数并点击 "Start Transcode"
        └─► 若存在匹配的 Pending 任务:
              └─► 执行 updatePendingJob() 同步 UI 参数至快照后再启动
```

#### **逻辑调整 1：单文件拖拽直觉（`WM_DROPFILES` 改造）**

修改 `WM_DROPFILES` 消息响应机制。拖入单文件时不直接压入队列快照，仅刷新 UI 界面；多文件拖入时批量生成队列。

```
// 伪代码参考: src/gui/rtx_gui.cpp
case WM_DROPFILES: {
    HDROP hDrop = (HDROP)wParam;
    UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
    if (fileCount == 1) {
        wchar_t filePath[MAX_PATH];
        DragQueryFileW(hDrop, 0, filePath, MAX_PATH);
        SetWindowTextW(GetDlgItem(hwnd, IDC_EDT_INPUT), filePath);
        updatePreviewAndInspector(app, hwnd);
    } else if (fileCount > 1) {
        for (UINT i = 0; i < fileCount; ++i) {
            wchar_t filePath[MAX_PATH];
            DragQueryFileW(hDrop, i, filePath, MAX_PATH);
            enqueueJobFromPath(app, hwnd, filePath);
        }
    }
    DragFinish(hDrop);
    break;
}
```

#### **逻辑调整 2：`Pending` 任务快照实时同步（`updatePendingJob`）**

在 `onStartTranscode()` 执行启动前，增加对队列中对应 `Pending` 任务的刷新重置逻辑。

```
// 伪代码参考: src/gui/rtx_gui.cpp:L358-L398
void onStartTranscode(HWND hwnd, AppContext& app) {
    TranscodeProfile currentUiProfile = collectProfileFromUi(hwnd);
    
    if (app.jobQueue.hasPending()) {
        // 关键修复：刷新当前选定或匹配的 Pending 任务快照，消除参数冻结
        app.jobQueue.updatePendingJob(currentUiProfile);
    } else {
        // 若队列无 Pending 任务，根据当前 UI 路径与配置直接创建新任务
        enqueueJobFromUi(app, currentUiProfile);
    }
    
    startWorkerThread(app);
}
```

---

## **3\. 靶向手术 2：打通 RIFE 默认引擎路径解析与显式失败诊断**

### **3.1 精确代码坐标**

* `src/core/param_schema.cpp:L154-L174` (`resolveDefaultRifeEnginePath()`)  
* `src/core/param_schema.cpp:L271-L274` (`validateAndNormalizeProfile()`)  
* `src/shim/ngx_bridge/rife_engine.cpp:L233` (TRT 引擎分桶文件名匹配逻辑)  
* `src/core/hw_transcode_stages.cpp:L172-L227` (`RemasterPipeline::initRemasterAndFgStage()`)

### **3.2 规范化校验与引擎路径解析**

在 `param_schema.cpp` 中修正 `fps_mode` 与 `rife_engine_path` 的补全及校验逻辑。当 `fps_mode` 指定为 RIFE 模式时，强制规范化 `target_fps` 并绑定默认分桶模板路径。

```
// 伪代码参考: src/core/param_schema.cpp
void validateAndNormalizeProfile(TranscodeProfile& profile) {
    if (profile.fps_mode == "60-rife") {
        profile.target_fps = {60, 1};
        if (profile.rife_engine_path.empty()) {
            profile.rife_engine_path = resolveDefaultRifeEnginePath(profile);
        }
    } else if (profile.fps_mode == "120-rife") {
        profile.target_fps = {120, 1};
        if (profile.rife_engine_path.empty()) {
            profile.rife_engine_path = resolveDefaultRifeEnginePath(profile);
        }
    }
    // ... 其他模式校验
}
```

### **3.3 阻断静默降级（Zero Silent Fallback）**

彻底废除在 RIFE 加载失败后悄悄退化为 `nvof 60fps` 的防错机制。任何分桶加载异常或 TensorRT 初始化失败必须触发硬报错并终止流水线。

```
[RemasterPipeline 初始化 RIFE 阶段]
  │
  ├─► 检查 fps_mode ("60-rife" / "120-rife")
  │
  ├─► 加载 TensorRT Engine / 分桶模型 (rife_v4.25_<bucket>.engine)
  │     │
  │     ├─► [成功] ──► 状态栏打印: "Route: rife-trt | Target: 120fps" ──► 继续执行
  │     │
  │     └─► [失败] ──► 抛出 RuntimeError / 终止流水线 ──► UI 显示显式报错提示
  │                    (拒绝任何静默降级至 nvof 60fps 的操作)
```

改造 `hw_transcode_stages.cpp` 中的初始化逻辑：

```
// 伪代码参考: src/core/hw_transcode_stages.cpp
bool RemasterPipeline::initRemasterAndFgStage(const TranscodeProfile& profile) {
    if (profile.fps_mode == "60-rife" || profile.fps_mode == "120-rife") {
        std::string enginePath = profile.rife_engine_path;
        bool initSuccess = m_rifeEngine->loadBucketEngine(enginePath, profile.width, profile.height);
        
        if (!initSuccess) {
            // 红线贯彻：阻断静默回落，抛出显式诊断错误并中断
            std::string errorMsg = "[RIFE Error] Failed to initialize TensorRT engine bucket: " + enginePath;
            logError(errorMsg);
            updateStatusText("Error: RIFE initialization failed. Aborting.");
            return false; // 严禁设置 fallback 到 NV_OFFLOAD / NVOFA
        }
        
        logInfo("[Pipeline] Active Route: rife-trt | Output Target: " + std::to_string(profile.target_fps.num) + " fps");
        updateStatusText("Running: RIFE TRT (" + std::to_string(profile.target_fps.num) + " fps)");
    }
    return true;
}
```

---

## **4\. 本波次验收 Gate**

在提交代码前，必须执行全量回归测试并满足以下三项指标：

```
[验收 Gate 检查]
  ├─► 1. 队列 Pending 任务更新测试
  │       - 拖拽/入队后修改 GUI 为 120-rife 并启动，校验运行参数为 120-rife ─── [PASS]
  │
  ├─► 2. RIFE 防静默降级测试
  │       - 120-rife 路径下 target_fps 必须等于 {120, 1}
  │       - 引擎丢失时触发 ERROR 提示并停止转码，无 nvof 60fps 回落现象 ────── [PASS]
  │
  └─► 3. 架构合规红线审计
          - AuditTypes 保持 34/35 (零新增具名类型) ────────────────────────── [PASS]
          - AuditImports GUI <= 4 / HEADLESS == 2 (零新增静态 DLL) ──────────── [PASS]
```

