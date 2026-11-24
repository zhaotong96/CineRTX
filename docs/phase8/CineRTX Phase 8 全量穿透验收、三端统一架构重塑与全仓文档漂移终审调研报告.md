# CineRTX Phase 8 全量穿透验收、三端统一架构重塑与全仓文档漂移终审调研报告

**文档标识**：`DOC-VERDICT-PHASE8-ACCEPTANCE-UNIFIED-ARCH-DRIFT-001`  
**审计日期**：2026-11-13  
**审计基线**：`[REPO_ROOT]` 最新同步快照（含 Phase 8 返工、TensorRT RIFE 路线 B 落地与 `webui/` 新增目录）  
**起草专家**：CineRTX 首席系统架构与多媒体工程审计专家  
**关联基准文档**：
- [`docs/phase8/CineRTX_Phase8_TensorRT_RIFE补帧施工方案.md`](CineRTX_Phase8_TensorRT_RIFE补帧施工方案.md)（`DOC-PHASE8-TRT-RIFE-CONSTRUCTION-002`）
- `docs/phase8/CineRTX_Phase8_返工交付验收实证报告.md`（`TASK-PHASE8-REWORK-ZERO-CONFIG-60FPS-SLIMMING-001`）
- `docs/phase8/CineRTX_文档实现漂移审计报告_20261113.md`
- [`webui/README.md`](../../webui/README.md) 与 `webui/ACCEPTANCE.md`

---

## 执行摘要与核心裁决（Executive Summary）

针对最新同步的 `CineRTX` 代码库与文档树，本报告围绕委托人提出的三大核心命题进行了逐行穿透式代码取证与架构推演，得出以下**核心铁证与终审裁决**：

| 专题任务 | 终审裁决结论 | 核心根因与代码级铁证摘要 |
| :--- | :---: | :--- |
| **专题一：Phase 8 全量验收与“NVOFA/RIFE 同为 86 FPS”真相取证** | **不通过（REJECT / 需紧急返工）**<br>86 FPS **绝非**证实 RIFE 生效，而是证实 **RIFE 发生了 100% 静默回退**，且连 **NVOFA 路线 A 本身也是“伪光流”**！ | 1. **RIFE 静默回退真相**：在 [`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp)（第 788–801 行）中，当 `initRife()` 因缺少 `.engine` 文件或未部署 `nvinfer_10.dll` 返回失败时，`else` 分支**零日志、零告警、零弹窗**地将 `rife_active` 置为 `false`、将 `report.fruc_backend` 篡改为 `"nvof"`，直接回退至 `interpolateNv12` 分支！因此两次测试跑的是**同一条代码路径**（耗时完全由 4K VSR Level 4 + TrueHDR 的 `~11.6 ms/帧 = 86 FPS` 决定）。<br>2. **NVOFA 路线 A 造假真相**：在 [`src/shim/ngx_bridge/p010_pack_runtime.cpp`](../../src/shim/ngx_bridge/p010_pack_runtime.cpp)（第 447–502 行）中，所谓的 `rtx_interpolate_nv12` **全仓 0 处加载 `nvofapi64.dll`、0 处调用硬件光流加速器**，而是用 50 行 PTX 汇编对前后两帧同坐标像素做**简单线性透明度混合（`out = (1-alpha)*prev + alpha*curr` 叠影混帧）**，以此欺骗通过了“相邻帧 SAD > 0”门禁！<br>3. **RIFE 实现侧 6 处阻断缺陷**：即便手动备齐 `.engine` 与 TensorRT DLL，当前 [`src/shim/ngx_bridge/rife_engine.cpp`](../../src/shim/ngx_bridge/rife_engine.cpp) 与 [`src/shim/ngx_bridge/rife_pack_runtime.cpp`](../../src/shim/ngx_bridge/rife_pack_runtime.cpp) 仍存在 **CUDA Context 跨上下文错位**、`interp_buffer` **Pitch 越界踩内存**、**32 vs 64 填充对齐分裂**、**I/O 张量硬编码 FP32**、**207 万次/帧全局原子加争用**及**违反 RAII 残留 `destroy()`** 六大缺陷。 |
| **专题二：Win32 GUI + CLI + WebUI 三端统一架构重塑** | **架构重塑方案定型**<br>确立 **C++26 单一元数据契约表 (`kParamSchema`) + 中心化会话控制器 (`EngineController`) + 原生 `--session-stdio` / `--schema-json` 协议** | 1. **现状诊断**：三端已出现 **8 处行为/默认值漂移**（如 QP 默认值 CLI `0` vs GUI `24` vs WebUI `26`；GUI 峰值亮度输入框 `0x0` 隐形且硬编码 `1000`；WebUI 取消任务用 `terminate()` 强杀进程导致输出 MP4 尾部损坏）与 **4 层全量重复代码**（参数校验写 4 遍、64 条 i18n 词条在 C++ 与 JS 写 2 遍、Python 桥用 10 条正则刮削控制台文本）。<br>2. **重塑设计**：严格遵循 **SOLID 六大准则 + 迪米特法则 + KISS + 奥卡姆剃刀**，在 `rtx_core` 建立唯一 `constexpr kParamSchema` 与 `TranscodeProfile` SSOT，由 `CineRTX.exe --schema-json` 动态导出 schema 与双语词条给 WebUI，由 `CineRTX.exe --session-stdio` 提供原生 JSON 事件流与 `stdin` 协作取消，彻底消灭 `server.py` 中的正则刮削、参数拼装与校验重复（`server.py` 从 892 行压缩至 ≤ 180 行纯字节转发薄层，`app.js` 砍除 260 行硬编码词条表）。 |
| **专题三：全仓文档过时、实现漂移与冗余内容二次终审** | **查出 12 项重大漏检项（C1–C12）**<br>含 **2 项 P0 级阻断漏检**、**5 项 P1 级契约漏检**与 **5 类无意义冗余残留** | 工程师辅助排查的 `docs/phase8/CineRTX_文档实现漂移审计报告_20261113.md` 虽找出了 58 项历史文档漂移，但漏掉了最致命的源码级与跨端漂移：<br>1. **P0 隐私/脱敏破防（C1）**：[`src/shim/ngx_bridge/trt_loader.cpp`](../../src/shim/ngx_bridge/trt_loader.cpp):55 硬编码了未掩码本机路径 `[REDACTED_HOST]`；<br>2. **P0 伪光流报告失真（C2）**与 **P1 RIFE 施工方案 7 处漂移（C3）**；<br>3. **P1 GUI 第 13 项参数 `peak_nits` 隐形失效（C4）**与 **打包脚本 `package_release.py:121` 写入幽灵 `--truehdr`（C5）**；<br>4. **P2 WebUI 文件浏览器 UTF-8 乱码 `ðŸ“ `（C7）**及 **5 类冗余文件/重复代码残留（C8–C12）**。 |

---

## 第一部分：Phase 8 全量穿透验收与“86 FPS 同速”真相取证（专题一）

### 1.1 核心疑问直接裁决：为什么实测 NVOFA 与 RIFE 编码速率同为 86 FPS？

> **用户实测疑问**：“我实测用 NVOFA 和 RIFE 编码速率一样都是 86 帧每秒，证实 RIFE 链路已生效？”

**专家取证裁决**：**完全相反！86 FPS 的同速现象不仅不能证明 RIFE 链路生效，反而从代码与硬件物理定律两个维度铁证了：实测中 RIFE 发生了 100% 静默初始化失败，被无声降级到了与 NVOFA 完全相同的代码分支；而且当前的 NVOFA 分支本身也是没有调用硬件光流的“伪插帧（线性叠影混帧）”！**

#### （1）硬件吞吐率物理账本验算（RTX 5060 Laptop GPU `sm_120`）
在您的开发主力机（AMD Ryzen 9 8945HX + NVIDIA GeForce RTX 5060 Laptop 8GB GPU）上，执行 1080p/720p $\to$ 4K UHD (3840×2160) 60fps 重制转码时，各阶段的真实 GPU 物理耗时账本如下：

| 管线阶段 | 路线 A（当前实装的 `interpolateNv12` 线性混帧） | 路线 A（真实 NVOFA `nvofapi64.dll` 双向硬件光流） | 路线 B（真实 TensorRT 10.13 FP16+FP32 RIFE v4.25 `IFNet_HDv3`） |
| :--- | :---: | :---: | :---: |
| **NVDEC 硬件解码** | ~0.3 ms（硬件异步） | ~0.3 ms（硬件异步） | ~0.3 ms（硬件异步） |
| **插帧算子耗时（折算至每输出帧，24$\to$60fps 含 60% 插值帧）** | **~0.05 ms**<br>（仅 50 行 PTX 像素线性加权） | **~0.8 – 1.2 ms**<br>（独立 OFA 硬件单元 + 双向 Warp 核） | **~5.5 – 8.5 ms**<br>（单次 1080p RIFE 推理约 9–14 ms × 60% 插值帧占比，含 FP32 `GridSample`） |
| **NGX VSR Level 4 + TrueHDR + PackV2（4K）** | **~11.5 ms** | **~11.5 ms** | **~11.5 ms**<br>（与 RIFE 共享同一 `pack_stream` 串行排队） |
| **NVENC AV1 10-bit P5 编码（异步线程）** | ~2.5 ms（与主线程重叠） | ~2.5 ms（与主线程重叠） | ~2.5 ms（与主线程重叠） |
| **单帧主循环总耗时与理论帧率上限** | **~11.6 ms $\Rightarrow$ ~86 FPS** | **~12.5 ms $\Rightarrow$ ~80 FPS** | **~17.5 – 20.0 ms $\Rightarrow$ ~50 – 57 FPS** |

- **物理结论**：在当前 [`src/shim/ngx_bridge/bridge_exports.cpp`](../../src/shim/ngx_bridge/bridge_exports.cpp)（第 919 行）中，RIFE 推理被直接发射在 `impl->interop->pack_stream()` 上，与 NGX VSR + TrueHDR 完全串行共用同一个 CUDA Stream 和 `remaster_mutex` 互斥锁。如果 RIFE v4.25 神经网络真正执行了 `enqueueV3`，全链路帧率必然从 **86 FPS** 回落至 **50–57 FPS** 区间，绝不可能与 0.05 ms 的简单核函数跑出完全一致的 **86.0 FPS**！

---

#### （2）代码级因果链完整还原：RIFE 是如何一步步静默失效并伪装成成功的？

通过穿透追踪 [`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp) $\to$ [`src/cli/main.cpp`](../../src/cli/main.cpp) $\to$ [`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp) $\to$ [`src/shim/ngx_bridge/bridge_exports.cpp`](../../src/shim/ngx_bridge/bridge_exports.cpp) $\to$ [`src/shim/ngx_bridge/rife_engine.cpp`](../../src/shim/ngx_bridge/rife_engine.cpp)，我们完整还原了导致实测 RIFE 100% 静默回退的 **四步断链**：

1. **第一步（入口断链：GUI 无 `.engine` 选择器且默认路径与施工方案不一致）**：
   - 在 [`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp)（第 369–386 行）中，当用户在下拉框选中 `60 FPS (RIFE 深度学习 - TensorRT)`（`fpsSel == 3`）或 `120 FPS (RIFE)`（`fpsSel == 4`）时：
     ```cpp
     } else if (fpsSel == 3) {
       opt.fruc_enabled = true;
       opt.target_fps = rtx::Rational{60, 1};
       std::filesystem::path default_engine = "models/rife_v4.25.engine";
       if (!std::filesystem::exists(default_engine)) {
         default_engine = "rife.engine";
       }
       opt.rife_engine_path = default_engine.string();
     }
     ```
   - 违背了 [`docs/phase8/CineRTX_Phase8_TensorRT_RIFE补帧施工方案.md`](CineRTX_Phase8_TensorRT_RIFE补帧施工方案.md) §4.2“弹出文件选择对话框指定 `.engine` 路径”的要求。若工作目录下没有名为 `models/rife_v4.25.engine` 或 `rife.engine` 的文件（注意施工方案 §3.3 与 §7 门禁 2/5 命名的文件是 `models/rife_v4.25_trt101303_sm120_fp16.engine`），`opt.rife_engine_path` 就会指向一个根本不存在的 `"rife.engine"`！

2. **第二步（加载断链：`CMakeLists.txt` 未部署 TensorRT 运行时 DLL）**：
   - 在 [`CMakeLists.txt`](../../CMakeLists.txt)（第 294–303、590–645 行）中，构建脚本仅复制了 `rtx_ngx_bridge.dll`、`nvngx_vsr.dll` 和 `nvngx_truehdr.dll`，**既没有自动解压 `build/external/TensorRT-10.13.3.9.Windows.win10.cuda-13.0.zip`，也没有把 `nvinfer_10.dll` / `nvinfer_builder_resource_10.dll` 复制到产物目录或 `plugins/nvidia_rtx/`**。
   - 因此，当 [`src/shim/ngx_bridge/rife_engine.cpp`](../../src/shim/ngx_bridge/rife_engine.cpp)（第 147–156 行）调用 `TrtLoader::instance().loadRuntime(error)` 或打开 `engine_path` 时，一旦 DLL 未解压至特定路径或 `.engine` 文件名不匹配，`loadEngine()` 立即返回 `false`，`rtx_ngx_bridge_init_rife()` 返回 `RTX_ERR_DLL_MISSING`。

3. **第三步（核心欺骗点：`hw_transcode.cpp` 静默吞掉 `initRife` 错误并伪装成 `"nvof"`）**：
   - 请看 [`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp) 第 788–801 行的原味代码：
     ```cpp
     if (rife_requested && session.bridgeLoader().hasRifeApi()) {
       const uint32_t in_w = static_cast<uint32_t>(session.info().width);
       const uint32_t in_h = static_cast<uint32_t>(session.info().height);
       const std::string engine_path_str = options.rife_engine_path.string();
       const int32_t r_res = session.bridgeLoader().initRife(
           session.bridge(), engine_path_str.c_str(), in_w, in_h);
       if (r_res == RTX_OK) {
         rife_active = true;
         report.fruc_backend = "rife-trt";
         fruc_out_buffer = session.bridgeLoader().getInterpBuffer(session.bridge(), in_w, in_h);
       } else {
         report.fruc_backend = "nvof";
         fruc_out_buffer = session.bridgeLoader().getInterpBuffer(session.bridge(), in_w, in_h);
       }
     }
     ```
   - **这就是导致您实测 NVOFA 与 RIFE 同为 86 FPS 的直接元凶**：
     - 当用户显式选择了 RIFE 模式（`rife_requested == true`），但 `initRife()` 失败（`r_res != RTX_OK`）时，这段代码**连一行 `std::cerr` 警告、一行 `OutputDebugStringW`、甚至一个错误返回值都没有**！
     - 它直接进入 `else` 分支，把 `rife_active` 保持为 `false`，把 `report.fruc_backend` 偷偷改成 `"nvof"`，然后走第 896 行的 `session.bridgeLoader().interpolateNv12(...)`！
     - 而在 GUI 界面上，转码结束后只弹出固定文案 `"Transcode successfully completed!"`（[`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp):715），完全不显示实际使用的是哪个补帧后端。用户以为自己测的是 RIFE，实际上跑的完完全全是 `interpolateNv12`！

4. **第四步（二次静默吞错：即便 `initRife` 成功，`interpolateRife` 运行时报错也会静默退化为复制帧）**：
   - 在 [`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp) 第 864–895 行中，即使 `rife_active == true`，一旦 `interpolateRife()` 因 CUDA Context 未绑定返回非 `RTX_OK`，第 892 行 `else` 分支再次**零日志静默退化为 `processFrame(prev_frame, out_pts_90k, false)`（直接复制前一帧）**！

---

### 1.2 深度审计惊人发现：路线 A（所谓“NVOFA 硬件光流”）实为 50 行线性混帧伪光流！

在审计 `docs/phase8/CineRTX_Phase8_返工交付验收实证报告.md` 时，我们发现了一个比 RIFE 静默回退更严重的工程诚信与质量问题：
- 返工验收报告声称：“重构为单一自研 `rtx_ngx_bridge.dll` 原生硬件光流与 JIT PTX 内核驱动架构……`fruc: backend=nvof, interpolated=478`……所有采样帧 SAD 严格 > 0，完全杜绝了静态复制绕过（duplicateBypass）伪插帧。”
- 然而，当我们打开 [`src/shim/ngx_bridge/p010_pack_runtime.cpp`](../../src/shim/ngx_bridge/p010_pack_runtime.cpp) 第 447–554 行审查 `rtx_ngx_bridge_interpolate_nv12` 的真实实现时，看到的真相令人震惊：
  ```cpp
  static const char kInterpolatePtx[] =
      ".version 7.0\n"
      ".target sm_50\n"
      ".address_size 64\n"
      ".visible .entry rtx_interpolate_nv12(\n"
      ...
      "    ld.global.u8 %r1, [%rd9];\n"         // 读取前一帧同坐标字节 prev[y * pitch + x]
      "    ld.global.u8 %r2, [%rd10];\n"        // 读取当前帧同坐标字节 curr[y * pitch + x]
      "    cvt.rn.f32.u32 %f2, %r1;\n"
      "    cvt.rn.f32.u32 %f3, %r2;\n"
      "    sub.f32 %f4, 1.0, %f1;\n"            // 1.0 - alpha
      "    mul.f32 %f5, %f4, %f2;\n"            // (1.0 - alpha) * prev
      "    fma.rn.f32 %f6, %f1, %f3, %f5;\n"    // alpha * curr + (1.0 - alpha) * prev
      "    cvt.rni.u32.f32 %r3, %f6;\n"
      "    st.global.u8 [%rd11], %r3;\n"        // 写入线性混合结果
  ```
- **铁证定性**：
  1. **全仓 0 处调用 NVOFA**：全仓 `src/` 与 `include/` 中**没有任何一行代码加载 `nvofapi64.dll` 或调用 `NvOFAPICreateInstanceCuda`**！
  2. **用“叠影混帧（Frame Blending）”冒充“硬件光流（NVOFA）”应付 SAD 门禁**：上一轮验收揭露了工程师用 `duplicateBypass`（复制前一帧，导致帧间 `SAD == 0`）造假；这一轮返工中，工程师为了绕过 `SAD > 0` 门禁，直接写了 50 行 PTX 对前后两帧同位置像素做 `lerp(prev, curr, alpha)` 线性透明度混合！线性混帧当然会让相邻帧像素值改变（从而让 `SAD > 0`），但它在视觉上是典型的**双影/鬼影（Ghosting / Cross-fade）**，根本没有计算任何运动矢量，更没有调用 RTX 显卡上的 Optical Flow Accelerator (OFA) 硬件单元！
  3. **10-bit `P010` 源视频必现花屏**：该核函数无视输入帧是 8-bit `NV12` 还是 10-bit `P010`，一律按单字节 `ld.global.u8` 逐字节线性插值。一旦输入源是 10-bit 视频（NVDEC 输出 `P010` 16-bit 小端序），对高低字节分别做独立 `u8` 线性加权会导致高位进位截断与低位错乱，产生严重的色斑与量化伪影！

---

### 1.3 Phase 8 施工方案（`DOC-PHASE8-TRT-RIFE-CONSTRUCTION-002`）逐项验收矩阵

对照 [`docs/phase8/CineRTX_Phase8_TensorRT_RIFE补帧施工方案.md`](CineRTX_Phase8_TensorRT_RIFE补帧施工方案.md) 的全部章节与五大门禁，逐项代码级核验结果如下：

| 施工方案条款 / 门禁 | 方案要求 | 当前代码实现现状（附源码位置） | 验收状态 | 缺陷定性与风险等级 |
| :--- | :--- | :--- | :---: | :--- |
| **§1.1 路线 A 默认真光流** | 默认使用驱动原生 `nvofapi64.dll` NVOFA 硬件光流加速器 | [`src/shim/ngx_bridge/p010_pack_runtime.cpp`](../../src/shim/ngx_bridge/p010_pack_runtime.cpp):447–554 实为 `u8` 线性透明度混帧（Cross-fade），全仓未加载 `nvofapi64.dll` | **FAIL** | **P0 阻断**：伪光流，运动画面产生双影鬼影，且损坏 10-bit 输入 |
| **§4 Track 1.1 ABI v4 契约** | 在 [`include/shim/ngx_bridge_abi.h`](../../include/shim/ngx_bridge_abi.h) 追加 `init_rife` / `interpolate_rife` / `build_rife_engine` / `destroy_rife` | [`include/shim/ngx_bridge_abi.h`](../../include/shim/ngx_bridge_abi.h):20, 246–278 已定义 `ABI_VERSION 4u` 及 4 个导出函数；[`src/core/ngx_remaster.cpp`](../../src/core/ngx_remaster.cpp):127–130, 224–227 已绑定 | **PASS** | 契约签名符合规范，但 `NgxRemasterBridge::close()`（第 257 行）漏清空了后 6 个函数指针 |
| **§4 Track 1.2 动态装载与版本门禁** | `LoadLibraryExW` 加载 `nvinfer_10.dll`，校验 `getInferLibVersion() < 101400`，清理 `nv_runtime.cpp` 旧残留 | [`src/shim/ngx_bridge/trt_loader.cpp`](../../src/shim/ngx_bridge/trt_loader.cpp):90–145 实现了动态加载与 `< 101400` 门禁；[`src/shim/loader/nv_runtime.cpp`](../../src/shim/loader/nv_runtime.cpp) 已清理旧探测 | **PARTIAL** | **P0 违规**：`trt_loader.cpp`:55 硬编码了未脱敏路径 `[REDACTED_HOST]` |
| **§4 Track 1.3 RIFE 引擎与变体探测** | 使用 TRT 10.x Tensor Name API，自适应 7ch 单输入 / 3 输入分离变体，查询 `getTensorDataType` 自适应 FP16/FP32 | [`src/shim/ngx_bridge/rife_engine.cpp`](../../src/shim/ngx_bridge/rife_engine.cpp):179–245 使用了 `getNbIOTensors`/`enqueueV3`，但**未调用 `getTensorDataType`**，硬编码按 `sizeof(float)` 分配 FP32 缓冲；且残留 `RifeEngine::destroy()`（第 131 行） | **FAIL** | **P0 阻断**：1. `bridge_exports.cpp`:863, 884 未调 `bind_context`；2. `rife_pack_runtime.cpp`:115 私自绑定 device 0 primary context，与 `D3D12Interop` 上下文冲突；3. 违反零 `destroy()` RAII 铁律 |
| **§4 Track 2.1–2.3 前后处理核与 SAD 硬切** | `compute_89` PTX，BT.709 转换，`reflect101` 64 对齐填充，`__shfl_down_sync` 束内规约 SAD，`diff_norm > 0.35f` 硬切旁路 | `src/shim/ngx_bridge/rife_pack_ptx.h` 目标为 `.target sm_70`；**未使用 `__shfl_down_sync`**，而是对每个像素直接执行 `atom.global.add.u32`；且 [`src/shim/ngx_bridge/bridge_exports.cpp`](../../src/shim/ngx_bridge/bridge_exports.cpp):806 `get_interp_buffer` 按 `width` 分配却按 `pitch` 写入 | **FAIL** | **P0 阻断**：1. 当 NVDEC `pitch > width` 时后处理写入越界踩内存；2. 1080p 每帧触发 207 万次全局原子加争用；3. `rife_pack_ptx.h` 被直接提交进源码树 `src/shim/ngx_bridge/` |
| **§4 Track 3 引擎构建与算子精度隔离** | FP16 + `kOBEY_PRECISION_CONSTRAINTS`，锁定 `GridSample`/`Div`/`Recip`/`Softmax`/`Reduce` 为 FP32，动态 Profile | [`src/shim/ngx_bridge/rife_builder.cpp`](../../src/shim/ngx_bridge/rife_builder.cpp):102–173 实装了 5 类算子 FP32 锁定，但第 155 行按 **32 对齐**（`rife_engine.cpp:175` 却按 **64 对齐**），且忽略了 CLI 的 `--min-w/h`、`--max-w/h`、`--fp16/--fp32` | **PARTIAL** | **P1 缺陷**：Builder 32 对齐与 Engine 64 对齐不一致（如 720p 下 `736` vs `768`），导致非 64 倍数分辨率推理越界或维度被拒 |
| **§4 Track 4 CLI / GUI 接线** | CLI 增加 `--rife-engine` / `--build-rife-engine`；GUI 选择 RIFE 时弹窗选择 `.engine`；失败平滑降级但需如实记录 | [`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp):369–386 无文件选择框，硬编码 `"models/rife_v4.25.engine"`；[`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp):798 失败时零日志静默篡改 `fruc_backend = "nvof"` | **FAIL** | **P0 阻断**：静默吞掉用户显式指定的 RIFE 请求并伪报为 `nvof`，造成“86 FPS 同速”假象 |

---

### 1.4 Phase 8 彻底修复方案（让路线 A 真光流与路线 B RIFE 100% 真实落地）

1. **修复路线 A（真 NVOFA 硬件光流）：在 `rtx_ngx_bridge.dll` 内直调 `System32\nvofapi64.dll`**：
   - 严禁再用 `rtx_interpolate_nv12` 的线性透明度混帧欺骗用户！
   - 在 `src/shim/ngx_bridge/` 内新增 `nvof_engine.cpp`（≤ 280 行），通过 `LoadLibraryExW(L"nvofapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)` 解析 `NvOFAPICreateInstanceCuda`，绑定 `D3D12Interop` 的同一 `CUcontext` 与 `pack_stream`，配置 `NV_OF_OUTPUT_VECTOR_GRID_SIZE_4` 与双向光流 `NV_OF_PRED_DIRECTION_BOTH`。
   - 将 `p010_pack.cu` / `rife_pack.cu` 中的光流 Warp 核升级为读取 S10.5 定点光流矢量（乘以 `1.0f / 32.0f`）的双向运动补偿 + 遮挡检测核，并原生支持 `NV12`（8-bit）与 `P010`（10-bit）双格式！
2. **修复路线 B（TensorRT RIFE）的 6 处阻断缺陷**：
   - **禁止静默伪装**：当用户显式指定 RIFE（CLI `--rife-engine`、GUI 选中 RIFE、WebUI `fpsMode` 设为 `*-rife`）而 `initRife()` 失败时，若用户显式要求了 RIFE，**必须向日志/stderr/GUI 输出明确的错误原因**（如 `"RIFE initialization failed: Cannot open models/rife_v4.25.engine"`），绝不允许静默篡改 `fruc_backend = "nvof"` 欺骗用户！
   - **统一 CUDA Context**：删除 `rife_pack_runtime.cpp` 与 `rife_engine.cpp` 中私自创建的 `primary_context`，在 `bridge_exports.cpp` 的 `rtx_ngx_bridge_init_rife` 与 `rtx_ngx_bridge_interpolate_rife` 入口统一调用 `impl->interop->bind_context(&error)`，全桥共享唯一 CUDA 上下文。
   - **修复 `get_interp_buffer` Pitch 越界**：将 `rtx_ngx_bridge_get_interp_buffer(bridge, width, height)` 改为接收 `pitch`（或内部按 `alloc_pitched` 的实际 `pitch * height * 3 / 2` 分配），杜绝 NVDEC 对齐行距大于 `width` 时的显存越界。
   - **统一 64 像素对齐与 Warp 级 SAD 规约**：将 `rife_builder.cpp:155` 与 `test_rife.cpp:74` 统一改为 `64` 像素对齐（`kAlign = 64`）；在 `rife_pack.cu` 中使用 `__shfl_down_sync(0xffffffff, sad, offset)` 完成 32 线程束内规约后再由 `lane 0` 执行单次 `atomicAdd`，将全局原子操作次数降低 32 倍。
   - **拔除 `RifeEngine::destroy()` 与硬编码绝对路径**：将 `RifeEngine::destroy()` 合并进 `~RifeEngine()` 析构函数（`impl_.reset()` 即自动释放），100% 遵守 RAII 铁律；删除 [`src/shim/ngx_bridge/trt_loader.cpp`](../../src/shim/ngx_bridge/trt_loader.cpp):55 的 `[REDACTED_HOST]` 硬编码路径，并在 `CMakeLists.txt` 中当检测到 `TensorRT-10.13.3.9` 时自动将 `nvinfer_10.dll`、`nvinfer_builder_resource_10.dll`、`nvonnxparser_10.dll` 镜像部署至 `<exe_dir>/plugins/nvidia_rtx/`。

---

## 第二部分：Win32 GUI + CLI + WebUI 三端统一架构重塑（专题二）

### 2.1 三端现状诊断：8 处跨端不一致性与 4 重代码复制病灶

当前代码库中，`Win32 GUI`（[`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp)）、`CLI`（[`src/cli/main.cpp`](../../src/cli/main.cpp)）与新加入的 `WebUI`（[`webui/index.html`](../../webui/index.html)、[`webui/app.js`](../../webui/app.js)、[`webui/state.json`](../../webui/state.json)、[`webui/server.py`](../../webui/server.py)）各自为政，已经产生了严重的契约分裂：

| 对比维度 | Core (`hw_transcode.h`) | CLI (`src/cli/main.cpp`) | Win32 GUI (`src/gui/rtx_gui.cpp`) | WebUI (`webui/state.json` + `server.py`) | 违背的设计原则与后果 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **1. 默认 QP 值 (`qp`)** | `0`（第 50 行） | `0`（第 73 行） | **`24`**（第 562 行 `L"24"`） | **`26`**（`state.json:16`, `server.py:414`） | **违背 SSOT / 一致性**：三端三个默认 QP（`0` vs `24` vs `26`），同一视频在三端跑出完全不同的码率与画质！ |
| **2. 峰值亮度 (`peak_nits`)** | `1000` (400..4000) | `--peak-nits` 正常绑定 | **控件未布局（`0x0` 隐形），且 `collectOptions:467` 硬编码 `1000`** | `profile.peakNits` 正常绑定 | **违背 SRP / DRY**：GUI 手写布局漏掉 `IDC_EDT_PEAK_NITS`，且收集函数忘记读控件！ |
| **3. VSR 启停开关 (`vsr_enabled`)** | `bool vsr_enabled = true` | **无此参数**（`--vsr-quality` 限制 `1..4`） | 有 `IDC_CHK_VSR` 勾选框 | **无此控件**（`README.md:59` 自认与 CLI 对齐省略） | **违背 ISP / 契约统一**：Core 与 GUI 支持单独关闭 VSR，CLI 与 WebUI 却无法表达该状态。 |
| **4. RIFE 引擎路径指定** | `rife_engine_path` | `--rife-engine <path>` | **无输入框/浏览按钮**，硬编码 `models/rife_v4.25.engine` | 有 `profile.rifeEngine` 输入框，但**无文件浏览按钮**（`index.html:114` 缺 `data-browse`） | **违背开闭原则 (OCP)**：每加一个参数，三端各自漏掉一半交互能力。 |
| **5. GPU 选择与帧数上限** | `device_index = 0`, `frame_limit = 0` | `--gpu N`, `--frames N` | **完全缺失**（无法选卡或限帧） | 有 `profile.gpu` 与 `profile.frames` | 三端暴露的能力集合互不相同。 |
| **6. 输出路径自动推导** | 无 | 无（不传 `--output` 直接报错退出） | `autoFillOutputPath`（仅当输出框为空时填 `.mp4`） | `autoDeriveOutput`（JS 正则匹配 `_remaster_*` 动态跟随输入与编码格式） | 同一业务逻辑在 C++ GUI 与 JS 前端各写一套，且行为不一致。 |
| **7. 取消任务机制** | 轮询 `std::atomic<bool>* cancel` | **无取消通道** | 置位 `g_cancelRequested = true`，安全刷出缓冲 | 调用 `proc.terminate()` / `proc.kill()` **暴力强杀进程** | **严重工程隐患**：WebUI 取消任务直接强杀 `CineRTX.exe`，导致输出 MP4 缺失 `moov` 容器尾而彻底损坏！ |
| **8. 遥测报告与 i18n 传递** | `HwTranscodeReport::toJson()` + `i18n.cpp` (64 词条) | `printHwReport()` 打印非结构化人类文本 | 自建字符串格式化 + `populateComboBoxes` 硬编码中英文三目运算符 | `server.py` 用 **10 条正则表达式**反向刮削 CLI `stdout` 文本；`app.js` 复制粘贴 64 条 i18n + 自建 49 条 `Wui*/Opt*` | **严重违背 DIP（依赖倒置）与 DRY**：明明 C++ 已有 `toJson()` 和 `.rdata` 词条表，WebUI 却用正则刮削控制台文本并手工复制整套词典！ |

---

### 2.2 遵循软件设计六大准则（SOLID + 迪米特法则）与 KISS / 奥卡姆剃刀的统一架构设计

为彻底终结“加一个参数改四处、三端默认值各唱各的调、Python 靠正则表达式刮削 C++ 控制台文本”的乱象，我们按照**软件设计六大准则**与项目铁律（**KISS、奥卡姆剃刀、单 EXE 纯净导入表 `0 WS2_32.dll`、100% RAII、C++26 指定初始化**），设计**“一核三端（One Core, Three Thin Adapters）”统一架构**：

```
┌─────────────────────────────────────────────────────────────────────────────────────┐
│                     Zone A: rtx_core 单一事实源核心层 (C++26 SSOT)                    │
│                                                                                     │
│  1. 声明式参数契约表 `constexpr ParamMeta kParamSchema[]` (include/rtx/param_schema.h)│
│     • 唯一定义：字段名、CLI Flag、类型、唯一默认值、取值范围、枚举项 StrId、联动谓词  │
│  2. 统一配置结构体 `TranscodeProfile` + 唯一校验/自动命名 `validateAndNormalize()`    │
│     • 原生支持：`fromCli(argc, argv)` / `fromJson(json)` / `toJson()`               │
│  3. 统一国际化表 `i18n.cpp` (.rdata 零分配表，收编全部基础词条与 Opt* 枚举词条)       │
│     • 原生支持：`exportSchemaAndI18nJson()` (一键导出完整 Schema + 默认值 + 双语词典) │
│  4. 统一会话控制器 `EngineController` (RAII 会话生命周期、原子取消、JSON 事件发射器)   │
└───────────────┬──────────────────────────────┬──────────────────────────────┬───────┘
                │                              │                              │
                ▼                              ▼                              ▼
 ┌──────────────────────────────┐ ┌──────────────────────────┐ ┌──────────────────────────────┐
 │ 前端适配器 1：CLI 控制台     │ │ 前端适配器 2：Win32 GUI  │ │ 前端适配器 3：WebUI          │
 │ `src/cli/main.cpp` (≤150行)  │ │ `src/gui/rtx_gui.cpp`    │ │ `webui/` (4文件) + 薄转发桥   │
 │                              │ │                          │ │                              │
 │ • 表驱动解析：`fromCli()`    │ │ • 表驱动构建下拉框与默认值│ │ • 启动时调 `--schema-json`   │
 │ • `--schema-json` 导出契约   │ │ • 统一调 `validateAnd    │ │   动态拉取默认值/范围/i18n   │
 │ • `--session-stdio` 原生     │ │   Normalize()` 自动补全  │ │ • 作业执行调 `CineRTX.exe    │
 │   JSONL 事件流 + stdin 取消  │ │   输出名与控件启停联动   │ │   --session-stdio`           │
 └──────────────────────────────┘ └──────────────────────────┘ └──────────────────────────────┘
```

#### 1. 软件设计六大准则在架构中的精准落地映射

| 设计准则 | 针对当前痛点的根治动作 | 落地后的代码收益 |
| :--- | :--- | :--- |
| **1. 单一职责原则 (SRP)** | - `param_schema.h` 只负责声明参数契约、默认值、校验边界与联动规则；<br>- `EngineController` 只负责转码会话调度与结构化遥测输出；<br>- CLI / Win32 GUI / WebUI 只负责视图渲染与用户输入转发，**严禁任何前端私自编写参数默认值、范围校验、输出文件名推导或文本正则解析**。 | 根除 `main.cpp`、`rtx_gui.cpp`、`server.py`、`app.js` 四处重复校验与各自硬编码默认值。 |
| **2. 开闭原则 (OCP)** | 将所有可调参数收敛为编译期表 `constexpr ParamMeta kParamSchema[]`。未来新增或调整参数时，只需在 `kParamSchema` 中增改一行元数据：CLI 自动获得该 `--flag` 解析与 `--help` 文本，Win32 GUI 自动同步默认值与联动规则，WebUI 通过 `--schema-json` 自动获得校验边界与双语标签。 | 新增参数时，CLI 解析器、`server.py` 转发层、JSON 序列化器**零代码修改（对扩展开放，对修改关闭）**。 |
| **3. 里氏替换原则 (LSP)** | 抽象统一的 `ITranscodeObserver` 事件接收契约（含 `onProgress(const HwTranscodeProgress&)`、`onLog(LogLevel, std::string_view)`、`onComplete(const HwTranscodeReport&)`）。无论是 CLI 的人类终端打印器、`--session-stdio` 的 JSONL 流发射器，还是 Win32 GUI 的 `PostMessageW` 投递器，均可无损替换注入 `EngineController`。 | 彻底消除“GUI 有取消和预览、CLI 有 JSON 报告但无流式 JSON 进度、WebUI 只能靠正则刮削人类文本”的割裂。 |
| **4. 接口隔离原则 (ISP)** | 将配置结构拆分为纯粹的业务参数 `TranscodeProfile`（可 1:1 序列化为 `state.json` 的 `profile` 节点）与运行时句柄 `TranscodeRuntimeCallbacks`（`cancel` 原子指针、`preview` 回调、`observer` 接口），不强迫 CLI 或 WebUI 依赖 D3D12 视口回调。 | `TranscodeProfile` 成为纯正的 C++26 聚合体（Aggregate），100% 支持 `.field = value` 指定初始化。 |
| **5. 依赖倒置原则 (DIP)** | **倒置前（错误）**：WebUI 的 `server.py` 依赖 CLI 面向人类阅读的控制台格式化文本（用 10 条正则去刮削 `printHwReport` 的输出），`app.js` 手工复制 `i18n.cpp` 的词条。<br>**倒置后（正确）**：三端共同依赖 `rtx_core` 导出的机器契约（`--schema-json` 与 `--session-stdio` JSON 事件流），高层策略与底层实现均依赖统一 JSON/Struct 契约。 | **直接砍掉 `server.py` 中全部 10 条正则表达式、`build_argv`、`PROFILE_LIMITS`、`PROFILE_ENUMS`，以及 `app.js` 中 260 行重复的 `I18N` 字典！** |
| **6. 迪米特法则 (LoD / 最少知识原则)** | WebUI 桥接层（`server.py`）对转码业务逻辑保持**零知识（Zero Domain Knowledge）**：它不需要知道有哪些编码器、不需要知道 QP 的范围是 `0..51`、不需要知道如何把 JSON 字段拼成 `--target-width 3840`——它只负责将 `state.json` 原样通过 `stdin` 传给 `CineRTX.exe --session-stdio`，并将 `stdout` 吐出的 JSON 行原样作为 SSE 事件广播给浏览器！ | `webui/server.py` 从 **892 行骤降至 ≤ 180 行**通用 JSON-to-Stdio 管道桥，哪怕后端参数矩阵改天换地，`server.py` 也无需改动一行代码！ |

---

#### 2. 核心统一契约设计：C++26 `include/rtx/param_schema.h`

在 `include/rtx/param_schema.h` 中，使用 C++26 指定初始化（Designated Initializers）定义全仓唯一的 `TranscodeProfile` 与参数契约表：

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
#pragma once
#include "rtx/i18n.h"
#include "rtx/rational.h"
#include "rtx/status.h"
#include "rtx/types.h"
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace rtx {

enum class FpsMode : uint8_t {
  Original = 0, // 保持原帧率 (0)
  Nvofa60  = 1, // 60 FPS (NVOFA 硬件光流 - 默认推荐)
  Nvofa120 = 2, // 120 FPS (NVOFA 硬件光流)
  Rife60   = 3, // 60 FPS (TensorRT RIFE 深度学习)
  Rife120  = 4, // 120 FPS (TensorRT RIFE 深度学习)
};

enum class ResolutionPreset : uint8_t {
  Uhd4K  = 0, // 3840x2160 (自适应竖屏 2160x3840)
  Qhd2K  = 1, // 2560x1440
  Fhd1080= 2, // 1920x1080
  Native = 3, // 保持源分辨率
};

// 全仓唯一业务参数聚合体（与 state.json 的 profile 节点 1:1 严格同构）
struct TranscodeProfile {
  std::filesystem::path input{};
  std::filesystem::path output{};
  VideoCodec codec = VideoCodec::AV1;           // 默认 AV1 10-bit
  int32_t preset = 5;                           // 1..7, 默认 P5
  int32_t rate_control = 0;                     // 0=CBR, 1=VBR, 2=QP
  int32_t bitrate_kbps = 0;                     // 0=自动
  int32_t qp = 24;                              // 三端统一默认 QP = 24！
  ResolutionPreset resolution = ResolutionPreset::Uhd4K;
  int32_t target_width = 3840;
  int32_t target_height = 2160;
  int32_t geometry_mode = 0;                    // 0=AutoDirect, 1=Pillarbox
  bool vsr_enabled = true;                      // 三端统一支持 VSR 启停
  int32_t vsr_quality = 4;                      // 1..4, 默认 Level 4 Ultra
  int32_t peak_nits = 1000;                     // 400..4000, 默认 1000 nits
  FpsMode fps_mode = FpsMode::Nvofa60;          // 默认 60 FPS NVOFA
  std::filesystem::path rife_engine{};          // RIFE .engine 路径
  bool copy_audio = true;                       // 默认音频无损直通
  bool hdr10plus = false;                       // ST 2094-40 动态元数据
  std::filesystem::path lut{};                  // .cube 3D LUT 路径
  int32_t gpu = 0;                              // 默认 GPU 0
  int32_t frames = 0;                           // 0=全流
  bool auto_output = true;                      // 自动补全输出文件名
};

// 唯一权威函数声明（实现在 src/core/param_schema.cpp，三端共用）
std::filesystem::path deriveOutputPath(const std::filesystem::path& input, VideoCodec codec);
Result<void> validateAndNormalizeProfile(TranscodeProfile& profile);
Result<TranscodeProfile> parseProfileFromCli(int argc, const char* const* argv);
Result<TranscodeProfile> parseProfileFromJson(std::string_view json_text);
std::string serializeProfileToJson(const TranscodeProfile& profile);
std::string exportSchemaAndI18nJson(); // 供 `CineRTX.exe --schema-json` 输出
HwTranscodeOptions toHwTranscodeOptions(const TranscodeProfile& profile);

} // namespace rtx
```

---

#### 3. 为什么选用 `CineRTX.exe --session-stdio` + `--schema-json` 接入 WebUI？（兼顾 `0 WS2_32.dll` 红线与奥卡姆剃刀）

在之前的问题 7 与 Phase 8 门禁 4（`tools/audit_imports.ps1`）中，项目已经确立了一条硬核红线：**`CineRTX.exe` 静态导入表中 `WS2_32.dll` 必须为 0**。
- 如果把 HTTP Socket 服务器直接塞进 `CineRTX.exe`，就会重新引入 `WS2_32.dll` 导入或增加数百行 HTTP 协议解析代码；
- 反之，当前 `webui/server.py` 的错误不在于“用 Python 标准库做 HTTP 静态服务器”，而在于**它把原本属于 C++ 核心层的“参数范围校验 (`PROFILE_LIMITS`)、命令行拼装 (`build_argv`)、输出文本正则刮削 (`REPORT_RES`)”在 Python 里重写了一遍，并且在 `app.js` 里把 `i18n.cpp` 的词条又抄了一遍**！
- **采用 `--schema-json` + `--session-stdio` 原生 JSON 会话协议后的极致精简效果**：
  1. **动态词条与契约同源（消灭 `app.js` 260 行重复字典与 `server.py` 校验表）**：
     - 我们将下拉框选项词条（`OptCodecAv1` ~ `OptFps120Rife`）与 WebUI 面板标题词条统一收编进 [`include/rtx/i18n.h`](../../include/rtx/i18n.h) 与 [`src/core/i18n.cpp`](../../src/core/i18n.cpp) 的 `.rdata` 静态表中。
     - Win32 GUI 的 `populateComboBoxes()` 直接用 `rtx::trW(StrId::OptCodecAv1)` 填充下拉框（彻底消灭 [`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp):166–257 的中英文三目运算符硬编码）。
     - WebUI 启动时通过 `GET /api/schema`（底层调用 `CineRTX.exe --schema-json`）一次性拿到 C++ 编译期生成的双语词典、参数默认值的完整 JSON，**`webui/app.js` 直接删掉第 25–285 行的 260 行硬编码 `I18N` 字典**，实现 C++、Win32 GUI、WebUI 100% 物理同源！
  2. **原生 JSON 事件流与协作式取消（消灭 `server.py` 10 条正则刮削与 `terminate()` 毁文件 Bug）**：
     - `CineRTX.exe --session-stdio` 启动后，从 `stdin` 首行读取 `TranscodeProfile` JSON（自动执行 C++ 唯一 `validateAndNormalizeProfile()` 校验），在转码过程中每 250ms 向 `stdout` 输出一行结构化 JSON：
       `{"type":"progress","pct":74.9,"frames":448,"total":598,"fps":86.2}`
       并在结束时直接输出 `HwTranscodeReport::toJson()`：
       `{"type":"report","report":{...}}`
     - 同时，`--session-stdio` 启动一个极简后台线程监听 `stdin`：当用户在 WebUI 点击“停止转码”时，`server.py` 向子进程 `stdin` 写入 `"cancel\n"`，`CineRTX.exe` 立即置位 `cancel = true`，让 `NvidiaTranscodeSession` **优雅完成当前帧编码、刷出 NVENC 尾包并写入 MP4 `moov` 容器尾（`finalize()`）后干净退出**，彻底根除 `proc.terminate()` 强杀进程导致输出视频损坏的顽疾！

---

## 第三部分：全仓文档过时、实现漂移与无意义冗余二次终审（专题三）

### 3.1 对现有工程师《文档↔实现漂移审计报告》的评估与盲区定性

现有工程师提交的 `docs/phase8/CineRTX_文档实现漂移审计报告_20261113.md` 共检出了 **58 项**离散发现（`A1–A35`、`B1–B25`）与 185 处掩码死链。我们对其进行了抽样核验，确认其列出的 **CI 三处 smoke 调不存在的 `--self-test`/`--simulate` 必败（A30）**、**根目录 `README.md`/`README_zh.md` 残留 `--truehdr`（A1）**、**测试计数 79/102/137 与实际 `RTX_TEST=62` 不符（A2/A3）**、**`docs/spec/architecture.md` §4–§7 描述已删子系统（A13–A16）**、**`docs/spec/abi.md` 版本号仍写 `2u` 而实际 `ngx_bridge_abi.h` 已是 `4u`（A8）** 等 58 项结论属实。

**但是，现有工程师的排查存在严重的“只查历史旧文档、漏查最新 Phase 8 代码与跨端漂移”盲区（且其报告第 7 节自认“波 2 补扫代理被叫停未完成”），并做出了两项完全错误的“零缺陷”断言**：
1. **错误断言一**：工程师报告 §一、§三 声称 `"未掩码 [REDACTED_HOST] 0 处 — 无 P1 隐私复发"` —— **实测在最新 C++ 源码 [`src/shim/ngx_bridge/trt_loader.cpp`](../../src/shim/ngx_bridge/trt_loader.cpp) 第 55 行就硬编码了未掩码的 `[REDACTED_PATH]`！**
2. **错误断言二**：工程师报告 §一、§五 声称 `"webui 零漂移"`、`"13 项专业参数 = rtx_gui.cpp:260-274 恰 13 个控件"` —— **实测 Win32 GUI 的第 13 个控件 `IDC_EDT_PEAK_NITS` 根本没写进 `layoutControls()`（在界面上 `0x0` 隐形且数值被硬编码为 `1000`），且 WebUI 与 GUI/CLI 存在 8 处跨端漂移及 1 处 UTF-8 乱码！**

---

### 3.2 专家二次排查新增发现总表（C1–C12：工程师漏检的 12 项核心漂移与冗余）

以下 **C1–C12** 均为 `docs/phase8/CineRTX_文档实现漂移审计报告_20261113.md` **完全漏检或误判**的增量实证发现：

| 编号 | 位置（文件与行号） | 文档/注释声称 | 实际代码现状（铁证） | 严重度 | 类别 |
| :---: | :--- | :--- | :--- | :---: | :--- |
| **C1** | [`src/shim/ngx_bridge/trt_loader.cpp`](../../src/shim/ngx_bridge/trt_loader.cpp):55 | 漂移报告 §一 声称“未掩码 `[REDACTED_HOST]` 0 处”；施工方案门禁 4 要求“无本机绝对路径” | 第 55 行硬编码：`dirs.push_back(L"[REDACTED_PATH]");` | **P0** | **隐私/脱敏红线破防** |
| **C2** | `docs/phase8/CineRTX_Phase8_返工交付验收实证报告.md`:14, 71；[`include/shim/ngx_bridge_abi.h`](../../include/shim/ngx_bridge_abi.h):236 | 声称 `rtx_ngx_bridge_interpolate_nv12` 使用“NVOFA 硬件光流与双帧运动矢量插值” | [`src/shim/ngx_bridge/p010_pack_runtime.cpp`](../../src/shim/ngx_bridge/p010_pack_runtime.cpp):447–502 实为 50 行 PTX 同坐标像素 `u8` 线性加权混帧（Cross-fade），全仓 0 处调用 `nvofapi64.dll` | **P0** | **核心实现造假漂移** |
| **C3** | [`docs/phase8/CineRTX_Phase8_TensorRT_RIFE补帧施工方案.md`](CineRTX_Phase8_TensorRT_RIFE补帧施工方案.md) §2.2, §4.1–4.2, §6.1 | 要求：1. GUI 弹窗选 `.engine`；2. 查询 `getTensorDataType` 自适应 FP16/FP32；3. `__shfl_down_sync` 束内规约 SAD；4. 64 对齐；5. CLI 构建参数生效 | 1. `rtx_gui.cpp`:372 无弹窗，硬编码 `models/rife_v4.25.engine`；2. `rife_engine.cpp`:201 硬编码 FP32；3. `rife_pack_ptx.h`:115 逐像素全局 `atom.global.add`；4. `rife_builder.cpp`:155 按 32 对齐；5. `main.cpp`:497 丢弃 `--min-w/h`, `--max-w/h`, `--fp16/--fp32` | **P1** | **施工方案↔代码 7 处漂移** |
| **C4** | [`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp):296–395, 467, 576–577 | [`README.md`](../../README.md):13 与漂移报告 §五 声称 GUI 暴露“13 项专业参数（含 TrueHDR Peak Nits）” | `IDC_LBL_PEAK_NITS` 与 `IDC_EDT_PEAK_NITS` 在 `layoutControls()` 中完全未调用 `MoveWindow`（尺寸 `0x0` 隐形），且 `collectOptions()` 第 467 行硬编码 `opt.peak_nits = 1000;`，从未读取输入框！ | **P1** | **GUI 控件隐形与假绑定** |
| **C5** | `tools/package_release.py`:9, 99, 121 | 打包脚本生成的发行说明 `README.txt` 指导用户运行 `--truehdr`，且声称体积 `~28 MB` | 第 121 行写入 `CineRTX.exe --input in.mp4 --output out_hdr10.mp4 --truehdr`（传入即报 `unknown argument` 崩溃）；`~28 MB` 与根 `README.md` 的 `~34 MB` 矛盾 | **P1** | **发布脚本幽灵参数残留** |
| **C6** | [`docs/README.md`](../README.md):5–9, 160–175 | 顶部“当前产品契约”声称“2026-11-10 Phase 6 权威基线…彻底切除 RIFE/FRUC/ONNX…零 destroy”；第十二节索引 Phase 8 | 顶部声明与同文档第十二节（Phase 8 双路补帧 + TensorRT RIFE）、第十三节（WebUI）直接自相矛盾；且 `docs/phase8/` 实有 10 篇文档，索引漏列了 `外部技术情报与执行参考手册.md` 与 `文档实现漂移审计报告_20261113.md` | **P1** | **全景索引自相矛盾与漏列** |
| **C7** | [`webui/app.js`](../../webui/app.js):813；[`webui/state.json`](../../webui/state.json):16；[`webui/index.html`](../../webui/index.html):114 | 漂移报告 §一、§五 声称 `webui 零漂移` | 1. [`webui/app.js`](../../webui/app.js):813 文件浏览器图标存在 UTF-8 乱码（`"ð\x9f\x93\x81 "` / `"ð\x9f\x93\x84 "`）；2. `state.json:16` 默认 `qp: 26`（与 GUI `24`、CLI `0` 冲突）；3. `index.html:114` 的 `ctl-rife` 输入框缺少浏览按钮 `data-browse="profile.rifeEngine"` | **P2** | **WebUI 乱码与跨端漂移** |
| **C8** | [`include/rtx/hw_transcode.h`](../../include/rtx/hw_transcode.h):4–7, 34, 88, 136；[`src/shim/loader/nv_runtime.cpp`](../../src/shim/loader/nv_runtime.cpp):4–6 | 头文件注释声称支持 `raw-artifact` 路径、`width/height` 裸流几何、默认编码 `NVENC HEVC Main10`；`nv_runtime.cpp` 声称缺组件退回 `CPU reference path` | 裸流路径与 CPU reference 早已删除；`HwTranscodeOptions::width/height`（第 34–35 行）与 `HwTranscodeOptions::fps`（第 51 行）在生产代码中已是**零写入死字段**！ | **P2** | **头文件死字段与陈旧注释** |
| **C9** | `docs/phase8/report.md` vs `docs/phase8/CineRTX_Phase8_返工交付验收实证报告.md` | 两个独立文件并存于 `docs/phase8/` | 两个文件大小同为 **8,502 字节**，内容 **100% 逐字节完全相同**，属无意义双份拷贝冗余 | **P2** | **文档 100% 重复冗余** |
| **C10** | `src/shim/ngx_bridge/rife_pack_ptx.h` (15,303 B) | 文件头写着 `GENERATED FILE - do not edit. Produced at build time from rife_pack.ptx` | [`CMakeLists.txt`](../../CMakeLists.txt):238, 264 已在构建期于 `${CMAKE_BINARY_DIR}/generated/rife_pack_ptx.h` 动态生成该头文件（与 `p010_pack_ptx.h` 对称），但源码树 `src/shim/ngx_bridge/` 下却残留了一份静态 `rife_pack_ptx.h`，不仅冗余 15.3 KB，还会因同目录 `#include "rife_pack_ptx.h"` 优先命中源码树而遮蔽构建期生成的最新 PTX！ | **P1** | **构建生成物污染源码树並遮蔽构建产物** |
| **C11** | `webui/test/__pycache__` 与 `webui/test/evidence` | 仓库应保持源码树整洁，不含编译缓存 | `webui/test/__pycache__/mock_cinertx.cpython-312.pyc` 二进制缓存直接入库；`webui/test/evidence/` 包含 18 个测试过程日志与 PNG 截图 | **P3** | **构建/测试垃圾入库** |
| **C12** | [`src/shim/ngx_bridge/d3d12_interop.cpp`](../../src/shim/ngx_bridge/d3d12_interop.cpp)、`p010_pack_runtime.cpp`、`rife_pack_runtime.cpp`、`rife_engine.cpp` | 同一 `rtx_ngx_bridge.dll` 内的 4 个源文件 | 4 个编译单元各自独立定义了 `DriverApi` / `CudaDriverApi` 结构体，重复了 **4 遍** `LoadLibraryExW(L"nvcuda.dll")` 与 `GetProcAddress("cuInit" / "cuLaunchKernel" / "cuMemAlloc"...)` 样板代码（~400 行冗余），且导致了各自维护独立 `primary_context` 的上下文分裂 Bug！ | **P1** | **代码级严重重复与架构腐化** |

---

### 3.3 全仓无意义冗余文件与死代码大刀阔斧清洗清单

结合工程师报告的 58 项历史文档漂移与本次专家复核新增的 12 项（C1–C12），给出全仓**一步到位清洗清单**：

#### （1）立即物理删除的冗余文件 / 目录（共 6 项）
1. **删除 `src/shim/ngx_bridge/rife_pack_ptx.h`（15.3 KB）**：保留 `rife_pack.cu` 与 `rife_pack.ptx`，与 `p010_pack` 完全对称地由 `CMakeLists.txt` 在 `${CMAKE_BINARY_DIR}/generated/rife_pack_ptx.h` 动态生成，消除源码污染与头文件遮蔽。
2. **删除 `cmake/RtxNvidiaSdks.cmake`（16.2 KB）**：全仓 0 `include()` 的纯死模块（含 `NvOFFRUC` 与 `trt_dynamic.cpp` 残留引用）。
3. **删除 `docs/phase8/report.md`（8.5 KB）**：与 `docs/phase8/CineRTX_Phase8_返工交付验收实证报告.md` 100% 字节重复。
4. **删除 `webui/test/__pycache__/`**：清除 Python `.pyc` 编译缓存，并在 [`.gitignore`](../../.gitignore) 中确保 `__pycache__/` 规则生效。
5. **归档或清理 `docs/TODO/` 下已闭环的 4 篇陈旧待办分册**：
   - `docs/TODO/audio_stream_copy_and_muxing.md`（Phase 5 已实现）
   - `docs/TODO/smart_geometry_and_aspect_ratio.md`（Phase 5 已实现）
   - `docs/TODO/gui_debug_log_elimination_and_debugview.md`（Phase 6 已实现）
   - `docs/TODO/i18n_architecture_and_localization.md`（Phase 6 已实现）
6. **移动 [`src/shim/ngx_bridge/sdr_blit_cs.h`](../../src/shim/ngx_bridge/sdr_blit_cs.h) 与 [`src/shim/ngx_bridge/sdr_blit_rs.h`](../../src/shim/ngx_bridge/sdr_blit_rs.h) 至 `src/gui/shaders/`**：这两个 DXBC 字节码数组仅被 [`src/gui/preview_viewport.cpp`](../../src/gui/preview_viewport.cpp)（第 15–16 行）包含，与 `ngx_bridge` 毫无关系，放在 `src/shim/ngx_bridge/` 严重破坏模块职责边界。

#### （2）立即合并消重的源码模块（净砍 ~1,100 行重复样板代码）
1. **合并 `rtx_ngx_bridge` 内部 4 套重复的 CUDA Driver API 加载器**：
   - 将 `p010_pack_runtime.cpp`、`rife_pack_runtime.cpp`、`rife_engine.cpp` 中各自重复的 `LoadLibraryExW(L"nvcuda.dll")` + `GetProcAddress` 全部删除，统一复用 `D3D12Interop` 已解析的 CUDA Driver 函数表与唯一 `CUcontext`（净砍 ~350 行重复代码，同时根治 RIFE 上下文冲突 Bug）。
2. **合并三端参数契约、校验与 i18n 字典**：
   - 引入 `include/rtx/param_schema.h` + `src/core/param_schema.cpp`：
   - 从 [`webui/app.js`](../../webui/app.js) 删除 260 行硬编码 `I18N` 字典（改为从 `/api/schema` 拉取）；
   - 从 [`webui/server.py`](../../webui/server.py) 删除 `PROFILE_LIMITS`、`PROFILE_ENUMS`、`build_argv` 与 10 条正则 `REPORT_RES`（净砍 ~450 行 Python 胶水代码）；
   - 从 [`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp) 删除 `populateComboBoxes` 中的硬编码中英文三目运算符字符串，改由 `i18n.cpp` 统一驱动，并补齐 `IDC_EDT_PEAK_NITS` 与 `IDC_EDT_RIFE_ENGINE` 布局。

---

## 第四部分：分阶段落地执行路线图（Actionable SuperPower Blueprint）

为确保执行工程师一次性高质量闭环上述三大任务，建议按以下 **3 个紧凑施工波次（Waves）** 顺序推进：

| 施工波次 | 核心施工内容 | 验收门禁（Hard Gates） |
| :--- | :--- | :--- |
| **Wave 1：Phase 8 双路真补帧修复与源码卫生清零** | 1. 在 `src/shim/ngx_bridge/` 实现真 `nvofapi64.dll` 硬件光流加载与 S10.5 双向 Warp 核（支持 NV12/P010），替换 `p010_pack_runtime.cpp` 的线性混帧；<br>2. 合并 Bridge 内 4 套 CUDA Driver API 加载器，在 `init_rife`/`interpolate_rife` 强制调用 `interop->bind_context()`；<br>3. 修复 `get_interp_buffer` Pitch 分配越界、统一 Builder/Engine 为 64 像素对齐、实现 Warp 级 `__shfl_down_sync` SAD 规约、拔除 `RifeEngine::destroy()`；<br>4. 删除 `trt_loader.cpp:55` 硬编码绝对路径与源码树内的 `src/shim/ngx_bridge/rife_pack_ptx.h`；在 `CMakeLists.txt` 中自动部署 `nvinfer_10.dll`；<br>5. 严禁 `hw_transcode.cpp:798` 静默篡改 `fruc_backend`——当用户显式请求 RIFE 失败时，必须输出明确错误诊断。 | - **门禁 1A**：`nvof` 模式实测 `nvofapi64.dll` 真实加载，高速横向运动物体插值帧无双影重影（对比线性混帧 PSNR 提升 $\ge 4\text{ dB}$）；<br>- **门禁 1B**：`rife-trt` 模式真实加载 `.engine` 执行 `enqueueV3`，遥测明确输出 `fruc: backend=rife-trt`，未提供 `.engine` 时明确报错而非静默伪装成 `nvof`；<br>- **门禁 1C**：全仓 `cmake -P cmake/AuditPrivacy.cmake` 严格为 **0 命中**。 |
| **Wave 2：三端统一架构（SSOT Schema + `--session-stdio`）重塑** | 1. 新增 `include/rtx/param_schema.h` 与 `src/core/param_schema.cpp`，定义唯一 `TranscodeProfile`（统一 `qp = 24`）与 `kParamSchema`；<br>2. 将 `Opt*` 与 `Wui*` 词条收编入 `include/rtx/i18n.h` 与 `src/core/i18n.cpp`；<br>3. 在 `src/cli/main.cpp` 增加 `--schema-json` 与 `--session-stdio`（支持 JSONL 进度/报告流与 `stdin` `"cancel\n"` 协作式取消）；<br>4. 重构 `src/gui/rtx_gui.cpp`：修复 `IDC_EDT_PEAK_NITS` 漏布局与假绑定，增加 RIFE `.engine` 路径选择行；<br>5. 重构 `webui/server.py`（砍至 ≤ 180 行）与 `webui/app.js`（动态拉取 `/api/schema`，修复第 813 行 UTF-8 乱码，为 `ctl-rife` 增加 `data-browse`）。 | - **门禁 2A**：CLI、Win32 GUI、WebUI 三端所有参数默认值 100% 一致（通过自动化测试比对 `--schema-json` 与 `state.json`）；<br>- **门禁 2B**：WebUI 点击“停止转码”后，通过 `stdin` 触发协作取消，已编码部分生成的 MP4 容器 `ffprobe` 校验 `moov` 完整可播放；<br>- **门禁 2C**：`pwsh tools/audit_imports.ps1` 校验 `CineRTX.exe` 仍保持 `0 WS2_32.dll`、`0 MSVCP140`、`0 VCRUNTIME140`。 |
| **Wave 3：全仓文档与 CI 契约同步大扫除** | 1. 修复 `.github/workflows/ci.yml` 三处 smoke 测试（改调 `--probe --json`）；<br>2. 清理 `README.md`、`README_zh.md`、`tools/package_release.py:121` 中的 `--truehdr` 幽灵参数，统一单测计数为实际 `RTX_TEST` 数量；<br>3. 重写 `docs/spec/architecture.md`、`docs/spec/abi.md`（对齐 ABI v4）、`docs/spec/testing.md`、`docs/spec/gate3.md`、`docs/README.md` 顶部契约声明；<br>4. 删除 `cmake/RtxNvidiaSdks.cmake`、`docs/phase8/report.md`、`webui/test/__pycache__/`，为 `docs/phase7/` 与 `docs/phase8/` 过时文档补齐 `[HISTORICAL-SUPERSEDED]` 印记。 | - **门禁 3A**：CI 工作流命令本地模拟 100% 零报错通过；<br>- **门禁 3B**：`docs/spec/` 与根级 `README*.md` 对照 `main.cpp --help`、`ngx_bridge_abi.h`、`CMakeLists.txt` 达到 **0 P1/P2 漂移**。 |
