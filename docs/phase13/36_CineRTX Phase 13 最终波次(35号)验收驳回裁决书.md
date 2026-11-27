# 36_CineRTX Phase 13 最终波次（35号）验收驳回裁决书

- **裁决编号**：`CINERTX-P13-RULING-036`
- **裁决主体**：CineRTX 项目总协调负责人（Coordinator）
- **被审汇报**：[`35_CineRTX Phase 13 残余双单测清零与NVOFA真像素切镜返工汇报.md`](./35_CineRTX%20Phase%2013%20残余双单测清零与NVOFA真像素切镜返工汇报.md)
- **依据红线**：`[DEC-003]`、`[DEC-004]`、`[DEC-008]`、`[ISSUE-007]`
- **终审裁决**：🔴 **验收驳回（REJECTED FOR REWORK）—— `rtx_ngx_bridge.dll` 编译硬失败阻断全工程链接与单测回归；且 `32`–`34` 号深水区仍完全未实装。**

---

## 一、 宿主机真机实测结果（Windows 11 + MSVC 19.51 + RTX 5060）

| 核验维度 | 35 号汇报声称 | 宿主机实测结果 | 判定 |
| :--- | :--- | :--- | :---: |
| **`.\build.bat` 全量编译** | 标记待宿主机验证 | **`rtx_ngx_bridge.dll` `[4/13]` 编译硬失败**：`src/shim/ngx_bridge/bridge_exports.cpp` 第 707、716、717、734 行调用 `rtx_ngx::nv_cuda::loaded()`、`memcpyDtoH()`、`memcpyDtoDAsync()`，因未包含声明头文件（`nv_func_tables.h`）报 `error C3083` / `C2039` / `C3861` 导致全线构建中止。 | **FAIL (P0)** |
| **`rtx_tests.exe` / `AuditImports`** | 声称双单测待宿主机确认 150/150 | 因 `rtx_ngx_bridge.dll` 编译失败，`rtx_tests.exe` 与 `CineRTX.exe` 未能重新链接，无法完成 150/150 及双模 IAT 实测。 | **BLOCKED (P0)** |
| **四重静态门禁** | `AuditTypes 34/35`、`AuditPrivacy 0` | • `AuditPrivacy.cmake`：**`PASS: 0 violations across 429 file(s)`**<br>• `AuditTypes.cmake`：**`OK: 34/35`**<br>• `AuditSpdx.cmake`：**`PASS (GPL=114 MIT=39)`**<br>• `AuditSloc.cmake`：**Core Engine = `12,385`（`<= 23,000`），Full Repo = `31,728`** | **PASS** |

---

## 二、 31–34 号施工单兑现核查

1. **31 号施工单（P0 双单测 + NVOFA 真像素切镜 + WebUI 帧源）**：
   - `test_nle_vertical_slice.cpp` 与 `test_playback_session.cpp` 已修正 1001 时基字面量，但由于 `bridge_exports.cpp` 编译失败未能实跑确认 150/150；
   - `bridge_exports.cpp:707-734` 缺少 `nv_func_tables.h` 引入即盲写 `rtx_ngx::nv_cuda::*` 导致编译失败；同时 `31` 号要求的“C++ 后端向临时目录落盘 WebUI 预览帧”仍未实装（`webui/server.py` 依然只能读到空/204）。
2. **32–34 号施工单（SVT-AV1/x265 静态编联实测、60fps AI 增强播放器、D2D1/DirectWrite 组件库与可拖拽 DAG 画布）**：
   - 与 35 号汇报第三节披露一致，本轮仍未交付。

---

## 三、 返工指令

1. **立即修复 `bridge_exports.cpp` 编译阻断**（补全 `#include "nv_func_tables.h"` 或改用既有 `bridge->interop` 设备方法，确保 `.\build.bat` 退出码 `0`），并实跑 `rtx_tests.exe` 达成 **`150/150 PASS`** 与双模 `AuditImports.cmake` **`PASS`**。
2. **按 `31`（WebUI 帧源写入）→ `32` → `33` → `34` 号施工单补齐全部未实装模块**，禁止再提交未经过 MSVC 编译的桥接层代码。
