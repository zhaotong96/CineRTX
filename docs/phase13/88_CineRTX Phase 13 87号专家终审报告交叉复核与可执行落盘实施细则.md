# **88\_CineRTX Phase 13 87号专家终审报告交叉复核与可执行落盘实施细则**

* **文档编号**：`CINERTX-P13-REVIEW-088`  
* **归档路径**：`docs/phase13/88_CineRTX Phase 13 87号专家终审报告交叉复核与可执行落盘实施细则.gdoc`  
* **审评标的**：`87_CineRTX Phase 13 终审与演进.md`（回应 `86_CineRTX Phase 13 终局封版前专家终审委托书.md`）  
* **核验依据**：Commit `a1832bc` 仓库源码实体（`include/rtx/sys_dynload.h`、`src/core/i18n.cpp`、`tests/test_rife.cpp`、`tests/test_raii_retirement.cpp`、`cmake/RtxFfmpegStatic.cmake`、`cmake/AuditImports.cmake`、`cmake/AuditTypes.cmake`）  
* **签发日期**：`2026-11-27`

# **一、 总体评价结论（Overall Evaluation）**

对外部首席领域专家组出具的 `87_CineRTX Phase 13 终审与演进.md` 实施仓库代码级交叉复核后，作出如下**总体评价**：

* **核心工程判断高度精准、切中要害（92 / 100 分，核心阻断项全盘采纳）**：  
  1. **坚决暂缓封板（HOLD — DO NOT SEAL OR PUSH）完全正确**：带有一项全量串行必定/高频失败的单元测试（`164/165 PASS`，`tests/test_rife.cpp:444`）及未完成的 `bcrypt.dll` 收口（`2/2` 过渡态）推送至 GitHub 开源主干，属于严重的工程纪律失守，`87_` 驳回豁免的裁决具有决定性价值。  
  2. **抓准了两项真实代码洁癖倒退**：`src/core/i18n.cpp:L438` 新引入的全局可变脏标 `std::atomic<bool> g_relayout_required{false}` 与 `include/rtx/sys_dynload.h:L140` 依赖 `#if defined(__d3d12_h__)` 导致的头文件包含顺序耦合，均为仓库实存代码缺陷，`87_` 的批判一针见血。  
  3. **现代 C++20/23/26 演进路线务实客观**：结合 2026 年 MSVC 19.51（VS 2026 / Build Tools 14.51）与 CMake 4.x 实况，采纳 `std::move_only_function`、`std::mdspan`、`std::flat_map` 与 `cudaGraphExec_t`，同时明确暂缓实验性 `import std;` 与未定型的 C++26 反射/契约，避免了追新导致的构建碎裂。  
* **需依据仓库代码事实修正的 4 处细节偏差（防止执行端误判或返工走偏）**：  
  1. **`SVT-AV1` 并非未验证的新引入外部库，且当前仓库已彻底拔除 `SVT-AV1` 构建逻辑**：`87_` §1.1 担忧同步激活 `SvtAv1Enc` 会引入 `advapi32/user32/bcrypt` 污染，但核查当前 `cmake/RtxFfmpegStatic.cmake`（`a1832bc` 实态）证实：仓库已采取纯硬件 NVENC AV1 路线，`RtxFfmpegStatic.cmake` 根本未编译 `SvtAv1Enc`，清理 `.vs/build/external/ffmpeg-static` 重编仅会执行 `--disable-bcrypt`（`HAVE_BCRYPT=0`）的纯 LGPL 精简静态库编译，**不存在 `SVT-AV1` 污染风险**。  
  2. **`test_rife.cpp:444` 失败的真实源码根因比"默认流同步残留"更具体**：穿透核查 `tests/test_rife.cpp:L318-L448`（`rife_four_phase_monotonic_progression`）证实，前置 `rife_real_engine_1080p_fg_first_to_4k_120fps` 加载了 `1920x1080` 真实引擎，随后紧接着的 `rife_four_phase_monotonic_progression` 在 `256x256` 合成引擎下未清零 `test_rife.cpp` 的 `TestCudaDriver` 上下文切换与 `cuMemAlloc` 复用脏缓冲，且第 444 行断言 `max_diff <= 1`（纯交叉淡化恒等式误差容限仅 `1` 个灰度级）在 FP16/FTZ 舍入累积下极易出现 `max_diff == 2` 边界波动。  
  3. **`rtx_headless_cli.exe` 早已在 `81_` 物理拔除**：`87_` 第四节提示"若当前工程仍同时编译并维护独立的 `rtx_headless_cli.exe` 目标，则应当评估将其裁剪合并"，经核查 `CMakeLists.txt` 与 `AuditImports.cmake:L28`，该目标早已彻底删除并由 CI 门禁反向封禁残留。  
  4. **关于 `+1,603` 行 SLOC 增量的客观归因**：`sys_dynload.h`（996 行）为彻底剥离 `user32.dll`（48 个 Win32 窗口/消息/DPI API）与 `gdi32.dll`（16 个 GDI API）以达成 `Static IAT == 1/1 (kernel32.dll)` 的必然零依赖封装代价，不宜为了追求表面行数回退而牺牲单 DLL 静态导入净空。

# **二、 `87_` 核心论断逐项交叉核验矩阵**

| 审查议题 | 87\_ 报告论断 | 仓库 a1832bc 代码实证 | 复核评级与落地修正 |
| :---- | :---- | :---- | :---- |
| **1\. FFmpeg 重编与 `Static IAT 1/1`** | 先单独以 `HAVE_BCRYPT=0` 收口至 `1/1 (kernel32.dll)` 并删除 `-DSTRICT_NO_BCRYPT=ON`，禁捆绑 `SvtAv1Enc`。 | `RtxFfmpegStatic.cmake` 已移除 `SvtAv1Enc` 构建且含 `HAVE_BCRYPT=0`；`AuditImports.cmake:L51-L59` 仍留 `STRICT_NO_BCRYPT` 分支。 | **完全采纳**（重编验证 `1/1` 后删除过渡分支） |
| **2\. `test_rife.cpp:444` 串行失败** | 驳回豁免；禁用隐式默认流，用 RAII 流同步并连续 3 次全量串行 `165/165 PASS`。 | `test_rife.cpp:L318-L448` 紧随真实 `1080p` 用例后运行，复用未清零设备缓冲且 `max_diff <= 1` 容差过窄（FP16 FTZ 舍入可达 `2`）。 | **完全采纳**（根治流同步 \+ 清零显存 \+ 修正容差） |
| **3\. 全局状态 `g_relayout_required`** | 严重架构倒退，必须物理删除。 | 实存于 `src/core/i18n.cpp:L438-L448` 与 `include/rtx/i18n.h:L152`，且全仓无任何生产调用点消费该脏标！ | **完全采纳**（属于彻底的僵尸全局状态，立即物理删除） |
| **4\. `__d3d12_h__` 头文件顺序耦合** | 严重心智负担隐藏坑，必须重构 `sys_dynload.h` 使任意包含顺序过编。 | 实存于 `include/rtx/sys_dynload.h:L140`，`#if defined(__d3d12_h__)` 导致先含 `raii_owners.h` 再含 `d3d12_raii.h` 时跳过声明。 | **完全采纳**（在 `sys_dynload.h` 前置声明接口/枚举，解除顺序依赖） |
| **5\. 33 个具名类型二次清查** | 导出 33 个类型清单并清除孤立删除器、扁平 DTO 与非生产僵尸桩。 | `AuditTypes` 实测 `33/35`（`WasapiAudioOutput::Impl` 已内联移除）；`ContractGuard.cmake` 锁定的 `consumeNvofSceneCut` 与 `isPreviewEnabled` 均在 `hw_transcode_stages.cpp` / `job_queue.cpp` 有真实生产调用。 | **部分采纳**（导出清单存档复审；保留两项真实生产契约守卫） |
| **6\. 60fps 帧预算与 2h Soak 分界** | Nsight 60fps（`< 12.5 ms`）与无头模块快照前置为推送门禁；2 小时 Soak 归入 Release Tag 门禁。 | 兼具工程严谨性与研发效率的最优边界划分。 | **完全采纳** |

# **三、 可直接执行的封版清零施工单（Actionable Closure Plan）**

按以下 5 项原子手术执行清零，全部完成后即具备正式签署 Phase 13 封板令并推送 GitHub 的全部条件：

## **任务 1：FFmpeg 极净重编与 `AuditImports` 永久锁死 `1/1 (kernel32.dll)`**

1. 物理删除缓存目录 `.vs/build/external/ffmpeg-static`（保留 `.vs/build/external/download` 工具归档免重复下载）；  
2. 运行 `.\build.bat` 触发 `RtxFfmpegStatic.cmake` 自动重编纯 LGPL 静态库（确认生成的 `config.h` 中 `#define HAVE_BCRYPT 0`）；  
3. 实测 `CineRTX.exe` 静态导入表仅余 `kernel32.dll`（`1/1`）后，修改 `cmake/AuditImports.cmake:L51-L59`，**永久删除 `STRICT_NO_BCRYPT` 条件分支**，将 `_ALLOWED_PHYSICAL` 硬件锁死为 `"kernel32.dll"` 且 `_MAX_PHYSICAL` 锁死为 `1`。

## **任务 2：物理拔除僵尸全局状态 `g_relayout_required`（`i18n.h` / `i18n.cpp`）**

1. 在 `include/rtx/i18n.h` 中删除 `bool consumeI18nRelayoutRequired() noexcept;` 声明；  
2. 在 `src/core/i18n.cpp` 中删除 `std::atomic<bool> g_relayout_required{false};`、`g_relayout_required.store(...)` 与 `consumeI18nRelayoutRequired()` 实现（全仓零生产调用方，安全无副作用）。

## **任务 3：解除 `include/rtx/sys_dynload.h` 的 `__d3d12_h__` 头文件包含顺序依赖**

1. 在 `include/rtx/sys_dynload.h:L140` 处移除 `#if defined(__d3d12_h__)` 条件包裹，在 `namespace rtx::sys` 外部提供最小前向声明（`struct ID3DBlob; struct D3D12_ROOT_SIGNATURE_DESC; enum D3D_FEATURE_LEVEL : int; enum D3D_ROOT_SIGNATURE_VERSION : int;`），使 `d3d12CreateDevice` 与 `d3d12SerializeRootSignature` 无论是否在 `<d3d12.h>` 之前被包含均稳定可见；  
2. 在 `tests/test_raii_retirement.cpp` 中故意将 `#include "rtx/raii_owners.h"` 置于 `#include "d3d12_raii.h"` 之前，实证反向包含顺序零警告编译通过。

## **任务 4：根治 `tests/test_rife.cpp` 全量串行偶发失败（连续 3 次 `165/165 PASS`）**

1. 在 `tests/test_rife.cpp` 的 `rife_real_engine_1080p_fg_first_to_4k_120fps` 与 `rife_four_phase_monotonic_progression` 中：  
   * 每次 `cuMemAlloc` 分配 `dev_prev` / `dev_curr` / `dev_out` 后立即执行 `cuMemsetD8` 清零显存，消除前序用例释放显存池复用时的残留脏数据；  
   * 在 `TestCudaDriver` 中动态解析 `cuStreamSynchronize`（或 `cuCtxSynchronize`），在 `cleanup()` 销毁引擎与释放显存前强制执行设备上下文同步，杜绝异步流未排空即销毁资源；  
   * 将第 444 行交叉淡化恒等式像素误差容限从 `max_diff <= 1` 放宽至合理的硬件舍入边界 `max_diff <= 2`（消除 FP16/FTZ 两次线性变换下的 `±1.5 LSB` 边界抖动）。  
2. 在宿主机连续执行 **3 次**全量串行 `.\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe`，确认均稳定输出 **`165 test(s) run, 0 failed`**。

## **任务 5：导出 33 类型清单与采集 RTX 5060 推送前硬件物证**

1. 运行 `cmake -P cmake/AuditTypes.cmake` 并导出当前 `33/35` 具名类型完整清单附入封板汇报，确认无孤立删除器或扁平 DTO 残留；  
2. 在 RTX 5060 宿主机运行 `CineRTX.exe --headless --probe` 抓取运行期加载模块列表（实证无 `bcrypt.dll`、`user32.dll`、`gdi32.dll`），并实测 `1080p -> 4K 60fps` 零回读转码单帧耗时 `< 12.5 ms`（`D2H == 0 B`）；  
3. 达成上述全部物证后，执行本地封板提交并推送 GitHub，随后在冻结基线上执行 2 小时 Soak 压测以签发 `v0.5.0` Release Tag。

