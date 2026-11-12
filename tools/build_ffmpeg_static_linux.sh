#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
# Copyright (C) 2026 RTX Transcode Engine contributors
#
# Phase 9 (Task 9-B2): Linux static FFmpeg build for the cloud-native image.
# Same surgical whitelist as the Windows scripts plus the Phase 8 broadcast
# additions (mxf demuxer, prores/dnxhd/dvvideo decoders); the archives land
# in the CMake-resolved layout dist/include + dist/lib (libav*.a).
#
# Usage: tools/build_ffmpeg_static_linux.sh [ffmpeg_source_dir] [dist_dir] [build_dir]
set -euo pipefail

SRC_DIR="${1:-third_party/ffmpeg}"
DIST_DIR="${2:-build/external/ffmpeg-static}"
BUILD_DIR="${3:-build/external/ffmpeg-build}"

if [ ! -f "${SRC_DIR}/configure" ]; then
    echo "[CineRTX-FFmpeg] FFmpeg source tree not found at ${SRC_DIR}" >&2
    exit 1
fi

mkdir -p "${DIST_DIR}/include" "${DIST_DIR}/lib" "${BUILD_DIR}"

cd "${SRC_DIR}"
./configure \
    --prefix="${DIST_DIR}" \
    --disable-everything \
    --disable-shared \
    --enable-static \
    --disable-programs \
    --disable-doc \
    --disable-debug \
    --disable-network \
    --enable-avformat \
    --enable-avcodec \
    --enable-avutil \
    --enable-swresample \
    --enable-protocol=file,pipe,data \
    --enable-demuxer=mov,matroska,mpegts,flv,avi,mxf,aac,mp3,flac,ogg,wav,w64 \
    --enable-muxer=mp4,mov,matroska,webm,mpegts,flv,adts,flac,opus,wav,w64,null \
    --enable-bsf=h264_mp4toannexb,hevc_mp4toannexb,av1_metadata,extract_extradata,aac_adtstoasc,vp9_superframe,vp9_raw_reorder,null \
    --enable-parser=h264,hevc,av1,vp9,mpeg4video,aac,aac_latm,opus,mpegaudio,flac,ac3,vorbis \
    --enable-decoder=prores,dnxhd,dvvideo,aac,aac_fixed,aac_latm,opus,mp3,mp3float,flac,ac3,eac3,vorbis,alac,pcm_s16le,pcm_s16be,pcm_s24le,pcm_s24be,pcm_s32le,pcm_f32le,pcm_f32be,pcm_f64le,pcm_alaw,pcm_mulaw \
    --enable-encoder=aac,opus,eac3,ac3,flac,alac,pcm_s16le,pcm_s24le,pcm_s32le,pcm_f32le \
    --enable-filter=aresample,null \
    --extra-cflags="-O2 -fPIC" \
    --extra-ldflags="-fPIC"

make -j"$(nproc)"
make install

echo "[CineRTX-FFmpeg] Linux static dist ready at ${DIST_DIR}"
