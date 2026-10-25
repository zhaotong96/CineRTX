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
| `include/rtx/` | `GPL-3.0-or-later WITH Plugin-Exception` | `LICENSE`, `COPYING.EXCEPTION` |
| `src/core/`, `src/io/`, `src/cli/` | same | same |
| `tests/` | same | same |
| `CMakeLists.txt`, `cmake/`, `tools/`, `.github/` | same | same |
| `include/shim/` | `MIT OR Apache-2.0` | `LICENSE-MIT`, `LICENSE-APACHE` |
| `src/shim/` | `MIT OR Apache-2.0` | same |

The exception is an additional permission under GPLv3 section 7. It allows a
Covered Work to be conveyed together with Independent Modules (CUDA, NVENC,
NvOFFRUC, TensorRT, NGX) **only when** those modules are loaded at runtime
through the Plugin Interface and are not statically linked. It terminates for
any build that statically links an Independent Module.

## 3. Build-time rules

The following are mandatory and audited:

- No NVIDIA header may be included by GPL code:
  `cuda.h`, `cuda_runtime.h`, `nvcuvid.h`, `cuviddec.h`, `nvEncodeAPI.h`,
  `nvinfer.h`, `NvInferRuntime.h`, `nvsdk_ngx*.h`, `nvOpticalFlow*.h`,
  `NvFRUC.h`, `NvOFFRUC.h`.
- No NVIDIA library may appear on a link line:
  `nvcuda`, `nvcuvid`, `nvEncodeAPI64`, `nvinfer*`, `nvsdk_ngx*`, `NvOFFRUC`,
  `npp*`, `cudart*`.
- No `#pragma comment(lib, "...")` for an NVIDIA library.
- No `.lib`/`.a` binary blobs in the repository.
- The core and CLI must not list an NVIDIA DLL in their PE/ELF import tables.

The CMake targets reflect this: `rtx_core` and `rtx-transcode` link only
`rtx_core`, `rtx_rife_support`, and platform threads. `rtx_shim` and
`rtx_ngx_bridge` are shared libraries and are never link dependencies of the
CLI. The CLI loads `rtx_shim` at runtime. In this source drop the NGX bridge is
loaded directly by the test suite through `DynLibrary`; the SDK-enabled NVIDIA
backend is the intended production loader.

## 4. Runtime rules

`DynLibrary` (MIT/Apache-2.0) implements a narrow, auditable search order:

- explicit path from `RTX_NVIDIA_DIR` or the executable directory;
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
pwsh tools/check_spdx.ps1
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
pwsh tools/audit_imports.ps1 -Binary build/cmake/zig-windows/rtx-transcode.exe
```

On Windows the script parses the PE import and delay-import directories
directly, so it works without Visual Studio. It falls back to `dumpbin` or
`llvm-readobj` when present. On Linux it uses `readelf -d` / `objdump -p`.

It fails if any of the following appears as a dependency or imported symbol:

```
nvcuda  nvcuvid  nvEncodeAPI  nvsdk_ngx  nvngx  NvOFFRUC  nvinfer  nvrtc  nvapi
```

The same check is applied to `rtx_shim.dll` and `rtx_ngx_bridge.dll`: they are
allowed to load NVIDIA libraries at runtime, not to import them statically.

### 5.3 Release packaging audit

`tools/package.ps1` refuses to create a release if:

- any required artifact is missing;
- the SPDX or import audit fails;
- a SHA-256 manifest cannot be written.

It produces a zip, a `SHA256SUMS.txt`, and a JSON manifest listing every file's
size and hash. This addresses the failure mode where a release omitted the
bridge DLL or silently picked up a proprietary static import.

## 6. CI enforcement

`.github/workflows/ci.yml` runs on every push and pull request:

| Job | Compiler | Tests | Audits |
| --- | --- | --- | --- |
| `linux-gcc` | GCC + CMake | CTest | SPDX, `readelf` import audit |
| `windows-msvc` | MSVC 2022 + CMake | CTest | SPDX, PE import audit |
| `zig-windows` | Zig 0.13 + Ninja | CTest | SPDX, PE import audit |
| `package` (tags) | — | — | SHA-256 + manifest verification |

A failing audit blocks the build; packaging depends on all three build jobs.

## 7. Mapping to the delivery gate

| Gate item | Evidence |
| --- | --- |
| Core import table contains no NVIDIA DLL | `tools/audit_imports.ps1`, CI job |
| Missing GPU/SDK degrades gracefully | shim probe tests, `--backend nvidia` exit code 3/4/5 |
| Every GPL file has an SPDX header | `tools/check_spdx.ps1` |
| `LICENSE` and `COPYING.EXCEPTION` present | repository root, install rule |
| Shim files are MIT/Apache-2.0 | `check_spdx.ps1`, per-file headers |
| No proprietary binaries in the source tree | `check_spdx.ps1` binary scan |

## 8. Third-party components

- **NVIDIA CUDA / Video Codec SDK / Optical Flow SDK / TensorRT / RTX Video
  SDK** — not distributed here. Users install them under NVIDIA's terms. The
  engine only loads them at runtime.
- **FFmpeg** — optional. If built with `RTX_WITH_FFMPEG=ON`, the resulting
  binary must comply with the applicable FFmpeg license (LGPL or GPL depending
  on the configuration). The demuxer/muxer implementation is compiled in when
  an FFmpeg prefix is supplied (`RTX_FFMPEG_ROOT`); without one the build still
  succeeds and the I/O layer reports `FfmpegMissing` at runtime.
- **TensorRT RIFE engine** — an `.engine` file is a build artifact tied to the
  GPU, TensorRT version, and model. It is not distributed and should not be
  committed.
- Vendored code is limited to `third_party/open_headers/ffnvcodec/`, the MIT
  licensed nv-codec-headers (`Copyright (c) 2016 NVIDIA Corporation`, upstream
  `Permission is hereby granted, free of charge` notice retained verbatim).
  These are declarations only; no NVIDIA binary, import library, or CUDA
  runtime is linked. `tools/check_spdx.ps1` audits the directory and accepts
  only MIT-compatible headers.

## 9. Known gaps

- `reuse lint` is not installed in the development environment; the project
  uses an equivalent, stricter in-repo script. CI can add `reuse` later without
  changing the headers.
- The D3D12/CUDA interop layer loads `d3d12.dll` and `dxgi.dll` through
  `LoadLibraryExW`, so it carries no static Direct3D import either. The NGX
  evaluation path still requires the RTX Video SDK (`nvsdk_ngx_d.dll`,
  `nvngx_vsr.dll`, `nvngx_truehdr.dll`), which is not distributed here; without
  it the bridge reports `RTX_ERR_NGX` and never pretends a texture exists.
- SPDX exception identifiers are not registered with the SPDX License List.
  `Plugin-Exception` is defined by `COPYING.EXCEPTION` in this repository.
  Distributors who require a registered identifier can use
  `GPL-3.0-or-later WITH LicenseRef-Plugin-Exception` and point
  `LicenseRef-Plugin-Exception` at the same text.

---

## 10. 2026-10-25 P0-1 施工后的合规状态

* **无新增第三方代码**。remaster 链路全部为自有源码;内核 PTX
  (`p010_pack_ptx.h`)仍由 NVRTC 从自有 `.cu` 生成,许可证不变
  (MIT OR Apache-2.0)。
* **桥接 ABI 保持 append-only**:`rtx_ngx_bridge_remaster` 等导出为纯
  追加,`docs/abi.md` §9 记录了契约与双缓冲环深度约束。
* **验收工具不随交付**:`MediaInfo.exe`、`pack_verify.exe`、
  `ngx_eval_verify.exe`、`privws.exe` 仅部署在远端验收目录
  (`rtx_w6` / `rtx_b`),不进入源码树与交付包。
* **专有二进制仍在树外**:RTX Video SDK 的 `nvngx_vsr.dll` /
  `nvngx_truehdr.dll` / `_nvngx.dll` 仅存在于远端
  `rtx_w6\plugins\nvidia_rtx\`;本地 `out/rtx_sdk` 仅供构建,按
  `.gitignore` 排除在交付之外(其 `include/lib` 为 NVIDIA 专有 SDK
  副本,禁止再分发)。
* 复审:`tools/check_spdx.ps1` PASS(GPL=63/MIT=33,二进制 0);
  `tools/audit_imports.ps1` PASS(4 个 PE 无 NVIDIA 静态导入)。
