# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
# Copyright (C) 2026 RTX Transcode Engine contributors
#
# Invoked by the rtx_stage_ngx_plugin target.  RTX VSR and TrueHDR are mandatory
# product features, so a CineRTX.exe without the separately loaded bridge and
# NVIDIA feature DLLs beside it must fail the build instead of starting with a
# silently degraded remaster path.

if(NOT DEFINED PLUGIN_DIR OR PLUGIN_DIR STREQUAL "")
    message(FATAL_ERROR "NGX staging check invoked without PLUGIN_DIR")
endif()

set(_missing "")
foreach(_name rtx_ngx_bridge.dll nvngx_vsr.dll nvngx_truehdr.dll)
    if(NOT EXISTS "${PLUGIN_DIR}/${_name}")
        list(APPEND _missing "${_name}")
    endif()
endforeach()

if(_missing)
    string(REPLACE ";" ", " _names "${_missing}")
    message(FATAL_ERROR
        "CineRTX.exe is missing the mandatory RTX Video plugin files in "
        "${PLUGIN_DIR}: ${_names}.\n"
        "The stage step copies them from build/external/plugins/nvidia_rtx and "
        "the extracted RTX Video SDK. Check that "
        "build/external/rtx_video_sdk/bin/Windows/x64/rel contains "
        "nvngx_vsr.dll and nvngx_truehdr.dll; delete "
        "build/external/rtx_video_sdk and reconfigure to re-extract the pinned "
        "archive if it does not.")
endif()
