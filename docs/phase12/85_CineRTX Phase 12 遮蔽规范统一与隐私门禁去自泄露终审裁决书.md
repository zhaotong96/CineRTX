# **CineRTX Phase 12 遮蔽规范统一与隐私门禁去自泄露终审裁决书**

* **文档编号**：`CINERTX-P12-RULING-085`  
* **归档路径**：`docs/phase12/85_CineRTX Phase 12 遮蔽规范统一与隐私门禁去自泄露终审裁决书.md`  
* **审验标的**：  
  * `docs/phase12/84_CineRTX Phase 12 遮蔽规范统一与隐私门禁自泄露风险专家咨询委托书.md`（`CINERTX-P12-DELEGATION-084`）  
  * `docs/phase12/83_CineRTX Phase 12 全仓锁定态隐私泄露风险终审排查报告.md`（`DOC-P12-83`）  
  * 仓库最新实体源码：`cmake/AuditPrivacy.cmake`、`docs/archive/Phase1_to_Phase7_Evolution_Digest.md`、`docs/phase12/80_*.md`、`docs/phase10/17_*.md`  
* **审计基线**：Commit `d4a63cf`（`feat(phase12): complete Phase 12 C++26 Std-First refactor, headless DLL clearance, pure CMake gates, and seal v0.4.0`）  
* **签发主体**：多媒体系统工程与 GPU 异构计算首席领域专家组（Extreme Modern C++ Purist & Architecture Board）  
* **审验日期**：2026-11-24  
* **终审裁决**：**全面批准执行"遮蔽符号 `[REDACTED_*]` 全局统一"与"`cmake/AuditPrivacy.cmake` 零特定个人特征通用化重构"，同步封堵 `Doc 80` 与 `Doc 17` 最后两处残留漏网项（APPROVED — UNIFY TO `[REDACTED_*]`, PURGE SIGNATURE-AS-LEAKAGE IN `AuditPrivacy.cmake`, AND RELEASE `v0.4.0`）**——首席领域专家组完全认同 `84 号咨询委托书` 揭露的两大本质隐患：① 近期文档引入的尖括号占位符违反了项目自 Phase 1 确立的 `[REDACTED_*]` 历史黄金标准，且在 Markdown 渲染器与拓扑语义推断上造成二次泄密；② `cmake/AuditPrivacy.cmake` 通过 `string(CONCAT ...)` 拼接特定个人账户、特定盘符与特定测试机账号片段，使公开源码中的门禁脚本自身沦为"全仓最精准的个人隐私字典"（Signature-as-Leakage）。专家组签发如下唯一权威标准规范与纯通用 `cmake/AuditPrivacy.cmake` 定型实现，完成收口并确认 `0 privacy hits` 后即刻放行 `git push origin main --tags`。

# **执行摘要（Executive Ruling）**

首席领域专家组对 `84 号咨询委托书` 与 `83 号全仓锁定态排查报告` 的物证进行了逐行复核，签发以下四项刚性裁决：

1. **议题一裁决（废除全部尖括号自定义占位符，全仓强制统一回退至 `[REDACTED_*]` 标准体系）**：  
   * **历史与工程法理**：核查 `docs/archive/Phase1_to_Phase7_Evolution_Digest.md` 第 83 行及 Phase 8–11 历次终审卷宗，CineRTX 自立项起便以 **`[REDACTED_*]` 方括号大写语义令牌**作为唯一官方脱敏标准。近期冒出的尖括号自定义标签不仅割裂了文档风格，还会在 GitHub Flavored Markdown（GFM）中被误解析为非标准 HTML 标签而静默吞没正文，同时暴露了内部目录与网络拓扑语义。  
   * **全局替换规范**：全仓文档与脚本中所有自定义尖括号占位符必须 100% 清洗为以下标准 `[REDACTED_*]` 集合（同步将尖括号自定义占位符纳入 `AuditPrivacy.cmake` 拦截检测，防止回潮）：  
     * 本地私有绝对路径 / 仓库根路径 / 宿主机共享盘路径 \$\\rightarrow\$ **`[REDACTED_PATH]`**（若在相对引用场景中，优先直接改写为仓库相对路径 `./...` 或 `../../...`）  
     * 临时构建 / 证据目录路径 \$\\rightarrow\$ **`[REDACTED_TEMP_DIR]`**  
     * 宿主机名 / 测试机与 SSH 标识 \$\\rightarrow\$ **`[REDACTED_HOST]`**  
     * 个人用户账户名 \$\\rightarrow\$ **`[REDACTED_USER]`**  
     * 私有内网 IP 地址 \$\\rightarrow\$ **`[REDACTED_IP]`**  
     * 私有云盘与未公开仓库/资源标识 \$\\rightarrow\$ **`[REDACTED_CLOUD_DRIVE]`** / **`[REDACTED_ID]`**  
     * 个人邮箱地址 \$\\rightarrow\$ **`[REDACTED_EMAIL]`**  
     * 令牌 / 密钥凭据 \$\\rightarrow\$ **`[REDACTED_SECRET]`**  
2. **议题二裁决（定性 `AuditPrivacy.cmake` 字符拼装为"特征库即泄密源"，采纳方向 A 纯通用启发式正则重构）**：  
   * **法医级定性**：在公开开源仓库中使用 `string(CONCAT ...)` 拼接特定个人账户名、特定盘符目录名与特定测试机账号前缀，虽然骗过了脚本自身的字符串扫描，却将作者个人隐私完整编码进公开源码，属于典型的安全反模式（Signature-as-Leakage），**必须彻底根除**。  
   * **方向 A 通用启发式定型**：全面删除 `cmake/AuditPrivacy.cmake` 中所有针对特定个人账户、特定盘符名、特定测试账号的拼接片段，仅保留**通用结构化模式匹配（Generic Heuristic Patterns）**：  
     * 通用 Windows / POSIX 用户家目录路径结构（如盘符 `Users` 目录与 POSIX `/Users/<name>` 或 `/home/<name>` 结构）；  
     * 通用非系统标准本地盘符绝对路径（如双反斜杠或单斜杠转义的非系统根路径特征）与本地 `file://` URI 协议；  
     * 通用云盘同步目录与云端分享链接结构；  
     * 通用 33 位云端目录/对象裸资源 ID（根治 `docs/phase10/17_*.md` 第 3 节漏网项）；  
     * 通用 RFC1918 私网 IP（精确豁免 `10.13.3.9` TensorRT 与 `10.0.x.x` Windows SDK 版本号）与通用 SSH 登录串 `<user>@<host>`（豁免 `git@github.com`）；  
     * 通用个人商业邮箱（豁免 `@example.com` 与 `@users.noreply.github.com`）、通用高熵 API Token / PEM 私钥头及残留的自定义尖括号占位符。  
3. **定向封堵 `83 号报告` 检出的最后两处残余漏网项**：  
   * **项 1（P0）**：清洗 `docs/phase12/80_终审裁决：暂缓推送（HOLD git push）.md` 第 11、12、15、42 行中 4 处经双反斜杠转义的宿主机盘符路径，统一替换为 `[REDACTED_PATH]`；  
   * **项 2（P1）**：清洗 `docs/phase10/17_CineRTX_Phase10_WebUI_v3样式与几何精修终审验收通过报告.md` 第 3 节括号内的 33 位私有云盘目录资源 ID，统一替换为 `[REDACTED_ID]`；  
   * **项 3（P1）**：在 `cmake/AuditPrivacy.cmake` 文件扫描列表中补齐 `SECURITY.md` 与 `LICENSE*` 根目录文件，确保全仓零盲区。  
4. **最终推送放行程序**：  
   * 执行工程师合入新版 `cmake/AuditPrivacy.cmake` 并完成上述文档清洗后，运行 `cmake -DFIX=ON -P cmake/AuditPrivacy.cmake` 与无参 `cmake -P cmake/AuditPrivacy.cmake` 验证全仓 **`0 privacy hits`（退出码 `0`）**，随后协调推进组与项目负责人即可正式追加收口提交、打标 `v0.4.0` 并执行 `git push origin main --tags`。

# **一、 `[REDACTED_*]` 全局统一映射规范表**

| 敏感信息类别 | 废除的旧占位符 | 唯一法定标准占位符 | 适用场景与示例 |
| :---- | :---- | :---- | :---- |
| **本地私有路径 / 仓库绝对路径** | 原尖括号路径占位符（`repoRoot` / `userHome` / `hostPublicDir`） | **`[REDACTED_PATH]`**（或直接用相对路径 `./...`） | 编译器报错前缀、构建工作区路径、非系统盘符本地路径 |
| **临时测试与证据目录** | 原尖括号临时目录占位符（`tempDir`） | **`[REDACTED_TEMP_DIR]`** | 宿主机临时日志目录、环境变量临时路径示例 |
| **宿主机与测试机标识** | 原尖括号主机占位符（`testHost` / `host`） | **`[REDACTED_HOST]`** | 验收环境主机名、SSH 连接目标主机、内网工作站节点 |
| **个人账户名** | `<user>` | **`[REDACTED_USER]`** | 操作系统本地登录名、SSH 用户名 |
| **内网私有 IP** | `127.0.0.1`（若原为局域网段掩盖） | **`[REDACTED_IP]`** | 局域网段 IP（标准回环地址 `127.0.0.1` / `localhost` 除外） |
| **私有云盘与云端资源 ID** | `workspace`（若指代私有云盘） / 裸 33 位 ID | **`[REDACTED_CLOUD_DRIVE]`** / **`[REDACTED_ID]`** | 云端同步盘名称、云盘分享 URL、云端目录/文件资源 ID |
| **个人邮箱与凭据密钥** | 原尖括号邮箱占位符（`maintainer@example.com`） | **`[REDACTED_EMAIL]`** / **`[REDACTED_SECRET]`** | 个人商业邮箱、PAT 令牌、API Key、私钥块 |

# **二、 纯通用零自泄露 `cmake/AuditPrivacy.cmake` 权威实现**

> 权威定型实现已直接落盘至 [`cmake/AuditPrivacy.cmake`](../../cmake/AuditPrivacy.cmake)（经 86 号施工汇报完成 KWSys 区间量词兼容展开，**零任何特定个人账户名、零任何特定盘符单词、零任何特定测试账号字符碎片**，100% 基于结构化通用正则），本裁决书不再重复内嵌自检冲突代码块。

```
# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
# Copyright (C) 2026 RTX Transcode Engine contributors
#
# Gate P12-Privacy (Generic Heuristic Repository Privacy & Redaction Audit — Ruling 085):
# Usage:
#   cmake -P cmake/AuditPrivacy.cmake           # Strict audit mode (FATAL_ERROR on any hit)
#   cmake -DFIX=ON -P cmake/AuditPrivacy.cmake  # In-place normalize legacy tags & leaks, then re-audit

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(DEFINED ROOT AND NOT "${ROOT}" STREQUAL "")
    get_filename_component(REPO_ROOT "${ROOT}" ABSOLUTE)
endif()

if(NOT DEFINED FIX)
    set(FIX FALSE)
endif()

# Generic structural patterns only — zero author-specific or machine-specific fragments
set(_DRIVE_PREFIX "[A-Za-z]:[/\\\\]+")
set(_WIN_USER_RX "${_DRIVE_PREFIX}Users[/\\\\]+[A-Za-z0-9._-]+")
set(_POSIX_HOME_RX "(^|[^A-Za-z0-9_.-])/(Users|home)/[A-Za-z0-9._-]+")
set(_NON_SYS_DRIVE_RX "[C-Zc-z]:[/\\\\]+(PUBLIC|Public|work|Work|workspace|Projects|projects)[/\\\\]+[A-Za-z0-9._-]+")
set(_CLOUD_DRIVE_RX "(My|Google)[ \\t]+Drive")
set(_CLOUD_URL_RX "https?://(drive|docs)\\.google\\.com/[^ \\t\\r\\n)\\]\"'>[REDACTED_PATH]*")
set(_CLOUD_BARE_ID_RX "(ID|id)[：:][ \\t]*1[A-Za-z0-9_-]{25,45}")
set(_FILE_URI_RX "file:///[^ \\t\\r\\n)\\]\"'>[REDACTED_PATH]*")
set(_LAN_IP_RX "(^|[^0-9.])(192\\.168\\.[0-9]+\\.[0-9]+|10\\.[0-9]+\\.[0-9]+\\.[0-9]+|172\\.(1[6-9]|2[0-9]|3[01])\\.[0-9]+\\.[0-9]+)([^0-9.]|$)")
set(_SSH_TARGET_RX "(^|[^A-Za-z0-9_.-])[A-Za-z0-9._-]+@([0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+|localhost|[A-Za-z0-9-]+\\.local)")
set(_EMAIL_RX "[A-Za-z0-9._+-]+@(gmail|qq|163|126|outlook|hotmail|icloud)\\.com")
set(_SECRET_RX "(ghp_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,}|AIza[0-9A-Za-z_-]{30,}|-----BEGIN [A-Z ]*PRIVATE KEY-----)")
set(_LEGACY_ANGLE_TAG_RX "<(userHome|repoRoot|tempDir|testHost|hostPublicDir|maintainer@example\\.com)>")

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

        # 1. Normalize legacy angle-bracket tags to canonical [REDACTED_*] standard
        string(REGEX REPLACE "<(userHome|repoRoot|hostPublicDir)>" "[REDACTED_PATH]" _txt "${_txt}")
        string(REGEX REPLACE "[REDACTED_TEMP_DIR]" "[REDACTED_TEMP_DIR]" _txt "${_txt}")
        string(REGEX REPLACE "[REDACTED_HOST]" "[REDACTED_HOST]" _txt "${_txt}")
        string(REGEX REPLACE "<maintainer@example\\.com>" "[REDACTED_EMAIL]" _txt "${_txt}")

        # 2. Sanitize paths, URIs, cloud drive names, bare IDs, SSH targets, and emails
        string(REGEX REPLACE "file:///[A-Za-z]:/Users/[^/)\\r\\n]+/[^/)\\r\\n]+/CineRTX/" "../../" _txt "${_txt}")
        string(REGEX REPLACE "file:///[^ \\t\\r\\n)\\]\"'>[REDACTED_PATH]*" "[REDACTED_PATH]" _txt "${_txt}")
        string(REGEX REPLACE "[A-Za-z]:[/\\\\]+Users[/\\\\]+[^/\\\\\\r\\n`\"')\\]+([/\\\\]+[^/\\\\\\r\\n`\"')\\]+)*" "[REDACTED_PATH]" _txt "${_txt}")
        string(REGEX REPLACE "(^|[^A-Za-z0-9_.-])/(Users|home)/[A-Za-z0-9._-]+(/[^ \\t\\r\\n`\"')\\]+)*" "\\1[REDACTED_PATH]" _txt "${_txt}")
        string(REGEX REPLACE "[C-Zc-z]:[/\\\\]+(PUBLIC|Public|work|Work|workspace|Projects|projects)([/\\\\]+[^/\\\\\\r\\n`\"')\\]+)*" "[REDACTED_PATH]" _txt "${_txt}")
        string(REGEX REPLACE "(My|Google)[ \\t]+Drive" "[REDACTED_CLOUD_DRIVE]" _txt "${_txt}")
        string(REGEX REPLACE "https?://(drive|docs)\\.google\\.com/[^ \\t\\r\\n)\\]\"'>[REDACTED_PATH]*" "[REDACTED_ID]" _txt "${_txt}")
        string(REGEX REPLACE "(ID|id)[：:][ \\t]*1[A-Za-z0-9_-]{25,45}" "\\1：[REDACTED_ID]" _txt "${_txt}")
        string(REGEX REPLACE "(^|[^A-Za-z0-9_.-])[A-Za-z0-9._-]+@([0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+|localhost|[A-Za-z0-9-]+\\.local)" "\\1[REDACTED_HOST]" _txt "${_txt}")
        string(REGEX REPLACE "[A-Za-z0-9._+-]+@(gmail|qq|163|126|outlook|hotmail|icloud)\\.com" "[REDACTED_EMAIL]" _txt "${_txt}")

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
    string(REGEX REPLACE "\\r\\n" "\\n" _content "${_content}")
    string(REGEX REPLACE "\\r" "\\n" _content "${_content}")
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
```

# **三、 最终执行收口与推送放行三步指令**

执行工程师合入上述新版 `cmake/AuditPrivacy.cmake` 后，在 Windows 宿主机本地仓库按顺序执行以下三步指令，全绿后即可无任何隐患推送至 GitHub：

```
# 1. 一键将全仓残留路径、裸资源 ID 及非标准尖括号占位符统一转换为 [REDACTED_*] 标准，并复核 0 违规
cmake -DFIX=ON -P cmake/AuditPrivacy.cmake
cmake -P cmake/AuditPrivacy.cmake
# 预期输出: [AuditPrivacy] PASS: 0 violations across ... file(s) ([REDACTED_*] compliant).

# 2. 五重纯原生硬门禁与最终发布包刷新验证
cmake -P cmake/AuditSpdx.cmake
cmake -P cmake/AuditTypes.cmake
cmake -P cmake/AuditSloc.cmake
cmake -DTARGET_BIN=.vs/build/cmake/windows-msvc-ninja/CineRTX.exe -DMODE=GUI -P cmake/AuditImports.cmake
cmake -DTARGET_BIN=.vs/build/cmake/windows-msvc-ninja/rtx_headless_cli.exe -DMODE=HEADLESS -P cmake/AuditImports.cmake
cmake -DBUILD_DIR=.vs/build/cmake/windows-msvc-ninja -P cmake/PackageRelease.cmake

# 3. 核验工作区干净并追加收口提交、刷新 v0.4.0 标签与推送远端
git status
git add -A
git commit -m "chore(privacy): unify [REDACTED_*] standard and harden generic AuditPrivacy gate"
git tag -f -a v0.4.0 -m "CineRTX v0.4.0: C++26 Std-First (34/35 types), Pure CMake Gates, [REDACTED_*] Compliant"
git push origin main --tags
```

**首席领域专家组正式签署：自本裁决书落地并跑通上述 `AuditPrivacy.cmake` 零违规校验后，全仓达到 100% 隐私纯粹与开源发布标准，准予正式推送 GitHub！**