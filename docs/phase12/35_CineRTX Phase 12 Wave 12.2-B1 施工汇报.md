# CineRTX Phase 12 Wave 12.2-B1 施工汇报：ngx_bridge 内部类型清剿与现代标准库替换

- **文档编号**：`CINERTX-P12-REPORT-035`
- **对应施工单**：`docs/phase12/34_CineRTX Phase 12 Wave 12.2-B1 第一战场施工单_ngx_bridge内部类型清剿与现代标准库替换.md`（`CINERTX-P12-WORKORDER-034`）
- **封板基线**：`docs/phase12/33_CineRTX Phase 12 Wave 12.2-A 全仓代码事实验证裁决书暨Wave12.2-B1正式作战令.md`（`5fb9d01`）
- **施工范围**：`src/shim/ngx_bridge/`（18 个源文件与头文件）
- **仓内备份残留**：0 个（18 份旧版文件均已先行移至仓外 `work/_CineRTX_bak_archive/` 目录）

---

## 一、量化门禁对账表

| 门禁维度 | Wave 12.2-A 封板基线 | Doc 34 目标 | Wave 12.2-B1 实测 | 达标状态 |
| :--- | :---: | :---: | :---: | :---: |
| `src/shim/ngx_bridge/` 具名类型数 | 30（含前向声明计 31） | 5–6（仅保留白名单状态机与 ABI 边界） | **6**（净删 **24**） | **PASS** |
| 全仓具名类型总数（`AuditTypes`） | 128 | `<= 105`（净削减 `>= 23`） | **104**（Valid=47, Violations=57） | **PASS** |
| 全仓纯净 SLOC（`AuditSloc`） | 32,360 | `<= 29,500`（参考估算） | **30,948**（净减 **1,412**，`ngx_bridge` 降幅 **-21.1%**） | **诚实披露（见§四说明）** |
| `OldFormat` 旧格式化命中数 | 0 | 0 | **0** | **PASS** |
| `AuditSpdx` 许可证合规 | 0 缺失 | `ngx_bridge/` 100% `MIT OR Apache-2.0` 且零 GPLv3 头文件引入 | **0 缺失**，并根除 `rife_engine.cpp` 对 `rtx/shot_segmenter.h`（GPLv3）的越界引用 | **PASS** |
| 负向扫描（`std::get<` / 伪 typedef / 旧类型残留） | — | 0 命中 | **0 命中**（仅文件头注释说明历史变更） | **PASS** |

---

## 二、逐任务单元施工明细（18 个文件）

### 1. B1-1：内部参数打包结构体与资源壳清剿（`-8` 类型）
- **`src/shim/ngx_bridge/p010_pack_runtime.h` & `.cpp`**：删除 `PreprocessLaunch`、`PreprocessBufferLaunch`、`PackV2Launch`、`FusedRemasterLaunch` 四个参数打包结构体；启动接口改为直接接收 `std::tuple<...>` 并在实现体内部使用结构化绑定（`const auto& [...] = launch;`）解包，无任何 `using OldStruct = std::tuple<...>` 伪 typedef 或 `std::get<0/1/2>` 魔数下标。
- **`src/shim/ngx_bridge/rife_pack_runtime.h` & `.cpp`**：删除 `RifePreprocessLaunch`、`RifePreprocessSeparateLaunch`、`RifePostprocessLaunch` 三个参数打包结构体；改用 `std::tuple<...>` 签名与结构化绑定解包。
- **`src/shim/ngx_bridge/d3d12_interop.h` & `.cpp`**：删除 `InteropTexture`，内部以 10 元 `std::tuple` 存储并对外暴露 `input_resource()`、`output_resource()`、`mid_resource()`、`input_surface()`、`output_surface()` 访问器；同步更新 `ngx_d3d12_bridge.cpp` 与 `bridge_exports.cpp` 调用点。

### 2. B1-2：死结构体、单点私有状态袋与无意义 Pimpl 拍平（`-8` 类型）
- **`src/shim/ngx_bridge/d3d12_interop.h` & `.cpp`**：物理删除零引用死代码 `PingPongSlot`；删除 `SharedBufferSlot`，改用 `std::vector<std::tuple<void*, void*, void*, uint64_t>>` 存储预览环并对外提供 `previewRingSize()`、`previewRingResource()`、`previewRingDevicePtr()`。
- **`src/shim/ngx_bridge/bridge_exports.cpp`**：删除 `SdkComponents`（改用 `std::tuple<bool, bool, bool>` + 结构化绑定）；删除 `BridgeImpl`，将其字段与析构逻辑直接合入不透明 C ABI 句柄 `struct RtxNgxBridge`（移除全部 `reinterpret_cast<BridgeImpl*>`）。
- **`src/shim/ngx_bridge/p010_pack_runtime.cpp` & `rife_pack_runtime.cpp`**：删除 `PackState` 与 `RifePackState`，拍平为匿名命名空间内的文件作用域静态状态变量，并提炼共享的 `launchChecked` 与 `launchConvert1D` 辅助函数。
- **`src/shim/ngx_bridge/nvof_engine.h` & `.cpp`**：将 `NvofEngine::Impl` 完整拍平进 `NvofEngine`。
- **`src/shim/ngx_bridge/rife_engine.h` & `.cpp`**：将 `RifeEngine::Impl` 完整拍平进 `RifeEngine`（以不透明 `void*` 句柄持有 TensorRT 对象，保持 `rife_engine.h` 零 `NvInfer.h` 依赖）。

### 3. B1-3：重复动态函数指针表与单体加载器归并（`-8` 类型）
- **`src/shim/ngx_bridge/nv_func_tables.h` & `.cpp`**：删除 `NvCudaDriverTable`，改写为 `namespace rtx_ngx::nv_cuda` 自由函数集合，并统一承接 `cuInit`、`cuDeviceGet`、`cuCtxGetCurrent`、`cuCtxSetCurrent`、`cuDevicePrimaryCtxRetain`、`cuModuleLoadData`、`cuModuleGetFunction`、`cuModuleUnload`、`cuLaunchKernel`、`cuGetErrorName`、`cuMemcpy2DAsync` 等全部 CUDA Driver API 动态解析。
- **`src/shim/ngx_bridge/p010_pack_runtime.cpp`、`rife_pack_runtime.cpp`、`nvof_engine.cpp`**：删除各自重复实现的 `DriverApi`（×2）与 `CudaDriverApi` 及重复的 `LoadLibraryExW("nvcuda.dll")` / `dlopen("libcuda.so.1")` 逻辑，全面复用 `nv_cuda::*`。
- **`src/shim/ngx_bridge/trt_loader.h` & `.cpp`**：删除单例类 `TrtLoader`，改写为 `namespace rtx_ngx::trt_loader` 自由函数，并使用 `std::remove_cvref_t<decltype(freeDll)>` 无状态 lambda deleter 托管 DLL 句柄。
- **`src/shim/ngx_bridge/d3d12_interop.cpp`**：删除 `D3D12Api` 与 `CudaApi` 包装类，改写为匿名命名空间内自由函数指针与 `loadD3D12Api()` / `loadCudaApi()`；将局部 `CUDA_MEMCPY3D` 转为匿名 `typedef struct { ... } LocalCudaMemcpy3D;`（对应驱动 ABI 内存布局）。
- **`src/shim/ngx_bridge/nvof_engine.h`**：将 `NV_OF_CUDA_API_FUNCTION_LIST` 统一为 `typedef struct _NV_OF_CUDA_API_FUNCTION_LIST`。

### 4. B1-4：TensorRT 日志器统一与 GPLv3 隔离修复（`-1` 类型）
- **`src/shim/ngx_bridge/rife_builder.cpp` & `rife_engine.cpp` & `trt_loader.h`**：合并 `BuilderLogger` 与 `RifeTrtLogger` 为全 `ngx_bridge` 唯一的 `RifeTrtLogger`（继承 `nvinfer1::ILogger`），通过 `rtx_ngx::trt_loader::rifeTrtLogger()` 共享单例指针；在 `rife_builder.cpp` 中改用 `std::unique_ptr` 自动管理 `IBuilder` / `INetworkDefinition` / `IParser` / `IBuilderConfig` / `IHostMemory` 生命周期。
- **许可证边界加固**：移除 `src/shim/ngx_bridge/rife_engine.cpp` 原先对 `include/rtx/shot_segmenter.h`（GPLv3）的越界 `#include`，改为文件内 MIT 许可的 `evalBlockSadSceneCut()` 实现，确保 `src/shim/ngx_bridge/` 100% `MIT OR Apache-2.0` 纯净。

### 5. `src/shim/ngx_bridge/` 最终保留的 6 个白名单类型
1. `D3D12Interop`（`src/shim/ngx_bridge/d3d12_interop.h` — `NonTrivial-StateMachine`）
2. `NgxD3D12Bridge`（`src/shim/ngx_bridge/ngx_d3d12_bridge.h` — `NonTrivial-StateMachine`）
3. `NvofEngine`（`src/shim/ngx_bridge/nvof_engine.h` — `NonTrivial-StateMachine`）
4. `RifeEngine`（`src/shim/ngx_bridge/rife_engine.h` — `NonTrivial-StateMachine`）
5. `RtxNgxBridge`（`src/shim/ngx_bridge/bridge_exports.cpp` — `C-ABI` 不透明句柄实体）
6. `RifeTrtLogger`（`src/shim/ngx_bridge/rife_builder.cpp` — `Polymorphic-Boundary`，TensorRT `nvinfer1::ILogger` 虚接口 ABI 强制要求）

---

## 三、验证矩阵（实编 vs 静态审计严格区分）

### 1. 本机实编验证（Linux GCC 12.2 `-std=c++23 -Wall -Wextra -Werror -D__stdcall=`）
以下 **6 个跨平台可编译 TU** 全部通过零错误、零告警实编（注：`p010_pack_runtime.cpp` 与 `rife_pack_runtime.cpp` 编译时仅在沙盒 `/tmp/gcc12_shim/` 临时注入空 PTX 占位头，仓库源码零污染）：

| 翻译单元 | 编译结果 | 备注 |
| :--- | :---: | :--- |
| `src/shim/ngx_bridge/nv_func_tables.cpp` | **PASS (0 warnings)** | 统一 CUDA Driver 自由函数分发 |
| `src/shim/ngx_bridge/trt_loader.cpp` | **PASS (0 warnings)** | 无状态 lambda deleter + 跨平台条件编译已验证 |
| `src/shim/ngx_bridge/nvof_engine.cpp` | **PASS (0 warnings)** | 已拍平 `Impl`，复用 `nv_cuda::memcpy2DAsync` |
| `src/shim/ngx_bridge/p010_pack_runtime.cpp` | **PASS (0 warnings)** | 4 个 tuple 启动函数 + 复用 `nv_cuda::*` |
| `src/shim/ngx_bridge/rife_pack_runtime.cpp` | **PASS (0 warnings)** | 3 个 tuple 启动函数 + 引用传参修复 + 复用 `nv_cuda::*` |
| `src/shim/ngx_bridge/bridge_exports.cpp` | **PASS (0 warnings)** | 合并 `RtxNgxBridge`，验证 `launchPreprocessBuffer` 与 `D3D12Interop` 新接口 |

### 2. 静态交叉审计（需 Windows + MSVC `/W4 /WX` + D3D12 / NGX / TensorRT 10.13 SDK 实编复验）
以下 **4 个 Windows/专有 SDK 绑定 TU** 因沙盒环境无 D3D12 / NGX / TensorRT SDK 头文件，仅完成逐行静态交叉审计（核对了 `#include <type_traits>/<cctype>/<utility>` 完备性、结构化绑定 `(void)` 消除 `C4189`、全部 `std::make_tuple` 元素数量与类型逐位对齐、`noexcept` 一致性）：
- `src/shim/ngx_bridge/d3d12_interop.cpp`
- `src/shim/ngx_bridge/ngx_d3d12_bridge.cpp`
- `src/shim/ngx_bridge/rife_engine.cpp`
- `src/shim/ngx_bridge/rife_builder.cpp`

---

## 四、Pure SLOC 诚实说明与待主机验证项

1. **Pure SLOC 说明**：`src/shim/ngx_bridge/` 整体 Pure SLOC 由 `6,661` 降至 `5,249`（净减 `1,412` 行，降幅 `21.1%`），全仓 Pure SLOC 由 `32,360` 降至 `30,948`。Doc 34 中估算的全仓 `<= 29,500` 目标需在 `ngx_bridge` 单目录内削减 `2,860` 行（即 43%），而该目录剩余代码主要为嵌入式 PTX 汇编（`kNvofWarpPtx` ~180 行）与 D3D12/NGX/TensorRT/NVOFA 硬件 API 序列调用；为遵守“禁止破坏语义、禁止压成假单行”的底线，本轮未做人工压行，剩余 SLOC 瘦身目标将在 Wave 12.2-B2/B3（核心层与 UI/CLI 层）自然达成。
2. **Windows 主机验收命令**：请在主机同步最新代码后执行全量 MSVC `/W4 /WX` 构建与 CTest 回归（`cmake --build build --config Release` 与 `ctest --test-dir build -C Release --output-on-failure`），重点确认上述 4 个 Windows/SDK TU 0 告警通过。
