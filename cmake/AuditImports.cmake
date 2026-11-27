# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
# Copyright (C) 2026 RTX Transcode Engine contributors
#
# Gate P13-W0 (Phase 13 Wave 0 Single-EXE Static PE IAT Audit — 76_ / 77_ / 78_ Ruling):
#   Usage:
#     cmake -DTARGET_BIN=<path/to/CineRTX.exe> -P cmake/AuditImports.cmake
#
# Enforces:
#   - Zero rtx_headless_cli.exe sibling residual (Single-EXE convergence).
#   - Zero-tolerance static IAT blacklist (17 physical DLLs):
#     shell32.dll, dwmapi.dll, comdlg32.dll, comctl32.dll, uxtheme.dll,
#     d3d12.dll, dxgi.dll, dcomp.dll, ole32.dll, kernelbase.dll, advapi32.dll,
#     dwrite.dll, d2d1.dll, mmdevapi.dll, avrt.dll, gdi32.dll, user32.dll
#   - API Set (api-ms-win-*) strict contract whitelist:
#     ONLY api-ms-win-core-synch-l1-2-0.dll is allowed.
#   - Physical static IAT hard cap: 1/1 (kernel32.dll ONLY).

if(NOT DEFINED TARGET_BIN OR "${TARGET_BIN}" STREQUAL "")
    message(FATAL_ERROR "[AuditImports] Missing -DTARGET_BIN=<path/to/CineRTX.exe>")
endif()
if(NOT EXISTS "${TARGET_BIN}")
    message(FATAL_ERROR "[AuditImports] Binary not found: ${TARGET_BIN}")
endif()

get_filename_component(_BIN_DIR "${TARGET_BIN}" DIRECTORY)
if(EXISTS "${_BIN_DIR}/rtx_headless_cli.exe" OR EXISTS "${_BIN_DIR}/rtx_headless_cli")
    message(FATAL_ERROR "[AuditImports] FAIL: Prohibited legacy rtx_headless_cli binary found beside ${TARGET_BIN}")
endif()

set(_BLACKLIST
    "shell32.dll"
    "dwmapi.dll"
    "comdlg32.dll"
    "comctl32.dll"
    "uxtheme.dll"
    "d3d12.dll"
    "dxgi.dll"
    "dcomp.dll"
    "ole32.dll"
    "kernelbase.dll"
    "advapi32.dll"
    "dwrite.dll"
    "d2d1.dll"
    "mmdevapi.dll"
    "avrt.dll"
    "gdi32.dll"
    "user32.dll")

set(_APISET_ALLOWED
    "api-ms-win-core-synch-l1-2-0.dll")

set(_ALLOWED_PHYSICAL "kernel32.dll")
set(_MAX_PHYSICAL 1)

set(_IMPORTS "")
find_program(_DUMPBIN_EXE dumpbin)
if(NOT _DUMPBIN_EXE)
    message(FATAL_ERROR "[AuditImports] dumpbin.exe not found; run from VS Developer Prompt or build.bat")
endif()
execute_process(
    COMMAND "${_DUMPBIN_EXE}" /nologo /imports "${TARGET_BIN}"
    OUTPUT_VARIABLE _dump_out
    RESULT_VARIABLE _dump_rc)
if(NOT _dump_rc EQUAL 0)
    message(FATAL_ERROR "[AuditImports] dumpbin /imports failed (${_dump_rc}) for ${TARGET_BIN}")
endif()
string(REGEX MATCHALL "[A-Za-z0-9._-]+\.[dD][lL][lL]" _raw_dlls "${_dump_out}")
foreach(_d IN LISTS _raw_dlls)
    string(TOLOWER "${_d}" _dl)
    list(APPEND _IMPORTS "${_dl}")
endforeach()
if(NOT _IMPORTS)
    message(FATAL_ERROR "[AuditImports] Failed to parse PE imports for ${TARGET_BIN}")
endif()

list(REMOVE_DUPLICATES _IMPORTS)
list(SORT _IMPORTS)

set(_PHYSICAL_DLLS "")
set(_APISET_DLLS "")
set(_VIOLATIONS "")

foreach(_dll IN LISTS _IMPORTS)
    if(_dll MATCHES "^api-ms-win-")
        list(APPEND _APISET_DLLS "${_dll}")
        if(NOT _dll IN_LIST _APISET_ALLOWED)
            list(APPEND _VIOLATIONS "Non-whitelisted API Set static IAT DLL: ${_dll}")
        endif()
    else()
        list(APPEND _PHYSICAL_DLLS "${_dll}")
        if(_dll IN_LIST _BLACKLIST)
            list(APPEND _VIOLATIONS "Blacklisted physical static IAT DLL: ${_dll}")
        elseif(NOT _dll IN_LIST _ALLOWED_PHYSICAL)
            list(APPEND _VIOLATIONS "Non-whitelisted static IAT DLL: ${_dll}")
        endif()
    endif()
endforeach()

list(LENGTH _PHYSICAL_DLLS _phys_count)
list(LENGTH _APISET_DLLS _apiset_count)
string(JOIN ", " _phys_display ${_PHYSICAL_DLLS})
string(JOIN ", " _apiset_display ${_APISET_DLLS})

if(_phys_count GREATER _MAX_PHYSICAL)
    list(APPEND _VIOLATIONS "Physical static IAT count ${_phys_count} > ${_MAX_PHYSICAL} (${_phys_display})")
endif()

list(LENGTH _VIOLATIONS _vcount)
if(_vcount GREATER 0)
    foreach(_v IN LISTS _VIOLATIONS)
        message(WARNING "[AuditImports] ${_v}")
    endforeach()
    message(FATAL_ERROR "[AuditImports] FAIL (SINGLE-EXE): ${_vcount} violation(s) on ${TARGET_BIN}; physical(${_phys_count})=[${_phys_display}]; apiset(${_apiset_count})=[${_apiset_display}]")
endif()

message(STATUS "[AuditImports] PASS (SINGLE-EXE): ${_phys_count}/${_MAX_PHYSICAL} physical DLL(s) = [${_phys_display}]; ${_apiset_count} STL API Set(s) = [${_apiset_display}]")
