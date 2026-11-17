# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
# Copyright (C) 2026 RTX Transcode Engine contributors
#
# Native CMake PTX-to-C++ header generator (replaces tools/embed_ptx.py).

if(NOT DEFINED INPUT_PTX OR NOT DEFINED OUTPUT_HEADER)
    message(FATAL_ERROR "Usage: cmake -DINPUT_PTX=<in.ptx> -DOUTPUT_HEADER=<out.h> -P cmake/EmbedPtx.cmake")
endif()

if(NOT EXISTS "${INPUT_PTX}")
    message(FATAL_ERROR "EmbedPtx.cmake: input file does not exist: ${INPUT_PTX}")
endif()

get_filename_component(_ptx_name "${INPUT_PTX}" NAME)
get_filename_component(_ptx_stem "${INPUT_PTX}" NAME_WE)
string(TOUPPER "${_ptx_stem}" _stem_upper)
set(_guard "RTX_NGX_BRIDGE_${_stem_upper}_PTX_H")

if(DEFINED VAR_NAME AND NOT "${VAR_NAME}" STREQUAL "")
    set(_var_name "${VAR_NAME}")
else()
    if(_ptx_stem STREQUAL "p010_pack")
        set(_var_name "kP010PackPtx")
    elseif(_ptx_stem STREQUAL "rife_pack")
        set(_var_name "kRifePackPtx")
    else()
        set(_var_name "kEmbeddedPtx")
    endif()
endif()

file(READ "${INPUT_PTX}" _ptx_text)
# Clamp PTX ISA header to .version 7.5 / .target sm_75 so drivers prior to CUDA 13.0 R580
# never reject the module with CUDA_ERROR_UNSUPPORTED_PTX_VERSION (222).
string(REGEX REPLACE "\\.version[ \t]+[0-9]+\\.[0-9]+" ".version 7.5" _ptx_text "${_ptx_text}")
string(REGEX REPLACE "\\.target[ \t]+sm_[0-9]+" ".target sm_75" _ptx_text "${_ptx_text}")

get_filename_component(_out_dir "${OUTPUT_HEADER}" DIRECTORY)
if(_out_dir)
    file(MAKE_DIRECTORY "${_out_dir}")
endif()

set(_hdr "/* SPDX-License-Identifier: MIT OR Apache-2.0\n")
string(APPEND _hdr " * Copyright (c) 2026 RTX Transcode Engine contributors\n")
string(APPEND _hdr " *\n")
string(APPEND _hdr " * GENERATED FILE - do not edit. Produced at build time from ${_ptx_name}.\n")
string(APPEND _hdr " */\n\n")
string(APPEND _hdr "#ifndef ${_guard}\n")
string(APPEND _hdr "#define ${_guard}\n\n")
string(APPEND _hdr "namespace rtx_ngx {\n\n")
string(APPEND _hdr "inline constexpr const char* ${_var_name} = R\"RTXPTX(\n")
string(APPEND _hdr "${_ptx_text})RTXPTX\";\n\n")
string(APPEND _hdr "}  // namespace rtx_ngx\n\n")
string(APPEND _hdr "#endif /* ${_guard} */\n")

file(WRITE "${OUTPUT_HEADER}" "${_hdr}")
