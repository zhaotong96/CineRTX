# **CineRTX Phase 11 施工验收裁决报告与下一步项目演进计划**

**终审裁决结论：不予通过（REJECTED）。** 经穿透审计 docs/phase11/ 下 05–10 号施工文档、全仓核心与测试源码、CMakeLists.txt 及《Phase 11 验收实证记录》（11\_验收实证记录.md），执行工程师声称“返工任务已全部完成”的说法与仓库现状严重不符。**您对“项目代码膨胀 10k+ 行”的强烈质疑完全成立，且精准命中了本次施工的核心病灶——“单测孤岛式堆砌”与“高难度轨道未闭环”。** 对照 05 号总纲确立的 8 道全局总验收门禁（Gate 11A-1 至 Gate 11E-1），当前尚无任何一条垂直切片在真实产品二进制与桌面环境中形成完整交付闭环，**严禁合并发布 v0.3.0**1。  
支持这一裁决最直接的二进制铁证，来自《验收实证记录》中 AuditImports.cmake 留下的可执行文件体积账本：CineRTX.exe 在 Track 11C 收尾时为 4,425,728 字节，而在叠加了 Track 11D 的 NLE 工程模型、RenderGraph、AVX2 音频矩阵、WSOLA 变速以及 Track 11E 的 vram\_scheduler.cpp 等数千行代码之后，CineRTX.exe 体积仅增至 4,429,312 字节，净增量仅 3,584 字节（恰好合 7 个 512 字节 PE 文件对齐扇区）2。仅 src/gui/shaders/preview\_tone\_map\_cs.h 中内联的 DXBC 着色器字节数组本身就占 3,476 字节，扣除后主程序增量仅余 108 字节2。由于 CMakeLists.txt 对 CineRTX 启用了 /OPT:REF 与 /OPT:ICF 链接期死代码消除，而 Win32 宿主 rtx\_gui.cpp、workstation\_canvas.cpp 与转码管线 hw\_transcode\_stages.cpp **完全未引用**新落盘的核心模块，导致绝大多数新增代码在链接 CineRTX.exe 时被全部剥离，仅在 rtx\_tests.exe 的单元测试中孤立运行4。

## **一、 代码膨胀 10k+ 行的法医级成因剖析**

结合所检索源码的文件行号锚点与静态记录（注：当前归档记录未包含逐文件的 git diff \--stat 全表，以下体积与行数拆解严格基于已调阅文件的精确行号及实测统计），本次 10k+ 行代码膨胀主要由四类结构性问题叠加导致。  
第一，**二进制测试夹具与构建产物回潮源码树**。tests/test\_playback\_session.cpp 全长 1,084 行，其中第 83–946 行共约 864 行（占整文件约 80%）为直接硬编码在 C++ 源码中的 H.264、HEVC 与 AV1 原始码流十六进制字节数组，真正的测试逻辑与断言仅占第 950–1084 行的 135 行（约 12%）8。此外，src/gui/shaders/preview\_tone\_map\_cs.h 全长 703 行（前 124 行为 fxc 反汇编注释，第 125–703 行共 579 行为 3,476 字节的 DXBC 数组）被直接提交至源码目录，违反了 Phase 6 确立并载入《Phase 1–10 权威演进摘要》的“禁止编译期 PTX/CSO 产物入库，须构建期生成至 \${CMAKE\_BINARY\_DIR}/generated”反模式红线3。仅上述两份非业务逻辑载荷即占 1,787 行。  
第二，**“单测孤岛”堆积与产品主干零接线**。执行工程师在 rtx\_core 中新增了 playback\_session.cpp（实证锚点超 1,100 行）、wasapi\_audio\_output.cpp（超 431 行）、gop\_seek\_scheduler.cpp、nle\_project.cpp（超 1,025 行）、render\_graph.cpp（超 119 行）、audio\_mixer\_avx2.cpp（超 356 行）、wsola\_stretch.cpp（超 92 行）与 vram\_scheduler.cpp2。但核查 src/gui/rtx\_gui.cpp、src/gui/workstation\_canvas.cpp 与 src/core/hw\_transcode\_stages.cpp 可确认，三者均未包含上述任何新模块的头文件5。Win32 播放器仍在用 66 ms 定时器驱动离线单帧抓帧，Win32 NLE 面板仍在操作全局 sharedNleTimeline()，离线转码阶段也未接入显存调度器5。  
第三，**基础设施重复造轮子**。src/core/nle\_project.cpp 内部手写了一整套零依赖 JSON 对象扫描器、数组扫描器及 UTF-8 代理对转义解码器12，与 src/core/param\_schema.cpp 中已有的零依赖 JSON 扫描器实现高度重合13；schema/reflection\_codegen.cpp 内部手写了独立 SHA-256 哈希函数14；src/core/wasapi\_audio\_output.cpp 则因 core 层禁止反向包含 src/gui/d3d12\_raii.h 而局部复制了一套同构的 ComOwner\<T\> 模板11。  
第四，**单元测试及桩代码（Mocks/Fakes）大幅扩编**。全量测试由 v0.2.0 基线的 78 项扩充至 143 项（在 CMakeLists.txt 注册了 7 个新测试源文件），包含 test\_raii\_retirement.cpp（超 600 行）、test\_nle\_vertical\_slice.cpp（超 483 行）、test\_audio\_mixer\_avx2.cpp（超 467 行）、test\_flicks\_semantics.cpp（超 345 行）、test\_vram\_scheduler.cpp 与 test\_param\_schema\_reflection.cpp，内含大量独立搭建的无头 D3D12 台架与伪造接口桩（如 FakeComObject、FakeAudioClock、FakeDemuxer、GatedFenceSink）1。这些测试验证了局部算法类的正确性，却掩盖了“真实功能未接入产品、真实硬件桌面未运行”的集成空心化。

## **二、 05–10 号施工单与 Gate 11A–11E 逐轨深度验收**

| 施工分轨与文档 | 施工方自报 | 代码与实证核验状态 | 已落地核心资产 | 阻断性缺口与高危缺陷 | 门禁裁决 |
| :---- | :---- | :---- | :---- | :---- | :---- |
| **Track 11A (06号)** 元数据收口与五域 RAII | 已完成 | **部分完成（缺关键压测与实证）** | CMakeLists.txt 升至 0.3.0；HISTORY\_SUMMARY\_P1\_P10.md、raii\_owners.h、d3d12\_raii.h；NvidiaTranscodeSession 成员拓扑重排4 | 11\_验收实证记录.md 无 11A 专项记录2；缺 NvidiaTranscodeSession 与 RemasterPipeline 的百次创建销毁及失败注入测试16；GUI 仍残留裸 std::thread 与手工 new/delete5 | ❌ **FAIL** |
| **Track 11B (07号)** P2996 静态反射生成轨 | 已完成 | **部分完成（缺闭环审计与全消重）** | schema\_model.hpp、GCC 16 reflection\_codegen.cpp 产出三文件；parseProfileFromJson、profileToJson 与 CLI 映射已迁移13 | 11\_验收实证记录.md 零记录2；validateAndNormalizeProfile 仍硬编码范围且 exportSchemaJson 重复拼接 JSON13；缺 GCC 16 负向编译、CI Diff 与 Zig 0.13 实证2 | ⚠️ **UNVERIFIED** |
| **Track 11C (08号)** Flicks 与实时播放器 | 已完成 | **内核扎实但未接入产品** | time\_model.h 正交拆分、telemetry\_bus.h、MediaPlaybackSession 与 WasapiAudioOutput 单测通过三制式，修复 9 项底层 Bug2 | rtx\_gui.cpp 完全未接入 MediaPlaybackSession，仍用 66 ms 定时器假播7；Seek 靠重建 NVDEC 会话实现10；热拔插未实测2；含 864 行内联夹具8 | ❌ **FAIL** |
| **Track 11D (09号)** DComp / NLE / AVX2 | 已完成 | **隔离完成，存在异步悬垂风险** | NleProject/6 命令、CompiledRenderGraph、mixAvx2/WSOLA、PreviewViewport 与 PE 解析修复2 | Session 0 下 presented=0 skipped=100，DComp 从未在桌面实呈2；presentShared 存在共享纹理提前释放悬垂风险7；NLE 与音频矩阵全为孤岛5 | ❌ **FAIL** |
| **Track 11E (10号)** RIFE 0.5 / VRAM / 13.1 | 已完成 | **实质性未完成（仅交纯逻辑空壳）** | 仅提交了未接线的 VramAdaptiveScheduler 纯逻辑类与单测、rife\_pack\_runtime.cpp 预留了 edge\_reflect101 参数15 | RIFE scale=0.5 ABI、A=128 对齐及 test\_rife\_scale05.cpp 全部缺失4；VRAM 调度未接真实探针6；SDK 13.1 探测与矩阵零交付2；RIFE 单测 5 跑 1 挂未修2 | ❌ **REJECTED** |

### **2.1 Track 11A（06 号施工单）：所有权原语落地，核心会话压测与 GUI 纯化未达标**

Track 11A 在底层设施上完成度较高，已将根目录 CMakeLists.txt 升级至 VERSION 0.3.0，撰写了结构严谨的 docs/archive/HISTORY\_SUMMARY\_P1\_P10.md，并在 include/rtx/raii\_owners.h 与 src/gui/d3d12\_raii.h 建立了五域 move-only 所有权与 GpuRetirementQueue 延迟退役机制4。src/core/hw\_transcode\_internal.h 与 hw\_transcode.cpp 已将 NvidiaTranscodeSession 析构改写为 \= default 并按构造依赖重排成员序，hw\_transcode\_stages.cpp 也已使用 CudaDeviceBuffer、CudaPinnedHostBuffer（彻底废除 std::free 危险路径）、RifeSession 与 PipelineWorker6。  
但对照 Gate 11A-1 与 Gate 11A-2 仍存在四处明确不达标项。首先，《验收实证记录》中缺失 Track 11A 的独立实证章节与 i18n.h 静态扫描记录2。其次，Gate 11A-2 明确要求对 **NvidiaTranscodeSession、RemasterPipeline 与 PreviewViewport** 连续执行 100 次创建/销毁循环及每步人工失败注入验证零泄漏1，而 tests/test\_raii\_retirement.cpp 仅测试了独立原语、MappedResource 和 PreviewViewport（且在 Session 0 中因无法建立合成交换链而 presented=0 skipped=100，导致 cmd\_fence\_ 从未真正推进），**完全没有对 NvidiaTranscodeSession 和 RemasterPipeline 进行百次循环及失败注入压测**2。第三，src/gui/rtx\_gui.cpp 仍在 WorkstationApp 中裸持 std::thread workerThread、scrubThread 与 HBRUSH/HFONT，并在 WM\_DESTROY 中手动清理，src/gui/workstation\_canvas.cpp 仍在 WM\_CREATE/WM\_DESTROY 中裸写 new NleWindowState{} 与 delete state5。第四，README.md 仍写着 Run test suite (92 tests)，与最新的 143 项测试基线脱节2。

### **2.2 Track 11B（07 号施工单）：反射代码生成器成立，消重与门禁审计均未闭环**

Track 11B 成功建立了零外部依赖的 schema/schema\_model.hpp 与 GCC 16 reflection\_codegen.cpp（并务实绕开了 GCC 16.2.0 std::meta::extract 无法直接物化 const char\* 字面量的编译器限制），输出了 param\_schema.generated.hpp、param\_schema.generated.inc 与 schema.json14。src/core/param\_schema.cpp 中的 parseProfileFromJson() 与 profileToJson() 已改由 forEachProfileField 驱动，src/cli/main.cpp 的常规单值参数解析也已接入 findCliFieldBinding13。  
然而，该轨并未通过 Gate 11B-1 验收。一方面，代码消重留有尾巴：src/core/param\_schema.cpp 中的 validateAndNormalizeProfile() 仍把参数上下限（如 0..200000、0..51、1..4、400..4000）硬编码写在分支语句中，未直接消费生成表 kParamSchema 内的 min\_val/max\_val，而 exportSchemaJson() 仍在用 35 行 std::format 循环重新格式化 kParamSchema，与 reflection\_codegen.cpp 生成 schema/generated/schema.json 构成两套序列化逻辑13。另一方面，**《验收实证记录》中完全缺失 Track 11B 的验收记录**2——既无 GCC 16 运行生成的命令行日志，也无制造重复 json\_key/cli\_flag 触发编译期报错的负向测试证据，更无新增单字段联动检查、CI Diff 门禁校验与 Zig 0.13 预设交叉编译全绿实证2。

### **2.3 Track 11C（08 号施工单）：内核质量优秀，但产品播放体验仍停留在 66 ms 假循环**

客观而言，Track 11C 在核心运行时层面的质量最高：include/rtx/time\_model.h 严格剥离了无状态 absoluteTimestampToFlicks() 与有状态 IncrementalFlicksAccumulator，test\_flicks\_semantics.cpp 完整覆盖了 1/15360 非整除时基、B 帧乱序、负 PTS、VFR 与全制式 SMPTE Drop-Frame 分钟边界2；playback\_session.cpp 与 wasapi\_audio\_output.cpp 实现了事件驱动 WASAPI 主时钟、Fence 门控槽位归还与 AtomicTelemetryBus 无锁遥测，并在实机联调中根治了 9 个隐蔽产品缺陷（包括 NVDEC worker 线程 CUDA 上下文亲和性、av\_bsf\_flush 修复 seek 后 EOF 拒包、EOS 排空截断、暂停冻结 IAudioClock、纠正 IAudioRenderClient 与 IAudioClock::GetPosition 参数类型等）2。  
但对照 08 号施工单交付“Win32 独立实时播放垂直切片”的目标，Track 11C 仍存在三项致命断层。首先，**src/gui/rtx\_gui.cpp 完全没有接入 MediaPlaybackSession 与 AtomicTelemetryBus**7。无论是 \[2\] 播放器 还是 \+1 无边框全屏快播，按下播放键后运行的仍然是旧版 WM\_TIMER(3, 66ms) 循环（每次推进 pos \+ 66 并通过 runScrubPreviewAsync() 拉起一次离线 NvidiaTranscodeSession 抓单帧），既无音频回放与 IAudioClock 同步，且 IDC\_BTN\_STEP\_BACK/FWD 步进仍硬编码 1000 / 24 ms7。其次，playback\_session.cpp 中的 performSeek() 因底层 NvidiaShim::decoderFlush 为终态 EOS，不得不采取**每次 Seek 重建整个 DecoderOwner 实例**的权宜之计，连续拖拽或逐帧后退时将引发高频硬件解码器重建10。第三，AUDCLNT\_E\_DEVICE\_INVALIDATED 热拔插自愈仅通过静态代码审查认定，未进行真实或模拟设备拔插测试，且 TelemetrySnapshot::vram\_used\_bytes 在 publishTelemetry() 中仍硬编码填 02。

### **2.4 Track 11D（09 号施工单）：三切片全量隔离，存在 D3D12 异步释放悬垂风险**

垂直切片 A（DirectComposition 零回读视口）完成了静态枚举清理、动态 dcomp.dll 加载与 AuditImports.cmake PE 解析器修复，并在无头台架上验证了计算着色器像素数学精度2。但该切片暴露出两处严重问题：第一，在 Session 0 无头环境中，DCompositionCreateDevice 返回 E\_ACCESSDENIED、CreateSwapChainForComposition 返回 DXGI\_ERROR\_NOT\_CURRENTLY\_AVAILABLE，100 次视口循环全部跳过呈现（presented=0 skipped=100），这意味着 Gate 11D-1 要求的真实 DWM 合成、HDR/SDR 跨屏切换与 100 次 resizeSwapChain 无撕裂/黑闪/泄漏**从未获交互式桌面实证**2；第二，**存在真实的 GPU 异步资源悬垂风险**——rtx\_gui.cpp 的 publishPreviewHandoff() 将借用的 previewResource 装入 ComOwner\<IUnknown\> 传给 PreviewViewport::presentShared()7，而 presentShared() 提交 Compute Shader 命令并信号 fence\_value\_ 后即刻返回，**既未对 shared\_resource 增加引用计数，也未将其置入 retirement\_ 延迟退役队列受该 fence\_value\_ 保护**31。一旦转码桥接在 WM\_APP\_PREVIEW 返回后销毁，GPU 队列仍在执行读取已释放共享资源的着色器命令，直接违背 Track 11A 的 GPU 异步退役铁律1。此外，《验收实证记录》还自承当 enable\_truehdr \= 0 时，SDR 右半预览因线性/PQ 语义不匹配而异常变暗2；且工程记录显示，为消除“零主机拷贝”静态 grep 误报，执行工程师把 preview\_viewport.cpp 第 12 行注释中的英文 API 词改为中文以凑齐 0 匹配2。  
垂直切片 B（NleProject 与 RenderGraph）虽然完整实现了确定性 JSON 序列化、6 个可逆编辑命令与时间线帧映射逻辑12，但既未替换 workstation\_canvas.cpp 中的旧版全局 NleTimeline（其中 onNleRazor 仍残留 msToFlicks(100)）5，也未接入 MediaPlaybackSession 或 RemasterPipeline 跑通任何一帧真实视频的剪辑预览与硬切导出2。test\_nle\_vertical\_slice.cpp 仅在内存中把 frameAt() 结构体字段与手写循环做纯数值比对，完全未兑现 Gate 11D-2 的“多段硬切导出逐帧一致”切片目标30。  
垂直切片 C（AVX2 音频矩阵与 WSOLA）在 test\_audio\_mixer\_avx2.cpp 中实测 0.5x 变速时长翻倍且音高漂移仅 0.0432%（远优于 \< 2% 门禁），mixAvx2 与标量误差 \< 1e-52。然而如《验收实证记录》所坦言，WSOLA 仍停留在独立离线接口阶段，既未接入 MediaPlaybackSession（后者仍在使用变调线性插值 VarispeedCursor），也未接入 NLE 导出2；同时 mixAvx2() 函数体内每次调用都会构造 std::vector\<VoicePrep\> preps 触发堆分配，若直接用于实时音频线程将触发分配禁令29。

### **2.5 Track 11E（10 号施工单）：近乎全面未实施，已知 RIFE 随机性缺陷被直接搁置**

对照 10 号施工单的三大任务与 Gate 11E-1，Track 11E 是本次汇报中最严重的未交付区域。第一，**Task 11E-1（RIFE scale=0.5 ABI 与运行时）几乎未动**：仅 rife\_pack\_runtime.cpp 中存在 edge\_reflect101 参数传递35，而 bridge\_exports.cpp 中根本没有 rtx\_ngx\_bridge\_init\_rife\_ex37，rife\_engine.cpp 与 rife\_builder.cpp 仍写死 kAlign \= 64（未实现 A=128 对齐）36，hw\_transcode\_stages.cpp 仍在 initRemasterAndFgStage() 中向 initRife 固定传入源视频宽高 session\_-\>info().width/height6，CMakeLists.txt 中甚至未注册 tests/test\_rife\_scale05.cpp4。第二，**Task 11E-2（滞后 VRAM 调度器）仅交付纯逻辑空壳**：VramAdaptiveScheduler 虽在单测中验证了 75%–85% 震荡零抖动15，但全仓无任何代码接入 DXGI 本地显存预算、cuMemGetInfo 空闲显存或 TensorRT 峰值分配，也未接入 RemasterPipeline 与 8 GB GPU 4K 压制防 OOM 验证2。第三，**Task 11E-3（SDK 13.1 接入与实证矩阵）零交付**：《验收实证记录》在 Track 11D 门禁后即结束，完全未见 nv\_encoder.cpp 的 queryAv1AdvancedCaps() 实证记录、R580 回退验证或 Default vs 7B vs 15B/31B \+ UHQ 的完整实测矩阵2。  
尤为严重的是，Track 11D 全量回归第 2 轮曾发生 rife\_four\_phase\_monotonic\_progression 失败；复现脚本跑 5 次出现 4 过 1 挂（退出码 0,0,0,1,0），同一源码编译的 TensorRT 合成引擎体积在 77,420 B 与 153,180 B 之间倍差波动，偶发 max \|diff\| \= 37 \> 12。实证记录已精准诊断根因为 rife\_builder.cpp 遗漏 clearFlag(BuilderFlag::kTF32) 且 FP32 精度锁仅覆盖 6 类算子，并将该缺陷正式移交 Track 11E 根治——但截至本次提交，该缺陷依然处于未修复状态2。

## **三、 跨轨道暴露的基础设施冲突与口径矛盾**

在统筹下一步返工前，必须先裁决两项已在《验收实证记录》中暴露的底层工程矛盾。  
第一，**静态 FFmpeg 的 LGPL 构建与 20 项 SSOT 参数存在功能性死链**。实证记录披露，cmake/RtxFfmpegStatic.cmake 在禁用 \--enable-gpl 的前提下配置了 \--enable-encoder=libx264/libx265/libsvtav1，致使 CpuVideoEncoder::create() 在运行期恒定返回 Unsupported2。这不仅让 hw\_transcode.cpp 的 use\_cpu\_encoder 路径及 SSOT 中的 av1\_cpu、hevc\_cpu 成为运行期不可用的“假选项”，也是执行工程师被迫在 test\_playback\_session.cpp 中塞入 864 行硬编码码流夹具的直接原因2。项目必须在 v0.3.0 封板前拍板：是剥离 GPL 的 libx265 并独立启用许可证兼容的 libsvtav1（BSD-3-Clause-Clear），还是从 schema\_model.hpp 中清理无效的 CPU 软编枚举。  
第二，**测试环境驱动版本号口径矛盾与 Session 0 盲区**。《验收实证记录》环境表中将测试机（RTX 5060 Laptop GPU）的驱动版本记录为 621.01（R580 分支）2。在 NVIDIA 驱动分支体系中，621.01 属于高于 R610 的版本而非 R580，该记录错误会直接干扰 Track 11E 对“R580 低版本驱动下 SDK 13.1 特性自动回退”的判定依据1。此外，由于测试机长期通过 Session 0 无头 SSH 运行，涉及 DirectComposition 交换链创建与全屏展示的核心路径均被跳过，后续所有图形呈现门禁必须回归真实交互式 Windows 桌面会话取证2。

## **四、 下一步返工整改与项目演进计划**

建议立即退回本次合并请求，暂停一切新特性扩张，严格按以下五个波次执行“先瘦身治本、再打通切片、后收口高阶特性”的返工计划：

### **Wave 0：代码瘦身消冗与高危缺陷止血（P0，立即执行）**

> 1. **清除源码树编译产物与内联二进制夹具**：将 src/gui/shaders/preview\_tone\_map\_cs.h（703 行）移出源码树，在 CMakeLists.txt 中改为构建期调用 fxc.exe 生成至 \${CMAKE\_BINARY\_DIR}/generated/；将 tests/test\_playback\_session.cpp 中 864 行内联十六进制数组剥离为外部测试夹具或构建期生成，恢复测试文件纯度。  
> 2. **合并重复基础代码**：抽离 param\_schema.cpp 与 nle\_project.cpp 中重复的零依赖 JSON 扫描器与 UTF-8 转义解码逻辑，统一收口至共享的 include/rtx/json\_scanner.h 与 src/core/json\_scanner.cpp；视需要精简 reflection\_codegen.cpp 内部辅助代码。  
> 3. **封堵异步 GPU 悬垂与 RIFE 随机性失败**：改造 PreviewViewport::presentShared() 与 rtx\_gui.cpp 的递交契约，将消费中的 shared\_resource（ComOwner\<IUnknown\>）登记入 retirement\_.retire(fence\_value\_, ...)，杜绝桥接提前销毁引发的 GPU 读取野指针；修复 enable\_truehdr \= 0 下预览图与 preview\_tone\_map\_cs.hlsl 的 SDR/PQ 色彩空间错配；在 rife\_builder.cpp 补充 config-\>clearFlag(nvinfer1::BuilderFlag::kTF32) 并扩大层精度锁定范围，验证 rife\_four\_phase\_monotonic\_progression 连续 10 次 100% PASS。

### **Wave 1：补齐 Track 11A 与 11B 审计缺口（P0）**

> 1. **闭合 Gate 11A-1 与 11A-2**：在 tests/test\_raii\_retirement.cpp 补充对 NvidiaTranscodeSession 与 RemasterPipeline 的连续 100 次创建/销毁与每步失败注入零泄漏压测；将 rtx\_gui.cpp 中 WorkstationApp 的 workerThread/scrubThread/HBRUSH/HFONT 及 workstation\_canvas.cpp 中的 NleWindowState 纳入 PipelineWorker 与轻量 Win32 RAII 拥有者；同步校准 README.md 测试计数与 i18n.h 静态扫描证据。  
> 2. **闭合 Gate 11B-1 全自动链**：改造 param\_schema.cpp 中的 validateAndNormalizeProfile() 与 exportSchemaJson()，使参数数值边界校验直接读取生成表 kParamSchema 并消除手写 JSON 模板重复；在本地及 CI 补齐并记录 GCC 16 代码生成、重复 json\_key/cli\_flag 编译期报错负向测试、CI Diff 一致性检查与 Zig 0.13 预设构建全绿实证。

### **Wave 2：打破孤岛，接通 Track 11C 与 11D 真实垂直切片（P1）**

> 1. **接通 Win32 实时播放器与热拔插实测**：在 rtx\_gui.cpp 中彻底拔除 WM\_TIMER(3, 66ms) 驱动 runScrubPreview() 的假播逻辑，正式接入 MediaPlaybackSession \+ WasapiAudioOutput \+ AtomicTelemetryBus（30 Hz 拉取）并替换 1000/24 硬编码步进；修复 NvidiaShim 解码器非终态 Flush 能力，消除 performSeek() 每次重建 NVDEC 实例的开销；执行 AUDCLNT\_E\_DEVICE\_INVALIDATED 设备热拔插模拟或物理验证。  
> 2. **接通统一 NLE 模型、RenderGraph 与实时音频总线**：用 rtx::nle::NleProject \+ EditHistory 替换 workstation\_canvas.cpp 中的旧版 NleTimeline；打通 CompiledRenderGraph 驱动 MediaPlaybackSession（时间线实时预览）与 RemasterPipeline（多段硬切实际容器导出，片段切换点自动下发 reset\_temporal 与强制 IDR），用端到端输出文件帧级校验代替纯内存整数比对；消除 mixAvx2() 每次调用的 std::vector 堆分配，并将 wsolaStretch 与 mixAvx2 接入 MediaPlaybackSession 与 NLE 导出总线，替换 VarispeedCursor。  
> 3. **完成交互式桌面 DirectComposition 验收**：在真实交互式 Windows 桌面会话（非 Session 0）实测 CineRTX.exe，确认 PreviewViewport 实际合成呈现成功（presented \> 0）、队列任务缩略预览实时联动、HDR/SDR 跨屏切换与连续 100 次 resizeSwapChain 无撕裂/闪烁/显存泄漏，且监看路径 D2H \== 0 B。

### **Wave 3：补齐 Track 11E 实质交付并完成 v0.3.0 封板（P2/P3）**

> 1. **明确产品前置决策**：第一，正式拍板 v0.3.0 驱动兼容基线（维持 R580 / n13.0.19.1 或升级至 R610 / 13.1.15）并核实测试机真实驱动号；第二，裁决 CpuVideoEncoder 去留（独立启用兼容许可证的 libsvtav1 或从 SSOT 清理死选项）。  
> 2. **交付 RIFE scale=0.5 与显存闭环调度**：在 ngx\_bridge\_abi.h 与 bridge\_exports.cpp 新增 RtxRifeConfig 与 rtx\_ngx\_bridge\_init\_rife\_ex；在 rife\_engine.cpp、rife\_builder.cpp 与 rife\_pack.cu 落地 A=128 对齐与 reflect101 边缘填充；修正 hw\_transcode\_stages.cpp 按实际拓扑传递目标宽高并补全 tests/test\_rife\_scale05.cpp；在 RemasterPipeline 中接入真实 DXGI 预算、cuMemGetInfo 与 TRT 峰值分配驱动 VramAdaptiveScheduler，完成 8 GB GPU 4K 连续压测无 OOM。  
> 3. **输出完整口径的 SDK 13.1 实测矩阵**：在 nv\_encoder.cpp 实装 queryAv1AdvancedCaps() 与低版本驱动优雅回退，完整记录 GPU 型号、驱动版本、SDK 版本、素材规格、编码参数、预热时长、统计窗口与重复次数，输出 Default vs HighQuality-7B vs Master-UHQ-15B/31B 的 FPS、编码器占用、VRAM 峰值与客观画质矩阵。所有 Gate 11A–11E 实证齐全后，方可执行 **v0.3.0** 封板发布。

### **Wave 4：v0.4.0 后续演进方向**

在 v0.3.0 的五域 RAII 底盘、独立实时播放内核与 1V+1A 最小 NLE 垂直切片稳定运行后，v0.4.0 将重点推进三项演进：在 CompiledRenderGraph 之上叠加多轨视频叠化混合（Cross-Dissolve 等 Compute Shader 转场）；为 MediaPlaybackSession 探索轻量级运行期动态 VSR 实时挂载调度；以及打通 WebUI v3 与核心 NleProject / MediaPlaybackSession 引擎之间的双向工程与播放状态同步。

#### **引用的著作**

> 1. 05\_CineRTX\_Phase11\_SuperPower施工总纲\_五轨并行契约与物理文件隔离矩阵.md [受控工程档案]  
> 2. 11\_验收实证记录.md [受控工程档案]  
> 3. preview\_tone\_map\_cs.h [受控工程档案]  
> 4. CMakeLists.txt [受控工程档案]  
> 5. workstation\_canvas.cpp [受控工程档案]  
> 6. hw\_transcode\_stages.cpp [受控工程档案]  
> 7. rtx\_gui.cpp [受控工程档案]  
> 8. test\_playback\_session.cpp [受控工程档案]  
> 9. HISTORY\_SUMMARY\_P1\_P10.md [受控工程档案]  
> 10. playback\_session.cpp [受控工程档案]  
> 11. wasapi\_audio\_output.cpp [受控工程档案]  
> 12. nle\_project.cpp [受控工程档案]  
> 13. param\_schema.cpp [受控工程档案]  
> 14. reflection\_codegen.cpp [受控工程档案]  
> 15. test\_vram\_scheduler.cpp [受控工程档案]  
> 16. test\_raii\_retirement.cpp [受控工程档案]  
> 17. test\_flicks\_semantics.cpp [受控工程档案]  
> 18. test\_param\_schema\_reflection.cpp [受控工程档案]  
> 19. d3d12\_raii.h [受控工程档案]  
> 20. hw\_transcode\_internal.h [受控工程档案]  
> 21. raii\_owners.h [受控工程档案]  
> 22. 06\_CineRTX\_Phase11\_施工单\_Track\_11A\_基线元数据收口与五域异构RAII退役体系.md [受控工程档案]  
> 23. schema\_model.hpp [受控工程档案]  
> 24. param\_schema.generated.hpp [受控工程档案]  
> 25. main.cpp [受控工程档案]  
> 26. 07\_CineRTX\_Phase11\_施工单\_Track\_11B\_C++26\_P2996静态反射独立代码生成轨.md [受控工程档案]  
> 27. playback\_session.h [受控工程档案]  
> 28. time\_model.h [受控工程档案]  
> 29. audio\_mixer\_avx2.cpp [受控工程档案]  
> 30. test\_nle\_vertical\_slice.cpp [受控工程档案]  
> 31. preview\_viewport.cpp [受控工程档案]  
> 32. render\_graph.cpp [受控工程档案]  
> 33. vram\_scheduler.cpp [受控工程档案]  
> 34. vram\_scheduler.h [受控工程档案]  
> 35. rife\_pack\_runtime.cpp [受控工程档案]  
> 36. rife\_engine.cpp [受控工程档案]  
> 37. bridge\_exports.cpp [受控工程档案]  
> 38. hw\_transcode.cpp [受控工程档案]  
> 39. README.md [受控工程档案]  
> 40. 09\_CineRTX\_Phase11\_施工单\_Track\_11D\_DirectComposition零回读视口与统一NLE引擎及AVX2音频矩阵.md [受控工程档案]  
> 41. 08\_CineRTX\_Phase11\_施工单\_Track\_11C\_Flicks语义重构与WASAPI实时播放内核.md [受控工程档案]  
> 42. rife\_builder.cpp [受控工程档案]  
> 43. 10\_CineRTX\_Phase11\_施工单\_Track\_11E\_VSR\_FRUC显存感知自适应调度与SDK13.1分级接入.md [受控工程档案]