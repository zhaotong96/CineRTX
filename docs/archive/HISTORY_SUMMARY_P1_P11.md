# CineRTX Phase 1–11 权威演进摘要（归档至 Phase 11 收口）

> **档案编号**：`ARCHIVE-HISTORY-SUMMARY-P1-P11-001`
> **编撰日期**：2026-11-24（Phase 12 · Wave 12.4 收官归档，依据 `CINERTX-P12-RULING-064` §3.2）
> **档案定位**：在完整保留 [`HISTORY_SUMMARY_P1_P10.md`](HISTORY_SUMMARY_P1_P10.md) 全部 Phase 1–10 历史记录基础上，增补 **Phase 11（v0.3.0）** 全程核心里程碑、存活架构遗产、反模式治理与转向 Phase 12 的架构动因，供全生命周期溯源。
> **现行契约**：一律以根目录 `README.md` / `README_zh.md` 与 `docs/spec/` 为准；Phase 12 终审卷宗见 `docs/phase12/`。

---

## 一、 Phase 1–11 里程碑总表

| 阶段 | 版本 | 终审日期 | 核心里程碑 | 单测基线 |
| :--- | :--- | :--- | :--- | :--- |
| P1 | — | 2026-10-21 | NVDEC→显存→NVENC 零拷贝裸转码闭环；`D2H=0 / H2D=0` | 101 |
| P2 | — | 2026-10-25 | D3D12↔CUDA 外部显存互操作 + NGX VSR/TrueHDR + P010 打包；1080p→4K HDR10 **128.7 FPS** | — |
| P3 | v0.2.0 | 2026-11-01 | FFmpeg 全量静态内联、单 EXE 绝对自包含；Flicks 全局时基立项 | 137 |
| P4 | — | 2026-10-31 | Modern C++26 全栈重构、`Result<T>` 类型契约、全仓反 Slop | — |
| P5 | — | 2026-11-08 | 音轨零拷贝透传、竖屏智能几何、SDR/HDR 双模合规 | 93 |
| P6 | — | 2026-11-10 | 49,042 行 → <15,000 行瘦身；AV1 10-bit 默认；原生参数矩阵 UI；绿色包 34.35 MB | 79 |
| P7 | v0.2.1 | 2026-11-11 | 3D LUT 融合核函数、HDR10+、FLIP 预览视口、多 GPU GOP 分片、TDR 自愈 | 91 |
| P8 | — | 2026-11-14 | 双路真补帧（NVOFA + TensorRT RIFE）落地；三端统一 SSOT；Win32 "3+1" 四态工作站 | 73 |
| P9 / P9.1 | v0.1.0 GM | 2026-11-15 | 真权重 RIFE v4.25 120fps 全链路根治；1:5 重复帧顽疾根除（`longest_frozen_run = 0`） | 75 |
| P10 | v0.2.0 | 2026-11-18 | 五大阻断断路抢修；20 项 SSOT 全模态码控；VSR-First 拓扑；WebUI v3；异步软中断 | 78 |
| **P11** | **v0.3.0** | **2026-11-22** | **Wave 0 暗礁根治 + 五域异构 RAII + DirectComposition 零回读视口 + Flicks 705.6 MHz + RIFE `scale=0.5` A=128 + 7,300 MiB 显存滞后调度器 + SDK 13.1 多缓冲池** | **128+** |

> Phase 1–10 逐阶段详情（核心遗产、反模式黑名单、硬件踩坑总账）完整留存于 [`HISTORY_SUMMARY_P1_P10.md`](HISTORY_SUMMARY_P1_P10.md) 与 [`Phase1_to_Phase7_Evolution_Digest.md`](Phase1_to_Phase7_Evolution_Digest.md)。

---

## 二、 Phase 11（v0.3.0）核心演进与存活遗产

### 1. Wave 0 止血与纯净重启
- 无损封存早期尝试至分支 `archive/phase11-attempt-1`（`e04429a`），主干重置回纯净 `v0.2.0`（`52103a7`）。
- 根治 RIFE `rife_four_phase_monotonic_progression` 偶发失败：`rife_builder.cpp` 清除 `BuilderFlag::kTF32` 并锁定 5 类敏感层 FP32 精度，`rife_pack_runtime.cpp` 接入异步 `memset_d32_async` 清零 Block SAD 显存。
- 落地 `tests/fixtures/` 物理 `.mkv` 测试夹具，清理 `cmake/RtxFfmpegStatic.cmake` 无效 CPU 编码器开关。

### 2. Phase 11.1 — 异构资源生命周期与 C++26 反射过渡契约
- 确立严格编译链事实：MSVC 2026 预览版尚无 C++26 P2996/P3394 静态反射实现，改用零重复成员指针描述符表维持 20 项 SSOT 单源契约，同时预留 P2996 适配缝隙。
- 重排 `NvidiaTranscodeSession` 成员声明逆序以实现 `= default` 析构；根除 `RemasterPipeline` 在 `cuMemFreeHost` 缺失时误调 `std::free()` 的堆损坏隐患；引入 `GpuRetirementQueue` 延迟退役队列与百次失败注入单测 `tests/test_raii_retirement.cpp`。

### 3. Phase 11.2 / 11.3 — 实时视听播控与 NLE 剪辑合龙
- 落地动态 `dcomp.dll` 合成视口（DirectComposition 零主机像素回读 `D2H == 0 B`），配合 WASAPI 独占/共享音频输出与 `AUDCLNT_E_DEVICE_INVALIDATED` 自动恢复。
- 刚性锁定原生 `705,600,000 Hz Flicks` 无损时基（否决 14.112 MHz 降采样失真提议），落地 GOP-Aware 帧精确 Seek、NLE 四轨剪辑与 AVX2 零堆分配混音热循环。

### 4. Phase 11.4 — RIFE 高阶算子、显存滞后调度与 SDK 13.1 多缓冲池
- RIFE `scale=0.5` 升级 `A=128` 对齐模数与 `reflect101` 镜像填充核函数，同步更新保守 PTX ISA 产物，消除 4K/竖屏边缘黑边伪影。
- 基于 WDDM 8 GB 显卡实际 `7,300 MiB` DXGI 预算建立显存滞后调度器（82% = `5,986 MiB` 降级高水位、68% = `4,964 MiB` 恢复低水位、120 帧最小驻留防抖）。
- `nv_encoder.cpp` 升级为 `std::vector<BitstreamSlot>` 多缓冲环形池与完整 EOS 排空循环，支持 Video Codec SDK 13.1 运行时双模协商与分层 B 帧。

### 5. Phase 11.2-R 反 Slop 熔断与 Phase 12（v0.4.0）立项动因
- Phase 11.2 期间全仓源码一度膨胀至 43,450 行，且手写 `raii_owners.h` 等大量机械样板包装器引发用户严厉熔断裁决——确立**“标准库先验替换律（Std-First）”**与**“自定义具名类型零容忍（≤ 35 个且须显式 `@rtx-type-justification`）”**。
- 该架构裁决直接催生 **Phase 12（v0.4.0）极限现代 C++26 重构与无头 DLL 净空工程**，最终实现全仓具名类型 `34/35` 硬门禁、核心无头引擎 `~12.2K Pure SLOC`（≤ 23,000 硬阻断）与双形态极简静态 IAT（无头 `rtx_headless_cli.exe` 2 项物理 DLL / 桌面 `CineRTX.exe` 4 项物理 DLL）。
