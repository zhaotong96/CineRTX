# CineRTX Phase 11（v0.3.0）架构深度评审报告

**评审对象**：CineRTX 仓库 `zhaotong96/CineRTX`；`00_CineRTX 演进方向调研.md`、`01_CineRTX_Phase11_演进方向调研_专家修订终稿.md`、`02_CineRTX Phase 11 架构冲突深度评判.md`  
**源码基线**：GitHub `main`，Phase 10 / `v0.2.0`，Commit `52103a7363c9b431eb4ab8ea8da86007ebe2dd49`（2026-11-18）  
**评审日期**：2026-11-19  
**评审性质**：文档审阅 + GitHub 当前源码静态审查 + 官方编译器 / Windows / NVIDIA 技术资料核验。未在本环境重新编译、运行 78 项 GPU 测试或进行目标 GPU 性能测量。

---

## 0. 执行摘要与最终裁决

CineRTX 已经从一个 GPU 视频重制原型，成长为具备 NVDEC/NVENC、RTX Video / NGX、CUDA-D3D12 互操作、NVOFA、TensorRT RIFE、FFmpeg 静态集成和 Win32/CLI/WebUI 多端入口的 C++ 工程。Phase 10 的主要价值是把离线重制链路收敛为一个相对完整的生产路径。Phase 11 的方向——实时播放、真正的时间线模型、可复用的渲染图和多轨音频——是合理的，但这不是简单地在现有 GUI 上增加剪辑按钮，而是要从“离线转码程序”迈向“媒体运行时与编辑引擎”。

本次评审最重要的判断如下：

| 议题 | 最终裁决 | 关键理由 |
|---|---|---|
| RAII | **强制推进，且应提升为架构硬门禁** | 当前已使用工厂创建和 `unique_ptr`，但 CUDA 指针、COM 接口、Win32 HANDLE、RIFE 会话及 GPU 异步资源生命周期仍有手工清理和超时后释放风险 |
| C++26 静态反射 | **强推实验轨道；不把 MSVC 生产构建链作为先决条件** | 截至评审日，微软公开的 MSVC 14.53 Preview 更新及语言一致性资料没有列出 P2996/P3394 支持；GCC 16 已提供 P2996/P3394 实验实现。可以用 GCC 16 做隔离式反射代码生成，让现有 MSVC 构建消费生成产物 |
| DirectComposition | **方向认可，现有技术方案必须修订** | `D3D12_COMMAND_QUEUE_PRIORITY_LOW` 不存在；composition swap chain 有自身的 FLIP/Scaling 约束；复用 D3D12 设备也不等于自动实现 0 GPU copy |
| Flicks | **保留原生 705,600,000 Hz，修正现有迁移与测试** | 原生时基有广泛整除优势；但 1 Flick 是约 1.417 ns，不是亚纳秒；GUI 时间线仍以毫秒为主，现有 24 小时测试没有真正覆盖非整除余数场景 |
| RealtimePlayer | **作为独立核心子系统建设** | 当前仓库没有真正的 WASAPI 音频主时钟播放内核；现有 PreviewViewport 是 960×540、15fps 级的预览路径，不是播放器 |
| NLE | **先建立可序列化、可回放的最小编辑模型，再扩展多轨** | 当前 `NleTimeline` 仍是 Win32 GUI 头文件中的毫秒数据结构；尚非可独立运行的 Project/Sequence/Track/Clip/RenderGraph 引擎 |
| VSR/FRUC | **保留双拓扑，依据实时显存预算调度** | 00 对 4K RIFE 的显存数字缺少实测；当前 NVOFA/RIFE 已按需互斥，但 `scale=0.5` 尚未贯穿现有桥接 API 和 RIFE 运行时 |
| Video Codec SDK 13.1 | **分能力接入，升级驱动基线必须单独决策** | 当前头文件固定在 n13.0.19.1；13.1.15 对应更高驱动基线。高层级 Hierarchical B-Frames/UHQ 会增加显存、并可能降低编码 FPS，不能描述为无成本升级 |

### 核心建议

**Phase 11 不应被定义为“一次性完成工业级 NLE”。** 更合理的目标是完成媒体运行时的可靠性跃迁：先收口资源生命周期和时钟语义，再交付无 VSR 依赖的实时播放器垂直切片，随后建立最小 NLE 工程模型与统一 RenderGraph，最后再叠加更昂贵的 AI 算子、自适应显存调度和高级编码配置。

对于您的两项明确诉求，我的裁决不是折中式“可以考虑”：

- **RAII：强制要求。** 新代码不得新增裸拥有资源或依赖调用者记住 `shutdown()` 的对象协议；GPU fence 完成与资源退役也必须被纳入 RAII 所有权模型。
- **C++26 静态反射：强制启动。** Phase 11 应有一个正式的 Reflection Pilot/Codegen Track，并以真实生成产物驱动现有参数契约，而不是只写一份“未来支持后再迁移”的占位说明。只是生产版主构建仍由 MSVC 承担，直至 MSVC 有可验证、可复现、可支持的 P2996 实现。

---

## 1. 评审基线：仓库当前到底是什么状态

### 1.1 版本和构建基线

GitHub `main` 的最新提交为 `52103a7`，提交说明为 Phase 10 全栈架构收口，标记 `v0.2.0`。提交说明记载了 78/78 GPU 单测通过和 4K AV1 HDR10 NVOFA 样例验证；这是仓库提交记录中的验收声明，本次评审没有重新执行这些测试。

但当前 `CMakeLists.txt` 仍为：

```cmake
project(CineRTX
    VERSION 0.1.0
    ...)
```

同时目标将 `${PROJECT_VERSION}` 注入 `RTX_VERSION_STRING`。因此，版本元数据仍然存在真实漂移，不能只更新 README 而不修 CMake。当前构建设置使用 C++23 编译特性，MSVC 额外使用 `/std:c++latest`；`CMakePresets.json` 同时保留 Windows MSVC 和 Zig 工具链路径，`toolchain-zig.cmake` 固定 Zig 0.13.0 回退包。

这一点直接影响静态反射决策：项目不是一个可以随意替换编译器的纯标准库命令行小程序。Windows SDK、NVIDIA RTX Video / NGX 静态存根、NVENC 头文件、静态 CRT、PE 导入门禁以及当前 MSVC / Zig 构建组合，都是现实约束。

### 1.2 多端参数契约已收口，但尚非完全自动生成

`include/rtx/param_schema.h` 当前拥有：

- `TranscodeProfile`：20 个参数成员；
- `kParamSchema`：20 项 `ParamMeta` constexpr 元数据表；
- 默认值和参数枚举约束；
- `parseProfileFromJson()`、`profileToJson()`、`exportSchemaJson()` 接口。

默认 QP 已统一为 22，Rate Control 默认 `cq`，Pipeline Order 默认 `vsr-first`。这说明 00 稿所描述的 `QP 0/24/26`、WebUI 正则刮削、旧版 19 项参数等问题，不能再作为 Phase 11 的主要施工目标。

但“20 项参数进入一份元数据表”不等于“C++ 内部已经做到零重复”。当前 JSON 解析、序列化及 CLI 绑定仍有手写字段读取和映射。01 稿对现状的“完美 SSOT”评价偏乐观，02 稿对此的补充判断更准确。

### 1.3 i18n 数量已能从源码确定

当前 `include/rtx/i18n.h` 的 `StrId::Count` 编译期常量为 125，且有 `static_assert(kStrIdCount == 125)`。但该断言的错误消息还写着“113 strongly-typed entries”。因此：01/02 对 125 与 129 的谨慎处理是正确的；在本次实际源码核验中，当前头文件已经给出了 125 的事实值，真正遗留的是断言文本和文档同步。

### 1.4 当前播放器、预览和 NLE 的能力边界

当前 `PreviewViewport` 使用独立 D3D12 Device、子 HWND、960×540 `R10G10B10A2` 双缓冲 FLIP_DISCARD 交换链，并把主机侧预览数据复制到 D3D12 upload heap。它是过渡型预览组件，不是完整的零主机回读实时播放器。

当前 `workstation_canvas.h` 的 `NleClip` / `NleTimeline` 仍然位于 Win32 GUI 层，字段主要是 `start_ms`、`duration_ms`、`playhead_ms`、`in_ms`、`out_ms`。时间线操作通过 GUI 消息将鼠标位置转换成毫秒 seek。它可以展示轨道和剪辑块，但尚不能等同于一个独立的 NLE 项目模型或可编译的多轨渲染图。

本次 GitHub 搜索未发现真正的 `RealtimePlayer` 内核，也未在当前播放实现中发现 `IAudioClient` / `IAudioClock` 的实际使用。WASAPI 主时钟目前仍属于设计规划，而不是已交付能力。

**基线结论：** 01/02 对 `v0.2.0 → v0.3.0` 的版本基线判断成立；但应把“现有 UI 已有的时间线/预览外壳”和“真正的 Player/NLE 引擎”严格区分。

---

## 2. 对 00、01、02 三份文档的评价

### 2.1 总体评价

| 文档 | 优点 | 主要问题 | 评审定位 |
|---|---|---|---|
| **00 原始调研稿** | 战略视野较大，敢于从播放器、时间线、音频和异构 GPU 算子整体思考；提出静态反射、RAII、Flicks、DirectComposition、VSR/FRUC、SDK 13.1 等关键方向 | 基线停留在 v0.1.0；部分设计建立在虚构或错误的现状上；数学、API 和显存论断有硬伤；性能数字没有测试口径 | **保留战略意图，不作为施工依据** |
| **01 专家修订终稿** | 将基线对齐 v0.2.0；纠正 Flicks 降采样、PreviewViewport 带宽、VMM 别名和 AppTransZeroCopy 适用边界；补齐 WASAPI 播放调度内核 | 对 P2996 采取了过度保守的终止式裁决；把 `kParamSchema` 的现状评价得过高；对低优先级 D3D12 队列的表述有不可实现之处；RIFE `scale=0.5` 和 SDK 13.1 成本尚需实证 | **主线架构基线，但必须经过本报告的二次修订** |
| **02 冲突深度评判** | 三份文档中最有工程价值；以 R-01~R-08 组织争议，指出源码落点和现存债务；对 Flicks、PreviewViewport、RIFE 输入尺度和驱动基线提出了进一步检查项 | 对 MSVC 反射现状的表达过于绝对；仍倾向把 FieldDescriptor 当作最终解决方案；对 RAII 的讨论仍偏重“删手动 reset”，低估 GPU fence 退役问题；把高层级 B-Frame 的性能影响描述得过于乐观 | **最好的冲突审查稿，但仍不是最终施工裁决** |

### 2.2 对 00：战略方向值得保留，论证需要重做

00 的价值在于它没有把 Phase 11 限制为一次常规重构，而是意识到 CineRTX 的下一阶段必须处理三个相互关联的问题：

1. 多端契约和工程底座如何避免继续分叉；
2. 用户能否真正播放、监看和精确定位媒体；
3. 引擎能否从单路转码进入多轨时间线和音频混合。

这三条方向应当保留。但以下主张不应进入施工文档：

- `CpuReferenceBackend`、旧双轨转码、正则刮削等历史清理项已经不能继续当作 Phase 11 的现状问题。
- `D3D12_COMMAND_QUEUE_PRIORITY_LOW` 不是 D3D12 的合法队列优先级枚举。
- 4K RGBA16F、60fps 的完整主机回读带宽应为 `3840 × 2160 × 8 × 60 = 3,981,312,000 B/s`，约 3.71 GiB/s / 3.98 GB/s，而不是 1.5 GB/s。更重要的是，这不是当前 PreviewViewport 的数据路径。
- `14,112,000 Hz` 不是原生 Flicks 的等价替代；它无法整除 `30000/1001 fps`、`60000/1001 fps` 和 192 kHz 音频样本周期。
- `cuMemCreate/cuMemMap` 不能把一个由 NVIDIA 专有驱动黑盒自行分配、没有向上暴露分配器所有权的 NvOFFRUC 工作区，直接与 TensorRT 分配器内存做安全的物理页别名。
- `AppTransZeroCopy` 不能直接套在 1080p→4K NGX VSR 改变了像素格式和表面几何的重制链路上。
- “干扰率 <0.5%”“不增加 VRAM”“绝不撕裂”“128.7 FPS”等表述必须给出硬件、驱动、素材、重复次数、统计窗口和测量方法；没有数据时只能写成目标，不能写成事实。

### 2.3 对 01：方向正确，但需要把“可行”和“值得做”分开

01 最重要的贡献，是把 Phase 11 拉回真实的 `v0.2.0` 代码基线，并将大部分错误的早期方案从施工蓝图中剔除。它对 Flicks、DirectComposition 方向、WASAPI 主时钟、SDK 13.1 帧精确 Seek 的战略价值判断总体正确。

但它对静态反射的裁决有一个逻辑跳跃：

> “MSVC 暂无公开 P2996 支持”并不能推导出“CineRTX 不应该在 Phase 11 开始使用 P2996”。

真正应当推导出的是：不能要求 MSVC 生产构建立即解析 P2996 语法；但可以用有实现的编译器做隔离式代码生成，然后让 MSVC 消费生成代码。01 将这两个问题合并成了“反射现在不应进入主线”的单一结论。

此外，01 对 D3D12 queue priority 的描述需要实质修正；对 RIFE `scale=0.5` 的“75% 激活显存节省”应从承诺改成待测假设；对 SDK 13.1 的 Hierarchical B-Frames/UHQ 应从“无明显性能代价”改成有明确资源预算的高级编码档。

### 2.4 对 02：最值得作为评审过程模板，但还要补齐底层资源模型

02 的 R-01~R-08 组织方式是有效的：先区分事实冲突，再给证据，最后明确裁决。它尤其正确地强调了：

- 不要重复解决 Phase 10 已经完成的问题；
- 不要将 14.112 MHz 当作 Flicks 的替代时钟；
- 当前播放器缺少真正的音频主时钟和解码调度；
- RIFE 的尺寸契约不能在 `vsr-first` 分支中继续默认为源分辨率；
- Video Codec SDK 13.1 需要同步核对头文件和驱动基线。

但其 RAII 检查仍不够深入。`PreviewViewport::release()` 在等待 fence 时忽略了 `WaitForSingleObject` 是否超时，之后继续释放资源；这是 GPU 异步生命周期问题，不只是裸 COM 指针风格问题。另一个遗漏是：当前 RIFE 运行时公开的初始化入口仍然是 `initRife(engine_path, width, height)`，当前运行时路径按 64 对齐 TensorRT shape；仓库的 Phase 8 参考材料虽然提到了 `scale=0.5` 和 128 对齐，但这一模式尚未作为完整的运行时 ABI/Engine Profile 契约接入。因此，不能把它当成已有能力。

**文档裁决：** 以 01 的修订方向为主、采用 02 的冲突审计方法，但将本报告中的 RAII、反射、D3D12、RIFE 和 SDK 修订补入后，才可转成 Phase 11 的施工上位文件。

---

## 3. RAII：从“智能指针风格”升级为异构资源所有权体系

### 3.1 必须先统一 RAII 的定义

RAII 不是“析构函数里面不能出现 `Release()` 或 `close()`”，也不是“所有类都必须只有一个 `create()` 工厂”。RAII 的核心是：**资源获取与一个明确的拥有者对象绑定；拥有者生命周期结束时，资源确定地、恰好一次地释放；依赖关系由类型和成员结构表达，而不是靠调用者记住释放顺序。**

对 CineRTX 来说，RAII 至少要覆盖五类资源：

1. **CPU/标准库资源**：文件、线程、mutex、AVPacket、AVFormatContext、AVIOContext、缓冲区。
2. **Win32 资源**：`HMODULE`、`HANDLE`、`HWND`、event、mapped view。
3. **COM/D3D12 资源**：`ID3D12Device`、queue、allocator、command list、resource、fence、swap chain、DirectComposition Visual/Target。
4. **CUDA 资源**：`CUdeviceptr`、pinned host allocation、stream、event、external memory、external semaphore、CUarray、surface/texture object。
5. **桥接和模型会话资源**：NGX bridge、RIFE TensorRT engine/context、NVDEC/NVENC session、外部 DLL 函数表。

这五类资源不能共用一个“万能 SmartPtr”。它们的销毁 API、Context 依赖、线程约束和异步完成条件不同。应提供少量、按资源语义区分的 move-only owner 类型。

### 3.2 当前源码的 RAII 现状与缺口

#### A. `NvidiaTranscodeSession`：有工厂 RAII，但析构顺序仍由手写代码维持

当前已经有：

- private 构造函数；
- `static Result<std::unique_ptr<NvidiaTranscodeSession>> create(...)`；
- `DecoderHandle`、`EncoderHandle`、`BridgeHandle` 等 `unique_ptr + custom deleter`。

这已经是正确方向。问题是析构函数仍然手工调用：

```cpp
muxer_.reset();
cpu_encoder_.reset();
encoder_.reset();
decoder_.reset();
bridge_.reset();
demuxer_.reset();
shim_.close();
bridge_loader_.close();
```

这说明资源销毁拓扑还没有真正编码在成员声明顺序中。当前成员顺序里 `encoder_` 和 `bridge_` 的声明关系，不能自然保证 NVENC session 在 bridge 的 P010/NGX 资源前销毁。

建议将资源拥有者按“依赖先声明、依赖者后声明”的方式重排。示意顺序：

```cpp
NgxRemasterBridge bridge_loader_;
NvidiaShim shim_;
std::unique_ptr<IDemuxer> demuxer_;
BridgeHandle bridge_;
DecoderHandle decoder_;
EncoderHandle encoder_;
std::unique_ptr<CpuVideoEncoder> cpu_encoder_;
std::unique_ptr<IMuxer> muxer_;
```

C++ 对成员按声明逆序析构，因此顺序将变为 `muxer → cpu_encoder → encoder → decoder → bridge → demuxer → shim → bridge_loader`。这只是基于当前 deleter 关系的候选顺序；落地时必须再核验 bridge、shim、decoder、encoder 的真实依赖图，并将该关系写成架构注释和测试。

目标不是“把八行代码删掉”这么简单，而是做到：新增成员时，只要把它放在正确依赖位置，资源拓扑就仍然成立；构造中途失败时，已经创建的成员也能自动安全回滚。

#### B. `RemasterPipeline`：GPU 指针、Pinned Host Buffer 和 RIFE 仍由父类析构手工回收

当前类直接保存：

- `uint64_t anchor_p010_[2]`；
- `void* pinned_host_buf_[2]`；
- `bool rife_active_`；
- `uint64_t fruc_out_buffer_`；
- `CudaDriverApi cuda_`；
- `std::thread encode_thread_`。

析构函数手工 join 编码线程、调用 `destroyRife()`、循环执行 `mem_free`、`mem_free_host`，并在某些情况下退回 `std::free()`。

这些都应转为独立拥有者：

- `CudaDeviceBuffer`：保存 device pointer、大小、allocator/API owner、CUDA Context 归属；
- `CudaPinnedHostBuffer`：严格记录由哪一种 CUDA API 分配，必须用匹配 API 释放，不应在 `cuMemFreeHost` 不可用时默认用 `std::free()` 释放 CUDA pinned allocation；
- `RifeSession`：独立拥有 `initRife/destroyRife` 生命周期，并保证它先于 bridge 销毁；
- `PipelineWorker`：拥有线程、退出信号和 join；析构时先通知退出、唤醒等待队列、join，再允许 GPU 缓冲区析构。

还要处理两个隐藏依赖：

1. CUDA 释放通常要求正确的 Context 处于 current 状态。Deleter 应保存可重绑的 Context/API 所有者，而不是假定析构一定发生在创建资源的线程。
2. `CudaDriverApi` 的动态模块和函数指针必须有明确寿命。不能让 RAII deleter 依赖一个已经卸载的 driver module/function table。

成员次序应保证 `session_`（拥有 bridge/Context）和 `cuda_`（拥有 driver API）先于依赖它们的 GPU buffer 声明，利用逆序析构使 buffer/RIFE/worker 先销毁。

#### C. `PreviewViewport`：类有析构，但字段类型没有所有权语义

当前 viewport 已有析构函数和 move 语义，但成员是大量 `void*`、`HANDLE`、`HWND`，依赖局部 fail lambda 和手写 `safeRelease()` 清理。

建议在 `src/gui/` 内部提供最小的 move-only wrappers：

| 类型 | 负责资源 | 释放动作 |
|---|---|---|
| `ComOwner<T>` / `ComPtr<T>` | D3D12/DXGI/DComp 接口 | `Release()` |
| `UniqueHandle` | fence event、Win32 HANDLE | `CloseHandle()` |
| `UniqueModule` | 动态 DLL | `FreeLibrary()` |
| `UniqueHwnd` | 确认由 viewport 创建并独占的 child HWND | `DestroyWindow()` |
| `MappedResource` | 持久 Map 的 D3D12 resource | `Unmap()`，并保证 resource 活到 Unmap 完成 |

这里可以使用 WRL `ComPtr`，也可以实现一个很薄的内部 `ComOwner<T>`。如果项目目标是零额外 DLL、零重型框架，轻量内部模板足够。

#### D. 最严重的生命周期问题：GPU fence 未完成时不能释放资源

当前 `PreviewViewport::release()` 大致流程是：等待 fence，等待最多 1 秒，然后无论等待是否成功，继续 Unmap、Release backbuffer、swap chain、upload buffer、queue 等资源。

`resizeSwapChain()` 也存在类似逻辑：等待 100ms 后忽略等待结果，继续释放 backbuffer 并 ResizeBuffers。

这不是“代码不够优雅”，而是 GPU 可能仍在使用资源时 CPU 已经释放该资源。RAII 只能保证 CPU 对象的生命周期；它不会自动知道 GPU command list 何时执行完毕。

Phase 11 必须建立 GPU Resource Retirement 规则：

- 每次向 queue 提交引用资源的命令，都记录最后一个 fence value；
- 对象析构或 resize 时，先等待所有引用这些资源的提交完成；
- 若设备已移除，走明确的 device-lost 路径；
- 若只是等待超时，不能把“超时”当成“已完成”。要么保留资源并延迟退役，要么执行设备失效判定和受控回收；
- swapchain ResizeBuffers 前必须确保相关 backbuffer 引用和 GPU 使用都已结束。

这是 RAII 从普通 C++ 资源管理升级到异构 GPU 资源管理的分界线。

### 3.3 RAII 的 Phase 11 硬门禁

建议新增全局规则：

> 所有新资源必须有唯一、明确、move-only 的拥有者；所有非拥有借用必须显式标识；释放顺序由成员声明或组合对象表达；任何异步 GPU 资源必须在最后一次 GPU 使用完成后才能退役；不得要求调用方记住配对调用 `initialize/shutdown`、`open/close`、`create/destroy`。

建议 Gate：

1. 每一个资源创建步骤都注入失败，验证此前已创建的资源无泄漏；
2. 对 `NvidiaTranscodeSession`、`RemasterPipeline`、`PreviewViewport` 连续创建/销毁至少 100 次；
3. GPU 队列有未完成工作时触发 resize/close/cancel，验证资源不提前释放；
4. 模拟 device removed、CUDA API 缺失、RIFE 初始化失败、NVENC 初始化失败；
5. 用调试计数器或测试 deleter 证明每个拥有资源恰好释放一次；
6. 编码线程退出必须先于依赖资源析构，不允许析构后后台线程继续访问成员。

**最终 RAII 裁决：** 彻底支持强 RAII；但 Phase 11 的 RAII 任务应命名为“异构资源所有权与 GPU 退役机制收口”，而不是“删除手动析构代码”。

---

## 4. C++26 静态反射：支持先行，但必须选择正确的先行方式

### 4.1 现实编译器状态

截至 2026-11-19：

- Microsoft 2026 年 11 月 MSVC Build Tools Preview 更新记录（14.53）没有列出 P2996/P3394 静态反射支持；微软公开语言一致性资料也没有为 MSVC 公布 P2996/P3394 的可用实现。因此不能把 `/std:c++latest` 理解为“MSVC 已经实现了所有 C++26 特性”。
- GCC 16 已提供 P2996R13 Reflection 和 P3394R4 Annotations 的实现路径，使用 `-std=c++26 -freflection` 等实验开关。
- Bloomberg 的 `clang-p2996` 属于实验性分支，适合验证提案和探索，不适合直接作为生产发布编译器。

这意味着 02 的事实方向正确：目前不能假定 MSVC 主生产构建可以直接编译 `^^`、`std::meta::info` 和 `template for`。但 01/02 的方案不应止步于“用 FieldDescriptor 过渡，等 MSVC 未来支持”。

### 4.2 我的裁决：Phase 11 必须真正引入 P2996/P3394

建议把“采用静态反射”分成两个独立问题：

1. **项目是否现在就开始使用静态反射？** 我的答案是肯定的，且应写进 Phase 11 的硬目标。
2. **MSVC 是否必须立即承担 P2996 生产编译？** 当前答案是否定的，因为没有公开可验证的支持实现。

把两者解耦，CineRTX 可以先行使用新语言能力，同时不牺牲现有 Windows 发布链。

### 4.3 推荐架构：GCC 16 Reflection Codegen + MSVC Production Consumer

建议建立独立子工程，例如：

```text
schema/
  schema_model.hpp          # 不包含 Windows / CUDA / FFmpeg / NGX 头文件
  reflection_codegen.cpp    # GCC 16 + P2996/P3394
  generated/
    param_schema.generated.hpp
    param_schema.generated.inc
    schema.json
```

设计流程：

1. 将 `TranscodeProfile` 的字段定义收敛到一个纯 C++ 数据契约头文件；该头文件不依赖 Windows SDK、CUDA、TensorRT 或项目运行时实现。
2. 使用 P2996 反射枚举 `TranscodeProfile` 成员，使用 P3394 annotation 描述 CLI flag、JSON key、类型、范围、枚举取值、i18n key、可见性和校验策略。
3. 用 GCC 16 编译一个独立的反射代码生成器，生成 C++ 描述符/序列化适配代码以及 JSON Schema。
4. 现有 MSVC 主工程只包含生成的标准 C++20/23 产物，不解析反射语法。
5. CI 比较生成物是否与仓库中提交的生成文件一致，避免不同机器生成不同契约；也可在 CI 中实时生成后进行 diff 检查。
6. 当 MSVC 将来真正实现 P2996/P3394，再将 codegen 任务迁入 MSVC，移除独立 GCC 生成依赖。

这样做不是规避静态反射，而是把反射放在最适合它的边界：**编译期元数据生成器**。运行时业务二进制不需要在反射 API 尚不稳定时承担编译风险。

### 4.4 反射应该生成什么，不能指望它自动解决什么

反射最适合消除的是“字段结构重复”：

- `TranscodeProfile` 字段枚举；
- JSON key 与成员映射；
- CLI flag 与成员映射；
- schema/defaults 导出；
- 类型级别的 parse/serialize 路由；
- 成员数、字段名唯一性、annotation 完整性的编译期检查。

但是，静态反射并不会凭空理解全部业务语义。以下内容仍应显式定义：

- `resolution=native` 与宽高 override 的组合规则；
- `codec=av1_cpu` 与 rate-control 的兼容关系；
- RIFE engine 路径在不同 fps mode 下的约束；
- `vsr_enabled=false` 时 VSR quality 参数的处理；
- 输出路径、HDR 元数据、GPU 可用性等运行时校验。

也不要把“读取默认值”写成反射一定能从任意复杂成员初始化器中自动抽取语义。更稳健的方法是通过一个规范默认对象实例化后，生成器读取其字段值；字段约束则使用显式 annotation 或 validator 元数据。

另一个重要边界：反射不会替代 JSON parser。当前零依赖 JSON 解析代码仍需要单独审查字符串转义、嵌套对象、重复 key、数值范围和错误诊断。代码生成可以消灭字段重复，但不能把不完整的 JSON 解析器变成完整解析器。

### 4.5 是否同时尝试 full experimental Clang/MSVC-compatible build

可以建立一个非阻断研究任务，测试 clang-p2996 或未来 clang-cl 的完整工程能力，但不得作为 Phase 11 的主生产链 Gate。必须单独验证：

- MSVC ABI / CRT `/MT` 兼容；
- Windows SDK 和资源编译；
- NVIDIA `nvsdk_ngx_s.lib`、TensorRT、NVENC 相关库的链接；
- Win32 GUI 入口、PE import audit、静态 CRT 和二进制尺寸门禁；
- Visual Studio / CMakePresets / Ninja 集成。

不要假设 GCC 16 或实验性 Clang 可以直接链接所有当前 MSVC 工具链产物。

**最终静态反射裁决：** Phase 11 应正式交付一个“真实使用 P2996/P3394 生成当前 `TranscodeProfile` 契约”的实验性工具链，并让其生成文件进入主生产构建。MSVC 继续负责发布二进制，直至它有真正可用的反射实现。FieldDescriptor 可作为兼容输出形式或临时后端，但不应成为终点。

---

## 5. DirectComposition 与播放器：从预览控件变成媒体播放运行时

### 5.1 当前 PreviewViewport 不是实时播放器

现有 PreviewViewport 的主要职责是给离线重制任务显示低频预览。其现状包括：

- 独立 D3D12 Device；
- 子 HWND + `CreateSwapChainForHwnd`；
- 960×540 `R10G10B10A2` 双缓冲；
- 主机数据写入 upload heap，再复制到 backbuffer；
- 预览刷新受节流，约 15fps 级别。

因此，Phase 11 不是“给现有播放器换个 swap chain”，而是要新建实时播放调度内核，同时升级显示宿主。

### 5.2 DirectComposition 方向成立，但原文有一个硬性 API 错误

00/01 所提出的“D3D12 Device + DirectComposition Visual + 主窗口融合”是合理的。它能解决子窗口区域与主窗口绘制的组合问题，并为 HUD、A/B split、全屏播放器和未来多视口布局提供更干净的合成树。

但文档提出的 `D3D12_COMMAND_QUEUE_PRIORITY_LOW` 并不存在。D3D12 的公开优先级枚举包括 NORMAL、HIGH 和 GLOBAL_REALTIME，没有 LOW。不能把它写入施工代码或架构契约。

建议：

- 复用当前 `D3D12Interop` 已创建、按 CUDA LUID 匹配适配器的 headless D3D12 Device，但先为 GUI 暴露明确的 borrowed-device/renderer-host 接口；不要让 GUI 私自创建第二个“看起来相同”的设备。
- 初始使用 NORMAL queue，结合有限的 command list 数量、Present pacing、queue depth 上限和 fence 节流来控制视口工作量。
- 不使用 GLOBAL_REALTIME 作为“低优先级”替代。它语义相反，而且对系统权限和 GPU 调度有要求。
- DirectComposition composition swap chain 使用其 API 要求的 FLIP/Scaling 组合。`CreateSwapChainForComposition` 的配置约束不同于现有 `CreateSwapChainForHwnd` 的 FLIP_DISCARD 路径；不能简单把当前 descriptor 原样复制过去。
- 清晰区分“零主机回读”和“零 GPU copy”。NGX/CUDA 输出纹理到 composition swapchain backbuffer 可能仍需一次 GPU copy 或 shader pass；只要没有 D2H 像素回读，它仍可称为 GPU-resident / zero-host-roundtrip，但不应夸大成物理意义上的完全无复制。

### 5.3 推荐的播放器分层

建议明确分开三个对象：

```text
MediaPlaybackSession
  ├─ DemuxScheduler
  ├─ DecodeAheadQueue
  ├─ AudioOutput (WASAPI)
  ├─ PlaybackClock
  ├─ FrameDeadlineScheduler
  └─ VideoRenderer / Presenter
        ├─ D3D12 device/resource bridge
        ├─ DirectComposition visual tree
        └─ optional VSR / TrueHDR operators
```

离线 Export 与实时 Playback 可以共享解码、GPU 算子和封装器，但不能共享同一个调度策略：

- Export 的目标是最大化吞吐，允许排队和深流水；
- Playback 的目标是按 deadline 呈现，过期帧可能需要丢弃，解码队列必须有界，音频时钟不能等待 VSR 完成。

因此不要把 `RemasterPipeline` 原样改造成播放器。应共享 GPU operator/resource abstraction，而不是共享整个任务调度控制面。

### 5.4 WASAPI 音频主时钟

01/02 的 WASAPI 方向正确。推荐共享模式、事件驱动 `IAudioClient`，通过 `IAudioClock::GetPosition()` 与 `GetFrequency()` 获取音频设备位置/频率，并转换为主时间轴。对每个视频帧，将其 PTS 映射到同一时基，再计算相对音频主时钟的呈现 deadline。

实现时必须处理：

- 音频设备 endpoint 被拔出或切换；
- `IAudioClock` position / QPC correlation；
- 音频缓冲区 underrun；
- Seek 后音频与视频 clock 重新基准化；
- 无音轨内容退化为 QPC / `steady_clock` 主时钟；
- 暂停、变速、逐帧步进和音频静音状态。

音频回调线程必须遵守实时音频原则：不得执行文件 I/O、动态内存分配、长锁等待或同步 GPU fence wait。

### 5.5 GOP-Aware Frame-Accurate Seek

Video Codec SDK 13.1 的 GOP-aware seek 范式非常适合 Player/NLE：

1. demux/index 定位到目标时间戳之前最近的适当 IDR/关键帧；
2. seek demuxer；
3. 对 decoder 执行 flush/discontinuity 处理；
4. 解码参考帧以恢复正确的参考状态；
5. 对非目标中间帧跳过昂贵的 map/post-process；
6. 当目标帧到达时再做映射和必要的 GPU 后处理。

但这应被视作一套可移植到现有 `IDemuxer`/NVDEC 调度器的算法范式，不是一个可以直接调用的“SDK seek 函数”。当前 `hw_transcode.cpp` 已有基于 keyframe 的 scrub preview 起点，可作为实验入口。

### 5.6 Decode-ahead Ring 不应硬编码成 4–6 帧

4–6 帧可作为初始参数，但不应写成固定架构不变量。队列长度应取决于：帧间隔、解码耗时分布、VSR 推理时间、GPU fence 延迟、系统 VRAM budget、播放倍率和输出刷新率。必须是有界队列，并且每个 slot 的资源所有权要和最后一个 fence value 绑定。

**播放器验收建议：** 先实现本地 H.264/HEVC/AV1 素材的原生解码回放、音频主时钟、暂停/Seek/逐帧、设备切换恢复；在此之上再加 VSR/TrueHDR。不要把“4K 60/120fps 零卡顿”作为第一阶段 Gate。

---

## 6. Flicks：保留 705.6 MHz，但从“显示单位”升级为真实时间轴语义

### 6.1 为什么必须保留原生 705,600,000 Hz

`include/rtx/flicks.h` 中已定义：

```cpp
inline constexpr uint64_t kFlicksPerSecond = 705600000ULL;
```

原生时基具有重要的整除性：

| 媒体节拍 | 一个周期对应 Flicks |
|---|---:|
| 24 fps | 29,400,000 |
| 24000/1001 fps | 29,429,400 |
| 25 fps | 28,224,000 |
| 30000/1001 fps | 23,543,520 |
| 60000/1001 fps | 11,771,760 |
| 44.1 kHz audio | 16,000 |
| 48 kHz audio | 14,700 |
| 192 kHz audio | 3,675 |

`14,112,000 Hz` 对 30000/1001、60000/1001 和 192 kHz 不再保持整数周期，因此 00 的降采样提议应当拒绝。

需要纠正一个常见表述：`1 Flick = 1 / 705,600,000 s ≈ 1.41723356 ns`，不是亚纳秒。`int64_t` 的可表示时长约 414 年，这足以覆盖常规媒体工程。

### 6.2 当前代码里“有 Flicks”不等于 NLE 已采用 Flicks 时间轴

当前仓库有三层状态：

1. `flicks.h` 和 `rational.cpp`：已实现原生 Flicks 时基与 128 位换算；
2. `hw_transcode_stages.cpp`：已把部分 FRUC 输出 PTS 运算切换到 Flicks；
3. `workstation_canvas.h/.cpp`：NLE 模型仍使用毫秒字段，`kFlicksPerMs` 只是用于显示换算，坐标映射依然是 `timelineXToMs()` / `timelineMsToX()`。

而且 `formatFlicksTimecode()` 当前固定按 24fps 计算帧号。这不能作为通用 SMPTE timecode，因为项目可能是 23.976、25、29.97 drop-frame、30、50、59.94 或 60 fps。

所以 01 关于“迁移 workstation_canvas.cpp”判断正确，但应该明确：当前 Flicks 更像核心时钟工具，还没有成为 NLE 数据模型的唯一时间轴类型。

### 6.3 需要区分绝对 PTS 转换和增量误差结转

当前 `FlicksDriftCompensator::rescaleToFlicks()` 保存上一轮除法余数，而 `ffmpeg_io.cpp` 将视频/音频 packet 的绝对 `PTS` 顺序送入该接口。视频 PTS 在 B-frame 内容中可能按解码顺序非单调到达。

这至少需要一次严格的语义审查：

- 如果输入是独立的绝对 timestamp，每个 PTS 应按其原始 rational time base 独立换算；
- 如果输入是连续的 delta，则可以把每次除法余数带到下一次增量转换，形成 Bresenham 式误差结转；
- 不能在没有定义清楚的情况下，把“绝对时间戳”和“增量采样间隔”混在一个有状态转换器里。

现有 `flicks_drift_24h` 测试使用的 29.97 time base 与 705.6 MHz 恰好可整除，因此测试过程中的 remainder 没有真正覆盖非零余数场景。建议增加非整除 time base、VFR、B-frame PTS 重排、seek/discontinuity、负时间戳和长时段属性测试。

还必须明确：任意容器 time base 不一定能被 705.6 MHz 整除；对这类来源，Flicks 是高精度内部表示，但仍需保留原始 `PTS + Rational time_base` 作为源时间戳依据，不能声称所有任意 VFR 来源都能无损映射为整数 Flicks。

### 6.4 推荐时间模型

每个媒体对象至少区分：

- `SourceTimestamp`：源 PTS/DTS + 原始 rational time base；
- `TimelinePosition`：项目时间轴上的 Flicks；
- `FrameRate`：项目帧率 Rational；
- `PlaybackRate`：速度比 Rational；
- `AudioSamplePosition`：采样帧索引 + sample rate；
- `TimecodeFormat`：帧率、drop-frame/non-drop-frame、显示格式。

内部时间线以 Flicks 存储，但不要丢失源时间戳和原始 time base。导出时按容器和流 time base 精确 rescale；播放时按音频设备时钟/系统 QPC 映射；两者不能被“1.4ns Flicks tick”混为一谈。

**Flicks 裁决：** 保留 705.6 MHz；迁移所有 NLE `*_ms` 核心字段；修正绝对 PTS 与 delta remainder 的语义；重建 timecode 和误差测试。不要再提出任何 14.112 MHz 降采样方案。

---

## 7. NLE：从 Win32 Timeline Widget 建立真正的编辑引擎

### 7.1 当前 NLE 状态是 UI 原型，不是工程模型

当前 `NleClip` 只包含名称、路径、`start_ms`、`duration_ms`、轨道索引；`NleTimeline` 包含 clips、playhead、in/out、duration 和 selected index。这些数据直接放在 GUI header 中，并和窗口事件、绘制、鼠标坐标、消息传递结合。

这对于原型 GUI 是合理的，但后续不能以此为 NLE 核心继续堆功能。否则项目模型会被 Win32 控件生命周期、当前窗口状态和毫秒近似时间绑死。

### 7.2 推荐的最小领域模型

```text
NleProject
  └─ Sequence
       ├─ VideoTrack[]
       ├─ AudioTrack[]
       ├─ SubtitleTrack[]
       └─ TimelineSettings (Flicks + Rational frame rate)

MediaSource
  ├─ URI / relative path / asset identity
  ├─ stream descriptors
  ├─ source PTS/time_base
  ├─ media duration / keyframe index
  └─ relink/proxy metadata

Clip
  ├─ source reference
  ├─ source_in / source_out
  ├─ timeline_start / timeline_duration (Flicks)
  ├─ playback_speed (Rational)
  ├─ transform / crop / color / effect stack
  └─ audio mapping / gain / pan

Transition
  ├─ interval
  ├─ type (cut / dissolve / wipe / audio crossfade)
  └─ parameters

RenderGraph
  ├─ decode nodes
  ├─ compositing nodes
  ├─ effects (VSR / TrueHDR / FRUC / LUT)
  ├─ audio mixer nodes
  └─ encoder / muxer sink
```

工程模型应当独立于 Win32 GUI，可以被 CLI、GUI 和 WebUI 使用。UI 只负责发出 edit commands，并消费不可变/版本化的项目快照。

### 7.3 预览与导出必须共享同一套编辑语义

不要让预览单独实现一套“看起来差不多”的逻辑，导出又走另一套 RemasterPipeline 业务规则。更可靠的结构是：

- 编辑数据模型编译成 RenderGraph；
- Preview 与 Export 使用相同的图语义；
- Preview 允许选择低分辨率、低质量或跳过部分昂贵节点；
- Export 采用母版质量配置；
- 质量策略只能改变节点精度/分辨率，不得改变 clip 时间映射、转场时序或音画语义。

这既能减少“预览与导出结果不一致”，也能避免 UI 直接调用一大坨离线转码函数。

### 7.4 项目文件和 Undo/Redo

JSON 工程文件需要版本字段、稳定 ID、相对路径/资产引用、项目帧率、时间轴刻度和媒体流描述。不要只保存绝对文件路径和数组索引；素材移动后应支持 relink。

Undo/Redo 建议使用 Command Pattern 或基于项目快照的 transaction log。命令必须可逆并可组合，例如 InsertClip、TrimClip、SplitClip、MoveClip、SetGain、SetEffect、DeleteClip。撤销栈不应包含 GPU 资源句柄，只记录项目状态变更。

### 7.5 Phase 11 最小可验收 NLE 垂直切片

建议先完成：

1. 新建/打开/保存一个版本化项目；
2. 一条视频轨 + 一条音频轨；
3. 素材导入、拖入时间线、移动、Trim、Split、Delete；
4. Flicks 时间轴、正确项目帧率 timecode、帧精确 Seek；
5. 预览播放与暂停；
6. 单段/多段硬切 Export，输出结果能与时间线帧映射一致；
7. Undo/Redo 与保存重开一致。

在这条垂直切片可靠后，再扩展多轨叠加、交叉叠化、多轨混音、WSOLA 和复杂效果图。这样比一开始就承诺“Fairlight 级音频 + 多轨 + 全套转场 + 4K VSR/FRUC + UHQ”更可控。

---

## 8. VSR / FRUC 与显存：保留双拓扑，但把自适应变成实测系统

### 8.1 00 的显存结论证据不足

00 以“4K RIFE 激活张量超过 3.8GB”为理由，直接锁死 `fg-first`。02 正确指出该数字没有足够的独立实测支撑。仅从像素数量推导出“必然 OOM”是不成立的：显存还取决于模型、精度、TensorRT optimization profile、临时张量、workspace、批大小、缓冲帧数、NGX 内部资源和桌面系统占用。

因此建议保留 `vsr-first` 与 `fg-first`，依据输入/输出分辨率、模型、设备预算和实测峰值决定运行策略，而不是写死一种拓扑。

### 8.2 当前已有 NVOFA / RIFE 互斥初始化

当前 `RemasterPipeline::initRemasterAndFgStage()` 按用户选择的路径初始化 RIFE 或 NVOFA，并在析构时销毁 RIFE。这意味着“两个补帧后端全程同时驻留”并不是当前默认现状；Phase 11 应保留这一互斥原则。

但当前 RIFE 初始化和 interpolation buffer 仍使用 `session_->info().width/height` 的源分辨率。若在 `vsr-first` 路径中对 VSR 后的高分辨率锚点进行 RIFE 插帧，必须明确 RIFE 的输入输出尺寸契约，不能仅将 `pipeline_order` 切换而不调整 bridge/engine/buffer 的尺寸。

### 8.3 `scale=0.5` 目前是设计/参考能力，不是已接通的运行时能力

当前桥接 API 暴露的 RIFE 初始化入口是 `initRife(engine_path, width, height)`，当前 TensorRT 引擎 shape 逻辑主要按 64 像素对齐处理；Phase 8 资料提到 `scale=0.5` 需要 128 对齐和反射填充，但当前对外 ABI 没有一个清晰的 scale 参数或 profile 标识。

因此，01/02 不能把“RIFE 原生 scale=0.5 可用”当成现有运行时事实。它应被列为 Phase 11 的一项新增实现：

- 对 scale、输入分辨率、输出分辨率、padding/crop 规则建立 ABI 契约；
- TensorRT optimization profile 必须覆盖真实尺寸；
- 反射填充与裁剪要有画面边缘测试；
- 对高速运动、大位移、硬切镜和低照度素材验证伪影；
- 实测 TensorRT peak allocation 与整条管线 VRAM peak。

半分辨率的像素面积理论上为四分之一，但不能据此直接承诺“总激活显存节省 75%”，因为 workspace、输出张量、其他中间缓存和 NGX 内存不一定按相同比例变化。

### 8.4 正确的显存感知调度器

不要用“8GB 卡 × 88%”这一条固定阈值做唯一依据。建议结合：

- DXGI local video memory budget / current usage；
- CUDA 当前空闲显存；
- TensorRT 实际峰值分配；
- NGX / bridge 资源尺寸；
- decode surfaces、anchor frames、lookahead 和 encode reference buffer；
- 视口/桌面合成占用。

调度器应具有 hysteresis，避免一帧内在 VSR-first/FG-first 之间来回抖动。策略可以按 profile 选择：NVOFA、RIFE full-resolution、RIFE half-resolution、降低输出分辨率、跳过插帧或回退到原帧。

**VSR/FRUC 裁决：** 保留双拓扑；不做 NvOFFRUC 与 TensorRT 黑盒内存别名；补齐 RIFE 尺寸/scale ABI；所有 VRAM 决策必须由 telemetry 和真实设备预算驱动。

---

## 9. Video Codec SDK 13.1：值得接入，但必须显式处理性能和驱动代价

### 9.1 功能优先级判断

NVIDIA Video Codec SDK 13.1 值得 CineRTX 研究的能力包括：

1. GOP-aware frame-accurate seek；
2. AV1 Hierarchical Reference Mode / Hierarchical B-Frames；
3. UHQ tuning 与 iterative encode；
4. NVDEC per-macroblock analytics；
5. AppTransZeroCopy sample。

其中最直接赋能 Player/NLE 的是 GOP-aware seek；最可能提升高质量母版压缩效率的是 AV1 Hierarchical Reference/UHQ；NVDEC analytics 可作为切镜检测的附加信息；AppTransZeroCopy 则要严格限制在表面规格兼容的直通分支。

### 9.2 SDK 13.1 的分层 B 帧不是“免费提高压缩率”

02 对 SDK 13.1 的高层级 B 帧引用了无性能惩罚的表述，但这不能套用到 15/31 帧的 AV1 Hierarchical Reference Mode。NVENC 编程指南对高层级参考结构明确提示：B-frame 深度增加会带来编码 FPS、encoder utilization 和视频内存消耗变化；UHQ 还会启用额外分析/滤波相关资源。

因此：

- 7 B-frames 可以作为需要实测的“高质量档”候选；
- 15/31 B-frames 应是“母版/离线高压缩档”，不是实时默认档；
- UHQ、Lookahead、Temporal Filtering 应与 B-frame 深度一起做 profile matrix；
- 每一种 profile 都要测 encode FPS、GPU encoder utilization、VRAM peak、码率/质量和延迟；
- 不应写“在保持实时吞吐下媲美 SVT-AV1 极慢预设”这种未经实测的承诺。

### 9.3 当前头文件版本与最低驱动基线必须一起决策

当前 `.gitmodules` 将 `third_party/nv-codec-headers` 固定在 `n13.0.19.1`，注释明确将其与 R580 driver 分支对应，并指出更新头文件需要更高驱动基线。NV Codec Headers 的 13.1.15 对应 Video Codec SDK 13.1.15，并要求更新的驱动分支（610.0 或更高）。

这意味着 SDK 13.1 并不是单纯把 submodule tag 换一下。项目必须先决定：

- **策略 A：维持 R580/13.0 基线。** 生产主线继续按 13.0 ABI 发布；13.1 能力作为独立实验/可选构建，等待更高驱动基线。
- **策略 B：提高最低驱动基线到 R610。** 同步升级 nv-codec-headers、驱动兼容门禁、运行时 capability detection、CI 和发布文档。

不要在同一个发布包里把“编译时有 13.1 结构定义”误认为“目标机器驱动和硬件必然支持这些特性”。所有高级编码模式都应通过 `NvEncGetEncodeCaps` 等能力探测进行运行时判定。

### 9.4 AppTransZeroCopy 的适用边界

`AppTransZeroCopy` 展示了应用分配/绑定共享 CUarray 的 NVDEC→NVENC 直通模式。它的价值在于兼容的解码输出 surface 可以直接成为编码器输入 surface，减少中间 buffer copy。

CineRTX 的核心重制流程涉及 VSR 放大、色彩转换、P010 打包，输入输出尺寸/格式发生变化，不能把这一范式当成整个重制链路的直接替代。可以在原分辨率直通或表面规格兼容的特定分支中研究；对 VSR→编码输出，仍需要明确定义最终表面、GPU copy/compute pass 和 NVENC 输入注册。

**SDK 13.1 裁决：** 优先移植 GOP-aware seek；高层级 B-frame/UHQ 作为可选母版 profile；NVDEC analytics 作为附加检测信号；AppTransZeroCopy 只用于符合 surface 约束的直通分支；先决定 R580/13.0 还是 R610/13.1 的产品驱动基线。

---

## 10. Phase 11 施工组织建议：保留四条分轨，但调整依赖关系

01/02 提出的 11A–11D 拆分方向总体可行，但四条轨道不是完全并行的。它们共享几个关键基础契约：RAII 资源模型、GPU 设备/Context、Flicks 时间轴和 NLE RenderGraph。建议按以下依赖关系组织。

### Track 11A — 工程底座、RAII 与版本收口（P0）

工作内容：

- 修复 `CMakeLists.txt` 的真实版本号；同步 `README.md` / `README_zh.md` / release metadata；
- 修正 `i18n.h` 的 stale static_assert message；
- 对 `NvidiaTranscodeSession` 做成员声明顺序和 custom deleter 收口；
- 将 CUDA device/pinned memory、RIFE session、COM、HANDLE、HMODULE 等转成 move-only owners；
- 修复 PreviewViewport fence timeout 后仍释放资源的问题；
- 为 worker stop/join、device lost 和 GPU resource retirement 增加测试。

Gate：失败注入无泄漏；100 次创建/销毁；未完成 GPU fence 时 resize/close 不提前释放；静态资源所有权审查通过。

### Track 11B — P2996/P3394 反射先行实验（P0，与 11A 并行）

工作内容：

- 建立纯头文件 schema model；
- GCC 16 + P2996/P3394 生成参数描述符、CLI/JSON 映射和 schema JSON；
- 生成产物由现有 MSVC C++23 主线消费；
- CI 检查生成物稳定、字段完整、key/flag 唯一性；
- 研究 clang-p2996 / MSVC 后续版本，但不阻断发布。

Gate：当前 20 项参数由反射产物驱动 parse/serialize/schema export；新增测试字段只改一次源定义；MSVC 主工程照常编译。

### Track 11C — Flicks 与 MediaPlayback Runtime（P1）

工作内容：

- 区分 absolute PTS rescale 和 delta remainder carry；
- 扩展时间戳属性测试和 seek discontinuity 测试；
- 将 NLE GUI 时间字段迁移为 Flicks；
- 实现 WASAPI audio-master clock、decode-ahead queue、frame deadline scheduler；
- 实现 GOP-aware frame-accurate seek；
- 第一阶段以原生解码播放为目标，不要求 VSR/RIFE。

Gate：音频主时钟稳定；播放/暂停/Seek/逐帧可靠；有界队列；无 GUI 阻塞；音画偏差在预先定义的测试阈值内。

### Track 11D — DirectComposition / NLE Core / GPU Graph（P1/P2）

这条轨道内部建议再切垂直切片：

1. DirectComposition composition swapchain + 共享 D3D12 Device/queue/fence；
2. Project/Sequence/Track/Clip 版本化模型；
3. 同一 RenderGraph 的 Preview/Export 两种执行策略；
4. 最小一视频轨+一音轨工程编辑、硬切、多段导出；
5. 多轨音频混音、SPSC、AVX2 runtime dispatch、WSOLA；
6. 交叉叠化和更复杂的 effect graph。

Gate：保存/重开项目后时间线语义不变；预览与导出的帧映射一致；GPU 视口无 D2H 像素回读；窗口 resize/device lost 路径不泄漏。

### Track 11E — VSR/FRUC 自适应与 SDK 13.1（P2/P3）

工作内容：

- 对 `vsr-first/fg-first` 建立统一尺寸契约；
- 实现 RIFE scale 参数/engine profile 和 padding/crop；
- 引入 DXGI/CUDA VRAM budget telemetry 和 hysteresis scheduler；
- 选择 13.0/R580 或 13.1/R610 产品基线；
- 实测 Hierarchical B-Frames/UHQ profile matrix。

Gate：真实 GPU 峰值显存、编码 FPS、压缩率/视觉质量数据；无 OOM；功能能力探测准确；低端驱动回退行为可验证。

### 依赖关系

```text
11A RAII / 工程基线 ──────────────┐
                                  ├─> 11C 播放器与时钟 ──> 11D NLE Core / RenderGraph
11B P2996 Codegen ────────────────┘                         │
                                                            └─> 11E 高级 GPU 调度/编码
11A + 共享 D3D12 资源契约 ───────────────> DirectComposition ─┘
```

11B 可以与 11A 并行；DirectComposition 可以做独立技术 spike；真正的 Player/NLE 应在 RAII、时钟和设备资源契约稳定后展开；SDK 13.1 的高级编码则不应该阻塞最小编辑器垂直切片。

---

## 11. 建议列入 Phase 11 的量化验收门禁

| 领域 | 建议验收指标 |
|---|---|
| 构建与版本 | 所有发布产物、CLI `--version`、CMake package、README 统一为 v0.3.0；MSVC 主线持续通过；Zig/CI 回归不倒退 |
| RAII | 失败注入路径资源释放完整；无 GPU 未完成时提前释放；100 次 open/close/resize 测试；所有资源有明确 owner/borrower |
| Reflection | 20 项参数 schema/CLI/JSON 绑定由 GCC16 P2996 生成；字段遗漏、重复 json key/cli flag 编译期失败；MSVC 消费生成产物通过 |
| Flicks | 覆盖 23.976/29.97/59.94、44.1/48/96/192kHz、非整除 time base、VFR、B-frame reordering、seek/discontinuity、负 PTS；验证绝对时间戳和增量换算误差 |
| Playback | WASAPI clock 连续；播放/暂停/seek/逐帧/倍速基本功能；decode queue 有界；设备拔出和恢复可控；无 UI thread 等待 GPU |
| DirectComposition | composition swapchain descriptor 符合 API；同一 adapter/device 资源链路明确；GPU fence 完成后资源退役；证明 0 D2H pixel transfer，而不是口头声称 0 copy |
| NLE | project save/reopen、clip trim/split/move/delete、Undo/Redo、Flicks frame stepping、至少一视频轨+一音轨、多段硬切导出正确 |
| Audio | 回调线程无阻塞/分配/I/O；采样位置精确；多轨混音无削波策略；AVX2 runtime dispatch 或明确 CPU floor；WSOLA 主观/客观质量测试 |
| VRAM | 同时记录 OS/DXGI budget、CUDA usage、TensorRT/NGX 峰值；调度器有 hysteresis；模型和表面尺寸可审计；不使用未经测量的固定“88% 一刀切” |
| SDK 13.1 | driver/codec capability detection；R580/13.0 或 R610/13.1 基线明确；Hierarchical B/UHQ 的 FPS、utilization、VRAM、质量有可复现实验数据 |

性能指标必须给出 GPU 型号、驱动版本、CUDA/TensorRT/RTX Video SDK/Video Codec SDK 版本、输入素材、分辨率、编码器配置、运行时长、warm-up、统计口径和重复次数。没有这些信息的 FPS 或“干扰率”不能作为架构验收数据。

---

## 12. 最终拍板建议

我建议项目负责人对 Phase 11 做如下正式决策：

1. **批准 100% RAII 作为全仓架构硬红线**，并将 GPU fence retirement 纳入 RAII，不仅清理裸指针。
2. **批准 P2996/P3394 先行实验作为 Phase 11 正式交付项**：GCC 16 reflection codegen 立即落地；MSVC 继续做生产 consumer；不等待 MSVC 才开始使用静态反射。
3. **采纳 01/02 的 v0.2.0 基线和 R-01~R-08 大部分纠偏**，但补入本报告对 D3D12 queue priority、Flicks、RIFE scale、SDK 13.1 性能和驱动要求的修订。
4. **将 Player、NLE、DirectComposition 分成可验收的垂直切片**，而不是承诺一次性交付完整工业级 NLE。
5. **维持 13.0/R580 发布基线，直至项目负责人明确批准提高驱动最低版本**；13.1 高级编码先做技术验证，不应成为 v0.3.0 最小产品链的阻塞项。

### 最终结论

CineRTX 的技术路线值得继续大胆推进，但接下来最重要的不是“同时引入更多 NVIDIA API 和更高版本 C++ 特性”，而是建立能让这些复杂能力安全共存的三项底层秩序：

- **资源秩序：** RAII + GPU fence retirement + Context/设备归属；
- **时间秩序：** Flicks + Rational PTS + 音频主时钟；
- **执行秩序：** 同一项目语义下的 Preview/Export RenderGraph，以及吞吐型与实时型调度器分离。

当这三项秩序稳定后，C++26 静态反射、多轨 NLE、RIFE/VSR 自适应和 SDK 13.1 高级编码才有坚实的扩展基础。

---

## 附录 A：本次重点核验的源码位置

以下链接固定在本次审查的 `52103a7` 基线，便于逐项复查：

- [CMakeLists.txt](https://github.com/zhaotong96/CineRTX/blob/52103a7363c9b431eb4ab8ea8da86007ebe2dd49/CMakeLists.txt) — 版本号、C++23、MSVC/Zig 构建选项。
- [CMakePresets.json](https://github.com/zhaotong96/CineRTX/blob/52103a7363c9b431eb4ab8ea8da86007ebe2dd49/CMakePresets.json) — MSVC 与 Zig presets。
- [cmake/toolchain-zig.cmake](https://github.com/zhaotong96/CineRTX/blob/52103a7363c9b431eb4ab8ea8da86007ebe2dd49/cmake/toolchain-zig.cmake) — Zig 0.13.0 fallback。
- [include/rtx/param_schema.h](https://github.com/zhaotong96/CineRTX/blob/52103a7363c9b431eb4ab8ea8da86007ebe2dd49/include/rtx/param_schema.h) — 20 参数结构与元数据表。
- [src/core/param_schema.cpp](https://github.com/zhaotong96/CineRTX/blob/52103a7363c9b431eb4ab8ea8da86007ebe2dd49/src/core/param_schema.cpp) — 手写 JSON 字段解析/序列化。
- [src/core/hw_transcode_internal.h](https://github.com/zhaotong96/CineRTX/blob/52103a7363c9b431eb4ab8ea8da86007ebe2dd49/src/core/hw_transcode_internal.h) — Session / RemasterPipeline 资源成员。
- [src/core/hw_transcode.cpp](https://github.com/zhaotong96/CineRTX/blob/52103a7363c9b431eb4ab8ea8da86007ebe2dd49/src/core/hw_transcode.cpp) — Session 工厂与显式析构。
- [src/core/hw_transcode_stages.cpp](https://github.com/zhaotong96/CineRTX/blob/52103a7363c9b431eb4ab8ea8da86007ebe2dd49/src/core/hw_transcode_stages.cpp) — RIFE/NVOFA 初始化、裸 CUDA 释放、Flicks FRUC。
- [include/rtx/flicks.h](https://github.com/zhaotong96/CineRTX/blob/52103a7363c9b431eb4ab8ea8da86007ebe2dd49/include/rtx/flicks.h) — 705.6 MHz 时间基。
- [src/core/rational.cpp](https://github.com/zhaotong96/CineRTX/blob/52103a7363c9b431eb4ab8ea8da86007ebe2dd49/src/core/rational.cpp) — 128 位有理数/Flicks 转换。
- [src/io/ffmpeg_io.cpp](https://github.com/zhaotong96/CineRTX/blob/52103a7363c9b431eb4ab8ea8da86007ebe2dd49/src/io/ffmpeg_io.cpp) — PTS 映射与 remainder carry。
- [src/gui/preview_viewport.h](https://github.com/zhaotong96/CineRTX/blob/52103a7363c9b431eb4ab8ea8da86007ebe2dd49/src/gui/preview_viewport.h) / [preview_viewport.cpp](https://github.com/zhaotong96/CineRTX/blob/52103a7363c9b431eb4ab8ea8da86007ebe2dd49/src/gui/preview_viewport.cpp) — 当前 D3D12 preview 与 fence release。
- [src/gui/workstation_canvas.h](https://github.com/zhaotong96/CineRTX/blob/52103a7363c9b431eb4ab8ea8da86007ebe2dd49/src/gui/workstation_canvas.h) / [workstation_canvas.cpp](https://github.com/zhaotong96/CineRTX/blob/52103a7363c9b431eb4ab8ea8da86007ebe2dd49/src/gui/workstation_canvas.cpp) — 毫秒时间线与 GUI 模型。
- [src/shim/ngx_bridge/d3d12_interop.h](https://github.com/zhaotong96/CineRTX/blob/52103a7363c9b431eb4ab8ea8da86007ebe2dd49/src/shim/ngx_bridge/d3d12_interop.h) / [d3d12_interop.cpp](https://github.com/zhaotong96/CineRTX/blob/52103a7363c9b431eb4ab8ea8da86007ebe2dd49/src/shim/ngx_bridge/d3d12_interop.cpp) — 当前 headless D3D12 Device、CUDA interop 和 queue。
- [src/shim/ngx_bridge/rife_engine.cpp](https://github.com/zhaotong96/CineRTX/blob/52103a7363c9b431eb4ab8ea8da86007ebe2dd49/src/shim/ngx_bridge/rife_engine.cpp) — 当前 RIFE shape/padding 逻辑。
- [.gitmodules](https://github.com/zhaotong96/CineRTX/blob/52103a7363c9b431eb4ab8ea8da86007ebe2dd49/.gitmodules) — nv-codec-headers 13.0.19.1 pin。
- [include/rtx/i18n.h](https://github.com/zhaotong96/CineRTX/blob/52103a7363c9b431eb4ab8ea8da86007ebe2dd49/include/rtx/i18n.h) — 当前 `StrId::Count=125` 和 stale assert message。

## 附录 B：核验的外部一手资料

- Microsoft： [MSVC Build Tools Preview updates — November 2026](https://devblogs.microsoft.com/cppblog/msvc-build-tools-preview-updates-november-2026/)
- Microsoft： [C++ language conformance status](https://learn.microsoft.com/en-us/cpp/overview/visual-cpp-language-conformance?view=msvc-170)
- GCC： [GCC 16 changes](https://gcc.gnu.org/gcc-16/changes.html)
- Bloomberg： [clang-p2996 experimental implementation](https://github.com/bloomberg/clang-p2996)
- Microsoft： [D3D12_COMMAND_QUEUE_PRIORITY](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_command_queue_priority)
- Microsoft： [IDXGIFactory2::CreateSwapChainForComposition](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgifactory2-createswapchainforcomposition)
- Microsoft： [IAudioClock::GetPosition](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclock-getposition)
- NVIDIA： [Video Codec SDK 13.1 — Zero-Copy Transcode, AV1 B-Frames and Frame-Accurate Seek](https://developer.nvidia.com/blog/nvidia-video-codec-sdk-13-1-zero-copy-transcode-av1-b-frames-and-frame-accurate-seek/)
- NVIDIA： [NVENC Video Encoder API Programming Guide](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.1/nvenc-video-encoder-api-prog-guide/index.html)
- NVIDIA/FFmpeg： [nv-codec-headers README](https://github.com/FFmpeg/nv-codec-headers/blob/master/README)
- NVIDIA： [Introducing Low-Level GPU Virtual Memory Management](https://developer.nvidia.com/blog/introducing-low-level-gpu-virtual-memory-management/)
