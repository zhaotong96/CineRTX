# CineRTX Phase 10：WebUI 施工单 10W-1 — SSOT 参数契约、全模态编码选项与预设映射

- **施工单编号**：`CINERTX-P10-WUI-TASK-10W1`
- **对应评审缺陷**：`WUI-P10-01`（P0 级阻断缺陷）
- **母本来源**：[`08_CineRTX_Phase10_WebUI_v2重构评审与三端对齐SuperPower规范.md`](08_CineRTX_Phase10_WebUI_v2重构评审与三端对齐SuperPower规范.md)
- **施工目标**：将 WebUI v2 与 C++26 核心（Track 10B/10C/10D）的 20 项 SSOT 参数、全模态编码器（`av1_cpu` / `hevc_cpu`）、码率控制（`cq` / `crf` / `qp`）及管线拓扑（`vsr-first` / `fg-first`）实现 1:1 无缝对齐。

---

### 一、 核心病灶与根因分析

1. **SSOT 参数容量断层**：C++ 侧 `include/rtx/i18n.h`（125 项词条）与 `include/rtx/param_schema.h` 已明确扩充至 20 项参数，但 `webui/index.html` 仍标有 `19 · SSOT` 徽章，缺失第 20 项参数 `pipelineOrder`。
2. **编码器与码率控制枚举缺位**：
   - `#ctl-codec` 缺失 `av1_cpu`（`libsvtav1` 10-bit P010）与 `hevc_cpu`（`libx265` 10-bit P010）；
   - `#ctl-rc` 缺失 Phase 10 默认的 `cq`（恒定感知画质 + Spatial AQ）以及 `crf`（纯 CPU 软编）；
   - `#ctl-qp` 的显隐条件写死 `profile.rateControl=qp`，导致选择 `cq` 或 `crf` 时画质输入框直接消失。
3. **推导规则与预设遗漏**：`webui/app.js` 的 `OUTPUT_SUFFIX` 与 `looksAutoFor` 未涵盖 CPU 编码器后缀，`PRESET_FIELDS` 遗漏了 `rateControl`、`qp` 与 `pipelineOrder`。

---

### 二、 施工改造步骤

#### 1. 改造 `webui/index.html`（预设卡片、参数检查器与 NLE 面板）

- **位置 A（左栏 5 张预设卡片，`index.html:122-168`）**：
  为全部 5 张预设卡片注入 `data-rate-control="cq"`、`data-qp="22"`、`data-pipeline-order="vsr-first"`（其中 `cpu-master` 预设配置 `data-rate-control="crf"`、`data-qp="20"`、`data-codec="av1_cpu"`）：
  ```html
  <div class="preset-list" id="preset-list">
    <button type="button" class="preset-card active" data-preset="master"
            data-codec="av1" data-preset-level="5" data-rate-control="cq" data-qp="22"
            data-resolution="4k" data-vsr-enabled="true" data-vsr-quality="4"
            data-fps-mode="60-nvofa" data-pipeline-order="vsr-first" data-peak-nits="1000">
      <span class="preset-icon">★</span>
      <div class="preset-text">
        <span class="preset-name">4K 60fps AV1 HDR10 母版</span>
        <span class="preset-meta">默认最优 · AV1 CQ22 / 4K / NVOFA 60</span>
      </div>
    </button>
    <button type="button" class="preset-card" data-preset="rife"
            data-codec="av1" data-preset-level="6" data-rate-control="cq" data-qp="20"
            data-resolution="4k" data-vsr-enabled="true" data-vsr-quality="4"
            data-fps-mode="60-rife" data-pipeline-order="vsr-first" data-peak-nits="1000">
      <span class="preset-icon">◆</span>
      <div class="preset-text">
        <span class="preset-name">4K 60fps RIFE 深度学习臻彩</span>
        <span class="preset-meta">RIFE v4.25 · VSR->FG · CQ20</span>
      </div>
    </button>
    <button type="button" class="preset-card" data-preset="esports"
            data-codec="av1" data-preset-level="4" data-rate-control="cq" data-qp="22"
            data-resolution="4k" data-vsr-enabled="true" data-vsr-quality="4"
            data-fps-mode="120-nvofa" data-pipeline-order="vsr-first" data-peak-nits="1000">
      <span class="preset-icon">⚡</span>
      <div class="preset-text">
        <span class="preset-name">4K 120fps 电竞高帧率</span>
        <span class="preset-meta">120 FPS · VSR->FG 极速拓扑</span>
      </div>
    </button>
    <button type="button" class="preset-card" data-preset="compat"
            data-codec="hevc" data-preset-level="5" data-rate-control="cq" data-qp="22"
            data-resolution="2k" data-vsr-enabled="true" data-vsr-quality="3"
            data-fps-mode="60-nvofa" data-pipeline-order="vsr-first" data-peak-nits="1000">
      <span class="preset-icon">🎬</span>
      <div class="preset-text">
        <span class="preset-name">2K 60fps HEVC 广泛兼容</span>
        <span class="preset-meta">HEVC Main10 CQ22 / 2K / VSR L3</span>
      </div>
    </button>
    <button type="button" class="preset-card" data-preset="cpu-master"
            data-codec="av1_cpu" data-preset-level="5" data-rate-control="crf" data-qp="20"
            data-resolution="native" data-vsr-enabled="false" data-vsr-quality="4"
            data-fps-mode="original" data-pipeline-order="vsr-first" data-peak-nits="1000">
      <span class="preset-icon">🎞</span>
      <div class="preset-text">
        <span class="preset-name">原生分辨率 CPU CRF 母版</span>
        <span class="preset-meta">libsvtav1 CRF20 · TrueHDR 无损</span>
      </div>
    </button>
  </div>
  ```

- **位置 B（右栏参数检查器，`index.html:280-408`）**：
  1. 徽章更正为 `<span class="badge-accent" id="inspector-badge">20 · SSOT</span>`；
  2. `#ctl-codec` 补齐 CPU 编码器选项：
     ```html
     <select id="ctl-codec" data-bind="profile.codec">
       <option value="av1" data-i18n="OptCodecAv1">AV1 10-bit (NVENC)</option>
       <option value="hevc" data-i18n="OptCodecHevc">HEVC Main10 (NVENC)</option>
       <option value="av1_cpu" data-i18n="OptCodecAv1Cpu">AV1 10-bit CPU (libsvtav1)</option>
       <option value="hevc_cpu" data-i18n="OptCodecHevcCpu">HEVC 10-bit CPU (libx265)</option>
     </select>
     ```
  3. `#ctl-rc` 补齐 `cq` 与 `crf`，扩展 `#ctl-qp` 联动显隐条件：
     ```html
     <select id="ctl-rc" data-bind="profile.rateControl">
       <option value="cq" data-i18n="OptRcCq">CQ 恒定感知画质 (Spatial AQ)</option>
       <option value="vbr" data-i18n="OptRcVbr">VBR 动态码率</option>
       <option value="cbr" data-i18n="OptRcCbr">CBR 恒定码率</option>
       <option value="qp" data-i18n="OptRcQp">Constant QP (I/P/B 阶梯)</option>
       <option value="crf" data-i18n="OptRcCrf">CRF 纯 CPU 母版软编</option>
     </select>
     ```
     ```html
     <div class="param-cell" data-visible-when="profile.rateControl=cq||profile.rateControl=qp||profile.rateControl=crf">
       <label for="ctl-qp" data-i18n="ParamCq">CQ / QP / CRF 值</label>
       <input id="ctl-qp" type="number" min="0" max="51" data-bind="profile.qp" data-type="number">
     </div>
     ```
  4. 新增第 20 项参数 `pipelineOrder`：
     ```html
     <div class="param-cell full">
       <label for="ctl-pipeline-order" data-i18n="ParamPipelineOrder">管线拓扑顺序</label>
       <select id="ctl-pipeline-order" data-bind="profile.pipelineOrder">
         <option value="vsr-first" data-i18n="OptPipeVsrFirst">VSR -> FG (先超分后补帧 - 默认)</option>
         <option value="fg-first" data-i18n="OptPipeFgFirst">FG -> VSR (先补帧后超分)</option>
       </select>
     </div>
     ```
  5. 在 `#ws-nle`（`index.html:619`）同步补齐 `nle-pipeline-order` 控件。

#### 2. 改造 `webui/app.js` 与 `webui/state.json`

- **扩展输出后缀表（`app.js:252-257`）**：
  ```javascript
  const OUTPUT_SUFFIX = {
    av1: "_remaster_av1.mp4",
    av1_cpu: "_remaster_av1.mp4",
    hevc: "_remaster_hevc.mp4",
    hevc_cpu: "_remaster_hevc.mp4",
  };
  ```
- **扩展预设映射字段（`app.js:656-660`）**：
  ```javascript
  const PRESET_FIELDS = {
    codec: "codec", presetLevel: "preset", rateControl: "rateControl", qp: "qp",
    resolution: "resolution", vsrEnabled: "vsrEnabled", vsrQuality: "vsrQuality",
    fpsMode: "fpsMode", pipelineOrder: "pipelineOrder", peakNits: "peakNits",
  };
  ```
- **更新 `webui/state.json` 默认 profile**：
  将 `rateControl` 置为 `"cq"`，`qp` 置为 `22`，新增 `"pipelineOrder": "vsr-first"`。

---

### 三、 验收检查点（Gate 10W-1）

- [ ] `rg "av1_cpu|hevc_cpu" webui/index.html webui/app.js` 匹配 $\ge 4$ 处；
- [ ] `rg "OptRcCq|OptRcCrf|ParamPipelineOrder|OptPipeVsrFirst|OptPipeFgFirst" webui/index.html` 100% 命中；
- [ ] 切换预设卡片（如点击 `4K 60fps RIFE` 或 `CPU CRF 母版`），检查器各字段实时联动并正确呈现 CQ/CRF 值输入框；
- [ ] `webui/state.json` 包含完备的 20 项 SSOT 字段。
