# **CineRTX Phase 11 演进方案（16–20 号文档）深度穿透评审与全栈实施总解**

对照 16 号演进母本及当前主干基线（Commit 52103a7 叠加 Phase 11.1 资产，体积约 4.87 MB、109/109 项单测全绿）1进行穿透核验，项目负责人提出的 17–20 号计划文档在工程治理上准确落盘了三项关键机制：协调负责人与外部执行工程师的正交职责切分、Phase 11.2 至 11.4 串行垂直切片合龙，以及由 Linker .map 修饰符号留存、负向 rg 扫描与 WTS Session \> 0 交互式桌面实呈构成的三层验收闭环1。上述机制能有效防范 Phase 11 首轮出现的“单测孤岛堆砌”与主干集成空心化问题1。  
然而，将 17–20 号施工单逐条映射至仓库现行源码与 CMakeLists.txt 后，可定位出 **7 处会直接引发编译阻断、文件越界冲突或核心门禁失效的隐性断层**：主要包括 17 号物理隔离矩阵遗漏跨层接线文件且引用了主干构建表外的文件名 nvidia\_transcode\_loader.cpp1、bridge\_exports.cpp 的 preview\_ring 仍为纯 CUDA 设备指针而非可被视口借用的 D3D12 共享缓冲7、现行 preview\_viewport.cpp 仍硬编码 960×540 与 100.0f 常数8、workstation\_canvas.cpp 仍残留已从 Schema 剔除的软件编码下拉项4、hw\_transcode\_stages.cpp 的 NVENC 分支漏传 keyframe 标志且闲置了 anchor\_p010\_10、CMakeLists.txt 仅直接嵌入 rife\_pack.ptx 而不编译 .cu 文件6，以及 nv\_encoder.cpp 的单缓冲 IPP 状态机与 n13.0.19.1 头文件无法直接支撑 SDK 13.1 AV1 分层 B 帧与 UHQ 实测6。若不先修补上述契约断层而直接派发施工单，外包交付物在合龙阶段仍将频繁返工。下文给出逐项修正裁决与分波次全栈技术实施方案。

## **一、 17–20 号计划与 16 号母本及现行源码的穿透对账**

本次审计以仓库现行构建脚本和核心源码（CMakeLists.txt、preview\_viewport.h/.cpp、d3d12\_raii.h、d3d12\_interop.h/.cpp、bridge\_exports.cpp、rtx\_gui.cpp、workstation\_canvas.cpp、hw\_transcode\_stages.cpp、nv\_decoder.cpp、nv\_encoder.cpp、rife\_builder.cpp、rife\_engine.cpp）为事实锚点，核验 17–20 号施工单对 16 号演进计划的承接精度。除管理口径与工期预估（0.5 日微整改收尾 \+ 11.2 约 3–4 日 \+ 11.3 约 3–4 日 \+ 11.4 约 4–5 日）完全吻合外1，共有 7 项结构性偏差须在派单前同步修订：

| 序号 | 涉及文档与源码锚点 | 现行计划与代码的冲突断层 | 根因定位与工程后果 | 强制修正裁决 |
| :---- | :---- | :---- | :---- | :---- |
| **C-01** | 17 号 §四隔离矩阵 vs. 18 号 & CMakeLists.txt | 18 号要求改造 nvidia\_transcode\_loader.cpp 且须联动着色器、单测与桥接接口，但 17 号隔离矩阵写了构建表外的文件名且未放行关联文件1。 | CMakeLists.txt 中解码器实现位于 src/shim/loader/nv\_decoder.cpp6；preview\_tone\_map\_cs.hlsl、test\_raii\_retirement.cpp 与桥接导出层未放行则 11.2 无法编译接线3。 | 修订 17 号隔离表：11.2 改指 nv\_decoder.cpp 及关联 ABI/Shim 封装文件，按波次解锁必需的共享桥接与测试文件。 |
| **C-02** | 18 号 vs. bridge\_exports.cpp & d3d12\_interop.h | PreviewViewport::presentShared() 要求借用 ID3D12Device\* 与共享 ID3D12Resource\*，但桥接层尚未实现 rtx\_ngx\_bridge\_borrow\_preview8。 | bridge\_exports.cpp 中的 preview\_ring\[2\] 仍由 cuMemAllocPitch 分配7，且 d3d12\_interop.cpp 尚未加载 cuExternalMemoryGetMappedBuffer15。 | 11.2 在 D3D12Interop 落地 3 槽共享 D3D12 Buffer 与 CUDA 外部内存映射，并在 ngx\_bridge\_abi.h 扩展导出借用函数。 |
| **C-03** | 18 号 vs. preview\_viewport.cpp | 现行 preview\_viewport.cpp 硬编码 kPreviewSourceWidth \= 960、kPreviewSourceHeight \= 540 与 kSdrReferenceNits \= 100.0f8。 | \[2\] 播放器 与全屏快播呈现 1080p/4K/竖屏原生流时会出现裁剪错位，enable\_truehdr \== 0 下也无法传递 SDR/PQ 语义4。 | 扩展 presentShared() 参数传入动态源宽高、input\_is\_pq（写入 ViewportConstants::reserved\[0\]）与 max\_nits。 |
| **C-04** | 17 号 & 19 号 vs. workstation\_canvas.cpp | 16 号已从 Schema 剔除 av1\_cpu、hevc\_cpu 与 crf 选项，但 workstation\_canvas.cpp 仍将其写入下拉框4。 | 用户在 Win32 UI 选中废弃软编选项后，validateAndNormalizeProfile() 会直接返回 ErrorCode::Unsupported4。 | 在 11.2 收尾或 11.3 首日从 workstation\_canvas.cpp 移除 av1\_cpu、hevc\_cpu 与 crf 下拉项，100% 对齐 kParamSchema。 |
| **C-05** | 19 号 vs. hw\_transcode\_stages.cpp | encodeWorkerBody() 的 NVENC 分支构造 RtxFrameDesc encode\_frame{} 时未赋值 encode\_frame.keyframe10。 | 即使 CompiledRenderGraph 在切点传入 force\_key \= true，nv\_encoder.cpp 也收不到 NV\_ENC\_PIC\_FLAG\_FORCEIDR，硬切导出必挂10。 | 11.3 在 hw\_transcode\_stages.cpp 的 NVENC 分支显式补入 encode\_frame.keyframe \= item.force\_keyframe ? 1 : 0;。 |
| **C-06** | 17 号 & 20 号 vs. CMakeLists.txt | 20 号要求修改 rife\_pack.cu，但 CMakeLists.txt 仅启用 LANGUAGES CXX 并直接嵌入 rife\_pack.ptx6。 | 若只改 .cu 不重新编译并校验 rife\_pack.ptx，修改不会进入产物；若编译生成高版本 PTX ISA 还会重演驱动拒载事故6。 | 20 号单明确规定 .cu \-\> .ptx 同步更新及低版本 PTX ISA 兼容性，并放行 rife\_pack\_runtime.\* 与 .ptx。 |
| **C-07** | 20 号 vs. nv\_encoder.cpp & n13.0.19.1 | 生产子模块锁定 n13.0.19.1，且 nv\_encoder.cpp 写死 frameIntervalP \= 1、zeroReorderDelay \= 1 与单比特流缓冲6。 | 13.0 头文件不含分层参考枚举20；开启 B 帧或 Lookahead 后单缓冲与无重排假设会引发 NV\_ENC\_ERR\_NEED\_MORE\_INPUT 报错及尾帧丢失11。 | 20 号单落地双版本运行时协商、隔离枚举常量及多缓冲环形池，并重写 rtx\_shim\_encoder\_flush() 排空环。 |

此外，穿透 CMakeLists.txt 与 hw\_transcode\_stages.cpp 还确认两处工程待补项：其一，CMakeLists.txt 的 CineRTX 目标尚未开启 /MAP 链接选项，也未定义 fxc.exe 着色器编译规则6；其二，hw\_transcode\_stages.cpp 虽在 options\_.vsr\_before\_fg 时分配了 anchor\_p010\_ 设备缓冲，但 handleDecodedFrame() 仍始终先补帧后超分，且 initRife 固定传源宽高10，该拓扑断链必须在 Phase 11.4 一并合龙。

## **二、 Phase 11.1 遗留欠账清零与修订版防踩踏隔离矩阵**

### **1\. 开工首日微整改（A-01 至 A-05）闭环方案**

在派发 18 号施工单前的 0.5 个工作日内，协调负责人必须严格核验并推动清零 16 号文档定位的五项审计欠账1，同时修正过时认知——实证显示现行 rtx\_gui.cpp 的 WorkstationApp 已改用 UniqueThread、UniqueBrush 与 UniqueFont22，无需重复改造，整改焦点应精准锁定在以下五项实务：

> 1. **核验环境硬门槛（A-01）**：在测试宿主机通过只读命令确认 nvidia-smi 驱动版本不低于 621.14 WHQL（规避 621.01 在 RTX 50 系列 GPU 上的视频播放冻结与黑屏已知缺陷）1，并通过 quser 确认当前会话为 Active 且 ID \> 0 的交互式桌面，彻底退出 Session 0 无头会话1。  
> 2. **补齐零短路中段失败注入压测（A-02）**：针对 tests/test\_raii\_retirement.cpp 仅用空路径触发首步 I/O 短路的缺陷4，在 tests/fixtures/ 落盘极简合法 H.264/HEVC/AV1+AAC 媒体小文件（AuditSpdx.cmake 仅禁止 .lib/.dll/.exe/.engine/.onnx 等构建与模型产物，轻量测试媒体不违规）4。压测用例须涵盖越界参数（在打开 demuxer\_ 与 shim\_ 后触发失败）、错误 RIFE 路径（触发 RemasterPipeline::initRemasterAndFgStage 中段回滚）以及 PreviewViewport 在 WAIT\_TIMEOUT 与 DeviceLost 下的释放守恒计数。  
> 3. **完成 param\_schema.cpp 扫描器消重（A-03）**：在 src/core/param\_schema.cpp 引入 include/rtx/json\_scanner.h，删除内部重复保留的 JsonMember、scanJsonString、scanJsonObject 与 decodeJsonString 私有实现4，保证后续 11.3 开发的 nle\_project.cpp 直接复用单一扫描底座1。  
> 4. **补齐构建与稳定性实证（A-04）**：落盘 RIFE 连续 10 次运行通过且引擎字节数一致的日志、verify\_ssot.py（parity / single-field / negative）运行日志、CI Diff 校验结果与 Zig 0.13 交叉构建全绿实证1。  
> 5. **清理 UI 宽字符乱码与废弃选项（A-05 与 C-04）**：修复 src/gui/workstation\_canvas.cpp 中 ChromeText::SendEncoder 的损坏字面量 L"ðŸš€ ..." 为合法 UTF-16 文本4，并同步从 collectProfileFromUi、applyProfileToUi 与 populateComboBoxes 中剔除已废弃的 av1\_cpu、hevc\_cpu 与 crf 选项4。

### **2\. 修订版物理文件隔离矩阵**

针对 17 号文档 §四隔离矩阵与实际源码结构及跨模块接线存在的冲突，现行三份施工单的文件修改授权边界修订如下，既保留波次串行防踩踏约束，又消除外包工程师因权限缺失而无法接入主干的制度障碍：

| 施工波次 | 修正后允许修改的既有文件 | 允许新建的文件 | 严禁越界触碰的文件 |
| :---- | :---- | :---- | :---- |
| **Phase 11.2** （18 号单 \+ A-02\~05） | CMakeLists.txt、src/core/param\_schema.cpp、src/core/nvidia\_shim.cpp、src/core/ngx\_remaster.cpp、src/core/hw\_transcode\_stages.cpp、src/core/job\_queue.cpp、src/shim/loader/nv\_decoder.cpp、src/shim/loader/nv\_shim\_static\_table.cpp、include/shim/rtx\_shim\_abi.h、include/shim/ngx\_bridge\_abi.h、include/rtx/nvidia\_shim.h、include/rtx/embedded\_shim.h、include/rtx/ngx\_remaster.h、src/shim/ngx\_bridge/d3d12\_interop.h/.cpp、src/shim/ngx\_bridge/bridge\_exports.cpp、src/gui/preview\_viewport.h/.cpp、src/gui/d3d12\_raii.h、src/gui/rtx\_gui.cpp、src/gui/workstation\_canvas.cpp（仅 A-05/C-04）、src/gui/shaders/preview\_tone\_map\_cs.hlsl（git rm 旧 .h）、tests/test\_raii\_retirement.cpp | include/rtx/time\_model.h、include/rtx/telemetry\_bus.h、include/rtx/playback\_session.h、src/core/playback\_session.cpp、src/core/wasapi\_audio\_output.cpp、src/core/gop\_seek\_scheduler.cpp、tests/fixtures/\*、tests/test\_flicks\_semantics.cpp、tests/test\_playback\_session.cpp | schema/\*、src/core/nle\_project.\*、src/core/render\_graph.\*、src/core/audio\_mixer\_avx2.\*、src/shim/ngx\_bridge/rife\_\*、src/shim/loader/nv\_encoder.cpp |
| **Phase 11.3** （19 号单） | CMakeLists.txt、src/gui/workstation\_canvas.h/.cpp、src/gui/rtx\_gui.cpp（NLE 快捷键与导出接线）、include/rtx/playback\_session.h、src/core/playback\_session.cpp、src/core/hw\_transcode\_stages.cpp、include/rtx/audio\_mixer\_avx2.h、src/core/audio\_mixer\_avx2.cpp、tests/test\_audio\_mixer\_avx2.cpp | include/rtx/nle\_project.h、src/core/nle\_project.cpp、include/rtx/render\_graph.h、src/core/render\_graph.cpp、src/core/wsola\_stretch.cpp、tests/test\_nle\_vertical\_slice.cpp | src/gui/preview\_viewport.\*、src/shim/ngx\_bridge/rife\_\*、src/shim/loader/nv\_encoder.cpp、schema/\* |
| **Phase 11.4** （20 号单） | CMakeLists.txt、include/shim/ngx\_bridge\_abi.h、include/rtx/ngx\_remaster.h、src/core/ngx\_remaster.cpp、src/shim/ngx\_bridge/bridge\_exports.cpp、src/shim/ngx\_bridge/rife\_engine.h/.cpp、src/shim/ngx\_bridge/rife\_builder.cpp、src/shim/ngx\_bridge/rife\_pack\_runtime.h/.cpp、src/shim/ngx\_bridge/rife\_pack.cu、src/shim/ngx\_bridge/rife\_pack.ptx、src/core/hw\_transcode.cpp、src/core/hw\_transcode\_stages.cpp、src/core/playback\_session.cpp、src/shim/loader/nv\_encoder.cpp | include/rtx/vram\_scheduler.h、src/core/vram\_scheduler.cpp、tests/test\_vram\_scheduler.cpp、tests/test\_rife\_scale05.cpp | src/gui/\*、schema/\*、src/core/nle\_project.\*、src/core/wasapi\_\* |

## **三、 Phase 11.2（18 号单）深度技术解法：DComp 零回读通路与非终态 Seek**

### **1\. 构建期着色器生成与 Linker .map 管道**

在 CMakeLists.txt 落地 fxc.exe 自动构建时，考虑到外部执行节点可能不运行在 VS Developer Command Prompt 中，find\_program 除了探测 \$ENV{WindowsSdkVerBinPath}/x64 与 \$ENV{WindowsSdkDir}/bin/x643，还应使用 file(GLOB ... "C:/Program Files (x86)/Windows Kits/10/bin/\*/x64") 按版本号降序加入搜索提示，若未命中则直接 message(FATAL\_ERROR ...) 报错阻断。  
由于 CMakeLists.txt 中 CineRTX 与 rtx\_tests 均编译 src/gui/preview\_viewport.cpp6，必须将生成的 \${CMAKE\_BINARY\_DIR}/generated/preview\_tone\_map\_cs.h 同时追加至两个目标的 target\_sources（触发精确构建顺序依赖）并向二者追加 \${CMAKE\_BINARY\_DIR}/generated 至 target\_include\_directories3。随后将 preview\_viewport.cpp 与 test\_raii\_retirement.cpp 的包含指令改为 \#include "preview\_tone\_map\_cs.h" 并从源码树物理删除 src/gui/shaders/preview\_tone\_map\_cs.h3。同时，在 CMakeLists.txt 的 if(WIN32) if(MSVC) 块中为 CineRTX 追加 /MAP:\$\<TARGET\_FILE\_DIR:CineRTX\>/CineRTX.exe.map 链接选项，同时保持 /OPT:REF 与 /OPT:ICF 处于开启状态，确保产物校验具备公信力4。

### **2\. D3D12–CUDA 三槽共享预览环与 PreviewViewport 四项加固**

核查现行 preview\_viewport.cpp 可知，视口着色器通过 DXGI\_FORMAT\_R32\_UINT 的 Buffer\<uint\> SRV 直接读取 shared\_resource8，而现行 bridge\_exports.cpp 的 preview\_ring\[2\] 仍是由 cuMemAllocPitch 分配的纯 CUDA 设备内存，既非 D3D12 共享资源，也未实现任何 rtx\_ngx\_bridge\_borrow\_preview 导出函数7。  
要打通真正的 0 B D2H 通道，须在 src/shim/ngx\_bridge/d3d12\_interop.cpp 补充加载 cuExternalMemoryGetMappedBuffer（该 API 签名已在 nv\_func\_tables.cpp 中使用，可直接对齐）15。在 bridge\_exports.cpp 中将 preview\_ring 从 2 槽扩充为 **3 槽** D3D12\_RESOURCE\_DIMENSION\_BUFFER 共享资源（3 × 960 × 540 × 4 B \= 6,220,800 B，约 5.93 MiB，相比 2 槽 3.96 MiB 仅多占用 1.98 MiB 显存，行距 3840 B 天然对齐）4。每个槽位以 D3D12\_HEAP\_FLAG\_SHARED 和初始状态 D3D12\_RESOURCE\_STATE\_COMMON 提交创建，通过 CreateSharedHandle 导出 Win32 句柄并由 CUDA cuImportExternalMemory 与 cuExternalMemoryGetMappedBuffer 映射出 CUdeviceptr，供融合 pack 核函数写入。同步在 include/shim/ngx\_bridge\_abi.h（严守 MIT OR Apache-2.0 头边界）14 追加 rtx\_ngx\_bridge\_borrow\_preview(RtxNgxBridge\* bridge, uint32\_t slot, void\*\* out\_d3d12\_device, void\*\* out\_d3d12\_resource, uint32\_t\* out\_pitch)，并在 NgxRemasterBridge 完成静态绑定25。生产端（hw\_transcode\_stages.cpp）在写入某槽位前通过轻量原子世代探测确认该槽位未被视口占用，若 3 槽均忙则置 preview\_rgba10\_ptr \= 0 直接跳过本次 UI 预览写入，从而兼顾“零撕裂”与“零阻塞后台转码”3。  
在视口侧，对照现行 preview\_viewport.cpp 与 d3d12\_raii.h 源码实况8，presentShared() 虽然已补入 AddRef \+ retirement\_.retire(fence\_value\_, res) 基础路径8，但 16 号与 18 号文档点明的四处高危边界仍处于未修状态，必须逐一封堵3：

> 1. **封堵 UINT64\_MAX 设备移除假完成陷阱**：现行 PreviewViewport::waitForGpuIdle() 与 GpuRetirementQueue::collect() 均直接判断 GetCompletedValue() \>= fence\_value\_8。当 GPU 发生 TDR 或驱动掉线时，GetCompletedValue() 会被 D3D12 强制推至 UINT64\_MAX3。必须优先检测 completed \== UINT64\_MAX，命中后立即调用 device\_-\>GetDeviceRemovedReason()，置位 device\_lost\_ \= true 并调用 retirement\_.releaseAll() 归还 COM 引用，返回 ErrorCode::DeviceLost3。  
> 2. **补全命令级对象延迟退役与 Signal 检查**：现行 retireAllGpuResources() 仅移交了 render\_targets\_、view\_heap\_ 与 swap\_chain\_8。当遇到 WAIT\_TIMEOUT 时，必须把正在被队列执行的 cmd\_allocator\_、cmd\_list\_、pipeline\_、root\_signature\_、cmd\_queue\_ 与旧 device\_ 一并纳入 retireAllGpuResources()3；在 adoptDevice() 更换设备前还应将 visual\_-\>SetContent(nullptr) 与 dcomp\_target\_-\>SetRoot(nullptr) 置空并 Commit()，防止 DWM 持有旧交换链。此外，presentShared() 中的 (void)cmd\_queue\_-\>Signal(...) 严禁忽略失败返回值，一旦失败即触发 device\_lost\_ 受控清理3。  
> 3. **校准帧延迟等待序与动态源参数解耦**：现行 presentShared() 先以 0 ms 探测 GetFrameLatencyWaitableObject() 再探测 waitForGpuIdle(0)8，若计算队列尚忙将白白消耗帧延迟信号4。必须调换为先 waitForGpuIdle(0) 后 WaitForSingleObject(GetFrameLatencyWaitableObject(), 0\)3。同时将 presentShared() 签名扩展为接受动态 src\_width、src\_height、bool input\_is\_pq 与 float max\_nits（移除 960×540 与 100.0f 硬编码）8，把 input\_is\_pq 存入 ViewportConstants::reserved\[0\]（维持 12 DWORD 根常量布局不变），更新 preview\_tone\_map\_cs.hlsl 按此标志区分 Studio-Swing SDR Gamma 解码与 PQ ST.2084 解码，根治 enable\_truehdr \== 0 下的 SDR 预览压暗缺陷4，并彻底删除 presentFromHostOrShared 空桩与 rtx\_gui.cpp 中的 previewHost3。

### **3\. nv\_decoder.cpp 非终态复位与实时播放零回读路径**

核查 src/shim/loader/nv\_decoder.cpp 可确认，rtx\_shim\_decoder\_flush() 当前将 decoder-\>end\_of\_stream \= true 并向解析器提交 CUVID\_PKT\_ENDOFSTREAM12。为在不破坏 RemasterPipeline 编码收尾语义的前提下支持高频拖拽和 ,/. 步进，应保留 rtx\_shim\_decoder\_flush() 原逻辑，而在 include/shim/rtx\_shim\_abi.h（及 nv\_shim\_static\_table.cpp、embedded\_shim.h、nvidia\_shim.h/.cpp）新增 rtx\_shim\_decoder\_reset(RtxDecoder\* decoder)。其内部在 decoder-\>stream 上执行 cuStreamSynchronize 以确保前序异步导出拷贝安全落盘，随后清空 ready 队列、将全部 slots 复原为 in\_use \= false 归还 free\_slots、清零 end\_of\_stream 与 flushed 标志，并向 cuvidParseVideoData 提交携带 CUVID\_PKT\_DISCONTINUITY（0x04）的空包刷新解析器内部参考状态3。借助该非终态复位，GopSeekScheduler 即可在单个 CUvideodecoder 实例上实现毫秒级 Open-GOP 帧精确跳转，消除重建解码器与 CUDA 主上下文引用计数归零抖动3。  
在呈现路径上，Phase 11.2 须避免转码预览与原生播放器走两套回读路径。MediaPlaybackSession 的 video\_worker 单线程绑定 CUDA 主上下文后3，应对解码出的 NV12/P010 设备表面直接在 GPU 上运行轻量 NV12/P010 \-\> R10G10B10A2 PTX 打包核函数，写入一套轻量无头 D3D12–CUDA 3 槽共享 Buffer（尺寸按源帧动态分配），再连同源宽高及解码色彩空间标志 input\_is\_pq 送入 PreviewViewport::presentShared()。由此实现 \[2\] 播放器、无边框全屏快播与转码队列监看共享同一套 DComp 零回读视口，D2H 像素拷贝字节数恒为 0 B1。音频端以 WasapiAudioOutput 的 IAudioClock::GetPosition() \+ QPC 外推构建 705,600,000 Hz 原生 Flicks 主时钟（无音轨素材退化至 steady\_clock）29，在 pause() 时冻结时钟锚点3，捕获 AUDCLNT\_E\_DEVICE\_INVALIDATED 后由控制面无锁切换默认音频端点续播4。最后在 rtx\_gui.cpp 中彻底拔除 WM\_TIMER(3, 66ms) 与 runScrubPreviewAsync()，改由 30 Hz 定时器轮询无锁 AtomicTelemetryBus 刷新界面状态3。

## **四、 Phase 11.3（19 号单）深度技术解法：统一 NLE 引擎、硬切 IDR 修复与零分配音频总线**

### **1\. NleProject 契约对齐与 workstation\_canvas.cpp 替换**

src/core/nle\_project.cpp 必须 100% 复用 include/rtx/json\_scanner.h 实现 toJson() 与 fromJson()，绝不允许保留或重写第二份 JSON 扫描器17。针对命令集合的一致性，09 号施工单要求实现 RelinkAssetCmd30，而 19 号施工单仅列出了 6 类剪辑与轨道命令17，但门禁 Gate 11D-2 明确要求验收工程重链接（Relink）的可逆闭环2。因此 EditHistory 必须同时覆盖 AddClipCommand、TrimClipCommand、SplitClipCommand、MoveClipCommand、DeleteClipCommand、ReorderTracksCommand 与 RelinkAssetCommand，且所有命令实现类严禁持有任何 GPU、COM 或 CUDA 句柄17。  
在 src/gui/workstation\_canvas.h/.cpp 中，必须物理删除 sharedNleTimeline()、NleTimeline、NleClip、timelineXToMs、timelineMsToX 以及 onNleRazor 中残留的 msToFlicks(100) 边界判断9。画布时间轴的缩放、标尺刻度、吸附定位、入出点标记与 Ctrl+Z / Ctrl+Y 撤销重做全面切至 NleProject 的 64 位整数 Flicks 计量与 formatSmpteTimecode()，并将原 nleTimelineProc 通过 GWLP\_USERDATA 手工管理裸指针的 NleWindowState 收归安全 RAII 持有9。

### **2\. CompiledRenderGraph 双向驱动与 NVENC 硬切 IDR 断链修复**

CompiledRenderGraph::compile() 将 NleProject 的非线性多轨结构编译为确定性的线性时间分段算子图，供 MediaPlaybackSession（实时编辑预览）与 RemasterPipeline（离线高质量容器导出）共同消费，确保两者的剪辑时间映射、音画对齐与硬切点完全一致17。  
离线硬切导出合龙前必须修复两处 hw\_transcode\_stages.cpp 的实际源码断点：第一，现行 encodeWorkerBody() 的 GPU NVENC 分支在构造 RtxFrameDesc encode\_frame{} 时遗漏了对 encode\_frame.keyframe 的赋值，导致 PendingEncodeItem::force\_keyframe 仅在 CPU 分支生效而无法触发 GPU NVENC 的强制 IDR10。必须补入 encode\_frame.keyframe \= item.force\_keyframe ? 1 : 0;，使 nv\_encoder.cpp 自动打入 NV\_ENC\_PIC\_FLAG\_FORCEIDR \| NV\_ENC\_PIC\_FLAG\_OUTPUT\_SPSPPS11。第二，当前 RemasterPipeline 仅围绕单一 NvidiaTranscodeSession 打开一次解复用器与解码器10；在接入 CompiledRenderGraph 执行多段 1V+1A 导出时，须在整个导出生命周期内保持单一 encoder\_ 与 muxer\_ 实例不断流（维持以时间线 Flicks 换算 90 kHz 的单调连续 PTS），按图切换或 Seek 源素材的 demuxer\_ 与 decoder\_，并在跨素材片段边界自动置位 force\_reset\_temporal \= true（驱动 NVSDK\_NGX\_Parameter\_Reset 清除 VSR 时域历史）32 与 force\_key \= true（强制生成 IDR 关键帧），最终以输出 MP4/MKV 文件的实际解复用帧数和切点 IDR 位置作为逐帧验收依据4。

### **3\. 零分配 AVX2 混音与流式 WSOLA 状态机**

现行 audio\_mixer\_avx2.cpp 的 mixAvx2() 在每次调用时构造 std::vector\<VoicePrep\>，若接入实时音频线程将违反零分配红线4。重构方案为引入由调用方长期持有的固定容量 AudioMixerScratchpad（或采用 std::array\<VoicePrep, 32\> 栈上分块处理超多轨情况），确保热循环内 malloc/new 调用次数严格为 0，并在 tests/test\_audio\_mixer\_avx2.cpp 中通过 Debug 构建重载 operator new 计数断言予以锁死17。  
同时，实时播放与连续 NLE 导出按音频缓冲块流式拉取样本，离线整段缓冲形式的 wsolaStretch 无法直接跨块无缝衔接。应当围绕 wsolaStretch 封装带预设环形分析窗与汉宁重叠相加尾迹区的 WsolaStreamState（所有工作缓冲在会话创建期一次性分配），全面替换存在变调失真的 VarispeedCursor4，使 0.25x–2.0x 变速回放与导出兼顾无接缝听感与基频漂移 \< 0.05% 的实证指标4。

## **五、 Phase 11.4（20 号单）深度技术解法：RIFE 0.5 对齐、显存水线建模与 SDK 13.1 状态机重构**

### **1\. RIFE scale=0.5（A=128 \+ reflect101）、PTX 构建与管线拓扑对齐**

在 include/shim/ngx\_bridge\_abi.h 追加向后兼容的 ABI v5 结构体 RtxRifeConfig 与 rtx\_ngx\_bridge\_init\_rife\_ex 入口，并同步更新 bridge\_exports.cpp 与 NgxRemasterBridge 函数指针绑定18。针对 CMakeLists.txt 不直接编译 .cu 的现状6，修改 rife\_pack.cu 添加 A=128 向上对齐及 reflect101 边界镜像核函数后，必须采用保守兼容的 PTX ISA 目标版本重新生成并同步更新 src/shim/ngx\_bridge/rife\_pack.ptx 与 rife\_pack\_runtime.h/.cpp（避免出现 Phase 10 中 PTX .version 9.0 在低版本驱动下被 JIT 拒载的故障）6。  
数学对齐与 TensorRT 引擎构建方面，rife\_engine.cpp 与 rife\_builder.cpp 当前不仅硬编码 kAlign \= 6434，且 rife\_builder.cpp 将 Optimization Profile 上限写死为 max\_w \= max(1920, opt\_w)、max\_h \= max(1088, opt\_h)34。接入 flow\_scale \== 0.5f 时，除了将对齐模数升级为 A \= 128（保证经 2× 光流降采样及金字塔各层后仍满足整数对齐）33 并在输出回写时精确裁回原始 input\_width × input\_height 外33，还必须确保动态 Profile kMAX 覆盖至 4K 对齐尺寸 3840×217624；reflect101 采样在越界时按不重复边缘像素规律对称折返（如 \-1 映射至 1，W 映射至 W \- 2；当宽高为 1 时退化夹取至 0），从而彻底消除边缘黑边与光流断层18。最后，在 hw\_transcode\_stages.cpp 中彻底闭环 vsr-first 与 fg-first 分支：不再固定读取 session\_-\>info().width/height 调用 initRife10，而是按 VramAdaptiveScheduler 选定的 TopologyTier 传入真实输入尺寸，并将闲置的 anchor\_p010\_ 真正接入 vsr-first 超分后补帧流转中10。

### **2\. 8 GB 显卡 4K 压制显存建模与滞后调度边界**

为防止在 8 GB GDDR7 显存的 RTX 5060 Laptop GPU（总显存 8192 MiB）执行 4K 压制时出现盲目降级或 OOM，下面按未压缩帧尺寸与张量形状核算关键显存组件的理论基准占用（实际 TensorRT 工作区峰值、驱动参考池及 Lookahead 分析缓存仍须以物理硬件实测 trt\_peak\_allocation\_bytes 与 CUDA/DXGI 采样为准）24：

| 显存占用项（理论等效容量估算） | 1080p（1920×1080） | 4K（3840×2160） | 调度器建模与防 OOM 边界意义 |
| :---- | :---- | :---- | :---- |
| 单帧像素基数（NV12 / P010 / RGBA16F） | 2.97 MiB / 5.93 MiB / 15.82 MiB | 11.87 MiB / 23.73 MiB / 63.28 MiB | 4K 10-bit 帧体量是 1080p NV12 的 8 倍，决定参考帧池敏感度24。 |
| D3D12Interop 共享表面（输入/中间/输出 \+ 3 槽预览） | 输入 7.91 MiB | mid RGBA8 31.64 MiB \+ out RGBA16F 63.28 MiB \+ preview 5.93 MiB | 仅桥接层自身静态固定占用约 109 MiB（不含 NGX 模型权重）15。 |
| RIFE Scratch 张量（7ch 输入 \+ 3ch 输出 FP32） | A=64（1920×1088）: 79.69 MiB A=128（1920×1152）: 84.38 MiB | A=64/128（3840×2176）: 318.75 MiB | 若加载 kHALF 引擎另增一半张量缓冲，外加最高 1024 MiB TRT 工作区上限24。 |
| NVENC AV1 分层 B 帧等效未压缩 P010 参考量 | 随帧数线性增长 | 7B: 166.11 MiB / 15B: 355.96 MiB / 31B: 735.64 MiB | 31B 外加 UHQ Lookahead 在 8 GB 显卡上极易触顶，须实施深度保护19。 |

在 Windows WDDM 环境中，操作系统、DWM 桌面合成与视口交换链通常先期占用约 500–900 MiB，使 DXGI 报告的进程可用本地预算 dxgi\_budget\_bytes 大致落在 7200–7600 MiB 区间（按 7300 MiB 示例估算，82% 降级水位线约为 5986.0 MiB，68% 恢复水位线约为 4964.0 MiB；若错误使用物理总量 8192 MiB 计算，82% 为 6717.4 MiB，此时可能已逼近 WDDM 内存驱逐边界）24。因此，VramAdaptiveScheduler（默认降级水印 82%、恢复水印 68%、最小驻留 120 帧）18 必须以真实采集的 dxgi\_budget\_bytes 为核心分母，联合 dxgi\_current\_usage\_bytes、cuMemGetInfo 空闲显存、RIFE 实际 trt\_peak\_allocation\_bytes 与编解码参考池进行水位判断，并严格按 VsrFirst\_RifeFull \-\> VsrFirst\_RifeHalf \-\> VsrFirst\_Nvofa \-\> FgFirst\_RifeFull \-\> PassThrough\_NoFruc 梯队平滑降级33。切换过程中保持 NVOFA 与 RIFE 互斥按需初始化与销毁，严禁对黑盒管理显存的 NVOFA 执行跨库 cuMemMap 物理页别名19。

### **3\. SDK 13.1 双版本兼容协商与 nv\_encoder.cpp 状态机重构**

针对生产默认维持 nv-codec-headers n13.0.19.1（兼容 R580）4 与在 \>= 621.14 WHQL 驱动下实测 SDK 13.1 AV1 分层 B 帧（1 / 3 / 7 / 15 / 31 帧）及 UHQ 的要求4，仅在 nv\_encoder.cpp 中增加静态能力探测无法直接跑通实测。工程上必须同步完成以下两层改造：  
首先，实施 **运行时 API 版本双模协商与特性探测**。在 nv\_encoder.cpp 创建会话前调用 NvEncodeAPIGetMaxSupportedVersion(\&max\_ver)（nv\_func\_tables.cpp 已解析该入口）16。当检测到系统驱动支持 API 版本 \>= 13.1（SDK 13.1.15 对应最低驱动 \>= 610.0）37 且启用了高阶 Profile 时，按实际协商版本打开编码会话，并通过局部隔离常量补充 NV\_ENC\_BFRAME\_REF\_MODE\_HIERARCHICAL \= 0x420 与 UHQ Tuning 定义，随后由 queryAv1AdvancedCaps() 调用 NvEncGetEncodeCaps 探测 NV\_ENC\_CAPS\_SUPPORT\_BFRAME\_REF\_MODE（校验位掩码是否支持分层参考）33、NV\_ENC\_CAPS\_SUPPORT\_LOOKAHEAD20 与 NV\_ENC\_CAPS\_SUPPORT\_TEMPORAL\_FILTERING33（注意时域滤波要求 frameIntervalP \>= 5 且禁止启用 zeroReorderDelay）39。当处于 \< 610.0 低版本驱动或硬件不支持时，自动回退至 13.0 会话与 Default 配置并输出告警日志，杜绝初始化崩溃18。  
其次，**彻底重构 nv\_encoder.cpp 的 B 帧重排与多缓冲池**。现行 nv\_encoder.cpp 仅持有一个 NV\_ENC\_OUTPUT\_PTR bitstream 且在 encodeDeviceFrame() 中遇到非 NV\_ENC\_SUCCESS 状态即按失败退出，同时 rtx\_shim\_encoder\_flush() 假设 zeroReorderDelay \= 1 而直接空转返回11。依据 NVIDIA NVENC 官方编程指南，启用 B 帧与 Lookahead 时客户端分配的 I/O 缓冲数至少应为 4 \+ B 帧数（并计入 Lookahead 深度）21。必须将单输出槽升级为 std::vector\<BitstreamSlot\>（配套各自 WDDM 异步完成事件）环形池；当 nvEncEncodePicture 返回 NV\_ENC\_ERR\_NEED\_MORE\_INPUT（因 B 帧重排或 Lookahead 缓冲蓄水中）时合法返回缓冲蓄水信号而非视作 RTX\_ERR\_INTERNAL 错误11；并重写 rtx\_shim\_encoder\_flush()，在提交 NV\_ENC\_PIC\_FLAG\_EOS 后循环锁定并吐出所有待重排的延迟编码包直至排空11。最终在配备 8 GB GDDR7 显存的 RTX 5060 测试机（驱动 \>= 621.14 WHQL）上，如实记录 GPU 型号、驱动与各 SDK 版本、测试片源规格、编码参数、预热时长、统计窗口与重复次数，输出 Default vs HighQuality-7B vs Master-UHQ-15B/31B 的 FPS、编码器占用、VRAM 峰值与 PSNR/SSIM/VMAF 实测矩阵2。

## **六、 协调负责人三层硬核验收与推进执行序列**

为确保 Phase 11 剩余波次高质量合龙并按期封板 v0.3.0，协调负责人应严格按以下四步序列管控调度与集成验收：

> 1. **波次放行前检（Pre-Flight）**：在宿主机核验 nvidia-smi 驱动版本 \>= 621.14 且 quser 为交互桌面 ID \> 0，确认 A-01 至 A-05 五项欠账已 100% 销号并归档日志，按本文修订版物理隔离矩阵锁定边界后，按 18 \-\> 19 \-\> 20 严格串行派单，严禁跨阶段并发合流1。  
> 2. **静态文件边界与工程卫生门禁**：每波次交付后核查 git status 修改列表未越界；执行 rg "runScrubPreviewAsync\|SetTimer\\(hwnd, 3, 66" src/gui/rtx\_gui.cpp（11.2 起必须 0 命中）1 与 rg "start\_ms\|duration\_ms\|playhead\_ms\|msToFlicks\|sharedNleTimeline" src/gui/workstation\_canvas.\*（11.3 起必须 0 命中）17；执行 cmake \-P cmake/AuditSpdx.cmake 与 cmake \-P cmake/AuditImports.cmake，确保全仓 SPDX 分层合规且 CineRTX.exe 体积处于 1.2–12.0 MB 包络、动态 DLL 零违规泄漏1。  
> 3. **Linker .map 非内联修饰符号留存审计**：在 /OPT:REF、/OPT:ICF 与 /MAP 全开条件下检查 CineRTX.exe.map 的 Publics by Value 节区，确认核心非内联工厂与控制入口实体进入主程序：11.2 必查 MediaPlaybackSession::create、MediaPlaybackSession::seek、WasapiAudioOutput::create、GopSeekScheduler、PreviewViewport::presentShared、PreviewViewport::adoptDevice（豁免纯内联 AtomicTelemetryBus）1；11.3 必查 NleProject::toJson/fromJson、CompiledRenderGraph::compile、mixAvx2、wsolaStretch17。  
> 4. **物理桌面交互与全链路实证封板**：在 WTS Session \> 0 交互式 Windows 桌面下运行全量 rtx\_tests.exe（零 \[SKIP-GPU\]）4 与 CineRTX.exe 实机测试——11.2 实证三制式回放、IAudioClock 同步、设备热拔插（AUDCLNT\_E\_DEVICE\_INVALIDATED）自动重连、100 次 resizeSwapChain、HDR/SDR 跨屏切换及 presented \> 0、D2H \== 0 B1；11.3 实证 Debug 混音 0 次堆分配、WSOLA 基频漂移 \< 0.05% 以及多段 1V+1A 硬切导出容器经解复用后的帧数与切点 IDR 逐帧一致4；11.4 实证 test\_rife\_scale05 无边缘黑边、8 GB RTX 5060 4K 压制零 OOM（显存受控于 82% 滞后水位线下）及完整口径 SDK 13.1 实测矩阵落盘，所有门禁全绿后方可提请用户批准合流并打标发布 v0.3.01。

#### **引用的著作**

> 1. 17\_CineRTX Phase 11 外包施工总纲暨协调负责人备忘录.md [受控工程档案]  
> 2. [受控工程档案]  
> 3. 18\_CineRTX Phase 11.2 SuperPower 施工单\_实时视听内核与DirectComposition视口.md [受控工程档案]  
> 4. CineRTX Phase 11 演进计划 [受控工程档案]  
> 5. CineRTX Phase 11 验收与演进 [受控工程档案]  
> 6. CMakeLists.txt [受控工程档案]  
> 7. bridge\_exports.cpp [受控工程档案]  
> 8. preview\_viewport.cpp [受控工程档案]  
> 9. workstation\_canvas.cpp [受控工程档案]  
> 10. hw\_transcode\_stages.cpp [受控工程档案]  
> 11. nv\_encoder.cpp [受控工程档案]  
> 12. nv\_decoder.cpp [受控工程档案]  
> 13. preview\_viewport.h [受控工程档案]  
> 14. ngx\_bridge\_abi.h [受控工程档案]  
> 15. d3d12\_interop.cpp [受控工程档案]  
> 16. nv\_func\_tables.cpp [受控工程档案]  
> 17. 19\_CineRTX Phase 11.3 SuperPower 施工单\_统一NLE引擎与零分配AVX2音频总线.md [受控工程档案]  
> 18. 20\_CineRTX Phase 11.4 SuperPower 施工单\_RIFE高阶算子与SDK13.1分级实证.md [受控工程档案]  
> 19. CineRTX Phase 11 冲突评判与方案 [受控工程档案]  
> 20. nv-codec-headers/include/ffnvcodec/nvEncodeAPI.h at master, [https\://github.com/FFmpeg/nv-codec-headers/blob/master/include/ffnvcodec/nvEncodeAPI.h](https://github.com/FFmpeg/nv-codec-headers/blob/master/include/ffnvcodec/nvEncodeAPI.h)  
> 21. NVENC Video Encoder API Programming Guide, [https\://docs.nvidia.com/video-technologies/video-codec-sdk/13.1/nvenc-video-encoder-api-prog-guide/](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.1/nvenc-video-encoder-api-prog-guide/)  
> 22. rtx\_gui.cpp [受控工程档案]  
> 23. CineRTX Phase 11 复盘与演进 [受控工程档案]  
> 24. [unknown\_url]([internal-reference])  
> 25. ngx\_remaster.cpp [受控工程档案]  
> 26. d3d12\_raii.h [受控工程档案]  
> 27. vivecinema/vivecinema/HWAccelDecoder.cpp at master \- GitHub, [https\://github.com/openbigdatagroup/vivecinema/blob/master/vivecinema/HWAccelDecoder.cpp](https://github.com/openbigdatagroup/vivecinema/blob/master/vivecinema/HWAccelDecoder.cpp)  
> 28. third\_party/ffmpeg \- Git at Google \- Fuchsia, [https\://fuchsia.googlesource.com/third\_party/ffmpeg/+/a3fc3e7c47aa1d8163cee7c0bd1a5c95f0c2fc2d%5E1..a3fc3e7c47aa1d8163cee7c0bd1a5c95f0c2fc2d/](https://fuchsia.googlesource.com/third_party/ffmpeg/+/a3fc3e7c47aa1d8163cee7c0bd1a5c95f0c2fc2d%5E1..a3fc3e7c47aa1d8163cee7c0bd1a5c95f0c2fc2d/)  
> 29. 08\_CineRTX\_Phase11\_施工单\_Track\_11C\_Flicks语义重构与WASAPI实时播放内核.md [受控工程档案]  
> 30. 09\_CineRTX\_Phase11\_施工单\_Track\_11D\_DirectComposition零回读视口与统一NLE引擎及AVX2音频矩阵.md [受控工程档案]  
> 31. hw\_transcode.cpp [受控工程档案]  
> 32. ngx\_d3d12\_bridge.cpp [受控工程档案]  
> 33. 10\_CineRTX\_Phase11\_施工单\_Track\_11E\_VSR\_FRUC显存感知自适应调度与SDK13.1分级接入.md [受控工程档案]  
> 34. rife\_builder.cpp [受控工程档案]  
> 35. rife\_engine.cpp [受控工程档案]  
> 36. CineRTX Phase 11 架构冲突深度评判 [受控工程档案]  
> 37. nv-codec-headers/README at master \- GitHub, [https\://github.com/FFmpeg/nv-codec-headers/blob/master/README](https://github.com/FFmpeg/nv-codec-headers/blob/master/README)  
> 38. 8809 (hevc\_nvenc / b\_ref\_mode each is not working) \- FFmpeg Wiki, [https\://trac.ffmpeg.org/ticket/8809](https://trac.ffmpeg.org/ticket/8809)  
> 39. Using FFmpeg with NVIDIA GPU Hardware Acceleration, [https\://docs.nvidia.com/video-technologies/video-codec-sdk/13.1/ffmpeg-with-nvidia-gpu/index.html](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.1/ffmpeg-with-nvidia-gpu/index.html)  
> 40. NVENC Video Encoder API Programming Guide, [https\://docs.nvidia.com/video-technologies/video-codec-sdk/13.0/nvenc-video-encoder-api-prog-guide/index.html](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.0/nvenc-video-encoder-api-prog-guide/index.html)  
> 41. CineRTX\_Phase9.1\_推送前仓库卫生完善与CMake全原生闭环专家裁决暨SuperPower施工规范.md [受控工程档案]