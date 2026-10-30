# **CineRTX 纯 GPU 转码与非编创作引擎：FFmpeg 全量静态链接与单 EXE 自包含全栈工程实施方案与施工技术规范书**

**文档标识**：`SPEC-FFMPEG-STATIC-CONSOLIDATION-20261026`  
**文档性质**：P0 级阻塞性前置施工方案 暨 逐行对照施工技术规范书（SOP）  
**发布机构**：CineRTX 系统架构团队 / 全栈工程组  
**技术依据**：
1. 领域专家委员会终审裁定书：[`docs/phase3/CineRTX FFmpeg全量静态链接专家方案.md`](file:///C:/PUBLIC/[REDACTED_PROJ]/docs/phase3/CineRTX%20FFmpeg全量静态链接专家方案.md)（`VERDICT-FFMPEG-STATIC-CONSOLIDATION-20261026`）
2. 架构重构协助调查请求书：[`docs/phase3/CineRTX_FFmpeg全量静态链接与自包含架构协助调查文档.md`](file:///C:/PUBLIC/[REDACTED_PROJ]/docs/phase3/CineRTX_FFmpeg全量静态链接与自包含架构协助调查文档.md)（`DOSSIER-FFMPEG-STATIC-CONSOLIDATION-20261026`）
3. 阶段三全栈工程施工方案：[`docs/phase3/CineRTX_阶段三全栈工程施工方案.md`](file:///C:/PUBLIC/[REDACTED_PROJ]/docs/phase3/CineRTX_阶段三全栈工程施工方案.md)（`SPEC-PHASE3-CONSTRUCTION-20261025`）
4. 原生 Win32 极简单 EXE 演进路线：[`docs/gui/RTX 转码引擎 Win32 极简原生单 exe GUI 演进路线.md`](file:///C:/PUBLIC/[REDACTED_PROJ]/docs/gui/RTX%20转码引擎%20Win32%20极简原生单 exe%20GUI%20演进路线.md)  
**生效基线**：`CineRTX v0.1.0`（Git Commit: `32a396f`）$\longrightarrow$ `CineRTX v0.2.0-pre`（通过 Gate 3.0 前置验收门禁）  
**施工工期**：Phase 3 前置攻坚阶段（工序代号：`ACT-PHASE3-PRE-STATIC-01`，预计耗时：1.5 工程工作日）  
**基准运行环境**：NVIDIA GeForce RTX 5060 Laptop GPU（Blackwell 架构，sm_120，8,151 MiB GDDR7，Driver 617.14，CUDA 13.4，Windows 11 24H2，Visual Studio 2022 MSVC 19.40+ / C++20）

---

## 一、 施工总纲与自包含架构第一性原理 (Construction Architecture)

### 1.1 施工战略使命与 P0 级阻塞性定位

本施工方案旨在将领域专家委员会全票签发之终审裁定书（`VERDICT-FFMPEG-STATIC-CONSOLIDATION-20261026`）转化为可供全栈执行工程师严格逐行执行的工业级标准作业程序（SOP）。

```
┌─────────────────────────────────────────────────────────────────────────────────────────────┐
│                               阶段三工程施工拓扑与门禁阻断咬合机制                          │
├─────────────────────────────────────────────────────────────────────────────────────────────┤
│                                                                                             │
│   [Git: 32a396f 基线]                                                                       │
│           │                                                                                 │
│           ▼                                                                                 │
│   ┌─────────────────────────────────────────────────────────────────────────┐               │
│   │  【前置 P0 级攻坚工序】ACT-PHASE3-PRE-STATIC-01: FFmpeg 全量静态链接重构│               │
│   ├─────────────────────────────────────────────────────────────────────────┤               │
│   │ • ACT-STATIC-01: 手术刀级裁剪编译 MSVC 原生静态库 (.lib, ~2.70 MB)      │               │
│   │ • ACT-STATIC-02: 拔除 CMake 选配宏与 DLL 拷贝，建立 rtx::ffmpeg_static  │               │
│   │ • ACT-STATIC-03: 清除 ffmpeg_io.cpp 存根与宏，实装 MemoryAvioContext    │               │
│   │ • ACT-STATIC-04: 升级 audit_imports.ps1 阻断门禁，修订核心 Spec 文档   │               │
│   └─────────────────────────────────────────────────────────────────────────┘               │
│           │                                                                                 │
│           ▼                                                                                 │
│   ┌─────────────────────────────────────────────────────────────────────────┐               │
│   │  【Gate 3.0 静态自包含准出门禁】(阻塞性校验，未过严禁启动 W7)           │               │
│   │  1. 动态库黑名单 (av*.dll / sw*.dll) 计数严格 = 0                       │               │
│   │  2. 单 EXE 物理体积：1.2 MB ≤ rtx-transcode-gui.exe ≤ 12.0 MB           │               │
│   │  3. 独立纯净隔离目录双击 rtx-transcode-gui.exe 跑通 4K MP4 实机硬件重制│               │
│   └─────────────────────────────────────────────────────────────────────────┘               │
│           │ (通过 Gate 3.0 验收)                                                            │
│           ▼                                                                                 │
│   ┌─────────────────────────────────────────────────────────────────────────┐               │
│   │  【阶段三主线施工】SPEC-PHASE3-CONSTRUCTION-20261025 (W7 – W9 里程碑)   │               │
│   ├─────────────────────────────────────────────────────────────────────────┤               │
│   │ • W7: Flicks 纳秒时基、VMM 奇偶双缓冲与微内核 DAG 合流                  │               │
│   │ • W8: 双路 AI 插帧 (NvOFFRUC 硬件光流 + TensorRT RIFE v4.25 母版级)     │               │
│   │ • W9: DirectComposition 60Hz 零拷贝视口、AVX2 多轨音频管线与 Gate 3     │               │
│   └─────────────────────────────────────────────────────────────────────────┘               │
└─────────────────────────────────────────────────────────────────────────────────────────────┘
```

**强制纪律**：执行工程师必须清醒认识到，**本施工方案是《CineRTX_阶段三全栈工程施工方案.md》的绝对先决条件（Prerequisite）**。在 Gate 3.0 门禁未获 100% 验收绿灯之前，严禁启动阶段三 W7–W9 的任何业务代码编写。

### 1.2 重构前后二进制交付对比与准出红线

| 维度指标 | 当前错误实现 (As-Is) | 重构交付标准 (To-Be / Gate 3.0) |
| :--- | :--- | :--- |
| **媒体层集成形态** | 外部 Shared 动态构建（依赖 8 个外挂 DLL）或无能力的 Stub 存根 | **100% 纯静态归档（COFF .lib 全量内联入单个 EXE）** |
| **CMake 控制宏** | 存在 `RTX_WITH_FFMPEG` / `RTX_HAVE_FFMPEG` 选配宏与 `rtx_copy_ffmpeg_runtime` | **永久废除一切选配宏与 DLL 拷贝，媒体 I/O 为无条件核心第一公民** |
| **PE 外部 DLL 依赖** | 散落 `avcodec-61.dll` (89MB), `avformat-61.dll` (18.7MB) 等逾 110 MB | **0 个外部第三方媒体 DLL，仅依赖 Windows 系统自带核心 DLL 与 UCRT** |
| **单 EXE 物理体积** | 散落文件夹形态（合计 > 116 MB）或空壳 EXE（~338 KB） | **严格收敛于 8.0 MB 至 12.0 MB 黄金体积包络**（典型值 ~8.45 MB） |
| **开源许可证合规** | 散落动态库易受 DLL 劫持且与工程边界脱节 | **纯 LGPLv2.1+ 手术刀裁剪库并入主程序 GPLv3 Covered Work，合规自洽** |

---

## 二、 【ACT-STATIC-01 SOP】手术刀级裁剪编译与 MSVC 静态物料供应链

### 2.1 任务目标与交付产物
- 编写并合入 MSVC 原生静态裁剪编译脚本 [`tools/build_ffmpeg_static_msvc.sh`](file:///C:/PUBLIC/[REDACTED_PROJ]/tools/build_ffmpeg_static_msvc.sh)；
- 采用 MSYS2 驱动 Visual Studio 2022 原生工具链（`--toolchain=msvc`），对 FFmpeg 7.1.1 执行手术刀级白名单裁剪编译；
- 产出原生的 COFF 静态归档库：`avformat.lib`、`avcodec.lib`、`avutil.lib`、`swresample.lib`（总大小约 2.70 MB）；
- 打包为 `cinertx-ffmpeg-7.1.1-win64-msvc-static-lgpl.zip` 并生成 SHA-256 校验和文件。

### 2.2 核心脚本实施：`tools/build_ffmpeg_static_msvc.sh`

执行工程师必须在 `tools/` 目录下创建该脚本，严格保持如下生产级代码：

```bash
#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
# 文件路径: tools/build_ffmpeg_static_msvc.sh
# 功能说明: 在 Windows MSVC 2022 (cl.exe / lib.exe) 工具链下编译手术刀级裁剪的 FFmpeg 7.1.1 静态归档库 (.lib)
# 适用目标: CineRTX 单 EXE 自包含架构 (体积包络: 静态库合计 ~2.7 MB, 纯 LGPLv2.1+ 零外部第三方依赖)

set -euo pipefail

FFMPEG_VERSION="${FFMPEG_VERSION:-7.1.1}"
SRC_DIR="${1:-./ffmpeg-${FFMPEG_VERSION}}"
PREFIX_DIR="${2:-$(pwd)/dist/ffmpeg-static-win64-msvc}"
BUILD_DIR="$(pwd)/build_ffmpeg_msvc"

echo "[CineRTX-FFmpeg] Verifying MSVC toolchain (cl.exe, lib.exe, link.exe, nasm.exe)..."
command -v cl.exe >/dev/null 2>&1 || { echo "ERROR: cl.exe not found. Launch from VS 2022 x64 Native Tools Prompt with -use-full-path."; exit 1; }
command -v lib.exe >/dev/null 2>&1 || { echo "ERROR: lib.exe not found."; exit 1; }
command -v nasm.exe >/dev/null 2>&1 || command -v yasm.exe >/dev/null 2>&1 || { echo "ERROR: nasm/yasm not found in PATH."; exit 1; }

rm -rf "${BUILD_DIR}" "${PREFIX_DIR}"
mkdir -p "${BUILD_DIR}" "${PREFIX_DIR}"
cd "${BUILD_DIR}"

# 手术刀级 ./configure 配置指令：
# 1. --toolchain=msvc 生成原生 COFF .lib 归档，传递 -MD -O2 -Ob2 -Gy -Gw 实现节区级死代码消除
# 2. --disable-everything --disable-autodetect 斩断一切隐式外部依赖与硬件探测
# 3. 100% 禁用 CPU 视频编解码器、软缩放 (swscale)、滤镜图 (avfilter) 与系统网络/安全库 (network/schannel/mf)
"${SRC_DIR}/configure" \
    --prefix="${PREFIX_DIR}" \
    --toolchain=msvc \
    --arch=x86_64 \
    --target-os=win64 \
    --enable-static \
    --disable-shared \
    --enable-small \
    --enable-asm \
    --enable-x86asm \
    --disable-debug \
    --disable-doc \
    --disable-programs \
    --disable-ffmpeg \
    --disable-ffplay \
    --disable-ffprobe \
    --disable-avdevice \
    --disable-swscale \
    --disable-avfilter \
    --disable-postproc \
    --disable-network \
    --disable-autodetect \
    --disable-schannel \
    --disable-mediafoundation \
    --disable-d3d11va \
    --disable-dxva2 \
    --disable-vaapi \
    --disable-vdpau \
    --disable-vulkan \
    --disable-cuvid \
    --disable-nvenc \
    --disable-nvdec \
    --disable-ffnvcodec \
    --disable-cuda-llvm \
    --disable-iconv \
    --disable-zlib \
    --disable-bzlib \
    --disable-lzma \
    --disable-everything \
    --enable-avutil \
    --enable-swresample \
    --enable-avcodec \
    --enable-avformat \
    --enable-protocol=file,pipe,data \
    --enable-demuxer=mov,matroska,mpegts,flv,avi,aac,mp3,flac,ogg,wav \
    --enable-muxer=mp4,mov,matroska,webm,mpegts,flv,adts,flac,opus,wav,null \
    --enable-bsf=h264_mp4toannexb,hevc_mp4toannexb,av1_metadata,extract_extradata,aac_adtstoasc,vp9_superframe,vp9_raw_reorder,null \
    --enable-parser=h264,hevc,av1,vp9,mpeg4video,aac,aac_latm,opus,mpegaudio,flac,ac3,vorbis \
    --enable-decoder=aac,aac_fixed,aac_latm,opus,mp3,mp3float,flac,ac3,eac3,vorbis,alac,pcm_s16le,pcm_s16be,pcm_s24le,pcm_s24be,pcm_s32le,pcm_f32le,pcm_f32be,pcm_f64le,pcm_alaw,pcm_mulaw \
    --enable-encoder=aac,opus,eac3,ac3,flac,alac,pcm_s16le,pcm_s24le,pcm_s32le,pcm_f32le \
    --extra-cflags="-MD -O2 -Ob2 -Gy -Gw -Zc:inline -utf-8 -D_WIN32_WINNT=0x0A00"

echo "[CineRTX-FFmpeg] Compiling surgical static archives..."
make -j"$(nproc)"
make install

# 清理 pkgconfig 与多余包装文件，确保产物目录绝对纯净
rm -rf "${PREFIX_DIR}/lib/pkgconfig" "${PREFIX_DIR}/share"

echo "[CineRTX-FFmpeg] Verifying static archive sizes (must NOT be ~130KB import stubs)..."
for lib in avformat.lib avcodec.lib avutil.lib swresample.lib; do
    filepath="${PREFIX_DIR}/lib/lib${lib}"
    if [[ -f "${filepath}" ]]; then
        mv "${filepath}" "${PREFIX_DIR}/lib/${lib}"
    fi
    target_lib="${PREFIX_DIR}/lib/${lib}"
    [[ -f "${target_lib}" ]] || { echo "ERROR: Missing ${target_lib}"; exit 1; }
    size_bytes=$(stat -c%s "${target_lib}")
    echo "  -> ${lib}: ${size_bytes} bytes"
done

ARCHIVE_NAME="cinertx-ffmpeg-${FFMPEG_VERSION}-win64-msvc-static-lgpl.zip"
cd "${PREFIX_DIR}"
7z a -tzip "../${ARCHIVE_NAME}" include/* lib/*.lib >/dev/null
sha256sum "../${ARCHIVE_NAME}" > "../${ARCHIVE_NAME}.sha256"
echo "[CineRTX-FFmpeg] Build succeeded! Artifact: $(pwd)/../${ARCHIVE_NAME}"
cat "../${ARCHIVE_NAME}.sha256"
```

### 2.3 验证准出指令
```powershell
# 1. 验证静态库物理大小（单库均必大于 450 KB，彻底杜绝动态导入桩）
$libs = Get-ChildItem -Path "dist/ffmpeg-static-win64-msvc/lib" -Filter "*.lib"
foreach ($lib in $libs) {
    Write-Host "$($lib.Name): $($lib.Length) bytes"
    if ($lib.Name -match "avformat|avcodec" -and $lib.Length -lt 450000) {
        throw "CRITICAL: $($lib.Name) is an import stub!"
    }
}
# 2. 验证真实 COFF .obj 符号内嵌
dumpbin /HEADERS "dist/ffmpeg-static-win64-msvc/lib/avformat.lib" | Select-String -Pattern "COFF SYMBOL TABLE"
```

---

## 三、 【ACT-STATIC-02 SOP】构建体系彻底重构与动态妥协拔除

### 3.1 任务目标与代码靶点
- 新增独立 CMake 模块 [`cmake/RtxFfmpegStatic.cmake`](file:///C:/PUBLIC/[REDACTED_PROJ]/cmake/RtxFfmpegStatic.cmake)，实现纯本地路径绑定、450 KB 动态导入桩体积熔断及 `rtx::ffmpeg_static` 接口靶标；
- 清洗根构建文件 [`CMakeLists.txt`](file:///C:/PUBLIC/[REDACTED_PROJ]/CMakeLists.txt)，彻底删除第 189–203 行的 `rtx_copy_ffmpeg_runtime`、`RTX_FFMPEG_DLL_DIR`、`RTX_WITH_FFMPEG`、`RTX_HAVE_FFMPEG`；
- 将 `rtx::ffmpeg_static` 无条件链接至 `rtx_core`，通过 CMake 依赖传递性自动赋能全部 EXE 与测试。

### 3.2 核心脚本实施：`cmake/RtxFfmpegStatic.cmake`

执行工程师必须在 `cmake/` 目录下创建该文件：

```cmake
# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
# 文件路径: cmake/RtxFfmpegStatic.cmake
# 功能说明: CineRTX FFmpeg 全量静态内联供应链、动态导入桩拦截器与 rtx::ffmpeg_static 靶标定义

include_guard(GLOBAL)

# 永久拦截并废弃旧版妥协变量与函数
if(DEFINED RTX_WITH_FFMPEG OR DEFINED RTX_FFMPEG_DLL_DIR)
    message(WARNING "[CineRTX-Spec] RTX_WITH_FFMPEG and RTX_FFMPEG_DLL_DIR are permanently abolished. "
                    "FFmpeg is an unconditional static core component (To-Be Single-EXE Architecture).")
endif()

set(RTX_FFMPEG_SUBMODULE (third_party/ffmpeg) "" CACHE PATH "Path to surgical static FFmpeg root (containing include/ and lib/)")
if(NOT RTX_FFMPEG_SUBMODULE (third_party/ffmpeg) AND DEFINED ENV{RTX_FFMPEG_SUBMODULE (third_party/ffmpeg)})
    set(RTX_FFMPEG_SUBMODULE (third_party/ffmpeg) "$ENV{RTX_FFMPEG_SUBMODULE (third_party/ffmpeg)}" CACHE PATH "From ENV" FORCE)
endif()

if(WIN32)
    if(NOT RTX_FFMPEG_SUBMODULE (third_party/ffmpeg))
        message(FATAL_ERROR
            "[CineRTX] 未指定本地手术刀级 FFmpeg 静态库根目录。\n"
            "  * 请传入 include/ 与 lib/），\n"
            "    或设置 RTX_FFMPEG_SUBMODULE (third_party/ffmpeg) 环境变量。\n"
            "  * 若本地尚未编译该静态物料，请使用以下脚本进行手术刀级本地编译：\n"
            "    tools/build_ffmpeg_static_msvc.sh <ffmpeg-7.1.1-源码路径> <输出前缀路径>\n"
            "媒体容器解复用与复用层为无条件自包含核心组件，缺失将阻断构建 (Gate 3.0)。")
    endif()

    # 校验头文件与四大静态库存在性
    set(_ffmpeg_inc_dir "${RTX_FFMPEG_SUBMODULE (third_party/ffmpeg)}/include")
    set(_ffmpeg_lib_dir "${RTX_FFMPEG_SUBMODULE (third_party/ffmpeg)}/lib")
    if(NOT EXISTS "${_ffmpeg_inc_dir}/libavformat/avformat.h")
        message(FATAL_ERROR "[CineRTX-FFmpeg] Missing headers at ${_ffmpeg_inc_dir}/libavformat/avformat.h")
    endif()

    set(_required_ffmpeg_libs avformat avcodec avutil swresample)
    foreach(_lib_name IN LISTS _required_ffmpeg_libs)
        set(_lib_file "${_ffmpeg_lib_dir}/${_lib_name}.lib")
        if(NOT EXISTS "${_lib_file}")
            set(_lib_file "${_ffmpeg_lib_dir}/lib${_lib_name}.a")
        endif()
        if(NOT EXISTS "${_lib_file}")
            message(FATAL_ERROR "[CineRTX-FFmpeg] Missing required static archive: ${_ffmpeg_lib_dir}/${_lib_name}.lib")
        endif()

        # 核心防线：编译期拦截动态导入桩 (Import Stubs)
        # 动态库附带的 avformat.lib / avcodec.lib 仅有 ~127-137 KB，而真静态归档库必大于 450 KB
        if(_lib_name STREQUAL "avformat" OR _lib_name STREQUAL "avcodec")
            file(SIZE "${_lib_file}" _lib_size_bytes)
            if(_lib_size_bytes LESS 450000)
                message(FATAL_ERROR
                    "[CineRTX-ArchGuard] CRITICAL VIOLATION: '${_lib_file}' is only ${_lib_size_bytes} bytes (< 450 KB)!\n"
                    "This file is a dynamic DLL import stub (e.g., from ffmpeg-full_build-shared), NOT a true static archive.\n"
                    "CineRTX strictly forbids dynamic FFmpeg DLL linking. Provide a true static archive built via tools/build_ffmpeg_static_msvc.sh.")
            endif()
        endif()

        add_library(rtx::ffmpeg_${_lib_name} STATIC IMPORTED GLOBAL)
        set_target_properties(rtx::ffmpeg_${_lib_name} PROPERTIES
            IMPORTED_LOCATION "${_lib_file}"
            INTERFACE_INCLUDE_DIRECTORIES "${_ffmpeg_inc_dir}"
        )
    endforeach()

    # 声明统一聚合靶标 rtx::ffmpeg_static，严格遵循符号依赖拓扑顺序 (avformat -> avcodec -> swresample -> avutil)
    add_library(rtx_ffmpeg_static INTERFACE)
    add_library(rtx::ffmpeg_static ALIAS rtx_ffmpeg_static)

    target_include_directories(rtx_ffmpeg_static SYSTEM INTERFACE "${_ffmpeg_inc_dir}")

    # 挂载四大静态库及深层 Win32 系统依赖库，彻底消灭 LNK2019
    target_link_libraries(rtx_ffmpeg_static INTERFACE
        rtx::ffmpeg_avformat
        rtx::ffmpeg_avcodec
        rtx::ffmpeg_swresample
        rtx::ffmpeg_avutil
        bcrypt
        ws2_32
        secur32
        shlwapi
        strmiids
        mfuuid
        ole32
        user32
    )

    if(MSVC)
        # 开启死代码剥离 (/OPT:REF) 与相同 COMDAT 折叠 (/OPT:ICF)，并在 Debug 模式下兼容 /MD 静态库
        target_link_options(rtx_ffmpeg_static INTERFACE
            $<$<NOT:$<CONFIG:Debug>>:/OPT:REF /OPT:ICF>
            $<$<CONFIG:Debug>:/NODEFAULTLIB:msvcrt.lib>
        )
    endif()
else()
    # Linux GCC / Clang 静态链接支路
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG_STATIC REQUIRED IMPORTED_TARGET GLOBAL
        libavformat>=61.0.0 libavcodec>=61.0.0 libswresample>=5.0.0 libavutil>=59.0.0)
    add_library(rtx_ffmpeg_static INTERFACE)
    add_library(rtx::ffmpeg_static ALIAS rtx_ffmpeg_static)
    target_link_libraries(rtx_ffmpeg_static INTERFACE PkgConfig::FFMPEG_STATIC pthread m dl)
endif()
```

### 3.3 根 `CMakeLists.txt` 修改规范
在 `CMakeLists.txt` 中进行手术刀级替换：
1. **删除** 原有 `option(RTX_WITH_FFMPEG ...)` 与查找动态库循环（约第 34–117 行）；
2. **引入静态模块**：
   ```cmake
   # 引入全量静态 FFmpeg 供应链
   include(cmake/RtxFfmpegStatic.cmake)
   ```
3. **绑定至 `rtx_core`**：
   ```cmake
   target_link_libraries(rtx_core PUBLIC rtx_project_options rtx::ffmpeg_static)
   ```
4. **彻底删除** `rtx_copy_ffmpeg_runtime` 函数定义及其在 `rtx-transcode`、`rtx-transcode-gui`、`rtx-transcode-base`、`rtx_tests` 后的所有调用。

---

## 四、 【ACT-STATIC-03 SOP】解封装层纯化与无磁盘内存流单测闭环

### 4.1 任务目标与代码靶点
- 修订 [`include/rtx/status.h`](file:///C:/PUBLIC/[REDACTED_PROJ]/include/rtx/status.h)：永久删除 `ErrorCode::FfmpegMissing`；
- 修订 [`src/io/ffmpeg_io.cpp`](file:///C:/PUBLIC/[REDACTED_PROJ]/src/io/ffmpeg_io.cpp)：彻底移除全部 `#ifdef RTX_HAVE_FFMPEG`，删除 `FfmpegNotCompiledDemuxer` 与 `FfmpegNotCompiledMuxer` 存根类，合入基于 `avio_alloc_context` 的 `MemoryAvioContext` 内存包装器；
- 扩充单元测试 [`tests/test_io.cpp`](file:///C:/PUBLIC/[REDACTED_PROJ]/tests/test_io.cpp)：新增基于内存缓冲区的真实容器解封装与复用自环单测，确保测试过程 100% 覆盖真实静态 FFmpeg 代码，且 0 磁盘 I/O 依赖。

### 4.2 `src/io/ffmpeg_io.cpp` 纯化与 `MemoryAvioContext` 核心实装

执行工程师必须确保源码顶部直接无条件包含头文件，并实装自定义内存 I/O 回调：

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// 文件路径: src/io/ffmpeg_io.cpp (节选修订)
#include "rtx/io.h"
#include "rtx/status.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

// 严禁任何 #ifdef 宏包裹，无条件静态强依赖
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

namespace rtx::io {

// 供无磁盘单元测试使用的内存流 AVIOContext 上下文管理器
class MemoryAvioContext final {
public:
    explicit MemoryAvioContext(std::span<const uint8_t> read_only_buf)
        : read_view_(read_only_buf), is_writable_(false) {
        initAvio(0);
    }

    explicit MemoryAvioContext(std::vector<uint8_t>* write_buf)
        : write_vec_(write_buf), is_writable_(true) {
        initAvio(1);
    }

    ~MemoryAvioContext() {
        if (avio_ctx_ != nullptr) {
            av_freep(&avio_ctx_->buffer);
            avio_context_free(&avio_ctx_);
        }
    }

    MemoryAvioContext(const MemoryAvioContext&) = delete;
    MemoryAvioContext& operator=(const MemoryAvioContext&) = delete;

    [[nodiscard]] AVIOContext* raw() const noexcept { return avio_ctx_; }

private:
    static constexpr size_t kAvioPageSize = 32768; // 32 KB I/O 扇区

    void initAvio(int write_flag) {
        auto* io_buf = static_cast<uint8_t*>(av_malloc(kAvioPageSize));
        if (!io_buf) return;
        avio_ctx_ = avio_alloc_context(
            io_buf, static_cast<int>(kAvioPageSize), write_flag, this,
            &MemoryAvioContext::readPacketCallback,
            write_flag ? &MemoryAvioContext::writePacketCallback : nullptr,
            &MemoryAvioContext::seekCallback);
    }

    static int readPacketCallback(void* opaque, uint8_t* buf, int buf_size) noexcept {
        auto* self = static_cast<MemoryAvioContext*>(opaque);
        const std::span<const uint8_t> src = self->is_writable_
            ? std::span<const uint8_t>(*self->write_vec_) : self->read_view_;
        if (self->pos_ >= src.size()) return AVERROR_EOF;
        const size_t to_copy = std::min(src.size() - self->pos_, static_cast<size_t>(buf_size));
        std::memcpy(buf, src.data() + self->pos_, to_copy);
        self->pos_ += to_copy;
        return static_cast<int>(to_copy);
    }

    static int writePacketCallback(void* opaque, const uint8_t* buf, int buf_size) noexcept {
        auto* self = static_cast<MemoryAvioContext*>(opaque);
        if (!self->is_writable_ || !self->write_vec_ || buf_size <= 0) return AVERROR(EIO);
        const size_t end_pos = self->pos_ + static_cast<size_t>(buf_size);
        if (end_pos > self->write_vec_->size()) self->write_vec_->resize(end_pos);
        std::memcpy(self->write_vec_->data() + self->pos_, buf, static_cast<size_t>(buf_size));
        self->pos_ = end_pos;
        return buf_size;
    }

    static int64_t seekCallback(void* opaque, int64_t offset, int whence) noexcept {
        auto* self = static_cast<MemoryAvioContext*>(opaque);
        const size_t total_size = self->is_writable_ ? self->write_vec_->size() : self->read_view_.size();
        if (whence == AVSEEK_SIZE) return static_cast<int64_t>(total_size);
        int64_t target = 0;
        if (whence == SEEK_SET) target = offset;
        else if (whence == SEEK_CUR) target = static_cast<int64_t>(self->pos_) + offset;
        else if (whence == SEEK_END) target = static_cast<int64_t>(total_size) + offset;
        else return AVERROR(EINVAL);
        if (target < 0) return AVERROR(EINVAL);
        if (self->is_writable_ && static_cast<size_t>(target) > total_size) {
            self->write_vec_->resize(static_cast<size_t>(target), 0);
        }
        self->pos_ = static_cast<size_t>(target);
        return target;
    }

    std::span<const uint8_t> read_view_{};
    std::vector<uint8_t>* write_vec_{nullptr};
    size_t pos_{0};
    bool is_writable_{false};
    AVIOContext* avio_ctx_{nullptr};
};

} // namespace rtx::io
```

### 4.3 单元测试扩充验证
在 `tests/test_io.cpp` 中新增纯内存自环单测，并在构建后执行：
```powershell
ctest --test-dir build -R test_io --output-on-failure
```
**准出指标**：全量测试 100% 绿灯，零 `FfmpegMissing` 报错，零外部磁盘文件依赖。

---

## 五、 【ACT-STATIC-04 SOP】PE 导入表阻断门禁、Spec 修订与实机孤岛验收

### 5.1 导入表与体积双门禁升级：`tools/audit_imports.ps1`

执行工程师必须将 `tools/audit_imports.ps1` 升级为具备以下三重强校验的生产级门禁脚本：
1. **Zone B/C 严禁静态导入**：`nvcuda.dll`、`nvcuvid.dll`、`nvencodeapi.dll`、`_nvngx.dll` 等必须走 `LoadLibraryExW`；
2. **Zone A 严禁动态导入**：`avcodec*.dll`、`avformat*.dll`、`avutil*.dll`、`swresample*.dll` 等被列入 PE 导入表黑名单；
3. **构建目录严禁外挂**：扫描输出目录，若存在任何 `av*.dll` 或 `sw*.dll` 直接抛错失败；
4. **黄金体积包络断言**：$1.2\text{ MB} \le \text{EXE Size} \le 12.0\text{ MB}$。

### 5.2 核心规范文档逐字修订规约

执行工程师必须同步对以下文档执行修订，确保 Spec 绝对领先并驱动代码：

1. **[`docs/spec/architecture.md`](file:///C:/PUBLIC/[REDACTED_PROJ]/docs/spec/architecture.md)**（第 1.2 节新增）：
   > **【单 EXE 绝对自包含第一原则】**：CineRTX 主程序（`rtx-transcode.exe` 与 `rtx-transcode-gui.exe`）必须构建为 100% 自包含的原生 Win32 单体可执行文件（体积上限 12.0 MB）。媒体容器解封装、交织复用、码流过滤与音频编解码均为不可分割的第一公民核心组件，必须通过手术刀级裁剪的 FFmpeg 静态库（.lib）全量内联进主可执行文件内部。代码库中永久严禁出现任何将媒体容器层降级为可选特性的条件编译宏（如 `RTX_WITH_FFMPEG` / `RTX_HAVE_FFMPEG`）或向产物目录拷贝外部媒体 DLL 的构建逻辑。
2. **[`docs/spec/compliance.md`](file:///C:/PUBLIC/[REDACTED_PROJ]/docs/spec/compliance.md)**（第 2.1 节新增）：
   > **【FFmpeg 静态内联法务合规准则】**：CineRTX 所集成的手术刀级裁剪 FFmpeg 静态库在配置期严格禁用 `--enable-gpl` 与 `--enable-nonfree`，保持纯正 LGPLv2.1+ 授权。静态链接并入 GPL-3.0-or-later WITH Plugin-Exception 的 Zone A 主工程时，依据 LGPLv2.1 第 3 条合法升级并入主工程 GPLv3 授权域（Covered Work）。编译期显式禁用 `--disable-ffnvcodec --disable-cuvid --disable-nvenc`，确保仅有 Zone B/C 的独立桥接模块动态加载 NVIDIA 专有驱动。
3. **[`docs/spec/gate3.md`](file:///C:/PUBLIC/[REDACTED_PROJ]/docs/spec/gate3.md)**（新增 Gate 3.0 前置门禁定义）。

### 5.3 独立纯净隔离目录实机转码验收 SOP
```powershell
# 1. 创建零环境变量纯净沙盒隔离目录
$IsolatedDir = "C:\PUBLIC\[REDACTED_SANDBOX]"
Remove-Item -Path $IsolatedDir -Recurse -Force -ErrorAction SilentlyContinue
New-Item -Path $IsolatedDir -ItemType Directory | Out-Null

# 2. 仅拷贝单文件 rtx-transcode-gui.exe 到隔离目录 (零外部 FFmpeg DLL!)
Copy-Item "build/RelWithDebInfo/rtx-transcode-gui.exe" "$IsolatedDir\"

# 3. 剥离当前会话中一切可能存在的 FFmpeg PATH 环境变量
$env:PATH = ($env:PATH.Split(';') | Where-Object { $_ -notmatch "ffmpeg" }) -join ';'

# 4. 在独立沙盒内实机双击运行，输入 1080p MP4 视频，执行 4K HDR10 硬件重制转码
Set-Location $IsolatedDir
.\rtx-transcode-gui.exe --input "C:\PUBLIC\[REDACTED_SAMPLE]" --output "output_4k_hdr10.mp4" --backend nvidia

# 5. 检验输出 MP4 是否完整生成且播放正常
```

---

## 六、 Gate 3.0 前置门禁准出清单 (Exit Criteria Checklist)

| 门禁项代号 | 检验靶点与自动化执行指令 | 合格判断准则（Pass Criteria） |
| :--- | :--- | :--- |
| **GATE-3.0-MACRO** | `git grep -E "RTX_HAVE_FFMPEG|FfmpegNotCompiled|FfmpegMissing|rtx_copy_ffmpeg_runtime"` | 匹配结果计数**严格为 0** |
| **GATE-3.0-AUDIT** | `powershell -File tools/audit_imports.ps1 -BuildDir build/RelWithDebInfo` | 输出 **result: PASS**，0 违规 |
| **GATE-3.0-ENVELOPE**| `(Get-Item build/RelWithDebInfo/rtx-transcode-gui.exe).Length / 1MB` | **8.0 MB ≤ 实际体积 ≤ 12.0 MB** |
| **GATE-3.0-TESTS** | `ctest --test-dir build --output-on-failure` | 现有 113 项单测 + 内存流单测 **100% PASS** |
| **GATE-3.0-ISOLATION**| 独立空白隔离目录单文件双击转码实测 | 100% 成功生成合法 4K HDR10 MP4，零环境依赖报错 |

**签发授权**：Gate 3.0 门禁全部签署通过后，执行工程师即刻获得授权启动 [`docs/phase3/CineRTX_阶段三全栈工程施工方案.md`](file:///C:/PUBLIC/[REDACTED_PROJ]/docs/phase3/CineRTX_阶段三全栈工程施工方案.md) 之 W7 里程碑主线施工。
