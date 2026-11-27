# **57\_wave1-A\_RIFE管线几何统一\_PTX双平面钳位与流隔离施工单**

## **1\. 施工元数据与唯一基线**

### **1.1 波次与范围定义**

* **施工波次**：Wave 1 \- Stage A（与 Wave 1-B 并行推进，聚焦解决 55 号委托书提出的问题 A：RIFE 插帧退化为纯 dup 复制帧并伴随全屏紫色闪烁）。  
* **唯一基线**：Commit `a6451b7`（必须遵守 Superpowers 原子增量补丁原则，严格禁止整文件覆盖旧快照或引入破坏性重组）。

### **1.2 宪法级硬红线**

| 审计项目 | 红线约束指标 | 实施与校验规则 |
| :---- | :---- | :---- |
| **AuditTypes** | `== 34` (或原基线 `35`，零新增) | 严格零新增具名 `struct` / `class` / `enum`。内部上下文压栈统一使用无状态自由函数 \+ inline lambda 表达式；表面槽位与 CUDA 流事件句柄直接复用既有私有成员或内置 STL 容器。 |
| **AuditImports** | GUI `<= 4` / HEADLESS `== 2` | 严格零新增动态 DLL 导入或外部符号引用。 |
| **AuditPrivacy** | `== 0` | 零新增任何隐私敏感字段、日志泄漏点或非合规数据路径。 |
| **AuditSpdx** | `PASS` | 所有修改与新增代码段头部分配明确统一的开源协议头与版权标识。 |

---

## **2\. Task 1：根除初始化与推理几何分裂**

### **2.1 精确代码坐标与物理机理诊断**

* **代码坐标**：  
  * `src/core/hw_transcode_stages.cpp:L183-L188` (`initRifeEx`)  
  * `src/core/hw_transcode_stages.cpp:L417-L436` (`synthesizeAndRemasterWindow`)  
  * `rtx_ngx_bridge_interpolate_rife` (运行时 Bridge 执行入口)  
* **物理机理分析**：  
  在 Commit `a6451b7` 中，当配置开启 `vsr_before_fg == true`（例如 `1080p -> 4K` 放大场景）时：  
  1. `initRifeEx()` 根据目标输出尺寸 `targetWidth() x targetHeight()`（即 `3840x2160`）分配并初始化 `RifeEngine` 显存张量空间。  
  2. 然而在主处理循环中，`synthesizeAndRemasterWindow` 却将尚未超分的原生 `1920x1080` NV12 表面 (`prev_f` / `curr_f`) 直接传入 `interpolateRife()`。  
  3. PTX/CUDA 核函数在按 `pitch * prev_f.height` 假定偏移计算 UV 平面地址时，因几何尺寸不一致导致读入未初始化或越界的脏显存（`U=0, V=0` 区域导致全屏绿紫交替闪烁）。  
  4. 同时，输入的错误尺寸与地址偏移破坏了场景切换检测，使 `evalBlockSadSceneCut()` 计算出的 SAD 误判为超大场景突变，触发保护机制强制降级为纯 `dup` 复制帧。

### **2.2 执行步骤与架构修补**

#### **默认架构律（FG-First 原生分辨率插帧）**

在默认模式下（尤其满足 `target_w > src_w || target_h > src_h` 的放大场景），必须强制锁死 FG-First 规则：

1. `RifeEngine` 必须严格锁定解码原生分辨率 `(src_w, src_h)` 以及解码器的真实 `pitch` 进行初始化与插帧推理。  
2. 在原语级别完成 RIFE 插帧后，生成的中间帧与原始帧再送入后续的 VSR 节点逐帧超分至 `(target_w, target_h)`，最后接续 TrueHDR 与 3D LUT 处理链。

#### **保留 VSR-First 时的独立中间 NV12 表面契约**

若显示配置显式开启 `vsr_before_fg == true`，严禁直接将未经放大的 `prev_f` / `curr_f` 跨几何域传入 RIFE：

1. 必须先对每个源帧执行 1 次 VSR 预超分，并将超分结果按 `target_w x target_h` 规范化写入独立双槽 NV12 中间表面对。  
2. 记录中间表面的真实 `vsr_pitch` 与正确计算的 UV 偏移量：  
   \$\$\\text{vsr\_uv\_offset} \= \\text{vsr\_pitch} \\times \\text{target\_h}\$\$  
3. 确保 `initRifeEx` 与 `interpolateRife` 的尺寸和内存步长实现 100% 几何对齐。若目标尺寸超过分桶 `MAX` 尺寸上限，必须立即阻断报错并抛出错误码。

#### **入口零容忍几何硬断言**

在 `rtx_ngx_bridge_interpolate_rife` 入口统一增加显式参数校验硬断言：

```
// RTX NGX Bridge 入口几何与内存安全校验
if (prev.width != engine_width || prev.height != engine_height ||
    curr.width != engine_width || curr.height != engine_height ||
    out.width  != engine_width || out.height  != engine_height) {
    INCREMENT_COUNTER(fruc_failures);
    return RTX_ERR_INVALID_PARAM;
}

if (prev.pitch < prev.width || curr.pitch < curr.width || out.pitch < out.width) {
    INCREMENT_COUNTER(fruc_failures);
    return RTX_ERR_INVALID_PARAM;
}

if (prev.uv_ptr == nullptr || prev.uv_ptr == prev.y_ptr ||
    curr.uv_ptr == nullptr || curr.uv_ptr == curr.y_ptr ||
    out.uv_ptr  == nullptr || out.uv_ptr  == out.y_ptr) {
    INCREMENT_COUNTER(fruc_failures);
    return RTX_ERR_INVALID_PARAM;
}
```

---

## **3\. Task 2：核查并升级 `rife_pack.ptx`**

为彻底解决采样越界与显存紫闪问题，对 `rife_pack.ptx` 进行汇编级升级。为了防止在 `sm_70+` 架构上 JIT 编译产生精度截断，所有浮点立即数必须统一保持全十六进制 IEEE-754 规范格式（如 `0F3F80000000000000` 表示 `1.0f`）。

```
.version 7.0
.target sm_70
.address_size 64

// rife_pack.ptx - PTX Dual-Plane Address Translation & Dual-Clamping Pipeline
```

### **3.1 显式双平面寻址机制**

前处理（NV12 到 FP32/FP16 张量）与后处理（张量到 NV12）核函数必须显式接收 8 个核心参数：  
`y_ptr`, `uv_ptr`, `y_pitch`, `uv_pitch`, `src_w`, `src_h`, `pad_w`, `pad_h`。

* **Y 平面物理寻址公式**：  
  \$\$\\text{Offset}\_Y \= y \\times \\text{y\_pitch} \+ x\$\$  
* **UV 平面 interleaved 物理寻址公式**：  
  \$\$y\_{uv} \= y \\gg 1, \\quad x\_{uv} \= x \\ & \\ \\sim 1u\$\$  
  \$\$\\text{Offset}*{UV} \= y*{uv} \\times \\text{uv\_pitch} \+ x\_{uv}\$\$  
  * \$U\$ 分量读取逻辑地址：\$\\text{uv\_ptr} \+ \\text{Offset}\_{UV}\$  
  * \$V\$ 分量读取逻辑地址：\$\\text{uv\_ptr} \+ \\text{Offset}\_{UV} \+ 1\$

### **3.2 有效边界与镜像折返（Reflect101）**

采样坐标严格限制在 `[0, src_w - 1]` 与 `[0, src_h - 1]` 有效区域内。对于因 TensorRT 32 像素对齐产生的 Padding 填充区 `[src_w, pad_w)` 与 `[src_h, pad_h)`，采用 `reflect101` 镜像填充算法或边界 Clamp 约束：

\$\$\\text{Reflect101}(x, W) \= \\begin{cases} \-x & \\text{if } x \< 0 \\ 2W \- 2 \- x & \\text{if } x \\ge W \\ x & \\text{otherwise} \\end{cases}\$\$

后处理写出阶段加装绝对拦截墙：  
若 \$x \\ge \\text{src\_w}\$ 或 \$y \\ge \\text{src\_h}\$，直接终止执行并 `ret`，绝不跨越有效画面边界写穿显存。

### **3.3 双端硬钳位与防紫闪算法**

为消除浮点计算溢出导致的色度反转与紫闪，实施前/后处理双端防护：

1. **前处理输入**：RGB 归一化张量输出强制执行 `min(max(v, 0.0f), 1.0f)`。  
2. **后处理输出预过滤**：  
   * 判定输出 Tensor 中的 `NaN` 或 `Inf` 异常值，检测到即强制重置为零；  
   * 进行 `[0.0, 1.0]` 区间双精度钳位。  
3. **色域转换与色彩空间限制范围（Limited / Full Range）硬钳位**：  
   * **Limited Range (默认 16-235/240)**：  
     \$\$Y \= \\text{clamp}(Y, 16.0, 235.0)\$\$  
     \$\$C\_b \= \\text{clamp}(C\_b, 16.0, 240.0), \\quad C\_r \= \\text{clamp}(C\_r, 16.0, 240.0)\$\$  
   * **Full Range (0-255)**：  
     \$\$Y \= \\text{clamp}(Y, 0.0, 255.0), \\quad C\_b \= \\text{clamp}(C\_b, 0.0, 255.0), \\quad C\_r \= \\text{clamp}(C\_r, 0.0, 255.0)\$\$  
4. **2x2 色度宏块聚合写出**：  
   仅当 \$(x \\ & \\ 1 \== 0)\$ 且 \$(y \\ & \\ 1 \== 0)\$ 时，收集该 \$2 \\times 2\$ 像素块内 4 个采样点的平均 \$C\_b\$ 与 \$C\_r\$ 值，单次打包为 `u8x2` 原子的双通道数据安全写回 \$\\text{uv\_ptr}\$。

### **3.4 张量数据类型匹配契约**

在 `RifeEngine::init` 流程中，通过引擎查询接口 `getTensorDataType()` 动态获取输入输出张量的实际数据类型（`nvinfer1::DataType::kHALF` 或 `kFLOAT`），精准匹配映射对应的 PTX 核函数入口点。`GridSample` 核函数保持 FP32 精度以确保插帧运动矢量的计算稳定性。

---

## **4\. Task 3：分桶路径校验、防静默回落、防指针别名与流隔离**

### **4.1 分桶路径族校验与严格禁止静默回落**

在 `param_schema.cpp:L154-L174` 及 `L271-L274` 中扩展分桶模型校验机制：

```
// 校验模型目录 models/ 下的 Engine 文件完整性
std::vector<std::string> required_engines = {
    "rife_v4.25.engine",
    "rife_v4.25_wide.engine",
    "rife_v4.25_ultra.engine",
    "rife_v4.25_square.engine",
    "rife_v4.25_tall.engine",
    "rife_v4.25_landscape.engine",
    "rife_v4.25_portrait.engine"
};
```

* **路由与校验逻辑**：配合 `rife_engine.cpp:L233` 的尺寸分桶路由算法进行加载。  
* **禁用静默回落**：当系统配置的 `fps_mode` 为 `60-rife` 或 `120-rife` 时，管线被强行锁定至 `rife-trt`。若任何分桶模型文件缺失或 `RifeEngine::init` 初始化失败，**必须立即向上层抛出严重错误并中断流水线，严格禁止静默回落为 NVOFA 60fps**。

### **4.2 结合代次的防指针别名缓存键**

在保留 Phase 9 帧对缓存特性的基础上，为消除显存分配器重用相同虚拟地址（地址复用别名）导致误命中旧帧缓存的问题，构建包含全局代次计数器的安全缓存键：

```
struct RifeCacheKey {
    uint64_t frame_seq_id;    // 帧单调递增序列 ID
    uint32_t generation_id;   // Seek/Reset 发生的代次编号
    const void* y_ptr;        // Y 平面物理指针地址
    const void* uv_ptr;       // UV 平面物理指针地址
    uint32_t width;           // 几何宽度
    uint32_t height;          // 几何高度
    uint32_t pitch;           // 显存步长
    uint32_t color_flags;     // 色彩空间标识 (Full/Limited/BT.709/BT.2020)
};
```

当发生 Seek、FLush 或 Reset 事件时，立即递增 `generation_id` 并清空现有缓存槽，杜绝双槽复用时的无效命中。

### **4.3 专属非阻塞 CUDA 流与线程上下文隔离**

为了保障管道高并发执行及线程安全，防止在无预览窗口模式下回落至默认流 `0`：

1. **非阻塞流创建**：分配独立的 RIFE 专属非阻塞流及 Event 握手句柄：

```
cuStreamCreate(&m_rife_stream, CU_STREAM_NON_BLOCKING);
cuEventCreate(&m_rife_event, CU_EVENT_DISABLE_TIMING);
```

2. **零类新增上下文压栈**：禁止引入新的 C++ 类，使用无状态自由函数配对 RAII Lambda 实现 Context 隔离管理：

```
template <typename Func>
auto runInRifeContext(CUcontext ctx, Func&& func) -> decltype(func()) {
    cuCtxPushCurrent(ctx);
    try {
        auto result = func();
        CUcontext popped;
        cuCtxPopCurrent(&popped);
        return result;
    } catch (...) {
        CUcontext popped;
        cuCtxPopCurrent(&popped);
        throw;
    }
}
```

3. **异步清零与 EMA 状态隔离**：  
   * 用于场景切变检测的 `d_sad_sum` 必须通过 `cudaMemsetAsync(d_sad_sum, 0, sizeof(uint64_t), m_rife_stream)` 在专属流上实现 8 字节双字异步清零。  
   * RIFE 实例内部的 EMA（滑动平均亮度/运动指数）状态变量实现多实例严格隔离，防止不同卡或流之间的状态污染。

---

## **5\. 验收 Gate**

施工完成后，必须逐项核对并确保通过以下所有 Gate 校验：

```
[PASS] Gate 1: 几何统一 - 根除初始化与推理尺寸/Pitch分裂，FG-First/VSR-First均满足独立NV12契约。
[PASS] Gate 2: PTX安全 - rife_pack.ptx 汇编升级完成，双平面寻址、Reflect101镜像与双端钳位全面覆盖。
[PASS] Gate 3: 路由硬锁定 - 分桶模型族完整校验，60-rife/120-rife 严格禁止静默降级至 NVOFA。
[PASS] Gate 4: 缓存与流隔离 - 代次防指针别名机制生效，CU_STREAM_NON_BLOCKING 与 Context 压栈无缝隔离。
[PASS] Gate 5: 宪法指标 - AuditTypes == 34/35 (零新增类型), AuditImports GUI <= 4 / HEADLESS == 2, AuditPrivacy == 0, AuditSpdx == PASS。
```

