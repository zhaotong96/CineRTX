# **Wave 0 \- Stage A 宪法纠偏、卫生清剿与测试静音隔离施工单**

## **1\. 元信息与宪法规约**

### **1.1 工程上下文与前置基线**

| 属性项 | 配置/规约约束规范 | 校验口径与物理索引 |
| :---- | :---- | :---- |
| **施工波次** | `Wave 0 - Stage A`（前置止血与架构防线纠偏） | 优先于任何业务功能与重构推进 |
| **依赖前置** | Commit `0ee04fd` / Release Tag `v0.4.0` | `git reset --hard 0ee04fd` |
| **构建目标预设** | `windows-msvc-ninja` | MSVC `/std:c++latest` / Ninja Generator |
| **执行模式** | Superpowers 原子图谱隔离执行 | 严格遵循 TDD、防线审计与零破坏准则 |

### **1.2 宪法红线强约束 (Constitutional Directives)**

#### **红线 1：具名类型配额 (Type Quota)**

全仓 `include/` 与 `src/` 下具名 `struct` / `class` 数量实测已达 34/35（仅余 1 个配额，依据 \[DEC-009\]）。**本 Wave 严禁新增任何具名 `struct` 或 `class`**。所有新增接口或数据控制必须复用既有类型结构，或通过现有类型的标量字段扩展、`enum class` / `std::variant` 及匿名单层结构实现，严格保持类型总数 `<= 34`。

#### **红线 2：静态 IAT 导出/导入净空 (IAT Import Boundary)**

依据 `cmake/AuditImports.cmake` 物理审计门禁：

* **GUI 模式**：静态导入物理 DLL 数量必须 `<= 4`（仅允许 `kernel32.dll`, `user32.dll`, `gdi32.dll`, `bcrypt.dll`）。严禁静态链入以下 10 项黑名单 DLL：`shell32`, `dwmapi`, `comdlg32`, `comctl32`, `uxtheme`, `d3d12`, `dxgi`, `ole32`, `kernelbase`, `advapi32`。  
* **HEADLESS 模式**：静态导入物理 DLL 数量必须 `== 2`（依据 `[DEC-018]`）。

#### **红线 3：TRT 单 Profile 铁律（纠偏 `[DEC-011]` / `[ISSUE-005]`）**

**严厉纠正与废除《专家方案 02 号文档》§1.1 与 §3.1 中关于"在单个 `.engine` 内构建多组 `IOptimizationProfile` 动态切换"的违宪建议！**  
架构事实证明：Phase 11.4 实测单 TensorRT 引擎多 Profile 运行时切换会导致 VRAM 爆存至 `8.89 GB` 并直接引发物理帧率坍塌至 `<1fps`。全仓必须无条件执行"一桶一单 Profile 独立 `.engine`（One OptimizationProfile per Engine File）"原则。运行时多分辨率/多 Batch 切换必须通过离散 Engine 句柄按需载入与销毁实现。

#### **红线 4：代码规模与隐私合规 (SLOC & Privacy)**

* **核心 SLOC 上限**：严格限制 `<= 23,000`（当前物理行数 `12,157`）。  
* **隐私物理合规**：`AuditPrivacy == 0`。编译产物、二进制字符串及测试日志中严禁出现任何开发机本地盘符（如 `<local_user_profile_path>`）、绝对路径、特定用户名或设备 Hostname。

---

## **2\. Task 1：单元/集成测试"嘟嘟嘟"440Hz 扬声器异响与 HID/WM\_SETCURSOR 蜂鸣根除**

### **2.1 事实根因定位分析**

```
+-----------------------------------------------------------------------------------+
|                              TEST EXECUTION FLOW                                  |
|                                                                                   |
|  tests/test_playback_session.cpp:563                                              |
|  Construct PlaybackOptions{Rational{25,1}, 1.0, 5u, 0ull, 0, true}                |
|                                |                                                  |
|                                v                                                  |
|  Decode Container: h264/hevc/av1_25fps_gop12.mkv (Contains 440 Hz AAC mono)       |
|                                |                                                  |
|                                v                                                  |
|  Looping Operations: play -> Ended -> seek -> stepFrame -> setSpeed               |
|                                |                                                  |
|                                v                                                  |
|  src/core/playback_session_audio.cpp:194-197                                      |
|  mixAvx2 gain = 1.0f (Hardcoded) -> WasapiAudioOutput Pumps PCM to Hardware Speaker |
|  ==> RESULT: Physical 440Hz Audio Beep during ctest                               |
+-----------------------------------------------------------------------------------+
                                 +
                                 |  (Simultaneous Secondary Beep Issue)
                                 v
+-----------------------------------------------------------------------------------+
|  1. Unhandled WM_SETCURSOR in Test WindowProc -> Passes to DefWindowProc          |
|     ==> Triggers System MessageBeep penalty sound.                                |
|  2. Unescaped '\av1...' path string in test log ==> Triggers ASCII 0x07 (BEL).   |
+-----------------------------------------------------------------------------------+
```

1. **音频流物理泄露**：`tests/test_playback_session.cpp:544-646`（重点为第 563 行）在测试中构造 `PlaybackOptions`，加载包含 `440 Hz AAC mono` 音频轨的测试媒体文件（`h264_25fps_gop12.mkv`、`hevc_25fps_gop12.mkv`、`av1_25fps_gop12.mkv`），并密集执行 Seek/Step/Speed 组合测试。`src/core/playback_session_audio.cpp:194-197` 中 `mixAvx2` 混音增益被硬编码为 `1.0f`，导致 Audio Engine 直接打开 Windows 默认 WASAPI 物理设备，向硬件扬声器实时输出高分贝 440Hz 鸣响。  
2. **系统 MessageBeep 干扰**：无头 GUI/离屏测试创建的临时窗口缺少针对 `WM_SETCURSOR` 消息的拦截，导致消息透传至 `DefWindowProc` 并触发 Windows 系统报错音（`MessageBeep`）。  
3. **控制台 0x07 (BEL) 泄漏**：测试转义日志中包含了未转义的字符串（如包含 `\a` 路径的前缀 `\av1_25fps...`），被控制台解释为 `0x07 ASCII BEL` 蜂鸣指令。

### **2.2 零新增类型扩展设计 (Zero-Type Cost Design)**

为维持类型配额 `<= 34`，不允许添加新的 `struct` / `class`。扩展现有 `include/rtx/` 内的 `PlaybackOptions` 与 `WasapiAudioOutput` 接口：

```
// 在现有的 PlaybackOptions 结构体中扩展标量控制字段（不增加具名类型）
struct PlaybackOptions {
    Rational frame_rate{25, 1};
    float speed{1.0f};
    uint32_t buffer_depth{5u};
    uint64_t start_pts{0ull};
    int loop_count{0};
    bool enable_audio{true};
    
    // Stage A 新增扩展参数（默认静音沉没，零类开销）
    bool null_sink{false};      // true 时启用虚拟音频沉没器 (不打开物理 WASAPI 设备)
    float audio_gain{1.0f};    // 音频输出混音增益 (0.0f 为物理绝对静音)
};
```

### **2.3 执行步骤 (TDD 模式)**

#### **Step 1.1：扩展音频输出沉没逻辑与静音控制**

1. 修改 `src/core/wasapi_audio_output.cpp` 与 `src/core/playback_session_audio.cpp`：  
   * 在 `PlaybackSession` 初始化音频通道时校验 `options.null_sink`。若为 `true`，跳过 Windows `IMMDeviceEnumerator` 与 `IAudioClient` 的物理设备激活，接入内联的 Dummy Clock Pump（维持高精度主时钟推进与 PTS 同步逻辑，但丢弃或填充 `0.0f` PCM 缓冲）。  
   * 混音逻辑中应用 `options.audio_gain` 变量，当 `gain == 0.0f` 时跳过 AVX2 硬件乘法运算，直接执行 `memset(0)`。

#### **Step 1.2：单元测试集静音隔离与消息隔离**

1. **测试用例修正**：修改 `tests/test_playback_session.cpp:563` 及所有相关测试例中的 `PlaybackOptions` 实例化逻辑，统一强制显式声明静音参数：

```
PlaybackOptions opts{Rational{25, 1}, 1.0f, 5u, 0ull, 0, true, /*null_sink=*/true, /*audio_gain=*/0.0f};
```

2. **窗口消息拦截**：检查 `tests/test_gui_workstation.cpp` 及主/测试 WindowProc 过程，强制增加 `WM_SETCURSOR` 拦截处理：

```
case WM_SETCURSOR:
    if (LOWORD(lParam) == HTERROR || is_testing_mode) {
        SetCursor(NULL);
        return TRUE; // 阻止透传至 DefWindowProc 触发 MessageBeep
    }
    break;
```

3. **控制台 BEL 过滤器**：在测试日志输出流及路径格式化工具中，添加 ASCII 控制字符过滤函数，确保所有 `< 0x20` 的字符（尤其是 `0x07 \a`）被转义为 `\x07` 可视文本。

#### **Step 1.3：验证门禁**

运行 `ctest --output-on-failure -R test_playback_session`，验证测试 100% 通过且全程无物理扬声器 440Hz 异响与系统蜂鸣。

---

## **3\. Task 2：编译开关收敛、死分支清剿与构建入口统一**

### **3.1 事实根因与缺陷定位**

1. **构建入口分裂**：`build.bat:68` 硬编码指定 `-DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-zig.cmake`，与项目宪法文件 `README.md` 及发布流水线 `PackageRelease.cmake` 主推的 `windows-msvc-ninja` 预设产生严重撕裂。  
2. **残留死分支**：`src/shim/loader/nv_encoder.cpp:241, 277` 遗留了硬编码死代码 `constexpr const char* uhq_env = nullptr;` 与 `bframes_env = nullptr;`，导致后续判断分支沦为不可达代码。  
3. **CMake 构建逻辑混杂**：`CMakeLists.txt:200-244` 在 CMake Configure 阶段与 Build 阶段同时包含了重复的 CUDA PTX 编译规则；`rtx_tests` 构建目标将内部 `.cpp` 源码文件直接硬编码包含并重新编译，破坏了静态库的目标隔离边界。  
4. **CLI Help 双规漂移**：`src/cli/main.cpp:75-116` 内部维护了一套硬编码的英文 `printHelp()` 函数，与 `src/cli/param_schema.cpp::formatCliHelp()` 的动态 Schema 输出并存，导致命令行帮助信息修改时频繁产生漂移。  
5. **局部告警掩蔽**：`src/core/wasapi_audio_output.cpp:74` 强行使用了 `#pragma warning(disable : 4324)`（structure was padded due to alignment specifier），遮蔽了结构体对齐风险。

### **3.2 治理与重构步骤**

#### **Step 2.1：统一 `build.bat` 构建脚本**

重构 `build.bat`，全面移除 Zig 工具链的强行注入。默认直接调用 CMake Presets：

```
@echo off
cmake --preset windows-msvc-ninja %*
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
cmake --build --preset windows-msvc-ninja
```

保留原始命令行参数 `%*` 透传能力，严禁引入新的"幽灵"环境变量。

#### **Step 2.2：清剿 NVENC Loader 死分支**

直接物理删除 `src/shim/loader/nv_encoder.cpp` 第 241 行与 277 行的死代码常量声明，将依赖环境变量控制的编码增强逻辑恢复为标准的 `std::getenv` 运行时查询或固化为确定的配置常量。

#### **Step 2.3：规范 CMakeLists.txt 规则与测试链接边界**

1. 清理 `CMakeLists.txt:200-244` 中在配置期生成的旧 PTX 规则。统一使用 `add_custom_command` 与 `add_custom_target` 声明单一确定性的 PTX 编译流水线。  
2. 剥离 `rtx_tests` 中硬塞的 `.cpp` 源文件。强制 `rtx_tests` 仅链接 `rtx_core` / `rtx_shim` 静态库目标：

```
target_link_libraries(rtx_tests PRIVATE rtx_core rtx_shim GTest::gtest_main)
```

#### **Step 2.4：消除 CLI Help 双规机制**

彻底清空并删除 `src/cli/main.cpp:75-116` 中的硬编码 `printHelp()` 实现，将 CLI `--help` 入口统一路由至统一数据源：

```
#include "param_schema.hpp"

void printHelp() {
    std::cout << rtx::cli::formatCliHelp() << std::endl;
}
```

#### **Step 2.5：物理对齐数据结构以移除 `#pragma warning`**

分析 `src/core/wasapi_audio_output.cpp:74` 涉及的内部结构体，删除 `#pragma warning(disable : 4324)`。通过调整字段放置顺序或显式声明 `alignas` 重新设计内存布局，避开无谓的对齐填充：

```
// 重构内存布局：按 8 字节 -> 4 字节 -> 1 字节 严格递减排列
struct alignas(16) AudioBufferHeader {
    uint64_t timestamp_pts;
    uint32_t sample_rate;
    uint32_t channel_count;
    float gain;
    bool is_muted;
    uint8_t padding[7]; // 显式填充，消除 C4324 隐式 Padding 告警
};
```

---

## **4\. Task 3：`docs/TODO/README.md` 7 项历史遗留裁决与文档收编**

为彻底清空 `docs/TODO/` 的架构漂移状态，对历史遗留的 7 项待定议题在宪法框架下给予终局裁决与再归类：

```
                    docs/TODO 7 HISTORICAL ITEMS
                                 |
        +------------------------+------------------------+
        |                                                 |
        v                                                 v
[WAVE MIGRATION]                                 [PHASE 14 ARCHIVE]
Item #1 -> Wave 3-A/B Rational Base              Item #2 -> FFmpeg 8.x Evolution
Item #7 -> Wave 1-B Software CRF                 Item #3 -> Nsight Profiling Evidence
                                                 Item #4 -> Physical HDR10 Badge
                                                 Item #5 -> 2-Hour Soak Stability
                                                 Item #6 -> Linux Headless Port
```

### **4.1 逐项裁决与归档映射表**

| 序号 | 遗留议题名称 | 架构判定与解题路径 | 目标归宿标记 |
| :---- | :---- | :---- | :---- |
| **\#1** | **NTSC 1001 时基因子扩展** | 彻底弃用 `double` 浮点时间戳。在核心底座中引入基于 `int64_t num, den` 的 `Rational` 精确有理数表达，彻底消除 `23.976` (24000/1001)、`29.97` (30000/1001)、`59.94` (60000/1001) 的长时运行累积漂移误差。 | **Wave 3-A / Wave 3-B 底座统一落盘** |
| **\#2** | **FFmpeg 8.x API 演进适配** | 保持当前 FFmpeg 6.x / 7.x ABI 稳定链接，将 8.x 弃用 API 的迁移屏蔽层纳入远期技术债务池。 | 归档至 Phase 14 路线池 |
| **\#3** | **Nsight Systems 线程级证据** | 性能 Profiling 探针与 NVTX 标记代码不侵入主干核心逻辑，作为 Release 物理验收的独立 Profiling 附件。 | 归档至 Phase 14 验收附件 |
| **\#4** | **HDR10 物理徽标与 Color Matrix** | 移出 Stage A 范围，待 Color Management Pipeline 与 Dynamic Tone Mapping 完善后统一接入。 | 归档至 Phase 14 路线池 |
| **\#5** | **2 小时 Soak 长稳压测** | 属于 Release 最终门禁测试，不作为每日 CI/CD 单元测试标准。 | 归档至 Phase 14 验收门禁 |
| **\#6** | **Linux 无头 Headless 形态** | 属于远期跨平台解耦任务，当前专注于 Windows 平台 NVENC/WASAPI 极致性能优化。 | 归档至 Phase 14 路线池 |
| **\#7** | **CPU 软件编码 \+ CRF 兜底模式** | 作为 NVENC 硬件编码失败时的 Safe-mode 退化路径。 | **Wave 1-B 交付** |

---

## **5\. 验收门禁 (Verification Gates)**

施工完成后，必须按顺序严格通过以下四道物理门禁：

```
+-----------------------------------------------------------------------------------+
|                               VERIFICATION GATES                                  |
|                                                                                   |
|  +------------------------+                                                       |
|  | Gate 1: Zero Warning   |  cmake --build --preset windows-msvc-ninja            |
|  |           & Zero Error |  ==> 0 Warnings, 0 Errors                             |
|  +-----------+------------+                                                       |
|              |                                                                    |
|              v                                                                    |
|  +------------------------+                                                       |
|  | Gate 2: Silent Test    |  ctest --output-on-failure                            |
|  |           Suite        |  ==> 100% Pass, 0 Audio Beeps/BEL                     |
|  +-----------+------------+                                                       |
|              |                                                                    |
|              v                                                                    |
|  +------------------------+  AuditTypes <= 34/35                                  |
|  | Gate 3: Constitutional |  AuditImports (GUI <= 4, HEADLESS == 2)               |
|  |           Static Audits|  AuditPrivacy == 0                                    |
|  |                        |  AuditSloc <= 23,000                                  |
|  +-----------+------------+                                                       |
|              |                                                                    |
|              v                                                                    |
|  +------------------------+                                                       |
|  | Gate 4: Clean Git      |  git status --porcelain                               |
|  |           Workspace    |  ==> Zero untracked drifts in docs/TODO               |
|  +------------------------+                                                       |
+-----------------------------------------------------------------------------------+
```

### **5.1 门禁 1：零告警编译门禁**

```
cmake --preset windows-msvc-ninja
cmake --build --preset windows-msvc-ninja
```

* **标准**：编译输出必须达到 **0 Warning, 0 Error**（针对 MSVC `/W4 /WX` 严苛模式）。

### **5.2 门禁 2：静音测试套件门禁**

```
ctest --preset windows-msvc-ninja --output-on-failure
```

* **标准**：全仓单元测试与集成测试全绿。执行期间物理扬声器 **绝对静音**，无任何 440Hz 鸣响起伏、无 Windows `MessageBeep` 系统提示音、无控制台 ASCII `0x07 (BEL)` 蜂鸣。

### **5.3 门禁 3：CMake 静态宪法审计门禁**

在配置与编译构建中通过 `cmake/AuditTypes.cmake`、`cmake/AuditImports.cmake`、`cmake/AuditPrivacy.cmake` 及 `cmake/AuditSloc.cmake` 检查：

* `AuditTypes`：全仓具名 `struct`/`class` 累计总数 `<= 34`（硬性限额 35）。  
* `AuditImports`：GUI 静态链入 DLL `<= 4`（无黑名单 DLL）；HEADLESS 模式 `== 2`。  
* `AuditPrivacy`：物理检查断言 `AuditPrivacy == 0`，未发现绝对路径与私密字符串。  
* `AuditSloc`：核心 SLOC 行数处于 `<= 23,000` 绿色安全区。

### **5.4 门禁 4：文档清洁度门禁**

确认 `docs/TODO/README.md` 已全面更新并清理完成，`git status` 显示无未纳管的漂移临时文件。