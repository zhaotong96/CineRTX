# **CineRTX Phase 11.2-R 代码膨胀与 Slop 治理终审裁决暨重构方案**

针对 docs/phase11/ 下 23 号专家咨询委托书（CINERTX-P11-CONSULT-023）1，首席领域专家组经穿透核验 CMakeLists.txt、工作区核心源码及工具链语言规范后正式裁决：**全票批准“标准库先验替换律（Std-First）”与“自定义类型零容忍暨显式存在意义声明”最高架构军规，同意在启动 Phase 11.3 前设立为期 1.5–2 个工作日的「Phase 11.2-R（Refactor & De-slop）」强制整改波次，未经全量脱毒验收严禁推进任何 NLE 新功能开发**1。Phase 11.2 虽然取得了 128/128 单元测试全绿与 CineRTX.exe.map 修饰符号留存1，但单阶段物理新增 6,927 行、净增 5,824 行（手写源码暴增至 43,450 行）已暴露严重的“包装器伪抽象（Wrapper Slop）”与基础设施重复建设1，若不及时熔断清理，后续 Phase 11.3 与 11.4 将沿用同一反模式继续失控膨胀。  
在全面批复五大专项请示的同时，专家组对 23 号委托书的取证底稿与技术落地路径作出三项关键实证校准，防止施工期间引发编译阻断或遗漏真实测试缺口。第一，源码核查证实，src/core/playback\_session.cpp 匿名命名空间内的真实私有类实为 ShimOwner、DecoderOwner、AudioDecodeChain、VarispeedCursor 与 AudioPacketQueue 五个类型3，而委托书点名的 PlaybackClock、FrameDeadlineScheduler 与 DecodeAheadQueue 实为声明于 include/rtx/playback\_session.h 并被 tests/test\_playback\_session.cpp 直接测试的公共接口（其中 DecodeAheadQueue 封装的是 std::vector 环，并非委托书所称的互斥锁加 std::deque 队列）1——重构须一并消解这八处抽象并同步改写对应单测以保住 128/128 覆盖率1。第二，CI 流水线包含基于 LLVM 18 的 Zig 0.13 构建作业6，该环境下 libc++ 默认将 std::jthread 与 std::stop\_token 置于 \-fexperimental-library 门控之后8，且标准 C++23 std::out\_ptr 返回的代理对象不支持 Win32 宏 IID\_PPV\_ARGS9，而 MSVC 14.50–14.53 尚未实现 C++26 \#embed10，所有替换代码必须严格绕开上述工具链暗礁。第三，Phase 11.2-R 的目标净增量压缩至 \+2,000 行以内（目标估算约 \+1,887 行，单阶段净瘦身率约 67.6%）1，且必须同步补齐 22 号验收记录因 Session 0 无头环境而跳过执行的 WTS Session \> 0 交互式桌面实呈闭环2。

## **一、 专项请示 1：标准库先验替换律与自定义类型零容忍规范**

### **1.1 标准库先验替换映射准则**

在 CineRTX 全局启用 MSVC /std:c++latest 与 C++23 语言基线的前提下12，标准库已完整覆盖单资源独占所有权、协作式线程取消、C-API 输出指针回填及轻量值组合语义1。团队必须废除“为每个系统句柄或第三方上下文手写一个 FooOwner 结构体”的伪面向对象习惯1：include/rtx/com\_owner.h（94 行）1 予以物理删除，include/rtx/raii\_owners.h（385 行）1 则收敛为单一 4 行无状态函数指针删除器模板 FnDeleter\<auto Fn\> 搭配标准库类型别名，彻底消除重复的移动构造、移动赋值与 reset() 样板代码1。

| 现有 Slop 自定义类型与源码位置 | 现状与主要问题 | 标准库先验替换方案 | 关键语义与跨工具链约束 |
| :---- | :---- | :---- | :---- |
| UniqueHandle、UniqueModule、UniqueHwnd、UniqueGdiObject（raii\_owners.h） | 分别占 22、21、21、28 行（合计 92 行核心样板），机械重复移动与释放逻辑1 | 全部改写为 std::unique\_ptr\<T, FnDeleter\<...\>\> 类型别名（对应 CloseHandle、FreeLibrary、DestroyWindow、DeleteObject），单条别名占 1–2 行 | 对 CreateFileW 返回值须在调用侧将 INVALID\_HANDLE\_VALUE 归一化为 nullptr，防止 unique\_ptr::operator bool() 误判 \-1 为有效句柄16 |
| ComOwner\<T\>（com\_owner.h）、WapiComOwner\<T\> 与 MixFormatOwner（wasapi\_audio\_output.cpp） | 跨文件手写两套重复 COM 智能指针（仅 com\_owner.h 即占 94 行）1 | 统一收口为 template\<typename T\> using ComPtr \= std::unique\_ptr\<T, FnDeleter\<\&releaseComUnknown\>\> \+ C++23 std::out\_ptr\<void\*\>；WAVEFORMATEX 改用 FnDeleter\<\&CoTaskMemFree\> 别名 | 因 std::out\_ptr 代理缺少 operator\* 而无法通过 IID\_PPV\_ARGS(std::out\_ptr(...)) 编译9，须改为 \_\_uuidof(T), std::out\_ptr\<void\*\>(ptr) 并保证 ptr 类型与 T 一致 |
| UniqueThread 与 PipelineWorker（raii\_owners.h） | 仅 UniqueThread 即占 30 行，手工包裹 std::thread 与停止函数1 | 全面替换为 C++20 std::jthread \+ std::stop\_token \+ std::stop\_callback（配合 std::condition\_variable\_any 响应停止唤醒） | Zig 0.13 内置 LLVM 18 的 libc++ 需 \-fexperimental-library 方可启用 std::jthread7；CMakeLists.txt 须仅针对 Clang/Zig 按探测追加该标志 |
| RifeSession（raii\_owners.h）与 ComApartment（wasapi\_audio\_output.cpp） | 仅用于在作用域退出时触发单一清理动作的极简回调壳16 | RifeSession 改为 std::unique\_ptr\<NvidiaTranscodeSession, FnDeleter\<...\>\>（非拥有宿主触发 destroyRife）；ComApartment 改为 CoUninitialize 哨兵别名 | 维持 rife\_session\_ 声明序晚于 session\_ 的依赖不变量16，零新增自定义类 |
| CudaPinnedHostBuffer（raii\_owners.h）与 MappedResource（d3d12\_raii.h） | 同时包装主机指针与字节数或 Unmap 状态，代码冗余且分散16 | CudaPinnedHostBuffer 替换为 std::pair\<std::unique\_ptr\<void, std::function\<void(void\*)\>\>, size\_t\>；MappedResource 替换为自由工厂返回 std::pair\<std::unique\_ptr\<ID3D12Resource, std::function\<void(ID3D12Resource\*)\>\>, void\*\> | CUDA 锁页路径若缺失释放函数须绑定空操作删除器，绝不回退 std::free16；上传堆延迟退役提取 .release() 后交由 GpuRetirementQueue 按先 Unmap 后 Release 回收18 |
| CudaDeviceBuffer（raii\_owners.h） | 持有 uint64\_t 显存地址、字节数与 CUcontext 绑定回调16 | 选项 A：仅在 64 位架构 static\_assert(sizeof(void\*) \== sizeof(uint64\_t)) 下以不完整标签 struct CudaDeviceTag; 配合 std::unique\_ptr 别名替代；选项 B：保留不足 25 行极简值类型并强制加注 C-ABI 存续声明 | 标准库 std::unique\_ptr 的 pointer 要求满足 NullablePointer（可与 std::nullptr\_t 比较），原始 uint64\_t 无法直接实例化而不经指针强转；优先推荐保留单一极简声明豁免以物理隔离 GPU 与 CPU 地址空间 |
| 一次性数据袋：Borrowed（rtx\_gui.cpp）、Entry 与 DeferredPool（d3d12\_raii.h） | 仅用于局部传参、队列条目或静态锁打包的 2–3 成员被动结构体1 | Borrowed 删去并直接复用 std::tuple\<void\*, void\*, uint32\_t\>；Entry 替换为 std::tuple\<uint64\_t, ComPtr\<IUnknown\>, std::function\<void()\>\>；DeferredPool 替换为函数内双局部静态变量 std::mutex 与 std::vector | 超过 3 个具名语义字段的领域模型或跨线程缓存行对齐结构不得滥用 std::tuple，应走显式存续声明审核 |

### **1.2 自定义类型存续声明模板与 CMake 静态阻断门禁**

全仓任何保留或新增的 class 与 struct（不包括前向声明、enum class、单一删除器适配器模板 FnDeleter、以及 GCC 16 静态反射生成产物）必须在其定义体紧邻上方标注标准格式的 @rtx-type-justification 存续声明块，明确论证标准库机制为何不可替代以及该类型的唯一不变量：

C++  
// @rtx-type-justification  
// Category: \<C-ABI \| Hardware-Layout \| Polymorphic-Boundary \| NonTrivial-StateMachine \| Concurrency-Invariant \| Domain-Model\>  
// Why-Std-Fails: \<说明为何 std::unique\_ptr/std::jthread/std::tuple/std::variant/自由函数无法表达\>  
// Invariant: \<概括该类型唯一不可替代的内存布局、跨界接口或异步生命周期不变量\>

上述声明遵循四项刚性红线。其一，合法豁免范围严格限定于六类类别：C-ABI（extern "C" 跨模块结构与 D3D12 根常量布局）、Hardware-Layout（如 alignas(64) TelemetrySnapshot 缓存行对齐与非指针 uint64\_t CUdeviceptr 显存地址所有权）16、Polymorphic-Boundary（抽象虚接口及其跨层实现，如 IVideoFrameSink、IDemuxer、IEditCommand）5、NonTrivial-StateMachine（协调多资源异步退役、设备丢失或线程拓扑的宿主状态机，如 PreviewViewport、GpuRetirementQueue、MediaPlaybackSession、WasapiAudioOutput、NvidiaTranscodeSession）5、Concurrency-Invariant（无锁并发原语，如 Seqlock AtomicTelemetryBus）20 以及 Domain-Model（承载反射元数据或序列化契约的多字段核心模型，如 TranscodeProfile、MediaInfo）19。其二，凡属单资源所有权壳、仅含 1–2 个数值的纯算术包装器、3 字段以内的局部数据袋、或跨目录重复实现的等价模板，均按 Slop 垃圾代码立即删除1。其三，参照 Phase 9.1 零外部脚本闭环准则25，新增纯 CMake 原生脚本 cmake/AuditTypes.cmake，在构建与 CI 期扫描 include/ 与 src/ 下所有具名 class/struct 定义，一旦前 8 行缺失合法 @rtx-type-justification 声明或 Category 越出六类白名单即报错阻断。

## **二、 专项请示 2：playback\_session.cpp 标准库脱毒与消肿方案**

对长达 1,254 行的 src/core/playback\_session.cpp 实施手术时，必须打破“仅改写 4 个私有类”的局限，将清理范围同时覆盖文件内部匿名命名空间与 include/rtx/playback\_session.h 暴露的小粒度包装类型1。首先，匿名命名空间中的 ShimOwner 与 DecoderOwner 合并为两行 std::unique\_ptr 别名；AudioDecodeChain 拆解为四个 FFmpeg 原生对象 std::unique\_ptr 别名（AVCodecContext、SwrContext、AVFrame、AVPacket）加两个无状态自由函数 openAudioDecode() 与 drainAudioDecode()；VarispeedCursor 浓缩为单一 18 行自由函数 applyLinearVarispeed()（直接操作调用方持有的 std::deque\<float\> 尾迹缓冲与浮点游标，直至 Phase 11.3 由 WsolaStreamState 接替）3；AudioPacketQueue 则整体并入 MediaPlaybackSession::Impl 内的有界 std::deque\<Packet\> 与 std::condition\_variable\_any，不再单独定义类3。  
其次，彻底淘汰 include/rtx/playback\_session.h 中过度碎片化的 PlaybackClock、FrameDeadlineScheduler、DecodeAheadSlot 与 DecodeAheadQueue5。在 include/rtx/time\_model.h 新增标准时间类型 using Flicks \= std::chrono::duration\<int64\_t, std::ratio\<1, 705600000\>\>;26，利用 C++ \<chrono\> 在编译期将纳秒到 Flicks 精确化简为有理数比例 441 / 625，从而彻底删去 PlaybackClock::now() 中硬编码的 0.7056 浮点数魔法常数与 80 行时钟类3，改由 2 个自由函数操作状态元组完成软件时钟与 IAudioMasterClock 切换。FrameDeadlineScheduler 实质仅依据帧周期与其半值容差做阈值判定，直接简化为两个 constexpr 自由函数 classifyFrameDeadline() 与 waitDurationFlicks()3。DecodeAheadQueue 与 DecodeAheadSlot 则由 Impl 直接持有的 std::deque\<std::tuple\<RtxFrameDesc, int64\_t, uint64\_t\>\>（元素依次为帧描述、呈现截止 Flicks 与退役 Fence）配合容量上限整数变量取代3。tests/test\_playback\_session.cpp 对应的 4 组纯调度测试随接口签名同步改写为直测上述自由函数、Flicks 时钟原语与有界双端队列策略，保持 128 项用例全量不减1。  
再次，借重构之机一并根除跨线程音频锁竞争，将 MediaPlaybackSession::Impl 重组为标准库驱动的四层单线程亲和拓扑3。22 号验收记录显示，前期版本曾因 video\_worker 执行 seek() 与 audio\_worker 解码同时触碰 audio\_chain 与 varispeed 引发堆破坏，从而被迫引入 audio\_state\_mutex2。在脱毒版拓扑中，应彻底删除 audio\_state\_mutex，改为由 audio\_worker（std::jthread）独占拥有 FFmpeg AVCodecContext、SwrContext 与线性变速尾部状态；当 video\_worker（std::jthread）执行 performSeek() 时，仅清空音频包队列并投递一个携带递增 seek\_epoch 的空包哨兵，由 audio\_worker 检测到纪元变更后自行安全执行 avcodec\_flush\_buffers() 与缓冲复位。唯一保留的互斥量仅服务于控制面等待唤醒与队列背压阻塞，且借助 std::condition\_variable\_any::wait(lock, stoken, ...) 在会话析构时瞬间响应 std::jthread 自动发出的停止信号，不再需要手写 closed\_、stop 与双层销毁胶水3。

| 逻辑层次与线程亲和 | 采用的标准库原语与类型 | 职责范围与同步不变量 | 目标行数预算 |
| :---- | :---- | :---- | :---- |
| 公共控制接口与策略函数 | std::chrono::duration（Flicks）、constexpr 自由函数、std::unique\_ptr | 对外暴露 MediaPlaybackSession 工厂与播控方法，无锁读写 AtomicTelemetryBus 与原子 playhead\_5 | 约 110 行 |
| video\_worker 视频解码与呈示面 | std::jthread、std::stop\_token、std::deque\<std::tuple\<...\>\> | 独占 IDemuxer、DecoderPtr 与 CUDA 上下文；执行 GopSeekScheduler、rtx\_shim\_decoder\_reset 非终态复位、截止时间判定与 Fence 退役归还2 | 约 200 行 |
| audio\_worker 音频解码与喂料面 | std::jthread、std::condition\_variable\_any、FFmpeg std::unique\_ptr 别名 | 独占音频解码链与变速尾迹，响应 seek\_epoch 自复位（零跨线程锁），绝不等待 GPU 推理或 Fence，持续向 WasapiAudioOutput 喂料3 | 约 110 行 |
| WASAPI 硬件回调面（wasapi\_audio\_output.cpp） | std::jthread、std::stop\_callback、ComPtr\<T\>、std::out\_ptr\<void\*\> | 严守回调四条红线（零 I/O、零堆分配、零长锁、零同步 Fence Wait），仅执行环形缓冲拷贝与 IAudioClock::GetPosition() 原子发布17 | playback\_session.cpp 合计约 420 行（wasapi 同步缩减至约 350 行） |

## **三、 专项请示 3：视口域解耦与 rtx\_gui.cpp 瘦身方案**

针对暴力内联于 src/gui/rtx\_gui.cpp\#L113-L315 的 203 行私有类 PlaybackDCompSink1，专家组明确裁决：**既不留存于 rtx\_gui.cpp，也严禁将其迁入 src/gui/preview\_viewport；必须与 src/core/ngx\_remaster.cpp\#L85-L160 的全局静态预览注册表合并重构，收口为 ngx\_remaster.cpp 提供的统一无头预览适配器，全面实现核心与界面分离**1。  
源码穿透对照揭露了 PlaybackDCompSink 膨胀的本质原因：首先，src/gui/preview\_viewport.cpp 在 CMakeLists.txt 中同时由 CineRTX 与 rtx\_tests 编译链接，且仅负责 D3D12 Compute Shader 色调映射与 DirectComposition 交换链合成12；若将绑定 NvidiaShim、RtxNgxBridge 与 IVideoFrameSink 的逻辑塞入 preview\_viewport，不仅污染视口模块依赖边界，还会让本已达 1,029 行的 preview\_viewport.cpp 更加臃肿1。其次，PlaybackDCompSink 在 rtx\_gui.cpp 内将 ngx\_remaster.cpp 早已写好的 3 槽原子占坑标志（slot\_flags\_）、最新槽位发布序号（latest\_slot\_）以及 rtx\_ngx\_bridge\_borrow\_preview 调用又完整复制了一遍19。更关键的是，PlaybackDCompSink::ensureBorrowed() 额外维护了 Borrowed borrowed\_\[3\] 并手工调用 IUnknown::AddRef/Release，而 bridge\_exports.cpp 的 rtx\_ngx\_bridge\_borrow\_preview 本身只是 O(1) 返回桥接内已分配的常驻指针，且 PreviewViewport::presentShared() 在每次呈示时本就会执行 AddRef() 并压入 GpuRetirementQueue19。  
统一通道的具体落地分为三步：第一步，彻底删除 src/core/ngx\_remaster.cpp\#L85-L160 中的文件级全局变量 g\_active\_preview\_bridge、g\_preview\_slot\_flags 与 g\_preview\_latest\_slot1，消除全局隐式状态穿透1，转而在 include/rtx/ngx\_remaster.h 暴露调用方持有的实例槽位状态与无头预览 Sink 工厂 createHeadlessPreviewSink(const MediaInfo&, std::function\<void()\> on\_frame, BorrowCallback& out\_borrow)。第二步，在 ngx\_remaster.cpp 内部基于 ComPtr/std::unique\_ptr 持有 NvidiaShim 与无头 RtxNgxBridge（reserved\[0\] \= 1）19，将 MediaInfo 的 Full/Limited Range、色彩矩阵与 PQ/MaxNits 折算逻辑收归核心层，通过直接读取 rtx\_ngx\_bridge\_borrow\_preview 消除冗余的 Borrowed 缓存结构19。第三步，rtx\_gui.cpp 剥离全部 \#include "shim/ngx\_bridge\_abi.h" 与 \#include "rtx/nvidia\_shim.h"19，仅保留 std::unique\_ptr\<rtx::IVideoFrameSink\> 与标准回调；同时将重复的 presentPlaybackFrame() 与 presentTranscodePreview() 合并为单一 15 行 presentSlotToViewport()，并用 std::array 控件坐标表驱动重写 applyWorkspaceLayout() 与 WM\_CREATE 冗长 MoveWindow 样板，将 WorkstationApp 的 UniqueThread/UniqueBrush/UniqueFont 切至 std::jthread 与标准 std::unique\_ptr 别名19。同步对 preview\_viewport.cpp 与 d3d12\_raii.h 实施 ComPtr \+ std::out\_ptr\<void\*\>、单一 resourceBarrier() 辅助函数及统一 markDeviceLost() 瘦身（100% 保留 UINT64\_MAX 校验、全命令对象退役、Waiting 序与 reserved\[0\] 四项安全加固）2。经上述联合手术，rtx\_gui.cpp 将从 1,539 行压缩至 880 行左右（远低于 1,000 行红线）1，preview\_viewport.cpp 从 1,029 行回落至约 730 行，d3d12\_raii.h 收敛至百余行以内。

## **四、 专项请示 4：测试码流与二进制夹具的 CMake 自动化隔离标准**

针对 tests/fixtures/test\_streams.h 内 1,306 行十六进制字节数组（含大段 0x00 填充）对版本库源码树的严重污染，必须在 Phase 11.2-R 中将其物理移除（git rm tests/fixtures/test\_streams.h）1。尽管 C++26 标准包含 \#embed 二进制嵌入特性31，但截至 2026 年 11 月微软 MSVC Build Tools（14.50–14.53）仍未实现 \#embed 指令支持10，因此该路径在 Windows 主生产链不可行。结合 CineRTX 在 Phase 9.1 确立的纯 CMake 原生零脚本架构纪律25，专家组确定“物理资源构建投递”为主方案、“构建期原生生成”为备选方案的分级标准。  
首选工业级标准方案为**物理 .mkv 测试夹具 \+ CMake POST\_BUILD 复制 \+ \<fstream\> 注入 MemoryAvioContext**。首先将原先生成好的三段微型 Matroska 测试流（1.0 s / 25 fps / 320×240 H.264 High 8-bit、HEVC Main10、AV1 Main 10-bit 搭配 440 Hz AAC 音轨，且通过先封装无音轨 MKV 再合流避免 21 ms AAC priming 网格偏移）30 直接还原为实体二进制文件 tests/fixtures/h264\_25fps\_gop12.mkv、tests/fixtures/hevc\_25fps\_gop12.mkv 与 tests/fixtures/av1\_25fps\_gop12.mkv（每份仅数 KB，可用 ffprobe 直接审计）；经核查，AuditSpdx.cmake 仅扫描源码扩展名与禁止 .lib/.dll/.exe/.engine/.onnx 等构建或模型权重文件25，轻量开源自制 .mkv 媒体样本完全合规。随后在 CMakeLists.txt 中为 rtx\_tests 添加 POST\_BUILD 命令 cmake \-E copy\_if\_different 将 tests/fixtures/\*.mkv 自动同步至 \$\<TARGET\_FILE\_DIR:rtx\_tests\>/fixtures/，并在 tests/test\_playback\_session.cpp 与 tests/test\_raii\_retirement.cpp 中通过不足 10 行的标准库 std::ifstream(..., std::ios::binary \| std::ios::ate) 读入 std::vector\<uint8\_t\>，继续经由 makeMemoryDemuxer() 驱动 MemoryAvioContext 测试容器层（同步补强 A-02 真实小文件编解码中段失败注入压测）4。若项目团队额外要求 rtx\_tests.exe 可被孤立复制至空白目录且零外部文件运行，则可完全镜像 cmake/EmbedPtx.cmake 机制12，新增约 25 行纯原生 CMake 模块 cmake/EmbedBinary.cmake，在构建期利用 file(READ ... HEX) 将 .mkv 自动转换为 \${CMAKE\_BINARY\_DIR}/generated/test\_streams\_generated.h（与 preview\_tone\_map\_cs.h 同归构建目录，绝不进入 Git 源码树）12。两套方案均实现源码树零十六进制机器码，但首选物理 .mkv 方案兼顾最低编译内存开销与直接可读性。

## **五、 专项请示 5：Phase 11.2-R 专项整改波次批复与量化验收门禁**

### **5.1 Phase 11.2-R 分步施工序列与体积净增量预算**

专家组正式批准设立为期 **1.5–2 个工作日** 的 **Phase 11.2-R（Refactor & De-slop）** 强制整改波次，在所有准出指标达标前全面冻结 Phase 11.3 派单1。需要重点提示的是，若仅清除 test\_streams.h（-1,306 行）、playback\_session.cpp 私有类（约 \-800 行）与 PlaybackDCompSink（-203 行），合计仅能消减约 2,300 行，Phase 11.2 净增量仍将高达 \+3,500 行以上，无法满足 \<= \+2,000 行的硬指标1。因此必须同步执行 raii\_owners.h/com\_owner.h 清零、preview\_viewport.cpp 与 d3d12\_raii.h COM/屏障去重、rtx\_gui.cpp 表驱动压缩及测试冗余桩清理，具体施工序列与估算净行数账本（目标预算，待重构后以真实统计脚本核准）如下表所示：

| 施工步骤与覆盖文件 | 核心脱毒与标准库替换动作 | 目标净行数变化估算（相对现网 43,450 行） |
| :---- | :---- | :---- |
| Step R-1：底座清零与工具链适配（raii\_owners.h、com\_owner.h、wasapi\_audio\_output.cpp、d3d12\_raii.h、CMakeLists.txt） | 删除 com\_owner.h（-94 行）；raii\_owners.h 收缩为 FnDeleter \+ std::unique\_ptr 别名（385 → 约 55 行）；移除 WapiComOwner/ComApartment/MixFormatOwner/MappedResource/Entry/DeferredPool；为 Clang/Zig libc++ 按探测追加 \-fexperimental-library 以放行 std::jthread1 | 约 \-609 行 |
| Step R-2：播放内核与单测消肿（playback\_session.h/.cpp、time\_model.h、test\_playback\_session.cpp、test\_raii\_retirement.cpp） | 落地 Flicks std::chrono::duration 取代 0.7056 浮点3；清零 5 个私有类与 3 个公共小包装类，改为 std::jthread \+ std::condition\_variable\_any \+ std::deque \+ 自由函数，通过单线程亲和消除 audio\_state\_mutex3；精简冗余包装器测试桩并补齐 A-02 中段失败注入33 | 约 \-1,239 行（playback\_session.cpp 1,254 → 约 420 行） |
| Step R-3：视口解耦与界面瘦身（ngx\_remaster.h/.cpp、rtx\_gui.cpp、preview\_viewport.cpp） | 移除 ngx\_remaster.cpp 全局静态预览状态，落地唯一 createHeadlessPreviewSink1；从 rtx\_gui.cpp 拔除 PlaybackDCompSink、合并呈示函数并表驱动压缩布局（1,539 → 约 880 行）；preview\_viewport.cpp 改用 ComPtr \+ std::out\_ptr\<void\*\> \+ 统一屏障辅助函数（1,029 → 约 730 行）1 | 约 \-988 行 |
| Step R-4：十六进制测试夹具物理剥离（tests/fixtures/、CMakeLists.txt） | git rm tests/fixtures/test\_streams.h（-1,306 行），还原 3 份实体 .mkv 样本并配置 CMake POST\_BUILD 复制与 \<fstream\> 注入 MemoryAvioContext1 | 约 \-1,281 行 |
| Step R-5：存续声明标注与门禁脚本合入（新增 cmake/AuditTypes.cmake 及全仓注释声明） | 为全仓所有保留的具名 class/struct 补齐 @rtx-type-justification 声明块，合入并运行 AuditTypes.cmake 静态门禁 | 约 \+180 行 |
| Phase 11.2-R 合计目标净效 | 全仓手写源码由 43,450 行回落至约 39,513 行（相对 Phase 11.1 基线 37,626 行净增量压至约 \+1,887 行） | 净减约 3,937 行（单阶段净瘦身率约 67.6%，满足 ≥ 60% 且净增 ≤ \+2,000） |

### **5.2 Phase 11.2-R 八项硬性准出验收门禁**

在 23 号委托书拟定的 6 项准出指标之上1，专家组强制增设跨工具链构建回归（Gate R-7）与真实交互式桌面补测闭环（Gate R-8），八项门禁全部落盘实证方准合流进入 Phase 11.3：

| 门禁编号 | 核验维度 | 硬性量化准出标准与实证要求 |
| :---- | :---- | :---- |
| Gate R-1 | 全仓代码体量红线 | src/ \+ tests/ \+ include/ \+ cmake/ 手写源码总行数相对 Phase 11.1 基线（37,626 行）净增量 ≤ \+2,000 行（总行数 ≤ 39,626 行），相比当前 \+5,824 行瘦身率 ≥ 60%1 |
| Gate R-2 | playback\_session.cpp 纯洁度 | 文件内私有 class/struct 数量为 0（不含合法标注 @rtx-type-justification 的 Impl 与 Control），总行数 ≤ 500 行，且 0.7056 硬编码与 audio\_state\_mutex 彻底清零1 |
| Gate R-3 | GUI 与核心边界隔离 | rtx\_gui.cpp 中 PlaybackDCompSink 及底层硬件引用彻底清零，文件总行数 \< 1,000 行1；ngx\_remaster.cpp 全局静态 g\_active\_preview\_bridge 归零1；rg "runScrubPreviewAsync\|SetTimer\\(hwnd, 3, 66" src/gui/rtx\_gui.cpp 维持 0 命中2 |
| Gate R-4 | 夹具零十六进制污染 | tests/fixtures/test\_streams.h 已从 Git 索引移除，版本库源码与头文件中无任何硬编码二进制码流十六进制字节数组1 |
| Gate R-5 | 自定义类型 100% 审计 | cmake \-P cmake/AuditTypes.cmake 返回 0，全仓每一个留存具名 class/struct 均具备合法 @rtx-type-justification 声明且无单资源包装壳残留1 |
| Gate R-6 | 测试、包络与符号留存 | rtx\_tests.exe 保持 128/128 PASS（含真实 .mkv 中段失败注入 A-02 与更新后的调度测试，零 \[SKIP-GPU\]）1；AuditSpdx.cmake 与 AuditImports.cmake（PE 体积 1.2–12.0 MB、零泄漏 DLL）100% PASS21；CineRTX.exe.map 在 /OPT:REF /OPT:ICF 下留存核心播放与视口非内联修饰符号2 |
| Gate R-7 | 全预设跨工具链验证 | MSVC /MT 编译零错误零警告，zig-windows（Zig 0.13 \+ Ninja）与 linux-gcc CI 预设全部编译通过，实证 std::jthread 与 std::out\_ptr 无兼容阻断6 |
| Gate R-8 | 物理桌面实呈补课闭环 | 在驱动 621.24 WHQL 的 WTS Session ID \> 0 交互式桌面运行 CineRTX.exe1，补全 22 号报告缺失的现场实证：H.264/HEVC/AV1 播放与转码监看 presented \> 0 且 D2H \== 0 B、100 次 resizeSwapChain 无崩溃泄漏、HDR/SDR 跨屏切换色彩无压暗、及音频热拔插触发 AUDCLNT\_E\_DEVICE\_INVALIDATED 后无锁自动续播2 |

## **六、 下一步立即执行指令与 Phase 11.3 防反弹约束**

协调负责人应于收到本裁决后按以下原子顺序推进整改，确保不留二次返工死角：

> 1. 先行在 CMakeLists.txt 合入仅针对 Clang/Zig libc++ 的 \-fexperimental-library 探测并落地 cmake/AuditTypes.cmake 检查骨架，确立工具链与构建门禁红线8。  
> 2. 依序执行 Step R-1 至 Step R-4（标准化 raii\_owners.h 别名并销毁 com\_owner.h → 消肿 playback\_session.cpp/.h 与单线程音频去锁 → 合并 PlaybackDCompSink 入核心适配器并瘦身 rtx\_gui.cpp/preview\_viewport.cpp → 剥离 test\_streams.h 至物理 .mkv 夹具），同步完成全仓留存类型的 @rtx-type-justification 标注1。  
> 3. 在驱动 621.24 WHQL 与交互式桌面会话下跑通 Gate R-1 至 Gate R-8 全套自动化扫描与实机检验，形成独立《Phase 11.2-R 脱毒验收实证记录》归档至 docs/phase11/1。  
> 4. 仅当 Phase 11.2-R 八项门禁 100% 签字放行后，方可向外部工程师派发 19 号施工单（Phase 11.3）；且 Phase 11.3 的 NleProject、CompiledRenderGraph、mixAvx2() 与 WsolaStreamState 从首日开发起即强制受 AuditTypes.cmake 与 Std-First 契约硬拦截，严禁任何新 Slop 包装类或重复 JSON 扫描器入库1。

#### **引用的著作与仓内资源**

> 1. [`23_CineRTX Phase 11.2 代码膨胀与Slop重度污染治理专家咨询委托书.md`](23_CineRTX%20Phase%2011.2%20代码膨胀与Slop重度污染治理专家咨询委托书.md)  
> 2. [`22_CineRTX Phase 11.2 施工验收记录.md`](22_CineRTX%20Phase%2011.2%20施工验收记录.md)  
> 3. [`playback_session.cpp`](../../src/core/playback_session.cpp)  
> 4. [`test_playback_session.cpp`](../../tests/test_playback_session.cpp)  
> 5. [`playback_session.h`](../../include/rtx/playback_session.h)  
> 6. [`compliance.md`](../spec/compliance.md)  
> 7. [0.13.0 Release Notes The Zig Programming Language](https://ziglang.org/download/0.13.0/release-notes.html)  
> 8. [libc++ C++20 Status](https://libcxx.llvm.org/Status/Cxx20.html)  
> 9. [C API와 스마트 포인터의 소유권 전달은 C++23 std::out_ptr로 이렇게](https://kr-gamedev.tistory.com/149)  
> 10. [#embed in Visual Studio C++ : r/VisualStudio - Reddit](https://www.reddit.com/r/VisualStudio/comments/1qsvqz4/embed_in_visual_studio_c/)  
> 11. [C++23 Support in MSVC Build Tools 14.51 - C++ Team Blog](https://devblogs.microsoft.com/cppblog/c23-support-in-msvc-build-tools-14-51/)  
> 12. [`CMakeLists.txt`](../../CMakeLists.txt)  
> 13. [Microsoft C/C++ language conformance by Visual Studio version](https://learn.microsoft.com/en-us/cpp/overview/visual-cpp-language-conformance?view=msvc-170)  
> 14. [libc++ C++23 Status](https://libcxx.llvm.org/Status/Cxx23.html)  
> 15. [`com_owner.h`](../../include/rtx/com_owner.h)  
> 16. [`raii_owners.h`](../../include/rtx/raii_owners.h)  
> 17. [`wasapi_audio_output.cpp`](../../src/core/wasapi_audio_output.cpp)  
> 18. [`d3d12_raii.h`](../../src/gui/d3d12_raii.h)  
> 19. [`rtx_gui.cpp`](../../src/gui/rtx_gui.cpp)  
> 20. [`telemetry_bus.h`](../../include/rtx/telemetry_bus.h)  
> 21. [`21_CineRTX Phase11 施工方案深度审查.md`](21_CineRTX%20Phase11%20施工方案深度审查.md)  
> 22. [`preview_viewport.h`](../../src/gui/preview_viewport.h)  
> 23. [`hw_transcode_internal.h`](../../src/core/hw_transcode_internal.h)  
> 24. [`04_CineRTX Phase 11 冲突评判与方案.md`](04_CineRTX%20Phase%2011%20冲突评判与方案.md)  
> 25. [`CineRTX_Phase9.1_推送前仓库卫生完善与CMake全原生闭环专家裁决暨SuperPower施工规范.md`](../phase9/CineRTX_Phase9.1_推送前仓库卫生完善与CMake全原生闭环专家裁决暨SuperPower施工规范.md)  
> 26. [`time_model.h`](../../include/rtx/time_model.h)  
> 27. [`ngx_remaster.cpp`](../../src/core/ngx_remaster.cpp)  
> 28. [`bridge_exports.cpp`](../../src/shim/ngx_bridge/bridge_exports.cpp)  
> 29. [`preview_viewport.cpp`](../../src/gui/preview_viewport.cpp)  
> 30. [`test_streams.h`](../../tests/fixtures/test_streams.h)  
> 31. [State of C++ 2026 | The Dev Newsletter](https://devnewsletter.com/p/state-of-cpp-2026/)  
> 32. [`21_CineRTX Phase11 施工方案深度审查.md`](21_CineRTX%20Phase11%20施工方案深度审查.md)  
> 33. [`16_CineRTX Phase 11 演进计划.md`](16_CineRTX%20Phase%2011%20演进计划.md)  
> 34. [`gate3.md`](../spec/gate3.md)  
> 35. [`testing.md`](../spec/testing.md)