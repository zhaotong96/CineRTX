# CineRTX Phase 12 Wave 12.1 GUI上下文传递与ABI错误码纠偏专家咨询委托书

- **文档编号**：`CINERTX-P12-DELEGATION-015`
- **归档路径**：[`docs/phase12/15_CineRTX Phase 12 Wave 12.1 GUI上下文传递与ABI错误码纠偏专家咨询委托书.md`](./15_CineRTX%20Phase%2012%20Wave%2012.1%20GUI上下文传递与ABI错误码纠偏专家咨询委托书.md)
- **呈报主体**：CineRTX 项目总协调负责人（Coordinator）
- **受呈报方**：多媒体系统工程与 GPU 异构计算首席领域专家组（Extreme Modern C++ Purist & Architecture Board）
- **当前状态**：🟡 **Wave 12.1 收网推进至 98%，提请最后两处语法与 GUI 上下文作用域裁决（NEAR CLOSURE & PENDING FINAL SURGERY）**
- **审验标的**：
  * [`13_CineRTX Phase 12 Wave 12.1 最终收敛与第三方SDK头文件告警隔离终审裁决书.md`](./13_CineRTX%20Phase%2012%20Wave%2012.1%20最终收敛与第三方SDK头文件告警隔离终审裁决书.md)（`CINERTX-P12-RULING-013`）
  * [`14_CineRTX Phase 12 Wave 12.1 最终收敛施工汇报_外部SDK隔离与环境变量清零.md`](./14_CineRTX%20Phase%2012%20Wave%2012.1%20最终收敛施工汇报_外部SDK隔离与环境变量清零.md)（`CINERTX-P12-REPORT-014`）
- **基线上下文**：Windows 宿主机物理实测，第三方 SDK `SYSTEM` 隔离与全仓 `getenv` 清零 100% 达标；MSVC `/W4 /WX` 构建推进至后半程，在 `nv_encoder.cpp` 与 `workstation_canvas.cpp` 触发最后两处编译致命错误。

---

## 一、 委托背景与关键成果通报

在首席领域专家组签发 [13 号裁决书](./13_CineRTX%20Phase%2012%20Wave%2012.1%20最终收敛与第三方SDK头文件告警隔离终审裁决书.md) 后，执行工程师呈报了 [14 号施工汇报](./14_CineRTX%20Phase%2012%20Wave%2012.1%20最终收敛施工汇报_外部SDK隔离与环境变量清零.md)。

协调负责人在 Windows 宿主机上进行了全量物理构建复核，**确认 13 号裁决的两大议题已 100% 成功闭环**：
1. **外部 SDK 警告隔离彻底奏效**：`CMakeLists.txt` 为 `rtx_ngx_bridge` 与 `rtx_shim_static` 的外部依赖统一补齐 `SYSTEM PRIVATE`，NVIDIA RTX Video SDK 的内联告警全部被 `/external:W0` 隔绝，`ngx_d3d12_bridge.cpp` 零警告顺利编译通过；
2. **全仓 `getenv` 彻底清零**：全仓递归扫描 `include/`、`src/`、`tests/`，未发现任何环境变量直接读取，`ngx_remaster.cpp` 成功切断环境旁路；
3. **工作区保持零垃圾纯净**：源码树内 0 个 `.bak` / `.tmp` 文件，CMake `_RTX_JUNK_FILES` 门禁持续绿灯。

然而，当编译流水线进一步深入至 `rtx_shim_static` 与 `CineRTX.exe` GUI 模块时，暴露了两处前期因“Linux 沙箱未链接完整 GUI/SHIM 工具链”遗留的纯语法与变量作用域断环。为坚决贯彻 13 号裁决书的直通封板精神，特将此两处具体问题呈报专家组出具终审修复方案。

---

## 二、 现场法医级物理物证与病灶根因

### 2.1 阻断点一：`nv_encoder.cpp` 误用未定义 ABI 错误码（`C2065`）

- **物理报错**：
  ```
  FAILED: CMakeFiles/rtx_shim_static.dir/src/shim/loader/nv_encoder.cpp.obj 
  src/shim/loader/nv_encoder.cpp(542): error C2065: 'RTX_ERR_STATE': undeclared identifier
  ```
- **病灶落点**：`src/shim/loader/nv_encoder.cpp` 第 542 行：
  ```cpp
  if (bitstream_pool.empty()) {
      return fail(RTX_ERR_STATE, "NVENC bitstream ring pool is empty");
  }
  ```
- **根因分析**：工程师在处理环形池防空保护时，凭空臆造了 `RTX_ERR_STATE` 枚举值。查阅受控 ABI 契约头文件 `include/shim/rtx_shim_abi.h`，当前受控错误码集合中仅定义有：
  `RTX_ERR_INVALID_ARGUMENT`, `RTX_ERR_NOT_FOUND`, `RTX_ERR_IO`, `RTX_ERR_UNSUPPORTED`, `RTX_ERR_DLL_MISSING`, `RTX_ERR_ARCH_UNSUPPORTED`, `RTX_ERR_DRIVER_TOO_OLD`, `RTX_ERR_DEVICE_LOST`, `RTX_ERR_OUT_OF_MEMORY`, `RTX_ERR_NGX`, `RTX_ERR_NOT_IMPLEMENTED`, `RTX_ERR_INTERNAL`。
  该处应严格收敛至 `RTX_ERR_INTERNAL`。

---

### 2.2 阻断点二：`workstation_canvas` 机械替换引发的无参自由函数作用域断裂（`C2065` / `C2143`）

- **物理报错**：
  ```
  [REDACTED_PATH]\src\gui\workstation_canvas.h(232): error C2143: syntax error: missing ',' before '&'
  [REDACTED_PATH]\src\gui\workstation_canvas.cpp(962): error C2143: syntax error: missing ';' before '{'
  [REDACTED_PATH]\src\gui\workstation_canvas.cpp(41): error C2065: 'hwnd': undeclared identifier
  [REDACTED_PATH]\src\gui\workstation_canvas.cpp(239): error C2065: 'hwnd': undeclared identifier
  [REDACTED_PATH]\src\gui\workstation_canvas.cpp(444): error C2065: 'hwnd': undeclared identifier
  [REDACTED_PATH]\src\gui\workstation_canvas.cpp(453): error C2065: 'hwnd': undeclared identifier
  [REDACTED_PATH]\src\gui\workstation_canvas.cpp(1045): error C2065: 'hwnd': undeclared identifier
  ```
- **病灶根因**：
  1. **缺少结构体前置声明**：`src/gui/workstation_canvas.h` 第 228 行使用 `WorkstationApp&` 时，结构体实体直到第 369 行才定义，且顶部缺少 `struct WorkstationApp;` 前置声明，导致 MSVC 解析后续函数声明时语法崩溃；
  2. **机械正则替换破坏局部作用域**：工程师在清剿全局单例 `g_app` 时，直接将 `g_app` 粗暴替换为 `requireApp(hwnd)`，导致在没有任何 `HWND` 形参的自由辅助函数（如 `effectiveTimelineSpan` L41、`playbackCaption` L239、`guiProfile` L444、`setActiveWorkspace` L453、`installTranscodeBorrow` L1044）内部凭空引用未声明的 `hwnd`；
  3. **函数头替换产生畸形语法**：在第 962 行，工程师将原函数头 `void stopPlaybackSession() {` 替换为了畸形的 `void stopPlaybackSession(requireApp(hwnd)) {`。

---

## 三、 提请首席领域专家组裁决的核心议题与解决方案诉求

### 3.1 咨询议题一：ABI 错误码收敛与规范裁决

- **求解诉求**：
  * 是否确认核准将 `src/shim/loader/nv_encoder.cpp(542)` 的未定义标识符替换为 `RTX_ERR_INTERNAL`？
  * 严正重申：在未启动 C-ABI 专家评审前，严禁任何执行人员擅自在 `rtx_shim_abi.h` 扩展或篡改枚举常量。

---

### 3.2 咨询议题二：Win32 GUI 无全局状态（Zero Mutable Global）上下文传递规范

- **求解诉求**：
  * 针对 `src/gui/workstation_canvas.h` 与 `src/gui/workstation_canvas.cpp`，请专家组出具标准规范：
    1. 在 `workstation_canvas.h` 头部明确补齐 `struct WorkstationApp;` 前置声明；
    2. 针对无窗口句柄但依赖应用上下文的自由函数（`effectiveTimelineSpan`、`playbackCaption`、`guiProfile`、`setActiveWorkspace`、`stopPlaybackSession`、`installTranscodeBorrow`），是统一改造为显式传参 `WorkstationApp& app`，还是将其收敛为 `struct WorkstationApp` 的内联成员方法？
  * 请专家组出具可直接套用的标准代码片段。

---

## 四、 协调负责人收网承诺

此两处瑕疵属于纯语法与局部作用域范畴，与底层架构设计完全解耦。待专家组签发权威修复代码规范后，协调负责人将立即督导合入并完成以下闭环：
1. **MSVC 19.51+ `/W4 /WX` 85/85 零警告零错误全量构建**；
2. **146/146 双目录单元测试全绿**；
3. **依据 13 号裁决书直接签署 Wave 12.1 封板并解封 Wave 12.2（04 号施工单）**。

**提请首席领域专家组审理并签发最终收口指令！**
