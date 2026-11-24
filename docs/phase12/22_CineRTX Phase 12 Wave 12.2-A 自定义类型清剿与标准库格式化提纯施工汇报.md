# CineRTX Phase 12 Wave 12.2-A 施工汇报：门禁升级、旧式格式化清零与第一批自定义类型清剿

- **汇报编号**：`CINERTX-P12-REPORT-022`
- **归档路径**：`docs/phase12/22_CineRTX Phase 12 Wave 12.2-A 自定义类型清剿与标准库格式化提纯施工汇报.md`
- **依据施工单**：[`04_CineRTX Phase 12 SuperPower 施工单_自定义类型清剿与C++26标准库替换.md`](./04_CineRTX%20Phase%2012%20SuperPower%20施工单_自定义类型清剿与C++26标准库替换.md)（`CINERTX-P12-WORKORDER-004`）
- **前置封板基线**：[`21_CineRTX Phase 12 Wave 12.1 终审复验裁决书暨Wave12.2派发令.md`](./21_CineRTX%20Phase%2012%20Wave%2012.1%20终审复验裁决书暨Wave12.2派发令.md)（`CINERTX-P12-FINAL-SEAL-021`）
- **执行范围**：Wave 12.2 第一批次（Wave 12.2-A，32 个文件原位替换，旧版本统一归档至仓外 `work/_CineRTX_bak_archive`，仓内零 `.bak` 残留）

## 一、 核心指标与门禁进度总览

| 门禁 / 指标 | Wave 12.1 基线 | Wave 12.2 目标 | Wave 12.2-A 实测 | 当前状态 |
| :-- | :-- | :-- | :-- | :-- |
| **Gate 12.2-3a：旧式格式化扫描** (`snprintf` / `_snwprintf` / `ostringstream`) | 25 命中 | **0 命中** | **0 命中** | **PASS（100% 清零）** |
| **Gate 12.2-1：`cmake/AuditTypes.cmake` 门禁脚本** | 6 类白名单 / 无上限 / 8 行窗口 | 5 类白名单 / `≤ 35` 硬上限 / 6 行窗口 / 词法剥离 / 模板查重 | 脚本已全量升级（`-P` 独立脚本模式执行 `FATAL_ERROR` 硬门禁；配置期默认 `WARNING` 防阻断） | **脚本就绪** |
| **Gate 12.2-1：全仓具名自定义类型数** | 155 个 | `≤ 35` 个 | **128 个（净减 27 个）** | **进行中（Wave 12.2-B 待清剿 93 个）** |
| **Gate 12.2-2：Pure SLOC（134 镜像源码+单测+脚本）** | 32,635 行（全仓含其余 cmake 约 33,143） | `≤ 23,000` 行 | **32,360 行（净减 275 行）** | **进行中（Wave 12.2-B 待瘦身）** |
| **Gate 12.2-3c：`AuditSpdx`** | PASS | PASS | **0 缺失（PASS）** | **PASS** |
| **仓内垃圾文件卫生（`_RTX_JUNK_FILES`）** | 0 残留 | 0 残留 | **0 残留（32 份旧文件均移至仓外备份目录）** | **PASS** |

## 二、 本批次（Wave 12.2-A）已完成施工明细

### 1. 任务 W12.2-1：`cmake/AuditTypes.cmake` 词法与白名单硬门禁升级
- **白名单锁定**：将 `RTX_TYPE_CATEGORIES` 严格锁定为 5 类：`C-ABI`、`Hardware-Layout`、`NonTrivial-StateMachine`、`Std-Customization`、`Domain-Model`（彻底移除 `Polymorphic-Boundary` 与 `Concurrency-Invariant`）。
- **硬上限与词法剥离**：新增 `RTX_TYPE_MAX_COUNT 35` 数量门禁；匹配 `class`/`struct` 前剥离行内注释（`//...`、`/*...*/`）、`alignas(...)`、`[[...]]`、`__declspec(...)` 与 `final` 修饰符。
- **契约质量校验**：将 `@rtx-type-justification` 邻近窗口收紧至 `6` 行，校验 `Why-Std-Fails:`（`≥ 12` 字符）与 `Invariant:`（`≥ 10` 字符）非占位且全仓无模板复写。
- **防构建断环策略**：独立执行 `cmake -P cmake/AuditTypes.cmake`（检测到 `CMAKE_SCRIPT_MODE_FILE`）时默认开启 `RTX_AUDIT_TYPES_STRICT=ON` 并触发 `FATAL_ERROR`；通过 `CMakeLists.txt` 的 `include(cmake/AuditTypes.cmake)` 配置时默认以 `WARNING` 输出待清剿清单，待 Wave 12.2-B 压减至 `≤ 35` 后切换为配置期硬阻断。

### 2. 任务 W12.2-3：全仓旧式格式化彻底清零（25 → 0）
已全面替换为 C++20/23 类型安全的 `std::format` 或直接 `std::string` / `std::istreambuf_iterator`：
- `include/rtx/string_utils.h`：`escapeJson` 移除 `<cstdio>` 与 `std::snprintf`，改用 `std::format("\\u{:04x}", ...)`。
- `src/core/json_scanner.cpp`：`escapeJson` 直接委托 `rtx::str::escapeJson`，消除重复转义实现。
- `src/core/hw_transcode.cpp`、`src/core/i18n.cpp`：清除全部 `std::snprintf` 控制字符转义，统一为 `std::format`。
- `src/core/lut3d.cpp`：移除 `<sstream>` 与 `std::ostringstream`，改用 `std::istreambuf_iterator<char>` 直读文件。
- `src/core/job_queue.cpp`：`EncoderJob::toJson()` 移除 `std::ostringstream`，改用 `std::format`。
- `src/cli/main.cpp`、`src/cli/shim_probe.cpp`：清除全部 `std::snprintf` 与 4 处 `std::ostringstream`，统一采用 `std::format`。
- `src/gui/workstation_canvas.h`、`src/gui/rtx_gui.cpp`：清除全部 6 处 `_snwprintf_s`，改用 `std::format(L"...", ...)`。
- `src/shim/loader/nv_func_tables.cpp`：移除 `std::ostringstream`，改用 `std::format` + `std::string`。
- `src/shim/ngx_bridge/ngx_d3d12_bridge.cpp`、`src/shim/ngx_bridge/d3d12_interop.cpp`：清除全部 6 处 `std::snprintf`，改用 `std::format`。

### 3. 任务 W12.2-2：第一批 27 个自定义类型清剿与合规重分类（155 → 128）

| 涉及文件 | 清剿 / 重分类变更 | 数量变化 |
| :-- | :-- | :-- |
| `include/rtx/types.h` | 5 个空标签结构（`SlotTag`/`DeviceTag`/`SurfaceTag`/`StreamTag`/`EventTag`）替换为 `enum class HandleTag` 非类型模板参数；删除零引用死类型 `DeviceInfo` 与 `PlaneRect`；重写 `TaggedHandle` 专属契约 | **-7** |
| `include/rtx/status.h` | 删除 `FastError` 结构体，以 `std::pair<ErrorCode, std::int32_t>` 替换 `HotResult<T>` 错误通道；重写 `Error` 专属契约 | **-1** |
| `include/rtx/geometry.h`、`tests/test_types.cpp` | 删除向后兼容包装类 `SmartGeometryCalculator`，调用点直调 `calculateSmartGeometry` | **-1** |
| `include/rtx/json_scanner.h`、`src/core/json_scanner.cpp`、`src/core/param_schema.cpp` | 删除 `JsonMember`，全仓一步到位替换为 `std::pair<std::string, std::string_view>`（零过渡 `using` 别名） | **-1** |
| `include/rtx/param_schema.h`、`src/cli/main.cpp`、`tests/test_param_schema_reflection.cpp` | 删除 `FieldBinding<Profile>` 包装结构，直接使用 `ProfileMemberPtr`（`std::variant`） | **-1** |
| `include/rtx/hdr.h`、`src/core/sei_synthesizer.cpp` | 将纯静态工具类 `class SeiSynthesizer` 转换为 `namespace SeiSynthesizer`（调用点零破坏）；消除 `Hdr10PlusBitWriter` 为局部 lambda；清除 3 个未引用死函数（`putLe32`/`findInt`/`boolString`） | **-2** |
| `src/core/rational.cpp` | 将内部 128 位辅助结构 `Uint128` 与 `Div128` 替换为 `std::pair` 组合 | **-2** |
| `src/shim/loader/nv_func_tables.h`、`.cpp`、`nv_runtime.cpp` | 删除 `rtx_shim_static` 中 7 个零调用死 CUDA 外部内存/信号量结构（对应探测函数指针改为 `void*` 仅做符号存在性检查）；删除死 `cuvid::Table` 与单点 `nvenc::Table` | **-9** |
| `src/shim/loader/dyn_library.h`、`nv_runtime.cpp`、`nv_backend_shim.cpp` | 删除 `ComponentStatus`（替换为 5 元 `std::tuple`）；将 `ProbeState` 合并进 `RtxBackend` 并重分类为 `C-ABI` | **-2** |
| `src/shim/loader/nv_decoder.cpp` | 将 `DecoderFunctions` 内联至 `RtxDecoder`，将 `Slot` 替换为 `std::tuple` | **-2** |
| `src/shim/loader/nv_encoder.cpp` | 将 `BitstreamSlot` 替换为 `std::tuple` | **-1** |
| `include/rtx/telemetry_bus.h` | 依据施工单 §一.2.2 将 192B `alignas(64)` 缓存行隔离原子总线 `AtomicTelemetryBus` 从废止类别 `Concurrency-Invariant` 重分类为 `Hardware-Layout` | **0（合规重分类）** |

*(注：上述单项合计 -29 含 2 处重复计次合并，静态脚本实测全仓具名类型由基线 155 净降至 **128**，净减 27 个。)*

### 4. 任务 W12.2-4：SSOT 编译期参数契约扩展
- 经核查，仓库已完成出树代码生成向 `include/rtx/param_schema.h` 编译期 `kParamDescriptors`（`consteval validateParamDescriptors()`）收敛，无独立 `schema/` 目录。
- 在移除 `FieldBinding` 冗余包装后，新增 `rtx::formatCliHelp()`（`include/rtx/param_schema.h` 与 `src/core/param_schema.cpp`），直接遍历 `kParamDescriptors` 并联动 `i18n` 的 `tr(meta.label_id)` 生成 CLI 参数帮助与取值范围说明，避免帮助文本与 SSOT 契约漂移。

## 三、 验证范围与沙箱环境边界声明

1. **本地 Linux 沙箱已实证项（GCC 12.2 + `-std=c++23 -Wall -Wextra -Werror`）**：
   - 使用本地临时 `<format>` 语法验证垫片（仅位于沙箱 `/tmp`，未进入仓库），对 25 个不依赖 Windows SDK / FFmpeg / Deducing-`this` 的修改及关联源码与单测文件（含 `geometry.cpp`、`status.cpp`、`json_scanner.cpp`、`lut3d.cpp`、`shot_segmenter.cpp`、`vram_scheduler.cpp`、`sei_synthesizer.cpp`、`i18n.cpp`、`gop_seek_scheduler.cpp`、`wsola_stretch.cpp`、`audio_mixer_avx2.cpp`、`logger.cpp`、`runtime_env.cpp`、`nvidia_shim.cpp`、`shim_probe.cpp`、`dyn_library.cpp`、`nv_func_tables.cpp`、`nv_cuda_context.cpp`、`nv_runtime.cpp`、`nv_backend_shim.cpp`、`test_hdr.cpp`、`test_lut3d.cpp`、`test_vram_scheduler.cpp`、`test_audio_mixer_avx2.cpp`、`test_shim.cpp`）执行 `-Werror` 编译，**25/25 零错误零警告通过**。
   - 静态脚本实测：`snprintf|_snwprintf|ostringstream` 在 `include/` 与 `src/` **0 命中**；`SPDX-License-Identifier` **0 缺失**；全部 32 个替换文件的旧版本均已移入仓外 `work/_CineRTX_bak_archive`。
2. **明确未覆盖项（需 Windows 宿主机实机复验）**：
   - 本 Linux 沙箱无 MSVC 19.51+、Win32/D3D12 SDK、NVIDIA RTX Video SDK (NGX)、TensorRT、FFmpeg 静态库与 RTX GPU，未执行 `cmake --build --preset windows-msvc-ninja`（`/W4 /WX`）及 `rtx_tests.exe` 146/146 实机回归。

## 四、 Wave 12.2-B 剩余缺口与宿主机推进清单

为避免在无 Windows MSVC 编译器条件下对 90+ 跨 Win32/D3D12/NGX/TensorRT/FFmpeg 文件执行盲改引发大面积 `/WX` 构建断环，以下剩余工作明确留待 **Wave 12.2-B（结合 Windows MSVC 宿主机增量编译回归）** 收口：

1. **剩余 ~93 个具名类型清剿（`128 → ≤ 35`）**：
   - `src/shim/ngx_bridge/*`（25 → ~5）：清剿 `p010_pack_runtime.h`（4 个 Launch 参数袋）与 `rife_pack_runtime.h`（3 个 Launch 参数袋）、合并 `BuilderLogger`/`RifeTrtLogger` 为单一 `TrtLogger`、消除 `DriverApi`/`CudaApi`/`D3D12Api`/`SdkComponents`/`PackState`/`RifePackState`/`TrtLoader`/`NvofEngine::Impl`/`RifeEngine::Impl` 及 `d3d12_interop.h` 3 个槽位结构。
   - `src/io/*` 与 `include/rtx/io.h`、`cpu_video_encoder.h`（14 → 2）：消除 `Polymorphic-Boundary` 接口 `IDemuxer`/`IMuxer`/`CpuVideoEncoder`（改为具体状态机）及 `bitstream.h`/`.cpp` 4 个解析辅助结构。
   - `include/rtx/playback_session.h` 与 `src/core/playback_session_impl.h`、`wasapi_audio_output.cpp`、`ngx_remaster.cpp`（10 → 2）：以 `std::move_only_function` 替代 `IVideoFrameSink`/`IAudioMasterClock`/`HeadlessPlaybackSink`，消除 `Control`（`Concurrency-Invariant`）与 PIMPL `::Impl`。
   - `src/core/hw_transcode_internal.h`、`include/rtx/hw_transcode.h`、`src/gui/*` 及 `include/rtx/*` 其余被动数据袋，并为留存 `≤ 35` 个核心类型逐一补齐唯一非模板 `@rtx-type-justification`。
2. **Pure SLOC 深度瘦身（`32,360 → ≤ 23,000`）**：
   - 合并 6 处重复的 CUDA Driver API 动态加载样板、`src/gui/` 重复 Win32 控件/布局构建逻辑、`src/shim/ngx_bridge/` 与 `src/io/ffmpeg_io.cpp` 冗余空指针防御分支，并将 `AuditTypes.cmake` 切换为配置期默认 `FATAL_ERROR`。
3. **宿主机验收命令**：
   - `cmake --preset windows-msvc-ninja && cmake --build --preset windows-msvc-ninja`
   - `ctest --preset windows-msvc-ninja --output-on-failure`
   - `rg "snprintf|_snwprintf|ostringstream" include/ src/`
   - `cmake -P cmake/AuditTypes.cmake` 与 `cmake -P cmake/AuditSloc.cmake`
