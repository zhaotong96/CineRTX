/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * Zone C internal header for TensorRT RIFE engine construction inside rtx_ngx_bridge.
 */

#ifndef RTX_NGX_RIFE_BUILDER_H
#define RTX_NGX_RIFE_BUILDER_H

#include "ngx_bridge_abi.h"

namespace rtx::ngx {

RtxResult buildRifeEngineInBridge(
    const RtxNgxRifeBuildParams* params,
    RtxNgxRifeBuildResult* result);

}  // namespace rtx::ngx

#endif  // RTX_NGX_RIFE_BUILDER_H
