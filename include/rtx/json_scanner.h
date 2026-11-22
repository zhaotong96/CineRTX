// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Zero-dependency JSON scanner and deterministic serializer primitives.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "rtx/status.h"

namespace rtx::json {

// @rtx-type-justification
// Category: Domain-Model
// Why-Std-Fails: JSON 键值对解析领域模型：保留键名拷贝与值字符串切片引用（零拷贝视图），std::pair 无法直观区分 key 与 raw_value 视图语义。
// Invariant: value 为源 JSON 文本的有效子串视图，其生命周期不长于源字符串。
struct JsonMember {
    std::string key;
    std::string_view value;
};

// String escaping & quoting for deterministic serialization
std::string escapeJson(std::string_view input);
std::string jsonQuoted(std::string_view input);

// Low-level tokenizers & scanners
void skipWs(std::string_view s, std::size_t& pos) noexcept;
Result<std::string_view> scanStringRaw(std::string_view s, std::size_t& pos);
Result<std::string> decodeString(std::string_view raw);
Result<std::string_view> scanValue(std::string_view s, std::size_t& pos);
Result<std::vector<JsonMember>> scanObject(std::string_view s, std::size_t& pos);
Result<std::vector<std::string_view>> scanArray(std::string_view s, std::size_t& pos);

// Member lookup and typed extraction
const JsonMember* findMember(const std::vector<JsonMember>& members, std::string_view key) noexcept;
Result<std::string> memberString(const std::vector<JsonMember>& ms, std::string_view key, std::string_view default_val = {});
Result<int64_t> memberInt64(const std::vector<JsonMember>& ms, std::string_view key, int64_t default_val = 0);
Result<int> memberInt(const std::vector<JsonMember>& ms, std::string_view key, int default_val = 0);
Result<double> memberDouble(const std::vector<JsonMember>& ms, std::string_view key, double default_val = 0.0);
Result<bool> memberBool(const std::vector<JsonMember>& ms, std::string_view key, bool default_val = false);
Result<std::vector<std::string_view>> memberArray(const std::vector<JsonMember>& ms, std::string_view key);
Result<std::vector<JsonMember>> memberObject(const std::vector<JsonMember>& ms, std::string_view key);

}  // namespace rtx::json
