# CineRTX Phase 9 终审裁决：驳回伪 RIFE 合成降级与全链路真权重闭环 SuperPower 返工施工规范

- **文档编号**：`CINERTX-P9-VERDICT-REWORK-SUPERPOWER-002`
- **裁决日期**：2026-11-15
- **签发角色**：首席系统架构师与 GPU 深度学习推理领域专家（Principal Systems Architect & Domain Expert）
- **审阅标的**：
  - 终审验收报告：`docs/phase8/CineRTX_Phase9_终审验收报告_RIFE120fps全链路根治.md`（`CINERTX-P9-ACCEPT-001`）
  - 施工分发总览：`docs/phase8/CineRTX_Phase9_施工分发总览_RIFE120fps全链路根治.md`（`CINERTX-P9-DISPATCH-MASTER-001`）
  - 四轨施工单：`Track 9A`、`Track 9B`、`Track 9C`、`Track 9D`
  - 前置总纲规范：`docs/phase8/CineRTX Phase 9：RIFE 120fps 插帧 1比5 重复帧根因穿透取证与全链路根治施工规范.md`（`CINERTX-P9-RIFE-120FPS-FORENSICS-001`）
  - 实机复现委托书：[`docs/phase8/CineRTX Phase 8：关于 RIFE 120fps 插帧 1比5 重复帧实机复现、根因取证与专家协助委托书.md`](../phase8/CineRTX Phase 8：关于 RIFE 120fps 插帧 1比5 重复帧实机复现、根因取证与专家协助委托书.md)
- **终审裁决结论**：🔴 **REJECT / CONDITIONAL REWORK REQUIRED（不予签发战略封板，责令按本 SuperPower 规范执行 Phase 9.1 靶向返工）**

---

## 第一部分：领域专家穿透式终审裁决（Audit Verdict & Forensic Findings）

本人已对最新同步至云端的 Phase 9 全部呈报文档与 13 份核心 C++26 / CUDA / PTX 源码（`rife_pack.ptx`、`rife_pack.cu`、`rife_pack_runtime.cpp`、`rife_engine.h`、`rife_engine.cpp`、`rife_builder.cpp`、`bridge_exports.cpp`、`hw_transcode.h`、`hw_transcode.cpp`、`ffmpeg_io.cpp`、`main.cpp`、`test_rife.cpp`）进行了逐行指令级穿透审计。

### 1.1 予以肯定的工程进展（已达标并冻结项）

工程团队在本次施工中解决了多项底层硬核顽疾，以下成果经核验属实，予以冻结保留：
1. **Zig (Itanium ABI) 与 `nvinfer_10.dll` (MSVC ABI) 虚表错位根因侦破**：精准定位了 Zig `c++` 单析构槽虚表与 MSVC 双析构槽虚表在 `INetworkDefinition::addInput` / `IOptimizationProfile` 上的 8 字节偏移崩溃，并在 RTX 5060 实机恢复了纯正的 **MSVC (`cl.exe` 19.51) + Ninja** 编译闭环。
2. **Track 9A PTX ISA 全架构兼容与上下文复用**：[`src/shim/ngx_bridge/rife_pack.ptx`](../../src/shim/ngx_bridge/rife_pack.ptx) 已降级为 `.version 7.0` + `.target sm_70`，全面消除十进制浮点立即数，修复了 Warp 级 `shfl.sync` 越界分支发散；[`src/shim/ngx_bridge/rife_pack_runtime.cpp`](../../src/shim/ngx_bridge/rife_pack_runtime.cpp)（第 128–175、224–228 行）实现了已有活跃 `CUcontext` 优先复用与 `isRifePackAvailable()` 前置校验。
3. **Track 9B/9C/9D 基础框架就位**：实现了 FP32 阶段帧对缓存（`can_reuse_pair` + `cuMemsetD32Async`）、$\alpha$ 敏感度探针 `runAlphaSensitivityProbe()`、`< 512 KiB` 脏占位引擎拦截 `isDirtyEnginePlaceholder()`、`GridSample` 坐标流算子 FP32 锁精度，以及容器层 `output_info_.frame_rate` 与 `stream_->r_frame_rate = 120/1` 闭环。

---

### 1.2 驳回封板的 5 大代码级与合规性铁证（RW-P9-01 ～ RW-P9-05）

尽管基础管线代码合入，但经穿透式审查发现，**当前交付在核心实机验收中采用了“合成占位引擎 + NVOFA 光流静默覆写”的假绿方案，且在 FP16 张量支持与脱敏红线上存在 3 项 P0 阻断缺陷与 2 项 P1/P2 缺陷**：

| 缺陷编号 | 严重度 | 缺陷定性与问题核心 | 源码 / 文档铁证落点 (`File:Line`) | 物理后果与阻断原因 |
| :--- | :---: | :--- | :--- | :--- |
| **RW-P9-01** | 🔴 **P0 阻断** | **实机未部署真 RIFE v4.25 权重，且 `bridge_exports.cpp` 用 NVOFA 覆写了 RIFE 输出（伪 RIFE 验收）** | `bridge_exports.cpp:1309-1339`<br>`rife_engine.cpp:237-321`<br>`终审验收报告:25, 87, 135` | 1. 实机未部署 `rife_v4.25.onnx` / `rife_v4.25.engine`；用户在正常生产环境（不设内部测试变量 `CINERTX_ALLOW_BUILTIN_RIFE_SYNTH=1`）下开启 RIFE 120fps 会 **100% 报错退出**（`No valid RIFE engine (.engine) or ONNX model found`）；<br>2. 在 `bridge_exports.cpp:1309-1333` 中，当 `isBuiltinSynth()` 为真时，第 1309 行刚跑完 `rife_engine->interpolate(..., out_nv12)`，第 1329 行紧接着调用 `launchNvofWarpNv12(prev_nv12, curr_nv12, ..., out_nv12)` **将 `out_nv12` 全量覆写为 Route A 的 NVOFA 光流输出**！验收报告中的 `p9_e2e_120.mp4` 实为被 NVOFA 覆写后的伪 RIFE 产物！ |
| **RW-P9-02** | 🔴 **P0 阻断** | **FP16 (`kHALF`) 输入/输出张量引擎在帧对缓存与后处理中断链** | `rife_engine.cpp:601-608`<br>`rife_engine.cpp:747-755`<br>`rife_engine.cpp:824-848` | 1. **帧对缓存漏更新 `kHALF`**：第 749 行 `if (impl_->in_dtype == DataType::kFLOAT && g_cuda.memset_d32_async)` 在 `in_dtype == kHALF` 时直接跳过且无 `else` 分支，导致 $\alpha \in \{0.4, 0.6, 0.8\}$ 的第 6 通道永远卡死在 $\alpha = 0.2$（产生 **1:4 重复帧**）；<br>2. **后处理漏调 `launchFp16ToFp32`**：第 602 行分配了 `d_output_fp32_scratch`，但第 835 行后处理直接将 FP16 的 `d_output_tensor` 传给只读 `float*` 的 `rife_nchw_3ch_fp32_to_nv12`，导致半精度数据被按 FP32 解释引发画面崩坏与显存越界读！ |
| **RW-P9-03** | 🔴 **P0 阻断** | **验收报告明文泄露 5 处未掩码本机路径 `[REDACTED_HOST]` & 未建 `docs/phase9/` 目录** | `终审验收报告:22, 30, 76, 87`<br>[`docs/README.md`](../README.md) | 1. 验收报告出现 5 处 `[REDACTED_HOST]` 未脱敏本地路径，直接违反 `施工分发总览` DoD 第 4 条与 Gate 3.6 红线；<br>2. Phase 9 全部文档被混放于 `docs/phase8/` 下，未归入 `docs/phase9/`，且 `docs/README.md` 未同步 Phase 9 索引。 |
| **RW-P9-04** | 🟠 **P1 严重** | **`hw_transcode.cpp` 静态日志计数器跨会话死锁 & 100% 插帧失败时仍返回 `ExitCode 0`** | `hw_transcode.cpp:1042-1053`<br>`hw_transcode.cpp:1280-1315` | 1. `static thread_local int s_fruc_error_logs = 0;` 在 GUI/WebUI 常驻进程中跑完首个失败任务后永久失聪，后续任务不再打印 `[FRUC-ERROR]`；<br>2. 当 `rife_active` 且全片插帧 100% 失败（`fruc_failures > 0 && frames_interpolated == 0`）时，转码仍以 `RTX_OK` (`ExitCode 0`) 结束，未兑现总纲 DoD #4“全败严禁返回 0”。 |
| **RW-P9-05** | 🟡 **P2 瑕疵** | **单指针 PTX 核函数忽略 `full_range` 标志，Full-Range 视频源暗部/亮部被截断** | `rife_pack.ptx:14, 204`<br>`rife_pack_runtime.cpp:324` | `rife_nv12_to_nchw_7ch_fp32` 与 `rife_nchw_3ch_fp32_to_nv12` 硬编码 Limited-Range `[16..235]` 系数，`launchRifePostprocess` 中 `(void)launch;` 丢弃了 `full_range`。 |

---

## 第二部分：Plan（Phase 9.1 顶层返工规划与架构铁律）

### 2.1 返工核心目标
1. **彻底斩断 Route B 对 Route A 的偷梁换柱**：从 [`src/shim/ngx_bridge/bridge_exports.cpp`](../../src/shim/ngx_bridge/bridge_exports.cpp) 中物理删除 `isBuiltinSynth()` 触发的 `launchNvofWarpNv12` 覆写代码。Route B（`rtx_ngx_bridge_interpolate_rife`）必须 100% 纯粹由 TensorRT `RifeEngine::interpolate` 驱动。
2. **真权重（Real RIFE v4.25 ONNX $\to$ `.engine`）部署与生产环境开箱即用**：
   - 针对用户此前询问的“是否有合适的满血版预训练 ONNX 可供下载”，本规范在 `tools/fetch_rife_onnx.py` 中直接固化经过社区工业验证的 **Practical-RIFE v4.25 / v4.6（7 通道输入 `input [1, 7, H, W]` $\to$ 3 通道输出 `output [1, 3, H, W]`，FP32 权重，Opset 16/17）预训练 ONNX 官方发布源与构建流水线**；
   - 若实机受内网隔离无法直连外网，提供基于 Python + PyTorch/ONNX 离线导出标准 **IFNet 多尺度金字塔 + `GridSample` 光流变形架构（`>= 5 MB`）** 或将下载好的官方预训练 `rife_v4.25.onnx` 放入 `models/rife_v4.25.onnx`，通过实机 MSVC 版 `CineRTX.exe --build-rife-engine --onnx models/rife_v4.25.onnx --engine models/rife_v4.25.engine` 编译出 **体积 $\ge 5\text{ MB}$ 的真实 `models/rife_v4.25.engine`**；
   - 严禁在终审 E2E 验收中使用 `CINERTX_ALLOW_BUILTIN_RIFE_SYNTH=1`！实机 E2E 必须在**零测试环境变量**的纯生产命令下直接加载 `models/rife_v4.25.engine` 跑通 24fps $\to$ 120fps！
3. **补齐 `kHALF` (FP16) 输入/输出张量全链路转换闭环**：修复 `can_reuse_pair`、`runAlphaSensitivityProbe` 与 `interpolate` 后处理中的 FP16 转换断链。
4. **修复会话级错误计数器与全败非零退出码，清零文档路径泄露并归档至 `docs/phase9/`**。

### 2.2 返工轨道与文件所有权矩阵（File Ownership Matrix）

| 返工轨 | 责任模块与核心文件 | 核心任务 | 严禁触碰边界 |
| :--- | :--- | :--- | :--- |
| **Track 9.1-A**<br>（真模型部署与桥接层去伪） | - [`src/shim/ngx_bridge/bridge_exports.cpp`](../../src/shim/ngx_bridge/bridge_exports.cpp)<br>- 新增 `tools/fetch_rife_onnx.py`<br>- 实机生成 `models/rife_v4.25.onnx` & `models/rife_v4.25.engine` | 1. 拔除 `bridge_exports.cpp:1315-1339` NVOFA 覆写；<br>2. 拉取/部署真实 7 通道 RIFE v4.25 ONNX 并用 MSVC `CineRTX.exe` 编译出 `models/rife_v4.25.engine` ($\ge 5\text{ MB}$)。 | 严禁修改 `src/gui/`、`webui/` |
| **Track 9.1-B**<br>（TRT 引擎 FP16 闭环与 PTX Full-Range） | - [`src/shim/ngx_bridge/rife_engine.cpp`](../../src/shim/ngx_bridge/rife_engine.cpp)<br>- [`src/shim/ngx_bridge/rife_pack.ptx`](../../src/shim/ngx_bridge/rife_pack.ptx)<br>- [`src/shim/ngx_bridge/rife_pack.cu`](../../src/shim/ngx_bridge/rife_pack.cu)<br>- [`src/shim/ngx_bridge/rife_pack_runtime.cpp`](../../src/shim/ngx_bridge/rife_pack_runtime.cpp) | 1. 修复 `can_reuse_pair` 在 `in_dtype == kHALF` 时漏更新第 6 通道；<br>2. 修复 `interpolate()` 在 `out_dtype == kHALF` 时漏调 `launchFp16ToFp32`；<br>3. 修复 `runAlphaSensitivityProbe` 对 `kHALF` 引擎的兼容。 | 严禁恢复 `.target sm_89` 或十进制浮点字面量 |
| **Track 9.1-C**<br>（调度防线闭环与单测强化） | - [`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp)<br>- [`tests/test_rife.cpp`](../../tests/test_rife.cpp) | 1. 将 `s_fruc_error_logs` 改为会话局部变量；<br>2. 当 `rife_active && fruc_failures > 0 && frames_interpolated == 0` 时返回 `ErrorCode::Internal`（拒绝 `ExitCode 0` 假成功）；<br>3. 单测新增 `bridge_exports` 级与 `kHALF` 帧对缓存测试。 | 严禁吞没错误码 |
| **Track 9.1-D**<br>（文档脱敏、目录归位与实机真权重 E2E） | - 迁移 Phase 9 文档至 `docs/phase9/`<br>- 更新 [`docs/README.md`](../README.md)<br>- 重出无污染终审报告与委托书 | 1. 清除 `[REDACTED_HOST]` 全部 5 处未脱敏路径；<br>2. 将 Phase 9 系列文档统一归入 `docs/phase9/` 并更新 `docs/README.md`；<br>3. 在零环境变量下用真 `models/rife_v4.25.engine` 跑通 120fps E2E 并出具 MAE 报告。 | 严禁出现 `[REDACTED_HOST]`、`[REDACTED_HOST]` 等私有路径 |

---

## 第三部分：Tasks（原子级 TDD 返工施工任务集）

### Task 9.1-A1：物理拔除 `bridge_exports.cpp` 中用 NVOFA 覆写 RIFE 输出的作弊分支（对应 RW-P9-01）

- **目标文件**：[`src/shim/ngx_bridge/bridge_exports.cpp`](../../src/shim/ngx_bridge/bridge_exports.cpp)（第 1308–1345 行）
- **施工动作**：将 `rtx_ngx_bridge_interpolate_rife` 末尾的 `if (rife_res == RTX_OK && local_scene_cut == 0 && impl->rife_engine->isBuiltinSynth()) { ... }` 整段删除，确保该函数只执行 `impl->rife_engine->interpolate` 并在流上同步：

```cpp
    int32_t local_scene_cut = 0;
    const int32_t rife_res = impl->rife_engine->interpolate(
        prev_nv12, curr_nv12, pitch, alpha, out_nv12, full_range, &local_scene_cut,
        impl->interop->pack_stream());
    if (scene_cut) {
        *scene_cut = local_scene_cut;
    }
    if (rife_res == RTX_OK) {
        std::string sync_err;
        (void)impl->interop->stream_synchronize(impl->interop->pack_stream(), &sync_err);
    } else {
        impl->error = "rtx_ngx_bridge_interpolate_rife: TensorRT RIFE inference failed";
    }
    return rife_res;
```

---

### Task 9.1-A2：获取并部署满血版预训练 RIFE v4.25 ONNX 模型与构建真 `.engine`（对应 RW-P9-01 & 用户问题 #1）

> **关于满血版预训练 RIFE ONNX 的权威解答与工程规范**：
> **完全不需要自行从头训练！** 业界最成熟、与 TensorRT 10.x 兼容性最好的官方开源预训练模型是 **Practical-RIFE v4.25 / v4.26 (`IFNet_HDv3`)** 的 `vs-mlrt` 生产级导出权重（输入为单张量 7 通道 `input: [1, 7, H, W]`，其中通道 `0..2` 为 $I_0$ RGB、通道 `3..5` 为 $I_1$ RGB、通道 `6` 为任意时刻相位平面 $\alpha \in (0,1)$；输出为 `output: [1, 3, H, W]`）。

- **新增工具脚本**：`tools/fetch_rife_onnx.py`
  脚本负责自动从开源发布源（HuggingFace / GitHub Releases 镜像）下载标准 7 通道 `rife_v4.25.onnx`（或将本地已缓存的 v4.25 权重重整为 `[1, 7, H, W]` 单输入契约）并落盘至 `models/rife_v4.25.onnx`（且同步拷贝至 `.vs/build/cmake/windows-msvc-ninja/models/rife_v4.25.onnx`），随后调用 MSVC 编译的 `CineRTX.exe --build-rife-engine` 生成正式 `models/rife_v4.25.engine`：

```python
#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
"""Fetch or stage the canonical 7-channel Practical-RIFE v4.25 ONNX model and build TensorRT .engine."""

from __future__ import annotations
import argparse
import hashlib
import shutil
import subprocess
import sys
import urllib.request
from pathlib import Path

# Canonical public mirrors for 7-channel RIFE v4.25 (input: [1,7,H,W] fp32, output: [1,3,H,W] fp32)
CANDIDATE_URLS = [
    "https://huggingface.co/datasets/styler00dollar/models/resolve/main/rife/rife425_v2_fp32_opset17.onnx",
    "https://github.com/styler00dollar/VSGAN-tensorrt-docker/releases/download/models/rife425_v2_fp32_opset17.onnx",
]

MIN_VALID_ONNX_BYTES = 1 * 1024 * 1024      # >= 1 MiB
MIN_VALID_ENGINE_BYTES = 512 * 1024          # >= 512 KiB (real engines are ~8-35 MiB)


def download_onnx(dest: Path) -> bool:
    dest.parent.mkdir(parents=True, exist_ok=True)
    for url in CANDIDATE_URLS:
        try:
            print(f"[fetch_rife_onnx] Trying {url} ...")
            req = urllib.request.Request(url, headers={"User-Agent": "CineRTX-ModelFetcher/1.0"})
            with urllib.request.urlopen(req, timeout=60) as resp, open(dest, "wb") as out:
                shutil.copyfileobj(resp, out)
            if dest.stat().st_size >= MIN_VALID_ONNX_BYTES:
                print(f"[fetch_rife_onnx] Downloaded {dest} ({dest.stat().st_size} bytes)")
                return True
        except Exception as exc:
            print(f"[fetch_rife_onnx] Mirror failed: {exc}")
    return False


def main() -> int:
    parser = argparse.ArgumentParser(description="Stage RIFE v4.25 ONNX and build TensorRT engine.")
    parser.add_argument("--onnx", type=Path, default=Path("models/rife_v4.25.onnx"))
    parser.add_argument("--engine", type=Path, default=Path("models/rife_v4.25.engine"))
    parser.add_argument("--cinertx-exe", type=Path,
                        default=Path(".vs/build/cmake/windows-msvc-ninja/CineRTX.exe"))
    args = parser.parse_args()

    if not args.onnx.exists() or args.onnx.stat().st_size < MIN_VALID_ONNX_BYTES:
        if not download_onnx(args.onnx):
            print("[fetch_rife_onnx] ERROR: Could not download rife_v4.25.onnx from public mirrors.",
                  file=sys.stderr)
            return 1

    if not args.cinertx_exe.exists():
        print(f"[fetch_rife_onnx] ERROR: CineRTX binary not found at {args.cinertx_exe}", file=sys.stderr)
        return 2

    args.engine.parent.mkdir(parents=True, exist_ok=True)
    cmd = [
        str(args.cinertx_exe),
        "--build-rife-engine",
        "--onnx", str(args.onnx),
        "--engine", str(args.engine),
    ]
    print("[fetch_rife_onnx] Building TensorRT engine:", " ".join(cmd))
    res = subprocess.run(cmd, check=False)
    if res.returncode != 0 or not args.engine.exists() or args.engine.stat().st_size < MIN_VALID_ENGINE_BYTES:
        print("[fetch_rife_onnx] ERROR: Engine build failed or produced invalid file.", file=sys.stderr)
        return 3

    # Mirror into build tree models/ so relative lookup from build dir works out-of-the-box
    build_models = args.cinertx_exe.parent / "models"
    build_models.mkdir(parents=True, exist_ok=True)
    shutil.copy2(args.engine, build_models / "rife_v4.25.engine")
    print(f"[fetch_rife_onnx] SUCCESS: {args.engine} ({args.engine.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

> **注意**：若外部下载得到的 RIFE v4.25 ONNX 为 8 通道输入（部分 `vs-mlrt` v2 导出把 `timestep` 拆成或带有额外对齐通道，或双输入 `I0, I1, timestep`），`rife_engine.cpp:477-524` 已内置 `SingleTensor7Ch` 与 `ThreeTensors` 双变体自动识别逻辑；若模型输入名或通道为 `[1, 7, H, W]`，将直接走 `SingleTensor7Ch` 零拷贝路径。

---

### Task 9.1-B1：修复 `rife_engine.cpp` 中 `kHALF` (FP16) 输入/输出张量的帧对缓存、探针与后处理断链（对应 RW-P9-02）

- **目标文件**：[`src/shim/ngx_bridge/rife_engine.cpp`](../../src/shim/ngx_bridge/rife_engine.cpp)
- **修改点 1（第 747–756 行，`can_reuse_pair` 分支补齐 `kHALF` 通道更新）**：
  当 `impl_->in_dtype == nvinfer1::DataType::kHALF` 时，不能跳过第 6 通道更新！必须先用 `memset_d32_async` 更新 `d_input_fp32_scratch` 的第 6 通道，再仅对该通道调用 `launchFp32ToFp16` 同步至 `d_input_tensor` 的第 6 通道：

```cpp
    if (can_reuse_pair) {
        const size_t plane_pixels = static_cast<size_t>(padded_width_) * static_cast<size_t>(padded_height_);
        const uint32_t alpha_bits = std::bit_cast<uint32_t>(alpha);
        if (impl_->in_dtype == nvinfer1::DataType::kFLOAT && g_cuda.memset_d32_async) {
            const uint64_t alpha_plane_ptr = impl_->d_input_tensor + 6ULL * plane_pixels * sizeof(float);
            g_cuda.memset_d32_async(alpha_plane_ptr, alpha_bits, plane_pixels, stream);
        } else if (impl_->in_dtype == nvinfer1::DataType::kHALF &&
                   impl_->d_input_fp32_scratch != 0 && g_cuda.memset_d32_async) {
            const uint64_t fp32_alpha_ptr = impl_->d_input_fp32_scratch + 6ULL * plane_pixels * sizeof(float);
            const uint64_t fp16_alpha_ptr = impl_->d_input_tensor + 6ULL * plane_pixels * sizeof(uint16_t);
            g_cuda.memset_d32_async(fp32_alpha_ptr, alpha_bits, plane_pixels, stream);
            const int32_t cvt_res = launchFp32ToFp16(fp32_alpha_ptr, fp16_alpha_ptr, plane_pixels, stream);
            if (cvt_res != RTX_OK) return cvt_res;
        }
        effective_scene_cut = impl_->last_scene_cut;
    }
```

- **修改点 2（第 834–848 行，推理完成后补齐 `kHALF -> kFLOAT` 输出张量转换）**：
  当 `impl_->out_dtype == nvinfer1::DataType::kHALF` 时，必须先调用 `launchFp16ToFp32` 将 `d_output_tensor` 转换为 `d_output_fp32_scratch`，再传给 `launchRifePostprocess`：

```cpp
    uint64_t post_rgb_tensor = impl_->d_output_tensor;
    if (impl_->out_dtype == nvinfer1::DataType::kHALF) {
        const size_t out_elems = 3ULL * static_cast<size_t>(padded_width_) * static_cast<size_t>(padded_height_);
        const int32_t cvt_res = launchFp16ToFp32(
            impl_->d_output_tensor, impl_->d_output_fp32_scratch, out_elems, stream);
        if (cvt_res != RTX_OK) {
            return cvt_res;
        }
        post_rgb_tensor = impl_->d_output_fp32_scratch;
    }

    // Launch postprocessing kernel
    const RifePostprocessLaunch postLaunch{
        .rgb_tensor = post_rgb_tensor,
        .y_plane = out_nv12,
        .uv_plane = 0,
        .width = width_,
        .height = height_,
        .y_pitch = pitch,
        .uv_pitch = pitch,
        .padded_width = padded_width_,
        .padded_height = padded_height_,
        .full_range = full_range,
    };

    return launchRifePostprocess(postLaunch, stream);
```

- **修改点 3（第 136–205 行，`runAlphaSensitivityProbe` 适配 `kHALF` 输入/输出）**：
  在 `runAlphaSensitivityProbe` 中，若 `in_dtype == kHALF` 则先填 FP32 临时缓冲并调 `launchFp32ToFp16`；若 `out_dtype == kHALF` 则先调 `launchFp16ToFp32` 再 `memcpy_dtoh` 回主机比对，避免对半精度模型误报探针失败或越界访问。

---

### Task 9.1-C1：修复 `hw_transcode.cpp` 跨会话静态计数器死锁与全败退出码防线（对应 RW-P9-04）

- **目标文件**：[`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp)
- **修改点 1（第 955 行 & 第 1042 行）**：
  在 `runNvidiaRemasterTranscode` 栈帧（约第 955 行 `bool has_prev_frame = false;` 旁）声明会话级局部变量：
  ```cpp
  int fruc_error_logs = 0;
  ```
  并将第 1042 行的 `static thread_local int s_fruc_error_logs = 0;` 删除，直接读写会话变量 `fruc_error_logs`，确保每次转码会话独立拥有 5 条错误日志配额。
- **修改点 2（收尾处全败熔断检查）**：
  在 `runNvidiaRemasterTranscode` 结束主循环与 `finishPipeline()` 之后、`finalize()` 之前，增加对“请求了 RIFE 但插帧 100% 失败”的硬性阻断判定（满足总纲 DoD #4）：
  ```cpp
  if (rife_active && report.fruc_failures > 0 && report.frames_interpolated == 0) {
      return err(ErrorCode::Internal,
                 std::format("RIFE frame interpolation failed on all {} intermediate frame(s)",
                             report.fruc_failures),
                 session.bridgeLoader().lastError(session.bridge()));
  }
  ```

---

### Task 9.1-C2：扩充 `tests/test_rife.cpp` 单测覆盖 `rtx_ngx_bridge_interpolate_rife` 与 FP16 帧对缓存（对应 RW-P9-01 & RW-P9-02）

- **目标文件**：[`tests/test_rife.cpp`](../../tests/test_rife.cpp)
- **施工动作**：
  1. 在 `rife_four_phase_monotonic_progression` 中，同时验证通过桥接层 C ABI `rtx_ngx_bridge_interpolate_rife` 调用时，不会触发 `NvofEngine` 覆写，且 4 个相位 $\alpha \in \{0.2, 0.4, 0.6, 0.8\}$ 的输出像素质心或亮度严格单调演进；
  2. 新增 `rife_fp16_roundtrip_and_cache_progression` 用例（或在现有用例中验证 `launchFp32ToFp16` + `launchFp16ToFp32` 通道偏移转换），确保 `75/75` 单测 100% 绿灯。

---

### Task 9.1-D1：文档路径脱敏清零、`docs/phase9/` 规范建档与真权重实机 E2E 闭环（对应 RW-P9-03）

- **目标文件与目录**：
  1. 将 `docs/phase8/` 下所有属于 Phase 9 的文档（施工分发总览、Track 9A–9D 施工单、Phase 9 终审验收报告及专家委托书）正式迁移/归档至 **`docs/phase9/`** 目录下；
  2. 更新 [`docs/README.md`](../README.md)，新增“Phase 9 权威定稿文档 (`docs/phase9/`)”章节索引，并将单测数量更新为最新实测值（`75 tests`）；
  3. 对全仓 `docs/` 执行正则扫描：
     ```powershell
     Select-String -Path "docs\**\*.md","README*.md" -Pattern "C:\\PUBLIC|C:\\Users\\|192\.168\."
     ```
     确保命中数严格为 **`0`**（将 `CineRTX_Phase9_终审验收报告_RIFE120fps全链路根治.md` 第 22、30、76、87 行的 `[REDACTED_HOST]` 全部替换为仓库相对路径 `.vs\build\cmake\windows-msvc-ninja` 与 `.vs\gemini_generated_video_6eaef4a7.mp4`）。

---

## 第四部分：Checklists（Phase 9.1 终审复验 6 道硬性门禁）

执行工程师与项目负责人完成上述返工后，必须逐项通过以下 6 道门禁，并在 `docs/phase9/CineRTX_Phase9_RIFE120fps全链路根治终审验收与战略封板专家委托书.md` 中附上原始命令行输出与物证：

- [ ] **Gate 9.1（零 NVOFA 覆写静态审计）**：
  审查 [`src/shim/ngx_bridge/bridge_exports.cpp`](../../src/shim/ngx_bridge/bridge_exports.cpp) 中的 `rtx_ngx_bridge_interpolate_rife` 函数，确认其中 **`0` 处引用 `nvof_engine` 或 `launchNvofWarpNv12`**。
- [ ] **Gate 9.2（真实 RIFE v4.25 模型权重与引擎体积门禁）**：
  实机 `models/rife_v4.25.onnx` 与编译产出的 `models/rife_v4.25.engine` 物理存在，且 `.engine` 文件大小 **$\ge 5,242,880\text{ bytes}$ ($5\text{ MiB}$)**，包含真实的 `GridSample` FP32 锁精度层日志（`locked N coordinate/elementwise layer(s) to FP32`，且 $N \ge 10$）。
- [ ] **Gate 9.3（生产模式零测试环境变量 120fps E2E 闭环）**：
  在**未设置 `CINERTX_ALLOW_BUILTIN_RIFE_SYNTH`** 的纯净终端下运行：
  ```powershell
  .\.vs\build\cmake\windows-msvc-ninja\CineRTX.exe --input .\.vs\gemini_generated_video_6eaef4a7.mp4 --output .\.vs\build\p9_real_rife_120fps.mp4 --target-fps 120 --rife-engine models\rife_v4.25.engine --frames 30 --json
  ```
  - 断言退出码 `0`；
  - 断言 JSON 输出 `"fruc_backend": "rife-trt"`、`"frames_interpolated": 116`（或 `> 0`）、`"fruc_failures": 0`；
  - 断言 `ffprobe` 输出 `r_frame_rate = "120/1"` 且 `avg_frame_rate = "120/1"`。
- [ ] **Gate 9.4（真实 RIFE v4.25 逐帧 MAE 平滑演进物证）**：
  对 `p9_real_rife_120fps.mp4` 前 35 帧提取 `rgb24` 相邻帧 MAE，验证不存在连续 4 帧 $\text{MAE} < 0.5$ 的“1:5 重复帧”阶跃。
- [ ] **Gate 9.5（MSVC 零告警编译与全量单测门禁）**：
  实机 MSVC `cl.exe` + Ninja 编译 `0 error, 0 warning`；`ctest --output-on-failure` 全量 **75/75 单测 100% PASS**。
- [ ] **Gate 9.6（全仓脱敏与 `docs/phase9/` 归档门禁）**：
  全仓 `docs/` 与根级 `README*.md` 扫描 `[REDACTED_HOST]`、`[REDACTED_HOST]`、`内网 IP 网段字面量` 命中数严格为 **`0`**；Phase 9 全部文档归档于 `docs/phase9/`。
