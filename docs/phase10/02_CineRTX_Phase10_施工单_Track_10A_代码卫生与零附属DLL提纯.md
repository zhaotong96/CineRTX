# CineRTX Phase 10 施工单（Track 10A）：代码卫生、零附属 DLL 提纯与良构重构

- **文档编号**：`CINERTX-P10-TRACK-10A`
- **对应总纲任务**：任务 1（代码卫生、架构错配与非良构债务大清洗） & 任务 2（`RTX_BUILD_TESTS` 残留开关根治）
- **核心目标**：以奥卡姆剃刀精神彻底拔除历史妥协留下的死核函数、重复编译目标、冗余动态加载反射层与上帝函数，使代码库回归紧凑、正交、C++26 良构状态。

---

## 一、 问题 2 专题解答与根治：`RTX_BUILD_TESTS` CMake 告警

### 1.1 现象与因果链
全新删除 `build` 目录后执行 Configure 时出现：
```text
CMake Warning:
  Manually-specified variables were not used by the project:
    RTX_BUILD_TESTS
```
- **根因**：Phase 8 收官时，`CMakeLists.txt` 遵循“测试即一等公民、禁止条件编译掩盖编译错误”原则，删除了 `option(RTX_BUILD_TESTS ...)`，将 `rtx_tests` 设为常驻编译目标（见 `CMakeLists.txt:711`）。然而，`CMakePresets.json` 的第 `14` 行（`base` 预设）和第 `69` 行（`windows-msvc` 预设）仍保留着 `"RTX_BUILD_TESTS": "ON"`，导致 VS / CMake 每次调用 `cmake --preset windows-msvc-ninja` 时传入 `-DRTX_BUILD_TESTS=ON`，触发 CMake `unused variable` 告警。
- **修复落点（已由专家在云端 `CMakePresets.json` 原地完成）**：
  - `CMakePresets.json` 中 `base` 与 `windows-msvc` 的 `"RTX_BUILD_TESTS": "ON"` 已彻底拔除。

---

## 二、 代码卫生与架构错配全量审计清单（4 大手术刀）

### 手术刀 1：`p010_pack.cu` 死核拔除与 PTX 归口（消除 `1.14 MB` PTX 膨胀）

#### 现状病灶
1. `src/shim/ngx_bridge/p010_pack.cu` 包含 4 个 `__global__` 核函数，但生产环境中**仅有 2 个被调用**：
   - `rtx_yuv_to_rgb10`（行 `224-299`）：**存活**（被 `launchPreprocess` 调用，用于将 NV12/P010 解码帧转为 RGBA16F 喂给 NGX VSR）。
   - `rtx_rgba16f_to_p010_bt2020nc`（行 `301-409`）：**死代码**（Phase 4 初代 Pack v1 核，已被 `v2` 完全取代，`bridge_exports.cpp` 中 0 调用）。
   - `rtx_rgba16f_to_p010_bt2020nc_v2`（行 `411-599`）：**存活**（被 `launchPackV2` 调用，执行 3D LUT + PQ 打包 + MaxCLL/MaxFALL 统计归约 + 预览分屏输出）。
   - `rtx_cuda_remaster_fused_p010`（行 `614-857`）：**死代码**（Phase 5 遗留的 Catmull-Rom + ITU-R BT.2446 软超分核，`launchFusedRemaster` 在全仓 **0 调用**，且因双层循环 `#pragma unroll` 展开了 16 次 `powf` 与 `tex3D`，独占了 `p010_pack.ptx` 近 **29,000 行 / 1.0 MB** 的汇编体积！）。
2. 与此同时，`src/shim/ngx_bridge/p010_pack_runtime.cpp:488-659` 却把 NVOFA 双向光流 Warp 核函数 `rtx_nvof_warp_nv12` 硬编码在 C++ 源码字符串字面量中，破坏了 `.cu -> .ptx -> EmbedPtx.cmake` 的一致性。

#### 施工指令（Task 10A-1）
1. 在 `src/shim/ngx_bridge/p010_pack.cu` 中：
   - 物理删除 `rtx_rgba16f_to_p010_bt2020nc`（行 `301-409`）与 `rtx_cuda_remaster_fused_p010`（行 `601-857`）。
   - 将 `rtx_nvof_warp_nv12`（及 Phase 10 新增的 10-bit `rtx_nvof_warp_p010`）正式写入 `p010_pack.cu`，重新生成 `p010_pack.ptx`（体积控制在 `< 180 KB`）。
2. 在 `src/shim/ngx_bridge/p010_pack_runtime.h` 与 `p010_pack_runtime.cpp` 中：
   - 物理删除 `launchFusedRemaster`、`FusedRemasterLaunch`、`rtx_ngx_pack_launch` 以及行 `488-659` 的内联 PTX 字符串字面量，直接从 `pack.module`（`kP010PackPtx`）加载 `rtx_nvof_warp_nv12` 与 `rtx_nvof_warp_p010`。

---

### 手术刀 2：终结 `rtx_ngx_bridge` 动静双编与 `GetProcAddress` 反射壳（实现真·单文件 `CineRTX.exe`）

#### 现状病灶
1. 在 `CMakeLists.txt:382-446` 中，Windows 构建同时创建了 `add_library(rtx_ngx_bridge SHARED ...)` 和 `add_library(rtx_ngx_bridge_static STATIC ...)`：
   - 8 个重量级源文件（`bridge_exports.cpp`, `rife_engine.cpp`, `rife_builder.cpp`, `d3d12_interop.cpp` 等）在每次 Windows 构建时被 MSVC 重复编译两遍！
   - 更严重的是，`rtx_shim` 早已通过 `RTX_EMBEDDED_SHIM=1` 静态嵌入 `rtx_core`，而 `rtx_ngx_bridge` 在 Windows 上却仍生成一个额外的 `rtx_ngx_bridge.dll`，再由 `src/core/ngx_remaster.cpp`（400 行）通过 `LoadLibraryW("rtx_ngx_bridge.dll")` 和 15 次 `GetProcAddress` 逐个绑定函数指针！
2. 此外，`src/shim/ngx_bridge/bridge_exports.cpp:372-533` 中还留有 7 个早已被废弃的 C ABI 导出函数（`rtx_ngx_bridge_get_textures`, `rtx_ngx_bridge_process`, `rtx_ngx_bridge_get_fence`, `rtx_ngx_bridge_flush`, `rtx_ngx_bridge_reset`, `rtx_ngx_bridge_set_frame_stats`, `rtx_ngx_bridge_selftest`），在 `ngx_remaster.cpp` 中 0 引用。

#### 施工指令（Task 10A-2）
1. **统一静态直链（全平台开启 `RTX_EMBEDDED_NGX_BRIDGE=1`）**：
   - 在 `CMakeLists.txt` 中删除 `add_library(rtx_ngx_bridge SHARED ...)`，仅保留唯一的 `rtx_ngx_bridge_static`（包含 `ngx_d3d12_bridge.cpp` 并链接 `nvsdk_ngx_s.lib`），在 Windows 与 Linux 下统一执行：
     ```cmake
     target_link_libraries(rtx_core PUBLIC rtx_ngx_bridge_static)
     target_compile_definitions(rtx_core PUBLIC RTX_EMBEDDED_NGX_BRIDGE=1 RTX_SHIM_STATIC=1)
     ```
   - 由此，`CineRTX.exe` 彻底摆脱自有的 `rtx_ngx_bridge.dll` 附属文件，编译耗时直接减半！
2. **精简 `src/core/ngx_remaster.cpp` 与 `src/core/nvidia_shim.cpp`**：
   - 拔除 `ngx_remaster.cpp:150-265` 与 `nvidia_shim.cpp` 中所有 `LoadLibraryW` / `GetProcAddress` 反射查找分支，直接内联绑定编译期符号，两文件代码量从 `550` 行精简至 `< 120` 行。
3. **拔除 `bridge_exports.cpp` 中 7 个废弃导出函数**：
   - 物理删除 `rtx_ngx_bridge_get_textures`、`rtx_ngx_bridge_process`、`rtx_ngx_bridge_get_fence`、`rtx_ngx_bridge_flush`、`rtx_ngx_bridge_reset`、`rtx_ngx_bridge_set_frame_stats`、`rtx_ngx_bridge_selftest` 及其在 `include/shim/ngx_bridge_abi.h` 中的声明。

---

### 手术刀 3：抽取公共零依赖文本与路径工具头 `include/rtx/string_utils.h`

#### 现状病灶
- `escapeJson`、`trimAscii`、`toLowerAscii`、`utf8ToWide`、`wideToUtf8`、`pathToUtf8` 在 `src/core/param_schema.cpp`、`src/cli/main.cpp`、`src/gui/rtx_gui.cpp`、`src/gui/workstation_canvas.cpp` 中各自复制粘贴了一份（共 4 份重复实现）。

#### 施工指令（Task 10A-3）
- 新建 `include/rtx/string_utils.h`，以 `constexpr` / `inline` 纯头文件形式提供统一的 UTF-8 $\leftrightarrow$ UTF-16 转换、JSON 转义、ASCII 大小写折叠与空白裁剪，删除其余 4 处的重复实现：

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
#pragma once

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace rtx::str {

[[nodiscard]] constexpr std::string_view trimAscii(std::string_view sv) noexcept {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r' || sv.front() == '\n')) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r' || sv.back() == '\n')) {
        sv.remove_suffix(1);
    }
    return sv;
}

[[nodiscard]] inline std::string toLowerAscii(std::string_view sv) {
    std::string out;
    out.reserve(sv.size());
    for (char c : sv) {
        out.push_back((c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c);
    }
    return out;
}

[[nodiscard]] inline std::string escapeJson(std::string_view input) {
    std::string out;
    out.reserve(input.size() + 8);
    for (unsigned char c : input) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8]{};
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    return out;
}

template <typename IntT = int>
[[nodiscard]] constexpr std::optional<IntT> parseInt(std::string_view sv) noexcept {
    sv = trimAscii(sv);
    if (sv.empty()) return std::nullopt;
    IntT val{};
    const auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val);
    if (ec != std::errc{} || ptr != sv.data() + sv.size()) return std::nullopt;
    return val;
}

#if defined(_WIN32)
[[nodiscard]] inline std::wstring utf8ToWide(std::string_view utf8) {
    if (utf8.empty()) return {};
    const int len = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (len <= 0) return {};
    std::wstring out(static_cast<size_t>(len), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), len);
    return out;
}

[[nodiscard]] inline std::string wideToUtf8(std::wstring_view wide) {
    if (wide.empty()) return {};
    const int len = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(static_cast<size_t>(len), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), len, nullptr, nullptr);
    return out;
}
#endif

} // namespace rtx::str
```

---

## 三、 Track 10A 独立验证门禁（Checklist）

- [ ] **静态扫描门禁**：
  - `rg "RTX_BUILD_TESTS" CMakePresets.json CMakeLists.txt build.bat .github/` 返回 **0 匹配**。
  - `rg "rtx_cuda_remaster_fused_p010|launchFusedRemaster|rtx_rgba16f_to_p010_bt2020nc\b" src/` 返回 **0 匹配**。
  - `rg "rtx_ngx_bridge_get_textures|rtx_ngx_bridge_process|rtx_ngx_bridge_selftest" src/ include/` 返回 **0 匹配**。
- [ ] **构建与体积门禁**：
  - `src/shim/ngx_bridge/p010_pack.ptx` 文件大小 `< 180,000` 字节。
  - `cmake --preset windows-msvc-ninja && cmake --build --preset windows-msvc-ninja` 零告警通过，且 `ctest --preset windows-msvc-ninja --output-on-failure` 10 套单测 100% PASS。
