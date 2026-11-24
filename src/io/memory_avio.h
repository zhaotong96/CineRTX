// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// In-memory AVIOContext wrapper (ACT-STATIC-03 / Wave 12.2-D).
// Owns libavformat AVIOContext plus its 32 KB I/O page via std::shared_ptr custom deleter.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <tuple>
#include <vector>

struct AVIOContext;

namespace rtx::io {

inline constexpr size_t kMemoryAvioPageSize = 32768;

// Layout: (read_view, write_vec, pos, is_writable, avio_ctx).
using MemoryAvioState = std::tuple<
    std::span<const uint8_t>,
    std::vector<uint8_t>*,
    size_t,
    bool,
    struct AVIOContext*>;

// Phase 12 (Wave 12.2-D): std::shared_ptr<MemoryAvioState> with custom deleter replaces MemoryAvioContext class.
using MemoryAvioContext = std::shared_ptr<MemoryAvioState>;

[[nodiscard]] MemoryAvioContext makeMemoryAvioRead(std::span<const uint8_t> read_only_buf);
[[nodiscard]] MemoryAvioContext makeMemoryAvioRead(std::span<const std::byte> read_only_buf);
[[nodiscard]] MemoryAvioContext makeMemoryAvioWrite(std::vector<uint8_t>* write_buf);

[[nodiscard]] struct AVIOContext* memoryAvioRaw(const MemoryAvioContext& ctx) noexcept;
[[nodiscard]] bool memoryAvioValid(const MemoryAvioContext& ctx) noexcept;
[[nodiscard]] bool memoryAvioWritable(const MemoryAvioContext& ctx) noexcept;
[[nodiscard]] size_t memoryAvioPosition(const MemoryAvioContext& ctx) noexcept;
[[nodiscard]] size_t memoryAvioSize(const MemoryAvioContext& ctx) noexcept;
void memoryAvioSetSeekable(const MemoryAvioContext& ctx, bool seekable) noexcept;

}  // namespace rtx::io
