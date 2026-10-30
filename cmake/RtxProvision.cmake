# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
# Copyright (C) 2026 RTX Transcode Engine contributors
#
# Shared download/verify/extract helper for the bootstrap layers.
#
# Archives are pinned by SHA-256 in the callers and cached under
# <root>/download/, so a re-run validates the archive and reuses it without
# touching the network.  curl is tried first because it honours
# HTTPS_PROXY/HTTP_PROXY; file(DOWNLOAD) cannot, and is only a fallback.

include_guard(GLOBAL)

# rtx_fetch_archive(<url> <sha256> <archive_path> <out_var>)
#
# Ensures <archive_path> exists and matches <sha256>, downloading it only when
# it is absent or invalid.  curl runs first because it honours
# HTTPS_PROXY/HTTP_PROXY; file(DOWNLOAD) cannot.
function(rtx_fetch_archive url expected_sha archive out_var)
    set(${out_var} FALSE PARENT_SCOPE)

    if(EXISTS "${archive}")
        file(SHA256 "${archive}" _archive_sha)
        if(_archive_sha STREQUAL "${expected_sha}")
            set(${out_var} TRUE PARENT_SCOPE)
            return()
        endif()
        message(STATUS "[CineRTX-Bootstrap] Discarding ${archive} (SHA256 mismatch)")
        file(REMOVE "${archive}")
    endif()

    get_filename_component(_archive_dir "${archive}" DIRECTORY)
    file(MAKE_DIRECTORY "${_archive_dir}")
    message(STATUS "[CineRTX-Bootstrap] Downloading ${url}")

    find_program(_rtx_curl NAMES curl.exe curl)
    if(_rtx_curl)
        execute_process(
            COMMAND "${_rtx_curl}" -fL --retry 3 --retry-delay 2
                    --connect-timeout 30 -o "${archive}" "${url}"
            RESULT_VARIABLE _curl_result
            OUTPUT_QUIET
            ERROR_VARIABLE _curl_error)
        if(NOT _curl_result EQUAL 0)
            message(STATUS "[CineRTX-Bootstrap] curl failed: ${_curl_error}")
            file(REMOVE "${archive}")
        endif()
    endif()

    if(NOT EXISTS "${archive}")
        file(DOWNLOAD "${url}" "${archive}"
             SHOW_PROGRESS
             STATUS _dl_status)
        list(GET _dl_status 0 _dl_code)
        if(NOT _dl_code EQUAL 0)
            list(GET _dl_status 1 _dl_msg)
            file(REMOVE "${archive}")
            message(STATUS "[CineRTX-Bootstrap] Download failed: ${_dl_msg}")
            return()
        endif()
    endif()

    file(SHA256 "${archive}" _archive_sha)
    if(NOT _archive_sha STREQUAL "${expected_sha}")
        file(REMOVE "${archive}")
        message(STATUS "[CineRTX-Bootstrap] SHA256 mismatch; archive discarded")
        return()
    endif()
    set(${out_var} TRUE PARENT_SCOPE)
endfunction()

# rtx_provision_tool(<name> <url> <sha256> <exe_name> <root_dir> <out_var>)
#
# Extracts the archive into <root_dir>/tools/<name>/ and returns the first
# matching <exe_name>.  On failure <out_var> is set to "" and the caller
# decides whether that is fatal.
function(rtx_provision_tool tool_name url expected_sha exe_name root_dir out_var)
    set(_tool_dir "${root_dir}/tools/${tool_name}")
    set(_archive_dir "${root_dir}/download")
    set(_archive "${_archive_dir}/${tool_name}.zip")

    file(GLOB_RECURSE _existing "${_tool_dir}/${exe_name}")
    if(_existing)
        list(GET _existing 0 _exe)
        set(${out_var} "${_exe}" PARENT_SCOPE)
        return()
    endif()

    rtx_fetch_archive("${url}" "${expected_sha}" "${_archive}" _archive_ok)
    if(NOT _archive_ok)
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()

    file(MAKE_DIRECTORY "${_tool_dir}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E tar xf "${_archive}"
        WORKING_DIRECTORY "${_tool_dir}"
        RESULT_VARIABLE _extract_result)
    if(NOT _extract_result EQUAL 0)
        message(STATUS "[CineRTX-Bootstrap] Failed to extract ${_archive}")
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()

    file(GLOB_RECURSE _found "${_tool_dir}/${exe_name}")
    if(_found)
        list(GET _found 0 _exe)
        set(${out_var} "${_exe}" PARENT_SCOPE)
    else()
        set(${out_var} "" PARENT_SCOPE)
    endif()
endfunction()
