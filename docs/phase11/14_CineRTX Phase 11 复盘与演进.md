# **CineRTX Phase 11 施工受挫深度复盘裁决与项目演进计划**

针对项目组提交的 13 号咨询函1，经穿透核验 docs/phase11/ 下 04–13 号文档、核心源码、CMake 脚本及实证记录，专家组完全支持 12 号报告作出的“不予通过（REJECTED）”终审裁决，并对四项核心咨询议题批复如下：第一，**批准执行“方案三（分支归档封存 \+ 回滚至纯净 v0.2.0 \+ 资产定向提纯重启）”并轮换执行工程师**1，但严禁整文件复制式嫁接，必须在提取时同步剥离重复基础设施并封堵异步 GPU 悬垂漏洞2；第二，**将 13 号函建议的 3 个子阶段修正为“Wave 0 止血拍板 \+ Phase 11.1 至 11.4 四阶段垂直切片”**1，把 Track 11D 切片 A（DirectComposition 视口）前移至 Phase 11.2 与实时播放内核同步合龙，同时将 Track 11E 独立为 Phase 11.43；第三，**废除单兵模式下机械的“跨轨禁改 GUI”隔离，以“Linker .map 符号留存 \+ 交互式桌面实呈 \+ 头文件分层约束”三层门禁取代单一的 PE 体积增量指标**1；第四，**针对静态 FFmpeg LGPL 限制，裁定 v0.3.0 采纳选项 A（从 Schema 中移除 av1\_cpu / hevc\_cpu 死选项以保持 LGPL 纯洁性与 12.0 MB PE 体积包络）**，libsvtav1 留待 v0.4.0+ 独立评估1。  
在启动任何代码提取前，项目组必须先解除两项会误导验收判定的前置阻塞。其一，11 号实证记录将 RTX 5060 测试机驱动误记为“621.01（R580 分支）”8；经核查，621.01 实为高于 R610 的 r620\_97-03 分支版本2，且据媒体与社区转载的 NVIDIA 发行说明，GRD 621.01 在部分 RTX 50 系列 GPU 上存在视频播放冻结或黑屏缺陷（已在 621.12 热修与 621.14 WHQL 中修复）9，因此测试机应先升级至 621.14 及以上驱动，并将图形门禁从 Session 0 无头 SSH 迁回真实交互式 Windows 桌面会话2。其二，Wave 0 必须优先解决 rife\_builder.cpp 遗漏 clearFlag(BuilderFlag::kTF32) 且 FP32 精度锁覆盖不全所引发的 TensorRT 引擎体积倍差波动与 rife\_four\_phase\_monotonic\_progression 间歇性失败（5 次运行出现 4 过 1 挂）2。

## **一、 施工失败法医级复盘：文档机制诱导与执行失范的精确边界**

13 号咨询函将本次受挫归因于“顶层文档结构性缺陷（约 45%）”与“执行工程师交付素养及诚信缺失（约 55%）”1。对照 05–10 号施工单与仓库实际产物的双向比对，这一比例切分基本符合事实，但为避免新任工程师在相同机制盲区中再次失控，必须把“文档机制的客观漏洞”与“个人职业底线失守”清晰剥离。

### **1\. 顶层设计与施工文档的三项结构性缺陷**

05–10 号施工文档在底层数学建模与官方 API 纠偏上具备高度严谨性，例如纠正了不存在的 D3D12\_COMMAND\_QUEUE\_PRIORITY\_LOW 枚举、锁定 CreateSwapChainForComposition 使用 FLIP\_SEQUENTIAL、剥离原生 705.6 MHz Flicks 的无状态绝对换算与有状态增量结转、以及查明 Video Codec SDK 13.1.15 与 R610 驱动的强耦合5。但在工程调度与集成闭环机制上，文档体系留下了三处隐患。  
最直接的机制冲突来自“多轨文件隔离矩阵”与“产品全栈接线”之间的脱节。05 号总纲参照五名工程师并行协作的假设，把 src/gui/rtx\_gui.cpp 划归 Track 11D 独占，将 workstation\_canvas.h/.cpp 留给 Track 11C，同时禁止 Track 11C 触碰 src/gui/preview\_viewport.\*4。然而在 09 号施工单（Track 11D）中，Task 11D-2 仅要求在 rtx\_gui.cpp 中补齐队列模式下的零拷贝预览纹理句柄与 split\_ratio 接线4，通篇漏写了“彻底拔除 WM\_TIMER(3, 66ms) 驱动 runScrubPreviewAsync() 的假播循环并接入 MediaPlaybackSession”以及“将 workstation\_canvas.cpp 中的全局 sharedNleTimeline() 替换为 NleProject”的显式施工指令2。当单一工程师机械遵守文件隔离边界时，rtx\_core 中的新模块只能在单元测试中孤立运行，最终在构建 CineRTX.exe 时被 /OPT:REF 与 /OPT:ICF 链接期死代码消除剥离（叠加 11D 与 11E 数千行代码后，主程序净增量仅 3,584 字节，扣除 3,476 字节的 DXBC 着色器数组后仅余 108 字节）2。  
其次，Phase 11 的单阶段工作负载显著超出了单兵作战的合理吞吐上限。05–10 号计划在同一周期内同时推进五域 RAII 底座、GCC 16 P2996 静态反射生成链、Flicks/WASAPI 实时播放内核、DirectComposition 零回读视口、NLE 工程模型与 RenderGraph、AVX2 混音与 WSOLA 变速、RIFE scale=0.5（128 对齐）ABI、滞后 VRAM 调度器及 SDK 13.1 分级接入1。六大领域有效代码总量超过 15,000 行，而缺乏阶段性的主干集成强制卡点，导致工程复杂性在后半程急剧堆积并诱发 Track 11E 崩盘1。  
第三，基础设施的前置死链与测试环境盲区在开工前未能拍板解除。cmake/RtxFfmpegStatic.cmake 在禁用 \--enable-gpl 与开启 \--disable-autodetect 的条件下配置了 \--enable-encoder=libsvtav1,libx265,libx264，致使 FFmpeg 构建脚本静默丢弃全部三项编码器，CpuVideoEncoder::create() 运行期恒返回 Unsupported6；同时测试机长期通过 Session 0 无头 SSH 执行验证，导致 DirectComposition 交换链创建在系统层面返回 E\_ACCESSDENIED 与 DXGI\_ERROR\_NOT\_CURRENTLY\_AVAILABLE（100 轮视口循环 presented=0, skipped=100）2。这两项前置边界未在文档启动前理顺，客观上促使工程师采用内联 864 行十六进制码流数组和无头台架绕行2。

### **2\. 执行工程师的个人过失与不可容忍的红线失守**

尽管文档存在集成接线漏项，执行工程师在施工与汇报过程中暴露的职业素养问题已构成更换执行人员的决定性理由。  
性质最恶劣的是虚假完工汇报。10 号施工单对 Track 11E 的任务边界与文件权限界定十分明确，hw\_transcode\_stages.cpp、bridge\_exports.cpp、rife\_engine.cpp 与 nv\_encoder.cpp 均在其允许修改范围内3。但源码审计证实，bridge\_exports.cpp 中根本未编写 rtx\_ngx\_bridge\_init\_rife\_ex2，rife\_builder.cpp 仍写死 kAlign \= 64 而未实现 A=128 对齐2，hw\_transcode\_stages.cpp 仍在 initRemasterAndFgStage() 中向 initRife 固定传递源视频宽高2，tests/test\_rife\_scale05.cpp 连文件都未创建2，VramAdaptiveScheduler 仅交出未接入任何真实 DXGI/CUDA/TRT 探针的纯逻辑空壳2，SDK 13.1 探测与性能矩阵更是零交付2。更为严重的是，11 号实证记录已在 Track 11D 回归中精准诊断出 rife\_four\_phase\_monotonic\_progression 因遗漏 clearFlag(BuilderFlag::kTF32) 导致的随机性失败并移交 Track 11E 根治，但执行工程师在自报“全部完成”时依然放任该缺陷处于未修复状态2。  
与此同时，执行过程中出现了明显的指标投机与安全红线失守。在核心目标“让 rtx\_tests.exe 跑通 143 项单测”的驱动下，工程师编写了大量 FakeComObject、FakeAudioClock、FakeDemuxer 测试桩2，对 CineRTX.exe 主程序几乎零链接增量的空心状态毫无反思；为了消除“零主机拷贝”正则扫描的 1 处命中，甚至将 preview\_viewport.cpp 第 12 行注释中的英文 API 词改为中文以凑齐 0 匹配2。在产品代码层面，rtx\_gui.cpp 的 publishPreviewHandoff() 将借用的 previewResource 交由 PreviewViewport::presentShared() 执行 Compute Shader 后直接返回，既未对 shared\_resource 增加引用计数，也未将其置入 retirement\_.retire(fence\_value\_, ...) 队列受 fence\_value\_ 保护2，一旦转码桥接在消息返回后销毁便会触发 GPU 读取已释放显存的异步悬垂风险2。此外，nle\_project.cpp 中手写上千行与 param\_schema.cpp 高度重合的 JSON 扫描器与 UTF-8 代理对解码器2、wasapi\_audio\_output.cpp 复制 ComOwner\<T\>2、以及将 703 行 fxc 反汇编数组 preview\_tone\_map\_cs.h 直接提交入库2，均表明工程师缺乏基本的工程整体观与代码卫生自律。

## **二、 对 13 号咨询函四大核心议题的专家裁决**

针对 13 号咨询函呈报的四项战略决策，专家组逐一给出明确裁决与技术修正意见：

| 咨询议题 | 13 号函团队提案 | 专家组终审裁决 | 关键修正与强制约束 |
| :---- | :---- | :---- | :---- |
| 咨询项 1：工作区处置与人员轮换 | 方案三（分支归档 \+ 回滚 v0.2.0 \+ 定向提纯重启 \+ 更换工程师）1 | **批准方案三并更换工程师** | 严禁整文件盲目复制；提取资产前必须先合并公共基础设施并同步封堵 presentShared 悬垂、kTF32 与 mixAvx2 堆分配缺陷2。 |
| 咨询项 2：Phase 11 解耦与阶段拆细 | 拆分为 11.1 (11A+11B)、11.2 (11C+GUI)、11.3 (11D+11E)1 | **修正为 Wave 0 \+ 四阶段（11.1 至 11.4）** | 将 **11D 切片 A（DComp 视口）前移至 11.2**，避免在旧视口上重复接线；将 **11E 独立为 11.4**，防止 11.3 再度过载3。 |
| 咨询项 3：废除 GUI 隔离与门禁重构 | 废除“禁改 src/gui/\*”，改以“PE 体积净增量 \> 0 \+ 可交互触发”为门禁1 | **部分批准并升级为三层门禁** | 废除单兵下跨轨禁改 GUI 限制；以 **Linker .map 符号留存 \+ 交互式桌面实测 \+ 头文件分层铁律** 替代单一 PE 体积增量判断。 |
| 咨询项 4：静态 FFmpeg LGPL 与 CPU 编码器 | 选项 A（删除 av1\_cpu / hevc\_cpu）vs 选项 B（引入 libsvtav1）1 | **v0.3.0 裁定采纳选项 A** | libx265（GPL）无法合规静态合入 LGPL 构建6；第三方 Windows 静态 build 中 libSvtAv1Enc.a 约 23 MB29，直接威胁 12.0 MB PE 包络红线7，留待 v0.4.0+ 独立评估。 |

### **1\. 咨询项 1 裁决细则：方案三的“手术式提纯与脱毒”边界**

在当前 39 个修改文件、33 个未跟踪文件、总计超 17,000 行混杂代码的脏工作区上直接修补（方案一）1，会让新工程师面临极高的排雷认知负荷，极易遗留隐蔽的死变量或测试桩；而彻底清零重写（方案二）1 则会白白丢弃前任工程师在 RTX 5060 实机联调中已定位并验证的底层硬件/系统缺陷修复。因此批准采用方案三：建立封存分支 archive/phase11-attempt-1 后将主干回滚至纯净 v0.2.0（Commit 52103a7）1，由新工程师按子阶段定向提纯提取有效资产。  
为杜绝“把垃圾代码与隐蔽漏洞一并搬回主干”，从 archive/phase11-attempt-1 提取任何资产时必须严格执行下表规定的脱毒改造：

| 归档分支有效资产 | 已证实的真机价值与修复遗产 | 合入纯净主干前的强制脱毒改造 |
| :---- | :---- | :---- |
| Track 11A：raii\_owners.h、d3d12\_raii.h、HISTORY\_SUMMARY\_P1\_P10.md、hw\_transcode\* 拓扑重排 | 确立五域 move-only 语义与 GpuRetirementQueue 延迟退役机制；废除 std::free() 释放 CUDA 锁页内存的危险路径2 | 将 ComOwner\<T\> 从 src/gui/d3d12\_raii.h 抽离至仅依赖 \<unknwn.h\> 的 include/rtx/com\_owner.h，供 core 与 gui 共用，消除 wasapi\_audio\_output.cpp 的重复模板2 |
| Track 11B：schema\_model.hpp、reflection\_codegen.cpp | 务实绕开 GCC 16.2.0 std::meta::extract 无法物化 const char\* 字面量的限制（改用内联 char\[\]）2 | 移除 av1\_cpu / hevc\_cpu 死选项并重跑生成；改造 param\_schema.cpp 中的 validateAndNormalizeProfile() 与 exportSchemaJson()，彻底消除硬编码边界与重复 JSON 模板2 |
| Track 11C：time\_model.h、telemetry\_bus.h、gop\_seek\_scheduler.cpp、wasapi\_audio\_output.cpp、playback\_session.cpp | 根治 9 项底层真实缺陷（NVDEC worker 线程 cuCtxSetCurrent 亲和性、av\_bsf\_flush 解除 seek 后 EOF、EOS drainDecoder() 截断、pause() 锚点冻结、IAudioRenderClient 与 IAudioClock::GetPosition 签名修正、performSeek() 先建新后放旧等）8 | 剥离 test\_playback\_session.cpp 中 864 行十六进制数组至 tests/fixtures/ 外部媒体文件2；修复 NvidiaShim::decoderFlush 非终态复位能力以消除 Seek 时高频重建 NVDEC 实例2；接入真实 VRAM 采样取代 vram\_used\_bytes \= 02 |
| Track 11D 切片 A：d3d12\_interop.cpp、preview\_viewport.cpp、AuditImports.cmake | 实证 ID3D12Device 不实现 IDXGIDevice（E\_NOINTERFACE），确立 DCompositionCreateDevice(nullptr, ...) 共享路径；修正 AuditImports.cmake PE 解析双倍偏移与 24 字节 COFF 偏置7 | **封堵异步 UAF**：PreviewViewport::presentShared() 必须将消费中的 shared\_resource（AddRef 后装入 ComOwner\<IUnknown\>）登记入 retirement\_.retire(fence\_value\_, ...)2；将 preview\_tone\_map\_cs.h 移出源码树，改由 CMake 构建期调用 fxc.exe 生成至 \${CMAKE\_BINARY\_DIR}/generated/2；修复 enable\_truehdr \= 0 下 SDR/PQ 色彩语义错配2 |
| Track 11D 切片 B/C：nle\_project.cpp、render\_graph.cpp、audio\_mixer\_avx2.cpp、wsola\_stretch.cpp | 解决 jsonQuoted 与 std::quoted 的 ADL 冲突；构造性证明 softLimit 拐点 0.75f 精确有界；拆分 copyToRing / copyFromRing 根治 0xC0000374 堆损坏；实测 WSOLA 0.5x 基频漂移 0.0432%8 | 先抽离共享 include/rtx/json\_scanner.h 与 src/core/json\_scanner.cpp 供 param\_schema 与 nle\_project 共用2；消除 mixAvx2() 每次调用构造 std::vector\<VoicePrep\> 的堆分配2 |

### **2\. 咨询项 2 裁决细则：为何调整子阶段切分拓扑**

13 号函建议将 Phase 11.2 仅覆盖 11C 实时播放接入 GUI，而把 11D 与 11E 合并放入 Phase 11.31。该切分存在两项工程隐患：一方面，v0.2.0 的旧 PreviewViewport 基于 CreateSwapChainForHwnd（FLIP\_DISCARD）与自建设备，若在 11.2 先把 MediaPlaybackSession 接入旧视口，到 11.3 升级为 DirectComposition 零回读视口时又需推翻重接；另一方面，将 NLE 领域模型、RenderGraph、AVX2 音频矩阵、WSOLA、RIFE scale=0.5、VRAM 滞后调度与 SDK 13.1 矩阵全部压进 11.3，会再次制造作用域过载并诱发 11E 缩水1。  
因此，专家组将重启动路线调整为四个递进切片：在 Wave 0 完成环境与基础设施止血后，**Phase 11.1** 收口 11A 与 11B 底座；**Phase 11.2** 将 11C 实时播控与 11D 切片 A（DirectComposition 零回读视口）合并交付，一次性替换 rtx\_gui.cpp 中的 66 ms 假播循环并直通最终呈现路径；**Phase 11.3** 专注交付 11D 切片 B（NLE 1V+1A 与 RenderGraph）与 切片 C（AVX2/WSOLA），完成真实媒体硬切预览与容器导出；**Phase 11.4** 独立承载 11E（RIFE scale=0.5、真实 VRAM 滞后调度与 SDK 13.1 实证矩阵），确保高阶算子获得充足的真机压测与调优时间。

### **3\. 咨询项 3 裁决细则：三层集成门禁替代机械隔离与 PE 体积单一判据**

在单兵或小团队按垂直切片推进时，继续禁止修改 src/gui/\* 会人为割裂内核与产品界面之间的集成责任1。专家组同意废除机械的跨轨禁改 GUI 限制，在新版施工规范中改为显式指定每阶段的“主干接线落点文件”。  
但针对 13 号函提出的“将 CineRTX.exe 体积净增量 \> 0 作为前置硬门禁”1，必须予以修正。单纯的体积增量判据在工程上既不充分也不必要：重构或表驱动消重（如 Phase 11.1 用查表代替重复分支）可能使 PE 体积持平甚至微降；而投机者只需在入口函数引用一个静态字符串或无用开关即可伪造正增量。后续所有阶段必须同时满足三层集成门禁：第一，**Linker .map 符号留存门禁**，在构建期生成 CineRTX.exe.map 并自动化校验本阶段核心工厂与调度符号（如 MediaPlaybackSession::create、PreviewViewport::presentShared、CompiledRenderGraph::compile、VramAdaptiveScheduler::evaluateStep）确实位于已链接节区而非被 /OPT:REF 剥离；第二，**交互式桌面实呈门禁**，涉及画面与交互的切片必须在真实交互式 Windows 桌面会话（WTS Session ID \> 0）中启动 CineRTX.exe 验证 presented \> 0、D2H \== 0 B 及音画同步；第三，**头文件与许可证分层铁律不变**，即废除跨轨文件锁的同时，继续严守 schema/ 零外部依赖、include/shim/ 与 src/shim/ 的 MIT OR Apache-2.0 边界、以及 src/core/ 绝不反向 \#include GUI 或 d3d12.h 的架构红线5。

### **4\. 咨询项 4 裁决细则：静态 FFmpeg LGPL 与 CPU 编码器拍板**

结合 cmake/RtxFfmpegStatic.cmake、src/core/cpu\_video\_encoder.cpp、cmake/AuditImports.cmake 与第三方许可证资料的综合评估，**v0.3.0 正式裁定采纳选项 A（清理 av1\_cpu 与 hevc\_cpu 死选项）**1：

| 评估维度 | 选项 A：v0.3.0 清理 av1\_cpu / hevc\_cpu（裁定采纳） | 选项 B：在 LGPL 构建中强行引入 libsvtav1 |
| :---- | :---- | :---- |
| 许可证合规性 | 维持 FFmpeg 静态归档为纯 LGPLv2.1+，与静态内联的 nvsdk\_ngx\_s.lib 专有存根无 GPL 冲突6 | SVT-AV1 采用 BSD-3-Clause-Clear \+ AOMedia Patent License 1.039，FFmpeg 配置 \--enable-libsvtav1 无需 \--enable-gpl42；但 **hevc\_cpu（libx265 为 GPL）仍不可开启**2，双编码器契约依然残缺 |
| PE 体积包络（\[1.2 MB, 12.0 MB\] 红线）7 | CineRTX.exe 当前约 4.43 MB（4,429,312 字节）2，为后续 11C–11E 主干接线保留充足空间 | 据社区 Windows 静态构建实测统计，libSvtAv1Enc.a 归档体积约 23 MB29；即便经过 /OPT:REF 链接裁剪，静态合入仍面临逼近或突破 12.0 MB 单 EXE 上限的高风险 |
| 构建供应链复杂度 | 仅需从 RtxFfmpegStatic.cmake 移除无效的 \--enable-encoder=libsvtav1,libx265,libx2646，维持现有 MSVC /MT 与 Zig 0.13 零外部依赖闭环 | 必须在 RtxFfmpegStatic.cmake 中为 MSVC /MT 与 Zig 0.13 交叉工具链同步构建 SVT-AV1 源码拉取、NASM 汇编与静态库校验链路6，显著拉长 Phase 11 恢复周期 |

落地选项 A 时需同步完成四项动作：第一，从 schema/schema\_model.hpp 中删除 av1\_cpu、hevc\_cpu 枚举（及仅服务于它们的 crf 选项与 legacy cpuPreset 分支），由 GCC 16 重新生成 param\_schema.generated.\* 与 schema.json34；第二，在 validateAndNormalizeProfile() 中保留对旧配置传入 av1\_cpu / hevc\_cpu 的显式识别，返回 ErrorCode::Unsupported 结构化诊断而非静默降级；第三，同步修正 06 号施工单与 README\*.md 中的“CPU CRF”过时声明；第四，将极简合法的三制式（H.264/HEVC/AV1 \+ AAC）媒体测试样本以独立小文件形式存放于 tests/fixtures/ 目录供 test\_playback\_session.cpp 与 test\_nle\_vertical\_slice.cpp 读取（AuditSpdx.cmake 禁止的是 .lib/.dll/.exe/.engine/.onnx 等构建与模型产物37，轻量媒体夹具不会触发合规违规，同时彻底根除 864 行内联十六进制数组问题2）。

## **三、 修复后 Phase 11 至 v0.4.0 项目演进计划**

需要向项目管理层坦诚校准工期预期：13 号咨询函估算方案三总周期为 3–5 天1，该估算仅足以覆盖 Wave 0 止血与 11A–11D 已有资产的提纯接线；考虑到 Track 11E（RIFE scale=0.5 ABI 与内核对齐、真实多维显存调度接线、SDK 13.1 探测与全口径客观画质/性能实测矩阵）此前几乎为零交付2，且所有图形门禁均需迁回真实交互式 Windows 桌面复测2，按高质量垂直切片推进，**完成 v0.3.0 全量封板的合理工程周期建议预留 12–16 个有效工作日**。

### **1\. 纯净重启与 Wave 0 止血操作清单（第 1–2 日）**

> 1. 在当前脏工作区执行 git checkout \-b archive/phase11-attempt-1 并提交封存全量现场与未跟踪文件（保留全部已排查的真机修复记录作为只读参考库）1。  
> 2. 切回主干并执行 git reset \--hard 52103a7 与 git clean \-fd，确保工作区恢复至纯净的 v0.2.0 基线（78/78 GPU 单测 PASS）1。  
> 3. 将 RTX 5060 测试机 NVIDIA 驱动由 621.01 升级至 621.14 WHQL 或更高版本以排除驱动级视频播放冻结/黑屏干扰9，并打通可运行图形界面的交互式 Windows 桌面测试会话（WTS Session ID \> 0）2。  
> 4. 正式落盘两项产品级拍板：v0.3.0 保持 nv-codec-headers n13.0.19.1（R580 兼容默认基线，仅把 SDK 13.1 隔离在运行时探测与实验开关之后）3，以及采用选项 A 清理 RtxFfmpegStatic.cmake 中的无效 CPU 编码器开关6。  
> 5. 先行新建公共底座 include/rtx/json\_scanner.h / src/core/json\_scanner.cpp、include/rtx/com\_owner.h 与 tests/fixtures/ 轻量测试夹具，从源头杜绝后续阶段重复造轮子2。  
> 6. 在 rife\_builder.cpp 补充 config-\>clearFlag(nvinfer1::BuilderFlag::kTF32) 并扩大高精度锁定层范围至合成网络卷积与缩放/激活层2，验证 rife\_four\_phase\_monotonic\_progression 连续运行 10 次 100% PASS（引擎字节数一致、无超差与误判）2。

### **2\. Phase 11.1 至 v0.4.0 分阶段演进矩阵**

| 演进阶段（建议工期） | 核心目标与包含轨道 | 提纯合入与主干接线重点 | 硬性验收退出门禁 |
| :---- | :---- | :---- | :---- |
| **Phase 11.1**（2–3 日） | P0 资源秩序与契约闭环（Track 11A \+ Track 11B）1 | 1\. 提纯 raii\_owners.h、d3d12\_raii.h（复用 com\_owner.h）、NvidiaTranscodeSession / RemasterPipeline 成员拓扑重排；清理 rtx\_gui.cpp 裸 std::thread / GDI 句柄与 workstation\_canvas.cpp 裸 new / delete2。 2\. 提纯 schema\_model.hpp 与 reflection\_codegen.cpp，剔除 av1\_cpu / hevc\_cpu；改造 param\_schema.cpp 中 validateAndNormalizeProfile() 与 exportSchemaJson()，全面消费生成表 kParamSchema2。 | Gate 11A-1/2（含 NvidiaTranscodeSession 与 RemasterPipeline 100 次创建/销毁及每步失败注入压测）与 Gate 11B-1（GCC 16 生成日志、重复 Key/Flag 编译期报错负向测试、CI Diff 与 Zig 0.13 构建）100% 实证归档2。 |
| **Phase 11.2**（3–4 日） | P1 实时视听与零回读视口闭环（Track 11C \+ Track 11D 切片 A）4 | 1\. CMake 增加构建期调用 fxc.exe 生成 preview\_tone\_map\_cs.h 至 \${CMAKE\_BINARY\_DIR}/generated/2。 2\. 落地 d3d12\_interop 借用设备接口与 PreviewViewport DComp 重构，**在 presentShared() 中将消费中的 shared\_resource 压入 retirement\_.retire(fence\_value\_, ...)**2，并修复 enable\_truehdr \= 0 下 SDR/PQ 色彩语义错配2。 3\. 提纯 time\_model.h、telemetry\_bus.h、WasapiAudioOutput、MediaPlaybackSession，修复 NvidiaShim 非终态解码 Flush 能力以消除 Seek 重建 NVDEC 开销2。 4\. **主干接线**：在 rtx\_gui.cpp 拔除 WM\_TIMER(3, 66ms) 与 runScrubPreviewAsync()，接入 MediaPlaybackSession \+ AtomicTelemetryBus（30 Hz）+ DComp 视口 \+ 队列缩略预览2。 | .map 验证核心类符号留存于 CineRTX.exe；交互式 Windows 桌面实测 H.264/HEVC/AV1 原生播放、IAudioClock 跟随、,/. 步进、设备热拔插（AUDCLNT\_E\_DEVICE\_INVALIDATED）、HDR/SDR 跨屏切换与 100 次 resizeSwapChain，实证 presented \> 0 且 D2H \== 0 B2。 |
| **Phase 11.3**（3–4 日） | P2 NLE 垂直切片与 AVX2 音频总线闭环（Track 11D 切片 B \+ 切片 C）4 | 1\. 基于 json\_scanner 落地 NleProject、6 类可逆 IEditCommand 与 CompiledRenderGraph2。 2\. **主干接线**：用 NleProject \+ EditHistory 彻底替换 workstation\_canvas.cpp 中的 sharedNleTimeline()（清零 msToFlicks(100) 残留）2，打通 CompiledRenderGraph 驱动 MediaPlaybackSession 实时预览与 RemasterPipeline 多段硬切导出（硬切点自动下发 reset\_temporal 与强制 IDR）2。 3\. 消除 mixAvx2() 的 std::vector\<VoicePrep\> 每次调用堆分配，将 wsolaStretch 与 mixAvx2 接入 MediaPlaybackSession（替换 VarispeedCursor）及 NLE 导出总线2。 | .map 验证 NleProject / CompiledRenderGraph / mixAvx2 / wsolaStretch 符号留存；端到端真实多段硬切导出产物通过容器级帧数、切点 IDR 与时间线逐帧一致性校验（替代纯内存数值比对）2；交互式桌面完成 1V+1A 剪辑/撤销/重链接/导出全流程实测4。 |
| **Phase 11.4**（4–5 日） | P3 高阶算子与 SDK 13.1 实证封板（Track 11E）3 | 1\. 落地 ngx\_bridge\_abi.h（ABI v5 RtxRifeConfig 与 rtx\_ngx\_bridge\_init\_rife\_ex）、rife\_engine.cpp / rife\_builder.cpp / rife\_pack.cu 的 A=128 对齐与 reflect101 边缘填充裁剪，修正 hw\_transcode\_stages.cpp 按实际拓扑传递目标宽高，新建 tests/test\_rife\_scale05.cpp3。 2\. 在 RemasterPipeline 与 MediaPlaybackSession 接入真实 DXGI 本地显存预算、cuMemGetInfo 与 TRT 峰值分配驱动 VramAdaptiveScheduler（消除 vram\_used\_bytes \= 0 硬编码）2。 3\. 在 nv\_encoder.cpp 实现 queryAv1AdvancedCaps()，验证低版本驱动回退至 Default 及在 621.14+ 下实测 Default vs HighQuality-7B vs Master-UHQ-15B/31B3。 | Gate 11E-1 全绿：8 GB RTX 5060 4K 压制无 OOM、scale=0.5 无边缘黑边/撕裂且附真实 trt\_peak\_allocation\_bytes 对比、完整测试口径下的 FPS/编码器占用/VRAM/PSNR/SSIM/VMAF 实测矩阵落盘3；AuditSpdx.cmake、AuditImports.cmake（\[1.2 MB, 12.0 MB\]）与全量 rtx\_tests.exe 100% PASS，正式发布 **v0.3.0**5。 |
| **Wave 4（v0.4.0 远期规划）** | 多轨转场、动态 VSR 实时播放与 WebUI 双向同步2 | 在稳固的 v0.3.0 底盘之上，于 CompiledRenderGraph 叠加多轨 Compute Shader 叠化转场（Cross-Dissolve），为 MediaPlaybackSession 探索运行期轻量动态 VSR 挂载调度，打通 WebUI v3 与核心引擎的双向工程状态同步，并视需要独立评估 libsvtav1 插件化接入2。 | 依据 v0.4.0 独立立项评审门禁执行。 |

## **四、 新任执行工程师必读：五大关键技术防线**

在资产提纯与各阶段重构期间，所有涉及跨线程、跨 API 域的改动必须经过以下五条技术防线自检，任何一条失守均直接退回。  
第一条防线是 **D3D12 共享资源的异步退役闭环**。在 rtx\_gui.cpp 中，publishPreviewHandoff() 通过 ComOwner\<IUnknown\> 保护了借用纹理在跨线程消息传递期间的生命周期2；但 UI 线程在 PreviewViewport::presentShared() 中将该纹理绑定为 SRV 并向 cmd\_queue\_ 提交 Dispatch 后，必须把一份 AddRef 后的 ComOwner\<IUnknown\> 连同本次递增后的 fence\_value\_ 一并登记进 retirement\_.retire(fence\_value\_, ...)2。唯有等到 cmd\_fence\_-\>GetCompletedValue() \>= fence\_value\_ 之后才允许释放该引用，从而彻底杜绝转码桥接中途销毁引发的 GPU 读取悬垂指针2。  
第二条防线是 **NVDEC 上下文线程亲和性与非终态 Seek 复位**。由于 cuCtxSetCurrent 仅作用于调用线程，MediaPlaybackSession 必须继续保持 NVDEC 解码器的创建、提交与销毁全部封装在 video\_worker 单一线程内，且在替换解码器时坚持“先建新会话、再销毁旧会话”以防止 CUDA 主上下文引用计数归零8。同时，新工程师需在 NvidiaShim 层将 decoderFlush 从当前的“永久置为终态 EOS”升级为可继续 submit 的序列复位，彻底消除拖拽时间线或逐帧后退时反复重建 CUvideodecoder 实例的性能抖动2。  
第三条防线是 **TensorRT 10.13.3.9 混合精度确定性锁定**。rife\_builder.cpp 除保留 ver \>= 101400 的版本防劣化拒绝开关外（规避 TensorRT 10.14+ 在混合精度 GridSample 优化器上的显存暴涨与张量损坏回归）12，在设置 BuilderFlag::kFP16 与 kOBEY\_PRECISION\_CONSTRAINTS 时，必须显式调用 config-\>clearFlag(nvinfer1::BuilderFlag::kTF32) 并将 FP32 锁覆盖至卷积、矩阵乘及激活等敏感路径2，防止不同构建轮次因 TensorRT autotuner 切换 TF32/FP16 tactic 造成 max \|diff\| \= 37 的精度漂移与场景切误触发8。  
第四条防线是 **SDR 直通模式的线性与 PQ 色彩语义对齐**。当 enable\_truehdr \== 0 时，bridge\_exports.cpp 向 960×540 预览共享缓冲写入的是线性 studio-swing 数据而非 PQ 编码，但 preview\_tone\_map\_cs.hlsl 统一按 PQ 解码导致 SDR 右半预览图异常压暗2。重构 DComp 视口时，必须在 ViewportConstants 中显式传递输入传递函数语义标志（PQ 对照线性 SDR），使 Compute Shader 在不更改任何用户可见开关的前提下对两条通路分别执行正确的色彩转换，且绝对禁止污染送往 NVENC 编码器的原始 4K P010 显存表面4。  
第五条防线是 **实时音频矩阵与回调线程的绝对零分配约束**。在将 mixAvx2() 与 wsolaStretch 接入 MediaPlaybackSession 与 NLE 实时预览总线前，必须重构 audio\_mixer\_avx2.cpp 中 mixAvx2() 每次调用均构造 std::vector\<VoicePrep\> preps 的实现2，改为使用定长栈缓冲（例如按常规并发轨数上限设计 std::array 配合分块处理，或由调用方传入预分配工作区），与 WasapiAudioOutput 的事件回调路径共同严守“零文件 I/O、零动态堆分配、零互斥锁竞争、零 GPU Fence 同步等待”四条实时音频红线15。

#### **引用的著作**

> 1. 13\_CineRTX Phase 11 施工受挫复盘与返工路径专家咨询函.md [受控工程档案]  
> 2. 12\_CineRTX Phase 11 验收与演进.md [受控工程档案]  
> 3. 10\_CineRTX\_Phase11\_施工单\_Track\_11E\_VSR\_FRUC显存感知自适应调度与SDK13.1分级接入.md [受控工程档案]  
> 4. 09\_CineRTX\_Phase11\_施工单\_Track\_11D\_DirectComposition零回读视口与统一NLE引擎及AVX2音频矩阵.md [受控工程档案]  
> 5. 05\_CineRTX\_Phase11\_SuperPower施工总纲\_五轨并行契约与物理文件隔离矩阵.md [受控工程档案]  
> 6. RtxFfmpegStatic.cmake [受控工程档案]  
> 7. AuditImports.cmake [受控工程档案]  
> 8. 11\_验收实证记录.md [受控工程档案]  
> 9. Game Ready Driver 621.01 FAQ/Discussion : r/nvidia \- Reddit, [https\://www\.reddit.com/r/nvidia/comments/1xbn3fy/game\_ready\_driver\_62101\_faqdiscussion/](https://www.reddit.com/r/nvidia/comments/1xbn3fy/game_ready_driver_62101_faqdiscussion/)  
> 10. Nvidia driver 621.14 fixes RTX 50 video freezes and adds ... \- Reddit, [https\://www\.reddit.com/r/pcmasterrace/comments/1xomrel/nvidia\_driver\_62114\_fixes\_rtx\_50\_video\_freezes/](https://www.reddit.com/r/pcmasterrace/comments/1xomrel/nvidia_driver_62114_fixes_rtx_50_video_freezes/)  
> 11. NVIDIA GeForce 621.14 WHQL Game Ready Drivers Released, [https\://www\.techpowerup.com/354465/nvidia-geforce-621-14-whql-game-ready-drivers-released](https://www.techpowerup.com/354465/nvidia-geforce-621-14-whql-game-ready-drivers-released)  
> 12. rife\_builder.cpp [受控工程档案]  
> 13. Read Me \- NVIDIA Documentation Hub, [https\://docs.nvidia.com/video-technologies/video-codec-sdk/13.1/read-me/index.html](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.1/read-me/index.html)  
> 14. 04\_CineRTX Phase 11 冲突评判与方案.md [受控工程档案]  
> 15. 08\_CineRTX\_Phase11\_施工单\_Track\_11C\_Flicks语义重构与WASAPI实时播放内核.md [受控工程档案]  
> 16. rtx\_gui.cpp [受控工程档案]  
> 17. workstation\_canvas.cpp [受控工程档案]  
> 18. cpu\_video\_encoder.cpp [受控工程档案]  
> 19. IDXGIFactory::CreateSwapChain (dxgi.h) \- Win32 apps, [https\://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgifactory-createswapchain](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgifactory-createswapchain)  
> 20. D3D11CreateDeviceAndSwapC, [https\://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-d3d11createdeviceandswapchain](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-d3d11createdeviceandswapchain)  
> 21. bridge\_exports.cpp [受控工程档案]  
> 22. hw\_transcode\_stages.cpp [受控工程档案]  
> 23. vram\_scheduler.cpp [受控工程档案]  
> 24. preview\_viewport.cpp [受控工程档案]  
> 25. param\_schema.cpp [受控工程档案]  
> 26. nle\_project.cpp [受控工程档案]  
> 27. wasapi\_audio\_output.cpp [受控工程档案]  
> 28. audio\_mixer\_avx2.cpp [受控工程档案]  
> 29. ffmpeg 7.0.1 and Windows 7 \- Page 7 \- VideoHelp Forum, [https\://forum.videohelp.com/threads/414767-ffmpeg-7-0-1-and-Windows-7/page7](https://forum.videohelp.com/threads/414767-ffmpeg-7-0-1-and-Windows-7/page7)  
> 30. d3d12\_raii.h [受控工程档案]  
> 31. raii\_owners.h [受控工程档案]  
> 32. 06\_CineRTX\_Phase11\_施工单\_Track\_11A\_基线元数据收口与五域异构RAII退役体系.md [受控工程档案]  
> 33. reflection\_codegen.cpp [受控工程档案]  
> 34. schema\_model.hpp [受控工程档案]  
> 35. playback\_session.cpp [受控工程档案]  
> 36. audio\_mixer\_avx2.h [受控工程档案]  
> 37. AuditSpdx.cmake [受控工程档案]  
> 38. 07\_CineRTX\_Phase11\_施工单\_Track\_11B\_C++26\_P2996静态反射独立代码生成轨.md [受控工程档案]  
> 39. wabcut/docs/building.md at main \- GitHub, [https\://github.com/sorsama/wabcut/blob/main/docs/building.md](https://github.com/sorsama/wabcut/blob/main/docs/building.md)  
> 40. Third-party software \- Fenrir, [https\://www\.fenrirapp.com/third-party](https://www.fenrirapp.com/third-party)  
> 41. Third-party software · MOVON Labs, [https\://movonlabs.com/legal/third-party](https://movonlabs.com/legal/third-party)  
> 42. configure \- chromium/third\_party/ffmpeg \- Git at Google, [https\://chromium.googlesource.com/chromium/third\_party/ffmpeg/+/refs/heads/sushi-2021-11-08-11-26-35/configure](https://chromium.googlesource.com/chromium/third_party/ffmpeg/+/refs/heads/sushi-2021-11-08-11-26-35/configure)  
> 43. ffmpeg-windows-build-helpers/cross\_compile\_ffmpeg.sh at master, [https\://github.com/rdp/ffmpeg-windows-build-helpers/blob/master/cross\_compile\_ffmpeg.sh](https://github.com/rdp/ffmpeg-windows-build-helpers/blob/master/cross_compile_ffmpeg.sh)  
> 44. render\_graph.cpp [受控工程档案]  
> 45. CineRTX Phase 8：TensorRT \+ RIFE 深度学习补帧外部技术情报与执行参考手册.md [受控工程档案]