# CineRTX Phase 10（v0.3.0）：全栈架构提纯与核心算法演进（六大核心攻坚）SuperPower 施工总纲

- **文档编号**：`CINERTX-P10-MASTER-PLAN-001`
- **版本基线**：承接 `v0.2.0`（Phase 9.1 Gold Master），迈向 `v0.3.0` 全栈架构提纯与画质/调度跃迁
- **设计哲学**：`KISS` + `奥卡姆剃刀（如无必要，勿增实体）` + `C++26 严苛 RAII / Designated Initializers / std::expected` + `单一事实源（SSOT）` + `最小完备机制（Minimally Sufficient Mechanism）`
- **编制角色**：音视频与异构计算首席领域专家

---

## 一、 六大核心任务全链路根因取证与因果推演总表

在 `v0.2.0` 正式封板上线后，针对全仓 `187` 项追踪文件（`~950 KB` C++/CUDA 源码 + `1.32 MB` PTX 文本）的穿透式审计，六大核心问题的精确代码落点、物理成因与架构解空间汇总如下：

| 序号 | 核心议题 | 现网精确病灶落点（File : Line） | 物理/工程根因推演 | Phase 10 根治方案与目标不变量 |
| :--- | :--- | :--- | :--- | :--- |
| **1** | **代码卫生、架构错配与非良构债务大清洗** | 1. `src/shim/ngx_bridge/p010_pack.cu:301-410, 614-857` & `p010_pack.ptx`（`37,952` 行 / `1.32 MB`）<br>2. `src/shim/ngx_bridge/p010_pack_runtime.cpp:488-659`<br>3. `CMakeLists.txt:382-446` & `src/core/ngx_remaster.cpp`<br>4. `src/core/hw_transcode.cpp:620-1325`（705 行单函数 + 5 层嵌套 `[&]` Lambda）<br>5. `src/gui/rtx_gui.cpp:85-125`（21 个散装文件级全局变量） | 1. **PTX 巨石死代码**：`rtx_rgba16f_to_p010_bt2020nc`（v1 旧核）与 `rtx_cuda_remaster_fused_p010`（Catmull-Rom 软超分核）在生产管线中 **0 调用**，因 `#pragma unroll` 展开导致 `p010_pack.ptx` 膨胀至 `1.32 MB`。<br>2. **动静分裂与重复编译**：Windows 下 `rtx_ngx_bridge` 同时编译 `SHARED` 与未使用的 `STATIC` 两套目标，且 `ngx_remaster.cpp` 维持 400 行 `GetProcAddress` 动态反射壳。<br>3. **上帝函数与全局散装状态**：`runNvidiaRemasterTranscode` 将解复用、解码、插帧、超分、SEI 合成、编码交织塞入单一 705 行栈帧。 | 1. 物理拔除 2 枚死核，`p010_pack.ptx` 体积从 `1.32 MB` 压降至 **`< 180 KB`（缩减 86%）**；将 `p010_pack_runtime.cpp` 内联 PTX 归口至 `.cu`。<br>2. 统一将 `rtx_ngx_bridge` 静态并入 `rtx_core`（`RTX_EMBEDDED_NGX_BRIDGE=1`），**消灭 `rtx_ngx_bridge.dll` 与 500 行动态加载反射表**。<br>3. 将 705 行大函数拆解为良构 C++26 管线类 `RemasterPipeline`，将 GUI 21 个全局变量收敛至 `WorkstationApp` 单一 RAII 上下文。 |
| **2** | **`CMake Warning: RTX_BUILD_TESTS` 告警定位** | 1. `CMakePresets.json:14`（`base` 预设）<br>2. `CMakePresets.json:69`（`windows-msvc` 预设）<br>3. `CMakeLists.txt:711` | Phase 8 Stage 1 遵循奥卡姆剃刀从 `CMakeLists.txt` 移除了 `option(RTX_BUILD_TESTS ...)` 并改为无条件构建 `rtx_tests`，但 `CMakePresets.json` 的 `cacheVariables` 仍残留 `"RTX_BUILD_TESTS": "ON"`，触发 CMake 未使用变量告警。 | 从 `CMakePresets.json` 彻底移除 `"RTX_BUILD_TESTS": "ON"`（现网已先行落地修复），确保全新 Configure **0 Warning**。 |
| **3** | **编码器扩充 `CQ`、`QP` 与纯 CPU `CRF` 模式** | 1. `include/rtx/param_schema.h:51`（仅 `cbr\|vbr\|qp`，默认 `cbr`）<br>2. `src/shim/loader/nv_encoder.cpp:267-294`（无 `targetQuality`、硬编码 `enableAQ = 0`、`constQP` 无 I/P/B 偏置）<br>3. `cmake/RtxFfmpegStatic.cmake:385,637`（禁用视频软编） | 1. 缺乏 NVENC 最核心的感知恒定质量模式 **`CQ`**（`NV_ENC_PARAMS_RC_VBR` + `targetQuality` + `-spatial-aq 1`）。<br>2. `QP` 模式将 I/P/B 帧设死同一 QP，浪费 P/B 帧码率。<br>3. FFmpeg 静态构建未启用 `libsvtav1` / `libx265`，无法执行纯 CPU `CRF` 高压缩率母版压制。 | 1. 新增 `rate_control = "cq"` 并设为**全局默认**（NVENC `VBR` + `targetQuality=22` + `enableAQ=1, aqStrength=8`）。<br>2. 升级 `qp` 支持 I/P/B 阶梯偏置（$\Delta_{\text{I}}=-2, \Delta_{\text{P}}=0, \Delta_{\text{B}}=+2$）。<br>3. 在 `RtxFfmpegStatic.cmake` 静态集成 `libsvtav1` / `libx265`，新增 `CpuVideoEncoder` 支持 10-bit HDR10 纯 CPU **`crf`** 模式。 |
| **4** | **场景切换果冻效应（Scene-Cut Jelly Artifact）根治与类 GOP 分组补帧** | 1. `src/core/hw_transcode.cpp:1058-1079`（NVOFA 路径 **0 切镜检测**）<br>2. `src/shim/ngx_bridge/rife_engine.cpp:960-962`（全局均值 SAD 阈值 `0.35f` 严重迟钝）<br>3. `src/core/hw_transcode.cpp:993-1082`（无镜头边界分组隔离、无 NGX VSR Reset、无 IDR 强制对齐） | 1. **NVOFA 裸奔**：默认 `60-nvofa` 完全不检测切镜，跨镜头强行算 $4\times 4$ 光流并双向 Warp，产生全屏撕裂果冻。<br>2. **RIFE 阈值失灵**：`diff_norm > 0.35f` 要求全图平均灰度跳变超 89.25（35%），真实切镜大多在 `0.08~0.22`，导致 90% 切镜漏检并跨镜融合。<br>3. **时域跨镜污染**：未按镜头分组（Shot-Group），切镜处未重置 NGX VSR 时域反馈缓存，也未向编码器发 `FORCEIDR`。 | 1. 打造统一 GPU 双指标切镜检测核（**$8\times 8$ 分块局部突变率 $p_{\text{outlier}}$ + 滑动窗口自适应基线 $3.2\bar{d}_{\text{motion}}$**），同时赋能 RIFE 与 NVOFA。<br>2. 引入类 GOP 的 **`ShotGroupSegmenter`（镜头组状态机）**：每个连续镜头独立闭环补帧，镜头切点严禁跨组插帧（尾部 Hold-Last-Frame 相位对齐），并同步触发 **NGX VSR `Reset=1`** 与 **Encoder `ForceIDR`**。 |
| **5** | **管线拓扑顺序反转：`VSR -> FG`（先超分后补帧）vs `FG -> VSR`** | 1. `src/core/hw_transcode.cpp:971-1089`（先对低分 `NV12` 调 `interpolate*`，再对每帧输出调 `remaster`）<br>2. `src/shim/ngx_bridge/bridge_exports.cpp:672-958` | 当前 `FG -> VSR` 拓扑存在两大物理缺陷：<br>1. **VSR 算力惩罚 $2\times\sim 5\times$**：`24->120fps` 时，每秒 96 帧插值帧也要重复跑 4K NGX VSR + TrueHDR。<br>2. **伪影非线性放大与呼吸效应**：低分插帧的亚像素重影被 NGX VSR Level 4 当作边缘锐化放大，且合成帧混入真实帧破坏 VSR 时域滤波器分布。 | 重构支持 **`pipeline_order = "vsr-first"`（新默认：先超分后补帧）** 与 `"fg-first"` 双拓扑：<br>1. 原始帧仅做 1 次 `NGX VSR -> TrueHDR -> P010`（省去 60%~80% VSR 开销，时域 100% 纯净）。<br>2. 补帧在超分后的 4K 10-bit `P010` 锚点帧对 `anchor_ring[2]` 上通过 **金字塔光流引导高分合成（Guided 4K P010 Synthesis）** 产出中间帧，兼得 4K 锐利边缘与 120fps 吞吐。 |
| **6** | **Encoder 模式异构多文件动态任务队列重构** | 1. `src/gui/rtx_gui.cpp:110, 502-532`（`std::vector<std::wstring> g_batchQueue` 静态路径数组）<br>2. `webui/server.py:208-293`（单任务 `JobRunner`）<br>3. `src/cli/main.cpp:475-552` | 现有队列仅保存文件路径字符串，所有文件强制共享同一套参数，且启动瞬间拷贝静态副本并锁死 UI——运行期无法动态追加/删除任务、无法为不同任务配置不同转码参数、无法在单个任务内打包多文件批次。 | 在 `rtx_core` 建立 **`rtx::EncoderJobQueue`** 领域核心：<br>1. 每个 `EncoderJob` 独立封装 `inputs (1..N 文件)` + 专属 `TranscodeProfile` 快照 + 独立状态机。<br>2. 常驻生产者-消费者工作线程支持**运行期实时入队、移除未执行任务、调整优先级、修改排队任务参数、单任务 Skip/Retry**，三端（Win32 多列表格 / WebUI / CLI Stdio）1:1 同构。 |

---

## 二、 四大平行施工轨道（Track 10A ~ Track 10D）物理文件隔离矩阵

为确保多工程师并行推进 TDD 施工时**零文件冲突（Zero Merge Conflict）**，Phase 10 严格按模块边界划分为 4 条正交施工轨道：

| 施工轨道 | 核心职责 | 独占修改/新增文件清单 | 严禁触碰边界 |
| :--- | :--- | :--- | :--- |
| **Track 10A**<br>代码卫生与零附属 DLL 提纯 | 1. 清理 `CMakePresets.json` `RTX_BUILD_TESTS` 残留<br>2. 拔除 `p010_pack.cu` 2 枚死核并将 `nvof_warp` 归口至 `.cu`/`.ptx`<br>3. 合并 `rtx_ngx_bridge` 静态直链入 `rtx_core`，拔除 `rtx_ngx_bridge.dll` 与动态加载反射壳<br>4. 抽取 `include/rtx/string_utils.h` 消除跨 TU 重复工具函数 | `CMakePresets.json`<br>`CMakeLists.txt`<br>`include/rtx/string_utils.h` *(New)*<br>`src/core/ngx_remaster.cpp`<br>`src/core/nvidia_shim.cpp`<br>`src/shim/ngx_bridge/p010_pack.cu`<br>`src/shim/ngx_bridge/p010_pack.ptx`<br>`src/shim/ngx_bridge/p010_pack_runtime.cpp`<br>`src/shim/ngx_bridge/bridge_exports.cpp` | 严禁修改 GUI 窗口布局与任务队列逻辑 |
| **Track 10B**<br>全模态编码引擎（`CQ` / `QP` / CPU `CRF`） | 1. 升级 `nv_encoder.cpp` 实现 NVENC `CQ`（`targetQuality` + `enableAQ=1`）与 I/P/B 阶梯 `QP`<br>2. 扩展 `RtxFfmpegStatic.cmake` 静态编译 `libx265` / `libsvtav1`<br>3. 新增 `src/io/cpu_video_encoder.cpp` 实现 10-bit P010 双缓冲异步 DtoH + CPU `CRF` 压制与 HDR10/HDR10+ 侧数据封装 | `cmake/RtxFfmpegStatic.cmake`<br>`include/shim/rtx_shim_abi.h`<br>`src/shim/loader/nv_encoder.cpp`<br>`include/rtx/cpu_video_encoder.h` *(New)*<br>`src/io/cpu_video_encoder.cpp` *(New)*<br>`tests/test_io.cpp` | 严禁修改 RIFE / NVOFA 插帧内核 |
| **Track 10C**<br>Shot-Group 防果冻分段与 `VSR -> FG` 双拓扑管线 | 1. 实现 `$8\times 8$ 分块突变 + 自适应基线 SAD` 双指标切镜检测核，赋能 RIFE 与 NVOFA<br>2. 实现 `ShotGroupSegmenter`（类 GOP 镜头分组隔离、NGX VSR `Reset=1`、编码器 `ForceIDR`）<br>3. 重构 `hw_transcode.cpp` 为良构 `RemasterPipeline` 类，实现 `vsr-first`（先超分后补帧：P010 双锚点环 + 光流引导 4K P010 合成）与 `fg-first` 可切换拓扑 | `include/rtx/shot_segmenter.h` *(New)*<br>`src/core/shot_segmenter.cpp` *(New)*<br>`include/rtx/hw_transcode.h`<br>`src/core/hw_transcode.cpp`<br>`src/shim/ngx_bridge/ngx_d3d12_bridge.h`<br>`src/shim/ngx_bridge/ngx_d3d12_bridge.cpp`<br>`src/shim/ngx_bridge/rife_pack.cu`<br>`src/shim/ngx_bridge/rife_pack.ptx`<br>`src/shim/ngx_bridge/rife_engine.cpp`<br>`src/shim/ngx_bridge/nvof_engine.cpp`<br>`tests/test_rife.cpp` | 严禁修改 Win32 GUI 控件树 |
| **Track 10D**<br>异构多文件动态任务队列与三端重构 | 1. 扩展 `param_schema.h/.cpp` 新增 `rate_control` (`cq`/`crf`) 与 `pipeline_order` (`vsr-first`/`fg-first`)<br>2. 新增 `include/rtx/job_queue.h` & `src/core/job_queue.cpp` 实现多文件异构参数动态任务队列 `EncoderJobQueue`<br>3. 重构 `rtx_gui.cpp`（收敛 `WorkstationApp` 上下文 + 多列任务队列 ListView + 运行期动态增删改排）、`main.cpp`（`--session-stdio` 动态队列指令）与 `webui/` | `include/rtx/param_schema.h`<br>`src/core/param_schema.cpp`<br>`include/rtx/i18n.h`<br>`src/core/i18n.cpp`<br>`include/rtx/job_queue.h` *(New)*<br>`src/core/job_queue.cpp` *(New)*<br>`src/gui/workstation_canvas.h`<br>`src/gui/workstation_canvas.cpp`<br>`src/gui/rtx_gui.cpp`<br>`src/cli/main.cpp`<br>`webui/server.py`<br>`webui/app.js`<br>`webui/index.html`<br>`tests/test_gui_workstation.cpp` | 严禁修改底层 CUDA PTX 核函数 |

---

## 三、 Phase 10 施工分册导航索引

1. **[`01_CineRTX_Phase10_架构提纯与核心算法演进SuperPower总纲.md`](01_CineRTX_Phase10_架构提纯与核心算法演进SuperPower总纲.md)**（本文档：全局根因取证、架构契约、轨道隔离与总门禁）
2. **[`02_CineRTX_Phase10_施工单_Track_10A_代码卫生与零附属DLL提纯.md`](02_CineRTX_Phase10_施工单_Track_10A_代码卫生与零附属DLL提纯.md)**（任务 1 & 任务 2：PTX 瘦身 86%、消灭 `rtx_ngx_bridge.dll` 双重编译、拔除死符号与 `RTX_BUILD_TESTS` 清理）
3. **[`03_CineRTX_Phase10_施工单_Track_10B_全模态编码引擎_CQ_QP_与CPU_CRF.md`](03_CineRTX_Phase10_施工单_Track_10B_全模态编码引擎_CQ_QP_与CPU_CRF.md)**（任务 3：NVENC `CQ` 感知恒定质量、阶梯 `QP` 与 `libsvtav1`/`libx265` 纯 CPU `CRF` 10-bit 管线）
4. **[`04_CineRTX_Phase10_施工单_Track_10C_ShotGroup镜头分组防果冻与VSR前置补帧拓扑.md`](04_CineRTX_Phase10_施工单_Track_10C_ShotGroup镜头分组防果冻与VSR前置补帧拓扑.md)**（任务 4 & 任务 5：类 GOP `ShotGroupSegmenter` 防果冻切镜隔离、NGX VSR 时域重置与 `Decode -> VSR -> FG` 先超分后补帧架构）
5. **[`05_CineRTX_Phase10_施工单_Track_10D_异构多文件动态任务队列与三端重构.md`](05_CineRTX_Phase10_施工单_Track_10D_异构多文件动态任务队列与三端重构.md)**（任务 6：`rtx::EncoderJobQueue` 多文件异构参数动态队列、`WorkstationApp` 状态提纯与 Win32/WebUI/CLI 三端协同）

---

## 四、 Phase 10 终审量化验收六大门禁（Gate 10.1 ~ Gate 10.6）

- [ ] **Gate 10.1（零告警配置与代码体积瘦身门禁）**：
  - 全新删除 `.vs/build/cmake/windows-msvc-ninja` 后执行 `cmake --preset windows-msvc-ninja`，控制台输出 **0 `CMake Warning`**（彻底无 `RTX_BUILD_TESTS` 警告）。
  - `src/shim/ngx_bridge/p010_pack.ptx` 体积 $\le 180\text{ KB}$（较 `v0.2.0` 的 `1,320,609 B` 缩减 $\ge 85\%$），且 `rtx_ngx_bridge` 与 `rtx_shim` 统一静态合入 `CineRTX.exe`，构建产物目录不再产生多余的自有 `rtx_ngx_bridge.dll`。
- [ ] **Gate 10.2（良构模块化与零散装全局状态门禁）**：
  - `src/core/hw_transcode.cpp` 中不存在任何超过 `120` 行的单一函数，不存在嵌套超过 `1` 层的捕获型 Lambda；`src/gui/rtx_gui.cpp` 中散装 `g_*` 全局变量数量 $\le 1$（唯一 `g_app` 实例）。
- [ ] **Gate 10.3（编码器 `CQ` / `QP` / CPU `CRF` 全模态实证门禁）**：
  - 同一段 1080p 测试源分别以 `--rate-control cq --qp 22`、`--rate-control qp --qp 24`、`--codec av1_cpu --rate-control crf --qp 24`、`--codec hevc_cpu --rate-control crf --qp 22` 跑通 10-bit HDR10 转码，`ffprobe` 校验输出色彩元数据（`bt2020nc / smpte2084 / MasteringDisplay`）100% 完备，且不同 `cq`/`crf` 档位（如 `18` vs `28`）输出文件大小严格单调递减（比特率差异 $\ge 2.2\times$）。
- [ ] **Gate 10.4（Shot-Group 切镜零果冻实证门禁）**：
  - 构造包含 **5 处硬切镜头（含同色调正反打切镜，帧间全局平均灰度差 `0.10 ~ 0.16`）** 的测试视频，分别运行 `120-rife` 与 `120-nvofa`：
  - `report.fruc_scene_cuts == 5`（100% 命中全部 5 处切镜，0 漏检）；
  - 切镜边界处输出的中间帧与前/后真实镜头帧的像素级误差 $\text{MAE} = 0.0$（严禁出现任何跨镜头半透明融合或光流撕裂果冻帧），且切镜后首帧编码包标记为 `IDR Keyframe`。
- [ ] **Gate 10.5（`VSR -> FG` 先超分后补帧画质与吞吐实证门禁）**：
  - 在 `--pipeline-order vsr-first`（先超分后补帧）下执行 `1080p 24fps -> 4K 120fps` 转码：
  - `report.remaster_pack_launches == report.frames_decoded`（NGX VSR 仅对原始解码帧调用 1 次，调用次数为 `fg-first` 模式的 $1/5$）；
  - 对比 `vsr-first` 与 `fg-first` 的 4K 120fps 输出帧序列，`vsr-first` 在字幕边缘与高频网格处的时域闪烁指标（Temporal Edge Flicker）显著低于 `fg-first`，且总转码吞吐 FPS 提升 $\ge 35\%$。
- [ ] **Gate 10.6（Encoder 动态多文件异构任务队列门禁）**：
  - 在 Job #1（1 个输入文件，`av1_nvenc + cq 22 + 4K 120-rife`）处于 `Running` 状态时，动态向队列追加 Job #2（3 个输入文件，`hevc_nvenc + qp 20 + 2K 60-nvofa`）与 Job #3（2 个输入文件，`av1_cpu + crf 26 + native original`），并在运行期动态删除或重排排队任务：
  - 工作线程零阻塞、零崩溃，按最新队列顺序依次跑完每个 Job 下辖的全部输入文件，每个输出文件严格匹配各自所属 `EncoderJob` 的独立参数。
