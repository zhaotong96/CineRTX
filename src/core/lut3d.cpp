// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Adobe Iridas .cube 3D LUT parser.  See include/rtx/lut3d.h for the contract.
// The implementation is allocation-frugal and rejects malformed files with a
// structured Status instead of throwing; the FP16 conversion rounds to
// nearest-even exactly like the GPU's __float2half so the identity-accuracy
// test can compare against a CPU reference without drift.

#include "rtx/lut3d.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace rtx {

float halfToFloat(uint16_t value) noexcept {
    const uint32_t sign = static_cast<uint32_t>(value & 0x8000u) << 16;
    uint32_t exponent = (value >> 10) & 0x1Fu;
    uint32_t mantissa = value & 0x3FFu;
    uint32_t bits = 0;
    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign;
        } else {
            exponent = 127 - 15 + 1;
            while ((mantissa & 0x400u) == 0) {
                mantissa <<= 1;
                --exponent;
            }
            mantissa &= 0x3FFu;
            bits = sign | (exponent << 23) | (mantissa << 13);
        }
    } else if (exponent == 0x1F) {
        bits = sign | 0x7F800000u | (mantissa << 13);
    } else {
        bits = sign | ((exponent + 127 - 15) << 23) | (mantissa << 13);
    }
    float result = 0.0f;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

uint16_t floatToHalf(float value) noexcept {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const uint32_t sign = (bits >> 16) & 0x8000u;
    const int32_t exponent = static_cast<int32_t>((bits >> 23) & 0xFFu) - 127;
    uint32_t mantissa = bits & 0x7FFFFFu;

    if (exponent > 15) {
        if (exponent == 128 && mantissa != 0) {
            return static_cast<uint16_t>(sign | 0x7E00u | (mantissa >> 13));
        }
        return static_cast<uint16_t>(sign | 0x7C00u);
    }
    if (exponent < -24) {
        return static_cast<uint16_t>(sign);
    }
    if (exponent < -14) {
        mantissa |= 0x800000u;
        const int shift = -exponent - 14;
        const uint32_t half_mantissa = mantissa >> (13 + shift);
        return static_cast<uint16_t>(sign | half_mantissa);
    }
    const uint32_t half_exponent = static_cast<uint32_t>(exponent + 15) << 10;
    const uint32_t half_mantissa = mantissa >> 13;
    const uint32_t round_bit = (mantissa >> 12) & 1u;
    const uint32_t sticky_bits = mantissa & 0xFFFu;
    const uint32_t round_add = (round_bit != 0 && (sticky_bits != 0 || (half_mantissa & 1u) != 0)) ? 1u : 0u;
    return static_cast<uint16_t>(sign | ((half_exponent | half_mantissa) + round_add));
}

namespace {

bool parseIntegerToken(const std::string& token, int& value) {
    const char* begin = token.c_str();
    const char* end = begin + token.size();
    const auto result = std::from_chars(begin, end, value);
    return result.ec == std::errc{} && result.ptr == end;
}

bool parseFloatToken(const std::string& token, float& value) {
    try {
        size_t consumed = 0;
        value = std::stof(token, &consumed);
        return consumed == token.size();
    } catch (const std::exception&) {
        return false;
    }
}

}  // namespace

Result<Lut3D> Lut3D::parse(const std::string& text) {
    int grid_size = 0;
    std::vector<float> table;  // RGB rows in .cube order
    bool header_done = false;
    std::istringstream stream(text);
    std::string line;
    int line_no = 0;
    while (std::getline(stream, line)) {
        ++line_no;
        // Strip a UTF-8 BOM on the first line.
        if (line_no == 1 && line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF) {
            line.erase(0, 3);
        }
        const size_t comment = line.find('#');
        if (comment != std::string::npos) {
            line.erase(comment);
        }
        if (line.find_first_not_of(" \t\r\n") == std::string::npos) {
            continue;
        }
        std::istringstream tokens(line);
        std::string keyword;
        tokens >> keyword;
        if (keyword == "TITLE" || keyword == "DOMAIN_MIN" || keyword == "DOMAIN_MAX" ||
            keyword == "LUT_3D_INPUT_RANGE" || keyword == "LUT_1D_INPUT_RANGE") {
            continue;  // informational
        }
        if (keyword == "LUT_3D_SIZE") {
            std::string value_token;
            tokens >> value_token;
            if (!parseIntegerToken(value_token, grid_size) || grid_size < 2 ||
                grid_size > 256) {
                return err(ErrorCode::InvalidArgument,
                           "LUT_3D_SIZE must be an integer in 2..256 (line " +
                               std::to_string(line_no) + ")");
            }
            table.reserve(static_cast<size_t>(grid_size) * grid_size * grid_size * 3u);
            header_done = true;
            continue;
        }
        if (keyword == "LUT_1D_SIZE") {
            return err(ErrorCode::Unsupported, "1D (.cube) LUT files are not supported");
        }
        if (!header_done) {
            return err(ErrorCode::InvalidArgument,
                       ".cube data rows appeared before LUT_3D_SIZE (line " +
                           std::to_string(line_no) + ")");
        }
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        // A data row starts with its red value, which `keyword` already
        // consumed from the stream; keep it as the first token.
        const std::string token_r = keyword;
        std::string token_g;
        std::string token_b;
        tokens >> token_g >> token_b;
        if (!parseFloatToken(token_r, r) || !parseFloatToken(token_g, g) ||
            !parseFloatToken(token_b, b)) {
            return err(ErrorCode::InvalidArgument,
                       "malformed .cube data row (line " + std::to_string(line_no) + ")");
        }
        table.push_back(r);
        table.push_back(g);
        table.push_back(b);
    }
    if (grid_size == 0) {
        return err(ErrorCode::InvalidArgument, ".cube file has no LUT_3D_SIZE header");
    }
    const size_t expected = static_cast<size_t>(grid_size) * grid_size * grid_size;
    if (table.size() != expected * 3u) {
        return err(ErrorCode::InvalidArgument,
                   ".cube data row count mismatch: expected " +
                       std::to_string(expected) + ", got " +
                       std::to_string(table.size() / 3u));
    }

    Lut3D lut;
    lut.grid_size_ = grid_size;
    lut.half4_table_.resize(expected * 4u);
    for (size_t i = 0; i < expected; ++i) {
        lut.half4_table_[i * 4u + 0u] = floatToHalf(table[i * 3u + 0u]);
        lut.half4_table_[i * 4u + 1u] = floatToHalf(table[i * 3u + 1u]);
        lut.half4_table_[i * 4u + 2u] = floatToHalf(table[i * 3u + 2u]);
        lut.half4_table_[i * 4u + 3u] = floatToHalf(1.0f);
    }
    return lut;
}

Result<Lut3D> Lut3D::loadFromCubeFile(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        return err(ErrorCode::NotFound, "3D LUT file does not exist: " + path.string());
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return err(ErrorCode::Io, "cannot open 3D LUT file: " + path.string());
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return parse(buffer.str());
}

}  // namespace rtx
