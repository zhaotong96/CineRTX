/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * Dynamic loader for TensorRT 10.13.3.9 (nvinfer_10.dll & nvonnxparser_10.dll).
 * Phase 12 (Wave 12.2-B1): zero-class free-function namespace dispatch.
 */

#ifndef RTX_NGX_BRIDGE_TRT_LOADER_H
#define RTX_NGX_BRIDGE_TRT_LOADER_H

#include <cstdint>
#include <string>

namespace rtx_ngx::trt_loader {

[[nodiscard]] bool loadRuntime(std::string* error = nullptr);
[[nodiscard]] bool loadParser(std::string* error = nullptr);

[[nodiscard]] bool isRuntimeLoaded() noexcept;
[[nodiscard]] bool isParserLoaded() noexcept;
[[nodiscard]] int32_t getVersion() noexcept;

[[nodiscard]] void* createInferRuntime(void* logger) noexcept;
[[nodiscard]] void* createInferBuilder(void* logger) noexcept;
[[nodiscard]] void* createNvOnnxParser(void* network, void* logger) noexcept;

/* Single shared nvinfer1::ILogger instance (defined in rife_builder.cpp). */
[[nodiscard]] void* rifeTrtLogger() noexcept;

}  // namespace rtx_ngx::trt_loader

#endif  // RTX_NGX_BRIDGE_TRT_LOADER_H
