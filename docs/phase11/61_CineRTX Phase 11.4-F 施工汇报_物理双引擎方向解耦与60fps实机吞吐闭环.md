# CineRTX Phase 11.4-F 施工汇报：物理双引擎方向解耦与 60fps 实机吞吐闭环

- **依据施工单**：[`docs/phase11/60_CineRTX Phase 11.4-F SuperPower 施工单_物理双引擎方向解耦与60fps实机吞吐闭环.md`](./60_CineRTX%20Phase%2011.4-F%20SuperPower%20施工单_物理双引擎方向解耦与60fps实机吞吐闭环.md)（法医依据：[`59_CineRTX Phase 11.4 RIFE双Profile性能崩塌法医终审裁决书暨Phase 11.4-F抢险与Phase 12准入方案.md`](./59_CineRTX%20Phase%2011.4%20RIFE双Profile性能崩塌法医终审裁决书暨Phase%2011.4-F抢险与Phase%2012准入方案.md)）
- **同步基线**：施工前已从工作区重新同步最新 `rife_builder.cpp`、`rife_engine.cpp`、`test_rife_scale05.cpp` 与 `CMakeLists.txt`；3 个目标文件均以 `*.pre60.bak` 重命名留底后原位替换，未执行 `git commit` / `git push`。
- **总体结论**：代码侧改造（`PKG-F2`、`PKG-F3`、`PKG-F4` 单测门禁）已全部落盘并通过静态门禁；因执行沙箱无宿主机 `.vs/build` 本地构建目录及物理 RTX 5060 GPU，脏引擎 SHA-256 固化清理（`PKG-F1`）、串行重建双引擎及 60fps 实机压测留待宿主机执行并回填。

## 一、四大任务包施工清点

| 任务包 | 核心改造内容 | 涉及文件（仓库相对路径） | 状态 |
| :---: | :--- | :--- | :--- |
| **PKG-F1** | 脏引擎 SHA-256 哈希固化与物理清除（`.vs/build/.../rife_v4.25.engine`，42.3 MB 污染产物） | 宿主机构建/部署目录（沙箱不可见） | 待宿主机执行（见第三节命令） |
| **PKG-F2** | 彻底拔除 `rife_builder.cpp` 中双 Profile `add_profile` 逻辑，恢复单一 `IOptimizationProfile`；按入参方向自动生成横屏 Profile（`min=[1,c,128,128]`、`opt=[1,c,1152,1920]`、`max=[1,c,2176,3840]`）或竖屏 Profile（`min=[1,c,128,128]`、`opt=[1,c,1920,1152]`、`max=[1,c,3840,2176]`），严守 `kWORKSPACE = 1 GiB`（`1ULL << 30`）防 OOM 剪枝。 | [`src/shim/ngx_bridge/rife_builder.cpp`](../../src/shim/ngx_bridge/rife_builder.cpp) | 已完成 |
| **PKG-F3** | 在 `rife_engine.cpp` 实现方向感知路由（优先检索/构建 `rife_v4.25_portrait.engine` 与 `rife_v4.25_landscape.engine`；横屏向下兼容仅允许 `20 MB ~ 32 MB` 旧版 `rife_v4.25.engine`，严格拒载 `> 35 MB` 污染产物）；彻底拔除 `setOptimizationProfileAsync` 动态切换并硬校验 `getNbOptimizationProfiles() == 1`；删除写死的 `constexpr uint32_t kAlign = 64;` 并统一收口至 `align_mod_`（默认 `128u`）；确认 `interpolate()` 热循环全程透传专属 CUDA `stream`。 | [`src/shim/ngx_bridge/rife_engine.cpp`](../../src/shim/ngx_bridge/rife_engine.cpp) | 已完成 |
| **PKG-F4** | 在 `tests/test_rife_scale05.cpp` 追加 `RTX_TEST(rife_trt_throughput_dual_engine_gate)`：通过 `NgxRemasterBridge` 动态加载（守住 `[DEC-010]` 零专有静态符号红线），依次对横屏 `3840x2160`（`rife_v4.25_landscape.engine`）与竖屏 `2160x3840`（`rife_v4.25_portrait.engine`）执行体积 `< 32 MB`、`trt_peak < 3200 MiB`、5 帧预热 + 60 帧连续真实 `interpolateRife()` 计时硬断言（均帧 `<= 16.67 ms` / `>= 60 fps`）。 | [`tests/test_rife_scale05.cpp`](../../tests/test_rife_scale05.cpp) | 代码已完成（实机压测待宿主机回填） |

## 二、本地静态合规门禁核验（3/3 全通过）

| 门禁检查项 | 验证口径 | 结果 |
| :--- | :--- | :--- |
| SPDX 许可头 | 3/3 修改文件均保留合法 SPDX 许可标识 | 通过 |
| 脱敏审计 | 零 外部私有云盘、零 本地绝对URI 路径残留 | 0 违规 |
| 双 Profile 彻底肃清 | `rife_builder.cpp` 零 `add_profile`；`rife_engine.cpp` 零 `setOptimizationProfileAsync` 且强校验 `nb_profiles == 1` | 通过 |
| 对齐模数收口 | `rife_engine.cpp` 零 `constexpr uint32_t kAlign = 64;`，统一收口至 `align_mod_`（默认 `128u`） | 通过 |
| 方向路由与防污染红线 | `rife_engine.cpp` 完整具备 `rife_v4.25_portrait.engine` / `rife_v4.25_landscape.engine` 寻址及 `> 35 MB` 拒载守卫 | 通过 |
| 动态隔离红线 (`[DEC-010]`) | `test_rife_scale05.cpp` 经 `NgxRemasterBridge` 动态调用，零新增专有静态依赖 | 通过 |

## 三、宿主机硬件执行与门禁回填清单（PKG-F1 & GATE-F1 ~ GATE-F5）

因当前执行环境为无宿主机本地构建目录及物理 RTX 5060 GPU 的 Linux 沙箱，请协调负责人在 Windows 宿主机按序执行以下 4 步并回填实测物证（严禁虚构跑分）：

1. **PKG-F1 脏引擎哈希封存与清理**：
   ```powershell
   Get-FileHash .vs\build\cmake\windows-msvc-ninja\models\rife_v4.25.engine -Algorithm SHA256
   Remove-Item -Force .vs\build\cmake\windows-msvc-ninja\models\rife_v4.25.engine, .vs\build\external\models\rife_v4.25.engine -ErrorAction SilentlyContinue
   ```
2. **PKG-F2 串行重建横竖屏双单 Profile 引擎（验证 `GATE-F1` 体积 `20~30 MB` 与 `GATE-F2` 零 OOM）**：
   ```powershell
   ninja -C build CineRTX rtx_ngx_bridge rtx_tests
   .\build\CineRTX.exe --build-rife-engine --onnx models\rife_v4.25.onnx --engine models\rife_v4.25_landscape.engine --width 1920 --height 1152
   .\build\CineRTX.exe --build-rife-engine --onnx models\rife_v4.25.onnx --engine models\rife_v4.25_portrait.engine  --width 1152 --height 1920
   Get-Item models\rife_v4.25_landscape.engine, models\rife_v4.25_portrait.engine | Select-Object Name, Length
   ```
3. **GATE-F3 / GATE-F5 全量单测与四门禁平账**：
   ```powershell
   .\build\rtx_tests.exe
   cmake -P cmake/AuditTypes.cmake; cmake -P cmake/AuditSloc.cmake; cmake -P cmake/AuditSpdx.cmake; cmake -P cmake/AuditImports.cmake
   ```
4. **GATE-F4 4K 横竖屏 60fps 端到端实测待回填表**：

| 场景 | 引擎文件 | 引擎体积 (MB) | 构建期峰值显存 (GB) | 单测均帧耗时 (ms) | 端到端转码速率 (fps) | 峰值显存 (MiB) | `D2H` |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| 横屏 4K (`3840x2160`) | `rife_v4.25_landscape.engine` | 待实测 (`20~30`) | 待实测 (`<= 2.6`) | 待实测 (`<= 16.67`) | 待实测 (`>= 60`) | 待实测 (`<= 5986`) | `0 B` |
| 竖屏 4K (`2160x3840`) | `rife_v4.25_portrait.engine` | 待实测 (`20~30`) | 待实测 (`<= 2.6`) | 待实测 (`<= 16.67`) | 待实测 (`>= 60`) | 待实测 (`<= 5986`) | `0 B` |
