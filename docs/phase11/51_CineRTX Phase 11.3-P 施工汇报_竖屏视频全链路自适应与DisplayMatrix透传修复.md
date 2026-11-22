# CineRTX Phase 11.3-P 施工汇报：竖屏视频全链路自适应与 DisplayMatrix 透传修复

- **依据施工单**：[`docs/phase11/50_CineRTX Phase 11.3-P SuperPower 施工单_竖屏视频全链路自适应与DisplayMatrix透传修复.md`](./50_CineRTX%20Phase%2011.3-P%20SuperPower%20施工单_竖屏视频全链路自适应与DisplayMatrix透传修复.md)（排查依据：[`49_CineRTX_Phase11_竖屏视频无法转码根因法医排查与修复方案.md`](./49_CineRTX_Phase11_竖屏视频无法转码根因法医排查与修复方案.md)）
- **执行方式**：16 个存量文件均以 `*.pre50.bak` 重命名留底后原位替换，新增 1 个单元测试文件 `tests/test_geometry.cpp`（共 17 个文件）；未执行任何 `git commit` / `git push`。
- **总体结论**：6 大核心修复任务（`RC-PORTRAIT-01` ~ `RC-PORTRAIT-06` + 单元测试矩阵）已全部完成，静态合规门禁 17/17 通过，竖屏全档位几何契约与 CUDA 预览居中自适应映射在 Linux GCC C++23 环境下 100% 实测通过；Windows MSVC 全量构建、TRT 引擎物理重建及 RTX 5060 真机验证留待宿主机执行。

## 一、六大任务施工清点

| 任务模块 | 核心改造内容 | 涉及文件（仓库相对路径） | 状态 |
| :--- | :--- | :--- | :--- |
| **任务 1：全档位竖屏几何契约与预设覆写 (`RC-PORTRAIT-01` / `RC-PORTRAIT-04`)** | `param_schema.cpp` 无条件按 `4k`/`2k`/`fhd`/`native` 覆写 `target_width`/`target_height` 消除旧残留；`geometry.cpp` 实现全档位 9:16 等比竖屏翻转、偶数对齐 `(v + 1) & ~1`、源尺寸保底与 Pillarbox 居中黑边计算；`hw_transcode.cpp` 锁定 `native`（`0x0`）原画直通、全档位 AutoDirect 翻转及 Pillarbox 越界防御；从 `RtxNgxBridgeConfig::reserved[64]` 无损切分 `output_offset_x/y` + `scaled_width/height`（保持 ABI 尺寸不变）透传至 `NgxD3D12Bridge::process()` 的 `OutputSubrectBase`/`OutputSubrectSize`。 | [`src/core/param_schema.cpp`](../../src/core/param_schema.cpp)、[`src/core/geometry.cpp`](../../src/core/geometry.cpp)、[`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp)、[`src/core/hw_transcode_stages.cpp`](../../src/core/hw_transcode_stages.cpp)、[`include/shim/ngx_bridge_abi.h`](../../include/shim/ngx_bridge_abi.h)、[`src/shim/ngx_bridge/ngx_d3d12_bridge.h`](../../src/shim/ngx_bridge/ngx_d3d12_bridge.h)、[`src/shim/ngx_bridge/ngx_d3d12_bridge.cpp`](../../src/shim/ngx_bridge/ngx_d3d12_bridge.cpp) | 已完成 |
| **任务 2：RIFE TensorRT Profile 动态维度扩展 (`RC-PORTRAIT-02`)** | `rife_builder.cpp` 将动态优化 Profile 上限对称放开至 `max_w = max(2176, opt_w)`、`max_h = max(2176, opt_h)`（64 字节对齐）；`rife_engine.cpp` 在 `fits` 校验中增加越界前置诊断，旧引擎遇超界竖屏输入时明确提示重建 `2176x2176` 引擎而非静默崩溃。 | [`src/shim/ngx_bridge/rife_builder.cpp`](../../src/shim/ngx_bridge/rife_builder.cpp)、[`src/shim/ngx_bridge/rife_engine.cpp`](../../src/shim/ngx_bridge/rife_engine.cpp) | 已完成 |
| **任务 3：FFmpeg DisplayMatrix 旋转侧数据透传 (`RC-PORTRAIT-03`)** | `MediaInfo` 新增 `std::array<int32_t, 9> display_matrix{}` 与 `bool has_display_matrix{false}`；`FfmpegDemuxer::open()` 扫描 `codecpar->coded_side_data` 读取 36 字节 `AV_PKT_DATA_DISPLAYMATRIX`；`FfmpegMuxer::open()` 原样经 `av_packet_side_data_add` 注入输出流。 | [`include/rtx/io.h`](../../include/rtx/io.h)、[`src/io/ffmpeg_io.cpp`](../../src/io/ffmpeg_io.cpp) | 已完成 |
| **任务 4：NLE 预归一化与 CUDA 预览等比自适应 (`RC-PORTRAIT-05`)** | `onNleExportSequence()` 在调用 `profileToHwOptions(profile)` 前显式执行 `(void)rtx::validateAndNormalizeProfile(profile, false);`；`p010_pack.cu` 以等比居中自适应映射取代硬编码 `x0 >> 2, y0 >> 2`，彻底消除竖屏下半屏 43.75% 裁切。 | [`src/gui/workstation_canvas.cpp`](../../src/gui/workstation_canvas.cpp)、[`src/shim/ngx_bridge/p010_pack.cu`](../../src/shim/ngx_bridge/p010_pack.cu) | 已完成 |
| **任务 5：NVENC 高度 Caps 探测与 High Tier 提升 (`RC-PORTRAIT-06`)** | `nv_encoder.cpp` 初始化前通过 `nvEncGetEncodeCaps` 探测 `NV_ENC_CAPS_WIDTH_MAX`/`NV_ENC_CAPS_HEIGHT_MAX`；当 `height > 2160` 时对 AV1/HEVC 自动提升至 `tier = 1 (High)`；`nv_encoder.cpp` 与 `ngx_d3d12_bridge.cpp` 失败日志均打印分辨率尺寸与原始 API 返回值/驱动错误原文。 | [`src/shim/loader/nv_encoder.cpp`](../../src/shim/loader/nv_encoder.cpp)、[`src/shim/ngx_bridge/ngx_d3d12_bridge.cpp`](../../src/shim/ngx_bridge/ngx_d3d12_bridge.cpp) | 已完成 |
| **任务 6：全档位竖屏单元测试矩阵** | 新增 `tests/test_geometry.cpp`（覆盖 `1080x1920` / `720x1280` 全档位、`2k -> 4k -> fhd -> native -> 4k` 来回切换、4K 竖屏保底与 Pillarbox）；在 `tests/test_io.cpp` 新增 90° `DisplayMatrix` 36 字节写读往返单测；在 `CMakeLists.txt` 注册 `tests/test_geometry.cpp`。 | [`tests/test_geometry.cpp`](../../tests/test_geometry.cpp)、[`tests/test_io.cpp`](../../tests/test_io.cpp)、[`CMakeLists.txt`](../../CMakeLists.txt) | 已完成 |

## 二、本地静态门禁与沙箱实测结果

### 1. 静态合规门禁（17/17 文件全量通过）

| 检查项 | 门禁口径 | 结果 |
| :--- | :--- | :--- |
| SPDX 许可头 | 17/17 文件均含 `SPDX-License-Identifier` | 17/17 通过 |
| 脱敏审计 | 零 外部私有云盘、零 本地绝对URI 路径残留 | 0 违规 |
| 现代 C++ 类型红线 (`[DEC-009]`) | 全程零新增自定义 `struct`/`class`，严格守住 153 个命名类型上限 | 0 违规 |
| 动态隔离红线 (`[DEC-010]`) | `CMakeLists.txt` 保持 `rtx_ngx_bridge SHARED` 物理隔离，零专有静态符号回流 | 通过 |
| ABI 二进制兼容性 | `RtxNgxBridgeConfig` 新增 4×`int32_t` 均从 `reserved[64]` 切分，剩余 `reserved[48]`，结构体总尺寸不变 | 通过 |

### 2. 竖屏几何与预览映射实测（GCC C++23 `-O2 -Wall -Wextra`，0 警告）

| 测试项 | 输入 / 预设 | 实测输出 | 结论 |
| :--- | :--- | :--- | :--- |
| `1080x1920` AutoDirect | `4k` / `2k` / `fhd` | `2160x3840` / `1440x2560` / `1080x1920`（严格 9:16、偶数、`>= src`） | 通过 |
| `720x1280` AutoDirect | `4k` / `2k` / `fhd` | `2160x3840` / `1440x2560` / `1080x1920`（严格 9:16、偶数、`>= src`） | 通过 |
| `2160x3840` 源尺寸保底 | `fhd` (`1920x1080`) | `2160x3840`（无降采样倒挂） | 通过 |
| `1080x1920` Pillarbox | `3840x2160` 画布 | `scaled=1214x2160`，`offset_x=1312`，`L=1312, R=1314`（居中之和 3840） | 通过 |
| CUDA 预览自适应映射 (`960x540`) | `3840x2160` | `dst=960x540 @(0,0)`，覆盖 `518400/518400`，`dup=0` | 通过 |
| CUDA 预览自适应映射 (`960x540`) | `2160x3840` / `1440x2560` / `1080x1920` | `dst=304x540 @(328,0)`，覆盖 `164160/164160`，`dup=0`，零截断 | 通过 |

## 三、阻塞项与宿主机接手验收清单

1. **沙箱环境限制（非代码缺陷）**：当前沙箱无 Windows/MSVC/CUDA/NVENC/TensorRT/FFmpeg 完整工具链，无法本地跑通 `ninja -C build rtx_tests` 与 RTX 5060 真机实测。
2. **宿主机接手必跑项**：
   - 全量编译与单测：`ninja -C build rtx_tests && .\build\rtx_tests.exe`（验证 `geometry_*` 与 `io_display_matrix_passthrough_roundtrip` 全绿）。
   - 四大 CMake 门禁：`cmake -P cmake/AuditTypes.cmake`、`AuditSloc.cmake`、`AuditSpdx.cmake`、`AuditImports.cmake`。
   - RIFE 引擎重建：在 RTX 5060 上执行 `--build-rife-engine` 生成 `2176x2176` 动态 Profile 新版 `models/rife_v4.25.engine`。
   - 真实竖屏穿透验证：以含 `DisplayMatrix` 的实拍 `1080x1920` 视频跑通 `4k`/`2k`/`fhd`/`native` 及 `60-rife`，并用 `ffprobe` 校验分辨率、旋转矩阵与 `D2H == 0 B`。
