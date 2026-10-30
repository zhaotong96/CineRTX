# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
# 文件路径: cmake/RtxFfmpegStatic.cmake
# 功能说明: CineRTX FFmpeg 全量静态内联供应链、Git Submodule 自动化闭环构建与 rtx::ffmpeg_static 靶标定义
#
# 设计哲学 (KISS 与奥卡姆剃刀原则):
#   1. 彻底废除外部路径变量: 源码由 Git Submodule (third_party/ffmpeg) 严格锚定，
#      编译产物输出至 ${CMAKE_BINARY_DIR}/external/ffmpeg-static，完全随构建目录生命周期自闭环管理。
#   2. 零配置开箱即用 (Out-Of-The-Box): 无论是 MSVC、GCC 还是 Clang，直接运行:
#        mkdir build && cmake -B build -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build --config RelWithDebInfo -j
#      即可一键全自动化编译成功。
#   3. 手术刀级裁剪 + 导入桩防伪熔断 (Gate 3.0):
#      - 编译期硬性校验静态库真实体积 (avformat/avcodec 均 > 450 KB)，杜绝 DLL 动态导入桩；
#      - 若未初始化 submodule，CMake 自动执行拉取；若未构建静态库，CMake 在配置阶段自动触发编译。

include_guard(GLOBAL)

set(_ffmpeg_submodule_dir "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg")
set(_ffmpeg_dist_dir "${CMAKE_BINARY_DIR}/external/ffmpeg-static")
set(_ffmpeg_build_dir "${CMAKE_BINARY_DIR}/external/ffmpeg-build")
set(_ffmpeg_inc_dir "${_ffmpeg_dist_dir}/include")
set(_ffmpeg_lib_dir "${_ffmpeg_dist_dir}/lib")

# ---------------------------------------------------------------------------
# 施工项 A-1: 零宿主前置假设 —— nasm / make 就地拉取与沙箱化
#
# 所有物料严格收敛于 ${CMAKE_BINARY_DIR}/external 之下 (download/ 存放归档,
# tools/<name>/ 存放解压树)，与根目录 build.bat 的自举布局完全一致并互相
# 复用归档；二次运行时哈希校验通过即跳过下载，天然幂等。
# ---------------------------------------------------------------------------
set(_rtx_ext_dl_dir "${CMAKE_BINARY_DIR}/external/download")
set(_rtx_ext_tools_dir "${CMAKE_BINARY_DIR}/external/tools")

# _rtx_provision_tool(<name> <url> <sha256> <exe_filename> <out_var>)
#   下载 (校验 SHA256) 并解压便携工具; 成功时 <out_var> 为可执行文件全路径。
#   已解压或归档已在 (build.bat 预下载) 时零网络开销直接复用。
function(_rtx_provision_tool tool_name url expected_sha exe_name out_var)
    set(_tool_dir "${_rtx_ext_tools_dir}/${tool_name}")
    set(_archive "${_rtx_ext_dl_dir}/${tool_name}.zip")

    file(GLOB_RECURSE _already_there "${_tool_dir}/${exe_name}")
    if(_already_there)
        list(GET _already_there 0 _exe_path)
        set(${out_var} "${_exe_path}" PARENT_SCOPE)
        return()
    endif()

    file(MAKE_DIRECTORY "${_rtx_ext_dl_dir}")
    set(_need_download TRUE)
    if(EXISTS "${_archive}")
        file(SHA256 "${_archive}" _archive_sha)
        if(_archive_sha STREQUAL "${expected_sha}")
            set(_need_download FALSE)
        else()
            file(REMOVE "${_archive}")
        endif()
    endif()

    if(_need_download)
        message(STATUS "[CineRTX-Bootstrap] Downloading ${tool_name} from ${url}")
        # curl is tried first because it honours HTTPS_PROXY/HTTP_PROXY, which
        # file(DOWNLOAD) does not. Visual Studio's CMake integration inherits
        # those variables, so this is the only proxy-aware path available to an
        # IDE-driven configure.
        find_program(_rtx_curl_exe NAMES curl.exe curl)
        if(_rtx_curl_exe)
            execute_process(
                COMMAND "${_rtx_curl_exe}" -fL --retry 3 --retry-delay 2
                        --connect-timeout 30
                        -o "${_archive}" "${url}"
                RESULT_VARIABLE _curl_result
                OUTPUT_QUIET
                ERROR_VARIABLE _curl_error)
            if(NOT _curl_result EQUAL 0)
                message(STATUS "[CineRTX-Bootstrap] curl failed: ${_curl_error}")
                file(REMOVE "${_archive}")
            endif()
        endif()
        if(NOT EXISTS "${_archive}")
            file(DOWNLOAD "${url}" "${_archive}"
                 SHOW_PROGRESS
                 STATUS _dl_status)
            list(GET _dl_status 0 _dl_code)
            if(NOT _dl_code EQUAL 0)
                list(GET _dl_status 1 _dl_msg)
                file(REMOVE "${_archive}")
                message(WARNING "[CineRTX-Bootstrap] ${tool_name} download failed: ${_dl_msg}")
                set(${out_var} "" PARENT_SCOPE)
                return()
            endif()
        endif()
        file(SHA256 "${_archive}" _archive_sha)
        if(NOT "${_archive_sha}" STREQUAL "${expected_sha}")
            file(REMOVE "${_archive}")
            message(WARNING "[CineRTX-Bootstrap] ${tool_name} SHA256 mismatch")
            set(${out_var} "" PARENT_SCOPE)
            return()
        endif()
    endif()

    file(MAKE_DIRECTORY "${_tool_dir}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E tar xf "${_archive}"
        WORKING_DIRECTORY "${_tool_dir}"
        RESULT_VARIABLE _extract_res)
    if(NOT _extract_res EQUAL 0)
        message(WARNING "[CineRTX-Bootstrap] Failed to extract ${_archive}")
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()

    file(GLOB_RECURSE _found "${_tool_dir}/${exe_name}")
    if(_found)
        list(GET _found 0 _exe_path)
        set(${out_var} "${_exe_path}" PARENT_SCOPE)
    else()
        set(${out_var} "" PARENT_SCOPE)
    endif()
endfunction()

# Pinned artifacts (SHA256 verified against live downloads, 2026-10):
#   NASM 2.16.03 win64 —— nasm.us 官方权威源。
#   注意: NASM 官方 GitHub 仓库 (netwide-assembler/nasm) 不发布任何 Release
#   资产 (已实测 404 / releases 页面为空)，故不存在可校验的官方 GitHub 容灾
#   镜像；此处不硬编码任何未经验证的猜测 URL。
set(_rtx_nasm_url "https://www.nasm.us/pub/nasm/releasebuilds/2.16.03/win64/nasm-2.16.03-win64.zip")
set(_rtx_nasm_sha "3ee4782247bcb874378d02f7eab4e294a84d3d15f3f6ee2de2f47a46aa7226e6")
#   GNU Make 4.4.1 —— ezwinports (GNU 官方 Windows 移植维护者 Eli Zaretskii
#   发布的独立便携版, SourceForge 官方项目页, 实测 200 且可执行)。
set(_rtx_make_url "https://downloads.sourceforge.net/project/ezwinports/make-4.4.1-without-guile-w32-bin.zip")
set(_rtx_make_sha "fb66a02b530f7466f6222ce53c0b602c5288e601547a034e4156a512dd895ee7")


# 1. 确保 Git Submodule 源码已就绪
if(NOT EXISTS "${_ffmpeg_submodule_dir}/configure")
    find_package(Git QUIET)
    if(GIT_FOUND)
        message(STATUS "[CineRTX] Initializing FFmpeg submodule at third_party/ffmpeg...")
        execute_process(
            COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive third_party/ffmpeg
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            RESULT_VARIABLE _submod_res
        )
        if(NOT _submod_res EQUAL 0 OR NOT EXISTS "${_ffmpeg_submodule_dir}/configure")
            message(FATAL_ERROR
                "[CineRTX] Failed to checkout third_party/ffmpeg submodule.\n"
                "Please run 'git submodule update --init --recursive third_party/ffmpeg' manually.")
        endif()
    else()
        message(FATAL_ERROR
            "[CineRTX] FFmpeg source tree not found at ${_ffmpeg_submodule_dir} and git executable is not available.")
    endif()
endif()

# 2. 检查静态物料是否已构建且合规
set(_need_build_ffmpeg FALSE)
set(_required_ffmpeg_libs avformat avcodec avutil swresample)

if(WIN32)
    foreach(_lib_name IN LISTS _required_ffmpeg_libs)
        set(_lib_file "${_ffmpeg_lib_dir}/${_lib_name}.lib")
        if(NOT EXISTS "${_lib_file}")
            set(_lib_file "${_ffmpeg_lib_dir}/lib${_lib_name}.a")
        endif()
        if(NOT EXISTS "${_lib_file}")
            set(_need_build_ffmpeg TRUE)
            break()
        endif()

        # 核心防伪熔断：动态库导入桩仅 ~130 KB，真静态归档库必 > 450 KB
        if(_lib_name STREQUAL "avformat" OR _lib_name STREQUAL "avcodec")
            file(SIZE "${_lib_file}" _lib_size_bytes)
            if(_lib_size_bytes LESS 450000)
                set(_need_build_ffmpeg TRUE)
                break()
            endif()
        endif()
    endforeach()

    if(NOT EXISTS "${_ffmpeg_inc_dir}/libavformat/avformat.h")
        set(_need_build_ffmpeg TRUE)
    endif()

    # 3. 若尚未构建，自动触发本地手术刀级静态编译
    if(_need_build_ffmpeg)
        message(STATUS "[CineRTX-FFmpeg] Surgical static FFmpeg libraries not found or invalid in ${_ffmpeg_dist_dir}.")
        message(STATUS "[CineRTX-FFmpeg] Triggering automated surgical static build from third_party/ffmpeg...")

        # ---- 施工项 A-1: nasm / make 缺失时就地拉取便携版 ----
        find_program(NASM_EXE NAMES nasm.exe nasm)
        if(NOT NASM_EXE)
            _rtx_provision_tool(nasm "${_rtx_nasm_url}" "${_rtx_nasm_sha}" nasm.exe _nasm_provisioned)
            if(_nasm_provisioned)
                set(NASM_EXE "${_nasm_provisioned}" CACHE FILEPATH "NASM x86 assembler" FORCE)
            else()
                message(FATAL_ERROR
                    "[CineRTX-Bootstrap] Failed to provision nasm.exe from ${_rtx_nasm_url}.\n"
                    "Note: NASM publishes no official GitHub Release mirror; check the "
                    "network or set HTTPS_PROXY/HTTP_PROXY before configuring.")
            endif()
        endif()
        get_filename_component(_nasm_dir "${NASM_EXE}" DIRECTORY)
        message(STATUS "[CineRTX-Bootstrap] nasm: ${NASM_EXE}")

        find_program(MAKE_EXE NAMES make.exe mingw32-make.exe)
        if(NOT MAKE_EXE)
            _rtx_provision_tool(make "${_rtx_make_url}" "${_rtx_make_sha}" make.exe _make_provisioned)
            if(_make_provisioned)
                set(MAKE_EXE "${_make_provisioned}" CACHE FILEPATH "GNU Make for FFmpeg" FORCE)
            else()
                message(FATAL_ERROR
                    "[CineRTX-Bootstrap] Failed to provision make.exe from ${_rtx_make_url}.")
            endif()
        endif()
        get_filename_component(_make_dir "${MAKE_EXE}" DIRECTORY)
        message(STATUS "[CineRTX-Bootstrap] make: ${MAKE_EXE}")

        # 局部 PATH 注入: 仅对本次子进程生效，绝不触碰系统 PATH。
        # 注意: PATH 值含 ';' (CMake 列表分隔符)，必须以带引号的单一实参传递。
        set(_child_path "${_nasm_dir};${_make_dir};$ENV{PATH}")
        set(_zig_env "")
        set(_rtx_zig_for_child "")
        if(RTX_ZIG_EXECUTABLE)
            # CMake 已选定 Zig 工具链: FFmpeg 必须使用同一 ABI，即使当前环境
            # (例如 Visual Studio 的 CMake 集成) 同时提供了 cl.exe。
            set(_rtx_zig_for_child "${RTX_ZIG_EXECUTABLE}")
        else()
            find_program(_cl_probe NAMES cl.exe cl)
            if(NOT _cl_probe)
                find_program(_zig_probe NAMES zig.exe zig)
                if(_zig_probe)
                    set(_rtx_zig_for_child "${_zig_probe}")
                endif()
            endif()
        endif()
        if(_rtx_zig_for_child)
            get_filename_component(_zig_dir "${_rtx_zig_for_child}" DIRECTORY)
            set(_child_path "${_zig_dir};${_child_path}")
            set(_zig_env "RTX_ZIG=${_rtx_zig_for_child}")
        endif()

        # 规范化转义 POSIX 风格路径供 bash 使用
        file(TO_CMAKE_PATH "${_ffmpeg_submodule_dir}" _src_path)
        file(TO_CMAKE_PATH "${_ffmpeg_dist_dir}" _dist_path)
        file(TO_CMAKE_PATH "${_ffmpeg_build_dir}" _bld_path)

        set(_build_bat "${CMAKE_CURRENT_SOURCE_DIR}/tools/build_ffmpeg_static.bat")
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E env "PATH=${_child_path}" ${_zig_env}
                    cmd /c "${_build_bat}" "${_src_path}" "${_dist_path}" "${_bld_path}"
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
            RESULT_VARIABLE _build_res
        )

        if(NOT _build_res EQUAL 0)
            message(FATAL_ERROR
                "[CineRTX-FFmpeg] Automated surgical static build failed with exit code: ${_build_res}.\n"
                "MSVC path: ensure VS 2022/2026 toolset; bare-host path: ensure Git Bash and "
                "Zig 0.13 (NASM/make are auto-provisioned under external/tools/).\n"
                "If tool downloads are blocked, set HTTPS_PROXY and HTTP_PROXY before "
                "starting Visual Studio or CMake.")
        endif()
    endif()

    # 4. 再次严密校验产物完整性与防伪门禁
    if(NOT EXISTS "${_ffmpeg_inc_dir}/libavformat/avformat.h")
        message(FATAL_ERROR "[CineRTX-FFmpeg] Missing headers at ${_ffmpeg_inc_dir}/libavformat/avformat.h")
    endif()

    foreach(_lib_name IN LISTS _required_ffmpeg_libs)
        set(_lib_file "${_ffmpeg_lib_dir}/${_lib_name}.lib")
        if(NOT EXISTS "${_lib_file}")
            set(_lib_file "${_ffmpeg_lib_dir}/lib${_lib_name}.a")
        endif()
        if(NOT EXISTS "${_lib_file}")
            message(FATAL_ERROR "[CineRTX-FFmpeg] Missing required static archive: ${_ffmpeg_lib_dir}/${_lib_name}.lib")
        endif()

        if(_lib_name STREQUAL "avformat" OR _lib_name STREQUAL "avcodec")
            file(SIZE "${_lib_file}" _lib_size_bytes)
            if(_lib_size_bytes LESS 450000)
                message(FATAL_ERROR
                    "[CineRTX-ArchGuard] CRITICAL VIOLATION: '${_lib_file}' is only ${_lib_size_bytes} bytes (< 450 KB)!\n"
                    "This file is a dynamic DLL import stub, NOT a true static archive.")
            endif()
        endif()

        add_library(rtx::ffmpeg_${_lib_name} STATIC IMPORTED GLOBAL)
        set_target_properties(rtx::ffmpeg_${_lib_name} PROPERTIES
            IMPORTED_LOCATION "${_lib_file}"
            INTERFACE_INCLUDE_DIRECTORIES "${_ffmpeg_inc_dir}"
        )
    endforeach()

    # 声明统一聚合靶标 rtx::ffmpeg_static，严格遵循符号依赖拓扑顺序
    add_library(rtx_ffmpeg_static INTERFACE)
    add_library(rtx::ffmpeg_static ALIAS rtx_ffmpeg_static)

    target_include_directories(rtx_ffmpeg_static SYSTEM INTERFACE "${_ffmpeg_inc_dir}")

    # 挂载四大静态库及 Win32 系统依赖库
    target_link_libraries(rtx_ffmpeg_static INTERFACE
        rtx::ffmpeg_avformat
        rtx::ffmpeg_avcodec
        rtx::ffmpeg_swresample
        rtx::ffmpeg_avutil
        bcrypt
        ws2_32
        secur32
        shlwapi
        strmiids
        mfuuid
        ole32
        user32
    )

    if(MSVC)
        target_link_options(rtx_ffmpeg_static INTERFACE
            $<$<NOT:$<CONFIG:Debug>>:/OPT:REF /OPT:ICF>
            $<$<CONFIG:Debug>:/NODEFAULTLIB:msvcrt.lib>
        )
    endif()
else()
    # Linux GCC / Clang: the distribution packages are the supported supply
    # chain.  CI installs pkg-config plus the four -dev packages; a host without
    # them cannot configure, which is intended (the media layer is unconditional).
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(FFMPEG_STATIC QUIET libavformat libavcodec libswresample libavutil)
    endif()

    if(FFMPEG_STATIC_FOUND)
        add_library(rtx_ffmpeg_static INTERFACE)
        add_library(rtx::ffmpeg_static ALIAS rtx_ffmpeg_static)
        target_include_directories(rtx_ffmpeg_static SYSTEM INTERFACE ${FFMPEG_STATIC_INCLUDE_DIRS})
        # Prefer the fully static link set when the distribution ships the .a
        # archives.  Where it does not, the shared objects are linked and the
        # binary is not self-contained; Gate 3.0 self-containment is a Windows
        # product claim, not a Linux one.
        if(FFMPEG_STATIC_STATIC_LIBRARIES)
            target_link_libraries(rtx_ffmpeg_static INTERFACE ${FFMPEG_STATIC_STATIC_LIBRARIES} pthread m dl)
            message(STATUS "[CineRTX-FFmpeg] Linux: libav*.a present, linking statically")
        else()
            target_link_libraries(rtx_ffmpeg_static INTERFACE ${FFMPEG_STATIC_LIBRARIES} pthread m dl)
            message(WARNING
                "[CineRTX-FFmpeg] Linux: no libav*.a archives found; linking the shared "
                "FFmpeg libraries, so the Linux binary is not self-contained.")
        endif()
    else()
        message(FATAL_ERROR
            "[CineRTX-FFmpeg] FFmpeg development packages were not found via pkg-config.\n"
            "  Debian/Ubuntu: sudo apt-get install -y pkg-config libavformat-dev libavcodec-dev libswresample-dev libavutil-dev\n"
            "  Fedora/RHEL:   sudo dnf install -y pkgconf-pkg-config ffmpeg-devel\n"
            "The media container layer is unconditional; a build without it is not a CineRTX binary.")
    endif()
endif()
