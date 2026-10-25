# RTX 超分辨率与 HDR 离线转码引擎
## 二阶段返工（RW2-01 … RW2-05）复验报告

---

**文档编号**：`RTX-REWORK-20261024-PHASE2`
**性质**：返工任务书 [`rework_order_phase2.md`](rework_order_phase2.md) 的整改与复验记录
**执行**：执行工程团队
**复验环境**（两台宿主均实测；连接方式见 [`rework_order.md`](rework_order.md) 第 16-19 行）：

| 项目 | 验收宿主 [REDACTED_HOST] | 研发机（开发宿主） |
| --- | --- | --- |
| GPU | NVIDIA GeForce RTX 5060 Laptop GPU（Blackwell, sm_120, 8 GiB） | NVIDIA GeForce GTX 970M（Maxwell, sm_52, 3 GiB） |
| 驱动 / CUDA | 617.14 / CUDA 13.4 | 582.66 / CUDA 13.0 |
| NVDEC | H.264 + HEVC Main10 | H.264（GM204 无 HEVC 引擎） |
| OS | Windows 11 | Windows x64 |
| 工具链 | Zig 0.13（Clang 18.1.6）、CMake 3.29.6、Ninja、FFmpeg 8.1 shared（`C:\Users\[REDACTED_USER]\rtx\`） | 同左（`C:\tc\`） |
| 构建目录 | `C:\PUBLIC\[REDACTED_PROJ]\build\rework-zig`（经 SSH 同步本工作区源码树 106 个文件后干净构建） | `build/rework-zig` |

二阶段返工任务书 §4 指定的验收宿主为 `[REDACTED_HOST]`（RTX 5060）。本报告第 2、3 节的全部门禁均已在该宿主实测通过；研发机结果作为并行证据一并列出。

---

## 1. 整改内容（按返工任务书顺序）

### RW2-01 [P0] D3D12-CUDA 纹理映射参数与驱动 API 版本 — 已修复

[`src/shim/ngx_bridge/d3d12_interop.cpp`](../src/shim/ngx_bridge/d3d12_interop.cpp)：

1. **描述符通道表**：`toCudaFormat`/`channelsOf` 改为单一事实源表格
   `kFormats[]`，精确匹配：
   * `RTX_PIXEL_RGBA16F` → `CU_AD_FORMAT_HALF` × 4 通道（8 字节/像素）；
   * `RTX_PIXEL_R10G10B10A2` → `CU_AD_FORMAT_UNSIGNED_INT32` × **1** 通道（4 字节/像素）；
   * `RTX_PIXEL_RGB32` → `CU_AD_FORMAT_UNSIGNED_INT8` × 4 通道（4 字节/像素）。
   `import_into_cuda` 新增不变量校验：`通道字节数 × 通道数` 必须等于
   `bytesPerPixel(format)`，否则在调用驱动前即以明确错误拒绝（防止未来格式表
   笔误退化为难以定位的 `CUDA_ERROR_INVALID_VALUE`）。
2. **驱动符号 `_v2` 化**：`cuMemcpy2D`/`cuMemcpy2DAsync` 改为优先加载
   `cuMemcpy2D_v2`/`cuMemcpy2DAsync_v2`，裸符号仅作回退。这消除了 64 位
   Windows 上用 32 位遗留入口解析现代 `CUDA_MEMCPY2D` 结构体导致的
   `CUDA_ERROR_INVALID_CONTEXT`。
3. **上下文绑定**：`bind_context()`（`cuCtxSetCurrent` 至导入该纹理的同一个
   Primary Context）在 D3D12→CUDA 拷贝与 CUDA→D3D12 写回前均强制执行，且
   写回路径此前忽略绑定结果，现已检查并上报。
4. **特性级别回退**：`D3D12CreateDevice` 按 12_1 → 12_0 → 11_0 依次尝试
   （代码注释早已声明但未实现）。Maxwell 研发机因此可以运行互操作自测。
5. **新增缺陷修复（超出任务书清单，实机跑通时发现）**：CUDA→D3D12 写回后
   D3D12 回读曾报 "tiled layout not resolved" 伪失败。根因是跨 API 竞态：
   写回是 CUDA 流上的异步拷贝，D3D12 队列与之无共享顺序，回读可能早于写回
   落板。已在写回后加入 `cuStreamSynchronize`（`_v2` 优先） retire CUDA 流
   （生产主循环按任务书 §4.5 用共享 64 位围栏排序，语义相同）。

### RW2-02 [P0] NVDEC 异步映射池容量 — 已修复

[`src/shim/loader/nv_decoder.cpp`](../src/shim/loader/nv_decoder.cpp)
`onSequence`：`create.ulNumOutputSurfaces` 由硬编码 `2` 改为
`max(slots.size(), 8)`，与异步导出（ACT-04 移除 `cuStreamSynchronize`）后的
在途帧数匹配，杜绝 `cuvidMapVideoFrame64 -> 205 (CUDA_ERROR_NOT_MAPPED)`。
`ulNumDecodeSurfaces`（DPB，=32）语义不变。

### RW2-03 [P0] 互操作自测纳入 CTest — 已完成

[`tests/test_shim.cpp`](../tests/test_shim.cpp) 新增
`RTX_TEST(shim_ngx_d3d12_interop_selftest)`：

* 动态解析 `rtx_ngx_bridge_selftest`，先校验参数拒绝路径；
* 探测宿主能力（`nvcuda.dll` / `d3d12.dll` 可加载性）：
  * 不支持环境 → 必须返回 `RTX_ERR_DLL_MISSING` 且 detail 非空（诚实失败，
    绝不静默通过）；
  * 支持环境 → 必须返回 `RTX_OK`，且 detail 包含
    `round trip OK` / `256x256 RGBA16F` / `D3D12 -> CUDA -> D3D12`。
* 此后任何通道不匹配或上下文脱靶都会被构建-测试链自动拦截。

### RW2-04 [P1] W4 无锁互操作双向闭环 — 已在真实 GPU 实测通过

在验收宿主 RTX 5060（sm_120）与研发机 GTX 970M（sm_52）上分别以
`RTX_BRIDGE_TRACE=1` 运行 `rtx_ngx_bridge_selftest(256, 256, ...)`，完整日志
见第 3.3 节。三个验证点全部成立：

1. D3D12 上传 RGBA16F 渐变纹理 → CUDA 经导入表面读取，逐样本一致；
2. CUDA 修改像素（XOR 翻转）→ 写回表面 → CUDA 流 retire；
3. D3D12 回读校验，与 CUDA 写入值逐样本一致。

两台宿主均返回 `RTX_OK (0)`，detail：
`round trip OK: 256x256 RGBA16F, D3D12 -> CUDA -> D3D12`。验收宿主另以生产
画质 1920×1080 复跑通过（`round trip OK: 1920x1080 RGBA16F, ...`）。

### RW2-05 [P1] W5/W6 依赖澄清与如实复验 — 已完成

* W5 维持挂起：宿主与研发机均无专有 NGX SDK（`nvsdk_ngx_d.dll` /
  `nvngx_vsr.dll` / `nvngx_truehdr.dll`）。`rtx_ngx_bridge_create` /
  `process` 返回 `RTX_ERR_NGX`，`last_error` 与 `query_caps().diagnostics`
  均指名缺失的 DLL，绝不伪造纹理存在。
* `rtx_ngx_bridge_query_caps` 修正：`cuda_interop_available` 此前被错误地
  与 SDK 绑定；现按 D3D12/CUDA 驱动实际可用性独立报告（W4 与 W5 解耦），
  诊断文案区分“互操作可用 + W5 挂起”与“两者皆缺”两种情况。
* 本文档即为整改后的统一复验报告；本报告中未宣称任何端到端超分/逆色调映射
  结果。

---

## 2. 静态与自动化门禁实测

### 2.1 单元测试套件（含新增互操作用例）

验收宿主（RTX 5060, sm_120）：

```text
C:\PUBLIC\[REDACTED_PROJ]\build\rework-zig> rtx_tests.exe
[==========] 102 test(s) run, 0 failed (534 ms)
```

研发机（GTX 970M, sm_52）：

```text
C:\PUBLIC\[REDACTED_PROJ]\build\rework-zig> rtx_tests.exe
[==========] 102 test(s) run, 0 failed (2687 ms)
```

（返工前基线为 101；新增 `shim_ngx_d3d12_interop_selftest`。两台宿主的用例
清单逐条一致，该用例均为 OK。）

### 2.2 CTest（验收宿主；研发机复跑相同）

```text
C:\PUBLIC\[REDACTED_PROJ]\build\rework-zig> ctest --output-on-failure
100% tests passed, 0 tests failed out of 4
```

### 2.3 合规与导入表门禁（验收宿主；研发机复跑相同）

```text
> pwsh tools/check_spdx.ps1
SPDX and boundary audit
  licenses : GPL=59 MIT=29
  binaries : 0
  result   : PASS

> pwsh tools/audit_imports.ps1 -BuildDir build/rework-zig
  [PE] rtx-transcode.exe          result: no NVIDIA static imports
  [PE] rtx-transcode-base.exe     result: no NVIDIA static imports
  [PE] rtx_tests.exe              result: no NVIDIA static imports
  [PE] rtx_shim.dll               result: no NVIDIA static imports
  [PE] rtx_ngx_bridge.dll         result: no NVIDIA static imports
  inspected: 5 binary/binaries
  result: PASS
```

---

## 3. Gate 1 无回退复验（RW2-02 证据）

### 3.1 验收宿主（RTX 5060, sm_120）—— 任务书指定宿主

8-bit 容器路径（`sample.mp4`，hevc）：

```text
> pwsh tools/verify_gate1.ps1 -BuildDir build/rework-zig `
       -Container C:\Users\[REDACTED_USER]\rtx\sample.mp4 -Codec hevc -BitDepth 8
Gate 1 re-acceptance
  [PASS] unit-tests - [==========] 102 test(s) run, 0 failed (480 ms)
  [PASS] probe - NVIDIA GeForce RTX 5060 Laptop GPU; caps=nvdec
  [PASS] raw-nv12-to-p010 - exit=0, 7372800 bytes (expected 7372800)
  [PASS] container-zero-copy - frames=241/241, D2H=0, H2D=0, zero_copy=True

4/4 checks passed
```

10-bit Main10 路径（`ffmpeg -c:v hevc_nvenc -pix_fmt p010le` 生成的
`sample10.mp4`，hevc）：

```text
> pwsh tools/verify_gate1.ps1 -BuildDir build/rework-zig `
       -Container sample10.mp4 -Codec hevc -BitDepth 10
  [PASS] unit-tests - [==========] 102 test(s) run, 0 failed (478 ms)
  [PASS] probe - NVIDIA GeForce RTX 5060 Laptop GPU; caps=nvdec
  [PASS] raw-nv12-to-p010 - exit=0, 7372800 bytes (expected 7372800)
  [PASS] container-zero-copy - frames=241/241, D2H=0, H2D=0, zero_copy=True

4/4 checks passed
```

ACT-04 曾造成的回退（`cuvidMapVideoFrame64 -> 205`）已消除：raw 路径与容器
路径（8-bit 与 10-bit Main10）均正常，241/241 帧 `D2H=0, H2D=0, zero_copy=True`。

### 3.2 研发机（GTX 970M, sm_52）—— 并行证据

```text
> pwsh tools/verify_gate1.ps1 -BuildDir build/rework-zig `
       -Container sample.mp4 -Codec hevc -BitDepth 8
  [PASS] unit-tests - [==========] 102 test(s) run, 0 failed (3086 ms)
  [PASS] probe - NVIDIA GeForce GTX 970M; caps=nvdec
  [PASS] raw-nv12-to-p010 - exit=0, 7372800 bytes (expected 7372800)
  [PASS] container-zero-copy - frames=241/241, D2H=0, H2D=0, zero_copy=True

4/4 checks passed
```

（10-bit Main10 路径 GM204 无 HEVC 引擎，无法在该机覆盖，已由验收宿主补足。）

### 3.3 W4 互操作闭环日志（验收宿主 RTX 5060, sm_120，RTX_BRIDGE_TRACE=1）

```text
[rtx_ngx] selftest: enter
[rtx_ngx] selftest: cuda context
[rtx_ngx] selftest: cuda context ready
[rtx_ngx] selftest: luid query
[rtx_ngx] create: begin
[rtx_ngx] create: device ready
[rtx_ngx] create: cuda context bound
[rtx_ngx] create: input texture ready          (R10G10B10A2, 4 字节/像素)
[rtx_ngx] create: output texture ready         (RGBA16F, 8 字节/像素)
[rtx_ngx] selftest: upload                     (D3D12 写入渐变)
[rtx_ngx] selftest: d3d12->cuda copy           (CUDA 经导入表面读取, 一致)
[rtx_ngx] selftest: cuda->d3d12 copy           (CUDA XOR 修改并写回)
[rtx_ngx] selftest: download                   (D3D12 回读, 一致)
selftest(256, 256) -> 0
  detail: round trip OK: 256x256 RGBA16F, D3D12 -> CUDA -> D3D12
```

同一宿主上以生产画质 1920×1080 复跑亦通过：

```text
selftest(1920, 1080) -> 0
  detail: round trip OK: 1920x1080 RGBA16F, D3D12 -> CUDA -> D3D12
```

（研发机 GTX 970M 复跑 256×256 同样返回 `RTX_OK`，日志逐行一致。）

---

## 4. 变更文件清单

| 文件 | 变更 |
| --- | --- |
| `src/shim/ngx_bridge/d3d12_interop.cpp` | RW2-01：格式描述符表 + 不变量校验、`_v2` 符号优先、上下文绑定检查、写回后流同步、特性级别回退、`interopPrerequisitesAvailable()` |
| `src/shim/ngx_bridge/d3d12_interop.h` | 声明 `interopPrerequisitesAvailable()` |
| `src/shim/ngx_bridge/bridge_exports.cpp` | RW2-05：`query_caps` 如实区分互操作可用性与 NGX SDK 缺失；诊断文案 |
| `src/shim/loader/nv_decoder.cpp` | RW2-02：`ulNumOutputSurfaces = max(slots.size(), 8)` |
| `tests/test_shim.cpp` | RW2-03：新增 `shim_ngx_d3d12_interop_selftest` 与宿主能力探测辅助函数 |
| `docs/testing.md`、`README.md`、`docs/gate2.md` | 测试计数 101→102、覆盖表、Phase 2 状态、返工结果记录 |

---

## 5. 如实声明的未决事项

1. **ACT-07 Nsight Trace**：两台宿主机均未安装 `nsys`，1000 帧 Trace 仍阻塞。
2. **W5（VSR + TrueHDR）**：两台宿主机均无专有 RTX Video SDK，保持挂起
   （`RTX_ERR_NGX` 桩降级 + 指名缺失 DLL 的友好诊断）。
3. **W6 调度器合流**：依赖 W5，未动工。
4. **Gate 2.1 / 2.3 / 2.4**：依赖 W5 与 Nsight，维持 Not met。

**结论**：RW2-01 … RW2-05 五项返工任务已完成，二阶段复验任务书 §4 的全部 4 项
门禁均已在指定验收宿主 `[REDACTED_HOST]`（RTX 5060, sm_120）实测满足：

| 门禁 | 结果 |
| --- | --- |
| 1. Gate 1 无回退 | **满足**——验收宿主 8-bit 与 10-bit Main10 均 4/4 PASS |
| 2. W4 互操作自测绿灯 | **满足**——`selftest(256,256)` 返回 `RTX_OK`，detail 含 `round trip OK: 256x256 RGBA16F, D3D12 -> CUDA -> D3D12` |
| 3. 自动化测试无死角 | **满足**——`rtx_tests.exe` 102/102 含互操作自测，CTest 4/4 |
| 4. 合规与导入表 | **满足**——`check_spdx.ps1` PASS，`audit_imports.ps1` 5 个二进制 0 静态导入 |

在 W5/W6 环境依赖（RTX Video SDK、Nsight）解除前，仍不得向领域专家提交二阶段
终审；W4 准出意见已具备签署条件。
