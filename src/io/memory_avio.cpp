// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "memory_avio.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

extern "C" {
#include <libavformat/avio.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
}

namespace rtx::io {
namespace {

// AVSEEK_SIZE is a bit flag in the high bits of `whence`; libavformat may
// additionally pass AVSEEK_FORCE, which is irrelevant for a memory sink.  The
// flag must be tested before masking, otherwise a size probe is mistaken for
// SEEK_SET(0) and avio_size() silently reports zero.
constexpr int kSeekMask = 0xFF;

}  // namespace

MemoryAvioContext::MemoryAvioContext(std::span<const uint8_t> read_only_buf)
    : read_view_(read_only_buf), is_writable_(false) {
    initAvio(0);
}

MemoryAvioContext::MemoryAvioContext(std::vector<uint8_t>* write_buf)
    : write_vec_(write_buf), is_writable_(true) {
    initAvio(1);
}

MemoryAvioContext::~MemoryAvioContext() { releaseAvio(); }

size_t MemoryAvioContext::size() const noexcept {
    if (is_writable_) {
        return write_vec_ != nullptr ? write_vec_->size() : 0;
    }
    return read_view_.size();
}

void MemoryAvioContext::setSeekable(bool seekable) noexcept {
    if (avio_ctx_ != nullptr) {
        avio_ctx_->seekable = seekable ? AVIO_SEEKABLE_NORMAL : 0;
    }
}

void MemoryAvioContext::initAvio(int write_flag) {
    auto* io_buf = static_cast<uint8_t*>(av_malloc(kPageSize));
    if (io_buf == nullptr) {
        return;
    }
    std::memset(io_buf, 0, kPageSize);
    avio_ctx_ = avio_alloc_context(
        io_buf, static_cast<int>(kPageSize), write_flag, this,
        &MemoryAvioContext::readPacketCallback,
        write_flag != 0 ? &MemoryAvioContext::writePacketCallback : nullptr,
        &MemoryAvioContext::seekCallback);
    if (avio_ctx_ == nullptr) {
        av_free(io_buf);
        return;
    }
    // libavformat defaults to non-seekable for caller-supplied contexts.  Both
    // the mov and the matroska muxers need a seekable target to rewrite their
    // index, and a memory sink always is one.
    avio_ctx_->seekable = AVIO_SEEKABLE_NORMAL;
    avio_ctx_->direct = 0;
}

void MemoryAvioContext::releaseAvio() noexcept {
    if (avio_ctx_ != nullptr) {
        // avio_context_free() releases the AVIOContext but not the caller's
        // buffer; both must be released here, in this order.
        av_freep(&avio_ctx_->buffer);
        avio_context_free(&avio_ctx_);
        avio_ctx_ = nullptr;
    }
}

int MemoryAvioContext::readPacketCallback(void* opaque, uint8_t* buf, int buf_size) noexcept {
    auto* self = static_cast<MemoryAvioContext*>(opaque);
    if (self == nullptr || buf == nullptr || buf_size <= 0) {
        return AVERROR(EINVAL);
    }
    const std::span<const uint8_t> source = self->is_writable_
                                                ? std::span<const uint8_t>(
                                                      self->write_vec_ != nullptr
                                                          ? self->write_vec_->data()
                                                          : nullptr,
                                                      self->write_vec_ != nullptr
                                                          ? self->write_vec_->size()
                                                          : 0)
                                                : self->read_view_;
    if (self->pos_ >= source.size()) {
        return AVERROR_EOF;
    }
    const size_t to_copy =
        std::min(source.size() - self->pos_, static_cast<size_t>(buf_size));
    std::memcpy(buf, source.data() + self->pos_, to_copy);
    self->pos_ += to_copy;
    return static_cast<int>(to_copy);
}

int MemoryAvioContext::writePacketCallback(void* opaque, const uint8_t* buf, int buf_size) noexcept {
    auto* self = static_cast<MemoryAvioContext*>(opaque);
    if (self == nullptr || !self->is_writable_ || self->write_vec_ == nullptr ||
        buf_size <= 0 || buf == nullptr) {
        return AVERROR(EIO);
    }
    const size_t end_pos = self->pos_ + static_cast<size_t>(buf_size);
    if (end_pos > self->write_vec_->size()) {
        self->write_vec_->resize(end_pos);
    }
    std::memcpy(self->write_vec_->data() + self->pos_, buf, static_cast<size_t>(buf_size));
    self->pos_ = end_pos;
    return buf_size;
}

int64_t MemoryAvioContext::seekCallback(void* opaque, int64_t offset, int whence) noexcept {
    auto* self = static_cast<MemoryAvioContext*>(opaque);
    if (self == nullptr) {
        return AVERROR(EINVAL);
    }
    const size_t total_size = self->size();
    if ((static_cast<uint64_t>(whence) & static_cast<uint64_t>(AVSEEK_SIZE)) != 0) {
        return static_cast<int64_t>(total_size);
    }
    const int mode = static_cast<int>(whence) & kSeekMask;
    int64_t target = 0;
    switch (mode) {
        case SEEK_SET:
            target = offset;
            break;
        case SEEK_CUR:
            target = static_cast<int64_t>(self->pos_) + offset;
            break;
        case SEEK_END:
            target = static_cast<int64_t>(total_size) + offset;
            break;
        default:
            return AVERROR(EINVAL);
    }
    if (target < 0) {
        return AVERROR(EINVAL);
    }
    if (self->is_writable_ && self->write_vec_ != nullptr &&
        static_cast<size_t>(target) > total_size) {
        // Sparse extension: a muxer may seek past the current tail before the
        // next write (moov/mdat back-patching), and the gap must read as zero.
        self->write_vec_->resize(static_cast<size_t>(target), 0);
    }
    self->pos_ = static_cast<size_t>(target);
    return target;
}

}  // namespace rtx::io
