# CineRTX Phase 12（Wave 12.1）施工汇报：构建净化与微观卫生清剿

- **依据施工单**：[`docs/phase12/03_CineRTX Phase 12 SuperPower 施工单_构建净化与微观卫生清剿.md`](./03_CineRTX%20Phase%2012%20SuperPower%20施工单_构建净化与微观卫生清剿.md)（`CINERTX-P12-WORKORDER-003`）
- **同步基线**：施工前已从工作区重新同步全部 11 个最新存量文件，均以 `*.pre12_03.bak` 重命名留底后原位替换，并新建 `src/shim/ngx_bridge/nv_func_tables.h/.cpp`（共 13 个文件）；未执行 `git commit` / `git push`。
- **总体结论**：6 项微观卫生病灶（`HYG-P12-01` ~ `HYG-P12-06`）已全部清零并通过 13/13 静态合规/负向正则门禁及 GCC `-Werror` C++23 整数显存状态机实测；Windows MSVC `/WX` 全量编译、146/146 宿主单测及桌面交互验活留待 Windows 宿主机执行。

## 一、六大任务施工清点（W12.1-1 ~ W12.1-6）

| 任务编号 | 对应病灶 | 核心改造内容 | 涉及文件（仓库相对路径） | 状态 |
| :---: | :---: | :--- | :--- | :--- |
| **W12.1-1** | `HYG-P12-04` | 删除 `CMakeLists.txt` 配置期静默 `git submodule update` 并改为 `FATAL_ERROR` 离线硬拦截；移除全局 `/wd4324`，升级 MSVC `/WX` 与 GCC/Clang `-Werror`；在 `telemetry_bus.h` 的 `alignas(64)` 结构体紧邻处加精确 `#pragma warning(push/disable:4324/pop)`；注册 `nv_func_tables.cpp`。 | [`CMakeLists.txt`](../../CMakeLists.txt)、[`include/rtx/telemetry_bus.h`](../../include/rtx/telemetry_bus.h) | 已完成 |
| **W12.1-2** | `HYG-P12-01` | 物理删除 `nv_encoder.cpp` 中单缓冲遗留字段（`bitstream`、`completion_event`、`async_event`、`async_registered`）；为每个 `BitstreamSlot` 封装独立 RAII `UniqueEventHandle` 与 1:1 `nvEncRegisterAsyncEvent`；销毁严格按「注销异步事件 → 销毁比特流缓冲 → 自动关闭事件句柄」顺序执行；`encodeFrame()` 与 `rtx_shim_encoder_flush()` 严格按槽位索引等待对应事件；清零隐藏环境变量 `RTX_NVENC_UHQ`/`RTX_NVENC_BFRAMES`。 | [`src/shim/loader/nv_encoder.cpp`](../../src/shim/loader/nv_encoder.cpp) | 已完成 |
| **W12.1-3** | `HYG-P12-02` | 新建 `src/shim/ngx_bridge/nv_func_tables.h/.cpp`（`MIT OR Apache-2.0`）统一维护 `NvCudaDriverTable`；删除 `rife_engine.cpp` 内重复 `CudaDriverApi`/`g_cuda`；彻底拔除 `readEnvValue` 与 `std::getenv` 隐式开关旁路。 | [`src/shim/ngx_bridge/nv_func_tables.h`](../../src/shim/ngx_bridge/nv_func_tables.h)、[`src/shim/ngx_bridge/nv_func_tables.cpp`](../../src/shim/ngx_bridge/nv_func_tables.cpp)、[`src/shim/ngx_bridge/rife_engine.cpp`](../../src/shim/ngx_bridge/rife_engine.cpp) | 已完成 |
| **W12.1-4** | `HYG-P12-03` | `VramScheduler` 合并冗余双计数器为单一饱和 `uint32_t dwell_frames_`；水位线比较改为 128 位防溢出纯整数交叉乘法（`__uint128_t` / `_umul128`）配合整数百分比常量 `82%` / `68%`，消除浮点舍入误差。 | [`include/rtx/vram_scheduler.h`](../../include/rtx/vram_scheduler.h)、[`src/core/vram_scheduler.cpp`](../../src/core/vram_scheduler.cpp)、[`tests/test_vram_scheduler.cpp`](../../tests/test_vram_scheduler.cpp) | 已完成 |
| **W12.1-5** | `HYG-P12-05` | 彻底拔除全局可变单例（`src/gui/` 全量 0 命中）；由 `wWinMain` 持有 `std::unique_ptr<WorkstationApp>` 并在主窗口与时间轴子画布的 `WM_NCCREATE` 经 `CREATESTRUCTW::lpCreateParams` 写入 `GWLP_USERDATA`、`WM_NCDESTROY` 清零 `0`，杜绝迟到消息 UAF。 | [`src/gui/workstation_canvas.h`](../../src/gui/workstation_canvas.h)、[`src/gui/workstation_canvas.cpp`](../../src/gui/workstation_canvas.cpp)、[`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp) | 已完成 |
| **W12.1-6** | `HYG-P12-06` | `drawBrandMark()` 改为零分配 `GetStockObject(DC_BRUSH)` + `SetDCBrushColor`；`raii_owners.h` 补充 `UniqueBitmap` 与 `makeSolidBrush()` 工厂；`paintTimelineCanvas()` 与 `rtx_gui.cpp` 全量改用 RAII 句柄，`src/gui/` 下裸 `CreateSolidBrush`/`CreatePen`/`DeleteObject` 彻底清零；内部辅助函数收口为 `WorkstationApp&` 消除冗余判空。 | [`include/rtx/raii_owners.h`](../../include/rtx/raii_owners.h)、[`src/gui/workstation_canvas.cpp`](../../src/gui/workstation_canvas.cpp)、[`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp) | 已完成 |

## 二、本地静态门禁与实测结果

| 验收门禁项 | 验证口径 / 命令 | 实测结果 |
| :--- | :--- | :--- |
| **Gate 12.1-2（环境变量清零）** | `std::getenv\|_dupenv_s\|GetEnvironmentVariable` 扫描修改范围内全部 `include/` 与 `src/` | **0 命中** |
| **Gate 12.1-2（全局状态清零）** | 全局单例变量名扫描 `include/` 与 `src/` | **0 命中** |
| **Gate 12.1-2（裸 GDI 清零）** | `CreateSolidBrush\|CreatePen\|DeleteObject` 扫描 `src/gui/` | **0 命中** |
| **Gate 12.1-3（SPDX 与类型红线）** | `src/shim/` 100% `MIT OR Apache-2.0`，其余 `GPL-3.0-or-later WITH Plugin-Exception`；`NvCudaDriverTable` 替换 `CudaDriverApi` 净增 0 个具名类型 | **通过（13/13）** |
| **W12.1-4 C++23 `-Werror` 实测** | `g++ -std=c++23 -O2 -Wall -Wextra -Werror` 编译运行 `vram_scheduler.cpp` | **PASS**（0 警告，`high82=5986 MiB`，`low68=4964 MiB`，5 级降级与 120 帧滞后全绿） |

## 三、阻塞项与宿主机终验命令（Gate 12.1-1 ~ 12.1-4）

当前沙箱为无 Windows/MSVC/CUDA/Win32 桌面的 Linux 环境，请在 Windows 宿主机执行以下终验闭环：

```powershell
cmake --preset windows-msvc-ninja
cmake --build --preset windows-msvc-ninja
.\build\rtx_tests.exe
cmake -P cmake/AuditTypes.cmake; cmake -P cmake/AuditSpdx.cmake; cmake -P cmake/AuditSloc.cmake; cmake -P cmake/AuditImports.cmake
```
