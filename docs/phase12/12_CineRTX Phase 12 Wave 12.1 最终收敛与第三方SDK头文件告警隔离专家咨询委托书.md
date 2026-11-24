# CineRTX Phase 12 Wave 12.1 最终收敛与第三方 SDK 头文件告警隔离专家咨询委托书

- **文档编号**：`CINERTX-P12-DELEGATION-012`
- **归档路径**：[`docs/phase12/12_CineRTX Phase 12 Wave 12.1 最终收敛与第三方SDK头文件告警隔离专家咨询委托书.md`](./12_CineRTX%20Phase%2012%20Wave%2012.1%20最终收敛与第三方SDK头文件告警隔离专家咨询委托书.md)
- **呈报主体**：CineRTX 项目总协调负责人（Coordinator）
- **受呈报方**：多媒体系统工程与 GPU 异构计算首席领域专家组（Extreme Modern C++ Purist & Architecture Board）
- **当前状态**：🟡 **Wave 12.1 纠偏达 85%，提请最后两项编译阻断微观方案终审（ADVANCED RECTIFICATION & PENDING FINAL CLOSURE）**
- **审验标的**：
  * [`10_CineRTX Phase 12 Wave 12.1 施工异化与构建断环终审裁决暨纠偏施工方案.md`](./10_CineRTX%20Phase%2012%20Wave%2012.1%20施工异化与构建断环终审裁决暨纠偏施工方案.md)（`CINERTX-P12-RULING-010`）
  * [`11_CineRTX Phase 12 Wave 12.1 纠偏施工汇报_构建零警告与契约闭环.md`](./11_CineRTX%20Phase%2012%20Wave%2012.1%20纠偏施工汇报_构建零警告与契约闭环.md)（`CINERTX-P12-REPORT-011`）
- **基线上下文**：Windows 宿主机物理复核，CMake 配置通过，`AuditTypes.cmake` 全绿（155/155 通过），MSVC `/WX` 构建推进至第 47/85 个目标。

---

## 一、 委托背景与整改进展通报

在首席领域专家组签发 [10 号纠偏裁决书](./10_CineRTX%20Phase%2012%20Wave%2012.1%20施工异化与构建断环终审裁决暨纠偏施工方案.md) 后，执行工程师端正态度，呈报了 [11 号纠偏施工汇报](./11_CineRTX%20Phase%2012%20Wave%2012.1%20纠偏施工汇报_构建零警告与契约闭环.md)。

协调负责人在 Windows 宿主机上使用 MSVC 19.51+ 与 Ninja 进行了真实的穿透式构建与门禁复核，**确认以下关键纠偏项已 100% 成功闭环**：
1. **工作区垃圾清零**：源码树内 11 个 `*.pre12_03.bak` 已全部物理移出；`CMakeLists.txt` 配置期 `_RTX_JUNK_FILES` 阻断门禁运行正常。
2. **具名类型契约全绿**：`cmake -P cmake/AuditTypes.cmake` 实测输出 `OK: 155 named types, all justified (Std-First law)`，`nv_encoder.cpp` 中 `BitstreamSlot` 契约紧邻彻底达标。
3. **内存自然重排消除 `C4324`**：`telemetry_bus.h`、`audio_mixer_avx2.h` 显式补齐成功；`wasapi_audio_output.cpp` 结构体重排顺利编译，零 `C4324` 警告。
4. **未引用形参消除 `C4100`**：`hw_transcode_stages.cpp` 的 `audio_segments` 成功省略形参名，编译无警告。

然而，在推进 MSVC 全量构建时，触发了最后**两处阻断性问题**，特呈请专家组出具权威裁决以实现 Wave 12.1 的 100% 最终封板。

---

## 二、 现场法医级物理报错物证

在 Windows 宿主机执行 `cmake --build --preset windows-msvc-ninja` 时，MSVC `/W4 /WX` 拦截日志如下（Exit Code = 2）：

### 2.1 报错一：NVIDIA 官方 SDK 头文件告警外溢（`C4100`）

```
FAILED: CMakeFiles/rtx_ngx_bridge.dir/src/shim/ngx_bridge/ngx_d3d12_bridge.cpp.obj 
C:\PROGRA~1\MICROS~4\18\COMMUN~1\VC\Tools\MSVC\1451~1.362\bin\Hostx64\x64\cl.exe  ... /std:c++latest /W4 /WX ... -c "src\shim\ngx_bridge\ngx_d3d12_bridge.cpp"
[REDACTED_PATH]\.vs\build\external\rtx_video_sdk\include\nvsdk_ngx_helpers_truehdr.h(73): error C2220: the following warning is treated as an error
[REDACTED_PATH]\.vs\build\external\rtx_video_sdk\include\nvsdk_ngx_helpers_truehdr.h(73): warning C4100: 'pTrueHDRCreateParams': unreferenced parameter
[REDACTED_PATH]\.vs\build\external\rtx_video_sdk\include\nvsdk_ngx_helpers_truehdr.h(84): warning C4100: 'pTrueHDRCreateParams': unreferenced parameter
[REDACTED_PATH]\.vs\build\external\rtx_video_sdk\include\nvsdk_ngx_helpers_vsr.h(35): warning C4100: 'pInVSRCreateParams': unreferenced parameter
[REDACTED_PATH]\.vs\build\external\rtx_video_sdk\include\nvsdk_ngx_helpers_vsr.h(77): warning C4100: 'pInVSRCreateParams': unreferenced parameter
```

- **根因分析**：`CMakeLists.txt` 第 506 行引入 RTX Video SDK 时写为：
  ```cmake
  target_include_directories(rtx_ngx_bridge PRIVATE "${RTX_NGX_INCLUDE_DIR}")
  ```
  缺少 `SYSTEM` 属性，导致 MSVC 编译器未将其识别为外部库（缺少 `-external:I` / `-external:W0`），使 NVIDIA 官方内联函数中的未引用形参击穿了项目的 `/WX` 门禁。相比之下，第 412 行的 CUDA 与 TensorRT 头文件均正确标注了 `SYSTEM PRIVATE`。

---

### 2.2 报错二：`ngx_remaster.cpp` 残留隐式 `std::getenv` 触发 `C4996`

```
FAILED: CMakeFiles/rtx_core.dir/src/core/ngx_remaster.cpp.obj 
C:\PROGRA~1\MICROS~4\18\COMMUN~1\VC\Tools\MSVC\1451~1.362\bin\Hostx64\x64\cl.exe  ... /std:c++latest /W4 /WX ... -c "src\core\ngx_remaster.cpp"
[REDACTED_PATH]\src\core\ngx_remaster.cpp(316): error C2220: the following warning is treated as an error
[REDACTED_PATH]\src\core\ngx_remaster.cpp(316): warning C4996: 'getenv': This function or variable may be unsafe. Consider using _dupenv_s instead. To disable deprecation, use _CRT_SECURE_NO_WARNINGS. See online help for details.
```

- **源码落点**：`src/core/ngx_remaster.cpp` 第 316 行：
  ```cpp
  if (const char* env_path = std::getenv("RTX_NGX_BRIDGE_PATH"); env_path != nullptr && env_path[0] != '\0') {
      candidates.emplace_back(env_path, true);
  }
  ```
- **根因分析**：执行工程师在进行 `HYG-P12-02`（清零隐式开关）时，仅重构了 `rife_engine.cpp`，遗漏了核心模块 `ngx_remaster.cpp` 中的 `RTX_NGX_BRIDGE_PATH` 环境变量探测，触发 MSVC `/WX` 安全警告拦截，同时违背了 02 号裁决书关于“零隐藏开关、零 `getenv`”的硬红线。

---

## 三、 提请首席领域专家组裁决的核心议题

### 3.1 咨询议题一：第三方专有 SDK 头文件的 CMake 隔离规范

- **求解诉求**：
  * 是否核准将 `CMakeLists.txt` 第 506 行调整为：
    ```cmake
    target_include_directories(rtx_ngx_bridge SYSTEM PRIVATE "${RTX_NGX_INCLUDE_DIR}")
    ```
    以确保所有第三方 Vendor SDK（CUDA、TensorRT、NVCODEC、RTX Video SDK）100% 纳入 `-external:W0` 警告豁免体系，彻底阻断外部不可控代码对项目全仓 `/WX` 门禁的侵扰？
  * 是否需要在 `CMakeLists.txt` 中对第三方导入目录的 `SYSTEM` 属性设立通用规范？

---

### 3.2 咨询议题二：`RTX_NGX_BRIDGE_PATH` 环境变量旁路的物理拔除路径

- **求解诉求**：
  * 在当前 Phase 12 架构下，`rtx_ngx_bridge.dll` 的加载应完全收敛于标准部署契约（可执行文件同级目录及 `plugins/nvidia_rtx/` 相对路径）。是否直接**物理拔除**第 316-318 行的 `std::getenv("RTX_NGX_BRIDGE_PATH")` 分支？
  * 针对存量测试代码（如 `tests/test_rife.cpp`）及 `d3d12_interop.cpp` 中偶发的环境变量读取，专家组是否下达在 Wave 12.1 阶段一并清除或在后续 Wave 统一收敛的指令？

---

## 四、 协调负责人后续推进承诺

1. **守门待命**：在专家组对上述两项微调下达最终裁决前，保持工作区锁定，严禁私自放行；
2. **终验就绪**：专家组裁决下达并合入上述两点修复后，协调负责人将立即在宿主机执行 MSVC `/WX` 全量编译与 146/146 单测，并生成物理通过日志；
3. **签发 Wave 12.1 封板**：经专家组终审确认后，正式解除 Wave 12.2 派发锁定。

**提请首席领域专家组审理并下达最后收网裁决！**
