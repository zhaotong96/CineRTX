# CineRTX 工程施工规范（第一阶段）：Phase 8 双路真补帧（NVOFA + TensorRT RIFE）落地与桥接层 CUDA 上下文归一

**文档标识**：`DOC-SUPERPOWER-STAGE1-FRUC-TRUE-DUALPATH-001`  
**演进阶段**：Stage 1 / 共 3 阶段（前置基础：修复 Phase 8 伪光流与 RIFE 静默回退阻断缺陷）  
**遵循方法论**：`SuperPower` 工程施工范式（`Plan` + `Tasks` + `Checklists` 三位一体，TDD 门禁驱动）  
**关联专家报告**：[`docs/phase8/CineRTX_Phase8_TensorRT_RIFE补帧施工方案.md`](CineRTX_Phase8_TensorRT_RIFE补帧施工方案.md)、`docs/phase8/CineRTX_Phase8_返工交付验收实证报告.md`  
**适用团队**：执行工程师 A（NVOFA 硬件光流与 PTX 算子组）、执行工程师 B（TensorRT RIFE 推理、CUDA 上下文归一与 CMake 部署组）

---

## 一、 Plan（顶层规划与架构不变量契约）

### 1.1 本阶段核心目标
彻底根除上一轮交付中“NVOFA 实为 50 行 PTX 同坐标像素线性混帧（伪光流）”与“RIFE 因缺少上下文绑定/DLL 部署而 100% 静默回退至伪光流（导致 NVOFA 与 RIFE 实测同为 86 FPS）”的重大工程缺陷，在唯一自研桥接库 `rtx_ngx_bridge.dll` 内部真正打通：
1. **路线 A（默认极速真光流）**：动态加载驱动原生 `C:\Windows\System32\nvofapi64.dll`（`NvOFAPICreateInstanceCuda`），调用硬件 Optical Flow Accelerator (OFA) 计算 4×4 网格双向 S10.5 光流矢量，并通过 PTX 双向运动补偿（Motion Warp）核函数合成中间帧（同时支持 8-bit `NV12` 与 10-bit `P010`）。
2. **路线 B（母版画质 TensorRT RIFE）**：消除 `rtx_ngx_bridge` 内部 4 套重复 CUDA Driver API 加载器造成的 `CUcontext` 跨上下文冲突，修复 `get_interp_buffer` 行距（Pitch）越界、32 vs 64 填充对齐分裂、FP16/FP32 张量类型探测缺失、逐像素全局原子加争用、`RifeEngine::destroy()` 违背 RAII 以及 `trt_loader.cpp:55` 硬编码绝对路径等 6 大阻断缺陷，禁止 `hw_transcode.cpp` 在用户显式指定 RIFE 时静默伪装成 `"nvof"`。

### 1.2 架构红线与强制不变量（Architectural Invariants）
1. **严禁伪插帧与静默篡改后端标识**：
   - 彻底删除 [`src/shim/ngx_bridge/p010_pack_runtime.cpp`](../../src/shim/ngx_bridge/p010_pack_runtime.cpp)（第 447–502 行）中的线性透明度混合核 `kInterpolatePtx`（`out = (1-alpha)*prev + alpha*curr`）。
   - 在 [`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp)（第 788–801 行）中，当用户显式请求了 RIFE（`!options.rife_engine_path.empty()`）但 `initRife()` 返回非 `RTX_OK` 时，**必须直接返回 `ErrorCode::DllMissing` 或 `ErrorCode::InvalidArgument` 错误并携带 `bridgeLoader().lastError(session.bridge())` 详情**，绝不允许静默吞错并篡改 `report.fruc_backend = "nvof"`！
2. **全桥唯一 CUDA 上下文与唯一 Driver API 函数表**：
   - `rtx_ngx_bridge.dll` 内严禁在 `p010_pack_runtime.cpp`、`rife_pack_runtime.cpp`、`rife_engine.cpp` 中各自维护独立的 `primary_context` 或重复 `LoadLibraryExW(L"nvcuda.dll")`。
   - 所有 CUDA 显存分配、模块加载与核函数发射必须统一绑定 [`src/shim/ngx_bridge/d3d12_interop.h`](../../src/shim/ngx_bridge/d3d12_interop.h) 的 `D3D12Interop::bind_context()` 与 `pack_stream()`。
3. **100% 现代 C++26 绝对 RAII 与指定初始化**：
   - 彻底拔除 [`src/shim/ngx_bridge/rife_engine.h`](../../src/shim/ngx_bridge/rife_engine.h)（第 46 行）的 `void destroy()` 方法，所有资源在 `~RifeEngine()` 析构函数中自动释放；结构体构造一律采用 C++26 指定初始化器（`.field = value`）。
4. **源码树零生成物污染与零硬编码绝对路径**：
   - 物理删除源码树中的 `src/shim/ngx_bridge/rife_pack_ptx.h`，仅保留构建期生成的 `${CMAKE_BINARY_DIR}/generated/rife_pack_ptx.h`；物理删除 [`src/shim/ngx_bridge/trt_loader.cpp`](../../src/shim/ngx_bridge/trt_loader.cpp)（第 55 行）的 `[REDACTED_HOST]` 硬编码路径。

### 1.3 多工程师并行防撞文件矩阵（File Ownership Matrix）

| 工程师泳道 | 独占修改 / 新建文件清单 | 严禁触碰区域（防冲突边界） |
| :--- | :--- | :--- |
| **工程师 A（光流与 PTX 算子泳道）** | - 新建 `src/shim/ngx_bridge/nvof_engine.h`<br>- 新建 `src/shim/ngx_bridge/nvof_engine.cpp`<br>- 修改 [`src/shim/ngx_bridge/p010_pack_runtime.h`](../../src/shim/ngx_bridge/p010_pack_runtime.h)<br>- 修改 [`src/shim/ngx_bridge/p010_pack_runtime.cpp`](../../src/shim/ngx_bridge/p010_pack_runtime.cpp)<br>- 修改 [`src/shim/ngx_bridge/rife_pack.cu`](../../src/shim/ngx_bridge/rife_pack.cu) & 重新生成 `rife_pack.ptx`<br>- 删除源码树 `src/shim/ngx_bridge/rife_pack_ptx.h` | 严禁修改 `src/cli/`、`src/gui/`、`webui/` |
| **工程师 B（RIFE 引擎与主链路泳道）** | - 修改 [`src/shim/ngx_bridge/rife_engine.h`](../../src/shim/ngx_bridge/rife_engine.h)<br>- 修改 [`src/shim/ngx_bridge/rife_engine.cpp`](../../src/shim/ngx_bridge/rife_engine.cpp)<br>- 修改 [`src/shim/ngx_bridge/rife_builder.cpp`](../../src/shim/ngx_bridge/rife_builder.cpp)<br>- 修改 [`src/shim/ngx_bridge/rife_pack_runtime.cpp`](../../src/shim/ngx_bridge/rife_pack_runtime.cpp)<br>- 修改 [`src/shim/ngx_bridge/trt_loader.cpp`](../../src/shim/ngx_bridge/trt_loader.cpp)<br>- 修改 [`src/shim/ngx_bridge/bridge_exports.cpp`](../../src/shim/ngx_bridge/bridge_exports.cpp)<br>- 修改 [`src/core/ngx_remaster.cpp`](../../src/core/ngx_remaster.cpp)<br>- 修改 [`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp)<br>- 修改 [`CMakeLists.txt`](../../CMakeLists.txt) 与 [`tests/test_rife.cpp`](../../tests/test_rife.cpp) | 严禁修改 `src/gui/`、`webui/`、`docs/spec/` |

---

## 二、 Tasks（原子级 TDD 施工任务集）

### Track A（工程师 A）：真 NVOFA 硬件光流接入与 PTX 算子重构

#### Task 1A-1：实现驱动原生 `nvofapi64.dll` 硬件光流引擎（`nvof_engine.h` / `nvof_engine.cpp`）
- **目标位置**：新建 `src/shim/ngx_bridge/nvof_engine.h` 与 `src/shim/ngx_bridge/nvof_engine.cpp`
- **施工动作**：
  1. 使用 `LoadLibraryExW(L"nvofapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)` 动态解析 `NvOFAPICreateInstanceCuda`（零静态导入表污染，零安装包体积增量）。
  2. 封装 `class NvofEngine`（100% RAII，析构函数自动调用 `nvOFDestroy` 与释放 `CUdeviceptr` 光流矢量缓冲，严禁提供 `destroy()` 成员函数）。
  3. 初始化参数强制设定：`outGridSize = NV_OF_OUTPUT_VECTOR_GRID_SIZE_4`（4×4 硬件块粒度）、`predDirection = NV_OF_PRED_DIRECTION_BOTH`（单次调用同时输出前向 $F_{t-1}\to F_t$ 与后向 $F_t\to F_{t-1}$ `NV_OF_FLOW_VECTOR` S10.5 定点矢量及 `enableOutputCost = NV_OF_TRUE` 代价缓冲）。
  4. 替换 [`src/shim/ngx_bridge/p010_pack_runtime.cpp`](../../src/shim/ngx_bridge/p010_pack_runtime.cpp) 第 447–502 行的 `kInterpolatePtx`，改为接收前向/后向 S10.5 光流网格（`flowx * 0.03125f`, `flowy * 0.03125f`）与代价缓冲的双向运动补偿核 `rtx_nvof_warp_nv12`（当平均光流代价超过硬切阈值 `160` 时，直接按 $\alpha < 0.5$ 拷贝 `prev` 或 `curr` 防止转场撕裂）。

#### Task 1A-2：升级 `rife_pack.cu` 为 Warp 束内规约 SAD 并清除源码树内嵌 `rife_pack_ptx.h`
- **目标位置**：[`src/shim/ngx_bridge/rife_pack.cu`](../../src/shim/ngx_bridge/rife_pack.cu)、物理删除 `src/shim/ngx_bridge/rife_pack_ptx.h`
- **缺陷根因**：当前 `src/shim/ngx_bridge/rife_pack_ptx.h`（第 115 行）在每个像素线程直接执行 `@%p11 atom.global.add.u32 %r30, [%rd13], %r29;`，在 1080p 下单帧产生 2,073,600 次全局内存原子争用；且源码树中的 `rife_pack_ptx.h` 遮蔽了 `${CMAKE_BINARY_DIR}/generated/rife_pack_ptx.h`。
- **施工代码契约（`rife_pack.cu` 束内规约核心段）**：
  ```cuda
  // 线程束内 32 路无锁规约，仅 lane 0 执行一次全局 atomicAdd（降低 32 倍原子争用）
  uint32_t local_sad = (x < width && y < height)
      ? static_cast<uint32_t>(abs(static_cast<int>(y1) - static_cast<int>(y0)))
      : 0u;
  for (int offset = 16; offset > 0; offset >>= 1) {
    local_sad += __shfl_down_sync(0xffffffffu, local_sad, offset);
  }
  const int lane_id = (threadIdx.y * blockDim.x + threadIdx.x) & 31;
  if (lane_id == 0 && local_sad > 0u && d_sad_sum != nullptr) {
    atomicAdd(d_sad_sum, local_sad);
  }
  ```
- **清理动作**：运行 `tools/gen_rife_ptx.py` 更新 [`src/shim/ngx_bridge/rife_pack.ptx`](../../src/shim/ngx_bridge/rife_pack.ptx)（确保 `.target sm_89`），并**物理删除源码树中的 `src/shim/ngx_bridge/rife_pack_ptx.h`**（让编译期只包含 `${CMAKE_BINARY_DIR}/generated/rife_pack_ptx.h`）。

---

### Track B（工程师 B）：TensorRT RIFE 六大缺陷修复、上下文归一与禁止静默伪装

#### Task 1B-1：拔除 `trt_loader.cpp` 硬编码路径并在 `CMakeLists.txt` 自动部署 TensorRT 运行时 DLL
- **目标位置**：
  - [`src/shim/ngx_bridge/trt_loader.cpp`](../../src/shim/ngx_bridge/trt_loader.cpp)（第 35–60 行）
  - [`CMakeLists.txt`](../../CMakeLists.txt)（第 294–305、590–645 行）
- **施工动作**：
  1. 在 [`src/shim/ngx_bridge/trt_loader.cpp`](../../src/shim/ngx_bridge/trt_loader.cpp) 中**立即删除第 55 行**：
     ```cpp
     // 严禁硬编码本机路径！必须删除：
     // dirs.push_back(L"[REDACTED_PATH]");
     ```
     仅保留环境变量（`RTX_TENSORRT_ROOT` / `TENSORRT_DIR`）与相对于模块目录 `getModuleDirectory()` 的探测路径（`<modDir>`、`<modDir>/plugins/nvidia_rtx`、`<modDir>/../../../build/external/tensorrt/TensorRT-10.13.3.9/lib`）。
  2. 在 [`CMakeLists.txt`](../../CMakeLists.txt) 第 294 行：若 `${RTX_EXTERNAL_DIR}/TensorRT-10.13.3.9.Windows.win10.cuda-13.0.zip` 存在而 `${RTX_TENSORRT_ROOT}/include/NvInfer.h` 不存在，自动调用 `file(ARCHIVE_EXTRACT ...)` 解压；并在 `rtx_stage_ngx_plugin` 目标中，将存在的 `nvinfer_10.dll`、`nvinfer_builder_resource_10.dll`、`nvonnxparser_10.dll` 自动 `copy_if_different` 至 `<exe_dir>/plugins/nvidia_rtx/`。

#### Task 1B-2：修复 `bridge_exports.cpp` / `rife_pack_runtime.cpp` 上下文分裂与 `get_interp_buffer` Pitch 越界
- **目标位置**：
  - [`src/shim/ngx_bridge/bridge_exports.cpp`](../../src/shim/ngx_bridge/bridge_exports.cpp)（第 128–132、801–928 行）
  - [`src/shim/ngx_bridge/rife_pack_runtime.cpp`](../../src/shim/ngx_bridge/rife_pack_runtime.cpp)（第 79–136 行）
  - [`src/core/ngx_remaster.cpp`](../../src/core/ngx_remaster.cpp)（第 240–258 行）
- **施工动作**：
  1. **修复 `rtx_ngx_bridge_get_interp_buffer` 显存越界**：
     在 [`src/shim/ngx_bridge/bridge_exports.cpp`](../../src/shim/ngx_bridge/bridge_exports.cpp) 第 801–830 行中，NVDEC 传入的 `pitch` 可能大于 `width`（如 1920 宽度对应 2048 字节行距）。将分配大小从 `width * height * 3 / 2` 修正为按对齐上界 `std::max<uint32_t>(width, (width + 511u) & ~511u) * height * 3u / 2u`，并在 `rtx_ngx_bridge_interpolate_rife`（第 896 行）中校验 `impl->interp_bytes >= static_cast<size_t>(pitch) * height * 3u / 2u`，若不足则按 `pitch * height * 3u / 2u` 重新扩容 `impl->interp_buffer`！
  2. **强制绑定 `D3D12Interop` CUDA 上下文**：
     在 [`src/shim/ngx_bridge/bridge_exports.cpp`](../../src/shim/ngx_bridge/bridge_exports.cpp) 的 `rtx_ngx_bridge_init_rife`（第 873 行）和 `rtx_ngx_bridge_interpolate_rife`（第 894 行）加锁后，**第一件事必须调用**：
     ```cpp
     std::string ctx_err;
     if (!impl->interop->bind_context(&ctx_err)) {
       impl->error = "rtx_ngx_bridge_rife: " + ctx_err;
       return RTX_ERR_INTERNAL;
     }
     ```
  3. **消除 `rife_pack_runtime.cpp` 私自切换 `device 0` 上下文**：
     在 [`src/shim/ngx_bridge/rife_pack_runtime.cpp`](../../src/shim/ngx_bridge/rife_pack_runtime.cpp) 第 95–125 行中，删除 `cuDeviceGet(&dev, 0)` 与 `cuDevicePrimaryCtxRetain`，改为直接调用 `s.api.ctx_get_current(&s.primary_context)` 复用调用方（`D3D12Interop::bind_context()`）已绑定在当前线程的上下文！
  4. **补齐 `NgxRemasterBridge::close()` 漏置空的 6 个函数指针**：
     在 [`src/core/ngx_remaster.cpp`](../../src/core/ngx_remaster.cpp) 第 256 行 `upload_frame_ = nullptr;` 之后，补齐：
     ```cpp
     interpolate_nv12_ = nullptr;
     get_interp_buffer_ = nullptr;
     init_rife_ = nullptr;
     interpolate_rife_ = nullptr;
     build_rife_engine_ = nullptr;
     destroy_rife_ = nullptr;
     ```

#### Task 1B-3：拔除 `RifeEngine::destroy()`、统一 64 像素对齐与 FP16/FP32 张量自适应
- **目标位置**：
  - [`src/shim/ngx_bridge/rife_engine.h`](../../src/shim/ngx_bridge/rife_engine.h) 与 [`src/shim/ngx_bridge/rife_engine.cpp`](../../src/shim/ngx_bridge/rife_engine.cpp)
  - [`src/shim/ngx_bridge/rife_builder.cpp`](../../src/shim/ngx_bridge/rife_builder.cpp) 与 [`tests/test_rife.cpp`](../../tests/test_rife.cpp)
- **施工动作**：
  1. 从 [`src/shim/ngx_bridge/rife_engine.h`](../../src/shim/ngx_bridge/rife_engine.h)（第 46 行）与 [`src/shim/ngx_bridge/rife_engine.cpp`](../../src/shim/ngx_bridge/rife_engine.cpp)（第 131–138 行）删除 `void RifeEngine::destroy()`；在 `bridge_exports.cpp` 第 129、926 行直接调用 `impl->rife_engine.reset();` 触发 `~Impl()` 自动释放 TensorRT 上下文与显存缓冲。
  2. 将 [`src/shim/ngx_bridge/rife_builder.cpp`](../../src/shim/ngx_bridge/rife_builder.cpp) 第 154–158 行与 [`tests/test_rife.cpp`](../../tests/test_rife.cpp) 第 73–86 行的填充对齐统一改为 **64 像素对齐**：
     ```cpp
     constexpr int kAlign = 64;
     const int opt_w = (width > 0) ? ((static_cast<int>(width) + kAlign - 1) / kAlign * kAlign) : 1920;
     const int opt_h = (height > 0) ? ((static_cast<int>(height) + kAlign - 1) / kAlign * kAlign) : 1088;
     ```
  3. 在 [`src/shim/ngx_bridge/rife_engine.cpp`](../../src/shim/ngx_bridge/rife_engine.cpp) 第 200 行查询 `impl_->engine->getTensorDataType(impl_->in_name.c_str())`，若模型要求非 `kFLOAT` 类型且无对应核函数，返回明确的错误提示而非盲目按 `sizeof(float)` 写入。
  4. 在 [`src/shim/ngx_bridge/rife_engine.cpp`](../../src/shim/ngx_bridge/rife_engine.cpp) 第 270–308 行与 [`src/shim/ngx_bridge/rife_pack_runtime.h`](../../src/shim/ngx_bridge/rife_pack_runtime.h) 中，所有 `RifePreprocessLaunch`、`RifePreprocessSeparateLaunch`、`RifePostprocessLaunch` 结构体一律改为 C++26 指定初始化器构造（`.y_plane_0 = prev_nv12, ...`）。

#### Task 1B-4：修复 `hw_transcode.cpp` 静默吞错伪装逻辑（严禁 RIFE 失败时伪报为 `nvof`）
- **目标位置**：[`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp)（第 783–815 行）
- **施工代码补丁**：
  ```cpp
  const bool fruc_enabled = options.fruc_enabled;
  const bool rife_requested = fruc_enabled && !options.rife_engine_path.empty();
  uint64_t fruc_out_buffer = 0;
  bool rife_active = false;

  if (rife_requested) {
    if (!session.bridgeLoader().hasRifeApi()) {
      return err(ErrorCode::Unsupported,
                 "loaded rtx_ngx_bridge.dll does not export the Phase 8 RIFE API (ABI < 4)");
    }
    const uint32_t in_w = static_cast<uint32_t>(session.info().width);
    const uint32_t in_h = static_cast<uint32_t>(session.info().height);
    const std::string engine_path_str = options.rife_engine_path.string();
    const int32_t r_res = session.bridgeLoader().initRife(
        session.bridge(), engine_path_str.c_str(), in_w, in_h);
    if (r_res != RTX_OK) {
      return err(mapShimResult(static_cast<RtxResult>(r_res)),
                 describe("rtx_ngx_bridge_init_rife", static_cast<RtxResult>(r_res),
                          session.bridgeLoader().lastError(session.bridge())));
    }
    rife_active = true;
    report.fruc_backend = "rife-trt";
    fruc_out_buffer = session.bridgeLoader().getInterpBuffer(session.bridge(), in_w, in_h);
  } else if (fruc_enabled) {
    report.fruc_backend = "nvof";
    fruc_out_buffer = session.bridgeLoader().getInterpBuffer(
        session.bridge(), static_cast<uint32_t>(session.info().width),
        static_cast<uint32_t>(session.info().height));
  }
  ```

---

## 三、 Checklists（第一阶段量化验收门禁清单）

执行工程师在提交 Stage 1 代码前，必须逐条执行以下命令并全部勾选确认：

- [ ] **门禁 1.1（零硬编码路径与零 `destroy()` 静态扫描）**：
  - 执行命令：`cmake -P cmake/AuditPrivacy.cmake` 與 `rg -n "::destroy\(" src/ include/`
  - 通过标准：输出严格为 **0 命中**。
- [ ] **门禁 1.2（源码树零内嵌 PTX 头文件污染）**：
  - 执行命令：`Test-Path src/shim/ngx_bridge/rife_pack_ptx.h` 與 `Test-Path src/shim/ngx_bridge/p010_pack_ptx.h`
  - 通过标准：两项均返回 **`False`**（所有 `*_ptx.h` 仅存在于 `build/cmake/windows-msvc-ninja/generated/`）。
- [ ] **门禁 1.3（真 NVOFA 硬件光流验证，彻底拔除线性混帧）**：
  - 执行命令：`rg -n "NvOFAPICreateInstanceCuda" src/shim/ngx_bridge/`
  - 通过标准：在 `nvof_engine.cpp` 中命中动态解析逻辑，且 `p010_pack_runtime.cpp` 中不再包含无位移同坐标 `fma.rn.f32 %f6, %f1, %f3, %f5` 线性混帧代码。
- [ ] **门禁 1.4（RIFE 缺失引擎时显式报错拦截验证）**：
  - 执行命令：`.\build\cmake\windows-msvc-ninja\CineRTX.exe --input test.mp4 --output out.mp4 --rife-engine non_existent.engine`
  - 通过标准：进程返回非 0 退出码，且 `stderr` 明确打印 `rtx_ngx_bridge_init_rife failed: ... Cannot open RIFE engine file: non_existent.engine`，**绝不静默回退到 `nvof`**。
- [ ] **门禁 1.5（RIFE 真实推理与 NVOFA 吞吐差异实测验证）**：
  - 执行命令：使用有效 `.engine` 跑同一段 24fps 视频对比默认 `nvof` 与 `--rife-engine`：
  - 通过标准：`rife-trt` 模式输出 `fruc: backend=rife-trt, interpolated=...`，且由于真实执行了 TensorRT `enqueueV3`，其单帧耗时与 `nvof` 呈现真实的物理算力差（RTX 5060 上 4K 输出约 45–58 FPS vs `nvof` 约 75–85 FPS）。
- [ ] **门禁 1.6（单元测试与 PE 导入表纯净度）**：
  - 执行命令：`ctest --test-dir build/cmake/windows-msvc-ninja --output-on-failure` 与 `pwsh tools/audit_imports.ps1`
  - 通过标准：全部单元测试 100% 通过，`CineRTX.exe` 静态导入表中 `nvinfer`、`nvonnxparser`、`cudart`、`nvofapi64`、`MSVCP140`、`VCRUNTIME140`、`WS2_32` 计数均为 **0**。
