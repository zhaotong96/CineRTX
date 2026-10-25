/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/*
 * Thin dynamic-loading wrapper for the open NVENC headers.  FFmpeg's
 * nv-codec-headers distribution ships nvEncodeAPI.h plus the loader typedefs
 * for nvcuvid, but (as of n13.0.19.1) it does not ship a separate
 * dynlink_nvEncodeAPI.h.  This wrapper provides the same loader surface that
 * downstream projects expect without copying any proprietary SDK file:
 * every declaration below is derived from the MIT-licensed nvEncodeAPI.h.
 */

#ifndef FFNVCODEC_DYNLINK_NVENCODEAPI_H
#define FFNVCODEC_DYNLINK_NVENCODEAPI_H

#include "nvEncodeAPI.h"

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

#endif /* FFNVCODEC_DYNLINK_NVENCODEAPI_H */
