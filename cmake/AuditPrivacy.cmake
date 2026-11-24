# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
# Copyright (C) 2026 RTX Transcode Engine contributors
#
# Gate P12-Privacy (Generic Heuristic Repository Privacy & Redaction Audit — Ruling 085):
#   Usage:
#     cmake -P cmake/AuditPrivacy.cmake             # Strict audit mode (FATAL_ERROR on any hit)
#     cmake -DFIX=ON -P cmake/AuditPrivacy.cmake    # In-place normalize legacy tags & leaks, then re-audit

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(DEFINED ROOT AND NOT "${ROOT}" STREQUAL "")
    get_filename_component(REPO_ROOT "${ROOT}" ABSOLUTE)
endif()

if(NOT DEFINED FIX)
    set(FIX FALSE)
endif()

# Generic structural patterns only — zero author-specific or machine-specific fragments.
# Note: CMake's built-in KWSys regex engine does not support {m,n} quantifiers, so fixed
# minimum lengths for bare resource IDs and API tokens are expressed with explicit repeats.
set(_ID_CHAR "[A-Za-z0-9_-]")
set(_ID_5 "${_ID_CHAR}${_ID_CHAR}${_ID_CHAR}${_ID_CHAR}${_ID_CHAR}")
set(_ID_25 "${_ID_5}${_ID_5}${_ID_5}${_ID_5}${_ID_5}")

set(_ALNUM_CHAR "[A-Za-z0-9]")
set(_ALNUM_5 "${_ALNUM_CHAR}${_ALNUM_CHAR}${_ALNUM_CHAR}${_ALNUM_CHAR}${_ALNUM_CHAR}")
set(_ALNUM_20 "${_ALNUM_5}${_ALNUM_5}${_ALNUM_5}${_ALNUM_5}")

set(_TOK_CHAR "[A-Za-z0-9_]")
set(_TOK_5 "${_TOK_CHAR}${_TOK_CHAR}${_TOK_CHAR}${_TOK_CHAR}${_TOK_CHAR}")
set(_TOK_20 "${_TOK_5}${_TOK_5}${_TOK_5}${_TOK_5}")

set(_DRIVE_PREFIX "[A-Za-z]:[/\\\\]+")
set(_WIN_USER_RX "${_DRIVE_PREFIX}Users[/\\\\]+[A-Za-z0-9._-]+")
set(_POSIX_HOME_RX "(^|[^A-Za-z0-9_.-])/(Users|home)/[A-Za-z0-9._-]+")
set(_NON_SYS_DRIVE_RX "[C-Zc-z]:[/\\\\]+(PUBLIC|Public|work|Work|workspace|Projects|projects)([/\\\\]+[A-Za-z0-9._-]+|[^A-Za-z0-9_]|$)")
set(_CLOUD_DRIVE_RX "(My|Google)[ \t]+Drive")
set(_CLOUD_URL_RX "https?://(drive|docs)\\.google\\.com/[^ \t\r\n)\\]\"'>]*")
set(_CLOUD_BARE_ID_RX "(ID|id)[：:][ \t`]*1${_ID_25}[A-Za-z0-9_-]*")
set(_FILE_URI_RX "file:///[^ \t\r\n)\\]\"'>]*")
set(_LAN_IP_RX "(^|[^0-9.])(192\\.168\\.[0-9]+\\.[0-9]+|10\\.[0-9]+\\.[0-9]+\\.[0-9]+|172\\.(1[6-9]|2[0-9]|3[01])\\.[0-9]+\\.[0-9]+)([^0-9.]|$)")
set(_SSH_TARGET_RX "(^|[^A-Za-z0-9_.-])[A-Za-z0-9._-]+@([0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+|localhost|[A-Za-z0-9-]+\\.local)")
set(_EMAIL_RX "[A-Za-z0-9._+-]+@(gmail|qq|163|126|outlook|hotmail|icloud)\\.com")
set(_SECRET_RX "(ghp_${_ALNUM_20}[A-Za-z0-9]*|github_pat_${_TOK_20}[A-Za-z0-9_]*|AIza${_ID_25}${_ID_5}[A-Za-z0-9_-]*|-----BEGIN [A-Z ]*PRIVATE KEY-----)")
set(_LEGACY_ANGLE_TAG_RX "<(userHome|repoRoot|tempDir|testHost|hostPublicDir|drive|public_dir|externalBackupDir|maintainer@example\\.com)>")

# Collect canonical repository files (zero blind spots)
file(GLOB_RECURSE _PRIV_FILES
    "${REPO_ROOT}/include/*.h"
    "${REPO_ROOT}/include/*.hpp"
    "${REPO_ROOT}/src/*.h"
    "${REPO_ROOT}/src/*.hpp"
    "${REPO_ROOT}/src/*.cpp"
    "${REPO_ROOT}/src/*.cu"
    "${REPO_ROOT}/src/*.hlsl"
    "${REPO_ROOT}/tests/*.h"
    "${REPO_ROOT}/tests/*.hpp"
    "${REPO_ROOT}/tests/*.cpp"
    "${REPO_ROOT}/cmake/*.cmake"
    "${REPO_ROOT}/docs/*.md"
    "${REPO_ROOT}/webui/*.html"
    "${REPO_ROOT}/webui/*.js"
    "${REPO_ROOT}/webui/*.css"
    "${REPO_ROOT}/webui/*.py"
    "${REPO_ROOT}/webui/*.md"
    "${REPO_ROOT}/webui/state.json"
    "${REPO_ROOT}/.github/workflows/*.yml"
)

foreach(_root_doc
    "${REPO_ROOT}/CMakeLists.txt"
    "${REPO_ROOT}/CMakePresets.json"
    "${REPO_ROOT}/README.md"
    "${REPO_ROOT}/README_zh.md"
    "${REPO_ROOT}/SECURITY.md"
    "${REPO_ROOT}/DECISIONS.md"
    "${REPO_ROOT}/ISSUES.md"
    "${REPO_ROOT}/LICENSE"
    "${REPO_ROOT}/LICENSE-MIT"
    "${REPO_ROOT}/LICENSE-APACHE"
    "${REPO_ROOT}/COPYING.EXCEPTION"
    "${REPO_ROOT}/build.bat"
    "${REPO_ROOT}/.gitignore"
    "${REPO_ROOT}/.gitmodules")
    if(EXISTS "${_root_doc}")
        list(APPEND _PRIV_FILES "${_root_doc}")
    endif()
endforeach()

list(REMOVE_DUPLICATES _PRIV_FILES)
list(SORT _PRIV_FILES)

if(FIX)
    set(_FIXED_COUNT 0)
    foreach(_f IN LISTS _PRIV_FILES)
        file(RELATIVE_PATH _rel "${REPO_ROOT}" "${_f}")
        if(_rel STREQUAL "cmake/AuditPrivacy.cmake")
            continue()
        endif()
        file(READ "${_f}" _txt)
        set(_orig "${_txt}")

        # 1. Normalize legacy angle-bracket tags to canonical [REDACTED_*] standard
        string(REGEX REPLACE "<(userHome|repoRoot|hostPublicDir|drive|public_dir|externalBackupDir)>" "[REDACTED_PATH]" _txt "${_txt}")
        string(REGEX REPLACE "<tempDir>" "[REDACTED_TEMP_DIR]" _txt "${_txt}")
        string(REGEX REPLACE "<testHost>" "[REDACTED_HOST]" _txt "${_txt}")
        string(REGEX REPLACE "<maintainer@example\\.com>" "[REDACTED_EMAIL]" _txt "${_txt}")

        # 2. Sanitize paths, URIs, cloud drive names, bare IDs, SSH targets, LAN IPs, and emails
        string(REGEX REPLACE "file:///[A-Za-z]:/Users/[^/)\r\n]+/[^/)\r\n]+/CineRTX/" "../../" _txt "${_txt}")
        string(REGEX REPLACE "file:///[^ \t\r\n)\\]\"'>]*" "[REDACTED_PATH]" _txt "${_txt}")
        string(REGEX REPLACE "[A-Za-z]:[/\\\\]+Users[/\\\\]+[^]/\\\\\r\n`\"')|]+([/\\\\]+[^]/\\\\\r\n`\"')|]+)*" "[REDACTED_PATH]" _txt "${_txt}")
        string(REGEX REPLACE "(^|[^A-Za-z0-9_.-])/(Users|home)/[A-Za-z0-9._-]+(/[^] \t\r\n`\"')|]+)*" "\\1[REDACTED_PATH]" _txt "${_txt}")
        string(REGEX REPLACE "[C-Zc-z]:[/\\\\]+(PUBLIC|Public|work|Work|workspace|Projects|projects)([/\\\\]+[^]/\\\\\r\n`\"')|]+)*" "[REDACTED_PATH]" _txt "${_txt}")
        string(REGEX REPLACE "(My|Google)[ \t]+Drive" "[REDACTED_CLOUD_DRIVE]" _txt "${_txt}")
        string(REGEX REPLACE "https?://(drive|docs)\\.google\\.com/[^ \t\r\n)\\]\"'>]*" "[REDACTED_ID]" _txt "${_txt}")
        string(REGEX REPLACE "(ID|id)[：:][ \t`]*1${_ID_25}[A-Za-z0-9_-]*`?" "\\1：[REDACTED_ID]" _txt "${_txt}")
        string(REGEX REPLACE "(^|[^A-Za-z0-9_.-])[A-Za-z0-9._-]+@([0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+|localhost|[A-Za-z0-9-]+\\.local)" "\\1[REDACTED_HOST]" _txt "${_txt}")
        string(REGEX REPLACE "[A-Za-z0-9._+-]+@(gmail|qq|163|126|outlook|hotmail|icloud)\\.com" "[REDACTED_EMAIL]" _txt "${_txt}")
        string(REGEX REPLACE "192\\.168\\.[0-9]+\\.[0-9]+" "[REDACTED_IP]" _txt "${_txt}")

        if(NOT _txt STREQUAL _orig)
            file(WRITE "${_f}" "${_txt}")
            math(EXPR _FIXED_COUNT "${_FIXED_COUNT} + 1")
        endif()
    endforeach()
    message(STATUS "[AuditPrivacy] Sanitized ${_FIXED_COUNT} file(s) to [REDACTED_*] standard.")
endif()

set(_VIOLATIONS "")
set(_SCANNED 0)

foreach(_f IN LISTS _PRIV_FILES)
    file(RELATIVE_PATH _rel "${REPO_ROOT}" "${_f}")
    if(_rel STREQUAL "cmake/AuditPrivacy.cmake")
        continue()
    endif()
    math(EXPR _SCANNED "${_SCANNED} + 1")

    file(READ "${_f}" _content)
    string(REGEX REPLACE "\r\n" "\n" _content "${_content}")
    string(REGEX REPLACE "\r" "\n" _content "${_content}")
    string(REPLACE "\\" "\\\\" _content "${_content}")
    string(REPLACE ";" "\\;" _content "${_content}")
    string(REPLACE "\n" ";" _lines "${_content}")

    set(_lineno 0)
    foreach(_raw IN LISTS _lines)
        math(EXPR _lineno "${_lineno} + 1")
        string(REPLACE "\\;" ";" _line "${_raw}")
        string(REPLACE "\\\\" "\\" _line "${_line}")

        if(_line MATCHES "${_WIN_USER_RX}")
            list(APPEND _VIOLATIONS "${_rel}:${_lineno}: Windows user path leak")
        endif()
        if(_line MATCHES "${_POSIX_HOME_RX}")
            list(APPEND _VIOLATIONS "${_rel}:${_lineno}: POSIX user home path leak")
        endif()
        if(_line MATCHES "${_NON_SYS_DRIVE_RX}")
            list(APPEND _VIOLATIONS "${_rel}:${_lineno}: Host non-system drive path leak")
        endif()
        if(_line MATCHES "${_CLOUD_DRIVE_RX}")
            list(APPEND _VIOLATIONS "${_rel}:${_lineno}: Cloud drive name leak")
        endif()
        if(_line MATCHES "${_CLOUD_URL_RX}")
            list(APPEND _VIOLATIONS "${_rel}:${_lineno}: Cloud share URL leak")
        endif()
        if(_line MATCHES "${_CLOUD_BARE_ID_RX}")
            list(APPEND _VIOLATIONS "${_rel}:${_lineno}: Bare cloud resource ID leak")
        endif()
        if(_line MATCHES "${_FILE_URI_RX}")
            list(APPEND _VIOLATIONS "${_rel}:${_lineno}: Local file:// URI leak")
        endif()
        if(_line MATCHES "${_SSH_TARGET_RX}")
            list(APPEND _VIOLATIONS "${_rel}:${_lineno}: SSH host/user target leak")
        endif()
        if(_line MATCHES "${_EMAIL_RX}")
            list(APPEND _VIOLATIONS "${_rel}:${_lineno}: Personal email address leak")
        endif()
        if(_line MATCHES "${_SECRET_RX}")
            list(APPEND _VIOLATIONS "${_rel}:${_lineno}: Potential secret/token/key leak")
        endif()
        if(_line MATCHES "${_LEGACY_ANGLE_TAG_RX}")
            list(APPEND _VIOLATIONS "${_rel}:${_lineno}: Non-standard angle-bracket redaction tag (use [REDACTED_*])")
        endif()
        if(_line MATCHES "${_LAN_IP_RX}")
            string(REPLACE "10.13.3.9" "" _ip_check "${_line}")
            string(REGEX REPLACE "10\\.0\\.[0-9]+\\.[0-9]+" "" _ip_check "${_ip_check}")
            if(_ip_check MATCHES "${_LAN_IP_RX}")
                list(APPEND _VIOLATIONS "${_rel}:${_lineno}: RFC1918 private LAN IP leak")
            endif()
        endif()
    endforeach()
endforeach()

list(LENGTH _VIOLATIONS _vcount)
if(_vcount GREATER 0)
    foreach(_v IN LISTS _VIOLATIONS)
        message(WARNING "[AuditPrivacy] ${_v}")
    endforeach()
    message(FATAL_ERROR "[AuditPrivacy] FAIL: ${_vcount} violation(s) across ${_SCANNED} file(s).")
endif()

message(STATUS "[AuditPrivacy] PASS: 0 violations across ${_SCANNED} file(s) ([REDACTED_*] compliant).")
