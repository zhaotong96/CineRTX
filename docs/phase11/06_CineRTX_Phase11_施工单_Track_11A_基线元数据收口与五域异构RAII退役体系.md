# CineRTX Phase 11 施工单 Track 11A：基线元数据收口与五域异构 RAII 退役体系

- **施工单编号**：`CINERTX-P11-TASK-11A`
- **优先级与依赖**：**P0（无前置依赖，与 Track 11B 同步启动）**
- **承接工程师**：系统内核与 GPU 资源生命周期工程师（Systems & GPU Lifecycle Engineer）
- **母本依据**：[`04_CineRTX Phase 11 冲突评判与方案.md`](./04_CineRTX Phase 11 冲突评判与方案.md) §2.2、§4（Track 11A）、§5（Gate 11A-1/2）
- **独占文件边界**：
  - 修改：`CMakeLists.txt`、`README.md`、`README_zh.md`、`include/rtx/i18n.h`、`src/core/hw_transcode_internal.h`、`src/core/hw_transcode.cpp`、`src/core/hw_transcode_stages.cpp`、`src/gui/preview_viewport.h`、`src/gui/preview_viewport.cpp`
  - 新建：`docs/archive/HISTORY_SUMMARY_P1_P10.md`、`include/rtx/raii_owners.h`、`src/gui/d3d12_raii.h`、`tests/test_raii_retirement.cpp`
  - **严禁触碰**：`include/rtx/param_schema.h`、`src/core/param_schema.cpp`、`src/cli/main.cpp`、`schema/*`（由 Track 11B 独占）

---

## 一、 施工目标与核心痛点

1. **清零 `v0.2.0` 元数据漂移**：修复 [`CMakeLists.txt`](../../CMakeLists.txt) 仍标 `VERSION 0.1.0` 覆盖 `RTX_VERSION_STRING`、[`include/rtx/i18n.h`](https://github.com/zhaotong96/CineRTX/blob/52103a7363c9b431eb4ab8ea8da86007ebe2dd49/include/rtx/i18n.h) 中 `static_assert(kStrIdCount == 125)` 错误信息仍写 `"113 strongly-typed entries"`、以及 `README*.md` 仍停留在 19 参数 / 75 单测 / 独立 `rtx_ngx_bridge.dll` 的漂移。
2. **根治 4 处深层异构资源与异步 GPU 生命周期缺陷**：
   - [`src/gui/preview_viewport.cpp`](../../src/gui/preview_viewport.cpp) 的 `release()`（等待 1000 ms）与 `resizeSwapChain()`（等待 100 ms）在 `WaitForSingleObject` 超时后未校验返回值是否为 `WAIT_OBJECT_0`，直接强拆仍在 GPU in-flight 的后备缓冲与上传堆；
   - [`src/core/hw_transcode_stages.cpp`](../../src/core/hw_transcode_stages.cpp) 析构函数裸持 `anchor_p010_[2]` / `pinned_host_buf_[2]` / `rife_active_`，并在 `cuda_.mem_free_host` 缺失时危险回退 `std::free()`；
   - [`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp) 中 `NvidiaTranscodeSession::~NvidiaTranscodeSession()` 依赖 8 行手写 `.reset()`/`.close()` 维持销毁序，未编码进成员声明拓扑；
   - `PreviewViewport::create()` 充斥裸 COM 指针与局部 `fail` Lambda。

---

## 二、 原子 TDD 施工任务（Atomic Tasks）

### Task 11A-1：版本元数据统一与 Phase 1–10 文档压缩归档
1. 修改 `CMakeLists.txt`：`project(CineRTX VERSION 0.3.0 ...)`。
2. 修改 `include/rtx/i18n.h`：将 `static_assert(kStrIdCount == 125, ...)` 错误信息更新为 `"125 strongly-typed entries"`。
3. 同步更新 `README.md` 与 `README_zh.md`：反映 `v0.3.0`、20 项 SSOT 参数、125 项双语词条、静态内联 `RTX_EMBEDDED_NGX_BRIDGE`、全模态 CQ/QP/CPU CRF、WebUI v3 与 78+ GPU 单测基线。
4. 编写 `docs/archive/HISTORY_SUMMARY_P1_P10.md`：将 Phase 1–10 核心里程碑、存活架构遗产与反模式黑名单提炼为单页权威摘要。

### Task 11A-2：落地五域 move-only 所有权原语（`include/rtx/raii_owners.h` & `src/gui/d3d12_raii.h`）
在不引入重型外部依赖库与额外系统 DLL 的前提下，落地严格 move-only（`= delete` 拷贝构造与拷贝赋值）的轻量所有权封装：

```cpp
// include/rtx/raii_owners.h (SPDX: GPL-3.0-or-later WITH Plugin-Exception)
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <thread>
#include <utility>

namespace rtx {

// 1. Win32 域：UniqueHandle / UniqueModule / UniqueHwnd (仅在 _WIN32 定义)
struct UniqueHandle {
    void* handle = nullptr;
    UniqueHandle() noexcept = default;
    explicit UniqueHandle(void* h) noexcept : handle(h) {}
    ~UniqueHandle() noexcept;
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    UniqueHandle(UniqueHandle&& o) noexcept : handle(std::exchange(o.handle, nullptr)) {}
    UniqueHandle& operator=(UniqueHandle&& o) noexcept;
    explicit operator bool() const noexcept { return handle != nullptr; }
    void reset(void* h = nullptr) noexcept;
};

// 2. CUDA 域：显式绑定 Context/API 引用，严禁 cuMemFreeHost 缺失时退回 std::free()
enum class HostAllocKind : uint8_t { None = 0, CudaPinned = 1, SystemHeap = 2 };

class CudaDeviceBuffer {
public:
    using FreeFn = int (*)(unsigned long long);
    CudaDeviceBuffer() noexcept = default;
    CudaDeviceBuffer(uint64_t ptr, size_t bytes, FreeFn free_fn,
                     std::function<void()> bind_ctx = {}) noexcept;
    ~CudaDeviceBuffer() noexcept;
    CudaDeviceBuffer(const CudaDeviceBuffer&) = delete;
    CudaDeviceBuffer& operator=(const CudaDeviceBuffer&) = delete;
    CudaDeviceBuffer(CudaDeviceBuffer&& o) noexcept;
    CudaDeviceBuffer& operator=(CudaDeviceBuffer&& o) noexcept;
    uint64_t ptr() const noexcept { return ptr_; }
    size_t bytes() const noexcept { return bytes_; }
    void reset() noexcept;
private:
    uint64_t ptr_ = 0;
    size_t bytes_ = 0;
    FreeFn free_fn_ = nullptr;
    std::function<void()> bind_ctx_;
};

class CudaPinnedHostBuffer {
public:
    using FreeHostFn = int (*)(void*);
    // 强制记录分配来源：CudaPinned 仅允许由 cuMemFreeHost 释放，绝不回退 std::free()
    CudaPinnedHostBuffer() noexcept = default;
    static CudaPinnedHostBuffer wrapCudaPinned(void* ptr, size_t bytes, FreeHostFn fn,
                                               std::function<void()> bind_ctx = {}) noexcept;
    static CudaPinnedHostBuffer wrapSystemHeap(void* ptr, size_t bytes) noexcept;
    ~CudaPinnedHostBuffer() noexcept;
    // move-only...
};

// 3. 会话与工作线程域：RifeSession & PipelineWorker
class RifeSession { /* 析构自动调用 destroyRife，声明必晚于 session_ */ };
class PipelineWorker { /* 析构必先置位停止信号 + notify_all() + join()，声明必晚于缓冲区 */ };

} // namespace rtx
```

并在 `src/gui/d3d12_raii.h` 中落地：
- `ComOwner<T>`：极简 COM 拥有者（move-only，析构自动 `Release()`，提供 `put()` / `get()` / `reset()`）；
- `MappedResource`：持有持久 `Map` 指针与对应的 `ComOwner<ID3D12Resource>` 引用，保证仅在 GPU 使用完成后再执行 `Unmap(0, nullptr)`；
- `GpuRetirementQueue`：跟踪每次命令提交绑定的单调递增 `fence_value` 与待退役 COM 对象列表，仅当 `fence->GetCompletedValue() >= entry.fence_value` 时方才释放。

### Task 11A-3：重排 `NvidiaTranscodeSession` 与 `RemasterPipeline` 成员声明拓扑
1. 修改 `src/core/hw_transcode_internal.h` 中 `NvidiaTranscodeSession` 的成员声明顺序（核验真实 deleter 依赖后写明架构注释）：
   ```cpp
   // 构造顺序（依赖先声明） -> 析构顺序严格逆序：
   // muxer_ -> cpu_encoder_ -> encoder_ -> decoder_ -> bridge_ -> demuxer_ -> shim_ -> bridge_loader_
   NgxRemasterBridge bridge_loader_;
   NvidiaShim shim_;
   std::unique_ptr<IDemuxer> demuxer_;
   BridgeHandle bridge_;
   DecoderHandle decoder_;
   EncoderHandle encoder_;
   std::unique_ptr<CpuVideoEncoder> cpu_encoder_;
   std::unique_ptr<IMuxer> muxer_;
   ```
2. 删除 `src/core/hw_transcode.cpp` 中 `NvidiaTranscodeSession::~NvidiaTranscodeSession()` 里的 8 行手写 `.reset()`/`.close()`，改为 `= default`（或仅留非拥有清理日志）。
3. 重构 `src/core/hw_transcode_stages.cpp` 中 `RemasterPipeline`：以 `CudaDeviceBuffer anchor_p010_[2]`、`CudaPinnedHostBuffer pinned_host_buf_[2]`、`RifeSession rife_session_`、`PipelineWorker encode_worker_` 替换全部裸资源，并确保成员顺序满足：`session_` & `cuda_` 先于所有 GPU buffer，buffer 先于 `rife_session_`，`rife_session_` 先于 `encode_worker_`（使析构时工作线程最先停止并 join）。

### Task 11A-4：修复 `PreviewViewport` GPU Fence 超时强拆缺陷
1. 重构 `src/gui/preview_viewport.h/.cpp`，用 `ComOwner<T>`、`UniqueHandle`、`UniqueHwnd`、`MappedResource` 与 `GpuRetirementQueue` 全面替换裸 `void*` 与局部 `fail` Lambda。
2. 改造 `waitForGpuIdle(uint32_t timeout_ms)`：
   - 当 `WaitForSingleObject(fence_event_.handle, timeout_ms)` 返回 `WAIT_OBJECT_0`：安全回收退役池；
   - 当返回 `WAIT_TIMEOUT`：**严禁直接释放后备缓冲或调用 `ResizeBuffers`**！立即调用 `device_->GetDeviceRemovedReason()`：
     - 若返回 `S_OK`（设备健康但仍忙）：将旧资源压入 `GpuRetirementQueue` 延迟退役，取消本次 `ResizeBuffers`（返回 `ErrorCode::Timeout` 并留待下一帧重试）；
     - 若返回非 `S_OK`（Device Lost）：置位 `device_lost_ = true`，走受控设备丢失重建路径。

### Task 11A-5：编写 RAII 失败注入与 GPU 退役专项单测（`tests/test_raii_retirement.cpp`）
覆盖：
1. 100 次连续创建/销毁循环与每步构造失败注入，通过测试 deleter 计数器断言**每个资源释放次数恒等于 1（零泄漏、零重复释放）**；
2. 模拟 `WAIT_TIMEOUT`（Fence 未达标）触发 `resizeSwapChain` / `release`，验证资源被保留在延迟退役池中不被提前释放，直至模拟 Fence 推进后才精确释放；
3. 模拟 `DXGI_ERROR_DEVICE_REMOVED` 验证可控 Device-Lost 恢复；
4. 验证 `PipelineWorker` 析构必先于 buffer 析构（通过析构序事件日志断言）。

---

## 三、 量化验收门禁 Checklist（Gate 11A-1 & Gate 11A-2）

- [ ] **静态扫描（零危险兜底与零裸销毁）**：
  - `rg "std::free\(pinned_host_buf" src/` 返回 0 匹配；
  - `rg "safeRelease\(" src/gui/preview_viewport.cpp` 返回 0 匹配；
  - `rg "WAIT_TIMEOUT|GetDeviceRemovedReason" src/gui/preview_viewport.cpp` 存在完整超时分流与设备丢失检测。
- [ ] **版本与文档一致性**：
  - `CineRTX.exe --version` 输出 `0.3.0`；
  - `rg "113 strongly-typed" include/rtx/i18n.h` 返回 0 匹配；
  - `docs/archive/HISTORY_SUMMARY_P1_P10.md` 已生成且覆盖 Phase 1–10。
- [ ] **合规门禁**：
  - `cmake -P cmake/AuditSpdx.cmake` ExitCode `0`；
  - `cmake -DBUILD_DIR=.vs/build/cmake/windows-msvc-ninja -P cmake/AuditImports.cmake` ExitCode `0`（体积在 `1.2–12.0 MB` 内）。
- [ ] **单元测试**：
  - `cmd /c build.bat` ExitCode `0`；
  - `rtx_tests.exe` 原有 78 项 + 新增 `test_raii_retirement` 全部 100% PASS，无 `[SKIP-GPU]`。
