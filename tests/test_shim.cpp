// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "test_framework.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

#include "dyn_library.h"
#include "shim/ngx_bridge_abi.h"
#include "shim/rtx_shim_abi.h"
#include "shim_probe.h"

using namespace rtx_shim;

namespace {

std::string executableDirectory() {
#if defined(_WIN32)
    wchar_t buffer[32768] = {};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, 32768);
    if (length == 0 || length >= 32768) {
        return {};
    }
    const std::filesystem::path path(std::wstring(buffer, length));
    const std::u8string directory = path.parent_path().u8string();
    return std::string(directory.begin(), directory.end());
#else
    char buffer[4096] = {};
    const ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (length <= 0) {
        return {};
    }
    return std::filesystem::path(std::string(buffer, static_cast<size_t>(length)))
        .parent_path()
        .string();
#endif
}

std::string runtimeLibrary(const char* windows_name, const char* posix_name) {
#if defined(_WIN32)
    const char* name = windows_name;
#elif defined(__APPLE__)
    const char* name = posix_name;
#else
    const char* name = posix_name;
#endif
    const std::string directory = executableDirectory();
    if (directory.empty()) {
        return name;
    }
    return (std::filesystem::path(directory) / name).string();
}

std::string shimPath() {
    const char* environment = std::getenv("RTX_TEST_SHIM_PATH");
    if (environment != nullptr && environment[0] != '\0') {
        return environment;
    }
    return runtimeLibrary("rtx_shim.dll", "librtx_shim.so");
}

std::string ngxBridgePath() {
    const char* environment = std::getenv("RTX_TEST_NGX_PATH");
    if (environment != nullptr && environment[0] != '\0') {
        return environment;
    }
    return runtimeLibrary("rtx_ngx_bridge.dll", "librtx_ngx_bridge.so");
}

// Probes whether a system library is loadable, without keeping a reference.
// Used to decide whether the D3D12/CUDA interop self-test is expected to run
// on this host at all: a machine without the NVIDIA driver must observe the
// structured RTX_ERR_DLL_MISSING, never a silent pass.
bool systemLibraryLoadable(const char* windows_name, const char* posix_name) {
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
    const std::string path = shimPath();
    RTX_REQUIRE(!path.empty());
    DynLibrary library;
    std::string error;
    RTX_CHECK(library.openExplicitPath(path, &error));
    RTX_CHECK(library.isOpen());
    RTX_CHECK(!library.path().empty());
    RTX_CHECK(error.empty());
    RTX_CHECK(DynLibrary::platformName().find("windows") != std::string::npos ||
              DynLibrary::platformName().find("linux") != std::string::npos ||
              DynLibrary::platformName().find("macos") != std::string::npos);
    RTX_CHECK(library.symbol("rtx_shim_abi_version") != nullptr);
    RTX_CHECK(library.symbol("this_symbol_does_not_exist") == nullptr);

    DynLibrary missing;
    RTX_CHECK(!missing.openExplicitPath(path + ".missing", &error));
    RTX_CHECK(!missing.isOpen());
    RTX_CHECK(!error.empty());
    RTX_CHECK(!missing.openSystemLibrary("rtx-does-not-exist-12345", &error));
    RTX_CHECK(!missing.isOpen());

    DynLibrary moved = std::move(library);
    RTX_CHECK(!library.isOpen());
    RTX_CHECK(moved.isOpen());
    RTX_CHECK(moved.symbol("rtx_shim_probe") != nullptr);
    moved.close();
    RTX_CHECK(!moved.isOpen());
}

RTX_TEST(shim_abi_version_and_result_names) {
    const std::string path = shimPath();
    RTX_REQUIRE(!path.empty());
    DynLibrary library;
    RTX_REQUIRE(library.openExplicitPath(path));
    const auto abi_version =
        library.symbolAs<uint32_t(RTX_SHIM_CALL*)()>("rtx_shim_abi_version");
    RTX_REQUIRE(abi_version != nullptr);
    RTX_CHECK_EQ(abi_version(), RTX_SHIM_ABI_VERSION);
    const auto result_name =
        library.symbolAs<const char*(RTX_SHIM_CALL*)(RtxResult)>("rtx_shim_result_name");
    RTX_REQUIRE(result_name != nullptr);
    RTX_CHECK_EQ(std::string(result_name(RTX_OK)), std::string("RTX_OK"));
    RTX_CHECK_EQ(std::string(result_name(RTX_ERR_ARCH_UNSUPPORTED)),
                 std::string("RTX_ERR_ARCH_UNSUPPORTED"));
    RTX_CHECK_EQ(std::string(result_name(RTX_ERR_NOT_IMPLEMENTED)),
                 std::string("RTX_ERR_NOT_IMPLEMENTED"));
    RTX_CHECK_EQ(std::string(result_name(static_cast<RtxResult>(999))),
                 std::string("RTX_ERR_UNKNOWN"));
}

RTX_TEST(shim_probe_diagnostics) {
    const std::string path = shimPath();
    RTX_REQUIRE(!path.empty());
    DynLibrary library;
    RTX_REQUIRE(library.openExplicitPath(path));
    const auto probe = library.symbolAs<RtxResult(RTX_SHIM_CALL*)(RtxDiagnostics*)>(
        "rtx_shim_probe");
    RTX_REQUIRE(probe != nullptr);
    RtxDiagnostics diagnostics{};
    RTX_CHECK_EQ(probe(&diagnostics), RTX_OK);
    RTX_CHECK(diagnostics.summary[0] != '\0');
    RTX_CHECK(diagnostics.component_count <= RTX_MAX_COMPONENTS);
    RTX_CHECK_EQ(probe(nullptr), RTX_OK);

    const auto create = library.symbolAs<RtxResult(RTX_SHIM_CALL*)(const RtxBackendConfig*,
                                                                  RtxBackend**)>(
        "rtx_shim_create_backend");
    RTX_REQUIRE(create != nullptr);
    const RtxBackendConfig config = validBackendConfig();
    RtxBackend* backend = reinterpret_cast<RtxBackend*>(0x1);
    RTX_CHECK_EQ(create(&config, nullptr), RTX_ERR_INVALID_ARGUMENT);
    RTX_CHECK_EQ(create(nullptr, &backend), RTX_ERR_INVALID_ARGUMENT);
    RTX_CHECK(backend == nullptr);

    RtxBackendConfig invalid = config;
    invalid.width = 0;
    RTX_CHECK_EQ(create(&invalid, &backend), RTX_ERR_INVALID_ARGUMENT);
    invalid = config;
    invalid.vsr_quality = 0;
    RTX_CHECK_EQ(create(&invalid, &backend), RTX_ERR_INVALID_ARGUMENT);
    invalid = config;
    invalid.target_width = 640;
    RTX_CHECK_EQ(create(&invalid, &backend), RTX_ERR_UNSUPPORTED);

    const RtxResult created = create(&config, &backend);
    // Maxwell gates the RTX Video features with ARCH_UNSUPPORTED; a Turing+
    // machine without the NGX SDK reports NGX; a CPU-only host reports
    // DLL_MISSING.  All are honest outcomes for this configuration.  Stage-1
    // hardware is exercised by the codec ABI below.
    RTX_CHECK(created == RTX_ERR_ARCH_UNSUPPORTED || created == RTX_ERR_NOT_IMPLEMENTED ||
              created == RTX_ERR_NGX || created == RTX_ERR_DLL_MISSING ||
              created == RTX_ERR_DRIVER_TOO_OLD || created == RTX_OK);
    if (created == RTX_OK) {
        RTX_REQUIRE(backend != nullptr);
        const auto get_info = library.symbolAs<RtxResult(RTX_SHIM_CALL*)(RtxBackend*,
                                                                        RtxDeviceInfo*)>(
            "rtx_shim_get_device_info");
        const auto get_caps =
            library.symbolAs<RtxResult(RTX_SHIM_CALL*)(RtxBackend*, uint32_t*)>(
                "rtx_shim_get_capabilities");
        const auto last_error =
            library.symbolAs<const char*(RTX_SHIM_CALL*)(RtxBackend*)>("rtx_shim_last_error");
        const auto destroy =
            library.symbolAs<void(RTX_SHIM_CALL*)(RtxBackend*)>("rtx_shim_destroy_backend");
        RTX_REQUIRE(get_info != nullptr && get_caps != nullptr && last_error != nullptr &&
                    destroy != nullptr);
        RtxDeviceInfo info{};
        uint32_t caps = 0;
        RTX_CHECK_EQ(get_info(backend, &info), RTX_OK);
        RTX_CHECK_EQ(get_caps(backend, &caps), RTX_OK);
        RTX_CHECK(info.name[0] != '\0');
        RTX_CHECK(last_error(backend) != nullptr);
        destroy(backend);
    } else {
        RTX_CHECK(backend == nullptr);
        const auto destroy =
            library.symbolAs<void(RTX_SHIM_CALL*)(RtxBackend*)>("rtx_shim_destroy_backend");
        const auto last_error =
            library.symbolAs<const char*(RTX_SHIM_CALL*)(RtxBackend*)>("rtx_shim_last_error");
        RTX_REQUIRE(destroy != nullptr && last_error != nullptr);
        destroy(nullptr);
        RTX_CHECK(last_error(nullptr) != nullptr);
        destroy(nullptr);
    }
}

RTX_TEST(shim_hardware_abi_exports) {
    const std::string path = shimPath();
    RTX_REQUIRE(!path.empty());
    DynLibrary library;
    RTX_REQUIRE(library.openExplicitPath(path));

    const char* symbols[] = {
        "rtx_shim_decoder_create",       "rtx_shim_decoder_destroy",
        "rtx_shim_decoder_submit",       "rtx_shim_decoder_receive",
        "rtx_shim_decoder_flush",        "rtx_shim_decoder_release_frame",
        "rtx_shim_decoder_download",     "rtx_shim_decoder_get_stats",
        "rtx_shim_decoder_last_error",   "rtx_shim_encoder_create",
        "rtx_shim_encoder_destroy",      "rtx_shim_encoder_encode",
        "rtx_shim_encoder_encode_host",  "rtx_shim_encoder_flush",
        "rtx_shim_encoder_get_sequence_header",
        "rtx_shim_encoder_get_stats",    "rtx_shim_encoder_last_error",
    };
    for (const char* symbol : symbols) {
        RTX_CHECK(library.symbol(symbol) != nullptr);
    }

    const auto decoder_create =
        library.symbolAs<RtxResult(RTX_SHIM_CALL*)(const RtxDecoderConfig*, RtxDecoder**)>(
            "rtx_shim_decoder_create");
    const auto encoder_create =
        library.symbolAs<RtxResult(RTX_SHIM_CALL*)(const RtxEncoderConfig*, RtxEncoder**)>(
            "rtx_shim_encoder_create");
    const auto decoder_receive =
        library.symbolAs<RtxResult(RTX_SHIM_CALL*)(RtxDecoder*, RtxFrameDesc*)>(
            "rtx_shim_decoder_receive");
    const auto encoder_encode =
        library.symbolAs<RtxResult(RTX_SHIM_CALL*)(RtxEncoder*, const RtxFrameDesc*,
                                                  RtxPacketDesc*)>("rtx_shim_encoder_encode");
    RTX_REQUIRE(decoder_create != nullptr && encoder_create != nullptr &&
                decoder_receive != nullptr && encoder_encode != nullptr);

    RtxDecoder* decoder = reinterpret_cast<RtxDecoder*>(0x1);
    RtxEncoder* encoder = reinterpret_cast<RtxEncoder*>(0x1);
    RTX_CHECK_EQ(decoder_create(nullptr, &decoder), RTX_ERR_INVALID_ARGUMENT);
    RTX_CHECK(decoder == nullptr);
    RTX_CHECK_EQ(encoder_create(nullptr, &encoder), RTX_ERR_INVALID_ARGUMENT);
    RTX_CHECK(encoder == nullptr);
    RTX_CHECK_EQ(decoder_receive(nullptr, nullptr), RTX_ERR_INVALID_ARGUMENT);
    RTX_CHECK_EQ(encoder_encode(nullptr, nullptr, nullptr), RTX_ERR_INVALID_ARGUMENT);

    RtxDecoderConfig decoder_config{};
    RTX_CHECK_EQ(decoder_create(&decoder_config, &decoder), RTX_ERR_INVALID_ARGUMENT);
    RtxEncoderConfig encoder_config{};
    RTX_CHECK_EQ(encoder_create(&encoder_config, &encoder), RTX_ERR_INVALID_ARGUMENT);
}

RTX_TEST(shim_stage1_backend_gate) {
    const std::string path = shimPath();
    RTX_REQUIRE(!path.empty());
    DynLibrary library;
    RTX_REQUIRE(library.openExplicitPath(path));
    const auto create = library.symbolAs<RtxResult(RTX_SHIM_CALL*)(const RtxBackendConfig*,
                                                                  RtxBackend**)>(
        "rtx_shim_create_backend");
    const auto destroy =
        library.symbolAs<void(RTX_SHIM_CALL*)(RtxBackend*)>("rtx_shim_destroy_backend");
    const auto get_info = library.symbolAs<RtxResult(RTX_SHIM_CALL*)(RtxBackend*,
                                                                    RtxDeviceInfo*)>(
        "rtx_shim_get_device_info");
    RTX_REQUIRE(create != nullptr && destroy != nullptr && get_info != nullptr);

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
    const RtxResult created = create(&config, &backend);
    RTX_CHECK(created == RTX_OK || created == RTX_ERR_DLL_MISSING ||
              created == RTX_ERR_ARCH_UNSUPPORTED || created == RTX_ERR_DRIVER_TOO_OLD);
    if (created == RTX_OK) {
        RTX_REQUIRE(backend != nullptr);
        RtxDeviceInfo info{};
        RTX_CHECK_EQ(get_info(backend, &info), RTX_OK);
        RTX_CHECK(info.name[0] != '\0');
        destroy(backend);
    } else {
        RTX_CHECK(backend == nullptr);
    }
}

RTX_TEST(shim_ngx_bridge_abi) {
    const std::string path = ngxBridgePath();
    RTX_REQUIRE(!path.empty());
    DynLibrary library;
    std::string error;
    RTX_REQUIRE(library.openExplicitPath(path, &error));

    const auto query_caps = library.symbolAs<RtxResult(RTX_SHIM_CALL*)(RtxNgxBridgeCaps*)>(
        "rtx_ngx_bridge_query_caps");
    const auto create =
        library.symbolAs<RtxResult(RTX_SHIM_CALL*)(const RtxNgxBridgeConfig*, RtxNgxBridge**)>(
            "rtx_ngx_bridge_create");
    const auto destroy =
        library.symbolAs<void(RTX_SHIM_CALL*)(RtxNgxBridge*)>("rtx_ngx_bridge_destroy");
    const auto get_textures = library.symbolAs<RtxResult(RTX_SHIM_CALL*)(
        RtxNgxBridge*, RtxNgxTextureDesc*, RtxNgxTextureDesc*, uint64_t*, uint64_t*)>(
        "rtx_ngx_bridge_get_textures");
    const auto process = library.symbolAs<RtxResult(RTX_SHIM_CALL*)(RtxNgxBridge*, uint64_t)>(
        "rtx_ngx_bridge_process");
    const auto flush =
        library.symbolAs<RtxResult(RTX_SHIM_CALL*)(RtxNgxBridge*)>("rtx_ngx_bridge_flush");
    const auto reset =
        library.symbolAs<RtxResult(RTX_SHIM_CALL*)(RtxNgxBridge*, int32_t, int32_t)>(
            "rtx_ngx_bridge_reset");
    const auto last_error =
        library.symbolAs<const char*(RTX_SHIM_CALL*)(RtxNgxBridge*)>(
            "rtx_ngx_bridge_last_error");
    RTX_REQUIRE(query_caps != nullptr && create != nullptr && destroy != nullptr &&
                get_textures != nullptr && process != nullptr && flush != nullptr &&
                reset != nullptr && last_error != nullptr);

    RtxNgxBridgeCaps caps{};
    RTX_CHECK_EQ(query_caps(&caps), RTX_OK);
    // With RTX Video SDK present and deployed, vsr/truehdr report 1; in stub environments they report 0.
    RTX_CHECK(caps.vsr_available == 0 || caps.vsr_available == 1);
    RTX_CHECK(caps.truehdr_available == 0 || caps.truehdr_available == 1);
    RTX_CHECK_EQ(caps.d3d12_device_created, 0);
    RTX_CHECK(caps.diagnostics[0] != '\0');
    RTX_CHECK_EQ(query_caps(nullptr), RTX_ERR_INVALID_ARGUMENT);

    RtxNgxBridgeConfig config{};
    config.input_width = 1920;
    config.input_height = 1080;
    config.output_width = 3840;
    config.output_height = 2160;
    config.vsr_quality = 2;
    config.enable_truehdr = 1;
    RtxNgxBridge* bridge = reinterpret_cast<RtxNgxBridge*>(0x1);
    RTX_CHECK_EQ(create(nullptr, &bridge), RTX_ERR_INVALID_ARGUMENT);
    RTX_CHECK_EQ(create(&config, nullptr), RTX_ERR_INVALID_ARGUMENT);
    RTX_CHECK(bridge == nullptr);
    config.vsr_quality = 5;
    RTX_CHECK_EQ(create(&config, &bridge), RTX_ERR_INVALID_ARGUMENT);
    config.vsr_quality = 2;
    // create() now builds the headless D3D12 device and the shared textures.
    // Without the RTX Video SDK it still refuses to hand out a bridge, but the
    // exact code depends on whether the machine exposes a D3D12 adapter.
    const RtxResult created = create(&config, &bridge);
    RTX_CHECK(created == RTX_OK || created == RTX_ERR_NGX ||
              created == RTX_ERR_ARCH_UNSUPPORTED || created == RTX_ERR_NOT_IMPLEMENTED);
    if (created != RTX_OK) {
        RTX_CHECK(bridge == nullptr);
    } else {
        destroy(bridge);
        bridge = nullptr;
    }

    RtxNgxTextureDesc input{};
    RtxNgxTextureDesc output{};
    uint64_t input_handle = 1;
    uint64_t output_handle = 2;
    RTX_CHECK_EQ(get_textures(nullptr, &input, &output, &input_handle, &output_handle),
                 RTX_ERR_INVALID_ARGUMENT);
    RTX_CHECK_EQ(input_handle, static_cast<uint64_t>(0));
    RTX_CHECK_EQ(output_handle, static_cast<uint64_t>(0));
    RTX_CHECK_EQ(get_textures(nullptr, nullptr, &output, &input_handle, &output_handle),
                 RTX_ERR_INVALID_ARGUMENT);
    RTX_CHECK_EQ(get_textures(nullptr, &input, &output, nullptr, &output_handle),
                 RTX_ERR_INVALID_ARGUMENT);
    RTX_CHECK_EQ(process(nullptr, 1), RTX_ERR_INVALID_ARGUMENT);
    RTX_CHECK_EQ(flush(nullptr), RTX_ERR_INVALID_ARGUMENT);
    RTX_CHECK_EQ(reset(nullptr, 1920, 1080), RTX_ERR_INVALID_ARGUMENT);
    RTX_CHECK_EQ(reset(reinterpret_cast<RtxNgxBridge*>(0x1), 0, 1080),
                 RTX_ERR_INVALID_ARGUMENT);
    RTX_CHECK(last_error(nullptr) != nullptr);
    RTX_CHECK(std::string(last_error(nullptr)).find("SDK") != std::string::npos);
    destroy(nullptr);
}

RTX_TEST(shim_ngx_bridge_remaster_abi) {
    const std::string path = ngxBridgePath();
    RTX_REQUIRE(!path.empty());
    DynLibrary library;
    std::string error;
    RTX_REQUIRE(library.openExplicitPath(path, &error));

    const auto abi_version =
        library.symbolAs<uint32_t(RTX_SHIM_CALL*)()>("rtx_ngx_bridge_abi_version");
    const auto remaster = library.symbolAs<RtxResult(RTX_SHIM_CALL*)(
        RtxNgxBridge*, const RtxNgxRemasterInput*, RtxNgxRemasterOutput*,
        RtxNgxRemasterStats*)>("rtx_ngx_bridge_remaster");
    const auto get_stats =
        library.symbolAs<RtxResult(RTX_SHIM_CALL*)(RtxNgxBridge*, RtxNgxRemasterStats*)>(
            "rtx_ngx_bridge_get_remaster_stats");
    RTX_REQUIRE(abi_version != nullptr);
    RTX_REQUIRE(remaster != nullptr);
    RTX_REQUIRE(get_stats != nullptr);
    // The remaster contract rides on ABI v2; a different number means the
    // bridge and the core disagree about the appended struct layout.
    RTX_CHECK_EQ(abi_version(), RTX_NGX_BRIDGE_ABI_VERSION);

    RtxNgxRemasterStats stats{};
    RTX_CHECK_EQ(get_stats(nullptr, &stats), RTX_ERR_INVALID_ARGUMENT);
    RTX_CHECK_EQ(get_stats(nullptr, nullptr), RTX_ERR_INVALID_ARGUMENT);

    RtxNgxRemasterInput input{};
    RtxNgxRemasterOutput output{};
    RTX_CHECK_EQ(remaster(nullptr, &input, &output, &stats), RTX_ERR_INVALID_ARGUMENT);
    // A null input / null output must never reach the CUDA driver.  The fake
    // pointer below is only ever paired with an invalid input: per the ABI a
    // non-null bridge is a real handle and would be dereferenced.
    const auto fake = reinterpret_cast<RtxNgxBridge*>(0x1);
    RTX_CHECK_EQ(remaster(fake, nullptr, &output, &stats), RTX_ERR_INVALID_ARGUMENT);
    RTX_CHECK_EQ(remaster(fake, &input, nullptr, &stats), RTX_ERR_INVALID_ARGUMENT);
    // Bad formats / matrices / pitches are rejected before any GPU work.
    input.y_plane = 0;
    input.format = RTX_PIXEL_NV12;
    RTX_CHECK_EQ(remaster(fake, &input, &output, &stats), RTX_ERR_INVALID_ARGUMENT);
    input.y_plane = 0x1000;
    input.uv_plane = 0x2000;
    input.format = RTX_PIXEL_RGBA16F;
    RTX_CHECK_EQ(remaster(fake, &input, &output, &stats), RTX_ERR_INVALID_ARGUMENT);
    input.format = RTX_PIXEL_P010;
    input.y_pitch = 4096;
    input.uv_pitch = 4096;
    input.matrix = 7;
    RTX_CHECK_EQ(remaster(fake, &input, &output, &stats), RTX_ERR_INVALID_ARGUMENT);
    input.matrix = 0;
    input.full_range = 2;
    RTX_CHECK_EQ(remaster(fake, &input, &output, &stats), RTX_ERR_INVALID_ARGUMENT);
    input.full_range = 0;
    // Zero pitches are rejected as well.
    input.y_pitch = 0;
    RTX_CHECK_EQ(remaster(fake, &input, &output, &stats), RTX_ERR_INVALID_ARGUMENT);
    input.y_pitch = 4096;
    input.uv_pitch = 0;
    RTX_CHECK_EQ(remaster(fake, &input, &output, &stats), RTX_ERR_INVALID_ARGUMENT);
}

RTX_TEST(shim_ngx_d3d12_interop_selftest) {
    const std::string path = ngxBridgePath();
    RTX_REQUIRE(!path.empty());
    DynLibrary library;
    std::string error;
    RTX_REQUIRE(library.openExplicitPath(path, &error));
    const auto selftest =
        library.symbolAs<RtxResult(RTX_SHIM_CALL*)(int32_t, int32_t, char*, size_t)>(
            "rtx_ngx_bridge_selftest");
    RTX_REQUIRE(selftest != nullptr);

    // Argument validation is host-independent.
    char detail[512] = {};
    RTX_CHECK_EQ(selftest(0, 256, detail, sizeof(detail)), RTX_ERR_INVALID_ARGUMENT);
    RTX_CHECK_EQ(selftest(256, 0, detail, sizeof(detail)), RTX_ERR_INVALID_ARGUMENT);
    RTX_CHECK_EQ(selftest(256, 256, nullptr, sizeof(detail)), RTX_ERR_INVALID_ARGUMENT);

    // The 256x256 gradient round trip needs the CUDA driver (nvcuda.dll) and
    // the headless D3D12 stack (d3d12.dll/dxgi.dll).  A host without them must
    // observe the structured error, not a crash and not a silent pass.
    const bool have_cuda = systemLibraryLoadable("nvcuda.dll", "libcuda.so.1");
    const bool have_d3d12 = systemLibraryLoadable("d3d12.dll", nullptr);

    detail[0] = '\0';
    const RtxResult result = selftest(256, 256, detail, sizeof(detail));
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
    probe.path = "C:\\shim\\rtx_shim.dll";
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
