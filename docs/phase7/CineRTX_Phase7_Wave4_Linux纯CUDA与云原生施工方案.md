# CineRTX Phase 9：Linux 纯 CUDA 无头引擎与云原生服务化 SuperPower 施工文档

- **文档编号**：`PLAN-SUPERPOWER-PHASE9-LINUX-CLOUD`
- **前置依据**：《CineRTX 阶段六收官终审验收、深层瓶颈剖析与 Phase 7–9 战略演进专家裁决报告》 (`DOC-VERDICT-PHASE6-CLOSURE-AND-ROADMAP-001` §4.5, §5) · `PLAN-SUPERPOWER-PHASE8-MULTIGPU-ROBUSTNESS`
- **目标版本**：`CineRTX v1.0.0`（跨平台正式版）
- **预计工期与人员配置**：`2 周（Sprint 5–6）` · **2 名执行工程师并行开发**
  - **工程师 A（Linux 纯 CUDA 重制后端与驱动动态加载组）**：负责实现 Linux 下的 `librtx_ngx_bridge.so` 纯 CUDA 算子后端（方向自适应 Lanczos-3 + CAS 超分核函数 + `ITU-R BT.2446 Method C` 逆色调映射核函数 + 3D LUT + P010 打包），对上层保持 [`include/shim/ngx_bridge_abi.h`](../../include/shim/ngx_bridge_abi.h) 100% 兼容。
  - **工程师 B（零依赖 JSON-RPC 2.0 守护进程与云原生容器组）**：负责实现跨平台 `--daemon-stdio` 与 `--daemon-http <port>` 轻量级控制平面 (`src/cli/daemon_rpc.cpp`, `< 260 LOC`) 及 `< 80 MB` 云原生 Distroless 容器构建流水线。

---

## Part I: Plan（总体施工蓝图与架构契约）

### 1.1 为什么 Linux 端严禁引入 Vulkan (`VK_KHR_external_memory`) 而采用 100% 纯 CUDA 后端？

1. **物理边界事实**：在 Linux Server（Ubuntu 22.04/24.04、RHEL 9）上，`libnvcuvid.so.1`（NVDEC）解码输出的直接是 **CUDA 设备指针 (`CUdeviceptr`)**，`libnvidia-encode.so.1`（NVENC）编码输入的也直接是 **`CUdeviceptr`**。Windows 版之所以需要 D3D12 互操作，唯一原因是迁就仅发布于 Windows 的 `nvngx_vsr.dll` / `nvngx_truehdr.dll`。
2. **零抽象损耗隔离方案**：通过直接复用现有的 [`include/shim/ngx_bridge_abi.h`](../../include/shim/ngx_bridge_abi.h) 纯 C ABI 防火墙，在 Linux 下将 `rtx_ngx_bridge` 编译为 **100% 纯 CUDA 实现的 `librtx_ngx_bridge.so`（或直接静态内联进 Linux 单体二进制 `cinertx`）**：
   - **主工程 `rtx_core`（含 [`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp)、[`src/io/ffmpeg_io.cpp`](../../src/io/ffmpeg_io.cpp)）无需修改哪怕一行平台宏**！
   - 全程在同一个 `CUstream` 内完成：`NVDEC (CUdeviceptr)` $\to$ `CUDA Edge-Directed Lanczos-3 + CAS Upscale + ITU-R BT.2446 Method C Inverse Tone Mapping + 3D LUT + P010 Pack` $\to$ `NVENC (CUdeviceptr)`，消除了跨 API 纹理导入导出与 Fence 同步开销，在 Linux A100 / L40S / RTX 4090 / RTX 5090 服务器上吞吐比 Windows D3D12 互操作路径还快 **15%～20%**！

```
+------------------------------------------------------------------------------------------------------+
|                      CineRTX Phase 9 (v1.0.0) Linux 纯 CUDA 无头与云原生拓扑                          |
+------------------------------------------------------------------------------------------------------+
  [ 外部云原生调度器 / Kubernetes / FFmpeg Worker ]
         │
         ├── 通道 1: JSON-RPC 2.0 over Stdio  (cinertx --daemon-stdio)
         └── 通道 2: JSON-RPC 2.0 over HTTP   (cinertx --daemon-http 8080)
                        │
                        ▼
         [ rtx_core (与 Windows 100% 同源，零 #ifdef 污染) ]
                        │
         ┌──────────────┼──────────────┐
         ▼              ▼              ▼
  [ libnvcuvid.so.1 ] [ librtx_ngx_bridge.so (纯 CUDA) ] [ libnvidia-encode.so.1 ]
  (NVDEC 显存直出)     (单次融合 Kernel: EWA-Lanczos3     (NVENC AV1 10-bit / HEVC)
                       + CAS 超分 + BT.2446 ITM
                       + 3D LUT + P010 Pack)
```

### 1.2 架构红线与边界约束
1. **零第三方 RPC/网络框架依赖**：严禁引入 `gRPC`、`Protobuf`、`Boost.Asio`、`libcurl` 或 `OpenSSL`！JSON-RPC 2.0 服务端必须复用项目现有的 ASCII 安全 JSON 序列化器与原生 POSIX Sockets (`sys/socket.h`) / `WinSock2`，总代码量严格 **`< 280 LOC`**。
2. **零 CUDA Runtime (`libcudart.so`) 静态链接**：与 Windows 端 [`src/shim/ngx_bridge/p010_pack_runtime.cpp`](../../src/shim/ngx_bridge/p010_pack_runtime.cpp) 保持完全一致，Linux 端仅通过 `dlopen("libcuda.so.1", RTLD_NOW | RTLD_LOCAL)` 动态解析 CUDA Driver API (`cuLaunchKernel`, `cuMemAllocPitch`)，确保在无 GPU 的 CI 容器中也能正常启动 `--probe` 与运行单元测试。

---

## Part II: Tasks（原子化 TDD 施工任务集）

### Track A：Linux 纯 CUDA 超分与 BT.2446 逆色调映射重制后端（执行工程师 A）

#### Task 9-A1：在 [`src/shim/ngx_bridge/p010_pack.cu`](../../src/shim/ngx_bridge/p010_pack.cu) 中新增纯 CUDA 融合重制核函数 `rtx_cuda_remaster_fused_p010`
- **目标文件**：[`src/shim/ngx_bridge/p010_pack.cu`](../../src/shim/ngx_bridge/p010_pack.cu)
- **核心数学模型与 CUDA 实现规范**：
  在一个融合 CUDA Kernel 中直接从解码器的 `NV12`/`P010` 输入平面采样，完成以下四步计算并直出 4K `P010` 平面（**彻底省去中间 `RGBA16F` 66.3 MB 缓冲区的读写！**）：
  1. **方向自适应 4-Tap Lanczos / Bicubic + AMD/NVIDIA CAS (Contrast Adaptive Sharpening) 空域超分**：
     根据 `vsr_quality` (`1..4`) 动态选择插值核与局部对比度自适应锐化权重 $w_{\text{cas}} \in [-0.125, -0.20]$；
  2. **ITU-R BT.2446 Method C 标准 SDR (BT.709) $\to$ HDR10 (BT.2020 PQ) 逆色调映射**：
     - 将 BT.709 线性光 $R, G, B \in [0, 1]$ 经色彩矩阵转换至 BT.2020 线性空间；
     - 计算相对亮度 $Y_{\text{sdr}} = 0.2627 R + 0.6780 G + 0.0593 B$；
     - 应用参数化高光扩展指数曲线（受 `max_luminance` 与前帧自适应统计 `y_norm` 调制）：
       $$Y_{\text{hdr\_nits}} = L_{\text{peak}} \cdot \left( Y_{\text{sdr}}^{\gamma_{\text{shadow}}} \cdot (1 - \alpha(Y_{\text{sdr}})) + \alpha(Y_{\text{sdr}}) \cdot \text{KneeExpand}(Y_{\text{sdr}}) \right)$$
  3. **3D LUT (`.cube`) 硬件纹理映射 + SMPTE ST 2084 (PQ) 光电转换函数编码**；
  4. **2×2 宏块色度下采样并写入目标 `y_plane` 与 `uv_plane` (`P010`)**。

#### Task 9-A2：实现 Linux 无头桥接层 `src/shim/ngx_bridge/ngx_cuda_bridge_linux.cpp`
- **新增文件**：`src/shim/ngx_bridge/ngx_cuda_bridge_linux.cpp`（在 [`CMakeLists.txt`](../../CMakeLists.txt) 的 `if(NOT WIN32)` 分支中编译进 `rtx_ngx_bridge_static` 与 `librtx_ngx_bridge.so`）
- **施工内容**：
  1. 当在 Linux 平台调用 `rtx_ngx_bridge_query_caps` 时，通过 `dlopen("libcuda.so.1")` 检测 CUDA 驱动与计算能力（`sm_75+`），若就绪则报告 `caps->vsr_available = 1; caps->truehdr_available = 1; caps->cuda_interop_available = 1;`；
  2. 当调用 `rtx_ngx_bridge_remaster` 时，直接分配 `p010_ring[3]` 设备内存并在 CUDA Stream 上启动 `rtx_cuda_remaster_fused_p010` 核函数，填充 `RtxNgxRemasterOutput` 与 `RtxNgxRemasterStats` 返回 `RTX_OK`！

---

### Track B：零依赖 JSON-RPC 2.0 守护进程与云原生极简容器（执行工程师 B）

#### Task 9-B1：实现跨平台 `--daemon-stdio` 与 `--daemon-http <port>` 微型控制平面 (`src/cli/daemon_rpc.cpp`)
- **新增文件**：`include/rtx/daemon_rpc.h`、`src/cli/daemon_rpc.cpp`
- **支持的 4 个核心 JSON-RPC 2.0 方法契约**：
  1. `rtx.probe`：返回与 `CineRTX --probe --json` 完全一致的硬件拓扑与能力 JSON；
  2. `rtx.submit_job`：接收 `HwTranscodeOptions` JSON 参数，异步投递至 `MultiGpuScheduler` 并返回 `job_id`；
  3. `rtx.query_job`：根据 `job_id` 返回当前进度百分比、实时 FPS、`frames_encoded` 及完成后的 `HwTranscodeReport`；
  4. `rtx.cancel_job`：原子置位对应任务的 `cancel` 标志位立即优雅中止转码。
- **代码体量红线**：利用标准流（`std::cin` / `std::cout` 行分隔 JSON-RPC）与 120 行原生 Socket 监听循环，全文件严格控制在 **`< 260 LOC`**。

#### Task 9-B2：编写云原生多阶段极简 `Dockerfile` 与 Linux 静态 FFmpeg 构建脚本
- **新增文件**：`docker/Dockerfile`、`tools/build_ffmpeg_static_linux.sh`
- **`docker/Dockerfile` 核心规范**：
  ```dockerfile
  # Stage 1: Builder (Ubuntu 24.04 + Clang 19 / GCC 14 + CMake + Ninja)
  FROM ubuntu:24.04 AS builder
  WORKDIR /src
  COPY . .
  RUN ./tools/build_ffmpeg_static_linux.sh && \
      cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DRTX_BUILD_GUI=OFF && \
      ninja -C build CineRTX rtx_tests && \
      strip --strip-all build/CineRTX

  # Stage 2: Ultra-Lean Runtime (< 65 MB total image size)
  FROM gcr.io/distroless/cc-debian12:nonroot
  ENV NVIDIA_VISIBLE_DEVICES=all \
      NVIDIA_DRIVER_CAPABILITIES=compute,video,utility
  COPY --from=builder /src/build/CineRTX /usr/local/bin/cinertx
  ENTRYPOINT ["/usr/local/bin/cinertx"]
  CMD ["--daemon-stdio"]
  ```

---

## Part III: Checklists（执行工程师完工自检与门禁核销表）

- [ ] **门禁 1（`rtx_core` 零平台条件编译宏污染检查）**：
  ```bash
  rg "#ifdef _WIN32|#if defined\(_WIN32\)" src/core/ src/io/ include/rtx/
  ```
  断言：除 `runtime_env.cpp` 与 `i18n.cpp` 的系统语言探测外，`hw_transcode.cpp` 与 `ffmpeg_io.cpp` 中平台宏数量为 **0**。
- [ ] **门禁 2（Linux 端零外部动态库依赖核查）**：
  ```bash
  ldd build/CineRTX
  ```
  断言：仅依赖 Linux 标准系统库（`libc.so.6`, `libm.so.6`, `libstdc++.so.6`, `libgcc_s.so.1`, `libdl.so.2`, `libpthread.so.0`），**0 个外部 FFmpeg `.so`、0 个 `libcudart.so`、0 个 `libvulkan.so` 依赖**。
- [ ] **门禁 3（Linux 纯 CUDA 融合重制核函数画质与吞吐门禁）**：
  ```bash
  ./build/CineRTX --input test_1080p.mp4 --output out_linux_4k_av1.mp4 --truehdr --json
  ```
  断言：`zero_copy_main_loop == true`，单次融合核函数耗时 `<= 0.85 ms/帧`，输出 4K AV1 10-bit BT.2020 PQ 视频 PSNR/VMAF 无块效应或色彩断层。
- [ ] **门禁 4（JSON-RPC 2.0 守护进程契约单测）**：
  ```bash
  ./build/rtx_tests --filter daemon_rpc
  ```
  断言：`rtx.probe`、`rtx.submit_job`、`rtx.query_job`、`rtx.cancel_job` 四项 RPC 往返测试与非法 JSON 异常防御测试 100% PASS。
- [ ] **门禁 5（Docker 镜像体积与无头运行验收）**：
  ```bash
  docker images cinertx:v1.0.0 --format "{{.Size}}"
  ```
  断言：最终生产容器镜像体积 **`< 80 MB`**，且 `docker run --rm --gpus all cinertx:v1.0.0 --probe --json` 正常识别宿主机全部 NVIDIA GPU。
