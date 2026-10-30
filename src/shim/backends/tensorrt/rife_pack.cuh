/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * CUDA launch interface for the RIFE pre/post-processing kernels (W8-1).
 *
 * Declared separately from the .cu so a host-only build (no CUDA toolchain) can
 * still see the contract, and so rife_engine.cpp never needs a CUDA header.
 */

#ifndef RTX_SHIM_RIFE_PACK_CUH
#define RTX_SHIM_RIFE_PACK_CUH

#include <cstdint>

// Tensor layout the RIFE v4.25 IFNet expects: NCHW FP16, height padded up to a
// multiple of 32 (1080 -> 1088), six RGB channels (frame N then frame N+1) plus
// the timestep scalar in channel 6.
struct RtxRifePackParams {
    const uint8_t* previous_nv12;
    const uint8_t* next_nv12;
    uint16_t* tensor;       // [1][7][tensor_h][tensor_w]
    int width;              // 1920
    int height;             // 1080
    int y_pitch;            // bytes
    int uv_pitch;           // bytes
    int tensor_width;       // 1920
    int tensor_height;      // 1088
    float timestep;         // 0.5f
};

struct RtxRifeUnpackParams {
    const uint16_t* tensor; // [1][3][tensor_h][tensor_w]
    uint8_t* nv12;          // out, width x height NV12
    int width;
    int height;
    int y_pitch;
    int uv_pitch;
    int tensor_width;
    int tensor_height;
};

extern "C" {

// Returns 0 on success.  Both launchers are asynchronous on `stream`; the
// caller owns the synchronisation.
int rtx_rife_pack_launch(const RtxRifePackParams* params, void* stream);
int rtx_rife_unpack_launch(const RtxRifeUnpackParams* params, void* stream);

}  // extern "C"

#endif /* RTX_SHIM_RIFE_PACK_CUH */
