// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// In-memory AVIOContext wrapper (ACT-STATIC-03).
//
// The container layer must be verifiable without a single byte touching the
// filesystem: the hermetic test suite muxes a container into a std::vector,
// demuxes it back, and never creates a temporary file.  This class owns the
// libavformat AVIOContext plus its 32 KB I/O page and implements the read /
// write / seek callbacks over a span or a vector.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// Forward declaration only: the elaborated form below is used everywhere so the
// header stays valid whether or not <libavformat/avio.h> was included first.
struct AVIOContext;

namespace rtx::io {

class MemoryAvioContext final {
public:
    // 32 KB I/O page: one L2-friendly chunk per refill, matching libavformat's
    // default IO_BUFFER_SIZE class of workloads.
    static constexpr size_t kPageSize = 32768;

    // Read-only view over caller-owned bytes.  The span must outlive the
    // context; the demuxer keeps the source vector alive for its own lifetime.
    explicit MemoryAvioContext(std::span<const uint8_t> read_only_buf);

    // Append/overwrite sink.  The vector is resized on demand and the context
    // never takes ownership of it.
    explicit MemoryAvioContext(std::vector<uint8_t>* write_buf);

    ~MemoryAvioContext();

    MemoryAvioContext(const MemoryAvioContext&) = delete;
    MemoryAvioContext& operator=(const MemoryAvioContext&) = delete;
    MemoryAvioContext(MemoryAvioContext&&) = delete;
    MemoryAvioContext& operator=(MemoryAvioContext&&) = delete;

    [[nodiscard]] struct AVIOContext* raw() const noexcept { return avio_ctx_; }
    [[nodiscard]] bool valid() const noexcept { return avio_ctx_ != nullptr; }
    [[nodiscard]] bool writable() const noexcept { return is_writable_; }
    [[nodiscard]] size_t position() const noexcept { return pos_; }
    [[nodiscard]] size_t size() const noexcept;

    // Marks the stream as seekable (AVIO_SEEKABLE_NORMAL).  libavformat leaves
    // avio_alloc_context() contexts non-seekable by default, and the mov /
    // matroska muxers refuse to finalise a non-seekable target.
    void setSeekable(bool seekable) noexcept;

private:
    void initAvio(int write_flag);
    void releaseAvio() noexcept;

    static int readPacketCallback(void* opaque, uint8_t* buf, int buf_size) noexcept;
    static int writePacketCallback(void* opaque, const uint8_t* buf, int buf_size) noexcept;
    static int64_t seekCallback(void* opaque, int64_t offset, int whence) noexcept;

    std::span<const uint8_t> read_view_{};
    std::vector<uint8_t>* write_vec_{nullptr};
    size_t pos_{0};
    bool is_writable_{false};
    struct AVIOContext* avio_ctx_{nullptr};
};

}  // namespace rtx::io
