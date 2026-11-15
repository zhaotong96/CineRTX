# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
# Copyright (C) 2026 RTX Transcode Engine contributors
#
# Native CMake portable release packager (replaces tools/package.ps1 and tools/package_release.py).

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(NOT DEFINED OUTPUT_DIR OR "${OUTPUT_DIR}" STREQUAL "")
    set(OUTPUT_DIR "${REPO_ROOT}/.vs/build/dist")
endif()
get_filename_component(OUTPUT_DIR "${OUTPUT_DIR}" ABSOLUTE BASE_DIR "${CMAKE_BINARY_DIR}")

if(NOT DEFINED BUILD_DIR OR "${BUILD_DIR}" STREQUAL "")
    set(BUILD_DIR "${REPO_ROOT}/.vs/build/cmake/windows-msvc-ninja")
endif()
get_filename_component(BUILD_DIR "${BUILD_DIR}" ABSOLUTE BASE_DIR "${CMAKE_BINARY_DIR}")

if(NOT DEFINED VERSION OR "${VERSION}" STREQUAL "")
    file(READ "${REPO_ROOT}/CMakeLists.txt" _cmake_txt)
    if(_cmake_txt MATCHES "project[ \t\r\n]*\\([ \t\r\n]*CineRTX[ \t\r\n]+VERSION[ \t\r\n]+([0-9]+\\.[0-9]+\\.[0-9]+)")
        set(VERSION "${CMAKE_MATCH_1}")
    else()
        set(VERSION "0.1.0")
    endif()
endif()

include("${CMAKE_CURRENT_LIST_DIR}/AuditSpdx.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/AuditImports.cmake")

if(CMAKE_HOST_WIN32 OR EXISTS "${BUILD_DIR}/CineRTX.exe")
    set(PLATFORM "windows-x64")
    set(EXE_NAME "CineRTX.exe")
else()
    set(PLATFORM "linux-x64")
    set(EXE_NAME "CineRTX")
endif()

set(PKG_NAME "CineRTX-v${VERSION}-${PLATFORM}")
set(STAGE_DIR "${OUTPUT_DIR}/${PKG_NAME}")
get_filename_component(STAGE_DIR "${STAGE_DIR}" ABSOLUTE)

file(REMOVE_RECURSE "${STAGE_DIR}")
file(MAKE_DIRECTORY "${STAGE_DIR}" "${OUTPUT_DIR}")

file(COPY "${BUILD_DIR}/${EXE_NAME}" DESTINATION "${STAGE_DIR}")
if(EXISTS "${BUILD_DIR}/rtx_ngx_bridge.dll")
    file(COPY "${BUILD_DIR}/rtx_ngx_bridge.dll" DESTINATION "${STAGE_DIR}")
endif()
if(EXISTS "${BUILD_DIR}/nvngx_vsr.dll")
    file(COPY "${BUILD_DIR}/nvngx_vsr.dll" DESTINATION "${STAGE_DIR}")
endif()
if(EXISTS "${BUILD_DIR}/nvngx_truehdr.dll")
    file(COPY "${BUILD_DIR}/nvngx_truehdr.dll" DESTINATION "${STAGE_DIR}")
endif()
if(EXISTS "${BUILD_DIR}/plugins")
    file(COPY "${BUILD_DIR}/plugins" DESTINATION "${STAGE_DIR}")
endif()

foreach(_doc "README.md" "README_zh.md" "LICENSE" "LICENSE-MIT" "LICENSE-APACHE" "COPYING.EXCEPTION")
    if(EXISTS "${REPO_ROOT}/${_doc}")
        file(COPY "${REPO_ROOT}/${_doc}" DESTINATION "${STAGE_DIR}")
    endif()
endforeach()

if(EXISTS "${REPO_ROOT}/include")
    file(COPY "${REPO_ROOT}/include" DESTINATION "${STAGE_DIR}")
endif()

file(GLOB_RECURSE _staged_files "${STAGE_DIR}/*")
list(SORT _staged_files)

set(_sums_text "")
set(_manifest_items "")
foreach(_f IN LISTS _staged_files)
    if(NOT IS_DIRECTORY "${_f}")
        file(RELATIVE_PATH _rel "${STAGE_DIR}" "${_f}")
        file(SIZE "${_f}" _fsize)
        file(SHA256 "${_f}" _fsha)
        string(APPEND _sums_text "${_fsha}  ${_rel}
")
        if(NOT "${_manifest_items}" STREQUAL "")
            string(APPEND _manifest_items ",
")
        endif()
        string(APPEND _manifest_items "    {\"path\": \"${_rel}\", \"size\": ${_fsize}, \"sha256\": \"${_fsha}\"}")
    endif()
endforeach()

file(WRITE "${STAGE_DIR}/SHA256SUMS.txt" "${_sums_text}")
string(TIMESTAMP _utc_now "%Y-%m-%dT%H:%M:%SZ" UTC)
set(_manifest_json "{\n  \"name\": \"${PKG_NAME}\",\n  \"version\": \"${VERSION}\",\n  \"platform\": \"${PLATFORM}\",\n  \"generated_utc\": \"${_utc_now}\",\n  \"files\": [\n${_manifest_items}\n  ]\n}\n")
file(WRITE "${STAGE_DIR}/manifest.json" "${_manifest_json}")
file(COPY_FILE "${STAGE_DIR}/manifest.json" "${OUTPUT_DIR}/${PKG_NAME}.manifest.json")

set(ZIP_PATH "${OUTPUT_DIR}/${PKG_NAME}.zip")
file(REMOVE "${ZIP_PATH}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar "cfv" "${ZIP_PATH}" --format=zip "."
    WORKING_DIRECTORY "${STAGE_DIR}"
    RESULT_VARIABLE _zip_rc
    OUTPUT_QUIET)
if(NOT _zip_rc EQUAL 0)
    message(FATAL_ERROR "[PackageRelease] Failed to create archive ${ZIP_PATH}")
endif()

file(SHA256 "${ZIP_PATH}" _zip_sha)
file(WRITE "${ZIP_PATH}.sha256" "${_zip_sha}  ${PKG_NAME}.zip
")
file(REMOVE_RECURSE "${STAGE_DIR}")

message(STATUS "[PackageRelease] package : ${ZIP_PATH}")
message(STATUS "[PackageRelease] sha256  : ${_zip_sha}")
