// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "shim_probe.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sstream>

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
    char buffer[16] = {};
    if (codepoint <= 0xFFFFu) {
        std::snprintf(buffer, sizeof(buffer), "\\u%04x",
                      static_cast<unsigned int>(codepoint));
        output += buffer;
        return;
    }
    const uint32_t value = codepoint - 0x10000u;
    const uint32_t high = 0xD800u + (value >> 10);
    const uint32_t low = 0xDC00u + (value & 0x3FFu);
    std::snprintf(buffer, sizeof(buffer), "\\u%04x\\u%04x",
                  static_cast<unsigned int>(high), static_cast<unsigned int>(low));
    output += buffer;
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

// Emit pure ASCII.  \uXXXX escapes survive shells and log collectors that
// decode a child process's UTF-8 stdout with a legacy code page before saving
// it.  Invalid UTF-8 is replaced with U+FFFD so the JSON is always well formed.
std::string escapeJson(const std::string& value) {
    std::string output;
    output.reserve(value.size());
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
        case RTX_ERR_TENSORRT_VERSION:
            return "RTX_ERR_TENSORRT_VERSION";
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
        {RTX_CAP_TENSORRT, "tensorrt"},
        {RTX_CAP_D3D12_INTEROP, "d3d12-interop"},
        {RTX_CAP_CPU_REFERENCE, "cpu-reference"},
    };
    std::ostringstream out;
    for (const auto& entry : names) {
        if ((caps & entry.bit) == 0) {
            continue;
        }
        if (out.tellp() > 0) {
            out << ", ";
        }
        out << entry.name;
    }
    if (out.tellp() == 0) {
        return "none";
    }
    return out.str();
}

ShimProbeResult probeShim() {
    ShimProbeResult outcome;
    // NvidiaShim owns the sidecar/embedded resolution: it prefers rtx_shim.dll
    // next to the executable and falls back to the statically linked table in
    // the single-EXE build.
    rtx::NvidiaShim shim;
    std::string error;
    if (!shim.open(&error)) {
        outcome.error = error;
        return outcome;
    }
    outcome.loaded = true;
    outcome.path = shim.path();
    outcome.abi_version = RTX_SHIM_ABI_VERSION;
    outcome.result = static_cast<int32_t>(shim.probe(&outcome.diagnostics));
    return outcome;
}

std::string formatProbeHuman(const ShimProbeResult& probe) {
    std::ostringstream out;
    out << "NVIDIA shim probe\n";
    if (!probe.loaded) {
        out << "  status: unavailable\n";
        out << "  detail: " << probe.error << "\n";
        return out.str();
    }
    out << "  shim: " << probe.path << " (ABI " << probe.abi_version << ")\n";
    out << "  result: " << probe.result << " (" << resultLabel(probe.result) << ")\n";
    if (!probe.error.empty()) {
        out << "  diagnostic: " << probe.error << "\n";
    }
    const RtxDiagnostics& diagnostics = probe.diagnostics;
    out << "  summary: " << safeString(diagnostics.summary, sizeof(diagnostics.summary)) << "\n";
    if (diagnostics.device.name[0] != '\0') {
        out << "  device: " << safeString(diagnostics.device.name, sizeof(diagnostics.device.name))
            << "\n";
        out << "  compute: sm_" << diagnostics.device.compute_major
            << diagnostics.device.compute_minor << "\n";
        out << "  vram: " << (diagnostics.device.vram_bytes / (1024ull * 1024ull)) << " MiB\n";
        out << "  cuda: " << diagnostics.device.cuda_version << "\n";
        out << "  capabilities: " << capabilityList(diagnostics.device.capabilities) << "\n";
    }
    out << "  components:\n";
    const uint32_t component_count =
        std::min<uint32_t>(diagnostics.component_count, RTX_MAX_COMPONENTS);
    for (uint32_t i = 0; i < component_count; ++i) {
        const RtxComponentInfo& component = diagnostics.components[i];
        out << "    [" << (component.present ? 'x' : ' ') << "] "
            << safeString(component.name, sizeof(component.name));
        if (component.version != 0) {
            out << " v" << component.version;
        }
        const std::string note = safeString(component.note, sizeof(component.note));
        if (!note.empty()) {
            out << " - " << note;
        }
        out << "\n";
    }
    return out.str();
}

std::string formatProbeJson(const ShimProbeResult& probe) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"loaded\": " << (probe.loaded ? "true" : "false") << ",\n";
    out << "  \"path\": \"" << escapeJson(probe.path) << "\",\n";
    out << "  \"error\": \"" << escapeJson(probe.error) << "\",\n";
    out << "  \"abi_version\": " << probe.abi_version << ",\n";
    out << "  \"result\": " << probe.result << ",\n";
    out << "  \"result_name\": \"" << resultLabel(probe.result) << "\",\n";
    out << "  \"diagnostics\": {\n";
    out << "    \"summary\": \"" << escapeJson(safeString(
        probe.diagnostics.summary, sizeof(probe.diagnostics.summary))) << "\",\n";
    out << "    \"device\": {\n";
    out << "      \"name\": \"" << escapeJson(safeString(
        probe.diagnostics.device.name, sizeof(probe.diagnostics.device.name))) << "\",\n";
    out << "      \"compute_major\": " << probe.diagnostics.device.compute_major << ",\n";
    out << "      \"compute_minor\": " << probe.diagnostics.device.compute_minor << ",\n";
    out << "      \"vram_bytes\": " << probe.diagnostics.device.vram_bytes << ",\n";
    out << "      \"cuda_version\": " << probe.diagnostics.device.cuda_version << ",\n";
    out << "      \"capabilities\": " << probe.diagnostics.device.capabilities << ",\n";
    out << "      \"capability_names\": \"" << escapeJson(
        capabilityList(probe.diagnostics.device.capabilities)) << "\"\n";
    out << "    },\n";
    out << "    \"components\": [\n";
    const uint32_t component_count =
        std::min<uint32_t>(probe.diagnostics.component_count, RTX_MAX_COMPONENTS);
    for (uint32_t i = 0; i < component_count; ++i) {
        const RtxComponentInfo& component = probe.diagnostics.components[i];
        out << "      {\"name\": \"" << escapeJson(safeString(component.name,
                                                               sizeof(component.name)))
            << "\", \"present\": " << (component.present != 0 ? "true" : "false")
            << ", \"version\": " << component.version << ", \"path\": \""
            << escapeJson(safeString(component.path, sizeof(component.path))) << "\", \"note\": \""
            << escapeJson(safeString(component.note, sizeof(component.note))) << "\"}";
        if (i + 1 != component_count) {
            out << ",";
        }
        out << "\n";
    }
    out << "    ]\n";
    out << "  }\n";
    out << "}\n";
    return out.str();
}

}  // namespace rtx::cli
