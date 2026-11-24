/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 */

#include "dyn_library.h"

namespace rtx_shim {

void dynClose(DynLibrary& lib) {
    auto& [h, p, e] = lib;
    h.reset();
    p.clear();
    e.clear();
}

}  // namespace rtx_shim
