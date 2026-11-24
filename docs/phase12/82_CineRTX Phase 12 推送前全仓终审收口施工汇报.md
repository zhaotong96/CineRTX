# 82_CineRTX Phase 12 推送前全仓终审收口施工汇报

- **关联正式施工令**：`81_CineRTX Phase 12 推送前全仓终审收口正式施工令.md`（`CINERTX-P12-EXEC-ORDER-081`）
- **关联前序文档**：`79_CineRTX Phase 12 推送前全仓终审收口SuperPower施工单.md`、`80_终审裁决：暂缓推送（HOLD git push）.md`
- **施工基线**：Commit `40148dda8f`（`chore(release): purge scripts/, enforce CMake-native AuditPrivacy, and seal v0.4.0`）
- **施工日期**：2026-11-24
- **施工状态**：TASK-01 至 TASK-06 全部 11 份文件原位改造已完成并通过 YAML 解析、CMake 控制流平衡、零敏感词镜像审计与静态门禁零退化复核；旧版已全部外置归档至仓外受控目录（`*.r81bak`），仓内 `.bak` 残留为 `0`；待 Windows MSVC 宿主机同步执行 §三 验收流水线并采集 `git diff --stat`、147 单测与新 ZIP SHA-256 哈希后即可呈请放行 `git push origin main --tags`。

---

## 一、TASK-01 至 TASK-06 施工落盘明细（共 11 份受控文件，0 触碰 C++ 业务源码）

| 任务编号 | 涉及文件 | 施工内容与静态校验 |
| :--- | :--- | :--- |
| **TASK-01（P0 阻断）** | `.github/workflows/ci.yml` | 1. 为 `linux-gcc`、`windows-msvc`、`windows-zig` 的 `actions/checkout@v4` 全部补齐 `submodules: recursive`。<br>2. 在顶层 `compliance` 作业追加 `AuditTypes.cmake`、`AuditSloc.cmake`、`AuditPrivacy.cmake`，形成完整五重静态门禁。<br>3. `linux-gcc` 移除错误的 Windows PE 解析器调用，改为 `ldd` 检查 ELF 动态依赖不含 `libcuda|libnvcuvid|libnvidia-encode|libnvinfer`；`windows-msvc` 与 `windows-zig` 升级为双模 `AuditImports.cmake`（`CineRTX.exe -DMODE=GUI` + `rtx_headless_cli.exe -DMODE=HEADLESS`）。<br>4. 全部 3 处 SSOT Schema 断言由 `19` 修正为 `20`；`windows-zig` 产物上传补入 `rtx_headless_cli.exe`。（PyYAML 解析全量通过。） |
| **TASK-02（P0 阻断）** | `cmake/AuditPrivacy.cmake`<br>`docs/phase12/77_*.md` | 1. `cmake/AuditPrivacy.cmake` 保持动态 `string(CONCAT ...)` 拼装，新增非用户盘符宿主机路径规则 `_HOST_PUB_RX` 与测试机 SSH 账号规则 `_TEST_SSH_RX`，并将 `.github/workflows/*.yml`、`src/*.cu`、`src/*.hlsl`、`webui/state.json`、`build.bat`、`.gitmodules` 纳入 `_PRIV_FILES`，同步补齐 `if(FIX)` 替换与审计拦截逻辑（`if/endif=18, foreach/endforeach=5`）。<br>2. 将 `docs/phase12/77_*.md` 内嵌代码块整体替换为零字面敏感词的 `string(CONCAT ...)` 权威实现。 |
| **TASK-03（P0 军规）** | `CMakeLists.txt`（L315–358） | **严格保持现状，零改动**，完整维持 RIFE ONNX 开箱即用自动拉取与离线优雅提示能力。 |
| **TASK-04（P1 规范）** | `docs/spec/compliance.md`<br>`docs/spec/architecture.md` | 1. `compliance.md` 全面更新旧 `tools/*.ps1` / `tools/build_ffmpeg_static_*.sh` 为原生 `cmake/Audit*.cmake`、`cmake/PackageRelease.cmake`、`cmake/RtxFfmpegStatic.cmake`，拔除已废除环境变量引用，校准为确定性可执行文件同级目录与 `plugins/nvidia_rtx/` 寻址契约。<br>2. `architecture.md` 顶层加注 `v0.4.0`（Phase 12）纯 CMake 原生门禁治理适用说明。 |
| **TASK-05（P1 契约）** | `docs/TODO/README.md`<br>`webui/README.md` | 1. `docs/TODO/README.md` 第 3 节正式追加第 7 条「CPU 广播级编码轨道与 CRF 恒定质量码控演进」远期规划。<br>2. `webui/README.md` 20 参数表中为 `"av1_cpu"`、`"hevc_cpu"`、`"crf"` 加注 *(Roadmap / 规划路线)* 标注，明确当前 `v0.4.0` 优先承载 20 项纯 GPU 零拷贝 SSOT。 |
| **TASK-06（P1/P2 卫生）** | `cmake/PackageRelease.cmake`<br>`README.md`、`README_zh.md`<br>`docs/phase11/34_*.md` | 1. `PackageRelease.cmake:6` 注释修正为真实 Preset `.vs/build/cmake/windows-msvc-ninja`（`if/endif=22, foreach/endforeach=3`）。<br>2. `README.md` 与 `README_zh.md` 构建门禁清单补齐第五门禁 `cmake -P cmake/AuditPrivacy.cmake`。<br>3. `docs/phase11/34_*.md` 全部 **16 处**误加伪协议头的相对链接已修复为合规 `./` 相对路径。 |

---

## 二、沙箱静态门禁复核与纪律承诺

| 核验项 | 目标红线 | 实测结果 | 判定 |
| :--- | :--- | :--- | :---: |
| **`AuditPrivacy` 镜像复核** | 全量 10 份受改文档/脚本 + 源码树 0 命中 | **0 privacy hits**（含 `77` 号文与本 `82` 号文） | **PASS** |
| **`AuditTypes`（具名类型预算）** | `34 / 35`，0 Violations | **34 / 35（0 Violations）** | **PASS** |
| **Core Engine Pure SLOC** | `12,157` 行（≤ 23,000） | **0 行变动**（未触碰任何 C++ 业务源码） | **PASS** |
| **`AuditSpdx` / `OldFormat` / `.bak`** | `0` / `0` / `0` | **Missing = 0 / Hits = 0 / 仓内 `.bak` = 0** | **PASS** |

**郑重承诺**：本轮施工严格恪守 81 号施工令全部四条铁律——**零触碰任何 `include/`、`src/`、`tests/` 下的 C++ 业务逻辑源码**，**零改动 `CMakeLists.txt` 开箱即用自动拉取机制**，**仓内零 `.bak`/`.tmp` 残留**，且**未擅自执行任何 `git commit` 或 `git push`**。

---

## 三、Windows MSVC 宿主机最终验收与发版封装流水线

> 注：因当前执行环境为无 Windows MSVC/RTX 5060 的沙箱容器，请宿主机同步上述 11 份更新文件后依次执行以下命令，完成全仓历史文档二次清洗（清除漏网宿主机非用户盘符路径与测试机账号）、五重原生硬门禁、双模 IAT、147 单测及最终 ZIP SHA-256 哈希采集：

```powershell
# 1. 全仓一键二次脱敏并复核 0 隐私命中（同步清洗历史文档中残余路径与测试账号）
cmake -DFIX=ON -P cmake/AuditPrivacy.cmake
cmake -P cmake/AuditPrivacy.cmake

# 2. 五重纯原生静态硬门禁（全部必须返回退出码 0）
cmake -P cmake/AuditSpdx.cmake
cmake -P cmake/AuditTypes.cmake
cmake -P cmake/AuditSloc.cmake
cmake -P cmake/AuditPrivacy.cmake

# 3. 编译、双模 PE 导入表硬审计与 147 项单测全绿
cmake --preset windows-msvc-ninja
cmake --build --preset windows-msvc-ninja --target CineRTX rtx_headless_cli rtx_tests
cmake -DTARGET_BIN=.vs/build/cmake/windows-msvc-ninja/CineRTX.exe -DMODE=GUI -P cmake/AuditImports.cmake
cmake -DTARGET_BIN=.vs/build/cmake/windows-msvc-ninja/rtx_headless_cli.exe -DMODE=HEADLESS -P cmake/AuditImports.cmake
.\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe

# 4. 重新封装最终版 CineRTX-v0.4.0-windows-x64.zip 并记录 SHA-256 与 diff 统计
cmake -DBUILD_DIR=.vs/build/cmake/windows-msvc-ninja -P cmake/PackageRelease.cmake
git diff --stat
```
