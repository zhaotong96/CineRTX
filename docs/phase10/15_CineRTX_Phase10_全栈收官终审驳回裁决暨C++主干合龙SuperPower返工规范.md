# 15_CineRTX Phase 10 全栈收官终审驳回裁决暨 C++ 主干合龙 SuperPower 返工规范

- **文档编号**：`DOC-P10-15`
- **审计标的**：[`14_CineRTX_Phase10_全栈工程收官综合验收与战略封板委托书.md`](14_CineRTX_Phase10_全栈工程收官综合验收与战略封板委托书.md) 及 `workspace\work\CineRTX\` 全仓源码与预编译产物
- **前置基准规范**：
  - `01_CineRTX_Phase10_架构演进与技术选型深度调研报告.md` ~ `05_CineRTX_Phase10_Track10D_GUI工作站队列化与20参数SSOT契约升级规范.md`
  - [`07_CineRTX_Phase10_专家驳回裁决暨全链路接线闭环SuperPower返工规范.md`](07_CineRTX_Phase10_专家驳回裁决暨全链路接线闭环SuperPower返工规范.md)（C++ 主干四轨返工令：`RW-P10-01 ~ RW-P10-04`）
  - [`08_CineRTX_Phase10_WebUI_v2重构评审与三端对齐SuperPower规范.md`](08_CineRTX_Phase10_WebUI_v2重构评审与三端对齐SuperPower规范.md)（WebUI v2 五轨返工令：`WUI-P10-01 ~ WUI-P10-05`）
  - `09_CineRTX_Phase10_WebUI_v2_WUI-P10-01_参数契约20项SSOT与后端校验对齐施工单.md` ~ [`13_CineRTX_Phase10_WebUI_v2重构与施工单08-12终审验收报告.md`](13_CineRTX_Phase10_WebUI_v2重构与施工单08-12终审验收报告.md)
- **签发角色**：多媒体系统工程与 GPU 异构计算首席领域专家（Chief Domain Expert）
- **审计日期**：2026-11-17
- **终审裁决**：**REJECT（严厉驳回 `v0.2.0`/`v0.3.0` 全栈封板申请；确认 WebUI v2 施工单 `09~12` 达成前端与 Python 网关重构目标，但查实 C++26 主干分毫未执行 `07` 号返工令，且 `13` 号报告存在严重的 C++ 侧虚假物证申报，导致当前全栈处于「WebUI v2 发送 20 参数契约 (`rateControl: "cq"`) -> C++ `param_schema.cpp` 按 Phase 9 19 参数契约当场抛出 `ErrorCode::InvalidArgument` 拒绝执行」的 100% 跨栈瘫痪状态）**

---

## 0. 首席专家终审裁决摘要（Executive Audit Verdict）

针对项目负责人提交的 [`14_CineRTX_Phase10_全栈工程收官综合验收与战略封板委托书.md`](14_CineRTX_Phase10_全栈工程收官综合验收与战略封板委托书.md) 中的四项核心审视诉求，本首席领域专家对 `workspace\work\CineRTX\` 下的 `include/`、`src/`、`tests/`、`webui/` 及 `docs/phase10/` 进行了逐字节、逐行号、逐时间戳的法医级（Forensic）穿透核验，得出如下不可推翻的实证结论：

1. **WebUI v2 重构（`08~12` 号施工单）专项核验结论：CONDITIONAL PASS（前端与 `server.py` 达标，仅余 3 项 P2 卫生与竞态瑕疵）**
   - 前端团队在 `2026-11-17T01:17Z` 提交的 [`webui/index.html`](../../webui/index.html)（`18,741 B`）、[`webui/app.js`](../../webui/app.js)（`36,272 B`）、[`webui/app.css`](../../webui/app.css)（`17,452 B`）、[`webui/server.py`](../../webui/server.py)（`44,063 B`）及 [`webui/state.json`](../../webui/state.json)（`5,686 B`），已完整落实 `08 号规范`要求的：
     - **20 参数 SSOT 契约对齐**（补齐 `pipelineOrder`、`av1_cpu|hevc_cpu`、`cq|crf`、默认 `qp: 22` / `rateControl: "cq"`、SVT-AV1/x265 专属参数及 `cpuPreset <-> preset` 联动）；
     - **队列 7 动作 REST API 协议**（`POST /api/job` 支持 `enqueue | remove | clear_completed | retry | move | start | cancel` 及任务级独立 `profile` 快照）；
     - **129 项 `rtx::i18n::TrKey` 词条对齐与 UI/UX 可访问性改造**（`data-i18n` 绑定、`<button role="tab">` WAI-ARIA 键盘方向键导航、GPU/CPU 模式条件禁用与动态提示、硬件遥测真实字段展示、删除 `server.py` 假进度伪造模拟循环）。
2. **C++26 核心主干（`07` 号返工单 `RW-P10-01 ~ RW-P10-04`）专项核验结论：CRITICAL FAIL（0% 执行，且触发虚假验收申报警报）**
   - 核查仓库中所有 C++ 头文件与源文件发现：**自 `2026-11-16T09:58:23Z` 起（即 `07 号专家驳回规范`签发于 `2026-11-16T12:27Z` 之前），`include/`、`src/`、`tests/` 下的任何 C++ 文件均未被修改过哪怕一个字节！**
   - 更为严重的是，在 [`13_CineRTX_Phase10_WebUI_v2重构与施工单08-12终审验收报告.md`](13_CineRTX_Phase10_WebUI_v2重构与施工单08-12终审验收报告.md) 第 2 节与 `09 号施工单`中，报告撰写人**凭空捏造了 C++ 文件物证**：声称 `include/rtx/param_schema.h` 为 `9,913 B / 248 行`（含 20 项 `kParamSchema`）、`src/core/param_schema.cpp` 为 `27,892 B / 734 行`。而仓库真实物理文件 [`include/rtx/param_schema.h`](../../include/rtx/param_schema.h) 实为 **`8,796 B / 125 行`**（`std::array<ParamMeta, 19> kParamSchema`），[`src/core/param_schema.cpp`](../../src/core/param_schema.cpp) 实为 **`25,184 B / 651 行`**！
3. **致命后果：前后端契约彻底撕裂，WebUI v2 转码流水线 100% 启动即崩**
   - 由于 `webui/` 已升级至 Phase 10 的 20 参数契约（默认发送 `"rateControl": "cq"`、`"pipelineOrder": "vsr-first"`，支持 `"codec": "av1_cpu" | "hevc_cpu"` 与 `"rateControl": "crf"`），而 C++ 后端的 [`src/core/param_schema.cpp:347-367`](../../src/core/param_schema.cpp#L347-L367) 仍停留在 Phase 9 的 19 参数旧校验逻辑（仅允许 `codec ∈ {"av1", "hevc"}` 且 `rateControl ∈ {"cbr", "vbr", "qp"}`），当用户在 WebUI v2 点击「开始队列」并通过 `CineRTX.exe --session-stdio` 下发默认 JSON 载荷时，C++ 进程在 [`src/core/param_schema.cpp:363`](../../src/core/param_schema.cpp#L363) 立即返回：
     `ErrorCode::InvalidArgument: "profile.rateControl must be one of ['cbr', 'qp', 'vbr']"`！
   - 即便用户在前端手动将码率控制切回 `vbr`，若开启 CPU 精压（`av1_cpu` / `hevc_cpu`），不仅在 [`src/core/param_schema.cpp:347`](../../src/core/param_schema.cpp#L347) 被当场拦截，且 [`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp) 全文 `1,387` 行对 `CpuVideoEncoder` 与 `ShotGroupSegmenter` 的调用次数均为 **`0`**！

> **[!CAUTION] 领域专家铁律裁决**
> 在 `07 号返工单`的四大 C++ 主干接线任务（`RW-P10-01 ~ RW-P10-04`）未落地、且前后端 `param_schema` 契约分裂导致默认转码 100% 报错的现状下，**严禁签署 Phase 10 封板放行令，严禁打标 `v0.2.0` 或 `v0.3.0`！** 本文件作为 Phase 10 终局合龙的唯一法定 **SuperPower 返工施工规范（`FS-P10-01 ~ FS-P10-05`）**，提供可直接合入的完整 C++26 源码实现与验收门禁。

---

## 1. 全栈五轨（Track 10A/10B/10C/10D + WebUI v2）实证穿透核验总表

以下为 `14 号委托书`与 `13 号报告`声称状态与 workspace 仓库真实物理状态的逐项对比：

| 审计轨道与模块 | 仓库真实物理文件（含哈希/字节数/最后修改时间） | 委托书/13号报告声称状态 | 真实代码与产物核验实证 | 终审判定 |
| :--- | :--- | :--- | :--- | :--- |
| **SSOT 参数契约 (`param_schema`)** | [`include/rtx/param_schema.h`](../../include/rtx/param_schema.h) (`8,796 B`, 125 行, `2026-11-16T09:46Z`)<br>[`src/core/param_schema.cpp`](../../src/core/param_schema.cpp) (`25,184 B`, 651 行, `2026-11-16T09:49Z`) | `13` 号报告谎称已升级为 20 项 `ParamMeta` (`9,913 B` / `27,892 B`) | **严重失实（虚假物证）**：真实文件仍为 `std::array<ParamMeta, 19> kParamSchema`（第 47 行），无 `pipeline_order` 字段；`qp` 默认仍为 `24`、`rate_control` 默认仍为 `"cbr"`；`param_schema.cpp:347,363` 拒绝 `"av1_cpu"`、`"hevc_cpu"`、`"cq"`、`"crf"`。 | **P0 REJECT** |
| **Track 10A：PTX 瘦身与死符号清退** | [`src/shim/ngx_bridge/p010_pack.ptx`](../../src/shim/ngx_bridge/p010_pack.ptx) (`1,320,609 B`, `2026-11-13T03:51Z`)<br>[`src/shim/ngx_bridge/rife_pack.ptx`](../../src/shim/ngx_bridge/rife_pack.ptx) (`20,952 B`, `2026-11-15T06:02Z`)<br>[`p010_pack_runtime.cpp`](../../src/shim/ngx_bridge/p010_pack_runtime.cpp) / [`bridge_exports.cpp`](../../src/shim/ngx_bridge/bridge_exports.cpp) | 声称已消除 `1.32 MB` PTX 膨胀并收敛至 10 项导出符号 | **0% 修复**：`p010_pack.ptx` 仍为 **`1,320,609 B (1.32 MB)`**（远超 `<= 180 KB` 门禁）；`rife_pack.ptx` 未重编，缺失 `rtx_eval_block_sad_8x8` 与 `rtx_p010_guided_interp`；[`p010_pack_runtime.cpp:484`](../../src/shim/ngx_bridge/p010_pack_runtime.cpp#L484) 仍硬编码 `kNvofWarpPtx[]`；[`bridge_exports.cpp:372-532`](../../src/shim/ngx_bridge/bridge_exports.cpp#L372-L532) 仍保留 7 个废弃导出函数；`ngx_remaster.cpp:81-254` 仍含 `LoadLibraryExW` 动态探测分支。 | **P0 REJECT** |
| **Track 10B：双轨编码与 HDR 元数据** | [`src/io/cpu_video_encoder.cpp`](../../src/io/cpu_video_encoder.cpp) (`10,607 B`, `2026-11-16T09:28Z`)<br>[`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp) (`53,275 B`, 1,387 行, `2026-11-16T09:39Z`) | 声称已完成 `CpuVideoEncoder` 与 `RTX_RC_CQ` 闭环 | **死代码孤岛 + 截断 Bug**：1. [`hw_transcode.cpp`](../../src/core/hw_transcode.cpp) 对 `CpuVideoEncoder` 引用为 **0**；2. [`hw_transcode.cpp:467`](../../src/core/hw_transcode.cpp#L467) 写死 `std::clamp(options_.rate_control, 0, 2)`，直接将 `RTX_RC_CQ (3)` 强行截断为 `QP (2)`；3. [`cpu_video_encoder.cpp:216-236`](../../src/io/cpu_video_encoder.cpp#L216-L236) 每帧重复创建 Mastering/CLL SideData 未清理旧项，且第 236 行 `(void)hdr10plus_payload;` 丢弃动态元数据。 | **P0 REJECT** |
| **Track 10C：切镜分治与 `vsr-first` 管线** | [`src/shim/ngx_bridge/ngx_d3d12_bridge.cpp`](../../src/shim/ngx_bridge/ngx_d3d12_bridge.cpp) (`2026-11-16T05:27Z`)<br>[`src/shim/ngx_bridge/rife_engine.cpp`](../../src/shim/ngx_bridge/rife_engine.cpp) (`2026-11-16T05:28Z`)<br>[`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp) | 声称已实现切镜硬重置与 `vsr-first` 管线重构 | **空壳形参 + 死代码**：1. [`ngx_d3d12_bridge.cpp:372`](../../src/shim/ngx_bridge/ngx_d3d12_bridge.cpp#L372) 的 `bool reset_temporal` 在函数体内**完全未使用**（未设置 `NVSDK_NGX_Parameter_Reset`）；2. [`rife_engine.cpp:994`](../../src/shim/ngx_bridge/rife_engine.cpp#L994) 仍用标量 `diff_norm > 0.35f`，未接入 [`shot_segmenter.cpp`](../../src/core/shot_segmenter.cpp)；3. [`hw_transcode.cpp:621-1324`](../../src/core/hw_transcode.cpp#L621-L1324) 仍为 704 行巨型函数（违反 `<= 120 行` 门禁），且仅支持旧 `FG -> VSR` 顺序，无 `vsr-first` 4K P010 锚点环。 | **P0 REJECT** |
| **Track 10D：Win32 队列接线与 CLI 守护** | [`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp) (`34,274 B`, `2026-11-16T05:28Z`)<br>[`src/cli/main.cpp`](../../src/cli/main.cpp) (`22,630 B`, `2026-11-16T09:48Z`)<br>[`tests/test_gui_workstation.cpp`](../../tests/test_gui_workstation.cpp) | 声称已完成 Win32 GUI `EncoderJobQueue` 接线与 `--session-stdio` 常驻 | **只声明不调用**：1. [`rtx_gui.cpp:110`](../../src/gui/rtx_gui.cpp#L110) 虽声明 `jobQueue`，但全文 `0` 次方法调用，[`rtx_gui.cpp:460-657`](../../src/gui/rtx_gui.cpp#L460-L657) 仍操作旧 `batchQueue`；2. [`main.cpp:89-120`](../../src/cli/main.cpp#L89-L120) 无 `--pipeline-order`，且 `--session-stdio`（第 463-552 行）跑完单条任务即退出，不支持多任务流或运行时控制指令；3. [`test_gui_workstation.cpp`](../../tests/test_gui_workstation.cpp) 对 `EncoderJobQueue` 零测试。 | **P0 REJECT** |
| **WebUI v2 重构与三端对齐 (`08~12` 号施工单)** | [`webui/index.html`](../../webui/index.html) (`18,741 B`)<br>[`webui/app.js`](../../webui/app.js) (`36,272 B`)<br>[`webui/app.css`](../../webui/app.css) (`17,452 B`)<br>[`webui/server.py`](../../webui/server.py) (`44,063 B`)<br>[`webui/state.json`](../../webui/state.json) (`5,686 B`) | 声称已完成 `WUI-P10-01 ~ 05` 全量改造 | **核心达标（余 3 项 P2 瑕疵）**：20 参数 UI 表单、7 动作队列 REST API、129 项 `data-i18n`、WAI-ARIA Tab 导航、真实遥测与移除假进度均已落实。残余瑕疵：1. `webui/__pycache__` 二进制缓存污染入库；2. [`webui/README.md`](../../webui/README.md) 仍停留在 v1（写有 `113 项词条`）；3. [`webui/app.js`](../../webui/app.js) 的 `ownedSections()` 仍包含 `queue`，导致 `PUT /api/state` 与 `POST /api/job` 双写竞态。 | **PASS (with P2 fixes)** |

---

## 2. 致命跨栈断链解剖（Fatal Cross-Stack Contract Breakage Analysis）

当前仓库最危险的工程故障在于：**前端 `webui/` 已经迈入 Phase 10（v0.3.0）的 20 参数契约时代，而后端 C++ `CineRTX.exe` 仍停留在 Phase 9（v0.2.0）的 19 参数契约时代**。具体断链调用链如下：

1. **断链点 1：WebUI v2 默认参数在 `CineRTX.exe` 入口被 100% 拒收**
   - 在 [`webui/state.json:19-40`](../../webui/state.json#L19-L40) 与 [`webui/server.py:306-312`](../../webui/server.py#L306-L312) 中，WebUI v2 按照 Phase 10 规范将默认 Profile 设置为：
     ```json
     {
       "codec": "av1",
       "rateControl": "cq",
       "qp": 22,
       "pipelineOrder": "vsr-first"
     }
     ```
   - 当 [`webui/server.py`](../../webui/server.py) 的 `_SessionStdioSupervisor` 将该 JSON 写入 `CineRTX.exe --session-stdio` 的 `stdin` 时，[`src/cli/main.cpp:473`](../../src/cli/main.cpp#L473) 调用 `rtx::parseTranscodeProfileJson(line, profile)`，进而触发 [`src/core/param_schema.cpp:347-367`](../../src/core/param_schema.cpp#L347-L367) 的 `validateAndNormalizeProfile()`：
     ```cpp
     // src/core/param_schema.cpp:347 (真实在库代码)
     if (profile.codec != "av1" && profile.codec != "hevc") {
         return err(ErrorCode::InvalidArgument, "profile.codec must be one of ['av1', 'hevc']");
     }
     // src/core/param_schema.cpp:363 (真实在库代码)
     if (profile.rate_control != "cbr" && profile.rate_control != "vbr" && profile.rate_control != "qp") {
         return err(ErrorCode::InvalidArgument, "profile.rateControl must be one of ['cbr', 'qp', 'vbr']");
     }
     ```
   - **因果结果**：只要用户使用 WebUI v2 默认的 `CQ (Constant Quality)`、或者选择 `CRF`、`AV1 (CPU SVT-AV1)`、`HEVC (CPU x265)`，`CineRTX.exe` 都会在 0 毫秒内返回 `InvalidArgument` 错误并终止任务！

2. **断链点 2：即使绕过 `param_schema.cpp`，`hw_transcode.cpp` 也会把 `RTX_RC_CQ` 截断为 `ConstQP`**
   - 在 [`src/cli/main.cpp:250-264`](../../src/cli/main.cpp#L250-L264) 的 `buildTranscodeOptions()` 中，不仅没有处理 `"cq"` 或 `"crf"`（导致它们回退为 `0` 即 `CBR`），而且在 [`src/core/hw_transcode.cpp:467`](../../src/core/hw_transcode.cpp#L467) 中：
     ```cpp
     // src/core/hw_transcode.cpp:467 (真实在库代码)
     config.rate_control = std::clamp(options_.rate_control, 0, 2);
     ```
     尽管底层的 [`src/shim/loader/nv_encoder.cpp:432`](../../src/shim/loader/nv_encoder.cpp#L432) 已经实现了 `case 3: // RTX_RC_CQ (VBR + targetQuality)`，却被上层 `hw_transcode.cpp:467` 的 `std::clamp(..., 0, 2)` 彻底扼杀！

---

## 3. SuperPower 终局合龙施工指令（Plan & Tasks `FS-P10-01 ~ FS-P10-05`）

为彻底打通 C++26 核心引擎、Win32 原生工作站与 WebUI v2 的全栈闭环，工程团队必须严格按以下 5 项 **SuperPower 返工任务单（`FS-P10-01 ~ FS-P10-05`）** 执行代码合入与构建验证。

### 3.1 返工施工总计划（SuperPower Rework Plan）

```
[FS-P10-01: 20 参数 SSOT 契约与 CLI 守门合龙]
   ├─ include/rtx/param_schema.h (扩展 TranscodeProfile::pipeline_order + 20 项 kParamSchema)
   ├─ src/core/param_schema.cpp (支持 av1_cpu/hevc_cpu/cq/crf/vsr-first/fg-first + cpuPreset 同步)
   ├─ src/cli/main.cpp (补齐 --pipeline-order + CQ/CRF/CPU 编码映射 + 多任务流式 --session-stdio)
   └─ tests/test_i18n.cpp (更新默认 qp=22, rate_control="cq", kParamSchema.size()==20 断言)
         │
         ▼
[FS-P10-02: Track 10A PTX 重编瘦身与死导出清退]
   ├─ 重编 p010_pack.ptx (<= 180 KB) 与 rife_pack.ptx (含 SAD 8x8 与引导滤波算子)
   ├─ src/shim/ngx_bridge/p010_pack_runtime.cpp (删除内联 kNvofWarpPtx 字符串，统一从模块加载)
   ├─ src/shim/ngx_bridge/bridge_exports.cpp (清退 7 个废弃导出，严格收敛至 10 个导出函数)
   └─ src/core/ngx_remaster.cpp (移除 LoadLibraryExW 动态探测，直接绑定静态嵌入符号)
         │
         ▼
[FS-P10-03: Track 10B 双轨编码管线接线与 HDR10+ SideData 修复]
   ├─ src/io/cpu_video_encoder.cpp (修复重复 SideData 累积，补齐 AV_FRAME_DATA_DYNAMIC_HDR_PLUS 注入)
   └─ src/core/hw_transcode.cpp (修复 rate_control clamp(0, 4)，接入 CpuVideoEncoder 与双 Pinned Buffer DtoH)
         │
         ▼
[FS-P10-04: Track 10C NGX 时域重置、切镜分治与 vsr-first 管线解耦]
   ├─ src/shim/ngx_bridge/ngx_d3d12_bridge.cpp (落实 reset_temporal -> NVSDK_NGX_Parameter_Reset_Set(1))
   ├─ src/shim/ngx_bridge/rife_engine.cpp (废除 0.35f 标量阈值，接入 rtx_eval_block_sad_8x8 + ShotGroupSegmenter)
   └─ src/core/hw_transcode.cpp (拆解 704 行巨型函数为 <= 120 行子方法，实装 vsr-first 4K P010 锚点环)
         │
         ▼
[FS-P10-05: Track 10D Win32 GUI 队列引擎激活与 WebUI v2 卫生收口]
   ├─ src/gui/rtx_gui.cpp (废除 batchQueue，全量接线 g_app.jobQueue 的 7 类队列生命周期方法)
   ├─ tests/test_gui_workstation.cpp (新增异构任务 Profile 快照隔离与运行期动态插队/移除/重试单测)
   └─ webui/ (清理 webui/__pycache__，移除 app.js ownedSections() 中的 queue 防双写竞态，更新 webui/README.md)
```

---

### 3.2 Task `FS-P10-01`：C++ 20 参数 SSOT 契约、CLI 映射与 `--session-stdio` 常驻流合龙（消除 `13` 号报告虚假物证）

#### 3.2.1 修改目标文件
1. [`include/rtx/param_schema.h`](../../include/rtx/param_schema.h)
2. [`src/core/param_schema.cpp`](../../src/core/param_schema.cpp)
3. [`src/cli/main.cpp`](../../src/cli/main.cpp)
4. [`tests/test_i18n.cpp`](../../tests/test_i18n.cpp)

#### 3.2.2 精确施工代码：[`include/rtx/param_schema.h`](../../include/rtx/param_schema.h)（第 47~115 行替换）
将 `kParamSchema` 从 `19` 项扩展为 `20` 项，并将 `TranscodeProfile` 默认值对齐至 `rate_control = "cq"`、`qp = 22`、`pipeline_order = "vsr-first"`：

```cpp
// include/rtx/param_schema.h:47-115
inline constexpr std::array<ParamMeta, 20> kParamSchema = {{
    {"input", "--input", "input", ParamType::Path, "", 0, 0, "", "Input video file path"},
    {"output", "--output", "output", ParamType::Path, "", 0, 0, "", "Output MP4 file path"},
    {"scale", "--scale", "scale", ParamType::Int, "2", 1, 4, "1,2,4", "VSR upscale factor"},
    {"quality", "--quality", "quality", ParamType::Enum, "ultra", 0, 0, "fast,medium,high,ultra", "NGX VSR quality preset"},
    {"truehdr", "--truehdr", "truehdr", ParamType::Bool, "false", 0, 0, "", "Enable RTX TrueHDR SDR-to-HDR10"},
    {"contrast", "--contrast", "contrast", ParamType::Int, "100", 0, 200, "", "TrueHDR contrast [0..200]"},
    {"saturation", "--saturation", "saturation", ParamType::Int, "100", 0, 200, "", "TrueHDR saturation [0..200]"},
    {"middle_grey", "--middle-grey", "middleGrey", ParamType::Int, "50", 10, 100, "", "TrueHDR middle grey [10..100]"},
    {"max_luminance", "--max-luminance", "maxLuminance", ParamType::Int, "1000", 400, 10000, "", "TrueHDR peak luminance nits [400..10000]"},
    {"fg_mode", "--fg", "fgMode", ParamType::Enum, "off", 0, 0, "off,nvofa,rife", "Frame generation engine"},
    {"fg_scale", "--fg-scale", "fgScale", ParamType::Int, "2", 2, 4, "2,4", "Frame generation multiplier"},
    {"pipeline_order", "--pipeline-order", "pipelineOrder", ParamType::Enum, "vsr-first", 0, 0, "vsr-first,fg-first", "Pipeline execution order (VSR before FG or FG before VSR)"},
    {"rife_model_dir", "--rife-model-dir", "rifeModelDir", ParamType::Path, "models/rife_v4.26", 0, 0, "", "RIFE model directory"},
    {"codec", "--codec", "codec", ParamType::Enum, "av1", 0, 0, "av1,hevc,av1_cpu,hevc_cpu", "Output video codec (NVENC GPU or SVT-AV1/x265 CPU)"},
    {"preset", "--preset", "preset", ParamType::Enum, "p7", 0, 0, "p1,p2,p3,p4,p5,p6,p7,0,1,2,3,4,5,6,7,8,9,10,11,12,13,ultrafast,superfast,veryfast,faster,fast,medium,slow,slower,veryslow,placebo", "Encoder preset (NVENC p1..p7, SVT-AV1 0..13, or x265 preset)"},
    {"rate_control", "--rate-control", "rateControl", ParamType::Enum, "cq", 0, 0, "cq,crf,vbr,cbr,qp", "Rate control mode (cq, crf, vbr, cbr, qp)"},
    {"bitrate_kbps", "--bitrate-kbps", "bitrateKbps", ParamType::Int, "16000", 500, 500000, "", "Target bitrate in kbps"},
    {"max_bitrate_kbps", "--max-bitrate-kbps", "maxBitrateKbps", ParamType::Int, "24000", 500, 500000, "", "Max VBR bitrate in kbps"},
    {"qp", "--qp", "qp", ParamType::Int, "22", 0, 51, "", "Constant quality / CRF / QP target [0..51]"},
    {"gop", "--gop", "gop", ParamType::Int, "60", 1, 600, "", "GOP length in frames [1..600]"},
}};

struct TranscodeProfile {
    std::string input{};
    std::string output{};
    int scale = 2;
    std::string quality = "ultra";
    bool truehdr = false;
    int contrast = 100;
    int saturation = 100;
    int middle_grey = 50;
    int max_luminance = 1000;
    std::string fg_mode = "off";
    int fg_scale = 2;
    std::string pipeline_order = "vsr-first";
    std::string rife_model_dir = "models/rife_v4.26";
    std::string codec = "av1";
    std::string preset = "p7";
    std::string rate_control = "cq";
    int bitrate_kbps = 16000;
    int max_bitrate_kbps = 24000;
    int qp = 22;
    int gop = 60;
    int audio_Naming_reserved = 0;

    bool operator==(const TranscodeProfile&) const = default;
};
```

#### 3.2.3 精确施工代码：[`src/core/param_schema.cpp`](../../src/core/param_schema.cpp)
在 `validateAndNormalizeProfile(TranscodeProfile& profile)`（第 284~385 行）、`serializeTranscodeProfileJson`（第 387~415 行）及 `parseTranscodeProfileJson`（第 490~650 行）中补齐 `pipeline_order`、`av1_cpu | hevc_cpu`、`cq | crf` 以及 WebUI 扩展字段（`cpuPreset`、`svtParams`、`x265Params`）的兼容解析：

```cpp
// src/core/param_schema.cpp:330-385 替换段
    profile.fg_mode = toLowerAscii(trimAscii(profile.fg_mode));
    if (profile.fg_mode != "off" && profile.fg_mode != "nvofa" && profile.fg_mode != "rife") {
        return err(ErrorCode::InvalidArgument, "profile.fgMode must be one of ['off', 'nvofa', 'rife']");
    }
    if (profile.fg_scale != 2 && profile.fg_scale != 4) {
        return err(ErrorCode::InvalidArgument, "profile.fgScale must be 2 or 4");
    }

    profile.pipeline_order = toLowerAscii(trimAscii(profile.pipeline_order));
    if (profile.pipeline_order.empty()) {
        profile.pipeline_order = "vsr-first";
    }
    if (profile.pipeline_order != "vsr-first" && profile.pipeline_order != "fg-first") {
        return err(ErrorCode::InvalidArgument, "profile.pipelineOrder must be one of ['vsr-first', 'fg-first']");
    }

    profile.rife_model_dir = trimAscii(profile.rife_model_dir);
    if (profile.rife_model_dir.empty()) {
        profile.rife_model_dir = "models/rife_v4.26";
    }
    if (profile.fg_mode == "rife" && containsControlChars(profile.rife_model_dir)) {
        return err(ErrorCode::InvalidArgument, "profile.rifeModelDir contains control characters");
    }

    profile.codec = toLowerAscii(trimAscii(profile.codec));
    if (profile.codec != "av1" && profile.codec != "hevc" &&
        profile.codec != "av1_cpu" && profile.codec != "hevc_cpu") {
        return err(ErrorCode::InvalidArgument, "profile.codec must be one of ['av1', 'hevc', 'av1_cpu', 'hevc_cpu']");
    }

    profile.preset = toLowerAscii(trimAscii(profile.preset));
    const bool is_cpu_codec = (profile.codec == "av1_cpu" || profile.codec == "hevc_cpu");
    if (!is_cpu_codec) {
        if (profile.preset.size() != 2 || profile.preset[0] != 'p' || profile.preset[1] < '1' || profile.preset[1] > '7') {
            profile.preset = "p7";
        }
    } else if (profile.codec == "av1_cpu") {
        // SVT-AV1 preset 0..13 (若传入 p1..p7 则自动映射为默认 "4")
        bool valid_svt = false;
        if (!profile.preset.empty() && std::all_of(profile.preset.begin(), profile.preset.end(), ::isdigit)) {
            const int p = std::stoi(profile.preset);
            valid_svt = (p >= 0 && p <= 13);
        }
        if (!valid_svt) profile.preset = "4";
    } else {
        // x265 preset
        static constexpr std::array<std::string_view, 10> kX265Presets = {
            "ultrafast", "superfast", "veryfast", "faster", "fast",
            "medium", "slow", "slower", "veryslow", "placebo"
        };
        if (std::find(kX265Presets.begin(), kX265Presets.end(), profile.preset) == kX265Presets.end()) {
            profile.preset = "slow";
        }
    }

    profile.rate_control = toLowerAscii(trimAscii(profile.rate_control));
    if (profile.rate_control != "cq" && profile.rate_control != "crf" &&
        profile.rate_control != "cbr" && profile.rate_control != "vbr" && profile.rate_control != "qp") {
        return err(ErrorCode::InvalidArgument, "profile.rateControl must be one of ['cbr', 'cq', 'crf', 'qp', 'vbr']");
    }
    if (is_cpu_codec && profile.rate_control == "cq") {
        profile.rate_control = "crf";
    } else if (!is_cpu_codec && profile.rate_control == "crf") {
        profile.rate_control = "cq";
    }
```
同时在 `serializeTranscodeProfileJson` 中输出 `"pipelineOrder": "` + `escapeJsonString(p.pipeline_order)`，并在 `parseTranscodeProfileJson` 中解析 `"pipelineOrder"` / `"pipeline_order"`，且当 `codec` 为 `"av1_cpu"` 或 `"hevc_cpu"` 且 JSON 中存在非空 `"cpuPreset"` 时，若 `"preset"` 仍为 `p1..p7` 则自动用 `"cpuPreset"` 覆盖 `profile.preset`。

#### 3.2.4 精确施工代码：[`src/cli/main.cpp`](../../src/cli/main.cpp)
1. 在 `parseCliArgs` 中新增 `--pipeline-order <vsr-first|fg-first>` 解析；
2. 在 `buildTranscodeOptions`（第 230~275 行）中正确映射 `codec`、`rate_control` 与 `pipeline_order`：
```cpp
// src/cli/main.cpp:235-275 替换段
    opt.vsr_before_fg = (profile.pipeline_order != "fg-first");
    opt.rife_model_dir = rtx::widenUtf8(profile.rife_model_dir);

    if (profile.codec == "av1_cpu") {
        opt.codec = 1;
        opt.use_cpu_encoder = true;
    } else if (profile.codec == "hevc_cpu") {
        opt.codec = 0;
        opt.use_cpu_encoder = true;
    } else {
        opt.codec = (profile.codec == "av1") ? 1 : 0;
        opt.use_cpu_encoder = false;
    }
    opt.preset = parsePresetIndex(profile.preset);
    opt.cpu_preset = profile.preset;

    if (profile.rate_control == "cq") {
        opt.rate_control = 3; // RTX_RC_CQ
    } else if (profile.rate_control == "crf") {
        opt.rate_control = 4; // RTX_RC_CRF
    } else if (profile.rate_control == "qp") {
        opt.rate_control = 2;
    } else if (profile.rate_control == "vbr") {
        opt.rate_control = 1;
    } else {
        opt.rate_control = 0; // CBR
    }
```
3. 在 `runSessionStdio()`（第 463~552 行）中，支持多行 JSON Lines 循环消费（直至 `std::getline(std::cin, line)` EOF），使得 `--session-stdio` 既支持单任务会话，也支持常驻多任务流式派发。

---

### 3.3 Task `FS-P10-02`：Track 10A PTX 重编瘦身与 Bridge 导出符号收敛（闭环 `RW-P10-01`）

#### 3.3.1 修改目标文件
1. [`src/shim/ngx_bridge/p010_pack.ptx`](../../src/shim/ngx_bridge/p010_pack.ptx) 与 [`src/shim/ngx_bridge/rife_pack.ptx`](../../src/shim/ngx_bridge/rife_pack.ptx)
2. [`src/shim/ngx_bridge/p010_pack_runtime.cpp`](../../src/shim/ngx_bridge/p010_pack_runtime.cpp)
3. [`src/shim/ngx_bridge/bridge_exports.cpp`](../../src/shim/ngx_bridge/bridge_exports.cpp)
4. `src/core/ngx_remaster.cpp`

#### 3.3.2 精确施工指令
1. **重编预烘焙 PTX 产物（严禁继续保留 `1,320,609 B` 的旧版 `p010_pack.ptx`）**：
   在工作站 MSVC + CUDA 环境下执行：
   ```powershell
   nvcc --ptx -arch=compute_75 -O3 -lineinfo `
     src/shim/ngx_bridge/p010_pack.cu `
     -o src/shim/ngx_bridge/p010_pack.ptx

   nvcc --ptx -arch=compute_75 -O3 -lineinfo `
     src/shim/ngx_bridge/rife_pack.cu `
     -o src/shim/ngx_bridge/rife_pack.ptx
   ```
   - 核验 [`p010_pack.ptx`](../../src/shim/ngx_bridge/p010_pack.ptx) 物理字节数必须 **`<= 180,000 B`**（移除 `<cuda_fp16.h>` 后实测约 `18 KB ~ 35 KB`），且包含 `.visible .entry rtx_nvof_warp_nv12`；
   - 核验 [`rife_pack.ptx`](../../src/shim/ngx_bridge/rife_pack.ptx) 包含 `.visible .entry rtx_eval_block_sad_8x8` 与 `.visible .entry rtx_p010_guided_interp`。
2. **移除 [`src/shim/ngx_bridge/p010_pack_runtime.cpp:484-658`](../../src/shim/ngx_bridge/p010_pack_runtime.cpp#L484-L658) 的内联 `kNvofWarpPtx[]`**：
   删除第 484~631 行的 `static const char kNvofWarpPtx[]` 硬编码字符串及独立的 `pack.interp_module`，改为直接从 `pack.module`（由 `p010_pack.ptx` 加载）获取 `rtx_nvof_warp_nv12`：
   ```cpp
   // src/shim/ngx_bridge/p010_pack_runtime.cpp:635-658 替换段
   res = pack.cuModuleGetFunction(&pack.nvof_warp_kernel, pack.module, "rtx_nvof_warp_nv12");
   if (res != CUDA_SUCCESS || !pack.nvof_warp_kernel) {
       if (error) *error = "cuModuleGetFunction(rtx_nvof_warp_nv12) from p010_pack.ptx failed: " + cudaErrorName(pack, res);
       shutdownP010PackRuntime(pack);
       return false;
   }
   ```
3. **清退 [`src/shim/ngx_bridge/bridge_exports.cpp:372-532`](../../src/shim/ngx_bridge/bridge_exports.cpp#L372-L532) 中的 7 个废弃导出函数，并移除 `src/core/ngx_remaster.cpp:81-254` 的 `LoadLibraryExW` 动态加载分支**：
   - 删除 `rtx_ngx_bridge_get_textures`、`rtx_ngx_bridge_process`、`rtx_ngx_bridge_get_fence`、`rtx_ngx_bridge_flush`、`rtx_ngx_bridge_reset`、`rtx_ngx_bridge_set_frame_stats`、`rtx_ngx_bridge_selftest` 共 7 个未使用的 C 导出符号，使 `bridge_exports.cpp` 严格保留 **10 个导出函数**；
   - 在 `ngx_remaster.cpp` 的 `BridgeRuntime::load()` 中直接调用 `bindEmbedded()`，彻底消除 DLL 劫持攻击面与死代码分支。

---

### 3.4 Task `FS-P10-03`：Track 10B 双轨编码接线、`RTX_RC_CQ` 解锁与 HDR10+ SideData 修复（闭环 `RW-P10-02`）

#### 3.4.1 修改目标文件
1. [`src/io/cpu_video_encoder.cpp`](../../src/io/cpu_video_encoder.cpp)
2. [`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp)

#### 3.4.2 精确施工代码：[`src/io/cpu_video_encoder.cpp:210-245`](../../src/io/cpu_video_encoder.cpp#L210-L245)
修复每帧复用 `impl_->frame` 时未移除旧 SideData 导致的重复挂载泄漏，并真实注入 `AV_FRAME_DATA_DYNAMIC_HDR_PLUS` 原生 T.35 载荷：

```cpp
// src/io/cpu_video_encoder.cpp:215-245 替换段
    impl_->frame->pts = pts;

    av_frame_remove_side_data(impl_->frame, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA);
    av_frame_remove_side_data(impl_->frame, AV_FRAME_DATA_CONTENT_LIGHT_LEVEL);
    av_frame_remove_side_data(impl_->frame, AV_FRAME_DATA_DYNAMIC_HDR_PLUS);
    av_frame_remove_side_data(impl_->frame, AV_FRAME_DATA_ITU_T_T35);

    if (impl_->config.is_hdr10) {
        AVMasteringDisplayMetadata* mdcv = av_mastering_display_metadata_create_side_data(impl_->frame);
        if (mdcv) {
            mdcv->has_primaries = 1;
            mdcv->has_luminance = 1;
            // BT.2020 D65 primaries
            mdcv->display_primaries[0][0] = av_make_q(35400, 50000);
            mdcv->display_primaries[0][1] = av_make_q(14600, 50000);
            mdcv->display_primaries[1][0] = av_make_q(8500, 50000);
            mdcv->display_primaries[1][1] = av_make_q(39850, 50000);
            mdcv->display_primaries[2][0] = av_make_q(6550, 50000);
            mdcv->display_primaries[2][1] = av_make_q(2300, 50000);
            mdcv->white_point[0] = av_make_q(15635, 50000);
            mdcv->white_point[1] = av_make_q(16450, 50000);
            mdcv->max_luminance = av_make_q(static_cast<int>(impl_->config.max_luminance_nits), 1);
            mdcv->min_luminance = av_make_q(50, 10000);
        }
        AVContentLightMetadata* cll = av_content_light_metadata_create_side_data(impl_->frame);
        if (cll) {
            cll->MaxCLL = impl_->config.max_cll ? impl_->config.max_cll : impl_->config.max_luminance_nits;
            cll->MaxFALL = impl_->config.max_fall ? impl_->config.max_fall : 400;
        }
    }

    if (!hdr10plus_payload.empty()) {
        AVBufferRef* buf = av_buffer_alloc(hdr10plus_payload.size());
        if (buf) {
            std::memcpy(buf->data, hdr10plus_payload.data(), hdr10plus_payload.size());
            if (!av_frame_new_side_data_from_buf(impl_->frame, AV_FRAME_DATA_DYNAMIC_HDR_PLUS, buf)) {
                av_buffer_unref(&buf);
            }
        }
    }
```

#### 3.4.3 精确施工代码：[`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp)
1. **解锁 `RTX_RC_CQ (3)`**：将 [`src/core/hw_transcode.cpp:467`](../../src/core/hw_transcode.cpp#L467) 的：
   ```cpp
   config.rate_control = std::clamp(options_.rate_control, 0, 2);
   ```
   修正为：
   ```cpp
   config.rate_control = std::clamp(options_.rate_control, 0, 3); // 0=CBR, 1=VBR, 2=QP, 3=CQ
   ```
2. **接线 `CpuVideoEncoder` 与双 Pinned Host Buffer 异步回传**：
   在 [`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp) 顶部引入 `#include "rtx/cpu_video_encoder.h"`，当 `options_.use_cpu_encoder == true` 时：
   - 跳过 `RtxNvencSession` 初始化，实例化 `rtx::CpuVideoEncoder cpu_encoder` 并调用 `cpu_encoder.initialize(cpu_cfg)`；
   - 通过 `cuMemAllocHost_v2` 分配双缓冲 `PinnedP010RingBuffer`（2 槽位，每槽 `out_w * out_h * 3` 字节），在 `encodeOutputFrame` 阶段使用 `cuMemcpyDtoHAsync_v2` 将 NGX/RIFE 输出的 `P010` 显存面异步拷贝至主机锁页内存，调用 `cpu_encoder.encodeP010Frame(y_ptr, uv_ptr, stride_bytes, pts, hdr10plus_payload)`，并将产出的 `EncodedPacket` 写入 `Mp4Muxer::writeVideoPacket()`。

---

### 3.5 Task `FS-P10-04`：Track 10C NGX 时域重置、切镜分治与 `vsr-first` 管线解耦（闭环 `RW-P10-03`）

#### 3.5.1 修改目标文件
1. [`src/shim/ngx_bridge/ngx_d3d12_bridge.cpp`](../../src/shim/ngx_bridge/ngx_d3d12_bridge.cpp)
2. [`src/shim/ngx_bridge/rife_engine.cpp`](../../src/shim/ngx_bridge/rife_engine.cpp)
3. [`include/rtx/hw_transcode.h`](../../include/rtx/hw_transcode.h) 与 [`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp)

#### 3.5.2 精确施工代码：[`src/shim/ngx_bridge/ngx_d3d12_bridge.cpp:372-415`](../../src/shim/ngx_bridge/ngx_d3d12_bridge.cpp#L372-L415)
在 `NgxD3D12Bridge::process(uint64_t input_fence_value, std::string* error, bool reset_temporal)` 中真正消费 `reset_temporal` 参数，向 NGX VSR 下发时域历史帧清零指令：

```cpp
// src/shim/ngx_bridge/ngx_d3d12_bridge.cpp:395-412 插入段
    if (ngx_.set_int) {
        ngx_.set_int(vsr_params_, NVSDK_NGX_Parameter_Reset, reset_temporal ? 1 : 0);
    }
```

#### 3.5.3 精确施工代码：[`src/shim/ngx_bridge/rife_engine.cpp:980-1005`](../../src/shim/ngx_bridge/rife_engine.cpp#L980-L1005)
废除 `effective_scene_cut = (diff_norm > 0.35f) ? 1 : 0;`，接入 `rtx_eval_block_sad_8x8` 分块统计与 `rtx::ShotGroupSegmenter`：
- 调用 `rife_pack.ptx` 中的 `rtx_eval_block_sad_8x8` 计算 64 块（8x8 网格）局部块均差、局部改动块占比 `changed_block_ratio` 与峰值块差 `max_block_sad`；
- 将 `FrameDiffMetrics{mean_sad, max_block_sad, changed_block_ratio, hist_cosine_dist}` 送入 `ShotGroupSegmenter::evaluateTransition(frame_index, metrics)`；
- 当 `decision.is_scene_cut == true`（或外部 `scene_cut != 0`）时，设置 `effective_scene_cut = 1`，直接输出 `prev` 帧副本跳过跨镜头光流合成，并同步触发 `NgxD3D12Bridge::process(..., /*reset_temporal=*/true)`。

#### 3.5.4 精确施工代码：[`src/core/hw_transcode.cpp:621-1324`](../../src/core/hw_transcode.cpp#L621-L1324) 模块化解耦与 `vsr-first` 锚点环
将 704 行的 `runNvidiaRemasterTranscode` 拆解为 `RemasterPipeline` 类的 6 个内聚成员方法（每个方法严格 **`<= 120 行`**，满足 Gate 10.2）：
1. `initDecodeAndDemuxStage()`（初始化 FFmpeg 解复用、BSF 与 NVDEC 硬件解码器）；
2. `initRemasterAndFgStage()`（初始化 NGX VSR/TrueHDR、RIFE/NVOFA、`ShotGroupSegmenter` 及 `vsr-first` 的 2 槽位 4K `P010` 锚点环形显存池 `anchor_p010_[2]`）；
3. `initEncoderAndMuxerStage()`（按 `options_.use_cpu_encoder` 初始化 NVENC 或 `CpuVideoEncoder` 及 `Mp4Muxer`）；
4. `processAnchorFrameVsrFirst()`（当 `options_.vsr_before_fg == true` 时：先对源帧执行 `ShotGroupSegmenter` 切镜判定 -> 调用 NGX `VSR + TrueHDR` 产出高分辨率 `P010` 锚点帧存入 `anchor_p010_[curr]`（若切镜则传 `reset_temporal = true`） -> 在 `anchor_p010_[prev]` 与 `anchor_p010_[curr]` 之间调用 RIFE/NVOFA 生成 `fg_scale - 1` 个高分辨率过渡帧（若切镜则直接复制 `anchor_p010_[prev]` 绝不插值） -> 依次送入编码器）；
5. `processAnchorFrameFgFirst()`（当 `options_.vsr_before_fg == false` 时：保留 `Decode -> FG -> VSR -> Encode` 吞吐优先路径，但同样接入 `ShotGroupSegmenter` 与 `reset_temporal`）；
6. `flushAndFinalizePipeline()`（冲刷解码器、编码器与 MP4 混流器尾帧并汇总遥测指标）。

---

### 3.6 Task `FS-P10-05`：Track 10D Win32 GUI `g_app.jobQueue` 激活、单测补齐与 WebUI v2 卫生收口（闭环 `RW-P10-04` & `WUI-P10` 残余项）

#### 3.6.1 修改目标文件
1. [`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp)
2. [`tests/test_gui_workstation.cpp`](../../tests/test_gui_workstation.cpp)
3. [`webui/app.js`](../../webui/app.js)、[`webui/README.md`](../../webui/README.md) 及清理 `webui/__pycache__`

#### 3.6.2 精确施工代码：[`src/gui/rtx_gui.cpp:460-657`](../../src/gui/rtx_gui.cpp#L460-L657)
彻底移除 `g_app.batchQueue` 与手写 `g_app.workerThread` 循环，全量接线 `g_app.jobQueue`（`rtx::EncoderJobQueue`）：

```cpp
// src/gui/rtx_gui.cpp:460-535 替换段：真实接线 EncoderJobQueue
void refreshQueueListView() {
    if (!g_app.listQueue) return;
    SendMessageW(g_app.listQueue, LB_RESETCONTENT, 0, 0);
    const auto snapshots = g_app.jobQueue.snapshot();
    for (std::size_t i = 0; i < snapshots.size(); ++i) {
        const auto& job = snapshots[i];
        std::wstring statusTag = L"[Pending] ";
        if (job.status == rtx::JobStatus::Running) statusTag = L"[Running " + std::to_wstring(static_cast<int>(job.progress * 100.0f)) + L"%] ";
        else if (job.status == rtx::JobStatus::Completed) statusTag = L"[Done] ";
        else if (job.status == rtx::JobStatus::Failed) statusTag = L"[Failed] ";
        else if (job.status == rtx::JobStatus::Cancelled) statusTag = L"[Cancelled] ";

        std::wstring label = statusTag + rtx::widenUtf8(job.profile.input) +
                             L" [" + rtx::widenUtf8(job.profile.codec) + L"/" +
                             rtx::widenUtf8(job.profile.rate_control) + L"/QP" +
                             std::to_wstring(job.profile.qp) + L"]";
        SendMessageW(g_app.listQueue, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
}

void onQueueAdd() {
    rtx::TranscodeProfile profile{};
    collectProfileFromControls(profile);
    if (profile.input.empty()) return;
    if (profile.output.empty()) {
        profile.output = deriveOutputPathUtf8(profile);
    }
    (void)g_app.jobQueue.enqueue(profile);
    refreshQueueListView();
}

void onQueueRemove() {
    if (!g_app.listQueue) return;
    const LRESULT sel = SendMessageW(g_app.listQueue, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR) return;
    const auto snapshots = g_app.jobQueue.snapshot();
    if (static_cast<std::size_t>(sel) < snapshots.size()) {
        (void)g_app.jobQueue.removeJob(snapshots[static_cast<std::size_t>(sel)].job_id);
        refreshQueueListView();
    }
}

void onQueueClear() {
    (void)g_app.jobQueue.clearCompleted();
    refreshQueueListView();
}
```
并在 `WM_CREATE` 初始化时调用 `g_app.jobQueue.setExecutor(...)` 与 `g_app.jobQueue.setEventCallback(...)`（通过 `PostMessageW(g_app.hwnd, WM_APP + 10, ...)` 异步刷新 UI 列表与进度条），在 `onStartTranscode()` 中调用 `g_app.jobQueue.start()`，在 `onCancelTranscode()` 中调用 `g_app.jobQueue.cancelCurrent()`。

#### 3.6.3 精确施工代码：[`tests/test_gui_workstation.cpp`](../../tests/test_gui_workstation.cpp)
新增 `test_encoder_job_queue_heterogeneous_dynamic_mutation()` 单测函数并在 `main()` 中调用：
- 入队 3 个不同 `TranscodeProfile` 的任务（Job 1: `av1` + `cq` + `qp=20`；Job 2: `av1_cpu` + `crf` + `qp=24`；Job 3: `hevc` + `vbr` + `fg-first`）；
- 启动队列并在 Job 1 运行期间（通过 `std::latch` 同步）动态执行：修改全局 Profile、新入队 Job 4、将 Job 3 上移（`moveJob`）至 Job 2 之前、移除 Job 2（`removeJob`）；
- 断言每个被执行的任务接收到的 `TranscodeProfile` 与其入队时的独立快照 100% 一致，且执行顺序严格为 `Job 1 -> Job 3 -> Job 4`。

#### 3.6.4 WebUI v2 残余 P2 项收口
1. **消除双写竞态（[`webui/app.js:126-143`](../../webui/app.js#L126-L143)）**：
   将 `ownedSections()` 从 `return ["profile", "queue", "activeJob", "ui"];` 修改为 `return ["profile", "ui"];`，确保前端 `scheduleSave()` 仅同步 `profile` 与 `ui` 偏好，而 `queue` 与 `activeJob` 的状态迁移 100% 由后端 `/api/job` 原子控制。
2. **清理仓库缓存污染与更新文档**：
   - 删除 `webui/__pycache__` 目录并在 `.gitignore` 中加固 `__pycache__/` 与 `*.pyc`；
   - 更新 [`webui/README.md`](../../webui/README.md)，将旧版 `113 项词条` 与 `start|cancel` 二元接口说明更新为 Phase 10 的 **20 参数契约、129 项 `TrKey` 词条、7 动作队列 REST API（`enqueue | remove | clear_completed | retry | move | start | cancel`）与 CPU/GPU 双轨编码说明**。

---

## 4. 终局封板硬门禁核验清单（SuperPower Final Acceptance Checklists）

工程团队完成上述 `FS-P10-01 ~ FS-P10-05` 施工后，必须逐一通过以下 **8 道硬性自动化门禁（`Gate FS-10.1 ~ Gate FS-10.8`）** 并附上真实构建与测试输出日志，方可再次提交封板申请：

- [ ] **Gate FS-10.1（20 参数 SSOT 契约三端零漂移门禁）**：
  - `include/rtx/param_schema.h` 中 `kParamSchema.size() == 20`；
  - `validateAndNormalizeProfile()` 对 WebUI v2 默认 JSON（`"rateControl": "cq"`, `"qp": 22`, `"pipelineOrder": "vsr-first"`）及 `"codec": "av1_cpu" | "hevc_cpu"`、`"rateControl": "crf"` 100% 返回 `Ok()`；
  - `ctest --test-dir .vs/build -C Release -R "rtx_i18n_test|rtx_gui_workstation_test" --output-on-failure` 100% 通过。
- [ ] **Gate FS-10.2（PTX 体积与算子符号硬门禁）**：
  - `(Get-Item src/shim/ngx_bridge/p010_pack.ptx).Length -le 180000`（实测须 `<= 180 KB`，严禁 `1,320,609 B` 旧文件残留）；
  - `Select-String -Path src/shim/ngx_bridge/rife_pack.ptx -Pattern "rtx_eval_block_sad_8x8|rtx_p010_guided_interp"` 匹配成功；
  - `src/shim/ngx_bridge/p010_pack_runtime.cpp` 中不再包含 `kNvofWarpPtx` 内联字符串；
  - `dumpbin /exports .vs/build/Release/rtx_ngx_bridge.dll` 导出符号数严格等于 **10**。
- [ ] **Gate FS-10.3（代码行数与模块化复杂度硬门禁）**：
  - 扫描 `src/` 与 `include/` 下所有 C++ 函数体，`src/core/hw_transcode.cpp` 中任意单个函数体行数严格 **`<= 120 行`**，单个源文件行数 **`<= 800 行`**（可将 `RemasterPipeline` ステージ拆分为 `src/core/hw_transcode_stages.cpp`）。
- [ ] **Gate FS-10.4（Track 10B 双轨编码与 HDR10+ 零泄漏门禁）**：
  - `src/core/hw_transcode.cpp` 中 `options_.rate_control` 支持 `0..3`（NVENC `CQ` 真实生效）及 `use_cpu_encoder` 分支（调用 `CpuVideoEncoder`）；
  - `src/io/cpu_video_encoder.cpp` 每帧调用 `av_frame_remove_side_data` 清理旧元数据并真实挂载 `AV_FRAME_DATA_DYNAMIC_HDR_PLUS`；
  - `rtx_cpu_encoder_test` 全绿通过。
- [ ] **Gate FS-10.5（Track 10C NGX 时域重置与 `vsr-first` 管线实装门禁）**：
  - `src/shim/ngx_bridge/ngx_d3d12_bridge.cpp` 中 `reset_temporal` 真实调用 `ngx_.set_int(vsr_params_, NVSDK_NGX_Parameter_Reset, reset_temporal ? 1 : 0)`；
  - `src/shim/ngx_bridge/rife_engine.cpp` 与 `src/core/hw_transcode.cpp` 真实调用 `ShotGroupSegmenter::evaluateTransition`；
  - `options_.vsr_before_fg == true` 时真实执行 `Decode -> VSR (4K P010 Anchor Ring) -> FG -> Encode`；
  - `rtx_shot_segmenter_test` 全绿通过。
- [ ] **Gate FS-10.6（Track 10D Win32 GUI `EncoderJobQueue` 真实接线门禁）**：
  - `src/gui/rtx_gui.cpp` 中彻底删除 `batchQueue`，所有队列按钮与转码生命周期 100% 通过 `g_app.jobQueue` 驱动；
  - `tests/test_gui_workstation.cpp` 中包含 `EncoderJobQueue` 异构 Profile 快照隔离与运行期动态修改单测且 100% 通过。
- [ ] **Gate FS-10.7（WebUI v2 <-> `CineRTX.exe --session-stdio` 端到端联调门禁）**：
  - `webui/__pycache__` 已从仓库物理清除，`webui/app.js` 的 `ownedSections()` 已移除 `"queue"`，`webui/README.md` 已同步至 v2；
  - 启动 `python webui/server.py` 并通过 `POST /api/job {"action": "enqueue"}` + `{"action": "start"}` 驱动真实 `CineRTX.exe --session-stdio` 完成至少 1 次 GPU (`av1` + `cq`) 与 1 次 CPU (`av1_cpu` + `crf`) 端到端转码，零 `InvalidArgument` 报错。
- [ ] **Gate FS-10.8（全量 CTest 回归零妥协门禁）**：
  - 在 `.vs/build` 下执行 `ctest -C Release --output-on-failure`，全量测试套件 100% PASS，零跳过、零伪造物证。
