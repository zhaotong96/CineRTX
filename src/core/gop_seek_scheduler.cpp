// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
// Track 11C-4 / Phase 12 Wave 12.2-C: GOP-Aware 帧精确 Seek 调度器自由函数。

#include "rtx/playback_session.h"

#include "rtx/flicks.h"
#include "rtx/rational.h"

namespace rtx {

GopSeekScheduler makeGopSeekScheduler(FfmpegDemuxer& demuxer, Rational video_time_base) {
    return {[&demuxer](int64_t pts) { return demuxer.seekToKeyframeBefore(pts); },
            video_time_base, 0, 0u, false};
}

GopSeekScheduler makeGopSeekScheduler(std::function<Result<void>(int64_t)> seek_fn,
                                      Rational video_time_base) noexcept {
    return {std::move(seek_fn), video_time_base, 0, 0u, false};
}

Result<void> gopSeekTo(GopSeekScheduler& sched, int64_t target_flicks) {
    auto& [seek_fn, time_base, target, skipped, active] = sched;
    if (!time_base.valid() || !seek_fn) {
        return err(ErrorCode::InvalidArgument, "gop seek: invalid video time base");
    }
    const int64_t stream_pts = rescaleQ(target_flicks, kTimeBaseFlicks, time_base, Rounding::Down);
    Result<void> seeked = seek_fn(stream_pts);
    if (!seeked) return seeked;
    target = target_flicks;
    skipped = 0;
    active = true;
    return {};
}

bool gopSeekAccepting(GopSeekScheduler& sched, int64_t frame_flicks) noexcept {
    auto& [seek_fn, time_base, target, skipped, active] = sched;
    (void)seek_fn; (void)time_base;
    if (!active) return true;
    if (frame_flicks >= target) {
        active = false;
        return true;
    }
    ++skipped;
    return false;
}

void gopSeekCancel(GopSeekScheduler& sched) noexcept {
    auto& [seek_fn, time_base, target, skipped, active] = sched;
    (void)seek_fn; (void)time_base; (void)target;
    active = false;
    skipped = 0;
}

}  // namespace rtx
