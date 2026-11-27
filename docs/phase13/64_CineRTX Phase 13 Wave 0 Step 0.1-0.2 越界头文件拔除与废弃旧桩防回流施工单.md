# **64\_CineRTX Phase 13 Wave 0 (Step 0.1–0.2)：越界头文件拔除与废弃旧桩防回流施工单**

* **施工单编号**：`CINERTX-P13-WO-064`  
* **所属波次**：`Wave 0（前置止血与架构防线隔离波次）`  
* **前置基线**：Commit `a6451b7` \+ `60_` 增量重构（总纲见 `63_CineRTX Phase 13 RIFE重构与PTX指令级合龙多波次多阶段施工Superpower规约`）  
* **后续衔接**：`65_CineRTX Phase 13 Wave 1 Step 1.1-1.3 PTX 7.5指令级同步手术施工单`  
* **独家触碰文件**：`tests/test_rife.cpp`  
* **签发日期**：`2026-11-26`

## **一、波次定位与宪法依据（`[DEC-010]` / `[DEC-009]`）**

1. **物理隔离红线（`[DEC-010]`）**：`rtx_tests.exe` 属于 GPLv3 测试可执行文件，严禁包含 `src/shim/ngx_bridge/` 目录下的内部实现头文件（`rife_engine.h`、`nv_func_tables.h`），也不链接 `rife_engine.cpp` 或 NVIDIA 专有 TensorRT 导入库 `nvinfer_10.lib`。所有 RIFE 能力必须经由 `include/shim/ngx_bridge_abi.h` 与 `include/rtx/ngx_remaster.h` 暴露的纯 C ABI 动态加载边界调用。  
2. **废弃旧桩防回流红线**：严禁任何外部或本地快照将已废弃的原型符号（`makeAdaptiveFrameBudgetState`、`evaluateFramePipelineTier`、`FramePipelineTier`）或误将 `RifeAspectBucket` 当布尔值 `!t1` 的旧代码覆写回 `tests/test_rife.cpp`。  
3. **具名类型配额红线（`[DEC-009]`）**：严格保持 `AuditTypes == 34 / 35`（`+0` 新增类型）。

## **二、原子施工步骤（Step 0.1 – Step 0.2）**

### **Step 0.1：物理拔除 `tests/test_rife.cpp` 顶部越界内部头文件引用**

* **代码坐标**：`tests/test_rife.cpp:L6-L10`  
* **操作动作**：打开 `tests/test_rife.cpp`，定位文件顶部的 `#include` 区块，**物理删除**以下两行越界包含指令：

```
// ❌ 物理删除以下两行越界内部头文件（阻断 LNK2019 与内部符号渗透）：
#include "../src/shim/ngx_bridge/rife_engine.h"
#include "../src/shim/ngx_bridge/nv_func_tables.h"
```

* **保留合法公共边界包含（核对确认必须存在）**：

```
#include "test_framework.h"
#include "rtx/playback_session.h"
#include "rife_pack_runtime.h"
#include "rtx/ngx_remaster.h"
#include "shim/ngx_bridge_abi.h"
```

### **Step 0.2：清理废弃旧桩区间并执行全文件负向扫描防线**

* **代码坐标**：`tests/test_rife.cpp:L719-L746`（若存在任何残余原型函数）  
* **操作动作**：  
  1. 确认 `test_shot_group_segmenter_hard_and_subtle_cuts` 与后续 RIFE Case 之间不存在 `makeAdaptiveFrameBudgetState`、`evaluateFramePipelineTier` 或 `FramePipelineTier` 定义/调用；  
  2. 在宿主机 PowerShell 7 执行负向正则表达式扫描，确保以下越界符号在 `tests/test_rife.cpp` 中返回 **0 命中**：

```

Select-String -Path tests/test_rife.cpp -Pattern "rife_engine\.h|nv_func_tables\.h|rtx_ngx::RifeEngine|rtx_ngx::nv_cuda|evaluateFramePipelineTier|makeAdaptiveFrameBudgetState|FramePipelineTier"
|---|---|---|---|---|
```

## **三、Wave 0 通关门禁（Exit Gate）**

| 检查项 | 核验方法 | 通关判据 |
| :---- | :---- | :---- |
| **Gate W0-1：越界头清零** | `Select-String 扫描 tests/test_rife.cpp` | `rife_engine.h` 与 `nv_func_tables.h` 匹配数为 `0` |
| **Gate W0-2：废弃桩清零** | `Select-String 扫描 tests/test_rife.cpp` | `evaluateFramePipelineTier` / `FramePipelineTier` 匹配数为 `0` |
| **Gate W0-3：文件隔离边界** | `git status --short` | 仅 `tests/test_rife.cpp` 处于修改态，无额外脏文件 |

**通关后立即进入 `65_CineRTX Phase 13 Wave 1 Step 1.1-1.3 PTX 7.5指令级同步手术施工单`。**