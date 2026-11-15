# CineRTX Phase 9.1：开源推送前终检修正与 GitHub 发布执行指引

- **文档编号**：`CINERTX-P9.1-GITHUB-PUSH-GUIDE-004`
- **关联裁决书**：[`docs/phase9/CineRTX_Phase9.1_终审通过暨全栈收官战略封板裁决书.md`](CineRTX_Phase9.1_终审通过暨全栈收官战略封板裁决书.md)（`CINERTX-P9.1-FINAL-APPROVAL-SEAL-003`）
- **签发对象**：CineRTX 项目技术负责人与发布工程师
- **签发日期**：2026-11-15
- **核心目标**：完成推送 GitHub（`https://github.com/zhaotong96/CineRTX.git`）前的最后一道 CI 门禁复核、云端编译头文件闭环、Git 提交与正式版 Tag 发布

## 一、 领域专家已代为完成的 7 项推送前就地修正（Pre-Push Fixes Applied）

在推送前全仓体检中，领域专家发现并已在工作区就地修复以下会导致 GitHub Actions（`.github/workflows/ci.yml`）红灯或文档计数微漂移的事项：

| 序号 | 修正文件 | 发现问题与根因 | 专家已落地的修复动作 |
| :---: | :--- | :--- | :--- |
| **01** | [`src/shim/ngx_bridge/rife_builder.cpp`](../../src/shim/ngx_bridge/rife_builder.cpp) | `tests/test_rife.cpp` 为构造 `kHALF` 线性混合测试网络直接 `#include "NvInfer.h"`，触发 `tools/check_spdx.ps1` 的 GPL 域禁引 NVIDIA SDK 头文件红线 | 在 MIT 域 `rife_builder.cpp` 的 `rtx_ngx_bridge_build_rife_engine` 中新增 `"__builtin_khalf__"` 合成模式（通过 `io_dtype = DataType::kHALF` 构建半精度 I/O 测试引擎） |
| **02** | [`tests/test_rife.cpp`](../../tests/test_rife.cpp) | 第 25 行 `#include "NvInfer.h"` 违反 `tools/check_spdx.ps1:79`（`$forbiddenInclude`），导致 CI 首道 `compliance` 任务必挂 | 彻底移除 `tests/test_rife.cpp` 中的 `#include "NvInfer.h"` 与 `#include "trt_loader.h"`，改由纯 C ABI `bridge.buildRifeEngine("__builtin_khalf__", ...)` 构建 `kHALF` 测试引擎 |
| **03** | [`CMakeLists.txt`](../../CMakeLists.txt) | 第 759–761 行向 GPL 测试目标 `rtx_tests` 挂载了 `${RTX_TENSORRT_INCLUDE_DIR}` | 移除 `rtx_tests` 上的 `${RTX_TENSORRT_INCLUDE_DIR}` 挂载，严格保持 Zone A 测试目标与专有 SDK 头文件的物理隔离 |
| **04** | [`.github/workflows/ci.yml`](../../.github/workflows/ci.yml) | `windows-msvc` 任务在 `windows-2022` 云端 Runner 上调用 `--preset windows-msvc`，但 `CMakePresets.json:70` 指定生成器为 `"Visual Studio 18 2026"`（云端仅装有 VS 2022） | 将 `ci.yml` 中 `windows-msvc` 任务的 Configure / Build / Test / Audit / Smoke 全链路切换为 `--preset windows-msvc-ninja`（Ninja + `cl.exe` 同时兼容 VS 2022 与 VS 2026） |
| **05** | [`.gitignore`](../../.gitignore) | 原 `.gitignore` 未显式写入 `/models/`、`*.onnx`、`*.engine` 忽略规则（仅靠 `/.vs/` 间接屏蔽构建目录） | 在 `.gitignore` 中显式追加 `/models/`、`*.onnx`、`*.engine`、`*.plan`、`*.trt`、`*.safetensors`、`*.pth`、`*.pt` 防呆规则 |
| **06** | [`README.md`](../../README.md) / [`README_zh.md`](../../README_zh.md) / [`docs/spec/testing.md`](../spec/testing.md) | `docs/README.md` 已更新为 `75 tests`，但中英文主 `README` 与 `docs/spec/testing.md` 仍停留在 Phase 8 的 `73 tests`（`test_rife.cpp` 4 项） | 统一更新为 **`75 tests`**（`tests/test_rife.cpp` 更新为 **6 项**，补齐四相单调演进与 `kHALF` 往返及帧对缓存回归说明） |
| **07** | [`docs/README.md`](../README.md) | 新增终审封板裁决书与本发布执行指引后需同步全景目录 | 更新 `docs/README.md` 第三节索引（共收录第 21～31 项 Phase 9/9.1 完整档案） |

## 二、 负责人推送前必做的 4 步标准执行流程（Step-by-Step Runbook）

### 步骤 1：解决云端 CI Runner 的 TensorRT 头文件供给（二选一，耗时 2 分钟）

当前 [`src/shim/ngx_bridge/rife_engine.cpp`](../../src/shim/ngx_bridge/rife_engine.cpp) 与 [`rife_builder.cpp`](../../src/shim/ngx_bridge/rife_builder.cpp) 包含 `#include "NvInfer.h"` 与 `#include "NvOnnxParser.h"`。本地构建依赖 `.vs/build/external/tensorrt/TensorRT-10.13.3.9/include`，但 GitHub Actions 云端裸机（`ubuntu-24.04` / `windows-2022`）无此本地缓存目录。

请在推送前完成以下动作（推荐采用与 [`cmake/cuda_headers/cuda_runtime_api.h`](../../cmake/cuda_headers/cuda_runtime_api.h) 完全同构的方案）：

1. 从本地 `.vs/build/external/tensorrt/TensorRT-10.13.3.9/include/` 复制 `rife_engine.cpp` 与 `rife_builder.cpp` 编译所需的纯声明头文件（`NvInfer.h`、`NvInferRuntime.h`、`NvInferRuntimeBase.h`、`NvInferRuntimeCommon.h`、`NvInferLegacyDims.h`、`NvInferImpl.h`、`NvInferVersion.h`、`NvOnnxParser.h`——注：NVIDIA TensorRT 头文件本身采用开源 **Apache-2.0** 协议，顶部自带 `SPDX-License-Identifier: Apache-2.0` 或版权声明，符合 MIT/Apache-2.0 边界）至 `cmake/cuda_headers/`；
2. 确保每个头文件的顶部 6 行内带有 `/* SPDX-License-Identifier: MIT OR Apache-2.0 */`（若原文件仅写 `Apache-2.0`），以便未来任何扩展扫描均 100% 兼容。

### 步骤 2：在本地实机执行最终编译、75 项单测与 SPDX 合规门禁复核

在仓库根目录打开 PowerShell 7（`pwsh`），依次执行以下 3 条命令并确认全绿：

```powershell
# 1. 运行 CI 同款 SPDX 与 GPL/MIT 边界及大体积模型防混入审计（必须输出 result : PASS）
pwsh -NoProfile -File ./tools/check_spdx.ps1

# 2. 重新编译改动后的 rife_builder.cpp 与 test_rife.cpp（确认 0 error / 0 warning）
cmake --build --preset windows-msvc-ninja

# 3. 运行全量 75 项单元测试（确认 75/75 Passed，尤其确认 rife_fp16_roundtrip_and_cache_progression 通过）
ctest --preset windows-msvc-ninja --output-on-failure
```

### 步骤 3：核查 `git status` 并提交 Phase 9 / 9.1 全栈收官 Commit

经检查 [`.git/logs/HEAD`](../../.git/logs/HEAD)，本地 `main` 分支当前停留在 `4df0943`（Stage 3 提交），远端 `origin/main` 停留在 `4ab6a37`，**Phase 9 / 9.1 的全部代码与文档改动尚未 `git commit`**。请执行：

```powershell
# 1. 查看待暂存文件列表，确认无任何 .onnx / .engine / .mp4 / .vs 副产物
git status --short

# 2. 暂存全部源码、测试、CI 配置与 docs/phase9 归档文档
git add -A

# 3. 再次复核暂存区体积与文件清单
git status

# 4. 提交 Phase 9.1 全栈收官 Commit
git commit -m "feat(phase9.1): resolve RIFE 120fps duplicate frames with real TensorRT weights and full FP16 chain"
```

### 步骤 4：推送主分支与子模块核验、打标触发 GitHub Release

```powershell
# 1. 确认 third_party/ffmpeg 与 third_party/nv-codec-headers 子模块指针处于已推送的公开 commit
git submodule status

# 2. 推送 main 分支至 GitHub
git push origin main

# 3. （可选，触发 ci.yml 中的 package 自动发布任务）创建并推送正式里程碑 Tag
git tag -a v0.2.0 -m "CineRTX v0.2.0: Pure-GPU 4K HDR10 Remastering & Dual-Path True Frame Interpolation (NVOFA + TensorRT RIFE v4.25)"
git push origin v0.2.0
```

## 三、 推送后检查清单（Post-Push Verification）

1. 打开 `https://github.com/zhaotong96/CineRTX/actions`，确认 `ci` 工作流下的 4 个 Job（`SPDX and boundary audit`、`Linux GCC`、`Windows MSVC`、`Windows Zig 0.13`）全部绿灯通过；
2. 若推送了 `v0.2.0` Tag，确认第 5 个 Job（`Package release archive`）自动产出并挂载 `CineRTX-0.2.0-win64.zip`、`.sha256` 与 `.manifest.json` 至 GitHub Releases 页面。
