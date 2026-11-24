# CineRTX Phase 12 推送前全仓终审收口正式施工令

- **文件编号**：`CINERTX-P12-EXEC-ORDER-081`
- **归档路径**：`docs/phase12/81_CineRTX Phase 12 推送前全仓终审收口正式施工令.md`
- **签发主体**：CineRTX 工程协调推进组（Engineering Coordination Board）
- **接收主体**：多媒体系统工程与异构计算施工工程师（Implementation Engineer）
- **审计基线**：Commit `40148dda8f`（`chore(release): purge scripts/, enforce CMake-native AuditPrivacy, and seal v0.4.0`）
- **签发日期**：2026-11-24
- **定调与依据**：基于用户对 [79号工单](79_CineRTX%20Phase%2012%20%E6%8E%A8%E9%80%81%E5%89%8D%E5%85%A8%E4%BB%93%E7%BB%88%E5%AE%A1%E6%94%B6%E5%8F%A3SuperPower%E6%96%BD%E5%87%A5%E5%8D%95.md) 与 [80号裁决](80_%E7%BB%88%E5%AE%A1%E8%A3%81%E5%86%B3%EF%BC%9A%E6%9A%82%E7%BC%93%E6%8E%A8%E9%80%81%EF%BC%88HOLD%20git%20push%EF%BC%89.md) 的终审批复（**坚决维持 P1-1 开箱即用机制、在 TODO 中正式收录 P1-3 CRF 规划、全量展开并就地备案 P2 卫生项，坚决阻断推送并执行定点收口**）。

---

## 一、 施工铁律与物理边界约束

1. **零触碰 C++ 业务逻辑源码**：严禁修改 `include/rtx/`、`src/core/`、`src/shim/`、`src/gui/`、`tests/` 下的任何 C++ 业务逻辑，严格维持 `AuditTypes = 34/35` 与 `AuditSloc Core = 12,157` 封板基线不变。
2. **坚决维持 P1-1 开箱即用机制**：严禁触碰或删除 `CMakeLists.txt:315–358` 中关于 RIFE ONNX 模型的 CMake 自动拉取与配置期检测逻辑；该机制属于 Phase 9.1 确立的 P0 核心军规，绝不允许破坏。
3. **工作区纯洁度铁律**：严禁在工作区生成任何 `.bak`、`.tmp`、`.orig`、`~` 临时文件；严禁对 `.git/logs/` 本地日志执行任何破坏性操作。
4. **提交与推送禁令**：未经协调负责人与用户明确下达授权指令前，**绝对严禁执行 `git commit` 与 `git push`**。

---

## 二、 原子施工任务规范（Atomic Work Packages）

### 任务 TASK-01（P0 阻断）：修复 `.github/workflows/ci.yml` 四重 CI 断环

**涉及文件**：`.github/workflows/ci.yml`

1. **子模块递归检出（补齐空目录断环）**：
   在 `linux-gcc`（约 L48）、`windows-msvc`（约 L106）、`windows-zig`（约 L144）的 `actions/checkout@v4` 中统一显式配置：
   ```yaml
         - name: Check out source
           uses: actions/checkout@v4
           with:
             submodules: recursive
   ```
2. **补齐全套五重静态门禁（顶层 `compliance` 作业，约 L34–36）**：
   在 `cmake -P cmake/AuditSpdx.cmake` 之后串行追加：
   ```yaml
         - name: Audit named types (Std-First <= 35)
           run: cmake -P cmake/AuditTypes.cmake

         - name: Audit Core Engine Pure SLOC (<= 23,000)
           run: cmake -P cmake/AuditSloc.cmake

         - name: Audit repository privacy and host path hygiene
           run: cmake -P cmake/AuditPrivacy.cmake
   ```
3. **纠正 `AuditImports.cmake` 参数与跨平台调用（约 L72–73、L117–118、L167–168）**：
   - `linux-gcc`（L72–73）：Linux 产物为 ELF 二进制，**彻底移除**对 Windows PE 解析器 `AuditImports.cmake` 的调用，改为：
     ```yaml
         - name: Audit ELF dynamic dependencies
           run: |
             ldd .vs/build/cmake/linux-release/CineRTX | grep -ivE "libcuda|libnvcuvid|libnvidia-encode|libnvinfer"
     ```
   - `windows-msvc`（L117–118）与 `windows-zig`（L167–168）：更正为双模硬门禁传参：
     ```yaml
         - name: Audit PE static import tables (GUI + Headless)
           run: |
             cmake -DTARGET_BIN=.vs/build/cmake/<preset>/CineRTX.exe -DMODE=GUI -P cmake/AuditImports.cmake
             cmake -DTARGET_BIN=.vs/build/cmake/<preset>/rtx_headless_cli.exe -DMODE=HEADLESS -P cmake/AuditImports.cmake
     ```
     （分别将 `<preset>` 替换为 `windows-msvc-ninja` 与 `zig-windows`。）
4. **修正 SSOT 参数断言与 Release 产物上传（约 L85、L132、L182、L193）**：
   - 将各作业中的 `len(schema["params"]) == 19` 及 `len(s.get('params',[]))==19` 全部修正为 **`== 20`**。
   - 在 `windows-zig` 的 `Upload portable binaries` 补入 `.vs/build/cmake/zig-windows/rtx_headless_cli.exe`。

---

### 任务 TASK-02（P0 阻断）：加固 `cmake/AuditPrivacy.cmake` 并彻底净化 77 号文及漏网历史文档

**涉及文件**：`cmake/AuditPrivacy.cmake`、`docs/phase12/77_*.md` 及受影响历史文档

1. **加固 `cmake/AuditPrivacy.cmake` 扫描范围与动态拼接规则**：
   - 严守动态拼接原则（确保脚本自身 0 字面敏感词），在规则定义区（约 L18–29）补充：
     ```cmake
     string(CONCAT _HOST_PUB_RX "[A-Za-z]:[/\\\\]" "PUB" "LIC[/\\\\]CineRTX")
     string(CONCAT _TEST_SSH_RX "(^|[^A-Za-z0-9_])" "we" "ak" "@(127\\.0\\.0\\.1|localhost|[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+)")
     ```
   - 在 `_PRIV_FILES` 文件采集区（约 L32–60）补充纳入：
     - `"${REPO_ROOT}/.github/workflows/*.yml"`
     - `"${REPO_ROOT}/src/*.cu"`、`"${REPO_ROOT}/src/*.hlsl"`
     - `"${REPO_ROOT}/webui/state.json"`
     - 根文件列表补入 `"${REPO_ROOT}/build.bat"` 与 `"${REPO_ROOT}/.gitmodules"`。
   - 在 `if(FIX)` 替换区（约 L75–89）补入脱敏替换：
     - `${_HOST_PUB_RX}` $\to$ `"[REDACTED_PATH]"`
     - `${_TEST_SSH_RX}` $\to$ `"\\1[REDACTED_HOST]"`
   - 在审计检测循环（约 L121–147）补入对 `_HOST_PUB_RX` 与 `_TEST_SSH_RX` 的拦截。
2. **原位清洗 `docs/phase12/77_*.md` 第 50–115 行代码块**：
   - 将 `77` 号文内嵌代码块中的明文敏感词替换为与 `AuditPrivacy.cmake` 严格一致的 `string(CONCAT ...)` 拼接实现，消灭裸露字面量。
3. **全仓自动脱敏与零命中复核**：
   - 执行：
     ```powershell
     cmake -DFIX=ON -P cmake/AuditPrivacy.cmake
     cmake -P cmake/AuditPrivacy.cmake
     ```
   - 确认输出 `PASS: 0 privacy hits`。

---

### 任务 TASK-03（P0 军规维持）：坚决维持 `CMakeLists.txt` 开箱即用自动拉取机制

**涉及文件**：`CMakeLists.txt`（L315–358）

- **执行指令**：**保持现状，严禁改动！**
- 坚决贯彻 Phase 9.1 任务五确立的开箱即用军规，维持 CMake 配置期自动拉取 RIFE ONNX 模型及离线优雅降级为 `STATUS` 提示的完整能力。

---

### 任务 TASK-04（P1 规范）：同步更新 `docs/spec/compliance.md` 与 `architecture.md`

**涉及文件**：`docs/spec/compliance.md`、`docs/spec/architecture.md`

1. **`docs/spec/compliance.md` 校准**：
   - 将所有指向已物理铲除的旧 `tools/*.ps1` 校验命令全面更新为原生 CMake 门禁（`cmake -P cmake/AuditSpdx.cmake`、`cmake -P cmake/AuditTypes.cmake`、`cmake -P cmake/AuditSloc.cmake`、`cmake -P cmake/AuditPrivacy.cmake`、`cmake -DTARGET_BIN=... -DMODE=<GUI|HEADLESS> -P cmake/AuditImports.cmake`、`cmake -P cmake/PackageRelease.cmake`）；
   - 将 FFmpeg 静态编译配置锚点校准为 `cmake/RtxFfmpegStatic.cmake`；
   - 拔除已清理的环境变量 `RTX_NVIDIA_DIR`，规范为可执行文件同级目录及 `plugins/nvidia_rtx/` 确定性寻址契约。
2. **其余架构规范**：
   - 在 `docs/spec/architecture.md` 顶层加注版本适用说明，明确 v0.4.0 起全面由纯 CMake 原生硬门禁接管。

---

### 任务 TASK-05（P1 契约）：收录 CRF 远期规划并对齐 WebUI 契约

**涉及文件**：`docs/TODO/README.md`、`webui/README.md`

1. **`docs/TODO/README.md` 追加远期规划**：
   - 在第 3 节“v0.3+ 远期展望（非承诺性路线备忘）”中追加第 7 条：
     ```markdown
     7. **CPU 广播级编码轨道与 CRF 恒定质量码控演进**：依托底层的 `cpu_video_encoder.h/.cpp` 既有架构，评估在纯 GPU 硬件管线之外恢复接入 SVT-AV1 / x265 CPU 软编与 CRF 码控，以满足专业母版压制诉求。
     ```
2. **`webui/README.md` 契约说明对齐**：
   - 在第 73–75 行参数表中对 `"av1_cpu"`、`"hevc_cpu"`、`"crf"` 加注 `(Roadmap / 规划路线)` 标注，说明当前 v0.4.0 核心优先承载 20 项纯 GPU 零拷贝 SSOT。

---

### 任务 TASK-06（P1/P2 卫生收尾）：修正 Preset 名称、补齐 README 第五门禁及微观链接清洗

**涉及文件**：`cmake/PackageRelease.cmake`、`README.md`、`README_zh.md`、`docs/phase11/34_*.md`

1. **更正 Preset 名称**：
   - 将 `cmake/PackageRelease.cmake:6` 注释中不存在的 `windows-msvc-relwithdebinfo` 更正为真实 Preset `windows-msvc-ninja`。
2. **补齐双语 README 第五门禁**：
   - 在 `README.md`（L94–99）与 `README_zh.md`（L94–98）静态审计列表中补齐 `cmake -P cmake/AuditPrivacy.cmake`。
3. **历史微观链接清洗**：
   - 将 `docs/phase11/34_*.md` 中第 5、15 行误加的 `http://33_...` 伪协议头修复为合规的 Markdown 相对路径链接。

---

## 三、 施工验收全量流水线（Verification Pipeline）

工程师完成上述修改后，必须在 Windows 宿主机完整执行并记录以下验证流水线：

```powershell
# 1. 执行五重纯原生静态硬门禁（全部必须 PASS，退出码 0）
cmake -P cmake/AuditSpdx.cmake
cmake -P cmake/AuditTypes.cmake
cmake -P cmake/AuditSloc.cmake
cmake -P cmake/AuditPrivacy.cmake

# 2. 编译、双模 PE 导入表硬审计与 147 项单测全绿
cmake --preset windows-msvc-ninja
cmake --build --preset windows-msvc-ninja --target CineRTX rtx_headless_cli rtx_tests
cmake -DTARGET_BIN=.vs/build/cmake/windows-msvc-ninja/CineRTX.exe -DMODE=GUI -P cmake/AuditImports.cmake
cmake -DTARGET_BIN=.vs/build/cmake/windows-msvc-ninja/rtx_headless_cli.exe -DMODE=HEADLESS -P cmake/AuditImports.cmake
.\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe

# 3. 重新封装并生成最终发布包
cmake -DBUILD_DIR=.vs/build/cmake/windows-msvc-ninja -P cmake/PackageRelease.cmake
```

---

## 四、 成果交付物与汇报规范

施工工程师完成后，必须提交 **`docs/phase12/82_CineRTX Phase 12 推送前全仓终审收口施工汇报.md`**，汇报内容须如实列出：
1. 涉及文件的 `git diff --stat` 精确改动统计；
2. 五重静态门禁的实际运行输出与退出码；
3. 双模 `AuditImports.cmake` 与 147 项单测的实测证据；
4. `PackageRelease.cmake` 重新生成的 `CineRTX-v0.4.0-windows-x64.zip` SHA-256 哈希值；
5. 郑重承诺：未触碰任何 C++ 业务逻辑源码，未私自执行 `git commit` 或 `git push`。

协调推进组在收到 82 号施工汇报后，将立即执行独立只读法医复验，并向用户呈请最终发版授权。
