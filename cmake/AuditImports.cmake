# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
# Copyright (C) 2026 RTX Transcode Engine contributors
#
# Gate 12.3-1 (Wave 12.3 Dual-Mode Static PE IAT Audit — Ruling 058):
#   Usage:
#     cmake -DTARGET_BIN=<path/to/binary.exe> -DMODE=<HEADLESS|GUI> -P cmake/AuditImports.cmake
#
# Enforces:
#   - 10 physical DLL zero-tolerance static IAT blacklist:
#     shell32.dll, dwmapi.dll, comdlg32.dll, comctl32.dll, uxtheme.dll,
#     d3d12.dll, dxgi.dll, ole32.dll, kernelbase.dll, advapi32.dll
#   - API Set (api-ms-win-*) strict contract whitelist:
#     ONLY api-ms-win-core-synch-l1-2-0.dll is allowed (MSVC STL C++20/26
#     std::jthread / std::stop_token WaitOnAddress contract); any other
#     api-ms-win-* entry is rejected as a hard violation.
#   - Physical DLL counts are evaluated independently of STL API Sets:
#     - HEADLESS mode: physical static IAT MUST equal {kernel32.dll, bcrypt.dll} (2 items)
#     - GUI mode:      physical static IAT MUST be subset of {kernel32.dll, user32.dll, gdi32.dll, bcrypt.dll} (<= 4 items)

if(NOT DEFINED TARGET_BIN OR "${TARGET_BIN}" STREQUAL "")
    message(FATAL_ERROR "[AuditImports] Missing -DTARGET_BIN=<path/to/binary>")
endif()
if(NOT EXISTS "${TARGET_BIN}")
    message(FATAL_ERROR "[AuditImports] Binary not found: ${TARGET_BIN}")
endif()
if(NOT DEFINED MODE OR "${MODE}" STREQUAL "")
    set(MODE "GUI")
endif()
string(TOUPPER "${MODE}" MODE)

if(NOT MODE STREQUAL "HEADLESS" AND NOT MODE STREQUAL "GUI")
    message(FATAL_ERROR "[AuditImports] Invalid -DMODE=${MODE}; expected HEADLESS or GUI")
endif()

set(_BLACKLIST
    "shell32.dll"
    "dwmapi.dll"
    "comdlg32.dll"
    "comctl32.dll"
    "uxtheme.dll"
    "d3d12.dll"
    "dxgi.dll"
    "ole32.dll"
    "kernelbase.dll"
    "advapi32.dll")

set(_APISET_ALLOWED
    "api-ms-win-core-synch-l1-2-0.dll")

set(_HEADLESS_ALLOWED "kernel32.dll" "bcrypt.dll")
set(_GUI_ALLOWED "kernel32.dll" "user32.dll" "gdi32.dll" "bcrypt.dll")

set(_IMPORTS "")
find_program(_DUMPBIN_EXE dumpbin)
if(_DUMPBIN_EXE)
    execute_process(
        COMMAND "${_DUMPBIN_EXE}" /nologo /imports "${TARGET_BIN}"
        OUTPUT_VARIABLE _dump_out
        RESULT_VARIABLE _dump_rc)
    if(_dump_rc EQUAL 0)
        string(REGEX MATCHALL "[A-Za-z0-9._-]+\\.[dD][lL][lL]" _raw_dlls "${_dump_out}")
        foreach(_d IN LISTS _raw_dlls)
            string(TOLOWER "${_d}" _dl)
            list(APPEND _IMPORTS "${_dl}")
        endforeach()
    endif()
endif()

if(NOT _IMPORTS)
    find_program(_PY_EXE NAMES python3 python)
    if(NOT _PY_EXE)
        message(FATAL_ERROR "[AuditImports] Neither dumpbin nor python3/python is available to inspect ${TARGET_BIN}")
    endif()
    set(_py_script "
import struct, sys
data = open(sys.argv[1], 'rb').read()
if data[:2] != b'MZ': sys.exit(2)
pe = struct.unpack_from('<I', data, 0x3C)[0]
if data[pe:pe+4] != b'PE\\x00\\x00': sys.exit(2)
nsec = struct.unpack_from('<H', data, pe + 6)[0]
opt_sz = struct.unpack_from('<H', data, pe + 20)[0]
magic = struct.unpack_from('<H', data, pe + 24)[0]
dd_off = pe + 24 + (112 if magic == 0x20B else 96)
imp_rva, imp_sz = struct.unpack_from('<II', data, dd_off + 8)
sec_off = pe + 24 + opt_sz
secs = []
for i in range(nsec):
    o = sec_off + i * 40
    vsz, vrva, rsz, rptr = struct.unpack_from('<IIII', data, o + 8)
    secs.append((vrva, max(vsz, rsz), rptr))
def rva2off(rva):
    for vrva, sz, rptr in secs:
        if vrva <= rva < vrva + sz:
            return rptr + (rva - vrva)
    return None
dlls = []
if imp_rva:
    off = rva2off(imp_rva)
    while off is not None and off + 20 <= len(data):
        ilt, ts, fc, name_rva, iat = struct.unpack_from('<IIIII', data, off)
        if ilt == 0 and name_rva == 0: break
        noff = rva2off(name_rva)
        if noff is not None:
            end = data.find(b'\\x00', noff)
            dlls.append(data[noff:end].decode('ascii', errors='ignore').lower())
        off += 20
print(';'.join(sorted(set(dlls))))
")
    execute_process(
        COMMAND "${_PY_EXE}" -c "${_py_script}" "${TARGET_BIN}"
        OUTPUT_VARIABLE _py_out
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _py_rc)
    if(NOT _py_rc EQUAL 0)
        message(FATAL_ERROR "[AuditImports] PE import parsing failed for ${TARGET_BIN}")
    endif()
    set(_IMPORTS "${_py_out}")
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
        elseif(MODE STREQUAL "HEADLESS" AND NOT _dll IN_LIST _HEADLESS_ALLOWED)
            list(APPEND _VIOLATIONS "Non-whitelisted HEADLESS physical static IAT DLL: ${_dll}")
        elseif(MODE STREQUAL "GUI" AND NOT _dll IN_LIST _GUI_ALLOWED)
            list(APPEND _VIOLATIONS "Non-whitelisted GUI physical static IAT DLL: ${_dll}")
        endif()
    endif()
endforeach()

list(LENGTH _PHYSICAL_DLLS _phys_count)
list(LENGTH _APISET_DLLS _apiset_count)
string(JOIN ", " _phys_display ${_PHYSICAL_DLLS})
string(JOIN ", " _apiset_display ${_APISET_DLLS})

if(MODE STREQUAL "HEADLESS" AND NOT _phys_count EQUAL 2)
    list(APPEND _VIOLATIONS "HEADLESS physical static IAT count ${_phys_count} != 2 (${_phys_display})")
elseif(MODE STREQUAL "GUI" AND _phys_count GREATER 4)
    list(APPEND _VIOLATIONS "GUI physical static IAT count ${_phys_count} > 4 (${_phys_display})")
endif()

list(LENGTH _VIOLATIONS _vcount)
if(_vcount GREATER 0)
    foreach(_v IN LISTS _VIOLATIONS)
        message(WARNING "[AuditImports] ${_v}")
    endforeach()
    message(FATAL_ERROR "[AuditImports] FAIL (${MODE}): ${_vcount} violation(s) on ${TARGET_BIN}; physical(${_phys_count})=[${_phys_display}]; apiset(${_apiset_count})=[${_apiset_display}]")
endif()

message(STATUS "[AuditImports] PASS (${MODE}): ${_phys_count} physical DLL(s) = [${_phys_display}]; ${_apiset_count} STL API Set(s) = [${_apiset_display}]")
