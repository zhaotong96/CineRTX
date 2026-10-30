/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * Static binding table for rtx_shim_static.  The single-EXE build links this
 * archive instead of shipping rtx_shim.dll; the function bodies are the exact
 * same translation units the DLL exports.
 */

#include "rtx/embedded_shim.h"

namespace rtx {
namespace {

const EmbeddedShimApi kEmbeddedShimApi{
    &rtx_shim_abi_version,
    &rtx_shim_probe,
    &rtx_shim_decoder_create,
    &rtx_shim_decoder_destroy,
    &rtx_shim_decoder_submit,
    &rtx_shim_decoder_receive,
    &rtx_shim_decoder_flush,
    &rtx_shim_decoder_release_frame,
    &rtx_shim_decoder_download,
    &rtx_shim_decoder_get_stats,
    &rtx_shim_decoder_last_error,
    &rtx_shim_encoder_create,
    &rtx_shim_encoder_destroy,
    &rtx_shim_encoder_encode,
    &rtx_shim_encoder_encode_host,
    &rtx_shim_encoder_flush,
    &rtx_shim_encoder_get_sequence_header,
    &rtx_shim_encoder_get_stats,
    &rtx_shim_encoder_last_error,
};

}  // namespace

const EmbeddedShimApi* embeddedShimApi() { return &kEmbeddedShimApi; }

}  // namespace rtx
