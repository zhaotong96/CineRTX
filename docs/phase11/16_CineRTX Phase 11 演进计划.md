# **CineRTX Phase 11.1 终审验收裁决暨 Phase 11.2–v0.4.0 项目演进计划**

针对项目工程调度团队呈报的 15 号咨询委托书，首席领域专家组结合 14 号复盘裁决确立的“方案三（分支归档封存 \+ 回滚纯净 v0.2.0 \+ 资产定向提纯重启）”与四阶段垂直切片路线，已完成对 Wave 0 与 Phase 11.1 源码落地状态、测试覆盖实质及构建门禁的穿透审计1。专家组对 15 号委托书四项请示裁决如下：**第一，Wave 0 与 Phase 11.1 予以“有条件通过（CONDITIONAL PASS）”并准予基线封板**，但须在 Phase 11.2 开工首日并行清零核验检出的 5 项遗留欠账（含测试机驱动升级至 621.14 WHQL 及以上、切入交互式桌面会话、补全 RIFE 10 次连跑一致性验证、param\_schema.cpp 迁移复用公共 json\_scanner.h 及深化管线中段失败注入压测）1；**第二，确认 PreviewViewport::presentShared() 异步退役方案主干可行，但实审源码发现 UINT64\_MAX 设备移除假完成陷阱、releaseGpu() 遗漏命令对象退役等 4 处高危边界隐患，须于 Phase 11.2 一并封堵**1；**第三，批准将 Linker .map 符号留存设为 Phase 11.2 及后续合龙阶段的前置硬卡点，但须按 MSVC 修饰名解析、内联函数豁免及桌面运行时联动口径执行**1；**第四，在落实上述前置边界后，正式批准启动 Phase 11.2 施工**，并按约 10.5–13.5 个有效工作日（约 10–14 天）的余量节奏推进至 v0.3.0 封板与 v0.4.0 架构升维1。  
总体而言，团队在两个半小时内将主干从此前膨胀 17,000 余行的脏现场收口至纯净基线，使 rtx\_tests.exe 达到 109/109 PASS（CineRTX.exe 体积 5,106,176 字节，约 4.87 MB，符合 1.2–12.0 MB 包络与零违规 DLL 要求），有效扭转了首轮“单测孤岛”与主干脱节的局面1。尽管如此，逐行代码核验仍暴露出测试提前短路与旧 JSON 解析逻辑未迁出等局部尾欠；趁 Phase 11.2 进入高并发音视频同步与 DirectComposition 合成之前将其及时校准，是确保后续切片不再返工的关键前提2。

| 请示议题 | 15 号委托书诉求 | 专家组终审裁决 | 强制附带条件与纠偏指令 |
| :---- | :---- | :---- | :---- |
| 请示项 1：Wave 0 与 Phase 11.1 封板 | 认可实测证据并批准完成 Phase 11.1 验收封板1 | **有条件通过（CONDITIONAL PASS）** | 开工即核验测试机驱动 \>= 621.14 WHQL 与 WTS Session ID \> 0；在 11.2 首日清零 5 项审计欠账（A-01 至 A-05）1。 |
| 请示项 2：视口异步 UAF 封堵方案 | 审核 presentShared() 将 shared\_resource 登记入 retirement\_.retire() 的闭环设计1 | **主干批准，须补 4 处边界加固** | 封堵 GetCompletedValue() \== UINT64\_MAX 假完成陷阱；在 releaseGpu() 补全命令分配器/列表/PSO/根签名退役；修正帧延迟等待序与 SDR/PQ 标志5。 |
| 请示项 3：Linker .map 门禁口径 | 检索核心符号留存以防 /OPT:REF 剥离空心交付1 | **批准并升级为精密执行口径** | 仅审计 CineRTX.exe.map；匹配 Publics by Value MSVC 修饰名；豁免纯内联类 AtomicTelemetryBus；联动 66 ms 假播清零扫描与桌面实呈2。 |
| 请示项 4：Phase 11.2 正式开工批复 | 批准派遣子代理启动 Phase 11.2 施工1 | **批准开工** | 先解除驱动与交互式桌面盲区，按“构建期着色器 → 内核与非终态 Flush → GUI 主干拔旧接新 → 三层门禁”顺序合龙1。 |

## **一、 Wave 0 与 Phase 11.1 穿透式代码审计与欠账清单**

穿透核验当前工作区代码可确认，Wave 0 与 Phase 11.1 的核心骨架已按 14 号裁决高质量落盘1。历史脏现场已全量封存至 archive/phase11-attempt-1（Commit e04429a），主干干净回滚至 v0.2.0（52103a7）1。在 src/shim/ngx\_bridge/rife\_builder.cpp 中，TensorRT 构建配置已明确调用 config-\>clearFlag(nvinfer1::BuilderFlag::kTF32)，并将 FP32 精度锁由早先的 6 类算子扩展至涵盖 kCONVOLUTION、kMATRIX\_MULTIPLY、kACTIVATION 与 kSCALE 在内的 10 类坐标与算术敏感层（注：15 号文将其简述为“扩大至 5 类核心卷积/激活层”，源码实测共锁定 10 类层）1；同时 rife\_pack\_runtime.cpp 已接入 memset\_d32\_async 以消除 Block SAD 显存未清零导致的随机性误切1。在 Track 11A/11B 方面，全仓统一了仅依赖 \<unknwn.h\> 的 include/rtx/com\_owner.h 并接入 src/gui/d3d12\_raii.h1；NvidiaTranscodeSession 成员拓扑已按依赖逆序重排并改用 \= default 析构，CudaPinnedHostBuffer 彻底废除了 std::free() 误释 CUDA 锁页内存的隐患1；schema/schema\_model.hpp 与 schema/generated/param\_schema.generated.hpp（源码指纹 96d1299b66458807ac2861d70be89427f0e71c07ad038f332c171f73f6297db6）已移除 av1\_cpu、hevc\_cpu 与 crf 死选项，将 20 项参数契约收敛为纯 GPU 编码（preset 范围 1..7、qp 默认 22）20，且 src/core/param\_schema.cpp 中的 validateAndNormalizeProfile() 与 exportSchemaJson() 已全面改为由 forEachProfileField 与生成表 kParamSchema 驱动，对废弃 CPU 编码请求显式返回 ErrorCode::Unsupported1。  
对照 14 号裁决的绝对门禁，本轮穿透审计同时定位出 5 项微观执行偏差与审计证据缺口，须在 Phase 11.2 开工首日予以清零：

| 审计编号 | 核验对象与源码锚点 | 15 号委托书申报状态 | 穿透核验真实发现 | 风险定级与处置动作 |
| :---- | :---- | :---- | :---- | :---- |
| **A-01** | 测试机驱动版本与桌面会话（14 号裁决前置条）2 | Wave 0 与 11.1 报告未列示驱动版本及 WTS 会话 ID1 | 14 号文与 NVIDIA 发行说明指出 621.01 存在 RTX 50 系列视频播放冻结/黑屏缺陷 \[6979343\]，须先升至 621.14 WHQL 及以上并切出 Session 0 无头环境2。 | **P0（11.2 实测阻塞）**：开工前输出 nvidia-smi 驱动版本及 quser/WTS Session ID \> 0 实证。 |
| **A-02** | tests/test\_raii\_retirement.cpp 中的核心会话压测3 | 申报对 NvidiaTranscodeSession 与 RemasterPipeline 实施 100 次创建/销毁与每步失败注入、断言释放计数守恒1 | 源码中 transcode\_session\_and\_pipeline\_100x\_failure\_injection\_zero\_leak 仅注入空路径与不存在文件（在首步 I/O 校验即短路），未触及后续 CUDA/Bridge/NVDEC 阶段；释放计数断言仅在下方 MockResource 测试中执行3。 | **P1（压测深度不足）**：利用 tests/fixtures/ 合法小文件补充参数越界与编解码中段失败注入压测，补强真实对象生命周期覆盖。 |
| **A-03** | src/core/param\_schema.cpp 与 include/rtx/json\_scanner.h1 | 申报 Wave 0 已落地公共 json\_scanner 并完成消重1 | param\_schema.cpp 仍未 \#include "rtx/json\_scanner.h"，文件内部依然保留完整的私有 JsonMember、scanJsonString、scanJsonObject 与 decodeJsonString 实现4。 | **P1（基础设施未合流）**：立即将 param\_schema.cpp 切换至公共 json\_scanner，防止 11.3 引入 nle\_project.cpp 时再度出现双份扫描器。 |
| **A-04** | Gate 11B-1 全链实证与 RIFE 连跑次数1 | 申报 RIFE 连续跑 5 次 PASS，反射生成与 13 项单测全绿1 | 14 号文要求 RIFE 稳定性**连续实跑 10 次**并验证引擎字节数一致2；当前检索到的 test\_param\_schema\_reflection.cpp 实含 12 个 RTX\_TEST 用例25，且委托书未归档 verify\_ssot.py（parity / single-field / negative）运行日志、CI Diff 及 Zig 0.13 构建实证1。 | **P1（审计证据补齐）**：以脚本补跑 RIFE 10 次连跑、verify\_ssot.py 三项子命令与 Zig 0.13 构建并落盘日志。 |
| **A-05** | src/gui/workstation\_canvas.cpp 与着色器头文件位置3 | 申报工作区极度纯净，着色器将于 11.2 移出源码树1 | workstation\_canvas.cpp 中 ChromeText::SendEncoder 存在宽字符编码乱码（L"ðŸš€ ..."）26；此外 preview\_viewport.cpp 与 test\_raii\_retirement.cpp 均直接包含了 shaders/preview\_tone\_map\_cs.h3。 | **P2（工程卫生）**：修复 SendEncoder 字面量乱码；11.2 将 fxc.exe 生成产物迁至 \${CMAKE\_BINARY\_DIR}/generated/ 时须同步放行 CineRTX 与 rtx\_tests 包含路径并 git rm 源码树旧头文件。 |

鉴于 Wave 0 与 Phase 11.1 的底层架构主干（五域 RAII 成员拓扑、全局 ComOwner 收口、C++26 反射生成链与表驱动参数规范）健康稳固1，专家组同意予以封板，上述 A-01 至 A-05 项可作为 Phase 11.2 第 1 日上午的并行微整改任务消化，无须推倒重来。

## **二、 Phase 11.2 关键技术设计批复与深层隐患封堵**

### **1\. 对请示项 2 的深度审查：异步 UAF 封堵方案与四处高危边界补丁**

15 号委托书拟在 PreviewViewport::presentShared() 中对借用的 shared\_resource 执行 AddRef 后装入 ComOwner\<IUnknown\>，连同递增后的 fence\_value\_ 一并登记入 retirement\_.retire(fence\_value\_, std::move(res))，确保直到 GPU 异步计算着色器完成执行后才释放该资源1。从 Direct3D 12 运行时契约看，由于 ID3D12GraphicsCommandList 录制命令时不会对绑定的 ID3D12Resource 增加 COM 引用计数15，而 presentShared() 在提交 Compute Shader 并执行 Signal 后立即返回5，该延迟退役模型在主干方向上完全符合异构生命周期安全规范2。  
然而，对照当前已落盘或草拟的 src/gui/preview\_viewport.cpp、src/gui/d3d12\_raii.h 与 src/gui/rtx\_gui.cpp 源码细节5，专家组检出 4 处极易在设备异常、桥接切换或高频预览下触发隐患的边界漏洞，Phase 11.2 施工时必须强制同步修复：

> 1. **封堵 GetCompletedValue() \== UINT64\_MAX 设备移除假完成陷阱**：依据 Microsoft D3D12 官方规范，一旦发生 TDR 超时或设备移除（DXGI\_ERROR\_DEVICE\_REMOVED），所有受监控 Fence 的 GetCompletedValue() 均会被系统强制推至 UINT64\_MAX7。当前 PreviewViewport::waitForGpuIdle() 直接判断 \`if (fence\_value\_ \== 0 \|

\| cmd\_fence\_-\>GetCompletedValue() \>= fence\_value\_) 即回收退役池并返回成功5；一旦 GPU 掉线，UINT64\_MAX \>= fence\_value\_恒为真，将绕过GetDeviceRemovedReason() 检测并将已移除设备误判为空闲健康 \[cite: 5, 8\]。因此，waitForGpuIdle()与GpuRetirementQueue::collect()在读取完成值后必须优先校验completed \== UINT64\_MAX；若命中，须立即调用 device\_-\>GetDeviceRemovedReason()，置位 device\_lost\_ \= true并通过retirement\_.releaseAll()受控归还引用且返回ErrorCode::DeviceLost\[cite: 5, 6, 8\]。 2\. \*\*补全releaseGpu()在WAIT\_TIMEOUT路径下的全量命令对象退役\*\*：当前retireAllGpuResources()仅把render\_targets\_、view\_heap\_与swap\_chain\_ 移入延迟退役队列5。当桥接重建（adoptDevice()）或视口析构（releaseGpu(true)）遇到 GPU 繁忙超时（WAIT\_TIMEOUT）时，仅这三类对象进入进程级延迟退役池，而正在被 GPU 队列执行的 cmd\_allocator\_、cmd\_list\_、pipeline\_、root\_signature\_、cmd\_queue\_与旧device\_仍会在函数返回后立即随成员析构或覆写被Release()\[cite: 5, 6\]，违反 D3D12“命令分配器与管线状态在命令列表执行完毕前不得销毁”的契约15。改造时须将上述命令级对象及旧设备一并纳入retireAllGpuResources()，同时严禁以 (void)忽略cmd\_queue\_-\>Signal()的失败返回值——一旦Signal失败，不得将资源挂在不可达的fence\_value\_上，应直接触发device\_lost\_清理路径。 3\. \*\*校准帧延迟等待序并防范 2-Slot 预览环的跨队列写入撕裂\*\*：当前presentShared()先以 0 ms 超时调用WaitForSingleObject(swap\_chain\_-\>GetFrameLatencyWaitableObject(), 0)，随后再探测 waitForGpuIdle(0)\[cite: 5\]。若帧延迟信号已就绪并被消费，而紧接着的waitForGpuIdle(0)返回忙碌超时导致跳过Present()，可等待对象计数已被白白消耗，可能引发后续帧唤醒失配。应当调整为先探测 waitForGpuIdle(0)，确认计算队列空闲后再探测 GetFrameLatencyWaitableObject()。同时，D3D12Interop的 960×540 共享预览缓冲采用双槽轮转（共 4.14 MB）\[cite: 18\]，虽然ROW\_MAJOR缓冲在ExecuteCommandLists结束后会自动由NON\_PIXEL\_SHADER\_RESOURCE衰减回COMMON\[cite: 29, 30\]，但若转码/播放生产速率持续高于 UI 消费速率，CUDApack\_stream\_仍可能在视口着色器读完前覆写同一槽位。生产端应配合轻量状态探测（忙则跳过本次预览复制而非阻塞转码）或扩充至 3 槽位，确保“零撕裂且零阻塞”。 4\. \*\*拔除presentFromHostOrShared空桩与修复enable\_truehdr \= 0色彩错配\*\*：当前src/gui/rtx\_gui.cpp的WM\_APP\_PREVIEW仍调用空置的presentFromHostOrShared(frame.data(), pitch)，且 collectOptions()仍在向previewHost进行 CPU 拷贝27。Phase 11.2 须彻底删除previewHost与presentFromHostOrShared空桩并全量切换为presentShared()\[cite: 1, 27\]。同步利用ViewportConstants中现存的reserved\[3\]（不改变 48 字节 12-DWORD 根常量布局）\[cite: 5\] 显式传递输入传递函数语义标志（线性 studio-swing SDR 对照 PQ ST.2084），修正 enable\_truehdr \== 0\` 下右半预览因误按 PQ 解码导致的异常压暗，并严守绝不触碰送往 NVENC 的原始 4K P010 表面的边界铁律2。

### **2\. 对请示项 3 的深度审查：Linker .map 符号门禁的精确执行口径**

专家组批准将 Linker .map 符号留存作为 Phase 11.2 及后续阶段防止 /OPT:REF 剥离空心交付的硬性卡点1。但结合 MSVC 链接器实际机制，施工自动化脚本必须严格遵守四项执行规范，避免出现误报或漏检：  
首先，**仅对产品主程序开启 /MAP 并保持优化开关全开**。CMake 应仅为 CineRTX 目标开启 /MAP 选项，严禁用 rtx\_tests.exe.map 替代验证，且必须保持 /OPT:REF 与 /OPT:ICF 处于启用状态9。只有在全量死代码消除开启的条件下依然留在已链接节区中的符号，才能证明其真正进入了产品调用图2。  
其次，**解析 Publics by Value 中的 MSVC 修饰名并豁免纯内联类型**。MSVC .map 文件在 Publics by Value 节区中输出的是编译器修饰后的 C++ 符号名及对应的节区 Rva+Base，而非未修饰的明文签名10。自动化校验脚本需检索包含目标类名与方法名的修饰符号条目并确认其位于有效代码节区（或先经反修饰工具转换后精确比对）。同时需特别修正 15 号文拟检索 AtomicTelemetryBus 符号的口径1：该类读写方法若在头文件中内联展开，或被 /OPT:ICF 折叠进相同机器码的 COMDAT 块9，.map 表中可能不生成独立符号。因此，.map 硬卡点应锚定**确定非内联的核心工厂与控制面入口**（Phase 11.2 必查 MediaPlaybackSession::create、MediaPlaybackSession::seek、WasapiAudioOutput::create、GopSeekScheduler 核心方法、PreviewViewport::presentShared、PreviewViewport::adoptDevice），而 AtomicTelemetryBus 则通过头文件包含静态审查与运行时 30 Hz 遥测刷新来验证。  
第三，**与“负向拔除扫描”及“交互式桌面实呈”形成三层闭环**。.map 留存证明代码被链接，而不能证明旧假播循环已拔除或实际功能在桌面跑通。Phase 11.2 退出时必须同时满足：rg "runScrubPreviewAsync\|SetTimer\\(hwnd, 3, 66" src/gui/rtx\_gui.cpp 返回 0 命中27；AuditImports.cmake 校验 PE 体积落于 \[1.2 MB, 12.0 MB\] 且零违规动态 DLL13；在 WTS Session ID \> 0 的真实交互式 Windows 桌面启动 CineRTX.exe 实证 presented \> 0 与监看路径 D2H \== 0 B2。

### **3\. 对请示项 4 的批复与开工放行准则**

专家组**正式批准工程调度团队派遣专职子代理进场启动 Phase 11.2 施工**1。开工第 1 日上午须先完成驱动升级至 \>= 621.14 WHQL、打通 WTS Session ID \> 0 交互桌面会话，并同步消化 Phase 11.1 的 A-02 至 A-05 微欠账；随后严格按照“构建期着色器解耦与视口四处加固 → 实时播放内核与 NvidiaShim 非终态 Flush → rtx\_gui.cpp 拔旧接新 → 三层门禁实测”顺序串行合龙1。

## **三、 Phase 11.2 至 v0.3.0 全链路垂直切片演进规划**

基于 Wave 0 与 Phase 11.1 主体已完工的现状，自 Phase 11.2 起至 v0.3.0 正式封板，包含 0.5 日微整改收尾在内，建议预留 **10.5–13.5 个有效工作日（约 10–14 天）**（对应 Phase 11.2 约 3–4 日、Phase 11.3 约 3–4 日、Phase 11.4 约 4–5 日）2。各波次实施边界与硬性退出门禁规划如下：

| 演进波次（建议工期） | 核心目标与包含轨道 | 提纯合入与主干接线重点 | 硬性退出验收门禁 |
| :---- | :---- | :---- | :---- |
| **Phase 11.1 收尾微整改**（0.5 日，11.2 首日同步清零） | 解除测试环境阻塞并闭合 11A/11B 审计微欠账2 | 1\. 升级测试机驱动至 \>= 621.14 WHQL 并切入 WTS Session \> 0 桌面； 2\. param\_schema.cpp 改用 rtx/json\_scanner.h； 3\. 补齐 RIFE 10 次连跑一致性、verify\_ssot.py、Zig 0.13 构建及管线中段失败注入； 4\. 修正 workstation\_canvas.cpp 宽字符乱码2。 | A-01 至 A-05 五项欠账 100% 销号归档，rtx\_tests.exe 全绿无 \[SKIP-GPU\]。 |
| **Phase 11.2**（3–4 日） | P1 实时视听内核与 DirectComposition 零回读视口合龙（Track 11C \+ 11D 切片 A）1 | 1\. CMake 构建期调用 fxc.exe 生成着色器至 \${CMAKE\_BINARY\_DIR}/generated/ 并删除源码树旧头文件； 2\. 落地 PreviewViewport 四项加固（UINT64\_MAX、全命令对象退役、Waiting 序与防撕裂、SDR/PQ 标志）； 3\. 提纯 time\_model.h、telemetry\_bus.h、WasapiAudioOutput、GopSeekScheduler、MediaPlaybackSession，升级 NvidiaShim 非终态 Flush 与真实 VRAM 采样，剥离 864 行夹具至 tests/fixtures/； 4\. **rtx\_gui.cpp 主干接线**：彻底拔除 WM\_TIMER(3, 66ms) 与 runScrubPreviewAsync()，全面接入 MediaPlaybackSession \+ 30 Hz 遥测 \+ DComp 视口 \+ 队列预览1。 | 1\. CineRTX.exe.map 留存播放与视口非内联修饰符号，rg 扫描 66 ms 假播为 0； 2\. 交互式桌面实测 H.264/HEVC/AV1 播放、IAudioClock 跟随、倍速、GOP Seek、,/. 步进、热拔插恢复、100x resizeSwapChain 及 HDR/SDR 跨屏切换； 3\. 实证 presented \> 0 且 D2H \== 0 B，全量 rtx\_tests.exe 100% PASS1。 |
| **Phase 11.3**（3–4 日） | P2 NLE 垂直切片与零分配 AVX2/WSOLA 音频总线合龙（Track 11D 切片 B \+ C）2 | 1\. 基于 json\_scanner.h 落地 NleProject、6 类可逆 IEditCommand 与 CompiledRenderGraph； 2\. **workstation\_canvas.cpp 与管线主干接线**：以 NleProject \+ EditHistory 彻底替代 sharedNleTimeline()（清零 msToFlicks(100) 残留），打通 CompiledRenderGraph 驱动 MediaPlaybackSession 预览与 RemasterPipeline 多段硬切导出（切点自动下发 reset\_temporal \+ 强制 IDR）； 3\. 消除 mixAvx2() 每次调用构造 std::vector\<VoicePrep\> 的堆分配，将 wsolaStretch 与 mixAvx2 接入实时播放与 NLE 导出（替换 VarispeedCursor）2。 | 1\. .map 留存 NleProject / CompiledRenderGraph / mixAvx2 / wsolaStretch 核心符号； 2\. 真实多段硬切导出容器文件通过解复用帧数、切点 IDR 与时间线逐帧映射校验； 3\. 交互式桌面完成 1V+1A 剪辑/撤销/重链接/导出全流程实测，音频回调四条红线 0 违规，WSOLA 等单测复测 PASS2。 |
| **Phase 11.4**（4–5 日） | P3 高阶算子与 SDK 13.1 实证封板 v0.3.0（Track 11E）2 | 1\. 落地 ABI v5 RtxRifeConfig 与 rtx\_ngx\_bridge\_init\_rife\_ex，在 rife\_engine/rife\_builder/rife\_pack.cu 实现 A=128 对齐 \+ reflect101 填充裁剪，修正 hw\_transcode\_stages.cpp 按实际拓扑传递分辨率并新建 tests/test\_rife\_scale05.cpp； 2\. 在 RemasterPipeline 与 MediaPlaybackSession 接入真实 DXGI 预算、cuMemGetInfo 与 TRT 峰值驱动 VramAdaptiveScheduler； 3\. 在 nv\_encoder.cpp 实现 queryAv1AdvancedCaps()，验证低版本驱动回退与 621.14+ 下 Default vs 7B vs 15B/31B \+ UHQ 实测矩阵2。 | 1\. 8 GB RTX 5060 4K 压制无 OOM、scale=0.5 无边缘黑边且附真实 trt\_peak\_allocation\_bytes 对比； 2\. 完整口径（GPU/驱动/SDK/素材/参数/预热/窗口/次数）FPS、VRAM 与 PSNR/SSIM/VMAF 实测矩阵落盘； 3\. AuditSpdx.cmake、AuditImports.cmake（1.2–12.0 MB）与全量 rtx\_tests.exe 100% PASS，正式发布 **v0.3.0**2。 |

### **1\. Phase 11.2 深度施工要点：非终态 Seek 复位与主干彻底拔旧接新**

Phase 11.2 的核心技术难点在于兼顾“实时播放器无卡顿交互”与“DirectComposition 零回读视口稳态合成”2。在渲染路径上，通过 CMake 构建期调用 fxc.exe 生成着色器头文件至 \${CMAKE\_BINARY\_DIR}/generated/（并同时更新 CineRTX 与 rtx\_tests 包含路径）彻底终结编译产物入库问题2；同时落实前文确立的 UINT64\_MAX 校验、全命令对象延迟退役与 ViewportConstants SDR/PQ 语义对齐2。在播放内核层面，除了完整提纯 archive/phase11-attempt-1 中已定位的 9 项底层修复（包括 video\_worker 单线程绑定 CUDA 上下文、av\_bsf\_flush 解除 Seek 后 EOF 拒包、EOS 排空、pause() 锚点冻结以及先建新后毁旧会话）2，还必须把 NvidiaShim::decoderFlush 由当前的“永久置为终态 EOS”升级为基于 CUVID\_PKT\_DISCONTINUITY 的可复用非终态序列复位2，彻底消除拖拽进度条与高频 ,/. 步进时反复销毁重建 CUvideodecoder 实例的开销；同时剥离 test\_playback\_session.cpp 中 864 行内联十六进制码流至 tests/fixtures/2。完成内核提纯后，直接在 src/gui/rtx\_gui.cpp 中彻底拔除 WM\_TIMER(3, 66ms)、runScrubPreviewAsync() 与 previewHost 拷回，将 \[2\] 播放器、无边框全屏快播及队列作业预览全面接入 MediaPlaybackSession、30 Hz 无锁 AtomicTelemetryBus 与 PreviewViewport::presentShared()1，并在真实硬件上验证 AUDCLNT\_E\_DEVICE\_INVALIDATED 音频设备热拔插后的控制面无锁衔接续播2。

### **2\. Phase 11.3 深度施工要点：NLE 单一时间基替换与零分配音频矩阵**

Phase 11.3 旨在打破 11D 切片 B 与切片 C 的单测孤岛2。在工程与图模型层面，确认 param\_schema.cpp 与 nle\_project.cpp 均复用同一套 include/rtx/json\_scanner.h 后，将 src/gui/workstation\_canvas.cpp 中的全局 sharedNleTimeline() 及其残留的 msToFlicks(100) 与 \*\_ms 逻辑彻底替换为基于原生 705,600,000 Hz Flicks 的 NleProject \+ EditHistory 模型（确保 6 类 IEditCommand 严守零 GPU/COM/CUDA 句柄红线，并一并清除 ChromeText::SendEncoder 乱码）2。随后将 CompiledRenderGraph 同时接通 MediaPlaybackSession（时间线实时剪辑预览）与 RemasterPipeline（多段硬切实际容器导出），在跨片段硬切处自动注入 reset\_temporal \= true 与强制 IDR，并以输出容器文件的解复用帧数、IDR 切点与逐帧一致性检验作为唯一退出依据2。在实时音频总线层面，必须率先重构 src/core/audio\_mixer\_avx2.cpp 中的 mixAvx2()，使用固定容量栈缓冲或调用方预分配工作区取代每次调用构造 std::vector\<VoicePrep\> 的堆分配操作2，再将 wsolaStretch（此前的测试版曾在 0.5x 下测得 0.0432% 基频漂移18，移植至纯净主干后须全量复测验证）与 mixAvx2 正式接入实时播放与 NLE 导出总线，取代变调线性插值 VarispeedCursor2。

### **3\. Phase 11.4 深度施工要点：RIFE 0.5 对齐、滞后显存调度与 SDK 13.1 矩阵**

Phase 11.4 专注攻克高阶 GPU 算子与自适应调度，补齐首轮近乎为零交付的 Track 11E2。针对 RIFE scale=0.5，须在 include/shim/ngx\_bridge\_abi.h（严守 MIT OR Apache-2.0 头边界）与 bridge\_exports.cpp 追加向后兼容的 ABI v5 结构体 RtxRifeConfig 与入口 rtx\_ngx\_bridge\_init\_rife\_ex13；在 rife\_engine.cpp、rife\_builder.cpp 与 rife\_pack.cu 中针对 flow\_scale \== 0.5f 落地 A=128 像素对齐与 reflect101 边缘镜像填充裁剪，同步修正 hw\_transcode\_stages.cpp 中 initRemasterAndFgStage() 固定传源分辨率的旧缺陷，并新建 tests/test\_rife\_scale05.cpp 验证边缘无黑边及输出真实的 trt\_peak\_allocation\_bytes2。针对显存防 OOM 调度，须将 VramAdaptiveScheduler（默认降级水印 82%、恢复水印 68%、最小驻留 120 帧）接入真实 DXGI 本地显存预算、cuMemGetInfo、TRT 峰值分配及编解码参考池，在 8 GB GDDR7 显存的 RTX 5060 上实测 4K 压制全程平滑降级而不 OOM34。针对 Video Codec SDK 13.1 接入，v0.3.0 默认维持 .gitmodules 中的 nv-codec-headers n13.0.19.1（R580 兼容基线，SDK 13.1.15 要求最低驱动 \>= 610.0）34；在 nv\_encoder.cpp 实现基于 NvEncGetEncodeCaps 的 queryAv1AdvancedCaps() 运行时探测后，根据 NVIDIA 开发者技术资料，SDK 13.1 引入的 AV1 分层 B 帧支持 1/3/7/15/31 帧（厂商博客称 7 帧以上压缩增益最显著且性能影响极低，但技术综述提示会增加显存占用，如施工单对 7B 在 8 GB 显卡估计约 \~166 MiB 参考缓冲增量尚属待实测候选值）34，因此在 \>= 621.14 WHQL 下必须实测输出涵盖 Default、HighQuality-7B 与 Master-UHQ-15B/31B 的完整口径（GPU、驱动/SDK 版本、素材、编码参数、预热时长、统计窗口、重复次数）FPS、VRAM 峰值与 PSNR/SSIM/VMAF 对比矩阵，同时验证低版本驱动优雅回退至 Default2。全量门禁与 AuditSpdx.cmake / AuditImports.cmake 通过后，正式发布 **v0.3.0**2。

## **四、 Wave 4（v0.4.0）远期演进路线与长效工程治理**

在 v0.3.0 形成稳定、轻量且经过桌面实证的产品底座后，Wave 4（v0.4.0）将沿着四条高价值主线稳步扩展：

> 1. **多轨转场算子图升维**：在 CompiledRenderGraph 经过容器校验的 1V+1A 硬切底座之上，扩展多视频轨重叠区间调度与 D3D12/CUDA Compute Shader 转场节点（首期落地 Cross-Dissolve 叠化混合），继续保持预览与导出严格语义一致2。  
> 2. **实时播放会话动态 VSR 挂载**：在不违背“音频主时钟不阻塞、过期帧可丢弃”控制面隔离铁律的前提下13，基于视口缩放比与帧预算余量探索运行期轻量化动态 VSR 挂载与平滑旁路退回2。  
> 3. **WebUI v3 双向状态同步**：沿用“一核三端”契约架构，基于 EngineController 与 CineRTX.exe \--schema-json / \--session-stdio 原生 JSONL 通信管道（严守 0 WS2\_32.dll 静态导入红线），打通 WebUI v3 与 NleProject / MediaPlaybackSession 的双向工程状态与播放头同步2。  
> 4. **libsvtav1 独立插件化评估**：鉴于社区 Windows 静态构建统计显示 libSvtAv1Enc.a 归档体积约 23 MB，直接静态合入将严重威胁 CineRTX.exe 12.0 MB 单文件体积红线2，v0.4.0 如需重启软件 AV1 编码能力，应采用许可证兼容（BSD-3-Clause-Clear）的外置动态插件或独立子进程管线形式评估接入2。

在长效研发治理方面，Phase 11 前期的受挫与快速止血再次印证：仅靠静态文件权限隔离和“单测全绿”无法阻止系统性集成空心化2。自 Phase 11.2 起，调度团队应将三层集成门禁（.map 非内联修饰符号留存 \+ WTS Session \> 0 交互式桌面实呈 \+ 头文件/SPDX 分层铁律）**与**零短路测试审查（严禁以早期输入参数失败冒充完整对象生命周期压测）固化为每次合入主干的自动化硬卡点，确保进入仓库的每一行代码都在真实产品二进制与真实 GPU 硬件上产生确定性价值2。

#### **引用的著作**

> 1. 15\_CineRTX Phase 11.1 施工验收暨Phase 11.2 开工专家咨询委托书.md [受控工程档案]  
> 2. 14\_CineRTX Phase 11 复盘与演进.md [受控工程档案]  
> 3. test\_raii\_retirement.cpp [受控工程档案]  
> 4. param\_schema.cpp [受控工程档案]  
> 5. preview\_viewport.cpp [受控工程档案]  
> 6. d3d12\_raii.h [受控工程档案]  
> 7. ID3D12Fence::GetCompletedValue (d3d12.h) \- Win32 apps, [https\://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12fence-getcompletedvalue](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12fence-getcompletedvalue)  
> 8. A8C11 Story-mode Map(readback) failure — superseded by later, [https\://github.com/YehsukasThelomassus/gfx1030-dlss-nr-research/issues/82](https://github.com/YehsukasThelomassus/gfx1030-dlss-nr-research/issues/82)  
> 9. Linker Property Pages \| Microsoft Learn, [https\://learn.microsoft.com/en-us/cpp/build/reference/linker-property-pages?view=msvc-170](https://learn.microsoft.com/en-us/cpp/build/reference/linker-property-pages?view=msvc-170)  
> 10. Modern Windows Exploit Development \- Alex Omar, [https\://docs.alexomar.com/biblioteca/Modern%20Windows%20Exploit%20Development.pdf](https://docs.alexomar.com/biblioteca/Modern%20Windows%20Exploit%20Development.pdf)  
> 11. /OPT (Optimizations) \| Microsoft Learn, [https\://learn.microsoft.com/en-us/cpp/build/reference/opt-optimizations?view=msvc-170](https://learn.microsoft.com/en-us/cpp/build/reference/opt-optimizations?view=msvc-170)  
> 12. 13\_CineRTX Phase 11 施工受挫复盘与返工路径专家咨询函.md [受控工程档案]  
> 13. 05\_CineRTX\_Phase11\_SuperPower施工总纲\_五轨并行契约与物理文件隔离矩阵.md [受控工程档案]  
> 14. verify\_ssot.py [受控工程档案]  
> 15. DirectX-Specs/d3d/CPUEfficiency.md at master \- GitHub, [https\://github.com/microsoft/DirectX-Specs/blob/master/d3d/CPUEfficiency.md](https://github.com/microsoft/DirectX-Specs/blob/master/d3d/CPUEfficiency.md)  
> 16. What does the "Rva+Base" mean in a MSVC linker generated map, [https\://stackoverflow.com/questions/55569088/what-does-the-rvabase-mean-in-a-msvc-linker-generated-map-file](https://stackoverflow.com/questions/55569088/what-does-the-rvabase-mean-in-a-msvc-linker-generated-map-file)  
> 17. rife\_builder.cpp [受控工程档案]  
> 18. 11\_验收实证记录.md [受控工程档案]  
> 19. 06\_CineRTX\_Phase11\_施工单\_Track\_11A\_基线元数据收口与五域异构RAII退役体系.md [受控工程档案]  
> 20. schema\_model.hpp [受控工程档案]  
> 21. param\_schema.generated.hpp [受控工程档案]  
> 22. GeForce Game Ready Driver 621.14 \| Windows 11 \- NVIDIA, [https\://www\.nvidia.com/en-us/geforce/drivers/details/283968/](https://www.nvidia.com/en-us/geforce/drivers/details/283968/)  
> 23. NVIDIA GeForce Game Ready 621.14 WHQL Driver Released, [https\://www\.dsogaming.com/news/nvidia-geforce-game-ready-621-14-whql-driver-released/](https://www.dsogaming.com/news/nvidia-geforce-game-ready-621-14-whql-driver-released/)  
> 24. GeForce Game Ready Driver 621.14 \| Windows 11 \- NVIDIA, [https\://www\.nvidia.com/en-us/drivers/details/284062/](https://www.nvidia.com/en-us/drivers/details/284062/)  
> 25. test\_param\_schema\_reflection.cpp [受控工程档案]  
> 26. workstation\_canvas.cpp [受控工程档案]  
> 27. rtx\_gui.cpp [受控工程档案]  
> 28. ID3D12Device::GetDeviceRemovedReason (d3d12.h) \- Win32 apps, [https\://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-getdeviceremovedreason](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-getdeviceremovedreason)  
> 29. The Visual C++ Linker best practices: Developer Iteration, [https\://devblogs.microsoft.com/cppblog/the-visual-c-linker-best-practices-developer-iteration/](https://devblogs.microsoft.com/cppblog/the-visual-c-linker-best-practices-developer-iteration/)  
> 30. MapLoader.java \- ghidra \- GitHub, [https\://github.com/NationalSecurityAgency/ghidra/blob/master/Ghidra/Features/Base/src/main/java/ghidra/app/util/opinion/MapLoader.java](https://github.com/NationalSecurityAgency/ghidra/blob/master/Ghidra/Features/Base/src/main/java/ghidra/app/util/opinion/MapLoader.java)  
> 31. 09\_CineRTX\_Phase11\_施工单\_Track\_11D\_DirectComposition零回读视口与统一NLE引擎及AVX2音频矩阵.md [受控工程档案]  
> 32. 10\_CineRTX\_Phase11\_施工单\_Track\_11E\_VSR\_FRUC显存感知自适应调度与SDK13.1分级接入.md [受控工程档案]  
> 33. 08\_CineRTX\_Phase11\_施工单\_Track\_11C\_Flicks语义重构与WASAPI实时播放内核.md [受控工程档案]  
> 34. NVDEC \- problems with seeking \- NVIDIA Developer Forums, [https\://forums.developer.nvidia.com/t/nvdec-problems-with-seeking/79988](https://forums.developer.nvidia.com/t/nvdec-problems-with-seeking/79988)  
> 35. Recovering from an Invalid-Device Error \- Win32 apps, [https\://learn.microsoft.com/en-us/windows/win32/coreaudio/recovering-from-an-invalid-device-error](https://learn.microsoft.com/en-us/windows/win32/coreaudio/recovering-from-an-invalid-device-error)  
> 36. NVIDIA Video Codec SDK 13.1: Zero-Copy Transcode, AV1 B, [https\://developer.nvidia.com/blog/nvidia-video-codec-sdk-13-1-zero-copy-transcode-av1-b-frames-and-frame-accurate-seek/](https://developer.nvidia.com/blog/nvidia-video-codec-sdk-13-1-zero-copy-transcode-av1-b-frames-and-frame-accurate-seek/)  
> 37. NVIDIA Video Codec SDK 13.1: Zero-Copy Transcode, AV1, [https\://daily.dev/posts/nvidia-video-codec-sdk-13-1-zero-copy-transcode-av1-b-frames-and-frame-accurate-seek-k2lcpkzvh](https://daily.dev/posts/nvidia-video-codec-sdk-13-1-zero-copy-transcode-av1-b-frames-and-frame-accurate-seek-k2lcpkzvh)  
> 38. CineRTX Phase 8 全量穿透验收、三端统一架构重塑与全仓文档漂移终审调研报告.md [受控工程档案]  
> 39. CineRTX 演进方向调研 [受控工程档案]