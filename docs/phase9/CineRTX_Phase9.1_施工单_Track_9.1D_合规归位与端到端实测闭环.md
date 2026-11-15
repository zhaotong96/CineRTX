# CineRTX Phase 9.1 施工单：Track 9.1-D 合规归位与端到端实测闭环

> **工单编号**: `CINERTX-P9.1-TRACK-D`  
> **任务名称**: `文档全仓脱敏与归位、纯净环境实机 120fps E2E 转码闭环与终审复验呈报`  
> **责任小组**: `交付与集成验证组`  
> **主导依据**: [`docs/phase9/CineRTX_Phase9_终审裁决与全链路真权重闭环SuperPower返工规范.md`](docs/phase9/CineRTX_Phase9_终审裁决与全链路真权重闭环SuperPower返工规范.md)  
> **关联缺陷**: 🔴 **RW-P9-03 (P0 阻断)**、Gate 9.3 ~ Gate 9.6  
> **核心目标**: 彻底清零全仓私有路径泄露，统一归档至 `docs/phase9/`，在零测试环境变量纯净系统下用真实 RIFE 模型跑通 120fps 转码并完成 MAE 逐帧互异性验收

---

## 一、 被拒缺陷机理穿透 (RW-P9-03)

### 1. 缺陷本质：明文泄露未掩码本地绝对路径与目录漂移
在 Phase 9 终审验收报告中，出现了 5 处未脱敏的本地开发机路径（`[REDACTED_HOST]` 与 `[REDACTED_HOST]`），直接破坏了 Gate 3.6 隐私与脱敏红线。
同时，Phase 9 的施工单、总览及验收报告错放于 `docs/phase8/` 下，导致阶段目录与全仓索引严重失序。

---

## 二、 施工目标与详细步骤

### 任务 9.1-D1：文档归档迁移与全仓正则脱敏清零

1. **目录归位**：
   确认全仓 Phase 9 的所有文档均已统一存放于 **`docs/phase9/`** 目录下，`docs/phase8/` 仅保留 Phase 8 相关的历史定稿文档。
2. **全仓正则脱敏扫描**：
   在根目录下执行正则扫描命令：
   ```powershell
   Select-String -Path "docs\**\*.md","README*.md" -Pattern "C:\\PUBLIC|C:\\Users\\zt\\AppData|192\.168\."
   ```
   **门禁断言**：匹配命中数必须严格为 **`0`**。
3. **更新 `docs/README.md`**：
   在 [`docs/README.md`](docs/README.md) 中新增“Phase 9 权威定稿文档 (`docs/phase9/`)”章节，收录 Phase 9.1 返工总览、分轨施工单及最终验收报告，更新单测基线数为 75 项。

---

### 任务 9.1-D2：零测试环境变量下运行真实 120fps E2E 转码实测

1. **终端纯净度确认**：
   在执行测试前，必须确保环境变量中未设置任何 `CINERTX_*` 测试标记：
   ```powershell
   Remove-Item Env:CINERTX_ALLOW_BUILTIN_RIFE_SYNTH -ErrorAction SilentlyContinue
   ```
2. **运行实机转码**：
   调用编译好的 `CineRTX.exe`，指定由 Track 9.1-A 生成的官方真实引擎 `models/rife_v4.25.engine`（$\ge 5\text{ MiB}$）：
   ```powershell
   .\.vs\build\cmake\windows-msvc-ninja\CineRTX.exe `
       --input .\.vs\gemini_generated_video_6eaef4a7.mp4 `
       --output .\.vs\build\p9_real_rife_120fps.mp4 `
       --target-fps 120 `
       --rife-engine models\rife_v4.25.engine `
       --frames 30 `
       --json
   ```
3. **指标验收门禁 (Gate 9.3)**：
   - 进程退出码必须为 `0`；
   - JSON 输出中 `"fruc_backend": "rife-trt"`；
   - `"frames_interpolated"` 达到预期数量（如 116 帧）；
   - `"fruc_failures": 0`（零插帧失败）；
   - `"scene_cuts": 0`（零硬切误判）；
   - 使用 `ffprobe` 验证容器元数据：
     ```powershell
     ffprobe -v error -select_streams v:0 -show_entries stream=r_frame_rate,avg_frame_rate .\.vs\build\p9_real_rife_120fps.mp4
     ```
     断言输出严格为 `r_frame_rate=120/1` 与 `avg_frame_rate=120/1`。

---

### 任务 9.1-D3：连续帧逐帧 MAE 互异性物理检验 (Gate 9.4)

运行 Python 检验脚本，提取 `p9_real_rife_120fps.mp4` 前 35 帧计算相邻帧间绝对平均差（MAE）：
```powershell
python tools/p9_verify_mae.py --video .\.vs\build\p9_real_rife_120fps.mp4 --frames 35
```
**门禁判定标准**：
- 相邻帧对间 MAE 均大于 0；
- 不存在连续 4 帧 $\text{MAE} < 0.5$ 的平坦假死阶跃；
- 形成完整的连续帧差异表格，归档于终审复验报告中。

---

### 任务 9.1-D4：编制脱敏终审验收报告与专家呈报书

基于实机测试日志和 MAE 真实数据，在 `docs/phase9/` 下重新签发：
- `docs/phase9/CineRTX_Phase9.1_终审复验报告_真权重RIFE120fps全链路根治.md`
- `docs/phase9/CineRTX_Phase9.1_专家终审验收与战略封板委托书.md`
全面通过 Gate 9.1 ~ Gate 9.6 门禁，呈报领域专家组签署战略封板。

---

## 三、 交付验收标准 (DoD)

- [ ] 全仓 `docs/` 与根级 README 隐私脱敏扫描 0 命中；
- [ ] Phase 9 历史与定稿文档归位于 `docs/phase9/`，`docs/README.md` 全景索引更新；
- [ ] 零测试环境变量下成功完成 120fps 转码，输出视频经 ffprobe 确认为 120fps；
- [ ] 逐帧 MAE 检验彻底粉碎 1:5 重复帧；
- [ ] 终审复验报告与呈报委托书按规范交付。
