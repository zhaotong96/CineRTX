# **CineRTX Phase 11.3-S 终审验收裁决暨 Phase 12 极限现代 C++ 重构与 DLL 净空演进方案**

针对项目组呈报的 45 号终审验收委托书（CINERTX-P11-DELEGATION-045），首席领域专家组对照 42 号终审裁决、43 号施工单、44 号施工汇报及工作区实际落盘源码、构建脚本与门禁产物完成了逐行穿透核验，现签发终审裁决：**Phase 11.3-S 裁定为「有条件通过并暂缓纯净战略封板（CONDITIONAL PASS / HOLD CLEAN STRATEGIC SEAL）」**1。项目团队在底层现代 C++23 算子与硬件闭环上取得了实质性进展——MSVC Debug CRT \_CrtSetAllocHook 实证混音与 WSOLA 热循环零堆分配、Release /OPT:REF /OPT:ICF 下核心符号进入 .map、RTX 5060 真机 25 帧解码转 58 帧 10-bit HEVC 实现 D2H \== 0 B 显存闭环，以及全量 135 项单元测试 100% 通过1；然而源码穿透同时揭示出 **5 项未闭合的产品级与合规级硬伤**：onNleExportSequence() 内存在专门应付 .map 审计的 toJson/fromJson 空转调用且未将编译出的 AudioGraphSegment 接入导出管线、CMakeLists.txt 仍残留大批冗余选项并违背开源隔离红线将专有 nvsdk\_ngx\_s.lib 静态编入 GPL 主程序、AuditSloc.cmake 沿用过期基线且 Pure SLOC 倒挂增至 32,317 行、wsola\_stretch.cpp 存在三声道及以上音频静默归零与每步内存搬移缺陷，以及委托书缺失 43 号单 S-3.3 要求的 WTS Session \> 0 桌面 presented \> 0 与 ffprobe 多段 .mkv 逐帧核验物证1。  
基于上述物理证据，专家组批准在 **48 小时内完成 11.3-S 尾差止血与桌面补证后立即转入 Phase 11.4（20 号施工单，封板 v0.3.0）**1，并同步确立 **Phase 12 极限现代 C++ 纯化与系统依赖净空总方案**。下一版 Phase 12 将以奥卡姆剃刀与“标准库第一铁律（Std-First）”彻底重塑代码卫生：对 AuditTypes 在册的 153 个具名类型实施零容忍清剿，凡 std::variant、std::tuple、std::optional、std::expected、std::span、std::mdspan、std::move\_only\_function、std::flat\_map 与 Deducing this 可表达者一律物理消灭，留存类型必须重建真实不可替代性证明1；同时拔除全仓全局可变状态、冗余防御、隐藏开关与编译选项债，并对 {shell32, dwmapi, gdi32, user32, comdlg32, comctl32, uxtheme, d3d12, dxgi, bcrypt, ole32, kernel32, advapi32, kernelbase} 共 14 项 DLL 实施精准剥离——在单 EXE 混合形态下将静态导入表（IAT）压缩至 **4 项（kernel32.dll、user32.dll、gdi32.dll、bcrypt.dll）**（须经 dumpbin /imports 确认无静态库残留 advapi32 符号），并在 Phase 12 核心/界面解耦后将纯无头 CineRTX.exe 引擎静态 IAT 进一步净空至 **2 项极简底座（kernel32.dll \+ bcrypt.dll）**13。

## **一、 45 号委托书与 Phase 11.3-S 源码穿透验收裁决**

逐行比对 45 号委托书呈报的交付物证与仓内源码实体，可以确认底层 NLE 代数和模型、离线资产拦截以及 AVX2+FMA 矢量音频通路已按预期成型，但终端接线与构建脚本中仍留有拼凑痕迹与合规越界1。

| 验收维度 | 45 号委托书呈报结论 | 源码与实测穿透复核事实 | 终审定性 |
| :---- | :---- | :---- | :---- |
| 断链 1–4 产品接线与编辑完整性 | 声称打通多段导出、离线拦截、A1 切片、流式 WSOLA 与 GUI 快捷键1 | onNleExportSequence() 调用了 compileAudioRenderGraph() 却仅将其 .size() 拼入状态提示字符串，未向 runNvidiaRemasterRenderGraph() 传递 AudioGraphSegment；废弃函数 exportSelectedClipToEncoder() 仍残留；addFilesToBin() 分两次调用 nleAddClip() 插入 V1 与 A1，导致单次 Ctrl+Z 仅撤销 A1 而撕裂音画8 | **PARTIAL PASS**（音轨导出与原子撤销未闭环） |
| .map 符号留存真实性 | 声称 /OPT:REF /OPT:ICF 下 toJson/fromJson 等 5 组符号 100% 自然留存1 | workstation\_canvas.cpp 在导出成功后硬塞入无副作用假调用 const std::string proj\_json \= snapshot.toJson(); (void)rtx::NleProject::fromJson(proj\_json); 防止链接器裁掉符号，注释虽称“写入工程侧车校验”却未落盘任何文件，GUI 至今缺少真正工程保存与打开功能8 | **FAILED**（伪调用规避死代码审计） |
| Debug 零分配与真机转码 | 捕获 audio\_mixer/wsola 零堆分配日志；RTX 5060 实测 decoded=25, encoded=58, D2H=0 B1 | Flicks 数学复算精确自洽：25 fps 第 25 帧 PTS 为 24 × 28,224,000 \= 677,376,000 Flicks，按 60 fps 步长 11,760,000 产出 floor(677,376,000 / 11,760,000) \+ 1 \= 58 帧；但该数据实为单文件 25 帧硬件转码，缺 43 号单 S-3.3 要求的 WTS Session \> 0 preview\_viewport\_100x presented \> 0 日志及 ffprobe 多段 .mkv 硬切 IDR 与 PTS 逐帧校验1 | **单段实测 PASS / 多段与桌面证据 MISSING** |
| 构建清洗与开源合规边界 | 声称完成 CMakeLists.txt 深度脱毒（S-2.3）且四门禁全绿1 | CMakeLists.txt 仅清理了重复 RTX\_EXTERNAL\_DIR、限定 /O1 至 CineRTX 并增加 nvsdk\_ngx\_s\_dbg.lib；S-2.3 明令删除的 5 目标重复 CXX\_STANDARD 属性、rtx\_ngx\_bridge\_static 重复 SYSTEM PRIVATE 头路径、空 if(WIN32) endif() 与 rtx\_ngx\_shadow\_archives 均未删除；更关键的是 rtx\_core 直接静态链接含 nvsdk\_ngx\_s.lib 的 rtx\_ngx\_bridge\_static，违背 compliance.md 严禁在 GPL 主程序链接线上出现 nvsdk\_ngx\* 专有库的红线（CineRTX.exe 亦因此从 11D 的 4,429,312 字节膨胀至 5,217,792 字节，净增 788,480 字节）1 | **FAILED**（合规越界且清洗未兑现） |
| SLOC 双轨账本口径 | 声称 Pure SLOC 32,317 \<= 39,626 且对比历史峰值净减 6,245 行1 | AuditSloc.cmake 未按 S-2.4 校准为 Commit 749fe0d 基准 31,064；32,317 行较 11.3-R（31,770 行）再增 547 行，较 749fe0d 净增 1,253 行，超出 39 号单 \<= 30,664 红线 1,653 行（逼近初版被驳回的 32,380 行），引用 Phase 11.2-R 的 38,562 / 39,626 旧口径属于混淆记账1 | **FAILED**（体量倒挂与口径漂移） |

对核心算法实现的进一步走查还暴露出多处增加生产风险与维护心智负担的隐蔽缺陷。在 src/core/wsola\_stretch.cpp 中，WsolaStreamState 将 overlap\_tail 硬编码为 512 个 float 元素，而 wsolaProcessStream() 首部计算 max\_tail\_frames \= state.overlap\_tail.size() / ch\_count 并要求 max\_tail\_frames \>= kWsolaHalf（256 帧），这意味着一旦输入 3 声道及以上音频（如 5.1 或 7.1 环绕声），512 / channels \< 256 会在到达 1.0x 直通分支之前直接返回 0，造成多声道回放静默失声7。同文件在每次推进窗口时通过 std::copy\_n 将剩余环形样本整体左移，在 0.25x 慢放时每消费 64 帧就搬移近千帧数据，完全可以通过 C++20 std::shift\_left 或真正的模环形读指针实现零搬移；同时 corrAt() 仅采样第 0 声道能量且采用 512 样本短窗（48 kHz 下仅 10.67 ms），softLimitSample() 实为硬削波 std::clamp 却沿用了软限幅误导命名7。此外，src/cli/main.cpp 中仍残留 std::thread::detach() 裸分离线程、依赖异常的 std::stoi、if (target\_fps\_set && target\_fps.num \> 0\) {} 空语句块以及错位粘贴到 \--build-rife-engine 分支的分数帧率注释18。

## **二、 极限现代 C++20/23/26 准则与自定义类型零容忍清剿矩阵**

极致的现代 C++ 架构必须牢牢锚定生产编译链的实证能力边界，绝不为了追逐尚未落地的提案名词而牺牲跨编译器一致性或引入实验性宏税。截至 2026 年 11 月，ISO C++26 国际标准草案（DIS）在 10 月成员国投票中因 Contracts（pre/post/contract\_assert）争议未达三分之二通过门槛而遭否决，并于 11 月巴西 Búzios 全会重新定稿准备二次发起 DIS 投票19。核对 CineRTX 主力编译器实况，Phase 12 必须严格划定如下特性准入与规避红线12：

| 特性与提案层级 | MSVC 19.51 / 14.52–14.53 实况 | GCC 16.1 / Clang 22.1 实况 | Phase 12 工程准入与规避裁决 |
| :---- | :---- | :---- | :---- |
| C++20 基础设施（ranges, span, jthread, stop\_token, format, concepts, \<numbers\>, \<charconv\>, shift\_left） | 全面生产级支持（14.50+ 亦已配齐 CMake 4.x 与 SLNX 工具链）12 | 全面支持（脱离旧版 Zig 0.13 \-fexperimental-library 妥协）5 | **全仓强制底座**：替换全仓手写 snprintf、\_snwprintf\_s、std::stoi、分离 std::thread 与手写循环拷贝18 |
| C++23 核心消肿武器（expected, mdspan, move\_only\_function, flat\_map, Deducing this, static operator(), to\_underlying） | 14.50/14.51 STL 已就绪 expected、mdspan、move\_only\_function、flat\_map；14.52 解决了 19.51 下 /arch:AVX2 /O2 对 std::hash\<float\> \+0.0f/-0.0f 哈希不等缺陷，并在 Preview 更新中推进 /std:c++23 与模块完善12 | 全面支持 | **主力重构原语**：清零残留 std::function（含 hw\_transcode\_internal.h）与 \#ifdef \_\_cpp\_lib\_mdspan 回退宏；用 std::flat\_map 取代小表线性/哈希查找；禁止用裸 float 作无序容器键；升级工具链至 14.52+ 以消除 std::mdspan 的 C5246 误报3 |
| C++26 受限与前沿特性（std::inplace\_vector P0843、std::function\_ref P0792、静态反射 ^^ P2996、\#embed、Contracts） | 截至 14.51–14.53 仍未公开支持 P2996 静态反射与 \#embed；std::inplace\_vector（STL\#4766）与 std::execution（STL\#4768）仍在推进；std::out\_ptr 缺 operator\* 不适配 IID\_PPV\_ARGS12 | GCC 16.1 已合并 P2996 反射、inplace\_vector、function\_ref 等，但跨编译链不可移植26 | **拒绝体外代码生成与幻想语法**：坚持单链 C++23 constexpr 成员指针内生反射，绝不复辟 GCC 16 体外生成器；定容栈数组保持 std::array \+ std::span，仅在 \_\_cpp\_lib\_inplace\_vector 宏就绪后条件切入；COM 出参继续采用自由函数两段式收养或显式裸指针 reset12 |

依据“凡标准库可表达者绝不自建类”的极限洁癖，对 45 号委托书上报的 **153 个 AuditTypes 具名类型**进行穿透排查，发现尽管 Phase 11.3-R 清退了 OOP 命令类与 FormatMapping，存量代码中依然寄生着大批伪结构体、已死亡占位类型与被复制粘贴的虚假契约掩盖的数据袋1：

| 现存自定义类型及锚点 | 源码级洁癖诊断 | Phase 12 清剿裁决 | 标准库精确替代方案 |
| :---- | :---- | :---- | :---- |
| DynamicVsrTarget 与 ViewportVsrTarget（workstation\_canvas.h）29 | 仅含 4 个标量且自身契约写明 vsr\_active 等价于 scale \> 1.0（布尔字段冗余），同时附带无意义别名及全局静态 active\_vsr\_target()29 | **物理消灭（-2 类型）** | 改为 std::optional\<std::tuple\<int, int, double\>\>（无值即未激活），连带拔除全局 active\_vsr\_target() 及其 3 个包装函数 |
| ProbedMedia（workstation\_canvas.h）29 | 3 个几何/时长字段外加 bool ok，典型 C 风格状态袋29 | **物理消灭（-1 类型）** | probeMediaFile() 直接返回 std::optional\<std::tuple\<int, int, int64\_t\>\> |
| PingPongSlot（d3d12\_interop.h）39 | D3D12Interop 已改用 InteropTexture 与 SharedBufferSlot，既不持有也不返回 PingPongSlot，该结构仅凭粘贴来的 NonTrivial-StateMachine 注释占坑39 | **物理消灭（-1 类型）** | 核实 src/shim/ngx\_bridge/ 全目录无引用后整段删除 |
| InteropTexture、SharedBufferSlot（d3d12\_interop.h）39 | 没有任何析构函数或状态迁移方法，却全篇复制粘贴“协调多资源异步生命周期、成员声明序即析构逆序”的虚假注释39 | **合并降维（-1\~2 类型）** | 将裸 void\*/HANDLE 收归 FnDeleter / UniqueHandle 组成的 std::tuple 内部托管，或降格为局部硬件映射别名 |
| D3D12Api、CudaApi（d3d12\_interop.cpp）、CudaDriverApi（nvof\_engine.cpp）28 | 3 个匿名命名空间内的内部动态函数表，却全部谎称 Category: C-ABI（“extern 'C' 跨模块导出... append-only”），且多处重复绑定 nvcuda.dll28 | **清退与统合（-2\~3 类型）** | 销毁伪 C-ABI 壳，收归统一的无状态 rtx\_ngx 内部函数指针加载助手与 std::tuple 绑定表，消除 3 份重复的 cuMemcpy2DAsync\_v2 解析逻辑 |
| RenderGraphSegment、AudioGraphSegment（render\_graph.h）40 | 前 5 个字段完全同构，仅末尾 (hard\_cut, force\_keyframe) 与 gain 相异40 | **合并（-1 类型）** | 合一为携带 std::variant\<std::pair\<bool, bool\>, float\> 的统一切片结构，或保留纯区间元组 |
| Options（main.cpp）18 | 匿名命名空间局部结构却贴着“schema/测试逐字段锚定”虚假契约；将 Mode 枚举与互斥路径字段平铺共处，制造非法状态组合18 | **物理消灭（-1 类型）** | parseOptions() 直接返回 std::variant CLI 指令变体，使每个模式只携带自身必需参数 |
| NgxD3D12Bridge（ngx\_d3d12\_bridge.h）38 | 没有任何虚函数与继承基类，却错误标注为 Category: Polymorphic-Boundary38 | **纠正契约** | 重签为真实 NonTrivial-StateMachine 并清理冗余重复字段（如 3 组平滑双精度镜像与状态标志） |

完成上述清剿后，Phase 12 全仓准许存续的自定义类型必须严格收敛至六类合法白名单，并彻底禁用批量复制的模板化注解11：第一类 **C-ABI** 仅限 ngx\_bridge\_abi.h、rtx\_shim\_abi.h、EmbeddedShimApi 与驱动结构体（CUDA\_MEMCPY3D），因其字段布局、填充与调用约定构成不可变更的二进制边界10；第二类 **Hardware-Layout** 仅限 CudaDeviceBuffer、TelemetrySnapshot 与着色器根常量，因非指针 CUdeviceptr（uint64\_t）不可混同主机指针且缓存行/根签名受物理寄存器约束11；第三类 **Polymorphic-Boundary** 严格限定于真正含纯虚函数的抽象契约（IVideoFrameSink、IDemuxer、IMuxer、IAudioMasterClock），单一可调用对象无法表达多方法虚契约与测试桩替换12；第四类 **NonTrivial-StateMachine** 保留 PreviewViewport、GpuRetirementQueue、MediaPlaybackSession、WasapiAudioOutput、D3D12Interop、NgxD3D12Bridge、NvofEngine、VramAdaptiveScheduler 与 WsolaStreamState，以维护工作线程 join、GPU Fence 异步退役或 32 字节对齐环形状态机的严格析构拓扑10；第五类 **Concurrency-Invariant** 保留 AtomicTelemetryBus 等依赖内存序屏障的无锁 Seqlock 原语11；第六类 **Domain-Model** 仅限承载持久化 Schema 与 20 项成员指针反射锚点的具名领域核心（NleMediaAsset、TimelineClip、NleProject、TranscodeProfile、MediaInfo）12。

## **三、 奥卡姆剃刀全仓除冗：冗余防御、隐藏开关、编译选项与心智坑拔除方案**

秉持“边界一次验证、内部不可空引用与范围视图直通”的奥卡姆剃刀原则，Phase 12 必须同步从构建层与源码层拔除全部增加认知负担的冗余开关与隐性陷阱5。

### **3.1 构建系统深度脱毒与编译选项归一化**

针对 CMakeLists.txt 与 cmake/RtxFfmpegStatic.cmake 遗留的多头开关与隐蔽越界行为，Phase 12 须落地四项清洗举措5。首先在编译标准与警告开关层面，彻底删去 rtx\_core、rtx\_shim\_static、rtx\_ngx\_bridge\_static、CineRTX 与 rtx\_tests 五个目标上重复声明的 CXX\_STANDARD 23 属性块及冗余 \-Wno-unused-function，全部收拢至 rtx\_project\_options 单点管控，并待主力工具链切至 MSVC 14.52+/14.53 后将 /std:c++latest 收敛为稳定 /std:c++23、启用链接器 /WARNUNUSEDLIBS 排查死库5；同时移除迁移残留的 RTX\_ZIG\_EXECUTABLE、rtx\_ngx\_shadow\_archives 与 \-fexperimental-library 妥协开关，并把全局 /wd4324 抑制从 rtx\_project\_options 拔除，改为收缩 WasapiAudioOutput::Impl 中 7 处过度膨胀的独立 alignas(64) 原子变量（仅在真正跨越写者/读者线程边界的两组游标间保留单道缓存行隔离，直接消解 7 处 C4324 根源）5。  
其次在构建规则与隐蔽行为层面，删除 CMakeLists.txt 配置阶段执行的两段 execute\_process(EmbedPtx.cmake)，仅保留构建期 add\_custom\_command 按依赖触发生成；合并 rtx::ngx\_bridge 镜像重复的 if(WIN32)/else() 分支与 CineRTX、rtx\_tests 中相邻的双重 if(WIN32) 块，删除 rtx\_ngx\_bridge\_static 多余的 PRIVATE "\${RTX\_NGX\_INCLUDE\_DIR}"（仅留 SYSTEM PRIVATE）与文末空 if(WIN32) endif() 僵尸块5；为 HuggingFace RIFE ONNX 权重与外部归档拉取增加默认关闭的显式选项（RTX\_ALLOW\_NETWORK\_PROVISION），彻底斩断 cmake 配置期静默联网的不可控行为5。最后也是最核心的合规修正，必须将 ngx\_d3d12\_bridge.cpp 与 nvsdk\_ngx\_s.lib 从静态并入 rtx\_core 的 rtx\_ngx\_bridge\_static 中剥离，恢复为独立编译的 MIT OR Apache-2.0 动态插件 rtx\_ngx\_bridge.dll，同时移除 RtxFfmpegStatic.cmake 中无端传递的 user32、ole32、strmiids、mfuuid、shlwapi、secur32、ws2\_32 依赖以及 Debug 期 /NODEFAULTLIB:msvcrt.lib 补丁（统一 FFmpeg 与主工程静态 CRT 编译选项）5。

### **3.2 源码层多余防御、全局状态、隐藏开关与心智坑清零清单**

| 源码文件锚点 | 检出的冗余防御 / 隐藏开关 / 心智坑 | Phase 12 极限拔除与现代化方案 |
| :---- | :---- | :---- |
| src/shim/ngx\_bridge/d3d12\_interop.cpp28 | 1\. 硬编码死开关 bool traceEnabled() { return false; } 与虚假环境变量注释 RTX\_BRIDGE\_TRACE=1。 2\. 查表函数中 5 处重复结构化绑定加 (void) 哑元消除。 3\. std::snprintf 拼接及手动 Release() 链28 | 物理删除 traceEnabled()/trace() 死分支；格式查询改用 std::ranges::find \+ std::get\<N\>；改用 std::format；内部资源句柄收归 ComPtr/UniqueHandle，以声明序自动逆序释放28 |
| src/core/wsola\_stretch.cpp7 | 1\. 超过 2 声道直接 return 0 导致多声道消音。 2\. 每步用 std::copy\_n 搬移环形缓冲 prefix。 3\. softLimitSample 实为硬削波且误导命名；512 样本短窗单声道点积；\#ifdef \_\_cpp\_lib\_mdspan 宏分支与手写 kTwoPi7 | 按会话期一次性预分配支持任意声道的帧对齐分析环与尾迹缓冲，改用真正的环形模索引或 C++20 std::shift\_left 消除无效搬移；恢复 960 样本（20 ms @ 48 kHz）全声道平均分析；删除 \#else 死分支，使用 std::numbers::pi\_v\<double\> 及 audio\_mixer\_avx2 数学有界软限幅7 |
| src/gui/workstation\_canvas.h/.cpp8 | 1\. 全局可变单例 g\_app、active\_vsr\_target() 及 7 个全局包装函数。 2\. onNleExportSequence() 内空转 toJson/fromJson 假引用与未消费 AudioGraphSegment。 3\. 废弃 exportSelectedClipToEncoder()；IDC\_BTN\_NLE\_EXPORT 错放进 kPlayerIds。 4\. collectProfileFromUi/applyProfileToUi 维护第 4 份平行 20 项参数映射；paintCommandButton 25 项 switch；addFilesToBin 拆散 V1/A1 撤销步8 | 将 WorkstationApp 收归主窗 GWLP\_USERDATA unique\_ptr（WM\_NCCREATE/WM\_NCDESTROY 托管）并清零全局状态；移除假序列化并实现真正 Ctrl+S/Ctrl+O 工程读写与多轨混音导出；删除 exportSelectedClipToEncoder() 并将 IDC\_BTN\_NLE\_EXPORT 移入 kNleIds；全面由 kParamDescriptors 表驱动 UI 控件生成/收集/回填；paintCommandButton 改为 constexpr 查找表；新增 NleEditDelta 批量复合操作保障音画同撤；GDI 对象全部收归 FnDeleter 与 DC\_BRUSH8 |
| src/cli/main.cpp18 | 1\. parseInteger 用 try/catch std::stoi 控制流。 2\. 空 if (target\_fps\_set && target\_fps.num \> 0\) {} 与错位注释。 3\. runSessionStdio() 使用裸 std::thread::detach()。 4\. 字符串 rfind 拼接 JSON 与重复手动 CLI 参数分支18 | 改用无异常零分配 std::from\_chars；删除空分支与错位注释；stdin\_reader 切至 std::jthread；以 std::format 直接构建组合探测 JSON；所有标量 CLI 选项收归 kParamDescriptors 表驱动18 |
| src/core/runtime\_env.cpp44 | 双层 try/catch 盲试 8 个 locale 串与旧式 swprintf\_s 路径拼接44 | 按平台锁定确定性策略（Windows 固定 ".UTF8" \+ UTF-8 控制台代码页，POSIX 固定 "C.UTF-8" 回退 "C"）；用 std::filesystem::temp\_directory\_path() 与 std::format 替换 swprintf\_s44 |

## **四、 14 项系统 DLL 依赖法医级剖析与极限剥离路线图**

对照提问列举的全部 14 项 Windows DLL，结合 PE 导入表实测（gate11d\_imports.log 的 12 项基准加上启用 WASAPI 后的 ole32.dll 以及内核转发底座 kernelbase.dll）与仓内源码调用栈取证13，必须严格区分**静态 PE 导入表（IAT）剥离**与**运行时按需加载**，坚决避免为了抹除一个轻量化系统 DLL 而引入更复杂的替代依赖（如为替换 gdi32 而拉入 d2d1/dwrite COM 全家桶）。

| DLL 名称 | 仓内引入源码定位 | 静态 IAT 剥离可行性 | 运行时剥离可行性 | 极限剥离方案与 KISS 工程权衡 |
| :---- | :---- | :---- | :---- | :---- |
| **d3d12.dll** | CMakeLists.txt 为 preview\_viewport.cpp 与 rtx\_tests 静态链接 d3d12.lib5 | **可 100% 移除** | 仅 GUI 呈现时按需加载 | 仿照 d3d12\_interop.cpp28，在 preview\_viewport.cpp 改用 LoadLibraryExW(L"d3d12.dll", nullptr, LOAD\_LIBRARY\_SEARCH\_SYSTEM32) 解析序列化等入口，从 CMakeLists.txt 删除 d3d12 |
| **dxgi.dll** | CMakeLists.txt 为 preview\_viewport.cpp 与 rtx\_tests 静态链接 dxgi.lib5 | **可 100% 移除** | 仅 GUI 呈现时按需加载 | 在 preview\_viewport.cpp 中动态解析 CreateDXGIFactory2，从 CMakeLists.txt 删除 dxgi，使 CLI/无头进程零 DXGI 启动加载 |
| **dwmapi.dll** | rtx\_gui.cpp 调用 DwmSetWindowAttribute 开启暗色标题栏13 | **可 100% 移除** | 可选（Win10/11 标题栏美化） | 从 CMakeLists.txt 移除 dwmapi；仅在 GUI 窗体创建时用 4 行 LoadLibraryExW \+ GetProcAddress 可选设置 DWMWA\_USE\_IMMERSIVE\_DARK\_MODE \= 20，在无 DWM 环境自动静默降级13 |
| **comdlg32.dll** | rtx\_gui.cpp 仅在点击浏览按钮时调用 GetOpenFileNameW / GetSaveFileNameW13 | **可 100% 移除** | 仅点击“浏览”时按需加载 | 从 CMakeLists.txt 移除 comdlg32；在 runFileDialog() 内懒加载 comdlg32.dll，避免进程启动期拉入整个 Shell 文件框组件链 |
| **comctl32.dll** | workstation\_canvas.cpp 仅用其创建 PROGRESS\_CLASSW 与 TRACKBAR\_CLASSW（及 InitCommonControlsEx）8 | **可永久 100% 移除** | **可永久 100% 移除** | 删除该 2 个样式割裂的旧式通用控件，复用 workstation\_canvas.cpp 现有绘制原语实现暗色自绘进度条与滑块；永久删除 InitCommonControlsEx 与 comctl32 链接（标准 EDIT/BUTTON/LISTBOX/COMBOBOX 归属 user32.dll） |
| **uxtheme.dll** | workstation\_canvas.cpp 对子控件调用 SetWindowTheme(h, L"DarkMode\_Explorer", nullptr)8 | **可 100% 移除** | **Phase 12 解耦/自绘后可彻底根除** | 立即从 CMakeLists.txt 移除 uxtheme；GUI 暂留标准滚动条时改用懒加载探测 SetWindowTheme，待 Phase 12 GUI 彻底自绘或切换 WebUI v3 后实现零加载 |
| **shell32.dll** | main.cpp 调用 CommandLineToArgvW 解析参数，以及 GUI 拖拽文件 DragQueryFileW13 | **可 100% 移除** | CLI 零加载；GUI 按需 | 在 main.cpp 移除 CommandLineToArgvW（MSVC /MT 原生提供 \_\_argc/\_\_wargv47，或用 \~20 行纯 C++ 解析 GetCommandLineW()）；GUI 仅在 WM\_DROPFILES 时懒加载 shell32.dll；从 CMakeLists.txt 删除 shell32 |
| **ole32.dll** | wasapi\_audio\_output.cpp 调用 CoInitializeEx/CoUninitialize/CoCreateInstance/CoTaskMemFree；RtxFfmpegStatic.cmake 误传了 ole3236 | **可 100% 移除** | **WASAPI 播放期必需** | 先从 RtxFfmpegStatic.cmake 拔除冗余 ole32/strmiids/mfuuid43；再于 wasapi\_audio\_output.cpp 内懒加载上述 4 个 COM 函数，使 CLI 转码与探测 0 加载 COM；诚实边界在于，保留高精度 IAudioClock 主时钟在播放期间必须调用 WASAPI COM 接口（不降级至低精度多媒体接口）36 |
| **advapi32.dll** | 出现于 gate11d\_imports.log 12 项静态导入表，而仓内核心 C++ 源码未见显式 Reg\* 调用，高度怀疑来自不当静态链接的 nvsdk\_ngx\_s.lib 或静态库传递5 | **可移除（待 IAT 符号审计验证）** | 恢复独立 rtx\_ngx\_bridge.dll 后主进程剥离 | 将 nvsdk\_ngx\_s.lib 迁回独立 rtx\_ngx\_bridge.dll5，随后对 CineRTX.exe 执行 dumpbin /imports15 与 /WARNUNUSEDLIBS24 定位确切导入 thunk；若仍有静态库附带非关键系统调用则以懒加载替换，力争静态 IAT 零 advapi32.dll |
| **bcrypt.dll** | 静态链接 FFmpeg（libavutil）调用 BCryptGenRandom 生成容器 GUID/随机种子13 | 建议**保留** | 建议保留 | bcrypt.dll 是无 GUI/COM 寄生的极简系统密码学库。若强行改写 FFmpeg 上游源码剔除 bcrypt，会引入非标补丁维护债或迫使随机数回退至更沉重/低熵实现，得不偿失、违背 KISS 原则 |
| **gdi32.dll** | workstation\_canvas.cpp 与 rtx\_gui.cpp 用于基础矩形、字体与双缓冲绘制8 | **Core/GUI 解耦后可从核心移除** | GUI 内建议保留 | **KISS 关键取舍**：在 Win32 GUI 内试图换掉 gdi32.dll 必须引入 d2d1.dll \+ dwrite.dll \+ ole32.dll 等重型 COM 栈，属于典型反优化。正确解法是在 Phase 12 将界面剥离出主引擎，使核心 CineRTX.exe 零 gdi32.dll |
| **user32.dll** | Win32 窗口、消息泵与控件基座；同时被 RtxFfmpegStatic.cmake 无故传递链接5 | **Core/GUI 解耦后可从核心移除** | GUI 宿主必需 | 立即从 RtxFfmpegStatic.cmake 剔除无用的 user32（外科裁剪 FFmpeg 未启用任何窗口设备）6；Phase 12 将 Win32 界面迁入按需插件 rtx\_gui.dll 或独立 CineRTX-Studio.exe 后，核心 CineRTX.exe 彻底摆脱 user32.dll 与 Session 0/桌面队列耦合 |
| **kernel32.dll** | MSVC /MT 静态 CRT、线程/文件/内存与 LoadLibraryExW 基石13 | **不可移除（OS 铁底）** | 不可移除 | Win32 用户态应用最低运行基座。禁止绕过 kernel32.dll 直接调用未公开 ntdll.dll 原生系统调用，以免破坏 std::jthread 与标准库稳定性 |
| **kernelbase.dll** | Windows 7 起微软将 kernel32/advapi32 底层实现迁入 kernelbase.dll 并通过 API Set 路由15 | **可保持不出现在直接 IAT** | **OS 加载期不可移除** | 只要不链接 onecore.lib/mincore.lib 或仅在 kernelbase.dll 导出的 API Set 转发库，CineRTX.exe 顶层 PE 导入表可保持零 KERNELBASE.dll 项（gate11d\_imports.log 已实证未出现该项）14；但进程启动时系统加载器 LdrpInitializeProcess 必然将 kernelbase.dll 映射入内存供 kernel32.dll 转发使用，属于操作系统物理机制15 |

按上述路线精确收口后，**现行单 EXE 混合形态**即可率先摘除 9 项静态导入，使 CineRTX.exe 静态 IAT 收敛为 **kernel32.dll、user32.dll、gdi32.dll、bcrypt.dll 4 项基础系统库**（假设 dumpbin /imports 确认迁出 nvsdk\_ngx\_s.lib 后无其他 advapi32 残留引用）；而当 **Phase 12 核心/界面解耦架构**落地后，CineRTX.exe 纯引擎的静态 IAT 将进一步压榨至最终极限——仅保留 **kernel32.dll \+ bcrypt.dll 2 项静态底座**。

## **五、 Phase 11.4 收官至 Phase 12 极限重构分期演进路线与门禁**

为兼顾 v0.3.0 的按期发布与 Phase 12 的彻底纯化重塑，后续演进须严格按三阶段顺序推进，并在获得最终授权前继续严守零私自 git commit 与绝不 git push 的仓库主权纪律2：

> 1. **前置 48 小时止血补证（完结 Phase 11.3-S 并获批战略封板）**：在 hw\_transcode\_stages.cpp 真正消费 AudioGraphSegment 完成 1V+1A 混音导出，拔除 onNleExportSequence() 内假 toJson/fromJson 空转调用与废弃函数 exportSelectedClipToEncoder()，修正 wsola\_stretch.cpp 多声道 return 0 缺陷并将 nvsdk\_ngx\_s.lib 剥离回独立 rtx\_ngx\_bridge.dll5；同时在 WTS Session \> 0 桌面补充归档 preview\_viewport\_100x presented \> 0、D2H \== 0 B 日志及 ffprobe 多段 .mkv 切点 IDR 与 90 kHz PTS 单调性逐帧物证，清洗 CMakeLists.txt 剩余重复目标属性并将 AuditSloc.cmake 校准回 31,064 法定基线完成平账2。  
> 2. **Phase 11.4 决胜冲刺（4–5 工作日，按 20 号施工单封板 v0.3.0）**：严守 include/shim/ngx\_bridge\_abi.h 的 MIT OR Apache-2.0 边界追加 ABI v5 RtxRifeConfig 与 rtx\_ngx\_bridge\_init\_rife\_ex10；升级 rife\_engine.cpp/rife\_builder.cpp 至 A \= 128 对齐与 4K Profile（3840x2176），同步更新保守 PTX ISA 版本的 rife\_pack.ptx 实现 reflect101 镜像填充，修复 hw\_transcode\_stages.cpp 分辨率传参并接入 anchor\_p010\_10；落地 VramAdaptiveScheduler（基于 Windows WDDM 8 GB GDDR7 扣除桌面占用后约 7,300 MiB dxgi\_budget\_bytes 建模：82% 约 5,986 MiB 降级、68% 约 4,964 MiB 恢复、120 帧最小驻留）10；重构 nv\_encoder.cpp 实现 Video Codec SDK 13.1 NvEncodeAPIGetMaxSupportedVersion 双模协商、std::vector\<BitstreamSlot\> 环形异步池与 NV\_ENC\_ERR\_NEED\_MORE\_INPUT 蓄水排空，在 RTX 5060（驱动 ≥621.24 WHQL）输出 AV1 Default / 7B / 15B/31B+UHQ 实测矩阵并正式发布 v0.3.010。  
> 3. **Phase 12 极限现代 C++ 纯化与极简 DLL 重构（独立演进周期，迈向 v0.4.0+）**：实施核心引擎 CineRTX.exe（Pure CLI \+ \--session-stdio JSONL 引擎）与可选界面模块（rtx\_gui.dll 或 CineRTX-Studio.exe）彻底解耦3，按第 4 节方案清除 d3d12、dxgi、dwmapi、comdlg32、comctl32、uxtheme、shell32、ole32、advapi32、user32、gdi32 静态导入，将 CineRTX.exe 锁死在 kernel32.dll \+ bcrypt.dll 双底座与 \[1.2, 12.0\] MiB 体积包络内10；全仓执行第 2 节自定义类型清剿表、第 3 节 kParamDescriptors 全栈驱动及 std::jthread/std::from\_chars/std::format/std::flat\_map/std::shift\_left 现代化替换；升级 AuditTypes.cmake 为 file(READ) 词法前端并收紧 AuditImports.cmake 白名单至极简目标集，建立针对“伪引用调用/虚假类型注解/冗余编译开关”的零容忍自动化阻断门禁2。

#### **引用的著作**

> 1. [`45_CineRTX Phase 11.3-S 全栈接线与真机闭环专家终审验收委托书.md`](45_CineRTX%20Phase%2011.3-S%20全栈接线与真机闭环专家终审验收委托书.md)  
> 2. [`43_CineRTX Phase 11.3-S SuperPower 施工单_全栈接线与真机闭环.md`](43_CineRTX%20Phase%2011.3-S%20SuperPower%20施工单_全栈接线与真机闭环.md)  
> 3. [`42_CineRTX 11.3-R 终审验收与演进.md`](42_CineRTX%2011.3-R%20终审验收与演进.md)  
> 4. [`44_CineRTX Phase 11.3-S 施工汇报_全栈接线与真机闭环.md`](44_CineRTX%20Phase%2011.3-S%20施工汇报_全栈接线与真机闭环.md)  
> 5. [`CMakeLists.txt`](../../CMakeLists.txt)  
> 6. [`compliance.md`](../../compliance.md)  
> 7. [`wsola_stretch.cpp`](../../src/core/wsola_stretch.cpp)  
> 8. [`workstation_canvas.cpp`](../../src/gui/workstation_canvas.cpp)  
> 9. [unknown_doc]  
> 10. [`20_CineRTX Phase 11.4 SuperPower 施工单_RIFE高阶算子与SDK13.1分级实证.md`](20_CineRTX%20Phase%2011.4%20SuperPower%20施工单_RIFE高阶算子与SDK13.1分级实证.md)  
> 11. [`24_CineRTX 第23号文档深度方案.md`](24_CineRTX%20第23号文档深度方案.md)  
> 12. [`38_CineRTX 现代C++ 极限重构审计.md`](38_CineRTX%20现代%20C%2B%2B%20极限重构审计.md)  
> 13. [用户评审记录]  
> 14. [`11_验收实证记录.md`](11_验收实证记录.md)  
> 15. Before main(): tracing what Windows really does when you call, [https://www.chadduffey.com/windows/windbg/2026/04/20/BeforeMainCreateProcess](https://www.chadduffey.com/windows/windbg/2026/04/20/BeforeMainCreateProcess)  
> 16. [`render_graph.cpp`](../../src/core/render_graph.cpp)  
> 17. [`nle_project.h`](../../include/rtx/nle_project.h)  
> 18. [`main.cpp`](../../src/cli/main.cpp)  
> 19. C++26 is done (again)! — Trip report: November 2026 ISO C++, [https://www.reddit.com/r/cpp/comments/1y1y10z/c26_is_done_again_trip_report_november_2026_iso_c/](https://www.reddit.com/r/cpp/comments/1y1y10z/c26_is_done_again_trip_report_november_2026_iso_c/)  
> 20. C++26 - Wikipedia, [https://en.wikipedia.org/wiki/C%2B%2B26](https://en.wikipedia.org/wiki/C%2B%2B26)  
> 21. C++ – Sutter's Mill, [https://herbsutter.com/tag/c/](https://herbsutter.com/tag/c/)  
> 22. C++ 史上首次被否决！C++ 之父与标准委员会，终于闹掰了？, [https://tonybai.com/2026/11/09/cpp26-historic-dis-rejection-herb-sutter/](https://tonybai.com/2026/11/09/cpp26-historic-dis-rejection-herb-sutter/)  
> 23. Implement C++26 Standard features in MSVC - Developer Community, [https://developercommunity.visualstudio.com/t/Implement-C26-Standard-features-in-MSV/10777423](https://developercommunity.visualstudio.com/t/Implement-C26-Standard-features-in-MSV/10777423)  
> 24. MSVC Build Tools Preview updates - November 2026, [https://devblogs.microsoft.com/cppblog/msvc-build-tools-preview-updates-november-2026/](https://devblogs.microsoft.com/cppblog/msvc-build-tools-preview-updates-november-2026/)  
> 25. What's new for MSVC Build Tools - Microsoft Learn, [https://learn.microsoft.com/en-us/cpp/overview/what-s-new-for-msvc?view=msvc-170](https://learn.microsoft.com/en-us/cpp/overview/what-s-new-for-msvc?view=msvc-170)  
> 26. Kernel Development + C++26 Reflection = - Stream HPC, [https://streamhpc.com/blog/2026-07-08/kernel-development-c26-reflection-%F0%9F%A7%A1/](https://streamhpc.com/blog/2026-07-08/kernel-development-c26-reflection-%F0%9F%A7%A1/)  
> 27. C++26 Reflection: What It Actually Changes for Large Codebases, [https://www.wholetomato.com/blog/cpp26-reflection-large-codebases/](https://www.wholetomato.com/blog/cpp26-reflection-large-codebases/)  
> 28. [`d3d12_interop.cpp`](../../src/shim/ngx_bridge/d3d12_interop.cpp)  
> 29. [`workstation_canvas.h`](../../src/gui/workstation_canvas.h)  
> 30. Changelog · microsoft/STL Wiki - GitHub, [https://github.com/microsoft/STL/wiki/Changelog](https://github.com/microsoft/STL/wiki/Changelog)  
> 31. History for Changelog · microsoft/STL Wiki - GitHub, [https://github.com/microsoft/STL/wiki/Changelog/_history](https://github.com/microsoft/STL/wiki/Changelog/_history)  
> 32. Meta LWG issue: 2026-03 meeting · Issue #6223 · microsoft/STL, [https://github.com/microsoft/STL/issues/6223](https://github.com/microsoft/STL/issues/6223)  
> 33. P0843R14 `` · Issue #4766 · microsoft/STL - GitHub, [https://github.com/microsoft/STL/issues/4766](https://github.com/microsoft/STL/issues/4766)  
> 34. Support for C++26 Reflection has been merged into GCC trunk! : r/cpp, [https://www.reddit.com/r/cpp/comments/1qdn04o/support_for_c26_reflection_has_been_merged_into/](https://www.reddit.com/r/cpp/comments/1qdn04o/support_for_c26_reflection_has_been_merged_into/)  
> 35. CppCon 2026の発表概要まとめ - Faith and Brave, [https://faithandbrave.hateblo.jp/entry/2026/09/25/094405](https://faithandbrave.hateblo.jp/entry/2026/09/25/094405)  
> 36. [`wasapi_audio_output.cpp`](../../src/core/wasapi_audio_output.cpp)  
> 37. [`nvof_engine.cpp`](../../src/shim/ngx_bridge/nvof_engine.cpp)  
> 38. [`ngx_d3d12_bridge.h`](../../src/shim/ngx_bridge/ngx_d3d12_bridge.h)  
> 39. [`d3d12_interop.h`](../../src/shim/ngx_bridge/d3d12_interop.h)  
> 40. [`render_graph.h`](../../include/rtx/render_graph.h)  
> 41. [`embedded_shim.h`](../../include/rtx/embedded_shim.h)  
> 42. [`preview_viewport.h`](../../src/gui/preview_viewport.h)  
> 43. [CineRTX FFmpeg全量静态链接专家方案]  
> 44. [`runtime_env.cpp`](../../src/core/runtime_env.cpp)  
> 45. What is the difference between kernelBase.dll and Kernel32.dll, [https://reverseengineering.stackexchange.com/questions/20175/what-is-the-difference-between-kernelbase-dll-and-kernel32-dll](https://reverseengineering.stackexchange.com/questions/20175/what-is-the-difference-between-kernelbase-dll-and-kernel32-dll)  
> 46. Support Dark and Light themes in Win32 apps - Microsoft Learn, [https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/ui/apply-windows-themes](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/ui/apply-windows-themes)  
> 47. Results - UnpacMe, [https://www.unpac.me/results/999cd0bd-0dce-4ba2-89a4-f73107d9c132/](https://www.unpac.me/results/999cd0bd-0dce-4ba2-89a4-f73107d9c132/)  
> 48. MSVC Build Tools Preview updates - August 2026 - C++ Team Blog, [https://devblogs.microsoft.com/cppblog/msvc-build-tools-preview-updates-august-2026/](https://devblogs.microsoft.com/cppblog/msvc-build-tools-preview-updates-august-2026/)  
> 49. API Sets Added For Windows 10.0 - Geoff Chappell, Software Analyst, [https://www.geoffchappell.com/studies/windows/win32/apisetschema/history/sets100.htm](https://www.geoffchappell.com/studies/windows/win32/apisetschema/history/sets100.htm)  
> 50. Why does WriteProcessMemory in kernel32.dll do this? (ASM), [https://reverseengineering.stackexchange.com/questions/8384/why-does-writeprocessmemory-in-kernel32-dll-do-this-asm](https://reverseengineering.stackexchange.com/questions/8384/why-does-writeprocessmemory-in-kernel32-dll-do-this-asm)  
> 51. [`16_CineRTX Phase 11 演进计划.md`](16_CineRTX%20Phase%2011%20演进计划.md)  
> 52. [gate3.md]