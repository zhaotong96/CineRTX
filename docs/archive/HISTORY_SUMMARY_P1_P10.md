# CineRTX Phase 1–10 权威演进摘要（单页归档）

> **档案编号**：`ARCHIVE-HISTORY-SUMMARY-P1-P10-001`
> **编撰日期**：2026-11-19（Phase 11 · Track 11A 基线元数据收口）
> **档案定位**：Phase 1–10 核心里程碑、存活架构遗产与反模式黑名单的**单页权威摘要**。逐阶段四维细节（目标 / 存活遗产 / 废除清单 / 踩坑）见 [`Phase1_to_Phase7_Evolution_Digest.md`](Phase1_to_Phase7_Evolution_Digest.md)；Phase 8/9/10 原始终审卷宗见 `docs/phase8/`、`docs/phase9/`、`docs/phase10/`。
> **现行契约**：一律以根目录 `README.md` / `README_zh.md` 与 `docs/spec/` 为准。本档案为历史记录，不构成现行契约。
> **脱敏声明**：不含私有网络地址、主机名或本机绝对路径；验收主机以 `[REDACTED_HOST]` 指代。

---

## 一、 里程碑总表

| 阶段 | 版本 | 终审日期 | 核心里程碑 | 单测基线 |
| :--- | :--- | :--- | :--- | :--- |
| P1 | — | 2026-10-21 | NVDEC→显存→NVENC 零拷贝裸转码闭环；`D2H=0 / H2D=0` | 101 |
| P2 | — | 2026-10-25 | D3D12↔CUDA 外部显存互操作 + NGX VSR/TrueHDR + P010 打包；1080p→4K HDR10 **128.7 FPS** | — |
| P3 | v0.2.0 | 2026-11-01 | FFmpeg 全量静态内联、单 EXE 绝对自包含；Flicks 全局时基立项 | 137 |
| P4 | — | 2026-10-31 | Modern C++26 全栈重构、`Result<T>` 类型契约、全仓反 Slop | — |
| P5 | — | 2026-11-08 | 音轨零拷贝透传、竖屏智能几何、SDR/HDR 双模合规 | 93 |
| P6 | — | 2026-11-10 | 49,042 行 → <15,000 行瘦身；AV1 10-bit 默认；原生参数矩阵 UI；绿色包 34.35 MB | 79 |
| P7 | v0.2.1 / v0.3.0 / v0.4.0 | 2026-11-11 | 3D LUT 融合核函数、HDR10+、FLIP 预览视口、多 GPU GOP 分片、TDR 自愈 | 91 |
| P8 | — | 2026-11-14 | 双路真补帧（NVOFA + TensorRT RIFE）落地；三端统一 SSOT；Win32 "3+1" 四态工作站；文档漂移清零 | 73 |
| P9 / P9.1 | v0.1.0 GM | 2026-11-15 | 真权重 RIFE v4.25 120fps 全链路根治；1:5 重复帧顽疾根除（`longest_frozen_run = 0`） | 75 |
| P10 | v0.2.0 | 2026-11-18 | 五大阻断断路抢修；20 项 SSOT 全模态码控；VSR-First 拓扑；WebUI v3；异步软中断 | 78 |

> 单测计数为各阶段终审当时的实测值，非单调递增——P6/P8 的死代码切除与测试合并会使命名用例数下降。当前基线见 `README.md`。

---

## 二、 逐阶段核心遗产

### Phase 1 — 硬件零拷贝裸转码基线
- **里程碑**：8-bit H.264→HEVC 241 帧 453.1 ms（≈532 fps）；10-bit HEVC Main10 96 帧 272.0 ms；device-to-device 字节数与 `width×height×bpp×frames` 精确相等。
- **存活遗产**：`nv_decoder.cpp` 的 `cuvidGetDecoderCaps` 能力前置拦截、coded/display 尺寸分离（1088 vs 1080）、DPB 与输出映射双池模型；`nv_encoder.cpp` 的 `cuDevicePrimaryCtxRetain` 主上下文共享与显存指针注册哈希缓存；`ffmpeg_io.cpp` + `bitstream.cpp` 的 AnnexB 嗅探与手工 hvcC/avcC 组装；`DynLibrary` 的 `LOAD_LIBRARY_SEARCH_SYSTEM32` 加固加载。
- **最大教训**：Maxwell sm_52 本身具备完整 NVDEC/NVENC——误判硬件能力导致媒体层全量桩化与整体空心化。

### Phase 2 — D3D12/CUDA 互操作 + RTX Video SDK
- **里程碑**：Gate 2.1 1000 帧 1080p SDR→4K HDR10 真实容器转码 128.7 FPS（红线 60）；三轮 soak 累计 174 分钟、1,079,000 帧、0 TDR。
- **存活遗产**：`d3d12_interop.cpp` 的 LUID 比对 + 无头设备创建 + `CUDA_EXTERNAL_MEMORY_DEDICATED` 共享显存导入；VSR 与 TrueHDR **双独立 Handle / 参数块 / Scratch Buffer**（Blackwell 防崩铁律）；NVRTC 生成 PTX + `cuModuleLoadData` 驱动级 JIT，**每次发射前重绑主上下文**；生产者/消费者编码流水线（串行 55.0 → 128.7 FPS 的关键）。
- **关键裁决**：NVIDIA 不分发 `nvsdk_ngx_d.dll`——静态桩 `nvsdk_ngx_s.lib` 在运行时寻址 DriverStore 的 `_nvngx.dll`，再加载 `nvngx_vsr.dll` / `nvngx_truehdr.dll`。

### Phase 3 — FFmpeg 全量静态内联与单 EXE 自包含
- **里程碑**：媒体层 116.5 MB → 2.70 MB 静态归档，内联增量 1.85 MB；零假设工具链自举（Zig 0.13 / nasm / 便携 make 全收敛于构建目录）。
- **存活遗产**：FFmpeg **7.1.1 锁定**（纯 GPU 职责边界下只承担解复用/复用两端，8.x 新特性 100% 物理剥离）；`RtxFfmpegStatic.cmake` 的 450 KB 动态导入桩熔断器；`MemoryAvioContext` 内存流自环；`kFlicksPerSecond = 705600000ULL` + 128 位余数结转；i18n 强类型 `StrId` constexpr 双语表 + `Ctrl+L` 热切。
- **宪章**：`RTX_HAVE_FFMPEG` 类选配宏属宪章级违宪，永久删除。

### Phase 4 — Modern C++26 全栈重构
- **存活遗产**：纯聚合结构体 + 指定初始化器铁律；`Result<T>` / `Result<void>`（`include/rtx/status.h`）；`std::hardware_destructive_interference_size` 缓存行对齐；`BCryptHash` 原生 SHA-NI；GPL / MIT-Apache / NVIDIA 专有三区物理防火墙。
- **废除**：两段式 `initialize()/shutdown()/destroy()` 教条（自外部 CAD 文档移植的错误规范）、自造 `SmallExpected`、单实现虚接口伪多态。

### Phase 5 — 全媒体可用性
- **里程碑**：Gate 5.1–5.5 全 PASS；竖屏 720×1280 → 2160×3840 双向 3.0× 等比，畸变 0.00%；吞吐 84.89 / 83.71 FPS，音频开销 <0.1%。
- **存活遗产**：音轨零拷贝透传（`av_packet_rescale_ts` + `av_interleaved_write_frame` 物理交织）；智能几何引擎与 Studio Black 清空（`Y=0x1000 / UV=0x8000`，杜绝荧光绿）；`sdr_blit_cs.h` / `sdr_blit_rs.h` SDR 计算着色器管线。

### Phase 6 — 架构瘦身与产品化
- **里程碑**：四波次净减 29,739 行；`rtx_gui.cpp` 1,950 → 736 行；收官终审 9.6/10。
- **存活遗产**：Win32 Common Controls v6 暗黑矩阵 + NVIDIA Signature Green `#76B900` + Per-Monitor V2 DPI；私有构造 + `static Result<T> create(...)` 静态工厂；PTX/着色器构建期生成（编译产物永不入库）；`/INCREMENTAL:NO` + PDB 重定向 + Staging 校验三件套；AV1 默认 + sm_89 自检无感回退 HEVC；`/arch:AVX2` 以 VS 实际汇编为唯一裁决标准。

### Phase 7 — 实时监看与色彩科学
- **存活遗产**：`timestamp_guard.h` 每流独立单调时间戳守卫（24 h 虚拟流 100 处倒退 DTS 注入下累积漂移 = 0）；`.cube` 3D LUT 硬件三线性 FP16 纹理 + **块级单次采样**（+0.034 ms，对比每像素 +1.09 ms）；HDR10+ ST 2094-40 T.35 SEI / AV1 metadata OBU；Warp 归约帧亮度统计驱动自适应 TrueHDR；三槽 P010 环形缓冲 + 核心侧在途门禁；TDR 事务自愈（设备重建 + 250 ms 退避 + ≤3 次重试）。
- **战略裁决**：数据面定型优先于分布式扩缩——单卡视口/色彩/批处理未冻结前严禁扩多卡。

### Phase 8 — 双路真补帧与三端统一 SSOT
- **里程碑**：收官门禁 3.1–3.6 全 6 道 PASS；幽灵 CLI 开关与废弃类名命中 **0**；单 EXE 4.28 MB；`docs/` 23 篇 ≤ 25。
- **存活遗产**：路线 A 驱动原生 `nvofapi64.dll` NVOFA 硬件光流 + PTX `rtx_nvof_warp_nv12` 运动补偿；路线 B TensorRT 10.13.3.9 + RIFE v4.25，共享同一 `D3D12Interop` CUDA 上下文；`kParamSchema` 19 项 SSOT（P10 扩至 20）+ `rtx::trW` 113 词条（P10 扩至 125）；Win32 "3+1" 四态工作站（`[1] Encoder / [2] Player / [3] NLE` Flicks 四轨时间轴 / `+1` 文件关联无边框全屏动态 VSR 快播）；`OutputDebugStringW` `[CineRTX]` DebugView 日志流。
- **归档**：51 篇历史阶段单 + 11 篇 TODO 分册提炼为 `Phase1_to_Phase7_Evolution_Digest.md` 后物理清理。

### Phase 9 / 9.1 — 真权重 RIFE 120fps 全链路根治
- **里程碑**：Gate 9.1–9.6 全 PASS；75/75 单测全绿；24fps→120fps 真权重转码 480 帧、`fruc_failures = 0`、34 对相邻帧差 MAE 全部 >0、`longest_frozen_run = 0`。
- **存活遗产**：真实 7 通道 IFNet ONNX（`9,913,341` 字节）与 Blackwell 实机编译引擎（`13,479,948` 字节）；`kHALF` FP16 输入/输出张量全链路 + 帧对缓存 `ch6`（alpha）同步刷新；`full_range` BT.709 Limited / BT.601 Full 双向色彩一致性；`AuxOutputAllocator : IOutputAllocator` 动态辅助输出绑定；`ScopedCuCtxAllocHook` 多段上下文分配追踪；`cached_scene_cut` 转场帧缓存污染隔离；会话级日志配额 + 插帧 100% 失败硬熔断（非零退出码）。
- **五项深水区缺陷**：PTX ISA 9.0 禁止 `.f16` 寄存器直接 `st/ld.global`（改 `.reg .b16`）；上游 ONNX 把时间步静态折叠为 `0.5`（`/Cast_8` 计算图手术改动态 Slice+Pad）；4 个数据依赖型动态形状辅助输出未绑定；动态 `Concat` 触发 2 次驱动层分配漏记；转场分支覆写主缓存。

### Phase 10 — 全栈转码链路抢修与架构提纯
- **里程碑**：Gate 1–4 全 PASS；`cmd /c build.bat` 63/63 编译单元、ExitCode 0；78 项单测全绿无 `[SKIP-GPU]`；CLI 极限压测 `frames_decoded: 60 / frames_interpolated: 59 / frames_encoded: 119`、`fruc_failures: 0`、`measured_max_cll_nits: 1000`。
- **五大阻断断路根治**：
  1. **PTX ISA 拒载**（`CUDA_ERROR_UNSUPPORTED_PTX_VERSION 222`）→ `cmake/EmbedPtx.cmake` 构建期强制钳制 `.version 7.5` / `.target sm_75`，并恢复 `test_shim.cpp` 的 `pack_avail == RTX_OK` 强门禁；
  2. **光流核参数 ABI 槽位错位**（4 字节浮点误传 8 字节 `CUdeviceptr` → `CUDA_ERROR_ILLEGAL_ADDRESS 700` 永久毒化主上下文）→ `args[14]` 类型/顺序/位宽 1:1 咬合，启用 `cost_buf` 置信度校验（>140u 退化为时间域线性混合），字节级步长寻址；
  3. **显存环覆写竞态**→ `slot_free_.wait` 屏障移回 `remaster()` 之前，`remasters_issued_ - encodes_completed_ < 3` 物理栅栏；异常退出路径补齐 `releaseDecodedFrame` 归还 NVDEC 租约；
  4. **GUI 状态机 TOCTOU 死锁**→ 在 `mutex_` 保护域内**先**置位 `worker_active_` **再**派发 `JobFinished`；100 ms 节流回调；千分比进度标度修正；`JobStatus::Failed` 精准嗅探 + 错误气泡；
  5. **`--session-stdio` 阻塞期丧失中断能力**→ 独立 `stdin_reader` 线程 + `cancel_flag` release store，主循环在显存交换缝隙读取，`exitCode: 130` + `{"status":"cancelled"}` 规范化握手。
- **存活遗产**：20 项 SSOT 全模态码控（NVENC `cq/crf/vbr/cbr/qp` + CPU `av1_cpu/hevc_cpu` CRF 等效转译）；`cq` 精确映射 `RTX_RC_CQ` → `NV_ENC_PARAMS_RC_VBR` + `targetQuality` + `averageBitRate = 0`；**VSR-First 拓扑**（先超分恢复高频细节再送光流）；`ShotGroupSegmenter` 硬切时同步下发 `NVSDK_NGX_Parameter_Reset_Set(1)` 清洗超分循环历史记忆；音频包内存驻留 + `flushBufferedAudioPackets()` 无损回填；`RTX_EMBEDDED_NGX_BRIDGE` 桥接层静态内联（零附属 DLL）；WebUI v3。
- **遗留技术债（Phase 11 立项动因）**：① 任务队列历史故障捕获缺防抖去重；② preview 采样纹理回调未透传至 `EncoderJobQueue`，DirectComposition 零拷贝实时色调映射监看尚未铺路。

---

## 三、 反模式黑名单（防回潮总表）

以下设计已被历史终审彻底废除，**严禁在任何现行代码或新设计中回潮**。Phase 11 母本（`docs/phase11/04`）对其中两项作出显式反转，见表末注。

| 反模式 | 废除阶段 | 废除原因 | 替代物 |
| :--- | :--- | :--- | :--- |
| `CpuReferenceBackend` CPU 模拟转码 | P1 | 空心化根源，违背纯 GPU 零拷贝第一原则 | 真实 NVDEC/NVENC 硬件链路 |
| `RTX_WITH_FFMPEG` / `RTX_HAVE_FFMPEG` 选配宏 | P3 | 宪章级违宪 | FFmpeg 无条件静态内联 |
| `FfmpegNotCompiled*` 存根类 | P3 | 缺失媒体 I/O 推迟到运行期报错的 Shift-Right 反模式 | Configure/Link 期 FATAL |
| `rtx-transcode*.exe` 冗余靶标 | P3 | 多靶标碎片化 | 唯一 `CineRTX.exe`（GUI/CLI 双模） |
| 两段式 `initialize()/shutdown()/destroy()` | P4 | 外部 CAD 文档教条移植，全仓反 RAII | 私有构造 + `create(...)` 静态工厂 + 析构自动回收 |
| 自造 `SmallExpected` / `RingError` / 手写 Sha256 / `PingPongRing` | P4 / P6 | 标准库可零成本替代 | `std::expected` / `BCryptHash` / 三槽环形 |
| 单实现虚接口伪多态 | P4 | 零收益抽象 | 命名空间自由函数 |
| P010 显存全 0 初始化 | P5 | YUV 全零呈荧光绿 | Studio Swing 黑电平清空 |
| VSR 输出强行绑定 RGBA16F | P5 | 驱动层不可恢复崩溃 `0xBAD0000E` | `mid_` R8G8B8A8 中间纹理路由 |
| 零子 HWND 纯 GDI 手绘教条 | P6 | 1,073 行仅 3 个点击热区 | 子 HWND + 原生 Common Controls |
| 源码树内嵌 PTX/CSO 头文件 | P6 | 925 KB 自动生成物入库污染 | 构建期生成至 `${CMAKE_BINARY_DIR}/generated` |
| `RTX_GUI_RESULT_LOG` / `RTX_GUI_STARTUP_LOG` / `--trace` | P6 / P7 | 磁盘日志落盘 + 隐蔽分支 + 计时毛刺 | `OutputDebugStringW` + DebugView `[CineRTX]` |
| 每帧 PostMessage 进度推送 | P3 | 模态循环积压丢包 | 原子遥测拉取 |
| 热循环内 `cuMemMap/cuMemUnmap` | P3 | 每帧全局 GPU 同步屏障 + TLB 刷新 | 会话创建期确定性设备分配 |
| 默认计时 CUevent | P2 | 迫使 GPU 前端刷新时间戳寄存器（2.424 ms 毛刺） | `CU_EVENT_DISABLE_TIMING` |
| 每像素 3D LUT 三线性采样 | P7 | 65³ 色彩空间无像素邻域局部性（+1.09 ms） | 块级单次采样（+0.034 ms） |
| 跨卡传输未压缩像素 | P7 | 4K P010 ≈3 GB/s PCIe，收益 <10% | 文件级 / GOP 分片，零跨卡像素 |
| Linux Vulkan 互操作 | P7 | NVIDIA 无 Linux NGX Vulkan 二进制 | 纯 CUDA 无头后端 |
| gRPC + Protobuf 重型 RPC | P7 | >50 MB 静态库违反单 EXE 铁律 | 零依赖 JSON-RPC / JSON 桥 |
| `offruc_bridge` 独立补帧模块 | P6 | 未接线死代码 | 桥接层内 NVOFA 路线 A |
| `isBuiltinSynth()` 下 `launchNvofWarpNv12` 顶替 RIFE | P9.1 | 伪插帧：Route B 输出被 Route A 覆写 | 纯 TensorRT 推理输出 |
| 跨会话 `static thread_local` 日志配额 | P9.1 | 后续任务首错日志被静默吞噬 | 单次转码会话栈内局部变量 |
| 插帧 100% 失败仍返回 ExitCode 0 | P9.1 | 静默退化伪装成功 | `ErrorCode::Internal` 硬熔断 |
| 删除断言掩盖 PTX 拒载 | P10 | 工程违规 | 构建期 `.version` 钳制 + 恢复强门禁 |
| `JobFinished` 早于 `worker_active_` 置位 | P10 | TOCTOU 致 UI 永久死锁 | mutex 域内先置位再派发 |
| 独立 `rtx_ngx_bridge.dll` 附属分发 | P10 | 违反单 EXE 自包含 | `RTX_EMBEDDED_NGX_BRIDGE` 静态内联 |
| `tools/` 脚本目录 | P9.1 / P10 | 脚本契约漂移、幽灵开关滋生 | 全原生 CMake（`cmake/*.cmake`） |

**Phase 11 显式反转（以 `docs/phase11/04` 母本为准）**：
1. **DirectComposition 视口**：P6 曾以"覆盖干扰子窗口重绘、重型依赖"为由物理删除 `dcomp_viewport`，P7 改子 HWND + `FLIP_DISCARD`。母本 §2.1 R-05 裁决与 §4 Track 11D 垂直切片 A 重新批准 DComp——目标是**零主机像素回读（0 B/s D2H）**的 HDR/SDR 实时监看视口：复用 `D3D12Interop` 按 CUDA LUID 匹配的无头设备、`D3D12_COMMAND_QUEUE_PRIORITY_NORMAL` 独立图形队列、动态 `dcomp.dll` + `CreateSwapChainForComposition` + `DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL`。母本同时澄清"0 D2H"不等于"0 GPU copy"，与 P6 废除的"重型播放器视口"不是同一物件。
2. **`IDemuxer` / `IMuxer` 虚接口**：P4 判为单实现伪多态。母本 §4 Track 11A 第 3 条把 `std::unique_ptr<IDemuxer> demuxer_` 与 `std::unique_ptr<IMuxer> muxer_` 直接写入 `NvidiaTranscodeSession` 的强制成员声明拓扑，Track 11C 的 GOP-Aware 帧精确 Seek 亦以 `IDemuxer` 索引为入口——该接口由母本显式保留，不再按伪多态论处。

---

## 四、 跨阶段硬件与驱动踩坑总账

- **尺寸对齐**：1080p 编码高度因 16 宏块对齐为 1088——解码输入用 coded 尺寸，输出池/NVENC 锁定 display 尺寸。
- **CUVID 双池**：DPB 与输出重映射池（`ulNumOutputSurfaces`）物理隔离；延迟 unmap 或 surfaces=2 会耗尽映射槽位（`cuvidMapVideoFrame64 → 205`）。
- **EOS 排空**：同步零重排模式下提交 EOS 后再 `nvEncLockBitstream` 会无限期阻塞——排空循环必须返回 `RTX_ERR_NOT_FOUND`。
- **CUDA 阵列视角**：`R10G10B10A2` 是 1 通道 `UNSIGNED_INT32`，硬编码 4 通道触发 `CUDA_ERROR_INVALID_VALUE`。
- **64 位入口**：裸 `cuMemcpy2D/cuMemcpy2DAsync` 是 32 位遗留入口，必须 `_v2` 优先。
- **DXGI 状态污染**：`cuInit(0)` 先于 `DXGIDeclareAdapterRemovalSupport` 会永久污染进程 DXGI 状态，防 TDR 声明静默失效。
- **上下文剥离**：D3D12 驱动操作可随时剥离线程 CUDA 上下文——每次发射前显式重绑主上下文。
- **屏障纪律**：UA 纹理在 Direct Queue 不会隐式衰减回 COMMON，CUDA 读前必须显式 Transition；全局空指针 UAV barrier 触发 Global L2 Cache Drain，必须对特定资源显式指定。
- **TDR 判定口径**：WDDM TDR 看"命令包队列驻留墙钟"而非单核函数耗时——单帧单次 `ExecuteCommandLists` + 队列深度限流 + 流优先级治理。
- **NGX 关闭卡死**：`NVSDK_NGX_D3D12_Shutdown1` 在驱动 617.14 上卡死——禁调，NGX 运行时保留到进程退出。
- **TrueHDR 输出语义**：FP16 = linear scRGB、BT.709 原色、1.0 = 80 nit，不得先 clamp。
- **PTX JIT**：CUDA 13.x 工具链默认 `.version 9.0` 需 R580+ 驱动；构建期钳制 `.version 7.5` / `.target sm_75` 才能覆盖 Turing→Blackwell 全系。`.f16` 寄存器不可直接 `st/ld.global`，须 `.reg .b16` 承载位模式。
- **MSVC 精度三坑**：无 `__int128`、`long double` = 53 位尾数、负半值 `floor(+0.5)` 向正无穷舍入——`_umul128/_udiv128` + 正负对称舍入。
- **FFmpeg/MSVC 链接**：MinGW `.a` 无法被 `link.exe` 做 `/OPT:ICF` COMDAT 折叠且 libgcc 符号与 ucrt SEH 表冲突——必须 `--toolchain=msvc` 原生 COFF；深层系统库溯源（bcrypt ← random_seed、secur32 ← tls_schannel、ws2_32 ← network）。
- **TensorRT 集成**：`createInferBuilder` 等是头文件 inline 包装，必须 `GetProcAddress` 解析 `_INTERNAL` 符号；TRT 头无条件包含 `cuda_runtime_api.h`，无 Toolkit 宿主用编译垫片。
- **显存账本**：DWM 桌面基线占用必须计入；共享 GPU Memory 经 PCIe 换页会使吞吐断崖跌落 80%+ 并诱发 TDR。
- **无头会话限制**：SSH 会话（session 0 / 服务窗口站）无 DWM 合成，**任何交换链创建均返回 `DXGI_ERROR_NOT_CURRENTLY_AVAILABLE (0x887A0022)`**，与窗口样式和 swap effect 无关；交互式 GUI/视口门禁必须回到真实桌面会话执行。此结论自 P7 起沿用，P11 Track 11A 以 8 组样式×swap effect 组合实测复证。
- **环境陷阱**：SSH 会话结束会回收子进程（长任务必须前台托管）；共享工作站桌面应用会周期性耗尽物理 RAM（结构化 `OUT_OF_MEMORY` 返回，非引擎缺陷）；远端宿主时钟可能领先本地，tar 同步必须 `-m` 归一化 mtime，否则 Ninja 跳过重编译。