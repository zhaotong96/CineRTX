# CineRTX 纯 GPU 硬件零拷贝转码与视频重制引擎 · 领域专家终审与开源就绪请求书

> **文档标识**：`DOSSIER-FINAL-SIGNOFF-20261025`  
> **归档路径**：[`docs/phase2/CineRTX_领域专家终审与开源就绪请求书.md`](file:///C:/[REDACTED_PROJ]/docs/phase2/CineRTX_%E9%A2%86%E5%9F%9F%E4%B8%93%E5%AE%B6%E7%BB%88%E5%AE%A1%E4%B8%8E%E5%BC%80%E6%BA%90%E5%B0%B1%E7%BB%AA%E8%AF%B7%E6%B1%82%E4%B9%A6.md)  
> **起草日期**：2026-10-25  
> **提交方**：CineRTX 项目工程组与系统架构团队  
> **评审对象**：领域专家评审委员会（异构计算架构专家、音视频编解码专家、开源合规与系统安全专家）  
> **当前状态**：**终审签发请求中 (Pending Board Final Verdict)**  
> **前序基线**：一阶段专家终审通过、二阶段 W5/W6 验收通过、全量深度静态代码审计 12 项缺陷闭环、P0~P3 隐私与凭据清洗完成

---

## 1. 终审申请背景与执行概要 (Executive Summary)

CineRTX 是专为现代 NVIDIA RTX GPU 架构（Ada Lovelace / Blackwell）打造的纯 GPU 硬件级视频转码与 AI 画质重制引擎。项目自立项以来，严格遵循“纯 C ABI 物理隔离”、“物理零拷贝闭环（D2H/H2D 0 字节回传）”、“无托管单 exe Win32 GDI 自绘”以及“开源许可证严密隔离”四大顶层原则。

在此前的一阶段与二阶段施工验收基础上，工程组近期针对真实复杂生产环境与多线程异构并发极端边界，组织了最为严密的**深度静态代码审计**与**开源发布合规与隐私审计**：
1. **系统级缺陷闭环**：对《CineRTX 代码库深度静态审计》中识别出的 **12 项系统隐患（3 Critical, 4 High, 4 Medium, 1 Low）** 全部实现了微观级代码闭环合入，根除了 WDDM TDR 恢复失效、GPU 页表 UAF、动态几何切换泄漏、多线程 SPSC 竞争及 MSVC 浮点截断等硬核隐患；
2. **安全与隐私脱敏清零**：对新版《CineRTX 审计问题清单》指出的 **P0~P3 全量 31 项风险** 严格执行了“最小程度遮蔽与供应链加固”；
3. **供应链与开源法务筑基**：统一主许可证与例外条款为 `CineRTX Plugin-Exception`，理顺静态导入桩库（`nvsdk_ngx_s.lib`）法律关系；引入官方 SDK SHA256 双向哈希校验与坏包自愈；新增 `.gitattributes` 与 `SECURITY.md`。

各项工程门禁与实测指标已达到最高成熟度，特此向领域专家委员会正式提交终审请求书，提请专家团队签署开源就绪终审决议。

---

## 2. 核心架构与系统工程证据链 (System Architecture & Technical Verdict)

```
[输入视频 (MP4/MKV/MOV)]
         │
         ▼
 ┌──────────────┐     Zero-Copy VRAM Pool      ┌──────────────┐
 │  FfmpegDemux │ ───────────────────────────> │ NVDEC (CUVID)│
 └──────────────┘                               └──────┬───────┘
                                                       │ CUsurfObject (Zero-Copy)
                                                       ▼
 ┌────────────────────────────────────────────────────────────────────────┐
 │ 异构算力重制流水线 (DirectX 12 / CUDA / NGX Bridge)                     │
 │  • DXGIDeclareAdapterRemovalSupport 优先抢占 (防 TDR 机制 4)           │
 │  • CUsurfObject 莫顿序硬件映射 (无主机显存回读)                          │
 │  • D3D12 Queue 异步围栏同步 (Fence Timeline Monotonic)                 │
 │  • VSR 4K 超分 (v1.1) + TrueHDR 逆色调映射 (BT.2020 PQ / ST 2084)     │
 │  • P010 环形双缓冲 (Ping-Pong Ring) + 异常回滚保障                      │
 └────────────────────────────────────┬───────────────────────────────────┘
                                      │ NV_ENC_INPUT_RESOURCE_TYPE_CUDADEVICEPTR
                                      ▼
                               ┌──────────────┐
                               │ NVENC Main10 │ ──> [FfmpegMuxer 输出]
                               └──────────────┘
```

### 2.1 显存零拷贝完整性 (VRAM Zero-Copy Integrity)
* **物理证明**：解码表面通过 `cuvidMapVideoFrame64` 映射，直接交由 CUDA/D3D12 共享底层物理内存；重制后的 10-bit P010 显存表面通过设备指针直传 NVENC 编码器。全过程主机到设备（H2D）与设备到主机（D2H）传输字节数严格为 **0 字节**（遥测指示：`D2H 0 B · H2D 0 B (HW Verified)`）；
* **几何突变鲁棒性 (Issue 01 闭环)**：在视频流动态跳变分辨率时，强制同步解码流并完全销毁旧显存池中的 16 个底层 `CUevent` 驱动对象，重构底座 `CUvideodecoder`，彻底杜绝显存悬垂释放与越界读写。

### 2.2 驱动级防 TDR 与生命周期管理 (WDDM Stability & Anti-TDR)
* **API 初始化时序更正 (Issue 02 闭环)**：严格保证在调用 `cuInit(0)` 之前率先完成 NGX Bridge 的探测与 `DXGIDeclareAdapterRemovalSupport()` 注册，使 WDDM 图形子系统在驱动重置时能够向进程投递 `DXGI_ERROR_DEVICE_REMOVED`，彻底激活防 TDR 机制 4；
* **资源析构无 UAF (Issue 03 闭环)**：NgxD3D12Bridge 析构时绝不提交未决的残缺命令列表，必须先排空 GPU 队列围栏再安全释放分配器（`ID3D12CommandAllocator`）；CUDA 外部资源销毁前强制校验并绑定上下文（`bind_context`）。

### 2.3 无锁并发与内存安全模型 (Concurrency Model)
* **租约归还数据竞争消除 (Issue 04 闭环)**：针对多流水线线程（解码、编码、旁路）可能并发归还 `SurfaceLease` 导致单生产者队列（SPSC）数据竞争的问题，为 `SurfacePool::free_list_` 增加轻量互斥保护；将 `TdrGuard` 的并发提交与完成计数器全量升级为原子操作（`std::atomic<size_t>`）；
* **MESI 缓存行优化 (Issue 08 闭环)**：在 `SpscBoundedQueue` 中引入双端影子游标缓存（`head_cache_` / `tail_cache_`），大幅减少跨核 MESI 总线回写事务；构造函数严格校验容量幂次防止左移溢出死循环。

### 2.4 数值、色彩与码流防畸形安全 (Numerical & Bitstream Safety)
* **TrueHDR 中灰律钳位 (Issue 07 闭环)**：为场景切换检测器的高光比例因子追加 `std::min(1.0, (p - high_p) / span)` 截断保护，杜绝极亮或雪景画面下 `middle_gray` 出现负数（-7）导致 NGX 核心崩溃；
* **IEEE 754 舍入与 NaN 防篡改 (Issue 10 闭环)**：半精度转换函数 `floatToHalf` 严格保留 quiet NaN，消除 NaN 静默篡改为无穷大的隐患，并补齐偶数舍入逻辑；
* **有理数时基溢出消除 (Issue 06 闭环)**：实现无符号安全求绝对值 `absU64`，根除 `INT64_MIN` 有符号溢出未定义行为（UB）；MSVC 编译环境下实施高精度对称舍入；
* **抗畸形码流解析 (Issue 11 闭环)**：对指数哥伦布解码器增加防整数溢出校验；增加 NALU 超过 65535 字节的防截断保护。

---

## 3. 开源法务、安全供应链与隐私治理 (Compliance & Sanitization)

| 审计维度 | 治理标准与实施证据 | 专家复核要点 |
| :--- | :--- | :--- |
| **SPDX 许可证纯净度** | 全库 63 个核心模块声明 `GPL-3.0-or-later WITH Plugin-Exception`；33 个垫片与胶水层模块声明 `MIT OR Apache-2.0`。执行 `./tools/check_spdx.ps1` 报告：`licenses: GPL=63 MIT=33, binaries: 0, result: PASS`。 | 100% 零闭源专有头文件、0 导入符号污染。 |
| **例外条款名称对齐** | 统一主许可证与例外名称为 **`CineRTX Plugin-Exception`**，在 [`LICENSE`](file:///C:/[REDACTED_PROJ]/LICENSE) 与 [`COPYING.EXCEPTION`](file:///C:/[REDACTED_PROJ]/COPYING.EXCEPTION) 建立强绑定，彻底消除孤儿条款疑问。 | GitHub licensee 与合规自动化工具可准确解析。 |
| **静态导入桩库条款闭环** | 明确规定 Covered Work（GPL 转码核心）绝无静态链接；独立插件桥接库（`rtx_ngx_bridge.dll`，MIT/Apache）内部链接 `nvsdk_ngx_s.lib`，通过纯 C ABI 在运行期动态组合，完全符合例外条款定义。 | 消除商业发行与衍生开发中的法务歧义。 |
| **P0 凭据清零** | 针对历史文档中遗留的明文 SSH 账户密码与局域网代理，全量实施掩码：`[REDACTED]@192.168.***.***`、`密码: [REDACTED]`、`192.168.***.***:[REDACTED]`。 | 杜绝公共网络自动化爬虫探测与凭据窃取。 |
| **P1/P2 隐私与环境脱敏** | 主机名 `[REDACTED_HOST]` 脱敏，本地绝对路径 `C:\Users\[REDACTED_USER]\` 脱敏，兄弟项目名 `[REDACTED_PROJ]` 脱敏，123 条云盘外链 ID 脱敏；内核头文件中的构建机绝对路径全量清零。 | 消除个人身份推导与内部环境拓扑暴露。 |
| **供应链完整性校验** | [`CMakeLists.txt`](file:///C:/[REDACTED_PROJ]/CMakeLists.txt) 引入官方 SDK 归档 SHA256 强校验（`abf4f34e2b5a...`），并在下载前先行校验本地缓存，自动侦测并清理传输损坏的坏包。 | 防范中间人篡改或网络中断死锁。 |
| **跨平台规范与安全响应** | 注入 [`.gitattributes`](file:///C:/[REDACTED_PROJ]/.gitattributes) 实现代码库 LF 归一化与 Windows CRLF 平台检出自适应；注入 [`SECURITY.md`](file:///C:/[REDACTED_PROJ]/SECURITY.md) 提供私有漏洞披露指引。 | 消除跨平台换行污染与提供安全合规通道。 |

---

## 4. 全量自动化测试与工程门禁度量 (Verification Matrix)

在目标硬件环境（RTX 5060 Laptop GPU, sm_120, Driver 617.14, Windows 11, MSVC 2022 / C++20）下，执行全量自动化验收矩阵：

```
================================================================================
CineRTX 自动化门禁测试汇总报告
================================================================================
[门禁 1] SPDX 与开源合规性审计:
         ./tools/check_spdx.ps1 .................................. PASS (0 binaries)
[门禁 2] MSVC 2022 C++20 干净构建:
         cmake --build build --config RelWithDebInfo ............. PASS (0 errors, 0 fatal)
[门禁 3] 全量核心单元测试套件:
         build/RelWithDebInfo/rtx_tests.exe ...................... PASS (113/113 passed, 549ms)
[门禁 4] CTest 自动化测试套件:
         ctest --test-dir build -C RelWithDebInfo ................ PASS (4/4 passed, 100%)
[门禁 5] Win32 原生 GDI GUI 冒烟与自测:
         rtx-transcode-gui.exe --self-test ....................... PASS (4->8 frames)
================================================================================
```

### 113 项核心测试用例分类覆盖矩阵：
* **色彩空间与像素格式（13 项）**：涵盖 NV12/P010 双向转换、BT.709/BT.2020 矩阵、PQ 传递函数、半精度浮点转换与 PSNR 校验；
* **时序与时间戳运算（9 项）**：涵盖有理数算术、漂移补偿（DriftCompensator）、24p $\to$ 60p 帧率映射器与最近邻对称舍入；
* **无锁队列与显存池并发（18 项）**：涵盖 SPSC 队列边界容量、多线程并发生产者-消费者、显存租约移动语义、TdrGuard 水位与背压滞后；
* **调度器与异步状态机（15 项）**：涵盖 DAG 阶段顺序执行、在途帧超时守卫、单帧提交与故障快速短路；
* **动态桥接与 ABI 契约（10 项）**：涵盖纯 C 函数指针导出、ABI 版本协商、DX12 外部互操作自测；
* **HDR10 元数据与 SEI 合成（11 项）**：涵盖 SMPTE ST 2086、CTA-861-G CLL、HEVC 前缀 NALU 合成与 JSON 反序列化；
* **场景检测与自适应控制律（6 项）**：涵盖镜头硬切直方图检测、APL 亮度积分、TrueHDR 连续平滑与极值截断；
* **RIFE 插帧与模型契约（6 项）**：涵盖对齐尺寸推演、张量打包解包与 TensorRT 运行时门禁。

---

## 5. 专家终审请求事项与签署表 (Board Sign-off Request)

综上所述，CineRTX 视频重制与转码引擎无论在底层硬件零拷贝吞吐、多线程异步并发安全性、驱动生命周期异常鲁棒性，还是在开源合规、供应链防篡改及隐私安全边界上，均已达到工业级严谨交付标准。

工程组特此正式呈请领域专家委员会审阅本请求书与代码库实机证据，并签署终审批准意见：

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                  CineRTX 领域专家终审与开源就绪评审签署表                       │
├─────────────────────────┬───────────────────────┬──────────────┬─────────────┤
│ 评审专家职责            │ 审阅关注维度          │ 评审结论     │ 签署 / 日期 │
├─────────────────────────┼───────────────────────┼──────────────┼─────────────┤
│ 异构计算与硬件调度专家  │ D3D12/CUDA 零拷贝流转 │ [ ] 批准通过 │             │
│                         │ 防 TDR 机制与显存池   │ [ ] 驳回修正 │             │
├─────────────────────────┼───────────────────────┼──────────────┼─────────────┤
│ 音视频编解码算法专家    │ NVDEC/NVENC 流水线    │ [ ] 批准通过 │             │
│                         │ HDR10/VSR/TrueHDR 映射│ [ ] 驳回修正 │             │
├─────────────────────────┼───────────────────────┼──────────────┼─────────────┤
│ 开源法务与供应链安全专家│ 双许可证物理隔离与 ABI│ [ ] 批准通过 │             │
│                         │ P0~P3 凭据清零/SHA256 │ [ ] 驳回修正 │             │
├─────────────────────────┴───────────────────────┴──────────────┴─────────────┤
│ 委员会终审裁定：                                                             │
│ [ ] 全量通过终审验收，正式签发 GitHub 开源发布令 (Sign-off & Ready to Push)  │
│ [ ] 附带条件通过 (说明见附注)                                                 │
│ [ ] 驳回 (说明见附注)                                                        │
└──────────────────────────────────────────────────────────────────────────────┘
```
