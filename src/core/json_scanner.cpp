// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "rtx/json_scanner.h"

#include <charconv>
#include "rtx/string_utils.h"
#include <cstdlib>
#include <format>

namespace rtx::json {

std::string escapeJson(std::string_view input) {
    return rtx::str::escapeJson(input);
}
std::string jsonQuoted(std::string_view input) {
    return "\"" + escapeJson(input) + "\"";
}
void skipWs(std::string_view s, std::size_t& pos) noexcept {
    while (pos < s.size()) {
        const char c = s[pos];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            ++pos;
        } else {
            break;
        }
    }
}
Result<std::string_view> scanStringRaw(std::string_view s, std::size_t& pos) {
    skipWs(s, pos);
    if (pos >= s.size() || s[pos] != '"') {
        return err(ErrorCode::InvalidArgument, "expected '\"' at start of JSON string");
    }
    const std::size_t start = pos;
    ++pos;
    while (pos < s.size()) {
        const char c = s[pos];
        if (c == '"') {
            ++pos;
            return s.substr(start, pos - start);
        }
        if (c == '\\') {
            ++pos;
            if (pos >= s.size()) {
                return err(ErrorCode::InvalidArgument, "truncated escape sequence in JSON string");
            }
            ++pos;
            continue;
        }
        if (static_cast<unsigned char>(c) < 0x20) {
            return err(ErrorCode::InvalidArgument, "unescaped control character in JSON string");
        }
        ++pos;
    }
    return err(ErrorCode::InvalidArgument, "unterminated JSON string");
}
static void appendUtf8(std::string& out, uint32_t cp) {
    if (cp <= 0x7Fu) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FFu) {
        out.push_back(static_cast<char>(0xC0u | (cp >> 6u)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else if (cp <= 0xFFFFu) {
        out.push_back(static_cast<char>(0xE0u | (cp >> 12u)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6u) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else if (cp <= 0x10FFFFu) {
        out.push_back(static_cast<char>(0xF0u | (cp >> 18u)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 12u) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6u) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }
}
static int parseHex4(std::string_view s) {
    if (s.size() < 4) return -1;
    int v = 0;
    for (int i = 0; i < 4; ++i) {
        const char c = s[static_cast<std::size_t>(i)];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= (c - '0');
        else if (c >= 'a' && c <= 'f') v |= (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (c - 'A' + 10);
        else return -1;
    }
    return v;
}
Result<std::string> decodeString(std::string_view raw) {
    if (raw.size() < 2 || raw.front() != '"' || raw.back() != '"') {
        return err(ErrorCode::InvalidArgument, "malformed quoted JSON string");
    }
    std::string out;
    out.reserve(raw.size() - 2);
    for (std::size_t i = 1; i + 1 < raw.size(); ++i) {
        const char c = raw[i];
        if (c != '\\') {
            out.push_back(c);
            continue;
        }
        if (++i >= raw.size() - 1) {
            return err(ErrorCode::InvalidArgument, "truncated JSON escape");
        }
        switch (raw[i]) {
            case '"':  out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/':  out.push_back('/'); break;
            case 'b':  out.push_back('\b'); break;
            case 'f':  out.push_back('\f'); break;
            case 'n':  out.push_back('\n'); break;
            case 'r':  out.push_back('\r'); break;
            case 't':  out.push_back('\t'); break;
            case 'u': {
                if (i + 4 >= raw.size() - 1) {
                    return err(ErrorCode::InvalidArgument, "truncated \\u escape in JSON string");
                }
                int cp = parseHex4(raw.substr(i + 1, 4));
                if (cp < 0) return err(ErrorCode::InvalidArgument, "invalid hex in \\u escape");
                i += 4;
                // High surrogate, check for low surrogate
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    if (i + 6 < raw.size() && raw[i + 1] == '\\' && raw[i + 2] == 'u') {
                        int low = parseHex4(raw.substr(i + 3, 4));
                        if (low >= 0xDC00 && low <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                            i += 6;
                        }
                    }
                }
                // 孤立代理项不是合法 UTF-8：降级为 U+FFFD（替换字符），
                // 与私版扫描器的语义一致，绝不输出无效字节序列。
                if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;
                appendUtf8(out, static_cast<uint32_t>(cp));
                break;
            }
            default:
                return err(ErrorCode::InvalidArgument, "unknown JSON escape");
        }
    }
    return out;
}
Result<std::string_view> scanValue(std::string_view s, std::size_t& pos) {
    skipWs(s, pos);
    if (pos >= s.size()) {
        return err(ErrorCode::InvalidArgument, "JSON value expected");
    }
    const std::size_t start = pos;
    const char c = s[pos];
    if (c == '"') {
        return scanStringRaw(s, pos);
    }
    if (c == '{' || c == '[') {
        const char open_bracket = c;
        const char close_bracket = (open_bracket == '{') ? '}' : ']';
        int depth = 0;
        bool in_str = false;
        while (pos < s.size()) {
            const char ch = s[pos];
            if (in_str) {
                if (ch == '\\' && pos + 1 < s.size()) {
                    pos += 2;
                    continue;
                }
                if (ch == '"') {
                    in_str = false;
                }
                ++pos;
                continue;
            }
            if (ch == '"') {
                in_str = true;
                ++pos;
                continue;
            }
            if (ch == open_bracket) {
                ++depth;
            } else if (ch == close_bracket) {
                --depth;
                if (depth == 0) {
                    ++pos;
                    return s.substr(start, pos - start);
                }
            }
            ++pos;
        }
        return err(ErrorCode::InvalidArgument, "unterminated JSON object or array");
    }
    while (pos < s.size()) {
        const char ch = s[pos];
        if (ch == ',' || ch == '}' || ch == ']' || ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            break;
        }
        ++pos;
    }
// Primitive token (number, boolean, null)
    if (pos == start) {
        return err(ErrorCode::InvalidArgument, "JSON value expected");
    }
    return s.substr(start, pos - start);
}
Result<std::vector<std::pair<std::string, std::string_view>>> scanObject(std::string_view s, std::size_t& pos) {
    skipWs(s, pos);
    if (pos >= s.size() || s[pos] != '{') {
        return err(ErrorCode::InvalidArgument, "JSON object expected ('{')");
    }
    ++pos;
    std::vector<std::pair<std::string, std::string_view>> members;
    skipWs(s, pos);
    if (pos < s.size() && s[pos] == '}') {
        ++pos;
        return members;
    }
    while (true) {
        skipWs(s, pos);
        if (pos >= s.size() || s[pos] != '"') {
            return err(ErrorCode::InvalidArgument, "JSON key expected");
        }
        auto raw_key = scanStringRaw(s, pos);
        if (!raw_key) return std::unexpected(raw_key.error());
        auto key = decodeString(*raw_key);
        if (!key) return std::unexpected(key.error());

        skipWs(s, pos);
        if (pos >= s.size() || s[pos] != ':') {
            return err(ErrorCode::InvalidArgument, std::format("expected ':' after JSON key \"{}\"", *key));
        }
        ++pos;

        auto value = scanValue(s, pos);
        if (!value) return std::unexpected(value.error());

        for (const auto& m : members) {
            if (m.first == *key) {
                return err(ErrorCode::InvalidArgument, std::format("duplicate JSON key \"{}\"", *key));
            }
        }
        members.push_back(std::pair<std::string, std::string_view>{std::move(*key), *value});

        skipWs(s, pos);
        if (pos < s.size() && s[pos] == ',') {
            ++pos;
            continue;
        }
        if (pos < s.size() && s[pos] == '}') {
            ++pos;
            return members;
        }
        return err(ErrorCode::InvalidArgument, "expected ',' or '}' in JSON object");
    }
}
Result<std::vector<std::string_view>> scanArray(std::string_view s, std::size_t& pos) {
    skipWs(s, pos);
    if (pos >= s.size() || s[pos] != '[') {
        return err(ErrorCode::InvalidArgument, "JSON array expected ('[')");
    }
    ++pos;
    std::vector<std::string_view> items;
    skipWs(s, pos);
    if (pos < s.size() && s[pos] == ']') {
        ++pos;
        return items;
    }
    while (true) {
        auto val = scanValue(s, pos);
        if (!val) return std::unexpected(val.error());
        items.push_back(*val);

        skipWs(s, pos);
        if (pos < s.size() && s[pos] == ',') {
            ++pos;
            continue;
        }
        if (pos < s.size() && s[pos] == ']') {
            ++pos;
            return items;
        }
        return err(ErrorCode::InvalidArgument, "expected ',' or ']' in JSON array");
    }
}
const std::pair<std::string, std::string_view>* findMember(const std::vector<std::pair<std::string, std::string_view>>& members, std::string_view key) noexcept {
    for (const auto& m : members) {
        if (m.first == key) return &m;
    }
    return nullptr;
}
Result<std::string> memberString(const std::vector<std::pair<std::string, std::string_view>>& ms, std::string_view key, std::string_view default_val) {
    const std::pair<std::string, std::string_view>* m = findMember(ms, key);
    if (m == nullptr) return std::string(default_val);
    if (m->second == "null") return std::string(default_val);
    return decodeString(m->second);
}
Result<int64_t> memberInt64(const std::vector<std::pair<std::string, std::string_view>>& ms, std::string_view key, int64_t default_val) {
    const std::pair<std::string, std::string_view>* m = findMember(ms, key);
    if (m == nullptr) return default_val;
    if (m->second == "null") return default_val;
    int64_t val = 0;
    const auto [ptr, ec] = std::from_chars(m->second.data(), m->second.data() + m->second.size(), val);
    if (ec != std::errc{} || ptr != m->second.data() + m->second.size()) {
        return err(ErrorCode::InvalidArgument, std::format("malformed integer in JSON key \"{}\"", key));
    }
    return val;
}
Result<int> memberInt(const std::vector<std::pair<std::string, std::string_view>>& ms, std::string_view key, int default_val) {
    const auto r = memberInt64(ms, key, default_val);
    if (!r) return std::unexpected(r.error());
    return static_cast<int>(*r);
}
Result<double> memberDouble(const std::vector<std::pair<std::string, std::string_view>>& ms, std::string_view key, double default_val) {
    const std::pair<std::string, std::string_view>* m = findMember(ms, key);
    if (m == nullptr) return default_val;
    if (m->second == "null") return default_val;
    char* end = nullptr;
    std::string buf(m->second);
    const double val = std::strtod(buf.c_str(), &end);
    if (end == buf.c_str() || end != buf.c_str() + buf.size()) {
        return err(ErrorCode::InvalidArgument, std::format("malformed float in JSON key \"{}\"", key));
    }
    return val;
}
Result<bool> memberBool(const std::vector<std::pair<std::string, std::string_view>>& ms, std::string_view key, bool default_val) {
    const std::pair<std::string, std::string_view>* m = findMember(ms, key);
    if (m == nullptr) return default_val;
    if (m->second == "true") return true;
    if (m->second == "false") return false;
    return err(ErrorCode::InvalidArgument, std::format("expected boolean for JSON key \"{}\"", key));
}
Result<std::vector<std::string_view>> memberArray(const std::vector<std::pair<std::string, std::string_view>>& ms, std::string_view key) {
    const std::pair<std::string, std::string_view>* m = findMember(ms, key);
    if (m == nullptr) return std::vector<std::string_view>{};
    std::size_t pos = 0;
    auto items = scanArray(m->second, pos);
    if (!items) return std::unexpected(items.error());
    skipWs(m->second, pos);
    if (pos != m->second.size()) {
        return err(ErrorCode::InvalidArgument, std::format("trailing content in JSON array \"{}\"", key));
    }
    return *items;
}
Result<std::vector<std::pair<std::string, std::string_view>>> memberObject(const std::vector<std::pair<std::string, std::string_view>>& ms, std::string_view key) {
    const std::pair<std::string, std::string_view>* m = findMember(ms, key);
    if (m == nullptr) return std::vector<std::pair<std::string, std::string_view>>{};
    std::size_t pos = 0;
    auto obj = scanObject(m->second, pos);
    if (!obj) return std::unexpected(obj.error());
    skipWs(m->second, pos);
    if (pos != m->second.size()) {
        return err(ErrorCode::InvalidArgument, std::format("trailing content in JSON object \"{}\"", key));
    }
    return *obj;
}
}  // namespace rtx::json
