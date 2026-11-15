<!-- SPDX-License-Identifier: MIT -->
# Testing Specification

CineRTX is validated through **two complementary layers**:

1. **Deterministic Mock-Backed Unit & Contract Tests (Layer 1)** — run on every
   pull request across both Windows and Linux without requiring an NVIDIA GPU or
   proprietary SDK installation (`tests/mocks/mock_ngx.cpp`, `tests/mocks/mock_nvof.cpp`).
2. **Real-Hardware GPU & Packaging Verification Gates (Layer 2)** — run on physical
   Windows 11 RTX hosts (`RTX 30 / 40 / 50` series) before tagging a release.

---

## 1. Test Inventory (10 Converged C++26 Suites)

| Test Target / Script | Source File | Layer | GPU Required | Verified Invariants |
|---|---|---|---|---|
| `test_abi_layout` | `tests/test_abi_layout.cpp` | Layer 1 | No | `static_assert` size/offset verification of NVOF (`NV_OF_INIT_PARAMS`, `NV_OF_EXECUTE_INPUT_PARAMS_V2`) and NGX (`NVSDK_NGX_Parameter_V1`, `NVSDK_NGX_Feature_Create_Params`) C ABI structs, plus `rtx::Status` / `rtx::ErrorCode` / `rtx::Rational` value semantics (`std::expected`, `find_preset`, `rescale_pts`). |
| `test_ring_buffer` | `tests/test_ring_buffer.cpp` | Layer 1 | No | `VramRingBuffer` slot allocation, wrap-around indexing, and capacity invariants. |
| `test_ngx_shim` | `tests/test_ngx_shim.cpp` | Layer 1 | No | Dynamic loading of mock NGX provider (`tests/mocks/mock_ngx.cpp`), parameter bag round-trip, VSR (`kSuperResolution`) and TrueHDR (`kTrueHdr`) feature creation/evaluation/release lifecycle, and `ngx_snippet.h` parameter key parity. |
| `test_nvof_shim` | `tests/test_nvof_shim.cpp` | Layer 1 | No | Dynamic loading of mock NVOF provider (`tests/mocks/mock_nvof.cpp`), `NvOFInit` / `NvOFExecuteV2` / `NvOFDestroy` lifecycle, grid size query, and motion-vector buffer registration. |
| `test_pipeline` | `tests/test_pipeline.cpp` | Layer 1 | No | `Orchestrator::run` end-to-end execution against synthetic NV12 frames, stage ordering (`Decode -> VSR -> TrueHDR -> Interpolate -> Encode`), progress callback delivery, cancellation handling, and multi-track audio stream copy (`avcodec_parameters_copy`). |
| `test_c_api` | `tests/test_c_api.cpp` | Layer 1 | No | Pure C ABI (`include/rtx/c_api.h`) lifecycle (`rtx_session_create`, `rtx_session_configure`, `rtx_session_process_frame`, `rtx_session_destroy`), null-pointer guards, and error string retrieval. |
| `test_cli_args` | `tests/test_cli_args.cpp` | Layer 1 | No | CLI option parser (`src/cli/arg_parser.cpp`): `--vsr-scale`, `--hdr` / `--sdr`, `--fps`, `--interp-engine`, `--codec`, `--crf`, `--preset`, `--webui`, `--player`, and invalid-argument rejection. |
| `test_rife` | `tests/test_rife.cpp` | Layer 1/2 | Optional | RIFE v4.25 TensorRT engine builder (`RifeEngineBuilder`), zero-Python C++26 ONNX `/Cast_8` (`to=INT64 -> FLOAT`) sanitizer, dynamic profile (`1x11xHxW`), and `RifeEngine::interpolate` arbitrary-$t$ synthesis. |
| `test_webui_e2e` | `tests/test_webui_e2e.cpp` | Layer 1 | No | Native Win32 Winsock2 loopback HTTP/SSE client + `CreateProcessW` headless Edge/Chrome `--dump-dom` verifier covering all 3 WebUI workspaces (Transcoder / Player / NLE), `/api/probe`, `/api/jobs`, `/api/events`, and `/api/nle/render`. |
| `test_desktop_e2e` | `tests/test_desktop_e2e.cpp` | Layer 2 | Yes (Desktop) | Native Win32 UI automation (`FindWindowExW`, `SendInput`) verifying standalone D3D11 Borderless Quick-Player (`F11` fullscreen, `Space` pause, `Tab` HUD, `Esc` exit). |
| `cmake/AuditSpdx.cmake` | `cmake/AuditSpdx.cmake` | Layer 1 | No | Repository-wide SPDX license header check and banned proprietary string scan (`nvOpticalFlowCommon.h`, `nvhw_optical_flow`, `Copyright (c) NVIDIA`). |
| `cmake/AuditImports.cmake` | `cmake/AuditImports.cmake` | Layer 1 | No | PE import table (`IMAGE_DIRECTORY_ENTRY_IMPORT`) binary parser verifying `CineRTX.exe` and `rtx_core.dll` link only allowed Windows system DLLs (and FFmpeg DLLs when non-static) and zero proprietary NVIDIA SDK `.lib` imports (`nvcuda.dll`, `nvofapi64.dll`, `nvinfer*.dll` must be loaded strictly via `LoadLibraryExW`). |
| `cmake/PackageRelease.cmake` | `cmake/PackageRelease.cmake` | Layer 2 | No | Native CMake release packager (`cmake -P cmake/PackageRelease.cmake`) staging `CineRTX.exe`, `models/rife_v4.25.onnx`, `THIRD_PARTY_NOTICES.txt`, and generating `SHA256SUMS.txt`. |

---

## 2. Running Layer 1 Tests Locally

### Windows (MSVC + Ninja)

```powershell
cmake --preset windows-msvc-ninja
cmake --build --preset windows-msvc-ninja
ctest --preset windows-msvc-ninja --output-on-failure
```

### Windows (Bare-Host via Zig C++ Wrapper)

```powershell
cmake -B .vs/build/cmake/zig -G Ninja -DRTX_BUILD_TESTS=ON -DRTX_USE_STATIC_FFMPEG=OFF
cmake --build .vs/build/cmake/zig
ctest --test-dir .vs/build/cmake/zig --output-on-failure
```

### Linux (GCC 13+ / Clang 17+ Mock Harness)

```bash
cmake --preset linux-gcc-ninja
cmake --build --preset linux-gcc-ninja
ctest --preset linux-gcc-ninja --output-on-failure
```

---

## 3. Running Compliance & Import Audits

### SPDX & Clean-Room Header Audit

```powershell
cmake -DSOURCE_DIR=. -P cmake/AuditSpdx.cmake
```

Expected output:

```text
[AuditSpdx] PASS (checked N files, 0 violations)
```

### PE Import Table Clean-Room Audit (Windows Build)

```powershell
cmake -DBIN_DIR=.vs/build/cmake/windows-msvc-ninja -P cmake/AuditImports.cmake
```

Expected output:

```text
[AuditImports] PASS: All inspected PE binaries exclusively import permitted libraries.
```

---

## 4. Layer 2 Hardware & Release Packaging Verification

On an NVIDIA RTX workstation with R550+ drivers:

```powershell
# 1. Run all C++26 unit, WebUI, and RIFE tests
ctest --preset windows-msvc-ninja --output-on-failure

# 2. Verify clean-room PE import table
cmake -DBIN_DIR=.vs/build/cmake/windows-msvc-ninja -P cmake/AuditImports.cmake

# 3. Stage portable release archive and SHA256SUMS.txt
cmake -DBUILD_DIR=.vs/build/cmake/windows-msvc-ninja -P cmake/PackageRelease.cmake
```
