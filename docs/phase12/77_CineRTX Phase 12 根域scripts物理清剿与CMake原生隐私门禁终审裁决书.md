# **CineRTX Phase 12 根域 scripts 物理清剿与 CMake 原生隐私门禁终审裁决书**

| 属性 | 详细内容 |
| :---- | :---- |
| **文档编号** | `CINERTX-P12-FINAL-PURITY-077` |
| **归档路径** | `docs/phase12/77_CineRTX Phase 12 根域scripts物理清剿与CMake原生隐私门禁终审裁决书.md` |
| **审验标的** | `docs/phase12/76_CineRTX Phase 12 根域scripts违规反弹与脱敏工件二次泄密专家咨询委托书.md`（`CINERTX-P12-DELEGATION-076`）仓库根目录 `scripts/` 全目录、第 73/74/75/76 号文档族与 `cmake/` 静态门禁体系 |
| **前序依据** | `docs/phase11/38_CineRTX 现代 C++ 极限重构审计.md`（第四部分物理铲除令）、`DECISIONS.md`（`[DEC-002]` 资产物理隔离与奥卡姆剃刀铁律） |
| **签发主体** | 多媒体系统工程与 GPU 异构计算首席领域专家组（Extreme Modern C++ Purist & Architecture Board） |
| **审验日期** | 2026-11-24 |

**终审裁决**：**全票支持项目负责人（用户）全部质询，正式下达根域 `scripts/` 物理清剿令、纯原生 `cmake/AuditPrivacy.cmake` 门禁定型范本及零外置脚本发版总决议（APPROVED — PURGE `scripts/`, ENFORCE PURE CMAKE-NATIVE `AuditPrivacy.cmake`, SANITIZE DOCS & SEAL `v0.4.0`）**——首席领域专家组穿透核验确认：① 依据 `DECISIONS.md`（`[DEC-002]`）与 Phase 11.3 第 38 号审计裁决，根域 scripts/ 属于明确禁止反弹的体外脚本垃圾，立即执行 git rm \-rf scripts/ 物理铲除；② 废除 Python 外部解释器依赖，以 100% 原生 CMake 脚本 `cmake/AuditPrivacy.cmake` 接管全仓隐私与路径脱敏扫描及一键净化，并与 AuditSpdx.cmake、AuditTypes.cmake、AuditSloc.cmake、AuditImports.cmake 共同并入 cmake/PackageRelease.cmake 打包前五重硬门禁；③ 责令对第 73、74、75、76 号文档中的真实宿主路径与外部脚本指令进行原位净化重写（本 77 号裁决书自身严格贯彻零敏感词样板），随后仅需执行唯一确定的纯 CMake 三步发布流水线即可正式打标 v0.4.0 并推送远端。

# **执行摘要（Executive Ruling）**

首席领域专家组对 `76 号咨询委托书` 呈报的两项重大架构与合规偏差作出法医级终审定性，并针对四项请求签发如下刚性裁决：

1. **请求一裁决（100% 证实根域 `scripts/` 违规反弹，下达 `git rm -rf scripts/` 物理铲除令）**：  
   * **法理与历史铁证**：`DECISIONS.md`（`[DEC-002]`）与 Phase 11.3 第 38 号终审报告明确确立了"全仓构建、审计、打包原生收拢于 `cmake/*.cmake`，全仓测试原生收拢于 C++ 测试夹具（`tests/`）"的纯洁性红线，并早已下达物理铲除 `scripts/` 的强制指令。Phase 11 末期至 Phase 12 期间先后冒出的 `.ps1` 与 `.py` 体外脚本均属未经授权的"破窗反弹"，必须连根拔除，实现根域 `scripts/` **零存在**。  
2. **请求二裁决（确立纯原生 `cmake/AuditPrivacy.cmake` 为第五大静态硬门禁，零 Python / PowerShell 依赖）**：  
   * **架构自洽性**：CineRTX 工程的静态审计体系（`AuditSpdx.cmake`、`AuditTypes.cmake`、`AuditSloc.cmake`、`AuditImports.cmake`）全部采用零外部运行时依赖的纯 CMake 脚本语言（`cmake -P`）实现。隐私与路径泄露审计同样必须以纯 CMake 实现为 **`cmake/AuditPrivacy.cmake`**（支持 `-DFIX=ON` 一键原位脱敏模式与默认 `FATAL_ERROR` 硬阻断审计模式），并直接嵌入 `cmake/PackageRelease.cmake` 的前置门禁循环中。  
3. **请求三裁决（下达第 73–76 号文档原位纯净化重构令，本裁决书率先垂范）**：  
   * 73 号施工单在列举违规模式时未作转义或占位符抽象，直接将真实宿主账户路径与个人标识写入受控文档，构成典型的"脱敏工件二次泄密"。责令执行工程师立即使用 `cmake -DFIX=ON -P cmake/AuditPrivacy.cmake` 对全仓（尤其是 `docs/phase11/`、`docs/phase12/` 及 `73–76` 号文档）实施全量原位清洗，并将 74、75 号文档中的所有体外脚本命令统一修正为原生 `cmake -P` 指令。本 `77 号终审裁决书` 全文严格零出现任何真实个人账户名、云盘品牌词、本地盘符绝对路径或内网私网 IP。  
4. **请求四裁决（固化唯一确定的三步纯 CMake 原生发版流水线）**：  
   * 废除一切外部脚本包装，项目最终构建、五重门禁验证、发布包封装与 Git 推送收敛为仅依赖 `cmake` 与 `git` 的极简三步标准序列。

# **一、 第五大原生硬门禁：`cmake/AuditPrivacy.cmake` 权威标准实现**

将以下纯 CMake 脚本完整落盘至 `cmake/AuditPrivacy.cmake`（具备规范 `GPL-3.0-or-later WITH Plugin-Exception` SPDX 头，支持默认硬审计模式与 `-DFIX=ON` 一键清洗模式，且自身正则模式采用字符拆分构造以避免自匹配误报）：

```cmake
# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
# Copyright (C) 2026 RTX Transcode Engine contributors
#
# Gate P12-Privacy (Pure CMake-Native Repository Privacy & Path Leak Audit — Ruling 077):
#   Usage:
#     cmake -P cmake/AuditPrivacy.cmake             # Strict audit mode (FATAL_ERROR on any hit)
#     cmake -DFIX=ON -P cmake/AuditPrivacy.cmake    # In-place sanitize docs/ then re-audit

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(DEFINED ROOT AND NOT "${ROOT}" STREQUAL "")
    get_filename_component(REPO_ROOT "${ROOT}" ABSOLUTE)
endif()

if(NOT DEFINED FIX)
    set(FIX FALSE)
endif()

# Construct detection regexes dynamically so this script never triggers self-hits
set(_DRIVE_PREFIX "[A-Za-z]:[/\\\\]")
set(_WIN_USER_RX "${_DRIVE_PREFIX}Users[/\\\\][A-Za-z0-9._-]+")
string(CONCAT _LEGACY_HANDLE "z" "t")
set(_POSIX_HANDLE_RX "[/\\\\]Users[/\\\\]${_LEGACY_HANDLE}([^A-Za-z0-9_-]|$)")
string(CONCAT _CLOUD_M "M" "y")
string(CONCAT _CLOUD_G "Goo" "gle")
set(_CLOUD_DRIVE_RX "(${_CLOUD_M}|${_CLOUD_G})[ \t]+Drive")
string(CONCAT _CLOUD_URL_RX "https?://(drive|docs)\\." "goo" "gle\\.com/")
string(CONCAT _FILE_URI_RX "file:" "///")
set(_LAN_IP_RX "(^|[^0-9.])(192\\.168\\.[0-9]+\\.[0-9]+|10\\.[0-9]+\\.[0-9]+\\.[0-9]+|172\\.(1[6-9]|2[0-9]|3[01])\\.[0-9]+\\.[0-9]+)([^0-9.]|$)")
set(_EMAIL_RX "[A-Za-z0-9._+-]+@(gmail|qq|163|outlook|hotmail)\\.com")
string(CONCAT _HOST_PUB_RX "[A-Za-z]:[/\\\\]" "PUB" "LIC[/\\\\]CineRTX")
string(CONCAT _TEST_SSH_RX "(^|[^A-Za-z0-9_])" "we" "ak" "@(127\\.0\\.0\\.1|localhost|[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+)")

# Collect canonical tracked source, build, and documentation files
file(GLOB_RECURSE _PRIV_FILES
    "${REPO_ROOT}/include/*.h"
    "${REPO_ROOT}/include/*.hpp"
    "${REPO_ROOT}/src/*.h"
    "${REPO_ROOT}/src/*.hpp"
    "${REPO_ROOT}/src/*.cpp"
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
    "${REPO_ROOT}/src/*.cu"
    "${REPO_ROOT}/src/*.hlsl"
)

foreach(_root_doc
    "${REPO_ROOT}/CMakeLists.txt"
    "${REPO_ROOT}/CMakePresets.json"
    "${REPO_ROOT}/README.md"
    "${REPO_ROOT}/README_zh.md"
    "${REPO_ROOT}/DECISIONS.md"
    "${REPO_ROOT}/ISSUES.md"
    "${REPO_ROOT}/build.bat"
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

        string(REGEX REPLACE "${_FILE_URI_RX}[A-Za-z]:/Users/[^/)\r\n]+/(${_CLOUD_M}%20Drive|${_CLOUD_G}%20Drive)/work/CineRTX/" "../../" _txt "${_txt}")
        string(REGEX REPLACE "${_FILE_URI_RX}[A-Za-z]:/Users/[^/)\r\n]+/[^/)\r\n]+/CineRTX/" "../../" _txt "${_txt}")
        string(REGEX REPLACE "${_FILE_URI_RX}" "./" _txt "${_txt}")
        string(REGEX REPLACE "[A-Za-z]:\\\\Users\\\\[^\\\\\r\n`\"']+\\\\(${_CLOUD_M} Drive|${_CLOUD_G} Drive)\\\\work\\\\CineRTX\\\\" "[REDACTED_PATH]\\\\" _txt "${_txt}")
        string(REGEX REPLACE "[A-Za-z]:/Users/[^/\r\n`\"']+/(${_CLOUD_M} Drive|${_CLOUD_G} Drive)/work/CineRTX/" "[REDACTED_PATH]/" _txt "${_txt}")
        string(REGEX REPLACE "[A-Za-z]:\\\\Users\\\\[^\\\\\r\n`\"']+\\\\test\\\\evidence\\\\phase12" "[REDACTED_TEMP_DIR]\\\\CineRTX\\\\evidence\\\\phase12" _txt "${_txt}")
        string(REGEX REPLACE "[A-Za-z]:/Users/[^/\r\n`\"']+/test/evidence/phase12" "[REDACTED_TEMP_DIR]/CineRTX/evidence/phase12" _txt "${_txt}")
        string(REGEX REPLACE "[A-Za-z]:\\\\Users\\\\[^\\\\\r\n`\"']+\\\\" "[REDACTED_PATH]\\\\" _txt "${_txt}")
        string(REGEX REPLACE "[A-Za-z]:/Users/[^/\r\n`\"']+/" "[REDACTED_PATH]/" _txt "${_txt}")
        string(REGEX REPLACE "${_POSIX_HANDLE_RX}" "[REDACTED_PATH]\\1" _txt "${_txt}")
        string(REGEX REPLACE "${_CLOUD_DRIVE_RX}" "workspace" _txt "${_txt}")
        string(REGEX REPLACE "${_CLOUD_URL_RX}[^\r\n)\\]\"' ]*" "[internal-reference]" _txt "${_txt}")
        string(REGEX REPLACE "192\\.168\\.[0-9]+\\.[0-9]+" "127.0.0.1" _txt "${_txt}")
        string(REGEX REPLACE "${_EMAIL_RX}" "[REDACTED_EMAIL]" _txt "${_txt}")
        string(REGEX REPLACE "${_HOST_PUB_RX}" "[REDACTED_PATH]" _txt "${_txt}")
        string(REGEX REPLACE "${_TEST_SSH_RX}" "\\1[REDACTED_HOST]" _txt "${_txt}")

        if(NOT _txt STREQUAL _orig)
            file(WRITE "${_f}" "${_txt}")
            math(EXPR _FIXED_COUNT "${_FIXED_COUNT} + 1")
        endif()
    endforeach()
    message(STATUS "[AuditPrivacy] Sanitized ${_FIXED_COUNT} file(s).")
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
            list(APPEND _VIOLATIONS "${_rel}:${_lineno}: Windows user directory path leak")
        endif()
        if(_line MATCHES "${_POSIX_HANDLE_RX}")
            list(APPEND _VIOLATIONS "${_rel}:${_lineno}: Personal user handle leak")
        endif()
        if(_line MATCHES "${_CLOUD_DRIVE_RX}")
            list(APPEND _VIOLATIONS "${_rel}:${_lineno}: Cloud drive folder name leak")
        endif()
        if(_line MATCHES "${_CLOUD_URL_RX}")
            list(APPEND _VIOLATIONS "${_rel}:${_lineno}: Cloud share URL leak")
        endif()
        if(_line MATCHES "${_FILE_URI_RX}")
            list(APPEND _VIOLATIONS "${_rel}:${_lineno}: Local file URI leak")
        endif()
        if(_line MATCHES "${_EMAIL_RX}")
            list(APPEND _VIOLATIONS "${_rel}:${_lineno}: Personal email address leak")
        endif()
        if(_line MATCHES "${_HOST_PUB_RX}")
            list(APPEND _VIOLATIONS "${_rel}:${_lineno}: Host non-user drive path leak")
        endif()
        if(_line MATCHES "${_TEST_SSH_RX}")
            list(APPEND _VIOLATIONS "${_rel}:${_lineno}: Test host SSH account leak")
        endif()
        if(_line MATCHES "${_LAN_IP_RX}")
            # Exempt known software version token 10.13.3.9 (TensorRT)
            string(REPLACE "10.13.3.9" "" _ip_check "${_line}")
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
    message(FATAL_ERROR "[AuditPrivacy] FAIL: ${_vcount} privacy violation(s) detected across ${_SCANNED} file(s).")
endif()

message(STATUS "[AuditPrivacy] PASS: 0 privacy hits across ${_SCANNED} scanned file(s).")
```

# **二、 `cmake/PackageRelease.cmake` 门禁集成升级**

在 `cmake/PackageRelease.cmake` 第 40 行的前置门禁循环中，将 `"AuditPrivacy.cmake"` 正式加入强制前置门禁集合，确保任何包含未脱敏信息的构建产物均无法生成发布包：

```
# cmake/PackageRelease.cmake (Lines 40-50)
foreach(_audit_script "AuditSpdx.cmake" "AuditTypes.cmake" "AuditSloc.cmake" "AuditPrivacy.cmake")
    if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/${_audit_script}")
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_LIST_DIR}/${_audit_script}"
            WORKING_DIRECTORY "${REPO_ROOT}"
            RESULT_VARIABLE _audit_rc)
        if(NOT _audit_rc EQUAL 0)
            message(FATAL_ERROR "[PackageRelease] Pre-package gate failed: ${_audit_script} (rc=${_audit_rc})")
        endif()
    endif()
endforeach()
```

# **三、 立即执行整改与纯 CMake 三步发版流水线**

执行工程师与协调推进组按以下唯一确定的纯 CMake 原生指令完成最终收口：

## **第一步：物理铲除 `scripts/` 全目录并原位清洗全部过程文档**

```
# 1. 彻底物理铲除根域违规反弹的 scripts/ 全目录
git rm -rf scripts/

# 2. 落盘 cmake/AuditPrivacy.cmake 并更新 cmake/PackageRelease.cmake 后，执行一键原位全仓脱敏与硬审计
cmake -DFIX=ON -P cmake/AuditPrivacy.cmake
cmake -P cmake/AuditPrivacy.cmake
# 预期输出: [AuditPrivacy] PASS: 0 privacy hits across ... scanned file(s).
```

*(同步手工复核 `docs/phase12/73_...md`、`74_...md`、`75_...md`、`76_...md`，确保全文无任何真实账户标识及 `.ps1` / `.py` 调用残余。)*

## **第二步：五重 CMake 原生静态门禁与发布包一键构建**

```
# 1. 五重纯原生静态硬门禁全部绿灯
cmake -P cmake/AuditSpdx.cmake
cmake -P cmake/AuditTypes.cmake
cmake -P cmake/AuditSloc.cmake
cmake -P cmake/AuditPrivacy.cmake
cmake -DTARGET_BIN=build/windows-msvc-relwithdebinfo/RelWithDebInfo/CineRTX.exe -DMODE=GUI -P cmake/AuditImports.cmake
cmake -DTARGET_BIN=build/windows-msvc-relwithdebinfo/RelWithDebInfo/rtx_headless_cli.exe -DMODE=HEADLESS -P cmake/AuditImports.cmake

# 2. 运行全量单元测试并生成最终版便携发布归档与 SHA-256 清单
.\build\windows-msvc-relwithdebinfo\RelWithDebInfo\rtx_tests.exe
cmake -DBUILD_DIR=build/windows-msvc-relwithdebinfo/RelWithDebInfo -P cmake/PackageRelease.cmake
```

## **第三步：核验工作区零脏污并由项目负责人批准正式推送 GitHub**

```
git status
git add -A
git commit -m "chore(release): purge scripts/, enforce CMake-native AuditPrivacy, and seal v0.4.0"
git tag -a v0.4.0 -m "CineRTX v0.4.0: C++26 Std-First (34/35 types), Pure CMake Gates, Headless 2-DLL IAT"
git push origin main --tags
```

**首席领域专家组正式签署：根域 `scripts/` 物理清剿令与纯原生 `AuditPrivacy.cmake` 门禁即刻生效，全仓达到 100% 纯洁开源发布标准！**