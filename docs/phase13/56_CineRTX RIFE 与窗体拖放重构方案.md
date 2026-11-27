# **CineRTX RIFE 链路重构与窗体拖放及参数同步全套技术方案**

针对 Commit a6451b7 基线暴露的 P0 级双故障（RIFE 插帧退化为纯 dup 复制帧并伴随全屏紫色闪烁，以及 54\_ 改动回滚后亟待重构的窗体拖放与 120-rife 参数快照冻结问题），领域专家组基于工作区既有取证给出如下顶层裁决：**第一，在转码管线中确立“默认先原生分辨率 RIFE 插帧、后 VSR 超分（FG-First）”的默认架构律，若保留“先 VSR 后 RIFE（VSR-First）”路径则必须为其配备独立 targetWidth() x targetHeight() 中间 NV12 表面对与入口几何硬断言，同时全面改造 rife\_pack.ptx 显式双平面寻址/双向饱和钳位及 rtx\_ngx\_bridge\_interpolate\_rife 专属非阻塞 CUDA 流与上下文压栈隔离，根除越界读取、UV 错位与默认流 0 异步竞态；第二，在 Win32 GUI 队列中实施“即拖即入队 \+ 同批/当前焦点 Pending 靶向联动 \+ 路径感知启动同步”状态机，彻底化解单/多文件拖放可见性、“先拖后切 120-rife”直觉与多任务异构参数隔离三者间的冲突。** 全套方案严格以 a6451b7 为唯一基线增量设计，严守 AuditTypes \== 34/35（零新增具名类型）与 AuditImports（GUI 静态导入物理 DLL \<= 4 / HEADLESS \== 2）等工程红线1。  
需要先行声明数据与源码边界：本报告的全部代码坐标与现象取证均源自工作区受控文档 55\_、51\_、01\_、58\_ 及 Phase 9 验收卷宗中所载代码行号与实机报告，并辅以硬件公开规格、NVIDIA 官方文档与标准色彩空间/内存偏移推演进行验证1；由于当前环境无法直接访问仓库内 rife\_pack.ptx、rife\_engine.cpp、hw\_transcode\_stages.cpp 与 rtx\_gui.cpp 的未摘录源码全文6，下文对底层核函数与控件事件链的改造均以“本地前置核验项 \+ 确定性接口契约”形式下发，施工前需在本地 a6451b7 逐行核实对应分支后增量合入。

## **一、 问题 A 根因穿透：纯 dup 与全屏紫色闪烁的物理机理**

### **1.1 初始化与推理几何分裂导致的越界与跨平面踩踏**

在 a6451b7 基线中，当 vsr\_before\_fg \== true（当前默认模式，例如 1080p \-\> 4K）时，src/core/hw\_transcode\_stages.cpp:L183-L188 按输出目标分辨率 targetWidth() x targetHeight()（即 3840x2160）初始化 RifeEngine；然而主循环 synthesizeAndRemasterWindow()（src/core/hw\_transcode\_stages.cpp:L417-L436）却把解码原分辨率的 prev\_f / curr\_f（1920x1080 NV12）直接喂入 interpolateRife()，且 UV 平面偏移按 pitch \* prev\_f.height 计算1。这一“初始化尺寸按 4K、实际输入按 1080p”的几何分裂，在 NV12 平面线性字节布局上造成双向灾难性错位。  
以 GPU 表面典型 512 字节步长对齐为例，一帧 1920x1080 NV12（pitch \= 2048）的亮度 Y 平面仅覆盖字节 0 .. 2,211,839（1080 行），色度交织 UV 平面覆盖字节 2,211,840 .. 3,317,759（540 行），总分配仅 3,317,760 字节（约 3.16 MiB）8。当按 3840x2160 调度的 RIFE 前处理在 pitch \= 2048 约束下循环读取时，水平方向从 x \= 2048 起每行折返踩入相邻后续行，垂直方向在 y \>= 1080 后将 1080p 的 UV 平面当作下半幅亮度读入，并在 y \>= 1620 越界侵入缓冲末尾之后（最高偏移达 4,425,471 字节，超出原生缓冲末端 1,107,711 字节，即约 1.33 倍越界）；其 UV 读取循环同样越界 1,107,711 字节8。反之，若输出缓冲或下游按不一致步长解释（例如 RIFE 输出按 3840x2160 写回而下游按 1080p 的 pitch \* 1080 截取 UV 起始位置，或 1080p 输出缓冲被部分覆写/未覆写），则会直接把亮度像素块当成色度对读取，或使输出 UV 平面残留全零及未初始化显存8。  
该几何踩踏并发出委托书实测的“纯 dup”与“全屏/大面积紫色闪烁”双症状1。一方面，越界读入的折返像素与脏显存破坏了帧间一致性，导致 8x8 分块 SAD 统计在 evalBlockSadSceneCut() 中产生剧烈畸变并持续误判为场景硬切，管线随即绕过插帧网络并退化为近端帧复制（纯 dup）1。另一方面，委托书取证指出未拦截时因越界与 UV 错位（含 U=0, V=0）直接引发紫色闪烁1；对照 BT.709 Limited Range 色彩变换数值行为可知其两重物理触发机制8：其一，当亮部 Y 平面（如 Y \= 160..235）因偏移或分辨率错配被下游误读为 (Cb, Cr) 色度对时，BT.709 矩阵直接输出高饱和洋红/紫色（RGB \= (0.74..1.00, 0.20..0.42, 0.78..1.00)），而暗部 Y 值（Y \< 100）则映射为偏绿，随画面内容波动呈剧烈紫绿闪烁8；其二，当越界全零显存（Y=0, U=0, V=0）或有效亮度配零色度（Y=128, U=0, V=0，标准有限范围下对应偏绿 (0.00, 0.89, 0.00)）未经 \[0, 1\] 钳位直接进入 BT.709 解码时，会产生极端负值（如 RGB \= (-0.973, \+0.301, \-1.133) 或 (-0.388, \+0.886, \-0.549)），不仅击穿对半精度极其敏感的 Practical-RIFE v4.25 光流网络9，若后处理或下游缺乏饱和写回保护，负值或 NaN 溢出回绕为高位 Cb/Cr \> 170 同样即刻爆发全屏紫红8。具体实机究竟是上述哪一条路径占主导，需按第 2.2 节清单在本地核查 rife\_pack.ptx 源码后最终定界。

### **1.2 CUDA 默认流退化、指针别名与分桶路径隐患**

除尺寸分裂外，已有文档取证揭示了另外三处必须同期封堵的系统性缺陷。首先，test\_rife.cpp 中 rife\_four\_phase\_monotonic\_progression 在同进程前序 GPU 测试后偶发 scene cut 误报与像素偏差，直接关联到 pack\_stream() 在无预览环时退化为默认流 0 与上下文切换竞态1。根据 NVIDIA CUDA 驱动与 TensorRT 10.x 官方文档语义，采用 CU\_STREAM\_NON\_BLOCKING / cudaStreamNonBlocking 创建的非阻塞流与传统默认流 0 之间**没有隐式同步屏障**11。当 pack\_stream() 退回流 0 而 TensorRT enqueueV3()、cuMemsetD32Async 或 NVDEC cuvidMapVideoFrame 运行于非阻塞流时，清零 d\_sad\_sum、前处理、推理与回读之间发生并发竞态，随机读取未就绪或残留高位垃圾的 SAD 值，从而复现类似 Phase 9 中由 d\_sad\_sum 污染引发的硬切误杀1；同时若前序测试切换过当前线程的 CUcontext，无上下文保护的桥接调用将直接受到污染。  
其次，Phase 9（RC-5）为降低 1:5 插帧预处理开销引入了基于设备指针的帧对缓存复用机制5。但若解码或中间表面池采用固定 2 槽或小容量环形槽位复用设备地址，相邻帧窗口在周期 2 后就会出现完全相同的 (PtrA, PtrB) 指针组合（如窗口 0-\>1 与窗口 2-\>3 均表现为 (PtrA, PtrB)）；若缓存键未携带单调帧序号，则会发生地址别名伪命中并输出旧帧张量8。  
最后，构建目录 models/ 下实际仅存在 rife\_v4.25\_{wide,ultra,square,tall,landscape,portrait}.engine 六个分桶引擎而无 rife\_v4.25.engine 本体1。尽管 rife\_engine.cpp:L233 会把 rife\_v4.25\* 路由至同目录 preferred\_name，但 param\_schema.cpp:L154-L174（resolveDefaultRifeEnginePath()）与 L271-L274（validateAndNormalizeProfile()）以及 hw\_transcode\_stages.cpp:L172-L227（initRemasterAndFgStage()）之间若存在字面路径存在性检查或错误吞没，便可能将用户选择的 RIFE 模式静默退化回 NVOFA 60fps2。

## **二、 RIFE 全链路重构方案（基于 a6451b7 增量施工）**

### **2.1 管线顺序裁决：确立 FG-First 为默认律与 VSR-First 显式中间契约**

在目标真机 RTX 5060 Laptop GPU（Blackwell GB206 架构，本地实测 sm\_120 显存 8,123 MiB、CUDA 核心 3,328 个、Tensor 核心 104 个；公开硬件规格库标注其 128-bit GDDR7 带宽为 384 GB/s 或 448 GB/s，显存一旦溢出至系统内存将导致帧时间剧烈抖动）上5，必须对 1080p \-\> 4K 放大场景下 FG-First 与 VSR-First 的物理代价做出定量评估。需要说明的是，工作区目前缺乏这两种顺序在 RTX 5060 上的实机对比基准数据1，下表将**精确可算的张量几何规模**（基于 NV12/FP16 格式字节数与 RIFE 对齐宽高）与**参数化显存/吞吐推演模型**严格区分呈现，真机绝对毫秒数仍须以落地后实测为准：

| 评估维度 | 方案 A：FG-First（先原生 1080p RIFE，后 VSR 4K，裁决为默认） | 方案 B1：VSR-First 全分辨率（先 VSR 4K，后 3840x2176 RIFE） | 方案 B2：VSR-First 流降采样（先 VSR 4K，后内部 scale=0.5 RIFE） |
| :---- | :---- | :---- | :---- |
| RIFE 输入输出张量（FP16 精确字节） | 1920x1152 约 42.2 MiB（7+3 通道）8 | 3840x2176 约 159.4 MiB（7+3 通道）8 | 仍需承接 4K 张量或额外降采样缓冲8 |
| RIFE 对齐后相对计算像素量 | 1.00x（2.21 Mpx 基准）8 | **3.78x**（8.36 Mpx，原始 4K 为 4.00x）8 | 约 3.00x 估算（光流降尺度但精炼仍触及高分辨）8 |
| 整条转码管线峰值显存（模型估算） | 约 2,420 MiB（8,123 MiB 总量下余量充足，模型估算）8 | 约 4,100 MiB（与 4K NVENC lookahead/DWM 叠加极易触顶，模型估算）8 | 约 3,650 MiB（模型估算）8 |
| 每秒算子调用频次（24fps \-\> 120fps） | RIFE 96 次 @1080p \+ VSR 120 次 @4K8 | VSR 24 次 @4K \+ RIFE 96 次 @4K8 | VSR 24 次 @4K \+ RIFE 96 次 @4K8 |
| 算力开销拐点推演（仅作量级比较） | 若设 RIFE@1080p=5 ms、RIFE@4K=19 ms、VSR=4–8 ms（Phase 11.4 曾录得正常单 Profile 1080p enqueueV3 \~8 ms4），总算时 960–1,440 ms/s（约 83–125 out fps）8 | 仅当 VSR(1080p-\>4K) 单帧耗时高于 RIFE(4K) \- RIFE(1080p)（如 \> 14 ms）时总算时才可能反超方案 A，否则 1,920–2,016 ms/s（约 60–62.5 out fps）8 | 介于方案 A 与 B1 之间，但需牺牲高分辨精细边缘光流精度 |
| 空间画质与分辨率包络 | VSR 压尾重绘并修复 RIFE 双线性变形轻微模糊；即使目标超分至 \> 4K，RIFE 仍稳定落入原生分辨率分桶包络内3 | RIFE 反向 grid\_sample 柔化 VSR 锐利纹理；若目标分辨率超出分桶 MAX（如 \> 3840x2176）即触发拒止报错3 | 光流下采样对细小快速物体易产生运动伪影；同样受限于 4K 级缓冲内存 |

据此，专家组对管线几何契约做出强制性双向裁决。在默认模式下（尤其是包含空间放大 \`target\_w \> src\_w \|  
\| target\_h \> src\_h 时），\*\*RIFE 必须锁定在解码原生分辨率（info.width x info.height）执行插帧，再由 VSR 逐帧超分并接续 TrueHDR / 3D LUT\*\*。在 src/core/hw\_transcode\_stages.cpp中必须彻底废弃initRifeEx取targetWidth()而推理传prev\_f/curr\_f的错配代码：默认FG-First下两者 100% 绑定解码输入尺寸(src\_w, src\_h)及解码表面真实pitch\`。  
若为兼容 Phase 13 后续可控自定义 DAG 顺序而保留 vsr\_before\_fg \== true（VSR-First）路径3，则绝不允许直接向 interpolateRife() 传入未放大的 prev\_f/curr\_f。RemasterPipeline 必须在此模式下严格启用专用中间缓冲对（在现有管线私有实现体内直接扩展固定 2 槽设备指针、尺寸及步长字段，不新增具名结构体）：每当解码出新的源帧，先执行**单帧级 VSR 超分**（注意每个源帧仅做 1 次 VSR，而非每个插值时间步重复执行），并按 VSR 输出表面格式规范落盘为标准的 target\_w x target\_h NV12（或由显式 GPU 格式转换核从 RGB/RGBA 转为标准 NV12），记录其真实 vsr\_pitch 与 vsr\_uv\_offset \= vsr\_pitch \* target\_h；随后 initRifeEx(target\_w, target\_h, ...) 与 interpolateRife(vsr\_prev, vsr\_curr, vsr\_out) 必须共享完全一致的 (target\_w, target\_h, vsr\_pitch) 契约。若目标尺寸超出对应 RIFE 分桶引擎的 MAX 上限，应在初始化期直接返回明确的分辨率超限错误，禁止无视超限继续构建。此外，在 rtx\_ngx\_bridge\_interpolate\_rife 入口必须增设零容忍参数校验：若 prev、curr、out 三者任一宽高不等于引擎初始化绑定尺寸，或 pitch \< width、显式 uv\_ptr \== y\_ptr 或偏移越界，立刻返回 RTX\_ERR\_INVALID\_PARAM 并递增 fruc\_failures，绝不允许发射越界 GPU 核函数。

### **2.2 rife\_pack.ptx 核函数与张量接口核查及升级规范**

针对无法在只读视图直接查阅 rife\_pack.ptx 全文的现实约束6，施工前必须在本地对该 PTX 文件及 rife\_engine.cpp 的调用传参逐项执行核查，并按以下强制升级契约一次性加固（保持全十六进制 IEEE-754 浮点立即数规范以防 sm\_70+ JIT 截断5）：

| 核查域 | 本地须核验的隐患点 | 强制升级契约（PTX 核函数 \+ C++ 调用侧同步修改） |
| :---- | :---- | :---- |
| **NV12 Y/UV 双指针与双 pitch** | 原调用仅传基址与宽高，在核内以 pitch \* assumed\_h 推算 UV 起始地址，任意尺寸或对齐差异都会诱发 Y/UV 跨平面读取错位1 | 废除核内隐式偏移计算；前/后处理核函数显式接收 y\_ptr, uv\_ptr, y\_pitch, uv\_pitch, src\_w, src\_h, pad\_w, pad\_h。Y 平面按 y \* y\_pitch \+ x 寻址，UV 平面按 y\_uv \= y \>\> 1、x\_uv \= x & \~1u 寻址 uv\_ptr \+ y\_uv \* uv\_pitch \+ x\_uv |
| **有效边界与对齐填充隔离** | RIFE v4.25 要求 64 像素倍数对齐20，分桶推理内部进一步按 align\_mod \= 128 填充3；若核函数越过 src\_w, src\_h 直接读取至 pad\_w, pad\_h，会读入行尾/末尾脏显存 | 采样源图像时严格以 src\_w, src\_h 为合法边界，对 \[src\_w, pad\_w) 与 \[src\_h, pad\_h) 的填充坐标先执行镜像反射（reflect101）或 clamp(coord, 0, src \- 1\) 再访存；后处理写出时严格拦截 \`x \>= src\_w |
| y \>= src\_h\`，绝不写穿源缓冲有效区域 |  |  |
| **色彩标准与动态范围（Full/Limited）** | 若长期硬编码 BT.709 Limited Range 常量（Phase 9 卷宗曾记录 0f3B95A106 与 0f3BE666665），输入 BT.709 Full Range 或 BT.601 素材会产生黑位压缩或色度偏差 | 新增 color\_flags 参数支持 BT.709 Limited、BT.709 Full 与 BT.601 Limited；按 IEEE-754 单精度精确重写 BT.709 Limited 核心常量（1/219 \= 0f3B95A025、1.5748/224 \= 0f3BE65EE9、-0.187324/224 \= 0fBA5B38FC、-0.468124/224 \= 0fBB08F5AF、1.8556/224 \= 0f3C07B951）8 |
| **双向饱和防紫闪保护** | U=0, V=0 或越界样本生成负 RGB 送入 IFNet 易破坏光流；后处理若对负 float 或 NaN 直接转无符号 u8 而缺饱和钳位，会发生整型下溢回绕并爆出全屏紫红8 | **双端硬钳位**：1) 前处理 RGB 输出前强制钳位 max(0.0, min(1.0, c))；2) 后处理读取张量后先过滤 NaN/Inf 归零并钳位到 \[0.0, 1.0\]，转 YUV 后将 Y 夹取 \[16, 235\]（Limited）或 \[0, 255\]（Full）、将 Cb/Cr 夹取 \[16, 240\]（Limited）或 \[0, 255\]（Full），再配合整数安全夹取与饱和打包写回 u8 |
| **2x2 色度下采样写出** | 仅采左上角单像素算 (Cb, Cr) 易在高对比边缘产生彩边；多块写同一 UV 字节存在数据竞争 | 后处理核函数由 2x2 像素宏块负责人线程统一聚合并求 2x2 合法像素（越界像素夹取至 src\_w-1, src\_h-1）的平均 RGB，再计算并单次写出唯一的 (U, V) 字节对 |
| **张量精度契约匹配** | 分桶引擎在 TensorRT 下可能绑定 kHALF（FP16）或 kFLOAT（FP32），而核函数步长固定按单种类型读写即会引发 2 倍内存偏移 | RifeEngine::init 加载引擎后必须通过 engine-\>getTensorDataType() 动态核对输入/输出张量实际格式21，rife\_pack.ptx 分别提供 FP16 与 FP32 入口，并在桥接层做严格配对校验，且保留 rife\_builder.cpp 中 GridSample 层锁定 FP32 的稳定性规则5 |

### **2.3 六分桶引擎解析路由、禁静默回落与防别名帧对缓存**

为彻底根除因 models/rife\_v4.25.engine 本体缺失引起的路径误判与静默回落1，并严格遵守 \[DEC-011\] 单引擎单 Profile 原则（杜绝 Phase 11.4 双 Profile 切换导致的算子退化与显存暴涨）3，须在参数校验与引擎加载层固化三项契约。第一，在 src/core/param\_schema.cpp（resolveDefaultRifeEnginePath() 与 validateAndNormalizeProfile()）中，当 rife\_engine\_path 为默认路径 models/rife\_v4.25.engine 或用户未指定时，不得以该字面文件不存在而判为无效，而应执行“分桶族就绪验证”——即检测目标目录下是否存在 rife\_v4.25.engine 或同族 rife\_v4.25\_{wide,ultra,square,tall,landscape,portrait}.engine 集合1。进入 RifeEngine 初始化后，保持 128 像素模对齐（确保后续多层金字塔下采样仍满足 Practical-RIFE 要求的 64 倍数对齐3），并依据输入有效宽高比与 rife\_engine.cpp 既有的 6 分桶命名及各自 Profile 的 MIN/OPT/MAX 边界精确匹配最优分桶，消除 16:9 锚定限制1。  
第二，严格封堵静默退化旁路：当 profile.fps\_mode 为 60-rife 或 120-rife 时，从 validateAndNormalizeProfile() 到 RemasterPipeline::initRemasterAndFgStage() 全链路必须强锁 rife-trt 后端2；一旦对应分桶引擎文件缺失、加载失败或超限，必须立即中止并返回显式错误码，严禁在无明确用户干预下退化为 NVOFA 60fps2。  
第三，升级帧对缓存键消除指针别名：在维持 Phase 9 帧对预处理缓存以保障高复用率的基础上5，将缓存键从单一 (prev\_ptr, curr\_ptr) 扩展为 (prev\_seq\_id, curr\_seq\_id, prev\_ptr, curr\_ptr, src\_w, src\_h, y\_pitch, color\_flags)。调用侧每解码或超分产出一帧即赋予全局单调递增 uint64\_t seq\_id，在执行 Seek、Reset 或管线重建时递增基准代次并清空缓存，彻底消除 2 槽或固定环形表面池下的指针别名伪命中8。

### **2.4 rtx\_ngx\_bridge\_interpolate\_rife CUDA 流与上下文隔离**

针对无预览环时 pack\_stream() 退化为默认流 0 引发的异步竞态与同进程单测互扰1，桥接层必须全面执行非阻塞流与显式上下文隔离。首先，在整条 RIFE 桥接路径中彻底禁用默认流 0（nullptr）：RifeEngine 初始化时在自身绑定的 CUcontext 下创建一条专属的非阻塞流 rife\_stream\_（CU\_STREAM\_NON\_BLOCKING）以及两个禁用计时的 CUevent（ev\_in\_ready\_、ev\_out\_done\_）13。其次，建立严密的跨流事件握手协议：当调用方（RemasterPipeline 或单测）传入非空 caller\_stream 时，可统一把所有核函数排入 caller\_stream，或采用标准两段式跨流事件屏障（在 caller\_stream 调用 cuEventRecord(ev\_in\_ready\_, caller\_stream) 后由 rife\_stream\_ 调用 cuStreamWaitEvent，在全部 rife\_pack.ptx 核函数、64-bit d\_sad\_sum 清零、enqueueV3 推理及回读结束后于 rife\_stream\_ 调用 cuEventRecord(ev\_out\_done\_, rife\_stream\_)，再由 caller\_stream 等待 ev\_out\_done\_）21；而当无预览环或无外部流传入（caller\_stream \== nullptr）时，桥接层必须**强制改用内部专属 rife\_stream\_**（绝不回落至流 0），并在返回宿主可控表面或同步结果前对 rife\_stream\_ 执行事件/流同步。  
同时，在所有桥接入口（initRife / initRifeEx、interpolateRife、destroyRife）外围统一包装作用域上下文压栈辅助函数 runInRifeContext(ctx, fn)（内部配对执行 cuCtxPushCurrent 与 cuCtxPopCurrent22，采用自由函数与 lambda 而非新增具名结构体，以此严守 AuditTypes 红线），确保即便同进程前序 GPU 测试修改了当前线程 CUDA 上下文，RIFE 仍稳定运行于自身专属上下文中。最后，应在本地一并核查 d\_sad\_sum 在 a6451b7 中是否完整保有 Phase 9 确立的 8 字节分配、cuMemsetD32Async(..., 0, 2, active\_stream) 双字清零与 uint64\_t 读取契约5，并将 evalBlockSadSceneCut() 的 EMA 阈值状态严格保存在 RifeEngine 实例内部成员中，严禁任何全局或 static 跨实例残留状态3。

## **三、 GUI 拖放与参数同步统一交互方案（解决 WM\_DROPFILES \+ IDC\_CMB\_FPS）**

### **3.1 冲突根因与统一状态机设计**

在 a6451b7 中，拖入文件（WM\_DROPFILES，src/gui/rtx\_gui.cpp:L676-L695）或点击 \+（onQueueAdd()，L530-L546）会以当时默认参数（"60-nvofa"）立即创建 JobStatus::Pending 任务，随后用户切换 IDC\_CMB\_FPS 为 120-rife 并点击 Start（onStartTranscode()，L358-L398）时，系统虽调用 collectProfileFromUi(hwnd) 更新了 guiProfile(app)，却因 hasPending \== true 直接跳过且不更新队列，导致仍按冻结的 "60-nvofa" 执行2。此前 54\_ 尝试取消单文件入队和队列列表刷新，则直接破坏了“拖入立即可见”的反馈契约1；而粗暴遍历覆写全部 Pending 项又会破坏多任务异构参数隔离3。  
为此，专家组裁决采用“即拖即入队 \+ 靶向同步组 active\_sync\_ids \+ 路径感知启动”统一模型。该模型无需新增任何具名结构体，只需在 app 所属既定状态结构体中追加已选中任务 ID 与一个 std::vector\<uint64\_t\>（或等价内置容器）记录当前随面板实时联动的 Pending 任务 ID 集合即可。不同交互路径的完整流转规则如下表所示：

| 交互场景与用户动作 | 队列数据与 active\_sync\_ids 变更 | 检查器（UI）与反馈行为 | 对其余已有 Pending 任务的隔离保证 |
| :---- | :---- | :---- | :---- |
| **1\. 单文件拖入 Encoder 窗体（WM\_DROPFILES）** | 探测媒体，按当前 collectProfileFromUi **立即入队**为 Pending 任务 J\_k；设 selected\_job\_id \= J\_k、active\_sync\_ids \= {J\_k} | 刷新队列列表并高亮选中 J\_k；把路径写入 IDC\_EDT\_INPUT 并执行非破坏性 applyProbedMedia；状态栏提示已入队1 | **零影响**：此前已存在的队列项快照原封不动 |
| **2\. 多文件批量拖入 Encoder 窗体** | 全部合法媒体文件逐一探测并立即入队为 Pending 任务组 {J\_1..J\_m}；设 active\_sync\_ids \= {J\_1..J\_m}，焦点选中首项 J\_1 | 队列列表立即显示全部 m 个待办项；IDC\_EDT\_INPUT 显示焦点任务路径；支持拖入后立刻统一改选 120-rife 再点 Start | **零影响**：仅本次同批拖入尚未手动拆分编辑的任务组受同步控制 |
| **3\. 拖入后切换 IDC\_CMB\_FPS（如 120-rife）或调节任意编码参数** | CBN\_SELCHANGE 等控件通知更新 guiProfile(app)，并遍历 active\_sync\_ids 对其中仍处于 JobStatus::Pending 的任务调用 updatePendingJob(id, current\_profile) | 实时刷新队列列表对应行摘要（如明确标出 120-rife），给予用户“参数已绑定待转码项”的即时视觉确认 | **严格隔离**：不在 active\_sync\_ids 内的既有任务及已运行/完成任务绝不被改写 |
| **4\. 用户在队列列表中主动选中某任务 J\_i** | 置 selected\_job\_id \= J\_i 并收窄 active\_sync\_ids \= {J\_i}（单选绑定）；如 J\_i 非 Pending 则清空 active\_sync\_ids | 将 J\_i 独立的 TranscodeProfile 与输入路径回填至检查器及 IDC\_EDT\_INPUT，供单独查看或修改（Pending 态） | **完全独立**：所有面板修改仅作用域至 J\_i，彻底保障多任务异构参数隔离3 |
| **5\. 点击 \+（onQueueAdd）主动追加任务** | 按当前 guiProfile(app) 新建独立 Pending 任务 J\_new；重置 selected\_job\_id \= J\_new 与 active\_sync\_ids \= {J\_new} | 列表新增并选中 J\_new，表明当前面板已绑定至该新任务 | **零影响**：不影响历史任何队列项 |
| **6\. 浏览替换 IDC\_EDT\_INPUT 后点击 Start Transcode** | onStartTranscode() 先执行 collectProfileFromUi；若 selected\_job\_id 对应 Pending 任务且其输入路径与 IDC\_EDT\_INPUT 一致，再次同步一次该任务快照；若 IDC\_EDT\_INPUT 指向一个**未在 Pending 中出现的新文件路径**（例如通过浏览按钮选入而未按 \+），则在启动前自动将其按 current\_profile 补入队列后再调用 startWorker() | 启动转码并在状态栏显示真实激活的帧率模式与 RIFE 后端，杜绝旧快照误跑或新选路径被忽略 | **零误伤**：当路径不一致时不会用新路径覆盖旧队列项的输入文件 |
| **7\. 非 Encoder 工作区（如 Player）拖放** | 直接交由播放器加载媒体入口，不污染 Encoder 任务队列3 | 视口与控制条刷新为新加载媒体 | **零影响** |

### **3.2 Win32 控件与 IAT 合规防踩坑细则**

在将上述交互状态机合入 src/gui/rtx\_gui.cpp 与 src/gui/workstation\_canvas.cpp 时，必须同步处理四项 Win32 底层工程约束。第一，严禁静态引入 shell32.dll：WM\_DROPFILES 所依赖的 DragAcceptFiles、DragQueryFileW 与 DragFinish 均位于 shell32.dll 中23，而该 DLL 位列 cmake/AuditImports.cmake 的 10 项静态黑名单之中3。所有相关符号在 a6451b7 基础上必须坚持通过既有 sys\_dynload 动态加载调用，并在每次 WM\_DROPFILES 处理出口（含异常路径）确保执行 DragFinish(hDrop) 释放句柄。  
第二，堵死 CB\_SETCURSEL 不触发 CBN\_SELCHANGE 导致的状态失步24：Win32 标准规范明确规定程序调用 CB\_SETCURSEL 选中下拉项时不会向父窗口发送 CBN\_SELCHANGE 通知25。因此在 populateComboBoxes()（含语言切换重建下拉条目）或队列行切换回填控件时，必须依据当前 profile.fps\_mode 在 kFpsModes（original, 60-nvofa, 120-nvofa, 60-rife, 120-rife）中精确查出索引 0..42，发送 CB\_SETCURSEL 后校对 CB\_GETCURSEL 返回值并同步刷新内部状态，严禁语言刷新或控件重填时意外把选项复位回索引 1（60-nvofa）。  
第三，确立 applyProbedMedia() 非破坏性契约：媒体探测回填函数仅可更新源媒体只读元信息（源宽高、源帧率、时长、音轨特征）及未手工改写时的默认 .mp4 输出文件名，**严禁覆盖用户已经切换的 fps\_mode、编码器、VSR 或画质设置**。第四，在 onStartTranscode() 启动分发前，若检测到所选任务要求 60-rife/120-rife，必须核验 validateAndNormalizeProfile() 返回的 target\_fps 确实为 {60, 1} 或 {120, 1} 且 RIFE 族路径有效，绝不在错误状态下启动或偷偷改派给 NVOFA2。

## **四、 强制端到端真机可验证单测与工程红线审计**

### **4.1 RIFE 真机端到端与 GUI 同步必测用例矩阵**

既有单测仅覆盖 256x256 合成交叉淡入或结构字段映射，未能覆盖真实 rife\_v4.25\_\*.engine 端到端全链路，这是本次 P0 故障逃逸的核心盲区1。施工完成后，必须在真机测试套件中补齐并全绿通过以下 4 项硬性验收用例：

| 测试用例标识 | 运行载体与测试构造 | 强制量化通过标准（缺一不可） |
| :---- | :---- | :---- |
| **rife\_real\_engine\_1080p\_fg\_first\_to\_4k\_120fps** | 构造含明显线性平移高对比色块与大面积中性灰背景的 1920x1080 NV12/24fps 源序列，经真实 RemasterPipeline、models/rife\_v4.25\_\*.engine 与 VSR 执行默认 fg-first 输出 3840x2160 120fps .mp4（可并行追加非 16:9 如 1440x1080 验证分桶路由） | 1\) 封装帧率 r\_frame\_rate \== {120, 1}，输出总帧数符合 24 \-\> 120fps 扩帧规律，且 fruc\_failures \== 0、fruc\_scene\_cuts \== 05； 2\) **真实运动插值（非 dup）**：任意相邻输出帧间 L1/MAE \> 0.5，同一 5 帧插帧窗内前景运动质心随时间步呈单调推进，且中间帧对前后原帧的 MAE 均显著低于前后原帧间的原始 MAE； 3\) **NV12 UV 合法性（零紫绿闪）**：对解码回读的每一帧抽检中性灰区，满足 \` |
| **rife\_no\_preview\_ring\_stream\_isolation\_soak** | 同进程内先运行其他 GPU/D3D12/VSR 测试切换环境，随即在无预览环（caller\_stream \== nullptr 且无共享纹理）下连续跑 20 轮 initRifeEx \+ 4 相位插帧（0.2/0.4/0.6/0.8）与 RemasterPipeline 2 槽表面地址循环复用测试 | 1\) 全程 scene\_cut \== 0 且 4 个中间相位两两及与首尾帧严格互异5； 2\) 复用同一对设备指针但传入递增 frame\_seq\_id 与不同图像内容时，必须生成对应新内容插帧（验证无地址别名伪缓存命中）； 3\) 不出现默认流 0 异步竞态偏差 |
| **rife\_geometry\_mismatch\_fail\_fast\_guard** | 刻意在 initRifeEx(3840, 2160\) 后向 interpolateRife() 传入 1920x1080 缓冲或非法 pitch / uv\_offset | 桥接层在核函数调度前立即返回 RTX\_ERR\_INVALID\_PARAM，零越界访存，不崩溃且不静默回退 dup |
| **gui\_drop\_then\_switch\_120rife\_queue\_isolation** | 消息循环级单测：模拟 WM\_DROPFILES 拖入 clipA.mp4 \-\> 断言队列立即呈现 1 个 Pending 项与 IDC\_EDT\_INPUT \-\> 模拟切换 IDC\_CMB\_FPS 至索引 4（120-rife）触发 CBN\_SELCHANGE \-\> 点击 \+ 入队 clipB.mp4（保持 60-nvofa） \-\> 再选回 clipA 验证 | 1\) clipA 入队立即可见且随切换联动刷新，最终下发 target\_fps \== {120, 1}、\!rife\_engine\_path.empty() 且不回落至 NVOFA2； 2\) clipB 在队列内独立保持 60-nvofa 参数不受影响； 3\) 静态导入表无 shell32.dll |

### **4.2 工程红线守门与增量施工纪律**

整套方案在设计时已针对项目四大静态门禁完成合规收口1。在具名类型控制上，全仓要求保持 AuditTypes \== 34/35（零新增具名类型）2：RIFE 专属流/事件/色彩标志/缓存序号键全部作为原始成员或 std::tuple/原生类型直接写入既有的 RifeEngine::Impl 内部实现，上下文守卫使用自由函数 runInRifeContext 配合 lambda 执行以避免在任何 .h/.cpp 中引入新的 struct/class/enum 声明；VsrFramePairBuffer 与 active\_sync\_ids 亦直接以原始字段/std::vector\<uint64\_t\> 挂载于既有的 RemasterPipeline 实现与 app 所属状态体内。  
在静态导入表与合规性方面，WM\_DROPFILES 维持 sys\_dynload 动态加载 shell32.dll，CUDA Driver API 与 TensorRT 继续完全隔离于 rtx\_ngx\_bridge.dll 动态边界之后，保证 AuditImports 严格锁定 GUI \<= 4 与 HEADLESS \== 23。所有增量文件与测试代码须自带标准 SPDX 许可声明头且绝对禁止写入任何本地绝对路径、盘符、主机名或个人账号标识，确保 AuditPrivacy \= 0 与 AuditSpdx \= PASS1。所有施工严格在基线 a6451b7 上以小步差异补丁推进，严禁搬运旧分支文件整文件覆盖1。

## **五、 增量施工执行顺序清单**

> 1. **基线源码前置核验（本地 a6451b7 只读比对）**：核查 rife\_pack.ptx 现有签名与钳位指令、确认 rife\_engine.cpp 中 6 分桶匹配阈值与 d\_sad\_sum 8 字节读写契约、核对 hw\_transcode\_stages.cpp:L172-L227, L417-L436 与 rtx\_gui.cpp 拖放/列表控件消息编号。  
> 2. **RIFE 几何统一、PTX 加固与流隔离施工**：锁定默认 FG-First 原生分辨率插帧；如保留 VSR-First 则落盘独立 target\_w x target\_h NV12 中间表面对与单帧 VSR 调用；在桥接层加入入口几何硬断言、专属 CU\_STREAM\_NON\_BLOCKING 流 \+ 事件握手、runInRifeContext 压栈保护及复合缓存键；同步升级 rife\_pack.ptx 显式双指针/双步长、有效边界反射与双向 \[0, 1\]/饱和防紫闪保护。  
> 3. **分桶路径校验与防静默降级施工**：更新 param\_schema.cpp 对 models/rife\_v4.25\_\*.engine 分桶族的就绪校验逻辑，在 hw\_transcode\_stages.cpp 锁定 60-rife/120-rife 失败即报错阻断的不可回落语义。  
> 4. **GUI 拖放与队列参数同步重构**：在 rtx\_gui.cpp 与 workstation\_canvas.cpp 落地 WM\_DROPFILES 立即入队 \+ active\_sync\_ids 靶向联动 \+ CBN\_SELCHANGE 实时刷新队列文本 \+ populateComboBoxes() 显式索引校验 \+ applyProbedMedia() 非破坏性回填 \+ 路径感知 onStartTranscode()。  
> 5. **真机端到端穿透与静态门禁封板验证**：在 RTX 5060 真机运行既有单测全集及第 4.1 节新增的 4 项端到端验证用例，采集输出帧率、非 dup L1/MAE 与 NV12 UV 合法性实测日志，并确认 AuditTypes \== 34/35、AuditImports（GUI \<= 4 / HEADLESS \== 2）、AuditPrivacy \= 0 与 AuditSpdx \= PASS 全绿通过。

#### **引用的著作**

> 1. 55\_CineRTX RIFE 链路报废与窗体拖放失效根因排查及新方案委托书.md, [[REDACTED_ID]]([REDACTED_ID])  
> 2. 51\_CineRTX RIFE 120fps 选项失效与回落NVOFA 60fps 专家排障委托书.md, [[REDACTED_ID]]([REDACTED_ID])  
> 3. 01\_CineRTX Phase 13 顶层架构重塑与全栈能力演进领域专家咨询委托书.md, [[REDACTED_ID]]([REDACTED_ID])  
> 4. 58\_CineRTX Phase 11.4 RIFE双Profile重构性能崩塌与转码速率低于1fps法医排查专家咨询委托书.md, [[REDACTED_ID]]([REDACTED_ID])  
> 5. CineRTX\_Phase9\_RIFE120fps全链路根治终审验收与战略封板专家委托书.md, [[REDACTED_ID]]([REDACTED_ID])  
> 6. [[REDACTED_ID]]([REDACTED_ID])  
> 7. workspace\_resource://CksiRwohMU1Kcjd1RGY2RGxZR2JtbXJxalpzc3BjdlBQajE1ZGZzGiJhcHBsaWNhdGlvbi92bmQuZ29vZ2xlLWFwcHMuZm9sZGVyMAM  
> 8. [unknown\_url]([REDACTED_ID])  
> 9. mlx-community/RIFE-4.25 \- Hugging Face, [https\://huggingface.co/mlx-community/RIFE-4.25](https://huggingface.co/mlx-community/RIFE-4.25)  
> 10. 1\. Introduction — PTX ISA 9.5 documentation, [https\://docs.nvidia.com/cuda/parallel-thread-execution/index.html](https://docs.nvidia.com/cuda/parallel-thread-execution/index.html)  
> 11. Optimizing TensorRT Performance \- NVIDIA Documentation Hub, [https\://docs.nvidia.com/deeplearning/tensorrt/11.5.0/performance/optimization.html](https://docs.nvidia.com/deeplearning/tensorrt/11.5.0/performance/optimization.html)  
> 12. Best Practices — NVIDIA TensorRT 10.10.10 Developer Guide for, [https\://developer.nvidia.com/docs/drive/drive-os/7.0.3/public/drive-os-tensorrt-developer-guide/best-practices.html](https://developer.nvidia.com/docs/drive/drive-os/7.0.3/public/drive-os-tensorrt-developer-guide/best-practices.html)  
> 13. CUDA Driver API supported by HIP \- AMD ROCm Documentation, [https\://rocm.docs.amd.com/projects/HIPIFY/en/amd-staging/reference/tables/CUDA\_Driver\_API\_functions\_supported\_by\_HIP.html](https://rocm.docs.amd.com/projects/HIPIFY/en/amd-staging/reference/tables/CUDA_Driver_API_functions_supported_by_HIP.html)  
> 14. How can i use custream in cuvidMapVideoFrame, [https\://forums.developer.nvidia.com/t/how-can-i-use-custream-in-cuvidmapvideoframe/183462](https://forums.developer.nvidia.com/t/how-can-i-use-custream-in-cuvidmapvideoframe/183462)  
> 15. Nvidia GeForce RTX 5060 Laptop \- Benchmarks and Specs, [https\://www\.notebookcheck.net/Nvidia-GeForce-RTX-5060-Laptop-Benchmarks-and-Specs.934941.0.html](https://www.notebookcheck.net/Nvidia-GeForce-RTX-5060-Laptop-Benchmarks-and-Specs.934941.0.html)  
> 16. NVIDIA GeForce RTX 5060 Laptop GPU · Feex Wiki, [https\://feex.it/wiki/hardware/gpu/nvidia-geforce-rtx-5060-laptop-gpu/](https://feex.it/wiki/hardware/gpu/nvidia-geforce-rtx-5060-laptop-gpu/)  
> 17. NVIDIA GeForce RTX 5060 Laptop GPU \- VideoCardz.net, [https\://videocardz.net/nvidia-geforce-rtx-5060-laptop-gpu](https://videocardz.net/nvidia-geforce-rtx-5060-laptop-gpu)  
> 18. Nvidia GeForce RTX 5060 Laptop: performance tests and specs \| NR, [https\://nanoreview.net/en/gpu/geforce-rtx-5060-laptop](https://nanoreview.net/en/gpu/geforce-rtx-5060-laptop)  
> 19. RTX 5070 Laptop vs 5060, 5070 Ti, 5080: Which Tier? \[2026\], [https\://shattered.io/rtx-5070-laptop-vs-5060-5070-ti-5080-8gb-vs-12gb-2026/](https://shattered.io/rtx-5070-laptop-vs-5060-5070-ti-5080-8gb-vs-12gb-2026/)  
> 20. RIFE — PotionUI documentation, [https\://potionui.com/docs/models/rife](https://potionui.com/docs/models/rife)  
> 21. Quick Start Guide — NVIDIA TensorRT, [https\://docs.nvidia.com/deeplearning/tensorrt/10.x.x/getting-started/quick-start-guide.html](https://docs.nvidia.com/deeplearning/tensorrt/10.x.x/getting-started/quick-start-guide.html)  
> 22. include/cuda/cuda.h \- gcc \- Git at Google, [https\://gnu.googlesource.com/gcc/+/refs/heads/devel/m2pragma/include/cuda/cuda.h](https://gnu.googlesource.com/gcc/+/refs/heads/devel/m2pragma/include/cuda/cuda.h)  
> 23. QUERY\_FILE\_LAYOUT\_INCLU, [https\://docs.rs/xege-ffi/latest/xege\_ffi/constant.QUERY\_FILE\_LAYOUT\_INCLUDE\_STREAM\_INFORMATION\_FOR\_REPARSE\_ATTRIBUTE.html](https://docs.rs/xege-ffi/latest/xege_ffi/constant.QUERY_FILE_LAYOUT_INCLUDE_STREAM_INFORMATION_FOR_REPARSE_ATTRIBUTE.html)  
> 24. How to send a CBN\_SELCHANGE message when using, [https\://stackoverflow.com/questions/1468165/how-to-send-a-cbn-selchange-message-when-using-cb-setcursel](https://stackoverflow.com/questions/1468165/how-to-send-a-cbn-selchange-message-when-using-cb-setcursel)  
> 25. CBN\_SELCHANGE notification code \- Win32 apps \- Microsoft Learn, [https\://learn.microsoft.com/en-us/windows/win32/controls/cbn-selchange](https://learn.microsoft.com/en-us/windows/win32/controls/cbn-selchange)  
> 26. c++ \- The CBN\_SELCHANGE notification not works when i use the, [https\://stackoverflow.com/questions/37907524/the-cbn-selchange-notification-not-works-when-i-use-the-function-combobox-setcur](https://stackoverflow.com/questions/37907524/the-cbn-selchange-notification-not-works-when-i-use-the-function-combobox-setcur)