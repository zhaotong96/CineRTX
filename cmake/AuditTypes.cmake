# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
# Copyright (C) 2026 RTX Transcode Engine contributors
#
# Gate R-5: enforce @rtx-type-justification on every named class/struct under
# include/ and src/ (excluding forward declarations, enum class, and FnDeleter).
# Required block within 8 lines above the definition:
#   // @rtx-type-justification
#   // Category: <one of the six whitelisted categories>
#   // Why-Std-Fails: ...
#   // Invariant: ...

set(RTX_AUDIT_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")

set(RTX_TYPE_CATEGORIES
    "C-ABI"
    "Hardware-Layout"
    "Polymorphic-Boundary"
    "NonTrivial-StateMachine"
    "Concurrency-Invariant"
    "Domain-Model")

set(RTX_TYPE_VIOLATIONS "")
set(RTX_TYPE_COUNT 0)

file(GLOB_RECURSE _rtx_audit_files
    "${RTX_AUDIT_ROOT}/include/*.h"
    "${RTX_AUDIT_ROOT}/include/*.hpp"
    "${RTX_AUDIT_ROOT}/src/*.h"
    "${RTX_AUDIT_ROOT}/src/*.hpp"
    "${RTX_AUDIT_ROOT}/src/*.cpp")

foreach(_file IN LISTS _rtx_audit_files)
    file(READ "${_file}" _content)
    string(REGEX REPLACE "\r\n" "\n" _content "${_content}")
    string(REGEX REPLACE "\r" "\n" _content "${_content}")
    string(REPLACE "\\" "\\\\" _content "${_content}")
    string(REPLACE ";" "\\;" _content "${_content}")
    string(REPLACE "\n" ";" _lines "${_content}")
    list(LENGTH _lines _line_count)
    set(_justified FALSE)
    set(_category "")
    set(_has_why FALSE)
    set(_has_inv FALSE)
    set(_marker_at 0)
    if(_line_count EQUAL 0)
        continue()
    endif()
    math(EXPR _last "${_line_count} - 1")
    foreach(_i RANGE 0 ${_last})
        list(GET _lines ${_i} _raw_line)
        string(REPLACE "\\;" ";" _line "${_raw_line}")
        string(REPLACE "\\\\" "\\" _line "${_line}")
        if(_line MATCHES "@rtx-type-justification")
            if(_justified)
                math(EXPR _prev_line "${_marker_at} + 1")
                list(APPEND RTX_TYPE_VIOLATIONS
                    "${_file}:${_prev_line}: orphaned @rtx-type-justification block with no following type")
            endif()
            set(_justified TRUE)
            set(_category "")
            set(_has_why FALSE)
            set(_has_inv FALSE)
            set(_marker_at ${_i})
            continue()
        endif()
        if(_justified)
            if(_line MATCHES "^[ \t]*//[ \t]*Category:[ \t]*([A-Za-z-]+)")
                set(_category "${CMAKE_MATCH_1}")
            elseif(_line MATCHES "^[ \t]*//[ \t]*Why-Std-Fails:[ \t]*[^ \t].+")
                set(_has_why TRUE)
            elseif(_line MATCHES "^[ \t]*//[ \t]*Invariant:[ \t]*[^ \t].+")
                set(_has_inv TRUE)
            endif()
        endif()

        set(_is_def FALSE)
        set(_name "")
        set(_clean_line "${_line}")
        string(REGEX REPLACE "alignas\\([^)]+\\)[ \t]*" "" _clean_line "${_clean_line}")
        if(_clean_line MATCHES "^[ \t]*((const|static)[ \t]+)?(class|struct)[ \t]+([A-Za-z_][A-Za-z0-9_:]*)[ \t]*((final[ \t]*)?(:[ \t]*[^;{]+)?\\{.*|(final[ \t]*)?(:[ \t]*[^;{]+)?)$")
            set(_is_def TRUE)
            set(_name "${CMAKE_MATCH_4}")
        endif()

        if(_is_def)
            if(_name STREQUAL "FnDeleter")
                set(_justified FALSE)
                continue()
            endif()
            if(_justified AND _category IN_LIST RTX_TYPE_CATEGORIES AND _has_why AND _has_inv)
                math(EXPR RTX_TYPE_COUNT "${RTX_TYPE_COUNT} + 1")
            else()
                math(EXPR _def_line "${_i} + 1")
                list(APPEND RTX_TYPE_VIOLATIONS
                    "${_file}:${_def_line}: type '${_name}' lacks valid @rtx-type-justification (cat='${_category}', why=${_has_why}, inv=${_has_inv})")
            endif()
            set(_justified FALSE)
        elseif(_justified)
            math(EXPR _age "${_i} - ${_marker_at}")
            if(_age GREATER 8)
                math(EXPR _marker_line "${_marker_at} + 1")
                list(APPEND RTX_TYPE_VIOLATIONS
                    "${_file}:${_marker_line}: orphaned @rtx-type-justification block expired without matching class/struct")
                set(_justified FALSE)
            endif()
        endif()
    endforeach()
    if(_justified)
        math(EXPR _prev_line "${_marker_at} + 1")
        list(APPEND RTX_TYPE_VIOLATIONS
            "${_file}:${_prev_line}: orphaned @rtx-type-justification block at end of file")
    endif()
endforeach()

list(LENGTH RTX_TYPE_VIOLATIONS _violation_count)
if(_violation_count GREATER 0)
    foreach(_v IN LISTS RTX_TYPE_VIOLATIONS)
        message(WARNING "[AuditTypes] ${_v}")
    endforeach()
    message(FATAL_ERROR
        "[AuditTypes] ${_violation_count} named type(s) lack a valid @rtx-type-justification "
        "block (scanned ${RTX_TYPE_COUNT} justified types).  See docs/phase11/25 §R-5.")
endif()

message(STATUS "[AuditTypes] OK: ${RTX_TYPE_COUNT} named types, all justified (Std-First law)")
