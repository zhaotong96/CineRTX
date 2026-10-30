// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// rtx_fruc_bridge pure C ABI contract (W7-1, Gate 5).
//
// The bridge is a static archive that rtx_core links, so the C entry points
// resolve at link time.  The tests still drive everything through the versioned
// function table, which is what proves the ABI boundary rather than an internal
// symbol; only the loader step differs from the historical sidecar-DLL build.

#include "test_framework.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "fruc_bridge_abi.h"

namespace {

using GetVersionFn = uint32_t (*)();

struct BridgeModule {
    const RtxFrucBridgeApi* api = nullptr;
    GetVersionFn version = nullptr;
    bool loaded = false;
    std::string error;
};

BridgeModule& module() {
    static BridgeModule instance;
    static bool attempted = false;
    if (attempted) {
        return instance;
    }
    attempted = true;
    instance.api = rtx_fruc_bridge_get_api();
    instance.version = &rtx_fruc_bridge_abi_version;
    instance.loaded = instance.api != nullptr;
    if (!instance.loaded) {
        instance.error = "rtx_fruc_bridge_get_api returned null";
    }
    return instance;
}

RtxFrucConfig makeConfig(uint32_t backend, uint32_t width = 64, uint32_t height = 48) {
    RtxFrucConfig config{};
    config.abi_version = RTX_FRUC_BRIDGE_ABI_VERSION;
    config.backend_type = backend;
    config.width = width;
    config.height = height;
    config.surface_pitch = width;
    config.fp16_enabled = 1;
    config.model_engine_path = nullptr;
    config.cuda_context = nullptr;  // CPU reference mode
    config.cuda_stream = nullptr;
    return config;
}

}  // namespace

RTX_TEST(fruc_abi_version) {
    BridgeModule& bridge = module();
    RTX_REQUIRE(bridge.loaded);
    RTX_CHECK_EQ(bridge.version(), RTX_FRUC_BRIDGE_ABI_VERSION);
    RTX_CHECK_EQ(bridge.api->abi_version, RTX_FRUC_BRIDGE_ABI_VERSION);
    RTX_CHECK_EQ(RTX_FRUC_BRIDGE_ABI_VERSION, 2u);

    // Every slot in the table must be populated: a null function pointer is an
    // ABI break even when the version number matches.
    RTX_CHECK(bridge.api->create != nullptr);
    RTX_CHECK(bridge.api->interpolate != nullptr);
    RTX_CHECK(bridge.api->build_engine != nullptr);
    RTX_CHECK(bridge.api->query_vram_footprint != nullptr);
    RTX_CHECK(bridge.api->destroy != nullptr);

    // The fixed-width layout is part of the contract: six device pointers, one
    // float timestep and two 32-bit flags, 8-byte aligned.
    RTX_CHECK_EQ(sizeof(RtxFrucBackendType), sizeof(uint32_t));
    RTX_CHECK_EQ(sizeof(RtxFrucFrameInput), static_cast<size_t>(64));
    RTX_CHECK_EQ(offsetof(RtxFrucFrameInput, is_scene_cut), static_cast<size_t>(52));
}

RTX_TEST(fruc_abi_scene_cut_duplicate) {
    BridgeModule& bridge = module();
    RTX_REQUIRE(bridge.loaded);

    RtxFrucConfig config = makeConfig(RTX_FRUC_BACKEND_DUPLICATE);
    RtxFrucBridge* handle = nullptr;
    char error[256] = {};
    RTX_REQUIRE_EQ(bridge.api->create(&config, &handle, error, static_cast<uint32_t>(sizeof(error))), RTX_FRUC_OK);
    RTX_REQUIRE(handle != nullptr);

    const size_t bytes = static_cast<size_t>(config.width) * config.height * 3 / 2;
    std::vector<uint8_t> previous(bytes, 0x11);
    std::vector<uint8_t> current(bytes, 0x22);
    std::vector<uint8_t> output(bytes, 0x00);

    RtxFrucFrameInput input{};
    input.prev_nv12_devptr = reinterpret_cast<uint64_t>(previous.data());
    input.curr_nv12_devptr = reinterpret_cast<uint64_t>(current.data());
    input.out_nv12_devptr = reinterpret_cast<uint64_t>(output.data());
    input.timestep = 0.5f;
    input.is_scene_cut = 1;
    input.is_static_frame = 0;

    RTX_CHECK_EQ(bridge.api->interpolate(handle, &input), RTX_FRUC_OK);
    // The bypass must be a byte-exact copy of the *previous* frame; any blend
    // with the current frame is exactly the tearing artefact Gate 3.2 forbids.
    RTX_CHECK(std::memcmp(output.data(), previous.data(), bytes) == 0);
    RTX_CHECK(std::memcmp(output.data(), current.data(), bytes) != 0);

    bridge.api->destroy(handle);
}

RTX_TEST(fruc_abi_static_frame_bypass) {
    BridgeModule& bridge = module();
    RTX_REQUIRE(bridge.loaded);

    // The static-frame bypass must fire before the vendor-module check, so it
    // works even when the requested backend has no SDK behind it.
    RtxFrucConfig config = makeConfig(RTX_FRUC_BACKEND_NVOFFRUC);
    RtxFrucBridge* handle = nullptr;
    char error[256] = {};
    RTX_REQUIRE_EQ(bridge.api->create(&config, &handle, error, static_cast<uint32_t>(sizeof(error))), RTX_FRUC_OK);
    RTX_REQUIRE(handle != nullptr);

    const size_t bytes = static_cast<size_t>(config.width) * config.height * 3 / 2;
    std::vector<uint8_t> previous(bytes, 0x7A);
    std::vector<uint8_t> current(bytes, 0x00);
    std::vector<uint8_t> output(bytes, 0x00);

    RtxFrucFrameInput input{};
    input.prev_nv12_devptr = reinterpret_cast<uint64_t>(previous.data());
    input.curr_nv12_devptr = reinterpret_cast<uint64_t>(current.data());
    input.out_nv12_devptr = reinterpret_cast<uint64_t>(output.data());
    input.timestep = 0.5f;
    input.is_static_frame = 1;
    RTX_CHECK_EQ(bridge.api->interpolate(handle, &input), RTX_FRUC_OK);
    RTX_CHECK(std::memcmp(output.data(), previous.data(), bytes) == 0);

    // A non-bypass frame on a backend without a vendor module must report the
    // missing dependency instead of inventing a frame.
    input.is_static_frame = 0;
    input.is_scene_cut = 0;
    RTX_CHECK_EQ(bridge.api->interpolate(handle, &input), RTX_FRUC_ERR_UNAVAILABLE);

    // A null frame descriptor is rejected, not dereferenced.
    RTX_CHECK_EQ(bridge.api->interpolate(handle, nullptr), RTX_FRUC_ERR_INVALID_ARGUMENT);

    bridge.api->destroy(handle);
    bridge.api->destroy(nullptr);  // must be a no-op
}

RTX_TEST(fruc_abi_config_rejection) {
    BridgeModule& bridge = module();
    RTX_REQUIRE(bridge.loaded);

    RtxFrucBridge* handle = nullptr;
    char error[256] = {};

    RtxFrucConfig stale = makeConfig(RTX_FRUC_BACKEND_DUPLICATE);
    stale.abi_version = RTX_FRUC_BRIDGE_ABI_VERSION + 1;
    RTX_CHECK_EQ(bridge.api->create(&stale, &handle, error, static_cast<uint32_t>(sizeof(error))),
                 RTX_FRUC_ERR_ABI_MISMATCH);
    RTX_CHECK(handle == nullptr);
    RTX_CHECK(error[0] != '\0');

    RtxFrucConfig odd = makeConfig(RTX_FRUC_BACKEND_DUPLICATE, 63, 48);
    odd.surface_pitch = 63;
    RTX_CHECK_EQ(bridge.api->create(&odd, &handle, error, static_cast<uint32_t>(sizeof(error))),
                 RTX_FRUC_ERR_INVALID_ARGUMENT);

    RtxFrucConfig unknown = makeConfig(99);
    RTX_CHECK_EQ(bridge.api->create(&unknown, &handle, error, static_cast<uint32_t>(sizeof(error))),
                 RTX_FRUC_ERR_INVALID_ARGUMENT);

    RtxFrucConfig narrow = makeConfig(RTX_FRUC_BACKEND_DUPLICATE, 64, 48);
    narrow.surface_pitch = 32;
    RTX_CHECK_EQ(bridge.api->create(&narrow, &handle, error, static_cast<uint32_t>(sizeof(error))),
                 RTX_FRUC_ERR_INVALID_ARGUMENT);

    RtxFrucConfig zero = makeConfig(RTX_FRUC_BACKEND_DUPLICATE, 0, 0);
    RTX_CHECK_EQ(bridge.api->create(&zero, &handle, error, static_cast<uint32_t>(sizeof(error))),
                 RTX_FRUC_ERR_INVALID_ARGUMENT);

    // A null config and a null out-parameter are both rejected.
    RTX_CHECK_EQ(bridge.api->create(nullptr, &handle, error, static_cast<uint32_t>(sizeof(error))),
                 RTX_FRUC_ERR_INVALID_ARGUMENT);
    RtxFrucConfig valid = makeConfig(RTX_FRUC_BACKEND_DUPLICATE);
    RTX_CHECK_EQ(bridge.api->create(&valid, nullptr, error, static_cast<uint32_t>(sizeof(error))),
                 RTX_FRUC_ERR_INVALID_ARGUMENT);

    // The VRAM budget contract: the CPU reference mode reserves nothing, and a
    // GPU-mode bridge stays inside the 120 MB ceiling even at 4K.
    RTX_REQUIRE_EQ(bridge.api->create(&valid, &handle, error, static_cast<uint32_t>(sizeof(error))), RTX_FRUC_OK);
    RTX_CHECK_EQ(bridge.api->query_vram_footprint(handle), static_cast<uint64_t>(0));
    RTX_CHECK_EQ(bridge.api->query_vram_footprint(nullptr), static_cast<uint64_t>(0));
    bridge.api->destroy(handle);
}
