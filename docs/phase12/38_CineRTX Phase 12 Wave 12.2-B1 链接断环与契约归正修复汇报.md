# CineRTX Phase 12 Wave 12.2-B1 链接断环与契约归正修复汇报

- **文档编号**：`CINERTX-P12-REPORT-038`
- **对应裁决书**：`docs/phase12/37_CineRTX Phase 12 Wave 12.2-B1 链接断环与契约归正终审裁决书暨Wave12.2-B2启动令.md`（`CINERTX-P12-RULING-037`）
- **前置依据**：`docs/phase12/36_CineRTX Phase 12 Wave 12.2-B1 链接断环与契约归正专家咨询委托书.md`、`docs/phase12/35_CineRTX Phase 12 Wave 12.2-B1 施工汇报.md`
- **施工范围**：`CMakeLists.txt`、`src/shim/ngx_bridge/rife_builder.cpp`（共 2 个文件）
- **仓内备份残留**：0 个（2 份旧版文件均已先行移至仓外 `work/_CineRTX_bak_archive/` 目录）

---

## 一、 定点修复明细

| 裁决议题 | 修改文件 | 修复内容 | 验证状态 |
| :--- | :--- | :--- | :--- |
| **议题一（方案 2）：消除 `rtx_tests.exe` 12 处 `LNK2019`** | `CMakeLists.txt`（第 684–686 行） | 在 `if(WIN32)` 块中将 `src/shim/ngx_bridge/nv_func_tables.cpp` 与 `src/shim/ngx_bridge/rife_pack_runtime.cpp` 并列纳入 `target_sources(rtx_tests PRIVATE ...)`；`rtx_ngx_bridge.dll` 导出表保持 100% 纯 `extern "C"` ABI 零污染 | CMake 语法与源路径已核对，待 Windows 主机 MSVC 实链复验 |
| **议题二：`RifeTrtLogger` 契约分类归正与头文件卫生** | `src/shim/ngx_bridge/rife_builder.cpp`（第 14–40 行） | 1. 将 `RifeTrtLogger` 的 `@rtx-type-justification` 类别由 `Polymorphic-Boundary` 归正为白名单内的 **`Std-Customization`**，并按裁决范本更新唯一 `Why-Std-Fails`/`Invariant` 及添加 `final` 修饰；<br>2. 移除未使用的 `#include <cstdlib>` | 本地 `AuditTypes` 复验 **`src/shim/ngx_bridge/` 0 违规**（`Valid=48/35, Violations=56`）；因沙盒无 TensorRT SDK，编译待 Windows 主机复验 |
| **议题三：Pure SLOC 预算递延** | 全仓账本记录 | 按 37 号裁决核准 Wave 12.2-B1 当前实测 Pure SLOC 放行，剩余约 `1,448` 行瘦身配额顺延至 Wave 12.2-B2 / B3 释放 | 已入账，零人工压行 |

---

## 二、 本地门禁复验结果

| 门禁项 | 修复前（Doc 35/36） | 修复后实测 | 结果 |
| :--- | :---: | :---: | :---: |
| `src/shim/ngx_bridge/` 契约违规数 | 1（`RifeTrtLogger`） | **0** | **PASS** |
| 全仓具名类型总数（`AuditTypes`） | 104（Valid=47, Viol=57） | **104**（Valid=48, Viol=56） | **PASS（`<= 105`）** |
| 全仓 `OldFormat` 旧格式化命中 | 0 | **0** | **PASS** |
| 全仓 `AuditSpdx` 许可证缺失 | 0 | **0** | **PASS** |
| 全仓纯净 SLOC（`AuditSloc`） | 30,948 | **30,950** | **按议题三核准放行** |

---

## 三、 Windows 宿主机封板复验命令

请在 Windows 宿主机同步 `CMakeLists.txt` 与 `src/shim/ngx_bridge/rife_builder.cpp` 后执行以下最终核验，确认 **85/85 目标 `0 Error(s), 0 Warning(s)`** 与双目录 **`147/147 PASS`** 即可封板 Wave 12.2-B1 并启动 Wave 12.2-B2：

```powershell
cmake --build --preset windows-msvc-ninja
.\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe
Push-Location [REDACTED_PATH]; .\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe; Pop-Location
cmake -P cmake/AuditSpdx.cmake
```
