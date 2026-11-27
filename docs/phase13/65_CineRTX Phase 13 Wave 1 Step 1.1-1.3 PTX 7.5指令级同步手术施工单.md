# **65\_CineRTX Phase 13 Wave 1 (Step 1.1–1.3)：PTX 7.5 指令级同步手术施工单**

* **施工单编号**：`CINERTX-P13-WO-065`  
* **所属波次**：`Wave 1（无 nvcc 宿主机 PTX 7.5 等价指令同步波次）`  
* **前置依赖**：`64_CineRTX Phase 13 Wave 0 Step 0.1-0.2 越界头文件拔除与废弃旧桩防回流施工单` 通关  
* **后续衔接**：`66_CineRTX Phase 13 Wave 2 Step 2.1-2.3 纯C ABI驱动RIFE单测重构施工单`  
* **独家触碰文件**：`src/shim/ngx_bridge/rife_pack.ptx`  
* **签发日期**：`2026-11-26`

## **一、工程背景与 PTX 7.5（`sm_75`）寄存器安全性证明**

1. **为什么必须手修 `rife_pack.ptx`**：`CMakeLists.txt:L213, L237` 在构建期直接将 `src/shim/ngx_bridge/rife_pack.ptx` 转换为 C 数组头文件 `rife_pack_ptx.h` 并由 `rife_pack_runtime.cpp` 调用 CUDA 驱动 `cuModuleLoadData` 运行时 JIT 编译。宿主机未安装 `nvcc`，因此 `60_` 对 `rife_pack.cu` 的修复不会自动反映到运行时二进制中。  
2. **两项必须同步的算子语义修复**：  
   * **修复 A（`clamp01` 的 `NaN / ±Inf` 归零守卫）**：原 PTX 仅使用 `max.ftz.f32` \+ `min.ftz.f32`，当张量出现 `+Inf` 时输出 `1.0f` 而非 `rife_pack.cu` 规定的 `0.0f`。PTX 7.5 原生指令 `testp.finite.f32 %p, %val;` \+ `selp.f32 %dst, %val, 0f00000000, %p;` 可无分支将所有 `NaN` 与 `±Inf` 清零。  
   * **修复 B（`full_range != 0` 补全 `* 255.0f` 缩放）**：在 `rife_nchw_3ch_fp32_to_nv12` 中，采样后的 `%f1, %f2, %f3` 值域为 `[0.0f, 1.0f]`，但 `$L__BB1_4` 之前的 `full_range != 0` 分支未乘 `255.0f`（`0f437F0000`），直接用 `[0, 1]` 计算 BT.601 矩阵，导致输出 Y/U 全部坍缩至 `0` 或 `128`，引发真机紫色/全黑花屏。  
3. **零寄存器越界严格证明**：  
   * `rife_nv12_to_nchw_7ch_fp32` 顶部声明 `.reg .pred %p<28>; .reg .f32 %f<105>;`（当前仅用到 `%p1..%p26` 与 `%f1..%f94`，空闲 `%p27` 与 `%f95` 可安全使用）；  
   * `rife_nchw_3ch_fp32_to_nv12` 顶部声明 `.reg .pred %p<22>; .reg .f32 %f<80>;`（当前仅用到 `%p1..%p20` 与 `%f1..%f76`，空闲 `%p21` 与 `%f77, %f78, %f79` 可安全使用）。

## **二、原子施工步骤（Step 1.1 – Step 1.3）**

### **Step 1.1：修补 `rife_nv12_to_nchw_7ch_fp32` 预处理输出 `clamp01`（`rife_pack.ptx:L247-L270`）**

* **代码坐标**：`src/shim/ngx_bridge/rife_pack.ptx` 第 `247–270` 行（`$L__BB0_10:` 标签上方）  
* **操作动作**：在 `$L__BB0_10:` 标签之前插入以下 13 条指令（复用空闲谓词 `%p27` 与寄存器 `%f95`），确保写入全局显存的 `%f89..%f94` 在遇到 `NaN / ±Inf` 时强制归零：

x  
mov.f32 	%f95, 0f00000000;  
testp.finite.f32 	%p27, %f89;  
selp.f32 	%f89, %f89, %f95, %p27;  
testp.finite.f32 	%p27, %f90;  
selp.f32 	%f90, %f90, %f95, %p27;  
testp.finite.f32 	%p27, %f91;  
selp.f32 	%f91, %f91, %f95, %p27;  
testp.finite.f32 	%p27, %f92;  
selp.f32 	%f92, %f92, %f95, %p27;  
testp.finite.f32 	%p27, %f93;  
selp.f32 	%f93, %f93, %f95, %p27;  
testp.finite.f32 	%p27, %f94;  
selp.f32 	%f94, %f94, %f95, %p27;

\$L\_\_BB0\_10:

````

### Step 1.2：修补 `rife_nchw_3ch_fp32_to_nv12` 双线性与全分辨率路径 `clamp01`（`rife_pack.ptx:L395-L442`）

* **代码坐标**：`src/shim/ngx_bridge/rife_pack.ptx` 第 `395–442` 行
* **操作动作**：
  1. 在双线性采样分支（`bra.uni $L__POST_DONE;` 之前）的 R/G/B 三通道 `min.ftz.f32` 后各追加 2 条有限值归零指令（复用空闲谓词 `%p21` 与常量寄存器 `%f5 = 0f00000000`）：

```ptx
	// Channel 0 (R) -> %f1
	max.ftz.f32 	%f76, %f76, %f5;
	min.ftz.f32 	%f1, %f76, %f7;
	testp.finite.f32 	%p21, %f1;
	selp.f32 	%f1, %f1, %f5, %p21;

	// Channel 1 (G) -> %f2
	max.ftz.f32 	%f76, %f76, %f5;
	min.ftz.f32 	%f2, %f76, %f7;
	testp.finite.f32 	%p21, %f2;
	selp.f32 	%f2, %f2, %f5, %p21;

	// Channel 2 (B) -> %f3
	max.ftz.f32 	%f76, %f76, %f5;
	min.ftz.f32 	%f3, %f76, %f7;
	testp.finite.f32 	%p21, %f3;
	selp.f32 	%f3, %f3, %f5, %p21;
````

2. 在 `$L__POST_FULL:` 分支（`$L__POST_DONE:` 之前）的 R/G/B 三通道 `min.ftz.f32` 后同样各追加 2 条指令：

x  
\$L\_\_POST\_FULL:  
mad.lo.s32 	%r16, %r2, %r5, %r1;  
mul.wide.s32 	%rd7, %r16, 4;  
add.s64 	%rd8, %rd6, %rd7;  
ld.global.nc.f32 	%f4, \[%rd8\];  
max.ftz.f32 	%f6, %f4, %f5;  
min.ftz.f32 	%f1, %f6, %f7;  
testp.finite.f32 	%p21, %f1;  
selp.f32 	%f1, %f1, %f5, %p21;  
add.s32 	%r17, %r15, %r16;  
mul.wide.s32 	%rd9, %r15, 4;  
add.s64 	%rd10, %rd8, %rd9;  
ld.global.nc.f32 	%f8, \[%rd10\];  
max.ftz.f32 	%f9, %f8, %f5;  
min.ftz.f32 	%f2, %f9, %f7;  
testp.finite.f32 	%p21, %f2;  
selp.f32 	%f2, %f2, %f5, %p21;  
add.s32 	%r18, %r17, %r15;  
mul.wide.s32 	%rd11, %r18, 4;  
add.s64 	%rd12, %rd6, %rd11;  
ld.global.nc.f32 	%f10, \[%rd12\];  
max.ftz.f32 	%f11, %f10, %f5;  
min.ftz.f32 	%f3, %f11, %f7;  
testp.finite.f32 	%p21, %f3;  
selp.f32 	%f3, %f3, %f5, %p21;

````

#### Step 1.3：修补 `rife_nchw_3ch_fp32_to_nv12` 的 `full_range != 0` `* 255.0f` 缩放与 `128.5f` 偏移（`rife_pack.ptx:L445-L474`）

* **代码坐标**：`src/shim/ngx_bridge/rife_pack.ptx` 第 `445–474` 行（`setp.eq.s32 %p4, %r7, 0;` 至 `bra.uni $L__BB1_6;`）
* **操作动作**：将原 `full_range != 0` 分支整段替换为以下指令序列（将 `[0, 1]` 的 `%f1, %f2, %f3` 乘以 `255.0f = 0f437F0000` 存入 `%f77, %f78, %f79`，并在 Y/U/V 表达式中补全 `+ 0.5f` 与 `128.5f = 0f43008000`）：

```ptx
	setp.eq.s32 	%p4, %r7, 0;
	@%p4 bra 	$L__BB1_4;

	mov.f32 	%f17, 0f437F0000;
	mul.ftz.f32 	%f77, %f1, %f17;
	mul.ftz.f32 	%f78, %f2, %f17;
	mul.ftz.f32 	%f79, %f3, %f17;
	mul.ftz.f32 	%f12, %f78, 0f3F1645A2;
	fma.rn.ftz.f32 	%f13, %f77, 0f3E991687, %f12;
	fma.rn.ftz.f32 	%f14, %f79, 0f3DE978D5, %f13;
	add.ftz.f32 	%f14, %f14, 0f3F000000;
	max.ftz.f32 	%f16, %f14, %f5;
	min.ftz.f32 	%f18, %f16, %f17;
	cvt.rzi.ftz.u32.f32 	%r25, %f18;
	st.global.u8 	[%rd1], %r25;
	and.b32  	%r26, %r1, 1;
	setp.eq.b32 	%p5, %r26, 1;
	and.b32  	%r27, %r2, 1;
	setp.eq.b32 	%p6, %r27, 1;
	or.pred  	%p7, %p6, %p5;
	mov.pred 	%p8, 0;
	xor.pred 	%p9, %p7, %p8;
	@%p9 bra 	$L__BB1_6;

	fma.rn.ftz.f32 	%f19, %f77, 0fBE2CC921, 0f43008000;
	fma.rn.ftz.f32 	%f20, %f78, 0fBEA99B6F, %f19;
	fma.rn.ftz.f32 	%f21, %f79, 0f3F000000, %f20;
	mov.f32 	%f22, 0f00000000;
	max.ftz.f32 	%f23, %f21, %f22;
	min.ftz.f32 	%f25, %f23, %f17;
	fma.rn.ftz.f32 	%f26, %f77, 0f3F000000, 0f43008000;
	fma.rn.ftz.f32 	%f27, %f78, 0fBED65E46, %f26;
	fma.rn.ftz.f32 	%f28, %f79, 0fBDA686E8, %f27;
	max.ftz.f32 	%f29, %f28, %f22;
	min.ftz.f32 	%f30, %f29, %f17;
	cvt.rzi.ftz.u32.f32 	%r28, %f25;
	st.global.u8 	[%rd2], %r28;
	cvt.rzi.ftz.u32.f32 	%r29, %f30;
	st.global.u8 	[%rd2+1], %r29;
	bra.uni 	$L__BB1_6;
````

## **三、Wave 1 通关门禁（Exit Gate）**

| 检查项 | 核验方法 | 通关判据 |
| :---- | :---- | :---- |
| **Gate W1-1：PTX 语法与寄存器** | `Select-String -Path src/shim/ngx_bridge/rife_pack.ptx -Pattern "testp\.finite\.f32"` | 精确命中 **12 处**（预处理 6 处 \+ 后处理双线性 3 处 \+ 全分 3 处），且无新增 `.reg` 声明 |
| **Gate W1-2：Full-Range 缩放** | `Select-String -Path src/shim/ngx_bridge/rife_pack.ptx -Pattern "mul\.ftz\.f32\s+%f7[789]"` | 精确命中 **3 处**（`%f77, %f78, %f79` 乘以 `0f437F0000`） |
| **Gate W1-3：SPDX 与构建触发** | 编译 `rtx_ngx_bridge` 目标 | `CMakeLists.txt` 自动重新生成 `rife_pack_ptx.h` 且 `rtx_ngx_bridge.dll` 编译 0 Error 0 Warning |

**通关后立即进入 `66_CineRTX Phase 13 Wave 2 Step 2.1-2.3 纯C ABI驱动RIFE单测重构施工单`。**