# CineRTX Phase 12 推送前全仓终审收口 SuperPower 施工单

- **施工单编号**：`CINERTX-P12-WORKORDER-079`
- **归档路径**：`docs/phase12/79_CineRTX Phase 12 推送前全仓终审收口SuperPower施工单.md`
- **签发主体**：多媒体系统工程与 GPU 异构计算首席领域专家组（Extreme Modern C++ Purist & Architecture Board）
- **审计基线**：Commit `40148dda8f`（`chore(release): purge scripts/, enforce CMake-native AuditPrivacy, and seal v0.4.0`）
- **签发日期**：2026-11-24
- **终审定性**：**HOLD `git push` — 拦截推送并执行 6 项定点收口任务（`PRE-PUSH-01` ~ `PRE-PUSH-06`），全部门禁与自检绿灯后即刻放行 `v0.4.0` 推送远端**

---

## 一、 推送前全仓穿透审计总账与拦截动因

对基线 `40148dda8f` 执行五大维度（隐私脱敏、GPLv3 双许可隔离、现代 C++26 设计哲学、`[DEC-002]` 目录约定、CI 与发布链路）穿透核验后确认：**核心 C++26 源码（`include/`、`src/`）、生产二进制 GPLv3 + Plugin-Exception 物理隔离与根域零 `scripts/`/`tools/`/`schema/` 结构均已达标**；但实证检出以下 **P0 阻断推送缺陷**与 **P1 高危漂移**，必须按本施工单一次性收口后再推送 GitHub：

| 任务编号 | 级别 | 靶向文件与精确行号 | 实证缺陷与推送后果 |
| :--- | :---: | :--- | :--- |
| **PRE-PUSH-01** | **P0 阻断** | `.github/workflows/ci.yml`（L26–36、L48/106/144、L73/118/168、L85/132/182、L193） | **推送即 100% CI 全线崩溃（四重断环）**：① 3 个构建作业的 `actions/checkout@v4` 缺少 `submodules: recursive`，而 `CMakeLists.txt:64–67` 已废除配置期静默拉取并改为缺失 `nv-codec-headers` 直接 `FATAL_ERROR`；② L73/118/168 仍用旧参数 `-DBUILD_DIR=...` 调用已改为必填 `-DTARGET_BIN=... -DMODE=...` 的 `AuditImports.cmake`（且 Linux ELF 作业误调 Windows PE 解析器）；③ L85/132/182 的 Schema 断言仍写死 `len(params) == 19`（实际已升级为 `20` 项）；④ `compliance` 作业遗漏 `AuditTypes`、`AuditSloc`、`AuditPrivacy`，且 `zig-windows` 产物上传漏传 `rtx_headless_cli.exe`。 |
| **PRE-PUSH-02** | **P0 阻断** | `cmake/AuditPrivacy.cmake`（L18–96）与 `docs/phase12/77_*.md`（L50–115） | **受控文档二次泄密与隐私门禁三大盲区**：① `77` 号裁决书内嵌代码块未采用 `string(CONCAT ...)` 拆分，字面暴露个人账户标识与云盘目录特征；② `AuditPrivacy.cmake` 漏扫非用户盘符宿主机绝对路径（`[REDACTED_PATH]:/PUB...` 形式，残存于 `docs/phase12/07,11,20,28,32,38,39,43,44,56` 及 `docs/phase11/32`）与测试机 SSH 账号前缀（残存于 `docs/phase11/22,26`）；③ 未覆盖 `.github/workflows/*.yml`、`build.bat`、`.gitmodules`、`*.cu`、`*.hlsl` 与 `webui/state.json`。 |
| **PRE-PUSH-03** | **P1 高危** | `CMakeLists.txt`（L315–358） | **配置期静默联网残余**：Configure 阶段仍自动调用 `curl` / `file(DOWNLOAD)` 下载 RIFE v4.25 ONNX 模型，局部违背 `HYG-P12-04` 配置期离线确定性准则。 |
| **PRE-PUSH-04** | **P1 高危** | `docs/spec/compliance.md`（L21/47/51/93/109/128/148/176/178/192/208）、`architecture.md`、`gate1.md`、`gate3.md`、`testing.md` | **合规与架构权威规范严重漂移**：仍指向已物理铲除的旧外置目录及脚本，且 `compliance.md:93` 仍记录已拔除的隐藏环境变量 `RTX_NVIDIA_DIR`。 |
| **PRE-PUSH-05** | **P1 高危** | `webui/README.md`（L73–75、L92–101）、`webui/server.py`（L47、L49） | **三端 SSOT 契约漂移**：已标 `v0.4.0` 的 `webui/README.md` 仍宣称支持已从 C++26 核心剔除的 CPU 软编（`av1_cpu`/`hevc_cpu`/`crf`），`server.py` 仍残留对应死字典项。 |
| **PRE-PUSH-06** | **P1/P2 收尾** | `cmake/PackageRelease.cmake`（L6）、`docs/phase12/74,77,78_*.md`、`README.md`、`README_zh.md`、`docs/phase11/00,34_*.md` | **错误 Preset 名与文档卫生收尾**：修正不存在的 `windows-msvc-relwithdebinfo` 为真实 Preset `windows-msvc-ninja`（输出 `.vs/build/cmake/windows-msvc-ninja`）；在 `README.md` / `README_zh.md` 补列第五门禁 `AuditPrivacy.cmake`；修复 `docs/phase11/34` 失效链接与 `docs/phase11/00` 排版标记。 |

> **铁律**：本施工单严禁触碰任何 C++ 业务逻辑源码（`include/`、`src/`、`tests/`），不改变 `AuditTypes = 34/35` 与 `AuditSloc Core = 12,157` 封板基线；禁止在仓库内生成任何 `.bak`/`.tmp` 文件；禁止擅自执行 `git push`。

---

## 二、 原子施工任务规范（Atomic Tasks）

### 任务 `PRE-PUSH-01`：修复 `.github/workflows/ci.yml` 四重 CI 断环（P0）

**涉及文件**：`.github/workflows/ci.yml`

1. **补齐全套五重静态门禁（`compliance` 作业，L26–36）**：
   在 `cmake -P cmake/AuditSpdx.cmake` 之后串行追加：
   ```yaml
      - name: Audit named types (Std-First <= 35)
        run: cmake -P cmake/AuditTypes.cmake

      - name: Audit Core Engine Pure SLOC (<= 23,000)
        run: cmake -P cmake/AuditSloc.cmake

      - name: Audit repository privacy and host path hygiene
        run: cmake -P cmake/AuditPrivacy.cmake
   ```
2. **为全部构建作业补齐子模块检出（L48、L106、L144）**：
   将 `linux-gcc`、`windows-msvc`、`windows-zig` 中的 `actions/checkout@v4` 统一补充：
   ```yaml
      - name: Check out source
        uses: actions/checkout@v4
        with:
          submodules: recursive
   ```
3. **修复 `AuditImports.cmake` 双模参数与 Linux 跨平台误调（L72–73、L117–118、L167–168）**：
   - `linux-gcc`（L72–73）：Linux 产物为 ELF，**移除**对 Windows PE 解析器 `AuditImports.cmake` 的调用，改为校验 `ldd .vs/build/cmake/linux-release/CineRTX` 不含 `libcuda`/`libnvcuvid`/`libnvidia-encode`/`libnvinfer`。
   - `windows-msvc`（L117–118）与 `windows-zig`（L167–168）：替换为双模硬门禁调用：
     ```yaml
      - name: Audit PE static import tables (GUI + Headless)
        run: |
          cmake -DTARGET_BIN=.vs/build/cmake/<preset>/CineRTX.exe -DMODE=GUI -P cmake/AuditImports.cmake
          cmake -DTARGET_BIN=.vs/build/cmake/<preset>/rtx_headless_cli.exe -DMODE=HEADLESS -P cmake/AuditImports.cmake
     ```
     （将 `<preset>` 分别替换为 `windows-msvc-ninja` 与 `zig-windows`。）
4. **修正 SSOT 参数数量断言与 Release 产物上传（L85、L132、L182、L193）**：
   - 将 L85、L132、L182 中的 `len(schema["params"]) == 19`（及 `len(s.get('params',[]))==19`）全部修正为 **`== 20`**。
   - 在 `windows-zig` 的 `Upload portable binaries`（L192–193）补充上传 `.vs/build/cmake/zig-windows/rtx_headless_cli.exe`（若构建生成）。

---

### 任务 `PRE-PUSH-02`：加固 `cmake/AuditPrivacy.cmake` 并彻底净化 `doc77` 与漏网历史文档（P0）

**涉及文件**：`cmake/AuditPrivacy.cmake`、`docs/phase12/77_*.md` 及 `cmake -DFIX=ON` 命中的全部历史文档

1. **升级 `cmake/AuditPrivacy.cmake` 正则与扫描面**（严守动态拼接，确保脚本自身 0 字面敏感词）：
   - 在规则定义区（L18–29）补充以下 2 条动态拼接检测规则：
     ```cmake
     string(CONCAT _HOST_PUB_RX "[A-Za-z]:[/\\\\]" "PUB" "LIC[/\\\\]CineRTX")
     string(CONCAT _TEST_SSH_RX "(^|[^A-Za-z0-9_])" "we" "ak" "@(127\\.0\\.0\\.1|localhost|[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+)")
     ```
   - 在 `_PRIV_FILES` 采集区（L32–60）补充纳入：
     - `"${REPO_ROOT}/.github/workflows/*.yml"`
     - `"${REPO_ROOT}/src/*.cu"`、`"${REPO_ROOT}/src/*.hlsl"`
     - `"${REPO_ROOT}/webui/state.json"`
     - 根文件列表补入 `"${REPO_ROOT}/build.bat"` 与 `"${REPO_ROOT}/.gitmodules"`。
   - 在 `if(FIX)` 替换区（L75–89）补入对应脱敏替换：
     - `${_HOST_PUB_RX}` $\rightarrow$ `"[REDACTED_PATH]"`
     - `${_TEST_SSH_RX}` $\rightarrow$ `"\\1[REDACTED_HOST]"`
   - 在审计检测循环（L121–147）补入对 `_HOST_PUB_RX` 与 `_TEST_SSH_RX` 的 `list(APPEND _VIOLATIONS ...)` 拦截。
2. **原位净化 `docs/phase12/77_*.md` 第 50–115 行代码块**：
   - 将 `77` 号裁决书内嵌的 `AuditPrivacy.cmake` 示例同步替换为与实际 `cmake/AuditPrivacy.cmake` 一致的 `string(CONCAT ...)` 动态拼接版本，彻底消灭字面账户名与云盘目录词残留。
3. **执行全仓一键净化与零命中复核**：
   ```powershell
   cmake -DFIX=ON -P cmake/AuditPrivacy.cmake
   cmake -P cmake/AuditPrivacy.cmake
   ```
   确认输出 `PASS: 0 privacy hits` 且 `docs/phase12/07,11,20,28,32,38,39,43,44,56,77` 与 `docs/phase11/22,26,32` 均已 100% 脱敏。

---

### 任务 `PRE-PUSH-03`：收口 `CMakeLists.txt` 配置期静默联网下载（P1）

**涉及文件**：`CMakeLists.txt`（L315–358）

- 移除或显式门控配置期自动 `curl` / `file(DOWNLOAD "${_rife_onnx_url}" ...)` 逻辑：当本地未缓存 `rife_v4.25.onnx` 时，在配置阶段仅输出 `message(STATUS ...)` 提示离线/按需放置路径，绝不在 `cmake --preset` 配置阶段发起未授权外部网络请求（保持离线确定性与快速配置）。

---

### 任务 `PRE-PUSH-04`：同步 `docs/spec/` 权威合规与架构规范至 `v0.4.0` 实态（P1）

**涉及文件**：`docs/spec/compliance.md`（必改）；`docs/spec/architecture.md`、`gate1.md`、`gate3.md`、`testing.md`（同步加注历史快照标识或更新失效脚本路径）

1. **`docs/spec/compliance.md` 全量校准**：
   - 将所有指向旧外置目录的校验命令替换为原生 CMake 门禁：`cmake -P cmake/AuditSpdx.cmake`、`cmake -P cmake/AuditTypes.cmake`、`cmake -P cmake/AuditSloc.cmake`、`cmake -P cmake/AuditPrivacy.cmake`、`cmake -DTARGET_BIN=... -DMODE=<GUI|HEADLESS> -P cmake/AuditImports.cmake`、`cmake -P cmake/PackageRelease.cmake`；
   - 将 FFmpeg 静态编译配置锚点更新为 `cmake/RtxFfmpegStatic.cmake`；
   - 删除第 93 行已拔除的 `RTX_NVIDIA_DIR` 环境变量描述，改为确定性部署路径契约（可执行文件同级目录与 `plugins/nvidia_rtx/`）；
   - License Map 表格移除已不存在的旧脚本目录列项。
2. **其余 `docs/spec/` 规范文件**：在 `architecture.md`、`gate1.md`、`gate3.md`、`testing.md` 顶部补充版本适用说明或将失效脚本命令对齐至当前原生 `cmake -P` 体系。

---

### 任务 `PRE-PUSH-05`：清除 WebUI 废弃 CPU 软编契约漂移（P1）

**涉及文件**：`webui/README.md`（L73–77、L92–105）、`webui/server.py`（L46–50）

1. **`webui/README.md`**：
   - 第 73–75 行参数表中移除 `"av1_cpu"`、`"hevc_cpu"`、`"crf"` 与 `SVT-AV1 0..13`，收敛为纯 GPU 契约（`codec`: `"av1" | "hevc"`、`preset`: `1..7`、`rateControl`: `"cq" | "vbr" | "cbr" | "qp"`）；
   - 第 92–105 行删除“CPU 广播级编码轨道 (SVT-AV1 / x265)”章节，聚焦 NVIDIA 纯 GPU 零拷贝管线与 `vsr-first` / `fg-first` 双拓扑。
2. **`webui/server.py`**：
   - 删除第 47 行 `"av1_cpu": "_remaster_av1.mp4"` 与第 49 行 `"hevc_cpu": "_remaster_hevc.mp4"` 死字典项，确认不破坏任何现有路由或测试。

---

### 任务 `PRE-PUSH-06`：修正错误 Preset 名称与文档卫生收尾（P1/P2）

**涉及文件**：`cmake/PackageRelease.cmake`（L6）、`docs/phase12/74,77,78_*.md`、`README.md`、`README_zh.md`、`docs/phase11/00,34_*.md`

1. **统一正确 Preset 与构建目录**：将 `cmake/PackageRelease.cmake`（L6）及 `docs/phase12/74`、`77`（L233–238）、`78`（L43–51）中不存在的 `windows-msvc-relwithdebinfo` 与 `build/windows-msvc-relwithdebinfo/RelWithDebInfo` 统一更正为 `CMakePresets.json` 真实定义的 **`windows-msvc-ninja`** 与 **`.vs/build/cmake/windows-msvc-ninja`**。
2. **补齐 README 第五门禁**：在 `README.md`（L94–99）与 `README_zh.md`（L94–98）的审计命令块补入 `cmake -P cmake/AuditPrivacy.cmake`。
3. **历史文档微观清洁**：修正 `docs/phase11/34_*.md` 中 `http://33_CineRTX...` 等失效相对链接协议头，清除 `docs/phase11/00_*.md` 中残留的 `[span_28]` 排版标记。

---

## 三、 验收硬门禁与最终安全推送流水线

全部 6 项任务落盘后，在 Windows 宿主机依次执行以下唯一确定的纯 CMake 原生验证序列（任一步骤非 0 退出即拦截）：

```powershell
# 1. 五重纯原生静态硬门禁（全部必须 PASS，退出码 0）
cmake -P cmake/AuditSpdx.cmake
cmake -P cmake/AuditTypes.cmake
cmake -P cmake/AuditSloc.cmake
cmake -P cmake/AuditPrivacy.cmake

# 2. 全量构建、双模 PE IAT 硬审计与单元测试全绿（147/147 PASS）
cmake --preset windows-msvc-ninja
cmake --build --preset windows-msvc-ninja --target CineRTX rtx_headless_cli rtx_tests
cmake -DTARGET_BIN=.vs/build/cmake/windows-msvc-ninja/CineRTX.exe -DMODE=GUI -P cmake/AuditImports.cmake
cmake -DTARGET_BIN=.vs/build/cmake/windows-msvc-ninja/rtx_headless_cli.exe -DMODE=HEADLESS -P cmake/AuditImports.cmake
.\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe

# 3. 便携发布包五重联动封装（刷新 SHA256SUMS）
cmake -DBUILD_DIR=.vs/build/cmake/windows-msvc-ninja -P cmake/PackageRelease.cmake

# 4. 确认工作区纯净后提交、重打 v0.4.0 标签并推送 GitHub
git status
git add -A
git commit -m "chore(release): fix CI gates, harden AuditPrivacy, and align v0.4.0 specs"
git tag -f -a v0.4.0 -m "CineRTX v0.4.0: C++26 Std-First (34/35 types), Pure CMake Gates, Headless 2-DLL IAT"
git push origin main --tags
```
