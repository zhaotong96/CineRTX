// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "memory_avio.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <memory>

extern "C" {
#include <libavformat/avio.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
}

namespace rtx::io {
namespace {

constexpr int kSeekMask = 0xFF;

void releaseAvioState(MemoryAvioState* state) noexcept {
    if (state == nullptr) return;
    auto& [rv, wv, pos, wr, avio] = *state;
    (void)rv; (void)wv; (void)pos; (void)wr;
    if (avio != nullptr) {
        av_freep(&avio->buffer);
        avio_context_free(&avio);
        avio = nullptr;
    }
    delete state;
}

size_t stateSize(const MemoryAvioState& state) noexcept {
    const auto& [rv, wv, pos, wr, avio] = state;
    (void)pos; (void)avio;
    return wr ? (wv != nullptr ? wv->size() : 0) : rv.size();
}

int readPacketCallback(void* opaque, uint8_t* buf, int buf_size) noexcept {
    auto* state = static_cast<MemoryAvioState*>(opaque);
    if (state == nullptr || buf == nullptr || buf_size <= 0) return AVERROR(EINVAL);
    auto& [rv, wv, pos, wr, avio] = *state;
    (void)avio;
    const std::span<const uint8_t> source = wr
        ? std::span<const uint8_t>(wv != nullptr ? wv->data() : nullptr, wv != nullptr ? wv->size() : 0)
        : rv;
    if (pos >= source.size()) return AVERROR_EOF;
    const size_t to_copy = std::min(source.size() - pos, static_cast<size_t>(buf_size));
    std::memcpy(buf, source.data() + pos, to_copy);
    pos += to_copy;
    return static_cast<int>(to_copy);
}

int writePacketCallback(void* opaque, const uint8_t* buf, int buf_size) noexcept {
    auto* state = static_cast<MemoryAvioState*>(opaque);
    if (state == nullptr || buf_size <= 0 || buf == nullptr) return AVERROR(EIO);
    auto& [rv, wv, pos, wr, avio] = *state;
    (void)rv; (void)avio;
    if (!wr || wv == nullptr) return AVERROR(EIO);
    const size_t end_pos = pos + static_cast<size_t>(buf_size);
    try {
        if (end_pos > wv->size()) wv->resize(end_pos);
    } catch (...) {
        return AVERROR(ENOMEM);
    }
    std::memcpy(wv->data() + pos, buf, static_cast<size_t>(buf_size));
    pos = end_pos;
    return buf_size;
}

int64_t seekCallback(void* opaque, int64_t offset, int whence) noexcept {
    auto* state = static_cast<MemoryAvioState*>(opaque);
    if (state == nullptr) return AVERROR(EINVAL);
    auto& [rv, wv, pos, wr, avio] = *state;
    (void)rv; (void)avio;
    const size_t total_size = stateSize(*state);
    if ((static_cast<uint64_t>(whence) & static_cast<uint64_t>(AVSEEK_SIZE)) != 0) {
        return static_cast<int64_t>(total_size);
    }
    int64_t target = 0;
    switch (static_cast<int>(whence) & kSeekMask) {
        case SEEK_SET: target = offset; break;
        case SEEK_CUR: target = static_cast<int64_t>(pos) + offset; break;
        case SEEK_END: target = static_cast<int64_t>(total_size) + offset; break;
        default: return AVERROR(EINVAL);
    }
    if (target < 0) return AVERROR(EINVAL);
    if (wr && wv != nullptr && static_cast<size_t>(target) > total_size) {
        try {
            wv->resize(static_cast<size_t>(target), 0);
        } catch (...) {
            return AVERROR(ENOMEM);
        }
    }
    pos = static_cast<size_t>(target);
    return target;
}

MemoryAvioContext createAvio(MemoryAvioState* raw_state, int write_flag) {
    MemoryAvioContext ctx(raw_state, &releaseAvioState);
    auto* io_buf = static_cast<uint8_t*>(av_malloc(kMemoryAvioPageSize));
    if (io_buf == nullptr) return ctx;
    auto io_buf_deleter = [](uint8_t* p) noexcept { av_free(p); };
    std::unique_ptr<uint8_t, decltype(io_buf_deleter)> io_buf_guard(io_buf, io_buf_deleter);
    std::memset(io_buf, 0, kMemoryAvioPageSize);
    struct AVIOContext* avio = avio_alloc_context(
        io_buf, static_cast<int>(kMemoryAvioPageSize), write_flag, raw_state,
        &readPacketCallback, write_flag != 0 ? &writePacketCallback : nullptr, &seekCallback);
    if (avio == nullptr) return ctx;
    io_buf_guard.release();
    avio->seekable = AVIO_SEEKABLE_NORMAL;
    avio->direct = 0;
    auto& [rv, wv, pos, wr, slot] = *raw_state;
    (void)rv; (void)wv; (void)pos; (void)wr;
    slot = avio;
    return ctx;
}

}  // namespace

MemoryAvioContext makeMemoryAvioRead(std::span<const uint8_t> read_only_buf) {
    return createAvio(new MemoryAvioState{read_only_buf, nullptr, 0, false, nullptr}, 0);
}

MemoryAvioContext makeMemoryAvioRead(std::span<const std::byte> read_only_buf) {
    return makeMemoryAvioRead(std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(read_only_buf.data()), read_only_buf.size()));
}

MemoryAvioContext makeMemoryAvioWrite(std::vector<uint8_t>* write_buf) {
    return createAvio(new MemoryAvioState{{}, write_buf, 0, true, nullptr}, 1);
}

struct AVIOContext* memoryAvioRaw(const MemoryAvioContext& ctx) noexcept {
    if (!ctx) return nullptr;
    const auto& [rv, wv, pos, wr, avio] = *ctx;
    (void)rv; (void)wv; (void)pos; (void)wr;
    return avio;
}

bool memoryAvioValid(const MemoryAvioContext& ctx) noexcept {
    return memoryAvioRaw(ctx) != nullptr;
}

bool memoryAvioWritable(const MemoryAvioContext& ctx) noexcept {
    if (!ctx) return false;
    const auto& [rv, wv, pos, wr, avio] = *ctx;
    (void)rv; (void)wv; (void)pos; (void)avio;
    return wr;
}

size_t memoryAvioPosition(const MemoryAvioContext& ctx) noexcept {
    if (!ctx) return 0;
    const auto& [rv, wv, pos, wr, avio] = *ctx;
    (void)rv; (void)wv; (void)wr; (void)avio;
    return pos;
}

size_t memoryAvioSize(const MemoryAvioContext& ctx) noexcept {
    return ctx ? stateSize(*ctx) : 0;
}

void memoryAvioSetSeekable(const MemoryAvioContext& ctx, bool seekable) noexcept {
    if (auto* avio = memoryAvioRaw(ctx); avio != nullptr) {
        avio->seekable = seekable ? AVIO_SEEKABLE_NORMAL : 0;
    }
}

}  // namespace rtx::io
