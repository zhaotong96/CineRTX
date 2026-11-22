# **CineRTX Phase 11.3 施工验收终审裁决书暨极限现代 C++20/23/26 重构与演进方案**

**终审验收裁决：坚决驳回（REJECTED / UNACCEPTABLE），立即冻结外部执行工程师一切主干合龙权限，并将当前工作区转入全仓极限现代 C++20/23/26 纯化重构波次（Phase 11.3-R）。** 经对照 36 号施工单逐行穿透审计基准提交 749fe0d 之后的交付源码与门禁实测，Phase 11.3 六道准出门禁呈“四项不达标（FAILED / UNACCEPTABLE / FATAL BLOCKED）、一项有条件（CONDITIONAL）、仅一项通过（PASS）”的溃败格局：旧画布负向扫描残留 18 处命中，AuditTypes.cmake 检出 3 处类型违规，test\_audio\_mixer\_avx2.cpp 擅自全局重载 operator new/delete 劫持全仓测试进程，且同口径纯净有效代码行（Pure SLOC）由入场基准 31,064 行暴增至 32,380 行（净增 \+1,316 行，与 \-400 至 \-600 行的追偿预算形成约 1,800 行的倒挂）1。源码法医取证进一步揭露了未在 37 号委托书中点破的深层结构腐败：NleEditCommand 在注释中宣称使用 std::variant 却物理平铺全部 7 个命令结构体并夹带 4 个死字段，NleProject::toJson() 在数组末尾稳定生成违反 RFC 8259 的非法尾逗号 JSON，workstation\_canvas.cpp 存在跨线程非原子全局状态竞态，而 render\_graph.cpp 则堆砌未过审计的 VideoClaim 与多层堆分配循环1。  
从极致现代 C++（C++20/23/26）与奥卡姆剃刀原则审视，当前项目的真正病灶不仅是单次外包违规，而是全仓尚未彻底拔除“过度面向对象包装（Wrapper Slop）”“重复性空指针防御”“散落全局可变状态”“相互冲突的编译开关”以及“体外代码生成器”。依托 CineRTX 主力工具链 MSVC 14.51（cl 19.51）已具备的 C++23 完备标准库与 C++26 前沿特性（同时精准规避 \#embed 缺失、静态反射 ^^ 尚未落地与 std::inplace\_vector 未完备等编译器边界），全仓内部逻辑层完全可以推行“能用 std 绝不建类”的极限清零手术：用代数 std::variant、std::tuple/std::pair、std::optional、std::expected、std::span、std::mdspan、std::ranges、std::move\_only\_function、Deducing this 与唯一 FnDeleter 淘汰 15 个以上冗余自定义类型，并立即物理铲除未经受控立项的 scripts/ 与 schema/ 目录1。  
针对宿主机测试期间出现的高频“嘟嘟嘟”蜂鸣声，已确证其由 tests/test\_playback\_session.cpp 向物理 WASAPI 默认端点直接倾倒 440 Hz 正弦波，以及 tests/test\_raii\_retirement.cpp 在 100 次窗口创建销毁压测中以 Sleep(5) 阻塞线程且零消息抽水引发 win32k.sys 输入缓冲区溢出警报所致1。本文签署即刻生效的声学哑音化、非阻塞消息泵修复指令与 Phase 11.3-R 净减平账方案，确立从 Phase 11.3-R 收口直通 Phase 11.4（v0.3.0）及 Wave 4（v0.4.0）的演进路线。

## **第一部分 Phase 11.3 施工法医级验收审查与隐蔽病灶穿透**

对照 36 号施工单设定的六项硬性准出门禁与仓内源码实体（nle\_project.h/.cpp、render\_graph.h/.cpp、audio\_mixer\_avx2.h/.cpp、wsola\_stretch.cpp、workstation\_canvas.cpp 与 test\_audio\_mixer\_avx2.cpp），逐项核验结果汇总如下1。

| 门禁编号 | 36 号施工单规范契约 | 37 号委托书实测读数 | 源码穿透复核新增实质缺陷 | 终审裁决 |
| :---- | :---- | :---- | :---- | :---- |
| **Gate 11.3-1** | workstation\_canvas.\* 负向扫描 NleClip\|NleTimeline\|...\|\_ms 必须返回 0 命中2 | 实测残留 18 处命中（行 211 NleClip、行 265 paintNleTimeline、行 1159 registerNleTimelineWindowClass）1 | 除新类型命名 NleClip 撞车正则与遗留旧绘制函数名外，seekPlaybackFromSlider 仍手动算毫秒；拖拽修改破坏排序不变量且导出线程存在数据竞态5 | **FAILED** |
| **Gate 11.3-2** | 落地 NleProject 与 7 类可逆命令，100% 复用 json\_scanner.h 零泄漏2 | 引入大量自定义样板类，与剪辑引擎复用不足1 | NleEditCommand 注释谎称用 std::variant 实则平铺 7 个结构体且含 4 个死字段；toJson() 输出带尾逗号的非法 JSON；fromJson() 滥用异常控制流4 | **CONDITIONAL / 实判不合格** |
| **Gate 11.3-3** | 音频混音与 WSOLA 热循环零堆内存分配2 | 擅自在单测中编写 60 行全局 operator new/delete 劫持全仓进程1 | 自造 VoicePrep 与 AudioMixerScratchpad 重复包装；mixAvx2 未采用约定 \_mm256\_fmadd\_ps；WsolaStreamState 每会话堆分配只读 hann 表且手写步长偏移16 | **UNACCEPTABLE** |
| **Gate 11.3-4** | encodeWorkerBody() GPU 分支补齐 encode\_frame.keyframe（C-05 闭环）2 | 已补齐 keyframe 赋值1 | 源码层面单点补齐无额外瑕疵1 | **PASS** |
| **Gate 11.3-5** | CompiledRenderGraph 驱动多段硬切导出且与容器解复用逐帧吻合2 | 自造未声明 VideoClaim，且未见容器解复用逐帧物证1 | claimInterval 与 visibleSpans 在每次循环内反复构造拷贝 std::vector；CompiledRenderGraph 仅包装向量与两个冗余缓存字段3 | **FAILED** |
| **Gate 11.3-6** | AuditTypes.cmake 0 违规且同口径 Pure SLOC 净减 400–600 行2 | AuditTypes 报 3 处失败（nle\_project.h:91,94、render\_graph.cpp:17）；Pure SLOC 由 31,064 升至 32,380（+1,316 行）1 | 与追偿目标形成约 1,800 行倒挂，彻底击穿 Phase 11.2-R 结转的技术赤字平账契约1 | **FATAL BLOCKED** |

源码级穿透揭示出五项极其隐蔽且直接威胁可维护性与正确性的技术病灶。第一，NleEditCommand 存在“注释欺诈”与死字段堆积。在 include/rtx/nle\_project.h 中，类型存续声明声称其通过 std::variant 携带 7 类元数据增量，然而实际声明却将 AddClipCommand、TrimClipCommand、SplitClipCommand、MoveClipCommand、DeleteClipCommand、ReorderTracksCommand 与 RelinkAssetCommand 全部平铺为结构体实体字段，并额外引入枚举 NleEditKind5。这导致每条仅需几十字节的编辑指令都在内存中强制绑定两份完整 NleMediaAsset 与两份 NleClip 快照，迫使调用点在 workstation\_canvas.cpp 中写出 {NleEditKind::SplitClip, {}, {}, split, {}, {}, {}, {}} 的 8 元占位初始化5。与此同时，src/core/nle\_project.cpp 的 EditHistory::apply() 从未读取 ReorderTracksCommand::lane\_a、ReorderTracksCommand::lane\_b 以及 RelinkAssetCommand::old\_path、RelinkAssetCommand::new\_path，这四个成员纯粹是毫无作用的冗余字段4。  
第二，NleProject::toJson() 存在系统性破坏 JSON 语法的尾逗号缺陷，且 fromJson() 滥用异常进行局部控制流跳转。在 src/core/nle\_project.cpp 中，拼接 "assets" 与 "clips" 数组的循环在每个元素结尾执行 out.back() \= '}'; out \+= ',';，循环结束却直接拼接 out \+= '\]'; 而未回退最后一个多余逗号4。这意味着任何包含至少一个素材或片段的工程序列化结果均形如 "assets":\[{...},\] 与 "clips":\[{...},\]，虽可能被容错性宽松的局部扫描器跳过，但一旦交由标准 JSON 解析器（如浏览器 JSON.parse() 或 Python json.loads()）必将因违反 RFC 8259 语法而直接解析失败。同时，fromJson() 的内部辅助函数 requiredInt() 与 requiredString() 在返回契约明确为 Result\<NleProject\>（即 std::expected）的前提下仍抛出 std::runtime\_error 再由外层 try/catch 捕获转换，平白制造异常栈展开路径与冗余堆字符串构造4。  
第三，workstation\_canvas.cpp 存在后台导出线程数据竞态与时间线状态不变量破坏。除了新结构体故意定名为 NleClip 导致撞车 Gate 11.3-1 正则、保留旧函数名 paintNleTimeline 与 registerNleTimelineWindowClass、以及 seekPlaybackFromSlider() 仍残留手写 pos \* kFlicksPerSecond / 1000 毫秒换算外，时间轴交互逻辑本身存在明显缺陷1。在 nleTimelineProc 的 WM\_LBUTTONDOWN 命中测试中，循环遍历 project.clips() 仅判断时间水平坐标 \[start\_flicks, end) 而完全忽略鼠标纵坐标所在轨道，V2 与 V1 重叠时无法按所点轨道精确选中片段；随后的 WM\_MOUSEMOVE 又直接原地修改 clip-\>start\_flicks 而不重新排序，暂时破坏了 NleProject 维护的按 (start\_flicks, lane) 有序不变量4。更为严重的是，onNleExportSequence() 启动的 std::jthread 在后台线程直接写非原子的全局 bool g\_guiTranscoding（经 setGuiTranscoding(false)）及非原子 std::string g\_app.nleExportStatus，与 UI 线程读取构成未定义的数据竞态（Data Race），且直接向捕获的裸 HWND 投递 WM\_APP\_NLE\_EXPORT\_DONE 而未在窗口销毁阶段先停止导出线程6。  
第四，render\_graph.cpp 采用低效且自造未声明类的多遍堆分配算法。文件第 17 行私自构造未标注 @rtx-type-justification 的结构体 VideoClaim（其 5 个字段中有 3 个直接重复抄自自身持有的 const NleClip\*），直接触发 AuditTypes.cmake 拦截失败1。同时，claimInterval() 与 visibleSpans() 在外层每次迭代中反复构造新的 std::vector\<std::pair\<int64\_t, int64\_t\>\> 并覆盖拷贝，随后再经历两次排序与一次整向量重分配合并；而头文件中的 CompiledRenderGraph 仅是对 std::vector\<RenderGraphSegment\> 外加两个冗余缓存值（total\_duration\_flicks\_ 与 hard\_cut\_count\_）的伪装类包装3。  
第五，test\_audio\_mixer\_avx2.cpp 暴力污染全局运行时，且音频总线背离极简与 SIMD 约定。尽管 36 号施工单曾提出在 Debug 下重载 operator new 核验零堆分配，但外部工程师直接在多翻译单元合编的 rtx\_tests.exe 内无条件全局替换 12 个 operator new/delete（含对齐版本），导致同一测试二进制中全部 128 项测试的堆分配行为被单个文件的全局原子计数器劫持1。此外，audio\_mixer\_avx2.h 发明了仅包装裸指针加帧数的 VoicePrep 与包装定长数组的 AudioMixerScratchpad；mixAvx2() 在 AVX2 分支未使用 11D 架构规定的单指令融合乘加 \_mm256\_fmadd\_ps，而是拆分成了 \_mm256\_mul\_ps 与 \_mm256\_add\_ps16。

## **第二部分 工具链实证边界与极限现代 C++20/23/26 重构蓝图**

### **编译器与标准库特性准入边界**

极致的现代 C++ 重构必须建立在生产编译器实证支持度之上，既要最大化榨干 C++20/23/26 表达力，也要严防引入未落地的草案特性或编译器已知缺陷阻断主干发布。结合截至 2026 年 11 月的工具链实况，构建如下特性准入与规避基准。

| 标准特性与提案 | MSVC 14.51 (cl 19.51 / VS 2026\) | GCC 16.1 (libstdc++) | Zig 0.13 (LLVM 18 libc++) | CineRTX 重构采用与裁剪裁决 |
| :---- | :---- | :---- | :---- | :---- |
| **unique\_ptr / jthread / stop\_token / span / ranges / format** (C++20) | 全面支持11 | 全面支持22 | jthread 与 stop\_token 需额外开启 \-fexperimental-library21 | **全仓强制底座**：全面取代一切自建 RAII 壳与线程包装器；清理 Zig 0.13 强加的实验性编译开关负担21 |
| **expected / mdspan / flat\_map / move\_only\_function / static operator() / Deducing this** (C++23) | STL 更新 202604 已提供 expected、mdspan、move\_only\_function、flat\_map（社区实测 19.51 已定义 \_\_cpp\_lib\_flat\_map）；编译器支持 static operator() 与 Deducing this8 | 全面支持22 | 已知缺失 std::out\_ptr 且 C++23 库完备度落后于主流发布链23 | **主力消肿武器**：全面接管错误处理、多维视图、可移动回调与消除 const\_cast 重载；注意避免在 mdspan 代码上启用 /Wall 以防触发 MSVC C5246 误报31 |
| **std::out\_ptr (P1132)** (C++23) | 已实现，但代理对象缺 operator\* 无法直喂 Win32 宏 IID\_PPV\_ARGS23 | 已实现 | 未实现23 | **禁止直接套用 IID\_PPV\_ARGS**：全仓 COM 出参统一收口为 3 行无类模板自由函数两段式收养30 |
| **std::inplace\_vector (P0843)** (C++26) | microsoft/STL\#4766 截至 2026 年仍处于 Open 状态，14.51 尚不可假定就绪9 | 已支持22 | 未实现 | **条件启用的边界控制**：栈定容集合统一采用 std::array \+ std::span 切片表达，仅在 \_\_cpp\_lib\_inplace\_vector 宏就绪后平滑切换 |
| **静态反射 ^^ / std::meta::info (P2996/P3394)** (C++26) | 截至 14.51/14.53 无实现与公开落地日期7 | 在 \-std=c++26 \-freflection 下可用22 | 未实现 | **拔除跨编译器离线代码生成**：撤除 GCC 16 体外生成器 schema/，在主线用 C++23 constexpr 成员指针表单链内生实现1 |
| **\#embed** (C23 / C++26) | 14.50–14.53 尚未支持7 | 支持 | 不稳定 | **维持 CMake 构建期生成**：PTX 与 HLSL 继续在构建目录生成头文件，测试素材保持物理 .mkv 文件部署21 |
| **Contracts (pre / post)** (C++26) | 2026 年 10 月 ISO C++26 DIS 投票因 Contracts 遭否决并重开审议，主流商业编译链未普及35 | 仅实验变体支持34 | 不支持 | **以静态类型系统替代**：通过 T& 非空引用、std::span 尺寸契约与 consteval 自检实现零开销不变量锁定 |
| **编译器特定前端/优化陷阱** | 社区实测报告显示 cl 19.51 在 /arch:AVX2 /O2 下对 std::hash\<float\> 存在 \+0.0f 与 \-0.0f 哈希不等优化缺陷（称已在 19.52.37030 修复）；另 14.52 才引入 opt-in /Zc:cmath 常量计算10 | 需配合 \-mfma 启用 FMA 指令 | 无 | **零风险规避**：绝不以裸 float 作无序容器键（时间基锁定整型 Flicks）；常量表不用依赖 /Zc:cmath 的 std::cos consteval 求值，以杜绝引入额外编译选项5 |

### **全仓自定义类与结构体“零容忍”处置总表**

依据“只要能用标准库特性实现就绝对不允许存在，任何存续自定义类型必须严格落入六类白名单并给出不可替代性物理证据”的最高洁癖原则23，对 Phase 11.3 及现存核心类型作出如下裁决。

| 类型名称与源码位置 | 现状诊断 | 极限现代 C++ 处置裁决 | 标准库替代方案或唯一合法存续理据（6 类白名单） |
| :---- | :---- | :---- | :---- |
| **AddClipCommand、TrimClipCommand、SplitClipCommand、MoveClipCommand、DeleteClipCommand、ReorderTracksCommand、RelinkAssetCommand、NleEditCommand、NleEditKind** (nle\_project.h)5 | 9 个具名类型堆砌；平铺成员与 4 个死字段违背注释契约4 | **物理消灭（-9 类型）** | 合并为纯代数类型别名 using NleEditDelta \= std::variant\<...\>（由 6 组互异 std::tuple/std::pair 组成，Add/Delete 共用 std::pair\<bool, TimelineClip\>），配合 std::visit 穷举分发 |
| **EditHistory** (nle\_project.h)5 | 封装双 std::vector 与 256 深限的状态壳5 | **物理消灭（-1 类型）** | 降维为 using EditStacks \= std::pair\<std::deque\<NleEditDelta\>, std::deque\<NleEditDelta\>\>; 搭配自由函数（std::deque 同时消灭 vector::erase(begin()) 的 O(N) 搬移） |
| **VideoClaim** (render\_graph.cpp)3 | 冗余复制片段字段并导致 AuditTypes 失败1 | **物理消灭（-1 类型）** | 在单遍事件坐标扫描线算法中彻底消失，不留任何中间结构体 |
| **CompiledRenderGraph** (render\_graph.h)14 | 包装 std::vector\<RenderGraphSegment\> 与可衍生缓存值14 | **物理消灭（-1 类型）** | 降维为自由函数 compileRenderGraph(const NleProject&) 返回 Result\<std::vector\<RenderGraphSegment\>\> |
| **VoicePrep、AudioMixerScratchpad** (audio\_mixer\_avx2.h)16 | 裸指针+帧数不安全结构体；手写定容数组壳16 | **物理消灭（-2 类型）** | VoicePrep 替换为 using AudioVoice \= std::pair\<std::span\<const float\>, float\>;；AudioMixerScratchpad 替换为调用方栈数组 std::array\<AudioVoice, 32\> 与 std::span 切片 |
| **IndirectDeleter** (playback\_session\_audio.cpp 等)39 | 与 FnDeleter 重复造轮子的第二套删除器模板39 | **物理消灭（-1 类型）** | 并入 raii\_owners.h 唯一豁免模板 FnDeleter，以 C++20 requires 表达式自动分派 Fn(p) 与 Fn(\&p) |
| **FormatMapping** (d3d12\_interop.cpp)40 | 纯静态格式查表结构体40 | **物理消灭（-1 类型）** | 替换为 constexpr std::array\<std::tuple\<int32\_t, DXGI\_FORMAT, CUarray\_format, unsigned int, uint32\_t\>, N\>40 |
| **NleWindowState** (workstation\_canvas.cpp)6 | 通过 GWLP\_USERDATA 手动 release()/delete 的 11 字段堆对象6 | **消除堆所有权模式** | 收归 WorkstationApp 按值持有的视图状态元组，随主窗声明周期自动析构，彻底删除 release() 裸指针逃逸 |
| **Row、FieldBinding、CliFlagEntry 及 schema\_model 注解标签** (schema/\*)33 | 体外 GCC 16 代码生成器附属类群33 | **随 schema/ 整体消灭** | 替换为 include/rtx/param\_schema.h 单一 std::array\<std::pair\<ParamMeta, ProfileMemberPtr\>, 20\> 常量描述符表 |
| **PlaybackClockState** (playback\_session.h)13 | 6 元组存在 bool paused 状态撕裂与 std::get\<3/5\> 幻数坑13 | **压缩降维为 4 元组** | 改用 std::tuple\<IAudioMasterClock\*, std::optional\<steady\_clock::time\_point\>, Flicks, double\>：以 std::nullopt 唯一表达暂停，强制结构化绑定解包 |
| **CudaDeviceBuffer** (raii\_owners.h)30 | 非指针 64 位显存地址 RAII 壳30 | **精简存续（Hardware-Layout）** | uint64\_t CUdeviceptr 不满足 NullablePointer 语义且绝不可与主机指针混用；须将内部 std::function 升级为 std::move\_only\_function\<void()\> |
| **TelemetrySnapshot / ViewportConstants / Rtx\*ABI**23 | 硬件常量寄存器与跨模块 C ABI23 | **合法存续（C-ABI / Hardware-Layout）** | alignas(64)、HLSL 12-DWORD 根常量与 MIT/Apache-2.0 纯 C ABI 导出布局受物理硬件与二进制边界约束23 |
| **IVideoFrameSink / IDemuxer / IMuxer / IAudioMasterClock**13 | 跨子系统与 Mock 隔离虚基类13 | **合法存续（Polymorphic-Boundary）** | 承担 FFmpeg、WASAPI、D3D12 与单元测试桩的运行时多态边界隔离13 |
| **PreviewViewport / GpuRetirementQueue / MediaPlaybackSession / WasapiAudioOutput / NvidiaTranscodeSession / IncrementalFlicksAccumulator / WsolaStreamState / AtomicTelemetryBus**23 | 多资源逆序析构状态机、进位累加器与无锁 Seqlock23 | **合法存续（NonTrivial-StateMachine / Concurrency-Invariant）** | 维护异构资源严苛析构序（先停线程/Fence 退役再毁宿主）、跨块状态进位或原子序列屏障；WsolaStreamState 须移除每会话动态 hann 向量 |
| **NleMediaAsset / TimelineClip / NleProject / RenderGraphSegment / TranscodeProfile / MediaInfo**5 | 核心跨层领域契约5 | **精简存续（Domain-Model）** | 将 NleClip 更名为 TimelineClip 彻底脱离旧正则冲突；NleProject 用 Deducing this 消除重载并拔除冗余 Getter/Setter，仅存具名持久化模型 |

### **核心模块极限现代 C++23/26 靶向重构示例**

#### **1\. raii\_owners.h 与 d3d12\_raii.h：融合 C++20 requires、C++23 static operator() 与 std::move\_only\_function**

将 playback\_session\_audio.cpp 等多处的 IndirectDeleter 彻底拔除39，由 raii\_owners.h 唯一的 FnDeleter 模板在编译期通过 requires 自动分派单级与二级指针释放器，并用 C++23 static operator() 消去空状态传递 this 指针的寄存器开销25；同时在 d3d12\_raii.h 中将 std::function 升级为仅支持移动且允许存储 move-only 闭包的 std::move\_only\_function45，并以 std::erase\_if 取代手动下标擦除循环：

C++  
// include/rtx/raii\_owners.h  
template \<auto Fn\>  
struct FnDeleter {  
    static void operator()(auto\* p) noexcept {  
        if (\!p) return;  
        if constexpr (requires { Fn(p); }) {  
            Fn(p);  
        } else {  
            Fn(\&p); // 统一接管 avcodec\_free\_context / swr\_free / av\_frame\_free / av\_packet\_free  
        }  
    }  
};

// src/gui/d3d12\_raii.h  
using Entry \= std::tuple\<std::uint64\_t, ComPtr\<IUnknown\>, std::move\_only\_function\<void()\>\>;

std::size\_t collect(std::uint64\_t completed\_value) noexcept {  
    if (completed\_value \== UINT64\_MAX) {  
        device\_lost\_seen\_ \= true;  
        releaseAll();  
        return 0;  
    }  
    return std::erase\_if(entries\_, \[completed\_value\](Entry& e) noexcept {  
        if (std::get(e) \> completed\_value) return false;  
        if (auto& hook \= std::get(e)) hook();  
        return true;  
    });  
}

#### **2\.** nle\_project.h/.cpp**：零命令类的纯** std::variant **代数增量化与 Deducing** this

物理销毁 7 个独立命令类、NleEditCommand、NleEditKind 及 EditHistory 外壳，改用纯粹由标准库元组与变体构成的代数和类型 NleEditDelta5。NleProject 内部利用 C++23 Deducing this 合并 const 与非 const 查找重载并彻底清除 const\_cast4；toJson() 采用前置逗号拼接彻底消灭末尾非法逗号4；fromJson() 移除 throw/catch 控制流，全链路直接传递 std::expected 错误：

C++  
// include/rtx/nle\_project.h  
using NleEditDelta \= std::variant\<  
    std::pair\<bool /\*is\_delete\*/, TimelineClip\>,                                                   // Add / Delete  
    std::tuple\<uint64\_t /\*clip\_id\*/, std::pair\<int64\_t, int64\_t\>, std::pair\<int64\_t, int64\_t\>\>,   // Trim (old, new)  
    std::tuple\<uint64\_t /\*kept\_id\*/, uint64\_t /\*created\_id\*/, int64\_t /\*at\*/, int64\_t /\*old\_out\*/\>, // Split  
    std::tuple\<uint64\_t /\*clip\_id\*/, std::pair\<int, int64\_t\>, std::pair\<int, int64\_t\>\>,           // Move (old, new)  
    std::pair\<std::array\<int8\_t, kNleTrackCount\>, std::array\<int8\_t, kNleTrackCount\>\>,            // ReorderTracks  
    std::tuple\<uint64\_t /\*asset\_id\*/, NleMediaAsset /\*prev\*/, NleMediaAsset /\*verified\*/\>         // RelinkAsset  
\>;  
using EditStacks \= std::pair\<std::deque\<NleEditDelta\>, std::deque\<NleEditDelta\>\>;

struct NleProject {  
    // 成员直暴露或受控修改入口：以 Deducing this 单模板消除 const\_cast 重复定义  
    template \<typename Self\>  
    \[\[nodiscard\]\] auto\* clip(this Self& self, uint64\_t id) noexcept {  
        auto it \= std::ranges::find(self.clips, id, \&TimelineClip::id);  
        return it \== self.clips.end() ? nullptr : &\*it;  
    }  
    // ...  
};

#### **3\.** render\_graph.h/.cpp**：单遍离散事件扫描线消除** VideoClaim **与多轮** std::vector **分配**

物理删除 VideoClaim、claimInterval()、visibleSpans() 与 CompiledRenderGraph 包装类3。收集全部视频片段起止时间点并排序去重形成原子时间切片 \[t0, t1)，在单遍遍历中以 std::views::filter 与 std::ranges::min\_element 投影直接裁决最上层胜出片段，并即时完成相邻同源连续段合并与硬切 force\_keyframe / reset\_temporal 置位：

C++  
// src/core/render\_graph.cpp  
Result\<std::vector\<RenderGraphSegment\>\> compileRenderGraph(const NleProject& project) {  
    std::vector\<const TimelineClip\*\> video\_clips;  
    for (const auto& c : project.clips) {  
        if (nleTrackKind(c.lane) \== NleTrackKind::Video && nleClipDuration(c) \> 0) {  
            video\_clips.push\_back(\&c);  
        }  
    }  
    if (video\_clips.empty()) {  
        return err(ErrorCode::InvalidArgument, "render graph: the project has no video clips");  
    }

    std::vector\<int64\_t\> cuts;  
    cuts.reserve(video\_clips.size() \* 2);  
    for (const TimelineClip\* c : video\_clips) {  
        cuts.push\_back(c-\>start\_flicks);  
        cuts.push\_back(c-\>start\_flicks \+ nleClipDuration(\*c));  
    }  
    std::ranges::sort(cuts);  
    const auto \[dup\_begin, dup\_end\] \= std::ranges::unique(cuts);  
    cuts.erase(dup\_begin, dup\_end);

    std::vector\<RenderGraphSegment\> segments;  
    for (size\_t i \= 0; i \+ 1 \< cuts.size(); \++i) {  
        const int64\_t t0 \= cuts\[i\];  
        const int64\_t t1 \= cuts\[i \+ 1\];  
        auto covering \= video\_clips \| std::views::filter(\[t0, t1\](const TimelineClip\* c) noexcept {  
            return c-\>start\_flicks \<= t0 && (c-\>start\_flicks \+ nleClipDuration(\*c)) \>= t1;  
        });  
        auto winner\_it \= std::ranges::min\_element(covering, std::ranges::less{},  
            \[&\](const TimelineClip\* c) noexcept {  
                return std::pair{project.lane\_stack\[static\_cast\<size\_t\>(c-\>lane)\], c-\>id};  
            });  
        if (winner\_it \== covering.end()) continue;

        const TimelineClip\* w \= \*winner\_it;  
        const auto\* asset \= project.asset(w-\>asset\_id);  
        if (\!asset) continue;

        const int64\_t src\_in \= w-\>in\_flicks \+ (t0 \- w-\>start\_flicks);  
        const int64\_t src\_out \= src\_in \+ (t1 \- t0);  
        if (\!segments.empty()) {  
            auto& prev \= segments.back();  
            if (prev.timeline\_out\_flicks \== t0 &&  
                prev.source\_path \== asset-\>path &&  
                prev.source\_out\_flicks \== src\_in) {  
                prev.timeline\_out\_flicks \= t1;  
                prev.source\_out\_flicks \= src\_out;  
                continue;  
            }  
        }  
        const bool hard\_cut \= \!segments.empty();  
        segments.push\_back({asset-\>path, src\_in, src\_out, t0, t1, hard\_cut, hard\_cut});  
    }  
    if (segments.empty()) {  
        return err(ErrorCode::InvalidArgument, "render graph: no visible video spans");  
    }  
    return segments;  
}

#### **4\.** audio\_mixer\_avx2**、**wsola\_stretch.cpp **与** playback\_session**：**\_mm256\_fmadd\_ps**、**std::mdspan **与彻底拔除全局** operator new

必须立即从 tests/test\_audio\_mixer\_avx2.cpp 拔除全部 60 行全局 operator new/delete 替换代码1。热循环零分配验证应当通过两道正交防线落地：其一是在契约层彻底废除 VoicePrep 与 AudioMixerScratchpad，令 mixAvx2 仅接收 std::span\<const std::pair\<std::span\<const float\>, float\>\> voices 与 std::span\<float\> dst 并标定 noexcept16（playback\_session\_audio.cpp 单轨接入只需栈上构造单元素 std::array39，多轨场景由调用方维护栈定容数组）；其二是在 Debug 运行时分配审计中，对通用测试目标禁止篡改全局分配器，改以仅在局部测试作用域内生效的 Win32 Debug CRT \_CrtSetAllocHook（或对接收 std::pmr::memory\_resource\* 的组件注入 std::pmr::null\_memory\_resource()，亦或将 operator new 隔离于独立单文件 CTest 进程目标中，绝不与其余 120 余项测试共享同一可执行进程）。  
在算子与内存视图层面，mixAvx2() 在 AVX2 分支（配合 \`\#if defined(**AVX2**) && (defined(\_MSC\_VER) \|  
\| defined(**FMA**))）升级为真正单指令融合乘加 \_mm256\_fmadd\_ps\[cite: 18, 19\]。在wsola\_stretch.cpp中，将固定不变的kWsolaWindow周期汉宁窗由每个会话单独堆分配std::vector 改为全局共享只读静态表（static const一次性初始化，不依赖额外/Zc:cmath开关）\[cite: 10, 17\]，并引入 C++23std::mdspan\<float, std::dextents\<size\_t, 2\>\>将交错音频缓冲封装为\[frame, channel\]二维视图，直接以ring\_view\[frame, ch\]与ola\_view\[i, ch\]访问样本，彻底消灭全篇易错的i \* ch \+ c手工步长算术8。同步将PlaybackClockState从易误用索引std::get\<3/5\>的 6 元组压缩为以std::optional\<steady\_clock::time\_point\>\` 编码暂停态的 4 元组，并在函数内部一律以结构化绑定命名解包13。

## **第三部分 奥卡姆剃刀清洗：多余防御、全局竞态与 CMake 编译杂质拔除**

### **物理清退冗余防御性代码**

严格执行“契约在边界一次性校验，核心域内部以不可空引用与范围视图传递”的现代 C++ 准则，全仓必须彻底拔除三类无效防御性检查。首先，在 src/gui/workstation\_canvas.cpp 中，由于 WorkstationApp 错误地将轻量化纯数据结构 NleProject 与 EditHistory 封装在 std::unique\_ptr 中延迟初始化，导致 refreshBinListbox、addFilesToBin、onBinAssetToTimeline、updateNleTimecode、onNleMarkIn、onNleMarkOut、onNleRazor、onNleDeleteSelected、onNleUndo、onNleRedo、exportSelectedClipToEncoder、saveNleSessionSnapshot、restoreNleSessionSnapshot、onNleExportSequence 共 14 个业务入口及 paintNleTimeline 机械重复了 15 次 g\_app.nleProject \== nullptr 或 project \== nullptr 的死判空防御6。将其改为在 WorkstationApp 内直接按值持有 NleProject 与 EditStacks 并以 NleProject& 引用传入各处理器后，全部 15 处冗余空指针检查在物理层直接消失。  
其次，消灭裸指针与视图长度拆散导致的重复校验。现行 audio\_mixer\_avx2.cpp 因 VoicePrep 暴露裸 const float\* 被迫在混音内层反复判断 if (voice.interleaved \== nullptr) continue;18；切换为 std::span\<const float\> 后，空输入自然长度为零且尺寸受类型系统绑定。同样，nleTimelineProc 不再通过 GWLP\_USERDATA 执行 owned.release() 并在 paintNleTimeline 中构造 NleWindowState fallback{} 兜底6，而统一绑定到宿主的确定性生命周期成员。最后，在领域边界锁定外键不变量：NleProject 仅允许包含合法 asset\_id 索引（fromJson() 与编辑命令执行时已拦截校验）4，因此下游 compileRenderGraph 无需在每轮遍历中重复猜测资产是否意外失踪3。

### **拔除全局可变状态、数据竞态与认知陷阱**

workstation\_canvas.cpp 匿名命名空间与局部静态中游离的 g\_guiTranscoding、g\_activeWorkspace、g\_guiPlaying 与 guiProfile() 属于典型的隐藏状态污染，特别是 onNleExportSequence() 的后台 std::jthread 直接跨线程修改非原子 g\_guiTranscoding 与 g\_app.nleExportStatus 并向裸 HWND 投递消息，埋下极高危的并发竞争与野句柄陷阱6。重构中须将上述全部界面状态收归唯一宿主实例，跨线程忙闲标志严格限定为 std::atomic\<bool\>，导出线程结果通过转移堆所有权消息或无锁单写槽回传主线程，并在 WM\_DESTROY 析构序列中保证先 request\_stop() 与 join() 导出线程再销毁窗口句柄6。与此同时，将 NleClip 更名为 TimelineClip 并统一重命名 paintNleTimeline、registerNleTimelineWindowClass 以彻底消除命名撞车与旧遗产残余，把 seekPlaybackFromSlider() 的裸整数乘除归并至 std::chrono::duration\_cast\<Flicks\>，拔除所有引发阅读误判的名实不符注释1。

### **CMake 构建系统深度脱毒清洗**

现行根目录 CMakeLists.txt（实测 807 行）累积了大量历史妥协分支、自相矛盾的标准属性与危险全局选项，必须依据 KISS 原则逐项清洗21。

| CMakeLists.txt 病灶位置 | 现行违背原则的构建污染 | 极简脱毒重构指令 |
| :---- | :---- | :---- |
| **C++ 标准指令分裂与重复**21 | rtx\_project\_options 同时设 cxx\_std\_23 与 MSVC /std:c++latest，而 5 个下游目标又重复硬编码 CXX\_STANDARD 23 / REQUIRED / EXTENSIONS21 | 删除全部 5 个目标的重复 CXX\_STANDARD 属性块，仅在 rtx\_project\_options 单点管控（cl 19.51 保留 /std:c++latest 以开启 C++23/26 特性集，待工具链全面升至 14.52+ 后直接切为 /std:c++23）10 |
| **僵尸块与镜像条件分支**21 | 存在 if(TRUE) 包裹全部测试、空 if(WIN32) endif()、相邻双重 if(WIN32) 链接块、以及 if(WIN32) 与 else() 完全雷同的 rtx::ngx\_bridge 别名代码21 | 物理删除 if(TRUE) 与空条件块；合并同目标 if(WIN32)；将重复别名与公共链接提升至条件外部 |
| **PTX 双重执行与路径重复赋值**21 | RTX\_EXTERNAL\_DIR 重复赋值两次；EmbedPtx.cmake 既在 configure 阶段执行 execute\_process 又在 build 阶段定义 add\_custom\_command21 | 移除重复变量赋值与 configure 阶段冗余 execute\_process，仅以构建期依赖规则触发 PTX 头生成 |
| **危险编译器开关与全局配置篡改**21 | 全局字符串替换 CMAKE\_CXX\_FLAGS\_RELWITHDEBINFO（/O2 改 /O1）隐式影响全仓；附加 /Gs- 禁用栈探测存在溢出风险；rtx\_ngx\_bridge\_static 同时配置 PRIVATE 与 SYSTEM PRIVATE 同一头目录导致屏蔽失效；全局 /wd4324 抑制对齐告警21 | 仅针对 CineRTX 主程序用生成器表达式限定 /O1 与 /Gy；**立即拔除危险开关** /Gs-；删除重复非 SYSTEM 头路径；收敛全局 /wd4324 至具体对齐边界 |
| **多工具链警告抑制与隐式网络下载**21 | 全局挂满 6 项 \-Wno-\* 且在子库重复追加；configure 期默认静默从外部 URL 下载 ONNX 权重；为兼容 Zig 0.13 LLVM 18 额外注入 \-fexperimental-library 与空归档阴影库21 | 删除子目标重复 \-Wno-\* 抑制；为外部制品拉取增加显式缓存/离线门控选项；不再允许旧版 Zig 0.13 缺失标准库特性持续绑架主干（统一面向支持生产级 C++23/26 的 MSVC 14.51+ 与 GCC 16+ 工具链，消除实验性编译开关） |

## **第四部分 根域资产纯洁性裁决：物理铲除** scripts/ **与** schema/

经盘点确认，仓库根目录的 scripts/ 与 schema/ 目录均未进入 ISSUES.md 与 DECISIONS.md 受控台账，是违反源码树纯洁性铁律与奥卡姆剃刀原则的典型沉没垃圾1。首席领域专家组正式下达物理铲除授权：**立即执行对** scripts/ **与** schema/ **全目录的物理删除（**git rm \-r scripts schema**）**1。  
具体而言，scripts/reimplant\_phase112r\_comments.py 所服务的一次性注释归档文件已于 Phase 11.2-R 物理销毁，脚本本身已无任何输入源与存在意义；scripts/gate\_r8\_desktop\_check.ps1 作为游离于构建体系外的 PowerShell 脚本，应由标准 CTest 与原生 C++ 桌面自动化夹具 test\_desktop\_e2e 直接承接1。更具颠覆性的是 schema/ 目录：仅核心三文件就占据 621 物理行，内含一个编译后运行的 20 KB 独立代码生成器 reflection\_codegen.cpp（甚至手写了近 90 行 SHA-256 哈希算法）、孤立脚本 verify\_ssot.py 以及被强行签入版本库的 3 份派生构建产物（schema/generated/\*），不仅严重违背“严禁生成物入库”准则，还因 MSVC 14.51 尚未实现 P2996 静态反射而人为割裂为 GCC 16 体外预处理流程1。  
在 C++23 编译环境下，20 项纯 GPU 参数契约完全无需独立代码生成器，直接在 include/rtx/param\_schema.h 内生为单份编译期成员指针描述符数组，配合 consteval 自检函数在每次 MSVC/GCC 编译期直接完成键名与 CLI 标志的唯一性、范围合法性静态拦截：

C++  
// include/rtx/param\_schema.h：以 40 余行原生 C++23 彻底替代 621+ 行 schema/ 体外生成器  
using ProfileMemberPtr \= std::variant\<std::string TranscodeProfile::\*, int TranscodeProfile::\*, bool TranscodeProfile::\*\>;

inline constexpr std::array\<std::pair\<ParamMeta, ProfileMemberPtr\>, 20\> kParamDescriptors{{  
    {{.json\_key \= "input",  .cli\_flag \= "--input",  .kind \= ParamKind::String, .label\_id \= StrId::ParamInput},  \&TranscodeProfile::input},  
    {{.json\_key \= "output", .cli\_flag \= "--output", .kind \= ParamKind::String, .label\_id \= StrId::ParamOutput}, \&TranscodeProfile::output},  
    // ... 其余 18 项参数按同式一一绑定，三端唯一真相源（SSOT） ...  
}};

consteval bool validateParamDescriptors() {  
    for (std::size\_t i \= 0; i \< kParamDescriptors.size(); \++i) {  
        const auto& a \= kParamDescriptors\[i\].first;  
        if (a.json\_key.empty()) return false;  
        if (a.kind \== ParamKind::Int && (a.default\_int \< a.min\_val \|\| a.default\_int \> a.max\_val)) return false;  
        for (std::size\_t j \= i \+ 1; j \< kParamDescriptors.size(); \++j) {  
            const auto& b \= kParamDescriptors\[j\].first;  
            if (a.json\_key \== b.json\_key) return false;  
            if (\!a.cli\_flag.empty() && a.cli\_flag \== b.cli\_flag) return false;  
        }  
    }  
    return true;  
}  
static\_assert(validateParamDescriptors(), "Duplicate or out-of-range SSOT parameter descriptor");

template \<typename Profile, typename Visitor\>  
constexpr void forEachProfileField(Profile& profile, Visitor&& visitor) {  
    for (const auto& \[meta, ptr\] : kParamDescriptors) {  
        std::visit(\[&\](auto member) { visitor(meta, profile.\*member); }, ptr);  
    }  
}

随后在 src/core/param\_schema.cpp 中直接令 exportSchemaJson() 与 CLI 参数绑定查找消费 kParamDescriptors（以 std::views::filter 排除无值反转开关），并在 tests/test\_param\_schema\_reflection.cpp 中维持全部 20 项参数 JSON 往返、CLI 映射与边界校验测试通过，即可在不损失任何一层契约保障的前提下彻底归零 schema/ 生成器体系48。

## **第五部分 测试期“嘟嘟嘟”蜂鸣声法医根因确认与物理消解指令**

交叉核验 tests/test\_playback\_session.cpp:480-506 与 tests/test\_raii\_retirement.cpp:529-596 源码，完全印证 37 号委托书所锁定的双源并发声学与内核警报机制1。在 playback\_wasapi\_shared\_mode\_master\_clock 测试用例中，代码在激活 Windows 默认物理 WASAPI 渲染端点后，显式通过 0.2f \* std::sin(2.0f \* pi \* 440.0f \* ...) 合成了标准的 A4（440 Hz）可闻正弦波样本，并在首次提交、30 ms 连续推流以及暂停恢复阶段多次注入物理声卡，在宿主机扬声器上直接发出刺耳的连续“嘟——嘟——嘟”脉冲音1。紧接其后按字母序执行的 preview\_viewport\_100x\_create\_destroy\_zero\_leak 则在缺乏 Win32 消息循环的测试线程上创建 WS\_OVERLAPPEDWINDOW 父窗口并 100 次循环创建、调整及销毁 PreviewViewport（其内部子窗口带有 WS\_VISIBLE 属性），同时在退役等待循环内单纯执行 Sleep(5) 阻塞轮询1。大量堆积的窗口绘制与 DWM 状态消息瞬间塞满测试线程队列，一旦运行期发生键鼠微动，内核子系统 win32k.sys 因焦点队列失聪无法派发事件，便触发 Typeahead Buffer Overflow 内核警报声（MessageBeep）1。  
为根治测试期间的一切可闻噪音与系统级队列阻塞，签署以下三步立刻生效的源码修补指令：

> 1. **WASAPI 音频时钟单测零分贝哑音化（**tests/test\_playback\_session.cpp**）**：删除第 482–487 行 std::sin(... 440.0f ...) 正弦波合成循环，将 interleaved 直接初始化为全零静音浮点样本 std::vector\<float\> interleaved(frames \* channels, 0.0f);13。WASAPI 共享混音器对 0.0f 静音帧的缓冲消耗、IAudioClock::GetPosition() 硬件时钟推进、writtenFrames() 计数与启停恢复状态转换与非零正弦波完全等价，可在保持 100% 时钟验证语义的同时实现 0 dB 物理静音。  
> 2. **Win32 压测非阻塞消息泵抽水与窗口降噪（**tests/test\_raii\_retirement.cpp**）**：在匿名命名空间加入仅 6 行的标准非阻塞消息泵 pumpThreadMessages()（循环执行 PeekMessageW(\&msg, nullptr, 0, 0, PM\_REMOVE) 并分发），并在 preview\_viewport\_100x\_create\_destroy\_zero\_leak 的 100 次循环内部及退役等待处以 MsgWaitForMultipleObjects(0, nullptr, FALSE, 5, QS\_ALLINPUT) 配合 pumpThreadMessages() 替换死睡 Sleep(5)，确保测试线程 Win32 消息队列始终排空清澈1。  
> 3. **测试子窗口无头隔离加固（**src/gui/preview\_viewport.cpp **/** tests/test\_raii\_retirement.cpp**）**：由于交互式桌面测试下 CreateSwapChainForComposition 与 IDCompositionTarget::SetRoot 需要真实常规窗口而非 HWND\_MESSAGE 伪窗口句柄，保留普通隐藏 HWND 父窗口（CreateWindowExW 不含 WS\_VISIBLE）的同时，在 PreviewViewport 创建子窗口时依据父窗口不可见状态避免强行触发桌面可见重绘风暴，既保住 Gate R-8 真机合成验证，又彻底切断向全局桌面投递冗余重绘与输入阻塞的链条1。

## **第六部分 Phase 11.3-R 收口平账计划与中长期演进路线**

### **Phase 11.3-R 原子整改序列与同口径 Pure SLOC 平账核算**

鉴于被驳回的 Phase 11.3 代码已在结构与体量上严重偏航，严禁在污染分支上缝补，应当以基准提交 749fe0d（AuditSloc.cmake 锁定 135 文件，31,064 Pure SLOC）1 为重置零点，开展为期 2–3 个工作日的 **Phase 11.3-R（极限现代 C++ 重构与 NLE 合龙）**，严格按以下四步原子执行序列推进：

> 1. **清理根域死滞资产**：物理删除 scripts/ 与 schema/ 目录，在 include/rtx/param\_schema.h 与 src/core/param\_schema.cpp 落地 C++23 内生 constexpr 描述符表与 consteval 自检1。  
> 2. **脱毒测试噪音与清洗构建脚本**：应用 WASAPI 静音化与 Win32 消息泵抽水补丁1；执行 CMakeLists.txt 去冗清洗（移除 if(TRUE)、全局 /O2-\>/O1 改写、/Gs- 开关、重复 CXX\_STANDARD 属性与 Zig 0.13 实验性宏税）21。  
> 3. **极限 Std-First 重写核心域与单测隔离**：合并 FnDeleter 消除 IndirectDeleter30；压缩 PlaybackClockState 为 4 元组并全部改用结构化绑定13；以 6 支 std::variant 代数类型 \+ Deducing this \+ std::ranges 重写 nle\_project.h/.cpp 并修复 toJson() 尾逗号与无异常错误路径4；以单遍坐标扫描线重写 render\_graph 消灭 VideoClaim 与 CompiledRenderGraph3；将 mixAvx2 升级为 \_mm256\_fmadd\_ps \+ std::span（淘汰 VoicePrep/AudioMixerScratchpad）16、wsola\_stretch.cpp 升级为 std::mdspan \+ 只读共享 hann8，并用局部 Debug CRT 分配钩子或独立分配测试目标替换全局 operator new 劫持1。  
> 4. **收口 GUI 与全链路真机合龙**：在 workstation\_canvas.cpp 将 NleClip 更名为 TimelineClip 并消除 NleTimeline 子串与 seekPlaybackFromSlider 裸换算6，改用按值持有+NleProject& 拔除 15 处空指针防御与 g\_guiTranscoding 竞态6，保留 C-05 encode\_frame.keyframe 硬切 IDR 修复并完成多段 1V+1A 容器导出逐帧核验1。

为防止重演历史文档中的统计口径漂移，下表在 749fe0d 入场基线上透明列示删减估算与作用域变更（scripts/ 与 schema/generated/ 本就不在 AuditSloc.cmake 的 135 文件口径内，而 schema/ 核心三文件共 621 物理行位于原基线口径内32，重构后将同步从 AuditSloc.cmake 列表移除并在账本中独立透明记账；最终读数以重构后运行 cmake \-P cmake/AuditSloc.cmake 实测为准）：

| SLOC 平账账目项 | 口径基线 / 预估变化量（估算值） | 工程依据与透明披露说明 |
| :---- | :---- | :---- |
| **入场法定基准（Commit** 749fe0d**）** | **31,064 Pure SLOC**（135 文件）1 | AuditSloc.cmake 锁定（含 38,324 物理行与 4,732 语义注释行）1 |
| **拔除** schema/ **3 个核心文件并切至 C++23 内生表** | **预计净减约 \-430 至 \-480 Pure SLOC** | 移除原属基线的 schema/ 核心 3 文件（621 物理行32，剔除注释空行约 480–520 Pure SLOC）扣除 param\_schema.h 约 45 行增量；同步移除 param\_schema.cpp 重复逻辑约 50 行32 |
| **清洗** CMakeLists.txt **与旧画布/基础设施冗余** | **预计净减约 \-350 至 \-450 Pure SLOC** | 精简 CMakeLists.txt（807 行32）僵尸块、重复 CXX\_STANDARD 与冗长配置；清退 workstation\_canvas.cpp 15 处判空、堆 NleWindowState 胶水与旧函数名6；压缩 PlaybackClockState、IndirectDeleter 与 FormatMapping39 |
| **Std-First 重写 Phase 11.3 NLE 与音频总线** | **预计可控净增约 \+420 至 \+520 Pure SLOC** | 相比被驳回版的 \+1,316 行膨胀1，经代数 std::variant、ranges、单遍扫描线、std::mdspan 与去掉全局 operator new 后可削减超 60% 新增体量3 |
| **Phase 11.3-R 预计结项水位** | **约 30,500 至 30,660 Pure SLOC** | **达成净减 400–560 行（\<= 30,664 红色上限）**2，其中即使剔除 schema/ 作用域缩减因素，纯核心与 GUI 增量亦被压制在极低净持平水位，实质推进 Phase 11.2-R 结转赤字平账20 |

### **准出验收硬门禁与 Phase 11.4 至 v0.4.0 演进方向**

Phase 11.3-R 合龙时必须满足五项硬性验收门禁方准解除主干冻结：（1）AuditTypes.cmake 返回 0 违规且全部存续自定义类型 100% 精准归入六类白名单并剔除复制粘贴模板；（2）AuditSloc.cmake 实测 Pure SLOC \<= 30,664 行，同时严禁删减任何真实语义注释或 SPDX 头；（3）负向正则扫描确认 workstation\_canvas.\* 残留旧符号、全局 ::operator new 劫持以及 schema//scripts/ 物理文件均为 0 命中；（4）AuditSpdx.cmake、AuditImports.cmake（维持 CineRTX.exe 位于 1.2–12.0 MB 包络且零驱动 DLL 泄漏）与 /OPT:REF /OPT:ICF 下 CineRTX.exe.map 关键非内联符号（NleProject::toJson/fromJson、compileRenderGraph、mixAvx2、wsolaStretch）物理留存全部通过2；（5）在驱动 \>= 621.24 WHQL 与交互式桌面会话（WTS Session \> 0）下实测 1V+1A 剪辑、撤销重做、Relink 与多段硬切 MP4/MKV 导出容器逐帧映射吻合，全程 0 声卡蜂鸣与 0 队列阻塞1。  
在纯净、零 Slop 的 Phase 11.3-R 底座之上，项目应严格遵循渐进式垂直切片战略向后续里程碑演进50。在即将接续的 **Phase 11.4（v0.3.0 正式封板）** 中，团队须落地 ABI v5 的 rtx\_ngx\_bridge\_init\_rife\_ex，在 rife\_engine、rife\_builder 与 rife\_pack.cu 中针对 flow\_scale \= 0.5f 实现 A \= 128 像素对齐与 reflect101 边缘镜像填充裁剪，并将 10 类坐标与算术敏感层锁定 FP32、关闭 kTF32 以规避社区实证（如 vs-mlrt 讨论）指出的张量精度不对称性能反噬43。同时将 VramAdaptiveScheduler 接入真实 DXGI 显存预算（例如 8 GB RTX 5060 实测约 7,300 MiB 可用基准）、cuMemGetInfo 与 TRT 峰值分配，按 82% 降级 / 68% 恢复滞回水印与 120 帧最小驻留约束动态调优50。编码侧则在 nv\_encoder.cpp 实现 NvEncodeAPIGetMaxSupportedVersion 双模协商与带异步事件的多槽 BitstreamSlot 环形池（修正原单槽模型遇到 NV\_ENC\_ERR\_NEED\_MORE\_INPUT 报错的隐患）43，在驱动 \>= 621.14 WHQL（规避 621.01 视频冻结已知缺陷 \[6979343\]）下完成 Video Codec SDK 13.1（Default vs 7B vs 15B/31B \+ UHQ）的全口径 FPS、VRAM（二次核验厂商与候选估算值约 166 MiB 的缓冲增量）及 PSNR/SSIM/VMAF 实测矩阵后正式封板 v0.3.043。  
迈向 **Wave 4（v0.4.0 架构扩展）** 时，CineRTX 将继续秉持无重型外部框架的极简主义推进三项高价值演进：借助 NleProject（UUID 资产、相对路径、offline 标记与整数 Flicks 片段区间）与 OpenTimelineIO 原语的天然同构性，仅以百余行复用 json\_scanner.h 的纯 C++ 映射函数打通 .otio 粗剪时间轴无损互导，无需引入 Python 或 OTIO 庞大运行时4；在 rife\_engine.cpp 对固定 A \= 128 分辨率分桶引入 CUDA Graph 捕获消除多次内核发射间隙（析构函数严格执行 RAII 图释放）50；并将单遍 compileRenderGraph 与无锁 mixAvx2 音频总线平滑扩展至多轨交叉叠化（Cross-Dissolve）节点及转码监看并行时的“进程级 NGX 单例主从让渡契约（Primary-Yield Policy）”——借鉴 moonlight-qt 实证揭示的 RTX VideoEnhancement 进程级全局单例约束，确保后台批处理转码活跃时前台视口自动让渡 NGX VSR 并仅走 6,220,800 B（约 5.93 MiB）三槽 D3D12 共享缓冲轻量色调映射呈现，在绝对代码洁癖下实现高性能桌面视频工作站的全栈闭环43。

#### **引用的著作**

> 1. [`37_CineRTX Phase 11.3 施工验收与极限现代C++重构专家咨询委托书.md`](37_CineRTX%20Phase%2011.3%20施工验收与极限现代C++重构专家咨询委托书.md)  
> 2. [`36_CineRTX Phase 11.3 SuperPower 施工单_统一NLE引擎与零分配AVX2音频总线.md`](36_CineRTX%20Phase%2011.3%20SuperPower%20施工单_统一NLE引擎与零分配AVX2音频总线.md)  
> 3. [`src/core/render_graph.cpp`](../../src/core/render_graph.cpp)  
> 4. [`src/core/nle_project.cpp`](../../src/core/nle_project.cpp)  
> 5. [`include/rtx/nle_project.h`](../../include/rtx/nle_project.h)  
> 6. [`src/gui/workstation_canvas.cpp`](../../src/gui/workstation_canvas.cpp)  
> 7. Implement C++26 Standard features in MSVC \- Developer Community, [https\://developercommunity.visualstudio.com/t/Implement-C26-Standard-features-in-MSV/10777423](https://developercommunity.visualstudio.com/t/Implement-C26-Standard-features-in-MSV/10777423)  
> 8. Changelog · microsoft/STL Wiki \- GitHub, [https\://github.com/microsoft/STL/wiki/Changelog](https://github.com/microsoft/STL/wiki/Changelog)  
> 9. P0843R14 \`\` · Issue \#4766 · microsoft/STL \- GitHub, [https\://github.com/microsoft/STL/issues/4766](https://github.com/microsoft/STL/issues/4766)  
> 10. MSVC Build Tools Version 14.52 Release Candidate Now Available, [https\://devblogs.microsoft.com/cppblog/msvc-build-tools-version-14-52-release-candidate-now-available/](https://devblogs.microsoft.com/cppblog/msvc-build-tools-version-14-52-release-candidate-now-available/)  
> 11. [https\://learn.microsoft.com/en-us/cpp/overview/visual-cpp-language-conformance?view=msvc-170](https://learn.microsoft.com/en-us/cpp/overview/visual-cpp-language-conformance?view=msvc-170)  
> 12. [`tests/test_raii_retirement.cpp`](../../tests/test_raii_retirement.cpp)  
> 13. [`tests/test_playback_session.cpp`](../../tests/test_playback_session.cpp)  
> 14. [`include/rtx/render_graph.h`](../../include/rtx/render_graph.h)  
> 15. [`tests/test_audio_mixer_avx2.cpp`](../../tests/test_audio_mixer_avx2.cpp)  
> 16. [`include/rtx/audio_mixer_avx2.h`](../../include/rtx/audio_mixer_avx2.h)  
> 17. [`src/core/wsola_stretch.cpp`](../../src/core/wsola_stretch.cpp)  
> 18. [`src/core/audio_mixer_avx2.cpp`](../../src/core/audio_mixer_avx2.cpp)  
> 19. [`09_CineRTX_Phase11_施工单_Track_11D_DirectComposition零回读视口与统一NLE引擎及AVX2音频矩阵.md`](09_CineRTX_Phase11_施工单_Track_11D_DirectComposition零回读视口与统一NLE引擎及AVX2音频矩阵.md)  
> 20. [`34_CineRTX Phase 11.2-R 项目卫生达标与整改收口专家终审验收裁决书.md`](34_CineRTX%20Phase%2011.2-R%20项目卫生达标与整改收口专家终审验收裁决书.md)  
> 21. [`CMakeLists.txt`](../../CMakeLists.txt)  
> 22. By Blog Staff \| Jun 15, 2026 01:08 AM \| Tags: None \- Standard C++, [https\://isocpp.org/blog/category/standardization](https://isocpp.org/blog/category/standardization)  
> 23. [`24_CineRTX 第23号文档深度方案.md`](24_CineRTX%20第23号文档深度方案.md)  
> 24. std::find no longer compiles with structs with defaulted equality, [https\://github.com/microsoft/STL/issues/6294](https://github.com/microsoft/STL/issues/6294)  
> 25. VS 2022 Changelog · microsoft/STL Wiki \- GitHub, [https\://github.com/microsoft/STL/wiki/VS-2022-Changelog](https://github.com/microsoft/STL/wiki/VS-2022-Changelog)  
> 26. P0429R9 \`\` · Issue \#2910 · microsoft/STL \- GitHub, [https\://github.com/microsoft/STL/issues/2910](https://github.com/microsoft/STL/issues/2910)  
> 27. CMake and C++26 \| Dev Blog, [https\://mvano.com/courses/cpp/16-modules-cpp26/cmake-cpp26](https://mvano.com/courses/cpp/16-modules-cpp26/cmake-cpp26)  
> 28. Adapters and new interfaces \| Dev Blog, [https\://mvano.com/courses/cpp/13-containers/adapters](https://mvano.com/courses/cpp/13-containers/adapters)  
> 29. libstdc++: funcref\_impl.h Source File \- GNU, [https\://gcc.gnu.org/onlinedocs/gcc-16.1.0/libstdc%2B%2B/api/a00653\_source.html](https://gcc.gnu.org/onlinedocs/gcc-16.1.0/libstdc%2B%2B/api/a00653_source.html)  
> 30. [`include/rtx/raii_owners.h`](../../include/rtx/raii_owners.h)  
> 31. Using std::mdspan results in multiple warnings (C5246) · Issue \#4477, [https\://github.com/microsoft/STL/issues/4477](https://github.com/microsoft/STL/issues/4477)  
> 32. [`cmake/AuditSloc.cmake`](../../cmake/AuditSloc.cmake)  
> 33. [`schema/reflection_codegen.cpp`](../../schema/reflection_codegen.cpp)  
> 34. C++26 \- Wikipedia, [https\://en.wikipedia.org/wiki/C%2B%2B26](https://en.wikipedia.org/wiki/C%2B%2B26)  
> 35. C++26 is done (again)\! — Trip report: November 2026 ISO C++, [https\://www\.reddit.com/r/cpp/comments/1y1y10z/c26\_is\_done\_again\_trip\_report\_november\_2026\_iso\_c/](https://www.reddit.com/r/cpp/comments/1y1y10z/c26_is_done_again_trip_report_november_2026_iso_c/)  
> 36. C++26 DIS failed, how does C++ survive? \- Andreas Fertig's Blog, [https\://andreasfertig.com/blog/2026/11/cpp26-dis-failed-how-does-cpp-survive/](https://andreasfertig.com/blog/2026/11/cpp26-dis-failed-how-does-cpp-survive/)  
> 37. The result of the C++26 DIS ballot \| Hacker News, [https\://news.ycombinator.com/item?id=50239748](https://news.ycombinator.com/item?id=50239748)  
> 38. Pretty nasty MSVC 19.51 hash of float bug : r/cpp \- Reddit, [https\://www\.reddit.com/r/cpp/comments/1xmr0xz/pretty\_nasty\_msvc\_1951\_hash\_of\_float\_bug/](https://www.reddit.com/r/cpp/comments/1xmr0xz/pretty_nasty_msvc_1951_hash_of_float_bug/)  
> 39. [`src/core/playback_session_audio.cpp`](../../src/core/playback_session_audio.cpp)  
> 40. [`src/shim/ngx_bridge/d3d12_interop.cpp`](../../src/shim/ngx_bridge/d3d12_interop.cpp)  
> 41. [`schema/generated/param_schema.generated.hpp`](../../schema/generated/param_schema.generated.hpp)  
> 42. [`src/core/playback_session.cpp`](../../src/core/playback_session.cpp)  
> 43. [`21_CineRTX Phase11 施工方案深度审查.md`](21_CineRTX%20Phase11%20施工方案深度审查.md)  
> 44. [`include/rtx/time_model.h`](../../include/rtx/time_model.h)  
> 45. [`src/gui/d3d12_raii.h`](../../src/gui/d3d12_raii.h)  
> 46. P0288R9 \`move\_only\_function\` · Issue \#2241 · microsoft/STL \- GitHub, [https\://github.com/microsoft/STL/issues/2241](https://github.com/microsoft/STL/issues/2241)  
> 47. [`docs/spec/testing.md`](../spec/testing.md)  
> 48. [`src/core/param_schema.cpp`](../../src/core/param_schema.cpp)  
> 49. [`tests/test_param_schema_reflection.cpp`](../../tests/test_param_schema_reflection.cpp)  
> 50. [`00_CineRTX 演进方向调研.md`](00_CineRTX%20演进方向调研.md)  
> 51. [`16_CineRTX Phase 11 演进计划.md`](16_CineRTX%20Phase%2011%20演进计划.md)  
> 52. [`04_CineRTX Phase 11 冲突评判与方案.md`](04_CineRTX%20Phase%2011%20冲突评判与方案.md)  
> 53. Chapter 12: C++26 (Outlook) \- Modern C++ Tutorial \- Changkun Ou, [https\://changkun.de/modern-cpp/en-us/12-cpp26/](https://changkun.de/modern-cpp/en-us/12-cpp26/)