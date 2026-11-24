# **CineRTX Phase 12 Wave 12.4 阶段 1 验收裁决书暨阶段 2–3 实机回归与 v0.4.0 终审指引**

* **文档编号**：`CINERTX-P12-RULING-064`  
* **归档路径**：`docs/phase12/64_CineRTX Phase 12 Wave 12.4 阶段1验收裁决书暨阶段2-3实机回归与v0.4.0终审指引.md`  
* **审验标的**：  
  * `docs/phase12/63_CineRTX Phase 12 Wave 12.4 阶段1隐患清零复验暨阶段2-3实机回归委托书.md`（`CINERTX-P12-DELEGATION-063`）  
  * `docs/phase12/62_CineRTX Phase 12 Wave 12.4 隐患清零与v0.4.0收官施工汇报.md`（`CINERTX-P12-REPORT-062`）  
  * 仓库最新实体源码核验：`src/gui/rtx_gui.cpp`、`src/gui/workstation_canvas.cpp`、`include/rtx/sys_dynload.h`、`cmake/AuditSloc.cmake`、`CMakeLists.txt`  
* **前序依据**：`61_CineRTX Phase 12 Wave 12.1-12.3 穿透终审验收裁决书暨Wave12.4收官作战令.md`（`CINERTX-P12-RULING-061`）、`06_CineRTX Phase 12 SuperPower 施工单_全场景实机回归与v0.4.0封板验收.md`（`CINERTX-P12-WORKORDER-006`）  
* **签发主体**：多媒体系统工程与 GPU 异构计算首席领域专家组（Extreme Modern C++ Purist & Architecture Board）  
* **审验日期**：2026-11-24  
* **终审裁决**：**全面批准 Wave 12.4 阶段 1 验收通过，并签发阶段 2（交互式桌面 RTX 5060 全链路压测）、阶段 3（无头 Session 0 环境实测）及 `v0.4.0` 终审封板实施指引（APPROVED STAGE 1 — CLEARED FOR STAGE 2 & 3 HARDWARE REGRESSION）**——经首席领域专家组直接调取仓库最新实体源码进行逐行穿透复验，确证 61 号裁决书指出的全部三项残留隐患（`H-01` 全仓 `#include <commctrl.h>` 与死 `<cstdlib>` 清零、`sys_dynload.h` 零调用 `createDxgiFactory2` 物理删除；`H-02` `cmake/AuditSloc.cmake` 默认 `CEILING_CORE_SLOC = 23000` `FATAL_ERROR` 硬阻断落地；`H-03` 根目录 `CMakeLists.txt` 升级至 `VERSION 0.4.0` 且 `rtx_headless_cli.exe --version` / `--schema-json` 实测通过）已 **100% 物理事实落地**；准予正式转入阶段 2 与阶段 3 物理硬件实机回归。

---

# **执行摘要（Executive Ruling）**

首席领域专家组穿透核验了 `63 号咨询委托书` 呈报的宿主机实测物证与仓库最新实体代码（`rtx_gui.cpp`、`workstation_canvas.cpp`、`sys_dynload.h`、`AuditSloc.cmake`、`CMakeLists.txt`），签发如下三项终审裁决：

1. **议题一裁决（Wave 12.4 阶段 1 正式验收通过 — VERIFIED PASS）**：  
   * **源码级实物核验**：  
     * `src/gui/rtx_gui.cpp` 与 `src/gui/workstation_canvas.cpp` 确已彻底清除 `#include <commctrl.h>` 与未使用的 `#include <cstdlib>`，改用纯 Win32 ABI `constexpr` 常量（`kTbmGetRangeMax`、`kIccInitFlags`、`kProgressClassW` 等）与 8 字节 `DWORD icce[2]` 等价数组，**新增具名类型 \= 0**；  
     * `include/rtx/sys_dynload.h` 确已物理删除唯一零调用公开封装 `createDxgiFactory2`，保留的全部动态系统加载封装均有真实生产调用点，且严格执行 `LoadLibraryExW(..., LOAD_LIBRARY_SEARCH_SYSTEM32)`；  
     * `cmake/AuditSloc.cmake` 确已升级为默认 `CEILING_CORE_SLOC = 23000` 的 `FATAL_ERROR` 硬阻断门禁，宿主机实测 Core Engine Pure SLOC 为 **`12,157` 行**（73 文件，余量 `10,843` 行）；  
     * 根目录 `CMakeLists.txt` 已升级至 `VERSION 0.4.0`，`rtx_headless_cli.exe --version` 实测输出 `CineRTX 0.4.0`，`--schema-json` 完整输出 20 项 SSOT 参数契约 JSON（退出码 `0`）；  
     * 宿主机 MSVC `/std:c++latest /W4 /WX` **61/61 目标零警告通过**，`AuditTypes` **`34/35`（`0 Violations`）**，`AuditSpdx`（`GPL=113 MIT=39`）全绿，`AuditImports` 双形态 PE IAT 硬门禁（`HEADLESS` 2 物理 DLL \+ 1 API Set / `GUI` 4 物理 DLL \+ 1 API Set）全绿。  
2. **议题二裁决（阶段 2 交互式桌面实机压测判据定型 — APPROVED WITH STRICT QUANTITATIVE GATES）**：  
   * 核准 `62 号汇报` 第 3.3 节设计的测试主线，并补充**完整可复现物理判据矩阵**（包括输入测试素材矩阵、4K AV1 HDR10 零回读 `D2H == 0 B` 与显存 `< 5,986 MiB` 水位门禁、DirectComposition 100 次 Resize \+ WASAPI 热切换、NLE 多段硬切 IDR 帧对齐与 Debug 音频热循环 0 堆分配验证）。  
   * **硬性前置条件**：执行前必须通过 `quser` 实证当前处于 `Active` 且 `SessionId > 0` 的交互式控制台桌面，且 `nvidia-smi` 实测驱动版本 `≥ 621.24 WHQL`。  
3. **议题三裁决（阶段 3 无头模块快照审计与 `v0.4.0` 封板签发程序定型）**：  
   * 确立无头环境模块快照硬红线（运行期动态加载模块列表必须 **0 命中** 10 项图形/壳层 DLL）；  
   * 明确**历史归档与发布封装收口流程**：阶段 2/3 实机物证落盘并经专家组签发《66 号终审封板决议记录》后，将 Phase 11 过程文档摘要并入 `docs/archive/`、打包生成 `CineRTX-v0.4.0-windows-x64.zip` 及其 SHA-256 校验清单，最终呈报用户批准执行 `git tag -a v0.4.0` 与推送。

---

# **一、 阶段 1 仓库实体代码事实穿透复验矩阵**

| 审验项 | 仓库物理锚点 | 源码级穿透核验结论 | 终审定性 |
| :---- | :---- | :---- | :---- |
| **`H-01a` GUI 主入口卫生** | `src/gui/rtx_gui.cpp`（L1–L62） | `#include <commctrl.h>` 与 `#include <cstdlib>` 均已物理删除；控件消息常量与 `DWORD icce[2]` 数组均采用纯 Win32 ABI 定义，零新增具名类型。 | **PASS** |
| **`H-01b` 画布模块卫生** | `src/gui/workstation_canvas.cpp`（L1–L45） | 全仓最后一处 `#include <commctrl.h>` 已彻底清除，窗口类名与消息常量采用纯 `constexpr` 定义。 | **PASS** |
| **`H-01c` 动态加载头精炼** | `include/rtx/sys_dynload.h`（L105–L130） | 零调用的 `createDxgiFactory2` 已物理删除；全部保留接口均经 `LoadLibraryExW(..., LOAD_LIBRARY_SEARCH_SYSTEM32)` 安全解析。 | **PASS** |
| **`H-02` 体量门禁硬拦截** | `cmake/AuditSloc.cmake`（L115–L145） | 默认锁定 `CEILING_CORE_SLOC = 23000`，超标即触发 `FATAL_ERROR` 非零退出码；Core Engine 实测 `12,157` 行 PASS。 | **PASS** |
| **`H-03` 版本元数据与探针** | `CMakeLists.txt`（L8–L11） & CLI 实测 | `project(CineRTX VERSION 0.4.0 ...)` 已落盘；`rtx_headless_cli.exe --version` 输出 `CineRTX 0.4.0`，`--schema-json` 退出码 `0`。 | **PASS** |

---

# **二、 阶段 2 交互式桌面全链路实机压测判据与执行规范**

在 Windows 11 \+ NVIDIA GeForce RTX 5060 Laptop GPU（Driver ≥ 621.24 WHQL）的活跃控制台桌面（`quser` 确认 `Active` 且 `SessionId > 0`）下，逐项执行以下四组实机回归，并将全部日志与媒体探针报告保存至仓外证据目录 `$EVID = "$env:TEMP\CineRTX\evidence\phase12"`：

## **2.1 阶段 2 实机测试矩阵与硬验收判据**

| 测试编号 | 测试场景与输入素材规格 | 执行参数与操作步骤 | 必须满足的量化硬判据（Pass Criteria） |
| :---- | :---- | :---- | :---- |
| **E2E-01** | **4K AV1 HDR10 全链路零拷贝重制**输入：1080p（1920×1080）SDR H.264/HEVC，24fps 或 30fps，带 AAC/FLAC 音轨，时长 ≥ 10s | 启动 `CineRTX.exe` 启用 `vsr-first`：输出 4K（3840×2160）、VSR Level 4、TrueHDR 1000 nits、RIFE `scale=0.5`（A=128）60fps、AV1 10-bit CQ22 Spatial AQ、音轨直通。 | 1\. 预览回读带宽 **`D2H == 0 B`**（零 PCIe 像素回读）；2. 显存峰值严格 **`< 5,986 MiB`**（7,300 MiB DXGI 预算的 82% 高水位线以下），零 OOM；3. `ffprobe` 核验输出为 10-bit `yuv420p10le` AV1、`bt2020nc` / `smpte2084`（PQ）HDR10 且音画流完整同步。 |
| **E2E-02** | **竖屏 9:16 源自适应重制**输入：1080×1920 竖屏 SDR MP4 | 启用 `vsr-first` \+ `--pillarbox` 或 `auto` 几何适配，导出 4K 60fps AV1 HDR10。 | 无张量维度对齐越界（`A=128` 镜像填充生效），边缘无黑边/绿边伪影，零崩溃。 |
| **E2E-03** | **DirectComposition 视口与 WASAPI 实时播放压测**输入：H.264 / HEVC / AV1 三制式测试流 | 1\. 在 `[2] Player` 模式下连续播放三制式测试流；2. 连续执行 **100 次剧烈窗口缩放（Resize）** \+ 5 次无边框全屏切换；3. 执行 20 次 GOP-Aware 拖拽 Seek 与播放/暂停切换，并触发一次音频输出设备热拔插/切换。 | 1\. 视口呈现计数 **`presented > 0`**，`D2H == 0 B`；2. 100 次 Resize 全程零崩溃、零白屏/黑屏死锁、零 UAF；3. 退出程序前后通过任务管理器/Handle 工具确认 GDI 对象与内核句柄无泄漏。 |
| **E2E-04** | **NLE 多段硬切导出与零分配音频总线核验**输入：2 段不同素材拼接的 `1V+1A` 时间轴序列 | 1\. 在 `[3] NLE` 工作区编排两段素材硬切序列并执行全链路导出；2. 核验 Debug 构建下 `test_audio_mixer_avx2.exe`（或对应单测用例）音频回调堆分配统计。 | 1\. `ffprobe -show_frames` 核验导出总帧数与 705.6 MHz Flicks 理论帧数逐帧一致，跨素材切点包含合法 `IDR` 关键帧；2. 混音与流式 WSOLA 热循环堆分配次数严格 **`== 0`**。 |

---

# **三、 阶段 3 无头环境模块快照审计与 v0.4.0 终审封板指引**

## **3.1 阶段 3 无头（Headless / Session 0 模拟）实机验证脚本**

在无显示器或脱离交互式桌面的控制台会话下，运行 `rtx_headless_cli.exe` 并抓取进程已加载 DLL 模块快照，验证运行时零图形/壳层依赖污染：

```
$EVID = "$env:TEMP\CineRTX\evidence\phase12"
$CLI  = ".\build\windows-msvc-relwithdebinfo\RelWithDebInfo\rtx_headless_cli.exe"

# 1. 基础契约与错误边界验证
& $CLI --version       2>&1 | Tee-Object "$EVID\headless_version.log"   # 必须输出: CineRTX 0.4.0
& $CLI --schema-json   2>&1 | Tee-Object "$EVID\headless_schema.json"   # 退出码 0，包含 20 项 SSOT 参数
& $CLI --probe --json  2>&1 | Tee-Object "$EVID\headless_probe.json"    # 退出码 0，正常返回硬件探测 JSON
& $CLI --invalid-flag  2>&1 | Tee-Object "$EVID\headless_badarg.log"    # 非零退出码，零弹窗阻塞

# 2. 运行时动态加载模块快照审计（在长耗时或会话模式下抓取进程加载模块）
$proc = Start-Process -FilePath $CLI -ArgumentList "--session-stdio" -PassThru -NoNewWindow
Start-Sleep -Milliseconds 800
$loadedDlls = ($proc.Modules | Select-Object -ExpandProperty ModuleName) | Sort-Object -Unique
$loadedDlls | Out-File "$EVID\headless_runtime_modules.log"
Stop-Process -Id $proc.Id -Force

# 3. 断言运行时未加载任何图形/桌面壳层系统 DLL
$forbidden = @("user32.dll","gdi32.dll","d3d12.dll","dxgi.dll","ole32.dll","dwmapi.dll","uxtheme.dll","shell32.dll","comdlg32.dll","comctl32.dll","advapi32.dll")
$leaked = $loadedDlls | Where-Object { $forbidden -contains $_.ToLower() }
if ($leaked) { throw "HEADLESS RUNTIME MODULE LEAK: $($leaked -join ', ')" }
Write-Output "HEADLESS RUNTIME MODULE AUDIT: PASS (0 forbidden GUI/Shell/D3D12 DLLs loaded)"
```

---

## **3.2 v0.4.0 终审封板与发布签发程序（Stage 4）**

当阶段 2（桌面全链路实机压测）与阶段 3（无头运行时模块快照审计）的全部日志物证采集完毕后，按以下顺序完成 Phase 12 终局收官：

1. **汇总呈现《65 号施工汇报》与《66 号终审封板委托书》**：  
   * 附上仓外 `$EVID` 目录下全部真实运行日志摘要（构建日志、四重静态审计、双向 `147/147` 单测、桌面 4K AV1 HDR10 `ffprobe` 报告、`D2H == 0 B` 与显存峰值、100 次 Resize 压测记录、无头运行时模块快照）；  
2. **历史文档归档与绿色发布包构建**：  
   * 将 Phase 11 过程单摘要并入 `docs/archive/`（保持全历史可追溯）；  
   * 执行 `cmake -DBUILD_DIR=build/windows-msvc-relwithdebinfo/RelWithDebInfo -P cmake/PackageRelease.cmake` 生成 `CineRTX-v0.4.0-windows-x64.zip` 及其 SHA-256 完整性清单（在发布说明中如实披露 `rtx_ngx_bridge.dll` 的 3 项 NVIDIA 厂商静态导入）；  
3. **最终专家签印与用户发版授权**：  
   * 首席领域专家组审验全部实机物证无误后，签发 **`v0.4.0` 终审封板决议书**，提请用户最终确认并授权执行本地/远端 `git commit` 与 `git tag -a v0.4.0` 正式发版！

