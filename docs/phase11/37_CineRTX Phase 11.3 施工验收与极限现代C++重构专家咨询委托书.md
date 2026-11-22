# CineRTX Phase 11.3 施工验收裁决与极限现代 C++ 重构专家咨询委托书

- **文档编号**：`CINERTX-P11-ACCEPTANCE-CONSULT-037`
- **归档路径**：[`docs/phase11/37_CineRTX Phase 11.3 施工验收与极限现代C++重构专家咨询委托书.md`](37_CineRTX%20Phase%2011.3%20施工验收与极限现代C++重构专家咨询委托书.md)
- **呈报主体**：CineRTX 项目总协调负责人（Coordinator）
- **受呈报方**：CineRTX 首席领域专家组（Extreme Modern C++ Purist & Architecture Board）
- **当前状态**：🔴 **施工验收坚决驳回（REJECTED）兼极限现代 C++ 重构战略咨询**
- **基准提交**：`749fe0d` (`feat(phase11.3): complete Day-0 micro-hygiene fixes, lock AuditSloc baseline, and land Work Order 36`)
- **审计依据**：[`docs/phase11/36_CineRTX Phase 11.3 SuperPower 施工单_统一NLE引擎与零分配AVX2音频总线.md`](36_CineRTX%20Phase%2011.3%20SuperPower%20施工单_统一NLE引擎与零分配AVX2音频总线.md)

---

## 第一部分：Phase 11.3 外部执行工程师施工法医级验收审查

### 1.1 验收总决议：🔴 坚决驳回（REJECTED / UNACCEPTABLE）

外部执行工程师声称已完成 Phase 11.3（施工单 36 号）任务，申请合龙验收。项目协调负责人依据“只读检查先于结论，结论必须具备可复现物证”铁律，对其实施全维度穿透核验，核验判定：**多项核心准出门禁严重穿底，代码卫生发生恶性倒退，予以坚决驳回**。

| 门禁编号 | 门禁规范要求 | 实测结果 | 裁决结论 |
| :--- | :--- | :--- | :--- |
| **Gate 11.3-1** | 物理彻底拔除画布旧遗产（`NleClip`、`NleTimeline` 等正则 0 命中） | `workstation_canvas.cpp` 仍残留 **18 处命中** | 🔴 **FAILED** |
| **Gate 11.3-2** | 落地 `NleProject` 与 7 类命令栈，复用 `json_scanner.h` 零泄漏 | 引入大量自定义样板类，未与剪辑引擎高密复用 | ⚠️ **CONDITIONAL** |
| **Gate 11.3-3** | 音频混音与 WSOLA 热循环 **零堆内存分配** | 擅自重载全局 `operator new/delete` 污染全局单测 | 🔴 **UNACCEPTABLE** |
| **Gate 11.3-4** | NVENC 硬切切点强制 IDR 赋值（C-05 闭环） | `encodeWorkerBody()` 补齐 `keyframe` 赋值 | 🟢 **PASS** |
| **Gate 11.3-5** | `CompiledRenderGraph` 多段硬切连续导出 | 自造 `VideoClaim` 且未声明类型存续契约 | 🔴 **FAILED** |
| **Gate 11.3-6** | `AuditTypes.cmake` 拦截 + **净减 -400 至 -600 纯 SLOC 追偿** | 3 处类型违规；**纯 SLOC 恶性暴增 +1,316 行** | 🔴 **FATAL BLOCKED** |

---

### 1.2 违规物证与法医取证细目

#### 1. Gate 11.3-1 旧代码负向拔除门禁失守（残留 18 处旧符号）
在 [`src/gui/workstation_canvas.cpp`](../../src/gui/workstation_canvas.cpp) 中执行 36 号施工单刚性规定的负向扫描：
```powershell
Select-String -Path src/gui/workstation_canvas.cpp -Pattern "NleClip|NleTimeline|sharedNleTimeline|kFlicksPerMs|msToFlicks|formatFlicksTimecode|timelineXToMs|timelineMsToX|_ms"
```
实测输出 **18 处命中**：
- `src/gui/workstation_canvas.cpp:211`: `NleClip` 变量与函数重载依然存在；
- `src/gui/workstation_canvas.cpp:265`: `paintNleTimeline` 过程未予拔除；
- `src/gui/workstation_canvas.cpp:1159`: `registerNleTimelineWindowClass` 仍驻留系统。
工程师未执行“开工首日全量拔旧清债”，反而在旧有废弃代码上缝合新逻辑，公然违背施工单第 47-50 行“必须返回 0 命中”的刚性契约。

#### 2. Gate 11.3-6 代码体量追偿严重穿底（净增 +1,316 纯 SLOC）
运行同口径纯净行数统计工具 `cmake -P cmake/AuditSloc.cmake`：
- **进场法定基线（Commit `749fe0d`）**：规范集合 135 文件，纯 SLOC **31,064** 行；
- **施工后交付状态**：纯 SLOC **32,380** 行；
- **实际净增量**：**+1,316 纯 SLOC**（与施工单要求的 **-400 至 -600 行负增长追偿** 出现高达 **1,800 行的恶性倒挂**！）。

#### 3. `AuditTypes.cmake` 类型门禁拦截失败（3 处直接违规）
运行静态类型审计 `cmake -P cmake/AuditTypes.cmake`，直接返回错误：
```
CMake Error at cmake/AuditTypes.cmake:89 (message):
  [AuditTypes] FAIL: 3 named type(s) lack a valid @rtx-type-justification block:
    * include/rtx/nle_project.h:91: orphan block with no following named type
    * include/rtx/nle_project.h:94: orphan block at end of file
    * src/core/render_graph.cpp:17: VideoClaim
```
工程师在 `render_graph.cpp` 中自造结构体 `VideoClaim`，既未附带存续理由，又在 `nle_project.h` 尾部遗留未闭合的空契约块，工程素养极度低下。

#### 4. 全局测试桩粗暴劫持（全局重载 `operator new`）
在 [`tests/test_audio_mixer_avx2.cpp`](../../tests/test_audio_mixer_avx2.cpp) 第 43-100 行中，工程师为了验证“混音热循环零堆分配”，竟然直接编写了 60 行全局替换的 `void* operator new(size_t)` 与 `void operator delete(void*)`，通过全局计数器劫持了链接进 `rtx_tests.exe` 的全部单测用例！这种破坏 C++ 运行库隔离性的粗糙手法严重违反现代 C++ 测试最佳实践。

---

## 第二部分：面向极致现代 C++（C++20/23/26）专家咨询委托

### 2.1 咨询背景与重构愿景
CineRTX 项目当前全面运行在 MSVC 14.51 / C++26 编译环境之上。然而，由于前期各路执行工程师受过时面向对象思维与 AI 生成代码样板（Slop）影响，全仓充斥着大量虚妄的包装类、机械 Getter/Setter、多层伪防御性检查、冗余状态标记以及手写容器。

用户明确下达战略重构指令：
> **专家的角色是极致的 Modern C++ 拥趸，极致地推崇 C++20、23、26 的新特性，极致地遵循 Modern C++ 的设计准则（RAII、Lambda 表达式、极致的标准库应用等）。现在需要对项目做极限重构，对代码卫生有着极致的洁癖：所有的自定义类只要能用标准库的特性实现就绝对不允许存在，任何存在的自定义类必须明确显式说明存在必要性。任何额外的防御，任何无意义的冗余代码，任何其他隐藏的开关，任何额外的编译选项，任何违反了 KISS 和奥卡姆剃刀原则的，任何其他增加使用者心智负担的隐藏坑，一律不允许存在，全部拔除！**

### 2.2 专家请教与裁决议题

#### 议题一：标准库全栈替代自造类（Radical Std-First Modernization）
1. **多值契约与轻量聚合降维**：
   - 现存的各类小结构体（如命令 Delta、尺寸对、坐标范围、时间切片）如何系统性迁移至 `std::tuple`、`std::pair`、`std::optional`、`std::variant` 与 `std::expected`？
   - 存续类型判定基准：除对齐硬件内存布局（如 DXBC cbuffer、CUDA 硬件对齐向量、PE 导入表）与顶层公开发布 API 外，内部逻辑层是否可以实现 **零自定义类/结构体**？
2. **多维内存视图与切片**：
   - 评估引入 C++23 `std::mdspan` 与 `std::span` 全面淘汰自定义步长计算（`pitch` 计算、UV 平面偏移、音频通道交错切片）的可行性与具体重构路线。
3. **算法管线化与 Ranges / Views**：
   - 使用 C++20/23 `std::views::transform`、`std::views::filter`、`std::views::chunk`、`std::ranges::subrange` 彻底消灭手工手写的大量 `for` 循环与中间临时 `std::vector`。

#### 议题二：KISS 与奥卡姆剃刀——拔除多余防御与配置杂质
1. **过度防御清退**：
   - 许多模块在进入前连续进行 `if (ptr == nullptr) return ...;`，随后又在子函数、RAII 析构中重复判断达 4-5 次。请教专家如何通过 C++ 引用契约（`T&`）、非空所有权转移（`std::unique_ptr` 移动保证）及编译器属性从物理上消除无意义的防御性代码。
2. **构建选项与隐藏开关大清洗**：
   - 全面梳理 CMake 与源码预编译宏，剔除历史妥协残留的调试开关、分支后门及无效编译参数，保持流水线极致透明。

---

## 第三部分：测试期“嘟嘟嘟”蜂鸣声（疑似内核缓冲区溢出）法医溯源与治理咨询

### 3.1 现象描述与物理特征
- **用户反馈**：在宿主机运行测试时，系统高频发出 `嘟嘟嘟` 刺耳提示音，疑似触发 Windows 内核键盘/输入缓冲区溢出，观察似乎发生在执行包含 `100x` 循环用例附近。

### 3.2 项目负责人法医级穿透排查与证据确证

经过项目负责人深入源码、单测调用链路及 Windows 子系统交互逻辑的地毯式法医排查，锁定了两大确凿根因：

```mermaid
flowchart TD
    subgraph AudioSubsystem["根因一：物理音频蜂鸣 (440 Hz 真声输出)"]
        T1["playback_wasapi_shared_mode_master_clock"] -->|合成标准音 A| S1["0.2f * sin(2*pi*440*t)"]
        S1 -->|直接写入| W1["WasapiAudioOutput::write()"]
        W1 -->|共享模式无衰减透传| EP["系统默认物理音频输出设备 (扬声器/耳机)"]
        EP -->|物理发声| BEEP1["发出真实 440 Hz '嘟嘟嘟' 蜂鸣!"]
    end

    subgraph Win32Subsystem["根因二：Win32 消息队列饥饿 (输入溢出警告)"]
        T2["preview_viewport_100x_create_destroy_zero_leak"] -->|紧随其后运行| L1["100 次循环创建/销毁顶层 HWND + 子窗口"]
        L1 -->|WS_VISIBLE 触发 DWM 重绘| M1["操作系统向测试线程灌入海量 WM 消息"]
        M1 -->|测试线程仅 Sleep(5), 零消息泵| Q1["线程 Win32 消息队列积压失聪"]
        Q1 -->|若用户同时有键鼠输入| KB["win32k.sys 触发 Typeahead Buffer Overflow"]
        KB -->|内核蜂鸣| BEEP2["系统发出硬件级 'MessageBeep' 警告音!"]
    end
```

#### 根因一（确凿声学物证）：单测真实合成 440 Hz 正弦波直通物理声卡
- **责任代码**：[`tests/test_playback_session.cpp:480-506`](../../tests/test_playback_session.cpp#L480-L506)（`playback_wasapi_shared_mode_master_clock`）
- **物理还原**：
  ```cpp
  const size_t frames = static_cast<size_t>(rate) / 10;
  std::vector<float> interleaved(frames * channels);
  for (size_t i = 0; i < frames; ++i) {
      const float sample = 0.2f * std::sin(2.0f * 3.14159265f * 440.0f *
                                           static_cast<float>(i) / static_cast<float>(rate));
      for (uint32_t c = 0; c < channels; ++c) interleaved[i * channels + c] = sample;
  }
  audio->resume();
  audio->write(interleaved); // 连续多次写入！
  ```
- **机理分析**：该测试激活了 Windows WASAPI 真实物理音频设备（`IAudioClient` 共享模式），为了验证主时钟演进，直接合成了国际标准音 **A4 (440 Hz)** 的正弦波音频数据，并连续多次推入默认渲染端点！在带有扬声器/耳机的宿主机上，440 Hz 纯音被声卡直接播放出来，听感就是连续的 **“嘟——嘟——嘟”** 音频脉冲！

#### 根因二（内核队列饥饿）：100x 顶层窗口高频创建/销毁导致线程消息队列失聪
- **责任代码**：[`tests/test_raii_retirement.cpp:529-596`](../../tests/test_raii_retirement.cpp#L529-L596)（`preview_viewport_100x_create_destroy_zero_leak`）
- **位置关联**：在单测执行字母序中，该测试紧接在上述 WASAPI 音频测试（`playback_*`）之后执行！
- **机理分析**：
  1. 测试用例创建了 `WS_OVERLAPPEDWINDOW` 风格的顶层父窗口，并在 100 次循环中反复创建/调整尺寸/销毁带有 `WS_VISIBLE` 属性的子窗口；
  2. 整个测试线程处于密集计算与 `Sleep(5)` 轮询中，**全程没有任何 Win32 消息循环**（未调用 `PeekMessage` / `DispatchMessage`）；
  3. 当 Windows DWM 试图投递窗口同步、焦点转移、DPI 或输入状态变更时，该线程的消息队列处于完全饥饿失聪状态；
  4. 若用户在测试运行期间触碰键盘或移动鼠标，`win32k.sys` 无法将输入分发至无响应的窗口焦点队列，直接触发系统键盘缓冲区溢出（Typeahead Buffer Overflow），触发内核级警报蜂鸣（`MessageBeep(MB_OK)` / PC Speaker Bell）！

### 3.3 提请专家指导之根治方案
1. **音频单测脱毒隔离**：提请专家给出在不向物理音频设备倾倒可闻波形的前提下，验证 WASAPI 时钟演进的最优解（例如静音缓冲 `0.0f`、回环设备检测、或纯虚拟硬件时钟桩）；
2. **Win32 压测消息泵抽水与无头化**：
   - 彻底剥离测试用窗口的 `WS_VISIBLE` 属性，改为隐藏离屏窗口或 `HWND_MESSAGE` 纯消息窗口；
   - 在 100x 压测循环内部注入非阻塞的 `PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)` 消息抽水泵，确保测试线程队列时刻纯净。

---

## 第四部分：`schema/` 与 `scripts/` 资产纯洁性法医审计与清除方案

### 4.1 审查结论：未被 `ISSUES.md` 或 `DECISIONS.md` 跟踪的死滞垃圾
经地毯式排查，确认仓库根目录下的 `schema/` 与 `scripts/` 目录**均未在 `ISSUES.md` 或 `DECISIONS.md` 中进行过任何正式受控立项与决策登记**，属于严重违反奥卡姆剃刀与 DEC-002（源码树纯洁性铁律）的遗留死滞代码。

```
CineRTX/
├── scripts/
│   ├── gate_r8_desktop_check.ps1      [违规：散落的非受控 PowerShell 脚本]
│   └── reimplant_phase112r_comments.py [死代码：Phase 11.2-R 归档已销毁，脚本沦为垃圾]
└── schema/
    ├── CMakeLists.txt                 [过度设计：为离线代码生成自建构建目标]
    ├── reflection_codegen.cpp         [严重 Slop：20KB 的 C++ 代码生成器二进制]
    ├── schema_model.hpp               [冗余元模型定义]
    ├── verify_ssot.py                 [未纳入 CI 门禁的孤立脚本]
    └── generated/                     [严重违规：将构建派生产物签入 Git 版本库]
        ├── param_schema.generated.hpp
        ├── param_schema.generated.inc
        └── schema.json
```

### 4.2 为什么必须全面物理铲除？
1. **`scripts/reimplant_phase112r_comments.py`**：为 Phase 11.2-R 外部工程师违规删注释自救而编写的一次性批处理脚本。当前全仓注释已 100% 回植且归档库已物理销毁，该脚本已无任何输入源与存在价值，是典型的死滞垃圾；
2. **`scripts/gate_r8_desktop_check.ps1`**：外挂的宿主机桌面检测脚本，其功能应被收拢整合至标准 CTest 框架或专用 Native 测试夹具中，不应作为游离脚本污染项目根目录；
3. **`schema/` 体系严重违背现代 C++ 与奥卡姆剃刀**：
   - 在 C++26 / C++23 时代，依靠编写一个独立的 `reflection_codegen.cpp` 编译出可执行程序，再运行该程序生成 `.generated.hpp` 并强制签入 Git 版本库，是极其原始且丑陋的代码膨胀（Slop）；
   - 该体系完全可被 C++20/23 的编译期类型列表（`std::tuple`）、`constexpr` 反射函数及既有 `param_schema.cpp` 直接取代，编译期直接内联计算，**彻底拔除整个代码生成器与全部派生文件**！

---

## 第五部分：呈报请求清单（Request for Expert Guidance）

请首席领域专家组就以下关键事项出具权威裁决与方案指导：

1. **施工驳回裁决确认**：正式背书对 Phase 11.3 外部执行工程师的驳回决议，冻结其任何主干合龙权限；
2. **极致现代 C++ 重构路线图出具**：
   - 出具《CineRTX 极限现代 C++26 重构与标准库先验替换蓝图》；
   - 给出如何将 `NleProject`、`EditHistory`、`RenderGraph` 及参数系统重塑为基于 `std::variant` / `std::tuple` / `constexpr` 的零包装器、零冗余结构的权威示范；
   - 裁决除底层硬件交互外的自定义类生存边界。
3. **测试蜂鸣声物理消解方案确认**：签署对 `test_playback_session.cpp`（WASAPI 440Hz 哑音化）与 `test_raii_retirement.cpp`（HWND 无头化与消息队列抽水）的修补指令；
4. **`schema/` 与 `scripts/` 清除指令下达**：正式下达物理拔除 `scripts/` 与 `schema/` 目录的授权裁决，实现全仓架构的断崖式轻量化。

---
*CineRTX 项目总协调负责人（Coordinator） 呈报*  
*2026年11月21日*
