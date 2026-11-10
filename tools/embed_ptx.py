#!/usr/bin/env python3
"""Generate embeddable C++ header containing PTX text at build time.

Usage:
    python tools/embed_ptx.py <input.ptx> <output_header.h>
"""

import pathlib
import sys


def main() -> int:
    if len(sys.argv) < 3:
        print("Usage: embed_ptx.py <input.ptx> <output_header.h>", file=sys.stderr)
        return 1

    ptx_path = pathlib.Path(sys.argv[1]).resolve()
    out_path = pathlib.Path(sys.argv[2]).resolve()

    if not ptx_path.exists():
        print(f"Error: input file {ptx_path} does not exist", file=sys.stderr)
        return 1

    ptx_text = ptx_path.read_text(encoding="utf-8")
    guard = "RTX_NGX_BRIDGE_P010_PACK_PTX_H"
    header = f"""/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * GENERATED FILE - do not edit. Produced at build time from {ptx_path.name}.
 */

#ifndef {guard}
#define {guard}

namespace rtx_ngx {{

inline constexpr const char* kP010PackPtx = R"RTXPTX(
{ptx_text})RTXPTX";

}}  // namespace rtx_ngx

#endif /* {guard} */
"""
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(header, encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
