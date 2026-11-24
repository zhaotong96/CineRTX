# Open-Source Compliance

## 1. Why a boundary exists

The proprietary NVIDIA SDKs are not covered by the GPLv3 system-library
exception. Statically linking `nvcuvid.lib`, `nvEncodeAPI.lib`,
`nvsdk_ngx*.lib`, or `nvinfer.lib` into a GPLv3 program would make the
combined binary subject to GPLv3 distribution terms that NVIDIA's EULAs do not
grant. The project therefore uses two independent mechanisms:

1. a **license exception** that permits runtime plugin loading, and
2. a **pure C ABI shim** that physically removes proprietary symbols from the
   core's build and import table.

Neither mechanism grants rights to NVIDIA's software. Operators must obtain
and comply with NVIDIA's own licenses.

## 2. License map

| Path | SPDX expression | License text |
| --- | --- | --- |
| `include/rtx/`, `src/core/`, `src/io/`, `src/cli/` | `GPL-3.0-or-later WITH Plugin-Exception` | `LICENSE`, `COPYING.EXCEPTION` |
| `src/gui/`, `webui/` | `GPL-3.0-or-later WITH Plugin-Exception` | same |
| `tests/` | same | same |
| `CMakeLists.txt`, `cmake/`, `.github/` | same | same |
| `include/shim/`, `src/shim/` | `MIT OR Apache-2.0` | `LICENSE-MIT`, `LICENSE-APACHE` |

The exception is an additional permission under GPLv3 section 7. It allows a
Covered Work to be conveyed together with Independent Modules (CUDA, NVENC,
TensorRT, NGX) **only when** those modules are loaded at runtime through the
Plugin Interface and are not statically linked. It terminates for any build
that statically links an Independent Module.

### 2.1 FFmpeg static inlining compliance rule

**【FFmpeg 静态内联法务合规准则】** CineRTX 所集成的手术刀级裁剪 FFmpeg 静态库
在配置期严格禁用 `--enable-gpl` 与 `--enable-nonfree`，保持纯正 LGPLv2.1+
授权。静态链接并入 GPL-3.0-or-later WITH Plugin-Exception 的 Zone A 主工程时，
依据 LGPLv2.1 第 3 条合法升级并入主工程 GPLv3 授权域（Covered Work）。编译期
显式禁用 `--disable-ffnvcodec --disable-cuvid --disable-nvenc`，确保仅有
Zone B/C 的独立桥接模块动态加载 NVIDIA 专有驱动。

Operationally:

| Requirement | Where it is enforced |
| --- | --- |
| No `--enable-gpl`, no `--enable-nonfree` | `cmake/RtxFfmpegStatic.cmake` |
| No FFmpeg hardware/NVIDIA integration (`--disable-ffnvcodec --disable-cuvid --disable-nvenc --disable-cuda-llvm`) | same |
| No swscale/avfilter/postproc (LGPL modules the engine never calls) | same |
| Static archives, never DLLs | `cmake/RtxFfmpegStatic.cmake` (450 KB import-stub fuse) |
| Zero `av*.dll` / `sw*.dll` in the output tree | `cmake/AuditImports.cmake` |
| FFmpeg is **not** an Independent Module | `COPYING.EXCEPTION` section 0 |

The consequence for distribution: shipping `CineRTX.exe` conveys the
LGPLv2.1+ FFmpeg object code inside a GPLv3 Covered Work. LGPLv2.1 section 3
permits that relicensing, and the full corresponding source of both the engine
and the trimmed FFmpeg configuration is published, so no additional relinking
right is owed to the recipient.

## 3. Build-time rules

The following are mandatory and audited:

- No NVIDIA header may be included by GPL code:
  `cuda.h`, `cuda_runtime.h`, `nvcuvid.h`, `cuviddec.h`, `nvEncodeAPI.h`,
  `nvinfer.h`, `NvInferRuntime.h`, `nvsdk_ngx*.h`, `nvOpticalFlow*.h`.
- No NVIDIA library may appear on a link line:
  `nvcuda`, `nvcuvid`, `nvEncodeAPI64`, `nvinfer*`, `nvsdk_ngx*`, `npp*`,
  `cudart*`.
- No `#pragma comment(lib, "...")` for an NVIDIA library.
- No `.lib`/`.a` binary blobs in the repository.
- The core and CLI must not list an NVIDIA DLL in their PE/ELF import tables.

The CMake targets reflect this: `CineRTX.exe` links only `rtx_core` (plus
platform system libraries), and `rtx_core` links `rtx_shim_static` — the
static archives contain no NVIDIA code, only runtime
`LoadLibraryExW`/`GetProcAddress` resolution of driver modules — plus the
surgical static FFmpeg archives via `rtx::ffmpeg_static`. At runtime the core
binds the identical C ABI tables and dynamically loads the one component that
contains proprietary code: the SDK-enabled NGX bridge, always a separately
loaded DLL (`rtx_ngx_bridge.dll`), canonical staging at
`build/external/plugins/nvidia_rtx/`, mirrored to `plugins/nvidia_rtx/` beside
`CineRTX.exe`, and installed to the same relative location. It is never linked
into the GPLv3 executable, which remains `CineRTX.exe` alone. Non-Windows
hosts, which have no NGX/D3D12, compile the SDK-free static bridge
(`rtx_ngx_bridge_static`) instead; the test suite links that stub so the same
C ABI is exercised on every platform.

## 4. Runtime rules

`DynLibrary` (MIT/Apache-2.0) implements a narrow, auditable search order:

- deterministic executable directory and `plugins/nvidia_rtx/` subdirectory (zero environment-variable overrides);
- `%SystemRoot%\System32\<name>` for bare driver DLL names;
- `dlopen(path, RTLD_NOW | RTLD_LOCAL)` on POSIX.

It never searches the current working directory or unqualified `PATH`, which
prevents DLL planting. Every component load is recorded with its resolved path,
version, and a human-readable failure note.

Only function pointers are resolved; no proprietary type crosses the ABI. All
device pointers are `uint64_t`, and all handles are opaque.

## 5. Audits

### 5.1 SPDX and boundary audit

```powershell
cmake -P cmake/AuditSpdx.cmake
cmake -P cmake/AuditTypes.cmake
cmake -P cmake/AuditSloc.cmake
cmake -P cmake/AuditPrivacy.cmake
```

Checks:

- every C/C++/CUDA header and source under `include/`, `src/`, and `tests/`
  starts with an SPDX identifier;
- `include/shim/` and `src/shim/` are `MIT OR Apache-2.0`;
- all other source files are `GPL-3.0-or-later WITH Plugin-Exception`;
- no GPL file includes a forbidden NVIDIA header;
- no source file contains a `#pragma comment(lib, ...)` for an NVIDIA library;
- no `.lib`/`.a`/`.dll`/`.so` binaries are tracked under `include/`, `src/`,
  or `tests/`.

Exit code is non-zero on any violation.

### 5.2 Import-table audit

```powershell
cmake -DTARGET_BIN=.vs/build/cmake/windows-msvc-ninja/CineRTX.exe -DMODE=GUI -P cmake/AuditImports.cmake
cmake -DTARGET_BIN=.vs/build/cmake/windows-msvc-ninja/rtx_headless_cli.exe -DMODE=HEADLESS -P cmake/AuditImports.cmake
```

On Windows the script parses the PE import and delay-import directories
directly, so it works without Visual Studio. It falls back to `dumpbin` or
`llvm-readobj` when present. On Linux it uses `readelf -d` / `objdump -p`.

It fails if any of the following appears as a dependency or imported symbol:

```
nvcuda  nvcuvid  nvEncodeAPI  nvsdk_ngx  nvngx  nvinfer  nvrtc  nvapi
```

The same check is applied to `CineRTX.exe` itself, which embeds the shim
archive: it is allowed to load NVIDIA libraries at runtime, not to import them
statically. The separately staged `rtx_ngx_bridge.dll` is audited the same
way.

### 5.3 Release packaging audit

`cmake/PackageRelease.cmake` refuses to create a release if:

- any required artifact is missing;
- the SPDX or import audit fails;
- a SHA-256 manifest cannot be written.

It produces a zip, a `SHA256SUMS.txt`, and a JSON manifest listing every file's
size and hash. This addresses the failure mode where a release shipped an
incomplete deliverable or silently picked up a proprietary static import.

## 6. CI enforcement

`.github/workflows/ci.yml` runs on every push and pull request:

| Job | Compiler | Tests | Audits |
| --- | --- | --- | --- |
| `compliance` | — | — | SPDX headers and GPL/MIT boundary |
| `linux-gcc` | GCC + CMake | CTest | import audit, `--version`/`--help`/`--schema-json`/`--probe --json` CLI smoke |
| `windows-msvc` | MSVC (VS 18 generator) | CTest | import audit, CLI smoke |
| `zig-windows` | Zig 0.13 + Ninja | CTest | import audit, CLI smoke |
| `package` (tags) | — | — | SHA-256 + manifest verification |

A failing audit blocks the build; packaging depends on the three build jobs.

## 7. Mapping to the delivery gate

| Gate item | Evidence |
| --- | --- |
| Core import table contains no NVIDIA DLL | `cmake/AuditImports.cmake`, CI job |
| Missing GPU/SDK reports structured diagnostics | shim probe tests, `--probe --json` (CLI exit `4`/`5` only when the shim itself cannot answer) |
| Every GPL file has an SPDX header | `cmake/AuditSpdx.cmake` |
| `LICENSE` and `COPYING.EXCEPTION` present | repository root, install rule |
| Shim files are MIT/Apache-2.0 | `cmake/AuditSpdx.cmake`, per-file headers |
| No proprietary binaries in the source tree | `cmake/AuditSpdx.cmake` binary scan |

## 8. Third-party components

- **NVIDIA CUDA / Video Codec SDK / TensorRT / RTX Video SDK** — SDK headers,
  import libraries and static stubs are not part of this source repository.
  The runtime components the product uses are deployed as Independent Modules
  (for NGX, `plugins/nvidia_rtx/` beside `CineRTX.exe`) under NVIDIA's terms;
  the engine only loads them at run time.
- **FFmpeg** — statically linked, always. The surgical static archives
  (`libavformat`, `libavcodec`, `libavutil`, `libswresample`) are built by
  `cmake/RtxFfmpegStatic.cmake` without `--enable-gpl` and without
  `--enable-nonfree`, i.e. under **LGPLv2.1-or-later**. LGPLv2.1 section 3
  permits combining them into the GPLv3 Covered Work, so the shipped single
  executable is GPLv3 throughout. There is no configuration in which the engine
  omits the container layer.
- **TensorRT RIFE engine** — an `.engine` file is a build artifact tied to the
  GPU, TensorRT version, and model. It is not distributed and should not be
  committed.
- **nv-codec-headers** — vendored as the `third_party/nv-codec-headers` git
  submodule, MIT licensed, pinned to upstream tag `n13.0.19.1`
  (`Copyright (c) 2016 NVIDIA Corporation`, upstream
  `Permission is hereby granted, free of charge` notice retained verbatim).
  These are declarations only; no NVIDIA binary, import library, or CUDA
  runtime is linked. The thin dynamic-loading wrapper from the former vendor
  tree is original project code and now lives at
  `include/shim/dynlink_nvEncodeAPI.h` (`MIT OR Apache-2.0`), including
  `<ffnvcodec/nvEncodeAPI.h>` from the submodule. `cmake/AuditSpdx.cmake`
  audits the submodule's headers for MIT when the submodule is initialised
  and prints a notice when it is not.

## 9. Known gaps

- `reuse lint` is not installed in the development environment; the project
  uses an equivalent, stricter in-repo script. CI can add `reuse` later without
  changing the headers.
- The D3D12/CUDA interop layer loads `d3d12.dll` and `dxgi.dll` through
  `LoadLibraryExW`, so it carries no static Direct3D import either. The NGX
  path requires the proprietary RTX Video SDK at build time (`nvsdk_ngx_s.lib`,
  supplied automatically by the configure step and never committed) and
  NVIDIA's runtime (`_nvngx.dll`, `nvngx_vsr.dll`, `nvngx_truehdr.dll`) at run
  time; when the runtime is missing the bridge reports `RTX_ERR_NGX` and never
  pretends a texture exists.
- SPDX exception identifiers are not registered with the SPDX License List.
  `Plugin-Exception` is defined by `COPYING.EXCEPTION` in this repository.
  Distributors who require a registered identifier can use
  `GPL-3.0-or-later WITH LicenseRef-Plugin-Exception` or the modern SPDX 3.0
  syntax `GPL-3.0-or-later WITH AdditionRef-Plugin-Exception` and point
  the reference identifier at `COPYING.EXCEPTION`.
