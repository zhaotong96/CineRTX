# CineRTX 待办事项：双端架构同步、AV1 默认编码与专业级调参面板

> **文件标识**：`docs/TODO/cli_gui_synchronization_av1_and_pro_parameters.md`  
> **建立日期**：2026-11-09  
> **所属版本**：CineRTX Phase 5+ 架构演进  
> **核心优先级**：P1 (架构可靠性与产品化扩展)

---

## 1. 任务背景与核心教训

在 Phase 5 竖屏视频转码调测过程中，CLI 前端已将默认目标分辨率由 `3840x2160` 演进为 `0x0`（由 `SmartGeometryCalculator` 智能推导），但 Win32 GUI 前端在 `runJob` 启动入口中依然遗留了硬编码的 `options.target_width = 3840; options.target_height = 2160;`。

这一失误直接击穿了中枢调度自适应保护门禁，导致 GUI 拖拽转码的竖屏素材被严重横向压扁拉伸 3.16 倍。

> [!IMPORTANT]
> **核心架构教训**：
> 目前 CLI 与 Win32 GUI 双前端的参数装配逻辑是分离的，缺少**单一真实信息源（Single Source of Truth, SSOT）**，极易因双端代码不同步或前端各自硬编码而引入逻辑撕裂。作为工业级专业软件，必须根除“前端各自拼凑配置”的设计缺陷。

---

## 2. 待办目标一：CLI / GUI 双端无缝同步中枢 (SSOT 配置引擎)

### 2.1 物理痛点
* CLI 解析器与 Win32 GUI 各自实例化并填充 `rtx::HwTranscodeOptions`；
* 新增参数、默认值修正或架构自适应策略时，极易改此失彼。

### 2.2 解决方案
1. **构建统一配置模型中枢 (`JobProfileEngine`)**：
   * 在 `include/rtx/` 中抽象统一作业配置文件与默认值生成器；
   * 定义 `PresetProfile` 机制，所有默认策略（如 `Auto 4K` 几何计算、默认码率计算公式等）均在中枢确定；
2. **双前端统一接入中枢**：
   * CLI 命令行解析仅作为参数覆盖层（Override Layer）；
   * GUI 界面所有输入控件状态与事件直接映射至统一配置模型；
   * 杜绝任何前端私自硬编码分辨率、编码器参数或算法策略。

---

## 3. 待办目标二：默认编码格式由 HEVC 升级为 AV1 (AV1 10-bit Default)

### 3.1 演进目标
* 当前产品转码管道默认采用 HEVC Main10；
* 作为面向 NVIDIA RTX 40/50 世代硬件的现代专业转码引擎，全面支持并默认激活 **AV1 10-bit (Main Profile)** 编码流。

### 3.2 关键实施项
1. **默认编码器参数调整**：
   * 将 `options.codec` 默认值切换为 `rtx::VideoCodec::AV1`；
   * 默认容器输出封装支持 `.mp4` / `.mkv` 的 AV1 封装；
2. **硬件能力探测与优雅降级**：
   * 启动探测时通过 NVENC API 查询当前 GPU 是否支持 AV1 硬件编码器；
   * 若运行在较旧硬件（如 RTX 20/30 系列等无 AV1 NVENC 芯片），自动、透明且安全地降级为 HEVC Main10，并输出清晰诊断信息。

---

## 4. 待办目标三：工业级专业调参矩阵与扩展 UI 调控面板

### 4.1 物理痛点
* 当前 Win32 GUI 仅保留了预设极简切换与拖拽开始，可调控参数极为有限；
* 作为专业级生产力工具，高级用户与非编工程师需要深度介入底层编码细节与画质映射算法。

### 4.2 扩展参数矩阵规格设计

规划在 GUI 中引入**专业调参抽屉/属性检查器面板 (Professional Tuning Inspector)**，提供丰富详尽的可调参数矩阵：

#### A. 编码核心与码率控制
* **编码格式 (Codec Selection)**：`AV1 (默认)` / `HEVC` / `H.264`；
* **码率控制模式 (Rate Control)**：`CBR` / `VBR` / `Const-QP`；
* **目标码率与最大码率 (Bitrate)**：支持滑块与微调输入（Mbps）；
* **编码预设 (Preset)**：`P1 (最快)` 至 `P7 (最高质量)`；
* **GOP 结构**：GOP 长度、B 帧数量（0..4）、Lookahead 帧数；
* **自适应量化 (AQ)**：空间自适应量化 (Spatial AQ) 与时间自适应量化 (Temporal AQ) 开关及强度。

#### B. 画质与算法引擎
* **RTX VSR 等级**：`Level 1 (极速)` 至 `Level 4 (超高清质量)`；
* **TrueHDR 逆色调映射曲线调谐**：
  * 对比度 (Contrast: 0..200)；
  * 饱和度 (Saturation: 0..200)；
  * 中灰亮度基准 (Middle Gray: 10..100)；
  * 峰值亮度目标 (Max Luminance: 400..2000 nits)；
  * 场景自适应平滑滤波系数 (Smoothing Coefficient)。

#### C. 几何与画幅策略 (Geometry & Canvas)
* **画幅模式**：
  * `Auto Direct (默认)`：自动竖屏直出（如 2160x3840）；
  * `Pillarbox (演播室黑边)`：16:9 画布居中 + 零色差纯黑留白；
  * `Crop & Fill`：等比裁剪填满；
  * `Custom Resolution`：自定义输出宽高（像素级别自由指定，带偶数对齐校验）。

#### D. 音频与容器元数据
* **音频处理模式**：`Stream Copy (零拷贝原样透传)` / `AAC 转码` / `音频剥离`；
* **HDR10 静态元数据注入**：Mastering Display Color Volume、MaxCLL、MaxFALL 覆盖设置。

---

## 5. 验收准则与准入基准
1. **双端等效性测试**：任何通过 GUI 调整的参数组合，均能在 CLI 生成严格对应的命令行并输出逐 bit 一致的流；
2. **AV1 闭环验证**：在 RTX 40/50 平台测试 AV1 10-bit 编码容器合规性与播放器兼容性；
3. **UI 性能准则**：参数面板展开与折叠保持 60 FPS 流畅绘制，零卡顿，不阻断后台转码异步引擎。
