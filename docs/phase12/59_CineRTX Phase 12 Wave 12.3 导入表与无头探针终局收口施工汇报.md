# 59_CineRTX Phase 12 Wave 12.3 导入表与无头探针终局收口施工汇报

- **关联裁决**：`58_CineRTX Phase 12 Wave 12.3 导入表API Set合同冲突与纯无头门禁缺口终审裁决书暨Wave12.4启动令`（`CINERTX-P12-RULING-058`，全盘采纳方案 A）
- **关联委托与验收基线**：`57_CineRTX Phase 12 Wave 12.3 导入表 API Set 合同冲突与纯无头门禁缺口专家咨询委托书.md`、`06_CineRTX Phase 12 SuperPower 施工单_全场景实机回归与v0.4.0封板验收.md`
- **施工日期**：2026-11-24
- **施工状态**：三项收口补丁已全部完成原位替换，旧版文件已外置归档至 `work/_CineRTX_bak_archive/`（`.r58bak`），仓内 `.bak` 残留为 `0`；待 Windows MSVC 宿主机执行一次性实机复核即正式封板 Wave 12.3 并转入 Wave 12.4（v0.4.0 实机回归）。

---

## 一、施工改动清单（严格对齐裁决书 058）

| 补丁序号 | 目标文件 | 核心变更内容 | 约束核验 |
| :--- | :--- | :--- | :--- |
| **Patch 1** | `cmake/AuditImports.cmake` | 1. 将静态 PE IAT 拆分为 `_PHYSICAL_DLLS`（物理系统 DLL）与 `_APISET_DLLS`（`api-ms-win-*` 虚拟合同）双轨独立审计。<br>2. 保持 **10 项物理黑名单 DLL 零容忍**（`shell32.dll`, `dwmapi.dll`, `comdlg32.dll`, `comctl32.dll`, `uxtheme.dll`, `d3d12.dll`, `dxgi.dll`, `ole32.dll`, `kernelbase.dll`, `advapi32.dll`）。<br>3. `api-ms-win-*` 仅精确放行 MSVC STL C++20/26 `std::jthread`/`std::stop_token` 依赖的 **`api-ms-win-core-synch-l1-2-0.dll`**，其余任意 `api-ms-win-*` 一律强拦截。<br>4. 物理 DLL 数量独立校验：`HEADLESS == 2`（`{kernel32.dll, bcrypt.dll}`）、`GUI <= 4`（`{kernel32.dll, user32.dll, gdi32.dll, bcrypt.dll}` 子集）。<br>5. 使用 `string(JOIN ", " ...)` 替换原 `${_IMPORTS}` 分号直拼，彻底根治 CMake 列表分号展开导致的违规计数膨胀（2 -> 6）缺陷，并在 PASS/FAIL 输出中分别透明披露物理 DLL 与 STL API Set 清单。 | 控制流成对校验通过（`if/endif=14`, `foreach/endforeach=3`）；新增 `MODE` 合法性校验 |
| **Patch 2** | `CMakeLists.txt` | 在 `CineRTX` 目标块后新增纯控制台无头探针目标 **`rtx_headless_cli`**（别名 `rtx::headless_cli`）：<br>- 直接复用既有 CLI 源码：`src/cli/main_entry.cpp`、`src/cli/main.cpp`、`src/cli/shim_probe.cpp`（**0 新增业务代码行、0 新增命名类型**）。<br>- 仅链接 `rtx_core`、`rtx_project_options` 及 Win32 下的 `bcrypt`（非 Win32 链接 `${CMAKE_DL_LIBS}`），**零 `user32`/`gdi32` 链接、零 `src/gui` 源码与头文件引入**。<br>- 补齐 `RTX_VERSION_STRING="${PROJECT_VERSION}"` 宏定义与 `include/shim`、`src/cli` 私有包含路径，MSVC 启用 `/Gy`、`/SUBSYSTEM:CONSOLE /OPT:REF /OPT:ICF /INCREMENTAL:NO`，确保 `/std:c++latest /W4 /WX` 一次过编。 | 控制流成对校验通过（`if/endif=78`, `foreach/endforeach=4`）；静态核查 `src/cli/*` 无任何 `USER32`/`GDI32` 调用 |
| **Patch 3** | `cmake/AuditSloc.cmake` | 保留全仓规范源树统计口径，同时新增 **Per-Domain Pure SLOC 分域核算**（核心引擎 `include/`+`src/core/`+`src/io/`+`src/cli/` 对标 `<= 23,000` 目标、GUI 层 `src/gui/`、NVIDIA Shim `src/shim/`、单元测试 `tests/`、构建审计设施 `cmake/`+`CMakeLists.txt`），并新增可选 `CEILING_CORE_SLOC` 门禁参数。 | 控制流成对校验通过（`if/endif=9`, `foreach/endforeach=3`）；五域之和严格等于全仓 Pure SLOC |

---

## 二、静态自检与门禁基线核验

### 2.1 核心门禁状态（本轮施工后）

| 门禁项 | 结果 | 实测详情 |
| :--- | :---: | :--- |
| **`AuditTypes`（命名类型预算）** | **PASS** | **`34 / 35`（0 Violations）**，`rtx_headless_cli` 未引入任何新命名类型 |
| **`AuditSpdx`（双许可头）** | **PASS** | 缺口 `0`（本次仅修改 3 个已有规范 SPDX 头的 CMake 脚本） |
| **`OldFormat` / 旧式格式化清零** | **PASS** | 命中 `0`（目标 `0`） |
| **仓内 `.bak` 残留清零** | **PASS** | `CineRTX/` 仓内为 `0`；3 个旧版文件已外置归档至 `work/_CineRTX_bak_archive/*.r58bak` |

### 2.2 `AuditSloc.cmake` 双轨分域 SLOC 实测（沙箱镜像 136 规范文件）

> 注：Windows 宿主机工作区在 `cmake/` 目录下还包含 `AuditSpdx.cmake`、`EnsureRifeEngine.cmake`、`RtxProvision.cmake`、`VerifyNgxStaging.cmake` 等构建辅助脚本（57 号文档实测全仓 149 文件 / 30,967 Pure SLOC），其增量仅计入 `Build/Audit` 分域，**核心引擎 Pure SLOC 口径完全一致**。

| 统计域 | 文件数 | Pure SLOC | 对标目标 | 判定 |
| :--- | ---: | ---: | :--- | :---: |
| **Core Engine**（`include/` + `src/core/` + `src/io/` + `src/cli/`） | 72 | **12,401** | `<= 23,000` | **PASS**（余量 10,599 行） |
| **GUI Layer**（`src/gui/`） | 6 | 3,086 | 独立披露 | 透明在账 |
| **NVIDIA Shim / NGX Bridge**（`src/shim/`） | 32 | 8,186 | 独立披露 | 透明在账 |
| **Unit Tests**（`tests/`） | 22 | 5,648 | 独立披露 | 透明在账 |
| **Build / Audit Infra**（`cmake/` + `CMakeLists.txt`） | 4 | 1,127 | 独立披露 | 透明在账 |
| **Full-Repo Canonical Total（五域校验和）** | **136** | **30,448** | 对比 749fe0d 基线 31,064 | **净降 616 行** |

---

## 三、Windows 宿主机一键实机复核命令（Wave 12.3 终局封板验收）

请在 Windows MSVC 宿主机同步最新 3 个 CMake 文件后，执行以下命令完成 Wave 12.3 最终实机闭环：

```powershell
# 1. 重新配置并增量构建 CineRTX、rtx_headless_cli 与 rtx_tests
cmake --preset windows-msvc-relwithdebinfo
cmake --build --preset windows-msvc-relwithdebinfo --target CineRTX rtx_headless_cli rtx_tests

# 2. Gate 12.3-1 双模式静态 PE IAT 强门禁（预期双双 PASS）
cmake -DTARGET_BIN=build/windows-msvc-relwithdebinfo/RelWithDebInfo/CineRTX.exe -DMODE=GUI -P cmake/AuditImports.cmake
cmake -DTARGET_BIN=build/windows-msvc-relwithdebinfo/RelWithDebInfo/rtx_headless_cli.exe -DMODE=HEADLESS -P cmake/AuditImports.cmake

# 3. 验证 rtx_headless_cli 零窗口运行能力
.\build\windows-msvc-relwithdebinfo\RelWithDebInfo\rtx_headless_cli.exe --version

# 4. 复核双轨分域 SLOC、类型门禁与全量单测
cmake -P cmake/AuditSloc.cmake
.\build\windows-msvc-relwithdebinfo\RelWithDebInfo\rtx_tests.exe
```

**预期验收结果**：
1. `CineRTX.exe`（`GUI` 模式）：`PASS (GUI): 4 physical DLL(s) = [bcrypt.dll, gdi32.dll, kernel32.dll, user32.dll]; 1 STL API Set(s) = [api-ms-win-core-synch-l1-2-0.dll]`。
2. `rtx_headless_cli.exe`（`HEADLESS` 模式）：`PASS (HEADLESS): 2 physical DLL(s) = [bcrypt.dll, kernel32.dll]; 1 STL API Set(s) = [api-ms-win-core-synch-l1-2-0.dll]`。
3. `AuditSloc.cmake` 输出 `Core Engine: 12,401 -> PASS (<= 23000)`；`AuditTypes` 保持 `34/35 (0 Violations)`；`rtx_tests.exe` 保持 `147/147 PASS`。
4. 以上全部绿灯后，Wave 12.3 正式收口，可立即转入 **Wave 12.4**（依据 `06` 号文档执行全场景实机回归与 `v0.4.0` 封板验收）。
