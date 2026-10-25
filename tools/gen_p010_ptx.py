#!/usr/bin/env python3
"""Compile p010_pack.cu to PTX with NVRTC and emit an embeddable header.

The engine must not link the CUDA runtime, so the pack kernel is shipped as PTX
text that the driver JITs through cuModuleLoadData.  This script is the
build-time producer of that text:

    python tools/gen_p010_ptx.py --nvrtc <path to nvrtc64_*.dll> \
                                 --cuda-include <cuda_runtime/include>

It is only needed when the kernel source changes; the generated header is
checked in so a normal build never requires NVRTC or a CUDA toolkit.

NVRTC is called through ctypes on purpose: no CUDA toolkit, nvcc, or Python
package installation is required, only the redistributable nvrtc DLL.
"""

import argparse
import ctypes
import os
import pathlib
import sys

KERNEL_SOURCE = pathlib.Path("src/shim/ngx_bridge/p010_pack.cu")
OUTPUT_HEADER = pathlib.Path("src/shim/ngx_bridge/p010_pack_ptx.h")
OUTPUT_PTX = pathlib.Path("src/shim/ngx_bridge/p010_pack.ptx")


def check(result: int, call: str) -> None:
    if result != 0:
        raise RuntimeError(f"{call} failed with NVRTC error {result}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--nvrtc", required=True, help="path to nvrtc64_*.dll")
    parser.add_argument("--cuda-include", required=True, help="CUDA include directory")
    parser.add_argument("--arch", default="compute_75", help="PTX virtual architecture")
    args = parser.parse_args()

    source = KERNEL_SOURCE.read_text(encoding="utf-8")
    nvrtc_path = pathlib.Path(args.nvrtc).resolve()
    # NVRTC resolves nvrtc-builtins64_*.dll with a legacy bare-name LoadLibrary,
    # which only searches the application directory, the working directory and
    # PATH.  Put its directory on both so the redistributable wheel works from
    # anywhere.
    nvrtc_dir = str(nvrtc_path.parent)
    os.environ["PATH"] = nvrtc_dir + os.pathsep + os.environ.get("PATH", "")
    if hasattr(os, "add_dll_directory"):
        os.add_dll_directory(nvrtc_dir)
    previous_cwd = os.getcwd()
    os.chdir(nvrtc_dir)
    try:
        lib = ctypes.CDLL(str(nvrtc_path))
    finally:
        os.chdir(previous_cwd)

    lib.nvrtcCreateProgram.argtypes = [
        ctypes.POINTER(ctypes.c_void_p), ctypes.c_char_p, ctypes.c_char_p,
        ctypes.c_int, ctypes.POINTER(ctypes.c_char_p), ctypes.POINTER(ctypes.c_char_p)]
    lib.nvrtcCreateProgram.restype = ctypes.c_int
    lib.nvrtcCompileProgram.argtypes = [
        ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(ctypes.c_char_p)]
    lib.nvrtcCompileProgram.restype = ctypes.c_int
    lib.nvrtcGetPTXSize.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_size_t)]
    lib.nvrtcGetPTXSize.restype = ctypes.c_int
    lib.nvrtcGetPTX.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    lib.nvrtcGetPTX.restype = ctypes.c_int
    lib.nvrtcGetProgramLogSize.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_size_t)]
    lib.nvrtcGetProgramLogSize.restype = ctypes.c_int
    lib.nvrtcGetProgramLog.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    lib.nvrtcGetProgramLog.restype = ctypes.c_int
    lib.nvrtcDestroyProgram.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
    lib.nvrtcDestroyProgram.restype = ctypes.c_int

    program = ctypes.c_void_p()
    check(lib.nvrtcCreateProgram(ctypes.byref(program), source.encode("utf-8"),
                                 b"p010_pack.cu", 0, None, None), "nvrtcCreateProgram")

    options = [
        f"--gpu-architecture={args.arch}",
        "--std=c++17",
        "-default-device",
        "--device-as-default-execution-space",
        f"-I{pathlib.Path(args.cuda_include).resolve()}",
        "-lineinfo",
    ]
    encoded = [option.encode("utf-8") for option in options]
    array = (ctypes.c_char_p * len(encoded))(*encoded)
    result = lib.nvrtcCompileProgram(program, len(encoded), array)

    log_size = ctypes.c_size_t()
    lib.nvrtcGetProgramLogSize(program, ctypes.byref(log_size))
    if log_size.value > 1:
        log = ctypes.create_string_buffer(log_size.value)
        lib.nvrtcGetProgramLog(program, log)
        text = log.value.decode("utf-8", "replace").strip()
        if text:
            print(text, file=sys.stderr)
    if result != 0:
        lib.nvrtcDestroyProgram(ctypes.byref(program))
        raise RuntimeError(f"nvrtcCompileProgram failed with error {result}")

    ptx_size = ctypes.c_size_t()
    check(lib.nvrtcGetPTXSize(program, ctypes.byref(ptx_size)), "nvrtcGetPTXSize")
    buffer = ctypes.create_string_buffer(ptx_size.value)
    check(lib.nvrtcGetPTX(program, buffer), "nvrtcGetPTX")
    lib.nvrtcDestroyProgram(ctypes.byref(program))

    ptx = buffer.value.decode("utf-8")
    OUTPUT_PTX.write_text(ptx, encoding="utf-8")

    guard = "RTX_NGX_BRIDGE_P010_PACK_PTX_H"
    header = f"""/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * GENERATED FILE - do not edit.  Produced by tools/gen_p010_ptx.py from
 * src/shim/ngx_bridge/p010_pack.cu with NVRTC ({args.arch}).
 *
 * The PTX text is JIT-compiled by the CUDA driver at runtime
 * (cuModuleLoadData), so the bridge never links cudart64_*.dll.
 */

#ifndef {guard}
#define {guard}

namespace rtx_ngx {{

inline constexpr const char* kP010PackPtx = R"RTXPTX(
{ptx})RTXPTX";

}}  // namespace rtx_ngx

#endif /* {guard} */
"""
    OUTPUT_HEADER.write_text(header, encoding="utf-8")
    print(f"wrote {OUTPUT_PTX} ({len(ptx)} bytes) and {OUTPUT_HEADER}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
