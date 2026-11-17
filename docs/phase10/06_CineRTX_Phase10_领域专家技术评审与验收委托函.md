# CineRTX Phase 10 领域专家技术评审与验收委托函

**致：多媒体系统工程与 GPU 异构计算领域专家 / 评审委员会**  
**发件：CineRTX 项目工程治理中心**  
**日期：2026-11-16**  
**当前基线版本：v0.1.0 (Commit `74d4141`) -> Phase 10 (待发版 v0.2.0)**  

---

### 一、 委托背景与评审目的

CineRTX 在成功完成 `v0.1.0` 里程碑发布后，正式进入 **Phase 10 架构提纯与核心算法演进（SuperPower）阶段**。本阶段由执行团队完成了包括“零附属 DLL 静态提纯”、“全模态 CQ/QP/CRF 编码引擎”、“ShotGroup 镜头防果冻与拓扑重构”、“异构任务队列三端同构”在内的四大核心分轨攻坚。

目前代码已完成全量外科手术式修复，通过全量单元测试（78/78 PASS）、SPDX 许可证与 PE 导入表零泄漏合规门禁。现特委托阁下（领域专家）对 Phase 10 的核心设计方案、代码质量、数学模型及工程边界展开深入技术评审（Peer Review），出具独立专家意见。

---

### 二、 委托评审核心范围与关键质询点

请重点针对以下四个专业分轨及对应工程资产进行穿透式审查：

```
docs/phase10/
├── 01_CineRTX_Phase10_架构提纯与核心算法演进SuperPower总纲.md
├── 02_CineRTX_Phase10_施工单_Track_10A_代码卫生与零附属DLL提纯.md
├── 03_CineRTX_Phase10_施工单_Track_10B_全模态编码引擎_CQ_QP_与CPU_CRF.md
├── 04_CineRTX_Phase10_施工单_Track_10C_ShotGroup镜头分组防果冻与VSR前置补帧拓扑.md
└── 05_CineRTX_Phase10_施工单_Track_10D_异构多文件动态任务队列与三端重构.md
```

#### 1. Track 10A：零附属 DLL 提纯与 PTX 瘦身
- **工程落脚点**：[`CMakeLists.txt`](../../CMakeLists.txt)、[`p010_pack.cu`](../../src/shim/ngx_bridge/p010_pack.cu)
- **核心评审点**：
  1. `rtx_ngx_bridge` 与 `rtx_core` 的静态合并方案是否彻底消除了独立 DLL 依赖且满足零符号泄漏；
  2. 剔除死核后的 PTX 字节码尺寸与运行时 JIT 编译开销是否达到极简优化；
  3. PE 导入表对外部多媒体/闭源驱动 DLL 的零静态链接隔离是否坚固。

#### 2. Track 10B：NVENC CQ/QP 与 CPU CRF 纯净 10-bit P010 软编
- **工程落脚点**：[`cpu_video_encoder.h`](../../include/rtx/cpu_video_encoder.h)、[`cpu_video_encoder.cpp`](../../src/io/cpu_video_encoder.cpp)、[`nv_encoder.cpp`](../../src/shim/loader/nv_encoder.cpp)
- **核心评审点**：
  1. NVENC 硬件编码中 CQ 模式（`NV_ENC_PARAMS_RC_VBR` + `targetQuality` + `enableAQ`）与阶梯 QP（I/P/B 帧阶梯补偿）的映射逻辑是否符合工业级画质标准；
  2. 纯 CPU 软件编码器对 10-bit P010 YUV420P10LE 的色彩空间转换、HDR10 元数据透传（Color Primaries / Transfer / Matrix / Mastering Display / MaxCLL）与时间戳（Flicks PTS/DTS）平移计算是否严密无漂移；
  3. CPU 软编后备管线与 GPU 硬编切换时的状态一致性。

#### 3. Track 10C：ShotGroup 镜头分割与 `vsr-first` 拓扑防果冻
- **工程落脚点**：[`shot_segmenter.h`](../../include/rtx/shot_segmenter.h)、[`shot_segmenter.cpp`](../../src/core/shot_segmenter.cpp)、[`rife_engine.cpp`](../../src/shim/ngx_bridge/rife_engine.cpp)
- **核心评审点**：
  1. 基于 $8 \times 8$ 宏块下采样的全局与局部双阈值切镜检测算法对高动态场景与硬切镜的抗噪度与敏感度平衡；
  2. 触发切镜时强制重置 VSR 时域状态（Reset Temporal）、通知下游编码器强制插入 IDR 关键帧的信号链路是否完备；
  3. `vsr-first`（先超分后补帧）与 `fruc-first`（先补帧后超分）拓扑切换的显存占用与计算管线隔离。

#### 4. Track 10D：`EncoderJobQueue` 异构动态多任务调度
- **工程落脚点**：[`job_queue.h`](../../include/rtx/job_queue.h)、[`job_queue.cpp`](../../src/core/job_queue.cpp)
- **核心评审点**：
  1. 状态机迁移逻辑（`Pending` -> `Running` -> `Completed`/`Failed`/`Cancelled`）的并发安全性与原子操作；
  2. 资源互斥（GPU 编码会话显存占用上限）与任务取消时上下文快速回滚与资源释放机制；
  3. Win32 本地 GUI、WebUI 以及 CLI 终端三端共享任务队列状态时的接口一致性。

---

### 三、 随附物证与验证手段

为便于专家展开重现与验证，当前工作区环境与构建门禁已全部就绪：
- **构建平台**：Windows 11 / MSVC 19.44 / CUDA 12.9 / TensorRT 10.13.3.9
- **一键构建验证**：
  ```pwsh
  python scratch/configure.py cmake --build --preset windows-msvc-ninja
  ```
- **全量单元测试（78 项用例）**：
  ```pwsh
  python scratch/configure.py ctest --preset windows-msvc-ninja --output-on-failure
  ```
- **合规性校验**：
  ```pwsh
  python scratch/configure.py cmake -P cmake/AuditSpdx.cmake
  python scratch/configure.py cmake -DBUILD_DIR=.vs/build/cmake/windows-msvc-ninja -P cmake/AuditImports.cmake
  ```

---

### 四、 期望交付物

请专家针对上述四个分轨出具《Phase 10 技术评审与架构合规意见书》，重点阐述：
1. **架构合理性评定**（Pass / Conditional Pass / Reject）；
2. **潜在风险点与边界盲区提示**（特别是极端高码率、多路并发、异常帧率输入等边界情况）；
3. **后续阶段演化建议**。

特此委托！
