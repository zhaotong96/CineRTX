# CineRTX Phase 12 SuperPower 施工单（Wave 12.1）：构建净化与微观卫生清剿

- **施工单编号**：`CINERTX-P12-WORKORDER-003`  
- **归档路径**：`docs/phase12/03_CineRTX Phase 12 SuperPower 施工单_构建净化与微观卫生清剿.md`  
- **承接角色**：系统内核与现代 C++26 基础设施执行工程师（External Execution Engineer）  
- **签发主管**：多媒体系统工程与 GPU 异构计算首席领域专家组 / 项目总协调负责人  
- **前置基线**：`v0.3.0`（Commit `530a9a6`，146/146 单测全绿，AuditTypes 155 类型，Pure SLOC 33,143 行，驱动 ≥ 621.24 WHQL）  
- **依据母本**：[`02_CineRTX Phase 12 极限现代 C++26 重构与无头 DLL 净空终审裁决书.md`](./02_CineRTX%20Phase%2012%20极限现代%20C++26%20重构与无头%20DLL%20净空终审裁决书.md)（`CINERTX-P12-RULING-002`）  
- **建议工期**：1.5–2 个有效工作日  
- **核心目标**：清零 6 项微观卫生病灶（`HYG-P12-01` 至 `HYG-P12-06`），拔除全仓环境变量隐藏开关、全局可变状态 `g_app`、配置期静默联网拉取与全局编译警告抑制，实现全仓 `/WX` 零警告编译及 146/146 单元测试 100% PASS。

---

## 一、 施工军规与零容忍红线（Mandatory Guardrails）

进场执行前必须无条件遵守以下五条铁律，任一违规将在代码审查与自动化门禁期无条件驳回：

1. **标准库第一铁律（Std-First Law）**：严禁新增任何可用 C++20/23/26 标准库（`std::unique_ptr`、`std::jthread`、`std::expected`、`std::variant`、`std::tuple`、`std::span`、`std::move_only_function`）替代的自定义 `class` / `struct`。所有存续具名类型必须具备合法 `@rtx-type-justification` 契约块。  
2. **零隐藏开关铁律（Zero Hidden Switches）**：全仓严禁出现 `std::getenv`、`GetEnvironmentVariableA/W`、未在 `TranscodeProfile` 公开注册的秘密配置项或调试后门宏。程序行为 100% 由显式输入参数确定。  
3. **零全局可变状态铁律（Zero Mutable Globals）**：全仓严禁新增文件级或命名空间级可变全局变量/单例（`g_app` 等），GUI 状态必须严格绑定至顶层窗口实例（`GWLP_USERDATA` \+ RAII 所有者）。  
4. **奥卡姆剃刀与零冗余防御（KISS & Contract Programming）**：内部模块间优先传递非空引用 `T&` 或 `std::span`，拔除重复 `nullptr` 判空防御分支与死代码残余成员；外部 I/O 与硬件 SDK 边界仅保留一次性严格校验。  
5. **绝对离线确定性构建（Deterministic Offline Build）**：CMake 配置阶段严禁静默调用 `git` 或任何网络命令；开启 `/WX`（警告即错误），严禁在根目录全局屏蔽编译器警告。

---

## 二、 核心外科手术任务（Tasks W12.1-1 至 W12.1-6）

### 任务 W12.1-1：CMake 构建脚本净化与警告零容忍（`HYG-P12-04`）

**涉及文件**：`CMakeLists.txt` 及需要局部对齐注释的原子结构体头文件（如 `include/rtx/telemetry_bus.h`）

1. **拔除配置期静默 `git submodule update`**：  
   - 在根目录 `CMakeLists.txt` 中删除 `if(NOT EXISTS "${RTX_NVCODEC_INCLUDE_DIR}/ffnvcodec/nvEncodeAPI.h")` 内的 `find_package(Git QUIET)` 与 `execute_process(COMMAND ${GIT_EXECUTABLE} submodule update ...)` 整段逻辑。  
   - 若缺失 `nvEncodeAPI.h`，立即触发 `message(FATAL_ERROR "[CineRTX] nv-codec-headers missing at ${RTX_NVCODEC_INCLUDE_DIR}. Run 'git submodule update --init --recursive' manually before configuring.")`。  
2. **移除全局 `/wd4324` 并升级 `/WX`**：  
   - 从 `rtx_project_options` 的 `target_compile_options` 中移除 `/wd4324`，并追加 `/WX`（MSVC）与 `-Werror`（Clang/GCC，第三方头除外）。  
   - 对于明确使用 `alignas(64)` 做缓存行填充隔离的结构体（如 `TelemetrySnapshot`），在其定义紧邻前后使用精确的局部抑制：

```c
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
// struct alignas(64) ...
#ifdef _MSC_VER
#pragma warning(pop)
#endif
```

   - 清理 `CMakeLists.txt` 中任何已废弃的幽灵编译宏或死路径检查。

---

### 任务 W12.1-2：`nv_encoder.cpp` 单缓冲残余清除与多槽异步事件 1:1 绑定（`HYG-P12-01`）

**涉及文件**：`src/shim/loader/nv_encoder.cpp`

1. **物理清除单缓冲历史残余字段**：  
   - 删除编码器内部结构中遗留的旧版单缓冲成员（如独立于环形池之外的 `bitstream`、`completion_event` 等死成员），消除双套状态并存导致的阅读与维护心智负担。  
2. **槽位级异步完成事件严格封装**：  
   - 确保 `BitstreamSlot` 结构体持有独立的 `UniqueHandle completion_event`（经 `CreateEventW(nullptr, FALSE, FALSE, nullptr)` 创建）与对应的 `NV_ENC_CREATE_BITSTREAM_BUFFER` 句柄，且析构时先注销异步事件（`nvEncUnregisterAsyncEvent`）、再销毁比特流缓冲（`nvEncDestroyBitstreamBuffer`）、最后由 `UniqueHandle` 自动关闭内核事件句柄。  
   - 核验 `rtx_shim_encoder_encode()` 与 `rtx_shim_encoder_flush()` 严格按当前槽位索引绑定并等待对应槽位的 `completion_event`，确保在开启 `15B / 31B` 分层 B 帧与 Lookahead 重排时零状态错配、零尾部丢帧。

---

### 任务 W12.1-3：`rife_engine.cpp` 驱动符号表归一化与 `std::getenv` 隐式开关清零（`HYG-P12-02`）

**涉及文件**：`src/shim/ngx_bridge/rife_engine.cpp`、`src/shim/ngx_bridge/nv_func_tables.h`、`src/shim/ngx_bridge/nv_func_tables.cpp`

1. **消灭重复 CUDA 驱动函数表**：  
   - 删除 `src/shim/ngx_bridge/rife_engine.cpp` 内局部定义的重复 CUDA 驱动函数指针结构与 `GetProcAddress` 加载逻辑。  
   - 统一包含并复用 `nv_func_tables.h` 维护的单一驱动函数表（`NvCudaDriverTable`），保持 `src/shim/` 严格遵守 `MIT OR Apache-2.0` 许可证头。  
2. **彻底拔除 `std::getenv` 隐式环境旁路**：  
   - 删除 `rife_engine.cpp`（及全仓任何其他源文件）中全部 `std::getenv` / `_dupenv_s` / `GetEnvironmentVariable*` 调用。  
   - TensorRT 日志器与引擎构建参数完全由代码显式构造的配置驱动，不读取任何未声明的环境变量。

---

### 任务 W12.1-4：`VramAdaptiveScheduler` 单一整数状态机提纯（`HYG-P12-03`）

**涉及文件**：`include/rtx/vram_scheduler.h`、`src/core/vram_scheduler.cpp`、`tests/test_vram_scheduler.cpp`

1. **消除冗余双计数器**：  
   - 合并 `frames_in_tier_` 与多余步进状态变量为单一 `uint32_t dwell_frames_ = 0` 计数器，明确其唯一不变量：记录当前连续驻留在当前 `TopologyTier` 的已处理帧数（初始或切换层级时归零，每步递增至饱和）。  
2. **消除浮点乘法舍入误差**：  
   - 将水位线比较逻辑从 `double` 比率乘法重构为纯 `uint64_t` 整数百分比阈值（如 `degrade_pct = 82`、`recover_pct = 68`）与溢出安全的交叉乘法/高精度整数判定：  
     - 高水位触发：`estimated_used_bytes * 100ULL >= budget_bytes * degrade_pct`（或使用 `__uint128_t` / `_umul128` 防止 64 位乘法溢出）；  
     - 低水位恢复：`estimated_used_bytes * 100ULL <= budget_bytes * recover_pct && dwell_frames_ >= min_dwell_frames`。  
   - 同步更新 `tests/test_vram_scheduler.cpp` 断言（保持对 82% / 68% 水位线与 120 帧滞后防抖的 100% 覆盖）。

---

### 任务 W12.1-5：GUI 全局可变状态 `g_app` 拔除与窗口实例所有权收口（`HYG-P12-05`）

**涉及文件**：`src/gui/rtx_gui.cpp`、`src/gui/workstation_canvas.h`、`src/gui/workstation_canvas.cpp`

1. **消灭文件级全局单例 `g_app`**：  
   - 物理删除 `g_app` 全局变量，全仓搜索 `g_app` 必须达到 **0 命中**。  
   - 在 `WM_NCCREATE` 中通过 `CREATESTRUCTW::lpCreateParams` 将由 `wWinMain` 栈帧（或顶层作用域 `std::unique_ptr<WorkstationApp>`）拥有的 `WorkstationApp*` 绑定至主窗口 `GWLP_USERDATA`；子画布窗口（如 `timelineCanvasProc`）同样通过 `CREATESTRUCTW::lpCreateParams` 接收所属 `WorkstationApp*` 并存入自身 `GWLP_USERDATA`。  
2. **保证逆序析构与无悬垂回调**：  
   - 在 `WM_NCDESTROY` 时清空 `GWLP_USERDATA` 为 `0`，确保任何迟到的系统通知消息无法访问已进入析构流程的 `WorkstationApp` 实例。

---

### 任务 W12.1-6：清除裸 GDI 句柄申请与局部冗余防御（`HYG-P12-06`）

**涉及文件**：`src/gui/workstation_canvas.cpp`、`src/gui/rtx_gui.cpp`

1. **替换所有裸 `CreateSolidBrush` / `DeleteObject`**：  
   - 将 `drawBrandMark()` 及其他自绘逻辑中的 `CreateSolidBrush` \+ `SelectObject` \+ `DeleteObject` 全部替换为 `rtx::UniqueBrush` 或无分配 stock 画刷（`SetDCBrushColor(hdc, color)` \+ `GetStockObject(DC_BRUSH)`）。  
   - 静态扫描 `src/gui/` 下 `CreateSolidBrush`、`CreatePen`、`DeleteObject` 裸调用必须为 **0 命中**。  
2. **剪除同文件内重复 `nullptr` 防御**：  
   - 将已由调用链保证非空的内部绘制与状态辅助函数参数从 `WorkstationApp*` 改为 `WorkstationApp&`，移除函数体首部冗余的 `if (!app) return;` 分支。

---

## 三、 Wave 12.1 自动化验收门禁清单（Hard Gates）

外部执行工程师交付前，必须在本地宿主机（Windows 11 \+ MSVC 19.51+ \+ NVIDIA Driver ≥ 621.24 WHQL \+ WTS Interactive Session）完整运行以下门禁并落盘实证日志：

- [ ] **Gate 12.1-1（零警告编译）**：`cmake --build --preset windows-msvc-ninja`（已开启 `/WX`）全量目标 **0 错误 0 警告**；配置阶段无任何 `git` 网络子进程调用。  
- [ ] **Gate 12.1-2（负向正则清零扫描）**：  
  - `rg "std::getenv|_dupenv_s|GetEnvironmentVariable" include/ src/` → **0 命中**；  
  - `rg "\bg_app\b" include/ src/` → **0 命中**；  
  - `rg "CreateSolidBrush|DeleteObject" src/gui/` → **0 命中**（`raii_owners.h` 删除器定义除外）。  
- [ ] **Gate 12.1-3（类型与许可合规）**：  
  - `cmake -P cmake/AuditTypes.cmake` → **0 违规**（具名类型数量 ≤ 155，无新增 Slop 类型）；  
  - `cmake -P cmake/AuditSpdx.cmake` → **PASS**（`src/shim/` 严格 `MIT OR Apache-2.0`，其余 `GPL-3.0-or-later WITH Plugin-Exception`）。  
- [ ] **Gate 12.1-4（全量单测与桌面实呈无回归）**：  
  - `rtx_tests.exe` 全量运行 **146/146 PASS**（含 `test_vram_scheduler` 与 `nv_encoder` 分层 B 帧测试）；  
  - 交互式桌面启动 `CineRTX.exe` 验证 GUI 窗口创建、拖动、时间轴操作及退出无崩溃、无 UAF、无 GDI 句柄泄漏。