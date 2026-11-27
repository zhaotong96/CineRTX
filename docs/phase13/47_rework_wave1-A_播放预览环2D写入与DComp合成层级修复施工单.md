# **47\_rework\_wave1-A\_播放预览环2D写入与DComp合成层级修复施工单**

**Superpowers 原子靶向施工规约**  
*依据 46 号专家穿透复验裁决 `CINERTX-P13-EXPERT-AUDIT-046` 与 45 号施工汇报编制*

---

## **一、 施工元数据与保留确认**

### **1.1 施工波次与靶向定位**

本施工单属于 **Rework Wave 1 \- Stage A** 阶段，与 **Rework Wave 1 \- Stage B** 并行推进。本波次旨在彻底消除 45 号返工遗漏的底层缺陷，重点覆盖：

1. 生产端 `bridge_exports.cpp` 无头播放环 2D 写入缺失与行步长契约失配（修复 46 号硬伤 1）；  
2. 消费端 `preview_viewport.cpp:525` `CreateTargetForHwnd(..., FALSE)` 图层层级倒置缺陷（修复 46 号硬伤 4）。

| 维度 | 规范明细 |
| :---- | :---- |
| **施工波次编号** | `Rework Wave 1 - Stage A` |
| **穿透审计依据** | `CINERTX-P13-EXPERT-AUDIT-046` |
| **关联工件** | `src/shim/ngx_bridge/bridge_exports.cppsrc/shim/ngx_bridge/preview_ring.cu` (或对应 CUDA 预处理模块)`src/gui/preview_viewport.cppsrc/gui/rtx_gui.cpptests/test_playback_session.cpp` |

### **1.2 45 号成果保留确认（严禁推翻重来）**

45 号施工汇报中已完成并经 46 号专家复验通过的以下架构变更，必须予以 **100% 完整保留**，严禁任何形式的撤销或破坏性重写：

* **双尺寸与位掩码成员变量**：`preview_viewport.h:31-36, 115, 131` 中定义的 `src_width_`, `src_height_`, `viewport_width_`, `viewport_height_` 及状态掩码标记；  
* **DirectComposition 硬裁剪**：`preview_viewport.cpp:369, 421, 553, 599, 631, 643` 中利用 `IDCompositionRectangleClip` 实现的视口边界像素级硬裁剪逻辑；  
* **色彩空间与步长校验**：`DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709` 色彩空间设定、呈现前（Present）尺寸兜底校验以及消费端 `pitch >= src_width * 4u` 防御断言。

### **1.3 宪法级硬红线**

本波次施工必须严格遵守以下三条项目宪法硬红线：

1. **零新增具名类型**：严格保持类型库总量 `AuditTypes == 34/35`（全过程 `+0` 具名 `struct` / `class` / `enum`）；  
2. **零新增静态 DLL 依赖**：严格保持导入表 `AuditImports` GUI 模块 `<= 4`，HEADLESS 无头模块 `== 2`；  
3. **零 GPU \-\> CPU 像素回读**：预览与播放流水线全过程必须在 GPU VRAM 内完成，禁止任何形式的 `cudaMemcpy2D` 显存回读至系统内存。

---

## **二、 靶向手术 1：无头播放环 2D 写入与步长契约修复**

### **2.1 穿透事实诊断**

45 号施工汇报仅在消费端 `preview_viewport.cpp:631` 加强了步长校验，完全遗漏了生产端 `src/shim/ngx_bridge/bridge_exports.cpp` 的修改。经 46 号审计复验，生产端无头播放环仅写入了顶部数像素单线，其根本原因在于 CUDA 核函数网格维度硬编码为 1D（`grid.y == 1`），且行跨度未遵循物理对齐契约。

```
[45号缺陷状态]
CUDA Kernel (grid.y = 1) ──> 仅仅写入 1 Line ──> 共享槽位 (大面积未初始化/垃圾数据)
                                                     │
                                                     ▼
Preview Viewport <── (错误认为成功) ── 消费端仅做步长断言 check

[47号修复后状态]
CUDA Kernel (dim3 grid(x, y)) ──> 2D 完整写入 ──> 动态 3-Slot 环 (256B 对齐 pitch)
                                                     │
                                                     ▼
Preview Viewport <── 契约完全匹配 (Pitch & 完整 2D 像素) ── 帧呈现
```

### **2.2 精确手术坐标**

* `src/shim/ngx_bridge/bridge_exports.cpp`：  
  * `rtx_ngx_bridge_create` (当 `reserved[0] == 1` 无头播放环初始化分支)  
  * `rtx_ngx_bridge_playback_pack()`  
  * `previewRingRowBytes()`  
  * `borrow_preview()`  
* `src/shim/ngx_bridge/preview_ring.cu`（或 `preprocess_kernel.cu`）：  
  * `rtx_ngx::launchPreprocessBuffer` CUDA 预处理核函数启动器及 Kernel 函数体。

### **2.3 三项物理不变量修正指令**

#### **不变量 1：动态环容量与匹配尺寸**

在 `rtx_ngx_bridge_create`（`reserved[0] == 1`）中按真实媒体 `width x height` 分配 3-slot 共享预览环。若 `rtx_ngx_bridge_playback_pack()` 检测到当前解码帧的 `width` 或 `height` 超出当前已分配槽位容量（例如初始化时为 `64x64` 占位尺寸，首帧实际解码为 1920x1080 或 3840x2160）：

1. 必须立即调用 `cudaStreamSynchronize(stream)` 确保当前 GPU 任务完全挂起；  
2. 按照新 `width x height` 重新计算 `previewRingRowBytes()` 并重新分配 3-slot 共享环显存；  
3. 更新槽位容量元数据，严禁沿用过小的旧环容量引发 CUDA 越界写或截断崩溃。

#### **不变量 2：CUDA 2D 网格维数（消灭 `grid.y == 1` 单线写入）**

核查并重构 `rtx_ngx::launchPreprocessBuffer` 及其内部 CUDA Kernel：

* 必须采用 2D 线程块与网格配置：

```
dim3 block(16, 16);
dim3 grid((width + block.x - 1) / block.x, 
          (height + block.y - 1) / block.y);
```

  *若采用 1D Grid，线程总数必须扩展至 `width * height`，且核函数内部需解算 `y = tid / width` 与 `x = tid % width`。*


* **行字节偏移计算**：核函数内部计算输出目标地址时，每行偏移必须严格使用 `y * row_bytes + x * 4u`（其中 `row_bytes = previewRingRowBytes()`），**严禁使用 `y * width * 4u`**。确保 256 字节 Padding 内存块被正确跨越，杜绝行交错错位或斜向撕裂。

#### **不变量 3：`out_pitch` 行字节跨度量纲一致性**

确认 `borrow_preview` 函数返回的 `out_pitch` 参数值严格等于 `previewRingRowBytes()`：

\$\$\\text{out\_pitch} \= \\text{previewRingRowBytes}(width) \= \\text{alignUp}(width \\times 4\\text{B}, 256\\text{B})\$\$

保证此输出与消费端 preview\_viewport.cpp:631 的校验逻辑完全一致：  
\$\$\\text{pitch} \\ge \\text{src\_width} \\times 4\\text{u} \\quad \\land \\quad (\\text{pitch} \\pmod{4\\text{u}} \== 0\\text{u}) \\quad \\land \\quad \\text{pitchWords} \= \\frac{\\text{pitch}}{4\\text{u}}\$\$

---

## **三、 靶向手术 2：DComp 合成层级与窗口隔离修复**

### **3.1 穿透事实诊断**

1. **DComp 视觉树层级置顶**：`preview_viewport.cpp:525` 调用 `CreateTargetForHwnd(child_hwnd_.get(), TRUE, &raw_target)` 时，第二个参数传入了 `TRUE`（即 `topmost = TRUE`）。这导致 DirectComposition 强制将预览视觉树渲染在所有 Win32 HWND 最顶层，遮挡了主窗口的右键上下文菜单、下拉选择框与 Toast 状态通知。  
2. **跨层遮挡问题**：进入 Player 模式时，`applyWorkspaceLayout()` 虽然将 `child_hwnd_` 拉伸填充视口，但未将非 Player 的 Inspector（检查器）与 Queue（队列）控件显式隐藏，导致 Z-order 第 3 层的控件被第 2 层 DirectComposition 视觉树异常穿透或遮挡。

### **3.2 精确代码坐标**

* `src/gui/preview_viewport.cpp:525`  
* `src/gui/rtx_gui.cpp` (`applyWorkspaceLayout()` 中 `isPlayer` 分支)

### **3.3 手术动作指令**

```
[层级结构修正前]
Topmost DComp Target (TRUE)  ───> 覆盖在所有 Win32 窗口之上 (遮挡 Context Menu / Toast)
  └─ HWND (child_hwnd_)

[层级结构修正后]
Normal DComp Target (FALSE)  ───> 嵌入普通 HWND 视觉树 Z-Order
  ├─ Non-Player Controls      ───> 显式 ShowWindow(..., SW_HIDE)
  └─ Context Menu / Toast     ───> 正常覆盖在 Viewport 之上
```

#### **动作 1：修正 DComp Target 顶置标记**

修改 `src/gui/preview_viewport.cpp:525`，将 `topmost` 参数由 `TRUE` 修正为 `FALSE`：

```
// 修正前: hr = dcomp_desktop_->CreateTargetForHwnd(child_hwnd_.get(), TRUE, &raw_target);
// 修正后:
HRESULT hr = dcomp_desktop_->CreateTargetForHwnd(child_hwnd_.get(), FALSE, &raw_target);
```

#### **动作 2：Player 工作区控件隔离**

检查并重构 `src/gui/rtx_gui.cpp` 的 `applyWorkspaceLayout()` 函数中 `isPlayer == true` 的处理分支：

当用户切换至 Player 模式并将 child\_hwnd\_ 调整为 x \= 0, y \= bodyTop 的全屏/全宽区域时，必须对所有非 Player 控件（如属性面板、任务队列列表、日志窗口等）遍历执行：p  
::ShowWindow(hwnd\_inspector\_, SW\_HIDE);  
::ShowWindow(hwnd\_queue\_panel\_, SW\_HIDE);  
// 确保仅有 Player 视口及必要的播放控制条保持 SW\_SHOW

````

规避 Z-order 层级混淆，确保视口树与 Win32 原生 UI 树互相隔离。

---

## 四、 靶向手术 3：TDD 防回归单元测试断言

在 `tests/test_playback_session.cpp` 中补充针对无头播放环 2D 写入完整性的自动化测试用例，防止未来重现"仅首行写入"或"Pitch 计算错误"缺陷。

### 4.1 测试用例验证逻辑

```cpp
// tests/test_playback_session.cpp 补充片段
TEST_CASE("Playback Session 2D Preview Ring Integrity", "[playback][preview_ring]") {
    // 1. 初始化无头播放环 (reserved[0] = 1)
    BridgeHandle handle = nullptr;
    BridgeCreateParams params{};
    params.reserved[0] = 1;
    params.width = 1920;
    params.height = 1080;
    REQUIRE(rtx_ngx_bridge_create(&handle, &params) == BRIDGE_SUCCESS);

    // 2. 构造非纯黑测试源帧 (如渐变或非零 Pattern) 并执行 Pack
    REQUIRE(rtx_ngx_bridge_playback_pack(handle, src_gpu_buffer) == BRIDGE_SUCCESS);

    // 3. 获取借用预览指针与 Pitch
    void* preview_ptr = nullptr;
    uint32_t out_pitch = 0;
    uint32_t out_width = 0;
    uint32_t out_height = 0;
    REQUIRE(borrow_preview(handle, &preview_ptr, &out_pitch, &out_width, &out_height) == BRIDGE_SUCCESS);

    // 4. 断言 Pitch 契约
    REQUIRE(out_pitch >= out_width * 4u);
    REQUIRE((out_pitch % 256u) == 0u); // 256 字节对齐断言

    // 5. 校验首行、中间行、底部行 RGBA10 像素字非零 (GPU 侧 Kernel 或映射校验)
    uint32_t top_row_pixel = read_gpu_pixel_word(preview_ptr, out_pitch, 0, 0);
    uint32_t mid_row_pixel = read_gpu_pixel_word(preview_ptr, out_pitch, out_width / 2, out_height / 2);
    uint32_t bot_row_pixel = read_gpu_pixel_word(preview_ptr, out_pitch, out_width - 1, out_height - 1);

    REQUIRE(top_row_pixel != 0u);
    REQUIRE(mid_row_pixel != 0u);
    REQUIRE(bot_row_pixel != 0u); // 彻底封死"仅顶部数像素单线写入"回归

    rtx_ngx_bridge_destroy(handle);
}
````

---

# **五、 本波次施工验收 CheckList (Gate)**

施工完成后，必须逐项核对并确认以下验收标准：

| 序号 | 验收项目 | 期望结果 | 状态确认 |
| :---- | :---- | :---- | :---- |
| **1** | `bridge_exports.cpp` 2D 网格重构 | CUDA Kernel 明确使用 2D Grid (`grid.y > 1`)，每行寻址包含 `row_bytes` | \[ \] |
| **2** | 动态环容量重分配 | 解码尺寸变更时触发 `cudaStreamSynchronize` 并动态扩容，无显存越界 | \[ \] |
| **3** | Pitch 跨度契约 | `out_pitch == previewRingRowBytes()` 满足 256B 对齐，消费端校验 Pass | \[ \] |
| **4** | DComp 置顶修复 | `preview_viewport.cpp:525` 第二参数为 `FALSE`，右键菜单与 Toast 无遮挡 | \[ \] |
| **5** | Player 控件隔离 | 进入 Player 工作区时非相关控件隐式隐藏 (`SW_HIDE`) | \[ \] |
| **6** | 宪法红线 1 | `AuditTypes == 34/35`（全过程未新增任何具名结构体/类/枚举） | \[ \] |
| **7** | 宪法红线 2 | `AuditImports` GUI `<= 4` / HEADLESS `== 2`（零新增静态 DLL） | \[ \] |
| **8** | 宪法红线 3 | 零 GPU \-\> CPU 显存回读操作 | \[ \] |
| **9** | TDD 单元测试 | `tests/test_playback_session.cpp` 三行（顶/中/底）非零断言通过 | \[ \] |

