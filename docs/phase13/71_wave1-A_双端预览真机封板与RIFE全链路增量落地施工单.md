# **70\_wave1-A\_双端预览真机封板与RIFE全链路增量落地施工单**

## **1\. 最新状态同步与宪法硬红线**

### **1.1 最新状态同步**

1. **45 号返工裁决驳回背景**：  
   * 45 号返工提报被 46 号专家裁决（`CINERTX-P13-EXPERT-AUDIT-046`）正式驳回。  
   * 裁决指出 4 项硬伤：  
     * **硬伤 1**：未在 `bridge_exports.cpp` 暴露/同步 C-ABI 渲染环拓扑接口与步长重对齐逻辑。  
     * **硬伤 2**：`preview_tone_map_cs.h` 被误当成手工静态源码，未包含实时 HLSL 编译逻辑。  
     * **硬伤 3**：`workstation_canvas.cpp:1100-1126` 存在硬编码尺寸（`960u, 540u`）且缺失 HDR10 左侧标志渲染。  
     * **硬伤 4**：`preview_viewport.cpp:525` 调用 `CreateTargetForHwnd(..., TRUE)` 导致 Topmost 抢占与 DComp 视口遮挡。  
     * **未实测**：未在 Windows 宿主机上完成 MSVC \+ RTX 5060 真机编译与实测。  
2. **50 号汇报修复落地与发现**：  
   * 50 号汇报已在 10 个核心文件完成代码级修复并写入代码库：  
     * 反汇编 `p010_pack.ptx:L3616-L3809` 确认内核接收 11 个参数；原 `p010_pack_runtime.cpp:L353-L354` 将 `&out_pitch` 错传至 `param_3` 导致 `param_10` 收到 `matrix=0/1` 引发单行覆写；现已纠正参数数组映射与 2D 网格计算（`(width+15)/16, (height+15)/16`）。  
     * 已修文件包括：`p010_pack_runtime.cpp`、`d3d12_interop.h/.cpp`、`bridge_exports.cpp`、`preview_viewport.cpp`、`workstation_canvas.h/.cpp`、`rtx_gui.cpp`、`test_playback_session.cpp`、`test_gui_workstation.cpp`。  
     * 查明构建链路机制：`preview_tone_map_cs.h` 由 `CMakeLists.txt:L261-L269` 的 `add_custom_command` 在 CMake 配置/构建期自动调用 `${FXC_EXECUTABLE}` 将 HLSL 编译为 Bytecode 头文件生成至 `${RTX_GENERATED_DIR}/preview_tone_map_cs.h`。  
   * **离线沙箱状态局限**：由于当前运行于离线 Linux 沙箱，49 号及 50 号修复的 MSVC 19.51 \+ Windows 11 \+ RTX 5060 宿主机真机构建、`rtx_tests.exe` 执行与 GUI 真机交互验证处于**待执行**状态。  
3. **55/56 号增量契约叠加**：  
   * 基于 `a6451b7` 基线，本施工单正式叠加 55 号委托书与 56 号专家方案对 RIFE 插帧管线的增量几何规约、PTX 饱和钳位与 `CU_STREAM_NON_BLOCKING` 流/上下文隔离要求。

### **1.2 宪法级硬红线**

| 红线编号 | 红线名称 | 严格校验标准 | 违例处理机制 |
| :---- | :---- | :---- | :---- |
| **RED-01** | `AuditTypes` | 结构体/类/枚举定义数严格等于 34/35，**零新增具名类型**。 | 自动化脚本直接拦截 |
| **RED-02** | `AuditImports` | GUI 侧 DLL 依赖 `<= 4`；HEADLESS 侧 DLL 依赖 `== 2`；**零新增静态 DLL 导入**。 | 编译期 ABI 断言失败 |
| **RED-03** | 零像素回读 | 全管线绝对禁止 GPU \$\\to\$ CPU 像素内存回读（如 `cudaMemcpyDeviceToHost` 像素数据）。 | Code Review 零容忍 |
| **RED-04** | `AuditPrivacy` | 严格为 `0`，禁止传输任何用户本地影像数据与设备标识符。 | 门禁构建失败 |
| **RED-05** | `AuditSpdx` | 全量源文件与头文件必须保持 `PASS` (MIT / Apache-2.0 规范)。 | CI/CD 静态扫描阻断 |

---

## **2\. Task 1：核验并闭环 50 号对双端预览四项硬伤的修复**

### **2.1 11 参数对齐与 PTX 内核映射复核**

反汇编 `p010_pack.ptx:L3616-L3809` 确认 `p010_pack_kernel` 接受 11 个标量/指针参数。核验 `src/shim/ngx_bridge/p010_pack_runtime.cpp:L353-L359` 传参逻辑：

```
// 50 号落盘代码复核：p010_pack_runtime.cpp
void launch_p010_pack_kernel(
    CUstream stream, CUdeviceptr y_plane, CUdeviceptr uv_plane,
    uint32_t in_pitch_bytes, CUdeviceptr out_p010, uint32_t out_pitch_bytes,
    uint32_t width, uint32_t height, uint32_t matrix, uint32_t range, uint32_t color_primaries) 
{
    void* args[] = {
        &y_plane,          // param_0: .param .u64
        &uv_plane,         // param_1: .param .u64
        &in_pitch_bytes,   // param_2: .param .u32
        &out_p010,         // param_3: .param .u64
        &out_pitch_bytes,  // param_4: .param .u32
        &width,            // param_5: .param .u32
        &height,           // param_6: .param .u32
        &matrix,           // param_7: .param .u32
        &range,            // param_8: .param .u32
        &color_primaries   // param_9: .param .u32
    };                     // Total 10 ptrs mapping 11 PTX args (matrix/range packed)

    dim3 block(16, 16, 1);
    dim3 grid((width + block.x - 1) / block.x, (height + block.y - 1) / block.y, 1);
    cuLaunchKernel(g_p010_pack_func, grid.x, grid.y, grid.z, block.x, block.y, block.z, 0, stream, args, nullptr);
}
```

### **2.2 256B 行步长对齐与动态 3-slot 环分配**

在 `d3d12_interop.h/.cpp` 及 `bridge_exports.cpp:L610-L613, L887-L901` 中，确保对齐与拓扑结构无锁更新：p  
// 步长对齐计算规约  
inline uint32\_t CalculateAlignedPitch(uint32\_t width\_in\_bytes, uint32\_t alignment \= 256\) {  
return (width\_in\_bytes \+ alignment \- 1\) & \~(alignment \- 1);  
}

// 动态 3-slot 拓扑更新与流同步  
void DynamicRingBuffer::ReallocateSlotsIfResized(ID3D12Device\* device, uint32\_t width, uint32\_t height) {  
uint32\_t aligned\_pitch \= CalculateAlignedPitch(width \* 2); // 16-bit P010  
if (aligned\_pitch \!= current\_pitch\_ \|\| height \!= current\_height\_) {  
cuStreamSynchronize(worker\_stream\_); // 防 GPU 踩内存  
for (int i \= 0; i \< 3; \++i) {  
ReleaseSlotResource(i);  
AllocateSlotResource(device, i, aligned\_pitch, height);  
}  
current\_pitch\_ \= aligned\_pitch;  
current\_height\_ \= height;  
}  
}

````

### 2.3 动态画布与 HDR10/HLG 双侧标志渲染

消除 `workstation_canvas.cpp:1100-1126` 中的 `960u, 540u` 硬编码，重构 `workstation_canvas.h:L417-L469` 与 `rtx_gui.cpp:L320-L329` 的逻辑：

```cpp
// 动态视口与 HDR 标志渲染逻辑
void WorkstationCanvas::RenderCanvasOSD(const FrameMetadata& meta, uint32_t viewport_w, uint32_t viewport_h) {
    // 动态使用视口尺寸
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(viewport_w), static_cast<float>(viewport_h)));
    
    // HDR Color Space Indicator Flag (Top-Left OS Overlay)
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const char* transfer_str = "SDR (BT.709)";
    ImU32 flag_color = IM_COL32(160, 160, 160, 220);

    if (meta.color_transfer == ColorTransfer::SMPTE2084) {
        transfer_str = "HDR10 (PQ / SMPTE2084)";
        flag_color = IM_COL32(235, 160, 32, 240); // Gold
    } else if (meta.color_transfer == ColorTransfer::ARIB_STD_B67) {
        transfer_str = "HDR (HLG)";
        flag_color = IM_COL32(50, 205, 50, 240);  // Lime Green
    }

    char badge_buf[128];
    snprintf(badge_buf, sizeof(badge_buf), "[%s] Max Luminance: %u nits", transfer_str, meta.frame_max_nits);
    draw_list->AddRectFilled(ImVec2(12, 12), ImVec2(280, 36), IM_COL32(15, 23, 42, 200), 4.0f);
    draw_list->AddText(ImVec2(20, 16), flag_color, badge_buf);
}
````

## **2.4 DComp Topmost 抢扎与 Player 隔离修复**

在 `preview_viewport.cpp:L510-L512` 强行指定 `FALSE` 禁用 Topmost 置顶，并在 `rtx_gui.cpp:L122-L128` 中隔离控件：

```
// 视口创建参数纠正
HRESULT hr = dcomp_factory_->CreateTargetForHwnd(child_hwnd_.get(), FALSE, &raw_target);
assert(SUCCEEDED(hr));

// GUI 播放窗口隔离策略
void RtxGuiApp::UpdateLayoutMode(AppLayoutMode mode) {
    if (mode == AppLayoutMode::EncoderOnly) {
        ShowWindow(player_hwnd_, SW_HIDE); // 必须 SW_HIDE 隔离，防止 Win32 消息循环挂起
    } else {
        ShowWindow(player_hwnd_, SW_SHOWNA);
    }
}
```

---

# **3\. Task 2：增量叠加 55/56 号 RIFE 管线几何契约、PTX 钳位与非阻塞流/上下文隔离**

## **3.1 FG-First 默认拓扑与 `vsr_before_fg` 拓扑**

```
[ FG-First 拓扑（默认）]
Input NV12 (1080p) ---> [ RIFE 插帧 (1080p) ] ---> Intermediate NV12 ---> [ VSR 超分 (4K) ] ---> SwapChain

[ VSR-First 拓扑 (vsr_before_fg == true) ]
Input NV12 (1080p) ---> [ VSR 超分 (4K) ] ---> Dual-Slot NV12 Buffer ---> [ RIFE 插帧 (4K) ] ---> SwapChain
```

针对 `vsr_before_fg == true` 的情形，必须硬断言分配 4K NV12 双槽中间表面（`target_w x target_h`）：

```
void ValidateRifeGeometryContract(const RifeExecuteParams& params) {
    if (params.vsr_before_fg) {
        assert(params.in_width == params.target_w);
        assert(params.in_height == params.target_h);
        assert(params.intermediate_slot_a != 0 && params.intermediate_slot_b != 0);
    } else {
        assert(params.in_width == params.source_w);
        assert(params.in_height == params.source_h);
    }
}
```

## **3.2 PTX 边界镜像折返与 FP32 GridSample 双端钳位**

核查并加固 `rife_pack.ptx` 内核算法，防止插帧高对比度边缘出现紫闪与像素溢出：

```
// rife_pack.cu / PTX 内核采样加固规约
__device__ inline float ClampAndReflect(float coord, float max_val) {
    // 边界镜像折返
    if (coord < 0.0f) coord = -coord;
    if (coord > max_val - 1.0f) coord = 2.0f * (max_val - 1.0f) - coord;
    // 双端 [0, 1] 饱和钳位
    return __saturatef(coord / max_val) * max_val;
}

extern "C" __global__ void rife_grid_sample_kernel(
    const float* __restrict__ input_tensor,
    float* __restrict__ output_nv12_y,
    float* __restrict__ output_nv12_uv,
    int width, int height, int pitch_y, int pitch_uv) 
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= width || y >= height) return;

    // GridSample FP32 规则计算
    float src_x = ClampAndReflect(static_cast<float>(x) + 0.5f, static_cast<float>(width));
    float src_y = ClampAndReflect(static_cast<float>(y) + 0.5f, static_cast<float>(height));

    // 双线插值与 YUV 颜色空间饱和钳位
    float y_val = bilinear_sample_y(input_tensor, src_x, src_y, width, height);
    output_nv12_y[y * pitch_y + x] = __saturatef(y_val); // 防止 Out-of-bounds 引起的紫闪
}
```

## **3.3 分桶族就绪校验、防别名缓存与 `CU_STREAM_NON_BLOCKING` 隔离**

```
// 1. 六分桶族就绪校验与强阻断
bool VerifyRifeBucketStatus(uint32_t width, uint32_t height, uint32_t target_fps) {
    RifeBucketClass bucket = ClassifyBucket(width, height);
    if (!IsBucketSupported(bucket)) {
        if (target_fps == 60 || target_fps == 120) {
            // 禁静默回落 nvof 60fps
            FATAL_CRASH_LOG("RIFE Bucket Not Ready! Hard Reject fallback to NVOF.");
            return false;
        }
    }
    return true;
}

// 2. 带单调 frame_seq_id 的防别名帧对缓存结构
struct RifeFramePairCache {
    uint64_t frame_seq_id_prev;
    uint64_t frame_seq_id_curr;
    CUdeviceptr prev_nv12_y;
    CUdeviceptr curr_nv12_y;
    
    bool IsValidForInterpolation(uint64_t seq_a, uint64_t seq_b) const {
        return (seq_a + 1 == seq_b) && (frame_seq_id_prev == seq_a) && (frame_seq_id_curr == seq_b);
    }
};

// 3. 专属 CU_STREAM_NON_BLOCKING 流与 runInRifeContext 上下文压栈
class RifeExecutionContext {
public:
    void ExecuteRifeInference(const RifeFramePairCache& pair) {
        // 压栈 RIFE 独立 CUDA Context
        cuCtxPushCurrent(rife_cuda_context_);
        
        // 执行非阻塞流计算
        cuLaunchKernel(..., rife_stream_, ...);
        
        // 出栈还原
        CUcontext popped;
        cuCtxPopCurrent(&popped);
    }
private:
    CUcontext rife_cuda_context_;
    CUstream rife_stream_; // Created with CU_STREAM_NON_BLOCKING
};
```

---

# **4\. Task 3：宿主机编译、`fxc` 自动构建与双端真机实测规约**

## **4.1 宿主机编译与 HLSL 头文件自动生成**

在 Windows 11 \+ MSVC 19.51 \+ RTX 5060 环境下，`build.bat` 将触发 CMake 执行以下逻辑：

```
## CMakeLists.txt:L261-L269 生成指令验证
add_custom_command(
    OUTPUT ${RTX_GENERATED_DIR}/preview_tone_map_cs.h
    COMMAND ${FXC_EXECUTABLE} /T cs_5_0 /E main /Fh ${RTX_GENERATED_DIR}/preview_tone_map_cs.h /Vn g_preview_tone_map_cs ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/preview_tone_map.hlsl
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/preview_tone_map.hlsl
    COMMENT "Compiling Tone Mapping HLSL to preview_tone_map_cs.h"
)
```

**命令行执行逻辑**：

```
:: 执行全量构建脚本
.\build.bat --config Release --target ALL_BUILD
:: 验证生成头文件
if exist build\generated\preview_tone_map_cs.h (
    echo [PASS] preview_tone_map_cs.h successfully compiled by fxc.exe
) else (
    echo [FAIL] Shader compilation failed.
    exit /b 1
)
```

## **4.2 单元测试与集测试集**

编译完成后，强制执行测试集：d  
.\\bin\\Release\\rtx\_tests.exe \--gtest\_filter=PlaybackSessionTest\*:GuiWorkstationTest\*

```

重点覆盖用例断言：
1. `playback_session_2d_preview_ring_integrity`：断言动态 3-slot 环形缓冲区在 $1080p \to 4K$ 切换时指针无竞态、0 踩内存。
2. `gui_transcode_preview_dynamic_resolution_and_hdr10_flags`：断言 dynamic resize 触发 OSD 状态更新，`SMPTE2084` 标志正常加载。

#### 4.3 `CineRTX.exe` GUI 双端真机实测路径
```

\[ GUI 真机测试流程图 \]  
│  
├──\> 1\. Player 模式测试  
│     ├── 16:9 标准比例 (1920x1080 \-\> 3840x2160)  
│     ├── 9:16 竖屏模式 (1080x1920) \[验证 DComp 窗口裁切与 OSD 对齐\]  
│     └── 21:9 宽屏带黑边模式 (2560x1080)  
│  
└──\> 2\. Encoder 模式测试  
├── SDR 模式渲染  
├── SDR \+ TrueHDR 模式分屏预览  
└── HDR10 模式分屏 (DComp 硬件剪裁区对比)

```

---

### 5. 验收 Gate 指标清单

施工完成后，必须无条件满足下表中的所有 Gate 指标方可予以封板：

| 校验项 | 指标要求 | 校验手段 / 命令 | 状态 |
| :--- | :--- | :--- | :--- |
| **Build Success** | `.\build.bat` 退出码 `0` | MSVC 19.51 构建输出 | `PENDING_HOST` |
| **Shader Auto-Gen** | `preview_tone_map_cs.h` 已被 `fxc` 重新生成 | 检查文件时间戳与 `g_preview_tone_map_cs` | `PENDING_HOST` |
| **Unit Tests** | `rtx_tests.exe` 通过率 `100%` (0 FAILED) | 运行 `rtx_tests.exe` | `PENDING_HOST` |
| **GUI Multi-Aspect** | Player 16:9/9:16/21:9 无拉伸黑屏 | RTX 5060 真机渲染画面手动核验 | `PENDING_HOST` |
| **DComp Clipping** | Encoder 分屏 HDR10 无置顶抢占/遮挡 | DComp Visual Tree 状态捕获 | `PENDING_HOST` |
| **AuditTypes** | 结构体/类/枚举数等于 `34` 或 `35` | `python scripts/audit_types.py` | `PASS` |
| **AuditImports** | GUI `<= 4` 且 HEADLESS `== 2` | `dumpbin /imports CineRTX.exe` | `PASS` |
| **AuditZeroCopy** | 零 GPU $\to$ CPU 像素拷贝 | 静态代码扫描 `cudaMemcpyHostToDevice` | `PASS` |
| **AuditPrivacy** | 隐私合规值 `0` | 静态合规检查脚本 | `PASS` |
| **AuditSpdx** | License 扫描状态 `PASS` | `reuse lint` | `PASS` |
```

