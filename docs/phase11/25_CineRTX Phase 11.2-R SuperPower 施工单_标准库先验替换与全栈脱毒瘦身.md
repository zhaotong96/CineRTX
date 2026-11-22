# CineRTX Phase 11.2-R SuperPower 施工单：标准库先验替换与全栈脱毒瘦身

- **施工单号**：`CINERTX-P11-SP-025`
- **归档路径**：[`docs/phase11/25_CineRTX Phase 11.2-R SuperPower 施工单_标准库先验替换与全栈脱毒瘦身.md`](25_CineRTX%20Phase%2011.2-R%20SuperPower%20施工单_标准库先验替换与全栈脱毒瘦身.md)
- **承接角色**：核心多媒体运行时与现代 C++ 架构重构执行工程师（外部执行工程师）
- **发单角色**：项目总协调负责人（遵照用户最高裁决与领域专家裁决）
- **前置基线**：Phase 11.2 施工后基线（128/128 测试全绿，驱动 621.24 WHQL，当前全仓手写源码 43,450 行，净增 +5,824 行）
- **依据母本**：
  - [`docs/phase11/23_CineRTX Phase 11.2 代码膨胀与Slop重度污染治理专家咨询委托书.md`](23_CineRTX%20Phase%2011.2%20代码膨胀与Slop重度污染治理专家咨询委托书.md)
  - [`docs/phase11/24_CineRTX 第23号文档深度方案.md`](24_CineRTX%20第23号文档深度方案.md)
  - [`docs/phase11/17_CineRTX Phase 11 外包施工总纲暨协调负责人备忘录.md`](17_CineRTX%20Phase%2011%20外包施工总纲暨协调负责人备忘录.md)
- **建议工期**：1.5–2 个有效工作日
- **核心目标**：彻底铲除伪面向对象包装器 Slop，贯彻“标准库先验替换（Std-First）”；消解全仓无意义自定义类，建立 `@rtx-type-justification` 存续声明与 CMake 拦截门禁；**将全仓手写源码净增量从当前的 +5,824 行断崖式压缩至 $\le +2,000$ 行以内（目标回落至 $\approx +1,887$ 行，单阶段净瘦身率 $\ge 60\%$）**，且 128/128 单元测试与 5.02 MB PE 导入表保持 100% PASS。

---

## 一、 施工背景与最高架构军规

Phase 11.2 虽取得了 128/128 单测全绿并留存了 Linker `.map` 修饰名，但单切片代码物理暴增 6,927 行（净增 5,824 行），暴露出极度严重的 **AI 生成式样板堆砌（Slop）、伪面向对象过度包装、私有类泛滥与无视 C++ 标准库** 的恶习。用户已下达最高指令**紧急熔断 Phase 11.3 推进**，设立为期 1.5–2 个工作日的 **Phase 11.2-R（Refactor & De-slop）** 专项脱毒波次。

外部执行工程师进场前必须无条件宣誓并严格遵守以下 **三大最高架构铁律**：

1. **重新定义 Slop：手写包装器（`raii_owner`）等同于无意义的 Slop**
   - 现代 C++（CineRTX 已启用 `/std:c++latest` C++26）标准库原语极其完备。手写数十行包含构造/移动/析构样板的 `UniqueHandle`、`UniqueThread`、`DecoderOwner` 本质上是劣质的生成式 Slop！
   - 一律废除“为每个句柄或指针手写自定义结构体”的恶习。
2. **标准库先验替换律 (Law of Standard Library Primacy)**
   - **“所有的类都应该用 std 标准库先替换，实在没办法必须写的类才应该存在。”**
   - 独占所有权一律优先使用 `std::unique_ptr<T, Deleter>`；
   - 协作线程管理一律强制使用 C++20 `std::jthread` + `std::stop_token`，彻底淘汰手写 `UniqueThread`；
   - 时间度量一律使用 `std::chrono::duration`（Flicks 严格有理数分数），彻底清零 `0.7056` 浮点数魔法常数；
   - 状态组合与跨线程传递一律优先使用 `std::pair`、`std::tuple`、`std::variant`、`std::optional`、`std::span`，严禁随意手写临时包裹结构体。
3. **自定义类零容忍与显式声明存在意义律 (Zero Tolerance & Explicit Justification)**
   - 全仓对任何新增或存留的 `class` / `struct` 持零容忍态度；
   - 任何存续的具名类型，其声明紧邻上方必须包含 `@rtx-type-justification` 显式声明块，明确其为何无法用 `std::*` 替代。凡未声明或理由不成立者，构建期由 `cmake/AuditTypes.cmake` 强行阻断！

---

## 二、 核心重构任务与实施规范

### 任务 R-1：公共底座脱毒与工具链适配（预计减量：约 -609 行）
涉及文件：`include/rtx/com_owner.h`、`include/rtx/raii_owners.h`、`src/core/wasapi_audio_output.cpp`、`src/gui/d3d12_raii.h`、`CMakeLists.txt`

1. **物理删除 `include/rtx/com_owner.h`**：
   - 执行 `git rm include/rtx/com_owner.h`（清除 94 行手写智能指针轮子）。
2. **`include/rtx/raii_owners.h` 极致降维（385 行 $\to$ 约 55 行）**：
   - 引入单一 4 行无状态函数指针删除器模板：
     ```cpp
     template <auto Fn>
     struct FnDeleter {
         void operator()(auto p) const noexcept {
             if (p) Fn(p);
         }
     };
     ```
   - 将 Win32 句柄包装器全量收缩为 `std::unique_ptr` 类型别名：
     ```cpp
     using UniqueHandle = std::unique_ptr<void, FnDeleter<&CloseHandle>>;
     using UniqueModule = std::unique_ptr<HINSTANCE__, FnDeleter<&FreeLibrary>>;
     using UniqueHwnd = std::unique_ptr<HWND__, FnDeleter<&DestroyWindow>>;
     using UniqueBrush = std::unique_ptr<HBRUSH__, FnDeleter<&DeleteObject>>;
     using UniqueFont = std::unique_ptr<HFONT__, FnDeleter<&DeleteObject>>;
     ```
   - **Win32 边界防护**：在 `CreateFileW` 调用侧必须将 `INVALID_HANDLE_VALUE` 归一化为 `nullptr`，防止 `unique_ptr::operator bool()` 将 `-1` 误判为有效句柄。
   - **COM 智能指针收口**：
     ```cpp
     inline void releaseComUnknown(void* p) noexcept {
         if (p) static_cast<IUnknown*>(p)->Release();
     }
     template <typename T>
     using ComPtr = std::unique_ptr<T, FnDeleter<&releaseComUnknown>>;
     ```
   - **彻底拔除 `UniqueThread`**：全仓切换为 C++20 原生 `std::jthread`。
   - **CUDA 与复杂资源降维**：
     - `CudaPinnedHostBuffer` 替换为 `std::pair<std::unique_ptr<void, std::function<void(void*)>>, size_t>`；CUDA 锁页内存若缺失释放函数则绑定 no-op 删除器，严禁回退 `std::free`。
     - `CudaDeviceBuffer`：鉴于 `uint64_t` 是 GPU 设备显存地址且不满足标准库 `NullablePointer` 要求，保留不足 25 行的极简值结构体，并强制加注 `Category: Hardware-Layout` 存续声明。
3. **`wasapi_audio_output.cpp` 局部清理**：
   - 拔除 `WapiComOwner<T>`、`ComApartment`、`MixFormatOwner`，改用 `ComPtr<T>`、`FnDeleter<&CoTaskMemFree>` 与 `CoUninitialize` 作用域哨兵。
4. **工具链暗礁适配（Zig/Clang libc++）**：
   - 在 `CMakeLists.txt` 中增加探测：当编译器为 Clang/Zig 且使用 libc++ 时，追加 `-fexperimental-library` 编译标志，放行 `std::jthread` 与 `std::stop_token`。

---

### 任务 R-2：播放内核消肿与时间语义重构（预计减量：约 -1,239 行）
涉及文件：`include/rtx/time_model.h`、`include/rtx/playback_session.h`、`src/core/playback_session.cpp`、`tests/test_playback_session.cpp`、`tests/test_raii_retirement.cpp`

1. **标准 Flicks 时间语义落地（清零 `0.7056` 浮点数）**：
   - 在 `include/rtx/time_model.h` 定义原生标准时间周期：
     ```cpp
     using Flicks = std::chrono::duration<int64_t, std::ratio<1, 705600000>>;
     ```
   - 利用标准 `<chrono>` 在编译期自动折算：$1\text{ ns} = \frac{441}{625}\text{ Flicks}$（严格有理数，零精度丢失）。
   - 彻底删除 `PlaybackClock::now()` 中硬编码的 `ns * 0.7056` 浮点运算与 80 行时钟类，改为 2 个无状态自由函数操作状态元组。
2. **消灭全部 8 个私有类与碎片包装类**：
   - **`playback_session.cpp` 匿名命名空间 5 个私有类清零**：
     - `ShimOwner`、`DecoderOwner` $\to$ 合并为两行 `std::unique_ptr` 别名；
     - `AudioDecodeChain` $\to$ 拆解为 4 个 FFmpeg `std::unique_ptr` 别名与 2 个无状态自由函数（`openAudioDecode`、`drainAudioDecode`）；
     - `VarispeedCursor` $\to$ 浓缩为单一 18 行自由函数 `applyLinearVarispeed()`；
     - `AudioPacketQueue` $\to$ 直接使用 `std::deque<Packet>` 与 `std::condition_variable_any`。
   - **`playback_session.h` 3 个公共包装类清零**：
     - `PlaybackClock` $\to$ 自由函数 + `Flicks`；
     - `FrameDeadlineScheduler` $\to$ constexpr 自由函数 `classifyFrameDeadline()` 与 `waitDurationFlicks()`；
     - `DecodeAheadQueue` 与 `DecodeAheadSlot` $\to$ 由 `Impl` 直接持有的有界 `std::deque<std::tuple<RtxFrameDesc, int64_t, uint64_t>>`（帧描述、呈现截止 Flicks、退役 Fence）取代。
3. **单线程亲和拓扑与彻底拔除 `audio_state_mutex`**：
   - 彻底删除跨线程互斥锁 `audio_state_mutex`；
   - `audio_worker`（`std::jthread`）独占拥有 FFmpeg 解码上下文与变速尾迹；
   - 当 `video_worker` 执行 seek 时，仅向音频包队列投递携带递增 `seek_epoch` 的空包哨兵；
   - `audio_worker` 收到纪元变更后自行在单线程内执行 `avcodec_flush_buffers()` 与缓冲复位，实现**零跨线程锁竞争**；
   - 借助 `std::condition_variable_any::wait(lock, stoken, ...)` 响应 `std::jthread` 析构自动发出的停止信号，清除手写的 `closed_` 与双层停止胶水。
4. **单测覆盖率 100% 保持**：
   - 同步改写 `tests/test_playback_session.cpp`，将原针对 `PlaybackClock`、`FrameDeadlineScheduler`、`DecodeAheadQueue` 的 4 组调度单测对齐至新自由函数与 `std::deque` 策略；
   - 补齐 A-02 真实小文件编解码中段失败注入断言，确保 128/128 项单测全量 PASS。

---

### 任务 R-3：视口解耦与界面巨石拆解（预计减量：约 -988 行）
涉及文件：`include/rtx/ngx_remaster.h`、`src/core/ngx_remaster.cpp`、`src/gui/rtx_gui.cpp`、`src/gui/preview_viewport.cpp`、`src/gui/preview_viewport.h`、`src/gui/d3d12_raii.h`

1. **拔除全局静态注册表并落地统一无头 Sink**：
   - 彻底删除 `src/core/ngx_remaster.cpp#L85-L160` 中的文件级全局变量 `g_active_preview_bridge`、`g_preview_slot_flags` 与 `g_preview_latest_slot`；
   - 在 `include/rtx/ngx_remaster.h` 与 `ngx_remaster.cpp` 暴露调用方实例持有的无头预览 Sink 工厂：
     ```cpp
     std::unique_ptr<IVideoFrameSink> createHeadlessPreviewSink(
         const MediaInfo& info,
         std::function<void()> on_frame,
         BorrowCallback& out_borrow
     );
     ```
   - 将 PQ/MaxNits 折算与色彩矩阵收归核心层，直接通过 `rtx_ngx_bridge_borrow_preview` 获取常驻指针，消除冗余的 `Borrowed` 缓存结构。
2. **`src/gui/rtx_gui.cpp` 彻底脱毒（1,539 行 $\to$ 约 880 行）**：
   - 物理拔除暴力内联的 `PlaybackDCompSink`（203 行）；
   - 彻底移除 `#include "shim/ngx_bridge_abi.h"` 与 `#include "rtx/nvidia_shim.h"`，界面层仅依赖 `IVideoFrameSink` 抽象；
   - 将重复的 `presentPlaybackFrame()` 与 `presentTranscodePreview()` 合并为单一 15 行 `presentSlotToViewport()`；
   - 将 `WM_CREATE` 与 `applyWorkspaceLayout()` 中冗长的 `MoveWindow` 样板改写为 `std::array` 控件坐标表驱动；
   - 窗口辅助线程与 GDI 句柄全量切至 `std::jthread` 与标准别名。
3. **`preview_viewport.cpp` 与 `d3d12_raii.h` 瘦身（1,029 行 $\to$ 约 730 行）**：
   - 全面改用 `ComPtr` + `std::out_ptr<void*>`；
   - **MSVC 编译暗礁防护**：因 `std::out_ptr` 缺少 `operator*`，严禁使用 `IID_PPV_ARGS(std::out_ptr(...))`，必须统一使用 `__uuidof(T), std::out_ptr<void*>(ptr)`；
   - 提取单一 `resourceBarrier()` 辅助函数，统一 `markDeviceLost()` 错误传播；
   - 100% 保留 UINT64_MAX 校验、全命令列表退役、Waiting 序及 reserved[0] 四项安全加固。

---

### 任务 R-4：测试夹具物理十六进制机器码彻底剥离（预计减量：约 -1,281 行）
涉及文件：`tests/fixtures/test_streams.h`、`tests/fixtures/`、`CMakeLists.txt`、`tests/test_playback_session.cpp`

1. **彻底物理移除十六进制头文件**：
   - 执行 `git rm tests/fixtures/test_streams.h`（彻底消灭 1,306 行硬编码十六进制数组与无效 `0x00` 填充）。
2. **实体微型 `.mkv` 样本还原与 CMake 自动部署**：
   - 在 `tests/fixtures/` 下提交 3 份真实微型 Matroska 物理二进制样本（每份仅数 KB，先封装无音轨 MKV 再合流 440 Hz AAC 音轨，无 priming 偏移）：
     - `tests/fixtures/h264_25fps_gop12.mkv`
     - `tests/fixtures/hevc_25fps_gop12.mkv`
     - `tests/fixtures/av1_25fps_gop12.mkv`
   - 在 `CMakeLists.txt` 中为 `rtx_tests` 目标添加 `POST_BUILD` 指令：
     ```cmake
     add_custom_command(TARGET rtx_tests POST_BUILD
         COMMAND ${CMAKE_COMMAND} -E copy_if_different
             "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/h264_25fps_gop12.mkv"
             "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/hevc_25fps_gop12.mkv"
             "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/av1_25fps_gop12.mkv"
             "$<TARGET_FILE_DIR:rtx_tests>/fixtures/"
     )
     ```
3. **测试端标准库读入**：
   - 在测试代码中通过 `<fstream>` 以不到 10 行读入内存：
     ```cpp
     std::ifstream file(fixture_path, std::ios::binary | std::ios::ate);
     auto size = file.tellg();
     file.seekg(0, std::ios::beg);
     std::vector<uint8_t> buffer(size);
     file.read(reinterpret_cast<char*>(buffer.data()), size);
     ```
   - 经由 `makeMemoryDemuxer()` 驱动 `MemoryAvioContext`，版本库源码树彻底恢复 0 机器码纯净态。

---

### 任务 R-5：存续类型显式声明与 CMake 静态门禁（预计增量：约 +180 行）
涉及文件：`cmake/AuditTypes.cmake`、`CMakeLists.txt`、全仓保留的头文件与源文件

1. **存续类型标准声明契约**：
   全仓所有保留或新增的 `class` 与 `struct`（不含 enum class、前向声明及 `FnDeleter`）必须在其定义紧邻上方附带标准声明：
   ```cpp
   // @rtx-type-justification
   // Category: <C-ABI | Hardware-Layout | Polymorphic-Boundary | NonTrivial-StateMachine | Concurrency-Invariant | Domain-Model>
   // Why-Std-Fails: <说明为何 std::unique_ptr/std::jthread/std::tuple/std::variant/自由函数无法表达>
   // Invariant: <概括该类型唯一不可替代的内存布局、跨界接口或异步生命周期不变量>
   ```
2. **六大合法豁免白名单分类**：
   - `C-ABI`：`extern "C"` 跨模块导出结构、D3D12 根常量布局；
   - `Hardware-Layout`：`alignas(64)` 缓存行对齐结构、`CUdeviceptr` 显存地址包装；
   - `Polymorphic-Boundary`：抽象纯虚接口及其跨层实现（如 `IVideoFrameSink`、`IDemuxer`）；
   - `NonTrivial-StateMachine`：协调多资源异步生命周期的大型宿主状态机（如 `PreviewViewport`、`GpuRetirementQueue`、`MediaPlaybackSession`、`WasapiAudioOutput`）；
   - `Concurrency-Invariant`：无锁并发数据结构（如 Seqlock `AtomicTelemetryBus`）；
   - `Domain-Model`：承载静态反射或多字段领域配置模型（如 `TranscodeProfile`、`MediaInfo`）。
3. **编写纯 CMake 门禁脚本 `cmake/AuditTypes.cmake`**：
   - 扫描 `include/` 与 `src/` 下所有 `.h`、`.hpp`、`.cpp` 中的具名 `class` 与 `struct`；
   - 若定义前 8 行内缺失合法 `@rtx-type-justification` 声明块，或 `Category` 不在上述六类白名单中，立即抛出 `message(FATAL_ERROR ...)` 阻断构建。
   - 在根 `CMakeLists.txt` 中注册该门禁，并在 CI 与测试流程中常规运行。

---

## 三、 物理文件隔离与职责边界矩阵

严守 `DEC-022` 确立的正交分工，外部执行工程师与项目协调负责人职责严格物理隔离：

| 物理文件 / 模块路径 | 变更性质 | 责任主体 | 权限与动作约束 |
| :--- | :--- | :--- | :--- |
| `include/rtx/com_owner.h` | 物理删除 | **外部执行工程师** | 执行 `git rm`，清零手写轮子 |
| `include/rtx/raii_owners.h` | 核心重构 | **外部执行工程师** | 收缩为 `FnDeleter` 与别名，消除样板类 |
| `include/rtx/time_model.h` | 扩展重构 | **外部执行工程师** | 增加 `Flicks` chrono duration，消除浮点魔法数 |
| `include/rtx/playback_session.h` | 接口精简 | **外部执行工程师** | 清除 3 个公共包装类，改用标准库原语与自由函数 |
| `src/core/playback_session.cpp` | 深度脱毒 | **外部执行工程师** | 清除 5 个私有类，拔除 `audio_state_mutex`，压缩至 $\le 500$ 行 |
| `src/core/wasapi_audio_output.cpp` | 标准库适配 | **外部执行工程师** | 消除私有 COM 包装类，使用 `ComPtr` 与 `std::jthread` |
| `src/core/ngx_remaster.cpp` | 接口收口 | **外部执行工程师** | 拔除文件级静态全局变量，暴露无头 Sink 工厂 |
| `src/gui/rtx_gui.cpp` | 界面瘦身 | **外部执行工程师** | 物理拔除 `PlaybackDCompSink`，表驱动重构，压缩至 $< 1,000$ 行 |
| `src/gui/preview_viewport.cpp/.h` | 边界提纯 | **外部执行工程师** | 统一屏障，适配 `std::out_ptr`，压缩至 $\approx 730$ 行 |
| `src/gui/d3d12_raii.h` | 提纯收敛 | **外部执行工程师** | 收拢临时数据结构，保留必要加固 |
| `tests/fixtures/test_streams.h` | 物理删除 | **外部执行工程师** | 执行 `git rm`，彻底清零十六进制数组 |
| `tests/fixtures/*.mkv` | 实体还原 | **外部执行工程师** | 还原 3 份轻量真实二进制样本 |
| `tests/test_playback_session.cpp` | 单测适配 | **外部执行工程师** | 适配自由函数与 Flicks，补齐 A-02 注入断言 |
| `cmake/AuditTypes.cmake` | 新建门禁 | **外部执行工程师**编写 / **协调负责人**核验 | 纯 CMake 原生扫描门禁，阻断无声明自定义类 |
| `CMakeLists.txt` | 构建链微调 | **外部执行工程师** | 挂载 POST_BUILD 复制、Clang libc++ 标志与门禁 |
| **8 大准出门禁复核与 WTS 桌面实呈** | 战略验收 | **协调负责人专守** | 独立执行全套门禁复核、Linker .map 审计与真机实测 |

---

## 四、 八大硬性准出验收门禁（Gate R-1 ～ Gate R-8）

外部执行工程师完工后，必须通过以下 8 项刚性门禁，缺一不可：

```text
[Phase 11.2-R 刚性验收门禁流水线]
+---------------------------------------------------------------------------------+
| Gate R-1: 全仓手写代码净增量 <= +2,000 行（总行数 <= 39,626，相比 +5,824 瘦身 >= 60%） |
| Gate R-2: playback_session.cpp 私有类数量归 0，总行数 <= 500 行，0.7056 彻底清零 |
| Gate R-3: rtx_gui.cpp 行数 < 1,000 行，PlaybackDCompSink 归零，硬件引用头文件归零  |
| Gate R-4: test_streams.h 彻底移除，全仓头文件与源码中十六进制码流数组 0 残留    |
| Gate R-5: cmake -P cmake/AuditTypes.cmake 返回 0，存续类 100% 具备合法存在声明  |
| Gate R-6: 128/128 单测 PASS、PE 导入表 1.2–12.0 MB PASS、Linker .map 修饰名保留   |
| Gate R-7: MSVC /MT 零警告编译，Zig/Clang libc++ CI 编译无阻断                   |
| Gate R-8: WTS Session > 0 物理桌面真机实呈闭环（D2H == 0 B，100次 Resize 无崩溃） |
+---------------------------------------------------------------------------------+
```

### 门禁详细技术指标清单：
1. **Gate R-1（代码体量红线）**：
   - 运行统计脚本：`src/` + `tests/` + `include/` + `cmake/` 手写源码总物理行数相对 Phase 11.1 基线（37,626 行）净增量必须 $\le +2,000$ 行（总行数 $\le 39,626$ 行，预期目标约 39,513 行）。
2. **Gate R-2（`playback_session.cpp` 纯洁度）**：
   - 文件内除合法标注的 `Impl` 外，私有 `class` / `struct` 数量为 0；
   - 文件物理总行数 $\le 500$ 行（预期约 420 行）；
   - 正则扫描 `0.7056` 与 `audio_state_mutex` 命中数为 0。
3. **Gate R-3（GUI 边界隔离）**：
   - `rtx_gui.cpp` 中 `PlaybackDCompSink` 彻底清零，行数 $< 1,000$ 行（预期约 880 行）；
   - `rtx_gui.cpp` 中禁止包含 `shim/ngx_bridge_abi.h` 与 `rtx/nvidia_shim.h`；
   - `rg "runScrubPreviewAsync|SetTimer\(hwnd, 3, 66" src/gui/rtx_gui.cpp` 维持 0 命中。
4. **Gate R-4（夹具零十六进制污染）**：
   - `test_streams.h` 已被 `git rm`，全仓无任何内联媒体二进制十六进制数组。
5. **Gate R-5（自定义类型 100% 审计）**：
   - `cmake -P cmake/AuditTypes.cmake` 执行退出码为 0，无任何未声明类型或违规包装壳。
6. **Gate R-6（测试、包络与符号留存）**：
   - `rtx_tests.exe` 128/128 项单测全部通过（含更新后的调度与真实 MKV 测试，0 SKIP-GPU）；
   - `cmake -P cmake/AuditSpdx.cmake` PASS，`cmake -P cmake/AuditImports.cmake` PASS；
   - `CineRTX.exe.map` 在 `/OPT:REF /OPT:ICF` 下确认保留非内联播放与视口核心修饰符号。
7. **Gate R-7（全预设跨工具链验证）**：
   - 本机 MSVC 编译 0 错误 0 警告；
   - Zig 0.13 / Clang libc++ 在 `-fexperimental-library` 下顺利编译，无 `std::jthread` 链接阻断。
8. **Gate R-8（物理桌面实呈补课闭环）**：
   - 协调负责人在宿主机驱动 621.24 WHQL 的 WTS Session ID 1 活跃交互式桌面上运行 `CineRTX.exe`；
   - 确证 H.264/HEVC/AV1 播放与转码监看 `presented > 0` 且 `D2H == 0 B`；
   - 快速连续拉伸视口 100 次交换链无崩溃泄漏；
   - 音频热拔插触发 `AUDCLNT_E_DEVICE_INVALIDATED` 后无锁自动续播。

---

## 五、 交付物清单与完工回执契约

施工完成后，外部执行工程师必须提交以下受控工件供协调负责人审查验收：

1. **工作区代码提交物**：
   - 清理重构后的全套源码（严格保持未提交状态，等待统一验收审查）；
   - 新增的 `cmake/AuditTypes.cmake` 门禁脚本；
   - 新增的 3 份微型物理测试样本 `tests/fixtures/*.mkv`。
2. **施工完工验收记录文档**：
   - 实体落盘归档至 [`docs/phase11/26_CineRTX Phase 11.2-R 施工验收记录.md`](26_CineRTX%20Phase%2011.2-R%20施工验收记录.md)；
   - 必须逐项记录 Gate R-1 至 Gate R-7 的自测命令输出日志与代码行数增减精确账本。
3. **禁止事项**：
   - 严禁执行 `git commit`；
   - 严禁引入任何未带 `@rtx-type-justification` 的新类；
   - 严禁破坏已有的 128 项单测通过基线。

本施工单自下发之日起即刻生效，验收达标前坚决禁止向 Phase 11.3 推进！
