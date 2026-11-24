# CineRTX Phase 12 极限现代 C++26 重构与无头 DLL 净空专家咨询委托书

- **文档编号**：`CINERTX-P12-DELEGATION-001`
- **归档路径**：[`docs/phase12/01_CineRTX Phase 12 极限现代 C++26 重构与无头 DLL 净空专家咨询委托书.md`](./01_CineRTX%20Phase%2012%20极限现代%20C++26%20重构与无头%20DLL%20净空专家咨询委托书.md)
- **呈报主体**：CineRTX 项目总协调负责人（Coordinator）
- **受呈报方**：多媒体系统工程与 GPU 异构计算首席领域专家组（Extreme Modern C++ Purist & Architecture Board）
- **当前状态**：🟢 **Phase 12 正式开工，提请极限现代 C++26 重构、微观代码卫生清剿与无头 DLL 净空方案终审（INITIATED & SUBMITTED FOR STRATEGIC CONSULTATION）**
- **执行基线**：Commit `530a9a6`（Tag: `v0.3.0`，本地完成方案 B 历史原子化 Rebase 清洗，四门禁全通，146/146 单测全绿）
- **目标里程碑**：`Phase 12 (v0.4.0 Milestone Closure)`
- **指导总纲与输入依据**：
  * [`docs/phase11/57_CineRTX Phase 11.4 终审验收裁决书暨 Phase 12 极限现代 C++ 重构与 DLL 净空演进总纲.md`](../phase11/57_CineRTX%20Phase%2011.4%20终审验收裁决书暨%20Phase%2012%20极限现代%20C++%20重构与%20DLL%20净空演进总纲.md)（`CINERTX-P11-FINAL-APPROVAL-SEAL-057`）
  * [`docs/phase11/56_CineRTX Phase 11.4 终审验收与 v0.3.0 封板委托书.md`](../phase11/56_CineRTX%20Phase%2011.4%20终审验收与%20v0.3.0%20封板委托书.md)（`CINERTX-P11-DELEGATION-056`）
  * [`docs/phase11/53_CineRTX Phase 11.3 止血与竖屏修复终审验收裁决书暨 Phase 12 极限现代 C++ 重构与 DLL 净空总方案.md`](../phase11/53_CineRTX%20Phase%2011.3%20止血与竖屏修复终审验收裁决书暨%20Phase%2012%20极限现代%20C++%20重构与%20DLL%20净空总方案.md)（`CINERTX-P11-RULING-053`）
  * [`DECISIONS.md`](../../DECISIONS.md)（`[DEC-008]`, `[DEC-009]`, `[DEC-010]`）

---

## 一、 委托背景与 Phase 12 战役定位

在 Phase 11.4 阶段终审验收中，首席领域专家组在 [57 号裁决书](../phase11/57_CineRTX%20Phase%2011.4%20终审验收裁决书暨%20Phase%2012%20极限现代%20C++%20重构与%20DLL%20净空演进总纲.md) 中正式签发了 `v0.3.0` 里程碑封板裁决印章，并批准执行方案 B 历史原子化 Rebase 清洗。

目前，协调负责人已在本地完成以下前置法定准备：
1. **历史原子化清洗完毕**：执行 `git rebase -i 74d4141`，彻底修正了 Phase 10 Track 10A 历史提交中的静态直链，重建了纯净 `v0.2.0` 标签并为 Phase 11.4 打标 `v0.3.0`（`530a9a6`），全历史树 100% 捍卫了 GPLv3 许可隔离红线；
2. **基线全量绿灯锁死**：146/146 项单元测试全部通过（44.9s），`AuditTypes` 155 类型全具合法证明，`AuditSloc` 33,143 Pure SLOC，`AuditSpdx` GPL=112/MIT=37，`AuditImports` 确认动态桥接物理隔离无损；
3. **用户人工复验与 Phase 12 正式起步**：用户明确裁定项目正式跨入 Phase 12。

Phase 12（v0.4.0）是 CineRTX 摆脱历史债务、实现工程升维的关键攻坚战，战役核心目标直指**“现代 C++ 洁癖提纯”**与**“无头系统 DLL 极限净空”**。现正式向首席领域专家组呈报 01 号咨询委托书，提请专家组对 Phase 12 极限重构的架构边界、落地细则与四阶段实施路线进行全面审阅与权威裁决。

---

## 二、 咨询议题一：现代 C++20/23/26 极致重构标准与语法边界裁决

用户对 Phase 12 的重构军规给出了极高要求的明确指示：
> **“所有的自定义类只要能用标准库的特性实现就绝对不允许存在，任何存在的自定义类必须明确显式说明存在必要性。任何额外的防御，任何无意义的冗余代码，任何其他隐藏的开关，任何额外的编译选项，任何违反了 KISS 和奥卡姆剃刀原则的，任何其他增加使用者心智负担的隐藏坑，一律不允许存在，全部拔除！”**

结合 [57 号裁决书第二节](../phase11/57_CineRTX%20Phase%2011.4%20终审验收裁决书暨%20Phase%2012%20极限现代%20C++%20重构与%20DLL%20净空演进总纲.md)，提请专家组对以下核心语法特性与类型清剿边界进行最终裁定：

### 1.1 MSVC 19.51+ 现代 C++ 生产落地边界核准

| 特性类别 | 专家总纲要求特性 | 拟在 Phase 12 强制全面落地的具体工程场景 | 提请专家核准事项 |
| :--- | :--- | :--- | :--- |
| **代数和类型与错误流** | `std::expected<T, Error>`<br>`std::variant`<br>`std::optional` | 全面替代全仓所有 C 风格 `bool ok`、`int32_t status`、`RtxResult` 冗余标量袋；CLI 子命令解析彻底重构为 `std::variant<TranscodeCliArgs, ProbeCliArgs, BuildEngineCliArgs, StdioSessionArgs>`。 | 确认是否全仓私有函数均禁止使用自定义错误包装体，统一收敛为 `Result<T>`。 |
| **多维视图与零开销切片** | `std::span`<br>`std::mdspan`<br>`std::ranges` / `std::views` | 全面取代裸指针+大小对 `(ptr, size)`；`std::mdspan` 用于 NV12/P010 平面行距（`pitch`）二维寻址与多声道交错音频通道索引；Ranges 管道全面替代嵌套 `for` 循环。 | 确认 `std::mdspan` 在 MSVC 19.51 Release 下内联后是否达成 100% 零运行时开销。 |
| **现代生命周期与并发原语** | `std::jthread` \+ `stop_token`<br>`std::move_only_function`<br>`UniqueHandle` | 彻底拔除历史 `std::function`，消灭堆分配与虚表拷贝开销；工作线程全面接入协作式取消令牌（`std::stop_token`）；Win32 句柄统一收拢至标准 `UniqueHandle`。 | 确认是否全仓禁止裸持 Win32 `HANDLE` 与未绑释放器的裸指针。 |
| **编译期元编程与反射边界** | C++23 `Deducing this`<br>`static operator()`<br>`consteval`<br>`constexpr std::array` | 恪守专家裁决：**明确规避当前 MSVC 尚未公开稳定的 C++26 P2996（`^^`、`std::meta::info`）与 `#embed`**，坚持单链 `kParamDescriptors` 编译期成员指针反射表 + `consteval` 静态断言。 | 核准此实用主义反射方案在 C++26 正式标准落地前的过渡完备性。 |
| **COM 出参收养规范** | 规避 `std::out_ptr`<br>统一 `callComOut` | 恪守专家裁决：避开 `std::out_ptr` 临时代理对象与 `IID_PPV_ARGS` 的宏展开冲突，统一为单一自由函数模板两段式收养。 | 确认 `callComOut` 模板的最佳泛型实现形态。 |

### 1.2 全仓具名类型零容忍清剿方案（155 类型 $\rightarrow \le 110$ 类型，刚性净减 $\ge 45$ 个）

对照 [57 号裁决书第 2.2 节清单](../phase11/57_CineRTX%20Phase%2011.4%20终审验收裁决书暨%20Phase%2012%20极限现代%20C++%20重构与%20DLL%20净空演进总纲.md)，协调负责人已梳理出第一批 45+ 待清剿具名类型，提请专家组核准其物理删除与降维方式：

| 序号 | 待物理清除/降维类型 | 所在文件 | 违背洁癖的根因 | 替代标准库原语 / 处置动作 |
| :---: | :--- | :--- | :--- | :--- |
| **1** | `DynamicVsrTarget` | `workstation_canvas.h` | 仅 4 标量，布尔字段冗余，附带全局状态 | **物理删除** $\rightarrow$ `std::optional<std::tuple<int, int, double>>` |
| **2** | `ViewportVsrTarget` | `workstation_canvas.h` | 纯属重复包装 | **物理删除** $\rightarrow$ 统一为单一 `std::optional` 状态 |
| **3** | `ProbedMedia` | `workstation_canvas.h` | C 风格 `bool ok` 状态袋 | **物理删除** $\rightarrow$ `std::optional<std::tuple<int, int, int64_t>>` |
| **4** | `PingPongSlot` | `d3d12_interop.h` | 全仓零引用死代码 | **整段物理拔除** |
| **5** | `InteropTexture` | `d3d12_interop.h` | 伪状态机，裸持 `HANDLE` | **降维** $\rightarrow$ `std::tuple<ComPtr<ID3D12Resource>, UniqueHandle, CUexternalMemory, CUdeviceptr, size_t>` |
| **6** | `SharedBufferSlot` | `d3d12_interop.h` | 同上 | **降维** $\rightarrow$ 融入上述元组组合值语义 |
| **7** | 局部 `CudaDriverApi` (3处) | `rife_engine.cpp`<br>`d3d12_interop.cpp`<br>`nvof_engine.cpp` | 重复手写 `LoadLibraryExW("nvcuda.dll")`，谎称 C-ABI，破坏 DRY | **清退统合** $\rightarrow$ 无状态模板 `LoadedApi<auto... Fns>`，拔除全局 `g_cuda` |
| **8** | `RenderGraphSegment` | `render_graph.h` | 与 `AudioGraphSegment` 前 5 字段同构 | **合并降维** $\rightarrow$ 统一为 `TimelineSlice`，负载使用 `std::variant<std::pair<bool, bool>, float>` |
| **9** | `AudioGraphSegment` | `render_graph.h` | 同上 | 同上 |
| **10** | `Options` | `src/cli/main.cpp` | 互斥模式字段平铺，制造非法状态 | **物理删除** $\rightarrow$ `std::variant` 代数和类型 |
| **11** | `BitstreamSlot` 旧字段 | `nv_encoder.cpp` | 包含已废弃单缓冲残余 | **提纯** $\rightarrow$ 统一由 `UniqueHandle` 管理独立事件的定容池 |
| **12** | 历史遗留包装类 (34项) | 全仓散落 | `Control`、`Group`、`MappedResource` 等 | **批量清剿** $\rightarrow$ `constexpr std::array<std::tuple<...>, N>` 常量表 |

---

## 三、 咨询议题二：5 项微观代码卫生瑕疵的 Wave 12.1 外科手术治理

在 [57 号裁决书第 1.2 节](../phase11/57_CineRTX%20Phase%2011.4%20终审验收裁决书暨%20Phase%2012%20极限现代%20C++%20重构与%20DLL%20净空演进总纲.md) 中，专家组精准锁定了 5 项深层代码卫生瑕疵。协调负责人拟在 Phase 12 开工第一周（Wave 12.1）执行全面外科手术，提请专家组审阅其精准治理方案：

```text
瑕疵 1: nv_encoder.cpp 环形缓冲池事件错配与旧单缓冲成员残留
┌─────────────────────────────────────────────────────────────────────────────┐
│ 现状: bitstream_pool[1..N-1] 共用单一 completion_event，存在信号覆盖竞态;  │
│       RtxEncoder 仍残留旧单缓冲成员 bitstream / completion_event 等字段。   │
│ 方案: 彻底删除单缓冲旧成员，bitstream_pool 改为 std::array<Slot, 16> 定容池;│
│       每个 Slot 独立绑定 UniqueHandle 托管的自动复位事件;                  │
│       环境后门 RTX_NVENC_UHQ / RTX_NVENC_BFRAMES 正式升级为 CLI/SSOT 显式参数│
└─────────────────────────────────────────────────────────────────────────────┘

瑕疵 2: rife_engine.cpp 匿名驱动加载壳与全局可变静态单例
┌─────────────────────────────────────────────────────────────────────────────┐
│ 现状: 局部 struct CudaDriverApi 及 static CudaDriverApi g_cuda 重复存在;   │
│       局部手写 #pragma warning(disable: 4996) 抑制 std::getenv 告警。        │
│ 方案: 统一使用无状态 LoadedApi 变参模板按需加载; 彻底消灭 g_cuda;          │
│       环境变量读取收口至安全自由函数原语，拔除 pragma 警告抑制。           │
└─────────────────────────────────────────────────────────────────────────────┘

瑕疵 3: vram_scheduler.cpp 三计数器冗余与浮点转换
┌─────────────────────────────────────────────────────────────────────────────┐
│ 现状: 同时维护 frames_in_tier_ / consecutive_high / consecutive_low;        │
│       使用 std::llround(double * 0.82) 引入不必要的浮点运算。               │
│ 方案: 统一为单有符号滞后计数器 int32_t hysteresis_frames_（正累积高水位，   │
│       负累积低水位）; 纯整数有理数比率 (bytes * 82u) / 100u; 消除全量浮点。  │
└─────────────────────────────────────────────────────────────────────────────┘

瑕疵 4: CMakeLists.txt 配置/构建期职责混杂与全局警告掩码
┌─────────────────────────────────────────────────────────────────────────────┐
│ 现状: 配置期 execute_process(EmbedPtx) 与构建期 add_custom_command 重复;    │
│       配置期静默联网下载归档; 全局 /wd4324 掩盖对齐填充警告。              │
│ 方案: 彻底删除配置期 execute_process，仅保留构建期依赖触发;                 │
│       增设 option(RTX_ALLOW_NETWORK_PROVISION OFF);                         │
│       收缩 WasapiAudioOutput 缓存行对齐分布，从源头消灭告警并移除 /wd4324。 │
└─────────────────────────────────────────────────────────────────────────────┘

瑕疵 5: hw_transcode_stages.cpp 裸指针方法残余
┌─────────────────────────────────────────────────────────────────────────────┐
│ 现状: 保留 anchor_p010_.ptr() 裸指针调用。                                  │
│ 方案: 统一为 std::span / CUdeviceptr 值语义视图。                           │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 四、 咨询议题三：13 项 Win32 系统 DLL 极限剥离与无头解耦契约

依据 [57 号裁决书第四节](../phase11/57_CineRTX%20Phase%2011.4%20终审验收裁决书暨%20Phase%2012%20极限现代%20C++%20重构与%20DLL%20净空演进总纲.md)，为恪守奥卡姆剃刀与 KISS（绝不为删轻量系统库而引入重型 COM 运行时栈），提请专家组批准两阶段收敛路线：

### 4.1 阶段目标与静态导入表（IAT）收敛对比

```text
[ 现状 Phase 11.4 ] (13 项依赖)
CineRTX.exe ───> kernel32, user32, gdi32, shell32, dwmapi, comdlg32, comctl32,
                 uxtheme, d3d12, dxgi, ole32, bcrypt, kernelbase

[ 阶段 1：Phase 12A 单二进制混合态 ] (静态 IAT 缩减至 4 项物理极限)
CineRTX.exe ───> kernel32.dll, user32.dll, gdi32.dll, bcrypt.dll
* 永久物理拔除: comctl32.dll（全暗色自绘双缓冲接管）, uxtheme.dll（消除系统主题依赖）
* 运行时懒加载: d3d12.dll, dxgi.dll, dwmapi.dll, comdlg32.dll (GetOpenFileNameW),
                shell32.dll (WM_DROPFILES), ole32.dll (音频播放期懒加载)

[ 阶段 2：Phase 12B Core / GUI 架构正交解耦终态 ] (无头核心静态 IAT 降至 2 项绝对极限)
CineRTX.exe (Headless Transcode Engine) ───> { kernel32.dll, bcrypt.dll }
rtx_gui.dll (UI Plugin / Native Desktop) ──> { user32.dll, gdi32.dll } (按需加载)
```

### 4.2 提请专家确认的架构权衡：
1. **坚决拒绝 `IFileOpenDialog`**：保持使用 `LoadLibraryExW("comdlg32.dll")` 动态获取 `GetOpenFileNameW`，杜绝为了形式主义消灭 `comdlg32` 反而强行拉入完整的 Windows Shell COM 运行环境；
2. **坚决拒绝 Direct2D / DirectWrite**：GDI 双缓冲自绘仅存续于独立 GUI 插件内部，杜绝引入庞大 D2D COM 栈；
3. **无头引擎服务化契约**：解耦后的 `CineRTX.exe` 成为纯粹的 Stdio / CLI / 无头批处理转码引擎，完全脱离 Window Station 限制，支持 Windows Session 0 后台服务化运行。

---

## 五、 Phase 12 减债指标攻坚承诺与四阶段演进排期

对照专家组确立的硬性准出红线，协调负责人郑重确立如下全栈清零承诺，提请专家组下达开工授权：

### 5.1 刚性清零指标承诺表

| 核心工程维度 | 进场现状（Commit `530a9a6`） | Phase 12 刚性准出红线 | 预期减债幅度 |
| :--- | :---: | :---: | :---: |
| **Pure SLOC（纯代码行数）** | **33,143 行** | $\mathbf{\le 28,000\text{ 行}}$ | **刚性净削减 $\ge 5,143$ 行** |
| **Named Types（具名类型数）** | **155 个** | $\mathbf{\le 110\text{ 个}}$ | **刚性净清剿 $\ge 45$ 个** |
| **无头引擎静态 IAT 依赖** | **13 项系统 DLL** | $\mathbf{\{kernel32.dll, bcrypt.dll\}}$ | **静态消除 11 项系统库** |
| **全局可变状态** | `g_app`、`g_cuda` 存续 | **0 处全局可变状态** | **100% 物理清除** |
| **编译警告与构建环境** | 包含 `/wd4324` 全局掩码 | **MSVC 19.51 /W4 /WX 0 警告** | **拔除全部编译掩码** |

### 5.2 四阶段演进路线与工期排定

```
Phase 12 (v0.4.0) 施工排期路线图
├─ Wave 12.1: 构建脱毒与微观代码卫生清剿 (2 工作日)
│   ├─ 清理 CMakeLists.txt (删重复 EmbedPtx, 挂载 RTX_ALLOW_NETWORK_PROVISION=OFF)
│   ├─ 修复 nv_encoder.cpp 槽位独立事件错配，拔除单缓冲旧字段
│   ├─ 统一 CudaDriverApi 为无状态 LoadedApi 模板，消除 g_cuda
│   └─ 升级 AuditTypes.cmake 词法扫描器，首批清剿 15+ 伪包装类
├─ Wave 12.2: 全栈 Modern C++20/23/26 极限重构 (2-3 工作日)
│   ├─ 拔除 g_app 全局状态，生命周期绑定 GWLP_USERDATA RAII
│   ├─ UI/CLI 参数单源驱动 (SSOT)，全面遍历 kParamDescriptors 表驱动
│   ├─ 全面落地 std::mdspan, std::move_only_function, std::from_chars
│   └─ 达成指标: Pure SLOC <= 28,000 行，具名类型 <= 110 个
├─ Wave 12.3: 系统 DLL 极限剥离与 Core/GUI 正交解耦 (2 工作日)
│   ├─ 拔除 comctl32/uxtheme，暗色自绘双缓冲控件全面接管
│   ├─ 动态懒加载 d3d12/dxgi/dwmapi/comdlg32/shell32/ole32
│   └─ 实施 Core / GUI 物理分离，无头 CineRTX.exe 静态 IAT 锁死为 2 项
└─ Wave 12.4: 全量回归与 RTX 5060 实机物理终审 (1 工作日)
    └─ MSVC 19.51 /W4 /WX 0 警告，146+ 单测全 PASS，无头/桌面 4K 转码双闭环
```

---

## 六、 提请专家组裁决与下一步工作

总协调负责人现正式提请首席领域专家组对本委托书确立的架构路线实施终审裁决：
1. 签发 **Phase 12 开工专项裁决书（`CINERTX-P12-RULING-002`）**；
2. 审定并核准 **Wave 12.1 SuperPower 施工单** 的任务切片与物理文件边界；
3. 指导协调负责人组织外部高级工程师进场，正式击发 Phase 12 极限现代 C++ 重构首期战役。

---

> [!NOTE]
> 本工单是 Phase 12（v0.4.0）的顶层开工咨询工件，完整承接了 Phase 11.4 终审裁决（57 号总纲）的战略意志与技术减债契约。

> [!TIP]
> 架构纪律：Phase 12 期间仍严格实行 `[DEC-008]` 正交外包协作机制，协调负责人全面负责 MSVC 自愈、门禁平账与 RTX 5060 物理物证终验，守死代码卫生防线。

> [!IMPORTANT]
> 减债刚性红线：Pure SLOC 必须实现 $\ge 5,143$ 行物理净削减（$\le 28,000$ 行），具名类型必须削减 $\ge 45$ 个（$\le 110$ 个），此为 Phase 12 唯一准出底线。

> [!WARNING]
> 严防过渡样板（Slop）复辟：严禁以“重构”为名重新封装中间层、抽象工厂或虚假接口，必须全面以标准库纯代数类型与函数式泛型原语直接表达。

> [!CAUTION]
> 远端推送防线：在用户对本地 rebase 成果完成全量人工复验并下达明确推送指令前，严禁执行 `git push`，全流程坚决守死网络边界。
