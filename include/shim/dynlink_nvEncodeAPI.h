/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * Thin dynamic-loading wrapper for the MIT nv-codec-headers encode API.
 *
 * Upstream nv-codec-headers ships nvEncodeAPI.h plus the loader typedefs for
 * nvcuvid, but (as of n13.0.19.1) it does not ship a dynlink_nvEncodeAPI.h.
 * This wrapper provides the loader surface downstream projects expect without
 * vendoring any proprietary SDK file: every declaration below is derived from
 * the MIT-licensed <ffnvcodec/nvEncodeAPI.h> resolved through the
 * third_party/nv-codec-headers submodule.
 */

#ifndef RTX_SHIM_DYNLINK_NVENCODEAPI_H
#define RTX_SHIM_DYNLINK_NVENCODEAPI_H

#include <ffnvcodec/nvEncodeAPI.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef NVENCSTATUS(NVENCAPI* tNvEncodeAPICreateInstance)(
    NV_ENCODE_API_FUNCTION_LIST* functionList);
typedef NVENCSTATUS(NVENCAPI* tNvEncodeAPIGetMaxSupportedVersion)(
    uint32_t* version);

#ifndef NVENC_NO_DEPRECATED
typedef NVENCSTATUS(NVENCAPI* tNvEncodeAPIGetMaxSupportedVersionLegacy)(
    uint32_t* version);
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* RTX_SHIM_DYNLINK_NVENCODEAPI_H */
