# CineRTX Phase 11.3-S 全栈接线与真机闭环专家终审验收委托书

- **文档编号**：`CINERTX-P11-DELEGATION-045`
- **归档路径**：[`docs/phase11/45_CineRTX Phase 11.3-S 全栈接线与真机闭环专家终审验收委托书.md`](45_CineRTX%20Phase%2011.3-S%20全栈接线与真机闭环专家终审验收委托书.md)
- **呈报主体**：CineRTX 项目总协调负责人（Coordinator）
- **受呈报方**：CineRTX 首席领域专家组（Extreme Modern C++ Purist & Architecture Board）
- **当前状态**：🟢 **全栈接线与真机闭环完成，提请终审验收（COMPLETED & SUBMITTED FOR FINAL SEAL）**
- **执行基线**：Commit `749fe0d` (`feat(phase11.3): complete Day-0 micro-hygiene fixes, lock AuditSloc baseline, and land Work Order 36`)
- **依据工件**：
  * [`docs/phase11/42_CineRTX 11.3-R 终审验收与演进.md`](42_CineRTX%2011.3-R%20终审验收与演进.md)
  * [`docs/phase11/43_CineRTX Phase 11.3-S SuperPower 施工单_全栈接线与真机闭环.md`](43_CineRTX%20Phase%2011.3-S%20SuperPower%20施工单_全栈接线与真机闭环.md)
  * [`docs/phase11/44_CineRTX Phase 11.3-S 施工汇报_全栈接线与真机闭环.md`](44_CineRTX%20Phase%2011.3-S%20施工汇报_全栈接线与真机闭环.md)

---

## 一、 委托背景与演进定位

在 Phase 11.3-R 极限现代 C++ 重构中，全仓完成了 Standard-First 标准库大替换、`schema/` 外挂代码生成层彻底拔除、零虚表 `std::variant` NLE 模型与 AVX2+FMA 零堆分配混音总线构建。然而，首席领域专家组在 [`42 号终审裁决`](42_CineRTX%2011.3-R%20终审验收与演进.md) 中精准指出：虽然现代 C++ 底层算子已臻极境，但距离终端工业产品全栈闭环仍存在 **4 大产品级断链**（多段导出未接入 `RemasterPipeline`、音频变调未切流式 WSOLA、GUI 交互未接入 NLE、链接器死代码消除剥离风险），并做出了 `CONDITIONAL PASS / HOLD STRATEGIC SEAL` 的权威裁定。

为彻底消除产品断链，项目团队启动了 **Phase 11.3-S（SuperPower 全栈接线与真机闭环）** 短冲刺，严格按照 [`43 号施工单`](43_CineRTX%20Phase%2011.3-S%20SuperPower%20施工单_全栈接线与真机闭环.md) 确立的“阶段一纯 C++ 接线（无头高级工程师）+ 阶段二/三构建脱毒与真机实测（本地负责人）”正交协作架构，高质量打通了全栈产品闭环。

现所有断链整改、编译自愈、链接器符号留存、Debug 零分配物证与 RTX 5060 硬件转码实测已全部闭环，四大门禁全绿，135 项单元测试 100% 物理通过。

现正式呈报首席领域专家组，申请对 Phase 11.3-S 执行全栈合龙终审验收与战略封板。

---

## 二、 4 大产品级断链整改与交付物证

| 断链缺陷与要求 | 落地原语与实现机制 | 物理交付工件 | 闭环验证物证 |
| :--- | :--- | :--- | :--- |
| **断链 1：多段执行图与转码流水线脱节**<br>（42 号裁决 1.1 节） | 在 `hw_transcode_stages.cpp` 实现 `runNvidiaRemasterRenderGraph()`：维持**单 NVENC + 单 Muxer**，统一 90 kHz 时间基，按需执行跨素材 Seek 与动态参数重绑定；在切片跨素材硬切处按 C-05 契约强制插入 IDR 关键帧与重置时间域引用。 | [`src/core/hw_transcode_stages.cpp`](../../src/core/hw_transcode_stages.cpp)<br>[`include/rtx/hw_transcode.h`](../../include/rtx/hw_transcode.h) | 实机真实素材 58 帧硬件转码 `0 error`，`D2H == 0 B`，输出文件完整播放。 |
| **断链 2：离线资产静默黑帧与音轨缺失**<br>（42 号裁决 1.2 节） | 在 `render_graph.cpp` 实现离线资产返回 `ErrorCode::NotFound` 显式熔断报错（杜绝黑帧输出）；实现 `compileAudioRenderGraph()`，沿用扫描线统一提取 A1 音轨切片与线性增益。 | [`src/core/render_graph.cpp`](../../src/core/render_graph.cpp)<br>[`include/rtx/render_graph.h`](../../include/rtx/render_graph.h) | `nle_render_graph_sweep_line_hard_cut_idr_and_audio_slices` 单测 100% 覆盖。 |
| **断链 3：音频变速变调与 WSOLA 未对接**<br>（42 号裁决 1.3 节） | 落地 `WsolaStreamState` 环形缓冲状态机（跨帧历史保留与重叠相加）；在 `playback_session_audio.cpp` 中**彻底物理拔除** `applyLinearVarispeed`，变速回放完全经由 WSOLA，保全音高基频。 | [`src/core/wsola_stretch.cpp`](../../src/core/wsola_stretch.cpp)<br>[`src/core/playback_session_audio.cpp`](../../src/core/playback_session_audio.cpp) | 4 档速变频测试漂移 $< 0.05\%$；`audio_wsola_stream_pitch_drift_under_0_05_percent` PASS。 |
| **断链 4：GUI 交互未接入 NLE 撤销重做**<br>（42 号裁决 1.4 节） | 在 `workstation_canvas.cpp` 接入 `Ctrl+Z` (Undo)、`Ctrl+Y` (Redo)、`Delete` 快捷键，打通时间线 `(lane, x)` 双维命中测试；序列导出按钮触发 `onNleExportSequence()`，拉起多段执行图异步转码。 | [`src/gui/workstation_canvas.cpp`](../../src/gui/workstation_canvas.cpp)<br>[`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp) | GUI 交互单元测试通过，编辑操作支持双栈完整撤销重做。 |

---

## 三、 源码洁癖尾差清零与现场两项编译阻断自愈

### 3.1 源码洁癖尾差清零（S-1.5 & S-2.1）
1. **JSON 乱序与重复 ID 防御**：[`src/core/nle_project.cpp`](../../src/core/nle_project.cpp) 实现了键值乱序读取防御，拦截重复非零 ID，并在素材缩短时自动钳位轨道切片并记日志。
2. **`FormatMapping` 降维**：[`src/shim/ngx_bridge/d3d12_interop.cpp`](../../src/shim/ngx_bridge/d3d12_interop.cpp) 彻底清除结构体 `FormatMapping`，升级为 `constexpr std::array<std::tuple<...>, 3>`。
3. **C++23 回调现代化**：[`include/rtx/raii_owners.h`](../../include/rtx/raii_owners.h)、[`src/gui/d3d12_raii.h`](../../src/gui/d3d12_raii.h)、[`src/core/playback_session_impl.h`](../../src/core/playback_session_impl.h) 全量回调函数升格为 C++23 `std::move_only_function`。

### 3.2 现场抓获的两大致命低级失误自愈物证
无头工程师由于无本地编译器支持，提交代码中残留了 2 处严重编译阻断性错误，已被负责人在本地法医级审查中现场抓获并彻底自愈：
1. **`param_schema.cpp` 残留代码导致命名空间异常闭合**：
   - *问题*：第 37-47 行遗留废弃函数的残片代码（`return out; } ...`），游离花括号导致 `namespace rtx` 被意外提前闭合，引发后续 100+ 编译报错。
   - *自愈*：彻底物理拔除第 37-47 行残片，命名空间恢复纯净闭合。
2. **`test_audio_mixer_avx2.cpp` Win32 宏 `TRUE` 未声明**：
   - *问题*：第 44 行局部内存钩子 `scopedAllocHook` 盲写 `return TRUE;`，因未引入 `<windows.h>` 导致 MSVC Debug 编译彻底阻断。
   - *自愈*：将 `TRUE` 修正为标准整型字面量 `1`。

---

## 四、 负责人阶段二与阶段三宿主机真机闭环物理物证

### 4.1 构建系统深度脱毒（S-2.3 & S-2.4）
* **`CMakeLists.txt` 清理**：拔除第 266 行重复的 `RTX_EXTERNAL_DIR`；将 `/O1` 编译优化限定在 `CineRTX` 目标本身，不再全局篡改 `CMAKE_CXX_FLAGS_RELWITHDEBINFO`。
* **Debug 静态库冲突根除**：在 `CMakeLists.txt` 中支持自动识别并在 Debug 模式下动态绑定 `nvsdk_ngx_s_dbg.lib`，彻底解决了 MSVC 静态库 `/MT` 与 `/MTd` 的 `_ITERATOR_DEBUG_LEVEL` 链接冲突。
* **词法解析升级**：[`cmake/AuditTypes.cmake`](../../cmake/AuditTypes.cmake) 全面改用 `file(READ)` + 字符转义分行架构，彻底免疫注释内中括号与分号破坏。

### 4.2 MSVC Debug CRT 零堆分配物理物证（S-3.1）
在 MSVC 19.51 Debug 环境下编译并运行 `rtx_tests.exe --filter audio`，实测捕获控制台日志：
```text
[ RUN      ] audio_mixer_avx2_fma_and_zero_alloc_hot_loop
             [ALLOC-HOOK] avx2 hot-loop allocations: 0
[       OK ] audio_mixer_avx2_fma_and_zero_alloc_hot_loop
[ RUN      ] audio_wsola_stream_pitch_drift_under_0_05_percent
[       OK ] audio_wsola_stream_pitch_drift_under_0_05_percent
[ RUN      ] audio_wsola_stretch_mdspan_and_zero_alloc
             [ALLOC-HOOK] wsola stretch allocations: 0
[       OK ] audio_wsola_stretch_mdspan_and_zero_alloc
[==========] 7 test(s) run, 0 failed (177 ms)
```

### 4.3 链接器 Map 5 大核心符号留存确证（S-3.2）
检索 `.vs/build/cmake/windows-msvc-ninja/CineRTX.exe.map`，证实以下 5 大核心符号在开启 `/OPT:REF /OPT:ICF` 激进死代码消除下**100% 完整留存，零剥离**：
1. `?toJson@NleProject@rtx@@...`（行 4824）
2. `?fromJson@NleProject@rtx@@...`（行 4825）
3. `?compileRenderGraph@rtx@@...`（行 5112）
4. `?mixAvx2@rtx@@...`（行 47510）
5. `?wsolaProcessStream@rtx@@...` 与 `?wsolaStretch@rtx@@...`（行 47513, 47516）

### 4.4 物理 RTX 5060 硬件转码实测闭环（S-3.3）
在宿主机 RTX 5060 Laptop GPU（Driver 621.24 WHQL）上对真实素材执行硬件转码实测：
```json
{
  "backend": "nvidia",
  "device": "NVIDIA GeForce RTX 5060 Laptop GPU",
  "input_codec": "h264",
  "output_codec": "hevc",
  "output_bit_depth": 10,
  "frames_decoded": 25,
  "frames_encoded": 58,
  "decoder_device_to_host_bytes": 0,
  "decoder_host_pixel_copies": 0,
  "encoder_host_to_device_bytes": 0,
  "encoder_host_pixel_copies": 0,
  "zero_copy_main_loop": true,
  "elapsed_ms": 3121.7032
}
```
* **硬件直通**：NVDEC $\rightarrow$ Pack Kernel $\rightarrow$ NVENC 全程显存闭环，**`D2H == 0 B`**，主机内存零拷贝。
* **原生播放**：`playback_session_native_decode_h264_hevc_av1` 实测 3 种格式物理解码输出，`decoded=25 rendered=25 dropped=0 audio=1` 零丢帧。

---

## 五、 四大门禁与 CTest 验收数据汇总

```text
-- [AuditTypes] OK: 153 named types, all justified (Std-First law)
-- [AuditSloc] Canonical Files: 141 | Pure SLOC: 32317 (<= 39626) | Comments: 4900
-- [AuditSpdx] PASS (GPL=107 MIT=37)
-- [AuditImports] PASS: CineRTX.exe (5217792 bytes), zero leaked dynamic media/driver DLLs.
-- [CTest] 100% tests passed, 0 tests failed out of 1 (47.12 sec, 135 tests)
```

四大门禁数据相比 Phase 11.3 入场基线全面合规：
1. **AuditTypes**：153 个具名类型全部具备 `@rtx-type-justification` 契约，0 违规；
2. **AuditSloc**：纯代码行 32,317 行（对比历史峰值 38,562 行净减少 6,245 行，远超预算）；受保护契约注释 4,900 行；
3. **AuditSpdx**：107 项 GPL + 37 项 MIT，全量通过；
4. **AuditImports**：5.21 MB 二进制包络，无任何泄漏的动态媒体库或外部运行时 DLL；
5. **CTest**：全量 135/135 项单元测试 100% 物理通过（0 失败）。

---

## 六、 终审请示与战略封板提请

鉴于 Phase 11.3-S 已将 42 号终审裁决所列之 4 大产品级断链彻底整改完毕，所有现代 C++23 语法与流水线在宿主机真实软硬件环境下均已取得可重现之确定性物证，项目总协调负责人谨代表项目团队提请首席领域专家组：

1. **核准验收 Phase 11.3-S 全栈接线与 NLE 合龙交付成果**；
2. **正式批准签署 Phase 11.3 全栈 NLE 与现代 C++ 战略封板裁决书（GRANT STRATEGIC SEAL）**；
3. **批准推进下一阶段演进工作（Phase 11.4 / v0.3.0 RIFE 算子与 SDK 13.1 分级封板）**。
