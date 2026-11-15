# CineRTX Phase 1–7 演进归档摘要（单一权威演进档案）

> **档案编号**：`ARCHIVE-PHASE1-7-EVOLUTION-DIGEST-001`
> **编撰日期**：2026-11-14（阶段三收官合拢 · Track 3C）
> **档案定位**：本档案是 `docs/phase1` ~ `docs/phase7`、`docs/handover/`、`docs/gui/` 全部历史阶段单与 `docs/TODO/` 全部历史分册的**唯一提炼归档**；原目录与分册已在提炼校验后物理清理。当前权威系统实现一律以根目录 `README.md` / `README_zh.md`、`docs/spec/architecture.md` 为准；Phase 8 权威定稿见 `docs/phase8/`。
> **脱敏声明**：本档案不含任何私有网络地址、主机名或本机绝对路径；验收主机一律以 `[REDACTED_HOST]` 指代。

每个阶段章节固定四个维度：①阶段核心目标与里程碑判定；②至今存活生效的架构遗产与核心代码；③已被彻底废除的历史过渡设计（防回潮黑名单素材）；④关键排障经验与硬件踩坑记录。

---

## Phase 1 硬件零拷贝裸转码基线（W1–W3，2026-10-21 终审通过）

### ① 阶段核心目标与里程碑判定

- **核心目标**：交付“解复用 → NVDEC → 显存指针 → NVENC → 封装”的零拷贝裸转码闭环，兑现 Zero Host-Memory Copy 黄金准则；引入 MIT 授权的 ffnvcodec 头文件体系。
- **首轮否决**：执行工程师误判“GTX 970M（Maxwell sm_52）无法开发 RTX 50 系特性”，导致媒体层全量桩化、后端硬编码 NOT_IMPLEMENTED、`third_party/open_headers/` 空置、精力错投二三阶段模块——专家返工令下达 6 项整改指令。
- **返工核验**：本地与远端仓库副本全量 100 文件 SHA-256 比对零差异；主动上报根目录遗留转储文件与损坏样片。
- **终审通过（2026-10-21）**：RTX 5060 Laptop（Blackwell sm_120、CUDA 13.4、NVENC v209）实测——
  - 8-bit H.264→HEVC 241 帧 453.1 ms（≈532 fps、22.1× 实时），device-to-device 333,158,400 B（恰 = 720×1280×1.5×241）；
  - 10-bit HEVC Main10 96 帧 272.0 ms（≈353 fps、14.7× 实时），D2D 265,420,800 B（= 720×1280×3×96）；
  - `D2H=0 / H2D=0`，`verify_gate1.ps1` 4/4 PASS，单测 101/101，PE 导入表 0 NVIDIA 静态符号；
  - GTX 970M（sm_52、CUDA 13.0、NVENC v208）并行验证 196.8/264.0 fps——Maxwell 低水位线成立。
- **深度验收揭露 7 项汇报偏差**（列为二阶段强制整改 ACT-01~07）：空 `p010_pack.cu`（0 字节）、FfmpegMuxer 未写 mdcv/clli、VUI 漏 topleft、onDisplay 内嵌 `cuStreamSynchronize`、显存池双轨割裂、脚本覆盖缺漏、nsys 缺失。

### ② 至今存活生效的架构遗产与核心代码

- `src/shim/loader/nv_decoder.cpp`：`cuvidGetDecoderCaps` 能力前置拦截（不支持编码在会话创建期即失败）；编码 1088 / 显示 1080 尺寸分离（防 1088 黑边输出）；DPB（32 参考帧）与输出映射池双池模型；`cuvidMapVideoFrame64 → 两次 cuMemcpy2DAsync（亮度+色度）→ 立即 unmap`。
- `src/shim/loader/nv_encoder.cpp`：`cuDevicePrimaryCtxRetain` 主上下文共享；显存指针注册哈希缓存（O(1) 复用）；`NV_ENC_INPUT_RESOURCE_TYPE_CUDADEVICEPTR` 直通。
- `src/io/ffmpeg_io.cpp` + `bitstream.cpp`：AnnexB 首包嗅探 + h264/hevc_mp4toannexb BSF；手写 `BitReader`；按 ISO 14496-15 手工组装 hvcC/avcC。
- `sei_synthesizer.cpp`（SEI 137/144 单一事实源）、`rational.cpp`（128 位有理数重缩放 + 余数结转）、`scene_detect.cpp`（步长 4 归一化 Luma SAD 硬切检测）。
- `DynLibrary`：`LOAD_LIBRARY_SEARCH_SYSTEM32` 加固加载，封杀 CWD DLL 劫持。
- `include/shim/rtx_shim_abi.h`：ABI v2（不透明句柄 + uint64 设备地址）。
- 审计工具族：`tools/check_spdx.ps1`、`tools/audit_imports.ps1`（自研 PE32/PE32+ 解析）、`tools/package.ps1`、`tools/verify_gate1.ps1`。
- `--probe --json` 全 ASCII 转义（UTF-8 续字节保护）。

### ③ 已被彻底废除的历史过渡设计

- CPU 模拟转码后端 `CpuReferenceBackend`——空心化根源，返工令废除，真实硬件链路取而代之。
- `UnavailableDemuxer/Muxer` 桩 + `ErrorCode::FfmpegMissing`——Phase 3 随条件宏永久删除。
- `RTX_WITH_FFMPEG / RTX_HAVE_FFMPEG` 构建开关——宪章级违宪（Phase 3 正式废除）。
- 测试硬编码绝对路径宏——改 `executableDirectory()` 相对探测。
- onDisplay 内阻塞式 `cuStreamSynchronize`——违背异步铁律，改 CUevent。

### ④ 关键排障经验与硬件踩坑记录

- **认知偏差教训（本阶段最大坑）**：Maxwell sm_52 本身具备完整 NVDEC/NVENC——误判硬件能力导致整体空心化。
- GM204 无 HEVC 解码引擎：必须前置拦截，否则解析器 onSequence 才报错陷入未定义阻塞。
- 1080p 编码高度因 16 宏块对齐为 1088：解码输入用 coded 尺寸、输出池/NVENC 锁定 display 尺寸。
- CUVID 的 DPB 与输出重映射池（`ulNumOutputSurfaces`）是两个物理隔离资源池；延迟 unmap 或 surfaces=2 会耗尽映射槽位。
- 同步零重排模式下提交 EOS 后再 `nvEncLockBitstream` 会无限期阻塞——排空循环必须返回 `RTX_ERR_NOT_FOUND`。
- RTX 5060 无 NGX SDK 时 `create` 合法返回 `RTX_ERR_NGX`——测试断言必须覆盖该码。
- `p010_pack.cu` 0 字节空文件事件：汇报与实物不符的审计教训。
- `compute_75` PTX 可在 sm_120 上驱动 JIT 加载（后由 pack_verify 证实）；sm_52 上 JIT 拒绝是预期防御。
- `CUDA_EXTERNAL_MEMORY_DEDICATED` 是 Windows 跨 API 纹理导入的硬性要求。
- MSVC 无 `__SIZEOF_INT128__` 且 long double = 53 位尾数——为 Phase 3 时基精度审计埋下伏笔。

## Phase 2 D3D12/CUDA 互操作 + NGX VSR/TrueHDR + 开源发布（W4–W6，2026-10-25 全量通过）

### ① 阶段核心目标与里程碑判定

- **核心目标**：W4 无头 D3D12↔CUDA 外部显存互操作 → W5 RTX Video SDK（VSR+TrueHDR）串联 → W6 P010 打包核函数 + HDR10 双路注入 + 管线合流 + Win32 单 EXE GUI。
- **前置整改**：Phase 1 遗留 ACT-01~06 全部销账（PTX 动态加载、mdcv/clli、VUI topleft、NVDEC 异步化、脚本覆盖、文档卫生）；ACT-07（nsys）两台宿主均无 nsys，如实挂起。
- **二度返工**：互操作通道数硬编码与输出表面池过小导致 Gate 1 短暂回退，返工令 RW2-01~05 复验后恢复 4/4、互操作自测 round trip OK。
- **W4 里程碑准出（2026-10-24）**：签署里程碑，冻结 `rtx_shim_abi.h` / `ngx_bridge_abi.h` ABI v2。
- **W5 专有 SDK 误区裁决**：“SDK 缺失、W5 挂起”的结论被实机证据推翻——NVIDIA 不分发 `nvsdk_ngx_d.dll`，而是静态桩 `nvsdk_ngx_s.lib` 在运行时寻址系统 DriverStore 的 `_nvngx.dll`，再加载 `nvngx_vsr.dll` / `nvngx_truehdr.dll`。
- **最终验收（2026-10-25）**：
  - **Gate 2.1 PASS**：1000 帧 1080p SDR→4K HDR10 真实容器转码，主循环 7769.19 ms = **128.7 FPS**（红线 60）；进程显存峰值 ≈1245 MiB（整卡峰值 − 运行前基线口径）；零拷贝四项计数全 0。
  - **Gate 2.2 指标级 PASS**：pack v2 稳态均值 1.526 ms（≤1.8），nsys 证据缺失如实标注。
  - **Gate 2.3 文件级 PASS**：ST 2086 / BT.2020 / PQ / Display P3 0.0001–1000 cd/m² / MaxCLL 1000 / MaxFALL 300 全字段。
  - **Gate 2.4 GPU 稳定性 PASS**：三轮 soak 累计 174 分钟满载、1,079,000 帧、0 TDR / 0 蓝屏 / 显存平坦；2 小时连续窗口被共享工作站桌面应用内存周期耗尽环境性阻断（结构化错误返回，非引擎缺陷）。
  - **GUI 门禁 PASS**：3.30 MB、冷启动 18.55 ms、私有工作集 1.23 MB。
- **审计与发布**：深度静态审计评级 B+→A，12 项缺陷 100% 闭环后签发 GitHub 开源发布令；开源协议异常（Licensee 识别为 other）裁定 LICENSE 恢复 100% FSF 原版文本 + 独立 `COPYING.EXCEPTION`，spdx_id 恢复 GPL-3.0。

### ② 至今存活生效的架构遗产与核心代码

- `src/shim/ngx_bridge/d3d12_interop.cpp`：LUID 8 字节比对 + 无头 `D3D12CreateDevice`（12_1→12_0→11_0 降级）；Committed Resource + `D3D12_HEAP_FLAG_SHARED` + `CreateSharedHandle`；`cuImportExternalMemory` 强制 `CUDA_EXTERNAL_MEMORY_DEDICATED`；共享 `ID3D12Fence` 经 `cuImportExternalSemaphore` 导入；单一事实源格式表（`R10G10B10A2 = UNSIGNED_INT32 × 1 通道`）；`cuMemcpy2D_v2/cuMemcpy2DAsync_v2` 优先加载；高优先级 pack 流与队列深度限流。
- `ngx_d3d12_bridge.cpp`：VSR 与 TrueHDR **双独立 Handle、双独立参数块、双独立 Scratch Buffer**（Blackwell 防崩铁律）；4K SDR 中间纹理（不导入 CUDA）；三段式屏障纪律；能力探测与设备丢失轮询。
- `p010_pack_runtime.cpp`：NVRTC 生成 PTX + `cuModuleLoadData` 驱动级 JIT；**每次发射前重绑主上下文**；核函数族 `rtx_yuv_to_rgb10`（NV12/P010 → R10G10B10A2）与 `rtx_rgba16f_to_p010_bt2020nc` v1/v2（v2：scRGB 1.0=80nit → BT.2020 线性矩阵 → ST 2084 PQ → limited P010 + IGN 抖动 + topleft 色度）。
- 生产者/消费者编码流水线：NVENC 与 NGX 跨引擎并行——从串行 55.0 FPS 提升到 128.7 FPS 的关键。
- `SeiSynthesizer` 单一事实源（DCI-P3 D65 黄金参数）。
- /MT 静态 CRT shadow 桩库（Zig/MinGW 链接 MSVC 运行库的归档生成机制）。
- SDK 归档 SHA-256 双向校验坏包自愈供给；`plugins/nvidia_rtx/` 隔离部署布局；`[REDACTED_*]` 脱敏体系与 SECURITY.md。

### ③ 已被彻底废除的历史过渡设计

- `sdkPresent()` 布尔或探测（单 DLL 残留即误报双特性就绪）——改严格逻辑与。
- “nvsdk_ngx_d.dll” 幻影依赖表述——勘误为静态桩 + `_nvngx.dll`。
- 私有 `applyHdrDefaults` 硬编码——收敛至 `SeiSynthesizer` 单一事实源。
- `RTX_WITH_NGX_SDK` 开关——Windows 构建无条件编译 SDK 桥接。
- `--remaster` 开关与 `rtx-transcode*.exe` 目标名——单一产品契约取代（容器输入无条件重制）。
- 默认计时 CUevent——改 `CU_EVENT_DISABLE_TIMING`（消除 2.424 ms 毛刺）。

### ④ 关键排障经验与硬件踩坑记录

- `R10G10B10A2` 在 CUDA 阵列视角是 1 通道 UNSIGNED_INT32——通道数硬编码 4 导致 `CUDA_ERROR_INVALID_VALUE`。
- 64 位 Windows 上裸 `cuMemcpy2D/cuMemcpy2DAsync` 是 32 位遗留入口——必须 `_v2` 优先。
- 异步化整改后未扩 `ulNumOutputSurfaces`（原 2）引发 `cuvidMapVideoFrame64 → 205`。
- `cuInit(0)` 先于 `DXGIDeclareAdapterRemovalSupport` 会永久污染进程 DXGI 状态——防 TDR 声明静默失效。
- D3D12 驱动操作可随时剥离线程 CUDA 上下文——每次发射前显式重绑主上下文。
- UA 纹理在 Direct Queue 不会隐式衰减回 COMMON——CUDA 读前必须显式 Transition（驱动才插入 DCC 解压 + L2 flush）。
- 全局空指针 UAV barrier 会触发 Global L2 Cache Drain——必须对特定资源显式指定。
- WDDM TDR 判定是“命令包队列驻留墙钟”而非单核函数耗时——单帧单次 ExecuteCommandLists + 队列深度限流 + 流优先级治理。
- `NVSDK_NGX_D3D12_Shutdown1` 在驱动 617.14 上卡死——禁调，NGX 运行时保留到进程退出。
- TrueHDR FP16 输出 = linear scRGB、BT.709 原色、1.0 = 80 nit——不得先 clamp。
- 计时 CUevent 迫使 GPU 前端刷新时间戳寄存器——单帧 4 个计时事件是毫秒级毛刺根因。
- 驱动 617.14 专项：NVENC P3/P4 preset tuningInfo 返回 UNDEFINED（会话创建失败，已修）；`used_memory` 查询返回 N/A；SSH 会话性能计数器不可用。
- soak 环境陷阱：SSH 会话结束回收子进程；共享工作站桌面应用 ~50-60 分钟周期性耗尽 16 GB 物理 RAM（结构化 OUT_OF_MEMORY 返回）。

## Phase 3 v0.2.0 全栈演进 + FFmpeg 全量静态内联 + 单 EXE 自包含（2026-10-25 立项 / 2026-11-01 闭环）

### ① 阶段核心目标与里程碑判定

- **五大议题终审裁定**：1080p 紧凑插帧先行 → 4K 超分/TrueHDR 后置的唯一标准拓扑；三级滞后水位；实时视口方案；Flicks（1/705,600,000 s）全局时基；Gate 3 门禁体系。
- **前置 P0 攻坚**：产品负责人否决 `RTX_HAVE_FFMPEG` 选配宏（“就不该存在这个变量”），确立 **FFmpeg 全量静态内联、单 EXE 绝对自包含**第一宪章与 Gate 3.0 门禁；媒体层 116.5 MB → 2.70 MB 静态归档（`/OPT:REF/ICF` 后内联增量 1.85 MB），单 EXE ≈8.45 MB。
- **交付纪律**：执行工程师 56 文件 +6315 行补丁因“零物理编译”被判 75% 完成——下达轻量环境（Zig 0.13 + Ninja ~60 MB 免装）与全套实机门禁指令。
- **终极重构闭环（2026-11-01）**：零假设工具链自举（无编译器环境自动拉取 Zig 0.13 / nasm 2.16.03 / 便携 make，全收敛于 `build/external`）；废除冗余 EXE 靶标（唯一主程序 `CineRTX.exe`，无参 GUI / 带参 CLI 双模）；nv-codec-headers 规范化为 submodule（n13.0.19.1）；137 项单测 PASS、Gate 3.0 PASS、SPDX PASS。

### ② 至今存活生效的架构遗产与核心代码

- `tools/build_ffmpeg_static_*.sh` 手术刀白名单体系与 **FFmpeg 7.1.1 锁定依据**：
  - 纯 GPU 职责边界下 FFmpeg 仅承担解复用/复用两端——8.x 新特性（Whisper 滤镜、Vulkan Compute 编解码、D3D12/VAAPI 管线重构、APV/RealVideo 6.0）在白名单下 100% 物理剥离；
  - 8.x 主版本 ABI 重构（`AVChannelLayout` 强制化等）与 MSVC 静态链接确定性风险；
  - 已验证基线传承价值（128.7 FPS 吞吐与 soak 均以 7.1.1 校准）；
  - 体积精算：静态库 ~2.70 MB（avformat 1.15 / avcodec 0.95 / avutil 0.42 / swresample 0.18），内联增量 1.85 MB，EXE 稳定收敛 8.45 MB（8.0–12.0 MB 黄金包络）；
  - 未来 8.x 演进触发条件与零影响迁移 SOP（`src/io/` 单层替换 + 符号审计 + 门禁回归）。
- `cmake/RtxFfmpegStatic.cmake`：450 KB 动态导入桩熔断器；`rtx::ffmpeg_static` 接口靶标挂 bcrypt/ws2_32/secur32 等系统库；`/OPT:REF /OPT:ICF`。
- `MemoryAvioContext`（`avio_alloc_context` 内存流，32KB 扇区）：单测零磁盘自环，消除 Stub/Mock 必要性。
- `kFlicksPerSecond = 705600000ULL` 时基 + 128 位 `DriftCompensator` 余数结转（24 h 漂移 <1.42 ns 解析界）。
- i18n 强类型 `StrId` constexpr 双语表 + 多层语言协商（`--lang` > 环境变量 > `GetUserDefaultUILanguage` > en-US）+ `Ctrl+L` 热切。
- 插帧桥接纯 C ABI 设计（版本化配置/帧输入结构，is_scene_cut/is_static_frame 字段——后演进为 ngx_bridge v4 双路导出）。
- `tools/embed_ptx.py` 构建期 PTX 生成；`tools/gen_p010_ptx.py` NVRTC 生成链。
- `check_spdx.ps1` 追加 AI 权重后缀阻断（`.onnx/.engine/.plan/.trt/.safetensors/.pth/.pt`）；`audit_imports.ps1` 三重门（NVIDIA DLL 黑名单 + av*/sw* 黑名单 + 1.2–12 MB 体积包络）。

### ③ 已被彻底废除的历史过渡设计

- `RTX_WITH_FFMPEG / RTX_HAVE_FFMPEG / RTX_FFMPEG_DLL_DIR / RTX_FFMPEG_ROOT`——宪章级违宪，永久删除。
- `FfmpegNotCompiled*` 存根类——缺失媒体 I/O 推迟到运行期报错的 Shift-Right 反模式，改 Configure/Link 期 FATAL。
- `rtx_copy_ffmpeg_runtime`——把 >110 MB 的 FFmpeg DLL 拷到产物目录的碎片化分发。
- `rtx-transcode.exe / rtx-transcode-base.exe` 冗余靶标——统一为 `CineRTX.exe`。
- `third_party/open_headers/ffnvcodec/` 目录——改 submodule + 自研 2 KB `include/shim/dynlink_nvEncodeAPI.h`。
- 热循环内 `cuMemMap/cuMemUnmap`——每帧全局 GPU 同步屏障 + TLB 刷新。
- 每帧 PostMessage 进度推送——模态循环积压丢包，改原子遥测拉取。
- 探针互斥锁草案——改双缓冲原子索引翻转。
- 外部 FFmpeg 动态导入桩依赖（共享版 DLL 前缀路径）——submodule 就地编译 + 自举取代。

### ④ 关键排障经验与硬件踩坑记录

- **插帧拓扑数理依据**：4K 域插帧像素量 ×4、RIFE 特征金字塔 >3.8 GB 击穿 8 GB 显存；4K 光流位移矢量是 1080p 的 2 倍（易出棋盘伪影）；RIFE 权重在 sRGB/BT.709 gamma 空间收敛——故“1080p 先插帧、4K 后超分”为硬契约。
- **Flicks 数论**：705,600,000 可整除 24/25/30/48/50/60/90/100/120 fps 及 8k–192 kHz 采样率，但对 NTSC 系 23.976/29.97/59.94 出现小数——需扩展 Flicks（乘 1001 系因子）实现零余数（远期备忘）。
- MSVC 精度三坑：无 `__int128`、long double = 53 位尾数、负半值 floor(+0.5) 向正无穷舍入——`_umul128/_udiv128` + 正负对称舍入。
- CUevent 计时开销：计时 event 迫使驱动插入全局时间戳刷新——`CU_EVENT_DISABLE_TIMING` 后 <0.5 µs。
- CUDA VMM 语义：两槽映射同一物理块 = 并发踩踏；每帧 Unmap/Map = 全局屏障——仅对互斥工作区做静态别名。
- 显存账本：DWM 桌面基线占用；共享 GPU Memory 经 PCIe 换页会使吞吐断崖跌落 80%+ 并诱发 TDR。
- FFmpeg/MSVC 链接暗坑：MinGW `.a` 无法被 MSVC `link.exe` 做 `/OPT:ICF` COMDAT 折叠且 libgcc 符号与 ucrt SEH 表冲突——必须 `--toolchain=msvc` 原生 COFF；动态导入桩体积熔断（>450 KB 即真静态）；深层系统库溯源（bcrypt ← random_seed、secur32 ← tls_schannel、ws2_32 ← network、mf* ← mfenc）；/MD FFmpeg 静态库可安全链入主程序（纯 C 无 STL）。
- TensorRT 集成陷阱：`createInferBuilder` 等是头文件 inline 包装——必须 GetProcAddress 解析 `_INTERNAL` 符号；TRT 头无条件包含 cuda_runtime_api.h——无 Toolkit 宿主用编译垫片。
- 零假设自举：nasm 2.16.03 / 便携 make / zig 0.13.0 全部经 SHA-256 锚定下载并局部 PATH 注入——宿主 PATH 为空也能完成编译。
- 交付质量教训：未经真实编译器验证的补丁不能判生产可用；隔离沙盒目录（剥离 PATH）双击单 EXE 跑 4K 转码是 Gate 3.0 的最终判据。

## Phase 4 Modern C++26 全栈重构与全仓反 Slop（2026-10-31 终审 100% FULL PASS）

### ① 阶段核心目标与里程碑判定

- **核心目标**：建立零开销类型契约、64B 缓存行对齐无锁结构、三区法务物理防火墙；全仓反 Slop 治理、死代码切除与极简架构重塑。
- **里程碑判定**：专家终审验收（`CineRTX-EXPERT-VERDICT-20261031-PHASE4-FINAL`）**100% FULL PASS 无条件通过**，并据此签署 Phase 5 准入许可。

### ② 至今存活生效的架构遗产与核心代码

- 全仓结构体纯聚合化 + 指定初始化器 `.field = value` 铁律。
- `std::expected` 语义的 `Result<T>` / `Result<void>` 类型契约（`include/rtx/status.h`）。
- `std::hardware_destructive_interference_size` 缓存行对齐。
- `BCryptHash`（bcrypt.lib 原生 SHA-NI）替代手写哈希实现。
- 三区（GPL / MIT-Apache / NVIDIA 专有）物理隔离的构建映射与审计脚本加固。

### ③ 已被彻底废除的历史过渡设计

- 两段式 `initialize()/shutdown()/destroy()` 教条——自外部 CAD 项目文档移植的错误规范，正式废止并静态扫描归零。
- 自造 `SmallExpected` / `RingError` 轮子——换装标准库。
- 单实现虚接口抽象（`IDemuxer/IMuxer` 式伪多态）——命名空间自由函数。
- C 风格裸指针出参与手写逐字段深拷贝模块。

### ④ 关键排障经验与硬件踩坑记录

- 外部文档教条移植必须经对抗性审查（照搬即全仓反 RAII）。
- 静态扫描门禁（`initialize|shutdown|destroy` 匹配数 = 0）是唯一可执行的架构纪律。
- 标准库先行原则：`std::expected` / `<utility>` / `<bit>` 足以覆盖全部自造轮子场景。

## Phase 5 全媒体可用性与工程化落地（Wave 1–4，2026-11-08 全量交付，93 项单测）

### ① 阶段核心目标与里程碑判定

- **三大阻断缺陷根除**：转码后音轨丢失（哑巴视频）；9:16 竖屏 3.16 倍横向压扁畸变；SDR 直通崩溃 `0xBAD0000E` 与暗部发灰。
- **五门禁全 PASS**：
  - Gate 5.1 音画合流全保真（AAC 48 kHz 双声道完整透传、66 音频包交织）；
  - Gate 5.2 几何自适应（720×1280 → 2160×3840 双向 3.0× 等比，畸变 0.00%）；
  - Gate 5.3 SDR/HDR 双模合规（暗部 (0,0,0) 沉底、绿偏比 1.040 vs 源片 1.044、过曝 0.00%）；
  - Gate 5.4 吞吐不劣化（84.89 / 83.71 FPS，音频开销 <0.1%）；
  - Gate 5.5 绿色包解压即用（1302.60 MB 自包含分发，阶段六再瘦身至 34.35 MB）。
- **93 项单测 100% 全绿**（新增 `audio_stream_copy_demux_probe` / `packet_read` / `muxer_roundtrip` / `smart_geometry_portrait_and_pillarbox`——即今日 `tests/unit/test_audio_stream_copy.cpp` 四用例前身）。

### ② 至今存活生效的架构遗产与核心代码

- 音轨零拷贝透传体系：`AudioTrackInfo` RAII 克隆、muxer `writeAudioPacket`、`av_packet_rescale_ts` 严格时间基换算、`av_interleaved_write_frame` 物理交织、音频暂存队列消解启动期时序竞争。
- 智能几何引擎：竖屏 4K 直出（2160×3840，双向 3.0×）、Pillarbox 居中、消色差 Studio Black（`Y=0x1000 / UV=0x8000` 的 `clear_p010_black`——杜绝荧光绿）。
- SDR Blit 计算着色器管线：`src/shim/ngx_bridge/sdr_blit_cs.h` / `sdr_blit_rs.h`（至今被 `ngx_d3d12_bridge.cpp` 直接引用）与 `mid_` 中间纹理 `needs_vsr_` 路由修复。
- `tools/verify_transcode_quality.py` 逐像素画质审计（暗部极小值 / 过曝率 / 绿偏比三大量化指标）。
- `tools/package_release.py` 绿色自包含发布包流水线（SHA256SUMS + README.txt）。
- CLI `\r` 单行进度条（百分比 + 帧数 + 瞬时 FPS）。

### ③ 已被彻底废除的历史过渡设计

- P010 显存全 0 初始化（YUV 全零呈荧光绿）——Studio Swing 黑电平显存清空取代。
- VSR 输出强行绑定 RGBA16F（驱动层不可恢复崩溃 `0xBAD0000E`）——`mid_` R8G8B8A8 中间纹理路由取代。
- GUI 硬编码 `3840×2160` 击穿智能几何防线——中枢层自动纠正防线取代。
- “极速转码 / AI 重制”双模式 GUI 选择——单一重制契约取代。

### ④ 关键排障经验与硬件踩坑记录

- 竖屏畸变根因：固定目标分辨率忽略 SAR/DAR（水平 5.33× vs 垂直 1.69× = 3.16 倍畸变）。
- muxer 必须在编码器创建后立即开启，否则音频包无限堆积、次要音轨静默丢弃。
- D3D12 RootSignature/描述符堆的 COM GUID 硬编码错误导致 `CreateRootSignature` 失败。
- 像素级绿偏比审计是比肉眼更严的色彩回归武器（误差 0.004 的量化判定）。
- 双端参数不同步教训：CLI 演进为智能几何后 GUI 仍硬编码旧默认值——单一 SSOT 的立项动因（Phase 6/8 落地）。

## Phase 6 架构全面瘦身与专业级产品化（四波次，2026-11-09~11-10，收官终审 9.6/10）

### ① 阶段核心目标与里程碑判定

- **核心目标**：13 项委托议题、全仓 49,042 行暴力精简至 <15,000 行（核心 C++ ~7,200 行）。
- **四波次实测**：
  - W1 构建治理与死代码切除：净减 **29,739 行**（源码树 925 KB 内嵌 PTX 头 26,782 行改构建期生成；未接线插帧/ONNX/手写哈希死代码；`.ilk/.pdb` 污染归零；82 项单测全绿）；
  - W2 C++26 纯化：删除 C 风格 `types.cpp` 与零引用死类，两段式初始化静态扫描归零，100% RAII（79 项单测全绿）；
  - W3 AV1 默认 + 原生参数矩阵 UI：`rtx_gui.cpp` 1,950→736 行，13 项参数原生控件双向绑定，`Ctrl+L` 热切，默认 AV1 10-bit（RTX 5060 端到端实测成功）；
  - W4 全案文档对齐：36 篇历史文档 `[HISTORICAL-SUPERSEDED]` 归档印记、双语 README 物理分离、绿色包 34.35 MB。
- **收官终审**：阶段七裁决报告 **9.6/10 批准收官**，签发 Phase 1~6 全系统整体验收合格证书，附 4 处非阻断微瑕收口补丁。
- **GUI 演进脉络（`docs/gui/` 归档要旨）**：GDI 手绘方案以 Direct2D 首帧 178 ms/40 MB vs GDI 54 ms/15 MB 的量化基准立项；其“零子 HWND 教条”与 DirectComposition 预览在后续阶段被系统性推翻——禁止 Qt/Electron/CEF/WPF/MFC 的轻量原生路线与 NVIDIA 绿强调色则延续至今。

### ② 至今存活生效的架构遗产与核心代码

- Win32 Common Controls v6 原生暗黑交互矩阵：`DarkMode_Explorer` 主题 + `WM_CTLCOLOR*` 深色画刷 + `DWMWA_USE_IMMERSIVE_DARK_MODE` 标题栏；NVIDIA Signature Green `#76B900` 强调色；Per-Monitor V2 DPI。
- 100% RAII：私有构造 + `static Result<T> create(...)` 静态工厂 + 析构自动释放（唯一受控特例 = `rtx_ngx_bridge.dll` 的 `extern "C"` 导出边界）。
- PTX/着色器构建期生成至 `${CMAKE_BINARY_DIR}/generated`（“编译产物永不入库”）。
- `/INCREMENTAL:NO` + PDB 重定向 `symbols/` + Staging 校验三件套。
- AV1 默认解锁五处硬编码拦截点 + sm_89 启动自检无感回退 HEVC（`hw_transcode.cpp`）。
- `/arch:AVX2` 全局注入并以 VS 实际汇编为唯一裁决标准（`vmovdqu ymm` / `vpaddd ymm` / `vpshufb` 实证）。
- 34.35 MB 绿色发布包（4 个核心二进制 + SHA-256 清单）。
- `rtx::trW`/`StrId` 中枢 + 启动语言自检 + `Ctrl+L` 全控件原地热切换。

### ③ 已被彻底废除的历史过渡设计

- 零子 HWND 纯 GDI 手绘教条（1,073 行仅 3 个点击热区的反面教材）。
- DirectComposition 视口（`dcomp_viewport.*` 物理删除——转码工具无需内嵌重型播放器）。
- 未接线 RIFE/FRUC/ONNX 死代码与手写 Sha256（`rife.h`、fruc 桥接 ABI、`src/shim/backends/` 全目录、`--build-rife` 孤立开关）。
- 源码树内嵌 PTX/CSO 头文件（925 KB 自动生成物入库污染）。
- 两段式初始化教条与单实现虚接口。
- `PingPongRing` 零引用死类；`SmartGeometryCalculator` 静态伪类（退化为命名空间自由函数）。
- `RTX_GUI_RESULT_LOG` / `RTX_GUI_STARTUP_LOG` 环境变量与磁盘日志落盘（改 `OutputDebugStringW` DebugView 流）。
- 打包脚本捆绑未接线 TensorRT 巨型 DLL 时代（1.3 GB）。

### ④ 关键排障经验与硬件踩坑记录

- `.ilk/.pdb` 污染根因 = Ninja 单配置默认 RelWithDebInfo + RUNTIME_OUTPUT_DIRECTORY 指向插件目录 + MSVC 默认增量链接——三件套修复。
- MSVC 19.51 `/FAcs` 汇编实证是向量化金标准：`std::ranges::fill/copy`、`std::byteswap` 100% 生成 YMM 指令——严禁臆想向量化回退而造轮子。
- 跨代硬件能力差异：Turing/Ampere 支持 NGX 但物理无 AV1 NVENC 单元；第八代 NVENC（Ada sm_89 / Blackwell sm_120）才支持 AV1 10-bit——`nvEncGetEncodeGUIDCount` 探测 + 计算能力判定自动回退。
- 回退预案纪律：Wave 1 删除仅作用于未暂存工作区（零 commit），可 `git restore` 秒级回滚。
- 验收门禁量化红线：测试运行 ≤500 ms、构建 0 警告 0 错误、竖屏 4K 尺寸严格 2160×3840。

## Phase 7 实时监看、色彩科学与多形态演进（Wave 1–4，2026-11-11 全要素实证）

### ① 阶段核心目标与里程碑判定

- **战略选型裁决**：“数据面定型优先于分布式扩缩”——单卡数据面（视口+色彩+批处理）未冻结前严禁扩多卡（3D LUT/HDR10+ 会改核函数签名与 C ABI 契约，先行扩多卡必双倍返工）。
- **Phase 6.1 微瑕收口（v0.2.1，≤40 行）**：GUI 峰值亮度/画幅模式漏采集、音频直通取消勾选仍写轨、VSR 启停未透传、残留死字段清理。
- **实时监看与色彩科学波次（v0.3.0）**：ABI v3 + 融合核函数（LUT/Warp 归约/预览旁路）+ 三槽异步流水线 + `.cube` 解析器 + HDR10+ + FLIP 预览视口；融合核函数同会话增量 +0.039 ms（≤0.05 预算）；主循环吞吐较 Phase 6 基线 ~4.9 倍；91 项单测全绿。
- **多 GPU 与长稳波次（v0.4.0）**：拓扑探测与设备绑定、GOP 分片加速比 ≥1.90×（跨卡仅传压缩码流）、ProRes 80.3 / DNxHR 86.1 FPS 真机验证、VFR 时间戳守卫、TDR 注入自愈、1000 组确定性 Fuzzing 零崩溃；EXE 3.11 MB。
- **Linux 纯 CUDA 与云原生波次（v1.0.0）**：纯 CUDA 无头后端（`dlopen("libcuda.so.1")` 全动态解析）、零平台条件编译、JSON-RPC 微服务、Distroless 容器方案；以交叉编译实证（本机无 Linux/Docker 门禁受限如实标注）。

### ② 至今存活生效的架构遗产与核心代码

- `705,600,000 Hz` Flicks 时基 + 每透传流独立单调时间戳守卫（`include/rtx/timestamp_guard.h`——24 h 虚拟流 100 处倒退 DTS 注入下累积漂移 = 0）。
- `.cube` 3D LUT 融合打包：`tex3D<float4>` 硬件三线性 FP16 纹理（`cuArray3DCreate + cuMemcpy3D` 上传）+ 块级单次采样；`include/rtx/lut3d.h` 100% RAII 解析器。
- HDR10+（SMPTE ST 2094-40）：T.35 HEVC 前缀 SEI 与 AV1 metadata OBU 每帧注入；Warp 归约帧亮度统计（`__shfl_xor_sync` 32-lane + `atomicMax/atomicAdd`）经 `rtx_ngx_bridge_set_frame_stats` 驱动自适应 TrueHDR。
- D3D12 FLIP 预览视口：`PreviewViewport`（`FLIP_DISCARD` + `R10G10B10A2_UNORM` 双缓冲 + `ALLOW_TEARING` + 遮挡跳帧 + 拖动 scrub seek 重制）——零 dcomp。
- 三槽 P010 环形缓冲（`ring[i % 3]`）+ 核心侧在途门禁（NVENC 与 NGX 跨引擎重叠）。
- 全音轨/字幕直通 + 提前开 muxer（消灭音频包堆积）。
- ngx_bridge 纯 C ABI 防火墙 append-only 演进（v3 → 今日 v4：NVOFA/RIFE 双路补帧、预览、帧统计导出）。
- `rtx_cuda_remaster_fused_p010` 单次 launch 融合核函数（Catmull-Rom/CAS 超分算子 + BT.2446 Method C 逆色调映射 + 3D LUT + P010 打包）。
- TDR 事务自愈：设备重建 + 250 ms 退避 + ≤3 次重试 + 关键帧回退 + LUT 重上传。

### ③ 已被彻底废除的历史过渡设计

- Linux Vulkan 互操作（伪命题——NVIDIA 无 Linux NGX Vulkan 二进制，NVDEC/NVENC/打包全为原生 CUDA）。
- gRPC + Protobuf 重型 RPC（>50 MB 静态库违反单 EXE 铁律）——零依赖 JSON-RPC 微服务。
- DComp 时代视口自测开关与节流头文件（架构级门禁承担）。
- 跨卡传输未压缩像素的流水线切分（4K P010 24.88 MB/帧 × 60 FPS ≈ 3 GB/s PCIe，收益 <10%）——文件级/GOP 分片零跨卡像素。
- 首帧编码前音频无限缓冲——提前开 muxer。
- 恒定帧率回退假设（VFR 手机录屏致长视频音画漂移）——Flicks 单调平滑器。
- 每像素 3D LUT 三线性采样（+1.09 ms，65³ 色彩空间无像素邻域局部性）——块级单采样（+0.034 ms）。

### ④ 关键排障经验与硬件踩坑记录

- 无 nvcc 环境用 NVRTC wheel 重生成 PTX（`--arch compute_75` 产出多入口 PTX，`cuModuleLoadData` 运行时载入）。
- 核函数耗时门禁必须用同会话 A/B 增量判据——3 槽流水线重叠 + 笔记本 GPU 时钟状态使绝对值失真（0.21 ms → ~1.27 ms 底噪对基线同等生效）。
- 3D LUT 局部性陷阱：每像素 tex3D 三线性 +1.09 ms → 块级单采样 +0.034 ms。
- MP4/MKV 容器头 open 时写死 vs 全片实测 CLL finalize 才有的折中——首帧实测 CLL 提前注入容器头、全片值以 report JSON 承载。
- WDDM 驱动重置需 250 ms 退避等待；`--fault-inject-frame` 故障注入钩子支撑 TDR 自愈实证。
- Fuzzing 工程化：1000 组位翻转/截断注入、确定性种子、≤16 突变/样本、全部结构化 Status 返回、0 崩溃 0 泄漏。
- SSH 无头会话无法做交互式 GUI 验证——交互门禁必须回到真实桌面会话执行（阶段二/三沿用此结论）。
- 存量第三方警告基线（`-Wcast-function-type-mismatch` 非阻断）与构建基线一致，不判 FAIL。

---

## 废除设计黑名单（防回潮总表）

以下历史设计已被彻底废除，**严禁在任何现行代码或新设计中回潮**；本表为阶段三门禁 3.2/3.3 的登记底账（本档案为唯一允许保留这些历史标识符的存档位置）：

| 废除设计 | 原役种 | 废除原因 | 替代物 |
| :--- | :--- | :--- | :--- |
| `CpuReferenceBackend` | CPU 模拟转码后端 | 空心化根源，违背纯 GPU 零拷贝第一原则 | 真实 NVDEC/NVENC 硬件链路 |
| `SurfacePool`（宿主模拟形态及其独立模块） | 显存池类 | 双轨割裂；显存池模块整体退场 | NVDEC 显示几何池 + 桥接三槽 P010 环 |
| `BackpressureController`（独立水印类） | 背压水位机 | 与在途门禁/环形缓冲职责重叠，过度设计 | 核心侧 `remasters_issued − encodes_completed < 3` 门禁 |
| `offruc_bridge`（独立 OFFRUC 桥接模块） | 补帧桥 | 未接线死代码；双路补帧统一收编 | `rtx_ngx_bridge.dll` 内 NVOFA 路线 A |
| `--sdr`（SDR 直通独立开关） | CLI 幽灵开关 | 违反零开关铁律；SDR 直通缺陷已根治 | 缺省确定性产品路径（`--peak-nits` 等真实参数） |
| `RTX_WITH_FFMPEG` / `RTX_HAVE_FFMPEG` | 条件编译宏 | 宪章级违宪 | FFmpeg 无条件静态内联 |
| `RTX_GUI_RESULT_LOG` / `RTX_GUI_STARTUP_LOG` | 环境变量开关 | 磁盘日志落盘 + 隐蔽分支 | `OutputDebugStringW` + DebugView `[CineRTX]` |
| `--trace <file>`（Chrome 追踪输出） | CLI 幽灵开关 | 计时事件毛刺与死代码 | DebugView 日志流 |
| `rtx-transcode.exe` / `rtx-transcode-base.exe` | 冗余 EXE 靶标 | 多靶标碎片化 | 唯一 `CineRTX.exe`（GUI/CLI 双模） |
| `dcomp_viewport`（DirectComposition 视口） | 视口方案 | 覆盖干扰子窗口重绘，重型依赖 | 子 HWND + `FLIP_DISCARD` 预览视口 |
| `TraceRecorder`（Chrome 追踪器） | 追踪类 | 死代码；诊断流已统一 | `[CineRTX]` DebugView 流 |
| `ResolutionGuard`（几何突变守卫类） | 守卫类 | 职责并入桥接复位与自适应几何 | `rtx_ngx_bridge_reset` + 智能几何防线 |
| `defaultPoolSpecs()`（静态显存池规格表） | 显存规划 | 显存池模块整体退场 | 会话创建期确定性设备分配 |
| 手写 Sha256 / `SmallExpected` / `PingPongRing` | 自造轮子 | 标准库可零成本替代 | `BCryptHash` / `std::expected` / 三槽环形 |

---

## 归档清算对账

- 本档案替代并吸收：`docs/phase1/`（4 篇）、`docs/phase2/`（17 篇）、`docs/phase3/`（8 篇）、`docs/phase4/`（3 篇）、`docs/phase5/`（4 篇）、`docs/phase6/`（5 篇）、`docs/phase7/`（6 篇）、`docs/handover/`（1 篇）、`docs/gui/`（3 篇）共 **51 篇**历史阶段文档；
- 以及 `docs/TODO/` 全部 **11 篇**历史分册（音频透传、双端同步与 AV1 默认、FFmpeg 7.1.1 选型、GUI 日志消解、i18n 架构、打包分发闭环、阶段五路线图、阶段六主待办、RIFE 测试夹具、智能几何、UI 重构与插件状态）；
- 各篇原文头部均带 `[HISTORICAL-SUPERSEDED]` 归档印记，其结论已按上文章节沉淀；数字口径以各阶段终审实测为准，历史记录不构成现行契约。
