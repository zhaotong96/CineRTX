// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Phase 11.3-S（WO-047 任务 3）：恢复 rtx_ngx_bridge.dll 纯 C ABI 动态加载隔离网，
// 彻底切断 GPL 主程序对专有 nvsdk_ngx_s.lib 的静态直链。

#include "rtx/ngx_remaster.h"

#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <utility>
#include <vector>

#include "rtx/logger.h"
#include "rtx/nvidia_shim.h"
#include "rtx/playback_session.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace rtx {
namespace {

#if defined(_WIN32)
std::wstring widen(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(),
                                         static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring wide(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(),
                        static_cast<int>(value.size()), wide.data(), size);
    return wide;
}
std::string executableDirectory() {
    wchar_t buffer[32768] = {};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, 32768);
    if (length == 0 || length >= 32768) {
        return {};
    }
    const std::filesystem::path path(std::wstring(buffer, length));
    const std::u8string directory = path.parent_path().u8string();
    return std::string(directory.begin(), directory.end());
}
void* openSharedLibrary(const std::string& path, bool absolute_path) {
    const std::wstring wide = widen(path);
    if (wide.empty()) return nullptr;
    const DWORD flags = absolute_path
        ? (LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)
        : LOAD_LIBRARY_SEARCH_DEFAULT_DIRS;
    HMODULE mod = LoadLibraryExW(wide.c_str(), nullptr, flags);
    if (mod == nullptr) {
        mod = LoadLibraryW(wide.c_str());
    }
    return reinterpret_cast<void*>(mod);
}
void* resolveSymbol(void* handle, const char* name) {
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(handle), name));
}
#else
std::string executableDirectory() {
    char buffer[4096] = {};
    const ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (length <= 0) {
        return {};
    }
    return std::filesystem::path(std::string(buffer, static_cast<size_t>(length)))
        .parent_path()
        .string();
}
void* openSharedLibrary(const std::string& path, bool /*absolute_path*/) {
    return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
}
void* resolveSymbol(void* handle, const char* name) {
    return dlsym(handle, name);
}
#endif

template <typename Fn>
bool bindSym(void* handle, Fn& target, const char* name) noexcept {
    void* sym = resolveSymbol(handle, name);
    if (sym == nullptr) return false;
    target = reinterpret_cast<Fn>(sym);
    return true;
}

std::string bridgeFileName() {
#if defined(_WIN32)
    return "rtx_ngx_bridge.dll";
#elif defined(__APPLE__)
    return "librtx_ngx_bridge.dylib";
#else
    return "librtx_ngx_bridge.so";
#endif
}
std::atomic<NgxRemasterBridge*>& activePreviewMonitor() noexcept {
    static std::atomic<NgxRemasterBridge*> monitor{nullptr};
    return monitor;
}
// @rtx-type-justification
// Category: Polymorphic-Boundary
// Why-Std-Fails: IVideoFrameSink 跨层虚接口的播放桥实现：通过动态加载的 NgxRemasterBridge 调用 pack/borrow 并协调 3 槽共享标志生命周期，std::function 无法替代。
// Invariant: 槽位空闲标志（shared_ptr 共享所有权）由视口 Fence 退役回调归还；pack 失败立即归还标志；析构先释放 RtxNgxBridge 再卸载动态库句柄。
class HeadlessPlaybackSink final : public IVideoFrameSink {
public:
    static std::unique_ptr<IVideoFrameSink> create(const MediaInfo& info,
                                                   std::function<void()> on_frame,
                                                   BorrowCallback& out_borrow) {
        NvidiaShim shim;
        std::string shim_error;
        if (!shim.open(&shim_error)) {
            logError("headless preview sink: rtx_shim unavailable: " + shim_error);
            return nullptr;
        }
        RtxDiagnostics diagnostics{};
        if (shim.probe(&diagnostics) != RTX_OK) {
            logError("headless preview sink: rtx_shim probe failed");
            return nullptr;
        }
        auto loader = std::make_unique<NgxRemasterBridge>();
        std::string bridge_err;
        if (!loader->open(&bridge_err) || !loader->hasPlaybackPackApi()) {
            logError("headless preview sink: rtx_ngx_bridge.dll unavailable: " + bridge_err);
            return nullptr;
        }
        RtxNgxBridgeConfig config{};
        std::memcpy(config.luid, diagnostics.device.luid, sizeof(config.luid));
        config.luid_node_mask = diagnostics.device.luid_node_mask;
        config.input_width = info.width;
        config.input_height = info.height;
        config.output_width = config.input_width;
        config.output_height = config.input_height;
        config.vsr_quality = 0;
        config.enable_truehdr = 0;
        config.reserved[0] = 1u;

        RtxNgxBridge* bridge = nullptr;
        if (loader->create(&config, &bridge) != RTX_OK || bridge == nullptr) {
            logError("headless preview sink: headless bridge create failed");
            return nullptr;
        }
        const int32_t full_range = info.color.range == ColorRange::Full ? 1 : 0;
        const int32_t matrix = info.color.matrix == MatrixCoefficients::BT2020NC ? 1 : 0;
        auto sink = std::unique_ptr<HeadlessPlaybackSink>(
            new HeadlessPlaybackSink(std::move(loader), bridge, full_range, matrix, std::move(on_frame)));
        out_borrow = [raw = sink.get()](std::uint32_t& slot, PreviewSlotBorrow& borrow) {
            return raw != nullptr && raw->borrowLatest(slot, borrow);
        };
        return sink;
    }
    ~HeadlessPlaybackSink() override {
        if (bridge_ != nullptr && loader_) {
            loader_->releaseBridge(bridge_);
            bridge_ = nullptr;
        }
    }
    Result<void> present(const RtxFrameDesc& frame, int64_t /*deadline_flicks*/,
                         uint64_t /*retire_fence*/) override {
        std::uint32_t slot = 0;
        bool acquired = false;
        for (std::uint32_t i = 0; i < kSlotCount; ++i) {
            std::uint32_t expected = 0;
            if ((*slot_flags_)[i].compare_exchange_strong(expected, 1u, std::memory_order_acq_rel)) {
                slot = i;
                acquired = true;
                break;
            }
        }
        if (!acquired) {
            return {};  // 全忙：跳过本次预览，绝不阻塞解码线程
        }
        const int32_t packed = loader_->playbackPack(
            bridge_, frame.planes[0], frame.planes[1], frame.pitches[0], frame.pitches[1],
            static_cast<std::uint32_t>(frame.width), static_cast<std::uint32_t>(frame.height),
            frame.format, full_range_, matrix_, slot);
        if (packed != RTX_OK) {
            (*slot_flags_)[slot].store(0, std::memory_order_release);
            return err(ErrorCode::Internal, "playback pack failed",
                       std::to_string(static_cast<int>(packed)));
        }
        latest_slot_.store(slot, std::memory_order_release);
        published_.store(true, std::memory_order_release);
        if (on_frame_) {
            on_frame_();
        }
        return {};
    }
    [[nodiscard]] uint64_t completedFence() const noexcept override { return UINT64_MAX; }

    bool borrowLatest(std::uint32_t& slot, PreviewSlotBorrow& borrow) noexcept {
        if (!published_.load(std::memory_order_acquire) || !loader_ || !loader_->hasBorrowPreviewApi()) {
            return false;
        }
        const std::uint32_t latest = latest_slot_.load(std::memory_order_acquire);
        if (latest == slot) {
            return false;
        }
        void* device = nullptr;
        void* resource = nullptr;
        std::uint32_t pitch = 0;
        if (loader_->borrowPreview(bridge_, latest, &device, &resource, &pitch) != RTX_OK ||
            resource == nullptr) {
            return false;
        }
        slot = latest;
        borrow = {device, resource, pitch, slot_flags_};
        return true;
    }
private:
    static constexpr std::uint32_t kSlotCount = 3;

    HeadlessPlaybackSink(std::unique_ptr<NgxRemasterBridge> loader, RtxNgxBridge* bridge,
                         int32_t full_range, int32_t matrix, std::function<void()> on_frame)
        : loader_(std::move(loader)), bridge_(bridge), full_range_(full_range),
          matrix_(matrix), on_frame_(std::move(on_frame)) {}

    std::unique_ptr<NgxRemasterBridge> loader_;
    RtxNgxBridge* bridge_ = nullptr;
    int32_t full_range_ = 0;
    int32_t matrix_ = 0;
    std::function<void()> on_frame_;
    PreviewSlotFlags slot_flags_ = std::make_shared<std::array<std::atomic<std::uint32_t>, kSlotCount>>();
    std::atomic<std::uint32_t> latest_slot_{0};
    std::atomic<bool> published_{false};
};

}  // namespace

std::unique_ptr<IVideoFrameSink> createHeadlessPreviewSink(const MediaInfo& info,
                                                           std::function<void()> on_frame,
                                                           BorrowCallback& out_borrow) {
    return HeadlessPlaybackSink::create(info, std::move(on_frame), out_borrow);
}
void NgxRemasterBridge::publishActivePreviewBridge(NgxRemasterBridge& loader,
                                                   RtxNgxBridge* bridge) {
    loader.preview_bridge_ = bridge;
    for (auto& flag : *loader.preview_slot_flags_) {
        flag.store(0, std::memory_order_release);
    }
    loader.preview_latest_slot_.store(0, std::memory_order_release);
    activePreviewMonitor().store(&loader, std::memory_order_release);
}
void NgxRemasterBridge::clearActivePreviewBridge(NgxRemasterBridge* loader) noexcept {
    NgxRemasterBridge* expected = loader;
    activePreviewMonitor().compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
}
bool NgxRemasterBridge::borrowActivePreview(std::uint32_t slot, void** out_d3d12_device,
                                            void** out_d3d12_resource, std::uint32_t* out_pitch) {
    NgxRemasterBridge* loader = activePreviewMonitor().load(std::memory_order_acquire);
    return loader != nullptr &&
           loader->borrowPreviewSlot(slot, out_d3d12_device, out_d3d12_resource, out_pitch);
}
std::uint32_t NgxRemasterBridge::latestPreviewSlot() {
    NgxRemasterBridge* loader = activePreviewMonitor().load(std::memory_order_acquire);
    return loader != nullptr ? loader->latestPublishedSlot() : 0;
}
PreviewSlotFlags NgxRemasterBridge::previewSlotFlags() {
    NgxRemasterBridge* loader = activePreviewMonitor().load(std::memory_order_acquire);
    return loader != nullptr ? loader->previewSlotFlagsArray() : PreviewSlotFlags{};
}
bool NgxRemasterBridge::acquirePreviewSlot(std::uint32_t* out_slot) noexcept {
    for (std::uint32_t i = 0; i < kPreviewSlotCount; ++i) {
        std::uint32_t expected = 0;
        if ((*preview_slot_flags_)[i].compare_exchange_strong(expected, 1u,
                                                           std::memory_order_acq_rel)) {
            if (out_slot != nullptr) {
                *out_slot = i;
            }
            return true;
        }
    }
    return false;  // 三槽均忙：调用方跳过本次预览拷贝（零阻塞）
}
void NgxRemasterBridge::publishPreviewSlot(std::uint32_t slot) noexcept {
    if (slot < kPreviewSlotCount) {
        preview_latest_slot_.store(slot, std::memory_order_release);
    }
}
PreviewSlotFlags NgxRemasterBridge::previewSlotFlagsArray() noexcept {
    return preview_slot_flags_;
}
std::uint32_t NgxRemasterBridge::latestPublishedSlot() const noexcept {
    return preview_latest_slot_.load(std::memory_order_acquire);
}
bool NgxRemasterBridge::borrowPreviewSlot(std::uint32_t slot, void** out_d3d12_device,
                                          void** out_d3d12_resource,
                                          std::uint32_t* out_pitch) noexcept {
    if (preview_bridge_ == nullptr || borrow_preview_ == nullptr) {
        return false;
    }
    return borrow_preview_(preview_bridge_, slot, out_d3d12_device, out_d3d12_resource,
                           out_pitch) == RTX_OK;
}
NgxRemasterBridge::~NgxRemasterBridge() { close(); }

bool NgxRemasterBridge::open(std::string* error) {
    if (loaded()) {
        return true;
    }
    close();

    std::vector<std::pair<std::string, bool>> candidates;
    if (const char* env_path = std::getenv("RTX_NGX_BRIDGE_PATH"); env_path != nullptr && env_path[0] != '\0') {
        candidates.emplace_back(env_path, true);
    }
    const std::string exe_dir = executableDirectory();
    const std::string fname = bridgeFileName();
    if (!exe_dir.empty()) {
        candidates.emplace_back((std::filesystem::path(exe_dir) / fname).string(), true);
        candidates.emplace_back((std::filesystem::path(exe_dir) / "plugins" / "nvidia_rtx" / fname).string(), true);
    }
    candidates.emplace_back(fname, false);

    void* mod = nullptr;
    std::string chosen_path;
    for (const auto& [cand_path, is_abs] : candidates) {
        mod = openSharedLibrary(cand_path, is_abs);
        if (mod != nullptr) {
            chosen_path = cand_path;
            break;
        }
    }
    if (mod == nullptr) {
        error_ = "rtx_ngx_bridge dynamic library not found (" + fname + ")";
        if (error != nullptr) *error = error_;
        return false;
    }
    handle_ = mod;

    using AbiVerFn = uint32_t(RTX_SHIM_CALL*)();
    AbiVerFn abi_ver_fn = nullptr;
    if (bindSym(handle_, abi_ver_fn, "rtx_ngx_bridge_abi_version")) {
        const uint32_t ver = abi_ver_fn();
        if (ver < RTX_NGX_BRIDGE_ABI_VERSION) {
            error_ = "rtx_ngx_bridge ABI version " + std::to_string(ver) +
                     " is older than required " + std::to_string(RTX_NGX_BRIDGE_ABI_VERSION);
            close();
            if (error != nullptr) *error = error_;
            return false;
        }
    }

    const bool core_ok =
        bindSym(handle_, query_caps_, "rtx_ngx_bridge_query_caps") &&
        bindSym(handle_, create_, "rtx_ngx_bridge_create") &&
        bindSym(handle_, destroy_, "rtx_ngx_bridge_destroy") &&
        bindSym(handle_, remaster_, "rtx_ngx_bridge_remaster") &&
        bindSym(handle_, get_remaster_stats_, "rtx_ngx_bridge_get_remaster_stats") &&
        bindSym(handle_, last_error_, "rtx_ngx_bridge_last_error");
    if (!core_ok) {
        error_ = "rtx_ngx_bridge is missing required C ABI exports (" + chosen_path + ")";
        close();
        if (error != nullptr) *error = error_;
        return false;
    }

    // 可选扩展导出（Phase 7/8/11 增量 ABI，缺失时优雅降级）。
    (void)bindSym(handle_, set_lut3d_, "rtx_ngx_bridge_set_lut3d");
    (void)bindSym(handle_, preview_buffer_, "rtx_ngx_bridge_preview_buffer");
    (void)bindSym(handle_, download_preview_, "rtx_ngx_bridge_download_preview");
    (void)bindSym(handle_, borrow_preview_, "rtx_ngx_bridge_borrow_preview");
    (void)bindSym(handle_, playback_pack_, "rtx_ngx_bridge_playback_pack");
    (void)bindSym(handle_, upload_frame_, "rtx_ngx_bridge_upload_frame");
    (void)bindSym(handle_, interpolate_nv12_, "rtx_ngx_bridge_interpolate_nv12");
    (void)bindSym(handle_, get_interp_buffer_, "rtx_ngx_bridge_get_interp_buffer");
    (void)bindSym(handle_, init_rife_, "rtx_ngx_bridge_init_rife");
    (void)bindSym(handle_, init_rife_ex_, "rtx_ngx_bridge_init_rife_ex");
    (void)bindSym(handle_, interpolate_rife_, "rtx_ngx_bridge_interpolate_rife");
    (void)bindSym(handle_, build_rife_engine_, "rtx_ngx_bridge_build_rife_engine");
    (void)bindSym(handle_, destroy_rife_, "rtx_ngx_bridge_destroy_rife");

    embedded_ = false;
    path_ = std::move(chosen_path);
    error_.clear();
    if (error != nullptr) {
        error->clear();
    }
    return true;
}
void NgxRemasterBridge::close() {
    if (handle_ != nullptr) {
#if defined(_WIN32)
        FreeLibrary(reinterpret_cast<HMODULE>(handle_));
#else
        dlclose(handle_);
#endif
        handle_ = nullptr;
    }
    embedded_ = false;
    path_.clear();
    query_caps_ = nullptr;
    create_ = nullptr;
    destroy_ = nullptr;
    remaster_ = nullptr;
    get_remaster_stats_ = nullptr;
    last_error_ = nullptr;
    set_lut3d_ = nullptr;
    preview_buffer_ = nullptr;
    download_preview_ = nullptr;
    borrow_preview_ = nullptr;
    playback_pack_ = nullptr;
    upload_frame_ = nullptr;
    interpolate_nv12_ = nullptr;
    get_interp_buffer_ = nullptr;
    init_rife_ = nullptr;
    init_rife_ex_ = nullptr;
    interpolate_rife_ = nullptr;
    build_rife_engine_ = nullptr;
    destroy_rife_ = nullptr;
}
RtxResult NgxRemasterBridge::queryCaps(RtxNgxBridgeCaps* caps) const {
    return query_caps_ != nullptr ? query_caps_(caps) : RTX_ERR_DLL_MISSING;
}
RtxResult NgxRemasterBridge::create(const RtxNgxBridgeConfig* config,
                                    RtxNgxBridge** bridge) const {
    return create_ != nullptr ? create_(config, bridge) : RTX_ERR_DLL_MISSING;
}
void NgxRemasterBridge::releaseBridge(RtxNgxBridge* bridge) const {
    if (destroy_ != nullptr) {
        destroy_(bridge);
    }
}
RtxResult NgxRemasterBridge::remaster(RtxNgxBridge* bridge, const RtxNgxRemasterInput* input,
                                      RtxNgxRemasterOutput* output,
                                      RtxNgxRemasterStats* stats) const {
    return remaster_ != nullptr ? remaster_(bridge, input, output, stats) : RTX_ERR_DLL_MISSING;
}
RtxResult NgxRemasterBridge::getRemasterStats(RtxNgxBridge* bridge,
                                              RtxNgxRemasterStats* stats) const {
    return get_remaster_stats_ != nullptr ? get_remaster_stats_(bridge, stats)
                                          : RTX_ERR_DLL_MISSING;
}
RtxResult NgxRemasterBridge::setLut3d(RtxNgxBridge* bridge, const uint16_t* lut_half4_data,
                                      int32_t grid_size) const {
    return set_lut3d_ != nullptr ? set_lut3d_(bridge, lut_half4_data, grid_size)
                                 : RTX_ERR_DLL_MISSING;
}
RtxResult NgxRemasterBridge::previewBuffer(RtxNgxBridge* bridge, uint32_t slot,
                                           uint64_t* device_ptr, uint32_t* pitch) const {
    return preview_buffer_ != nullptr ? preview_buffer_(bridge, slot, device_ptr, pitch)
                                      : RTX_ERR_DLL_MISSING;
}
RtxResult NgxRemasterBridge::downloadPreview(RtxNgxBridge* bridge, void* destination,
                                             size_t destination_bytes) const {
    return download_preview_ != nullptr
               ? download_preview_(bridge, destination, destination_bytes)
               : RTX_ERR_DLL_MISSING;
}
int32_t NgxRemasterBridge::borrowPreview(RtxNgxBridge* bridge, uint32_t slot,
                                         void** out_d3d12_device, void** out_d3d12_resource,
                                         uint32_t* out_pitch) const {
    return borrow_preview_ != nullptr
               ? borrow_preview_(bridge, slot, out_d3d12_device, out_d3d12_resource, out_pitch)
               : RTX_ERR_DLL_MISSING;
}
int32_t NgxRemasterBridge::playbackPack(RtxNgxBridge* bridge, uint64_t y_plane, uint64_t uv_plane,
                                        uint32_t y_pitch, uint32_t uv_pitch, uint32_t width,
                                        uint32_t height, int32_t format, int32_t full_range,
                                        int32_t matrix, uint32_t slot) const {
    return playback_pack_ != nullptr
               ? playback_pack_(bridge, y_plane, uv_plane, y_pitch, uv_pitch, width, height,
                                format, full_range, matrix, slot)
               : RTX_ERR_DLL_MISSING;
}
RtxResult NgxRemasterBridge::uploadFrame(RtxNgxBridge* bridge, const void* y_plane,
                                         const void* uv_plane, uint32_t y_pitch,
                                         uint32_t uv_pitch, int32_t width, int32_t height,
                                         uint64_t* y_device, uint64_t* uv_device) const {
    return upload_frame_ != nullptr
               ? upload_frame_(bridge, y_plane, uv_plane, y_pitch, uv_pitch, width, height,
                               y_device, uv_device)
               : RTX_ERR_DLL_MISSING;
}
RtxResult NgxRemasterBridge::interpolateNv12(RtxNgxBridge* bridge, uint64_t prev_nv12,
                                             uint64_t curr_nv12, uint32_t pitch,
                                             float alpha, uint64_t out_nv12) const {
    return interpolate_nv12_ != nullptr
               ? static_cast<RtxResult>(interpolate_nv12_(bridge, prev_nv12, curr_nv12, pitch, alpha, out_nv12))
               : RTX_ERR_DLL_MISSING;
}
uint64_t NgxRemasterBridge::getInterpBuffer(RtxNgxBridge* bridge, uint32_t width, uint32_t height) const {
    return get_interp_buffer_ != nullptr ? get_interp_buffer_(bridge, width, height) : 0;
}
int32_t NgxRemasterBridge::initRife(RtxNgxBridge* bridge, const char* engine_path, uint32_t width, uint32_t height) const {
    return init_rife_ != nullptr ? init_rife_(bridge, engine_path, width, height) : RTX_ERR_DLL_MISSING;
}
int32_t NgxRemasterBridge::initRifeEx(RtxNgxBridge* bridge, const char* engine_path,
                                      const RtxRifeInitConfig* config,
                                      uint64_t* out_trt_peak_bytes) const {
    if (init_rife_ex_ != nullptr) {
        return init_rife_ex_(bridge, engine_path, config, out_trt_peak_bytes);
    }
    if (init_rife_ != nullptr && config != nullptr) {
        if (out_trt_peak_bytes != nullptr) *out_trt_peak_bytes = 0;
        return init_rife_(bridge, engine_path, config->width, config->height);
    }
    return RTX_ERR_DLL_MISSING;
}
int32_t NgxRemasterBridge::interpolateRife(RtxNgxBridge* bridge, uint64_t prev_nv12, uint64_t curr_nv12,
                                         uint32_t pitch, float alpha, uint64_t out_nv12, int32_t full_range,
                                         int32_t* scene_cut) const {
    return interpolate_rife_ != nullptr
               ? interpolate_rife_(bridge, prev_nv12, curr_nv12, pitch, alpha, out_nv12, full_range, scene_cut)
               : RTX_ERR_DLL_MISSING;
}
int32_t NgxRemasterBridge::buildRifeEngine(const char* onnx_path, const char* engine_out_path,
                                         uint32_t width, uint32_t height, char* err_buf, uint32_t err_cap) const {
    return build_rife_engine_ != nullptr
               ? build_rife_engine_(onnx_path, engine_out_path, width, height, err_buf, err_cap)
               : RTX_ERR_DLL_MISSING;
}
void NgxRemasterBridge::destroyRife(RtxNgxBridge* bridge) const {
    if (destroy_rife_ != nullptr) {
        destroy_rife_(bridge);
    }
}
const char* NgxRemasterBridge::lastError(RtxNgxBridge* bridge) const {
    return last_error_ != nullptr ? last_error_(bridge)
                                  : "rtx_ngx_bridge ABI unavailable";
}
}  // namespace rtx
