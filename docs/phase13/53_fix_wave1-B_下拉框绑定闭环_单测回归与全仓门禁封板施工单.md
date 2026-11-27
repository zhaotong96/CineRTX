# **53\_fix\_wave1-B\_下拉框绑定闭环\_单测回归与全仓门禁封板施工单**

**Superpowers 原子执行与验收闭环规约**  
依据委托书 \#51 及 `52_fix_wave1-A` 核心队列与 RIFE 路径逻辑基线编制。

---

## **1\. 元信息与宪法红线**

### **1.1 施工波次与依赖拓扑**

* **波次标识**：`Fix Wave 1 - Stage B`  
* **依赖关系**：前置依赖 `52_fix_wave1-A` 核心队列与 RIFE 路径逻辑已全面就绪。  
* **主要目标**：打通 `IDC_CMB_FPS` 下拉框与状态机全路径同步、补充防回归单元测试、完成 Windows 宿主机构建与全仓五重静态门禁封板。

### **1.2 基线锁定（Baseline Locking）**

* **锁定 Commit**：`a6451b7`  
* **基线状态**：单元测试 `153/153 PASS` \| 类型契约 `AuditTypes = 34/35` \| 动态链接 `AuditImports` GUI `<= 4` / HEADLESS `== 2`  
  \|---\|

### **1.3 宪法级硬红线（Constitutional Red Lines）**

全量修改与自动化编译测试必须无条件满足以下约束：

* **零类型膨胀**：`AuditTypes == 34/35`（零新增具名 `struct` / `class` / `enum`）。  
* **零静态依赖引入**：`AuditImports` GUI `<= 4` / HEADLESS `== 2`（零新增静态 DLL 导入）。  
* **零隐私侵犯**：`AuditPrivacy == 0`（严禁硬编码用户路径或凭据）。  
* **代码体积上限**：`AuditSloc <= 23,000`（代码行数严格受控）。  
* **版权规范**：`AuditSpdx == 0`（所有源文件保持标准开源合规文件头）。  
* **物理宿主机真实校验**：必须在 Windows 11 \+ MSVC 19.51 \+ RTX 5060 宿主机实际执行编译、单测与静态门禁校验，并输出真实贴图级日志。

---

## **2\. 靶向手术 1：`IDC_CMB_FPS` 下拉框与状态机全路径同步**

### **2.1 精确代码坐标**

* `workstation_canvas.cpp:L703-L707, L748-L758, L810-L812`（`IDC_CMB_FPS` 索引 `0..4` 与 `kFpsModes`: `original`, `60-nvofa`, `120-nvofa`, `60-rife`, `120-rife`）  
* `rtx_gui.cpp:L799`（`CBN_SELCHANGE` 事件响应分支）与 `populateComboBoxes()` 控件初始化/多语言刷 renewed 函数

### **2.2 修复动作与逻辑校准**

#### **索引与模式数组双向严格映射**

验证并确保 `kFpsModes` 与 `IDC_CMB_FPS` 界面索引保持绝对一致，严禁索引错位：

| 索引 (Index) | FPS 模式标识 (kFpsModes) | 逻辑含义描述 | 依赖管道 |
| :---- | :---- | :---- | :---- |
| **0** | `original` | 原始帧率直通 | Direct Stream |
| **1** | `60-nvofa` | NVOFA 光流补帧至 60fps | Optical Flow Accel |
| **2** | `120-nvofa` | NVOFA 光流补帧至 120fps | Optical Flow Accel |
| **3** | `60-rife` | RIFE 神经网络补帧至 60fps | RIFE TensorRT Engine |
| **4** | `120-rife` | RIFE 神经网络补帧至 120fps | RIFE TensorRT Engine |

#### **`populateComboBoxes()` 刷新逻辑抗重置改造**

在 UI 语言切换或界面重绘调用 `populateComboBoxes()` 时，先读取当前全局 Profile 中的 `fps_mode` 字符串，通过匹配索引反向设置 `SendMessage(hCmbFps, CB_SETCURSEL, target_index, 0)`。若匹配失败则默认选中索引 `1`（`60-nvofa`），严禁无条件强行重置为 `1`。

#### **`CBN_SELCHANGE` 状态机即时同步**

在 `rtx_gui.cpp:L799` 收到 `CBN_SELCHANGE` 通知时，立即执行：p  
int selected\_idx \= (int)SendMessage(hCmbFps, CB\_GETCURSEL, 0, 0);  
if (selected\_idx \>= 0 && selected\_idx \< 5\) {  
g\_Profile.fps\_mode \= kFpsModes\[selected\_idx\];  
syncPendingTaskSnapshot(g\_Profile); // 同步更新 Pending 任务快照  
}

````

防止用户手动选择 `4`（`120-rife`）后，因后续后台轮询或界面刷新将 Profile 重新覆盖为默认值。

---

## 3. 靶向手术 2：补充确定性自动化单元测试

在 `tests/test_gui_workstation.cpp`（或 `tests/test_param_schema_reflection.cpp`）中新增测试用例，构建防回归屏障：

```cpp
TEST_CASE("Validate IDC_CMB_FPS Selection Persistence & RIFE Pipeline Resolution") {
    // 1. 模拟界面选中索引 4 (120-rife)
    UiState mock_ui;
    mock_ui.cmb_fps_index = 4;
    
    // 2. 收集与校验 Profile
    Profile profile = collectProfileFromUi(mock_ui);
    bool valid = validateAndNormalizeProfile(profile);
    
    REQUIRE(valid == true);
    REQUIRE(profile.fps_mode == "120-rife");
    REQUIRE(profile.target_fps.numerator == 120);
    REQUIRE(profile.target_fps.denominator == 1);
    REQUIRE_FALSE(profile.rife_engine_path.empty());
    
    // 3. 断言绝不静默回落为 60-nvofa
    REQUIRE(profile.fps_mode != "60-nvofa");
    
    // 4. 验证异构多任务隔离：已有/新建 Pending 任务快照不受修改检查器影响
    TaskSnapshot pending_task = createPendingTaskSnapshot(profile);
    REQUIRE(pending_task.fps_mode == "120-rife");
    
    // 模拟 UI 被误切回 60-nvofa
    mock_ui.cmb_fps_index = 1;
    Profile new_profile = collectProfileFromUi(mock_ui);
    
    // 断言快照具备状态隔离能力
    REQUIRE(pending_task.fps_mode == "120-rife");
    REQUIRE(new_profile.fps_mode == "60-nvofa");
}
````

---

# **4\. 靶向手术 3：宿主机全量构建、测试与静态门禁封板**

执行环境：`Windows 11 Home 23H2` \| `MSVC 19.51 (Visual Studio 2026 Build Tools)` \| `NVIDIA GeForce RTX 5060`

## **4.1 全量构建命令**

```
.\build.bat
```

* **期望退出码**：`0`

## **4.2 单元测试全量回归**

l  
.\\bin\\rtx\_tests.exe

````
* **期望结果**：`All tests passed (154 assertions in test cases)`，包含原 `153` 个断言及新增的 `120-rife` 防回归断言。

#### 4.3 五重静态门禁封板校验

```powershell
cmake -P cmake/AuditTypes.cmake
# 输出断言: AuditTypes = 34/35 (零新增具名类型)

cmake -P cmake/AuditImports.cmake
# 输出断言: GUI <= 4, HEADLESS == 2 (零新增静态 DLL)

cmake -P cmake/AuditPrivacy.cmake
# 输出断言: 0 violations found

cmake -P cmake/AuditSloc.cmake
# 输出断言: Sloc <= 23,000

cmake -P cmake/AuditSpdx.cmake
# 输出断言: 0 violations found
````

### **4.4 GUI 真实转码管线实测验证**

启动客户端执行 Encoder 压测：

1. 打开 GUI 界面，在 `IDC_CMB_FPS` 下拉框中手动选择 `120-rife`。  
2. 点击 **Start Encoding** 启动任务。  
3. **预期表现**：状态栏与 Console 日志精准输出 `[Pipeline] Initializing RIFE TensorRT Engine (120 fps target)...`，实际输出视频帧率精确为 `120.00 fps`，日志与后端流水线均不再异常回落至 `60-nvofa`。

---

## **5\. 汇报附件与交付物清单**

完成上述变更后，提交的施工汇报必须提供以下佐证材料：

```
└── reports/
    ├── diff_summary.md              # 修改代码文件清单及行数增减摘要
    ├── build_host_stdout.log        # .\build.bat 完整输出日志 (Exit Code: 0)
    ├── rtx_tests_stdout.log         # rtx_tests.exe 运行日志 (包含新增防回归测试)
    ├── audit_fivefold_result.txt    # 5 重静态门禁 (Types/Imports/Privacy/Sloc/Spdx) 真实控制台输出
    └── test_120rife_execution.png   # RTX 5060 宿主机实测 120-rife 转码日志与状态栏截图
```

