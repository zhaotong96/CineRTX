// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
// Phase 12 (Wave 12.2-B2): eliminated BitReader, HevcSpsInfo, HevcVuiColor,
// and HevcSeiHdr10 in favor of free functions and std::tuple structured bindings.

#include "bitstream.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <tuple>
#include <vector>

namespace rtx {
namespace {

constexpr size_t kNoPosition = static_cast<size_t>(-1);

using BitState = std::tuple<std::span<const uint8_t>, size_t, bool>;

size_t bitsLeft(const BitState& s) noexcept {
    const auto& [data, pos, ok] = s;
    (void)ok;
    return data.size() * 8 - pos;
}

uint32_t readU(BitState& s, int count) noexcept {
    auto& [data, pos, ok] = s;
    if (count < 0 || count > 32 || data.size() * 8 - pos < static_cast<size_t>(count)) {
        ok = false;
        return 0;
    }
    uint32_t value = 0;
    for (int i = 0; i < count; ++i) {
        value = (value << 1) | ((data[pos / 8] >> (7 - static_cast<int>(pos % 8))) & 1u);
        ++pos;
    }
    return value;
}

uint32_t readUe(BitState& s) noexcept {
    int zeros = 0;
    while (true) {
        if (bitsLeft(s) == 0) {
            auto& [d, p, ok] = s; (void)d; (void)p; ok = false;
            return 0;
        }
        if (readU(s, 1) != 0) break;
        if (++zeros > 31) {
            auto& [d, p, ok] = s; (void)d; (void)p; ok = false;
            return 0;
        }
    }
    return zeros == 0 ? 0u : ((1u << zeros) - 1u) + readU(s, zeros);
}

int32_t readSe(BitState& s) noexcept {
    const uint32_t code = readUe(s);
    const auto& [d, p, ok] = s; (void)d; (void)p;
    if (!ok) return 0;
    return (code & 1u) != 0 ? static_cast<int32_t>((code + 1u) >> 1)
                            : -static_cast<int32_t>(code >> 1);
}

void skipBits(BitState& s, size_t count) noexcept {
    while (count > 0) {
        const size_t chunk = count > 32 ? 32 : count;
        (void)readU(s, static_cast<int>(chunk));
        count -= chunk;
    }
}

bool readerOk(const BitState& s) noexcept {
    const auto& [d, p, ok] = s; (void)d; (void)p;
    return ok;
}

std::vector<std::vector<uint8_t>> nalUnits(const uint8_t* data, size_t size) {
    std::vector<std::vector<uint8_t>> units;
    if (!data || size == 0) return units;
    if (looksLikeAnnexB(data, size)) return splitAnnexB(data, size);
    if (data[0] == 1 && size >= 23) {
        size_t pos = 23;
        for (int arr = 0; arr < data[22] && pos + 3 <= size; ++arr) {
            ++pos;
            const int count = (data[pos] << 8) | data[pos + 1];
            pos += 2;
            for (int i = 0; i < count && pos + 2 <= size; ++i) {
                const size_t len = (static_cast<size_t>(data[pos]) << 8) | data[pos + 1];
                pos += 2;
                if (len == 0 || len > size - pos) return units;
                units.emplace_back(data + pos, data + pos + len);
                pos += len;
            }
        }
        return units;
    }
    size_t pos = 0;
    while (pos + 4 <= size) {
        const size_t len = (static_cast<size_t>(data[pos]) << 24) |
                           (static_cast<size_t>(data[pos + 1]) << 16) |
                           (static_cast<size_t>(data[pos + 2]) << 8) |
                           static_cast<size_t>(data[pos + 3]);
        pos += 4;
        if (len == 0 || len > size - pos) break;
        units.emplace_back(data + pos, data + pos + len);
        pos += len;
    }
    return units;
}

std::vector<uint8_t> rbspFromNal(std::span<const uint8_t> nal) {
    std::vector<uint8_t> rbsp;
    rbsp.reserve(nal.size());
    int zeros = 0;
    for (uint8_t byte : nal) {
        if (zeros >= 2 && byte == 0x03) { zeros = 0; continue; }
        rbsp.push_back(byte);
        zeros = (byte == 0x00) ? zeros + 1 : 0;
    }
    return rbsp;
}

// (profile_space, tier, profile_idc, compatibility, constraints, level_idc,
//  chroma_format_idc, bit_depth_luma_minus8, bit_depth_chroma_minus8, max_sub_layers_minus1)
bool parseHevcSps(const std::vector<uint8_t>& nal,
                  std::tuple<int, int, int, uint32_t, uint64_t, int, int, int, int, int>& info) {
    if (nal.size() < 3 || ((nal[0] >> 1) & 0x3F) != 33) return false;
    const auto rbsp = rbspFromNal(std::span<const uint8_t>(nal).subspan(2));
    BitState r{rbsp, 0, true};
    auto& [profile_space, tier, profile_idc, compatibility, constraints, level_idc,
           chroma_format_idc, luma_m8, chroma_m8, max_sub] = info;

    (void)readU(r, 4);
    max_sub = static_cast<int>(readU(r, 3));
    (void)readU(r, 1);

    profile_space = static_cast<int>(readU(r, 2));
    tier = static_cast<int>(readU(r, 1));
    profile_idc = static_cast<int>(readU(r, 5));
    compatibility = readU(r, 32);
    constraints = (static_cast<uint64_t>(readU(r, 16)) << 32) | readU(r, 32);
    level_idc = static_cast<int>(readU(r, 8));

    for (int i = 0; i < max_sub; ++i) {
        const bool pp = readU(r, 1) != 0;
        const bool lp = readU(r, 1) != 0;
        if (pp) { (void)readU(r, 32); (void)readU(r, 32); (void)readU(r, 24); }
        if (lp) (void)readU(r, 8);
    }
    if (max_sub > 0) {
        for (int i = max_sub; i < 8; ++i) (void)readU(r, 2);
    }
    (void)readUe(r);
    chroma_format_idc = static_cast<int>(readUe(r));
    if (chroma_format_idc == 3) (void)readU(r, 1);
    (void)readUe(r);
    (void)readUe(r);
    if (readU(r, 1) != 0) {
        for (int i = 0; i < 4; ++i) (void)readUe(r);
    }
    luma_m8 = static_cast<int>(readUe(r));
    chroma_m8 = static_cast<int>(readUe(r));
    return readerOk(r) && luma_m8 >= 0 && luma_m8 <= 6;
}

bool skipProfileFields(BitState& r) noexcept {
    (void)readU(r, 2);
    (void)readU(r, 1);
    const int profile_idc = static_cast<int>(readU(r, 5));
    bool compat[32] = {};
    for (int i = 0; i < 32; ++i) compat[i] = readU(r, 1) != 0;
    const auto has = [&](int idc) { return profile_idc == idc || (idc >= 0 && idc < 32 && compat[idc]); };
    (void)readU(r, 4);
    if (has(4) || has(5) || has(6) || has(7) || has(8) || has(9) || has(10)) {
        skipBits(r, 9);
        if (has(5) || has(9) || has(10)) { (void)readU(r, 1); skipBits(r, 33); }
        else skipBits(r, 34);
    } else if (has(2)) {
        skipBits(r, 7); (void)readU(r, 1); skipBits(r, 35);
    } else {
        skipBits(r, 43);
    }
    (void)readU(r, 1);
    return readerOk(r);
}

bool skipProfileTierLevel(BitState& r, int max_sub) noexcept {
    if (!skipProfileFields(r)) return false;
    (void)readU(r, 8);
    bool pp[8] = {}, lp[8] = {};
    for (int i = 0; i < max_sub; ++i) { pp[i] = readU(r, 1) != 0; lp[i] = readU(r, 1) != 0; }
    if (max_sub > 0) {
        for (int i = max_sub; i < 8; ++i) (void)readU(r, 2);
    }
    for (int i = 0; i < max_sub; ++i) {
        if (pp[i] && !skipProfileFields(r)) return false;
        if (lp[i]) (void)readU(r, 8);
    }
    return readerOk(r);
}

bool skipScalingListData(BitState& r) noexcept {
    for (int size_id = 0; size_id < 4; ++size_id) {
        for (int matrix_id = 0; matrix_id < 6; matrix_id += (size_id == 3 ? 3 : 1)) {
            if (readU(r, 1) == 0) { (void)readUe(r); continue; }
            const int coeffs = std::min(64, 1 << (4 + (size_id << 1)));
            if (size_id > 1) (void)readSe(r);
            for (int i = 0; i < coeffs; ++i) (void)readSe(r);
        }
    }
    return readerOk(r);
}

bool skipShortTermRps(BitState& r, size_t index, std::vector<int>& num_delta_pocs) noexcept {
    if (index > 0 && readU(r, 1) != 0) {
        (void)readU(r, 1);
        (void)readUe(r);
        int count = 0;
        for (int i = 0; i <= num_delta_pocs[index - 1]; ++i) {
            const bool used = readU(r, 1) != 0;
            const bool use_delta = !used && (readU(r, 1) != 0);
            if (used || use_delta) ++count;
        }
        if (count > 32) return false;
        num_delta_pocs[index] = count;
    } else {
        const uint32_t neg = readUe(r), pos = readUe(r);
        if (neg > 32 || pos > 32) return false;
        num_delta_pocs[index] = static_cast<int>(neg + pos);
        for (uint32_t i = 0; i < neg + pos; ++i) { (void)readUe(r); (void)readU(r, 1); }
    }
    return readerOk(r);
}

bool parseHevcVuiColorFromSps(const std::vector<uint8_t>& nal,
                              std::tuple<bool, bool, int, int, int, bool>& color) {
    if (nal.size() < 3 || ((nal[0] >> 1) & 0x3F) != 33) return false;
    const auto rbsp = rbspFromNal(std::span<const uint8_t>(nal).subspan(2));
    BitState r{rbsp, 0, true};
    (void)readU(r, 4);
    const int max_sub = static_cast<int>(readU(r, 3));
    if (max_sub > 6) return false;
    (void)readU(r, 1);
    if (!skipProfileTierLevel(r, max_sub)) return false;
    (void)readUe(r);
    const uint32_t chroma_fmt = readUe(r);
    if (chroma_fmt > 3) return false;
    if (chroma_fmt == 3) (void)readU(r, 1);
    (void)readUe(r); (void)readUe(r);
    if (readU(r, 1) != 0) { for (int i = 0; i < 4; ++i) (void)readUe(r); }
    (void)readUe(r); (void)readUe(r);
    const uint32_t poc_lsb_m4 = readUe(r);
    if (!readerOk(r) || poc_lsb_m4 > 12u) return false;
    const int log2_max_poc_lsb = static_cast<int>(poc_lsb_m4) + 4;
    for (int i = (readU(r, 1) != 0 ? 0 : max_sub); i <= max_sub; ++i) {
        (void)readUe(r); (void)readUe(r); (void)readUe(r);
    }
    for (int i = 0; i < 6; ++i) (void)readUe(r);
    if (readU(r, 1) != 0 && readU(r, 1) != 0 && !skipScalingListData(r)) return false;
    (void)readU(r, 1); (void)readU(r, 1);
    if (readU(r, 1) != 0) {
        (void)readU(r, 4); (void)readU(r, 4); (void)readUe(r); (void)readUe(r); (void)readU(r, 1);
    }
    const uint32_t num_rps = readUe(r);
    if (num_rps > 64) return false;
    std::vector<int> num_delta_pocs(num_rps, 0);
    for (uint32_t i = 0; i < num_rps; ++i) {
        if (!skipShortTermRps(r, i, num_delta_pocs)) return false;
    }
    if (readU(r, 1) != 0) {
        const uint32_t lt = readUe(r);
        if (lt > 32) return false;
        for (uint32_t i = 0; i < lt; ++i) { skipBits(r, static_cast<size_t>(log2_max_poc_lsb)); (void)readU(r, 1); }
    }
    (void)readU(r, 1); (void)readU(r, 1);
    if (readU(r, 1) == 0) return false;
    if (readU(r, 1) != 0 && readU(r, 8) == 255) { (void)readU(r, 16); (void)readU(r, 16); }
    if (readU(r, 1) != 0) (void)readU(r, 1);
    if (readU(r, 1) == 0) return false;

    auto& [has_colour_desc, has_vst, primaries, transfer, matrix, full_range] = color;
    has_vst = true;
    (void)readU(r, 3);
    full_range = readU(r, 1) != 0;
    if (readU(r, 1) != 0) {
        has_colour_desc = true;
        primaries = static_cast<int>(readU(r, 8));
        transfer = static_cast<int>(readU(r, 8));
        matrix = static_cast<int>(readU(r, 8));
    }
    return readerOk(r);
}

bool parseSeiPayloads(const std::vector<uint8_t>& nal,
                      std::tuple<bool, bool, std::array<uint16_t, 8>, uint32_t, uint32_t, uint16_t, uint16_t>& hdr) {
    if (nal.size() < 3) return false;
    const int nal_type = (nal[0] >> 1) & 0x3F;
    if (nal_type != 39 && nal_type != 40) return false;
    const auto rbsp = rbspFromNal(std::span<const uint8_t>(nal).subspan(2));
    auto& [has_md, has_cll, prim, max_luma, min_luma, max_cll, max_fall] = hdr;
    size_t pos = 0;
    while (pos < rbsp.size()) {
        uint32_t payload_type = 0, payload_size = 0;
        while (pos < rbsp.size() && rbsp[pos] == 0xFF) { payload_type += 255; ++pos; }
        if (pos >= rbsp.size()) break;
        payload_type += rbsp[pos++];
        while (pos < rbsp.size() && rbsp[pos] == 0xFF) { payload_size += 255; ++pos; }
        if (pos >= rbsp.size()) break;
        payload_size += rbsp[pos++];
        if (payload_size > rbsp.size() - pos) break;
        const uint8_t* p = rbsp.data() + pos;
        const auto u16 = [p](size_t o) { return static_cast<uint16_t>((p[o] << 8) | p[o + 1]); };
        const auto u32 = [p](size_t o) {
            return (static_cast<uint32_t>(p[o]) << 24) | (static_cast<uint32_t>(p[o + 1]) << 16) |
                   (static_cast<uint32_t>(p[o + 2]) << 8) | static_cast<uint32_t>(p[o + 3]);
        };
        if (payload_type == 137 && payload_size >= 24) {
            prim = {u16(8), u16(10), u16(0), u16(2), u16(4), u16(6), u16(12), u16(14)};
            max_luma = u32(16);
            min_luma = u32(20);
            has_md = true;
        } else if (payload_type == 144 && payload_size >= 4) {
            max_cll = u16(0);
            max_fall = u16(2);
            has_cll = true;
        }
        pos += payload_size;
    }
    return has_md || has_cll;
}

void appendU16(std::vector<uint8_t>& out, size_t v) {
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(v & 0xFF));
}
void appendBe32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(v & 0xFF));
}

}  // namespace

std::vector<std::vector<uint8_t>> splitAnnexB(std::span<const uint8_t> data) {
    std::vector<std::vector<uint8_t>> units;
    const size_t size = data.size();
    if (size < 3) return units;
    size_t nal_start = kNoPosition;
    for (size_t i = 0; i + 2 < size;) {
        if (data[i] == 0x00 && data[i + 1] == 0x00 && data[i + 2] == 0x01) {
            if (nal_start != kNoPosition) {
                size_t end = i;
                while (end > nal_start && data[end - 1] == 0x00) --end;
                if (end > nal_start) units.emplace_back(data.begin() + nal_start, data.begin() + end);
            }
            i += 3;
            nal_start = i;
        } else {
            ++i;
        }
    }
    if (nal_start != kNoPosition && nal_start < size) {
        size_t end = size;
        while (end > nal_start && data[end - 1] == 0x00) --end;
        if (end > nal_start) units.emplace_back(data.begin() + nal_start, data.begin() + end);
    }
    return units;
}

std::vector<std::vector<uint8_t>> splitAnnexB(const uint8_t* data, size_t size) {
    return (data && size) ? splitAnnexB(std::span<const uint8_t>(data, size)) : std::vector<std::vector<uint8_t>>{};
}

bool looksLikeAnnexB(std::span<const uint8_t> data) {
    if (data.size() < 3) return false;
    return (data[0] == 0 && data[1] == 0 && data[2] == 1) ||
           (data.size() >= 4 && data[0] == 0 && data[1] == 0 && data[2] == 0 && data[3] == 1);
}

bool looksLikeAnnexB(const uint8_t* data, size_t size) {
    return (data && size) && looksLikeAnnexB(std::span<const uint8_t>(data, size));
}

std::vector<uint8_t> annexBToLengthPrefixed(const uint8_t* data, size_t size) {
    if (looksLikeAnnexB(data, size)) {
        std::vector<uint8_t> out;
        for (const auto& unit : splitAnnexB(data, size)) {
            appendBe32(out, static_cast<uint32_t>(unit.size()));
            out.insert(out.end(), unit.begin(), unit.end());
        }
        return out;
    }
    if (size >= 4) {
        const uint32_t first = (static_cast<uint32_t>(data[0]) << 24) | (static_cast<uint32_t>(data[1]) << 16) |
                               (static_cast<uint32_t>(data[2]) << 8) | static_cast<uint32_t>(data[3]);
        if (first > 0 && first <= size - 4) return std::vector<uint8_t>(data, data + size);
    }
    std::vector<uint8_t> out;
    appendBe32(out, static_cast<uint32_t>(size));
    out.insert(out.end(), data, data + size);
    return out;
}

std::vector<uint8_t> lengthPrefixedToAnnexB(const uint8_t* data, size_t size) {
    if (looksLikeAnnexB(data, size)) return std::vector<uint8_t>(data, data + size);
    static constexpr uint8_t kStartCode[4] = {0x00, 0x00, 0x00, 0x01};
    std::vector<uint8_t> out;
    size_t pos = 0;
    while (pos + 4 <= size) {
        const size_t len = (static_cast<size_t>(data[pos]) << 24) | (static_cast<size_t>(data[pos + 1]) << 16) |
                           (static_cast<size_t>(data[pos + 2]) << 8) | static_cast<size_t>(data[pos + 3]);
        pos += 4;
        if (len == 0 || len > size - pos) break;
        out.insert(out.end(), kStartCode, kStartCode + 4);
        out.insert(out.end(), data + pos, data + pos + len);
        pos += len;
    }
    if (out.empty()) out.assign(data, data + size);
    return out;
}

std::vector<uint8_t> extractParameterSets(const uint8_t* annexb, size_t size) {
    std::vector<uint8_t> out;
    static constexpr uint8_t kStartCode[4] = {0x00, 0x00, 0x00, 0x01};
    for (const auto& unit : splitAnnexB(annexb, size)) {
        if (unit.size() < 2) continue;
        const int hevc_t = (unit[0] >> 1) & 0x3F;
        const int avc_t = unit[0] & 0x1F;
        if ((hevc_t != 32 && hevc_t != 33 && hevc_t != 34) && (avc_t != 7 && avc_t != 8)) continue;
        out.insert(out.end(), kStartCode, kStartCode + 4);
        out.insert(out.end(), unit.begin(), unit.end());
    }
    return out;
}

std::vector<uint8_t> makeHevcConfigRecord(const uint8_t* annexb, size_t size) {
    std::vector<uint8_t> vps, sps, pps;
    for (const auto& unit : splitAnnexB(annexb, size)) {
        if (unit.size() < 3) continue;
        const int t = (unit[0] >> 1) & 0x3F;
        if (t == 32 && vps.empty()) vps = unit;
        else if (t == 33 && sps.empty()) sps = unit;
        else if (t == 34 && pps.empty()) pps = unit;
    }
    if (sps.empty()) return {};
    std::tuple<int, int, int, uint32_t, uint64_t, int, int, int, int, int> info{0, 0, 0, 0, 0, 0, 1, 2, 2, 0};
    if (!parseHevcSps(sps, info)) return {};
    const auto& [profile_space, tier, profile_idc, compatibility, constraints, level_idc,
                 chroma_fmt, luma_m8, chroma_m8, max_sub] = info;

    std::vector<uint8_t> out;
    out.reserve(64 + vps.size() + sps.size() + pps.size());
    out.push_back(1);
    out.push_back(static_cast<uint8_t>((profile_space << 6) | (tier << 5) | profile_idc));
    appendBe32(out, compatibility);
    for (int shift = 40; shift >= 0; shift -= 8) out.push_back(static_cast<uint8_t>((constraints >> shift) & 0xFF));
    out.push_back(static_cast<uint8_t>(level_idc));
    out.push_back(0xF0); out.push_back(0x00); out.push_back(0xFC);
    out.push_back(static_cast<uint8_t>(0xFC | (chroma_fmt & 0x03)));
    out.push_back(static_cast<uint8_t>(0xF8 | (luma_m8 & 0x07)));
    out.push_back(static_cast<uint8_t>(0xF8 | (chroma_m8 & 0x07)));
    appendU16(out, 0);
    out.push_back(static_cast<uint8_t>((std::min(max_sub + 1, 7) << 3) | (1 << 2) | 3));
    out.push_back(static_cast<uint8_t>(1 + (!vps.empty() ? 1 : 0) + (!pps.empty() ? 1 : 0)));

    auto appendArray = [&out](const std::vector<uint8_t>& unit, uint8_t type) {
        if (unit.empty() || unit.size() > 65535u) return;
        out.push_back(static_cast<uint8_t>(0x80 | type));
        appendU16(out, 1);
        appendU16(out, unit.size());
        out.insert(out.end(), unit.begin(), unit.end());
    };
    appendArray(vps, 32);
    appendArray(sps, 33);
    appendArray(pps, 34);
    return out;
}

std::vector<uint8_t> makeAvcConfigRecord(const uint8_t* annexb, size_t size) {
    std::vector<std::vector<uint8_t>> sps_units, pps_units;
    for (const auto& unit : splitAnnexB(annexb, size)) {
        if (unit.size() < 4) continue;
        if ((unit[0] & 0x1F) == 7) sps_units.push_back(unit);
        else if ((unit[0] & 0x1F) == 8) pps_units.push_back(unit);
    }
    if (sps_units.empty()) return {};
    const auto& sps = sps_units.front();
    std::vector<uint8_t> out{1, sps[1], sps[2], sps[3], 0xFF, static_cast<uint8_t>(0xE0 | (sps_units.size() & 0x1F))};
    for (const auto& unit : sps_units) { appendU16(out, unit.size()); out.insert(out.end(), unit.begin(), unit.end()); }
    out.push_back(static_cast<uint8_t>(pps_units.size() & 0xFF));
    for (const auto& unit : pps_units) { appendU16(out, unit.size()); out.insert(out.end(), unit.begin(), unit.end()); }
    return out;
}

std::vector<uint8_t> makeAv1ConfigRecord(const uint8_t* obu_data, size_t size, bool ten_bit) {
    if (!obu_data || size == 0) return {};
    if (size >= 4 && obu_data[0] == 0x81) return std::vector<uint8_t>(obu_data, obu_data + size);
    std::vector<uint8_t> out{0x81, 0x0C, static_cast<uint8_t>(ten_bit ? 0x4C : 0x0C), 0x00};
    out.insert(out.end(), obu_data, obu_data + size);
    return out;
}

int hevcConfigBitDepth(const uint8_t* record, size_t size) {
    return (record && size >= 18 && record[0] == 1) ? (8 + (record[17] & 0x07)) : 0;
}

int avcConfigBitDepth(const uint8_t* record, size_t size) {
    if (!record || size < 4 || record[0] != 1) return 0;
    const int p = record[1];
    return (p == 110 || p == 122 || p == 244 || p == 44) ? 10 : 8;
}

bool parseHevcVuiColor(const uint8_t* data, size_t size,
                       std::tuple<bool, bool, int, int, int, bool>& color) {
    for (const auto& unit : nalUnits(data, size)) {
        if (parseHevcVuiColorFromSps(unit, color)) return true;
    }
    return false;
}

bool parseHevcSeiHdr10(const uint8_t* data, size_t size,
                       std::tuple<bool, bool, std::array<uint16_t, 8>, uint32_t, uint32_t, uint16_t, uint16_t>& hdr) {
    for (const auto& unit : nalUnits(data, size)) parseSeiPayloads(unit, hdr);
    const auto& [has_md, has_cll, prim, max_l, min_l, max_c, max_f] = hdr;
    (void)prim; (void)max_l; (void)min_l; (void)max_c; (void)max_f;
    return has_md || has_cll;
}

}  // namespace rtx
