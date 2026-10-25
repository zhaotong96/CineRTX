/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * Opt-in bring-up tracing for the NVIDIA shim.  Set RTX_SHIM_TRACE=1 to print
 * every hardware entry point to stderr.  The trace is compiled in but costs a
 * single cached getenv() per call site when disabled, and it is the supported
 * way to diagnose a stall inside NVDEC/NVENC on a new GPU generation.
 */

#ifndef RTX_SHIM_NV_TRACE_H
#define RTX_SHIM_NV_TRACE_H

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace rtx_shim {

inline bool traceEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("RTX_SHIM_TRACE");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
    }();
    return enabled;
}

inline void trace(const char* format, ...) {
    if (!traceEnabled()) {
        return;
    }
    std::fputs("[rtx_shim] ", stderr);
    va_list arguments;
    va_start(arguments, format);
    std::vfprintf(stderr, format, arguments);
    va_end(arguments);
    std::fputc('\n', stderr);
    std::fflush(stderr);
}

}  // namespace rtx_shim

#define RTX_TRACE(...) ::rtx_shim::trace(__VA_ARGS__)

#endif /* RTX_SHIM_NV_TRACE_H */
