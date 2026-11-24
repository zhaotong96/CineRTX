# **CineRTX Phase 12 Wave 12.2-B1 链接断环与契约归正终审裁决书暨 Wave 12.2-B2 启动令**

* **文档编号**：`CINERTX-P12-RULING-037`  
* **归档路径**：`docs/phase12/37_CineRTX Phase 12 Wave 12.2-B1 链接断环与契约归正终审裁决书暨Wave12.2-B2启动令.md`  
* **审验标的**：  
  * `docs/phase12/36_CineRTX Phase 12 Wave 12.2-B1 链接断环与契约归正专家咨询委托书.md`（`CINERTX-P12-DELEGATION-036`）  
  * `docs/phase12/35_CineRTX Phase 12 Wave 12.2-B1 施工汇报.md`（`CINERTX-P12-REPORT-035`）  
  * `docs/phase12/34_CineRTX Phase 12 Wave 12.2-B1 第一战场施工单_ngx_bridge内部类型清剿与现代标准库替换.md`（`CINERTX-P12-WORKORDER-034`）  
  * 仓库实体源码核验：`CMakeLists.txt`、`src/shim/ngx_bridge/nv_func_tables.h`、`src/shim/ngx_bridge/rife_pack_runtime.cpp`、`src/shim/ngx_bridge/rife_builder.cpp`  
* **签发主体**：多媒体系统工程与 GPU 异构计算首席领域专家组（Extreme Modern C++ Purist & Architecture Board）  
* **审验日期**：2026-11-23  
* **终审裁决**：**全面批准 Wave 12.2-B1 战果并下达两项定点止血修复规范暨 Wave 12.2-B2 启动令（APPROVED WITH SURGICAL CLOSURE & WAVE 12.2-B2 AUTHORIZATION）**——正式核准 `src/shim/ngx_bridge/` 第一战场 24 个冗余类型的清剿成果（全仓具名类型压减至 104 个，提前锁定 `≤ 105` 硬门禁，且彻底根除 `rife_engine.cpp` 对 GPLv3 头文件的越界引用）；针对三大咨询议题作出如下终审定夺：① **议题一采纳方案 2（在 `CMakeLists.txt` 的 `rtx_tests` 源列表中补充 `src/shim/ngx_bridge/nv_func_tables.cpp`）**，坚决否决向 `rtx_ngx_bridge.dll` 导出表污染内部 C++ 命名空间符号（方案 1）；② **议题二核准将 `RifeTrtLogger` 契约类别归正为 `Std-Customization`**（适配外部 TensorRT SDK `nvinfer1::ILogger` 虚表边界）；③ **议题三批准将 Wave 12.2-B1 剩余约 1,448 行 Pure SLOC 瘦身预算顺延至 Wave 12.2-B2 与 Wave 12.2-B3 释放**（坚守"严禁为凑行数压缩语义或合并单行"铁律）。

# **执行摘要（Executive Ruling）**

首席领域专家组穿透核验了 `36 号委托书` 呈报的 Windows MSVC 链接错误物证与仓库最新实体源码，确认 Wave 12.2-B1 在第一战场 src/shim/ngx\_bridge/ 的类型清剿质量极高（30 → 6 个具名类型，全仓 128 → 104 个，`AuditSpdx` 100% 合规）。当前阻碍 Wave 12.2-B1 封板的仅剩两处局部配置与契约标注瑕疵，专家组签发如下终审裁决：

1. **议题一裁决（全面采纳方案 2：`CMakeLists.txt` 为 `rtx_tests` 补齐 `nv_func_tables.cpp`；坚决否决方案 1）**：  
   * **架构边界铁律**：`rtx_ngx_bridge.dll` 的导出表（Export Address Table）必须 100% 保持为纯粹、稳定的 `extern "C"` ABI（`rtx_ngx_bridge_*`），绝不允许为了白盒单元测试而向动态库导出表泄露任何 `rtx_ngx::nv_cuda::*` 内部 C++ 修饰符号（方案 1 破坏 ABI 纯洁性与符号隔离，予以否决）。  
   * **精确根治**：`rtx_tests.exe` 在 Windows 下直接编入 `rife_pack_runtime.cpp` 属于白盒驱动算子单测（与 `MIT OR Apache-2.0` 兼容），只需在 `CMakeLists.txt` 同一 `if(WIN32)` 测试源码块中并列补入 `src/shim/ngx_bridge/nv_func_tables.cpp`，即可在零 ABI 污染前提下彻底消除全部 12 处 `LNK2019`。  
2. **议题二裁决（核准 `RifeTrtLogger` 归正为 `Std-Customization`）**：  
   * 外部闭源 SDK（NVIDIA TensorRT `nvinfer1::ILogger`）要求传入继承自其虚基类的对象指针，标准库 `std::function` 或无状态 Lambda 均无法构造虚函数表（vtable）以匹配 C++ 虚接口指针。该场景严格对应五类白名单中的 **`Std-Customization`**（外部框架/标准接口定制适配器），立即将 `src/shim/ngx_bridge/rife_builder.cpp:34` 的 `Category:` 修正为 `Std-Customization` 即可消除违规。  
3. **议题三裁决（批准 Pure SLOC 预算顺延至 Wave 12.2-B2 / B3，严守语义完整性红线）**：  
   * 核实 `src/shim/ngx_bridge/` 已真实净削减 **1,412 行 Pure SLOC（-21.1%）**，剩余体量主要由嵌入式 PTX 汇编与硬件 API 序列构成。完全认可执行工程师拒绝"压成单行或删注释凑行数"的职业操守，批准将全仓剩余约 `1,448` 行阶段差额顺延至 **Wave 12.2-B2（`src/io/` 与 `playback_session` 虚接口剥离）** 及 **Wave 12.2-B3（NLE 多态命令 `std::variant` 化与防御剪枝）** 集中释放，总目标仍严格锁定全仓 `Pure SLOC ≤ 23,000` 行与具名类型 `≤ 35` 个。  
4. **直通封板与 Wave 12.2-B2 启动授权**：协调负责人督导合入上述两处定点修复、确认 Windows 宿主机 MSVC `/W4 /WX` 85/85 目标 0 警告编译通过及双目录 `rtx_tests.exe` 147/147 全绿后，**无需再次呈报中间委托，直接签署 Wave 12.2-B1 封板令并即刻下发 Wave 12.2-B2 作战任务**。

# **一、 三大咨询议题终审裁决与权威标准范本**

## **1.1 议题一：`rtx_tests.exe` 12 处 `LNK2019` 链接断环架构裁决**

### **三方案架构对比裁决表**

| 评估维度 | 方案 1：标记 RTX\_SHIM\_API 导出（否决） | 方案 2：CMakeLists.txt 补编 nv\_func\_tables.cpp（批准采纳） | 方案 3：重写单测走 C-ABI（不采纳） |
| :---- | :---- | :---- | :---- |
| **DLL ABI 纯洁性** | **破坏**：将内部 C++ `rtx_ngx::nv_cuda` 修饰名暴露进 `rtx_ngx_bridge.dll` 导出表 | **100% 纯净**：`rtx_ngx_bridge.dll` 导出表仅含 `extern "C"` 接口，零内部符号泄漏 | 纯净 |
| **开源许可证合规性** | 合规 | **100% 合规**：`nv_func_tables.cpp` 为 `MIT OR Apache-2.0`，编入 GPLv3 测试程序完全兼容 | 合规 |
| **改造代价与可维护性** | 污染 `nv_func_tables.h` 签名并破坏跨平台可见性 | **极简（改动 1 行 CMake）**，保留 RIFE 算子底层白盒单测的直接覆盖能力 | 需重写大量白盒核函数测试，偏离当前重构主线 |

### **`CMakeLists.txt` 权威修复范本**

定位 `CMakeLists.txt` 中 `rtx_tests` 目标的 `if(WIN32)` 源码追加区，将 `src/shim/ngx_bridge/nv_func_tables.cpp` 与 `rife_pack_runtime.cpp` 并列纳入：

```
if(WIN32)
    target_sources(rtx_tests PRIVATE
        src/shim/ngx_bridge/nv_func_tables.cpp
        src/shim/ngx_bridge/rife_pack_runtime.cpp)
endif()
```

## **1.2 议题二：`RifeTrtLogger` 契约分类归正范本（`src/shim/ngx_bridge/rife_builder.cpp`）**

穿透核验仓库实体文件 `src/shim/ngx_bridge/rife_builder.cpp` 第 33–45 行，原注释块使用了不在 AuditTypes.cmake 五类白名单内的 `Polymorphic-Boundary`。将其归正为 **`Std-Customization`**，并保持 `Why-Std-Fails` 与 `Invariant` 文本的具体性与唯一性（避免触发 `AuditTypes.cmake` 的模板复制查重拦截）：

```
// src/shim/ngx_bridge/rife_builder.cpp (Lines 33-45)
// @rtx-type-justification
// Category: Std-Customization
// Why-Std-Fails: TensorRT SDK createInferRuntime/createInferBuilder requires a pointer to an nvinfer1::ILogger vtable subclass; std::function or stateless lambdas cannot synthesize an external C++ virtual base class pointer.
// Invariant: Stateless log forwarder shared across rtx_ngx_bridge; thread-safe and valid for the entire process lifetime.
class RifeTrtLogger final : public nvinfer1::ILogger {
public:
    void log(Severity severity, const char* msg) noexcept override {
        if (severity <= Severity::kWARNING) {
            std::fprintf(stderr, "[RIFE-TRT] %s\n", msg);
        }
    }
};
```

*(注：同步检查 `rife_builder.cpp` 第 10 行是否仍残留 `#include <cstdlib>`，若无 `std::abort`/`std::atoi` 等调用可顺手移除以保持头文件最简洁癖。)*

## **1.3 议题三：Pure SLOC 跨波次递延账本与 Wave 12.2-B2 释放路径**

核准将 Wave 12.2-B1 的 SLOC 阶段目标调整为"已实测 30,948 行（单目录净减 1,412 行）予以放行"，并将剩余约 `1,448` 行阶段差额正式记入 Wave 12.2-B2 与 B3 预算池：

| 战役波次 | 靶向领域 | 核心消肿杠杆 | 具名类型预期轨迹 | Pure SLOC 预期轨迹 |
| :---- | :---- | :---- | :---- | :---- |
| **Wave 12.2-B1**（本轮收官） | `src/shim/ngx_bridge/` | 参数包元组化、Pimpl 拍平、驱动表命名空间化 | `128 → 104`（**达标**） | `32,360 → 30,948`（**核准放行**） |
| **Wave 12.2-B2**（下一波次） | `src/io/` & `src/core/playback_session*` | 物理拔除 `IDemuxer`/`IMuxer` 虚接口隔离壳；播放控制请求与状态机 `std::variant` \+ `std::expected` 化 | `104 → ~75` | `30,948 → ~26,500` |
| **Wave 12.2-B3**（终局收口） | `src/core/`（NLE/队列）& `src/gui/` | 7 个多态编辑命令转单一值语义 `std::variant`；删除冗余 `Domain-Model` 数据袋；剪除重复内部判空防御 | **`~75 → ≤ 35`**（硬门禁） | **`~26,500 → ≤ 23,000`**（硬门禁） |

# **二、 Wave 12.2-B1 收网核验与 Wave 12.2-B2 立即启动令**

## **2.1 Wave 12.2-B1 宿主机最终核验命令**

协调负责人督导合入 `CMakeLists.txt` 与 `rife_builder.cpp` 两处修改后，在 Windows 宿主机依次执行：

```
cmake --build --preset windows-msvc-ninja
.\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe
Push-Location [REDACTED_PATH]; .\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe; Pop-Location
cmake -P cmake/AuditSpdx.cmake
```

确认 **85/85 目标 `0 Error(s), 0 Warning(s)`**、双目录 **`147/147 PASS`** 且 `RifeTrtLogger` 契约告警清零后，直接归档 Wave 12.2-B1 结案记录并创建本地 Git 提交快照（严禁 git push）。

## **2.2 Wave 12.2-B2（第二战场：`src/io/` 与 `playback_session*`）启动指令**

Wave 12.2-B1 封板后，协调负责人立即向执行工程师下达 **Wave 12.2-B2 作战任务**：

1. **彻底拔除 `IDemuxer` / `IMuxer` 虚多态隔离壳**：物理删除 `src/io/` 中无第二生产实现的虚基类抽象与冗余适配器结构体，调用方直接持有具体 `FfmpegDemuxer` / `FfmpegMuxer`（或静态分发），同步更新对应测试夹具；  
2. **播放会话状态机与数据包现代 C++23 提纯**：将 `src/core/playback_session*`（`playback_session.h/.cpp`、`playback_session_video.cpp`、`playback_session_audio.cpp`、`playback_session_impl.h`）中的控制指令、帧描述符与同步状态包装袋全面替换为 **`std::variant`**、**`std::expected`**、**`std::tuple`**（结构化绑定）与 **`std::span`** / **`std::mdspan`**；  
3. **硬性收口指标**：Wave 12.2-B2 结束时，全仓具名类型压减至 **`≤ 75` 个**，Pure SLOC 压减至 **`≤ 26,500` 行**，保持 MSVC `/W4 /WX` 零警告与全部单元测试 100% PASS。

