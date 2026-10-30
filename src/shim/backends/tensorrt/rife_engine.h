/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * TensorRT RIFE v4.25 engine construction and integrity sidecar (W8-1).
 *
 * Two build modes:
 *
 *   in-process  when the TensorRT runtime is resolvable at run time
 *               (RTX_HAVE_TENSORRT headers plus nvinfer_10.dll / nvonnxparser_10.dll
 *               beside CineRTX.exe).  The builder enforces the FP16 / no-TF32 /
 *               obey-precision-constraints profile the RIFE graph needs and
 *               serialises the engine next to the ONNX file.
 *
 *   trtexec     otherwise.  The exact command is emitted for the operator to
 *               run, which is the sanctioned phase-2 path and keeps the engine
 *               build reproducible without an SDK in the build container.  A
 *               missing runtime degrades to this path instead of failing the
 *               process: nothing here is linked, so the executable still starts.
 *
 * Either way the ONNX digest is persisted to "<engine>.sha256" before the
 * engine is used, so a stale engine built from a different model is rejected
 * instead of silently producing wrong frames.
 */

#ifndef RTX_SHIM_RIFE_ENGINE_H
#define RTX_SHIM_RIFE_ENGINE_H

#include <cstdint>
#include <string>

#include "rtx/rife.h"
#include "rtx/status.h"

namespace rtx {

struct RifeEngineBuildRequest {
    std::string onnx_path;
    std::string engine_path;
    int width = 1920;
    int height = 1080;
    bool fp16 = true;
    bool verbose = false;
};

struct RifeEngineBuildReport {
    // True when the engine was serialised by this process.
    bool built_in_process = false;
    std::string onnx_sha256;
    std::string sidecar_path;
    // Populated when built_in_process is false: the command to run.
    std::string trtexec_command;
    uint64_t engine_bytes = 0;
    std::string tensorrt_version;
    // Why the in-process builder was skipped, when it was skipped because the
    // runtime could not be resolved (empty when no runtime was expected).
    std::string runtime_note;
};

// Lowercase hex SHA-256 of a file.  Implemented locally: the engine build path
// must not introduce an OpenSSL dependency into a single-EXE product.
Result<std::string> computeFileSha256(const std::string& path);

// Path of the integrity sidecar for an engine file ("<engine>.sha256").
std::string engineSidecarPath(const std::string& engine_path);

// Writes the sidecar atomically (temp file + rename).
Status writeEngineSidecar(const std::string& engine_path, const std::string& onnx_sha256);

// Reads the recorded ONNX digest.  ErrorCode::NotFound when the sidecar is
// absent, ErrorCode::Io when it cannot be read.
Result<std::string> readEngineSidecar(const std::string& engine_path);

// Validates the request, computes the ONNX digest, then either builds the
// engine or emits the trtexec command, and finally writes the sidecar.
Result<RifeEngineBuildReport> buildRifeEngine(const RifeEngineBuildRequest& request);

// True when this build can construct engines in-process.
bool tensorRtBuilderAvailable() noexcept;

}  // namespace rtx

#endif /* RTX_SHIM_RIFE_ENGINE_H */
