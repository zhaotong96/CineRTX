# 53_CineRTX Phase 12 Wave 12.2 终局止血暨 Wave 12.3 系统 DLL 净空施工汇报

- **对应裁决与作战令**：
  - `docs/phase12/52_CineRTX Phase 12 Wave 12.2-D 单点编译止血暨Wave12.2全量大封板裁决书与Wave12.3作战令.md`（`CINERTX-P12-FINAL-SEAL-052`）
  - `docs/phase12/05_CineRTX Phase 12 SuperPower 施工单_13项系统DLL净空与核心界面解耦.md`（`CINERTX-P12-WORKORDER-005`）
- **关联委托书**：`docs/phase12/51_CineRTX Phase 12 Wave 12.2-D 终局类型达成与单点编译止血专家咨询委托书.md`（`CINERTX-P12-DELEGATION-051`）
- **编号说明**：因 `docs/phase12/50_CineRTX Phase 12 Wave 12.2-D 编译止血与终局类型收口施工汇报.md` 已归档并被 51/52 号文档正式引用，为保全历史审计链，本次施工汇报顺延编号落盘为 53 号原生 Markdown 文档。
- **施工日期**：2026-11-23
- **变更规模**：新增 2 个文件（`include/rtx/sys_dynload.h`、`cmake/AuditImports.cmake`），原位替换 10 个文件；全部 10 份旧版本已以 `*.w3bak` 迁出 `CineRTX/` 至外置归档目录 `work/_CineRTX_bak_archive/`，仓库内零 `.bak`/`.w3bak` 残留。

---

## 1. 全量门禁验收总表

| 门禁 | 指标项 | 52 号裁决书 / 05 号施工单目标 | 实测结果 | 判定 |
| --- | --- | --- | --- | --- |
| **Wave 12.2 终局止血 A/B** | `include/rtx/io.h:34-36` & `src/io/ffmpeg_io.cpp:995-1007` | 清除死前置声明 `class MemoryAvioContext;`；解构 `hdr.mastering` 10 元组 | **已完成**（全文件残余字段 = 0，花括号平衡 = 0） | **PASS** |
| **Gate 12.3-1（静态 IAT 源头清零）** | 11 项黑名单系统 DLL 静态调用与 CMake 链接 | `shell32`/`dwmapi`/`comdlg32`/`comctl32`/`uxtheme`/`d3d12`/`dxgi`/`ole32`/`kernelbase` 全仓零静态调用与零 CMake 链接 | **全部剥离并切至 System32 延迟解析**；`CineRTX` 仅链接 `gdi32 user32`（+基础内核/bcrypt）；`cmake/AuditImports.cmake` 已就位 | **源码/构建 PASS（PE IAT 待实机链接复核）** |
| **Gate 12.3-2（隐式链接与劫持免疫）** | `#pragma comment(lib)` & `LoadLibrary[AW]` | 全仓 0 / 0，统一 `LoadLibraryExW(..., LOAD_LIBRARY_SEARCH_SYSTEM32)` | **0 / 0**（已删除 `rtx_gui.cpp` 4 条 pragma 与 `ngx_remaster.cpp:65` 不安全回退） | **PASS** |
| **Gate 12.2 类型不反弹** | `AuditTypes` 合法具名类型上限 | `<= 35` 个，0 违规，硬阻断 | **34 / 35，Violations = 0**（`sys_dynload.h` 零新增具名类型） | **PASS** |
| **卫生与许可** | `std::get<` / `OldFormat` / `AuditSpdx` / 仓内 `.bak` | 全仓 0 / 0 / 0 缺失 / 0 残留 | **0 / 0 / 0 / 0** | **PASS** |
| **Pure SLOC** | `AuditSloc` 全仓纯代码行 | `<= 23,000`（诚实披露） | **30,354**（新增 `sys_dynload.h` + `AuditImports.cmake`，未做假压缩或功能阉割） | **未达 23,000（诚实披露，见 §4）** |
| **编译与审计边界** | Linux GCC 12 便携 TU 实编 / Windows MSVC 实机 | 便携 TU 0E 0W；Windows 61/61 + 147/147 PASS | **20 / 20 GCC 12 PASS（0E 0W）**；其余 10 个 Windows 文件完成全量静态/花括号审计 | **便携子集 PASS / 实机 MSVC 待复核** |

---

## 2. Wave 12.2-D 最后单点编译止血核验（Doc 52 §1）

1. **断环 A（`include/rtx/io.h:34-36`）**：物理删除残留的 `namespace io { class MemoryAvioContext; }` 死前置声明，彻底消除与 `src/io/memory_avio.h` 中 `using MemoryAvioContext = std::shared_ptr<MemoryAvioState>;` 的 `C2371` / `C2027` 冲突。
2. **断环 B（`src/io/ffmpeg_io.cpp:995-1007`）**：在 `FfmpegMuxer::applyHdrSideData` 中将 `const MasteringDisplay& m = hdr.mastering;` 改写为结构化绑定 `const auto& [m_rx, m_ry, m_gx, m_gy, m_bx, m_by, m_wx, m_wy, m_max, m_min] = hdr.mastering;`，并全文件二次扫描确认无其他遗留的 `MasteringDisplay` / `ContentLightLevel` / `ColorDescription` 旧字段访问。

---

## 3. Wave 12.3（13 项系统 DLL 净空与核心界面解耦）施工明细（Tasks W12.3-1 ~ W12.3-5）

### 3.1 新增零具名类型安全延迟加载头：`include/rtx/sys_dynload.h`
- 全部采用 `inline` 自由函数，**新增具名类型 = 0**，严格保持 `AuditTypes = 34/35`。
- 所有系统 DLL 加载统一经 `loadSystemDll(name)` 调用 `LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)`，免疫当前目录 DLL 劫持。
- 内置纯 C++ Windows 命令行分词器 `rtx::sys::parseCommandLineW(const wchar_t*)`（遵循 MSVC CRT 引号与反斜杠规则），在 CLI 与 GUI 中彻底取代 `shell32.dll!CommandLineToArgvW`。

### 3.2 逐项系统 DLL 净空落地清单
1. **`d3d12.dll` / `dxgi.dll`**：
   - `CMakeLists.txt`：从 `CineRTX` 与 `rtx_tests` 的 `target_link_libraries` 中移除 `d3d12` 与 `dxgi`。
   - `src/gui/preview_viewport.cpp`：`D3D12SerializeRootSignature` / `CreateDXGIFactory1` 切至 `rtx::sys::d3d12SerializeRootSignature` / `rtx::sys::createDxgiFactory1`。
   - `tests/test_raii_retirement.cpp`：`D3D12CreateDevice` / `D3D12SerializeRootSignature` 切至 `rtx::sys::d3d12CreateDevice` / `rtx::sys::d3d12SerializeRootSignature`。
   - `src/shim/ngx_bridge/d3d12_interop.cpp` 原已采用动态加载，无需重复改造。
2. **`ole32.dll`**：
   - `src/core/wasapi_audio_output.cpp`：`CoInitializeEx`、`CoUninitialize`、`CoCreateInstance`、`CoTaskMemFree` 全部切至 `rtx::sys::*` 动态解析，并复用 `FnDeleter<&rtx::sys::coUninitializeGuard>` 与 `FnDeleter<&rtx::sys::coTaskMemFreeVoid>` 保持零泄漏 RAII 契约。
3. **`comdlg32.dll`（物理拔除）**：
   - `src/gui/rtx_gui.cpp`：移除 `<commdlg.h>` 及全部 `GetOpenFileNameW` / `GetSaveFileNameW` 调用，改用基于动态 `ole32.dll` 的现代 COM `IFileOpenDialog` / `IFileSaveDialog`（`<shobjidl_core.h>` + `rtx::ComPtr` + `__uuidof(...)`，支持单选打开、保存与 `onQueueAdd` 多选）。
4. **`comctl32.dll` / `dwmapi.dll` / `uxtheme.dll` / `shell32.dll`**：
   - `src/gui/rtx_gui.cpp`：物理删除 `#pragma comment(lib, "comctl32.lib")`、`#pragma comment(lib, "uxtheme.lib")`、`#pragma comment(lib, "dwmapi.lib")` 与 Common-Controls 清单 `#pragma comment(linker, ...)`；`DwmSetWindowAttribute`、`DragAcceptFiles`/`DragQueryFileW`/`DragFinish`、`InitCommonControlsEx` 全部切至 `rtx::sys::*` 按需安全加载；`CommandLineToArgvW` 替换为纯 C++ `rtx::sys::parseCommandLineW`。
   - `src/gui/workstation_canvas.cpp`：移除 `<uxtheme.h>`，`SetWindowTheme` 切至 `rtx::sys::setWindowTheme`。
   - `src/cli/main.cpp`：移除 `<shellapi.h>`，`CommandLineToArgvW` 替换为纯 C++ `rtx::sys::parseCommandLineW`。
   - `src/core/ngx_remaster.cpp`：删除第 65 行不安全 `LoadLibraryW` 回退分支，严格保留 `LoadLibraryExW`。
   - `CMakeLists.txt`：`CineRTX` 链接库由 `shell32 dwmapi gdi32 user32 comdlg32 comctl32 uxtheme d3d12 dxgi` 收敛为 **`gdi32 user32`**；`rtx_tests` 收敛为 **`user32`**。
5. **新增 `cmake/AuditImports.cmake`（Task W12.3-5）**：
   - 支持 `dumpbin /imports` 及纯 Python PE Import Directory 解析双引擎；硬性拦截 11 项黑名单 DLL（含 `api-ms-win-*`），校验 `HEADLESS` 模式严格 `= 2` 项（`kernel32.dll`, `bcrypt.dll`）与 `GUI` 模式 `<= 4` 项（`kernel32.dll`, `user32.dll`, `gdi32.dll`, `bcrypt.dll`）。

---

## 4. Pure SLOC 诚实披露与实机复核步骤

- 当前 `AuditSloc` 实测 **30,354** 行（因 Wave 12.3 新增了 `include/rtx/sys_dynload.h` 与 `cmake/AuditImports.cmake` 两份基础设施文件，且未删减任何业务功能、147 项单测或语义注释）。待宿主机完成 Wave 12.3 静态 IAT 验收后，如仍需压降至 `<= 23,000`，建议通过物理拆分 `src/gui/` 或独立 `rtx_shim` 仓/子模块边界来达成。

### Windows 宿主机终局复核命令
```powershell
cmake --build --preset windows-msvc-ninja
.\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe
Push-Location [REDACTED_PATH]; .\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe; Pop-Location
cmake -P cmake/AuditTypes.cmake
cmake -P cmake/AuditSpdx.cmake
cmake -DTARGET_BIN=.vs/build/cmake/windows-msvc-ninja/CineRTX.exe -DMODE=GUI -P cmake/AuditImports.cmake
```
