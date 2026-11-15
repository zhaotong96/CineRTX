# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
# Copyright (C) 2026 RTX Transcode Engine contributors
#
# Native CMake SPDX header, GPL/MIT boundary, and repository hygiene gate
# (replaces tools/check_spdx.ps1).

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(DEFINED ROOT AND NOT "${ROOT}" STREQUAL "")
    get_filename_component(REPO_ROOT "${ROOT}" ABSOLUTE)
endif()

set(GPL_EXPR "GPL-3.0-or-later WITH Plugin-Exception")
set(MIT_EXPR "MIT OR Apache-2.0")
set(VIOLATIONS "")
set(GPL_COUNT 0)
set(MIT_COUNT 0)

# 1. Verify forbidden legacy directories/archives are absent
if(EXISTS "${REPO_ROOT}/tools")
    list(APPEND VIOLATIONS "tools/: legacy script directory must be removed (use native cmake/*.cmake)")
endif()
file(GLOB _legacy_of_zips "${REPO_ROOT}/Optical_Flow_SDK*")
if(_legacy_of_zips)
    list(APPEND VIOLATIONS "Optical_Flow_SDK*: legacy archive present in repository root")
endif()

function(extract_spdx filepath out_expr)
    file(STRINGS "${filepath}" _lines LIMIT_COUNT 8 ENCODING UTF-8)
    set(_found "")
    foreach(_line IN LISTS _lines)
        if(_line MATCHES "SPDX-License-Identifier:[ \t]*([^ \t\r\n*]+.*)")
            set(_raw "${CMAKE_MATCH_1}")
            string(REGEX REPLACE "[ \t]*\\*/.*$" "" _raw "${_raw}")
            string(STRIP "${_raw}" _found)
            break()
        endif()
    endforeach()
    set(${out_expr} "${_found}" PARENT_SCOPE)
endfunction()

# 2. Source & header audit across include/, src/, tests/
foreach(_dir "include" "src" "tests")
    if(NOT EXISTS "${REPO_ROOT}/${_dir}")
        list(APPEND VIOLATIONS "${_dir}/: directory is missing")
    else()
        file(GLOB_RECURSE _files
            "${REPO_ROOT}/${_dir}/*.h"
            "${REPO_ROOT}/${_dir}/*.hpp"
            "${REPO_ROOT}/${_dir}/*.c"
            "${REPO_ROOT}/${_dir}/*.cc"
            "${REPO_ROOT}/${_dir}/*.cpp"
            "${REPO_ROOT}/${_dir}/*.cxx"
            "${REPO_ROOT}/${_dir}/*.cu"
            "${REPO_ROOT}/${_dir}/*.cuh")
        foreach(_f IN LISTS _files)
            file(RELATIVE_PATH _rel "${REPO_ROOT}" "${_f}")
            file(SIZE "${_f}" _sz)
            if(_sz EQUAL 0)
                list(APPEND VIOLATIONS "${_rel}: empty source file")
            else()
                extract_spdx("${_f}" _spdx)
                if("${_spdx}" STREQUAL "")
                    list(APPEND VIOLATIONS "${_rel}: missing SPDX-License-Identifier")
                else()
                    if(_rel MATCHES "^(include/shim/|src/shim/)")
                        set(_expected "${MIT_EXPR}")
                    else()
                        set(_expected "${GPL_EXPR}")
                    endif()
                    if(NOT "${_spdx}" STREQUAL "${_expected}")
                        list(APPEND VIOLATIONS "${_rel}: expected '${_expected}', found '${_spdx}'")
                    else()
                        if("${_expected}" STREQUAL "${GPL_EXPR}")
                            math(EXPR GPL_COUNT "${GPL_COUNT} + 1")
                        else()
                            math(EXPR MIT_COUNT "${MIT_COUNT} + 1")
                        endif()
                    endif()
                endif()
            endif()
        endforeach()
    endif()
endforeach()

# 3. Build & CI files
foreach(_bfile "${REPO_ROOT}/CMakeLists.txt")
    if(EXISTS "${_bfile}")
        extract_spdx("${_bfile}" _spdx)
        if(NOT "${_spdx}" STREQUAL "${GPL_EXPR}")
            list(APPEND VIOLATIONS "CMakeLists.txt: expected '${GPL_EXPR}', found '${_spdx}'")
        else()
            math(EXPR GPL_COUNT "${GPL_COUNT} + 1")
        endif()
    else()
        list(APPEND VIOLATIONS "CMakeLists.txt: missing")
    endif()
endforeach()

file(GLOB _cmake_files "${REPO_ROOT}/cmake/*.cmake")
foreach(_cf IN LISTS _cmake_files)
    file(RELATIVE_PATH _rel "${REPO_ROOT}" "${_cf}")
    extract_spdx("${_cf}" _spdx)
    if(NOT "${_spdx}" STREQUAL "${GPL_EXPR}")
        list(APPEND VIOLATIONS "${_rel}: expected '${GPL_EXPR}', found '${_spdx}'")
    else()
        math(EXPR GPL_COUNT "${GPL_COUNT} + 1")
    endif()
endforeach()

file(GLOB _wf_files "${REPO_ROOT}/.github/workflows/*.yml" "${REPO_ROOT}/.github/workflows/*.yaml")
foreach(_wf IN LISTS _wf_files)
    file(RELATIVE_PATH _rel "${REPO_ROOT}" "${_wf}")
    extract_spdx("${_wf}" _spdx)
    if(NOT "${_spdx}" STREQUAL "${GPL_EXPR}")
        list(APPEND VIOLATIONS "${_rel}: expected '${GPL_EXPR}', found '${_spdx}'")
    else()
        math(EXPR GPL_COUNT "${GPL_COUNT} + 1")
    endif()
endforeach()

# 4. Binary & AI model blob check in tracked source trees
foreach(_dir "include" "src" "tests" "cmake" "docs")
    if(EXISTS "${REPO_ROOT}/${_dir}")
        file(GLOB_RECURSE _blobs
            "${REPO_ROOT}/${_dir}/*.lib"
            "${REPO_ROOT}/${_dir}/*.a"
            "${REPO_ROOT}/${_dir}/*.dll"
            "${REPO_ROOT}/${_dir}/*.so"
            "${REPO_ROOT}/${_dir}/*.exe"
            "${REPO_ROOT}/${_dir}/*.obj"
            "${REPO_ROOT}/${_dir}/*.o"
            "${REPO_ROOT}/${_dir}/*.pdb"
            "${REPO_ROOT}/${_dir}/*.engine"
            "${REPO_ROOT}/${_dir}/*.onnx"
            "${REPO_ROOT}/${_dir}/*.plan"
            "${REPO_ROOT}/${_dir}/*.trt")
        foreach(_b IN LISTS _blobs)
            file(RELATIVE_PATH _rel "${REPO_ROOT}" "${_b}")
            list(APPEND VIOLATIONS "${_rel}: forbidden binary or model blob in source tree")
        endforeach()
    endif()
endforeach()

message(STATUS "SPDX and boundary audit (native CMake)")
message(STATUS "  root     : ${REPO_ROOT}")
message(STATUS "  licenses : GPL=${GPL_COUNT} MIT=${MIT_COUNT}")

if(VIOLATIONS)
    foreach(_v IN LISTS VIOLATIONS)
        message(STATUS "  FAIL ${_v}")
    endforeach()
    list(LENGTH VIOLATIONS _vcount)
    message(FATAL_ERROR "AuditSpdx FAILED with ${_vcount} violation(s)")
endif()
message(STATUS "  result   : PASS")
