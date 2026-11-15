# CineRTX Phase 9.1：专家直通代建完成暨 Git 索引同步与封板推送指引

- **文档编号**：`CINERTX-P9.1-DIRECT-EXECUTION-HANDOFF-008`
- **关联文档**：
  - [`docs/phase9/CineRTX_Phase9.1_推送前仓库卫生完善与终审封板专家委托书.md`](CineRTX_Phase9.1_推送前仓库卫生完善与终审封板专家委托书.md)（`CINERTX-P9.1-DELEGATION-REPO-HYGIENE-005`）
  - [`docs/phase9/CineRTX_Phase9.1_推送前仓库卫生完善与CMake全原生闭环专家裁决暨SuperPower施工规范.md`](CineRTX_Phase9.1_推送前仓库卫生完善与CMake全原生闭环专家裁决暨SuperPower施工规范.md)（`CINERTX-P9.1-REPO-HYGIENE-SUPERPOWER-006`）
  - [`docs/phase9/CineRTX_Phase9.1_全栈正式发布与开源交付大典.md`](CineRTX_Phase9.1_全栈正式发布与开源交付大典.md)（`CINERTX-P9.1-RELEASE-MANIFESTO-007`）

## 一、 领域专家已在仓库内直接代建完成的变更清单（100% 已落盘）

### 1.1 物理移除的冗余目录与碎片文件（共 3 个目录 + 2 个碎片单测）
1. **`tools/` 目录（含全部 18 个 `.py` / `.ps1` / `.sh` / `.bat` 脚本）**：已从仓库根目录物理移出，全仓外部构建脚本清零（仅保留根目录唯一构建入口 `build.bat`）。
2. **`tests/webui/` 目录（含 5 个 `.mjs` / `.py` / `.bat` 脚本）**：已从 `tests/` 物理移出。
3. **`tests/unit/` 目录（含硬编码本地私有 `.mp4` 路径的 `test_audio_stream_copy.cpp`）**：已从 `tests/` 物理移出，其几何断言与音视频流拷贝断言已作为纯内存零外部依赖测试迁入 `tests/test_types.cpp` 与 `tests/test_io.cpp`。
4. **`tests/test_status.cpp` 与 `tests/test_rational.cpp`**：已分别无损合并至 `tests/test_types.cpp` 与 `tests/test_flicks_timebase.cpp` 并从 `tests/` 移除，使 `tests/` 收敛为 **10 个高内聚纯 C++26 测试套件 + `test_main.cpp`**。

### 1.2 新建的 4 个原生 CMake 模块（位于 `cmake/`）
1. **`cmake/EmbedPtx.cmake`**：纯 CMake 原生实现 `.ptx` $\to$ `p010_pack_ptx.h` / `rife_pack_ptx.h`（`namespace rtx_ngx`），彻底移除 `CMakeLists.txt` 对 `find_package(Python3 REQUIRED)` 与 `tools/embed_ptx.py` 的依赖。
2. **`cmake/AuditSpdx.cmake`**：纯 CMake 原生实现全仓 SPDX 许可证头、`GPL-3.0-or-later WITH Plugin-Exception` 与 `MIT OR Apache-2.0` 边界、二进制/模型混入检查及 `tools/` 禁令审计（替代 `tools/check_spdx.ps1`）。
3. **`cmake/AuditImports.cmake`**：纯 CMake 原生实现 `CineRTX.exe` 体积包络（`1.2 MB ~ 12.0 MB`）与动态 DLL 零泄漏门禁（替代 `tools/audit_imports.ps1`）。
4. **`cmake/PackageRelease.cmake`**：纯 CMake 原生实现便携版 ZIP 打包、`SHA256SUMS.txt` 与 `manifest.json` 生成（替代 `tools/package.ps1` 与 `tools/package_release.py`）。

### 1.3 直接更新的 13 个核心工程文件
1. **`CMakeLists.txt`**：
   - 切换至 `cmake/EmbedPtx.cmake`（零 Python 依赖）；
   - 新增 Windows 构建期 RIFE v4.25 ONNX 权重自动供给（自动从本地 `models/rife_v4.25.onnx` 或 HuggingFace 源站缓存至 `.vs/build/external/models/rife_v4.25.onnx`，并在 `POST_BUILD` 自动同步 `rife_v4.25.onnx` 与已编译的 `rife_v4.25.engine` 至 `<exe_dir>/models/`）；
   - 更新 `RTX_TEST_SOURCES` 为收敛后的 10 个 C++26 测试套件。
2. **`src/shim/ngx_bridge/rife_builder.cpp`**：
   - 内置纯 C++26 零依赖 ONNX Protobuf 动态时间步补丁函数 `ensureRifeOnnxDynamicTimestepPatched()`，在 `parser->parseFromFile` 前自动识别并修复上游 `RIFE_fp32.onnx` 中被静态折叠为 `0.5` 的 `/Cast_8` 节点（注入 `/Slice_time` + `/Pad_time` 切取第 7 通道时间步 $t$），彻底摆脱 Python `import onnx`。
3. **`src/shim/ngx_bridge/rife_engine.cpp`**：
   - 修复构建目录迁入 `.vs/build/cmake/windows-msvc-ninja`（4 层深度）后原先 3 层 `.parent_path()` 无法触达仓库根目录 `models/` 与 `.vs/build/external/models` 的路径死角，改为向上 6 层自适应遍历探测；
   - 首启自动从 ONNX 编译生成 `models/rife_v4.25.engine` 后，自动将 `.engine` 同步回写缓存至 `.vs/build/external/models/rife_v4.25.engine`，确保后续 clean build 瞬间复用已编译引擎。
4. **`cmake/RtxFfmpegStatic.cmake`、`cmake/RtxBootstrap.cmake`、`cmake/toolchain-zig.cmake`、`build.bat`**：
   - 将 FFmpeg 静态编译脚本直接内联由 `RtxFfmpegStatic.cmake` 在 `.vs/build/external/ffmpeg-build/` 动态生成，并将全部外部缓存路径统一对齐至 `.vs/build/external`。
5. **`.gitignore` 与 `.github/workflows/ci.yml`**：
   - `.gitignore` 新增 `/Optical_Flow_SDK*`、`/RTX_Video_SDK*`、`/TensorRT*` 及 `/models/*.onnx`、`/models/*.engine` 忽略规则；
   - `ci.yml` 全面切换至 `cmake -P cmake/AuditSpdx.cmake`、`cmake -P cmake/AuditImports.cmake` 与 `cmake -P cmake/PackageRelease.cmake`。
6. **`tests/test_types.cpp`、`tests/test_flicks_timebase.cpp`、`tests/test_io.cpp`、`tests/test_shim.cpp`**：
   - 完成测试合并，并在 `test_io.cpp` 新增纯内存音视频流拷贝往返测试 `io_memory_audio_video_stream_copy_roundtrip`，在 `test_shim.cpp` 新增纯 C++26 PE 导入表解析测试 `shim_pe_static_import_table_zero_leak_gate`。

## 二、 移交项目负责人执行的残余必要项（仅需本地实机验证与 Git 提交推送）

由于沙箱无宿主机 GPU 与 Git CLI 写索引环境，请项目负责人在本地 PowerShell 终端依次执行以下 4 条指令完成最终验证与推送：

```powershell
# 1. 清理宿主机工作区若残留的未跟踪 Optical_Flow_SDK_5.0.7.zip
Remove-Item -Force Optical_Flow_SDK_5.0.7.zip -ErrorAction SilentlyContinue

# 2. 运行原生 CMake SPDX 与仓库卫生门禁
cmake -P cmake/AuditSpdx.cmake

# 3. 重新配置、编译并运行全量 C++26 单元测试 + 导入表门禁
cmake --preset windows-msvc-ninja
cmake --build --preset windows-msvc-ninja
ctest --preset windows-msvc-ninja --output-on-failure
cmake -DBUILD_DIR=.vs/build/cmake/windows-msvc-ninja -P cmake/AuditImports.cmake

# 4. 同步 Git 索引（记录 tools/、tests/webui/、tests/unit/ 移除及新增 cmake/*.cmake）并推送 v0.2.0
git add -A
git status --short
git commit -m "chore(phase9.1): eliminate tools/ scripts via native CMake, converge C++26 tests, and add zero-Python RIFE ONNX auto-provisioning"
git tag -a v0.2.0 -m "CineRTX v0.2.0 Gold Master"
git push origin main --tags
```
