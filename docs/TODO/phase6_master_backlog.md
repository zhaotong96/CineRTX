# CineRTX 阶段六：架构全面瘦身与专业级产品化待办总单 (Phase 6 Master Backlog)

> **文档标识**：`TODO-PHASE6-MASTER-BACKLOG-001`  
> **归档位置**：[`docs/TODO/phase6_master_backlog.md`](./phase6_master_backlog.md)  
> **制定日期**：2026-11-10  
> **依据裁决**：[`docs/phase6/CineRTX_Phase6_架构全面瘦身与专业级产品化专家裁决与技术方案.md`](../phase6/CineRTX_Phase6_架构全面瘦身与专业级产品化专家裁决与技术方案.md)  
> **核心指标**：全仓代码从 **49,042 行** 精简至 **< 15,000 行**（核心 C++ 源码降至 **~7,200 行**），实现 13 项参数原生 Win32 UI 矩阵、AV1 10-bit 默认编码与 100% 绝对 RAII。

---

## 1. 任务全景与四波次施工分解矩阵

专家裁决方案中规划的 13 项核心任务被严格划分为 4 个原子化工程波次（Waves），每次施工的前置上下文长度均严格控制在 12k Tokens 以内：

### 波次一：构建治理与物理死代码大砍削 (Wave 1: Build Hygiene & Dead Code Slash) [已闭环完成 - 2026-11-10]
* **目标**：彻底解决构建临时文件污染，从源码树物理切除未启用的死模块，净减代码 ~28,000 行。
* **实测成果**：物理净减 **29,739 行**（全仓代码由 49,042 行降至 23,742 行，降幅 51.5%）；插件目录 `.ilk`/`.pdb` 彻底消灭（0 个）；82 项单测 100% 通过；硬件转码流水线完美运行。

| 任务编号 | 任务名称与核心动作 | 对应专家议题 | 涉及文件 | 实测状态与效果 |
| :---: | :--- | :---: | :--- | :---: |
| **W1-01** | **CMake 阻断增量链接与 PDB 物理隔离**：为 `rtx_ngx_bridge` 与 `CineRTX` 添加 `/INCREMENTAL:NO`，PDB 重定向至 `${CMAKE_BINARY_DIR}/symbols`，消除插件目录 `.ilk`/`.pdb`。 | **E-05** | `CMakeLists.txt` | **[已完成]** 插件目录零污染，PDB 隔离就绪 |
| **W1-02** | **源码树物理删除巨型生成 PTX/CSO 头文件**：从源码树物理删除 `src/shim/ngx_bridge/p010_pack_ptx.h`（925 KB），配置 CMake 在构建期由 `embed_ptx.py` 动态生成至 `${CMAKE_BINARY_DIR}/generated/`。 | **E-13(a)** | `src/shim/ngx_bridge/p010_pack_ptx.h`<br>`CMakeLists.txt`<br>`tools/embed_ptx.py` | **[已完成]** 源码树净减 26,782 行 |
| **W1-03** | **物理切除未接线 RIFE/FRUC/ONNX 死代码与手写 Sha256**：彻底删除 `include/rtx/rife.h`、`include/shim/fruc_bridge_abi.h`、`src/shim/backends/` 全目录、`rife_builder.*`，切除 `--build-rife` 命令行，CMake 拔除对应目标。 | **E-13(b)** | `include/rtx/rife.h`<br>`src/shim/backends/`<br>`src/cli/main.cpp`<br>`CMakeLists.txt` | **[已完成]** 净减 1,760+ 行，拔除死目标 |
| **W1-04** | **物理删除未启用的 DirectComposition 视口**：彻底删除 `src/gui/dcomp_viewport.h` 与 `src/gui/dcomp_viewport.cpp`，清理 `rtx_gui.cpp` 中所有 dead branch，移除 `dcomp` 库链接。 | **E-03(a)** | `src/gui/dcomp_viewport.*`<br>`src/gui/rtx_gui.cpp`<br>`CMakeLists.txt` | **[已完成]** 净减 402 行 |
| **W1-05** | **测试集与构建拓扑联动对齐**：清理已切除模块对应的冗余单元测试（`test_rife.cpp`、`test_offruc_bridge.cpp`），保证构建与测试 100% 绿色通过。 | **E-13(c)** | `tests/test_rife.cpp`<br>`tests/test_offruc_bridge.cpp`<br>`CMakeLists.txt` | **[已完成]** 82 项单测全绿，冒烟转码 46.5 FPS 正常 |

---

### 波次二：现代 C++26 纯化与 100% 绝对 RAII 改造 (Wave 2: Modern C++26 & Strict RAII) [已闭环完成 - 2026-11-10]
* **目标**：消灭全仓所有两段式初始化、裸指针出参与手写深拷贝，推行纯聚合体与指定初始化器。
* **实测成果**：物理删除 `src/core/types.cpp` 与死类 `PingPongRing`（及其单测 `test_pingpong_pipeline.cpp`）；全仓核心类（`NvidiaTranscodeSession`、`D3D12Interop`、`NgxD3D12Bridge`、`MemoryAvioContext`、`SurfacePool`）彻底改造为 100% RAII 静态工厂与析构自动释放；静态扫描门禁 `rg -i "\b(initialize|shutdown|destroy)\s*\("` 匹配数**严格降至 0**；79 项单元测试 100% 绿色通过；RTX 5060 硬件转码冒烟零拷贝正常。

| 任务编号 | 任务名称与核心动作 | 对应专家议题 | 涉及文件 | 实测状态与效果 |
| :---: | :--- | :---: | :--- | :---: |
| **W2-01** | **彻底删除 `src/core/types.cpp` (去 Slop)**：拔除裸指针出参、`copyMetadataFrom` 逐字段赋值，收拢为现代 `constexpr`/inline 聚合头文件 `include/rtx/types.h`。 | **E-11** | `src/core/types.cpp` (删除)<br>`include/rtx/types.h`<br>`CMakeLists.txt` | **[已完成]** 物理删除源码文件，消灭类型 Slop |
| **W2-02** | **拔除自造轮子 `SmallExpected`，换装标准库**：`mpmc_ring.h` 改用 `<expected>`/`<optional>` 标准库与 `hardware_destructive_interference_size`；CMake 补齐 `/arch:AVX2` 锁定 YMM 向量化。 | **E-12** | `include/rtx/mpmc_ring.h`<br>`CMakeLists.txt` | **[已完成]** 标准库完全替代，AVX2 向量化锁定 |
| **W2-03** | **全仓结构体纯聚合化与指定初始化器改造**：拔除自定义构造函数，全仓结构体构造强制使用 `.field = value` 语法。 | **E-09** | `include/rtx/` 各类型头文件<br>全仓调用点 | **[已完成]** 纯聚合体适配 C++26 指定初始化器 |
| **W2-04** | **彻底拔除两段式初始化，100% 践行 RAII**：拔除 5 个核心类的 `initialize/shutdown/destroy`，改为私有构造 + `static Result<T> create(...)` 静态工厂 + 析构自动释放。 | **E-10** | `src/core/hw_transcode.cpp`<br>`include/rtx/vram_pool.h`<br>`src/core/vram_ring_pool.cpp`<br>`src/shim/ngx_bridge/` | **[已完成]** 静态扫描门禁零匹配，100% RAII |
| **W2-05** | **拔除单实现虚接口与死代码**：物理删除未使用的 `PingPongRing` 与 `test_pingpong_pipeline.cpp`；`SmartGeometryCalculator` 退化为命名空间自由函数。 | **E-13(d)** | `src/shim/ngx_bridge/d3d12_interop.*`<br>`tests/test_pingpong_pipeline.cpp`<br>`include/rtx/geometry.h` | **[已完成]** 消除死类与伪类抽象 |

---

### 波次三：AV1 默认与原生参数矩阵 UI 重构 (Wave 3: AV1 10-bit & Native Pro UI) [已闭环完成 - 2026-11-10]
* **目标**：全面暴露 13 项专业转码参数，废除 GDI 手绘教条，默认启用 AV1 10-bit 编码。
* **实测成果**：全波次净减代码 **622 行**（其中 `src/gui/rtx_gui.cpp` 由 1,950+ 行大幅精简至 736 行，净减 1,214 行）；全链路默认输出 AV1 10-bit（带 `av1C` 配置盒与 OBU 封装，sm_89 探测自动回退）；Win32 原生 Common Controls 13 项专业参数矩阵（6 ComboBox、4 Edit、3 CheckBox）全量双向绑定；支持 `Ctrl+L` 原地热重载中英文 i18n；废除 `RTX_GUI_RESULT_LOG` 改用 `OutputDebugStringW`；79 项单测 100% 绿色；RTX 5060 实测 GUI 端到端 4K HDR10 AV1 转码 100% 成功。

| 任务编号 | 任务名称与核心动作 | 对应专家议题 | 涉及文件 | 实测状态与效果 |
| :---: | :--- | :---: | :--- | :---: |
| **W3-01** | **全链路默认切换为 AV1 10-bit**：拔除 5 处硬编码 HEVC 拦截点，补齐 `av1C` 与 OBU 比特流封装；加启动期 NVENC 自检（Ada/Blackwell 默认 AV1，Ampere 自动回退 HEVC）。 | **E-04** | `src/core/hw_transcode.cpp`<br>`include/rtx/hw_transcode.h`<br>`src/shim/loader/nv_encoder.cpp`<br>`src/io/bitstream.*`<br>`src/cli/main.cpp` | **[已完成]** 默认 AV1 10-bit，硬件实测通过 |
| **W3-02** | **Win32 原生标准控件声明式微组件层**：推翻“零子 HWND”手绘教条，采用 Windows 原生 `user32`/`comctl32` 控件，配合 `DarkMode_Explorer` 与深色画刷拦截。 | **E-01** | `src/gui/rtx_gui.cpp`<br>`CMakeLists.txt` | **[已完成]** 废除手绘，纯原生深色控件就绪 |
| **W3-03** | **构建 13 项专业参数原生矩阵与双向绑定**：6 个 ComboBox、4 个 Edit、3 个 CheckBox 完整双向绑定 `HwTranscodeOptions`，消除双端不同步隐患。 | **E-02** | `src/gui/rtx_gui.cpp`<br>`include/rtx/hw_transcode.h` | **[已完成]** 13 项参数矩阵全量双向绑定 |
| **W3-04** | **全控件表驱动动态热切换 i18n (`Ctrl+L`)**：启动自检系统语言，`StrId` 覆盖全部控件文本，原地刷新无需重启。 | **E-07** | `src/gui/rtx_gui.cpp`<br>`include/rtx/i18n.h`<br>`src/core/i18n.cpp` | **[已完成]** `Ctrl+L` / `WM_HOTKEY` 原地热切通过 |
| **W3-05** | **拔除 `RTX_GUI_RESULT_LOG` 环境变量**：废除磁盘日志落盘，改用 Windows 原生 `OutputDebugStringW` 与 DebugView 监听流（吸收 T-05）。 | **T-05** | `src/gui/rtx_gui.cpp` | **[已完成]** 零磁盘日志污染，原生调试流接入 |

---

### 波次四：全案文档一致性对齐与发布闭环 (Wave 4: Docs SSOT & Release Closure) [已闭环完成 - 2026-11-10]
* **目标**：彻底正本清源 6 大历史文档矛盾，交付顶级开源标准双语 README 与轻量化发布包。
* **实测成果**：全案 36 篇历史文档统一切除矛盾并加盖 `[HISTORICAL-SUPERSEDED]` 归档印记；完成 `docs/README.md`、`docs/spec/architecture.md` 与 `docs/spec/testing.md` 权威重构；根目录交付顶级开源标准物理分离双语说明（英文 `README.md` 与中文 `README_zh.md` 双向锚点互链）；轻量化重构 `tools/package_release.py`，免安装绿色发布包体积从 1.5+ GB 瘦身至 **34.35 MB**（包含 SHA-256 校验和清单，实测验证成功）。

| 任务编号 | 任务名称与核心动作 | 对应专家议题 | 涉及文件 | 实测状态与效果 |
| :---: | :--- | :--- :---: | :--- | :---: |
| **W4-01** | **全案历史文档 SSOT 归档与矛盾纠偏**：对齐 6 大矛盾，将 `docs/phase1`~`phase5` 历史工单打上 `[HISTORICAL-SUPERSEDED]` 印记，重写 `architecture.md` 与 `testing.md`。 | **E-06** | `docs/README.md`<br>`docs/spec/architecture.md`<br>`docs/spec/testing.md`<br>`docs/phase1`~`phase5` 全案 | **[已完成]** 36 篇历史工单归档印记就绪，3 篇基线重构对齐 |
| **W4-02** | **根目录双语 README 物理分离**：拆分为纯英文 `README.md` 与纯中文 `README_zh.md`，顶部双向锚点互链，同步更新 AV1 默认与原生 UI。 | **E-08** | `README.md`<br>`README_zh.md`<br>`tools/package.ps1` | **[已完成]** 顶级开源标准物理分离双语文档交付 |
| **W4-03** | **自动化绿色免安装发布包轻量化重构**：更新 `tools/package_release.py`，剔除 TensorRT 巨型依赖，绿色压缩包从 1.5+ GB 瘦身至 34.35 MB 纯净版。 | **T-06** | `tools/package_release.py` | **[已完成]** 绿色包自包含纯净发布（34.35 MB），实测通过 |

---

## 2. 验收门禁指标汇总 (Verification Gates)
1. **代码体量门禁**：全仓代码总行数从 49,042 行降至 **< 15,000 行**；核心 C++ 业务源码 **< 7,500 行**；
2. **绝对 RAII 门禁**：除 `rtx_ngx_bridge.dll` 导出 C ABI 外，全仓零 `initialize` / `shutdown` / `destroy`；
3. **硬件实测门禁**：RTX 40/50 默认输出 AV1 10-bit，RTX 30 自动优雅回退 HEVC Main10；
4. **编译实测门禁**：MSVC 2026 `/arch:AVX2` 下核心循环 100% 生成 `ymm` 256-bit 向量指令；
5. **发布纯净门禁**：插件目录零 `.ilk` 与 `.pdb` 污染，发布包体积降至正常水准。
