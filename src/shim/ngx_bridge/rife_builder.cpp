/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * Zone C implementation of TensorRT RIFE v4.25 engine construction inside rtx_ngx_bridge.dll.
 */

#include "rife_builder.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#if defined(RTX_HAVE_TENSORRT)
#include <NvInfer.h>
#include <NvOnnxParser.h>
#endif

namespace rtx::ngx {
namespace {

using CreateInferBuilderFn = void* (*)(void* logger, std::int32_t version) noexcept;
using CreateOnnxParserFn = void* (*)(void* network, void* logger, std::int32_t version) noexcept;
using GetInferLibVersionFn = std::int32_t (*)() noexcept;

struct TrtDynamicApi {
    CreateInferBuilderFn create_infer_builder = nullptr;
    CreateOnnxParserFn create_onnx_parser = nullptr;
    GetInferLibVersionFn infer_lib_version = nullptr;
    std::string infer_module;
    std::string parser_module;

    bool complete() const noexcept {
        return create_infer_builder != nullptr && create_onnx_parser != nullptr &&
               infer_lib_version != nullptr;
    }
};

#if defined(_WIN32)
std::vector<std::wstring> searchDirectories() {
    std::vector<std::wstring> directories;

    wchar_t configured[8192] = {};
    const DWORD configured_length =
        ::GetEnvironmentVariableW(L"RTX_NVIDIA_DIR", configured, 8192);
    if (configured_length > 0 && configured_length < 8192) {
        directories.emplace_back(configured, configured_length);
    }

    wchar_t module[32768] = {};
    const DWORD written = ::GetModuleFileNameW(nullptr, module, 32768);
    if (written != 0 && written < 32768) {
        std::wstring directory(module, written);
        const size_t slash = directory.find_last_of(L"\\/");
        if (slash != std::wstring::npos) {
            directory.resize(slash);
            directories.push_back(directory);
        }
    }
    return directories;
}

HMODULE openBeside(const std::wstring& directory, const wchar_t* name) {
    std::wstring path = directory;
    if (!path.empty() && path.back() != L'\\' && path.back() != L'/') {
        path.push_back(L'\\');
    }
    path += name;
    return ::LoadLibraryExW(
        path.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
}

bool loadTrtApi(TrtDynamicApi& api, std::string* error) {
    static const wchar_t* kInferModules[] = {L"nvinfer_10.dll", L"nvinfer_11.dll"};
    static const wchar_t* kParserModules[] = {L"nvonnxparser_10.dll", L"nvonnxparser_11.dll"};

    HMODULE infer = nullptr;
    HMODULE parser = nullptr;

    for (const auto& dir : searchDirectories()) {
        for (const auto* mod : kInferModules) {
            infer = openBeside(dir, mod);
            if (infer != nullptr) break;
        }
        if (infer != nullptr) {
            for (const auto* mod : kParserModules) {
                parser = openBeside(dir, mod);
                if (parser != nullptr) break;
            }
            break;
        }
    }

    if (infer == nullptr || parser == nullptr) {
        if (error != nullptr) {
            *error = "TensorRT runtime DLLs (nvinfer_10.dll / nvonnxparser_10.dll) not found";
        }
        return false;
    }

    api.create_infer_builder = reinterpret_cast<CreateInferBuilderFn>(
        ::GetProcAddress(infer, "createInferBuilder_INTERNAL"));
    api.infer_lib_version = reinterpret_cast<GetInferLibVersionFn>(
        ::GetProcAddress(infer, "getInferLibVersion"));
    api.create_onnx_parser = reinterpret_cast<CreateOnnxParserFn>(
        ::GetProcAddress(parser, "createNvOnnxParser_INTERNAL"));

    if (!api.complete()) {
        if (error != nullptr) {
            *error = "TensorRT runtime missing required entry symbols";
        }
        return false;
    }
    return true;
}
#else
bool loadTrtApi(TrtDynamicApi&, std::string* error) {
    if (error) *error = "TensorRT in-process build not supported on POSIX in bridge";
    return false;
}
#endif

void buildTrtexecCommandStr(int width, int height, bool fp16, const char* onnx, const char* engine, char* out, size_t out_cap) {
    std::snprintf(out, out_cap,
                  "trtexec --onnx=\"%s\" --saveEngine=\"%s\" --minShapes=input:1x8x%dx%d "
                  "--optShapes=input:1x8x%dx%d --maxShapes=input:1x8x%dx%d %s --noTF32",
                  onnx, engine, height, width, height, width, height, width,
                  fp16 ? "--fp16" : "");
}

}  // namespace

RtxResult buildRifeEngineInBridge(
    const RtxNgxRifeBuildParams* params,
    RtxNgxRifeBuildResult* result) {
    if (params == nullptr || result == nullptr) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    std::memset(result, 0, sizeof(*result));

    if (params->onnx_path == nullptr || params->engine_path == nullptr ||
        params->onnx_path[0] == '\0' || params->engine_path[0] == '\0') {
        std::snprintf(result->error_message, sizeof(result->error_message),
                      "missing onnx_path or engine_path");
        return RTX_ERR_INVALID_ARGUMENT;
    }

    buildTrtexecCommandStr(params->width, params->height, params->fp16 != 0,
                           params->onnx_path, params->engine_path,
                           result->trtexec_command, sizeof(result->trtexec_command));

#if defined(RTX_HAVE_TENSORRT)
    TrtDynamicApi api;
    std::string load_error;
    if (!loadTrtApi(api, &load_error)) {
        result->built_in_process = 0;
        std::snprintf(result->error_message, sizeof(result->error_message), "%s", load_error.c_str());
        return RTX_OK;
    }

    class Logger final : public nvinfer1::ILogger {
    public:
        explicit Logger(bool verbose) : verbose_(verbose) {}
        void log(Severity severity, const char* message) noexcept override {
            if (severity <= Severity::kWARNING && verbose_) {
                std::fprintf(stderr, "[TensorRT-Bridge] %s\n", message != nullptr ? message : "");
            }
        }
    private:
        bool verbose_ = false;
    } logger(params->verbose != 0);

    std::unique_ptr<nvinfer1::IBuilder> builder(static_cast<nvinfer1::IBuilder*>(
        api.create_infer_builder(&logger, NV_TENSORRT_VERSION)));
    if (!builder) {
        result->built_in_process = 0;
        std::snprintf(result->error_message, sizeof(result->error_message),
                      "createInferBuilder returned null");
        return RTX_OK;
    }

    std::unique_ptr<nvinfer1::INetworkDefinition> network(builder->createNetworkV2(0));
    if (!network) {
        result->built_in_process = 0;
        std::snprintf(result->error_message, sizeof(result->error_message),
                      "createNetworkV2 returned null");
        return RTX_OK;
    }

    std::unique_ptr<nvonnxparser::IParser> parser(
        static_cast<nvonnxparser::IParser*>(api.create_onnx_parser(
            network.get(), &logger, NV_ONNX_PARSER_VERSION)));
    if (!parser || !parser->parseFromFile(params->onnx_path, static_cast<int>(nvinfer1::ILogger::Severity::kWARNING))) {
        result->built_in_process = 0;
        std::snprintf(result->error_message, sizeof(result->error_message),
                      "failed to parse ONNX graph %s", params->onnx_path);
        return RTX_OK;
    }

    std::unique_ptr<nvinfer1::IBuilderConfig> builder_config(builder->createBuilderConfig());
    if (!builder_config) {
        result->built_in_process = 0;
        std::snprintf(result->error_message, sizeof(result->error_message),
                      "createBuilderConfig returned null");
        return RTX_OK;
    }

    if (params->fp16 != 0) {
        builder_config->setFlag(nvinfer1::BuilderFlag::kFP16);
    }
    builder_config->clearFlag(nvinfer1::BuilderFlag::kTF32);

    std::unique_ptr<nvinfer1::IHostMemory> serialised(
        builder->buildSerializedNetwork(*network, *builder_config));
    if (!serialised) {
        result->built_in_process = 0;
        std::snprintf(result->error_message, sizeof(result->error_message),
                      "buildSerializedNetwork returned null");
        return RTX_OK;
    }

    {
        std::ofstream stream(params->engine_path, std::ios::binary | std::ios::trunc);
        if (!stream) {
            result->built_in_process = 0;
            std::snprintf(result->error_message, sizeof(result->error_message),
                          "cannot create engine output file %s", params->engine_path);
            return RTX_ERR_IO;
        }
        stream.write(static_cast<const char*>(serialised->data()),
                     static_cast<std::streamsize>(serialised->size()));
        result->engine_bytes = serialised->size();
    }

    result->built_in_process = 1;
    std::snprintf(result->tensorrt_version, sizeof(result->tensorrt_version),
                  "%d.%d.%d", NV_TENSORRT_MAJOR, NV_TENSORRT_MINOR, NV_TENSORRT_PATCH);
    return RTX_OK;
#else
    result->built_in_process = 0;
    std::snprintf(result->error_message, sizeof(result->error_message),
                  "TensorRT headers not compiled into bridge");
    return RTX_OK;
#endif
}

}  // namespace rtx::ngx
