#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
# 文件路径: tools/build_ffmpeg_static_zig.sh
# 功能说明: 零宿主前置假设 (无 MSVC) 下的 FFmpeg 手术刀级静态编译支路：
#           以 `zig cc -target x86_64-windows-gnu` 作为 MinGW-w64 交叉编译器，
#           产出真实 COFF 静态归档 libavformat.a / libavcodec.a /
#           libavutil.a / libswresample.a。
# 适用目标: CineRTX 单 EXE 自包含架构 (施工项 A / Deliverable 3)。
#           MSVC 路径 (build_ffmpeg_static_msvc.sh) 保持原样并在 cl.exe
#           可用时优先；本脚本仅在裸机宿主上由 build_ffmpeg_static.bat 选择。
#
# 使用方式 (由 tools/build_ffmpeg_static.bat 通过 Git Bash 调用):
#   ./tools/build_ffmpeg_static_zig.sh [FFMPEG_SRC_DIR] [PREFIX_DIR] [BUILD_DIR]
#
# 环境要求 (均由 cmake/RtxFfmpegStatic.cmake 注入局部 PATH):
#   RTX_ZIG   指向 zig.exe (或 zig 已在 PATH)
#   nasm.exe  2.16.03 (便携版自动拉取)
#   make.exe  GNU Make 4.4.1 (便携版自动拉取)
#
# 注意: 本脚本产出的 .a 为真实静态归档 (avformat/avcodec 均 > 450 KB)，
#       cmake/RtxFfmpegStatic.cmake 的导入桩防伪熔断门禁原样生效。

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
cd "${BUILD_DIR}"

# ---- zig 包装器 -------------------------------------------------------------
# FFmpeg configure 以不带引号的 $cc/$ar/... 展开调用工具，因此统一生成无空格
# 依赖的 POSIX 包装脚本；zig 0.13 提供 ar/ranlib/objcopy/rc，但不提供 nm 与
# strip：
#   * strip  —— 通过 --disable-stripping 彻底绕开；
#   * nm     —— configure 仅用 `nm -g` 探测符号修饰前缀 (extern_prefix)。
#               x86_64-windows-gnu 目标无下划线前缀，空输出即产生正确的
#               extern_prefix="" (已核实 FFmpeg 7.1 构建系统中 NM_CMD 不再被
#               任何 .mak/Makefile 消费)。
mkdir -p wrappers
WRAPPER_DIR="$(pwd)/wrappers"

make_wrapper() { # $1=name  $2...=argv
    local name="$1"; shift
    printf '#!/bin/sh\nexec %s "$@"\n' "$*" > "${WRAPPER_DIR}/${name}"
    chmod +x "${WRAPPER_DIR}/${name}"
}

# 编译器包装器需额外过滤 configure 为 mingw32 注入的
# `-Wl,--pic-executable,-e,mainCRTStartup`：zig 的 lld 驱动拒绝该参数
# ("unsupported linker arg")，导致 configure 的全部链接探测失败、
# HAVE_CBRT/HAVE_ROUND/... 被误判为 0，libm.h 的静态回退实现随即与
# zig 自带 mingw-w64 math.h 的声明冲突。本构建 --disable-programs 且
# 仅产出静态归档，可执行链接标志本就不会真正使用，剔除完全安全。
# (包装器由 Git Bash 的 sh=bash 执行，可安全使用数组语法。)
make_cc_wrapper() { # $1=name  $2=subcommand(cc|c++)
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

# 统一为带正斜杠的 Windows 路径 (msys sh 与原生进程均可直接 exec)
ZIG_WIN="${ZIG//\\//}"

make_cc_wrapper zcc   cc
make_cc_wrapper zcxx  c++
make_wrapper zar      "\"${ZIG_WIN}\" ar"
make_wrapper zranlib  "\"${ZIG_WIN}\" ranlib"
printf '#!/bin/sh\nexit 0\n' > "${WRAPPER_DIR}/znm"
chmod +x "${WRAPPER_DIR}/znm"

# ---- 手术刀级 ./configure ----------------------------------------------------
# 与 build_ffmpeg_static_msvc.sh 完全一致的裁剪清单；仅替换工具链相关部分：
#   --toolchain=msvc --target-os=win64   ->  --enable-cross-compile
#                                            --arch=x86_64 --target-os=mingw32
#   MSVC 专属 flags (-MD -O2 -Ob2 -Gy -Gw -Zc:inline -utf-8)
#                                        ->  clang/GNU 风格 -O2
#   新增 --disable-stripping (zig 0.13 无 strip 子命令)
"${SRC_DIR}/configure" \
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
    --enable-demuxer=mov,matroska,mpegts,flv,avi,aac,mp3,flac,ogg,wav,w64 \
    --enable-muxer=mp4,mov,matroska,webm,mpegts,flv,adts,flac,opus,wav,w64,null \
    --enable-bsf=h264_mp4toannexb,hevc_mp4toannexb,av1_metadata,extract_extradata,aac_adtstoasc,vp9_superframe,vp9_raw_reorder,null \
    --enable-parser=h264,hevc,av1,vp9,mpeg4video,aac,aac_latm,opus,mpegaudio,flac,ac3,vorbis \
    --enable-decoder=aac,aac_fixed,aac_latm,opus,mp3,mp3float,flac,ac3,eac3,vorbis,alac,pcm_s16le,pcm_s16be,pcm_s24le,pcm_s24be,pcm_s32le,pcm_f32le,pcm_f32be,pcm_f64le,pcm_alaw,pcm_mulaw \
    --enable-encoder=aac,opus,eac3,ac3,flac,alac,pcm_s16le,pcm_s24le,pcm_s32le,pcm_f32le \
    --extra-cflags="-O2 -D_WIN32_WINNT=0x0A00"

# ---- 路径归一化 -------------------------------------------------------------
# MSYS `pwd` 会以 POSIX 形态 (/c/...) 把源码树与包装器路径写入 Makefile 和
# ffbuild/config.mak；便携版原生 make.exe 会将其误解为驱动器相对路径
# (/c/x -> <当前驱动器>:\c\x) 而报 "No such file or directory"。
# 这里统一改写为 Windows 正斜杠形态 (C:/...)：原生 make.exe 的 include 与
# 依赖解析、以及经 sh.exe 执行的配方 (zig/nasm/install 均接受 C:/...) 全部兼容。
posix_to_win() {
    (cd "$1" && pwd -W 2>/dev/null) || cygpath -m "$1"
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

# ---- 线程与原子性熔断 -------------------------------------------------------
# configure 的任何链接探测失败都会让 FFmpeg 静默退化为 HAVE_THREADS 0 且回落到
# compat/atomics/dummy (原子操作变成非原子空实现)。zig 的 lld 拒绝 mingw32 的
# -Wl,--pic-executable 时正是如此 —— 上面的 zcc/zcxx 包装器已剔除该参数，但退化
# 本身是无声的，而 CineRTX 的 PipelineScheduler 会从多个线程调用 libav*，链接
# 这样的归档等于埋下数据竞争。因此在编译前硬性拒绝。
if ! grep -q '^#define HAVE_THREADS 1$' config.h; then
    echo "ERROR: FFmpeg configured itself with HAVE_THREADS 0."
    echo "       The static archives would be unusable from CineRTX's multi-threaded"
    echo "       scheduler. Inspect ffbuild/config.log for the failing link probe."
    exit 1
fi
if grep -q 'compat/atomics/dummy' ffbuild/config.mak; then
    echo "ERROR: FFmpeg fell back to compat/atomics/dummy (non-atomic no-op atomics)."
    echo "       Inspect ffbuild/config.log for the failing link probe."
    exit 1
fi
echo "[CineRTX-FFmpeg-Zig] Thread/atomic fuse OK: $(grep -o 'HAVE_[A-Z0-9]*THREADS 1' config.h | tr '\n' ' ')"

echo "[CineRTX-FFmpeg-Zig] Compiling surgical static archives (zig cc -target x86_64-windows-gnu)..."
"${MAKE_BIN}" -j"$(nproc)"
# V=1 is load-bearing, not verbosity preference.  In quiet mode FFmpeg's
# ffbuild/common.mak rewrites INSTALL into
#   @printf "INSTALL\t%s\n" $^; install -m 644 $^ "$dir"
# which duplicates the whole prerequisite list.  For install-libavutil-headers
# that is a single 11 KB recipe line, and make's `sh -c` invocation of it gets
# truncated mid-way -- install then receives a source header as its target and
# dies with "target '.../liba' is not a directory".  V=1 drops the printf
# wrapper, halving the line to ~5.6 KB, which passes intact.
"${MAKE_BIN}" V=1 install

# 清理 pkgconfig 与多余包装文件，确保产物目录绝对纯净
rm -rf "${PREFIX_DIR}/lib/pkgconfig" "${PREFIX_DIR}/share"

echo "[CineRTX-FFmpeg-Zig] Verifying static archive sizes (must NOT be ~130KB import stubs)..."
for name in avformat avcodec avutil swresample; do
    target_lib="${PREFIX_DIR}/lib/lib${name}.a"
    [[ -f "${target_lib}" ]] || { echo "ERROR: Missing ${target_lib}"; exit 1; }
    size_bytes=$(stat -c%s "${target_lib}")
    echo "  -> lib${name}.a: ${size_bytes} bytes"
done

# 导入桩熔断：真静态归档的 avformat/avcodec 必大于 450 KB
for lib in libavformat.a libavcodec.a; do
    size_bytes=$(stat -c%s "${PREFIX_DIR}/lib/${lib}")
    if [[ "${size_bytes}" -lt 450000 ]]; then
        echo "ERROR: ${lib} is only ${size_bytes} bytes (< 450 KB) - not a real static archive."
        exit 1
    fi
done

echo "[CineRTX-FFmpeg-Zig] Static libraries ready in ${PREFIX_DIR}/lib"
