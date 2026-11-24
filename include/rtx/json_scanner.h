// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Zero-dependency JSON scanner and deterministic serializer primitives.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "rtx/status.h"

namespace rtx::json {

std::string escapeJson(std::string_view input);
std::string jsonQuoted(std::string_view input);

void skipWs(std::string_view s, std::size_t& pos) noexcept;
Result<std::string_view> scanStringRaw(std::string_view s, std::size_t& pos);
Result<std::string> decodeString(std::string_view raw);
Result<std::string_view> scanValue(std::string_view s, std::size_t& pos);
Result<std::vector<std::pair<std::string, std::string_view>>> scanObject(std::string_view s, std::size_t& pos);
Result<std::vector<std::string_view>> scanArray(std::string_view s, std::size_t& pos);

const std::pair<std::string, std::string_view>* findMember(
    const std::vector<std::pair<std::string, std::string_view>>& members, std::string_view key) noexcept;
Result<std::string> memberString(
    const std::vector<std::pair<std::string, std::string_view>>& ms, std::string_view key, std::string_view default_val = {});
Result<int64_t> memberInt64(
    const std::vector<std::pair<std::string, std::string_view>>& ms, std::string_view key, int64_t default_val = 0);
Result<int> memberInt(
    const std::vector<std::pair<std::string, std::string_view>>& ms, std::string_view key, int default_val = 0);
Result<double> memberDouble(
    const std::vector<std::pair<std::string, std::string_view>>& ms, std::string_view key, double default_val = 0.0);
Result<bool> memberBool(
    const std::vector<std::pair<std::string, std::string_view>>& ms, std::string_view key, bool default_val = false);
Result<std::vector<std::string_view>> memberArray(
    const std::vector<std::pair<std::string, std::string_view>>& ms, std::string_view key);
Result<std::vector<std::pair<std::string, std::string_view>>> memberObject(
    const std::vector<std::pair<std::string, std::string_view>>& ms, std::string_view key);

}  // namespace rtx::json
