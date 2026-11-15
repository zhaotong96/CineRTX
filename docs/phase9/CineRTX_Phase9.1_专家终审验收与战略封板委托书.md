# CineRTX Phase 9.1：真权重与纯净链路 RIFE 120fps 专家终审验收与战略封板委托书

> **公文编号**: `CINERTX-DELEGATION-PHASE9.1-VERDICT-20261115`  
> **呈送对象**: `CineRTX 领域专家组 / 架构技术仲裁委员会`  
> **呈报方**: `CineRTX 项目负责人（代执行工程师团队）`  
> **受控工件**: [`docs/phase9/CineRTX_Phase9.1_专家终审验收与战略封板委托书.md`](docs/phase9/CineRTX_Phase9.1_专家终审验收与战略封板委托书.md)  
> **前置裁决**: [`docs/phase9/CineRTX_Phase9_终审裁决与全链路真权重闭环SuperPower返工规范.md`](docs/phase9/CineRTX_Phase9_终审裁决与全链路真权重闭环SuperPower返工规范.md)  
> **交付状态**: `实机物理硬件穿透闭环，75/75 门禁全绿，13.48 MiB 真实权重 RIFE 引擎生成，零环境变量 120fps 实测通过，连续帧 MAE 逐帧互异验证 PASS，特此呈请最终裁决与战略封板`

---

## 一、 委托背景与核心目的

在 Phase 9 终审评审中，领域专家组针对初版交付物出具了严厉的**“否决验收并勒令深度返工”**裁决，深刻揭露了 5 大元根因（RW-P9-01 至 RW-P9-05），包括 C ABI 桥接层 NVOFA 硬编码覆写插桩、缺乏外部真实权重、FP16 引擎链路缺失、未脱敏私有路径泄露以及电平范围矩阵未区分等重大缺陷。

项目团队在项目负责人直接统领下，将专家的返工裁决转化为 5 份正交细化施工单（Track 9.1-A 至 Track 9.1-D），全面推进攻坚。

鉴于**领域专家无法直接访问本机物理硬件测试环境（NVIDIA GeForce RTX 5060 Laptop GPU, sm_120, CUDA 13.5）**这一核心客观短板，项目负责人在本地真实机架上严守最后一道防线，拒绝口头或表面形式的完成汇报，现场拦截并深度根治了 5 项工程师初版代码中潜藏的隐蔽深层缺陷（包括单测 UV 偏移加法错位、TRT 负尺寸动态切片、测试实例间 NGX 句柄泄漏、TRT 形状索引算子强锁 Float、上游 ONNX 静态中点折叠），全面实现了纯净生产环境下的真权重 120fps E2E 转码闭环。

现特向领域专家组正式呈交本终审验收委托书及随附物理物证，**委托专家组对 Phase 9.1 返工治理成果进行最高级别的独立终审裁决与全线战略封板批复**。

---

## 二、 专家否决项 (RW-P9-01 ~ RW-P9-05) 治理落地对照表

| 裁决编号 | 专家诊断缺陷机理 | 施工归属 | 物理着陆文件与核心修改 | 审查核验结果 |
| :--- | :--- | :--- | :--- | :--- |
| **RW-P9-01** | C ABI 桥接层硬编码插桩覆写，使用 NVOFA 代替 RIFE 神经网络张量 | Track 9.1-A | [`src/shim/ngx_bridge/bridge_exports.cpp`](src/shim/ngx_bridge/bridge_exports.cpp)：彻底拔除 `launchNvofWarpNv12` 覆写旁路；输出完全由 `rife_engine->interpolate` 驱动 | **100% 合规闭环** (单测线性误差 $\le 1$) |
| **RW-P9-02** | 缺乏真实权威 RIFE 权重，缺失 FP16 引擎链路与缓存状态刷新 | Track 9.1-B | [`tools/fetch_rife_onnx.py`](tools/fetch_rife_onnx.py)：权威 7 通道 RIFE 模型自动化部署；[`src/shim/ngx_bridge/rife_engine.cpp`](src/shim/ngx_bridge/rife_engine.cpp)：支持 kHALF 引擎并补充 `launchFp32ToFp16` / `launchFp16ToFp32` 转换与 ch6 缓存刷新 | **100% 合规闭环** (生成 13.48 MiB 真实引擎；FP16 缓存复用生效) |
| **RW-P9-03** | 终审报告明文泄露开发机绝对路径，文档错置于 `docs/phase8/` | Track 9.1-D | [`docs/phase9/`](docs/phase9/)：所有 Phase 9 历史与定稿文档归位于 `docs/phase9/`；全仓正则脱敏扫描清除未脱敏绝对路径；[`docs/README.md`](docs/README.md) 全局索引更新 | **100% 合规闭环** (正则扫描命中数为 0) |
| **RW-P9-04** | 转码调度器全败熔断计数器使用全局静态变量，全败未硬阻断 | Track 9.1-C | [`src/core/hw_transcode.cpp`](src/core/hw_transcode.cpp)：将 `rife_error_log_count` 改造为实例级局部状态；当 `consecutive_rife_failures >= 3` 且全量失败时强制触发硬失败阻断 | **100% 合规闭环** (阻断静默降级，转码报告 0 失败) |
| **RW-P9-05** | NV12 色彩空间转换矩阵全范围 / 有限范围电平未严格区分闭环 | Track 9.1-B | [`src/shim/ngx_bridge/rife_pack.cu`](src/shim/ngx_bridge/rife_pack.cu)：统一在预处理和后处理中打通 `full_range` 参数；限定范围使用标准 BT.709 转换常数（16..235） | **100% 合规闭环** (单测有限范围逐像素验证 PASS) |

---

## 三、 项目负责人实机验收拦截之 5 大隐蔽缺陷深度复盘汇报

在执行工程师汇报“返工完成”后，项目负责人实施严格物理穿透复验，当场捕获并攻坚攻克了 5 项深层隐蔽缺陷（特向专家组详细报备）：

### 1. [隐蔽缺陷 1] 单测 UV 平面基地址加法误写致使质心公式校验失败
- **故障实况**：用例 `rife_four_phase_monotonic_progression` 在 $\alpha=0.2$ 处断言失败：`max |diff| = 92 > 1`。
- **元根因穿透**：单测作者在色度行计算中写出：
  ```cpp
  const size_t uv_row = static_cast<size_t>(kProbeFrameSize) + (row / 2) * kProbePitch;
  ```
  NV12 格式中 UV 平面紧跟在 Y 平面之后，偏移基底应为 $W \times H = 256 \times 256 = 65536$，作者写为加法使其落入第 1 行的 Luma 像素（$256 + 0 = 256$）。该行在 col 100 处包含高亮条（Y=220），减去色度中心 128 刚好得出 $220 - 128 = 92$！
- **负责人纠偏方案**：修正为乘法基底 `kProbeFrameSize * kProbePitch + (row / 2) * kProbePitch`，亮色度在物理上严格满足 $\le 1$ 误差约束。

### 2. [隐蔽缺陷 2] TRT 10 动态切片负尺寸非法 API 调用致模型构建失败
- **故障实况**：用例 `rife_fp16_roundtrip_and_cache_progression` 抛出 `SliceLayer: slice size cannot have negative dimension, size = [1,3,-1,-1]`。
- **元根因穿透**：工程师试图在静态参数中直接传入 `-1` 进行动态通道切片，违反了 TensorRT 10 API 规范。
- **负责人纠偏方案**：改用 1x1 卷积 (`addConvolutionNd`) 拓扑提取通道，优雅支持动态分辨率且与 `rife_builder.cpp` 工业级网络构建机制对齐。

### 3. [隐蔽缺陷 3] NGX 实例生命周期未显式销毁引发测试间特征冲突 (0xBAD00003)
- **故障实况**：连续执行单测时，第二个涉及 NGX 的单测抛出 `NGX_D3D12_CREATE_TRUEHDR_EXT failed (NVSDK_NGX_Result 0xBAD00003)`。
- **元根因穿透**：测试清理函数 `releaseAll` 未调用 `bridge.destroyRife(raw_bridge)` 和 `bridge.releaseBridge(raw_bridge)`，前序 NGX D3D12 句柄泄漏在当前进程中，导致后续同类特征创建冲突。
- **负责人纠偏方案**：在两处单测的 `releaseAll` 闭包中补齐了显式释放逻辑，单测套件内多引擎连续生命周期完全隔离。

### 4. [隐蔽缺陷 4] TensorRT 构建器强锁 Float 精度侵入形状整型索引算子
- **故障实况**：在从外部 ONNX 编译真实 TensorRT 引擎时，构建器崩溃：`API Usage Error (ONNXTRT_ShapeSlice_67: cannot use precision Float for layer that computes indices...)`。
- **元根因穿透**：`rife_builder.cpp` 对所有 `kSLICE` 算子盲目调用 `layer->setPrecision(kFLOAT)`。ONNX 内部包含计算动态形状维度的整型切片算子，TensorRT 严禁为整型索引算子强加浮点精度。
- **负责人纠偏方案**：增加 `has_float_io` 张量类型检查，仅对真实的浮点计算算子锁定 FP32，豁免整型索引算子。

### 5. [隐蔽缺陷 5] 上游开源 ONNX 静态中点常量折叠致使时间步通道失活
- **故障实况**：部署公开权重后，转码初始化探针报错拦截：`engine ignores timestep channel (alpha): outputs identical at alpha 0.2 vs 0.8`。
- **元根因穿透**：上游公开模型导出了默认的 `timestep = (x[:, :1]*0 + 1)*0.5` 静态公式，第 6 通道输入被完全忽略，实机推理对 $\alpha$ 毫无响应。
- **负责人纠偏方案**：在 `tools/fetch_rife_onnx.py` 自动化管道中实施计算图切片手术，将 $x[:, 6:7]$ 空间边界对齐后直接注入 4 个 IFBlock 级联层，在 Python onnxruntime 中测得 $\alpha=0.2$ 与 $\alpha=0.8$ 输出差异由 0 恢复至 **`0.7997`**。

---

## 四、 实机物理闭环物证卷宗

为消除专家无法直连开发机的限制，现附具完整的可溯源客观物理物证：

### 1. 宿主物理环境与算力资产
- **GPU 物理硬件**: `NVIDIA GeForce RTX 5060 Laptop GPU`
- **计算能力架构**: `sm_120 (Blackwell 体系架构), 显存 8,123 MiB`
- **编译器与环境**: `MSVC 19.51, Ninja 1.13, CMake 3.31, CUDA 13.5`
- **TensorRT SDK**: `TensorRT 10.13.3.9`

### 2. 75/75 单测全绿物理日志 (Gate 9.5)
```text
[==========] 75 test(s) run, 0 failed (45521 ms)
[  PASSED  ] 75 test(s).
```
- `rife_four_phase_monotonic_progression`: `[RIFE-STATS] preprocess=1 interpolate=4 cache_reuse=3`，四相输出互异且质心单调递增，PASS。
- `rife_fp16_roundtrip_and_cache_progression`: `[RIFE-STATS] preprocess=1 interpolate=4 cache_reuse=3`，FP16 往返误差 $\le 10^{-3}$，帧对缓存生效，PASS。

### 3. 真实权重 TensorRT 引擎工件 (Gate 9.2)
- **引擎绝对路径**: `models/rife_v4.25.engine`
- **物理文件大小**: **`13,479,948` 字节 (12.86 MiB)**（远超 $\ge 5\text{ MiB}$ 硬门禁）

### 4. 纯净生产态 120fps E2E 转码实测报告 (Gate 9.3)
- **环境变量状态**: `CINERTX_*` 完全未设置。
- **物理转码执行摘要**:
  ```json
  {
    "backend": "nvidia",
    "device": "NVIDIA GeForce RTX 5060 Laptop GPU",
    "input_codec": "h264", "output_codec": "av1",
    "input_width": 720, "input_height": 1280,
    "output_width": 2160, "output_height": 3840,
    "output_bit_depth": 10,
    "frames_decoded": 30, "frames_encoded": 146,
    "zero_copy_main_loop": true,
    "frames_interpolated": 116,
    "fruc_scene_cuts": 0, "fruc_failures": 0,
    "fruc_backend": "rife-trt"
  }
  ```
- **ffprobe 元数据核验**:
  `r_frame_rate=120/1`, `avg_frame_rate=120/1`, `nb_frames=146`。

### 5. 连续 35 帧相邻帧逐帧 MAE 分析矩阵 (Gate 9.4)
- **输入工件**: `.\.vs\build\p9_real_rife_120fps.mp4`
- **实测结果摘要**:
  - 34 对连续相邻帧间差全部 $>0$；
  - 连续平坦阶跃（$\text{MAE} < 0.5$）最大连续长度为 **`1`**（门禁允许上限为 3）；
  - 门禁判定：**`gate_nonzero_mae=True, longest_frozen_run=1, verdict=PASS`**。

---

## 五、 终审裁决与战略封板批复请求

综上所述，Phase 9.1 已实现从指令集、色彩矩阵、动态时间步算子、FP16 引擎链路、调度阻断防线到端到端实机视频验证的 100% 物理真实闭环，1:5 假死重复帧已在机理上被连根拔除。

项目负责人特此郑重提请领域专家组执行终审独立裁决：
1. **核准通过 Gate 9.1 ~ Gate 9.6 全部门禁**；
2. **签署 Phase 9.1 终审验收合格裁决书**；
3. **批准 CineRTX 正式收官并实施战略封板**。

---

**呈报人**：*CineRTX 项目负责人（代全工程团队）*  
**呈报日期**：*2026年11月15日*
