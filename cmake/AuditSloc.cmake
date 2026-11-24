# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
# Copyright (C) 2026 RTX Transcode Engine contributors
#
# Gate R-1 & Phase 12 Dual-Track Source Code Accounting (Ruling 058):
# Measures Pure SLOC (excluding blank lines and comments), Semantic Comments,
# and Total Physical Lines across the locked canonical source tree, plus
# per-domain Pure SLOC breakdown separating Core Engine from GUI/Shim/Tests/Build.
#
# Targets:
#   - Core Engine Pure SLOC (include/ + src/core/ + src/io/ + src/cli/): <= 23,000
#   - Full-Repo Canonical Tree: tracked transparently against locked 749fe0d baseline (31,064)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(DEFINED ROOT AND NOT "${ROOT}" STREQUAL "")
    get_filename_component(REPO_ROOT "${ROOT}" ABSOLUTE)
endif()

# 1. Collect fixed canonical source set
file(GLOB_RECURSE _sloc_include
    "${REPO_ROOT}/include/*.h"
    "${REPO_ROOT}/include/*.hpp")

file(GLOB_RECURSE _sloc_src
    "${REPO_ROOT}/src/*.h"
    "${REPO_ROOT}/src/*.hpp"
    "${REPO_ROOT}/src/*.cpp")

file(GLOB_RECURSE _sloc_tests
    "${REPO_ROOT}/tests/*.h"
    "${REPO_ROOT}/tests/*.hpp"
    "${REPO_ROOT}/tests/*.cpp")

file(GLOB _sloc_cmake
    "${REPO_ROOT}/cmake/*.cmake")

set(_sloc_files
    ${_sloc_include}
    ${_sloc_src}
    ${_sloc_tests}
    ${_sloc_cmake})

set(_sloc_extras
    "${REPO_ROOT}/CMakeLists.txt")

foreach(_extra IN LISTS _sloc_extras)
    if(EXISTS "${_extra}")
        list(APPEND _sloc_files "${_extra}")
    endif()
endforeach()

list(REMOVE_DUPLICATES _sloc_files)
list(SORT _sloc_files)

list(LENGTH _sloc_files _file_count)

# 2. Iterate and parse each file (full-repo + per-domain counters)
set(_total_physical 0)
set(_total_blank 0)
set(_total_comment 0)
set(_total_sloc 0)

set(_core_files 0)
set(_core_sloc 0)
set(_gui_files 0)
set(_gui_sloc 0)
set(_shim_files 0)
set(_shim_sloc 0)
set(_tests_files 0)
set(_tests_sloc 0)
set(_build_files 0)
set(_build_sloc 0)

foreach(_file IN LISTS _sloc_files)
    file(RELATIVE_PATH _rel "${REPO_ROOT}" "${_file}")
    file(READ "${_file}" _content)
    string(REGEX REPLACE "\r\n" "\n" _content "${_content}")
    string(REGEX REPLACE "\r" "\n" _content "${_content}")

    # Escape semicolons to safely turn string into list of lines
    string(REPLACE "\\" "\\\\" _content "${_content}")
    string(REPLACE ";" "\\;" _content "${_content}")
    string(REPLACE "\n" ";" _lines "${_content}")

    set(_in_block_comment FALSE)
    set(_file_sloc 0)

    foreach(_line IN LISTS _lines)
        string(REPLACE "\\;" ";" _raw_line "${_line}")
        string(REPLACE "\\\\" "\\" _raw_line "${_raw_line}")
        math(EXPR _total_physical "${_total_physical} + 1")
        string(STRIP "${_raw_line}" _stripped)

        if("${_stripped}" STREQUAL "")
            math(EXPR _total_blank "${_total_blank} + 1")
        elseif(_in_block_comment)
            math(EXPR _total_comment "${_total_comment} + 1")
            if(_stripped MATCHES "\\*/")
                set(_in_block_comment FALSE)
            endif()
        elseif(_stripped MATCHES "^/\\*")
            math(EXPR _total_comment "${_total_comment} + 1")
            if(NOT _stripped MATCHES "\\*/")
                set(_in_block_comment TRUE)
            endif()
        elseif(_stripped MATCHES "^(//|#)")
            math(EXPR _total_comment "${_total_comment} + 1")
        else()
            math(EXPR _total_sloc "${_total_sloc} + 1")
            math(EXPR _file_sloc "${_file_sloc} + 1")
        endif()
    endforeach()

    if(_rel MATCHES "^(include/|src/core/|src/io/|src/cli/)")
        math(EXPR _core_files "${_core_files} + 1")
        math(EXPR _core_sloc "${_core_sloc} + ${_file_sloc}")
    elseif(_rel MATCHES "^src/gui/")
        math(EXPR _gui_files "${_gui_files} + 1")
        math(EXPR _gui_sloc "${_gui_sloc} + ${_file_sloc}")
    elseif(_rel MATCHES "^src/shim/")
        math(EXPR _shim_files "${_shim_files} + 1")
        math(EXPR _shim_sloc "${_shim_sloc} + ${_file_sloc}")
    elseif(_rel MATCHES "^tests/")
        math(EXPR _tests_files "${_tests_files} + 1")
        math(EXPR _tests_sloc "${_tests_sloc} + ${_file_sloc}")
    else()
        math(EXPR _build_files "${_build_files} + 1")
        math(EXPR _build_sloc "${_build_sloc} + ${_file_sloc}")
    endif()
endforeach()

if(NOT DEFINED CEILING_CORE_SLOC OR "${CEILING_CORE_SLOC}" STREQUAL "")
    set(CEILING_CORE_SLOC 23000)
endif()
if(_core_sloc LESS_EQUAL CEILING_CORE_SLOC)
    set(_core_status "PASS (<= ${CEILING_CORE_SLOC})")
else()
    set(_core_status "FAIL (> ${CEILING_CORE_SLOC})")
endif()

# 3. Output results and dual-track ledger
message(STATUS "================================================================================")
message(STATUS "  CineRTX AuditSloc: Fixed-Set Dual-Track Source Code Accounting")
message(STATUS "================================================================================")
message(STATUS "  Canonical Files Scanned: ${_file_count} files (include, src, tests, cmake)")
message(STATUS "  Total Physical Lines:    ${_total_physical}")
message(STATUS "  Blank Lines:             ${_total_blank}")
message(STATUS "  Semantic Comments:       ${_total_comment} (Protected contracts & SPDX headers)")
message(STATUS "  Pure SLOC (Full Repo):   ${_total_sloc} (Locked 749fe0d baseline: 31,064)")
message(STATUS "--------------------------------------------------------------------------------")
message(STATUS "  Per-Domain Pure SLOC Breakdown (Ruling 058):")
message(STATUS "    Core Engine (include + src/core + src/io + src/cli): ${_core_sloc} (${_core_files} files) -> ${_core_status}")
message(STATUS "    GUI Layer   (src/gui):                               ${_gui_sloc} (${_gui_files} files)")
message(STATUS "    NVIDIA Shim (src/shim):                              ${_shim_sloc} (${_shim_files} files)")
message(STATUS "    Unit Tests  (tests):                                 ${_tests_sloc} (${_tests_files} files)")
message(STATUS "    Build/Audit (cmake + CMakeLists.txt):                ${_build_sloc} (${_build_files} files)")
message(STATUS "================================================================================")

if(DEFINED CEILING_SLOC AND _total_sloc GREATER CEILING_SLOC)
    message(FATAL_ERROR "AuditSloc FAILED: Full-repo Pure SLOC (${_total_sloc}) exceeds ceiling (${CEILING_SLOC})")
endif()

if(_core_sloc GREATER CEILING_CORE_SLOC)
    message(FATAL_ERROR
        "[AuditSloc] FAIL: Core Engine Pure SLOC (${_core_sloc}) exceeds hard ceiling (${CEILING_CORE_SLOC}).")
endif()
