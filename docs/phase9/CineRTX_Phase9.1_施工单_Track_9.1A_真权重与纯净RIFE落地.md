# CineRTX Phase 9.1 施工单：Track 9.1-A 真权重与纯净 RIFE 落地

> **工单编号**: `CINERTX-P9.1-TRACK-A`  
> **任务名称**: `真权重与纯净 RIFE 落地（物理拔除 NVOFA 偷梁换柱与自动化部署官方 RIFE 权重）`  
> **责任小组**: `底层推理与模型工程组`  
> **主导依据**: [`docs/phase9/CineRTX_Phase9_终审裁决与全链路真权重闭环SuperPower返工规范.md`](docs/phase9/CineRTX_Phase9_终审裁决与全链路真权重闭环SuperPower返工规范.md)  
> **关联缺陷**: 🔴 **RW-P9-01 (P0 阻断)**  
> **核心目标**: 彻底杜绝伪 RIFE 验收，100% 走真正的 RIFE 神经网络推理，生成 $\ge 5\text{ MiB}$ 的真 TensorRT 引擎

---

## 一、 被拒缺陷机理穿透 (RW-P9-01)

### 1. 缺陷本质与现场铁证
在 Phase 9 终审验收中，领域专家通过源码静态审计发现：
在 [`src/shim/ngx_bridge/bridge_exports.cpp`](src/shim/ngx_bridge/bridge_exports.cpp#L1315-L1339) 的 `rtx_ngx_bridge_interpolate_rife` 函数中，存在极其严重的逻辑欺骗：
```cpp
// 历史被拒作弊代码 (bridge_exports.cpp 第 1324-1335 行)：
if (rife_engine_->isBuiltinSynth()) {
    // ... 当检测到是测试合成模型时，居然调用了 NVOFA 的光流扭曲覆盖了输出！
    const int32_t warp_res = launchNvofWarpNv12(..., out_nv12, ...);
    // 此处 out_nv12 被完全覆写为 Route A (NVOFA) 的产物！
}
```
**严重危害**：
1. 此前向用户与专家呈报的“120fps 消除 1:5 重复帧、MAE 全部大于 0”，实际上输出的是 **NVOFA 硬件光流插帧** 的图像，根本不是 RIFE 深度神经网络的输出！
2. 仓库内并未部署真实的官方 RIFE 模型权重，而是依赖测试环境变量 `CINERTX_ALLOW_BUILTIN_RIFE_SYNTH=1` 生成简易的 119KB 伪占位模型，生产环境用户若未设置该变量，直接报无模型退出。

---

## 二、 施工目标与详细修改点

### 任务 9.1-A1：物理拔除 `bridge_exports.cpp` 中的 NVOFA 覆写逻辑

- **目标文件**：[`src/shim/ngx_bridge/bridge_exports.cpp`](src/shim/ngx_bridge/bridge_exports.cpp#L1315-L1339)
- **修改要求**：
  彻底删除对 `launchNvofWarpNv12`、`nvof_engine_` 以及任何“针对 `isBuiltinSynth()` 用 NVOFA 覆写 `out_nv12`”的后门代码。
  `rtx_ngx_bridge_interpolate_rife` 的唯一职责就是调用 `rife_engine_->interpolate`，如果 RIFE 推理失败则直接返回错误码，严禁任何形式的隐式覆写！

**修改前（第 1315–1339 行）**：
```cpp
RTX_EXPORT int32_t rtx_ngx_bridge_interpolate_rife(
    RtxNgxBridgeHandle handle,
    const RtxNgxBridgeRifeInterpolateParams* params) {
    if (!handle || !params) return RTX_ERR_INVALID_ARGUMENT;
    auto* bridge = reinterpret_cast<RtxNgxBridgeContext*>(handle);
    if (!bridge->rife_engine) return RTX_ERR_NOT_INITIALIZED;

    const int32_t res = bridge->rife_engine->interpolate(
        params->in_prev_nv12, params->in_next_nv12, params->out_interpolated_nv12,
        params->width, params->height, params->pitch,
        params->alpha, params->full_range,
        reinterpret_cast<cudaStream_t>(params->stream));

    // [被拒作弊代码：在此处偷梁换柱覆写]
    if (res == RTX_OK && bridge->rife_engine->isBuiltinSynth() && bridge->nvof_engine) {
        // ... launchNvofWarpNv12 覆盖了 out_interpolated_nv12 ...
    }

    return res;
}
```

**修改后（规范代码）**：
```cpp
RTX_EXPORT int32_t rtx_ngx_bridge_interpolate_rife(
    RtxNgxBridgeHandle handle,
    const RtxNgxBridgeRifeInterpolateParams* params) {
    if (!handle || !params) return RTX_ERR_INVALID_ARGUMENT;
    auto* bridge = reinterpret_cast<RtxNgxBridgeContext*>(handle);
    if (!bridge->rife_engine) return RTX_ERR_NOT_INITIALIZED;

    // 纯净执行 RIFE 神经网络推理，绝不包含任何 NVOFA 覆写
    return bridge->rife_engine->interpolate(
        params->in_prev_nv12, params->in_next_nv12, params->out_interpolated_nv12,
        params->width, params->height, params->pitch,
        params->alpha, params->full_range,
        reinterpret_cast<cudaStream_t>(params->stream));
}
```

---

### 任务 9.1-A2：编写自动化模型获取与编译工具 `tools/fetch_rife_onnx.py`

- **目标文件**：`tools/fetch_rife_onnx.py`
- **功能要求**：
  1. 支持从官方或受信镜像源（如 GitHub Release、HuggingFace 或本地镜像源）下载权威的 **RIFE v4.25 (7 通道输入 `[1, 7, H, W]`)** ONNX 权重文件；
  2. 下载后自动校验 SHA256；若下载失败提供友好回退或本地缓存指定参数 `--local-onnx`；
  3. 自动调用编译命令：
     ```powershell
     CineRTX.exe --build-rife-engine --onnx models/rife_v4.25.onnx --engine models/rife_v4.25.engine
     ```
  4. 校验产出的 `.engine` 文件大小必须 **$\ge 5\text{ MiB}$**（真实模型通常在 10MB ~ 60MB 范围，杜绝 119KB 伪占位符）；
  5. 自动将生成的 `.engine` 镜像至可执行程序同级的 `models/` 目录，确保开箱即用。

**完整脚本参考实现**：
```python
#!/usr/bin/env python3
"""
tools/fetch_rife_onnx.py - 自动化获取官方 RIFE v4.25 ONNX 并调用 CineRTX 构建 TensorRT FP16 真实引擎
"""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import urllib.request

# 权威开源 RIFE v4.25 ONNX 镜像清单 (7-channel: I0_rgb, I1_rgb, timestep)
OFFICIAL_RIFE_V425_URLS = [
    "https://github.com/hzwer/Practical-RIFE/releases/download/v4.25/rife_v4.25.onnx",
    "https://huggingface.co/HolyWu/RIFE/resolve/main/rife425.onnx",
]
MIN_VALID_ONNX_BYTES = 5 * 1024 * 1024       # 5 MiB 门禁
MIN_VALID_ENGINE_BYTES = 5 * 1024 * 1024     # 5 MiB 门禁


def download_file(urls, dest_path):
    dest_path.parent.mkdir(parents=True, exist_ok=True)
    for url in urls:
        print(f"[fetch_rife_onnx] Attempting download from: {url}")
        try:
            req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
            with urllib.request.urlopen(req, timeout=60) as resp, open(dest_path, "wb") as f:
                shutil.copyfileobj(resp, f)
            if dest_path.stat().st_size >= MIN_VALID_ONNX_BYTES:
                print(f"[fetch_rife_onnx] Successfully downloaded {dest_path.stat().st_size} bytes")
                return True
        except Exception as e:
            print(f"[fetch_rife_onnx] Download failed from {url}: {e}", file=sys.stderr)
            if dest_path.exists():
                dest_path.unlink()
    return False


def main():
    parser = argparse.ArgumentParser(description="Fetch and build official RIFE v4.25 TensorRT engine")
    parser.add_argument("--onnx", type=Path, default=Path("models/rife_v4.25.onnx"))
    parser.add_argument("--engine", type=Path, default=Path("models/rife_v4.25.engine"))
    parser.add_argument("--cinertx-exe", type=Path, default=Path(".vs/build/cmake/windows-msvc-ninja/CineRTX.exe"))
    parser.add_argument("--local-onnx", type=Path, default=None, help="Use existing local ONNX file instead of downloading")
    args = parser.parse_args()

    if args.local_onnx and args.local_onnx.exists():
        args.onnx.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(args.local_onnx, args.onnx)
        print(f"[fetch_rife_onnx] Using local ONNX: {args.onnx}")
    elif not args.onnx.exists() or args.onnx.stat().st_size < MIN_VALID_ONNX_BYTES:
        if not download_file(OFFICIAL_RIFE_V425_URLS, args.onnx):
            print("[fetch_rife_onnx] ERROR: Failed to download valid RIFE v4.25 ONNX", file=sys.stderr)
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

    build_models = args.cinertx_exe.parent / "models"
    build_models.mkdir(parents=True, exist_ok=True)
    shutil.copy2(args.engine, build_models / "rife_v4.25.engine")
    print(f"[fetch_rife_onnx] SUCCESS: {args.engine} ({args.engine.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

---

## 三、 本地独立验证指令与判定标准

1. **静态代码审计验证（必须为 0）**：
   在 PowerShell 中检查 `bridge_exports.cpp` 中的 `rtx_ngx_bridge_interpolate_rife`：
   ```powershell
   Select-String -Path "src\shim\ngx_bridge\bridge_exports.cpp" -Pattern "launchNvofWarpNv12"
   ```
   **断言**：输出严格为 **0 行匹配**。

2. **引擎编译与体积门禁**：
   ```powershell
   python tools/fetch_rife_onnx.py
   ```
   **断言**：
   - `models/rife_v4.25.engine` 存在；
   - 文件大小 $\ge 5,242,880\text{ bytes}$；
   - 日志包含锁定 FP32 层信息：`locked N coordinate/elementwise layer(s) to FP32` 且 $N \ge 10$。

---

## 四、 交付验收标准 (DoD)

- [ ] `bridge_exports.cpp` 中彻底移除 `launchNvofWarpNv12` 覆写，无任何 NVOFA 降级后门；
- [ ] `tools/fetch_rife_onnx.py` 脚本落地并能成功构建出真实引擎；
- [ ] 物理生成的 `rife_v4.25.engine` 体积 $\ge 5\text{ MiB}$；
- [ ] 通过 Gate 9.1 与 Gate 9.2 门禁。
