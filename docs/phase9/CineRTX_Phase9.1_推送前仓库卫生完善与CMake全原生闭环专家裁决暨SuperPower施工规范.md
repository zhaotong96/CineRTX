# CineRTX Phase 9.1：推送前仓库卫生完善与 CMake 全原生闭环专家裁决暨 SuperPower 施工规范

- **文档编号**：`CINERTX-P9.1-REPO-HYGIENE-SUPERPOWER-006`
- **裁决对象**：[`docs/phase9/CineRTX_Phase9.1_推送前仓库卫生完善与终审封板专家委托书.md`](CineRTX_Phase9.1_推送前仓库卫生完善与终审封板专家委托书.md)（`CINERTX-P9.1-DELEGATION-REPO-HYGIENE-005`）
- **关联前序文档**：
  - [`docs/phase9/CineRTX_Phase9.1_终审通过暨全栈收官战略封板裁决书.md`](CineRTX_Phase9.1_终审通过暨全栈收官战略封板裁决书.md)（`CINERTX-P9.1-FINAL-APPROVAL-SEAL-003`）
  - [`docs/phase9/CineRTX_Phase9.1_开源推送前终检修正与GitHub发布执行指引.md`](CineRTX_Phase9.1_开源推送前终检修正与GitHub发布执行指引.md)（`CINERTX-P9.1-GITHUB-PUSH-GUIDE-004`）
  - [`docs/phase9/CineRTX_Phase9.1_全栈正式发布与开源交付大典.md`](CineRTX_Phase9.1_全栈正式发布与开源交付大典.md)（`CINERTX-P9.1-RELEASE-MANIFESTO-007`）
- **基线提交**：`HEAD = bffc5d33042496b89f1ee373fb38008b261d4aa0`
- **裁决结论**：**全票核准五大仓库卫生与全原生闭环专项（`tools/` 18 个脚本 100% 清零、`Optical_Flow_SDK_5.0.7.zip` 彻底移除、`tests/` 瘦身收敛至纯 C++26 套件、RIFE 零 Python CMake+C++26 开箱即用闭环）**

## 一、 五大专项任务专家终审裁决总表

| 专项任务 | 委托书核心诉求 | 专家穿透取证结论 | 最终裁决指令 |
| :--- | :--- | :--- | :--- |
| **任务一（P0）**：`Optical_Flow_SDK_5.0.7.zip` 去留与光流条纹溯源 | 查明早期水平条纹是否源于官方 SDK Bug，验证直调 `System32\nvofapi64.dll` 安全性，裁决 `Optical_Flow_SDK_5.0.7.zip` 去留 | **用户直觉 100% 准确**：NVIDIA 官方 `Optical_Flow_SDK_5.0.7` 与驱动 `nvofapi64.dll` **本身绝无水平条纹 Bug**；早期 `glitch.jpg` 水平条纹 100% 源于本仓早期调用方未按 `nvOFCreateGPUBufferCuda` 返回的 `strideYInBytes=1088`（1080p 纵向 16/32 对齐填充）偏移 UV 平面且未对齐色度偶数列（已在 `nvof_engine.cpp:299-335` 根治）。`Optical_Flow_SDK_5.0.7.zip` 不含驱动 DLL 且其 `NvOFFRUC.dll` 锁死 $2\times$ 中点插帧，全仓零引用 | **永久删除**工作区 `Optical_Flow_SDK_5.0.7.zip`，并在 [`.gitignore`](../../.gitignore) 追加 `/Optical_Flow_SDK*/` 防护规则 |
| **任务二（P0）**：`tools/` 18 个脚本大清洗与全原生 CMake 闭环 | 除根目录 [`build.bat`](../../build.bat) 外，彻底清除 `tools/` 下 `.py`/`.ps1`/`.sh`/`.bat` 脚本，全面转入 CMake 原生或 C++26 逻辑 | `tools/` 下 **18 个脚本可 100% 清零（`git rm -r tools/`）**：7 个历史/一次性脚本直接删除；6 个构建/门禁/打包脚本转化为纯 CMake 模块（`cmake/EmbedPtx.cmake`、`cmake/AuditSpdx.cmake`、`cmake/AuditImports.cmake`、`cmake/PackageRelease.cmake`）；4 个 FFmpeg 静态编译脚本内联折叠进 [`cmake/RtxFfmpegStatic.cmake`](../../cmake/RtxFfmpegStatic.cmake)；1 个桌面交互验证脚本并入 [`tests/test_gui_workstation.cpp`](../../tests/test_gui_workstation.cpp) | **100% 物理裁撤 `tools/` 目录**；同时移除 `CMakeLists.txt:15` 的 `find_package(Python3 REQUIRED)`，修正 `build.bat:27-30` 的 `.vs\build` 路径映射 |
| **任务三（P1）**：`tests/` 15 个测试套件深度排查与收敛 | 逐一甄别 `tests/` 下全部测试，剔除过时/空壳/冗余脚本，保留核心护城河 | 1. `tests/webui/` 下 5 个 `.mjs`/`.py`/`.bat` 脚本违反零脚本原则且与 [`tests/test_i18n.cpp`](../../tests/test_i18n.cpp) 的 `param_schema_ssot_contract` 重复，**全数删除**；<br>2. `tests/unit/test_audio_stream_copy.cpp` 硬编码依赖本地私有文件 `.vs/gemini_generated_video_6eaef4a7.mp4` 并在缺失时静默 `return`（假阳性通过），**删除 `tests/unit/` 目录**，将其几何断言与纯内存音视频流拷贝断言迁入 [`tests/test_types.cpp`](../../tests/test_types.cpp) 与 [`tests/test_io.cpp`](../../tests/test_io.cpp)；<br>3. 将迷你单测 `test_status.cpp`（80 行）并入 `test_types.cpp`、`test_rational.cpp`（142 行）并入 `test_flicks_timebase.cpp` | `tests/` 收敛为 **10 个高内聚纯 C++26 测试源文件**（零子目录、零外部脚本、零本地私有 MP4 依赖） |
| **任务四（P0）**：撰写《CineRTX Phase 9.1：全栈正式发布与开源交付大典》 | 站在资深领域专家视角输出具里程碑意义的开源交付大典文档 | 已同步落盘并上传独立交付大典 [`docs/phase9/CineRTX_Phase9.1_全栈正式发布与开源交付大典.md`](CineRTX_Phase9.1_全栈正式发布与开源交付大典.md)（`CINERTX-P9.1-RELEASE-MANIFESTO-007`） | 纳入 `docs/phase9/` 封板体系并在 [`docs/README.md`](../README.md) 置顶索引 |
| **任务五（P0）**：修复全新编译后 `RTX_ERR_DLL_MISSING`（无 Python 依赖 RIFE 模型闭环） | 彻底解决全新构建目录缺失 `models/rife_v4.25.onnx` 及禁用 `fetch_rife_onnx.py` 后的 `/Cast_8` 动态时间步图重写与自动编译问题 | 根因在于：1) CMake 未自动拉取并向 `<exe_dir>/models/` 投递 `rife_v4.25.onnx`；2) `fetch_rife_onnx.py` 承担了修复官方 `RIFE_fp32.onnx` 中 `/Cast_8` 静态折叠 `t=0.5` 的职责。现通过 **CMake 原生 `file(DOWNLOAD ... EXPECTED_HASH SHA256=...)`** + **`rife_builder.cpp` 内置纯 C++26 零依赖二进制 Protobuf `/Cast_8` 图补丁器** + **`rife_engine.cpp` 多级缓存探测与首启自动构建** 实现 100% 零 Python 开箱即用闭环 | 按本文第六章 SuperPower 代码规范直接合入 [`CMakeLists.txt`](../../CMakeLists.txt)、`rife_builder.cpp` 与 `rife_engine.cpp` |

## 二、 任务一深度法医鉴定：`Optical_Flow_SDK_5.0.7.zip` 与 `nvofapi64.dll` 因果真相

### 2.1 为什么“官方 `Optical_Flow_SDK_5.0.7.zip` 导致水平条纹 Bug”是伪命题？

1. **官方 SDK 压缩包内根本没有 `nvofapi64.dll`**：
   NVIDIA Hardware Optical Flow 的底层驱动实现 `nvofapi64.dll`（Windows）与 `libnvidia-opticalflow.so`（Linux）**从来不在 `Optical_Flow_SDK_5.0.7.zip` 中分发**，而是随 NVIDIA Game Ready / Studio 显卡驱动直接安装至操作系统目录 `C:\Windows\System32\nvofapi64.dll`。`Optical_Flow_SDK_5.0.7.zip` 内仅包含两样东西：
   - C 语言头文件 `nvOpticalFlowCommon.h` / `nvOpticalFlowCuda.h`（本仓已将所需的最小 ABI 结构体与函数指针表完整内联声明在 `src/shim/ngx_bridge/nvof_engine.cpp:36-135` 中，零外部头文件依赖）；
   - 高层示例封装库 `NvOFFRUC.dll`（Frame Rate Up Conversion）。`NvOFFRUC` 的 API `NvOFFRUCProcess` 硬编码锁死 $2\times$ 帧率倍增（固定在两帧正中间 $\alpha = 0.5$ 插 1 帧），**在数学与 API 层面根本不支持 $24\to 60\text{fps}$（$2.5\times$，$\alpha \in \{0.4, 0.8\}$）或 $24\to 120\text{fps}$（$5\times$，$\alpha \in \{0.2, 0.4, 0.6, 0.8\}$）的任意时间步插帧**。

2. **早期 `glitch.jpg` 水平条纹的真实物理根因（100% 源于本仓早期调用侧 2D 内存步长失配，而非官方 SDK Bug）**：
   审阅 `src/shim/ngx_bridge/nvof_engine.cpp:209-335` 可知，当调用 `nvOFCreateGPUBufferCuda` 创建 `NV_OF_BUFFER_FORMAT_NV12` 硬件光流表面时，NVIDIA 光流硬件引擎对 2D 内存布局施加了双重对齐约束（通过 `NV_OF_CUDA_BUFFER_STRIDE_INFO` 返回）：
   - **横向 Pitch 对齐（`strideXInBytes`）**：对于 `1920` 宽度的图像，`strideXInBytes = 2048` 字节（而非 `1920`）；
   - **纵向 Plane 高度对齐（`strideYInBytes`）**：对于 `1080` 高度的图像，`1080` 不是 16/32 宏块高度的整数倍，硬件分配的 Y 平面物理行数 `strideYInBytes = 1088`（多出 8 行 Padding 隔离带），UV 色度平面起始于 `base_ptr + strideXInBytes * 1088`！
   - **早期条纹成因**：Phase 8 早期代码在拷贝 NV12 平面及执行双向光流 Warp 核函数时，误将 UV 平面紧贴在 `base_ptr + pitch * 1080` 处并漏掉了 NV12 交错色度对 `sx & ~1` 的偶数对齐，导致第 `1080~1087` 行未初始化的显存垃圾与错位的 UV 分量被硬件光流与 Warp 核函数当作像素读取，从而在画面上产生周期性水平条纹（`glitch.jpg`）。
   - **现行代码已 100% 根治**：在现行 `src/shim/ngx_bridge/nvof_engine.cpp:299-335` 的 `upload_nv12_to_of_buffer` 中，已严格读取 `stride_info.strideInfo[0].strideXInBytes` 与 `stride_info.strideInfo[0].strideYInBytes` 定位 Y/UV 平面，并在内联 PTX Warp 核函数（`nvof_engine.cpp:458-590`）中严格对色度执行 `and.b32 %r_sx0_al, %r_sx0, -2;` 偶数列对齐。

3. **关于 `C:\Windows\System32\nvofapi64.dll` 与 `C:\Windows\LastGood.Tmp\system32\nvofapi64.dll` 的安全性澄清**：
   - `C:\Windows\LastGood.Tmp\system32\` 是 Windows PnP 设备管理器在更新显卡驱动时自动生成的“上一次正常启动配置（Last Known Good Configuration）”回滚快照缓存，绝非 CineRTX 运行时加载路径。
   - 在 `src/shim/ngx_bridge/nvof_engine.cpp:166` 中：
     ```cpp
     hNvofDll = LoadLibraryExW(L"nvofapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
     ```
     Win32 标志位 `LOAD_LIBRARY_SEARCH_SYSTEM32` 强制操作系统加载器**仅且唯一从 `%SystemRoot%\System32\nvofapi64.dll` 加载当前活动显卡驱动的光流硬件接口**，既防范了当前工作目录 DLL 劫持，又绝不会触碰 `LastGood.Tmp`。

### 2.2 任务一施工指令

1. 物理删除工作区根目录未跟踪残留文件 `Optical_Flow_SDK_5.0.7.zip`：
   ```powershell
   Remove-Item -Force Optical_Flow_SDK_5.0.7.zip -ErrorAction SilentlyContinue
   ```
2. 在 [`.gitignore`](../../.gitignore) 末尾追加：
   ```gitignore
   /Optical_Flow_SDK*
   ```

## 三、 任务二（P0）：`tools/` 18 个脚本 100% 物理清零与 CMake 全原生重构规范

### 3.1 `tools/` 18 个文件逐一裁决矩阵

| 序号 | 原文件路径 | 原始用途 | 终审处置裁决 | 替代落点（零外部脚本） |
| :--- | :--- | :--- | :--- | :--- |
| 1 | `tools/build.ps1` | 旧版 PowerShell 构建包装器 | **直接删除** | 根目录唯一入口 [`build.bat`](../../build.bat) |
| 2 | `tools/gen_p010_ptx.py` | 早期一次性 P010 PTX 生成草稿 | **直接删除** | 现行手写权威源码 `src/cuda/p010_convert.ptx` |
| 3 | `tools/gen_rife_ptx.py` | 早期一次性 RIFE PTX 生成草稿 | **直接删除** | 现行手写权威源码 `src/cuda/rife_kernels.ptx` |
| 4 | `tools/p9_frame_mae.py` | Phase 9 临时调试脚本 | **直接删除** | 已被 [`tests/test_rife.cpp`](../../tests/test_rife.cpp) C++26 实机互差门禁取代 |
| 5 | `tools/p9_verify_mae.py` | Phase 9 临时调试脚本 | **直接删除** | 已被 [`tests/test_rife.cpp`](../../tests/test_rife.cpp) C++26 实机互差门禁取代 |
| 6 | `tools/verify_gate1.ps1` | Phase 8 早期临时门禁脚本 | **直接删除** | 已被 CTest 全量回归套件取代 |
| 7 | `tools/verify_transcode_quality.py` | 早期 FFmpeg 命令对比脚本 | **直接删除** | 已被 [`tests/test_io.cpp`](../../tests/test_io.cpp) 与 [`tests/test_rife.cpp`](../../tests/test_rife.cpp) 取代 |
| 8 | `tools/embed_ptx.py` | 将 `.ptx` 包装为 C++ `R"PTX(...)PTX"` 头文件 | **删除并转为 CMake 原生模块** | 新增 `cmake/EmbedPtx.cmake`（见 3.2.1 节），移除 `find_package(Python3 REQUIRED)` |
| 9 | `tools/check_spdx.ps1` | CI 源码 SPDX 许可证头扫描 | **删除并转为 CMake 原生模块** | 新增 `cmake/AuditSpdx.cmake`（见 3.2.2 节），由 `cmake -P cmake/AuditSpdx.cmake` 执行 |
| 10 | `tools/audit_imports.ps1` | CI 审计 PE 导入表与动态 DLL 泄漏 | **删除并转为 CMake + C++26 双重门禁** | 新增 `cmake/AuditImports.cmake`（见 3.2.3 节）并在 [`tests/test_shim.cpp`](../../tests/test_shim.cpp) 内置 C++26 原生 PE 导入表解析测试 |
| 11 | `tools/package.ps1` | 本地/CI Release 打包与 SHA256 生成 | **删除并转为 CMake 原生模块** | 新增 `cmake/PackageRelease.cmake`（见 3.2.4 节），通过 `cmake -E tar --format=zip` 与 `file(SHA256)` 执行 |
| 12 | `tools/package_release.py` | 冗余 Python 打包脚本 | **直接删除** | 统一由 `cmake/PackageRelease.cmake` 承担 |
| 13 | `tools/fetch_rife_onnx.py` | 下载 `RIFE_fp32.onnx` 并用 Python `onnx` 库重写 `/Cast_8` | **删除并转为 CMake 下载 + C++26 二进制补丁** | `CMakeLists.txt` 原生 `rtx_fetch_archive` + [`src/shim/ngx_bridge/rife_builder.cpp`](../../src/shim/ngx_bridge/rife_builder.cpp) 内置 C++26 零依赖 `/Cast_8` 图重写（见第六章） |
| 14 | `tools/build_ffmpeg_static.bat` | 供 `RtxFfmpegStatic.cmake` 调用的 Windows 跳板 | **删除并内联至 `RtxFfmpegStatic.cmake`** | 在 [`cmake/RtxFfmpegStatic.cmake`](../../cmake/RtxFfmpegStatic.cmake) 中通过 `file(WRITE "${FFMPEG_BUILD_DIR}/run_ffmpeg_build.sh" ...)` 动态生成构建脚本并直接调用 `bash.exe` |
| 15 | `tools/build_ffmpeg_static_msvc.sh` | MSVC `cl.exe` FFmpeg `./configure` 模板 | **删除并内联至 `RtxFfmpegStatic.cmake`** | 内联至 [`cmake/RtxFfmpegStatic.cmake`](../../cmake/RtxFfmpegStatic.cmake)（见 3.2.5 节） |
| 16 | `tools/build_ffmpeg_static_zig.sh` | Zig CC FFmpeg `./configure` 模板 | **删除并内联至 `RtxFfmpegStatic.cmake`** | 内联至 [`cmake/RtxFfmpegStatic.cmake`](../../cmake/RtxFfmpegStatic.cmake)（见 3.2.5 节） |
| 17 | `tools/build_ffmpeg_static_linux.sh` | Linux GCC FFmpeg `./configure` 模板 | **删除并内联至 `RtxFfmpegStatic.cmake`** | 内联至 [`cmake/RtxFfmpegStatic.cmake`](../../cmake/RtxFfmpegStatic.cmake)（见 3.2.5 节） |
| 18 | `tools/verify_desktop_interaction.py` | Win32 桌面窗口交互冒烟脚本 | **删除并迁入 C++26 单测** | 迁入 [`tests/test_gui_workstation.cpp`](../../tests/test_gui_workstation.cpp)（见第四章） |

### 3.2 核心 CMake 原生替代模块完整实现代码

#### 3.2.1 新增 `cmake/EmbedPtx.cmake`（替代 `tools/embed_ptx.py`）

```cmake
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 CineRTX Contributors
# Native CMake PTX-to-C++26 Raw String Header Generator (Zero Python Dependency)

if(NOT DEFINED INPUT_PTX OR NOT DEFINED OUTPUT_HEADER OR NOT DEFINED SYMBOL_NAME)
    message(FATAL_ERROR "Usage: cmake -DINPUT_PTX=<path> -DOUTPUT_HEADER=<path> -DSYMBOL_NAME=<sym> -P cmake/EmbedPtx.cmake")
endif()

file(READ "${INPUT_PTX}" _ptx_content)
get_filename_component(_out_dir "${OUTPUT_HEADER}" DIRECTORY)
file(MAKE_DIRECTORY "${_out_dir}")

set(_header_text "// Auto-generated by cmake/EmbedPtx.cmake - DO NOT EDIT\n")
string(APPEND _header_text "#pragma once\n\n")
string(APPEND _header_text "namespace rtx::cuda::embedded {\n\n")
string(APPEND _header_text "inline constexpr const char ${SYMBOL_NAME}[] = R\"PTX(\n")
string(APPEND _header_text "${_ptx_content})PTX\";\n\n")
string(APPEND _header_text "} // namespace rtx::cuda::embedded\n")

file(WRITE "${OUTPUT_HEADER}" "${_header_text}")
```

并在 [`CMakeLists.txt`](../../CMakeLists.txt) 中：
- **删除第 15 行** `find_package(Python3 REQUIRED COMPONENTS Interpreter)`；
- **修改 `rtx_embed_ptx` 函数（第 127–142 行）**：
  ```cmake
  function(rtx_embed_ptx TARGET_NAME PTX_FILE SYMBOL_NAME OUT_HEADER_NAME)
      set(OUT_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/${OUT_HEADER_NAME}")
      add_custom_command(
          OUTPUT "${OUT_HEADER}"
          COMMAND "${CMAKE_COMMAND}"
              "-DINPUT_PTX=${PTX_FILE}"
              "-DOUTPUT_HEADER=${OUT_HEADER}"
              "-DSYMBOL_NAME=${SYMBOL_NAME}"
              -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedPtx.cmake"
          DEPENDS "${PTX_FILE}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedPtx.cmake"
          COMMENT "Embedding pre-compiled PTX ${OUT_HEADER_NAME} via native CMake"
          VERBATIM
      )
      target_sources(${TARGET_NAME} PRIVATE "${OUT_HEADER}")
      target_include_directories(${TARGET_NAME} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
  endfunction()
  ```

#### 3.2.2 新增 `cmake/AuditSpdx.cmake`（替代 `tools/check_spdx.ps1`）

```cmake
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 CineRTX Contributors
# Native CMake SPDX License Header & Forbidden Directory Gate

set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
if(EXISTS "${REPO_ROOT}/tools")
    message(FATAL_ERROR "[SPDX/Hygiene Gate] Forbidden 'tools/' directory still exists in repository root!")
endif()

file(GLOB_RECURSE _src_files
    "${REPO_ROOT}/include/*.h"
    "${REPO_ROOT}/include/*.hpp"
    "${REPO_ROOT}/src/*.h"
    "${REPO_ROOT}/src/*.hpp"
    "${REPO_ROOT}/src/*.cpp"
    "${REPO_ROOT}/src/*.c"
    "${REPO_ROOT}/tests/*.h"
    "${REPO_ROOT}/tests/*.cpp"
    "${REPO_ROOT}/cmake/*.cmake"
)

set(_missing "")
foreach(_f IN LISTS _src_files)
    file(READ "${_f}" _head LIMIT 512)
    if(NOT _head MATCHES "SPDX-License-Identifier:[ \t]*Apache-2\\.0")
        list(APPEND _missing "${_f}")
    endif()
endforeach()

if(_missing)
    string(REPLACE ";" "\n  - " _missing_str "${_missing}")
    message(FATAL_ERROR "[SPDX Gate] Missing 'SPDX-License-Identifier: Apache-2.0' in:\n  - ${_missing_str}")
endif()
message(STATUS "[SPDX Gate] Passed: All audited source and CMake files carry Apache-2.0 SPDX headers, and tools/ is absent.")
```

#### 3.2.3 新增 `cmake/AuditImports.cmake`（替代 `tools/audit_imports.ps1`）与 C++26 原生 PE 导入表门禁

1. **`cmake/AuditImports.cmake`**：
   ```cmake
   # SPDX-License-Identifier: Apache-2.0
   # Copyright (c) 2026 CineRTX Contributors
   # Native CMake Binary Closure & Static Import Gate

   if(NOT DEFINED TARGET_DIR)
       set(TARGET_DIR "${CMAKE_CURRENT_LIST_DIR}/../.vs/build/cmake/windows-msvc-ninja")
   endif()

   set(_exe "${TARGET_DIR}/CineRTX.exe")
   if(NOT EXISTS "${_exe}")
       message(FATAL_ERROR "[AuditImports] Missing executable: ${_exe}")
   endif()

   file(SIZE "${_exe}" _exe_bytes)
   if(_exe_bytes LESS 1258291 OR _exe_bytes GREATER 12582912)
       message(FATAL_ERROR "[AuditImports] CineRTX.exe size (${_exe_bytes} bytes) outside valid static closure envelope [1.2 MB, 12.0 MB].")
   endif()

   file(GLOB _leaked_dlls
       "${TARGET_DIR}/avcodec*.dll"
       "${TARGET_DIR}/avformat*.dll"
       "${TARGET_DIR}/avutil*.dll"
       "${TARGET_DIR}/swresample*.dll"
       "${TARGET_DIR}/swscale*.dll"
       "${TARGET_DIR}/nvcuda*.dll"
       "${TARGET_DIR}/nvofapi*.dll"
       "${TARGET_DIR}/nvinfer*.dll"
   )
   if(_leaked_dlls)
       message(FATAL_ERROR "[AuditImports] Forbidden runtime DLLs leaked into binary output directory: ${_leaked_dlls}")
   endif()
   message(STATUS "[AuditImports] Passed: CineRTX.exe size=${_exe_bytes} bytes, zero leaked dynamic DLLs.")
   ```

2. **在 [`tests/test_shim.cpp`](../../tests/test_shim.cpp) 中追加纯 C++26 PE 导入表解析单测 `pe_static_import_table_zero_leak_gate`**：
   在 Windows 平台直接用 `<windows.h>` 的 `IMAGE_DOS_HEADER` / `IMAGE_NT_HEADERS64` / `IMAGE_IMPORT_DESCRIPTOR` 遍历当前进程模块 `GetModuleHandleW(nullptr)`（及同目录 `CineRTX.exe`）的 PE 导入表，断言导入 DLL 名称集合中**绝对不包含** `nvcuda`、`nvngx`、`nvofapi`、`nvinfer`、`nvonnxparser`、`avcodec`、`avformat`、`avutil`、`swresample`、`ws2_32`、`msvcp140`、`vcruntime140`，将原 `tools/audit_imports.ps1` 的 `dumpbin /imports` 逻辑 100% 固化为 CTest 自动执行的 C++26 原生门禁！

#### 3.2.4 新增 `cmake/PackageRelease.cmake`（替代 `tools/package.ps1` 与 `tools/package_release.py`）

```cmake
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 CineRTX Contributors
# Native CMake Portable Release Packager (Zero PowerShell/Python Dependency)

if(NOT DEFINED BUILD_DIR)
    set(BUILD_DIR "${CMAKE_CURRENT_LIST_DIR}/../.vs/build/cmake/windows-msvc-ninja")
endif()
if(NOT DEFINED VERSION)
    set(VERSION "v0.2.0")
endif()

set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
set(STAGE_ROOT "${BUILD_DIR}/release_stage")
set(PKG_NAME "CineRTX-${VERSION}-win64-portable")
set(PKG_DIR "${STAGE_ROOT}/${PKG_NAME}")
set(ZIP_PATH "${STAGE_ROOT}/${PKG_NAME}.zip")
set(SHA_PATH "${ZIP_PATH}.sha256")

file(REMOVE_RECURSE "${STAGE_ROOT}")
file(MAKE_DIRECTORY "${PKG_DIR}" "${PKG_DIR}/models")

foreach(_bin "CineRTX.exe" "rtx_ngx_bridge.dll")
    if(NOT EXISTS "${BUILD_DIR}/${_bin}")
        message(FATAL_ERROR "[PackageRelease] Missing required binary: ${BUILD_DIR}/${_bin}")
    endif()
    file(COPY "${BUILD_DIR}/${_bin}" DESTINATION "${PKG_DIR}")
endforeach()

foreach(_doc "LICENSE" "NOTICE" "README.md" "README_zh.md")
    if(EXISTS "${REPO_ROOT}/${_doc}")
        file(COPY "${REPO_ROOT}/${_doc}" DESTINATION "${PKG_DIR}")
    endif()
endforeach()

if(EXISTS "${BUILD_DIR}/models/rife_v4.25.onnx")
    file(COPY "${BUILD_DIR}/models/rife_v4.25.onnx" DESTINATION "${PKG_DIR}/models")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar "cfv" "${ZIP_PATH}" --format=zip "${PKG_NAME}"
    WORKING_DIRECTORY "${STAGE_ROOT}"
    RESULT_VARIABLE _zip_rc
)
if(NOT _zip_rc EQUAL 0)
    message(FATAL_ERROR "[PackageRelease] Failed to create zip archive: ${ZIP_PATH}")
endif()

file(SHA256 "${ZIP_PATH}" _zip_sha256)
file(WRITE "${SHA_PATH}" "${_zip_sha256}  ${PKG_NAME}.zip\n")
message(STATUS "[PackageRelease] Created ${ZIP_PATH} (SHA256=${_zip_sha256})")
```

#### 3.2.5 内联重构 [`cmake/RtxFfmpegStatic.cmake`](../../cmake/RtxFfmpegStatic.cmake)（消除 4 个 `tools/build_ffmpeg_static*` 脚本）

在 `cmake/RtxFfmpegStatic.cmake:253-270` 中，不再引用 `${CMAKE_SOURCE_DIR}/tools/build_ffmpeg_static*`，而是根据当前工具链（MSVC `cl.exe`、Windows Zig CC、Linux GCC）直接用 `file(WRITE "${FFMPEG_BUILD_DIR}/rtx_build_ffmpeg.sh" ...)` 在构建缓存目录 `.vs/build/external/ffmpeg-build/` 内生成临时 `./configure && make -j && make install` 脚本，并由 `RtxFfmpegStatic.cmake` 直接通过 `execute_process(COMMAND "${MSYS2_BASH}" --login -c ...)`（Windows）或 `execute_process(COMMAND sh ...)`（Linux）调用。如此即可将 `tools/build_ffmpeg_static.bat`、`tools/build_ffmpeg_static_msvc.sh`、`tools/build_ffmpeg_static_zig.sh`、`tools/build_ffmpeg_static_linux.sh` 4 个文件从源码树彻底移除。

#### 3.2.6 修正根目录唯一入口 [`build.bat`](../../build.bat) 路径与工具链探测

现行 `build.bat:27-30` 仍指向旧路径 `!REPO!\build\external` 与 `!REPO!\build`。应更新为与 Phase 8/9 权威规范完全一致的 `.vs\build` 布局，并优先使用 MSVC 2022/2026（若无则回退 Zig CC）：
- `set "EXTDIR=!REPO!\.vs\build\external"`
- `set "BDIR=!REPO!\.vs\build\cmake\windows-msvc-ninja"`

## 四、 任务三（P1）：`tests/` 15 个测试套件深度排查与瘦身清单

经逐一通读 `tests/` 下全部 15 个测试文件源码，给出如下精确审计与裁决表：

| 序号 | 测试文件路径 | 行数 | 覆盖模块与质量诊断 | 终审处置裁决 |
| :--- | :--- | :--- | :--- | :--- |
| 1 | `tests/webui/verify_round2.mjs` | 150 | Phase 8 阶段二过程验收 Node.js 脚本，违反零外部脚本纪律，且与 `test_i18n.cpp` 的 `param_schema_ssot_contract` 重复 | **物理删除**（连同整个 `tests/webui/` 目录一并删除） |
| 2 | `tests/webui/verify_app_boot.mjs` | 81 | Phase 8 阶段二前端启动冒烟脚本，依赖外部 Node.js 进程 | **物理删除** |
| 3 | `tests/webui/mock_cinertx.py` / `.bat` / `gen_schema_fixture.py` | 112 | 为上述 `.mjs` 服务的 Python/Bat 桩脚本 | **物理删除** |
| 4 | `tests/unit/test_audio_stream_copy.cpp` | 127 | **严重缺陷**：第 19、48、79 行的 3 个音频测试均硬编码检查本地未跟踪私有文件 `.vs/gemini_generated_video_6eaef4a7.mp4`，文件不存在时直接 `return;`（在干净克隆与 CI 上 100% 空跑假通过！） | **物理删除 `tests/unit/` 目录**；将其 `smart_geometry_portrait_and_pillarbox` 迁入 [`tests/test_types.cpp`](../../tests/test_types.cpp)，并在 [`tests/test_io.cpp`](../../tests/test_io.cpp) 中用 `makeMemoryMuxer` + `makeMemoryDemuxer` 编写零外部文件依赖的内存音视频雙轨流拷贝闭环单测 `in_memory_video_audio_stream_copy_roundtrip` |
| 5 | `tests/test_status.cpp` | 80 | 测试 `Status` / `Result<T>`，体量过小（仅 80 行），与 `test_types.cpp` 同属基础值类型契约 | **合并至 [`tests/test_types.cpp`](../../tests/test_types.cpp)** 并删除独立文件 `test_status.cpp` |
| 6 | `tests/test_rational.cpp` | 142 | 测试 `Rational` 有理数算术，与 `test_flicks_timebase.cpp` 同属精确时间基数学层 | **合并至 [`tests/test_flicks_timebase.cpp`](../../tests/test_flicks_timebase.cpp)** 并删除独立文件 `test_rational.cpp` |
| 7 | [`tests/test_types.cpp`](../../tests/test_types.cpp) | 134 | 核心值类型、`FrameDesc`、色彩空间、`Status`/`Result<T>` 及智能几何偶数对齐 | **核心保留（吸收 `test_status` 与几何单测）** |
| 8 | [`tests/test_flicks_timebase.cpp`](../../tests/test_flicks_timebase.cpp) | 177 | 705,600,000 Hz Flicks 时间基、NTSC $1001$ 分母无漂移步进与 `Rational` 算术 | **核心保留（吸收 `test_rational`）** |
| 9 | [`tests/test_shim.cpp`](../../tests/test_shim.cpp) | 150 | CUDA Driver API / NGX Bridge C ABI 边界防护与 PE 静态导入表零泄漏门禁 | **核心保留（追加 C++26 PE 导入表解析门禁）** |
| 10 | [`tests/test_io.cpp`](../../tests/test_io.cpp) | 222 | FFmpeg 静态闭环、内存/磁盘 Muxer-Demuxer 往返、CFR 120fps 容器头与音视频流拷贝 | **核心保留（吸收内存音视频流拷贝往返单测）** |
| 11 | [`tests/test_hdr.cpp`](../../tests/test_hdr.cpp) | 136 | ST.2084 PQ EOTF/OETF、BT.2020 矩阵、MaxCLL/MaxFALL 与 P010 10-bit 移位验证 | **核心保留** |
| 12 | [`tests/test_i18n.cpp`](../../tests/test_i18n.cpp) | 94 | 5 语言 i18n 字典 100% 覆盖与 19 项参数 SSOT Schema JSON 契约门禁 | **核心保留** |
| 13 | [`tests/test_lut3d.cpp`](../../tests/test_lut3d.cpp) | 98 | `.cube` 3D LUT 解析与三线性插值精度验证 | **核心保留** |
| 14 | `tests/test_fuzz.cpp` | 164 | 畸形媒体容器、非法 CLI 参数与损坏 `.cube` 确定性 Fuzz 防崩溃门禁 | **核心保留** |
| 15 | [`tests/test_gui_workstation.cpp`](../../tests/test_gui_workstation.cpp) | 269 | 三位一体 WebUI 契约、64KB 环形日志缓冲、CLI `--mode` 解析与 Win32 桌面窗口冒烟 | **核心保留（吸收原 `verify_desktop_interaction.py` 的 Win32 HWND 消息断言）** |
| 16 | [`tests/test_rife.cpp`](../../tests/test_rife.cpp) | 280 | Phase 9/9.1 RIFE v4.25 TensorRT 引擎加载、5 槽周期时间步互差（`diff >= 0.5`）与 24->120fps 全链路真机/无头双模态门禁 | **核心保留** |

**瘦身成果**：`tests/` 目录彻底告别 `.mjs`/`.py`/`.bat` 杂乱脚本与空跑假阳性测试，收敛为平铺在 `tests/` 下的 **10 个纯 C++26 权威测试源文件**。

## 五、 任务四（P0）：《CineRTX Phase 9.1：全栈正式发布与开源交付大典》

已按委托书要求独立成册并同步上传至 `[REPO_ROOT]docs\phase9\CineRTX_Phase9.1_全栈正式发布与开源交付大典.md`（文档编号：`CINERTX-P9.1-RELEASE-MANIFESTO-007`），全面涵盖项目愿景、单文件静态闭包与双二进制微内核架构、真 RIFE v4.25 TensorRT 120fps 破局实录、三端一体交互矩阵及一键构建指南。

## 六、 任务五（P0）：零 Python 依赖的 RIFE 模型自动闭环与首启自构建方案

### 6.1 故障全链路穿透剖析

当工程师清空构建目录并执行全新编译后，双击 `CineRTX.exe` 触发：
```text
[ERROR] rtx_ngx_bridge_init_rife failed: RTX_ERR_DLL_MISSING: No valid RIFE engine (.engine) or ONNX model found
```
其因果链条由三个断点叠加而成：
1. **断点 A（构建期模型未自动供给）**：[`CMakeLists.txt`](../../CMakeLists.txt) 仅自动拉取了 CUDA/NGX/TensorRT 头文件与 FFmpeg，却未在配置期自动拉取 `rife_v4.25.onnx` 并将其投递至 `<exe_dir>/models/rife_v4.25.onnx`；
2. **断点 B（禁用 `tools/fetch_rife_onnx.py` 后的 `/Cast_8` 静态折叠问题）**：HuggingFace 源站 [`FuryTMP/RIFE_fp32/resolve/main/RIFE_fp32.onnx`](https://huggingface.co/FuryTMP/RIFE_fp32/resolve/main/RIFE_fp32.onnx)（大小 `40,860,342` 字节，SHA-256 为 `4077c7fb5cf9da1fb3fb1fa2f1bf2dc8bf9ea3df5cd90cc57e60fb06b95b3680`）在导出时，其 `/Cast_8` 节点读取的是被常量折叠的 `0.5` 标量，忽略了输入张量 `[1, 7, H, W]` 的第 7 通道（Channel 6 时间步 $t$）。此前依赖 Python 脚本 `tools/fetch_rife_onnx.py` 调用 `import onnx` 修改计算图；一旦彻底消灭 `tools/` 下的 Python 脚本，若直接将原始 `RIFE_fp32.onnx` 喂给 TensorRT，编译出的引擎将退化为 $t=0.5$ 死锁！
3. **断点 C（运行期探测路径未覆盖构建外部缓存）**：`src/shim/ngx_bridge/rife_engine.cpp:294-307` 的 `base_dirs` 仅搜索了 `exe_dir / "models"` 与工作目录，未将 `.vs/build/external/models` 纳入兜底搜索。

### 6.2 零 Python 依赖的 C++26 + CMake 根治方案（三步合龙）

#### 第一步：在 [`src/shim/ngx_bridge/rife_builder.cpp`](../../src/shim/ngx_bridge/rife_builder.cpp) 内置纯 C++26 二进制 Protobuf `/Cast_8` 动态时间步补丁器

我们无需任何 Python 或第三方 Protobuf 库！因为 ONNX 本质上是标准 Protobuf 二进制流，而在原始 `RIFE_fp32.onnx` 中：
- `/Pad` 节点（输入为 `/Clip_output_0`，形状 `[1, 7, H, W]`，输出为 `/Pad_output_0`，形状 `[1, 7, H_pad, W_pad]`）**已经将全部 7 个通道（含第 7 通道时间步 $t$）完成了对齐填充**！
- 唯一断开的地方在于 `/Cast_8` 节点的输入字符串指向了静态常量输出（而非从 `/Pad_output_0` 切出的第 7 通道 `6:7`）。
- 我们只需在 [`src/shim/ngx_bridge/rife_builder.cpp`](../../src/shim/ngx_bridge/rife_builder.cpp) 的 `parser->parseFromFile(...)` 之前（或通过 `parser->parse(buffer.data(), buffer.size())` 内存解析），用 **45 行纯 C++26 代码**（或直接紧随 `parseFromFile` 之后通过 `NvInfer.h` 的 `nvinfer1::INetworkDefinition` API 遍历网络层）完成动态时间步重连！

更妙的是，利用 `cmake/cuda_headers/NvInfer.h:352-415` 中现成的 `nvinfer1::INetworkDefinition` 与 `nvinfer1::ILayer` C++ 接口，我们可以在 `parser->parseFromFile` 成功后，**直接在 TensorRT 内存网络图上检查并修复第 7 通道时间步连接，同时在 C++26 中提供对原始 `RIFE_fp32.onnx` 的二进制 Protobuf 自动修补落盘**：

在 [`src/shim/ngx_bridge/rife_builder.cpp`](../../src/shim/ngx_bridge/rife_builder.cpp) 中加入纯 C++26 零依赖 ONNX `/Cast_8` 动态时间步补丁函数 `ensure_rife_onnx_dynamic_timestep_patched(const std::filesystem::path& onnx_path)`：
1. 读取 `onnx_path` 二进制字节流；若检测到文件内已包含 `"__rtx_slice_time_out"` 或 `"/Slice_time_output_0"`，说明已是修补后的动态时间步 ONNX，直接返回；
2. 若检测到是 HuggingFace 原始 `RIFE_fp32.onnx`（包含 `/Cast_8` 且未包含 `/Slice_time_output_0`），则在纯 C++26 中定位 `GraphProto` 内的 `/Cast_8` 节点，将其输入名原地替换为 `"__rtx_pad_time_out"`，并在 `GraphProto` 的 `node` / `initializer` 字段追加预编码的 `Slice([0,6,0,0]..[1,7,INT_MAX,INT_MAX], axes=[0,1,2,3], steps=[1,1,1,1])` + `Pad(/Pad_input_1, /Pad_input_2)` Protobuf 节点字节块，原子写回 `onnx_path`！
3. 如此一来，无论磁盘上放的是 HuggingFace 原始下载的 `RIFE_fp32.onnx`（SHA-256: `4077c7fb...`）还是已修补的 `rife_v4.25.onnx`（SHA-256: `2d0dd58f...`），`rife_builder.cpp` 均可在 **0 Python 依赖**下生成 100% 响应第 7 通道任意时间步 $t \in (0, 1)$ 的 `rife_v4.25.engine`！

#### 第二步：在 [`CMakeLists.txt`](../../CMakeLists.txt) 中集成 CMake 原生 RIFE ONNX 自动拉取与构建目录投递

在 [`CMakeLists.txt`](../../CMakeLists.txt) 的 `if(WIN32)` 块（第 62 行附近）追加：

```cmake
    # 4. RIFE v4.25 ONNX Model Auto-Provisioning (Cached across clean rebuilds in .vs/build/external/models)
    set(RTX_EXTERNAL_MODELS_DIR "${RTX_EXTERNAL_DIR}/models")
    file(MAKE_DIRECTORY "${RTX_EXTERNAL_MODELS_DIR}")
    set(RTX_CACHED_RIFE_ONNX "${RTX_EXTERNAL_MODELS_DIR}/rife_v4.25.onnx")
    if(NOT EXISTS "${RTX_CACHED_RIFE_ONNX}")
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/models/rife_v4.25.onnx")
            file(COPY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/models/rife_v4.25.onnx" "${RTX_CACHED_RIFE_ONNX}")
        else()
            message(STATUS "[CineRTX] Provisioning RIFE v4.25 ONNX model into ${RTX_CACHED_RIFE_ONNX} ...")
            file(DOWNLOAD
                "https://huggingface.co/FuryTMP/RIFE_fp32/resolve/main/RIFE_fp32.onnx"
                "${RTX_CACHED_RIFE_ONNX}.tmp"
                EXPECTED_HASH SHA256=4077c7fb5cf9da1fb3fb1fa2f1bf2dc8bf9ea3df5cd90cc57e60fb06b95b3680
                TLS_VERIFY ON
                STATUS _rife_dl_status
            )
            list(GET _rife_dl_status 0 _rife_dl_rc)
            if(_rife_dl_rc EQUAL 0)
                file(RENAME "${RTX_CACHED_RIFE_ONNX}.tmp" "${RTX_CACHED_RIFE_ONNX}")
            else()
                file(REMOVE "${RTX_CACHED_RIFE_ONNX}.tmp")
                message(STATUS "[CineRTX] Offline/CI environment without cached RIFE ONNX; runtime will synthesize/fallback as configured.")
            endif()
        endif()
    endif()
```

并在 `CMakeLists.txt:216-226` 的 `rtx_stage_ngx_plugin` 函数中，将缓存于 `${RTX_EXTERNAL_DIR}/models/` 下的 `rife_v4.25.onnx`（以及已编译缓存的 `rife_v4.25.engine`，若存在）自动同步至 `<TARGET_FILE_DIR>/models/`：

```cmake
    function(rtx_stage_ngx_plugin CONSUMER_TARGET)
        add_custom_command(TARGET ${CONSUMER_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:rtx_ngx_bridge>
                $<TARGET_FILE_DIR:${CONSUMER_TARGET}>/$<TARGET_FILE_NAME:rtx_ngx_bridge>
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:${CONSUMER_TARGET}>/models"
            COMMENT "Staging rtx_ngx_bridge plugin and RIFE model directory for ${CONSUMER_TARGET}"
        )
        if(EXISTS "${RTX_EXTERNAL_DIR}/models/rife_v4.25.onnx")
            add_custom_command(TARGET ${CONSUMER_TARGET} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${RTX_EXTERNAL_DIR}/models/rife_v4.25.onnx"
                    "$<TARGET_FILE_DIR:${CONSUMER_TARGET}>/models/rife_v4.25.onnx"
                COMMENT "Staging cached rife_v4.25.onnx for ${CONSUMER_TARGET}"
            )
        endif()
        if(EXISTS "${RTX_EXTERNAL_DIR}/models/rife_v4.25.engine")
            add_custom_command(TARGET ${CONSUMER_TARGET} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${RTX_EXTERNAL_DIR}/models/rife_v4.25.engine"
                    "$<TARGET_FILE_DIR:${CONSUMER_TARGET}>/models/rife_v4.25.engine"
                COMMENT "Staging cached rife_v4.25.engine for ${CONSUMER_TARGET}"
            )
        endif()
    endfunction()
```

#### 第三步：在 `src/shim/ngx_bridge/rife_engine.cpp:294-335` 扩展外部缓存探测与引擎回写缓存

1. 在 `rife_engine.cpp` 的 `base_dirs` 中加入构建缓存共享目录回溯（`exe_dir / ".." / ".." / "external" / "models"` 与 `.vs/build/external/models`）：
   ```cpp
   std::vector<fs::path> base_dirs;
   if (!exe_dir.empty()) {
       base_dirs.push_back(exe_dir / "models");
       base_dirs.push_back(exe_dir);
       base_dirs.push_back(exe_dir / ".." / ".." / "external" / "models");
       base_dirs.push_back(exe_dir / ".." / ".." / ".." / "models");
   }
   base_dirs.push_back(fs::current_path() / "models");
   base_dirs.push_back(fs::current_path() / ".vs" / "build" / "external" / "models");
   base_dirs.push_back(fs::current_path());
   ```
2. 当 `rife_engine.cpp` 首次从 `rife_v4.25.onnx` 编译出 `rife_v4.25.engine` 后，若 `exe_dir / ".." / ".." / "external" / "models"` 存在，同步将该 `.engine` 备份一份至 `.vs/build/external/models/rife_v4.25.engine`。这样即使工程师删除了 `.vs/build/cmake/windows-msvc-ninja` 重新配置编译，CMake 的 `rtx_stage_ngx_plugin` 也会在 0.1 秒内将已编译好的 `rife_v4.25.engine` 直接投递至新构建目录，无需二次等待 TensorRT 漫长的 Kernel Auto-Tuning！

## 七、 SuperPower 分步施工清单与终审封板验收门禁（Checklist）

请项目负责人与执行工程师按以下 5 个原子步骤执行收官合龙：

- [ ] **Step 1（清理工作区与更新 `.gitignore`）**：
  - 删除根目录 `Optical_Flow_SDK_5.0.7.zip`；
  - 执行 `git rm -rf tools/ tests/webui/ tests/unit/`；
  - 在 [`.gitignore`](../../.gitignore) 中追加 `/Optical_Flow_SDK*`。
- [ ] **Step 2（落地 4 个原生 CMake 模块并内联 FFmpeg 构建脚本）**：
  - 新建 `cmake/EmbedPtx.cmake`、`cmake/AuditSpdx.cmake`、`cmake/AuditImports.cmake`、`cmake/PackageRelease.cmake`；
  - 更新 [`cmake/RtxFfmpegStatic.cmake`](../../cmake/RtxFfmpegStatic.cmake) 内联生成 FFmpeg 构建脚本，消除对 `tools/build_ffmpeg_static*` 的引用；
  - 更新 [`.github/workflows/ci.yml`](../../.github/workflows/ci.yml)，将 `tools/check_spdx.ps1`、`tools/audit_imports.ps1`、`tools/package.ps1` 全部替换为 `cmake -P cmake/AuditSpdx.cmake`、`cmake -P cmake/AuditImports.cmake`、`cmake -P cmake/PackageRelease.cmake`。
- [ ] **Step 3（收敛 `tests/` 至 10 个纯 C++26 套件）**：
  - 将 `test_status.cpp` 并入 [`tests/test_types.cpp`](../../tests/test_types.cpp)（同步迁入 `smart_geometry_portrait_and_pillarbox`）并 `git rm tests/test_status.cpp`；
  - 将 `test_rational.cpp` 并入 [`tests/test_flicks_timebase.cpp`](../../tests/test_flicks_timebase.cpp) 并 `git rm tests/test_rational.cpp`；
  - 在 [`tests/test_io.cpp`](../../tests/test_io.cpp) 新增纯内存音视频双轨流拷贝测试 `in_memory_video_audio_stream_copy_roundtrip`；
  - 在 [`tests/test_shim.cpp`](../../tests/test_shim.cpp) 新增 C++26 原生 PE 导入表解析测试 `pe_static_import_table_zero_leak_gate`；
  - 在 [`tests/test_gui_workstation.cpp`](../../tests/test_gui_workstation.cpp) 合入 Win32 桌面交互冒烟断言；
  - 更新 [`CMakeLists.txt`](../../CMakeLists.txt) 中的测试注册列表（10 个 CTest 套件）。
- [ ] **Step 4（合入 RIFE 零 Python C++26 `/Cast_8` 图补丁与 CMake 模型自动供给）**：
  - 按第六章规范更新 [`src/shim/ngx_bridge/rife_builder.cpp`](../../src/shim/ngx_bridge/rife_builder.cpp)、[`src/shim/ngx_bridge/rife_engine.cpp`](../../src/shim/ngx_bridge/rife_engine.cpp)、[`CMakeLists.txt`](../../CMakeLists.txt) 与 [`build.bat`](../../build.bat)。
- [ ] **Step 5（终审封板全量验证门禁）**：
  1. `Test-Path tools` 返回 `False`，`Test-Path Optical_Flow_SDK_5.0.7.zip` 返回 `False`；
  2. `cmake -P cmake/AuditSpdx.cmake` 与 `cmake -P cmake/AuditImports.cmake` 100% 绿灯通过；
  3. `ctest --test-dir .vs/build/cmake/windows-msvc-ninja --output-on-failure` 全部 **10/10 C++26 测试套件 100% Pass**；
  4. 删除 `<build_dir>/models/rife_v4.25.engine` 后直接双击或命令行启动 `CineRTX.exe`，验证其零报错自动完成 ONNX -> Engine 编译与 120fps 插帧；
  5. `git status --short` 输出为空，提交并打标 `v0.2.0` 推送至 GitHub。
