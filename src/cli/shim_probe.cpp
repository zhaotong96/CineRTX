// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "shim_probe.h"

#include <algorithm>
#include <format>
#include <cstring>

#include "rtx/nvidia_shim.h"

namespace rtx::cli {
namespace {

std::string safeString(const char* value, size_t capacity) {
    if (value == nullptr) {
        return {};
    }
    size_t length = 0;
    while (length < capacity && value[length] != '\0') {
        ++length;
    }
    return std::string(value, length);
}
void appendUnicodeEscape(std::string& output, uint32_t codepoint) {
    if (codepoint <= 0xFFFFu) {
        output += std::format("\\u{:04x}", static_cast<unsigned int>(codepoint));
        return;
    }
    const uint32_t value = codepoint - 0x10000u;
    output += std::format("\\u{:04x}\\u{:04x}",
                          static_cast<unsigned int>(0xD800u + (value >> 10)),
                          static_cast<unsigned int>(0xDC00u + (value & 0x3FFu)));
}
bool decodeUtf8(const std::string& value, size_t offset, uint32_t* codepoint, size_t* length) {
    const auto byte = [&value](size_t index) {
        return static_cast<unsigned char>(value[index]);
    };
    const unsigned char lead = byte(offset);
    if (lead < 0x80) {
        *codepoint = lead;
        *length = 1;
        return true;
    }
    if (lead >= 0xC2 && lead <= 0xDF) {
        if (offset + 1 >= value.size()) {
            return false;
        }
        const unsigned char second = byte(offset + 1);
        if ((second & 0xC0) != 0x80) {
            return false;
        }
        *codepoint = ((lead & 0x1Fu) << 6) | (second & 0x3Fu);
        *length = 2;
        return true;
    }
    if (lead >= 0xE0 && lead <= 0xEF) {
        if (offset + 2 >= value.size()) {
            return false;
        }
        const unsigned char second = byte(offset + 1);
        const unsigned char third = byte(offset + 2);
        if ((second & 0xC0) != 0x80 || (third & 0xC0) != 0x80) {
            return false;
        }
        if ((lead == 0xE0 && second < 0xA0) || (lead == 0xED && second >= 0xA0)) {
            return false;
        }
        *codepoint = ((lead & 0x0Fu) << 12) | ((second & 0x3Fu) << 6) | (third & 0x3Fu);
        *length = 3;
        return true;
    }
    if (lead >= 0xF0 && lead <= 0xF4) {
        if (offset + 3 >= value.size()) {
            return false;
        }
        const unsigned char second = byte(offset + 1);
        const unsigned char third = byte(offset + 2);
        const unsigned char fourth = byte(offset + 3);
        if ((second & 0xC0) != 0x80 || (third & 0xC0) != 0x80 ||
            (fourth & 0xC0) != 0x80) {
            return false;
        }
        if ((lead == 0xF0 && second < 0x90) || (lead == 0xF4 && second >= 0x90)) {
            return false;
        }
        *codepoint = ((lead & 0x07u) << 18) | ((second & 0x3Fu) << 12) |
                     ((third & 0x3Fu) << 6) | (fourth & 0x3Fu);
        *length = 4;
        return true;
    }
    return false;
}
}  // namespace

std::string escapeJson(const std::string& value) {
    std::string output;
    output.reserve(value.size());
// Emit pure ASCII.  \uXXXX escapes survive shells and log collectors that
// decode a child process's UTF-8 stdout with a legacy code page before saving
// it.  Invalid UTF-8 is replaced with U+FFFD so the JSON is always well formed.
    for (size_t index = 0; index < value.size();) {
        const unsigned char c = static_cast<unsigned char>(value[index]);
        switch (c) {
            case '"':
                output += "\\\"";
                ++index;
                break;
            case '\\':
                output += "\\\\";
                ++index;
                break;
            case '\n':
                output += "\\n";
                ++index;
                break;
            case '\r':
                output += "\\r";
                ++index;
                break;
            case '\t':
                output += "\\t";
                ++index;
                break;
            default:
                if (c < 0x20) {
                    appendUnicodeEscape(output, c);
                    ++index;
                } else if (c < 0x80) {
                    output += static_cast<char>(c);
                    ++index;
                } else {
                    uint32_t codepoint = 0;
                    size_t length = 0;
                    if (decodeUtf8(value, index, &codepoint, &length)) {
                        appendUnicodeEscape(output, codepoint);
                        index += length;
                    } else {
                        output += "\\ufffd";
                        ++index;
                    }
                }
                break;
        }
    }
    return output;
}
namespace {
const char* resultLabel(int32_t result) {
    switch (static_cast<RtxResult>(result)) {
        case RTX_OK:
            return "RTX_OK";
        case RTX_ERR_INVALID_ARGUMENT:
            return "RTX_ERR_INVALID_ARGUMENT";
        case RTX_ERR_NOT_FOUND:
            return "RTX_ERR_NOT_FOUND";
        case RTX_ERR_IO:
            return "RTX_ERR_IO";
        case RTX_ERR_UNSUPPORTED:
            return "RTX_ERR_UNSUPPORTED";
        case RTX_ERR_DLL_MISSING:
            return "RTX_ERR_DLL_MISSING";
        case RTX_ERR_ARCH_UNSUPPORTED:
            return "RTX_ERR_ARCH_UNSUPPORTED";
        case RTX_ERR_DRIVER_TOO_OLD:
            return "RTX_ERR_DRIVER_TOO_OLD";
        case RTX_ERR_DEVICE_LOST:
            return "RTX_ERR_DEVICE_LOST";
        case RTX_ERR_OUT_OF_MEMORY:
            return "RTX_ERR_OUT_OF_MEMORY";
        case RTX_ERR_NGX:
            return "RTX_ERR_NGX";
        case RTX_ERR_RESERVED_11:
            return "RTX_ERR_RESERVED_11";
        case RTX_ERR_NOT_IMPLEMENTED:
            return "RTX_ERR_NOT_IMPLEMENTED";
        case RTX_ERR_INTERNAL:
            return "RTX_ERR_INTERNAL";
    }
    return "RTX_ERR_UNKNOWN";
}
}  // namespace

std::string capabilityList(uint32_t caps) {
    const struct {
        uint32_t bit;
        const char* name;
    } names[] = {
        {RTX_CAP_NVDEC, "nvdec"},
        {RTX_CAP_NVENC_HEVC10, "nvenc-hevc10"},
        {RTX_CAP_NVENC_AV1, "nvenc-av1"},
        {RTX_CAP_VSR, "vsr"},
        {RTX_CAP_TRUEHDR, "truehdr"},
        {RTX_CAP_NVOFA, "nvofa"},
        {RTX_CAP_RESERVED_6, "reserved-6"},
        {RTX_CAP_D3D12_INTEROP, "d3d12-interop"},
        {RTX_CAP_CPU_REFERENCE, "cpu-reference"},
    };
    std::string out;
    for (const auto& entry : names) {
        if ((caps & entry.bit) == 0) continue;
        if (!out.empty()) out += ", ";
        out += entry.name;
    }
    return out.empty() ? "none" : out;
}
ShimProbeResult probeShim() {
    rtx::NvidiaShim shim;
    std::string error;
    RtxDiagnostics diagnostics{};
    if (!shim.open(&error)) {
        return {false, {}, error, 0u, RTX_ERR_DLL_MISSING, diagnostics};
    }
    const int32_t res = static_cast<int32_t>(shim.probe(&diagnostics));
    return {true, shim.path(), {}, RTX_SHIM_ABI_VERSION, res, diagnostics};
}
std::string formatProbeHuman(const ShimProbeResult& probe) {
    const auto& [loaded, path, error, abi_version, result, diagnostics] = probe;
    std::string out = "NVIDIA shim probe\n";
    if (!loaded) {
        out += std::format("  status: unavailable\n  detail: {}\n", error);
        return out;
    }
    out += std::format("  shim: {} (ABI {})\n  result: {} ({})\n",
                       path, abi_version, result, resultLabel(result));
    if (!error.empty()) out += std::format("  diagnostic: {}\n", error);
    out += std::format("  summary: {}\n", safeString(diagnostics.summary, sizeof(diagnostics.summary)));
    if (diagnostics.device.name[0] != '\0') {
        out += std::format(
            "  device: {}\n  compute: sm_{}{}\n  vram: {} MiB\n  cuda: {}\n  capabilities: {}\n",
            safeString(diagnostics.device.name, sizeof(diagnostics.device.name)),
            diagnostics.device.compute_major, diagnostics.device.compute_minor,
            diagnostics.device.vram_bytes / (1024ull * 1024ull),
            diagnostics.device.cuda_version, capabilityList(diagnostics.device.capabilities));
    }
    out += "  components:\n";
    const uint32_t component_count = std::min<uint32_t>(diagnostics.component_count, RTX_MAX_COMPONENTS);
    for (uint32_t i = 0; i < component_count; ++i) {
        const RtxComponentInfo& c = diagnostics.components[i];
        out += std::format("    [{}] {}", c.present ? 'x' : ' ', safeString(c.name, sizeof(c.name)));
        if (c.version != 0) out += std::format(" v{}", c.version);
        if (const std::string note = safeString(c.note, sizeof(c.note)); !note.empty()) {
            out += std::format(" - {}", note);
        }
        out += '\n';
    }
    return out;
}
std::string formatProbeJson(const ShimProbeResult& probe) {
    const auto& [loaded, path, error, abi_version, result, diagnostics] = probe;
    std::string out = std::format(
        "{{\n"
        "  \"loaded\": {},\n"
        "  \"path\": \"{}\",\n"
        "  \"error\": \"{}\",\n"
        "  \"abi_version\": {},\n"
        "  \"result\": {},\n"
        "  \"result_name\": \"{}\",\n"
        "  \"diagnostics\": {{\n"
        "    \"summary\": \"{}\",\n"
        "    \"device\": {{\n"
        "      \"name\": \"{}\",\n"
        "      \"compute_major\": {},\n"
        "      \"compute_minor\": {},\n"
        "      \"vram_bytes\": {},\n"
        "      \"cuda_version\": {},\n"
        "      \"capabilities\": {},\n"
        "      \"capability_names\": \"{}\"\n"
        "    }},\n"
        "    \"components\": [\n",
        loaded ? "true" : "false", escapeJson(path), escapeJson(error),
        abi_version, result, resultLabel(result),
        escapeJson(safeString(diagnostics.summary, sizeof(diagnostics.summary))),
        escapeJson(safeString(diagnostics.device.name, sizeof(diagnostics.device.name))),
        diagnostics.device.compute_major, diagnostics.device.compute_minor,
        diagnostics.device.vram_bytes, diagnostics.device.cuda_version,
        diagnostics.device.capabilities,
        escapeJson(capabilityList(diagnostics.device.capabilities)));
    const uint32_t component_count = std::min<uint32_t>(diagnostics.component_count, RTX_MAX_COMPONENTS);
    for (uint32_t i = 0; i < component_count; ++i) {
        const RtxComponentInfo& c = diagnostics.components[i];
        out += std::format(
            "      {{\"name\": \"{}\", \"present\": {}, \"version\": {}, \"path\": \"{}\", \"note\": \"{}\"{}\n",
            escapeJson(safeString(c.name, sizeof(c.name))),
            c.present ? "true" : "false", c.version,
            escapeJson(safeString(c.path, sizeof(c.path))),
            escapeJson(safeString(c.note, sizeof(c.note))),
            i + 1 < component_count ? "}," : "}");
    }
    out += "    ]\n  }\n}\n";
    return out;
}
}  // namespace rtx::cli
