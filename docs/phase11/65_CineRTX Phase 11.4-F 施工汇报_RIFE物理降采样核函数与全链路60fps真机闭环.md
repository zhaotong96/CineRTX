# CineRTX Phase 11.4-F 第二波次施工汇报：RIFE 物理降采样核函数与全链路 60fps 闭环

- **依据施工单**：[`docs/phase11/64_CineRTX Phase 11.4-F SuperPower 施工单_RIFE物理降采样核函数与全链路60fps真机闭环.md`](./64_CineRTX%20Phase%2011.4-F%20SuperPower%20施工单_RIFE物理降采样核函数与全链路60fps真机闭环.md)（法医依据：[`63_CineRTX Phase 11.4-F 4K显存击穿与RIFE空壳降采样二次法医终审裁决书暨全链路60fps根治方案.md`](./63_CineRTX%20Phase%2011.4-F%204K显存击穿与RIFE空壳降采样二次法医终审裁决书暨全链路60fps根治方案.md)）
- **同步基线**：施工前已从工作区重新同步全部 10 个最新源码/PTX/单测文件，所有修改均以 `*.pre64.bak` 重命名留底后原位替换，未执行 `git commit` / `git push`。
- **总体结论**：五大任务包（`PKG-G1` ~ `PKG-G5`）代码与 PTX 7.5 同步改造已全部落盘并通过静态门禁与 C++23 重采样/`kOPT` 数学实测；因沙箱无 `nvcc` 及物理 RTX 5060 GPU，宿主机可选 `nvcc` 重编译 PTX、`rtx_tests.exe` 实机 60fps 门禁及 4K 端到端压测留待宿主机回填。

## 一、五大任务包施工清点

| 任务包 | 核心改造内容 | 涉及文件（仓库相对路径） | 状态 |
| :---: | :--- | :--- | :--- |
| **PKG-G1** | 在 `rife_pack.cu` 与 `rife_pack.ptx`（保守 `.version 7.5` / `.target sm_75`）中为预处理/后处理核函数追加 `infer_width`/`infer_height` 参数；预处理落地 `2x2` 盒式面积均值 Luma 降采样 + `4:2:0` 直采 UV；后处理落地半像素中心对齐 2D 双线性上采样，严格钳制于 `[0, infer_width-1] x [0, infer_height-1]` 绝不采样填充区。 | [`src/shim/ngx_bridge/rife_pack.cu`](../../src/shim/ngx_bridge/rife_pack.cu)、[`src/shim/ngx_bridge/rife_pack.ptx`](../../src/shim/ngx_bridge/rife_pack.ptx) | 已完成 |
| **PKG-G2** | `RifePreprocessLaunch` 与 `RifePostprocessLaunch` 追加 `infer_width`/`infer_height`；`launchRifePreprocess`/`launchRifePostprocess` 自动回退至 `width`/`height` 并透传至 `cuLaunchKernel` `args[]`。 | [`src/shim/ngx_bridge/rife_pack_runtime.h`](../../src/shim/ngx_bridge/rife_pack_runtime.h)、[`src/shim/ngx_bridge/rife_pack_runtime.cpp`](../../src/shim/ngx_bridge/rife_pack_runtime.cpp) | 已完成 |
| **PKG-G3** | `rife_engine.h/.cpp` 真正消费 `scale_` 推导 `infer_width_`/`infer_height_` 并按 `align_mod_`（128u）对齐，横屏 4K（`3840x2160 @ 0.5`）精准生成 `1920x1152`、竖屏 4K（`2160x3840 @ 0.5`）精准生成 `1152x1920`，100% 命中 60 号单 Profile 引擎 `kOPT`；`interpolate()` 透传 `infer_width_`/`infer_height_`；`bridge_exports.cpp` 输出缓冲高度改为取 `impl->rife_engine->height()`。 | [`src/shim/ngx_bridge/rife_engine.h`](../../src/shim/ngx_bridge/rife_engine.h)、[`src/shim/ngx_bridge/rife_engine.cpp`](../../src/shim/ngx_bridge/rife_engine.cpp)、[`src/shim/ngx_bridge/bridge_exports.cpp`](../../src/shim/ngx_bridge/bridge_exports.cpp) | 已完成 |
| **PKG-G4** | 物理删除 `hw_transcode_internal.h` / `hw_transcode_stages.cpp` 中坏死 `anchor_p010_[2]`（净释放 `49.76 MiB` 显存），插帧一律读取 `prev_f.planes[0]` / `curr_f.planes[0]`；空间放大（如 1080p→4K）强制路由真 `fg-first`（1080p `scale=1.0` + VSR 2x），同分辨率 4K→4K 启用物理 `scale=0.5f`；将 `VramScheduler vram_scheduler_` 提升为 `RemasterPipeline` 持久化成员并在 `handleDecodedFrame()` 逐帧调用 `observeFrame()`。 | [`src/core/hw_transcode_internal.h`](../../src/core/hw_transcode_internal.h)、[`src/core/hw_transcode_stages.cpp`](../../src/core/hw_transcode_stages.cpp) | 已完成 |
| **PKG-G5** | `tests/test_rife_scale05.cpp` 增加物理半分辨率 `kOPT` 维度硬断言（`1920x1152` / `1152x1920`），收紧 `trt_peak_bytes <= 900 MiB` 与均帧耗时 `<= 15.0 ms`（`>= 66 fps`）硬门禁。 | [`tests/test_rife_scale05.cpp`](../../tests/test_rife_scale05.cpp) | 代码完成（实机计时待宿主机回填） |

## 二、本地静态门禁与 C++23 数学实测结果

| 验证项 | 实测结果 | 结论 |
| :--- | :--- | :--- |
| SPDX / 脱敏门禁（10/10 文件） | 9/9 源码文件具备合法 SPDX 头，0 外部私有云盘/本地绝对URI残留 | 通过 |
| PTX 参数同步检查 | `rife_pack.ptx` 已含 `rife_nv12_to_nchw_7ch_fp32_param_11/12` 与 `rife_nchw_3ch_fp32_to_nv12_param_8/9`，保持 `.version 7.5` / `.target sm_75` | 通过 |
| `kOPT` 张量维度实测 | `3840x2160 @ 0.5 -> 1920x1152`；`2160x3840 @ 0.5 -> 1152x1920`（逐像素精确命中 `kOPT`） | 通过 |
| 2x2 降采样 + 双线性上采样往返实测 | 填充区注入 `-999.0f` 哨兵后零越界采样，重建最大误差 `max_err = 0.001953` | 通过 |

## 三、宿主机硬件复验与待回填清单（GATE-G1 ~ GATE-G5）

1. **可选 `nvcc` 同步复编 PTX & 全量构建**：
   ```powershell
   nvcc -ptx -arch=sm_75 -O3 "src\shim\ngx_bridge\rife_pack.cu" -o "src\shim\ngx_bridge\rife_pack.ptx"
   ninja -C build rtx_ngx_bridge CineRTX rtx_tests
   .\build\rtx_tests.exe
   ```
2. **RTX 5060 实机 60+ fps 与 82% 水位线待回填表**：

| 场景 | 拓扑路由 | RIFE 张量维度 | RIFE 峰值显存 (MiB) | 单测均帧 (ms) | 端到端速率 (fps) | 系统峰值显存 (MiB) | `D2H` |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| 横屏 4K→4K (`3840x2160`) | `4k-scale05` | `1920x1152` | 待实测 (`<= 900`) | 待实测 (`<= 15.0`) | 待实测 (`>= 60`) | 待实测 (`<= 5986`) | `0 B` |
| 竖屏 4K→4K (`2160x3840`) | `4k-scale05` | `1152x1920` | 待实测 (`<= 900`) | 待实测 (`<= 15.0`) | 待实测 (`>= 60`) | 待实测 (`<= 5986`) | `0 B` |
| 1080p→4K 放大 | `fg-first` (`scale=1.0`) | `1920x1152` | 待实测 (`<= 900`) | 待实测 (`<= 15.0`) | 待实测 (`>= 60`) | 待实测 (`<= 5986`) | `0 B` |
