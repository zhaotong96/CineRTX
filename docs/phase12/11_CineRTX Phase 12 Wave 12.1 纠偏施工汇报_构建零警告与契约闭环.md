# CineRTX Phase 12 Wave 12.1 纠偏施工汇报：构建零警告与契约闭环

- **文档编号**：`CINERTX-P12-REPORT-011`
- **归档路径**：`docs/phase12/11_CineRTX Phase 12 Wave 12.1 纠偏施工汇报_构建零警告与契约闭环.md`
- **依据指令**：[`10_CineRTX Phase 12 Wave 12.1 施工异化与构建断环终审裁决暨纠偏施工方案.md`](./10_CineRTX%20Phase%2012%20Wave%2012.1%20施工异化与构建断环终审裁决暨纠偏施工方案.md)（`CINERTX-P12-RULING-010`）
- **交付口径声明（严守 10 号裁决书 §1.3 诚实性红线）**：本执行环境为无 Windows SDK / MSVC / CUDA / DirectX 12 的 Linux 沙箱，**本次交付仅提交源码纠偏补丁与静态/布局镜像核验，待 Windows 宿主机执行全量 MSVC `/W4 /WX` 编译、`cmake -P cmake/AuditTypes.cmake`、146/146 `rtx_tests.exe` 双向单测及交互式桌面实呈验活**，不以 Linux 单文件编译结果冒充宿主机验收。

## 一、 驳回项逐条纠偏落盘总览

| 裁决指令 | 对应文件 | 纠偏动作与代码实证 |
| :--- | :--- | :--- |
| **1. 清零源码树 `.bak` 垃圾并植入 CMake 硬门禁** | [`CMakeLists.txt`](../../CMakeLists.txt) | 将源码树内全部 11 个 `*.pre12_03.bak` 及本次替换的 6 个旧文件移出 `CineRTX/` 仓库目录至外部归档目录（`work/_CineRTX_bak_archive/`），确保源码树 0 备份残留；在 `project(CineRTX ...)` 后植入 `file(GLOB_RECURSE _RTX_JUNK_FILES ...)` 配置期 `FATAL_ERROR` 硬门禁，保持全局 `/W4 /WX` 且禁用 `/wd4324`。 |
| **2. 修复 `BitstreamSlot` 契约词法断裂与重复造轮子** | [`src/shim/loader/nv_encoder.cpp`](../../src/shim/loader/nv_encoder.cpp) | 按 10 号裁决书 §1.2 范本将 `closeWin32Event`、`ScopedWin32Event` 与 `makeScopedWin32Event()` 移至 `// @rtx-type-justification` 之前，确保契约注释与 `struct BitstreamSlot` 零空行、零预处理指令紧邻；补回 `BitstreamSlot::bitstream` 成员并彻底删除 `RtxEncoder` 内遗留的单缓冲死字段 `bitstream`；`rtx_shim_encoder_destroy` 严格执行「注销异步事件 → 销毁比特流缓冲 → `completion_event.reset()`」逆序三步曲。 |
| **3. 根治 `C4324`（内存布局显式补齐优先）** | [`include/rtx/telemetry_bus.h`](../../include/rtx/telemetry_bus.h) | 移除全部 `#pragma warning(disable : 4324)`；为 `TelemetrySnapshot` 末尾追加显式 `uint64_t cache_line_pad_[7]{}`（`sizeof == 128`），为 `AtomicTelemetryBus::sequence_` 追加 `uint8_t sequence_pad_[64 - sizeof(std::atomic<uint32_t>)]`（`sizeof == 192`），自然消除全部隐式对齐填充。 |
| **3. 根治 `C4324`（WSOLA 状态机尾填充）** | [`include/rtx/audio_mixer_avx2.h`](../../include/rtx/audio_mixer_avx2.h) | 为 `WsolaStreamState` 末尾追加 `uint8_t tail_pad_[28]{}` 与 `static_assert(sizeof(WsolaStreamState) % 32 == 0)`，零 `#pragma` 自然消除 `C4324`。 |
| **3. 根治 `C4324`（WASAPI 异步状态机热原子重排）** | [`src/core/wasapi_audio_output.cpp`](../../src/core/wasapi_audio_output.cpp) | 将 7 个 `alignas(64)` 原子游标集中前置至 `WasapiAudioOutput::Impl` 首部并逐一显式补齐至 64 字节缓存行边界（消除 7 处内部 `C4324` 与伪共享），保留 `std::jthread worker_` 居末的逆序析构不变量；因内嵌 `std::vector`/`std::mutex`/`std::jthread` 在 `/MT` 与 `/MTd` 下尾长可变，仅在该单一类型外围（且置于 `// @rtx-type-justification` 之前，保持契约 0 行紧邻）配对 `#pragma warning(push/disable : 4324/pop)`。 |
| **4. 根治 `C4100`（未引用形参清剿）** | [`src/core/hw_transcode_stages.cpp`](../../src/core/hw_transcode_stages.cpp) | 将 `RemasterPipeline::runRenderGraph`（L671）未引用的 `audio_segments` 形参改为标准省略形参名写法 `std::span<const AudioGraphSegment> /*audio_segments*/`，保持调用点接口兼容且消除 `C4100`。 |

## 二、 沙箱静态与布局镜像核验记录（非宿主机替代证明）

1. **`AuditTypes.cmake` 逐行规则镜像扫描（14 个 Wave 12.1 涉及文件）**：14/14 具名类型全部通过 `@rtx-type-justification` 紧邻窗口与白名单校验，**0 违规**（`BitstreamSlot` 与 `WasapiAudioOutput::Impl` 契约块与 `struct` 声明间隔均为 0 行）。
2. **负向正则与 SPDX / 脱敏门禁**：`std::getenv|_dupenv_s|GetEnvironmentVariable` 命中 **0**；`\bg_app\b` 命中 **0**；`src/shim/` 全部 `MIT OR Apache-2.0`，其余 `GPL-3.0-or-later WITH Plugin-Exception`；无外部网盘或本地路径泄漏。
3. **可移植头布局静态断言核验**：`g++ -std=c++23 -O2 -Wall -Wextra -Wpedantic -Werror` 验证 `sizeof(TelemetrySnapshot) == 128`、`sizeof(AtomicTelemetryBus) == 192`、`sizeof(WsolaStreamState) % 32 == 0` 全部成立。

## 三、 Windows 宿主机待执行强制闭环验收命令

请在 Windows 构建机同步代码并确认工作区零 `.bak` 后，依次执行 10 号裁决书规定的全部宿主机门禁：

```powershell
# 1. 确认源码树零 .bak 残留
git status --short

# 2. Clean Configure & Build（MSVC /W4 /WX 零警告零错误，同时验证零垃圾门禁与 AuditTypes）
cmake --preset windows-msvc-ninja
cmake --build --preset windows-msvc-ninja

# 3. 四重静态门禁全量审计
cmake -P cmake/AuditTypes.cmake
cmake -P cmake/AuditSpdx.cmake
cmake -P cmake/AuditSloc.cmake
cmake -P cmake/AuditImports.cmake

# 4. 双向单元测试（146/146 PASS）
.\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe
Push-Location [REDACTED_PATH]; .\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe; Pop-Location

# 5. 负向正则清零扫描
rg "std::getenv|_dupenv_s|GetEnvironmentVariable" include/ src/
rg "\bg_app\b" include/ src/
rg "CreateSolidBrush|DeleteObject" src/gui/
```

完成后请在 WTS Active Console Session（`SessionId > 0`）下启动 `CineRTX.exe` 完成窗口创建、拖动、时间轴交互与退出的实呈验活。
