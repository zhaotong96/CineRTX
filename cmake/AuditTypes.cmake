# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
# Copyright (C) 2026 RTX Transcode Engine contributors
#
# Gate 12.2-1 (Wave 12.2 Hardened Std-First Type Audit):
# Enforces @rtx-type-justification on every named class/struct under include/
# and src/ (excluding forward declarations, enum class, and FnDeleter).
#
# Required block within 6 lines above the definition:
#   // @rtx-type-justification
#   // Category: <C-ABI|Hardware-Layout|NonTrivial-StateMachine|Std-Customization|Domain-Model>
#   // Why-Std-Fails: <substantive, type-specific explanation>
#   // Invariant: <substantive, type-specific invariant>
#
# Hard cap: total justified named types across include/ and src/ MUST be <= 35.

set(RTX_AUDIT_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
if(DEFINED ROOT AND NOT "${ROOT}" STREQUAL "")
    get_filename_component(RTX_AUDIT_ROOT "${ROOT}" ABSOLUTE)
endif()

set(RTX_TYPE_CATEGORIES
    "C-ABI"
    "Hardware-Layout"
    "NonTrivial-StateMachine"
    "Std-Customization"
    "Domain-Model")

set(RTX_TYPE_MAX_COUNT 35)
set(RTX_TYPE_VIOLATIONS "")
set(RTX_TYPE_COUNT 0)
set(_RTX_SEEN_WHY_TEXTS "")
set(_RTX_SEEN_INV_TEXTS "")

file(GLOB_RECURSE _rtx_audit_files
    "${RTX_AUDIT_ROOT}/include/*.h"
    "${RTX_AUDIT_ROOT}/include/*.hpp"
    "${RTX_AUDIT_ROOT}/src/*.h"
    "${RTX_AUDIT_ROOT}/src/*.hpp"
    "${RTX_AUDIT_ROOT}/src/*.cpp")
list(SORT _rtx_audit_files)

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
    set(_why_text "")
    set(_inv_text "")
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
            set(_why_text "")
            set(_inv_text "")
            set(_has_why FALSE)
            set(_has_inv FALSE)
            set(_marker_at ${_i})
            continue()
        endif()

        if(_justified)
            if(_line MATCHES "^[ \t]*//[ \t]*Category:[ \t]*([A-Za-z-]+)")
                set(_category "${CMAKE_MATCH_1}")
                continue()
            elseif(_line MATCHES "^[ \t]*//[ \t]*Why-Std-Fails:[ \t]*(.+)$")
                string(STRIP "${CMAKE_MATCH_1}" _why_text)
                string(LENGTH "${_why_text}" _why_len)
                if(_why_len GREATER_EQUAL 12 AND NOT _why_text MATCHES "^(TODO|TBD|N/A|none|placeholder|\\.\\.\\.)")
                    set(_has_why TRUE)
                endif()
                continue()
            elseif(_line MATCHES "^[ \t]*//[ \t]*Invariant:[ \t]*(.+)$")
                string(STRIP "${CMAKE_MATCH_1}" _inv_text)
                string(LENGTH "${_inv_text}" _inv_len)
                if(_inv_len GREATER_EQUAL 10 AND NOT _inv_text MATCHES "^(TODO|TBD|N/A|none|placeholder|\\.\\.\\.)")
                    set(_has_inv TRUE)
                endif()
                continue()
            endif()
        endif()

        # Lexical pre-stripping: remove inline comments, attributes, declspecs, alignas, and final
        set(_clean_line "${_line}")
        string(REGEX REPLACE "//.*$" "" _clean_line "${_clean_line}")
        string(REGEX REPLACE "/\\*[^*]*\\*/" " " _clean_line "${_clean_line}")
        string(REGEX REPLACE "alignas\\([^)]*\\)" "" _clean_line "${_clean_line}")
        string(REGEX REPLACE "\\[\\[[^]]*\\]\\]" "" _clean_line "${_clean_line}")
        string(REGEX REPLACE "__declspec\\([^)]*\\)" "" _clean_line "${_clean_line}")
        string(REGEX REPLACE "(^|[ \t])final([ \t]+|:[ \t]*|\\{|$)" "\\1\\2" _clean_line "${_clean_line}")
        string(REGEX REPLACE "[ \t]+" " " _clean_line "${_clean_line}")
        string(STRIP "${_clean_line}" _clean_line)

        set(_is_def FALSE)
        set(_name "")
        if(_clean_line MATCHES "^((const|static) )?(class|struct) ([A-Za-z_][A-Za-z0-9_:]*)(( : [^;{]+)? ?\\{.*|( : [^;{]+)?)$")
            set(_is_def TRUE)
            set(_name "${CMAKE_MATCH_4}")
        endif()

        if(_is_def)
            if(_name STREQUAL "FnDeleter")
                set(_justified FALSE)
                continue()
            endif()
            math(EXPR _def_line "${_i} + 1")
            if(_justified AND _category IN_LIST RTX_TYPE_CATEGORIES AND _has_why AND _has_inv)
                # Reject copy-pasted boilerplate Why-Std-Fails / Invariant across distinct types
                if(_why_text IN_LIST _RTX_SEEN_WHY_TEXTS)
                    list(APPEND RTX_TYPE_VIOLATIONS
                        "${_file}:${_def_line}: type '${_name}' reuses duplicate boilerplate Why-Std-Fails text")
                elseif(_inv_text IN_LIST _RTX_SEEN_INV_TEXTS)
                    list(APPEND RTX_TYPE_VIOLATIONS
                        "${_file}:${_def_line}: type '${_name}' reuses duplicate boilerplate Invariant text")
                else()
                    list(APPEND _RTX_SEEN_WHY_TEXTS "${_why_text}")
                    list(APPEND _RTX_SEEN_INV_TEXTS "${_inv_text}")
                    math(EXPR RTX_TYPE_COUNT "${RTX_TYPE_COUNT} + 1")
                endif()
            else()
                list(APPEND RTX_TYPE_VIOLATIONS
                    "${_file}:${_def_line}: type '${_name}' lacks valid @rtx-type-justification (cat='${_category}', why=${_has_why}, inv=${_has_inv})")
            endif()
            set(_justified FALSE)
        elseif(_justified)
            math(EXPR _age "${_i} - ${_marker_at}")
            if(_age GREATER 6)
                math(EXPR _marker_line "${_marker_at} + 1")
                list(APPEND RTX_TYPE_VIOLATIONS
                    "${_file}:${_marker_line}: orphaned @rtx-type-justification block expired (>6 lines) without matching class/struct")
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

if(NOT DEFINED RTX_AUDIT_TYPES_STRICT)
    set(RTX_AUDIT_TYPES_STRICT ON)
endif()

list(LENGTH RTX_TYPE_VIOLATIONS _violation_count)
if(_violation_count GREATER 0)
    foreach(_v IN LISTS RTX_TYPE_VIOLATIONS)
        message(WARNING "[AuditTypes] ${_v}")
    endforeach()
    if(RTX_AUDIT_TYPES_STRICT)
        message(FATAL_ERROR
            "[AuditTypes] ${_violation_count} violation(s) detected (valid justified types: ${RTX_TYPE_COUNT}/${RTX_TYPE_MAX_COUNT}). See docs/phase12/04.")
    else()
        message(WARNING
            "[AuditTypes] ${_violation_count} Wave 12.2 violation(s) pending (valid=${RTX_TYPE_COUNT}/${RTX_TYPE_MAX_COUNT}); run 'cmake -P cmake/AuditTypes.cmake' for hard-gate enforcement.")
    endif()
endif()

if(RTX_TYPE_COUNT GREATER RTX_TYPE_MAX_COUNT)
    if(RTX_AUDIT_TYPES_STRICT)
        message(FATAL_ERROR
            "[AuditTypes] HARD CAP EXCEEDED: ${RTX_TYPE_COUNT} named types exceed Wave 12.2 limit of ${RTX_TYPE_MAX_COUNT}. See docs/phase12/04.")
    else()
        message(WARNING
            "[AuditTypes] Wave 12.2 type count ${RTX_TYPE_COUNT} exceeds cap ${RTX_TYPE_MAX_COUNT} (Wave 12.2-B pending).")
    endif()
endif()

message(STATUS "[AuditTypes] OK: ${RTX_TYPE_COUNT}/${RTX_TYPE_MAX_COUNT} named types, all uniquely justified (Wave 12.2 Std-First law)")
