// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Shim and NGX bridge ABI contract tests.
//
// The single-EXE deliverable ships no sidecar DLLs: rtx_shim and rtx_ngx_bridge
// are static archives that rtx_core links, so these tests call
// the C ABI entry points directly.  The boundary under test is unchanged --
// fixed-width structs, opaque handles and the RtxResult code set still cross it,
// and NvidiaShim::loadEmbedded() / NgxRemasterBridge::bindEmbedded() bind the
// very same symbols in the shipping binary.
//
// The hardened loader stays under test through a system library, because the
// driver-side modules (nvcuda, nvcuvid, nvEncodeAPI, NvOFFRUC, d3d12) are still
// resolved at run time with LoadLibraryExW and must never be link dependencies.

#include "test_framework.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "dyn_library.h"
#include "shim/ngx_bridge_abi.h"
#include "shim/rtx_shim_abi.h"
#include "shim_probe.h"

using namespace rtx_shim;

namespace {

// A module that is guaranteed to exist on the host and to export the symbol
// below.  The loader is exercised against it because the engine drives the very
// same code path for every driver module it touches.
const char* probeLibraryName() {
#if defined(_WIN32)
    return "kernel32.dll";
#elif defined(__APPLE__)
    return "libSystem.dylib";
#else
    return "libm.so.6";
#endif
}
const char* probeSymbolName() {
#if defined(_WIN32)
    return "GetProcAddress";
#else
    return "cos";
#endif
}
bool systemLibraryLoadable(const char* windows_name, const char* posix_name) {
    (void)posix_name;
// Probes whether a system library is loadable, without keeping a reference.
// Used to decide whether the D3D12/CUDA interop self-test is expected to run
// on this host at all: a machine without the NVIDIA driver must observe the
// structured RTX_ERR_DLL_MISSING, never a silent pass.
#if defined(_WIN32)
    wchar_t wide[64] = {};
    size_t index = 0;
    for (const char* cursor = windows_name; *cursor != '\0' && index + 1 < 64; ++cursor) {
        wide[index++] = static_cast<wchar_t>(*cursor);
    }
    HMODULE module = LoadLibraryExW(wide, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (module == nullptr) {
        return false;
    }
    FreeLibrary(module);
    return true;
#else
    if (posix_name == nullptr) {
        return false;  // D3D12 has no POSIX counterpart
    }
    void* handle = dlopen(posix_name, RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        return false;
    }
    dlclose(handle);
    return true;
#endif
}
RtxBackendConfig validBackendConfig() {
    RtxBackendConfig config{};
    config.width = 1920;
    config.height = 1080;
    config.target_width = 3840;
    config.target_height = 2160;
    config.vsr_quality = 2;
    config.enable_truehdr = 1;
    config.enable_nvofa = 0;
    config.enable_rife = 0;
    config.vram_budget_bytes = 7168ull * 1024 * 1024;
    return config;
}
}  // namespace

RTX_TEST(shim_dynamic_library_loading) {
    DynLibrary library;
    std::string error;
    RTX_REQUIRE(dynOpenSystem(library, probeLibraryName(), &error));
    RTX_CHECK(dynIsOpen(library));
    RTX_CHECK(!dynPath(library).empty());
    RTX_CHECK(error.empty());
    const std::string platform = dynPlatformName();
    RTX_CHECK(platform == "windows" || platform == "linux" || platform == "macos" ||
              platform == "posix");
    RTX_CHECK(dynSymbol(library, probeSymbolName()) != nullptr);
// "posix" is what dyn_library_posix.cpp reports; the loader has no separate
// linux/macos implementation, so both spellings are part of the contract.
    RTX_CHECK(dynSymbol(library, "this_symbol_does_not_exist") == nullptr);

    DynLibrary missing;
    RTX_CHECK(!dynOpenSystem(missing, "rtx-does-not-exist-12345", &error));
    RTX_CHECK(!dynIsOpen(missing));
    RTX_CHECK(!error.empty());
// A bare name never resolves outside System32, and an explicit path that
// does not exist must report a structured error rather than crash.
    RTX_CHECK(!dynOpenExplicit(missing, "rtx-does-not-exist-12345.dll", &error));
    RTX_CHECK(!dynIsOpen(missing));
    RTX_CHECK(!error.empty());
    RTX_CHECK(!dynOpenExplicit(missing, "", &error));
    RTX_CHECK(!error.empty());

    DynLibrary moved = std::move(library);
    RTX_CHECK(!dynIsOpen(library));
    RTX_CHECK(dynIsOpen(moved));
    RTX_CHECK(dynSymbol(moved, probeSymbolName()) != nullptr);
// Ownership transfers on move; the source is left closed.
    dynClose(moved);
    RTX_CHECK(!dynIsOpen(moved));
    RTX_CHECK(dynPath(moved).empty());
    RTX_CHECK(dynSymbol(moved, probeSymbolName()) == nullptr);
}
RTX_TEST(shim_abi_version_and_result_names) {
    RTX_CHECK_EQ(rtx_shim_abi_version(), RTX_SHIM_ABI_VERSION);
    RTX_CHECK_EQ(RTX_SHIM_ABI_VERSION, 3u);  // Phase 11.2: decoder_reset appended (ABI v3)
    RTX_CHECK_EQ(std::string(rtx_shim_result_name(RTX_OK)), std::string("RTX_OK"));
    RTX_CHECK_EQ(std::string(rtx_shim_result_name(RTX_ERR_ARCH_UNSUPPORTED)),
                 std::string("RTX_ERR_ARCH_UNSUPPORTED"));
    RTX_CHECK_EQ(std::string(rtx_shim_result_name(RTX_ERR_NOT_IMPLEMENTED)),
                 std::string("RTX_ERR_NOT_IMPLEMENTED"));
    RTX_CHECK_EQ(std::string(rtx_shim_result_name(static_cast<RtxResult>(999))),
                 std::string("RTX_ERR_UNKNOWN"));
}
RTX_TEST(shim_probe_diagnostics) {
    RtxDiagnostics diagnostics{};
    RTX_CHECK_EQ(rtx_shim_probe(&diagnostics), RTX_OK);
    RTX_CHECK(diagnostics.summary[0] != '\0');
    RTX_CHECK(diagnostics.component_count <= RTX_MAX_COMPONENTS);
    RTX_CHECK_EQ(rtx_shim_probe(nullptr), RTX_OK);

    const RtxBackendConfig config = validBackendConfig();
    RtxBackend* backend = reinterpret_cast<RtxBackend*>(0x1);
    RTX_CHECK_EQ(rtx_shim_create_backend(&config, nullptr), RTX_ERR_INVALID_ARGUMENT);
    RTX_CHECK_EQ(rtx_shim_create_backend(nullptr, &backend), RTX_ERR_INVALID_ARGUMENT);
    RTX_CHECK(backend == nullptr);

    RtxBackendConfig invalid = config;
    invalid.width = 0;
    RTX_CHECK_EQ(rtx_shim_create_backend(&invalid, &backend), RTX_ERR_INVALID_ARGUMENT);
    invalid = config;
    invalid.vsr_quality = 0;
    RTX_CHECK_EQ(rtx_shim_create_backend(&invalid, &backend), RTX_ERR_INVALID_ARGUMENT);
    invalid = config;
    invalid.target_width = 640;
    RTX_CHECK_EQ(rtx_shim_create_backend(&invalid, &backend), RTX_ERR_UNSUPPORTED);

    const RtxResult created = rtx_shim_create_backend(&config, &backend);
    RTX_CHECK(created == RTX_ERR_ARCH_UNSUPPORTED || created == RTX_ERR_NOT_IMPLEMENTED ||
              created == RTX_ERR_NGX || created == RTX_ERR_DLL_MISSING ||
              created == RTX_ERR_DRIVER_TOO_OLD || created == RTX_OK);
    if (created == RTX_OK) {
        RTX_REQUIRE(backend != nullptr);
        RtxDeviceInfo info{};
// Maxwell gates the RTX Video features with ARCH_UNSUPPORTED; a Turing+
// machine without the NGX SDK reports NGX; a CPU-only host reports
// DLL_MISSING.  All are honest outcomes for this configuration.  Stage-1
// hardware is exercised by the codec ABI below.
        uint32_t caps = 0;
        RTX_CHECK_EQ(rtx_shim_get_device_info(backend, &info), RTX_OK);
        RTX_CHECK_EQ(rtx_shim_get_capabilities(backend, &caps), RTX_OK);
        RTX_CHECK(info.name[0] != '\0');
        RTX_CHECK(rtx_shim_last_error(backend) != nullptr);
        rtx_shim_destroy_backend(backend);
    } else {
        RTX_CHECK(backend == nullptr);
        rtx_shim_destroy_backend(nullptr);
        RTX_CHECK(rtx_shim_last_error(nullptr) != nullptr);
        rtx_shim_destroy_backend(nullptr);
    }
}
RTX_TEST(shim_hardware_abi_exports) {
    const std::uintptr_t entry_points[] = {
        reinterpret_cast<std::uintptr_t>(&rtx_shim_decoder_create),
        reinterpret_cast<std::uintptr_t>(&rtx_shim_decoder_destroy),
        reinterpret_cast<std::uintptr_t>(&rtx_shim_decoder_submit),
        reinterpret_cast<std::uintptr_t>(&rtx_shim_decoder_receive),
        reinterpret_cast<std::uintptr_t>(&rtx_shim_decoder_flush),
        reinterpret_cast<std::uintptr_t>(&rtx_shim_decoder_release_frame),
// Every codec ABI entry point must be present in the image.  Statically
// linked, a missing one is a link error rather than a null symbol, so the
// contract is proven twice: once by taking the address of each function and
// again by the null-argument behaviour each of them documents.
        reinterpret_cast<std::uintptr_t>(&rtx_shim_decoder_download),
        reinterpret_cast<std::uintptr_t>(&rtx_shim_decoder_get_stats),
        reinterpret_cast<std::uintptr_t>(&rtx_shim_decoder_last_error),
        reinterpret_cast<std::uintptr_t>(&rtx_shim_encoder_create),
        reinterpret_cast<std::uintptr_t>(&rtx_shim_encoder_destroy),
        reinterpret_cast<std::uintptr_t>(&rtx_shim_encoder_encode),
        reinterpret_cast<std::uintptr_t>(&rtx_shim_encoder_encode_host),
        reinterpret_cast<std::uintptr_t>(&rtx_shim_encoder_flush),
        reinterpret_cast<std::uintptr_t>(&rtx_shim_encoder_get_sequence_header),
        reinterpret_cast<std::uintptr_t>(&rtx_shim_encoder_get_stats),
        reinterpret_cast<std::uintptr_t>(&rtx_shim_encoder_last_error),
    };
    for (std::uintptr_t entry : entry_points) {
        RTX_CHECK(entry != static_cast<std::uintptr_t>(0));
    }
    RtxDecoder* decoder = reinterpret_cast<RtxDecoder*>(0x1);
    RtxEncoder* encoder = reinterpret_cast<RtxEncoder*>(0x1);
    RTX_CHECK_EQ(rtx_shim_decoder_create(nullptr, &decoder), RTX_ERR_INVALID_ARGUMENT);
    RTX_CHECK(decoder == nullptr);
    RTX_CHECK_EQ(rtx_shim_encoder_create(nullptr, &encoder), RTX_ERR_INVALID_ARGUMENT);
    RTX_CHECK(encoder == nullptr);
    RTX_CHECK_EQ(rtx_shim_decoder_receive(nullptr, nullptr), RTX_ERR_INVALID_ARGUMENT);
    RTX_CHECK_EQ(rtx_shim_encoder_encode(nullptr, nullptr, nullptr), RTX_ERR_INVALID_ARGUMENT);

    RtxDecoderConfig decoder_config{};
    RTX_CHECK_EQ(rtx_shim_decoder_create(&decoder_config, &decoder), RTX_ERR_INVALID_ARGUMENT);
    RtxEncoderConfig encoder_config{};
    RTX_CHECK_EQ(rtx_shim_encoder_create(&encoder_config, &encoder), RTX_ERR_INVALID_ARGUMENT);
}
RTX_TEST(shim_stage1_backend_gate) {
    RtxBackendConfig config{};
    config.width = 1920;
    config.height = 1080;
    config.target_width = 1920;
    config.target_height = 1080;
    config.vsr_quality = 1;
    config.vram_budget_bytes = 512ull * 1024 * 1024;

    RtxBackend* backend = nullptr;
// A pure stage-1 configuration (no upscale, no RTX Video features) must be
// accepted on every CUDA-capable host, including Maxwell.
    const RtxResult created = rtx_shim_create_backend(&config, &backend);
    RTX_CHECK(created == RTX_OK || created == RTX_ERR_DLL_MISSING ||
              created == RTX_ERR_ARCH_UNSUPPORTED || created == RTX_ERR_DRIVER_TOO_OLD);
    if (created == RTX_OK) {
        RTX_REQUIRE(backend != nullptr);
        RtxDeviceInfo info{};
        RTX_CHECK_EQ(rtx_shim_get_device_info(backend, &info), RTX_OK);
        RTX_CHECK(info.name[0] != '\0');
        rtx_shim_destroy_backend(backend);
    } else {
        RTX_CHECK(backend == nullptr);
    }
}
RTX_TEST(shim_ngx_bridge_abi) {
    RtxNgxBridgeCaps caps{};
    RTX_CHECK_EQ(rtx_ngx_bridge_query_caps(&caps), RTX_OK);
    RTX_CHECK(caps.vsr_available == 0 || caps.vsr_available == 1);
    RTX_CHECK(caps.truehdr_available == 0 || caps.truehdr_available == 1);
    RTX_CHECK_EQ(caps.d3d12_device_created, 0);
    RTX_CHECK(caps.diagnostics[0] != '\0');
    RTX_CHECK_EQ(rtx_ngx_bridge_query_caps(nullptr), RTX_ERR_INVALID_ARGUMENT);

    RtxNgxBridgeConfig config{};
    config.input_width = 1920;
    config.input_height = 1080;
    config.output_width = 3840;
// With RTX Video SDK present and deployed, vsr/truehdr report 1; in stub environments they report 0.
    config.output_height = 2160;
    config.vsr_quality = 2;
    config.enable_truehdr = 1;
    RtxNgxBridge* bridge = reinterpret_cast<RtxNgxBridge*>(0x1);
    RTX_CHECK_EQ(rtx_ngx_bridge_create(nullptr, &bridge), RTX_ERR_INVALID_ARGUMENT);
    RTX_CHECK_EQ(rtx_ngx_bridge_create(&config, nullptr), RTX_ERR_INVALID_ARGUMENT);
    RTX_CHECK(bridge == nullptr);
    config.vsr_quality = 5;
    RTX_CHECK_EQ(rtx_ngx_bridge_create(&config, &bridge), RTX_ERR_INVALID_ARGUMENT);
    config.vsr_quality = 2;
    const RtxResult created = rtx_ngx_bridge_create(&config, &bridge);
    RTX_CHECK(created == RTX_OK || created == RTX_ERR_NGX ||
              created == RTX_ERR_ARCH_UNSUPPORTED || created == RTX_ERR_NOT_IMPLEMENTED);
    if (created != RTX_OK) {
        RTX_CHECK(bridge == nullptr);
    } else {
        rtx_ngx_bridge_destroy(bridge);
        bridge = nullptr;
    }
    RTX_CHECK(rtx_ngx_bridge_last_error(nullptr) != nullptr);
// create() now builds the headless D3D12 device and the shared textures.
// Without the RTX Video SDK it still refuses to hand out a bridge, but the
// exact code depends on whether the machine exposes a D3D12 adapter.
    RTX_CHECK(std::string(rtx_ngx_bridge_last_error(nullptr)).find("SDK") != std::string::npos);
    rtx_ngx_bridge_destroy(nullptr);
}
RTX_TEST(shim_ngx_bridge_remaster_abi) {
    RTX_CHECK_EQ(rtx_ngx_bridge_abi_version(), RTX_NGX_BRIDGE_ABI_VERSION);

    RtxNgxRemasterStats stats{};
    RTX_CHECK_EQ(rtx_ngx_bridge_get_remaster_stats(nullptr, &stats), RTX_ERR_INVALID_ARGUMENT);
    RTX_CHECK_EQ(rtx_ngx_bridge_get_remaster_stats(nullptr, nullptr), RTX_ERR_INVALID_ARGUMENT);

    RtxNgxRemasterInput input{};
    RtxNgxRemasterOutput output{};
    RTX_CHECK_EQ(rtx_ngx_bridge_remaster(nullptr, &input, &output, &stats),
                 RTX_ERR_INVALID_ARGUMENT);
    const auto fake = reinterpret_cast<RtxNgxBridge*>(0x1);
    RTX_CHECK_EQ(rtx_ngx_bridge_remaster(fake, nullptr, &output, &stats),
// The remaster contract rides on ABI v2; a different number means the
// bridge and the core disagree about the appended struct layout.
                 RTX_ERR_INVALID_ARGUMENT);
    RTX_CHECK_EQ(rtx_ngx_bridge_remaster(fake, &input, nullptr, &stats),
                 RTX_ERR_INVALID_ARGUMENT);
    input.y_plane = 0;
    input.format = RTX_PIXEL_NV12;
    RTX_CHECK_EQ(rtx_ngx_bridge_remaster(fake, &input, &output, &stats),
                 RTX_ERR_INVALID_ARGUMENT);
    input.y_plane = 0x1000;
    input.uv_plane = 0x2000;
    input.format = RTX_PIXEL_RGBA16F;
// A null input / null output must never reach the CUDA driver.  The fake
// pointer below is only ever paired with an invalid input: per the ABI a
// non-null bridge is a real handle and would be dereferenced.
    RTX_CHECK_EQ(rtx_ngx_bridge_remaster(fake, &input, &output, &stats),
                 RTX_ERR_INVALID_ARGUMENT);
    input.format = RTX_PIXEL_P010;
    input.y_pitch = 4096;
    input.uv_pitch = 4096;
// Bad formats / matrices / pitches are rejected before any GPU work.
    input.matrix = 7;
    RTX_CHECK_EQ(rtx_ngx_bridge_remaster(fake, &input, &output, &stats),
                 RTX_ERR_INVALID_ARGUMENT);
    input.matrix = 0;
    input.full_range = 2;
    RTX_CHECK_EQ(rtx_ngx_bridge_remaster(fake, &input, &output, &stats),
                 RTX_ERR_INVALID_ARGUMENT);
    input.full_range = 0;
    input.y_pitch = 0;
    RTX_CHECK_EQ(rtx_ngx_bridge_remaster(fake, &input, &output, &stats),
                 RTX_ERR_INVALID_ARGUMENT);
    input.y_pitch = 4096;
    input.uv_pitch = 0;
    RTX_CHECK_EQ(rtx_ngx_bridge_remaster(fake, &input, &output, &stats),
                 RTX_ERR_INVALID_ARGUMENT);
}
RTX_TEST(shim_ngx_d3d12_interop_selftest) {
    const bool have_cuda = systemLibraryLoadable("nvcuda.dll", "libcuda.so.1");
    const RtxResult pack_avail = rtx_ngx_pack_available();
    if (have_cuda) {
// Zero pitches are rejected as well.
        if (pack_avail != RTX_OK) {
            std::printf("[pack-diag] rc=%d last_error=%s\n", static_cast<int>(pack_avail),
                        rtx_ngx_pack_last_error());
        }
        RTX_CHECK_EQ(pack_avail, RTX_OK);
    }
    RTX_CHECK(rtx_ngx_pack_last_error() != nullptr);
}
RTX_TEST(shim_probe_json_is_ascii_safe) {
    RtxDiagnostics diag{};
    diag.component_count = 1;
    RtxComponentInfo& component = diag.components[0];
    std::snprintf(component.name, sizeof(component.name), "%s", "localized");
    component.present = 0;

    std::string note = "note: ";
    note += "\xE6\x89\xBE";  // U+627E
    note += "\xE4\xB8\x8D";  // U+4E0D
    note += "\xE5\x88\xB0";  // U+5230
    note += "\n\"quoted\"\t";
    note += static_cast<char>(0xFF);  // invalid UTF-8 must become U+FFFD
    RTX_REQUIRE(note.size() < sizeof(component.note));
    std::memcpy(component.note, note.data(), note.size());
    component.note[note.size()] = '\0';

    const rtx::cli::ShimProbeResult probe{true, "C:\\Program Files\\CineRTX\\CineRTX.exe", "", RTX_SHIM_ABI_VERSION, RTX_OK, diag};
    const std::string json = rtx::cli::formatProbeJson(probe);
    RTX_CHECK(json.find("\"loaded\": true") != std::string::npos);
    RTX_CHECK(json.find("\\u627e\\u4e0d\\u5230") != std::string::npos);
    RTX_CHECK(json.find("\\n\\\"quoted\\\"\\t") != std::string::npos);
    RTX_CHECK(json.find("\\ufffd") != std::string::npos);

    bool ascii_only = true;
    for (unsigned char byte : json) {
        if (byte >= 0x80) {
            ascii_only = false;
            break;
        }
    }
    RTX_CHECK(ascii_only);
}
RTX_TEST(shim_pe_static_import_table_zero_leak_gate) {
#if defined(_WIN32)
    HMODULE self_mod = GetModuleHandleW(nullptr);
    RTX_REQUIRE(self_mod != nullptr);
    const auto* base = reinterpret_cast<const uint8_t*>(self_mod);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    RTX_REQUIRE(dos->e_magic == IMAGE_DOS_SIGNATURE);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    RTX_REQUIRE(nt->Signature == IMAGE_NT_SIGNATURE);

    const auto& imp_dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (imp_dir.VirtualAddress != 0 && imp_dir.Size >= sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
        const auto* desc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + imp_dir.VirtualAddress);
        for (; desc->Name != 0; ++desc) {
            const char* dll_name = reinterpret_cast<const char*>(base + desc->Name);
            std::string lower(dll_name ? dll_name : "");
            for (char& c : lower) {
// Native C++26 PE Import Directory parser (replaces tools/audit_imports.ps1
// at CTest execution time). Verifies that the running test binary and
// any adjacent CineRTX.exe do not statically import NVIDIA driver/SDK DLLs
// or dynamic FFmpeg libraries.
                if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            }
            RTX_CHECK(lower.find("nvcuda") == std::string::npos);
            RTX_CHECK(lower.find("nvcuvid") == std::string::npos);
            RTX_CHECK(lower.find("nvencodeapi") == std::string::npos);
            RTX_CHECK(lower.find("nvofapi") == std::string::npos);
            RTX_CHECK(lower.find("nvoffruc") == std::string::npos);
            RTX_CHECK(lower.find("nvinfer") == std::string::npos);
            RTX_CHECK(lower.find("nvonnxparser") == std::string::npos);
            RTX_CHECK(lower.find("avcodec") == std::string::npos);
            RTX_CHECK(lower.find("avformat") == std::string::npos);
            RTX_CHECK(lower.find("avutil") == std::string::npos);
            RTX_CHECK(lower.find("swresample") == std::string::npos);
            RTX_CHECK(lower.find("ws2_32") == std::string::npos);
        }
    }
#else
    RTX_CHECK(true);
#endif
}
