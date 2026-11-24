# 56_CineRTX Phase 12 Wave 12.3 定点止血与导入表门禁收口施工汇报

- **对应终审裁决书**：`docs/phase12/55_CineRTX Phase 12 Wave 12.3 动态加载断环与导入表审计终审裁决书暨封板授权令.md`（`CINERTX-P12-FINAL-SEAL-055`）
- **关联委托书**：`docs/phase12/54_CineRTX Phase 12 Wave 12.3 动态加载断环与导入表审计专家咨询委托书.md`（`CINERTX-P12-DELEGATION-054`）
- **施工日期**：2026-11-24
- **变更规模**：原位替换 2 个源码文件（`include/rtx/sys_dynload.h`、`src/gui/rtx_gui.cpp`）并清理 1 个重名旧版审计脚本副本（旧版 2,630 字节 `cmake/AuditImports.cmake`）；全部 3 份旧文件均已迁出 `CineRTX/` 至外置归档目录 `work/_CineRTX_bak_archive/`，仓库内零 `.bak` 与 `(1)` 冲突副本残留。

---

## 1. 三项定点收敛修复明细（方案 A / 方案 B / 方案 C）

| 修复项 | 目标文件 | 根因与修复措施 | 验证结果 |
| --- | --- | --- | --- |
| **方案 A** | `include/rtx/sys_dynload.h`（第 25–26 行） | `WIN32_LEAN_AND_MEAN` 导致 `<windows.h>` 跳过 COM 基础头，引发 `LPUNKNOWN` 未定义（`C2061`/`C2065`）；已紧随 `#include <windows.h>` 显式补齐 `#include <unknwn.h>`（不引入重型 `<ole2.h>` 或任何静态库指令） | **PASS**（全文件 COM/Win32 基础类型全覆盖，0 新增具名类型） |
| **方案 B** | `src/gui/rtx_gui.cpp`（第 797 行） | `WM_APP_PROGRESS` 分支仍对已元组化的 `EncoderJob` 使用 `snapshots[i].status`（`C2039`）；已替换为 `rtx::encoderJobStatus(snapshots[i]) == rtx::JobStatus::Running`，并对 `rtx_gui.cpp` 全文复查所有旧字段名 | **PASS**（全文残余 `EncoderJob` 点访问 = **0**） |
| **方案 C** | `cmake/AuditImports.cmake` | 物理迁出 `cmake/` 目录下历史遗留的 2,630 字节旧版 `AuditImports.cmake` 至外置归档目录，仅保留唯一正式的 5,014 字节新版（支持 `dumpbin /imports` + 纯 Python PE Import Directory 双解析引擎、11 项黑名单零容忍拦截、无头 `= 2` 与 GUI `<= 4` 硬校验） | **PASS**（`cmake/` 内仅存唯一 5,014 字节正式版） |

---

## 2. 全仓门禁静态复核总表

| 门禁 | 实测结果 | 判定 |
| --- | --- | --- |
| **`AuditTypes`（具名类型终局硬上限）** | **34 / 35**，**Violations = 0**，`RTX_AUDIT_TYPES_STRICT = ON` | **PASS** |
| **`AuditSpdx`（许可证隔离）** | 缺失 = **0** | **PASS** |
| **现代 C++23/26 与安全加载卫生** | `std::get<` = **0**、`OldFormat` = **0**、`#pragma comment(lib)` = **0**、`LoadLibrary[AW]` = **0**、仓内 `.bak` = **0** | **PASS** |
| **Windows 宿主机终局构建与 PE IAT 复核** | 待宿主机执行 61/61 目标编译、双目录 147/147 单测与 `AuditImports.cmake` 实机验收 | **待宿主机一键收网** |

---

## 3. Windows 宿主机一键收网与双波次封板核验命令

拉取同步最新仓库（确认本地无残留 `cmake/AuditImports (1).cmake`）后依次执行：

```powershell
# 1. 全仓 MSVC /std:c++latest /W4 /WX 零警告编译与链接（61/61 目标）
cmake --build --preset windows-msvc-ninja

# 2. 双目录全量单元测试闭环（147/147 PASS）
.\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe
Push-Location [REDACTED_PATH]; .\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe; Pop-Location

# 3. 双形态 PE 静态导入表（IAT）硬门禁审计
dumpbin /nologo /imports .\.vs\build\cmake\windows-msvc-ninja\CineRTX.exe
cmake -DTARGET_BIN=.vs/build/cmake/windows-msvc-ninja/CineRTX.exe -DMODE=GUI -P cmake/AuditImports.cmake

# 4. 具名类型硬上限（34/35）与许可证隔离门禁
cmake -P cmake/AuditTypes.cmake
cmake -P cmake/AuditSpdx.cmake
```

上述四步全绿后，即可依据 55 号终审裁决书直接签署 Wave 12.2 暨 Wave 12.3 双波次封板并创建本地 Git 基线快照（严禁 `git push`），进入 Wave 12.4 实机回归与 `v0.4.0` 发布验收。
