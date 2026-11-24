# CineRTX Phase 12 全局调度与门禁检查清单总纲（协调负责人守门手册）

- **文档编号**：`CINERTX-P12-MASTER-CONTROL-007`  
- **归档路径**：`docs/phase12/07_CineRTX Phase 12 全局调度与门禁检查清单总纲.md`  
- **适用角色**：项目总协调负责人（Coordinator） / 架构治理守门人  
- **依据母本**：  
  - [`02_CineRTX Phase 12 极限现代 C++26 重构与无头 DLL 净空终审裁决书.md`](./02_CineRTX%20Phase%2012%20极限现代%20C++26%20重构与无头%20DLL%20净空终审裁决书.md)（`CINERTX-P12-RULING-002`）  
  - [`03_CineRTX Phase 12 SuperPower 施工单_构建净化与微观卫生清剿.md`](./03_CineRTX%20Phase%2012%20SuperPower%20施工单_构建净化与微观卫生清剿.md)（`CINERTX-P12-WORKORDER-003`）  
  - [`04_CineRTX Phase 12 SuperPower 施工单_自定义类型清剿与C++26标准库替换.md`](./04_CineRTX%20Phase%2012%20SuperPower%20施工单_自定义类型清剿与C++26标准库替换.md)（`CINERTX-P12-WORKORDER-004`）  
  - [`05_CineRTX Phase 12 SuperPower 施工单_13项系统DLL净空与核心界面解耦.md`](./05_CineRTX%20Phase%2012%20SuperPower%20施工单_13项系统DLL净空与核心界面解耦.md)（`CINERTX-P12-WORKORDER-005`）  
  - [`06_CineRTX Phase 12 SuperPower 施工单_全场景实机回归与v0.4.0封板验收.md`](./06_CineRTX%20Phase%2012%20SuperPower%20施工单_全场景实机回归与v0.4.0封板验收.md)（`CINERTX-P12-WORKORDER-006`）  
- **基线与目标**：`v0.3.0`（Commit `530a9a6`，Pure SLOC `33,143` 行，具名类型 `155` 个，系统 DLL `13` 项） \$\\rightarrow\$ **`v0.4.0`**（Pure SLOC `≤ 23,000` 行，具名类型 `≤ 35` 个，无头静态 IAT `= 2` 项 / 桌面静态 IAT `≤ 4` 项）

---

## 一、 协调负责人正交职责与防失控铁律

吸取 Phase 11 历史治理经验，协调负责人严格遵循“**调度派单、契约边界守门、自动化门禁穿透核验、物理真机复测**”四大核心职责，严禁盲目信任执行工程师自报“全部完成”的口头声明：

1. **串行垂直波次推进（Strict Serial Wave Gating）**：Wave 12.1 至 Wave 12.4 **严禁跨波次并行并发**。上一波次的全部硬性自动化门禁未落盘绿灯日志前，严禁派发下一波次施工单。  
2. **禁止古德哈特定律投机（Anti-Goodhart Enforcement）**：  
   - 严禁通过暴力剥离语义注释或合并代码为超长单行来压减行数（考核指标锁定为 `AuditSloc` 纯净有效代码行，且契约注释须 100% 完整）；  
   - 严禁使用 `using OldStruct = std::tuple<...>` 留存过渡类型别名以规避重构工作量；  
   - 严禁批量复制占位模板文案填充 `@rtx-type-justification`。  
3. **跨波次指标单调收敛不变量（Monotonic Hygiene Invariant）**：后续波次不得破坏前序波次已锁死的硬指标（例如 Wave 12.3 改写 DLL 延迟加载时，具名类型总数不得反弹突破 35 个，Pure SLOC 不得反弹突破 23,000 行）。  
4. **Git 写操作零越权**：所有施工过程日志与测试产物统一写入工作区外部证据目录（`$env:TEMP\CineRTX\evidence\phase12\`），未获用户明确书面指令前严禁执行 `git commit` 或 `git push`。

---

## 二、 Wave 12.1 至 12.4 派发拓扑与隔离边界总表

| 波次与施工单 | 依赖前提 | 承接角色 | 允许修改文件边界 | 严禁越界触碰区 | 预计工期 |
| :---- | :---- | :---- | :---- | :---- | :---: |
| **Wave 12.1** (`03号单`) | `v0.3.0` (`530a9a6`) 驱动 ≥ 621.24 WHQL | 系统内核与基础设施工程师 | `CMakeLists.txt`、`include/rtx/telemetry_bus.h`、`include/rtx/vram_scheduler.h`、`src/core/vram_scheduler.cpp`、`src/shim/loader/nv_encoder.cpp`、`src/shim/ngx_bridge/rife_engine.cpp`、`src/gui/rtx_gui.cpp`、`src/gui/workstation_canvas.*`、`tests/test_vram_scheduler.cpp` | `cmake/AuditTypes.cmake`、`cmake/AuditImports.cmake`、`schema/*` | 1.5–2 日 |
| **Wave 12.2** (`04号单`) | **Wave 12.1 全门禁 PASS** | 现代 C++26 架构重构工程师 | `cmake/AuditTypes.cmake`、`schema/*`、`include/rtx/*`、`src/core/*`、`src/io/*`、`src/cli/*`、`src/gui/*`、`src/shim/*`、`tests/*` | `cmake/AuditImports.cmake`（静态 IAT 规则留待 12.3 修改） | 2–3 日 |
| **Wave 12.3** (`05号单`) | **Wave 12.2 全门禁 PASS** | Windows 系统底层 ABI 与链接器工程师 | `CMakeLists.txt`、`cmake/AuditImports.cmake`、`src/core/*`、`src/io/*`、`src/cli/*`、`src/gui/*`、`src/shim/ngx_bridge/d3d12_interop.*`、`tests/*` | `schema/*`（禁止回退 12.2 的标准库替换成果） | 2 日 |
| **Wave 12.4** (`06号单`) | **Wave 12.3 全门禁 PASS** | 全栈测试与硬件实证工程师 | `CMakeLists.txt`（仅版本号 `0.4.0`）、`README.md`、`README_zh.md`、`webui/README.md`、`docs/archive/*`、`docs/phase12/08_*`（终审验收报告） | `include/`、`src/`（除版本号相关外原则上冻结源码改动） | 1.5–2 日 |

---

## 三、 四波次自动化卡点与命令行核验手册

协调负责人在每波次收单时，必须在干净的构建目录与项目根目录下亲自执行以下自动化检查命令并核对输出：

### 3.1 全波次通用基础门禁（每波次必检）

```
# 1. 离线构建与 /WX 零警告门禁（配置阶段严禁触发任何 git 网络子进程）
cmake --preset windows-msvc-ninja
cmake --build --preset windows-msvc-ninja

# 2. 双向单元测试门禁（构建目录 + 项目根目录双路径验证，防止相对路径硬编码）
.\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe
Push-Location [REDACTED_PATH]; .\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe; Pop-Location

# 3. 开源许可证边界隔离门禁（src/shim/ 必须 MIT OR Apache-2.0，其余 GPL-3.0-or-later）
cmake -P cmake/AuditSpdx.cmake
```

---

### 3.2 分波次专项自动化硬卡点清单

#### 阶段 A：Wave 12.1 放行卡点（`03号单` 收单验收）

- [ ] **卡点 12.1-A（零警告编译）**：`cmake --build` 输出 `0 Warning(s), 0 Error(s)`，且 `CMakeLists.txt` 全局编译选项中已彻底移除 `/wd4324` 与配置期 `git submodule update`。  
- [ ] **卡点 12.1-B（隐藏开关与全局状态清零扫描）**：

```
rg "std::getenv|_dupenv_s|GetEnvironmentVariable" include/ src/   # 必须 0 命中
rg "\bg_app\b" include/ src/                                      # 必须 0 命中
rg "CreateSolidBrush|DeleteObject" src/gui/                       # 必须 0 命中
```

- [ ] **卡点 12.1-C（运行时无回归）**：`rtx_tests.exe` 146/146 PASS；交互式桌面启动 `CineRTX.exe` 窗口创建、拖动、时间轴操作及退出无崩溃、无 UAF。

#### 阶段 B：Wave 12.2 放行卡点（`04号单` 收单验收）

- [ ] **卡点 12.2-A（具名自定义类型硬上限）**：

```
cmake -P cmake/AuditTypes.cmake
# 必须输出合法具名类型总数 <= 35 个（较 155 个基线清剿 >= 77%），0 违规，0 孤立契约块
```

- [ ] **卡点 12.2-B（Pure SLOC 瘦身硬上限）**：

```
cmake -P cmake/AuditSloc.cmake
# 必须实测 Pure SLOC <= 23,000 行（较 33,143 基线净瘦身 >= 30%）
```

- [ ] **卡点 12.2-C（旧式格式化与静态反射一致性）**：

```
rg "snprintf|_snwprintf|ostringstream" include/ src/   # 必须 0 命中
```

  - 核验 `schema/generated/` 产物与 `schema/schema_model.hpp` GCC 16 反射输出 100% 一致。

#### 阶段 C：Wave 12.3 放行卡点（`05号单` 收单验收）

- [ ] **卡点 12.3-A（源码零隐式链接与零不安全加载）**：

```
rg "#pragma\s+comment\s*\(\s*lib" include/ src/   # 必须 0 命中
rg "\bLoadLibrary[AW]\b" include/ src/             # 必须 0 命中（须用 LoadLibraryExW + LOAD_LIBRARY_SEARCH_SYSTEM32）
```

- [ ] **卡点 12.3-B（双形态静态导入表 IAT 终审）**：

```
dumpbin /imports .\.vs\build\cmake\windows-msvc-ninja\CineRTX.exe
cmake -DBUILD_DIR=.vs/build/cmake/windows-msvc-ninja -P cmake/AuditImports.cmake
```

  - **无头引擎核心目标**：静态 IAT 集合严格等于 `{kernel32.dll, bcrypt.dll}`（**2 项**）；  
  - **桌面混合发行目标（`CineRTX.exe`）**：静态 IAT 集合为 `{kernel32.dll, user32.dll, gdi32.dll, bcrypt.dll}` 的子集（**≤ 4 项**）；  
  - **静态黑名单 11 项 DLL**（`shell32`, `dwmapi`, `comdlg32`, `comctl32`, `uxtheme`, `d3d12`, `dxgi`, `ole32`, `kernelbase`, `advapi32`, `api-ms-win-*`）命中数严格为 **0**。  
- [ ] **卡点 12.3-C（无头冷启动验证）**：在无显卡/无头测试台架下执行纯 CLI 探测与参数校验，冷启动零缺库弹窗、零错误异常退出。

#### 阶段 D：Wave 12.4 最终封板卡点（`06号单` 终审与 `v0.4.0` 发布）

- [ ] **卡点 12.4-A（交互式桌面全链路压测）**：在 `quser` 确认 `Active` 且 `SessionId > 0` 环境下（RTX 5060 \+ Driver ≥ 621.24 WHQL），实测 4K AV1 HDR10 重制 `D2H == 0 B`、DirectComposition 视口 `presented > 0` 且连续 **100 次 Resize** 零崩溃、多段 `1V+1A` 硬切导出逐帧对齐、Debug 模式音频热循环堆分配次数 `== 0`。  
- [ ] **卡点 12.4-B（全局指标终审与文档齐备）**：  
  - `CineRTX.exe --version` 输出 `0.4.0`；  
  - `AuditTypes`（≤ 35）、`AuditSloc`（≤ 23,000）、`AuditImports`（2 项 / ≤ 4 项）、`AuditSpdx`、`rtx_tests.exe`（100% PASS）五门禁全绿；  
  - 终审验收报告归档至 `docs/phase12/08_CineRTX Phase 12 终审验收报告暨v0.4.0封板决议记录.md`，正式提请用户批准打标 **`v0.4.0`**。

---

## 四、 异常熔断与升级处置机制

当任一波次验收出现以下情形之一时，协调负责人立即启动**熔断拦截（HOLD & REJECT）**：

1. 发现执行工程师通过删除注释或拆分无意义文件伪造 `AuditSloc` 达标；  
2. 发现任何未在白名单内的自定义结构体或复制粘贴的虚假 `@rtx-type-justification` 声明；  
3. 发现 `dumpbin /imports` 出现任一黑名单系统 DLL 或第三方驱动动态链接符号泄漏；  
4. 发现交互式桌面压测出现 UAF、设备丢失未恢复、显存越界 OOM 或 `D2H > 0` 回读退化。

触发熔断后，协调负责人立即驳回当前交付包，锁定具体源码行号与门禁日志，责令执行工程师在当前波次隔离边界内原地清零后重新送审。