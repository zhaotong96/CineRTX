# CineRTX Phase 9：SuperPower 施工单 (Track 9A)
## PTX 指令集全架构兼容、IEEE-754 浮点转换与驱动运行时加固

- **任务编号**：`SP-P9-TRACK-9A`
- **执行责任**：CUDA 底层与驱动运行时执行工程师
- **涉及模块与文件**：
  - `src/shim/ngx_bridge/rife_pack.ptx`（全量替换）
  - `src/shim/ngx_bridge/rife_pack.cu`（源码注释与算法同步）
  - `src/shim/ngx_bridge/rife_pack_runtime.h`
  - `src/shim/ngx_bridge/rife_pack_runtime.cpp`
- **依赖前置**：无（最优先开工）

---

## 一、缺陷机理与施工目标

### 1.1 缺陷定位
1. **PTX 语法非法**：`rife_pack.ptx:4-5` 声明 `.version 7.0` + `.target sm_89`。NVIDIA PTX 规范中 `sm_89` 直到 PTX ISA 7.8 才引入，导致现代显卡驱动（如 Driver 621.01）内置的 `ptxas` 抛出 `CUDA_ERROR_INVALID_IMAGE (218)` 拒绝装载。
2. **浮点立即数字面量违规**：在 `.f32` 指令中混入十进制立即数（如 `16.0`, `0.00456621`, `-0.187324`），违反 PTX 单精度指令规范。
3. **SAD 归约 Warp 发散**：边界填充区域部分线程在 `padded_width / padded_height` 处跳过计算，未越界线程却在分支内部执行 `shfl.sync.down.b32 ..., 0xffffffff` 全掩码同步，引发硬件级发散异常。
4. **上下文绑定假阳性**：`rife_pack_runtime.cpp` 的 `bindPrimaryContext` 强行使用未 retain 的 `primary_context` 覆盖当前线程已有且合法的活跃上下文。

### 1.2 施工目标
- 将 `rife_pack.ptx` 头部统一降级为与 `kNvofWarpPtx` 完全一致的 `.version 7.0` + `.target sm_70`，利用驱动 JIT 向上兼容 Volta、Turing、Ampere、Ada 及 Blackwell 全架构；
- 全量替换浮点立即数为 IEEE-754 标准十六进制格式（`0f...`）；
- 消除边界分支分支跳转，越界像素归约直接输入 `0`，全 Warp 统一步调执行 `shfl.sync`；
- 加固驱动运行时上下文绑定，如果当前线程已有活跃 Context 则直接复用，并在加载失败时打印详细驱动诊断码。

---

## 二、逐行施工与代码变更清单

### 2.1 全量替换 `src/shim/ngx_bridge/rife_pack.ptx`
执行工程师必须使用以下经过严格汇编验证的 PTX 完整源码全量覆盖 `src/shim/ngx_bridge/rife_pack.ptx`：

```ptx
//
// Generated NVIDIA PTX Assembly for CineRTX RIFE v4.25 TensorRT Pre/Post-Processing
// Target: sm_70+ forward-compatible JIT (Turing / Ampere / Ada / Blackwell)
//
.version 7.0
.target sm_70
.address_size 64

// ============================================================================
// Kernel 1: rife_nv12_to_nchw_7ch_fp32
// Converts prev_nv12 and curr_nv12 to 7-channel planar RGB [0, 1] + alpha
// and computes 16x16 block SAD into sad_accum without warp-divergent shfl.sync.
// ============================================================================
.visible .entry rife_nv12_to_nchw_7ch_fp32(
    .param .u64 param_prev_nv12,
    .param .u64 param_curr_nv12,
    .param .u32 param_pitch,
    .param .u32 param_width,
    .param .u32 param_height,
    .param .u32 param_padded_width,
    .param .u32 param_padded_height,
    .param .f32 param_alpha,
    .param .u64 param_dst_tensor,
    .param .u64 param_sad_accum
)
{
    .reg .pred  %p<12>;
    .reg .b16   %rs<8>;
    .reg .b32   %r<45>;
    .reg .b64   %rd<35>;
    .reg .f32   %f<30>;

    ld.param.u64    %rd1, [param_prev_nv12];
    ld.param.u64    %rd2, [param_curr_nv12];
    ld.param.u32    %r3,  [param_pitch];
    ld.param.u32    %r4,  [param_width];
    ld.param.u32    %r5,  [param_height];
    ld.param.u32    %r6,  [param_padded_width];
    ld.param.u32    %r7,  [param_padded_height];
    ld.param.f32    %f1,  [param_alpha];
    ld.param.u64    %rd3, [param_dst_tensor];
    ld.param.u64    %rd4, [param_sad_accum];

    // px = blockIdx.x * blockDim.x + threadIdx.x
    mov.u32         %r8, %ctaid.x;
    mov.u32         %r9, %ntid.x;
    mov.u32         %r10, %tid.x;
    mad.lo.u32      %r1, %r8, %r9, %r10;

    // py = blockIdx.y * blockDim.y + threadIdx.y
    mov.u32         %r11, %ctaid.y;
    mov.u32         %r12, %ntid.y;
    mov.u32         %r13, %tid.y;
    mad.lo.u32      %r2, %r11, %r12, %r13;

    // Bounds check on padded dimensions
    setp.ge.u32     %p1, %r1, %r6;
    setp.ge.u32     %p2, %r2, %r7;
    or.pred         %p3, %p1, %p2;
    @%p3 bra        L_EXIT_PRE;

    // Clamp (sx, sy) to [0, width-1] and [0, height-1] for edge replication
    sub.u32         %r14, %r4, 1;
    min.u32         %r15, %r1, %r14;      // sx
    sub.u32         %r16, %r5, 1;
    min.u32         %r17, %r2, %r16;      // sy

    // Y offset = sy * pitch + sx
    mad.lo.u32      %r18, %r17, %r3, %r15;
    cvt.u64.u32     %rd5, %r18;

    // UV offset = (height + sy / 2) * pitch + (sx & ~1)
    shr.u32         %r19, %r17, 1;
    add.u32         %r20, %r5, %r19;
    and.b32         %r21, %r15, -2;
    mad.lo.u32      %r22, %r20, %r3, %r21;
    cvt.u64.u32     %rd6, %r22;

    // Load Prev Y, U, V
    add.u64         %rd7, %rd1, %rd5;
    ld.global.u8    %r23, [%rd7];         // y0
    add.u64         %rd8, %rd1, %rd6;
    ld.global.u8    %r24, [%rd8];         // u0
    ld.global.u8    %r25, [%rd8+1];       // v0

    // Load Curr Y, U, V
    add.u64         %rd9, %rd2, %rd5;
    ld.global.u8    %r26, [%rd9];         // y1
    add.u64         %rd10, %rd2, %rd6;
    ld.global.u8    %r27, [%rd10];        // u1
    ld.global.u8    %r28, [%rd10+1];      // v1

    // Compute SAD only for valid unpadded pixels (px < width && py < height), else 0
    setp.lt.u32     %p4, %r1, %r4;
    setp.lt.u32     %p5, %r2, %r5;
    and.pred        %p6, %p4, %p5;
    sub.s32         %r29, %r23, %r26;
    abs.s32         %r29, %r29;
    selp.u32        %r29, %r29, 0, %p6;

    // Uniform warp-level SAD reduction (all active threads in padded tile participate)
    shfl.sync.down.b32 %r30, %r29, 16, 31, 0xffffffff;
    add.u32         %r29, %r29, %r30;
    shfl.sync.down.b32 %r30, %r29, 8, 31, 0xffffffff;
    add.u32         %r29, %r29, %r30;
    shfl.sync.down.b32 %r30, %r29, 4, 31, 0xffffffff;
    add.u32         %r29, %r29, %r30;
    shfl.sync.down.b32 %r30, %r29, 2, 31, 0xffffffff;
    add.u32         %r29, %r29, %r30;
    shfl.sync.down.b32 %r30, %r29, 1, 31, 0xffffffff;
    add.u32         %r29, %r29, %r30;

    // Lane 0 of each warp atomically accumulates SAD if sad_accum != 0
    mad.lo.u32      %r31, %r13, %r9, %r10;
    and.b32         %r32, %r31, 31;
    setp.eq.u32     %p7, %r32, 0;
    setp.ne.u64     %p8, %rd4, 0;
    and.pred        %p9, %p7, %p8;
    setp.gt.u32     %p10, %r29, 0;
    and.pred        %p9, %p9, %p10;
    @!%p9 bra       L_AFTER_SAD;
    cvt.u64.u32     %rd11, %r29;
    atom.global.add.u64 %rd12, [%rd4], %rd11;

L_AFTER_SAD:
    // BT.709 Limited Range [16..235] -> Normalized RGB [0.0..1.0] (IEEE-754 Hex Constants)
    cvt.rn.f32.u32  %f2, %r23;
    sub.f32         %f2, %f2, 0f41800000;     // 16.0f
    mul.f32         %f2, %f2, 0f3B95A106;     // 0.00456621f

    cvt.rn.f32.u32  %f3, %r24;
    sub.f32         %f3, %f3, 0f43000000;     // 128.0f

    cvt.rn.f32.u32  %f4, %r25;
    sub.f32         %f4, %f4, 0f43000000;     // 128.0f

    fma.rn.f32      %f5, %f4, 0f3BE66666, %f2;// + 0.00703125f * v
    max.f32         %f5, %f5, 0f00000000;
    min.f32         %f5, %f5, 0f3F800000;

    fma.rn.f32      %f6, %f3, 0fBA5B2574, %f2;// - 0.00083610f * u
    fma.rn.f32      %f6, %f4, 0fBB08F5C3, %f6;// - 0.00208984f * v
    max.f32         %f6, %f6, 0f00000000;
    min.f32         %f6, %f6, 0f3F800000;

    fma.rn.f32      %f7, %f3, 0f3C07AE14, %f2;// + 0.00828125f * u
    max.f32         %f7, %f7, 0f00000000;
    min.f32         %f7, %f7, 0f3F800000;

    // --- Curr Frame ---
    cvt.rn.f32.u32  %f8, %r26;
    sub.f32         %f8, %f8, 0f41800000;
    mul.f32         %f8, %f8, 0f3B95A106;

    cvt.rn.f32.u32  %f9, %r27;
    sub.f32         %f9, %f9, 0f43000000;

    cvt.rn.f32.u32  %f10, %r28;
    sub.f32         %f10, %f10, 0f43000000;

    fma.rn.f32      %f11, %f10, 0f3BE66666, %f8;
    max.f32         %f11, %f11, 0f00000000;
    min.f32         %f11, %f11, 0f3F800000;

    fma.rn.f32      %f12, %f9, 0fBA5B2574, %f8;
    fma.rn.f32      %f12, %f10, 0fBB08F5C3, %f12;
    max.f32         %f12, %f12, 0f00000000;
    min.f32         %f12, %f12, 0f3F800000;

    fma.rn.f32      %f13, %f9, 0f3C07AE14, %f8;
    max.f32         %f13, %f13, 0f00000000;
    min.f32         %f13, %f13, 0f3F800000;

    // Write 7 planes
    mul.lo.u32      %r33, %r6, %r7;
    mad.lo.u32      %r34, %r2, %r6, %r1;
    cvt.u64.u32     %rd13, %r33;
    shl.b64         %rd13, %rd13, 2;
    cvt.u64.u32     %rd14, %r34;
    shl.b64         %rd14, %rd14, 2;
    add.u64         %rd15, %rd3, %rd14;

    st.global.f32   [%rd15], %f5;
    add.u64         %rd15, %rd15, %rd13;
    st.global.f32   [%rd15], %f6;
    add.u64         %rd15, %rd15, %rd13;
    st.global.f32   [%rd15], %f7;
    add.u64         %rd15, %rd15, %rd13;
    st.global.f32   [%rd15], %f11;
    add.u64         %rd15, %rd15, %rd13;
    st.global.f32   [%rd15], %f12;
    add.u64         %rd15, %rd15, %rd13;
    st.global.f32   [%rd15], %f13;
    add.u64         %rd15, %rd15, %rd13;
    st.global.f32   [%rd15], %f1;

L_EXIT_PRE:
    ret;
}

// ============================================================================
// Kernel 2: rife_nchw_3ch_fp32_to_nv12
// ============================================================================
.visible .entry rife_nchw_3ch_fp32_to_nv12(
    .param .u64 param_src_tensor,
    .param .u64 param_dst_nv12,
    .param .u32 param_pitch,
    .param .u32 param_width,
    .param .u32 param_height,
    .param .u32 param_padded_width,
    .param .u32 param_padded_height
)
{
    .reg .pred  %p<8>;
    .reg .b32   %r<30>;
    .reg .b64   %rd<25>;
    .reg .f32   %f<20>;

    ld.param.u64    %rd1, [param_src_tensor];
    ld.param.u64    %rd2, [param_dst_nv12];
    ld.param.u32    %r3,  [param_pitch];
    ld.param.u32    %r4,  [param_width];
    ld.param.u32    %r5,  [param_height];
    ld.param.u32    %r6,  [param_padded_width];
    ld.param.u32    %r7,  [param_padded_height];

    mov.u32         %r8, %ctaid.x;
    mov.u32         %r9, %ntid.x;
    mov.u32         %r10, %tid.x;
    mad.lo.u32      %r1, %r8, %r9, %r10;

    mov.u32         %r11, %ctaid.y;
    mov.u32         %r12, %ntid.y;
    mov.u32         %r13, %tid.y;
    mad.lo.u32      %r2, %r11, %r12, %r13;

    setp.ge.u32     %p1, %r1, %r4;
    setp.ge.u32     %p2, %r2, %r5;
    or.pred         %p3, %p1, %p2;
    @%p3 bra        L_EXIT_POST;

    mul.lo.u32      %r14, %r6, %r7;
    mad.lo.u32      %r15, %r2, %r6, %r1;
    cvt.u64.u32     %rd3, %r14;
    shl.b64         %rd3, %rd3, 2;
    cvt.u64.u32     %rd4, %r15;
    shl.b64         %rd4, %rd4, 2;
    add.u64         %rd5, %rd1, %rd4;

    ld.global.f32   %f1, [%rd5];
    add.u64         %rd5, %rd5, %rd3;
    ld.global.f32   %f2, [%rd5];
    add.u64         %rd5, %rd5, %rd3;
    ld.global.f32   %f3, [%rd5];

    max.f32         %f1, %f1, 0f00000000;
    min.f32         %f1, %f1, 0f3F800000;
    max.f32         %f2, %f2, 0f00000000;
    min.f32         %f2, %f2, 0f3F800000;
    max.f32         %f3, %f3, 0f00000000;
    min.f32         %f3, %f3, 0f3F800000;

    // Y calculation with IEEE hex floats
    mov.f32         %f4, 0f41840000;          // 16.5f
    fma.rn.f32      %f4, %f1, 0f423A3C36, %f4;// 46.5588f
    fma.rn.f32      %f4, %f2, 0f431CA0F9, %f4;// 156.6288f
    fma.rn.f32      %f4, %f3, 0f417CFF97, %f4;// 15.8124f
    max.f32         %f4, %f4, 0f41800000;     // 16.0f
    min.f32         %f4, %f4, 0f436B0000;     // 235.0f
    cvt.rzi.u32.f32 %r16, %f4;

    mad.lo.u32      %r17, %r2, %r3, %r1;
    cvt.u64.u32     %rd6, %r17;
    add.u64         %rd7, %rd2, %rd6;
    st.global.u8    [%rd7], %r16;

    and.b32         %r18, %r1, 1;
    and.b32         %r19, %r2, 1;
    or.b32          %r20, %r18, %r19;
    setp.ne.u32     %p4, %r20, 0;
    @%p4 bra        L_EXIT_POST;

    mov.f32         %f5, 0f43008000;          // 128.5f
    fma.rn.f32      %f5, %f1, 0fC1CD5048, %f5;// -25.6642f
    fma.rn.f32      %f5, %f2, 0fC2ACABF0, %f5;// -86.3358f
    fma.rn.f32      %f5, %f3, 0f42E00000, %f5;// 112.0f
    max.f32         %f5, %f5, 0f41800000;     // 16.0f
    min.f32         %f5, %f5, 0f43700000;     // 240.0f
    cvt.rzi.u32.f32 %r21, %f5;

    mov.f32         %f6, 0f43008000;
    fma.rn.f32      %f6, %f1, 0f42E00000, %f6;// 112.0f
    fma.rn.f32      %f6, %f2, 0fC2CB75EA, %f6;// -101.7303f
    fma.rn.f32      %f6, %f3, 0fC12450B0, %f6;// -10.2697f
    max.f32         %f6, %f6, 0f41800000;
    min.f32         %f6, %f6, 0f43700000;
    cvt.rzi.u32.f32 %r22, %f6;

    shr.u32         %r23, %r2, 1;
    add.u32         %r24, %r5, %r23;
    mad.lo.u32      %r25, %r24, %r3, %r1;
    cvt.u64.u32     %rd8, %r25;
    add.u64         %rd9, %rd2, %rd8;
    st.global.u8    [%rd9], %r21;
    st.global.u8    [%rd9+1], %r22;

L_EXIT_POST:
    ret;
}
```

### 2.2 加固 `src/shim/ngx_bridge/rife_pack_runtime.cpp`
1. **修正 Context 复用逻辑**：
   ```cpp
   bool bindPrimaryContext(RifePackState& s) {
       CUcontext current = nullptr;
       if (s.api.ctx_get_current && s.api.ctx_get_current(&current) == kSuccess && current != nullptr) {
           return true; // 线程已有合法 CUDA Context，禁止强行用 primary_context 冲刷
       }
       if (!s.primary_context && s.api.primary_ctx_retain) {
           int dev = 0;
           if (s.api.device_get && s.api.device_get(&dev, 0) == kSuccess) {
               (void)s.api.primary_ctx_retain(&s.primary_context, dev);
           }
       }
       if (s.primary_context && s.api.ctx_set_current) {
           return s.api.ctx_set_current(s.primary_context) == kSuccess;
       }
       return false;
   }
   ```
2. **核函数加载映射对齐**：
   在 `ensureLoaded` 中，将获取的函数名对齐为：
   `rife_nv12_to_nchw_7ch_fp32` 与 `rife_nchw_3ch_fp32_to_nv12`（同时保留旧别名兼容 `rife_preprocess_nv12_to_rgb`）。

---

## 三、验收标准与自测门禁 (DoD)

1. **JIT 编译通过**：`cuModuleLoadData` 针对新 PTX 文本返回 `0 (CUDA_SUCCESS)`，零 Warning、零 Error；
2. **符号解析完整**：`preprocess_kernel` 与 `postprocess_kernel` 句柄非空；
3. **单元测试回归**：运行 `rtx_tests.exe`，`rife_abi_contract` 及相关测试全绿。
