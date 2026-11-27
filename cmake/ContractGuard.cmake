# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
# Copyright (C) 2026 RTX Transcode Engine contributors
#
# Phase 13 (Wave 0-A): Mechanical Contract Symbol Guard (77_wave0-A Guardrail 1).
# Works both via include(cmake/ContractGuard.cmake) and standalone cmake -P cmake/ContractGuard.cmake.

get_filename_component(_CG_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(DEFINED ROOT AND NOT "${ROOT}" STREQUAL "")
    get_filename_component(_CG_ROOT "${ROOT}" ABSOLUTE)
endif()

set(_CG_NGX_HDR "${_CG_ROOT}/include/rtx/ngx_remaster.h")
if(NOT EXISTS "${_CG_NGX_HDR}")
    message(FATAL_ERROR "[ContractGuard] CRITICAL: Missing ${_CG_NGX_HDR}")
endif()
file(READ "${_CG_NGX_HDR}" _CG_HDR_CONTENT)
if(NOT _CG_HDR_CONTENT MATCHES "consumeNvofSceneCut")
    message(FATAL_ERROR "[ContractGuard] CRITICAL: 'consumeNvofSceneCut' signature missing from include/rtx/ngx_remaster.h! Aborting build.")
endif()

set(_CG_JQ_SRC "${_CG_ROOT}/src/core/job_queue.cpp")
if(NOT EXISTS "${_CG_JQ_SRC}")
    message(FATAL_ERROR "[ContractGuard] CRITICAL: Missing ${_CG_JQ_SRC}")
endif()
file(READ "${_CG_JQ_SRC}" _CG_JQ_CONTENT)
if(NOT _CG_JQ_CONTENT MATCHES "isPreviewEnabled")
    message(FATAL_ERROR "[ContractGuard] CRITICAL: 'isPreviewEnabled()' check missing from src/core/job_queue.cpp! Aborting build.")
endif()

message(STATUS "[ContractGuard] PASS: Protected baseline symbols (consumeNvofSceneCut, isPreviewEnabled) intact.")
