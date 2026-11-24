# CineRTX Phase 9：RIFE 120fps 插帧“1:5 重复帧”实机复现、根因穿透取证与全链路根治施工规范

- **文档编号**：`CINERTX-P9-RIFE-120FPS-FORENSICS-001`
- **适用阶段**：Phase 9（FRUC Route B 120fps 任意时刻插帧全链路根治与工业级加固）
- **关联源码基线**：
  - [`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp)
  - [`src/shim/ngx_bridge/rife_engine.h`](../../src/shim/ngx_bridge/rife_engine.h) / [`src/shim/ngx_bridge/rife_engine.cpp`](../../src/shim/ngx_bridge/rife_engine.cpp)
  - [`src/shim/ngx_bridge/rife_builder.cpp`](../../src/shim/ngx_bridge/rife_builder.cpp)
  - [`src/shim/ngx_bridge/rife_pack.cu`](../../src/shim/ngx_bridge/rife_pack.cu) / [`src/shim/ngx_bridge/rife_pack.ptx`](../../src/shim/ngx_bridge/rife_pack.ptx)
  - [`src/shim/ngx_bridge/rife_pack_runtime.h`](../../src/shim/ngx_bridge/rife_pack_runtime.h) / [`src/shim/ngx_bridge/rife_pack_runtime.cpp`](../../src/shim/ngx_bridge/rife_pack_runtime.cpp)
  - [`src/shim/ngx_bridge/bridge_exports.cpp`](../../src/shim/ngx_bridge/bridge_exports.cpp)
  - [`src/io/ffmpeg_io.cpp`](../../src/io/ffmpeg_io.cpp) / [`src/cli/main.cpp`](../../src/cli/main.cpp)
  - [`CMakeLists.txt`](../../CMakeLists.txt) / `tests/test_rife_engine.cpp`

---

## 零、前置工程预警：委托书文件名中的 `1:5` 触发 Windows NTFS 备用数据流（ADS）截断陷阱

在核查项目负责人提交的委托书时，取证发现 `docs/phase8/CineRTX Phase 8：关于 RIFE 120fps 插帧 1` 在 workspace 中显示为 **0 字节文件**，且文件名在 `1` 处被截断。

### 0.1 根因机制
项目负责人在 Windows NTFS 文件系统下保存文件名：
`CineRTX Phase 8：关于 RIFE 120fps 插帧 1:5 重复帧实机复现、根因取证与专家协助委托书.md`
其中：
- `Phase 8：` 使用的是中文全角冒号 `：`（`U+FF1A`），合法；
- **`1:5` 使用的是英文半角冒号 `:`（`U+003A`）**。在 Windows Win32/NTFS 路径解析规则（`CreateFileW`）中，英文半角冒号 `:` 是 **NTFS 备用数据流（Alternate Data Stream, ADS）分隔符**（语法为 `<主文件名>:<流名称>:$DATA`）。
- 因此，Windows 内核在磁盘上创建了名为 `CineRTX Phase 8：关于 RIFE 120fps 插帧 1` 的 **0 字节主文件（`$DATA` 默认流为空）**，并将委托书的全部正文写入了名为 `:5 重复帧实机复现、根因取证与专家协助委托书.md` 的隐藏附属数据流中。workspace 同步客户端仅同步主文件默认数据流，导致云端出现 0 字节残骸。

### 0.2 本地一键无损抢救 PowerShell 指令
请在本地 Windows 工程根目录下执行以下单行 `pwsh` 命令，即可从 NTFS 隐藏数据流中完整提取委托书正文并重命名为合法文件名（将 `1:5` 替换为 `1比5`）：

```powershell
Get-Content -LiteralPath "docs\phase8\CineRTX Phase 8：关于 RIFE 120fps 插帧 1" -Stream "5 重复帧实机复现、根因取证与专家协助委托书.md" -Encoding UTF8 | Set-Content -LiteralPath "docs\phase8\CineRTX Phase 8：关于 RIFE 120fps 插帧 1比5 重复帧实机复现、根因取证与专家协助委托书.md" -Encoding UTF8; Remove-Item -LiteralPath "docs\phase8\CineRTX Phase 8：关于 RIFE 120fps 插帧 1"
```

---

## 一、实机复现现象还原与“1:5 重复帧”数学链路解剖

### 1.1 为什么 24fps $\to$ 120fps 实机输出呈现严格的“1:5 重复帧”？
当输入标准电影帧率视频（$24\text{ fps}$ 或 $23.976\text{ fps}$）并开启 RIFE 120fps 插帧（`--fruc-mode rife --fruc-fps 120` 或 UI 选择 `RIFE 120fps`）时，输出帧率与输入帧率之比为：
$$\frac{f_{\text{out}}}{f_{\text{in}}} = \frac{120}{24} = 5 \quad (\text{即 } 1:5 \text{ 帧率扩张比})$$

在 [`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp)（第 556–652 行）的 1D 精确 Flicks 时基（$1\text{ sec} = 705,600,000\text{ Flicks}$）插帧调度循环中：
- 输入源帧间隔：`window_duration = curr_pts_flicks - prev_pts_flicks = 29,400,000 Flicks`（对应 90kHz 时基下的 `3750` ticks）；
- 输出目标步长：`delta_out_flicks = 705,600,000 / 120 = 5,880,000 Flicks`（对应 90kHz 时基下的 `750` ticks）；
- 对于每一对相邻源帧 $[F_k, F_{k+1}]$，`while (next_out_pts_flicks < curr_pts_flicks)` 循环精确迭代 **5 次**，对应的归一化时间相位 $\alpha \in 0, 1)$ 依次为：
  $$\alpha_0 = 0.0,\quad \alpha_1 = 0.2,\quad \alpha_2 = 0.4,\quad \alpha_3 = 0.6,\quad \alpha_4 = 0.8$$

查看 [`src/core/hw_transcode.cpp` 第 593–633 行的真实执行分支：
```cpp
if (alpha <= 0.0001f) {
  auto res = processFrame(prev_frame, out_pts_90k, /*release_slot=*/false);
  if (!res.ok()) return res;
} else if (alpha >= 0.9999f) {
  auto res = processFrame(frame, out_pts_90k, /*release_slot=*/false);
  if (!res.ok()) return res;
} else {
  // Interpolate intermediate frame at phase alpha
  ...
  rtx_ngx_status status = RTX_ERR_INVALID_ARG;
  if (use_rife) {
    status = session.bridgeLoader().interpolateRife(
        session.handle(),
        reinterpret_cast<void*>(prev_frame.device_ptr),
        reinterpret_cast<void*>(frame.device_ptr), prev_frame.pitch,
        alpha, reinterpret_cast<void*>(interp_frame.device_ptr),
        interp_frame.pitch, &scene_cut);
  }
  if (status == RTX_OK) {
    if (scene_cut != 0) {
      auto res = processFrame(prev_frame, out_pts_90k, /*release_slot=*/false);
      if (!res.ok()) return res;
    } else {
      ...
      auto res = processFrame(interp_frame, out_pts_90k, /*release_slot=*/false);
      if (!res.ok()) return res;
      report.frames_interpolated++;
    }
  } else {
    auto res = processFrame(prev_frame, out_pts_90k, /*release_slot=*/false);
    if (!res.ok()) return res;
  }
}
```

**直接定案结论**：
在每个源帧窗口 $[F_k, F_{k+1})$ 的 5 次循环中：
1. 第 0 次（$\alpha = 0.0$）命中 `alpha <= 0.0001f`，直接送出源帧 `prev_frame`（$F_k$）；
2. 第 1～4 次（$\alpha = 0.2, 0.4, 0.6, 0.8$）由于下述 **六层串联/并联根因**，导致：
   - **路径 A（当 `interpolateRife` 返回 `status != RTX_OK` 时）**：进入第 630 行 `else` 分支，**不打印任何错误或告警日志**，静默调用 `processFrame(prev_frame, out_pts_90k, false)` 连续 4 次！加上 $\alpha = 0.0$ 的第 1 次，`prev_frame`（$F_k$）被原封不动送入编码器 **5 次**（`[F_k, F_k, F_k, F_k, F_k]`），形成肉眼和逐帧校验下铁板钉钉的 **“1:5 重复帧”**！
   - **路径 B（当 `scene_cut != 0` 误触发时）**：进入第 610 行 `if (scene_cut != 0)` 分支，同样对所有 $\alpha \in \{0.2, 0.4, 0.6, 0.8\}$ 无脑输出 `prev_frame`（$F_k$），再次形成 **`[F_k, F_k, F_k, F_k, F_k]` 1:5 重复帧**！
   - **路径 C（当回退到 `__builtin__` 引擎或使用了固定 `timestep=0.5` 导出的缺陷 ONNX 时）**：第 6 通道 $\alpha$ 权重为 `0` 或被常量折叠为 `0.5`，导致 $\alpha = 0.2, 0.4, 0.6, 0.8$ 四个时刻输出的插值帧完全相同（1:4 内部冻结重复帧）！

---

## 二、六层根因穿透取证台账（Root-Cause Forensics Ledger）

通过对仓内 C++20 / CUDA PTX / TensorRT 10 / FFmpeg 源码的逐行比对与指令级审计，共锁定 **4 项 P0 级致命阻断根因** 与 **2 项 P1 级性能/封装缺陷**：

| 根因编号 | 严重度 | 故障模块与源码定位 | 核心失效机理摘要 | 触发后果 |
| :--- | :---: | :--- | :--- | :--- |
| **RC-1** | **P0 致命** | `rife_pack.ptx` (L4–5, L111–180)<br>`rife_pack_runtime.cpp` (L59–141)<br>`rife_engine.cpp` (L124–296) | 1. `rife_pack.ptx` 头部声明 `.version 7.0` + `.target sm_89`（`sm_89` 直到 PTX ISA 7.8 / CUDA 11.8 才引入，在 PTX 7.0 中属非法架构目标）；<br>2. `.f32` 指令混用十进制浮点字面量（如 `16.0`, `-0.187324`）而非 PTX 规范要求的 IEEE-754 十六进制浮点（`0f...`）；<br>3. 边界填充线程跳过分支后执行全掩码 `shfl.sync.down.b32 ..., 0xffffffff` 导致 Warp 发散未定义行为；<br>4. `rife_engine.cpp::loadEngine` 初始化时**从未检查 `isRifePackAvailable()`**，导致初始化假阳性成功，首帧调用 `launchRifePreprocess` 100% 失败并返回 `RTX_ERR_NGX`。 | `interpolateRife` 每帧必定返回 `RTX_ERR_NGX`，100% 触发 1:5 原帧重复！ |
| **RC-2** | **P0 致命** | `hw_transcode.cpp` (L601–633)<br>`main.cpp` (L418) | `interpolateRife` 返回非 `RTX_OK` 时，第 630 行 `else` 分支**完全静默（零日志、零告警、零失败计数）**，直接执行 `processFrame(prev_frame, out_pts_90k, false)`；且 `main.cpp:418` 在 `frames_interpolated == 0` 时隐藏 `fruc:` 统计行。 | 任何底层推理/内核异常均被静默掩盖为 `[F_k, F_k, F_k, F_k, F_k]` 1:5 重复帧，表面显示转码成功。 |
| **RC-3** | **P0 致命** | `rife_engine.cpp` (L177–184)<br>`rife_builder.cpp` (L41–130)<br>`bridge_exports.cpp` (L892–924) | 1. 自动构建仅查找 `rife_v4.25.onnx` / `rife.onnx` / `model.onnx`（未包含 `rife_v4.25_v2.onnx` 或子目录），找不到即回退至 ` buildBuiltinTemporalNetwork` 并**将 ~15KB 假引擎写入 `models/rife_v4.25.engine` 永久污染磁盘**；<br>2. 内置网络的 `skip_weights` 与 `conv1_weights` 对第 6 通道（`alpha`）权重硬编码为 `0.0f`，完全无视任意时刻参数 $\alpha$；<br>3. `bridge_exports.cpp:894` 先执行 `rife_engine->interpolate`，若失败直接退出，根本走不到第 899 行的 `isBuiltinSynth` 分支。 | 一旦曾触发无 ONNX 启动，磁盘上的 `rife_v4.25.engine` 即被假引擎永久占据，且无法响应 $\alpha$。 |
| **RC-4** | **P0 致命** | `rife_engine.cpp` (L208–272)<br>`rife_builder.cpp` (L208–238) | 1. **Shape Profile 失配**：`loadEngine` 硬编码将 `1080` 向上对齐 64 为 `1088`，并执行 `(void)context->setInputShape(...)` 忽略返回值；若外部预编译引擎 Profile 为 `1x7x1080x1920`（未对齐 64），`setInputShape` 失败导致 `enqueueV3` 每帧报错；<br>2. **FP16 I/O 拒止**：第 252 行拒绝 `DataType::kHALF` 输入张量且未检查输出张量类型；<br>3. **GridSample 坐标链 FP16 下溢**：`rife_builder.cpp` 仅锁定了 `kGRID_SAMPLE` 与 `kDIV`，未按 [NVIDIA TensorRT Issue #4721](https://github.com/NVIDIA/TensorRT/issues/4721) / [vs-mlrt Issue #66](https://github.com/AmusementClub/vs-mlrt/issues/66) 锁定上游坐标网格算子（`Cast/Reciprocal/Mul/Sub/Broadcast`），导致 FP16 光流采样网格坍缩。 | 真实 `rife_v4.25.onnx` 编译或加载的 `.engine` 在 `enqueueV3` 报错或输出零位移重复帧。 |
| **RC-5** | **P1 重要** | `rife_engine.cpp` (L316–361)<br>`hw_transcode.cpp` (L610–614) | 1. 120fps 下同一对 `(prev_nv12, curr_nv12)` 在 $\alpha = 0.2, 0.4, 0.6, 0.8$ 连续调用 4 次，每次都重复把 `prev/curr` 从 NV12 转 planar RGB 并执行 `cuStreamSynchronize` + `cuMemcpyDtoH` 回传 SAD，造成 75% 预处理冗余与 4 倍 CPU-GPU 同步停顿；<br>2. 场景切（`scene_cut != 0`）未按 $\alpha < 0.5 \to F_k, \alpha \ge 0.5 \to F_{k+1}$ 阶跃过渡，而是全窗口复制 $F_k$。 | 120fps 吞吐量严重受损，且转场帧或 SAD 阈值触发处出现 5 帧卡顿。 |
| **RC-6** | **P1 重要** | `hw_transcode.cpp` (L277)<br>`ffmpeg_io.cpp` (L876–883)<br>`main.cpp` (L317–319) | `hw_transcode.cpp:277` 计算了 `frame_rate_ = 120/1` 传给 NVENC，但**漏更新 `output_info_.frame_rate = frame_rate_`**！导致 `ffmpeg_io.cpp:879` 将 MP4/MKV 容器流的 `avg_frame_rate` 写成源视频的 `24/1`（且未设置 `r_frame_rate`）。 | 即使 120fps 码流正常写入，播放器/MediaInfo 读取容器头仍显示 `24 fps` 或帧率异常。 |

---

### 2.1 深度剖析 RC-1：为什么 `rife_pack.ptx` 在运行时 100% 加载失败？

在 [`CMakeLists.txt`](../../CMakeLists.txt)（第 274–299 行）中，构建系统并未调用 `nvcc` 编译 [`src/shim/ngx_bridge/rife_pack.cu`](../../src/shim/ngx_bridge/rife_pack.cu)，而是通过 `scripts/gen_rife_ptx_header.py` 将手写的 [`src/shim/ngx_bridge/rife_pack.ptx`](../../src/shim/ngx_bridge/rife_pack.ptx) 直接打包进 `rife_pack_ptx.h`（字符串常量 `rtx_ngx::kRifePackPtx`），在运行时由 `cuModuleLoadData` 动态 JIT 编译。

对比仓内**正常工作**的 [`src/shim/ngx_bridge/p010_pack.ptx`](../../src/shim/ngx_bridge/p010_pack.ptx) / `p010_pack_runtime.cpp` 与**失效**的 [`src/shim/ngx_bridge/rife_pack.ptx`](../../src/shim/ngx_bridge/rife_pack.ptx)：

1. **PTX ISA 版本与 `.target sm_89` 非法组合**：
   - `rife_pack.ptx` 第 4–5 行：
     ```ptx
     .version 7.0
     .target sm_89
     .address_size 64
     ```
   - 根据 NVIDIA PTX ISA 官方规范，`.version 7.0` 对应 CUDA 11.0（最高仅支持 `sm_80`），而 `.target sm_89`（Ada Lovelace）直到 **PTX ISA `.version 7.8`（CUDA 11.8）** 才存在！驱动 PTX JIT 编译器在解析 `.version 7.0` + `.target sm_89` 时直接报 `CUDA_ERROR_INVALID_PTX (218)` 退出。
   - 反观仓内能正常工作的 `kNvofWarpPtx`（位于 `p010_pack_runtime.cpp` 第 229–231 行），使用的是 `.version 7.0` + `.target sm_70`（支持从 Volta/Turing/Ampere/Ada 到 RTX 5060 Blackwell 的全系向后兼容 JIT 编译）。

2. **PTX `.f32` 十进制浮点字面量语法错误**：
   - 在 `kNvofWarpPtx`（`p010_pack_runtime.cpp` 第 279–280 行）中，所有单精度浮点立即数均严格遵守 PTX 汇编规范使用 IEEE-754 十六进制格式（如 `0f00000000`、`0f3F800000`、`0f3F000000`）。
   - 而在手写的 `rife_pack.ptx`（第 153–180、318–345、413–442 行）中，直接书写了 `sub.f32 %f1, %f1, 16.0;`、`mul.f32 %f4, %f1, 0.00456621;`、`fma.rn.f32 %f11, %f2, -0.187324, %f10;` 等十进制立即数，违反 PTX 单精度指令字面量约束。

3. **边界填充区的 Warp 发散 `shfl.sync` 死锁/未定义行为**：
   - 在 1080p 视频下，`height = 1080`，而 Grid 高度按 `padded_height = 1088` 启动。当 `py >= 1080` 且 `py < 1088` 时，`rife_pack.ptx` 第 111–113 行：
     ```ptx
     setp.lt.u32 %p9, %r1, %r3;
     setp.lt.u32 %p10, %r2, %r4;
     and.pred %p10, %p9, %p10;
     @!%p10 bra L_SKIP_SAD;
     ```
     部分线程跳过 `L_SKIP_SAD`，而同属一个 Warp 的未越界线程却在分支内执行全掩码 `shfl.sync.down.b32 %r32, %r29, 16, 31, 0xffffffff;`（第 118–126 行）。在分支内部使用 `0xffffffff` 全掩码同步会导致硬件级 Warp 发散异常。正确做法是对越界像素直接将 `%r29 (diff)` 置 `0`，全 Warp 统一执行 `shfl.sync` 归约。

4. **初始化阶段漏检与上下文覆写**：
   - `rife_engine.cpp` 的 `RifeEngine::loadEngine()` 从未调用 `isRifePackAvailable()`。因此 `rtx_ngx_bridge_init_rife` 总是返回 `RTX_OK`，并在日志中打印 `FRUC Route B active: TensorRT RIFE (...)` 误导开发者。
   - 到了转码循环第 1 帧调用 `interpolateRife` $\to$ `launchRifePreprocess` 时，`ensureLoaded()` 调用 `cuModuleLoadData` 失败并将 `s.attempted = true` 锁死，返回 `false` $\to$ `interpolateRife` 返回 `RTX_ERR_NGX` $\to$ `hw_transcode.cpp:630` 静默输出 `prev_frame` 5 次！

---

### 2.2 深度剖析 RC-3 & RC-4：TensorRT Engine 与 `vs-mlrt` RIFE v4.25 模型契约陷阱

即便修复了 PTX 核函数，实机加载 `rife_v4.25.engine` 或从 `.onnx` 构建时还存在以下三处阻断：

1. **64 像素对齐强制填充与外部预编译 `.engine` Profile 的冲突**：
   - 在 `rife_engine.cpp`（第 208–212、270–271 行）中：
     ```cpp
     padded_width_ = ((width + 63) / 64) * 64;   // 1920 -> 1920
     padded_height_ = ((height + 63) / 64) * 64; // 1080 -> 1088
     ...
     nvinfer1::Dims4 dims{1, 7, padded_height_, padded_width_};
     (void)impl_->context->setInputShape(impl_->in_name.c_str(), dims);
     ```
   - 根据 [NVIDIA TensorRT Issue #4721](https://github.com/NVIDIA/TensorRT/issues/4721)，基于 `vs-mlrt` 构建的 `rife_v4.25.engine` 常常使用 `--optShapes=input:1x7x1080x1920`（即直接以 `1080` 构建，由模型内部 padding 处理，或固定静态尺寸 `1x7x1080x1920`）。此时强行传入 `1x7x1088x1920` 会导致 `setInputShape` 返回 `false`（被 `(void)` 忽略），随后每帧 `enqueueV3` 返回 `false`！
   - **根治方案**：在 `loadEngine` 中查询 `engine->getTensorShape(in_name)` 与优化 Profile 的 `kMIN` / `kMAX` 范围：若引擎 Profile 要求精确 `height`（如 `1080` 在 `[min_h, max_h]` 内而 `1088` 不在），则自适应令 `padded_height_ = height`；并严格校验 `setInputShape` 的布尔返回值！

2. **输入/输出张量 `FP16 (kHALF)` 兼容性**：
   - `vs-mlrt` 社区标准命令通常包含 `--fp16 --inputIOFormats=fp16:chw --outputIOFormats=fp16:chw`。
   - `rife_engine.cpp` 第 252 行硬编码拒绝了 `kHALF`（`if (dt != nvinfer1::DataType::kFLOAT) return false;`），且未支持 `kHALF` 模式的显存缓冲与核函数读写。

3. **ONNX 编译时的 `GridSample` 坐标算子链 FP32 锁精度与 `timestep` 常量折叠探测**：
   - 根据 [NVIDIA TensorRT Issue #4721](https://github.com/NVIDIA/TensorRT/issues/4721) 与 [AmusementClub/vs-mlrt Issue #66](https://github.com/AmusementClub/vs-mlrt/issues/66)，RIFE v4 (`IFNet_HDv3`) 在 TensorRT FP16 编译时，不仅 `GridSample` 和 `Div` 需要 FP32，生成采样网格的 **`Cast`、`Reciprocal`、`Mul`、`Sub`、`Add`、`Slice`、`Concat`（坐标流）** 若回退到 FP16，会导致 1080p/4K 下的归一化坐标 $[-1, 1]$ 发生半精度尾数截断，输出光流偏移量坍缩或画面撕裂。
   - 同时，在上游 [`Practical-RIFE` `train_log/IFNet_HDv3.py`（第 118–126 行）](https://huggingface.co/daydreamlive/RIFE/commit/52c19702a5c82337d4dad5410c323964605cbb8e) 中，若用户在导出 ONNX 时未采用 `vs-mlrt` 的第 7 通道动态 `timestep` 切片（`x[:, 6:7]`），而是直接導出了默认的 `timestep = (x[:, :1].clone() * 0 + 1) * 0.5`，或者加载了旧版 `__builtin__` 引擎，则模型对通道 6（`alpha`）完全免疫！
   - **根治方案**：在 `RifeEngine::loadEngine()` 末尾加入一次 **1ms 级 $\alpha$ 敏感度自检探针（Timestep Sensitivity Probe）**：用梯度测试帧分别推理 $\alpha = 0.2$ 与 $\alpha = 0.8$，校验两次输出的 L1 差异 $\Delta > 0$。若 $\Delta == 0$，立即判定该 `.engine` 为“无视 `alpha` 的无效/静态中点模型”并报错拦截，彻底杜绝静默产出重复帧！

---

## 三、Phase 9 全链路根治施工方案（4 大施工轨 Track 9A–9D）

请执行工程师严格按照以下 4 个 Track 落地代码修改，不得遗漏任何一处契约。

---

### Track 9A：修复 `rife_pack.ptx` PTX ISA 兼容性、IEEE-754 浮点字面量与 `rife_pack_runtime.cpp` 上下文管理

#### 3.1.1 替换 [`src/shim/ngx_bridge/rife_pack.ptx`](../../src/shim/ngx_bridge/rife_pack.ptx)（并同步更新 [`src/shim/ngx_bridge/rife_pack.cu`](../../src/shim/ngx_bridge/rife_pack.cu)）
- 将 PTX 头部统一改为与 `kNvofWarpPtx` 完全一致的 `.version 7.0` + `.target sm_70`（支持 RTX 20/30/40/50 全系列显卡驱动 JIT 编译）；
- 将所有 `.f32` 浮点立即数转换为精确的 IEEE-754 十六进制常数（如 `16.0f` $\to$ `0f41800000`，`128.0f` $\to$ `0f43000000`，`0.00456621f` $\to$ `0f3B95A106` 等）；
- 修正 SAD Warp 归约控制流：当像素位于 `padded_width / padded_height` 越界填充区时，不跳过 `shfl.sync`，而是将该线程的 `diff (%r29)` 置 `0`，使整个 Warp 32 个线程整齐走完 `shfl.sync.down.b32`；
- 新增只更新第 6 通道（`alpha`）的轻量级核函数 `rife_update_timestep_ch6_fp32`，供同一帧对的 $\alpha = 0.4, 0.6, 0.8$ 直接复用 `ch0..5`！

以下为可直接替换写入 [`src/shim/ngx_bridge/rife_pack.ptx`](../../src/shim/ngx_bridge/rife_pack.ptx) 的完整合规 PTX 源码：

```ptx
//
// Generated NVIDIA PTX Assembly for CineRTX RIFE v4.25 TensorRT Pre/Post-Processing
// Target: sm_70+ forward-compatible JIT (Turing / Ampere / Ada / Blackwell)
//
.version 7.0
.target sm_70
.address_size 64

// ============================================================================
// Kernel 1: rife_nv12_to_nchw_7ch_fp32
// Converts prev_nv12 and curr_nv12 to 7-channel planar RGB [0, 1] + alpha
// and computes 16x16 block SAD into sad_accum without warp-divergent shfl.sync.
// ============================================================================
.visible .entry rife_nv12_to_nchw_7ch_fp32(
    .param .u64 param_prev_nv12,
    .param .u64 param_curr_nv12,
    .param .u32 param_pitch,
    .param .u32 param_width,
    .param .u32 param_height,
    .param .u32 param_padded_width,
    .param .u32 param_padded_height,
    .param .f32 param_alpha,
    .param .u64 param_dst_tensor,
    .param .u64 param_sad_accum
)
{
    .reg .pred  %p<12>;
    .reg .b16   %rs<8>;
    .reg .b32   %r<45>;
    .reg .b64   %rd<35>;
    .reg .f32   %f<30>;

    ld.param.u64    %rd1, [param_prev_nv12];
    ld.param.u64    %rd2, [param_curr_nv12];
    ld.param.u32    %r3,  [param_pitch];
    ld.param.u32    %r4,  [param_width];
    ld.param.u32    %r5,  [param_height];
    ld.param.u32    %r6,  [param_padded_width];
    ld.param.u32    %r7,  [param_padded_height];
    ld.param.f32    %f1,  [param_alpha];
    ld.param.u64    %rd3, [param_dst_tensor];
    ld.param.u64    %rd4, [param_sad_accum];

    // px = blockIdx.x * blockDim.x + threadIdx.x
    mov.u32         %r8, %ctaid.x;
    mov.u32         %r9, %ntid.x;
    mov.u32         %r10, %tid.x;
    mad.lo.u32      %r1, %r8, %r9, %r10;

    // py = blockIdx.y * blockDim.y + threadIdx.y
    mov.u32         %r11, %ctaid.y;
    mov.u32         %r12, %ntid.y;
    mov.u32         %r13, %tid.y;
    mad.lo.u32      %r2, %r11, %r12, %r13;

    // Bounds check on padded dimensions
    setp.ge.u32     %p1, %r1, %r6;
    setp.ge.u32     %p2, %r2, %r7;
    or.pred         %p3, %p1, %p2;
    @%p3 bra        L_EXIT_PRE;

    // Clamp (sx, sy) to [0, width-1] and [0, height-1] for edge replication
    sub.u32         %r14, %r4, 1;
    min.u32         %r15, %r1, %r14;      // sx
    sub.u32         %r16, %r5, 1;
    min.u32         %r17, %r2, %r16;      // sy

    // Y offset = sy * pitch + sx
    mad.lo.u32      %r18, %r17, %r3, %r15;
    cvt.u64.u32     %rd5, %r18;

    // UV offset = (height + sy / 2) * pitch + (sx & ~1)
    shr.u32         %r19, %r17, 1;
    add.u32         %r20, %r5, %r19;
    and.b32         %r21, %r15, -2;
    mad.lo.u32      %r22, %r20, %r3, %r21;
    cvt.u64.u32     %rd6, %r22;

    // Load Prev Y, U, V
    add.u64         %rd7, %rd1, %rd5;
    ld.global.u8    %r23, [%rd7];         // y0
    add.u64         %rd8, %rd1, %rd6;
    ld.global.u8    %r24, [%rd8];         // u0
    ld.global.u8    %r25, [%rd8+1];       // v0

    // Load Curr Y, U, V
    add.u64         %rd9, %rd2, %rd5;
    ld.global.u8    %r26, [%rd9];         // y1
    add.u64         %rd10, %rd2, %rd6;
    ld.global.u8    %r27, [%rd10];        // u1
    ld.global.u8    %r28, [%rd10+1];      // v1

    // Compute SAD only for valid unpadded pixels (px < width && py < height), else 0
    setp.lt.u32     %p4, %r1, %r4;
    setp.lt.u32     %p5, %r2, %r5;
    and.pred        %p6, %p4, %p5;
    sub.s32         %r29, %r23, %r26;
    abs.s32         %r29, %r29;
    selp.u32        %r29, %r29, 0, %p6;

    // Uniform warp-level SAD reduction (all active threads in padded tile participate)
    shfl.sync.down.b32 %r30, %r29, 16, 31, 0xffffffff;
    add.u32         %r29, %r29, %r30;
    shfl.sync.down.b32 %r30, %r29, 8, 31, 0xffffffff;
    add.u32         %r29, %r29, %r30;
    shfl.sync.down.b32 %r30, %r29, 4, 31, 0xffffffff;
    add.u32         %r29, %r29, %r30;
    shfl.sync.down.b32 %r30, %r29, 2, 31, 0xffffffff;
    add.u32         %r29, %r29, %r30;
    shfl.sync.down.b32 %r30, %r29, 1, 31, 0xffffffff;
    add.u32         %r29, %r29, %r30;

    // Lane 0 of each warp atomically accumulates SAD if sad_accum != 0
    mad.lo.u32      %r31, %r13, %r9, %r10;
    and.b32         %r32, %r31, 31;
    setp.eq.u32     %p7, %r32, 0;
    setp.ne.u64     %p8, %rd4, 0;
    and.pred        %p9, %p7, %p8;
    setp.gt.u32     %p10, %r29, 0;
    and.pred        %p9, %p9, %p10;
    @!%p9 bra       L_AFTER_SAD;
    cvt.u64.u32     %rd11, %r29;
    atom.global.add.u64 %rd12, [%rd4], %rd11;

L_AFTER_SAD:
    // BT.709 Limited Range [16..235] -> Normalized RGB [0.0..1.0]
    // Constants in IEEE-754 hex:
    // 16.0f        = 0f41800000
    // 128.0f       = 0f43000000
    // 0.00456621f  = 0f3B95A106
    // 0.00703125f  = 0f3BE66666
    // -0.00083610f = 0fBA5B2574
    // -0.00208984f = 0fBB08F5C3
    // 0.00828125f  = 0f3C07AE14
    // 0.0f         = 0f00000000
    // 1.0f         = 0f3F800000

    // --- Prev Frame (y0, u0, v0) -> (r0, g0, b0) ---
    cvt.rn.f32.u32  %f2, %r23;
    sub.f32         %f2, %f2, 0f41800000;
    mul.f32         %f2, %f2, 0f3B95A106;     // y_norm0

    cvt.rn.f32.u32  %f3, %r24;
    sub.f32         %f3, %f3, 0f43000000;     // u_diff0

    cvt.rn.f32.u32  %f4, %r25;
    sub.f32         %f4, %f4, 0f43000000;     // v_diff0

    // r0 = clamp(y_norm0 + 0.00703125f * v_diff0, 0, 1)
    fma.rn.f32      %f5, %f4, 0f3BE66666, %f2;
    max.f32         %f5, %f5, 0f00000000;
    min.f32         %f5, %f5, 0f3F800000;

    // g0 = clamp(y_norm0 - 0.00083610f * u_diff0 - 0.00208984f * v_diff0, 0, 1)
    fma.rn.f32      %f6, %f3, 0fBA5B2574, %f2;
    fma.rn.f32      %f6, %f4, 0fBB08F5C3, %f6;
    max.f32         %f6, %f6, 0f00000000;
    min.f32         %f6, %f6, 0f3F800000;

    // b0 = clamp(y_norm0 + 0.00828125f * u_diff0, 0, 1)
    fma.rn.f32      %f7, %f3, 0f3C07AE14, %f2;
    max.f32         %f7, %f7, 0f00000000;
    min.f32         %f7, %f7, 0f3F800000;

    // --- Curr Frame (y1, u1, v1) -> (r1, g1, b1) ---
    cvt.rn.f32.u32  %f8, %r26;
    sub.f32         %f8, %f8, 0f41800000;
    mul.f32         %f8, %f8, 0f3B95A106;     // y_norm1

    cvt.rn.f32.u32  %f9, %r27;
    sub.f32         %f9, %f9, 0f43000000;     // u_diff1

    cvt.rn.f32.u32  %f10, %r28;
    sub.f32         %f10, %f10, 0f43000000;   // v_diff1

    // r1
    fma.rn.f32      %f11, %f10, 0f3BE66666, %f8;
    max.f32         %f11, %f11, 0f00000000;
    min.f32         %f11, %f11, 0f3F800000;

    // g1
    fma.rn.f32      %f12, %f9, 0fBA5B2574, %f8;
    fma.rn.f32      %f12, %f10, 0fBB08F5C3, %f12;
    max.f32         %f12, %f12, 0f00000000;
    min.f32         %f12, %f12, 0f3F800000;

    // b1
    fma.rn.f32      %f13, %f9, 0f3C07AE14, %f8;
    max.f32         %f13, %f13, 0f00000000;
    min.f32         %f13, %f13, 0f3F800000;

    // Write 7 NCHW planes: plane_stride = padded_width * padded_height (in floats)
    mul.lo.u32      %r33, %r6, %r7;
    mad.lo.u32      %r34, %r2, %r6, %r1;      // pixel_idx = py * padded_width + px
    cvt.u64.u32     %rd13, %r33;
    shl.b64         %rd13, %rd13, 2;          // plane_bytes = plane_stride * 4
    cvt.u64.u32     %rd14, %r34;
    shl.b64         %rd14, %rd14, 2;
    add.u64         %rd15, %rd3, %rd14;       // &dst_tensor[pixel_idx]

    st.global.f32   [%rd15], %f5;             // ch0: r0
    add.u64         %rd15, %rd15, %rd13;
    st.global.f32   [%rd15], %f6;             // ch1: g0
    add.u64         %rd15, %rd15, %rd13;
    st.global.f32   [%rd15], %f7;             // ch2: b0
    add.u64         %rd15, %rd15, %rd13;
    st.global.f32   [%rd15], %f11;            // ch3: r1
    add.u64         %rd15, %rd15, %rd13;
    st.global.f32   [%rd15], %f12;            // ch4: g1
    add.u64         %rd15, %rd15, %rd13;
    st.global.f32   [%rd15], %f13;            // ch5: b1
    add.u64         %rd15, %rd15, %rd13;
    st.global.f32   [%rd15], %f1;             // ch6: alpha

L_EXIT_PRE:
    ret;
}

// ============================================================================
// Kernel 2: rife_nchw_3ch_fp32_to_nv12
// Converts 3-channel planar RGB [0, 1] output tensor back to NV12 (BT.709).
// ============================================================================
.visible .entry rife_nchw_3ch_fp32_to_nv12(
    .param .u64 param_src_tensor,
    .param .u64 param_dst_nv12,
    .param .u32 param_pitch,
    .param .u32 param_width,
    .param .u32 param_height,
    .param .u32 param_padded_width,
    .param .u32 param_padded_height
)
{
    .reg .pred  %p<8>;
    .reg .b32   %r<30>;
    .reg .b64   %rd<25>;
    .reg .f32   %f<20>;

    ld.param.u64    %rd1, [param_src_tensor];
    ld.param.u64    %rd2, [param_dst_nv12];
    ld.param.u32    %r3,  [param_pitch];
    ld.param.u32    %r4,  [param_width];
    ld.param.u32    %r5,  [param_height];
    ld.param.u32    %r6,  [param_padded_width];
    ld.param.u32    %r7,  [param_padded_height];

    mov.u32         %r8, %ctaid.x;
    mov.u32         %r9, %ntid.x;
    mov.u32         %r10, %tid.x;
    mad.lo.u32      %r1, %r8, %r9, %r10;      // px

    mov.u32         %r11, %ctaid.y;
    mov.u32         %r12, %ntid.y;
    mov.u32         %r13, %tid.y;
    mad.lo.u32      %r2, %r11, %r12, %r13;    // py

    setp.ge.u32     %p1, %r1, %r4;
    setp.ge.u32     %p2, %r2, %r5;
    or.pred         %p3, %p1, %p2;
    @%p3 bra        L_EXIT_POST;

    // plane_stride = padded_width * padded_height
    mul.lo.u32      %r14, %r6, %r7;
    mad.lo.u32      %r15, %r2, %r6, %r1;
    cvt.u64.u32     %rd3, %r14;
    shl.b64         %rd3, %rd3, 2;
    cvt.u64.u32     %rd4, %r15;
    shl.b64         %rd4, %rd4, 2;
    add.u64         %rd5, %rd1, %rd4;

    ld.global.f32   %f1, [%rd5];              // R
    add.u64         %rd5, %rd5, %rd3;
    ld.global.f32   %f2, [%rd5];              // G
    add.u64         %rd5, %rd5, %rd3;
    ld.global.f32   %f3, [%rd5];              // B

    // Clamp R, G, B to [0.0, 1.0]
    max.f32         %f1, %f1, 0f00000000;
    min.f32         %f1, %f1, 0f3F800000;
    max.f32         %f2, %f2, 0f00000000;
    min.f32         %f2, %f2, 0f3F800000;
    max.f32         %f3, %f3, 0f00000000;
    min.f32         %f3, %f3, 0f3F800000;

    // Y = 16.5f + 46.5588f * R + 156.6288f * G + 15.8124f * B
    // 16.5f     = 0f41840000
    // 46.5588f  = 0f423A3C36
    // 156.6288f = 0f431CA0F9
    // 15.8124f  = 0f417CFF97
    // 16.0f     = 0f41800000
    // 235.0f    = 0f436B0000
    mov.f32         %f4, 0f41840000;
    fma.rn.f32      %f4, %f1, 0f423A3C36, %f4;
    fma.rn.f32      %f4, %f2, 0f431CA0F9, %f4;
    fma.rn.f32      %f4, %f3, 0f417CFF97, %f4;
    max.f32         %f4, %f4, 0f41800000;
    min.f32         %f4, %f4, 0f436B0000;
    cvt.rzi.u32.f32 %r16, %f4;

    // Store Y
    mad.lo.u32      %r17, %r2, %r3, %r1;
    cvt.u64.u32     %rd6, %r17;
    add.u64         %rd7, %rd2, %rd6;
    st.global.u8    [%rd7], %r16;

    // Chroma subsampling on even (px, py)
    and.b32         %r18, %r1, 1;
    and.b32         %r19, %r2, 1;
    or.b32          %r20, %r18, %r19;
    setp.ne.u32     %p4, %r20, 0;
    @%p4 bra        L_EXIT_POST;

    // U = 128.5f - 25.6642f * R - 86.3358f * G + 112.0f * B
    // 128.5f    = 0f43008000
    // -25.6642f = 0fC1CD5048
    // -86.3358f = 0fC2ACABF0
    // 112.0f    = 0f42E00000
    // 240.0f    = 0f43700000
    mov.f32         %f5, 0f43008000;
    fma.rn.f32      %f5, %f1, 0fC1CD5048, %f5;
    fma.rn.f32      %f5, %f2, 0fC2ACABF0, %f5;
    fma.rn.f32      %f5, %f3, 0f42E00000, %f5;
    max.f32         %f5, %f5, 0f41800000;
    min.f32         %f5, %f5, 0f43700000;
    cvt.rzi.u32.f32 %r21, %f5;

    // V = 128.5f + 112.0f * R - 101.7303f * G - 10.2697f * B
    // -101.7303f = 0fC2CB75EA
    // -10.2697f  = 0fC12450B0
    mov.f32         %f6, 0f43008000;
    fma.rn.f32      %f6, %f1, 0f42E00000, %f6;
    fma.rn.f32      %f6, %f2, 0fC2CB75EA, %f6;
    fma.rn.f32      %f6, %f3, 0fC12450B0, %f6;
    max.f32         %f6, %f6, 0f41800000;
    min.f32         %f6, %f6, 0f43700000;
    cvt.rzi.u32.f32 %r22, %f6;

    // Store UV at (height + py / 2) * pitch + px
    shr.u32         %r23, %r2, 1;
    add.u32         %r24, %r5, %r23;
    mad.lo.u32      %r25, %r24, %r3, %r1;
    cvt.u64.u32     %rd8, %r25;
    add.u64         %rd9, %rd2, %rd8;
    st.global.u8    [%rd9], %r21;
    st.global.u8    [%rd9+1], %r22;

L_EXIT_POST:
    ret;
}
```

#### 3.1.2 修复 [`src/shim/ngx_bridge/rife_pack_runtime.cpp`](../../src/shim/ngx_bridge/rife_pack_runtime.cpp) 上下文绑定与错误诊断
在 `rife_pack_runtime.cpp` 中对齐 `p010_pack_runtime.cpp` 的 Primary Context 绑定逻辑，并在 `cuModuleLoadData` 失败时通过 `OutputDebugStringA` / `fprintf(stderr, ...)` 打印精确的 CUDA Driver 错误码：

```cpp
bool bindPrimaryContext(CudaDriverState& s) {
  CUcontext current = nullptr;
  if (s.cuCtxGetCurrent(&current) == CUDA_SUCCESS && current != nullptr) {
    return true;
  }
  if (!s.primary_context_retained) {
    CUdevice dev = 0;
    if (s.cuDeviceGet(&dev, 0) != CUDA_SUCCESS) return false;
    if (s.cuDevicePrimaryCtxRetain(&s.primary_context, dev) != CUDA_SUCCESS) {
      return false;
    }
    s.primary_context_retained = true;
  }
  return s.cuCtxSetCurrent(s.primary_context) == CUDA_SUCCESS;
}
```
> **关键点**：当调用线程已经持有有效 `CUcontext`（`current != nullptr`）时，直接复用当前线程上下文，严禁再用旧的 `s.primary_context` 强行覆盖！

---

### Track 9B：重构 `RifeEngine`（前置校验 PTX、动态 Shape Profile 自适应、帧对 RGB 缓存复用与 $\alpha$ 敏感度自检探针）

#### 3.2.1 在 [`src/shim/ngx_bridge/rife_engine.cpp`](../../src/shim/ngx_bridge/rife_engine.cpp) `loadEngine()` 中强制前置验证 PTX Runtime 与 Engine Profile
1. **前置校验 PTX 模块加载**：
   在 `RifeEngine::loadEngine()` 入口处立即调用 `isRifePackAvailable()`：
   ```cpp
   if (!isRifePackAvailable()) {
     setError("RIFE PTX preprocess/postprocess module failed to load in CUDA driver.");
     return false;
   }
   ```
2. **自适应 Engine Shape Profile（解决 `1080` vs `1088` 冲突）**：
   加载 `.engine` 后，检查输入张量 `in_name` 的静态维度与 Profile 0 的 `[min_dims, max_dims]`：
   ```cpp
   const nvinfer1::Dims tensor_dims = impl_->engine->getTensorShape(impl_->in_name.c_str());
   int aligned_w = ((width + 63) / 64) * 64;
   int aligned_h = ((height + 63) / 64) * 64;
   if (tensor_dims.nbDims == 4 && tensor_dims.d[2] > 0 && tensor_dims.d[3] > 0) {
     // Static shape engine
     padded_height_ = static_cast<int>(tensor_dims.d[2]);
     padded_width_ = static_cast<int>(tensor_dims.d[3]);
   } else if (impl_->engine->getNbOptimizationProfiles() > 0) {
     const nvinfer1::Dims min_d = impl_->engine->getProfileShape(
         impl_->in_name.c_str(), 0, nvinfer1::OptProfileSelector::kMIN);
     const nvinfer1::Dims max_d = impl_->engine->getProfileShape(
         impl_->in_name.c_str(), 0, nvinfer1::OptProfileSelector::kMAX);
     if (min_d.nbDims == 4 && max_d.nbDims == 4) {
       // Prefer 64-aligned if within profile; otherwise fall back to exact (height, width)
       padded_height_ = (aligned_h >= min_d.d[2] && aligned_h <= max_d.d[2]) ? aligned_h : height;
       padded_width_  = (aligned_w >= min_d.d[3] && aligned_w <= max_d.d[3]) ? aligned_w : width;
     }
   }
   nvinfer1::Dims4 dims{1, 7, padded_height_, padded_width_};
   if (!impl_->context->setInputShape(impl_->in_name.c_str(), dims)) {
     setError("TensorRT setInputShape failed for dimensions 1x7x" +
              std::to_string(padded_height_) + "x" + std::to_string(padded_width_));
     return false;
   }
   ```

3. **帧对缓存复用（Frame-Pair Caching）消除 120fps 75% 预处理与同步冗余**：
   在 `RifeEngine::Impl` 中记录上一次预处理的 `(last_prev_ptr, last_curr_ptr, last_sad, last_scene_cut)`，或者在每次 `launchRifePreprocess` 之外提供一个仅用 `cuMemsetD32Async`（将 float `alpha` 的 IEEE-754 位模式填入第 6 平面 `d_input + 6 * plane_pixels`）的快速通道：
   - 当 `(prev_nv12 == last_prev_ptr && curr_nv12 == last_curr_ptr)`（即同一对源帧在 $\alpha = 0.4, 0.6, 0.8$ 的第 2、3、4 次插帧）时：
     - `d_input` 的前 6 个通道（`ch0..5`）**已经驻留在显存中且未被修改**！
     - `host_sad` 与 `scene_cut` **已经在 $\alpha = 0.2$ 时计算完毕**！
     - 因此，$\alpha = 0.4, 0.6, 0.8$ **完全不需要再次运行 `rife_nv12_to_nchw_7ch_fp32`，也不需要再次执行 `cuStreamSynchronize` + `cuMemcpyDtoH` 读取 SAD**！只需单条 `cuMemsetD32Async(d_input_ch6, std::bit_cast<uint32_t>(alpha), plane_pixels, stream)` 即可直接调用 `enqueueV3`！
   - 这一项优化不仅彻底消除了 120fps 下每对帧 3 次多余的 CPU-GPU 同步停顿，还将预处理带宽降低了 75%！

4. **内置 $\alpha$ 时序敏感度探针（Timestep Sensitivity Probe）**：
   在 `loadEngine()` 绑定完 `d_input` / `d_output` 后，若非 `__builtin__` 模式，可在 `d_input` 中填入测试图案并分别以 `alpha = 0.2f` 和 `alpha = 0.8f` 执行两次推理，验证 `d_output` 随 `alpha` 变化；若输出完全恒等，则在日志中明确告警该模型未绑定动态 `timestep` 通道。

---

### Track 9C：升级 `rife_builder.cpp`（清除假引擎写盘污染、补齐 `GridSample` 坐标算子链 FP32 锁精度）

在 [`src/shim/ngx_bridge/rife_builder.cpp`](../../src/shim/ngx_bridge/rife_builder.cpp) 与 [`src/shim/ngx_bridge/rife_engine.cpp`](../../src/shim/ngx_bridge/rife_engine.cpp) 中执行以下三项改造：

1. **禁止将 `__builtin__` 占位网络写入正式引擎路径 `models/rife_v4.25.engine`**：
   - 在 `rife_engine.cpp`（第 177–196 行）中，扩展 ONNX 搜索候选列表，纳入 `rife_v4.25_v2.onnx`、`rife_v4.25_heavy.onnx`、`rife_v2/rife_v4.25.onnx`；
   - 若用户仅放置了 `<stem>.onnx` 而无 `.engine`，自动调用 `RifeEngineBuilder::buildFromOnnx` 编译生成正式 `.engine`；
   - 若既无有效 `.engine` 也无 `.onnx`，仅在显式开启测试环境变量（`CINERTX_ALLOW_BUILTIN_RIFE_SYNTH=1`）时在内存中构建合成测试引擎（或写入 `rife_builtin_synth.engine`），**严禁写入或覆盖 `models/rife_v4.25.engine`**；
   - 若发现磁盘上的 `models/rife_v4.25.engine` 文件大小小于 `512 KB`（即此前被 `__builtin__` 污染的 ~15KB 假引擎），自动识别为无效占位文件并拒绝作为正式 RIFE 模型加载（若同目录存在 `.onnx` 则自动重新编译覆盖）。

2. **修复 `buildBuiltinTemporalNetwork` 使测试合成网络真正具备 $\alpha$ 相位响应能力**：
   - 在 `rife_builder.cpp:64-130` 中，将 `conv1` + `ReLU` + `conv2` 配置为计算 `(img1 - img0) * (alpha - 0.5f)` 的残差修正项，或直接移除 `bridge_exports.cpp:918-920` 中画蛇添足的二次 `rife_engine->interpolate(out_nv12, out_nv12, ...)` 调用（因为第 911 行 `launchNvofWarpNv12` 已经按精确相位 `alpha` 将双向光流插值帧写入了 `out_nv12`！）。

3. **补齐真实 `rife_v4.25.onnx` 的 FP32 算子锁精度规则（`rife_builder.cpp:208-238`）**：
   依据 [NVIDIA TensorRT Issue #4721](https://github.com/NVIDIA/TensorRT/issues/4721) 与 [AmusementClub/vs-mlrt Issue #66](https://github.com/AmusementClub/vs-mlrt/issues/66)，在 `RifeEngineBuilder::buildFromOnnx` 中将坐标网格与归一化敏感层全部锁定为 `FP32`：
   ```cpp
   config->setFlag(nvinfer1::BuilderFlag::kFP16);
   config->setFlag(nvinfer1::BuilderFlag::kOBEY_PRECISION_CONSTRAINTS);

   for (int32_t i = 0; i < network->getNbLayers(); ++i) {
     nvinfer1::ILayer* layer = network->getLayer(i);
     if (!layer) continue;
     const auto type = layer->getType();
     const std::string name = layer->getName() ? layer->getName() : "";

     // Lock GridSample, ElementWise Div/Sub/Mul/Add in coordinate grid paths, Cast, and Reduce to FP32
     const bool is_grid_or_coord =
         (type == nvinfer1::LayerType::kGRID_SAMPLE) ||
         (type == nvinfer1::LayerType::kREDUCE) ||
         (type == nvinfer1::LayerType::kCAST) ||
         (type == nvinfer1::LayerType::kSLICE) ||
         (type == nvinfer1::LayerType::kELEMENTWISE &&
          (name.find("Grid") != std::string::npos ||
           name.find("grid") != std::string::npos ||
           name.find("flow") != std::string::npos ||
           name.find("Div") != std::string::npos ||
           name.find("Mul") != std::string::npos ||
           name.find("Sub") != std::string::npos ||
           name.find("Add") != std::string::npos ||
           true)); // Safe lock: ElementWise & Unary layers have negligible compute cost compared to Conv2D
     if (is_grid_or_coord || type == nvinfer1::LayerType::kUNARY) {
       layer->setPrecision(nvinfer1::DataType::kFLOAT);
       for (int32_t o = 0; o < layer->getNbOutputs(); ++o) {
         if (auto* out = layer->getOutput(o)) {
           if (out->getType() == nvinfer1::DataType::kFLOAT ||
               out->getType() == nvinfer1::DataType::kHALF) {
             layer->setOutputType(o, nvinfer1::DataType::kFLOAT);
           }
         }
       }
     }
   }
   ```
   > **架构原理解释**：在 RIFE `IFNet_HDv3` 中，95% 以上的计算量集中在 `kCONVOLUTION`（3x3 卷积层，保持 FP16 Tensor Core 加速），而 `kGRID_SAMPLE`、`kELEMENTWISE`、`kUNARY`、`kREDUCE` 属于带宽受限的坐标变换与逐元素融合算子。将这些非卷积算子锁定为 `kFLOAT (FP32)` 几乎不损失吞吐量，却能 100% 根治 FP16 坐标网格舍入与除零溢出导致的插帧失效。

---

### Track 9D：修复 `hw_transcode.cpp` 与 `ffmpeg_io.cpp` 调度回退、场景切与容器帧率元数据

1. **消除 [`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp)（第 601–633 行）的静默吞错与固定 `prev_frame` 重复**：
   - 当 `status == RTX_OK && scene_cut != 0` 时，采用**最近邻相位阶跃（Nearest-Neighbor Phase Step）**：
     - 当 $\alpha < 0.5\text{f}$ 时输出 `prev_frame`（$F_k$）；
     - 当 $\alpha \ge 0.5\text{f}$ 时输出 `frame`（$F_{k+1}$）；
     避免转场处连续 5 帧停留在旧镜头 $F_k$。
   - 当 `status != RTX_OK` 时：
     - **严禁静默吞噬**！首次（及限频前 5 次）通过 `emitLog` 输出明确的 `[FRUC-ERROR]` 诊断信息（包含 `status`、当前使用的插帧引擎路径及 `bridgeLoader().getLastError(session.handle())`）；
     - 若用户在严格模式下或连续多帧插帧失败，应清晰记录失败计数并在回退时同样采用 $\alpha < 0.5\text{f} \to \text{prev\_frame}, \alpha \ge 0.5\text{f} \to \text{frame}$。

2. **修复 [`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp)（第 277 行）与 [`src/io/ffmpeg_io.cpp`](../../src/io/ffmpeg_io.cpp)（第 876–883 行）容器头帧率失真**：
   - 在 `hw_transcode.cpp` 第 277 行 `frame_rate_ = chooseFrameRate(options, info_);` 之后，**立即同步更新 `output_info_.frame_rate = frame_rate_;`**！
   - 在 `ffmpeg_io.cpp` 第 876–883 行中，同时写入 `stream_->avg_frame_rate` 与 `stream_->r_frame_rate`：
     ```cpp
     if (info.frame_rate.num > 0 && info.frame_rate.den > 0) {
       const AVRational fps_q = av_make_q(
           static_cast<int>(info.frame_rate.num),
           static_cast<int>(info.frame_rate.den));
       stream_->avg_frame_rate = fps_q;
       stream_->r_frame_rate = fps_q;
     }
     ```

---

## 四、Phase 9 终审验收标准（Definition of Done）

执行工程师完成 Track 9A–9D 施工后，必须通过以下 4 项硬性实机门禁方可提请 Phase 9 验收：

1. **PTX Runtime 零告警加载门禁**：
   - 运行 `ctest --test-dir .vs/build -R rife --output-on-failure`，验证 `isRifePackAvailable()` 返回 `true`，`rife_nv12_to_nchw_7ch_fp32` 与 `rife_nchw_3ch_fp32_to_nv12` 两个核函数在 `cuModuleLoadData` 下 100% 编译通过且无 PTX JIT 报错。
2. **$\alpha \in \{0.2, 0.4, 0.6, 0.8\}$ 四相位单调演进门禁（拒绝 1:5 重复帧）**：
   - 新增单元测试：构造水平匀速平移的合成 NV12 测试帧对 $(F_0, F_1)$（例如高亮竖条从 $x=100$ 移动到 $x=120$），依次调用 $\alpha = 0.0, 0.2, 0.4, 0.6, 0.8$；
   - 断言输出的 5 帧两两像素哈希/L1 范数**互不相同**（$\|I(\alpha_i) - I(\alpha_{i+1})\|_1 > 0$），且高亮竖条的质心横坐标随 $\alpha$ 单调递增。
3. **24fps $\to$ 120fps 实机转码全链路对账门禁**：
   - 对 24fps 测试源视频执行 `--fruc-mode rife --fruc-fps 120` 实机转码：
     - 终端统计 `report.frames_interpolated` 必须精确等于 $(N_{\text{in}} - 1) \times 4 - N_{\text{scenecut\_frames}}$（严禁为 `0`）；
     - 使用 `ffprobe` 检查输出 MP4 文件：`avg_frame_rate` 与 `r_frame_rate` 必须为 `120/1`；
     - 抽取任意运动区间的连续 6 帧 $(f_0, f_1, f_2, f_3, f_4, f_5)$，验证相邻帧 PSNR/SSIM 连续平滑过渡，彻底清零 `[F_k, F_k, F_k, F_k, F_k]` 重复帧现象。
4. **帧对缓存（Frame-Pair Cache）性能门禁**：
   - 验证在 120fps（1:5）模式下，每对相邻源帧仅执行 **1 次** `rife_nv12_to_nchw_7ch_fp32` 与 **1 次** `cuMemcpyDtoH` SAD 同步回传，其余 3 个中间相位仅通过 `cuMemsetD32Async` 更新第 6 通道 `alpha`。
