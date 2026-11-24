# CineRTX Phase 12 SuperPower 施工单（Wave 12.2-B1）：第一战场 `src/shim/ngx_bridge/` 内部类型清剿与现代标准库替换

- **施工单编号**：`CINERTX-P12-WORKORDER-034`  
- **归档路径**：`docs/phase12/34_CineRTX Phase 12 Wave 12.2-B1 第一战场施工单_ngx_bridge内部类型清剿与现代标准库替换.md`  
- **承接角色**：现代 C++26 核心架构重构与 GPU 异构桥接执行工程师（External Execution Engineer）  
- **签发主管**：多媒体系统工程与 GPU 异构计算首席领域专家组 / 项目总协调负责人  
- **前置基线**：Wave 12.2-A 终审封板基线（Git 提交 `5fb9d01`，MSVC `/W4 /WX` 85/85 零警告，147/147 单测全绿，全仓具名类型 128 个）  
- **依据母本**：  
  - [`04_CineRTX Phase 12 SuperPower 施工单_自定义类型清剿与C++26标准库替换.md`](./04_CineRTX%20Phase%2012%20SuperPower%20施工单_自定义类型清剿与C++26标准库替换.md)（`CINERTX-P12-WORKORDER-004`）  
  - [`33_CineRTX Phase 12 Wave 12.2-A 全仓代码事实验证裁决书暨Wave12.2-B1正式作战令.md`](./33_CineRTX%20Phase%2012%20Wave%2012.2-A%20全仓代码事实验证裁决书暨Wave12.2-B1正式作战令.md)（`CINERTX-P12-FINAL-SEAL-033`）  
- **建议工期**：1.5–2 个有效工作日  
- **核心目标**：针对第一战场 `src/shim/ngx_bridge/` 内部现存 31 个非导出具名类型实施外科手术式清剿；全面采用 C++20/23/26 标准库原语（`std::tuple` 结构化解包、`std::span`、`std::mdspan`、无状态 Lambda `unique_ptr`、`namespace` 自由函数）替代内部参数传递包、步幅描述符、驱动表与包装壳；**实现全仓具名类型从 128 压减至 ≤ 105 个（净减 ≥ 23 个），Pure SLOC 从 32,360 压减至 ≤ 29,500 行**，100% 保持 `MIT OR Apache-2.0` 纯洁性与 147/147 PASS 单测无回归。

---

## 一、 最高架构军规与红线约束

1. **许可证物理隔离绝对红线（License Boundary）**：  
   - `src/shim/ngx_bridge/` 与 `include/shim/` 下全部文件必须 100% 保持 `SPDX-License-Identifier: MIT OR Apache-2.0`；  
   - **绝对严禁包含任何 `include/rtx/*.h`（GPLv3 域）头文件**，严禁引入任何 GPL 代码，确保 `rtx_ngx_bridge.dll` 为干净纯正的独立可分发二进制。
2. **零过渡别名铁律（Zero Pseudo-Typedef）**：  
   - 严禁留存 `using OldStruct = std::tuple<...>;` 等欺骗性过渡别名；  
   - 必须在所有生产调用点一步到位改写为标准库原语并使用结构化绑定解包（`auto [a, b, c] = ...;`），严禁散落 `std::get<0/1/2>` 魔法数字。
3. **白名单准入唯一性（Whitelisted Retentions Only）**：  
   - `src/shim/ngx_bridge/` 内部仅允许存续：  
     1. `include/shim/ngx_bridge_abi.h` 中定义的 `extern "C"` 跨 ABI 结构体与对外不透明句柄 `struct RtxNgxBridge`；  
     2. 管理核心 GPU 资源逆序析构拓扑的状态机类（如 `D3D12Interop`、`RifeEngine`、`NgxD3D12Bridge`、`NvofEngine`），且必须附带完整的 `@rtx-type-justification` 契约块。其余全部被动数据包、启动配置包、驱动函数表一律消灭。

---

## 二、 靶向清剿清单与标准库替换方案（31 个类型精准攻坚）

经全仓实体代码排查，`src/shim/ngx_bridge/` 现存 31 个结构体/类，按以下四大任务单元实施清剿重构：

### 任务 B1-1：CUDA/NVOF/Pack 启动参数传递包彻底解构（净减 8 个类型）

**涉及文件**：`src/shim/ngx_bridge/p010_pack_runtime.h/.cpp`、`src/shim/ngx_bridge/rife_pack_runtime.h/.cpp`

| 现存冗余结构体 | 物理所在文件 | 强制替换方案 | 解构与重构规范 |
| :--- | :--- | :--- | :--- |
| `PreprocessLaunch` | `p010_pack_runtime.h:22` | **`std::tuple<...>`** | 传参统一改用 `std::tuple` 或参数打散，调用点强制用 `auto [src, dst, w, h, ...] = ...;` 解包。 |
| `PreprocessBufferLaunch` | `p010_pack_runtime.h:42` | **`std::tuple<...>`** | 同上，消除临时包装结构体。 |
| `PackV2Launch` | `p010_pack_runtime.h:60` | **`std::tuple<...>`** | 同上，消除包装袋。 |
| `FusedRemasterLaunch` | `p010_pack_runtime.h:89` | **`std::tuple<...>`** | 同上，内核启动直接消费解包后的局部变量。 |
| `RifePreprocessLaunch` | `rife_pack_runtime.h:20` | **`std::tuple<...>`** | 同上，彻底拔除头文件中的自定义结构声明。 |
| `RifePreprocessSeparateLaunch`| `rife_pack_runtime.h:44` | **`std::tuple<...>`** | 同上，改用结构化参数元组。 |
| `RifePostprocessLaunch`| `rife_pack_runtime.h:68` | **`std::tuple<...>`** | 同上，清除所有 Launch 结构体。 |
| `InteropTexture` | `d3d12_interop.h:34` | **`std::tuple<...>` / C-ABI 组合** | 将内部中间纹理信息解构为 `std::tuple<ID3D12Resource*, cudaExternalMemory_t, void*, ...>`。 |

---

### 任务 B1-2：槽位状态与内部实现袋（Impl/State/Components）消融（净减 8 个类型）

**涉及文件**：`src/shim/ngx_bridge/d3d12_interop.h`、`src/shim/ngx_bridge/bridge_exports.cpp`、`src/shim/ngx_bridge/p010_pack_runtime.cpp`、`src/shim/ngx_bridge/rife_pack_runtime.cpp`、`src/shim/ngx_bridge/nvof_engine.h/.cpp`、`src/shim/ngx_bridge/rife_engine.h/.cpp`

| 现存冗余结构体 | 物理所在文件 | 强制替换方案 | 解构与重构规范 |
| :--- | :--- | :--- | :--- |
| `PingPongSlot` | `d3d12_interop.h:53` | **`std::tuple<...>`** | 双缓冲槽位直接使用元组表达 `(res, handle, fence_val)`。 |
| `SharedBufferSlot` | `d3d12_interop.h:81` | **`std::tuple<...>`** | 共享缓冲槽位直接使用元组。 |
| `SdkComponents` | `bridge_exports.cpp:78` | **`std::tuple<...>`** | 仅内部组装使用，降维为私有元组。 |
| `BridgeImpl` | `bridge_exports.cpp:88` | **结构合并 / 不透明句柄直连** | 与 `struct RtxNgxBridge` 不透明句柄统一定义合并，消除多重壳层包装。 |
| `PackState` | `p010_pack_runtime.cpp:117` | **内部静态元组 / 模块作用域** | 降维为局部私有状态元组或直接内联至运行上下文。 |
| `RifePackState` | `rife_pack_runtime.cpp:107` | **内部静态元组 / 模块作用域** | 降维为局部私有状态元组。 |
| `NvofEngine::Impl` | `nvof_engine.h:187, .cpp:96`| **扁平化收敛至 NvofEngine** | 消除 Pimpl 二级指针跳转，状态成员直接归入核心引擎类（带契约）。 |
| `RifeEngine::Impl` | `rife_engine.h:20, .cpp:97` | **扁平化收敛至 RifeEngine** | 消除 Pimpl 冗余包装，引擎本身作为状态机收口。 |

---

### 任务 B1-3：动态驱动函数表与工具类的命名空间降维（净减 8 个类型）

**涉及文件**：`src/shim/ngx_bridge/d3d12_interop.cpp`、`src/shim/ngx_bridge/nvof_engine.cpp/.h`、`src/shim/ngx_bridge/p010_pack_runtime.cpp`、`src/shim/ngx_bridge/rife_pack_runtime.cpp`、`src/shim/ngx_bridge/nv_func_tables.h`、`src/shim/ngx_bridge/trt_loader.h`

| 现存冗余类/表结构 | 物理所在文件 | 强制替换方案 | 解构与重构规范 |
| :--- | :--- | :--- | :--- |
| `CUDA_MEMCPY3D` | `d3d12_interop.cpp:61` | **标准 CUDA API 或别名消退** | 采用驱动层标准对齐布局，消除本地重复定义的未契约结构。 |
| `D3D12Api` | `d3d12_interop.cpp:86` | **`namespace d3d12_api` 自由函数** | 纯静态函数表直接降维为命名空间内的自由函数指针或自由函数。 |
| `CudaApi` | `d3d12_interop.cpp:126` | **`namespace cuda_api` 自由函数** | 纯静态函数表直接降维为命名空间内的自由函数。 |
| `CudaDriverApi` | `nvof_engine.cpp:55` | **`namespace nvof_driver` 自由函数**| 同上，消除具名结构体表。 |
| `DriverApi` (P010) | `p010_pack_runtime.cpp:51` | **局部自由函数指针** | 直接作为 `.cpp` 文件作用域的静态函数指针，消灭类型。 |
| `DriverApi` (RIFE) | `rife_pack_runtime.cpp:51` | **局部自由函数指针** | 直接作为 `.cpp` 文件作用域的静态函数指针，消灭类型。 |
| `NvCudaDriverTable` | `nv_func_tables.h:22` | **`namespace` / 自由表** | 降维为纯自由符号表或内联解析，消除独立具名类型。 |
| `TrtLoader` | `trt_loader.h:19` | **`namespace trt_loader` 自由函数** | 仅含加载逻辑的无状态工具类，全面降维为命名空间自由函数。 |

---

### 任务 B1-4：TensorRT 日志与辅助虚基类收口（净减 2 个类型）

**涉及文件**：`src/shim/ngx_bridge/rife_builder.cpp`、`src/shim/ngx_bridge/rife_engine.cpp`

| 现存类 | 物理所在文件 | 强制替换方案 | 解构与重构规范 |
| :--- | :--- | :--- | :--- |
| `BuilderLogger` | `rife_builder.cpp:30` | **统一日志器单例 / Lambda 适配** | 合并或降维为通用最小无状态日志器，消除重复手写的类定义。 |
| `RifeTrtLogger` | `rife_engine.cpp:40` | **与全局/模块最小日志器合流** | 统一收拢至公共无状态适配实现。 |

---

## 三、 白名单存续类型与契约注释规范

完成 Wave 12.2-B1 清剿后，`src/shim/ngx_bridge/` 内部**仅允许存续以下 5 个核心状态机类**（其余必须 100% 物理消除）：

```cpp
// 1. D3D12 共享资源与 CUDA 外部内存互操作核心状态机
// @rtx-type-justification
// Category: NonTrivial-StateMachine
// Why-Std-Fails: Manages D3D12 device, command queue, shared fence, and CUDA external memory lifecycle with strict reverse destruction order.
// Invariant: GPU resources and synchronization primitives remain valid and paired across interop operations.
class D3D12Interop { ... };

// 2. NGX 与 D3D12 集成桥接主会话状态机
// @rtx-type-justification
// Category: NonTrivial-StateMachine
// Why-Std-Fails: Coordinates NGX parameter lifecycle, feature evaluation contexts, and device resets.
// Invariant: NGX feature handles remain strictly bound to their parent D3D12 device.
class NgxD3D12Bridge { ... };

// 3. 硬件光流引擎核心会话
// @rtx-type-justification
// Category: NonTrivial-StateMachine
// Why-Std-Fails: Manages NVIDIA Optical Flow SDK session, input/output buffers, and cost buffers.
// Invariant: Flow vectors and hardware execution queues are safely synchronized.
class NvofEngine { ... };

// 4. RIFE TensorRT 运行时推理引擎状态机
// @rtx-type-justification
// Category: NonTrivial-StateMachine
// Why-Std-Fails: Encapsulates TensorRT runtime, execution contexts, and GPU stream ordering for neural interpolation.
// Invariant: CUDA bindings, execution context, and engine instances destruct in strict reverse order.
class RifeEngine { ... };

// 5. 对外不透明 C-ABI 句柄实现体
// @rtx-type-justification
// Category: C-ABI
// Why-Std-Fails: Opaque handle implementation exposed across DLL boundary to GPL core.
// Invariant: ABI layout stability guaranteed across dynamic link boundary.
struct RtxNgxBridge { ... };
```

---

## 四、 施工硬门禁与验收指标（Gate 12.2-B1）

执行工程师完成施工并呈报 `35_CineRTX Phase 12 Wave 12.2-B1 施工汇报.md` 时，必须满足以下硬门禁，否则协调负责人一律驳回：

1. **具名类型压减指标**：  
   - 全仓具名自定义类型从 **`128`** 个压减至 **`≤ 105` 个**（净削减 $\ge 23$ 个）；  
   - `src/shim/ngx_bridge/` 内部仅允许存续上述 5 个带白名单契约的核心状态机类型，清剿率达标。
2. **Pure SLOC 瘦身指标**：  
   - 全仓 Pure SLOC 从 **`32,360`** 行压减至 **`≤ 29,500` 行**（净消肿 $\ge 2,860$ 行）；  
   - 严禁通过删除有效注释、空行压缩或机械拼接长单行方式作弊（通过 `AuditSloc` 审计）。
3. **构建与警告零容忍**：  
   - Windows 宿主机 MSVC 19.51+ `/std:c++latest /W4 /WX` 构建 **85/85 目标 0 Error(s), 0 Warning(s)**。
4. **单元测试与功能零回归**：  
   - 双向运行 `rtx_tests.exe`（默认构建目录与外部目录）达成 **147/147 100% PASS**（重点覆盖 RIFE `scale=0.5`、VSR 与 D3D12 共享互操作用例）；  
   - `CineRTX.exe --version` 正常退出且输出 `CineRTX 0.3.0`。
5. **合规与环境纯洁度**：  
   - `cmake -P cmake/AuditSpdx.cmake` 验证 `src/shim/` 100% 保持 `MIT OR Apache-2.0`；  
   - 全仓零 `.bak`、`.tmp` 垃圾文件，无 `std::getenv` 隐式旁路。
