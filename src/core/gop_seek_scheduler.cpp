// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Track 11C-4：GOP-Aware 帧精确 Seek 调度器。
//
// 参照 NVIDIA Video Codec SDK 13.1 的 GOP-Aware Seek 范式，但完全建立在现有
// IDemuxer + NVDEC 之上，不依赖 R610 驱动。五步契约：
//   1. 定位目标之前最近的 IDR（兼容 Open GOP）；
//   2. Seek 解复用器，调用方随后向 NVDEC 下发 Flush / Discontinuity；
//   3. 解码参考帧恢复完整参考状态；
//   4. 非目标中间帧跳过显存映射与后处理，立即归还解码槽位；
//   5. frame_flicks >= target_flicks 命中后才映射与呈现。

#include "rtx/playback_session.h"

#include "rtx/flicks.h"
#include "rtx/rational.h"

namespace rtx {

GopSeekScheduler::GopSeekScheduler(IDemuxer& demuxer, Rational video_time_base) noexcept
    : demuxer_(&demuxer), time_base_(video_time_base) {}

Result<void> GopSeekScheduler::seekTo(int64_t target_flicks) {
    if (!time_base_.valid()) {
        return err(ErrorCode::InvalidArgument, "gop seek: invalid video time base");
    }
// 向下取整保证 seek 点绝不越过目标帧，Open GOP 的前向参考帧因此全部落在
// preroll 区间内被解码但不呈现。
    const int64_t stream_pts =
        rescaleQ(target_flicks, kTimeBaseFlicks, time_base_, Rounding::Down);
    Result<void> seeked = demuxer_->seekToKeyframeBefore(stream_pts);
    if (!seeked) {
        return seeked;
    }
    target_ = target_flicks;
    skipped_ = 0;
    active_ = true;
    return {};
}
bool GopSeekScheduler::accepting(int64_t frame_flicks) noexcept {
    if (!active_) {
        return true;
    }
    if (frame_flicks >= target_) {
        active_ = false;
        return true;
    }
    ++skipped_;
    return false;
}
void GopSeekScheduler::cancel() noexcept {
    active_ = false;
    skipped_ = 0;
}
}  // namespace rtx
