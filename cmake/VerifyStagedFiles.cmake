# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
# Copyright (C) 2026 RTX Transcode Engine contributors
#
# Invoked by the staging targets that deploy runtime modules beside
# CineRTX.exe.  A staged file that silently fails to arrive turns a mandatory
# product feature into a run-time "unavailable" report, so the build fails here
# instead.
#
#   cmake -DSTAGE_DIR=<dir> -DREQUIRED_FILES=a.dll;b.dll -P VerifyStagedFiles.cmake

if(NOT DEFINED STAGE_DIR OR STAGE_DIR STREQUAL "")
    message(FATAL_ERROR "Staging check invoked without STAGE_DIR")
endif()

if(NOT DEFINED REQUIRED_FILES OR REQUIRED_FILES STREQUAL "")
    message(FATAL_ERROR "Staging check invoked without REQUIRED_FILES")
endif()

set(_missing "")
foreach(_name IN LISTS REQUIRED_FILES)
    if(NOT EXISTS "${STAGE_DIR}/${_name}")
        list(APPEND _missing "${_name}")
    endif()
endforeach()

if(_missing)
    string(REPLACE ";" ", " _names "${_missing}")
    message(FATAL_ERROR
        "The runtime staging directory ${STAGE_DIR} is missing: ${_names}.\n"
        "These modules are resolved by the NVIDIA shim from the executable "
        "directory only (no System32 fallback), so a build without them would "
        "start with the affected feature reported unavailable. Check that the "
        "provisioned SDKs under build/external/optical_flow_sdk and "
        "build/external/tensorrt still contain them, then rebuild.")
endif()
