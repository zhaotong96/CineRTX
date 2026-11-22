# **CineRTX Phase 11 文档终审裁决与 SuperPower 工程施工方案**

经穿透比对 docs/phase11/ 下 00–03 号文档、GitHub main 分支 v0.2.0（Commit 52103a7）真实源码以及截至 2026 年 11 月的编译器与官方 SDK 一手规范，最终裁决如下：**03 号文档（《架构深度评审报告》）在事实准确性、底层物理约束与工程落地可行性上最为严谨，应确立为 Phase 11（v0.3.0）的核心架构评审基线**；01 号与 02 号文档成功纠正了 00 号文档的“版本时空倒错与技术幻想”，但仍残留了不存在的 D3D12 枚举、对 GPU 异步退役的盲区以及对驱动基线耦合的遗漏；00 号文档仅保留“工程精简、零拷贝播放器、多轨 NLE”三大战略方向，其具体技术论证不予采信1。  
针对您提出的两项核心工程意志，本方案不作模糊妥协，而是给出兼具确定性与前瞻性的施工裁决。第一，**关于 C++26 静态反射（P2996 \+ P3394）**：截至 2026 年 11 月，微软 MSVC（含 Visual Studio 2026 / Build Tools 14.50–14.53 Preview）**并未提供任何公开或实验性的 P2996/P3394 静态反射实现或编译开关**5——瓶颈不在于“是否允许开启实验选项”，而在于 MSVC 编译器前端尚无法解析 ^^ 与 std::meta::info。但据此完全推迟反射（01/02 号结论）同样过于保守；本方案采纳 03 号的双轨解耦路径，**在 Phase 11 正式设立独立的 GCC 16（-std=c++26 \-freflection）反射代码生成轨（Track 11B），由 P2996/P3394 直接驱动生成标准 C++23 描述符、编解码映射与 schema.json，并由现有 MSVC /MT 生产链无缝消费生成产物**，同时并行保留一条非阻断式的 clang-p2996（clang-cl）全链路探索分支2。  
第二，**关于强推 RAII**：全面支持并将 RAII 从 01/02 号停留在“删除 .reset() 与引入 ComPtr”的风格约束，**升格为覆盖五域异构资源与“GPU Fence 异步退役（GPU Resource Retirement）”的架构硬门禁**2。源码静态审计证实，当前 PreviewViewport::release() 与 resizeSwapChain() 在 WaitForSingleObject 超时后会忽略等待结果强行释放显存与交换链后备缓冲2，而 RemasterPipeline 在析构中仍在裸持设备指针并对锁页内存保留了 std::free() 兜底路径2。Phase 11 必须按“资源秩序 → 时间秩序 → 执行秩序 → 高阶算子”的依赖拓扑，拆分为 Track 11A–11E 五条垂直切片轨道稳步推进2。

## **一、00–03 号文档全景评价与多方冲突终审裁决**

四份文档完整映射了 CineRTX 在 Phase 10 刚经历“跨栈参数契约瘫痪与 PTX .version 9.0 紧急抢修”合龙之后、向 v0.3.0 迈进的认知收敛轨迹13。00 号文档以早期的 v0.1.0 为参照系去规划本已完成的 v0.2.0 目标，正文中夹杂未清洗的 AI 断字标记，并包含多处未经实证的带宽与显存断言1。01 号文档完成了关键的基线纠偏，构建了 R-01 至 R-08 的修订框架，并敏锐指出了“只有渲染视口、缺乏 WASAPI 音频主时钟播放内核”的结构性缺口1。02 号文档是最有价值的冲突审计底稿，挖出了 CMakeLists.txt 版本号覆写、param\_schema.cpp 内部 4 处手写重复以及 initRife 固定传入源分辨率等源码实证4。03 号文档则进一步完成了系统级物理约束穿透，揪出了 01/02 号共同遗漏的 D3D12 幻觉枚举、GPU Fence 超时强拆隐患、Flicks 绝对/增量语义混用、RIFE scale=0.5 ABI 断层以及 Video Codec SDK 13.1 驱动基线耦合等硬伤2。

| 冲突议题 | 00 号（原调研稿）立场 | 01 / 02 号（修订与评判稿）立场 | 03 号（深度评审报告）立场 | 终审裁决与施工准入定性 |
| :---- | :---- | :---- | :---- | :---- |
| **R-01 版本与文档基线** | 以 v0.1.0 为现状，要求修复 tools/、QP 0/24/26 漂移与正则刮削3。 | 锚定 v0.2.0（52103a7）；指出 CMakeLists.txt 仍标 0.1.0；01 记 i18n 为 125 项，02 提示需核对源码（125 vs 129）1。 | 源码实证 CMakeLists.txt 注入 VERSION 0.1.0 覆盖回退值；核实 i18n.h StrId::Count \== 125，仅 static\_assert 错误文本仍写 1132。 | **采纳 03 号**：Track 11A 同步修正 CMakeLists.txt 至 0.3.0、i18n.h 断言文本与 README，压缩归档 P1–P10 历史文档。 |
| **R-02 C++26 静态反射** | 立即在主线源码中用 P2996（^^、std::meta::info）重写 param\_schema.h3。 | 01 彻底否决并移入远期；02 承认 param\_schema.cpp 存在 4 处重复，建议用 FieldDescriptor 成员指针表过渡1。 | 核实 MSVC 14.53 Preview 仍无 P2996；主张把反射放在“代码生成边界”，用 GCC 16 P2996 生成，MSVC 消费生成产物2。 | **采纳 03 号并满足用户诉求**：启动 **Track 11B 独立 GCC 16 P2996/P3394 代码生成轨**，彻底消灭内部 4 处重复且零阻断 MSVC 发布链。 |
| **R-03 生命周期与 RAII** | 要求微内核贯彻 initialize() / execute() / shutdown() 三段式3。 | 坚决捍卫 create() 工厂绝对 RAII；02 指出 NvidiaTranscodeSession 手写 .reset() 与 RemasterPipeline、PreviewViewport 裸清理1。 | 升级为五类异构资源所有权体系；揭露 PreviewViewport 超时强拆 Fence 与 RemasterPipeline 析构混用 std::free() 隐患2。 | **采纳 03 号并全面强化**：绝不允许三段式回退；新增 **GPU Fence 异步退役门禁** 与百次失败注入测试。 |
| **R-04 VSR/FRUC 与显存** | 锁死 fg-first；称用“CUDA 13.4 VMM cuMemCreate”将 NvOFFRUC 与 TRT 显存别名化3。 | 保留双拓扑 \+ 自适应；指出 cuMemCreate 源于 CUDA 10.2，黑盒驱动不可别名；02 暴露 initRife 写死源尺寸1。 | 指出 scale=0.5 与 128 对齐尚未接入桥接 ABI，“75% 显存节省”缺乏实测；要求带滞后（Hysteresis）的多维 VRAM 预算调度2。 | **采纳 03 号**：保留双拓扑，不做黑盒 cuMemMap 别名；补齐 scale=0.5 ABI 与实测遥测矩阵，严禁拍脑袋阈值。 |
| **R-05 视口与播放器内核** | 误判现状为 4K RGBA16F D2H（错算为 1.5 GB/s）；推 D3D12\_COMMAND\_QUEUE\_PRIORITY\_LOW \+ DComp；忽略播放器内核3。 | 纠偏现状为 960×540（\~30 MB/s）；批准动态 dcomp.dll \+ 补齐 WASAPI RealtimePlayer，但沿用了 PRIORITY\_LOW 幻觉枚举1。 | 查明 D3D12 **不存在 PRIORITY\_LOW**；指出 CreateSwapChainForComposition 强制 FLIP\_SEQUENTIAL；区分“0 D2H”与“0 GPU copy”2。 | **采纳 03 号**：使用 NORMAL 队列 \+ 有限提交节奏；采用 FLIP\_SEQUENTIAL；将播放器调度与离线管线彻底解耦。 |
| **R-06 Flicks 时基数学** | 提出核心层降采样至 14,112,000 Hz（1/50），表内自承 44.1kHz 与 NTSC 不整除3。 | 刚性锁定原生 705,600,000 Hz（覆盖 414 年）；02 列出整除表证伪 14.112 MHz，要求迁移 workstation\_canvas.cpp1。 | 纠正“亚纳秒”误述（1 Flick ≈ 1.417 ns）；揭露 FlicksDriftCompensator 混用 B 帧乱序绝对 PTS 与有状态余数结转的语义 Bug2。 | **采纳 03 号**：锁定 705.6 MHz；分离绝对时间戳换算与增量余数结转；重建非整除/VFR/负 PTS 测试。 |
| **R-07 Video Codec SDK 13.1** | 声称用 AppTransZeroCopy 让 4K VSR 写入共享 CUarray 消除跨 Context 搬移3。 | 纠偏其仅限同尺寸直通转码；按 Seek / 分层 B 帧 / 宏块统计分级；02 提示核查 n13.0.19.1 固定版本1。 | 查实 13.1.15 **强制要求驱动 ≥ 610.0**（当前 n13.0.19.1 对应 R580）；指出 15/31 B 帧与 UHQ 有真实显存/FPS 代价2。 | **采纳 03 号**：v0.3.0 维持 R580/13.0 默认基线，先移植 GOP-Aware Seek 范式；13.1 高级特性走能力探测与实证选通。 |
| **R-08 工程实施节奏** | 一步到位承诺全栈终极 NLE \+ 全套 AI \+ 顶级音频矩阵，无测量口径3。 | 拆分为 11A–11D 四条“可并行”轨道，但未严格切分垂直切片与前置依赖1。 | 重组为 **11A–11E 五轨依赖拓扑**，要求先交付原生解码 RealtimePlayer 与 1V+1A 最小 NLE 切片，再叠加复杂算子2。 | **采纳 03 号**：遵循“资源秩序 → 时间秩序 → 执行秩序 → 高阶算子”的渐进垂直切片路线。 |

## **二、核心立场深度裁决：C++26 静态反射与异构 RAII 体系**

### **2.1 C++26 静态反射（R-02）：从“工具链死锁”转向“GCC 16 生成 \+ MSVC 消费”**

您期望在 Phase 11 使用 C++26 静态反射并愿意为此启用 MSVC 实验性功能，这一方向精准命中了当前代码库的内部冗余痛点。虽然 include/rtx/param\_schema.h 在 v0.2.0 中已经将跨端配置收敛为 20 项 kParamSchema2，但 src/core/param\_schema.cpp 内部仍在 parseProfileFromJson() 中手写了 20 余次 JSON 提取调用，在 profileToJson() 中手写了 20 个字段的格式化占位符，在 src/cli/main.cpp 中维持着冗长的 if-else 参数赋值链——每增删 1 个 TranscodeProfile 成员，C++ 后端内部仍需手工同步 4 处代码4。  
然而，阻碍这一设想在 MSVC 中直接编译的并非工程保守主义，而是编译器的客观交付状态。核查微软官方于 2026 年 11 月 13 日发布的《MSVC Build Tools Preview updates – November 2026》（Toolset 14.53 / 19.53.36930）以及 2026 年 10 月 16 日更新的 Microsoft C++ 语言一致性文档，**MSVC 迄今没有任何公开或实验性的 P2996/P3394 静态反射支持，也不存在可供开启的实验性标志**6。第三方行业追踪报告同样指出，主线 Clang 与当前固定为 0.13.0 的便携回退工具链 Zig 均不支持 P2996，截至 2026 年具备可用实现的编译器仅为 **GCC 16（-std=c++26 \-freflection）** 与 **Bloomberg 维护的实验性 clang-p2996 分支**2。  
如果为了在 src/core/ 主体源码中直接书写 ^^TranscodeProfile 而抛弃 MSVC，将立刻击穿 CineRTX 的 Windows 构建与分发底座。根目录 CMakeLists.txt 强制锁定了 MSVC 静态运行时 /MT（MultiThreaded$<$\<CONFIG:Debug\>:Debug\>）、/SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup 以及 /arch:AVX215；NVIDIA RTX Video SDK 静态存根库 nvsdk\_ngx\_s.lib 强依赖 MSVC CRT ABI，在非 MSVC 链下必须配合 ngx\_msvc\_crt\_compat.cpp 与影子归档库才能完成链接4；尽管有社区开发者验证过将 Bloomberg clang-p2996 源码编译为 clang-cl 搭配 libc++ 接入 Visual Studio 方案8，但行业评测同时指出当前实验性反射在 consteval 中途报错及 define\_aggregate 链接诊断上仍不成熟9，且全仓切换工具链极易触发 AuditImports.cmake 的 1.2–12.0 MB（1,258,291–12,582,912 字节）静态包络门禁与静态库 ABI 风险4。  
因此，**最优的工程解法是将“立即采用 P2996/P3394”与“强迫 MSVC 主编译链解析反射语法”彻底解耦**2。我们在 Track 11B 中建立一个不依赖任何 Windows SDK、CUDA、TensorRT 或 FFmpeg 头文件的纯标准 C++26 契约模型（schema/schema\_model.hpp）与独立的代码生成器（schema/reflection\_codegen.cpp），交由 GCC 16 以 \-std=c++26 \-freflection 编译运行2。生成器直接枚举 ^^ 反射成员与 P3394 \[\[=...\]\] 注解，输出标准 C++23 消费头文件（param\_schema.generated.hpp、param\_schema.generated.inc）以及 schema.json2。MSVC /std:c++latest 与 Zig 回退构建链只需常规包含生成的标准 C++23 产物即可运行，零编译阻断；待 MSVC 未来正式落地 P2996 之日，只需将生成器编译目标迁回 MSVC，即可零成本收口2。

### **2.2 绝对 RAII 升级（R-03）：建立五域异构所有权与 GPU Fence 退役不变量**

00 号文档要求微内核贯彻 initialize() / execute() / shutdown() 三段式显式生命周期，属于早已被 Phase 6 至 Phase 10 推翻的架构倒退，予以坚决驳回1。同时必须清醒认识到，**在异构 GPU 多媒体引擎中，绝对 RAII 的实质绝不只是“套用 unique\_ptr 并删掉析构函数里的 .reset()”**2。核验 v0.2.0 源码发现，当前代码库仍存在四处深层资源所有权与异步退役隐患，必须在 Phase 11 彻底根治：  
第一，**PreviewViewport 的 GPU Fence 超时强拆漏洞（严重安全缺陷）**。在 src/gui/preview\_viewport.cpp 中，release() 最多等待 WaitForSingleObject(fence\_event\_, 1000)，而 resizeSwapChain() 最多等待 100 ms；两处代码**均未检查返回值是否为 WAIT\_OBJECT\_0，在超时后仍继续执行 Unmap、Release 后备缓冲并销毁交换链与命令队列**2。若 GPU 因高负载或接近 TDR 临界点发生超时，CPU 会在 GPU 命令仍在飞行（in-flight）时撤走显存资源，直接诱发设备移除或驱动页错误2。类似地，CUDA 官方编程指南与外部资源互操作实践均指出，在异步 CUDA 流或图形操作尚未完成前销毁导入的 D3D12 外部内存对象会毒化上下文并导致 CUDA\_ERROR\_ILLEGAL\_ADDRESS21。  
第二，**RemasterPipeline 的裸设备指针与错误释放兜底**。src/core/hw\_transcode\_stages.cpp 的析构函数直接手工循环释放 anchor\_p010\_\[2\] 与 pinned\_host\_buf\_\[2\]，甚至在 cuda\_.mem\_free\_host 不可用时保留了 std::free(pinned\_host\_buf\_\[i\]) 的释放分支2。CUDA 锁页内存绝不可在匹配释放 API 缺失时混用 std::free()；同时 CUDA 释放要求关联的 CUcontext 处于当前绑定状态且 CudaDriverApi 模块尚未卸载，因此所有 CUDA 所有权类型必须绑定可重绑定的 Context/API 引用，且其成员声明位置必须晚于 session\_ 与 cuda\_2。  
第三，**NvidiaTranscodeSession 的析构顺序隐式维护**。目前该类虽已落实 private 构造函数与 static Result\<std::unique\_ptr\<NvidiaTranscodeSession\>\> create(...) 工厂，但析构函数仍手写了 8 行 .reset() 与 .close() 来硬维持销毁序4。一旦后续新增成员或构造中途失败触发栈展开，手写析构序将与声明逆序产生偏差2。  
第四，**PreviewViewport 的裸 COM 指针与局部清理闭包**。当前 create() 中充斥着十余个裸接口指针与局部 fail Lambda4。在严禁引入重型外部框架的前提下，必须在内部构造清晰的五域 move-only 所有权原语（CPU/标准库、Win32、COM/D3D12、CUDA、模型与桥接会话），并与 **GPU Fence 异步退役跟踪** 绑定2。

## **三、Phase 11 五大关键技术争议与物理约束纠偏**

在拆解施工单前，必须将 00–02 号文档中尚未对齐官方规格与实测约束的五处技术边界校准到位，防止施工人员按错误前提编写不可编译或高风险代码。  
**第一，DirectComposition 与 D3D12 队列优先级及交换链约束纠偏。** 00 号与 01 号文档均主张为视口开辟一条 D3D12\_COMMAND\_QUEUE\_PRIORITY\_LOW 命令队列1。经查 Microsoft Learn 官方 D3D12\_COMMAND\_QUEUE\_PRIORITY 文档，**D3D12 根本没有 LOW 优先级枚举，合法常量仅有 NORMAL (0)、HIGH (100) 与 GLOBAL\_REALTIME (10000)**（且 GLOBAL\_REALTIME 要求高权限且语义与降级隔离相反）2。工程方案应复用当前 D3D12Interop 按 CUDA LUID 匹配创建好的无头 ID3D12Device（通过显式的借用设备/渲染宿主接口向 GUI 暴露），新建一条 D3D12\_COMMAND\_QUEUE\_PRIORITY\_NORMAL 的独立图形队列，借助命令列表上限、Fence 节流与提交深度控制视口开销2。同时，依据 IDXGIFactory2::CreateSwapChainForComposition 官方契约，**合成交换链必须使用 DXGI\_SWAP\_EFFECT\_FLIP\_SEQUENTIAL（不支持 CreateSwapChainForHwnd 中的 FLIP\_DISCARD），且 Width/Height 不可传 0**，但可使用 DXGI\_SCALING\_ASPECT\_RATIO\_STRETCH 实现自适应比例缩放16。此外，从 NGX/CUDA 共享纹理（通过 D3D12\_HEAP\_FLAG\_SHARED \+ CreateSharedHandle \+ CUDA\_EXTERNAL\_MEMORY\_DEDICATED 互操作）向合成交换链后备缓冲写入时，仍可能经历 GPU 内部拷贝或色调映射 Pass，因此应严谨定义为“零主机像素回读（0 B/s D2H）”，而非物理意义上的完全零 GPU 拷贝2。  
**第二，Flicks 时基数学与 B 帧乱序 PTS 语义纠偏。** 1 Flick \= 1/705,600,000 s ≈ 1.417 ns（并非 00 号所称“亚纳秒”），在 int64\_t 下可表达约 414.22 年，并精确整除 24/25/30/48/50/60/120 fps、NTSC 24000/1001、30000/1001、60000/1001 及 44.1/48/96/192 kHz 音频周期2。00 号提出的 14,112,000 Hz 降采样方案虽然仍可整除 44.1 kHz（320 ticks，00 号表格自身误标为非整数），却直接破坏了对 30000/1001（470,870.4，余 12,000）、60000/1001（235,435.2，余 12,000）与 192 kHz（73.5，余 96,000）的整数周期性，必须彻底否决2。更重要的是，源码审查发现 src/io/ffmpeg\_io.cpp 将可能存在 B 帧解码重排（非单调）的绝对 PTS 直接送入带状态余数结转的 FlicksDriftCompensator::rescaleToFlicks()，而现有的 flicks\_drift\_24h 测试因恰好选用可被 705.6 MHz 整除的 29.97 时基，完全未覆盖非零余数与乱序场景2。Phase 11 必须在类型层面严格剥离“独立绝对时间戳精确换算（同时保留原始 SourceTimestamp PTS/DTS \+ Rational time\_base）”与“连续增量 Bresenham 余数结转”，并将 workstation\_canvas.cpp 中遗留的 \*\_ms 字段及写死 24fps 的 formatFlicksTimecode() 全面升级2。  
**第三，独立实时播放内核（RealtimePlayer）与离线管线的调度分离。** PreviewViewport 目前仅是 960×540 R10G10B10A2（约 3.96 MiB / 十进制 4.14 MB 显存，15 fps 节流下 D2H 约 29.66 MiB/s）的过渡预览组件，并非 00 号误算的“4K RGBA16F 1.5 GB/s（实际 3.71 GiB/s / 3.98 GB/s）GDI 绘制”，且当前仓库完全缺失真正的 IAudioClient/IAudioClock 播放调度内核2。离线 RemasterPipeline 追求最大吞吐并允许深流水阻塞，而 MediaPlaybackSession 必须按呈现截止时间（Deadline）调度——过期视频帧可丢弃，解码环必须有界，音频硬件时钟绝不允许阻塞等待 VSR 推理2。因此二者只能复用底层 GPU 算子与编解码抽象，**严禁将 RemasterPipeline 原样改造成播放器**2。  
**第四，VSR/FRUC 双拓扑调度与 RIFE scale=0.5 运行时 ABI 断层补齐。** 00 号断言“4K RIFE 激活张量超 3.8 GB 必然 OOM”缺少独立实测支撑，而 01/02 号关于 scale=0.5 “可节省 75% 激活显存”的表述同样只是像素面积缩小至 1/4 的理论推演（TensorRT Workspace、输出张量与 NGX 内部缓冲未必按同比例缩放）2。核查 src/core/hw\_transcode\_stages.cpp 可见，当前 initRemasterAndFgStage() 调用 initRife 与 getInterpBuffer 时仍固定传入源视频分辨率 session\_-\>info().width/height，且现有的 initRife(engine\_path, width, height) 按 64 像素对齐处理 shape，**scale=0.5（128 像素对齐与反射填充）尚未接入公开桥接 ABI 与运行时**2。Phase 11 必须先补全尺寸/scale 运行时契约，再基于 DXGI 本地显存预算、CUDA 空闲显存与 TensorRT 实测峰值构建带滞后防抖（Hysteresis）的调度器，取代未经验证的固定“88% 一刀切”阈值；同时保留 NVOFA 与 RIFE 的互斥按需初始化原则，绝不对黑盒管理工作区的 NvOFFRUC 做跨库 cuMemMap 物理页别名（cuMemCreate 早在 CUDA 10.2 即已存在，并非 CUDA 13.4 新特性）2。  
**第五，Video Codec SDK 13.1 驱动基线与性能代价的诚实评估。** 当前 .gitmodules 将 third\_party/nv-codec-headers 固定在 n13.0.19.1（对应 R580 驱动分支），而官方 ReadMe 与 nv-codec-headers 仓库均明确指出，**13.1.15 要求 Windows 与 Linux 最低驱动版本 ≥ 610.0 及 CUDA 13.1+ Toolkit**2。同时，尽管 NVIDIA 官方技术博客称 AV1 分层 B 帧在 15 帧下相比 HQ p7 几乎无额外性能代价、UHQ 时域滤波可在自然内容上平均带来约 4–5% 的编码增益，但 NVIDIA 自身文档亦明确指出深层分层 B 帧（如 15/31 帧）、UHQ 迭代与 4 级 Lookahead 会抬高显存消耗（按 02 号对 4K 3840×2176 P010 单帧 23.73 MiB 的等效容量估算，7 帧与 31 帧参考缓存即分别约合 166.11 MiB 与 735.64 MiB）并影响编码 FPS，且时域滤波要求 frameIntervalP \>= 5 并与超低延迟零重排模式互斥2。因此，v0.3.0 生产主线应**暂维持 R580 / 13.0 兼容基线**：先将不依赖 13.1 ABI 的 GOP-Aware 帧精确 Seek 算法范式移植入现有 IDemuxer/NVDEC 调度器；若后续决定升版至 R610 / 13.1，必须先经过 NvEncGetEncodeCaps（如 NV\_ENC\_CAPS\_SUPPORT\_BFRAME\_REF\_MODE）运行时能力探测与完整的实测性能矩阵验证2。

## **四、Phase 11 SuperPower 施工总方案（Track 11A–11E）**

吸取 Phase 10 曾发生过的“拆分文件时误将 slot\_free\_.wait 移至 remaster() 之后导致环形池踩踏”以及“删除单元测试断言掩盖 PTX .version 9.0 拒载”等教训14，Phase 11 放弃盲目的全面并行，改为按五轨强依赖拓扑 \+ 垂直切片（Vertical Slice）推进。所有新增及改动源码必须严格遵守 cmake/AuditSpdx.cmake 的许可证边界（include/shim/ 与 src/shim/ 为 MIT OR Apache-2.0，其余为 GPL-3.0-or-later WITH Plugin-Exception）及 cmake/AuditImports.cmake 的二进制闭包红线（1.2–12.0 MB 包络、零泄露 FFmpeg/NVIDIA 伴生 DLL）20。

| 施工分轨 | 优先级 | 前置依赖 | 核心交付物与垂直切片定位 |
| :---- | :---- | :---- | :---- |
| **Track 11A** | **P0** | 无 | 版本/文档收口、五域异构 RAII 所有权封装、GPU Fence 安全退役机制与百次失败注入压测。 |
| **Track 11B** | **P0** | 无（与 11A 并行） | GCC 16（-std=c++26 \-freflection）P2996/P3394 独立反射代码生成轨 \+ MSVC C++23 消费对接。 |
| **Track 11C** | **P1** | 11A | Flicks 绝对/增量语义分离迁移 \+ MediaPlaybackSession 垂直切片（WASAPI 主时钟 \+ 有界预解环 \+ GOP-Aware Seek，先实现原生解码回放）。 |
| **Track 11D** | **P1/P2** | 11A \+ 11B \+ 11C | 共享 D3D12 \+ 动态 dcomp.dll 视口、NleProject 模型、Preview/Export 统一 RenderGraph、1V+1A 最小 NLE 切片与 AVX2/SPSC/WSOLA 音频矩阵。 |
| **Track 11E** | **P2/P3** | 11A \+ 11D | RIFE scale=0.5（128 对齐）ABI、DXGI+CUDA 滞后 VRAM 自适应调度器、SDK 13.1 能力探测与实证基准矩阵。 |

### **Track 11A（P0）：基线元数据收口与异构 RAII 退役体系重构**

本轨目标是清扫 v0.2.0 元数据漂移，将五类异构资源与异步 GPU 生命周期全部纳入类型安全的所有权模型中，不留任何裸指针与超时强拆漏洞。

> 1. **修正仓库元数据漂移并瘦身历史上下文**：将根目录 CMakeLists.txt 的 project(CineRTX VERSION ...) 升级至 0.3.0 以消除 RTX\_VERSION\_STRING 对 CLI 版本号的覆写2；修正 include/rtx/i18n.h 中 static\_assert(kStrIdCount \== 125\) 仍残留 "113 strongly-typed entries" 的过时提示语2；同步更新 README.md 与 README\_zh.md 至 20 项 SSOT 参数、125 项词条、静态内联 RTX\_EMBEDDED\_NGX\_BRIDGE 与 WebUI v3 基线1；将 docs/phase1 至 docs/phase10 过程性返工单物理压缩归档至 docs/archive/HISTORY\_SUMMARY\_P1\_P10.md1。  
> 2. **落地五域 move-only 所有权原语**：在不引入重型外部依赖库与额外系统 DLL 的前提下，按资源语义分别封装轻量 move-only 拥有者（非拥有借用一律显式标注）：Win32 域落地 UniqueHandle、UniqueModule 与确认由视口独占创建的 UniqueHwnd；COM/D3D12 域在 src/gui/ 内落地极简 ComOwner\<T\> 与 MappedResource（析构保证 GPU 使用完毕后再 Unmap）；CUDA 域落地 CudaDeviceBuffer 与 CudaPinnedHostBuffer（严格记录分配 API 来源与可重绑定的 CUcontext 归属，**彻底废除对 CUDA 锁页内存回退调用 std::free() 的危险路径**），以及封装了异步同步约束的外部内存与信号量拥有者；会话与线程域落地 RifeSession 与 PipelineWorker（析构先发停止信号并唤醒队列完成 join，再允许任何缓冲区析构）2。  
> 3. **重排 NvidiaTranscodeSession 成员声明拓扑**：在 src/core/hw\_transcode\_internal.h 中按“被依赖者先声明、依赖者后声明”重排候选成员序（NgxRemasterBridge bridge\_loader\_ → NvidiaShim shim\_ → std::unique\_ptr\<IDemuxer\> demuxer\_ → BridgeHandle bridge\_ → DecoderHandle decoder\_ → EncoderHandle encoder\_ → std::unique\_ptr\<CpuVideoEncoder\> cpu\_encoder\_ → std::unique\_ptr\<IMuxer\> muxer\_），并移除 src/core/hw\_transcode.cpp 析构函数内手写的 8 行 .reset() 与 .close()2。落地时需结合真实 deleter 依赖图逐一复核 bridge\_、shim\_、decoder\_ 与 encoder\_ 的释放约束并写入架构测试2。  
> 4. **建立 GPU Resource Retirement 机制修复 PreviewViewport 超时缺陷**：重构 src/gui/preview\_viewport.cpp 的 release() 与 resizeSwapChain()，每次提交命令均绑定单调递增的 Fence 值；当等待 GPU 完成发生 WAIT\_TIMEOUT 时，**严禁直接继续释放后备缓冲或调用 ResizeBuffers**，而必须检查 GetDeviceRemovedReason()——若设备已丢失则进入受控的 Device-Lost 清理路径，若设备正常但 GPU 仍忙则将资源保留在延迟退役池中直至目标 Fence 真正完成2。

### **Track 11B（P0，与 11A 并行）：P2996/P3394 静态反射独立代码生成轨**

本轨目标是让 C++26 静态反射在 Phase 11 真正落地并消除 param\_schema.cpp 与 main.cpp 中的 4 处手工重复，同时保持 MSVC 与 Zig Windows 生产链 100% 稳定编译。

C++  
// schema/schema\_model.hpp (独立纯 C++26 契约定义：零 Windows/CUDA/FFmpeg/NGX 依赖)  
\#**pragma** once  
\#**include** \<string\>

namespace rtx::schema\_meta {

struct cli\_flag { const char\* name; };  
struct json\_key { const char\* name; };  
struct enum\_vals { const char\* csv; };  
struct int\_range { int min\_val; int max\_val; };  
struct normalized\_lower {};

struct TranscodeProfile {  
  \[\[=cli\_flag("--input"), \=json\_key("input")\]\]  
  std::string input{};

  \[\[=cli\_flag("--output"), \=json\_key("output")\]\]  
  std::string output{};

  \[\[=cli\_flag("--scale"), \=json\_key("scale"), \=int\_range{1, 4}, \=enum\_vals("1,2,4")\]\]  
  int scale \= 2;

  \[\[=cli\_flag("--pipeline-order"), \=json\_key("pipelineOrder"), \=enum\_vals("vsr-first,fg-first"), \=normalized\_lower{}\]\]  
  std::string pipeline\_order \= "vsr-first";

  \[\[=cli\_flag("--rate-control"), \=json\_key("rateControl"), \=enum\_vals("cq,crf,vbr,cbr,qp"), \=normalized\_lower{}\]\]  
  std::string rate\_control \= "cq";

  \[\[=cli\_flag("--qp"), \=json\_key("qp"), \=int\_range{0, 51}\]\]  
  int qp \= 22;  
  // ... 完整涵盖 20 项参数；后续新增字段仅改此单一事实源  
};

} // namespace rtx::schema\_meta

上述 schema/schema\_model.hpp 与独立生成器 schema/reflection\_codegen.cpp 共同构成反射隔离边界2。代码生成器使用 GCC 16（-std=c++26 \-freflection）编译，利用 ^^TranscodeProfile、\[: ... :\] 与 template for 在编译期遍历全部非静态数据成员及其 P3394 \[\[=...\]\] 注解，同时实例化一个规范默认对象直接读取运行时默认值（规避从复杂成员初始化器抽取表达式的脆弱性），并在编译期执行 static\_assert 校验每个成员注解完整性及 json\_key/cli\_flag 全局唯一性2。  
生成器运行后向外输出 generated/param\_schema.generated.hpp、generated/param\_schema.generated.inc 与 generated/schema.json2。主工程 src/core/param\_schema.cpp 与 src/cli/main.cpp 在现有 MSVC C++23 /std:c++latest 下直接消费生成产物，实现字段枚举、JSON 键映射、CLI 绑定与默认 Schema 导出的零手工重复2。必须明确边界：静态反射不代替跨字段业务规则校验（如 av1\_cpu 与 crf 联动、resolution=native 与宽高覆盖约束），也不代替底层 JSON 词法解析器的健全性审查（需单独覆盖字符串转义、嵌套对象与重复 Key 拒绝测试）2。CI 流水线在 Linux 上实时运行 GCC 16 生成器并对生成文件执行 Diff 一致性检查；同时可设一条非阻断式研究支线探索 clang-p2996（clang-cl）全链路兼容性，但不阻塞发布2。

### **Track 11C（P1）：Flicks 语义重构与独立 RealtimePlayer 垂直切片**

本轨依赖 Track 11A 的异构 RAII 底座，先建立严格的时间语义秩序，再交付不强绑 VSR/RIFE 的独立实时解码播放垂直切片，彻底补齐 Win32 Player 与 \+1 全屏快播体验1。

> 1. **拆分 Flicks 绝对换算与增量余数结转**：全仓锁定原生 705,600,000 Hz（废弃任何 14,112,000 Hz 降采样提案），保留 rational.cpp 中的 128 位 \_umul128/\_udiv128 运算2。在语义上将 FlicksDriftCompensator 彻底拆分为两类接口：用于可能受 B 帧重排的解码帧 PTS 的无状态绝对换算函数（保留源 SourceTimestamp{pts, dts, time\_base} 不丢失任意非整除 VFR 容器原始基准），以及专用于恒定采样/合成步进的有状态 Bresenham 余数结转器2。同步重构 formatFlicksTimecode() 以支持 23.976、24、25、29.97 DF/NDF、30、50、59.94、60 fps 全制式项目时间码，并将 workstation\_canvas.h/.cpp 中的 \*\_ms 时间轴字段迁移为 Flicks2。  
> 2. **搭建 MediaPlaybackSession 实时播放运行时**：严格与离线 RemasterPipeline 的调度控制面分离，内部解耦 DemuxScheduler、DecodeAheadQueue、AudioOutput (WASAPI)、PlaybackClock、FrameDeadlineScheduler 与 VideoRenderer2。DecodeAheadQueue 以 4–6 帧作为初始配置参数，但设计为依据帧间隔、解码耗时、VRAM 预算与倍速（0.25x–2x）动态约束的有界队列，每个槽位均绑定对应的 GPU Fence 退役值2。  
> 3. **实现 WASAPI 音频主时钟与 GOP-Aware 帧精确 Seek**：音频输出采用事件驱动共享模式 IAudioClient 投递 32-bit Float PCM，通过 IAudioClock::GetPosition()、GetFrequency() 与 QPC 时间戳外推换算为主时间轴（无音轨素材平滑退化至 steady\_clock）1。音频回调线程严守零文件 I/O、零动态堆分配、零长锁及零同步 GPU Fence 等待铁律，并完整捕获 AUDCLNT\_E\_DEVICE\_INVALIDATED 实现通过 IMMDeviceEnumerator::GetDefaultAudioEndpoint 的热拔插自动重连2。拖拽进度条与 ,/. 逐帧步进移植 Video Codec SDK 13.1 的 GOP-Aware Seek 范式：由 IDemuxer 索引定位至目标之前的最近 IDR（兼容 Open GOP），刷新解码器后仅解码恢复参考状态，对非目标中间帧跳过昂贵的映射与后处理，命中目标帧后再执行呈现2。状态同步采用核心层 alignas(64) TelemetrySnapshot 原子快照（memory\_order\_release/acquire）配合 GUI 30 Hz SetTimer 拉取，严禁高频 PostMessageW 阻塞消息队列1。

### **Track 11D（P1/P2）：DirectComposition 零回读视口与统一 NLE RenderGraph**

在 Track 11A–11C 稳定后，本轨按三个垂直切片依次落盘 DirectComposition 共享设备合成视口、版本化 NLE 领域模型以及 AVX2 多轨音频矩阵2。  
**垂直切片 A（DirectComposition 合成视口升级）** 首先在 src/shim/ngx\_bridge/d3d12\_interop.h 暴露清晰的借用设备接口，让 PreviewViewport 复用按 CUDA LUID 匹配的无头 ID3D12Device（淘汰独立的 ID3D12Device 与 960×540 D2H 拷贝），并开辟一条 D3D12\_COMMAND\_QUEUE\_PRIORITY\_NORMAL 的独立图形命令队列2。动态通过 LoadLibraryExW(..., LOAD\_LIBRARY\_SEARCH\_SYSTEM32) 加载 dcomp.dll（同步更新 cmake/AuditImports.cmake 与 preview\_viewport.h 白名单文档），采用 IDCompositionDevice 与 CreateSwapChainForComposition 创建配置 DXGI\_SWAP\_EFFECT\_FLIP\_SEQUENTIAL、非零宽高、DXGI\_SCALING\_ASPECT\_RATIO\_STRETCH 与 FRAME\_LATENCY\_WAITABLE\_OBJECT 的合成交换链，消除传统子 HWND 拖拽时的空域撕裂与闪烁1。跨屏移动或显示模式变化时调用 IDXGISwapChain4::CheckColorSpaceSupport：HDR 显示器直出 R16G16B16A16\_FLOAT（scRGB 线性空间）呈递 TrueHDR，SDR 显示器切至 R8G8B8A8\_UNORM 并通过独立 Compute Shader 执行 ACES Filmic 色调映射，绝不污染送往编码器的 4K 显存表面；同时将零拷贝纹理句柄与 split\_ratio 接入 EncoderJobQueue 补齐队列预览接线1。  
**垂直切片 B（独立 NleProject 模型与统一 RenderGraph）** 将时间线数据从 Win32 workstation\_canvas.h 剥离至跨端复用的核心引擎层，构建包含版本字段、稳定资产 ID 与重链接（Relink）元数据的 JSON 工程契约：NleProject → Sequence（Flicks \+ 有理数帧率）→ VideoTrack / AudioTrack / SubtitleTrack → Clip（源入出点、时间线 Flicks 起止、有理数倍速、特效栈）→ Transition2。撤销栈采用可逆 Command Pattern（如 InsertClip、TrimClip、SplitClip、MoveClip、DeleteClip），且严禁持有任何 GPU 句柄2。Preview 与 Export 共享同一份编译生成的 RenderGraph，质量配置仅可调节节点精度或跳过昂贵算子，绝不改变剪辑时间映射与转场时序，并在片段硬切边界自动下发 reset\_temporal \= true 与强制 IDR1。首期优先交付“新建/保存/重开 \+ 1V+1A 轨道 \+ 导入/移动/Trim/Split/Delete \+ Undo/Redo \+ 多段硬切导出”最小垂直切片，验收稳固后再叠加多轨混合与 Cross-Dissolve 转场2。  
**垂直切片 C（AVX2 多轨音频矩阵与 WSOLA 保调变速）** 将各轨道音频通过静态 libswresample 统一解码为 32-bit Float Planar PCM（48 kHz 或 96 kHz），采用 alignas(64) 隔离读写指针的无锁 SPSC 环形缓冲杜绝 MESI 伪共享，利用已启用的 /arch:AVX2（\_mm256\_fmadd\_ps）实现 8 路并行增益、声相、淡入淡出与总线防削波限幅，并在变速片段上接入纯 C++ WSOLA 重叠相加保调变速算子1。

### **Track 11E（P2/P3）：显存预算感知调度与 Video Codec SDK 13.1 分级接入**

本轨在最小播放器与 NLE 切片运行稳定后展开，确保高级 AI 算子与编码配置不拖垮核心交付：

> 1. **打通 RIFE scale=0.5 桥接 ABI 与尺寸契约**：修正 hw\_transcode\_stages.cpp 中 initRife 与 getInterpBuffer 固定读取源尺寸的错误假设2。在 ngx\_bridge\_abi.h 扩展 RIFE 配置契约以显式传递目标输入宽高、scale 因子（1.0 / 0.5）与 A=128 对齐及边缘 reflect101 填充裁剪逻辑，实测高速运动与画面边缘质量，并验证真实 TensorRT 峰值显存分配2。保留 NVOFA 与 RIFE 互斥初始化原则，不做跨库 cuMemMap 别名1。  
> 2. **落地带滞后防抖（Hysteresis）的多维 VRAM 调度器**：联合采集 DXGI 本地显存预算与占用、CUDA 空闲显存、TensorRT 峰值分配与解码/参考缓冲尺寸，在 vsr-first（NVOFA / RIFE 全分辨率 / RIFE scale=0.5）与 fg-first 或跳过插帧之间平滑切换，严防单帧边界抖动与 OOM2。  
> 3. **执行 SDK 13.1 基线决策与 Profile 实测矩阵**：v0.3.0 默认维持 n13.0.19.1（R580 基线）以确保广泛运行兼容性；若批准将最低驱动升版至 R610（≥ 610.0），则升级 nv-codec-headers 至 13.1.15，严格通过 NvEncGetEncodeCaps 运行时探测能力后开放 AV1 分层 B 帧（7 帧平衡候选；15/31 帧 \+ UHQ 迭代/Lookahead 作为高显存占用离线母版档，在 8GB 卡上实施深度保护），并在纯同尺寸直通子路径与切镜先验场景中独立评估 AppTransZeroCopy（CUarray 共享）及 NVDEC 宏块统计1。

## **五、SuperPower 量化验收门禁矩阵（Gate 11A–11E）**

所有轨道合入主干前必须逐项通过下表硬性门禁。凡涉及 FPS、编码器占用、显存节省或干扰率等性能数据，**必须完整记录 GPU 型号、驱动版本、CUDA/TensorRT/RTX Video SDK/Video Codec SDK 版本、输入素材规格、编码参数、预热时长、统计窗口与重复次数**；未附带实测口径的数字一律不得作为验收依据2。

| 门禁编号 | 所属分轨 | 量化验收标准与自动化验证动作 |
| :---- | :---- | :---- |
| **Gate 11A-1** | 11A | CineRTX.exe \--version、CMakeLists.txt 版本号、README 与发行包统一为 0.3.0；i18n.h static\_assert 文本对齐 125 词条；AuditSpdx.cmake 与 AuditImports.cmake（1.2–12.0 MB 包络与零泄露伴生 DLL）100% PASS2。 |
| **Gate 11A-2** | 11A | **RAII 失败注入与 GPU 退役门禁**：对 NvidiaTranscodeSession、RemasterPipeline、PreviewViewport 连续执行 100 次创建/销毁循环并在每步资源创建注入人工失败，验证零泄漏；在 GPU Fence 未完成（人为超时）时触发窗口关闭或 resizeSwapChain，验证不提前释放资源且 Device Lost 可控恢复；工作线程退出必先于缓冲区析构2。 |
| **Gate 11B-1** | 11B | **P2996/P3394 静态反射闭环门禁**：GCC 16（-std=c++26 \-freflection）成功编译运行 reflection\_codegen，当前 20 项参数全量由生成产物驱动；制造重复 json\_key/cli\_flag 触发编译期失败，新增测试字段仅改 schema\_model.hpp 一处，MSVC /std:c++latest 与 Zig 构建照常通过且 CI Diff 一致2。 |
| **Gate 11C-1** | 11C | **Flicks 精度与乱序测试门禁**：测试矩阵覆盖 23.976/29.97/59.94 fps、44.1/48/96/192 kHz、非整除任意 time\_base、VFR、B 帧重排 PTS、Seek 跳变与负 PTS，确认绝对换算与增量结转语义隔离、零累积漂移超界2。 |
| **Gate 11C-2** | 11C | **WASAPI 实时播放内核门禁**：H.264/HEVC/AV1 原生解码回放下，IAudioClock 主时钟稳定跟随；倍速、暂停、GOP-Aware Seek 与逐帧步进无死锁；播放中物理切换或拔出音频设备触发 AUDCLNT\_E\_DEVICE\_INVALIDATED 后可自动重连恢复；UI 线程零 GPU 阻塞2。 |
| **Gate 11D-1** | 11D | **DirectComposition 零回读视口门禁**：确认采用 NORMAL 队列与 DXGI\_SWAP\_EFFECT\_FLIP\_SEQUENTIAL；实证播放/监看路径 D2H 像素拷贝字节数为 0 B；HDR/SDR 跨屏切换与反复缩放 100 次无撕裂、黑边闪烁或显存泄漏2。 |
| **Gate 11D-2** | 11D | **NLE 垂直切片与音频门禁**：完成 1V+1A 工程保存/重开/重链接、Trim/Split/Move/Delete 与 Undo/Redo；预览与多段硬切导出逐帧映射一致；音频回调线程静态审查确认零阻塞/分配/I/O，AVX2 混音（含防削波）与 WSOLA 变速主客观测试达标2。 |
| **Gate 11E-1** | 11E | **VSR/FRUC 与 SDK 13.1 实证门禁**：验证 vsr-first \+ RIFE scale=0.5（128 对齐 \+ reflect101）无边缘伪影；同步记录 DXGI budget、CUDA 占用与 TRT 峰值验证滞后调度器 4K 压制无 OOM；输出 SDK 13.1 分层 B 帧（7 vs 15 vs 31）与 UHQ 实测矩阵（FPS、编码器占用、VRAM 峰值、质量对比），低版本驱动优雅回退2。 |

### **下一步立即执行动作**

> 1. **拍板产品驱动基线**：决定 v0.3.0 发布包维持 **R580 / nv-codec-headers n13.0.19.1** 兼容基线（仅先移植 GOP-Aware Seek 范式，将 13.1 高级特性置于可选实验开关之后），还是正式将用户最低显卡驱动门槛提升至 **R610（≥ 610.0）**2。  
> 2. **并行下发 P0 双轨施工单**：批准本方案后，立即在 docs/phase11/ 落盘 Track 11A（五域 RAII \+ GPU Fence 退役）与 Track 11B（GCC 16 P2996/P3394 代码生成轨）施工单并率先启动；待 Track 11A 资源秩序合龙后，立即切入 Track 11C 原生播放垂直切片与 Track 11D DirectComposition/NLE 核心建设2。

#### **引用的著作**

> 1. 01\_CineRTX\_Phase11\_演进方向调研\_专家修订终稿.md [受控工程档案]  
> 2. 03\_CineRTX\_Phase11\_架构深度评审报告.md [受控工程档案]  
> 3. 00\_CineRTX 演进方向调研.md [受控工程档案]  
> 4. 02\_CineRTX Phase 11 架构冲突深度评判.md [受控工程档案]  
> 5. C++26 Reflection: What It Actually Changes for Large Codebases, [https\://www\.wholetomato.com/blog/cpp26-reflection-large-codebases/](https://www.wholetomato.com/blog/cpp26-reflection-large-codebases/)  
> 6. [https\://devblogs.microsoft.com/cppblog/msvc-build-tools-preview-updates-november-2026/](https://devblogs.microsoft.com/cppblog/msvc-build-tools-preview-updates-november-2026/)  
> 7. [https\://learn.microsoft.com/en-us/cpp/overview/visual-cpp-language-conformance?view=msvc-170](https://learn.microsoft.com/en-us/cpp/overview/visual-cpp-language-conformance?view=msvc-170)  
> 8. Best way to try C++ 26 reflection on a Microsoft Visual Studio Solution, [https\://www\.reddit.com/r/cpp\_questions/comments/1td7foz/best\_way\_to\_try\_c\_26\_reflection\_on\_a\_microsoft/](https://www.reddit.com/r/cpp_questions/comments/1td7foz/best_way_to_try_c_26_reflection_on_a_microsoft/)  
> 9. Kernel Development \+ C++26 Reflection \= \- Stream HPC, [https\://streamhpc.com/blog/2026-07-08/kernel-development-c26-reflection-%F0%9F%A7%A1/](https://streamhpc.com/blog/2026-07-08/kernel-development-c26-reflection-%F0%9F%A7%A1/)  
> 10. Static Reflection in C++26: Generate Code at Compile Time, [https\://techietory.com/cpp26-static-reflection-compile-time-code-generation/](https://techietory.com/cpp26-static-reflection-compile-time-code-generation/)  
> 11. preview\_viewport.cpp [受控工程档案]  
> 12. hw\_transcode\_stages.cpp [受控工程档案]  
> 13. 15\_CineRTX\_Phase10\_全栈收官终审驳回裁决暨C++主干合龙SuperPower返工规范.md [受控工程档案]  
> 14. 19\_CineRTX\_Phase10\_转码全链路瘫痪根因法医排查与SuperPower抢修方案.md [受控工程档案]  
> 15. CMakeLists.txt [受控工程档案]  
> 16. DXGI\_SWAP\_CHAIN\_DESC1 (dxgi1\_2.h) \- Win32 \- Microsoft Learn, [https\://learn.microsoft.com/en-us/windows/win32/api/dxgi1\_2/ns-dxgi1\_2-dxgi\_swap\_chain\_desc1](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/ns-dxgi1_2-dxgi_swap_chain_desc1)  
> 17. [https\://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12\_command\_queue\_priority](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_command_queue_priority)  
> 18. nv-codec-headers/README at master \- GitHub, [https\://github.com/FFmpeg/nv-codec-headers/blob/master/README](https://github.com/FFmpeg/nv-codec-headers/blob/master/README)  
> 19. Read Me \- NVIDIA Documentation Hub, [https\://docs.nvidia.com/video-technologies/video-codec-sdk/13.1/read-me/index.html](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.1/read-me/index.html)  
> 20. AuditImports.cmake [受控工程档案]  
> 21. 4.22. CUDA Interoperability with APIs \- NVIDIA Documentation Hub, [https\://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/graphics-interop.html](https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/graphics-interop.html)  
> 22. cuImportExternalMemory fails after async memset on Vulkan/D3D12, [https\://github.com/shader-slang/slangpy/issues/929](https://github.com/shader-slang/slangpy/issues/929)  
> 23. [受控工程档案]  
> 24. IDXGIFactory2::CreateSwapChainForComposition method (dxgi1\_2.h), [https\://learn.microsoft.com/en-us/windows/win32/api/dxgi1\_2/nf-dxgi1\_2-idxgifactory2-createswapchainforcomposition](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgifactory2-createswapchainforcomposition)  
> 25. DXGI\_SCALING (dxgi1\_2.h) \- Win32 apps \| Microsoft Learn, [https\://learn.microsoft.com/en-us/windows/win32/api/dxgi1\_2/ne-dxgi1\_2-dxgi\_scaling](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/ne-dxgi1_2-dxgi_scaling)  
> 26. D3D11 CreateSwapChainForHwnd fails with either ... \- Stack Overflow, [https\://stackoverflow.com/questions/58586223/d3d11-createswapchainforhwnd-fails-with-either-dxgi-error-invalid-call-or-e-inva](https://stackoverflow.com/questions/58586223/d3d11-createswapchainforhwnd-fails-with-either-dxgi-error-invalid-call-or-e-inva)  
> 27. \[Feature Request\] D3D12 External Resource Interop API for Plugin, [https\://github.com/microsoft/onnxruntime/issues/26821](https://github.com/microsoft/onnxruntime/issues/26821)  
> 28. FFmpeg/nv-codec-headers: automatic mirror of https\://code ... \- GitHub, [https\://github.com/FFmpeg/nv-codec-headers](https://github.com/FFmpeg/nv-codec-headers)  
> 29. Bundled ffmpeg requires NVIDIA driver \>= 610.00 (SDK 13.1, [https\://github.com/yourcove/cove/issues/191](https://github.com/yourcove/cove/issues/191)  
> 30. Using FFmpeg with NVIDIA GPU Hardware Acceleration, [https\://docs.nvidia.com/video-technologies/video-codec-sdk/13.1/ffmpeg-with-nvidia-gpu/index.html](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.1/ffmpeg-with-nvidia-gpu/index.html)  
> 31. [https\://developer.nvidia.com/blog/nvidia-video-codec-sdk-13-1-zero-copy-transcode-av1-b-frames-and-frame-accurate-seek/](https://developer.nvidia.com/blog/nvidia-video-codec-sdk-13-1-zero-copy-transcode-av1-b-frames-and-frame-accurate-seek/)  
> 32. NVENC Video Encoder API Programming Guide, [https\://docs.nvidia.com/video-technologies/video-codec-sdk/13.0/nvenc-video-encoder-api-prog-guide/index.html](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.0/nvenc-video-encoder-api-prog-guide/index.html)  
> 33. nv-codec-headers/include/ffnvcodec/nvEncodeAPI.h at master, [https\://github.com/FFmpeg/nv-codec-headers/blob/master/include/ffnvcodec/nvEncodeAPI.h](https://github.com/FFmpeg/nv-codec-headers/blob/master/include/ffnvcodec/nvEncodeAPI.h)  
> 34. AuditSpdx.cmake [受控工程档案]  
> 35. GitHub \- mirror-bridge/mirror\_bridge: A reflection-based approach, [https\://github.com/FranciscoThiesen/mirror\_bridge](https://github.com/FranciscoThiesen/mirror_bridge)  
> 36. IAudioClock::GetPosition method (audioclient.h) \- Microsoft Learn, [https\://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclock-getposition](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclock-getposition)  
> 37. IAudioClock2::GetDevicePosition (audioclient.h) \- Win32 apps, [https\://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclock2-getdeviceposition](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclock2-getdeviceposition)  
> 38. Recovering from an Invalid-Device Error \- Win32 apps, [https\://learn.microsoft.com/en-us/windows/win32/coreaudio/recovering-from-an-invalid-device-error](https://learn.microsoft.com/en-us/windows/win32/coreaudio/recovering-from-an-invalid-device-error)