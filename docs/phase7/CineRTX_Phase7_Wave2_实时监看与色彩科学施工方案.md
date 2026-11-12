# CineRTX Phase 7：实时监看视口、融合 3D LUT / HDR10+ 色彩科学及异步批处理 SuperPower 施工文档

- **文档编号**：`PLAN-SUPERPOWER-PHASE7-VIEWPORT-COLOR`
- **前置依据**：《CineRTX 阶段六收官终审验收、深层瓶颈剖析与 Phase 7–9 战略演进专家裁决报告》 (`DOC-VERDICT-PHASE6-CLOSURE-AND-ROADMAP-001` §4.2, §4.3, §4.4) · `PLAN-SUPERPOWER-PHASE6-1-POLISH`
- **目标版本**：`CineRTX v0.3.0`
- **预计工期与人员配置**：`2 周（Sprint 1–2）` · **3 名执行工程师并行开发**
  - **工程师 A（CUDA 核函数与 NGX 桥接组）**：负责 [`src/shim/ngx_bridge/`](../../src/shim/ngx_bridge/) 下的 3 槽异步 P010 环形缓冲、[`p010_pack.cu`](../../src/shim/ngx_bridge/p010_pack.cu) 寄存器级融合 3D LUT + Warp 亮度归约 + `960x540` 预览旁路。
  - **工程师 B（核心色彩科学与容器复用组）**：负责 `.cube` 3D LUT 解析器 (`lut3d.h/.cpp`)、`SMPTE ST 2094-40` (HDR10+) 动态元数据合成 ([`include/rtx/hdr.h`](../../include/rtx/hdr.h))、3 槽异步主循环改造 ([`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp)) 与全音轨/字幕轨无损直通 ([`src/io/ffmpeg_io.cpp`](../../src/io/ffmpeg_io.cpp))。
  - **工程师 C（Win32 原生监看视口与批处理队列组）**：负责原生子 `HWND` D3D12 `FLIP_DISCARD` 实时 A/B 分屏监看视口 (`preview_viewport.h/.cpp`)、走带定帧预览 (Scrubbing) 与多文件拖拽批处理队列 ([`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp))。

---

## Part I: Plan（总体施工蓝图与架构契约）

### 1.1 系统架构演进拓扑图

```
+-------------------------------------------------------------------------------------------------------+
|                       CineRTX Phase 7 (v0.3.0) 单卡数据面终极定型架构图                                  |
+-------------------------------------------------------------------------------------------------------+
  [ 1. 多文件批处理队列 (Batch Queue) & 全音轨/字幕解复用 (FFmpeg Static Demuxer) ]
         │
         ▼ (Compressed Video Packets)
  [ 2. NVDEC 硬件解码 (cuvidMapVideoFrame64) ] ──(原画 NV12 表面)──┐
         │                                                        │
         ▼ (D3D12 / CUDA 共享纹理互操作)                           │
  [ 3. NGX VSR (4K 超分) + TrueHDR (自适应亮度反馈闭环) ]           │
         │                                                        ▼
         ▼ (3840x2160 RGBA16F 表面)                       ┌───────────────────────────────┐
  [ 4. 融合核函数 p010_pack.cu (单次 Kernel Launch) ] ◄───│ 10-bit 3D LUT (.cube)         │
         ├── (a) 寄存器内 FP16 3D LUT 硬件三线性/四面体插值 │ cudaTextureObject_t (L2 常驻)  │
         ├── (b) Warp Shuffle 归约帧真实 MaxCLL/MaxFALL   └───────────────────────────────┘
         │       ├──► 回传 NgxD3D12Bridge::set_frame_stats (动态平滑下一帧 TrueHDR)
         │       └──► 驱动 SeiSynthesizer 生成 SMPTE ST 2094-40 (HDR10+ OBU/SEI)
         ├── (c) 写出 3 槽异步环形缓冲 p010_ring[3] ──► [ 5. NVENC AV1/HEVC 10-bit 异步编码 ]
         └── (d) 旁路写出 960x540 R10G10B10A2 预览帧 ──► [ 6. 子 HWND D3D12 Flip 实时 A/B 视口 ]
```

### 1.2 四大核心架构铁律（严禁违背）
1. **核函数零额外带宽铁律**：3D LUT 查找、每帧亮度统计归约（MaxCLL/MaxFALL/99% 分位数）以及 `960x540` 实时预览降采样，**必须 100% 融合在 [`src/shim/ngx_bridge/p010_pack.cu`](../../src/shim/ngx_bridge/p010_pack.cu) 的同一个 Kernel Launch 内完成**，绝不允许启动额外的全帧读写 Kernel。
2. **视口零阻塞与低显存铁律**：实时监看视口严禁引入 `dcomp.dll`；必须使用原生 Win32 `STATIC` 子窗口 `HWND` 直接绑定 `DXGI_SWAP_EFFECT_FLIP_DISCARD` 交换链（分辨率固定为 `960×540`，格式 `DXGI_FORMAT_R10G10B10A2_UNORM`，双缓冲总显存 **`4.14 MB`**），且 `Present` 必须携带 `DXGI_PRESENT_DO_NOT_WAIT`。
3. **C ABI 向后兼容追加铁律**：对 [`include/shim/ngx_bridge_abi.h`](../../include/shim/ngx_bridge_abi.h) 的扩展必须利用现有 `reserved` 字节或追加新导出函数，并将 `RTX_NGX_BRIDGE_ABI_VERSION` 升至 `3u`。
4. **100% RAII 与指定初始化器铁律**：所有新增类（`Lut3D`、`PreviewViewport`）必须采用私有构造 + `static Result<T> create(...)` 静态工厂 + 析构函数自动释放。

---

## Part II: Tasks（原子化 TDD 施工任务集）

### Track A：CUDA 融合核函数与 NGX 桥接 3 槽异步化（执行工程师 A）

#### Task 7-A1：扩展 `ngx_bridge_abi.h` 契约以支持 3D LUT、帧亮度回传与预览缓冲
- **目标文件**：[`include/shim/ngx_bridge_abi.h`](../../include/shim/ngx_bridge_abi.h)（第 18–200 行）
- **施工内容**：
  1. 将 `#define RTX_NGX_BRIDGE_ABI_VERSION 2u` 升级为 `3u`；
  2. 在 `RtxNgxRemasterInput` 中利用 `reserved` 字段映射预览输出与 A/B 分屏参数：
     ```c
     typedef struct RtxNgxRemasterInput {
       uint64_t y_plane;          /* CUdeviceptr */
       uint64_t uv_plane;         /* CUdeviceptr */
       uint32_t y_pitch;          /* bytes */
       uint32_t uv_pitch;         /* bytes */
       int32_t format;            /* RTX_PIXEL_NV12 / RTX_PIXEL_P010 */
       int32_t full_range;        /* 0=limited, 1=full */
       int32_t matrix;            /* 0=BT.709, 1=BT.2020 NCL, 2=BT.601 */
       uint64_t preview_rgba10_ptr; /* Optional CUdeviceptr (960x540 R10G10B10A2), 0 = skip preview */
       uint32_t preview_pitch;    /* bytes per row for preview buffer */
       float split_x_norm;        /* 0.0..1.0 A/B split-screen ratio (<0 disables split) */
     } RtxNgxRemasterInput;
     ```
  3. 在 `RtxNgxRemasterOutput` 中利用 `reserved` 字段回传当前帧由 GPU 归约出的真实亮度统计（单位：nits）：
     ```c
     typedef struct RtxNgxRemasterOutput {
       uint64_t y_plane;
       uint64_t uv_plane;
       uint32_t y_pitch;
       uint32_t uv_pitch;
       int32_t width;
       int32_t height;
       int32_t format;
       uint32_t frame_max_nits;   /* Real GPU-reduced MaxCLL of this frame */
       uint32_t frame_avg_nits;   /* Real GPU-reduced MaxFALL of this frame */
       uint32_t ring_slot_index;  /* 0..2 in 3-slot P010 ring */
       int32_t reserved[2];
     } RtxNgxRemasterOutput;
     ```
  4. 新增 3D LUT 上传 C ABI 导出函数声明：
     ```c
     /* Uploads an N x N x N RGBA16F (half4) 3D LUT table into a CUDA 3D texture object.
      * Passing lut_half4_data == NULL or grid_size == 0 unbinds and frees the active LUT. */
     RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_set_lut3d(
         RtxNgxBridge* bridge, const uint16_t* lut_half4_data, int32_t grid_size);
     ```

#### Task 7-A2：在 [`src/shim/ngx_bridge/p010_pack.cu`](../../src/shim/ngx_bridge/p010_pack.cu) 中融合 FP16 3D LUT、Warp 亮度归约与 `960x540` 预览写出
- **目标文件**：[`src/shim/ngx_bridge/p010_pack.cu`](../../src/shim/ngx_bridge/p010_pack.cu) 与 [`src/shim/ngx_bridge/p010_pack_runtime.cpp`](../../src/shim/ngx_bridge/p010_pack_runtime.cpp)
- **核心 CUDA 核函数实现规范**：
  ```cuda
  // src/shim/ngx_bridge/p010_pack.cu — 融合 3D LUT + Warp 亮度归约 + 预览降采样
  __device__ __forceinline__ float3 applyLut3D(
      cudaTextureObject_t lut_tex, int lut_dim, float3 rgb_norm) {
      if (lut_tex == 0 || lut_dim <= 1) return rgb_norm;
      const float scale = static_cast<float>(lut_dim - 1) / static_cast<float>(lut_dim);
      const float offset = 0.5f / static_cast<float>(lut_dim);
      const float4 s = tex3D<float4>(
          lut_tex,
          __saturatef(rgb_norm.x) * scale + offset,
          __saturatef(rgb_norm.y) * scale + offset,
          __saturatef(rgb_norm.z) * scale + offset);
      return make_float3(s.x, s.y, s.z);
  }

  // 每个线程处理 2x2 像素宏块：在寄存器内完成 PQ/LUT 变换后：
  // 1. Warp 级归约最大亮度与亮度累加和（仅 lane 0 执行一次 atomicMax / atomicAdd 写入设备端 2x uint32 统计槽）
  unsigned int local_max_nits = static_cast<unsigned int>(fmaxf(0.0f, luma_nits));
  #pragma unroll
  for (int offset = 16; offset > 0; offset >>= 1) {
      local_max_nits = max(local_max_nits, __shfl_xor_sync(0xFFFFFFFFu, local_max_nits, offset));
  }
  if ((threadIdx.x == 0) && (threadIdx.y == 0) && stats_devptr != nullptr) {
      atomicMax(&stats_devptr[0], local_max_nits);
      atomicAdd(&stats_devptr[1], local_max_nits);
  }

  // 2. 若 preview_rgba10_ptr != 0 且当前 2x2 块命中 4x4 采样格点 ((bx & 1) == 0 && (by & 1) == 0)：
  // 直接打包为 R10G10B10A2_UNORM 写入 960x540 预览表面（支持 split_x_norm 左原画 / 右重制分屏）！
  ```
- **单步验证命令**：
  ```powershell
  ninja -C build/cmake/windows-msvc-ninja rtx_ngx_bridge rtx_tests
  ./build/cmake/windows-msvc-ninja/rtx_tests.exe --filter pack
  ```

#### Task 7-A3：将 `BridgeImpl::p010_ring[2]` 升级为 3 槽位 (`p010_ring[3]`) 并激活帧间自适应 TrueHDR 闭环
- **目标文件**：[`src/shim/ngx_bridge/bridge_exports.cpp`](../../src/shim/ngx_bridge/bridge_exports.cpp)（第 75–90 行、第 470–560 行）
- **施工内容**：
  1. 将 `uint64_t p010_ring[2] = {0, 0};` 扩展为 `uint64_t p010_ring[3] = {0, 0, 0};`（槽位索引改为 `impl->remaster_frames_issued % 3`）；
  2. 在 `rtx_ngx_bridge_remaster` 结束时，将 GPU 归约出的 `frame_max_nits` 与 `frame_avg_nits` 填入 `output->frame_max_nits` / `output->frame_avg_nits`，并自动计算归一化均值 `y_norm` 与高光比例 `p_high` 调用 `impl->ngx->set_frame_stats(...)`，无需上层干预即可激活内容自适应 TrueHDR！

---

### Track B：`.cube` 3D LUT 解析器、HDR10+ 动态元数据与全音轨/字幕直通（执行工程师 B）

#### Task 7-B1：实现零外部依赖 `.cube` 3D LUT 解析器与 FP16 转换器 (`include/rtx/lut3d.h` & `src/core/lut3d.cpp`)
- **新增文件**：`include/rtx/lut3d.h`、`src/core/lut3d.cpp`（注册至 [`CMakeLists.txt`](../../CMakeLists.txt) `RTX_CORE_SOURCES`）
- **核心契约实现**：
  ```cpp
  // include/rtx/lut3d.h — 100% RAII 3D LUT 解析与 FP16 (half4) 紧凑存储
  #pragma once
  #include <cstdint>
  #include <filesystem>
  #include <span>
  #include <vector>
  #include "rtx/status.h"

  namespace rtx {

  class Lut3D {
  public:
      [[nodiscard]] static Result<Lut3D> loadFromCubeFile(const std::filesystem::path& path);
      [[nodiscard]] int gridSize() const noexcept { return grid_size_; }
      [[nodiscard]] std::span<const std::uint16_t> half4Data() const noexcept { return half4_table_; }

  private:
      int grid_size_ = 0; // Supports 17, 33, 64, 65
      std::vector<std::uint16_t> half4_table_; // grid_size^3 * 4 (RGBA16F IEEE-754 binary16)
  };

  } // namespace rtx
  ```
- **单步验证**：在 `tests/test_color.cpp` 新增 `lut3d_cube_parse_and_identity_accuracy` 用例，验证 `33x33x33` 与 `65x65x65` 标准恒等 `.cube` 文件解析后 FP16 误差 `< 1e-3`。

#### Task 7-B2：在 [`include/rtx/hdr.h`](../../include/rtx/hdr.h) 与 `src/core/sei_synthesizer.cpp` 中实现 `SMPTE ST 2094-40` (HDR10+) 动态元数据合成
- **目标文件**：[`include/rtx/hdr.h`](../../include/rtx/hdr.h)、`src/core/sei_synthesizer.cpp`
- **施工内容**：
  1. 新增纯聚合结构体 `Hdr10PlusFrameStats`：
     ```cpp
     struct Hdr10PlusFrameStats {
         std::uint32_t maxscl_r_nits = 1000;
         std::uint32_t maxscl_g_nits = 1000;
         std::uint32_t maxscl_b_nits = 1000;
         std::uint32_t average_maxrgb_nits = 400;
         std::uint16_t knee_point_x = 500;
         std::uint16_t knee_point_y = 500;
     };
     ```
  2. 在 `SeiSynthesizer` 中新增静态函数：
     - `static std::vector<uint8_t> hdr10PlusItuT35Payload(const Hdr10PlusFrameStats& stats);`（按 `ITU-T T.35` Country Code `0xB5`、Provider Code `0x003C`、Application Identifier `0x04` 编码 `SMPTE ST 2094-40` 比特流）；
     - `static std::vector<uint8_t> av1Hdr10PlusMetadataObu(const Hdr10PlusFrameStats& stats);`（封装为 `OBU_METADATA` + `METADATA_TYPE_ITUT_T35`）。

#### Task 7-B3：升级 [`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp) 为 3 槽位并发 (`remasters_issued - encodes_completed < 3`) 并支持全音轨/字幕直通
- **目标文件**：[`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp)（第 430–460 行、第 725 行）、[`src/io/ffmpeg_io.cpp`](../../src/io/ffmpeg_io.cpp)
- **施工内容**：
  1. 在 `createEncoder()` 成功后立即调用 `openMuxer()`，徹底消除首帧编码前 `audio_packet_queue` 的内存堆积；
  2. 将 [`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp) 第 725 行的 `remasters_issued - encodes_completed < 2` 升级为 `< 3`（配合 Track A 的 `p010_ring[3]`）；
  3. 在 `finalize()` 中使用全片累计的最大 `frame_max_nits` 与帧平均 `frame_avg_nits` 更新 `report` 与 HDR10 `cll.max_cll` / `cll.max_fall`；
  4. 在 [`src/io/ffmpeg_io.cpp`](../../src/io/ffmpeg_io.cpp) 中支持复制源容器中全部 `AVMEDIA_TYPE_AUDIO` 与兼容的 `AVMEDIA_TYPE_SUBTITLE`（如 `mov_text` / `srt` / `ass`）流。

---

### Track C：Win32 原生子窗口 D3D12 实时 A/B 监看视口与多文件批处理队列（执行工程师 C）

#### Task 7-C1：创建 100% RAII 的 `PreviewViewport` 类 (`src/gui/preview_viewport.h` & `src/gui/preview_viewport.cpp`)
- **新增文件**：`src/gui/preview_viewport.h`、`src/gui/preview_viewport.cpp`
- **核心设计规范**：
  ```cpp
  // src/gui/preview_viewport.h — 零 dcomp.dll 依赖、基于原生子 HWND 的 D3D12 Flip 交换链视口
  #pragma once
  #include <windows.h>
  #include <cstdint>
  #include "rtx/status.h"

  namespace rtx::gui {

  class PreviewViewport {
  public:
      [[nodiscard]] static Result<PreviewViewport> create(HWND parent_hwnd, int width = 960, int height = 540);
      ~PreviewViewport();

      PreviewViewport(const PreviewViewport&) = delete;
      PreviewViewport& operator=(const PreviewViewport&) = delete;
      PreviewViewport(PreviewViewport&& other) noexcept;
      PreviewViewport& operator=(PreviewViewport&& other) noexcept;

      void setBounds(int x, int y, int w, int h) noexcept;
      [[nodiscard]] float splitRatio() const noexcept { return split_ratio_; }
      void setSplitRatio(float ratio) noexcept { split_ratio_ = ratio; }

      // 非阻塞呈现：使用 DXGI_PRESENT_ALLOW_TEARING | DXGI_PRESENT_DO_NOT_WAIT
      void presentFromHostOrShared(const void* r10g10b10a2_data, std::uint32_t pitch) noexcept;
      [[nodiscard]] HWND hwnd() const noexcept { return child_hwnd_; }

  private:
      PreviewViewport() = default;
      HWND child_hwnd_ = nullptr;
      void* d3d12_device_ = nullptr;
      void* cmd_queue_ = nullptr;
      void* swap_chain_ = nullptr;
      float split_ratio_ = 0.5f; // 默认居中 50/50 左原片右重制对比
  };

  } // namespace rtx::gui
  ```

#### Task 7-C2：在 [`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp) 中集成实时 A/B 视口、走带滑块、`3D LUT` 选择器与多文件批处理队列
- **目标文件**：[`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp)
- **施工内容**：
  1. **窗口布局升级**：将窗口默认尺寸从 `880×580` 调整为 `960×780`（Per-Monitor V2 DPI 自适应）：
     - 上半区（高度 `320px`）：嵌入 `PreviewViewport`（支持鼠标左键横向拖动调整 `split_ratio_` 实时分割线，底部配 1 个 `TRACKBAR_CLASS` 走带滑块 `IDC_TRK_SCRUB`）；
     - 中部区：原有 13 项参数网格 + 新增 `IDC_EDT_LUT_PATH` / `IDC_BTN_BROWSE_LUT`（选择 `.cube` 3D LUT 文件）与 `IDC_CHK_HDR10PLUS`（启用 HDR10+ 动态元数据）；
     - 下半区：支持多文件拖入的批处理任务状态队列。
  2. **多文件拖拽批处理 (`WM_DROPFILES`)**：
     修改 [`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp) 第 494–504 行的 `WM_DROPFILES` 处理逻辑：
     ```cpp
     case WM_DROPFILES: {
       HDROP hDrop = reinterpret_cast<HDROP>(wParam);
       const UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
       g_batchQueue.clear();
       for (UINT i = 0; i < fileCount; ++i) {
         wchar_t path[MAX_PATH] = {};
         if (DragQueryFileW(hDrop, i, path, MAX_PATH) > 0) {
           g_batchQueue.emplace_back(path);
         }
       }
       if (!g_batchQueue.empty()) {
         SetDlgItemTextW(hwnd, IDC_EDT_INPUT, g_batchQueue.front().c_str());
         SetDlgItemTextW(hwnd, IDC_EDT_OUTPUT, L"");
         autoFillOutputPath(hwnd);
       }
       DragFinish(hDrop);
       return 0;
     }
     ```
     当 `g_batchQueue.size() > 1` 时，工作线程顺序遍历队列中每个文件生成对应 `<stem>_remaster_av1.mp4` 并实时更新总进度！

---

## Part III: Checklists（执行工程师完工自检与门禁核销表）

- [ ] **门禁 1（100% RAII 与零 `dcomp.dll` 导入审计）**：
  ```powershell
  rg -i "\b(initialize|shutdown|destroy)\s*\(" include/ src/core/ src/gui/
  powershell -ExecutionPolicy Bypass -File tools/audit_imports.ps1
  ```
  断言：静态扫描匹配数为 `0`；`CineRTX.exe` PE 导入表中**不包含** `dcomp.dll`。
- [ ] **门禁 2（3D LUT + HDR10+ + 预览旁路核函数耗时门禁）**：
  ```powershell
  ./build/cmake/windows-msvc-ninja/CineRTX.exe --input test_1080p.mp4 --output out_lut_hdr10p.mp4 --truehdr --hdr10plus --lut film_emulation_65.cube --json
  ```
  断言：输出的 JSON 报告中 `remaster_pack_kernel_ms <= 0.25`（较基线 `0.20 ms` 增量不超过 `0.05 ms`），且 `frame_max_nits > 0`。
- [ ] **门禁 3（实时监看视口显存与吞吐零干扰验证）**：
  - 在 GUI 中开启实时 A/B 分屏预览转码同一 10 秒 1080p $\to$ 4K AV1 视频，对比开启视口前后的平均 FPS；
  - 断言：开启 15Hz 实时分屏预览时，总转码 FPS 波动 **< 1.0%**，且通过 `nvidia-smi` 观测额外显存占用 **< 8 MB**；最小化窗口或拖动分割线时零卡顿、零 TDR。
- [ ] **门禁 4（3 槽异步流水线吞吐提升验证）**：
  - 断言：在相同 RTX 40/50 GPU 上，`p010_ring[3]` + 提前打开 Muxer 后的 4K AV1 主循环吞吐较 Phase 6 基线（33.1 FPS）提升 **≥ 25%**。
- [ ] **门禁 5（单元测试扩展与全绿）**：
  - 新增 `test_lut3d.cpp`（4 项用例）与 `test_hdr.cpp` 中的 `hdr10plus_itut_t35_obu_synthesis` 用例全部 PASS（总用例数 **≥ 84 项**，100% PASS）。
