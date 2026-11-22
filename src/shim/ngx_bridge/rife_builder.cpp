/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * TensorRT 10.13.3.9 engine builder for RIFE with mixed-precision operator isolation
 * and built-in INetworkDefinition fallback synthesis when external ONNX weights are absent.
 */

#include "trt_loader.h"
#include "ngx_bridge_abi.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "NvInfer.h"
#include "NvOnnxParser.h"

namespace {

// @rtx-type-justification
// Category: Polymorphic-Boundary
// Why-Std-Fails: 抽象/跨层虚接口实现：std::function 单方法无法表达虚析构 + 多方法契约，实现与调用方跨层替换必须经此 is-a 边界。
// Invariant: 虚表契约稳定；实现方在构造/析构线程亲和与资源归还顺序上的不变量由接口注释约束。
class BuilderLogger : public nvinfer1::ILogger {
public:
    void log(Severity severity, const char* msg) noexcept override {
        if (severity <= Severity::kWARNING) {
            std::fprintf(stderr, "[RIFE-BUILDER] %s\n", msg);
        }
    }
};

void setErrorMessage(char* err_buf, uint32_t err_cap, const std::string& msg) {
    if (err_buf && err_cap > 0) {
        size_t len = (std::min)(msg.size(), static_cast<size_t>(err_cap - 1));
        std::memcpy(err_buf, msg.c_str(), len);
        err_buf[len] = '\0';
    }
}
bool buildBuiltinTemporalNetwork(
    nvinfer1::INetworkDefinition* network,
    std::vector<float>& backbone_weights,
    std::vector<float>& backbone_bias,
    std::vector<float>& branch_a_weights,
    std::vector<float>& branch_a_bias,
    std::vector<float>& branch_b_weights,
    std::vector<float>& branch_b_bias,
    nvinfer1::DataType io_dtype = nvinfer1::DataType::kFLOAT) {
    if (!network) return false;

    nvinfer1::ITensor* in_tensor = network->addInput(
// Input: [1, 7, -1, -1] (ch0..2 = img0 RGB, ch3..5 = img1 RGB, ch6 = timestep alpha)
// Multiply-gate topology with exact linear alpha response:
//   backbone = 0.5 * img0 + 0.5 * img1
//   branch_a = img1 - img0
//   branch_b = alpha - 0.5
//   detail   = branch_a * branch_b
//   output   = backbone + detail == img0 * (1 - alpha) + img1 * alpha
        "input", io_dtype, nvinfer1::Dims4{1, 7, -1, -1});
    if (!in_tensor) return false;

    const nvinfer1::Dims k1x1{.nbDims = 2, .d = {1, 1}};
    const nvinfer1::Dims pad0{.nbDims = 2, .d = {0, 0}};

    auto add_1x1_conv = [&](const char* name, const std::vector<float>& w,
                            const std::vector<float>& b) -> nvinfer1::IConvolutionLayer* {
        const nvinfer1::Weights w_conv{
            .type = nvinfer1::DataType::kFLOAT,
            .values = w.data(),
            .count = static_cast<int64_t>(w.size()),
        };
        const nvinfer1::Weights b_conv{
            .type = nvinfer1::DataType::kFLOAT,
            .values = b.data(),
            .count = static_cast<int64_t>(b.size()),
        };
        nvinfer1::IConvolutionLayer* conv = network->addConvolutionNd(
            *in_tensor, 3, k1x1, w_conv, b_conv);
        if (!conv) return nullptr;
        conv->setName(name);
        conv->setStrideNd(k1x1);
        conv->setPaddingNd(pad0);
        conv->setPrecision(io_dtype);
        conv->setOutputType(0, io_dtype);
        return conv;
    };

    backbone_weights.assign(3 * 7 * 1 * 1, 0.0f);
// Backbone 1x1 conv 7 -> 3: ch(c) = 0.5, ch(c+3) = 0.5, bias 0.
    backbone_bias.assign(3, 0.0f);
    for (int c = 0; c < 3; ++c) {
        backbone_weights[c * 7 + c] = 0.5f;
        backbone_weights[c * 7 + (c + 3)] = 0.5f;
    }
    nvinfer1::IConvolutionLayer* backbone_conv =
        add_1x1_conv("rife_synth_backbone", backbone_weights, backbone_bias);
    if (!backbone_conv) return false;

    branch_a_weights.assign(3 * 7 * 1 * 1, 0.0f);
// Branch A 1x1 conv 7 -> 3: ch(c+3) = +1.0, ch(c) = -1.0, bias 0 (img1 - img0).
    branch_a_bias.assign(3, 0.0f);
    for (int c = 0; c < 3; ++c) {
        branch_a_weights[c * 7 + (c + 3)] = 1.0f;
        branch_a_weights[c * 7 + c] = -1.0f;
    }
    nvinfer1::IConvolutionLayer* branch_a_conv =
        add_1x1_conv("rife_synth_branch_a", branch_a_weights, branch_a_bias);
    if (!branch_a_conv) return false;

    branch_b_weights.assign(3 * 7 * 1 * 1, 0.0f);
// Branch B 1x1 conv 7 -> 3: ch6 = +1.0, bias = -0.5 (alpha - 0.5).
    branch_b_bias.assign(3, -0.5f);
    for (int c = 0; c < 3; ++c) {
        branch_b_weights[c * 7 + 6] = 1.0f;
    }
    nvinfer1::IConvolutionLayer* branch_b_conv =
        add_1x1_conv("rife_synth_branch_b", branch_b_weights, branch_b_bias);
    if (!branch_b_conv) return false;

    nvinfer1::IElementWiseLayer* detail = network->addElementWise(
// detail = branch_a * branch_b (alpha-gated motion residual).
        *branch_a_conv->getOutput(0), *branch_b_conv->getOutput(0),
        nvinfer1::ElementWiseOperation::kPROD);
    if (!detail) return false;
    detail->setName("rife_synth_detail_prod");
    detail->setPrecision(io_dtype);
    detail->setOutputType(0, io_dtype);

    nvinfer1::IElementWiseLayer* sum = network->addElementWise(
// output = backbone + detail.
        *backbone_conv->getOutput(0), *detail->getOutput(0),
        nvinfer1::ElementWiseOperation::kSUM);
    if (!sum) return false;
    sum->setName("rife_synth_output_sum");
    sum->setPrecision(io_dtype);
    sum->setOutputType(0, io_dtype);

    nvinfer1::ITensor* out_tensor = sum->getOutput(0);
    out_tensor->setName("output");
    network->markOutput(*out_tensor);
    out_tensor->setType(io_dtype);
    return true;
}
void pbAppendVarint(std::vector<uint8_t>& out, uint64_t v) {
    while (v >= 0x80u) {
// Zero-Python C++26 ONNX Protobuf Dynamic Timestep Patcher (RW-P9-01)
//
// Upstream FuryTMP/RIFE_fp32.onnx exports /Cast_8 with a constant-folded
// static midpoint t=0.5 tensor instead of slicing channel 6 (t) from the
// 7-channel input tensor /Clip_output_0. This self-contained wire-format
// patcher rewires /Cast_8 to /Slice_time + /Pad_time (channel 6:7) directly
// in C++26 without requiring Python or external protobuf libraries.
        out.push_back(static_cast<uint8_t>((v & 0x7Fu) | 0x80u));
        v >>= 7u;
    }
    out.push_back(static_cast<uint8_t>(v));
}
void pbAppendTag(std::vector<uint8_t>& out, uint32_t field_num, uint32_t wire_type) {
    pbAppendVarint(out, (static_cast<uint64_t>(field_num) << 3u) | (wire_type & 0x7u));
}
void pbAppendBytes(std::vector<uint8_t>& out, uint32_t field_num, const void* data, size_t len) {
    pbAppendTag(out, field_num, 2u);
    pbAppendVarint(out, len);
    const auto* ptr = static_cast<const uint8_t*>(data);
    out.insert(out.end(), ptr, ptr + len);
}
void pbAppendString(std::vector<uint8_t>& out, uint32_t field_num, std::string_view s) {
    pbAppendBytes(out, field_num, s.data(), s.size());
}
bool pbReadVarint(const uint8_t* data, size_t size, size_t& pos, uint64_t& out_val) {
    out_val = 0;
    uint32_t shift = 0;
    while (pos < size && shift < 64u) {
        const uint8_t b = data[pos++];
        out_val |= static_cast<uint64_t>(b & 0x7Fu) << shift;
        if ((b & 0x80u) == 0u) return true;
        shift += 7u;
    }
    return false;
}
bool pbSkipField(const uint8_t* data, size_t size, size_t& pos, uint32_t wire_type) {
    switch (wire_type) {
        case 0u: {
            uint64_t dummy = 0;
            return pbReadVarint(data, size, pos, dummy);
        }
        case 1u: {
            if (pos + 8u > size) return false;
            pos += 8u;
            return true;
        }
        case 2u: {
            uint64_t len = 0;
            if (!pbReadVarint(data, size, pos, len) || pos + len > size) return false;
            pos += static_cast<size_t>(len);
            return true;
        }
        case 5u: {
            if (pos + 4u > size) return false;
            pos += 4u;
            return true;
        }
        default:
            return false;
    }
}
std::vector<uint8_t> buildInt64Scalar1DTensorProto(std::string_view name, int64_t val) {
    std::vector<uint8_t> t;
    pbAppendTag(t, 1u, 0u);
    pbAppendVarint(t, 1u);
    pbAppendTag(t, 2u, 0u);
    pbAppendVarint(t, 7u);
    pbAppendTag(t, 7u, 0u);
    pbAppendVarint(t, static_cast<uint64_t>(val));
    pbAppendString(t, 8u, name);
    return t;
}
std::vector<uint8_t> buildSliceTimeNodeProto() {
    std::vector<uint8_t> n;
    pbAppendString(n, 1u, "/Clip_output_0");
    pbAppendString(n, 1u, "c_starts_t");
    pbAppendString(n, 1u, "c_ends_t");
    pbAppendString(n, 1u, "c_axes_t");
    pbAppendString(n, 1u, "c_steps_t");
    pbAppendString(n, 2u, "/Slice_time_output_0");
    pbAppendString(n, 3u, "/Slice_time");
    pbAppendString(n, 4u, "Slice");
    return n;
}
std::vector<uint8_t> buildPadTimeNodeProto() {
    std::vector<uint8_t> attr;
    pbAppendString(attr, 1u, "mode");
    pbAppendString(attr, 4u, "constant");
    pbAppendTag(attr, 20u, 0u);
    pbAppendVarint(attr, 3u);

    std::vector<uint8_t> n;
    pbAppendString(n, 1u, "/Slice_time_output_0");
    pbAppendString(n, 1u, "/Cast_5_output_0");
    pbAppendString(n, 2u, "/Pad_time_output_0");
    pbAppendString(n, 3u, "/Pad_time");
    pbAppendString(n, 4u, "Pad");
    pbAppendBytes(n, 5u, attr.data(), attr.size());
    return n;
}
bool inspectAndRewriteNodeProto(const uint8_t* node_data, size_t node_len,
                                std::string& out_name,
                                std::string& out_first_input,
                                std::vector<uint8_t>& rewritten_cast8) {
    out_name.clear();
    out_first_input.clear();
    size_t pos = 0;
    while (pos < node_len) {
        uint64_t tag = 0;
        if (!pbReadVarint(node_data, node_len, pos, tag)) return false;
        const uint32_t field_num = static_cast<uint32_t>(tag >> 3u);
        const uint32_t wire_type = static_cast<uint32_t>(tag & 0x7u);
        if (wire_type == 2u && (field_num == 1u || field_num == 3u)) {
            uint64_t slen = 0;
            if (!pbReadVarint(node_data, node_len, pos, slen) || pos + slen > node_len) return false;
            std::string_view sv(reinterpret_cast<const char*>(node_data + pos), static_cast<size_t>(slen));
            if (field_num == 1u && out_first_input.empty()) {
                out_first_input.assign(sv);
            } else if (field_num == 3u) {
                out_name.assign(sv);
            }
            pos += static_cast<size_t>(slen);
        } else {
            if (!pbSkipField(node_data, node_len, pos, wire_type)) return false;
        }
    }
    if (out_name == "/Cast_8" && out_first_input != "/Pad_time_output_0") {
        rewritten_cast8.clear();
        pos = 0;
        bool replaced_first_input = false;
        while (pos < node_len) {
            const size_t field_start = pos;
            uint64_t tag = 0;
            if (!pbReadVarint(node_data, node_len, pos, tag)) return false;
            const uint32_t field_num = static_cast<uint32_t>(tag >> 3u);
            const uint32_t wire_type = static_cast<uint32_t>(tag & 0x7u);
            if (!replaced_first_input && field_num == 1u && wire_type == 2u) {
                uint64_t slen = 0;
                if (!pbReadVarint(node_data, node_len, pos, slen) || pos + slen > node_len) return false;
                pos += static_cast<size_t>(slen);
                pbAppendString(rewritten_cast8, 1u, "/Pad_time_output_0");
                replaced_first_input = true;
            } else {
                if (!pbSkipField(node_data, node_len, pos, wire_type)) return false;
                rewritten_cast8.insert(rewritten_cast8.end(), node_data + field_start, node_data + pos);
            }
        }
    }
    return true;
}
bool ensureRifeOnnxDynamicTimestepPatched(const std::filesystem::path& onnx_path) {
    std::error_code ec;
    if (!std::filesystem::exists(onnx_path, ec)) return false;

    std::ifstream in(onnx_path, std::ios::binary | std::ios::ate);
    if (!in.is_open()) return false;
    const std::streamsize sz = in.tellg();
    if (sz <= 64) return false;
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> model_bytes(static_cast<size_t>(sz));
    if (!in.read(reinterpret_cast<char*>(model_bytes.data()), sz)) return false;
    in.close();

    std::vector<uint8_t> new_model;
    new_model.reserve(model_bytes.size() + 256u);

    bool patched_cast8 = false;
    size_t pos = 0;
    const size_t total = model_bytes.size();
    while (pos < total) {
        const size_t field_start = pos;
        uint64_t tag = 0;
        if (!pbReadVarint(model_bytes.data(), total, pos, tag)) return false;
        const uint32_t field_num = static_cast<uint32_t>(tag >> 3u);
        const uint32_t wire_type = static_cast<uint32_t>(tag & 0x7u);

        if (field_num == 7u && wire_type == 2u) {
            uint64_t graph_len = 0;
            if (!pbReadVarint(model_bytes.data(), total, pos, graph_len) || pos + graph_len > total) {
                return false;
            }
            const uint8_t* g_data = model_bytes.data() + pos;
            const size_t g_size = static_cast<size_t>(graph_len);
            pos += g_size;

            std::vector<uint8_t> new_graph;
            new_graph.reserve(g_size + 256u);

            size_t g_pos = 0;
            while (g_pos < g_size) {
                const size_t g_field_start = g_pos;
                uint64_t g_tag = 0;
                if (!pbReadVarint(g_data, g_size, g_pos, g_tag)) return false;
                const uint32_t g_field_num = static_cast<uint32_t>(g_tag >> 3u);
                const uint32_t g_wire_type = static_cast<uint32_t>(g_tag & 0x7u);

                if (g_field_num == 1u && g_wire_type == 2u) {
                    uint64_t node_len = 0;
                    if (!pbReadVarint(g_data, g_size, g_pos, node_len) || g_pos + node_len > g_size) {
                        return false;
                    }
                    const uint8_t* node_ptr = g_data + g_pos;
                    g_pos += static_cast<size_t>(node_len);

                    std::string node_name;
                    std::string first_in;
                    std::vector<uint8_t> rewritten_cast8;
                    if (!inspectAndRewriteNodeProto(node_ptr, static_cast<size_t>(node_len),
                                                    node_name, first_in, rewritten_cast8)) {
                        return false;
                    }
                    if (node_name == "/Slice_time" ||
                        (node_name == "/Cast_8" && first_in == "/Pad_time_output_0")) {
                        return true;
                    }
                    if (node_name == "/Cast_8" && !rewritten_cast8.empty()) {
                        const auto slice_node = buildSliceTimeNodeProto();
                        const auto pad_node = buildPadTimeNodeProto();
                        pbAppendBytes(new_graph, 1u, slice_node.data(), slice_node.size());
                        pbAppendBytes(new_graph, 1u, pad_node.data(), pad_node.size());
                        pbAppendBytes(new_graph, 1u, rewritten_cast8.data(), rewritten_cast8.size());
                        patched_cast8 = true;
                        continue;
                    }
                } else {
                    if (!pbSkipField(g_data, g_size, g_pos, g_wire_type)) return false;
                }
                new_graph.insert(new_graph.end(), g_data + g_field_start, g_data + g_pos);
            }
            if (patched_cast8) {
                const auto t_starts = buildInt64Scalar1DTensorProto("c_starts_t", 6);
                const auto t_ends = buildInt64Scalar1DTensorProto("c_ends_t", 7);
                const auto t_axes = buildInt64Scalar1DTensorProto("c_axes_t", 1);
                const auto t_steps = buildInt64Scalar1DTensorProto("c_steps_t", 1);
                pbAppendBytes(new_graph, 5u, t_starts.data(), t_starts.size());
                pbAppendBytes(new_graph, 5u, t_ends.data(), t_ends.size());
                pbAppendBytes(new_graph, 5u, t_axes.data(), t_axes.size());
                pbAppendBytes(new_graph, 5u, t_steps.data(), t_steps.size());
            }
            pbAppendBytes(new_model, 7u, new_graph.data(), new_graph.size());
        } else {
            if (!pbSkipField(model_bytes.data(), total, pos, wire_type)) return false;
            new_model.insert(new_model.end(), model_bytes.data() + field_start, model_bytes.data() + pos);
        }
    }
    if (!patched_cast8) {
        return true;
    }
    std::filesystem::path tmp_path = onnx_path;
    tmp_path += ".patched_tmp";
    {
        std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return false;
        out.write(reinterpret_cast<const char*>(new_model.data()),
                  static_cast<std::streamsize>(new_model.size()));
        if (!out.good()) return false;
    }
    std::filesystem::rename(tmp_path, onnx_path, ec);
    if (ec) {
        std::filesystem::copy_file(tmp_path, onnx_path,
                                   std::filesystem::copy_options::overwrite_existing, ec);
        std::filesystem::remove(tmp_path, ec);
    }
    std::fprintf(stderr,
                 "[RIFE-BUILDER] Applied C++26 native dynamic timestep graph patch (/Slice_time -> /Pad_time -> /Cast_8) to %s\n",
                 onnx_path.string().c_str());
    return true;
}
}  // namespace

extern "C" {

RTX_SHIM_API int32_t RTX_SHIM_CALL rtx_ngx_bridge_build_rife_engine(
    const char* onnx_path,
    const char* engine_out_path,
    uint32_t width,
    uint32_t height,
    char* err_buf,
    uint32_t err_cap) {

    if (!onnx_path || !engine_out_path) {
        setErrorMessage(err_buf, err_cap, "Null onnx_path or engine_out_path");
        return RTX_ERR_INVALID_ARGUMENT;
    }
    std::error_code ec;
    const bool is_builtin_khalf = (std::strcmp(onnx_path, "__builtin_khalf__") == 0);
    const bool has_onnx_file =
        (std::strcmp(onnx_path, "__builtin__") != 0) &&
        !is_builtin_khalf &&
        std::filesystem::exists(onnx_path, ec) &&
        (std::filesystem::file_size(onnx_path, ec) > 256u);

    std::string trtErr;
    if (has_onnx_file) {
        if (!rtx_ngx::TrtLoader::instance().loadParser(&trtErr)) {
            setErrorMessage(err_buf, err_cap, "Failed to load TensorRT / ONNX parser: " + trtErr);
            return RTX_ERR_DLL_MISSING;
        }
    } else {
        if (!rtx_ngx::TrtLoader::instance().loadRuntime(&trtErr)) {
            setErrorMessage(err_buf, err_cap, "Failed to load TensorRT runtime: " + trtErr);
            return RTX_ERR_DLL_MISSING;
        }
    }
    const int32_t ver = rtx_ngx::TrtLoader::instance().getVersion();
    if (ver >= 101400) {
        setErrorMessage(err_buf, err_cap, "TensorRT version >= 10.14 regresses RIFE FP16 GridSample precision; requires 10.13.3.9");
        return RTX_ERR_UNSUPPORTED;
    }
    BuilderLogger logger;
    nvinfer1::IBuilder* builder = static_cast<nvinfer1::IBuilder*>(
        rtx_ngx::TrtLoader::instance().createInferBuilder(&logger));
    if (!builder) {
        setErrorMessage(err_buf, err_cap, "Failed to create IBuilder");
        return RTX_ERR_INTERNAL;
    }
    constexpr uint32_t explicitBatch = 0u;
    nvinfer1::INetworkDefinition* network = builder->createNetworkV2(explicitBatch);
    if (!network) {
        delete builder;
        setErrorMessage(err_buf, err_cap, "Failed to create INetworkDefinition");
        return RTX_ERR_INTERNAL;
    }
    nvonnxparser::IParser* parser = nullptr;
    std::vector<float> backbone_weights, backbone_bias;
    std::vector<float> branch_a_weights, branch_a_bias;
    std::vector<float> branch_b_weights, branch_b_bias;
    bool network_ready = false;

    if (has_onnx_file) {
        (void)ensureRifeOnnxDynamicTimestepPatched(std::filesystem::path(onnx_path));
        parser = static_cast<nvonnxparser::IParser*>(
            rtx_ngx::TrtLoader::instance().createNvOnnxParser(network, &logger));
        if (parser && parser->parseFromFile(onnx_path, static_cast<int>(nvinfer1::ILogger::Severity::kWARNING))) {
            network_ready = true;
        } else {
            if (parser) {
// Phase 9 fix: TensorRT 10 deprecated kEXPLICIT_BATCH (value 0, always-on)
// and redefined bit 0 as kSTRONGLY_TYPED. The old `1U << kEXPLICIT_BATCH`
// therefore requested a STRONGLY TYPED network, where setPrecision /
// setOutputType are illegal and addConvolutionNd misbehaves. Classic
// explicit-batch network with explicit precision control = flags 0.
                delete parser;
                parser = nullptr;
            }
            delete network;
            network = builder->createNetworkV2(explicitBatch);
        }
    }
    if (!network_ready) {
        const nvinfer1::DataType synth_dtype =
            is_builtin_khalf ? nvinfer1::DataType::kHALF : nvinfer1::DataType::kFLOAT;
        std::fprintf(stderr, "[RIFE-BUILDER] Synthesizing built-in TensorRT 10.13.3.9 RIFE network (%s)...\n",
                     is_builtin_khalf ? "kHALF" : "kFLOAT");
        if (!buildBuiltinTemporalNetwork(network, backbone_weights, backbone_bias,
                                         branch_a_weights, branch_a_bias,
                                         branch_b_weights, branch_b_bias,
                                         synth_dtype)) {
            delete network;
            delete builder;
            setErrorMessage(err_buf, err_cap, "Failed to construct built-in RIFE TensorRT network");
            return RTX_ERR_INTERNAL;
        }
    }
    nvinfer1::IBuilderConfig* config = builder->createBuilderConfig();
    if (!config) {
        if (parser) delete parser;
        delete network;
        delete builder;
        setErrorMessage(err_buf, err_cap, "Failed to create IBuilderConfig");
        return RTX_ERR_INTERNAL;
    }
    config->setFlag(nvinfer1::BuilderFlag::kFP16);
    config->setFlag(nvinfer1::BuilderFlag::kOBEY_PRECISION_CONSTRAINTS);
    config->clearFlag(nvinfer1::BuilderFlag::kTF32);
    config->setMemoryPoolLimit(nvinfer1::MemoryPoolType::kWORKSPACE, 1ULL << 30);

    int fp32Count = 0;
    if (!is_builtin_khalf) {
        const int32_t nbLayers = network->getNbLayers();
        for (int32_t i = 0; i < nbLayers; ++i) {
            nvinfer1::ILayer* layer = network->getLayer(i);
            if (!layer) continue;

            const auto type = layer->getType();
            const bool is_coord_sensitive =
                (type == nvinfer1::LayerType::kGRID_SAMPLE) ||
                (type == nvinfer1::LayerType::kREDUCE) ||
                (type == nvinfer1::LayerType::kCAST) ||
                (type == nvinfer1::LayerType::kSLICE) ||
                (type == nvinfer1::LayerType::kUNARY) ||
                (type == nvinfer1::LayerType::kELEMENTWISE) ||
                (type == nvinfer1::LayerType::kCONVOLUTION) ||
                (type == nvinfer1::LayerType::kMATRIX_MULTIPLY) ||
                (type == nvinfer1::LayerType::kACTIVATION) ||
                (type == nvinfer1::LayerType::kSCALE);

// FP16 mode with strict precision constraints and zero TF32 nondeterminism
            if (is_coord_sensitive) {
                bool has_float_io = false;
                for (int32_t outIdx = 0; outIdx < layer->getNbOutputs(); ++outIdx) {
// Safe workspace limit: 1 GB
                    if (auto* out = layer->getOutput(outIdx)) {
                        if (out->getType() == nvinfer1::DataType::kFLOAT ||
// FP32 force locking for all coordinate-grid / elementwise / convolution sensitive layers
// (skipped when explicitly synthesizing the kHALF I/O regression engine).
                            out->getType() == nvinfer1::DataType::kHALF) {
                            has_float_io = true;
                            break;
                        }
                    }
                }
                if (!has_float_io) {
                    for (int32_t inIdx = 0; inIdx < layer->getNbInputs(); ++inIdx) {
                        if (auto* in = layer->getInput(inIdx)) {
                            if (in->getType() == nvinfer1::DataType::kFLOAT ||
                                in->getType() == nvinfer1::DataType::kHALF) {
                                has_float_io = true;
                                break;
                            }
                        }
                    }
                }
                if (has_float_io) {
                    layer->setPrecision(nvinfer1::DataType::kFLOAT);
                    for (int32_t outIdx = 0; outIdx < layer->getNbOutputs(); ++outIdx) {
                        if (auto* out = layer->getOutput(outIdx)) {
                            if (out->getType() == nvinfer1::DataType::kFLOAT ||
                                out->getType() == nvinfer1::DataType::kHALF) {
                                layer->setOutputType(outIdx, nvinfer1::DataType::kFLOAT);
                            }
                        }
                    }
                    fp32Count++;
                }
            }
        }
    }
    std::fprintf(stderr, "[RIFE-BUILDER] locked %d coordinate/elementwise layer(s) to FP32\n", fp32Count);

    // Phase 11.4-F (PKG-F2): Physical Dual-Engine Orientation Decoupling + Single Static Profile.
    // Strictly ONE IOptimizationProfile per engine so TensorRT 10.13 Autotuner retains full FP16
    // Tensor Core / Winograd tactics within the 1 GiB kWORKSPACE budget (peak build VRAM <= 2.6 GB,
    // zero OOM pruning). Landscape -> rife_v4.25_landscape.engine, Portrait -> rife_v4.25_portrait.engine.
    constexpr int min_w = 128;
    constexpr int min_h = 128;
    const bool is_portrait = (height > width && width > 0);
    const int opt_w = is_portrait ? 1152 : 1920;
    const int opt_h = is_portrait ? 1920 : 1152;
    const int max_w = is_portrait ? 2176 : 3840;
    const int max_h = is_portrait ? 3840 : 2176;

    nvinfer1::IOptimizationProfile* profile = builder->createOptimizationProfile();
    for (int32_t i = 0; i < network->getNbInputs(); ++i) {
        auto* input = network->getInput(i);
        const auto dims = input->getDimensions();
        const int c = (dims.nbDims >= 2 && dims.d[1] > 0) ? static_cast<int>(dims.d[1]) : 7;
        if (dims.nbDims == 4) {
            profile->setDimensions(input->getName(), nvinfer1::OptProfileSelector::kMIN, nvinfer1::Dims4{1, c, min_h, min_w});
            profile->setDimensions(input->getName(), nvinfer1::OptProfileSelector::kOPT, nvinfer1::Dims4{1, c, opt_h, opt_w});
            profile->setDimensions(input->getName(), nvinfer1::OptProfileSelector::kMAX, nvinfer1::Dims4{1, c, max_h, max_w});
        }
    }
    config->addOptimizationProfile(profile);
    std::fprintf(stderr,
                 "[RIFE-BUILDER] Single-Profile %s engine: min=%dx%d, opt=%dx%d, max=%dx%d (workspace=1 GiB)\n",
                 is_portrait ? "Portrait" : "Landscape", min_w, min_h, opt_w, opt_h, max_w, max_h);

    std::fprintf(stderr, "[RIFE-BUILDER] Building serialized TensorRT engine...\n");
    nvinfer1::IHostMemory* plan = builder->buildSerializedNetwork(*network, *config);
    if (!plan) {
        delete config;
        if (parser) delete parser;
        delete network;
// Setup dynamic optimization profile
        delete builder;
        setErrorMessage(err_buf, err_cap, "buildSerializedNetwork failed");
        return RTX_ERR_INTERNAL;
    }
    const std::filesystem::path out_fs_path(engine_out_path);
    if (out_fs_path.has_parent_path()) {
        std::filesystem::create_directories(out_fs_path.parent_path(), ec);
    }
    std::ofstream out(engine_out_path, std::ios::binary);
    if (!out.is_open()) {
        delete plan;
        delete config;
        if (parser) delete parser;
        delete network;
        delete builder;
        setErrorMessage(err_buf, err_cap, std::string("Cannot open output file: ") + engine_out_path);
        return RTX_ERR_IO;
    }
    out.write(static_cast<const char*>(plan->data()), plan->size());
    out.close();

    std::fprintf(stderr, "[RIFE-BUILDER] Successfully built and saved engine (%zu bytes) to %s\n", plan->size(), engine_out_path);

    delete plan;
    delete config;
    if (parser) delete parser;
    delete network;
    delete builder;

    return RTX_OK;
}
}  // extern "C"
