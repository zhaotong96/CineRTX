// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Shim and NGX bridge ABI contract tests.
//
// The single-EXE deliverable ships no sidecar DLLs: rtx_shim, rtx_ngx_bridge and
// rtx_fruc_bridge are static archives that rtx_core links, so these tests call
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

// Probes whether a system library is loadable, without keeping a reference.
// Used to decide whether the D3D12/CUDA interop self-test is expected to run
// on this host at all: a machine without the NVIDIA driver must observe the
// structured RTX_ERR_DLL_MISSING, never a silent pass.
bool systemLibraryLoadable(const char* windows_name, const char* posix_name) {
    (void)posix_name;
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
    RTX_REQUIRE(library.openSystemLibrary(probeLibraryName(), &error));
    RTX_CHECK(library.isOpen());
    RTX_CHECK(!library.path().empty());
    RTX_CHECK(error.empty());
    // "posix" is what dyn_library_posix.cpp reports; the loader has no separate
    // linux/macos implementation, so both spellings are part of the contract.
    const std::string platform = DynLibrary::platformName();
    RTX_CHECK(platform == "windows" || platform == "linux" || platform == "macos" ||
              platform == "posix");
    RTX_CHECK(library.symbol(probeSymbolName()) != nullptr);
    RTX_CHECK(library.symbol("this_symbol_does_not_exist") == nullptr);

    // A bare name never resolves outside System32, and an explicit path that
    // does not exist must report a structured error rather than crash.
    DynLibrary missing;
    RTX_CHECK(!missing.openSystemLibrary("rtx-does-not-exist-12345", &error));
    RTX_CHECK(!missing.isOpen());
    RTX_CHECK(!error.empty());
    RTX_CHECK(!missing.openExplicitPath("rtx-does-not-exist-12345.dll", &error));
    RTX_CHECK(!missing.isOpen());
    RTX_CHECK(!error.empty());
    RTX_CHECK(!missing.openExplicitPath("", &error));
    RTX_CHECK(!error.empty());

    // Ownership transfers on move; the source is left closed.
    DynLibrary moved = std::move(library);
    RTX_CHECK(!library.isOpen());
    RTX_CHECK(moved.isOpen());
    RTX_CHECK(moved.symbol(probeSymbolName()) != nullptr);
    moved.close();
    RTX_CHECK(!moved.isOpen());
    RTX_CHECK(moved.path().empty());
    RTX_CHECK(moved.symbol(probeSymbolName()) == nullptr);
}

RTX_TEST(shim_abi_version_and_result_names) {
    RTX_CHECK_EQ(rtx_shim_abi_version(), RTX_SHIM_ABI_VERSION);
    RTX_CHECK_EQ(RTX_SHIM_ABI_VERSION, 2u);
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
    // Maxwell gates the RTX Video features with ARCH_UNSUPPORTED; a Turing+
    // machine without the NGX SDK reports NGX; a CPU-only host reports
    // DLL_MISSING.  All are honest outcomes for this configuration.  Stage-1
    // hardware is exercised by the codec ABI below.
    RTX_CHECK(created == RTX_ERR_ARCH_UNSUPPORTED || created == RTX_ERR_NOT_IMPLEMENTED ||
              created == RTX_ERR_NGX || created == RTX_ERR_DLL_MISSING ||
              created == RTX_ERR_DRIVER_TOO_OLD || created == RTX_OK);
    if (created == RTX_OK) {
        RTX_REQUIRE(backend != nullptr);
        RtxDeviceInfo info{};
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
    // Every codec ABI entry point must be present in the image.  Statically
    // linked, a missing one is a link error rather than a null symbol, so the
    // contract is proven twice: once by taking the address of each function and
    // again by the null-argument behaviour each of them documents.
    const std::uintptr_t entry_points[] = {
        reinterpret_cast<std::uintptr_t>(&rtx_shim_decoder_create),
        reinterpret_cast<std::uintptr_t>(&rtx_shim_decoder_destroy),
        reinterpret_cast<std::uintptr_t>(&rtx_shim_decoder_submit),
        reinterpret_cast<std::uintptr_t>(&rtx_shim_decoder_receive),
        reinterpret_cast<std::uintptr_t>(&rtx_shim_decoder_flush),
        reinterpret_cast<std::uintptr_t>(&rtx_shim_decoder_release_frame),
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
    // A pure stage-1 configuration (no upscale, no RTX Video features) must be
    // accepted on every CUDA-capable host, including Maxwell.
    RtxBackendConfig config{};
    config.width = 1920;
    config.height = 1080;
    config.target_width = 1920;
    config.target_height = 1080;
    config.vsr_quality = 1;
    config.vram_budget_bytes = 512ull * 1024 * 1024;

    RtxBackend* backend = nullptr;
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
    // With RTX Video SDK present and deployed, vsr/truehdr report 1; in stub environments they report 0.
    RTX_CHECK(caps.vsr_available == 0 || caps.vsr_available == 1);
    RTX_CHECK(caps.truehdr_available == 0 || caps.truehdr_available == 1);
    RTX_CHECK_EQ(caps.d3d12_device_created, 0);
    RTX_CHECK(caps.diagnostics[0] != '\0');
    RTX_CHECK_EQ(rtx_ngx_bridge_query_caps(nullptr), RTX_ERR_INVALID_ARGUMENT);

    RtxNgxBridgeConfig config{};
    config.input_width = 1920;
    config.input_height = 1080;
    config.output_width = 3840;
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
    // create() now builds the headless D3D12 device and the shared textures.
    // Without the RTX Video SDK it still refuses to hand out a bridge, but the
    // exact code depends on whether the machine exposes a D3D12 adapter.
    const RtxResult created = rtx_ngx_bridge_create(&config, &bridge);
    RTX_CHECK(created == RTX_OK || created == RTX_ERR_NGX ||
              created == RTX_ERR_ARCH_UNSUPPORTED || created == RTX_ERR_NOT_IMPLEMENTED);
    if (created != RTX_OK) {
        RTX_CHECK(bridge == nullptr);
    } else {
        rtx_ngx_bridge_destroy(bridge);
        bridge = nullptr;
    }

    RtxNgxTextureDesc input{};
    RtxNgxTextureDesc output{};
    uint64_t input_handle = 1;
    uint64_t output_handle = 2;
    RTX_CHECK_EQ(rtx_ngx_bridge_get_textures(nullptr, &input, &output, &input_handle,
                                             &output_handle),
                 RTX_ERR_INVALID_ARGUMENT);
    RTX_CHECK_EQ(input_handle, static_cast<uint64_t>(0));
    RTX_CHECK_EQ(output_handle, static_cast<uint64_t>(0));
    RTX_CHECK_EQ(rtx_ngx_bridge_get_textures(nullptr, nullptr, &output, &input_handle,
                                             &output_handle),
                 RTX_ERR_INVALID_ARGUMENT);
    RTX_CHECK_EQ(rtx_ngx_bridge_get_textures(nullptr, &input, &output, nullptr, &output_handle),
                 RTX_ERR_INVALID_ARGUMENT);
    RTX_CHECK_EQ(rtx_ngx_bridge_process(nullptr, 1), RTX_ERR_INVALID_ARGUMENT);
    RTX_CHECK_EQ(rtx_ngx_bridge_flush(nullptr), RTX_ERR_INVALID_ARGUMENT);
    RTX_CHECK_EQ(rtx_ngx_bridge_reset(nullptr, 1920, 1080), RTX_ERR_INVALID_ARGUMENT);
    RTX_CHECK_EQ(rtx_ngx_bridge_reset(reinterpret_cast<RtxNgxBridge*>(0x1), 0, 1080),
                 RTX_ERR_INVALID_ARGUMENT);
    RTX_CHECK(rtx_ngx_bridge_last_error(nullptr) != nullptr);
    RTX_CHECK(std::string(rtx_ngx_bridge_last_error(nullptr)).find("SDK") != std::string::npos);
    rtx_ngx_bridge_destroy(nullptr);
}

RTX_TEST(shim_ngx_bridge_remaster_abi) {
    // The remaster contract rides on ABI v2; a different number means the
    // bridge and the core disagree about the appended struct layout.
    RTX_CHECK_EQ(rtx_ngx_bridge_abi_version(), RTX_NGX_BRIDGE_ABI_VERSION);

    RtxNgxRemasterStats stats{};
    RTX_CHECK_EQ(rtx_ngx_bridge_get_remaster_stats(nullptr, &stats), RTX_ERR_INVALID_ARGUMENT);
    RTX_CHECK_EQ(rtx_ngx_bridge_get_remaster_stats(nullptr, nullptr), RTX_ERR_INVALID_ARGUMENT);

    RtxNgxRemasterInput input{};
    RtxNgxRemasterOutput output{};
    RTX_CHECK_EQ(rtx_ngx_bridge_remaster(nullptr, &input, &output, &stats),
                 RTX_ERR_INVALID_ARGUMENT);
    // A null input / null output must never reach the CUDA driver.  The fake
    // pointer below is only ever paired with an invalid input: per the ABI a
    // non-null bridge is a real handle and would be dereferenced.
    const auto fake = reinterpret_cast<RtxNgxBridge*>(0x1);
    RTX_CHECK_EQ(rtx_ngx_bridge_remaster(fake, nullptr, &output, &stats),
                 RTX_ERR_INVALID_ARGUMENT);
    RTX_CHECK_EQ(rtx_ngx_bridge_remaster(fake, &input, nullptr, &stats),
                 RTX_ERR_INVALID_ARGUMENT);
    // Bad formats / matrices / pitches are rejected before any GPU work.
    input.y_plane = 0;
    input.format = RTX_PIXEL_NV12;
    RTX_CHECK_EQ(rtx_ngx_bridge_remaster(fake, &input, &output, &stats),
                 RTX_ERR_INVALID_ARGUMENT);
    input.y_plane = 0x1000;
    input.uv_plane = 0x2000;
    input.format = RTX_PIXEL_RGBA16F;
    RTX_CHECK_EQ(rtx_ngx_bridge_remaster(fake, &input, &output, &stats),
                 RTX_ERR_INVALID_ARGUMENT);
    input.format = RTX_PIXEL_P010;
    input.y_pitch = 4096;
    input.uv_pitch = 4096;
    input.matrix = 7;
    RTX_CHECK_EQ(rtx_ngx_bridge_remaster(fake, &input, &output, &stats),
                 RTX_ERR_INVALID_ARGUMENT);
    input.matrix = 0;
    input.full_range = 2;
    RTX_CHECK_EQ(rtx_ngx_bridge_remaster(fake, &input, &output, &stats),
                 RTX_ERR_INVALID_ARGUMENT);
    input.full_range = 0;
    // Zero pitches are rejected as well.
    input.y_pitch = 0;
    RTX_CHECK_EQ(rtx_ngx_bridge_remaster(fake, &input, &output, &stats),
                 RTX_ERR_INVALID_ARGUMENT);
    input.y_pitch = 4096;
    input.uv_pitch = 0;
    RTX_CHECK_EQ(rtx_ngx_bridge_remaster(fake, &input, &output, &stats),
                 RTX_ERR_INVALID_ARGUMENT);
}

RTX_TEST(shim_ngx_d3d12_interop_selftest) {
    // Argument validation is host-independent.
    char detail[512] = {};
    RTX_CHECK_EQ(rtx_ngx_bridge_selftest(0, 256, detail, sizeof(detail)),
                 RTX_ERR_INVALID_ARGUMENT);
    RTX_CHECK_EQ(rtx_ngx_bridge_selftest(256, 0, detail, sizeof(detail)),
                 RTX_ERR_INVALID_ARGUMENT);
    RTX_CHECK_EQ(rtx_ngx_bridge_selftest(256, 256, nullptr, sizeof(detail)),
                 RTX_ERR_INVALID_ARGUMENT);

    // The 256x256 gradient round trip needs the CUDA driver (nvcuda.dll) and
    // the headless D3D12 stack (d3d12.dll/dxgi.dll).  A host without them must
    // observe the structured error, not a crash and not a silent pass.
    const bool have_cuda = systemLibraryLoadable("nvcuda.dll", "libcuda.so.1");
    const bool have_d3d12 = systemLibraryLoadable("d3d12.dll", nullptr);

    detail[0] = '\0';
    const RtxResult result = rtx_ngx_bridge_selftest(256, 256, detail, sizeof(detail));
    if (!have_cuda || !have_d3d12) {
        RTX_CHECK_EQ(result, RTX_ERR_DLL_MISSING);
        RTX_CHECK(detail[0] != '\0');
        return;
    }
    // Supported hardware: the round trip must pass, and the detail must name
    // the geometry and direction so a channel mismatch or context detach is
    // visible in the log instead of hiding behind a bare error code.
    RTX_CHECK_EQ(result, RTX_OK);
    const std::string message(detail);
    RTX_CHECK(message.find("round trip OK") != std::string::npos);
    RTX_CHECK(message.find("256x256 RGBA16F") != std::string::npos);
    RTX_CHECK(message.find("D3D12 -> CUDA -> D3D12") != std::string::npos);
    // W6 anti-TDR evidence: the hardware queue depth cap was exercised, the
    // pack stream carries the driver's top priority, and the adapter-removal
    // declaration status is reported rather than assumed.
    RTX_CHECK(message.find("queue depth cap 2") != std::string::npos);
    RTX_CHECK(message.find("pack stream priority high") != std::string::npos);
    RTX_CHECK(message.find("adapter removal") != std::string::npos);
}

RTX_TEST(shim_probe_json_is_ascii_safe) {
    rtx::cli::ShimProbeResult probe;
    probe.loaded = true;
    probe.abi_version = RTX_SHIM_ABI_VERSION;
    probe.result = RTX_OK;
    probe.path = "C:\\Program Files\\CineRTX\\CineRTX.exe";
    probe.diagnostics.component_count = 1;

    RtxComponentInfo& component = probe.diagnostics.components[0];
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
