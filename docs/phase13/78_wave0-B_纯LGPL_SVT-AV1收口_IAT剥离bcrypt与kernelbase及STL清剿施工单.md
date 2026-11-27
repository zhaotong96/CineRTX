# **78\_wave0-B\_纯LGPL\_SVT-AV1收口\_IAT剥离bcrypt与kernelbase及STL清剿施工单**

## **1\. 施工元数据与核心宪法红线**

### **1.1 元数据声明**

* **施工波次**：Wave 0 \- Stage B（紧接 `77_wave0-A` 回退 `0beda87` 后执行）。  
* **核心目标**：完成纯 LGPL \+ SVT-AV1 许可证收口、PE 静态 IAT 剥离 `bcrypt.dll` 与黑名单 `kernelbase.dll` 至 `<= 3`、工具链单一归一化、Modern C++23/26 标准库替代清剿及 `docs/TODO` 7 项远期规划收编。  
* **执行基线**：依据 76 号终审裁决《CineRTX Phase 13 74号返工终审验收报告与单EXE极限Modern C++重构演进方案》。

### **1.2 宪法硬红线规约**

| 维度 | 宪法约束标准 | 违规惩罚与判定逻辑 |
| :---- | :---- | :---- |
| **零开关与纯 LGPL 合规** | 彻底砍除 `CINERTX_ENABLE_GPL_CODECS` 编译选项、`libx265` 及 FFmpeg `--enable-gpl` / `--enable-nonfree` 编译分支。CPU 软编统一收敛至 BSD-3-Clause-Clear \+ AOM Patent License 1.0 的 `SVT-AV1`。 | 杜绝与专有 `rtx_ngx_bridge.dll` 产生的 GPL 许可证法律冲突。`AuditSpdx.cmake` 扫描发现任何 GPL/Non-Free 符号即门禁熔断。 |
| **单 EXE 静态 IAT 门禁** | 废弃 `AuditImports.cmake` GUI/HEADLESS 双口径检查。对唯一宿主产物 `CineRTX.exe` 强制锁死静态导入物理 DLL **`<= 3`**（仅限 `kernel32.dll`, `user32.dll`, `gdi32.dll`）。 | `bcrypt.dll` 与黑名单 `kernelbase.dll` 零容忍清零。构建步骤检测到导出表额外链接即宣告失败。 |
| **具名类型净降** | `AuditTypes.cmake` 具名类型总量严格执行 `AuditTypes < 34` 拦截线。严禁新增任何具名 `struct` / `class`。 | 必须通过 Modern C++23/26 STL 替代运动清退冗余自定义类与死类（如 `ShotGroupSegmenter`）。 |

---

## **2\. Task 1：纯 LGPL FFmpeg \+ SVT-AV1 许可证与工具链单一基线收口**

### **Step 1.1：砍除 `--enable-gpl`、`libx265` 与 `CINERTX_ENABLE_GPL_CODECS`**

1. 修改 `CMakeLists.txt`：物理移除 `option(CINERTX_ENABLE_GPL_CODECS ...)` 及关联分支条件，全面清剿 `x265` / `libx264` / `--enable-gpl` / `--enable-nonfree` 构建逻辑。  
2. 修改 `cmake/RtxFfmpegStatic.cmake`：将 FFmpeg 静态构建配置锁死为纯 LGPL 模式，CPU 软编码链条唯一指定为 `libsvtav1`：  
   * 移除 `--enable-gpl` 与 `--enable-libx265`  
   * 添加 `--enable-libsvtav1` 与 `--disable-decoder=hevc`（若仅需要 AV1 / H.264 解码）  
3. 法务文本同步：在 `AuditSpdx.cmake` 及第三方许可证合规声明中注销 GPLv3/GPLv2 文本，登记完整的 `BSD-3-Clause-Clear` 与 `Alliance for Open Media Patent License 1.0` 条款。

### **Step 1.2：统一工具链基线 `MSVC + Ninja + /MT + /W4 /WX`**

1. 物理移除 `build.bat:68` 中的 `cmake/toolchain-zig.cmake` 交叉编译预设选项。  
2. 全项目 C++ 工具链唯一锚定为宿主机 `MSVC + Ninja + /MT + /W4 /WX` 构建体系。  
3. 验证宿主机 `MSVC` / `clang-cl` 能无缝无错编译纯 C FFmpeg 静态归档文件（`libavcodec.lib`, `libavutil.lib`, `libswscale.lib`），移除 `RtxFfmpegStatic.cmake` 中残存的 Zig 专用兼容逻辑，实现全平台构建工具链归一化。

```
[Legacy Toolchains (Zig / Cross-GCC)] ---> 彻底移除
                                             |
[CineRTX Core Engine] ---------------------> MSVC + Ninja + /MT + /W4 /WX (Single Baseline)
                                             |
[FFmpeg Static Archive] -------------------> 纯 LGPL + SVT-AV1 (BSD-3-Clause-Clear)
```

---

## **3\. Task 2：PE 静态 IAT 精准剥离 bcrypt.dll 与黑名单 kernelbase.dll（5 \-\> \<= 3）**

### **Step 2.1：零容忍拔除 `bcrypt.dll`**

1. **FFmpeg 配置层切断**：在 `cmake/RtxFfmpegStatic.cmake` 中显式配置 `--disable-bcrypt`，强制生成的目标 `config.h` 满足：

```
#define HAVE_BCRYPT 0
```

   彻底切断 `libavutil/random_seed.c` 对 `BCryptGenRandom` 链接 `-lbcrypt` 的默认依赖探测。

   

2. **主工程链接层清理**：检查并移除所有 target 的 `target_link_libraries` 中的 `bcrypt.lib`。  
3. **随机源替代策略**：  
   * 针对非加密用途（如临时文件名生成、测试序列 ID），统一使用 C++21/23 标准库 `<random>` 下的 `std::mt19937_64`。  
   * 针对确需加密级安全随机数的路径，采用 `sys_dynload` 机制在运行时通过 `LoadLibraryA("bcrypt.dll")` \+ `GetProcAddress` 惰性解析 `BCryptGenRandom`，确保 PE 导入表（IAT）无静态依赖。

### **Step 2.2：零容忍拔除黑名单 `kernelbase.dll`**

1. **导入表符号精细化分析**：运行 `dumpbin /imports CineRTX.exe` 审查挂载在 `KERNELBASE.dll` 或 `api-ms-win-core-synch-l1-1-0.dll` 名下的符号。  
2. **MSVC `/MT` 静态运行时路径拦截**：  
   * **根因分析**：标准库 `<atomic>` 或 `<semaphore>` 中使用 `std::atomic::wait` / `std::counting_semaphore` 会拉入 `libcpmt.lib(atomic_wait.obj)`，进而强制链接 `Synchronization.lib` 并依赖 `WaitOnAddress` / `WakeByAddressSingle`。  
   * **重构方案**：全面清理代码库中的 `std::atomic::wait` 与 `std::counting_semaphore`，降级为基于 Windows 基础原语包装的 `std::mutex + std::condition_variable`；同步从 CMake 中剥离 `Synchronization.lib` 的显式与隐式链接。  
3. **Win10+ 高级 API 运行时化**：针对 `SetThreadDescription` 等 API，强制统一走 `sys_dynload` 惰性解析。  
4. **IAT 门禁升级**：更新 `cmake/AuditImports.cmake`，废弃 Headless/GUI 双重标准，对唯一主产物 `CineRTX.exe` 校验静态 IAT DLL 数量：

```
# AuditImports.cmake (Wave 0 Stage B 门禁控制)
set(MAX_ALLOWED_IMPORTS 3)
set(ALLOWED_DLLS "kernel32.dll" "user32.dll" "gdi32.dll")
set(FORBIDDEN_DLLS "bcrypt.dll" "kernelbase.dll" "synchronization.dll")

# 提取 PE 静态导入表并校验...
```

---

## **4\. Task 3：Modern C++23/26 标准库替代清剿、冗余自定义类清退与 docs/TODO 7 项收编**

### **Step 3.1：宿主机编译器探针与无宏使用**

基于 MSVC 最新工具链开启 `/std:c++23`（或 `/std:c++latest`）。确立如下标准库设施直接使用规约，全仓**严禁引入 `#ifdef` 版本隔离宏**：

| C++23/26 标准特性 | 替代的传统模式 / 自定义实现 | 应用场景与实施标准 |
| :---- | :---- | :---- |
| `std::expected<T, E>` | 自定义 `StatusOr<T>` / 错误码结构 | 统一作为跨模块无异常函数返回类型。 |
| `std::move_only_function<R(Args...)>` | `std::function` (有堆分配/拷贝开销) | 用于异步任务调度器与无状态回调传递。 |
| `std::jthread` & `std::stop_token` | 自定义 Thread 包装 / `std::thread` \+ `atomic<bool>` | 编码器后台工作线程与渲染管道异步线程控制。 |
| `std::vformat` / `std::format` | `sprintf_s` / 自定义 `StringPrintf` | 统一日志与日志格式化输出。 |

### **Step 3.2：具名类型物理清退与白名单管控**

运行 `AuditTypes.cmake` 扫描全仓 34 项具名类型，严格按三类白名单清理冗余类型，确保类型总数低于 34 阈值：

1. **白名单保留域**：  
   * 跨动态库边界 `extern "C"` POD 结构；  
   * GPU 16 字节对齐常量缓冲区（Constant Buffer）结构；  
   * 包含 3 个以上异构命名字段的 Rule-of-Zero 纯领域聚合体（Aggregate）。  
2. **重点清退目标与替代方案**：  
   * **死类拔除**：物理删除 `hw_transcode_internal.h:242` 的幽灵死类 `ShotGroupSegmenter`（类型数净减 1）。  
   * **RAII 句柄轻量化**：将自定义 RAII 句柄类替换为带无状态 Lambda 删除器的 `std::unique_ptr`：

```
using WasapiHandlePtr = std::unique_ptr<IAudioClient, decltype([](IAudioClient* p) noexcept {
    if (p) p->Release();
})>;
```

   * **错误返回统一**：抛弃自定义 Result 结构体，全面迁移至 `std::expected<T, CommonErrorCode>`。  
   * **Wasapi 硬件隔离**：`WasapiAudioOutput` 单测隔离采用 `std::variant<std::monostate, WasapiHandle>`。采用 `std::monostate` 状态推进虚拟时钟而不开启真实声卡硬件，消除 440Hz 测试鸣响。  
   * **多态体系退化**：将深层继承体系退化为 `enum class` 标量 \+ 无状态自由函数 / `std::move_only_function` 组合。

### **Step 3.3：奥卡姆剃刀卫生清剿与 docs/TODO 收编**

1. **代码卫生彻底清理**：  
   * 移除 `nv_encoder.cpp:241,277` 中的 `uhq_env` / `bframes_env` 死分支逻辑；  
   * 物理删除 `ngx_remaster.cpp:105` 全局函数 `activePreviewMonitor()`；  
   * 物理删除 `workstation_canvas.h:79-88` 全局函数 `active_vsr_target()`；  
   * 移除 `wasapi_audio_output.cpp:74` 的 `#pragma warning(disable : 4324)`，调整结构体成员布局实现自然对齐；  
   * 移除 `RtxNgxBridgeConfig::reserved[0]` 魔法占位字段；  
   * 精简 `CMakeLists.txt:200-244` 中重复定义的 PTX 编译规则。  
2. **`docs/TODO/README.md` 7 项远期规划收编**：

| TODO 编号 | 原规划主题 | 裁决决议与 Wave 0 Stage B 收编动作 |
| :---- | :---- | :---- |
| **\#1** | `<chrono>` 1001 时基支持 | 吸收进 Wave B，统一采用 Modern C++20 `<chrono>` 比例时基表示。 |
| **\#2** | FFmpeg 8.x 升级评估 | 延后至 Phase 14，当前 Wave 锁死 FFmpeg 7.x 纯 LGPL 静态归档。 |
| **\#3** | Nsight Systems 标记注入 | 转入 Wave C 阶段作为纯性能测试分析工具接入。 |
| **\#4** | HDR10 徽标自绘 | 并入 Wave D，统一走 Direct2D (D2D1) 矢量渲染管道。 |
| **\#5** | 2h Soak 稳定性测试 | 收编为发布前持续集成（CI）阶段的门禁流水线。 |
| **\#6** | Linux 无头形态抽象 | 移出 Phase 13。**严禁在 Windows 专用主干中预留跨平台虚接口**。 |
| **\#7** | SVT-AV1 编码器集成 | 本波次（Wave 0 Stage B）直接落地实施并完成纯 LGPL 闭环。 |

---

## **5\. 施工验收 Gate 指标体系**

施工完成后，必须通过以下自动化与静态检测门禁：

```
[Wave 0 Stage B Pipeline Verification]
 ├── MSVC Build Check  ------------> exit code 0 (/W4 /WX)
 ├── AuditImports.cmake ------------> CineRTX.exe Static DLLs <= 3 (kernel32, user32, gdi32)
 |                                    [bcrypt.dll: ZERO | kernelbase.dll: ZERO]
 ├── AuditTypes.cmake ---------------> Total Named Types < 34
 ├── rtx_tests.exe -----------------> 157/157 PASS (Audio hardware beep: NONE)
 ├── AuditPrivacy.cmake ------------> Privacy violations == 0
 └── AuditSpdx.cmake ---------------> License Compliance == PASS (No GPL / Non-Free)
```

1. **编译器构建**：执行 `.\build.bat`，MSVC 工具链在 `/W4 /WX` 严苛警告模式下零 Warning、零 Error，脚本退出码为 `0`。  
2. **静态导入表（IAT）校验**：`AuditImports.cmake` 判定 `CineRTX.exe` 依赖物理 DLL 数量 **`<= 3`**（仅 `kernel32.dll`, `user32.dll`, `gdi32.dll`），零 `bcrypt.dll`，零 `kernelbase.dll`。  
3. **类型数量控制**：`AuditTypes.cmake` 校验具名类型总数严格小于 34 项（已拔除 `ShotGroupSegmenter` 等死类与包装类）。  
4. **单元测试与合规性**：运行 `rtx_tests.exe`，实现 **157/157 PASS**，物理声卡零 440Hz 鸣响；`AuditPrivacy` 检查项返回 `0`；`AuditSpdx` 许可证审核通过（无 GPL/Non-Free 残留）。

