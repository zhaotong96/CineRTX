#!/usr/bin/env bash
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

if [[ ! -x "${SRC_DIR}/configure" ]]; then
    echo "ERROR: ${SRC_DIR}/configure not found. Pass the extracted FFmpeg ${FFMPEG_VERSION} source tree as argv[1]."
    exit 1
fi

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
    --enable-demuxer=mov,matroska,mpegts,flv,avi,aac,mp3,flac,ogg,wav,w64 \
    --enable-muxer=mp4,mov,matroska,webm,mpegts,flv,adts,flac,opus,wav,w64,null \
    --enable-bsf=h264_mp4toannexb,hevc_mp4toannexb,av1_metadata,extract_extradata,aac_adtstoasc,vp9_superframe,vp9_raw_reorder,null \
    --enable-parser=h264,hevc,av1,vp9,mpeg4video,aac,aac_latm,opus,mpegaudio,flac,ac3,vorbis \
    --enable-decoder=aac,aac_fixed,aac_latm,opus,mp3,mp3float,flac,ac3,eac3,vorbis,alac,pcm_s16le,pcm_s16be,pcm_s24le,pcm_s24be,pcm_s32le,pcm_f32le,pcm_f32be,pcm_f64le,pcm_alaw,pcm_mulaw \
    --enable-encoder=aac,opus,eac3,ac3,flac,alac,pcm_s16le,pcm_s24le,pcm_s32le,pcm_f32le \
    --extra-cflags="-MD -O2 -Ob2 -Gy -Gw -Zc:inline -utf-8 -D_WIN32_WINNT=0x0A00"

echo "[CineRTX-FFmpeg] Compiling surgical static archives..."
make -j"$(nproc)"
# V=1 is load-bearing: in quiet mode ffbuild/common.mak rewrites INSTALL into
# `@printf "INSTALL\t%s\n" $^; install -m 644 $^ "$dir"`, duplicating the whole
# prerequisite list.  install-libavutil-headers then becomes a single ~11 KB
# recipe line that make's `sh -c` invocation truncates, and install dies with
# "target '.../liba' is not a directory".  See build_ffmpeg_static_zig.sh.
make V=1 install

# 清理 pkgconfig 与多余包装文件，确保产物目录绝对纯净
rm -rf "${PREFIX_DIR}/lib/pkgconfig" "${PREFIX_DIR}/share"

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

# 导入桩熔断：真静态归档的 avformat/avcodec 必大于 450 KB
for lib in avformat.lib avcodec.lib; do
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
