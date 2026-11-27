# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
# 文件路径: cmake/RtxFfmpegStatic.cmake
# 功能说明: CineRTX FFmpeg 全量静态内联供应链、Git Submodule 自动化闭环构建与 rtx::ffmpeg_static 靶标定义
#
# 设计哲学 (KISS 与奥卡姆剃刀原则):
#   1. 彻底废除外部路径变量: 源码由 Git Submodule (third_party/ffmpeg) 严格锚定，
#      编译产物输出至 ${CMAKE_BINARY_DIR}/external/ffmpeg-static，完全随构建目录生命周期自闭环管理。
#   2. 零配置开箱即用 (Out-Of-The-Box): 无论是 MSVC、GCC 还是 Clang，直接运行:
#        mkdir build && cmake -B build -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build --config RelWithDebInfo -j
#      即可一键全自动化编译成功。
#   3. 手术刀级裁剪 + 导入桩防伪熔断 (Gate 3.0):
#      - 编译期硬性校验静态库真实体积 (avformat/avcodec 均 > 450 KB)，杜绝 DLL 动态导入桩；
#      - 若未初始化 submodule，CMake 自动执行拉取；若未构建静态库，CMake 在配置阶段自动触发编译。

include_guard(GLOBAL)

set(_ffmpeg_submodule_dir "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg")
set(_rtx_shared_ext_dir "${CMAKE_CURRENT_SOURCE_DIR}/.vs/build/external")
if(RTX_ZIG_EXECUTABLE OR (WIN32 AND NOT MSVC))
    set(_ffmpeg_dist_dir "${_rtx_shared_ext_dir}/ffmpeg-static-zig")
else()
    set(_ffmpeg_dist_dir "${_rtx_shared_ext_dir}/ffmpeg-static")
endif()
set(_ffmpeg_build_dir "${CMAKE_BINARY_DIR}/external/ffmpeg-build")
set(_ffmpeg_scripts_dir "${CMAKE_BINARY_DIR}/external/ffmpeg-scripts")
set(_ffmpeg_inc_dir "${_ffmpeg_dist_dir}/include")
set(_ffmpeg_lib_dir "${_ffmpeg_dist_dir}/lib")

# ---------------------------------------------------------------------------
# 施工项 A-1: 零宿主前置假设 —— nasm / make 就地拉取与沙箱化
#
# 所有物料统一收敛于 .vs/build/external 之下 (download/ 存放归档,
# tools/<name>/ 存放解压树)，与根目录 build.bat 及 CMakeLists.txt 的
# RTX_EXTERNAL_DIR 完全一致并互相复用归档。
# ---------------------------------------------------------------------------
set(_rtx_ext_dl_dir "${_rtx_shared_ext_dir}/download")
set(_rtx_ext_tools_dir "${_rtx_shared_ext_dir}/tools")

# _rtx_provision_tool(<name> <url> <sha256> <exe_filename> <out_var>)
#   下载 (校验 SHA256) 并解压便携工具; 成功时 <out_var> 为可执行文件全路径。
#   已解压或归档已在 (build.bat 预下载) 时零网络开销直接复用。
function(_rtx_provision_tool tool_name url expected_sha exe_name out_var)
    set(_tool_dir "${_rtx_ext_tools_dir}/${tool_name}")
    set(_archive "${_rtx_ext_dl_dir}/${tool_name}.zip")

    file(GLOB_RECURSE _already_there "${_tool_dir}/${exe_name}")
    if(_already_there)
        list(GET _already_there 0 _exe_path)
        set(${out_var} "${_exe_path}" PARENT_SCOPE)
        return()
    endif()

    file(MAKE_DIRECTORY "${_rtx_ext_dl_dir}")
    set(_need_download TRUE)
    if(EXISTS "${_archive}")
        file(SHA256 "${_archive}" _archive_sha)
        if(_archive_sha STREQUAL "${expected_sha}")
            set(_need_download FALSE)
        else()
            file(REMOVE "${_archive}")
        endif()
    endif()

    if(_need_download)
        message(STATUS "[CineRTX-Bootstrap] Downloading ${tool_name} from ${url}")
# curl is tried first because it honours HTTPS_PROXY/HTTP_PROXY, which
# file(DOWNLOAD) does not. Visual Studio's CMake integration inherits
# those variables, so this is the only proxy-aware path available to an
# IDE-driven configure.
        find_program(_rtx_curl_exe NAMES curl.exe curl)
        if(_rtx_curl_exe)
            execute_process(
                COMMAND "${_rtx_curl_exe}" -fL --retry 3 --retry-delay 2
                        --connect-timeout 30
                        -o "${_archive}" "${url}"
                RESULT_VARIABLE _curl_result
                OUTPUT_QUIET
                ERROR_VARIABLE _curl_error)
            if(NOT _curl_result EQUAL 0)
                message(STATUS "[CineRTX-Bootstrap] curl failed: ${_curl_error}")
                file(REMOVE "${_archive}")
            endif()
        endif()
        if(NOT EXISTS "${_archive}")
            file(DOWNLOAD "${url}" "${_archive}"
                 SHOW_PROGRESS
                 STATUS _dl_status)
            list(GET _dl_status 0 _dl_code)
            if(NOT _dl_code EQUAL 0)
                list(GET _dl_status 1 _dl_msg)
                file(REMOVE "${_archive}")
                message(WARNING "[CineRTX-Bootstrap] ${tool_name} download failed: ${_dl_msg}")
                set(${out_var} "" PARENT_SCOPE)
                return()
            endif()
        endif()
        file(SHA256 "${_archive}" _archive_sha)
        if(NOT "${_archive_sha}" STREQUAL "${expected_sha}")
            file(REMOVE "${_archive}")
            message(WARNING "[CineRTX-Bootstrap] ${tool_name} SHA256 mismatch")
            set(${out_var} "" PARENT_SCOPE)
            return()
        endif()
    endif()

    file(MAKE_DIRECTORY "${_tool_dir}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E tar xf "${_archive}"
        WORKING_DIRECTORY "${_tool_dir}"
        RESULT_VARIABLE _extract_res)
    if(NOT _extract_res EQUAL 0)
        message(WARNING "[CineRTX-Bootstrap] Failed to extract ${_archive}")
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()

    file(GLOB_RECURSE _found "${_tool_dir}/${exe_name}")
    if(_found)
        list(GET _found 0 _exe_path)
        set(${out_var} "${_exe_path}" PARENT_SCOPE)
    else()
        set(${out_var} "" PARENT_SCOPE)
    endif()
endfunction()

# Pinned artifacts (SHA256 verified against live downloads, 2026-10):
#   NASM 2.16.03 win64 —— nasm.us 官方权威源。
#   注意: NASM 官方 GitHub 仓库 (netwide-assembler/nasm) 不发布任何 Release
#   资产 (已实测 404 / releases 页面为空)，故不存在可校验的官方 GitHub 容灾
#   镜像；此处不硬编码任何未经验证的猜测 URL。
set(_rtx_nasm_url "https://www.nasm.us/pub/nasm/releasebuilds/2.16.03/win64/nasm-2.16.03-win64.zip")
set(_rtx_nasm_sha "3ee4782247bcb874378d02f7eab4e294a84d3d15f3f6ee2de2f47a46aa7226e6")
#   GNU Make 4.4.1 —— ezwinports (GNU 官方 Windows 移植维护者 Eli Zaretskii
#   发布的独立便携版, SourceForge 官方项目页, 实测 200 且可执行)。
set(_rtx_make_url "https://downloads.sourceforge.net/project/ezwinports/make-4.4.1-without-guile-w32-bin.zip")
set(_rtx_make_sha "fb66a02b530f7466f6222ce53c0b602c5288e601547a034e4156a512dd895ee7")

if(NOT EXISTS "${_ffmpeg_submodule_dir}/configure")
# 1. 确保 Git Submodule 源码已就绪
    find_package(Git QUIET)
    if(GIT_FOUND)
        message(STATUS "[CineRTX] Initializing FFmpeg submodule at third_party/ffmpeg...")
        execute_process(
            COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive third_party/ffmpeg
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            RESULT_VARIABLE _submod_res
        )
        if(NOT _submod_res EQUAL 0 OR NOT EXISTS "${_ffmpeg_submodule_dir}/configure")
            message(FATAL_ERROR
                "[CineRTX] Failed to checkout third_party/ffmpeg submodule.\n"
                "Please run 'git submodule update --init --recursive third_party/ffmpeg' manually.")
        endif()
    else()
        message(FATAL_ERROR
            "[CineRTX] FFmpeg source tree not found at ${_ffmpeg_submodule_dir} and git executable is not available.")
    endif()
endif()

set(_need_build_ffmpeg FALSE)
# 2. 检查静态物料是否已构建且合规
set(_required_ffmpeg_libs avformat avcodec avutil swresample)

if(WIN32)
    if(NOT EXISTS "${_ffmpeg_lib_dir}/.rtx-static-crt")
# CRT provenance gate.  The product is linked with the static CRT (/MT);
# FFmpeg archives built with /MD carry /DEFAULTLIB:MSVCRT and unresolved
# __imp_* references that the linker cannot satisfy from libucrt.lib.  The
# stamp is written only after a successful build of the current supply
# chain, so an existing build tree populated by the old /MD recipe is
# rebuilt once instead of being silently reused.
        set(_need_build_ffmpeg TRUE)
    endif()

    foreach(_lib_name IN LISTS _required_ffmpeg_libs)
        set(_lib_file "${_ffmpeg_lib_dir}/${_lib_name}.lib")
        if(NOT EXISTS "${_lib_file}")
            set(_lib_file "${_ffmpeg_lib_dir}/lib${_lib_name}.a")
        endif()
        if(NOT EXISTS "${_lib_file}")
            set(_need_build_ffmpeg TRUE)
            break()
        endif()

        if(_lib_name STREQUAL "avformat" OR _lib_name STREQUAL "avcodec")
# 核心防伪熔断：动态库导入桩仅 ~130 KB，真静态归档库必 > 450 KB
            file(SIZE "${_lib_file}" _lib_size_bytes)
            if(_lib_size_bytes LESS 450000)
                set(_need_build_ffmpeg TRUE)
                break()
            endif()
        endif()
    endforeach()

    if(NOT EXISTS "${_ffmpeg_inc_dir}/libavformat/avformat.h")
        set(_need_build_ffmpeg TRUE)
    endif()

    if(_need_build_ffmpeg)
# 3. 若尚未构建，自动触发本地手术刀级静态编译
        message(STATUS "[CineRTX-FFmpeg] Surgical static FFmpeg libraries not found or invalid in ${_ffmpeg_dist_dir}.")
        message(STATUS "[CineRTX-FFmpeg] Triggering automated surgical static build from third_party/ffmpeg...")

        find_program(NASM_EXE NAMES nasm.exe nasm)
# ---- 施工项 A-1: nasm / make 缺失时就地拉取便携版 ----
        if(NOT NASM_EXE)
            _rtx_provision_tool(nasm "${_rtx_nasm_url}" "${_rtx_nasm_sha}" nasm.exe _nasm_provisioned)
            if(_nasm_provisioned)
                set(NASM_EXE "${_nasm_provisioned}" CACHE FILEPATH "NASM x86 assembler" FORCE)
            else()
                message(FATAL_ERROR
                    "[CineRTX-Bootstrap] Failed to provision nasm.exe from ${_rtx_nasm_url}.\n"
                    "Note: NASM publishes no official GitHub Release mirror; check the "
                    "network or set HTTPS_PROXY/HTTP_PROXY before configuring.")
            endif()
        endif()
        get_filename_component(_nasm_dir "${NASM_EXE}" DIRECTORY)
        message(STATUS "[CineRTX-Bootstrap] nasm: ${NASM_EXE}")

        find_program(MAKE_EXE NAMES make.exe mingw32-make.exe)
        if(NOT MAKE_EXE)
            _rtx_provision_tool(make "${_rtx_make_url}" "${_rtx_make_sha}" make.exe _make_provisioned)
            if(_make_provisioned)
                set(MAKE_EXE "${_make_provisioned}" CACHE FILEPATH "GNU Make for FFmpeg" FORCE)
            else()
                message(FATAL_ERROR
                    "[CineRTX-Bootstrap] Failed to provision make.exe from ${_rtx_make_url}.")
            endif()
        endif()
        get_filename_component(_make_dir "${MAKE_EXE}" DIRECTORY)
        message(STATUS "[CineRTX-Bootstrap] make: ${MAKE_EXE}")

        set(_child_path "${_nasm_dir};${_make_dir};$ENV{PATH}")
# 局部 PATH 注入: 仅对本次子进程生效，绝不触碰系统 PATH。
# 注意: PATH 值含 ';' (CMake 列表分隔符)，必须以带引号的单一实参传递。
        set(_zig_env "")
        set(_rtx_zig_for_child "")
        if(RTX_ZIG_EXECUTABLE)
            set(_rtx_zig_for_child "${RTX_ZIG_EXECUTABLE}")
# CMake 已选定 Zig 工具链: FFmpeg 必须使用同一 ABI，即使当前环境
# (例如 Visual Studio 的 CMake 集成) 同时提供了 cl.exe。
        else()
            find_program(_cl_probe NAMES cl.exe cl)
            if(NOT _cl_probe)
                find_program(_zig_probe NAMES zig.exe zig)
                if(_zig_probe)
                    set(_rtx_zig_for_child "${_zig_probe}")
                endif()
            endif()
        endif()
        if(_rtx_zig_for_child)
            get_filename_component(_zig_dir "${_rtx_zig_for_child}" DIRECTORY)
            set(_child_path "${_zig_dir};${_child_path}")
            set(_zig_env "RTX_ZIG=${_rtx_zig_for_child}")
        endif()

        file(TO_CMAKE_PATH "${_ffmpeg_submodule_dir}" _src_path)
# 规范化转义 POSIX 风格路径供 bash 使用
        file(TO_CMAKE_PATH "${_ffmpeg_dist_dir}" _dist_path)
        file(TO_CMAKE_PATH "${_ffmpeg_build_dir}" _bld_path)

        file(MAKE_DIRECTORY "${_ffmpeg_scripts_dir}" "${_ffmpeg_build_dir}")
# Write helper scripts into _ffmpeg_scripts_dir (NEVER inside _ffmpeg_build_dir,
# because the build script executes `rm -rf "${BUILD_DIR}"` at startup!).
        file(WRITE "${_ffmpeg_scripts_dir}/build_ffmpeg_static_msvc.sh" [=[#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception

# 文件路径: tools/build_ffmpeg_static_msvc.sh
# 功能说明: 在 Windows MSVC 2022 (cl.exe / lib.exe) 工具链下编译手术刀级裁剪的 FFmpeg 7.1.1 静态归档库 (.lib)
# 适用目标: CineRTX 单 EXE 自包含架构 (体积包络: 静态库合计 ~2.7 MB, 纯 LGPLv2.1+ 零外部第三方依赖)
#
# 使用方式 (必须从 "x64 Native Tools Command Prompt for VS 2022" 启动的
# MSYS2 / Git Bash 中执行, 以便 cl.exe / lib.exe / nasm.exe 位于 PATH):
#
#   ./tools/build_ffmpeg_static_msvc.sh [FFMPEG_SRC_DIR] [PREFIX_DIR]
#
# 参数:
#   FFMPEG_SRC_DIR  已解压的 FFmpeg 7.1.1 源码树 (默认 ./ffmpeg-7.1.1)
#   PREFIX_DIR      产物安装前缀 (默认 ./dist/ffmpeg-static-win64-msvc)
#
# 产物:
#   <PREFIX_DIR>/include/libav*/...           头文件
#   <PREFIX_DIR>/lib/{avformat,avcodec,avutil,swresample}.lib
#   ./cinertx-ffmpeg-7.1.1-win64-msvc-static-lgpl.zip (+ .sha256)
#
# 注意: 本脚本产出的 .lib 为真实 COFF 静态归档 (avformat/avcodec 均 > 450 KB)。
# 若误用 ffmpeg-full_build-shared 附带的导入桩 (127-137 KB), CMake 侧的
# RtxFfmpegStatic.cmake 会在配置阶段以 FATAL_ERROR 阻断构建。
set -euo pipefail

FFMPEG_VERSION="${FFMPEG_VERSION:-7.1.1}"
SRC_DIR="${1:-./ffmpeg-${FFMPEG_VERSION}}"
PREFIX_DIR="${2:-$(pwd)/dist/ffmpeg-static-win64-msvc}"
BUILD_DIR="${3:-$(pwd)/build_ffmpeg_msvc}"

echo "[CineRTX-FFmpeg] Verifying MSVC toolchain (cl.exe, lib.exe, link.exe, nasm.exe)..."
command -v cl.exe >/dev/null 2>&1 || { echo "ERROR: cl.exe not found. Launch from VS 2022 x64 Native Tools Prompt with -use-full-path."; exit 1; }
command -v lib.exe >/dev/null 2>&1 || { echo "ERROR: lib.exe not found."; exit 1; }
command -v nasm.exe >/dev/null 2>&1 || command -v yasm.exe >/dev/null 2>&1 || { echo "ERROR: nasm/yasm not found in PATH."; exit 1; }

export MSYS2_ARG_CONV_EXCL="*"
# Phase 13 (Wave 0-B / 76_ & 78_ Ruling): Pure LGPL FFmpeg + BSD-3-Clause-Clear SVT-AV1 only.
# Zero GPL codecs (--enable-gpl / libx265 permanently removed) and zero hidden feature switches.
RTX_EXTRA_CODEC_FLAGS=""
RTX_EXTRA_LDFLAGS=""
if [[ -f "${PREFIX_DIR}/lib/SvtAv1Enc.lib" ]]; then
    RTX_EXTRA_CODEC_FLAGS="--enable-libsvtav1 --enable-encoder=libsvtav1"
    RTX_EXTRA_LDFLAGS="-LIBPATH:${PREFIX_DIR}/lib SvtAv1Enc.lib"
fi
if [[ ! -f "${SRC_DIR}/configure" ]]; then
    echo "ERROR: ${SRC_DIR}/configure not found. Pass the extracted FFmpeg ${FFMPEG_VERSION} source tree as argv[1]."
    exit 1
fi

rm -rf "${BUILD_DIR}" "${PREFIX_DIR}"
mkdir -p "${BUILD_DIR}" "${PREFIX_DIR}"

CYGPATH="$(command -v cygpath 2>/dev/null || true)"
# FFmpeg's configure refuses out-of-tree builds when the source path contains
# whitespace ("Out of tree builds are impossible with whitespace in source
# path"), and make cannot represent blanks in prerequisite/include paths.  A
# paths containing spaces violate both.  Resolve the Windows 8.3 short
# form *after* mkdir: short names are assigned when the directories are
# created, so resolving before the rm -rf/mkdir cycle yields stale aliases.
# build_ffmpeg_static.bat only guarantees bash's own directory on PATH, so
# cygpath is reachable through MSYS's fixed /usr/bin mount even when it is not
# on PATH.  On non-MSYS hosts the tool is absent and this block is skipped.
[[ -n "${CYGPATH}" ]] || CYGPATH=/usr/bin/cygpath.exe
if [[ -x "${CYGPATH}" ]]; then
    SRC_DIR="$("${CYGPATH}" -s -m "${SRC_DIR}")"
    PREFIX_DIR="$("${CYGPATH}" -s -m "${PREFIX_DIR}")"
    BUILD_DIR="$("${CYGPATH}" -s -m "${BUILD_DIR}")"
    case "${SRC_DIR}|${PREFIX_DIR}|${BUILD_DIR}" in
        *[[:blank:]]*)
            echo "ERROR: FFmpeg requires whitespace-free paths; enable 8.3 short names (fsutil 8dot3name set <volume> 0) or move the checkout." >&2
            exit 1 ;;
    esac
fi
cd "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}/tmp"
export TMPDIR="${BUILD_DIR}/tmp"

sed -i 's/\r$//' "${SRC_DIR}/configure" "${SRC_DIR}"/ffbuild/*.sh 2>/dev/null || true
# 手术刀级 ./configure 配置指令：
# 1. --toolchain=msvc 生成原生 COFF .lib 归档，传递 -MT -O2 -Ob2 -Gy -Gw 实现节区级死代码消除
#    注意: 必须使用静态 CRT (-MT)，与 CineRTX 主工程的 CMAKE_MSVC_RUNTIME_LIBRARY 保持一致。
#    若误用 -MD，归档会引用 __imp_* 并在 /MT 可执行文件链接时产生 LNK2001/LNK4217。
# 2. --disable-everything --disable-autodetect 斩断一切隐式外部依赖与硬件探测
# 3. 100% 禁用 CPU 视频编解码器、软缩放 (swscale)、滤镜图 (avfilter) 与系统网络/安全库 (network/schannel/mf)
sh "${SRC_DIR}/configure" \
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
    --enable-demuxer=mov,matroska,mpegts,flv,avi,mxf,asf,mpegps,rm,ivf,webm,dv,gif,concat,aac,mp3,flac,ogg,wav,w64 \
    --enable-muxer=mp4,mov,matroska,webm,mpegts,flv,adts,flac,opus,wav,w64,null \
    --enable-bsf=h264_mp4toannexb,hevc_mp4toannexb,av1_metadata,extract_extradata,aac_adtstoasc,vp9_superframe,vp9_raw_reorder,null \
    --enable-parser=h264,hevc,av1,vp8,vp9,mpeg4video,mpegvideo,vc1,aac,aac_latm,opus,mpegaudio,flac,ac3,vorbis \
    --enable-decoder=h264,hevc,av1,vp8,vp9,mpeg2video,mpeg4,vc1,wmv3,prores,dnxhd,dvvideo,aac,aac_fixed,aac_latm,opus,mp3,mp3float,flac,ac3,eac3,vorbis,alac,pcm_s16le,pcm_s16be,pcm_s24le,pcm_s24be,pcm_s32le,pcm_f32le,pcm_f32be,pcm_f64le,pcm_alaw,pcm_mulaw \
    --enable-encoder=aac,opus,eac3,ac3,flac,alac,pcm_s16le,pcm_s24le,pcm_s32le,pcm_f32le \
    --enable-encoder=mov_text \
    ${RTX_EXTRA_CODEC_FLAGS} \
    --extra-cflags="-MT -O2 -Ob2 -Gy -Gw -Zc:inline -utf-8 -D_WIN32_WINNT=0x0A00 -I${PREFIX_DIR}/include" \
    --extra-ldflags="${RTX_EXTRA_LDFLAGS}"

posix_to_win() {
    if command -v cygpath >/dev/null 2>&1; then
        cygpath -s -m "$1"
    elif [[ -x /usr/bin/cygpath.exe ]]; then
        /usr/bin/cygpath.exe -s -m "$1"
    else
        (cd "$1" && pwd -W 2>/dev/null) || echo "$1"
    fi
}
_src_posix="$(cd "${SRC_DIR}" && pwd)"
_bld_posix="$(pwd)"
_src_win="$(posix_to_win "${SRC_DIR}")"
_bld_win="$(posix_to_win "$(pwd)")"
echo "[CineRTX-FFmpeg] Normalizing recorded paths for native make.exe: ${_src_posix} -> ${_src_win}"
for f in Makefile ffbuild/config.mak ffbuild/config.sh ffbuild/config.fate config.h; do
    [[ -f "${f}" ]] || continue
    sed -i -e "s|${_src_posix}|${_src_win}|g" -e "s|${_bld_posix}|${_bld_win}|g" "${f}"
done
# Phase 13 (Wave 0-B): Deterministically disable HAVE_BCRYPT in FFmpeg so rebuilt avutil.lib carries zero bcrypt.dll IAT.
if [[ -f config.h ]]; then
    sed -i -e 's/#define HAVE_BCRYPT 1/#define HAVE_BCRYPT 0/g' config.h
fi
if [[ -f ffbuild/config.mak ]]; then
    sed -i -e 's/HAVE_BCRYPT=yes/!HAVE_BCRYPT=yes/g' -e 's/[[:space:]]*-lbcrypt//g' -e 's/[[:space:]]*bcrypt\.lib//g' ffbuild/config.mak
fi

echo "[CineRTX-FFmpeg] Compiling surgical static archives..."
make -j"$(nproc)"
make V=1 install
# V=1 is load-bearing: in quiet mode ffbuild/common.mak rewrites INSTALL into
# `@printf "INSTALL\t%s\n" $^; install -m 644 $^ "$dir"`, duplicating the whole
# prerequisite list.  install-libavutil-headers then becomes a single ~11 KB
# recipe line that make's `sh -c` invocation truncates, and install dies with
# "target '.../liba' is not a directory".

rm -rf "${PREFIX_DIR}/lib/pkgconfig" "${PREFIX_DIR}/share"
# 清理 pkgconfig 与多余包装文件，确保产物目录绝对纯净

echo "[CineRTX-FFmpeg] Verifying static archive sizes (must NOT be ~130KB import stubs)..."
for name in avformat avcodec avutil swresample; do
    if [[ -f "${PREFIX_DIR}/lib/lib${name}.a" ]]; then
        cp "${PREFIX_DIR}/lib/lib${name}.a" "${PREFIX_DIR}/lib/${name}.lib"
    elif [[ -f "${PREFIX_DIR}/lib/lib${name}.lib" ]]; then
        mv "${PREFIX_DIR}/lib/lib${name}.lib" "${PREFIX_DIR}/lib/${name}.lib"
    fi
    target_lib="${PREFIX_DIR}/lib/${name}.lib"
    [[ -f "${target_lib}" ]] || { echo "ERROR: Missing ${target_lib}"; exit 1; }
    size_bytes=$(stat -c%s "${target_lib}")
    echo "  -> ${name}.lib: ${size_bytes} bytes"
done

for lib in avformat.lib avcodec.lib; do
# 导入桩熔断：真静态归档的 avformat/avcodec 必大于 450 KB
    size_bytes=$(stat -c%s "${PREFIX_DIR}/lib/${lib}")
    if [[ "${size_bytes}" -lt 450000 ]]; then
        echo "ERROR: ${lib} is only ${size_bytes} bytes (< 450 KB) - this is a DLL import stub, not a static archive."
        exit 1
    fi
done

if command -v 7z >/dev/null 2>&1; then
    ARCHIVE_NAME="cinertx-ffmpeg-${FFMPEG_VERSION}-win64-msvc-static-lgpl.zip"
    cd "${PREFIX_DIR}"
    7z a -tzip "../${ARCHIVE_NAME}" include/* lib/*.lib >/dev/null || true
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "../${ARCHIVE_NAME}" > "../${ARCHIVE_NAME}.sha256" || true
        echo "[CineRTX-FFmpeg] Artifact created: $(pwd)/../${ARCHIVE_NAME}"
    fi
fi
echo "[CineRTX-FFmpeg] Static libraries ready in ${PREFIX_DIR}/lib"
]=])
        file(WRITE "${_ffmpeg_scripts_dir}/build_ffmpeg_static_zig.sh" [=[#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception

# 功能说明: 零宿主前置假设 (无 MSVC) 下的 FFmpeg 手术刀级静态编译支路：
#           以 `zig cc -target x86_64-windows-gnu` 作为 MinGW-w64 交叉编译器，
#           产出真实 COFF 静态归档 libavformat.a / libavcodec.a /
#           libavutil.a / libswresample.a。
#           MSVC 路径 (build_ffmpeg_static_msvc.sh) 保持原样并在 cl.exe
#           可用时优先；本脚本仅在裸机宿主或显式选择 Zig 时由 build_ffmpeg_static.bat 调用。
set -euo pipefail

SRC_DIR="${1:-./ffmpeg}"
PREFIX_DIR="${2:-$(pwd)/dist/ffmpeg-static-win64-zig}"
BUILD_DIR="${3:-$(pwd)/build_ffmpeg_zig}"

echo "[CineRTX-FFmpeg-Zig] Verifying portable toolchain (zig, nasm, make)..."
ZIG="${RTX_ZIG:-}"
if [[ -z "${ZIG}" ]]; then
    ZIG="$(command -v zig.exe || command -v zig || true)"
fi
[[ -n "${ZIG}" && -x "${ZIG}" ]] || { echo "ERROR: zig not found (set RTX_ZIG)."; exit 1; }
command -v nasm.exe >/dev/null 2>&1 || command -v nasm >/dev/null 2>&1 \
    || { echo "ERROR: nasm not found in PATH (provisioned by RtxFfmpegStatic.cmake)."; exit 1; }
MAKE_BIN="$(command -v make.exe || command -v make || true)"
[[ -n "${MAKE_BIN}" ]] || { echo "ERROR: make not found in PATH."; exit 1; }
echo "  zig : ${ZIG} ($("${ZIG}" version))"
echo "  nasm: $(command -v nasm.exe || command -v nasm)"
echo "  make: ${MAKE_BIN} ($("${MAKE_BIN}" --version | head -1))"

if [[ ! -f "${SRC_DIR}/configure" ]]; then
    echo "ERROR: ${SRC_DIR}/configure not found. Pass the FFmpeg source tree as argv[1]."
    exit 1
fi

rm -rf "${BUILD_DIR}" "${PREFIX_DIR}"
mkdir -p "${BUILD_DIR}" "${PREFIX_DIR}"

CYGPATH="$(command -v cygpath 2>/dev/null || true)"
[[ -n "${CYGPATH}" ]] || CYGPATH=/usr/bin/cygpath.exe
if [[ -x "${CYGPATH}" ]]; then
    SRC_DIR="$("${CYGPATH}" -s -m "${SRC_DIR}")"
    PREFIX_DIR="$("${CYGPATH}" -s -m "${PREFIX_DIR}")"
    BUILD_DIR="$("${CYGPATH}" -s -m "${BUILD_DIR}")"
    case "${SRC_DIR}|${PREFIX_DIR}|${BUILD_DIR}" in
        *[[:blank:]]*)
            echo "ERROR: FFmpeg requires whitespace-free paths; enable 8.3 short names (fsutil 8dot3name set <volume> 0) or move the checkout." >&2
            exit 1 ;;
    esac
fi
cd "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}/tmp"
export TMPDIR="${BUILD_DIR}/tmp"

mkdir -p wrappers
WRAPPER_DIR="$(pwd)/wrappers"

make_wrapper() {
    local name="$1"; shift
    printf '#!/bin/sh\nexec %s "$@"\n' "$*" > "${WRAPPER_DIR}/${name}"
    chmod +x "${WRAPPER_DIR}/${name}"
}
make_cc_wrapper() {
    cat > "${WRAPPER_DIR}/$1" <<EOF
#!/bin/sh
_out=()
for _a in "\$@"; do
    case "\$_a" in *--pic-executable*) ;; *) _out+=("\$_a") ;; esac
done
exec "${ZIG_WIN}" $2 -target x86_64-windows-gnu "\${_out[@]}"
EOF
    chmod +x "${WRAPPER_DIR}/$1"
}
ZIG_WIN="${ZIG//\\//}"

make_cc_wrapper zcc   cc
make_cc_wrapper zcxx  c++
make_wrapper zar      "\"${ZIG_WIN}\" ar"
make_wrapper zranlib  "\"${ZIG_WIN}\" ranlib"
printf '#!/bin/sh\nexit 0\n' > "${WRAPPER_DIR}/znm"
chmod +x "${WRAPPER_DIR}/znm"

export MSYS2_ARG_CONV_EXCL="*"
sed -i 's/\r$//' "${SRC_DIR}/configure" "${SRC_DIR}"/ffbuild/*.sh 2>/dev/null || true

sh "${SRC_DIR}/configure" \
    --prefix="${PREFIX_DIR}" \
    --enable-cross-compile \
    --arch=x86_64 \
    --target-os=mingw32 \
    --cc="${WRAPPER_DIR}/zcc" \
    --cxx="${WRAPPER_DIR}/zcxx" \
    --host-cc="${WRAPPER_DIR}/zcc" \
    --ar="${WRAPPER_DIR}/zar" \
    --ranlib="${WRAPPER_DIR}/zranlib" \
    --nm="${WRAPPER_DIR}/znm" \
    --enable-static \
    --disable-shared \
    --enable-small \
    --enable-asm \
    --enable-x86asm \
    --disable-debug \
    --disable-stripping \
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
    --enable-demuxer=mov,matroska,mpegts,flv,avi,mxf,asf,mpegps,rm,ivf,webm,dv,gif,aac,mp3,flac,ogg,wav,w64 \
    --enable-muxer=mp4,mov,matroska,webm,mpegts,flv,adts,flac,opus,wav,w64,null \
    --enable-bsf=h264_mp4toannexb,hevc_mp4toannexb,av1_metadata,extract_extradata,aac_adtstoasc,vp9_superframe,vp9_raw_reorder,null \
    --enable-parser=h264,hevc,av1,vp8,vp9,mpeg4video,mpegvideo,vc1,aac,aac_latm,opus,mpegaudio,flac,ac3,vorbis \
    --enable-decoder=h264,hevc,av1,vp8,vp9,mpeg2video,mpeg4,vc1,wmv3,prores,dnxhd,dvvideo,aac,aac_fixed,aac_latm,opus,mp3,mp3float,flac,ac3,eac3,vorbis,alac,pcm_s16le,pcm_s16be,pcm_s24le,pcm_s24be,pcm_s32le,pcm_f32le,pcm_f32be,pcm_f64le,pcm_alaw,pcm_mulaw \
    --enable-encoder=aac,opus,eac3,ac3,flac,alac,pcm_s16le,pcm_s24le,pcm_s32le,pcm_f32le \
    --extra-cflags="-O2 -D_WIN32_WINNT=0x0A00"

posix_to_win() {
    if command -v cygpath >/dev/null 2>&1; then
        cygpath -s -m "$1"
    elif [[ -x /usr/bin/cygpath.exe ]]; then
        /usr/bin/cygpath.exe -s -m "$1"
    else
        (cd "$1" && pwd -W 2>/dev/null) || echo "$1"
    fi
}
_src_posix="$(cd "${SRC_DIR}" && pwd)"
_bld_posix="$(pwd)"
_src_win="$(posix_to_win "${SRC_DIR}")"
_bld_win="$(posix_to_win "$(pwd)")"
echo "[CineRTX-FFmpeg-Zig] Normalizing recorded paths: ${_src_posix} -> ${_src_win}"
for f in Makefile ffbuild/config.mak ffbuild/config.sh ffbuild/config.fate config.h; do
    [[ -f "${f}" ]] || continue
    sed -i -e "s|${_src_posix}|${_src_win}|g" -e "s|${_bld_posix}|${_bld_win}|g" "${f}"
done

# Deterministically disable HAVE_BCRYPT in FFmpeg so rebuilt libavutil.a carries zero bcrypt.dll IAT.
if [[ -f config.h ]]; then
    sed -i -e 's/#define HAVE_BCRYPT 1/#define HAVE_BCRYPT 0/g' config.h
fi
if [[ -f ffbuild/config.mak ]]; then
    sed -i -e 's/HAVE_BCRYPT=yes/!HAVE_BCRYPT=yes/g' -e 's/[[:space:]]*-lbcrypt//g' -e 's/[[:space:]]*bcrypt\.lib//g' ffbuild/config.mak
fi

if ! grep -q '^#define HAVE_THREADS 1$' config.h; then
    echo "ERROR: FFmpeg configured itself with HAVE_THREADS 0."
    exit 1
fi
if grep -q 'compat/atomics/dummy' ffbuild/config.mak; then
    echo "ERROR: FFmpeg fell back to compat/atomics/dummy (non-atomic no-op atomics)."
    exit 1
fi
echo "[CineRTX-FFmpeg-Zig] Thread/atomic fuse OK: $(grep -o 'HAVE_[A-Z0-9]*THREADS 1' config.h | tr '\n' ' ')"

echo "[CineRTX-FFmpeg-Zig] Compiling surgical static archives (zig cc -target x86_64-windows-gnu)..."
"${MAKE_BIN}" -j"$(nproc)"
"${MAKE_BIN}" V=1 install

rm -rf "${PREFIX_DIR}/lib/pkgconfig" "${PREFIX_DIR}/share"

echo "[CineRTX-FFmpeg-Zig] Verifying static archive sizes (must NOT be ~130KB import stubs)..."
for name in avformat avcodec avutil swresample; do
    target_lib="${PREFIX_DIR}/lib/lib${name}.a"
    [[ -f "${target_lib}" ]] || { echo "ERROR: Missing ${target_lib}"; exit 1; }
    size_bytes=$(stat -c%s "${target_lib}")
    echo "  -> lib${name}.a: ${size_bytes} bytes"
done

for lib in libavformat.a libavcodec.a; do
    size_bytes=$(stat -c%s "${PREFIX_DIR}/lib/${lib}")
    if [[ "${size_bytes}" -lt 450000 ]]; then
        echo "ERROR: ${lib} is only ${size_bytes} bytes (< 450 KB) - not a real static archive."
        exit 1
    fi
done

echo "[CineRTX-FFmpeg-Zig] Static libraries ready in ${PREFIX_DIR}/lib"
]=])
        file(WRITE "${_ffmpeg_scripts_dir}/build_ffmpeg_static.bat" [=[@echo off
setlocal

REM SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
REM Auto-generated FFmpeg surgical static build wrapper (stored in ffmpeg-scripts,
REM separate from BUILD_DIR so rm -rf "${BUILD_DIR}" never deletes running scripts).

set "TOOLCHAIN_SH=build_ffmpeg_static_msvc.sh"
set "USE_MSVC=1"
if defined RTX_ZIG set "USE_MSVC="
if not defined USE_MSVC goto :check_msvc_done

REM ---- 1. Try to establish MSVC before enabling delayed expansion ----
REM vcvars64.bat / vsdevcmd.bat fails ("not was unexpected at this time") when
REM invoked with enabledelayedexpansion active or inside parenthesized blocks.
where cl.exe >nul 2>&1
if not errorlevel 1 goto :check_msvc_done

set "VS_DIR="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -property installationPath`) do set "VS_DIR=%%i"
)
if not defined VS_DIR goto :verify_cl
if not exist "%VS_DIR%\VC\Auxiliary\Build\vcvars64.bat" goto :verify_cl
echo [CineRTX-FFmpeg] Initializing MSVC x64 environment from %VS_DIR%...
call "%VS_DIR%\VC\Auxiliary\Build\vcvars64.bat" >nul

:verify_cl
where cl.exe >nul 2>&1
if errorlevel 1 set "USE_MSVC="

:check_msvc_done
setlocal enabledelayedexpansion

REM ---- 2. Zig/MinGW-w64 cross path when MSVC is unavailable or bypassed ----
if not defined USE_MSVC (
    set "ZIG_EXE="
    if defined RTX_ZIG set "ZIG_EXE=!RTX_ZIG!"
    if not defined ZIG_EXE (
        for /f "delims=" %%i in ('where zig.exe 2^>nul') do (
            if not defined ZIG_EXE set "ZIG_EXE=%%i"
        )
    )
    if defined ZIG_EXE (
        echo [CineRTX-FFmpeg] Using Zig/MinGW-w64 cross path: !ZIG_EXE!
        set "RTX_ZIG=!ZIG_EXE!"
        set "TOOLCHAIN_SH=build_ffmpeg_static_zig.sh"
        for %%D in ("!ZIG_EXE!") do set "PATH=%%~dpD;!PATH!"
    ) else (
        echo [CineRTX-FFmpeg] ERROR: neither cl.exe nor zig could be found.
        exit /b 1
    )
)

REM ---- 3. Find bash.exe (RTX_BASH, PATH, Git.exe relative, PortableGit, MSYS2) ----
set "BASH_CMD="
if defined RTX_BASH (
    if exist "!RTX_BASH!" set "BASH_CMD=!RTX_BASH!"
)
if not defined BASH_CMD (
    where bash.exe >nul 2>&1
    if !errorlevel! equ 0 (
        for /f "delims=" %%i in ('where bash.exe') do (
            if not defined BASH_CMD (
                echo "%%~fi" | findstr /i "System32\bash.exe" >nul
                if errorlevel 1 set "BASH_CMD=%%~fi"
            )
        )
    )
)
if not defined BASH_CMD (
    where git.exe >nul 2>&1
    if !errorlevel! equ 0 (
        for /f "delims=" %%g in ('where git.exe') do (
            for %%d in ("%%~dpg..") do (
                if not defined BASH_CMD if exist "%%~fd\bin\bash.exe" set "BASH_CMD=%%~fd\bin\bash.exe"
                if not defined BASH_CMD if exist "%%~fd\usr\bin\bash.exe" set "BASH_CMD=%%~fd\usr\bin\bash.exe"
            )
        )
    )
)
if not defined BASH_CMD (
    for %%p in (
        "C:\Program Files\Git\bin\bash.exe"
        "C:\Program Files\Git\usr\bin\bash.exe"
        "C:\Program Files (x86)\Git\bin\bash.exe"
        "%LOCALAPPDATA%\Programs\Git\bin\bash.exe"
        "%LOCALAPPDATA%\Programs\Git\usr\bin\bash.exe"
        "C:\msys64\usr\bin\bash.exe"
    ) do (
        if not defined BASH_CMD if exist %%p set "BASH_CMD=%%~p"
    )
)
if not defined BASH_CMD (
    for /d %%g in (
        "%USERPROFILE%\program\PortableGit*"
        "%USERPROFILE%\program\Git*"
        "%USERPROFILE%\scoop\apps\git\current"
        "C:\Program Files\Git*"
        "C:\Program Files (x86)\Git*"
        "C:\Program Files\PortableGit*"
        "%LOCALAPPDATA%\Programs\Git*"
    ) do (
        if not defined BASH_CMD if exist "%%~fg\bin\bash.exe" set "BASH_CMD=%%~fg\bin\bash.exe"
        if not defined BASH_CMD if exist "%%~fg\usr\bin\bash.exe" set "BASH_CMD=%%~fg\usr\bin\bash.exe"
    )
)

if not defined BASH_CMD (
    echo [CineRTX-FFmpeg] ERROR: bash.exe not found. Git for Windows / PortableGit or MSYS2 is required.
    exit /b 1
)

echo [CineRTX-FFmpeg] Using bash: !BASH_CMD!

REM ---- 4. Append bash's bin and usr\bin to the END of PATH ----
REM Keeping them at the end ensures MSVC's link.exe always wins over GNU /usr/bin/link.exe
REM while still providing sh.exe, cygpath.exe, sed.exe, grep.exe, stat.exe, rm.exe to make.exe!
for %%D in ("!BASH_CMD!") do (
    set "BASH_DIR=%%~dpD"
    for %%R in ("!BASH_DIR!..") do (
        if exist "%%~fR\usr\bin" (
            set "PATH=!PATH!;!BASH_DIR!;%%~fR\usr\bin"
        ) else (
            set "PATH=!PATH!;!BASH_DIR!"
        )
    )
)

set "SCRIPT_DIR=%~dp0"
set "SH_SCRIPT=!SCRIPT_DIR!!TOOLCHAIN_SH!"
set "SH_SCRIPT=!SH_SCRIPT:\=/!"

"!BASH_CMD!" "!SH_SCRIPT!" %*
exit /b %errorlevel%
]=])
        set(_build_bat "${_ffmpeg_scripts_dir}/build_ffmpeg_static.bat")

        find_program(_rtx_bash_exe NAMES bash.exe
            PATHS
                "C:/Program Files/Git/bin"
                "C:/Program Files/Git/usr/bin"
                "$ENV{LOCALAPPDATA}/Programs/Git/bin"
                "C:/msys64/usr/bin"
            NO_DEFAULT_PATH)
        set(_bash_env "")
        if(_rtx_bash_exe)
            file(TO_NATIVE_PATH "${_rtx_bash_exe}" _rtx_bash_native)
            set(_bash_env "RTX_BASH=${_rtx_bash_native}")
        endif()

        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E env "PATH=${_child_path}" ${_zig_env} ${_bash_env}
                    cmd /c call "${_build_bat}" "${_src_path}" "${_dist_path}" "${_bld_path}"
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
            RESULT_VARIABLE _build_res
        )

        if(NOT _build_res EQUAL 0)
            message(FATAL_ERROR
                "[CineRTX-FFmpeg] Automated surgical static build failed with exit code: ${_build_res}.\n"
                "MSVC path: ensure VS 2022/2026 toolset; bare-host path: ensure Git Bash and "
                "Zig 0.13 (NASM/make are auto-provisioned under external/tools/).\n"
                "If tool downloads are blocked, set HTTPS_PROXY and HTTP_PROXY before "
                "starting Visual Studio or CMake.")
        endif()

        file(WRITE "${_ffmpeg_lib_dir}/.rtx-static-crt"
            "static CRT supply chain verified\n")
    endif()
# The archives now match the product's /MT runtime; record that fact so
# the next configure can trust this prefix without rebuilding.

    if(NOT EXISTS "${_ffmpeg_inc_dir}/libavformat/avformat.h")
        message(FATAL_ERROR "[CineRTX-FFmpeg] Missing headers at ${_ffmpeg_inc_dir}/libavformat/avformat.h")
    endif()
# 4. 再次严密校验产物完整性与防伪门禁

    foreach(_lib_name IN LISTS _required_ffmpeg_libs)
        set(_lib_file "${_ffmpeg_lib_dir}/${_lib_name}.lib")
        if(NOT EXISTS "${_lib_file}")
            set(_lib_file "${_ffmpeg_lib_dir}/lib${_lib_name}.a")
        endif()
        if(NOT EXISTS "${_lib_file}")
            message(FATAL_ERROR "[CineRTX-FFmpeg] Missing required static archive: ${_ffmpeg_lib_dir}/${_lib_name}.lib")
        endif()

        if(_lib_name STREQUAL "avformat" OR _lib_name STREQUAL "avcodec")
            file(SIZE "${_lib_file}" _lib_size_bytes)
            if(_lib_size_bytes LESS 450000)
                message(FATAL_ERROR
                    "[CineRTX-ArchGuard] CRITICAL VIOLATION: '${_lib_file}' is only ${_lib_size_bytes} bytes (< 450 KB)!\n"
                    "This file is a dynamic DLL import stub, NOT a true static archive.")
            endif()
        endif()

        add_library(rtx::ffmpeg_${_lib_name} STATIC IMPORTED GLOBAL)
        set_target_properties(rtx::ffmpeg_${_lib_name} PROPERTIES
            IMPORTED_LOCATION "${_lib_file}"
            INTERFACE_INCLUDE_DIRECTORIES "${_ffmpeg_inc_dir}"
        )
    endforeach()

    add_library(rtx_ffmpeg_static INTERFACE)
    add_library(rtx::ffmpeg_static ALIAS rtx_ffmpeg_static)

# 声明统一聚合靶标 rtx::ffmpeg_static，严格遵循符号依赖拓扑顺序
    target_include_directories(rtx_ffmpeg_static SYSTEM INTERFACE "${_ffmpeg_inc_dir}")

    target_link_libraries(rtx_ffmpeg_static INTERFACE
        rtx::ffmpeg_avformat
        rtx::ffmpeg_avcodec
# 挂载四大静态库及 Win32 系统依赖库 (0 ws2_32.dll for Gate 4)
        rtx::ffmpeg_swresample
        rtx::ffmpeg_avutil
        bcrypt
        secur32
        shlwapi
        strmiids
        mfuuid
        ole32
        user32
    )

    if(MSVC)
        target_link_options(rtx_ffmpeg_static INTERFACE
            $<$<NOT:$<CONFIG:Debug>>:/OPT:REF /OPT:ICF>
        )
    endif()
else()
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(FFMPEG_STATIC QUIET libavformat libavcodec libswresample libavutil)
# Linux GCC / Clang: the distribution packages are the supported supply
# chain.  CI installs pkg-config plus the four -dev packages; a host without
# them cannot configure, which is intended (the media layer is unconditional).
    endif()

    if(FFMPEG_STATIC_FOUND)
        add_library(rtx_ffmpeg_static INTERFACE)
        add_library(rtx::ffmpeg_static ALIAS rtx_ffmpeg_static)
        target_include_directories(rtx_ffmpeg_static SYSTEM INTERFACE ${FFMPEG_STATIC_INCLUDE_DIRS})
        if(FFMPEG_STATIC_STATIC_LIBRARIES)
            target_link_libraries(rtx_ffmpeg_static INTERFACE ${FFMPEG_STATIC_STATIC_LIBRARIES} pthread m dl)
            message(STATUS "[CineRTX-FFmpeg] Linux: libav*.a present, linking statically")
# Prefer the fully static link set when the distribution ships the .a
# archives.  Where it does not, the shared objects are linked and the
# binary is not self-contained; Gate 3.0 self-containment is a Windows
# product claim, not a Linux one.
        else()
            target_link_libraries(rtx_ffmpeg_static INTERFACE ${FFMPEG_STATIC_LIBRARIES} pthread m dl)
            message(WARNING
                "[CineRTX-FFmpeg] Linux: no libav*.a archives found; linking the shared "
                "FFmpeg libraries, so the Linux binary is not self-contained.")
        endif()
    else()
        message(FATAL_ERROR
            "[CineRTX-FFmpeg] FFmpeg development packages were not found via pkg-config.\n"
            "  Debian/Ubuntu: sudo apt-get install -y pkg-config libavformat-dev libavcodec-dev libswresample-dev libavutil-dev\n"
            "  Fedora/RHEL:   sudo dnf install -y pkgconf-pkg-config ffmpeg-devel\n"
            "The media container layer is unconditional; a build without it is not a CineRTX binary.")
    endif()
endif()
