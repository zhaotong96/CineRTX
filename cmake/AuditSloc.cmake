# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
# Copyright (C) 2026 RTX Transcode Engine contributors
#
# Gate R-1 & Phase 11.3 Baseline Lock: Dual-track line counting
# Measures Pure SLOC (excluding blank lines and comments), Semantic Comments,
# and Total Physical Lines across the locked canonical source tree.
#
# Baseline entry watermark for Phase 11.3 (from Phase 11.2-R Final Verdict):
#   - Pure SLOC:        31,064 (Locked Commit 749fe0d canonical baseline)
#   - Semantic Comments: 4,732 (Protected semantic contracts)
#   - Phase 11.3 Goal:  -400 to -600 Pure SLOC debt reduction

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

# 2. Iterate and parse each file
set(_total_physical 0)
set(_total_blank 0)
set(_total_comment 0)
set(_total_sloc 0)

foreach(_file IN LISTS _sloc_files)
    file(READ "${_file}" _content)
    string(REGEX REPLACE "\r\n" "\n" _content "${_content}")
    string(REGEX REPLACE "\r" "\n" _content "${_content}")
    
    # Escape semicolons to safely turn string into list of lines
    string(REPLACE "\\" "\\\\" _content "${_content}")
    string(REPLACE ";" "\\;" _content "${_content}")
    string(REPLACE "\n" ";" _lines "${_content}")
    
    set(_in_block_comment FALSE)
    
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
        endif()
    endforeach()
endforeach()

# 3. Output results and ledger
message(STATUS "================================================================================")
message(STATUS "  CineRTX AuditSloc: Fixed-Set Dual-Track Source Code Accounting")
message(STATUS "================================================================================")
message(STATUS "  Canonical Files Scanned: ${_file_count} files (include, src, tests, cmake)")
message(STATUS "  Total Physical Lines:    ${_total_physical}")
message(STATUS "  Blank Lines:             ${_total_blank}")
message(STATUS "  Semantic Comments:       ${_total_comment} (Protected contracts & SPDX headers)")
message(STATUS "  Pure SLOC (Code only):   ${_total_sloc} (Locked 749fe0d baseline: 31,064)")
message(STATUS "--------------------------------------------------------------------------------")
message(STATUS "  Phase 11.3-S Locked Entry Baseline: 31,064 Pure SLOC | 4,732 Comments (Commit 749fe0d)")
message(STATUS "  Phase 11.3 Target Budget:           -400 to -600 Pure SLOC reduction")
message(STATUS "================================================================================")

if(DEFINED CEILING_SLOC AND _total_sloc GREATER CEILING_SLOC)
    message(FATAL_ERROR "AuditSloc FAILED: Pure SLOC (${_total_sloc}) exceeds ceiling (${CEILING_SLOC})")
endif()
