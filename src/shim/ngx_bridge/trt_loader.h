/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * Dynamic loader and version gate for TensorRT 10.13.3.9 runtime and ONNX parser.
 */

#ifndef RTX_NGX_BRIDGE_TRT_LOADER_H
#define RTX_NGX_BRIDGE_TRT_LOADER_H

#include <cstdint>
#include <string>

namespace rtx_ngx {

// @rtx-type-justification
// Category: NonTrivial-StateMachine
// Why-Std-Fails: 协调多资源异步生命周期的宿主状态机：std::unique_ptr/std::jthread/std::tuple 无法表达其声明序即析构逆序、线程亲和与受控降级路径的交叉不变量。
// Invariant: 成员声明序即依赖序（先声明者后析构）；状态迁移仅在持锁线程或属主线程发生。
class TrtLoader {
public:
    static constexpr int32_t kExpectedTrtVersion = 101303;  // (10 * 10000) + (13 * 100) + 3

    static TrtLoader& instance();

    bool loadRuntime(std::string* error = nullptr);
    bool loadParser(std::string* error = nullptr);

    bool isRuntimeLoaded() const noexcept;
    bool isParserLoaded() const noexcept;

    int32_t getVersion() const noexcept;

    void* createInferRuntime(void* logger);
    void* createInferBuilder(void* logger);
    void* createNvOnnxParser(void* network, void* logger);

private:
    TrtLoader() = default;
    ~TrtLoader();
    TrtLoader(const TrtLoader&) = delete;
    TrtLoader& operator=(const TrtLoader&) = delete;

    void* hNvInfer_ = nullptr;
    void* hNvOnnxParser_ = nullptr;
    int32_t version_ = 0;
    std::wstring trtDir_;

    void* (*createInferRuntime_INTERNAL_)(void* logger, int32_t version) = nullptr;
    void* (*createInferBuilder_INTERNAL_)(void* logger, int32_t version) = nullptr;
    int32_t (*getInferLibVersion_)() = nullptr;
    void* (*createNvOnnxParser_INTERNAL_)(void* network, void* logger, int32_t version) = nullptr;
};

}  // namespace rtx_ngx

#endif  // RTX_NGX_BRIDGE_TRT_LOADER_H
