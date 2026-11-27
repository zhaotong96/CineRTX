// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 CineRTX contributors
//
// CineRTX WebUI v2 — single JS file, zero dependencies (Phase 10 20-param SSOT).
//
// Architecture: ONE JSON document (`state`, mirrored 1:1 by state.json on the
// bridge) is the single source of truth.  Three workstation shapes (encoder /
// player / nle) are views over that same tree, sharing the same binding engine,
// the same kParamSchema contract and the same SSE runtime events:
//
//   DOM input  -> set(path, value) -> debounced PUT /api/state  (persist)
//   backend    -> SSE JSON events -> state.runtime / queue merge (render)
//   media      -> GET /api/media?path=... (HTTP Range 206)       (<video>)

"use strict";

/* ========================================================================= *
 *  i18n & parameter metadata — C++ SSOT via GET /api/schema
 * ========================================================================= */

const WUI_I18N_DEFAULTS = {
  en: {
    AppTitle: "CineRTX Professional GPU Remaster & NLE Engine",
    WuiSubtitle: "WebUI · JSON state-driven · fully decoupled from the backend",
    WuiNavEncoder: "Encoder",
    WuiNavPlayer: "Player",
    WuiNavNle: "NLE",
    WuiEngine: "Engine",
    WuiProbeRun: "Run probe",
    LangToggleHint: "Ctrl+L switches language",
    StatusReady: "Ready",
    StatusWorking: "Transcoding in progress...",
    StatusStopping: "Stopping transcode...",
    StatusCancelled: "Task cancelled",
    StatusFailed: "Transcode failed",
    StatusDonePrefix: "Done",
    StatusSamePath: "Input and output paths are identical; choose another file.",
    StatusDropFileOnly: "Drop or select a video file first.",
    StatusGpuProbing: "Probing GPU / NVENC...",
    StatusProbeOk: "Hardware probe OK",
    WuiPresetsTitle: "RTX Presets",
    WuiPresetMasterName: "4K 60fps AV1 HDR10 Master",
    WuiPresetMasterMeta: "Default Optimal · AV1 CQ22 / 4K / NVOFA 60",
    WuiPresetRifeName: "4K 60fps RIFE Deep Learning Master",
    WuiPresetRifeMeta: "RIFE v4.25 · VSR->FG · CQ20",
    WuiPresetEsportsName: "4K 120fps Esports High-FPS",
    WuiPresetEsportsMeta: "120 FPS · VSR->FG Fast Topology",
    WuiPresetCompatName: "2K 60fps HEVC Universal Compat",
    WuiPresetCompatMeta: "HEVC Main10 CQ22 / 2K / VSR L3",
    WuiPresetCpuMasterName: "Native Res CPU CRF Master",
    WuiPresetCpuMasterMeta: "libsvtav1 CRF20 · TrueHDR Lossless",
    WuiQueueTitle: "Batch Queue",
    BtnQueueAdd: "+ Enqueue",
    WuiBtnBatchImport: "Batch Multi-Import",
    BtnQueueUp: "↑ Up",
    BtnQueueDown: "↓ Down",
    BtnQueueRemove: "- Remove",
    WuiBtnQueueRetry: "↻ Retry",
    WuiBtnQueueClear: "Clear Completed",
    BtnQueueSkip: "⏭ Skip Current",
    ButtonStop: "⏹ Stop Queue",
    ButtonStart: "▶ Start Transcode",
    WuiColStatus: "Status",
    WuiColInput: "Input Files",
    WuiColPreset: "Codec & Preset",
    WuiColFruc: "FRUC",
    WuiColHdr: "HDR Peak",
    WuiColOutput: "Output",
    WuiColProgress: "Progress",
    WuiQueueEmptyTitle: "Queue is empty",
    WuiQueueEmptyHint: "Click 'Start Transcode' to run a single job with current profile",
    TelemetryTitle: "Task Telemetry",
    WuiAbViewport: "A/B Compare Viewport",
    StatThroughput: "Throughput",
    StatFrames: "Frames",
    StatElapsed: "Elapsed",
    StatZeroCopy: "Zero-Copy",
    ValueZeroCopyClean: "D2H 0 B - H2D 0 B",
    ValueHostCopies: "Host copies detected",
    ValueRunning: "running",
    ValueDash: "—",
    ProgressIdle: "—",
    WuiProbeTitle: "Hardware Probe",
    WuiReportTitle: "Job Report",
    WuiLogTitle: "Engine Log",
    WuiParamsTitle: "Parameters",
    WuiInspectorDraft: "Draft Mode · Edit parameters then click '+ Enqueue'",
    WuiInspectorEditingPrefix: "Editing Pending Job",
    WuiInspectorReadonlyPrefix: "Inspecting Job (Locked)",
    WuiBtnQueueDeselect: "Back to Draft",
    WuiWarnRemoveRunning: "Cannot remove a running job directly; use 'Skip' or 'Stop'",
    WuiGroupIo: "Input / Output",
    WuiGroupEncoder: "Encoder",
    WuiGroupVsrFruc: "Super Resolution & FRUC",
    WuiGroupHdr: "HDR Color Science",
    ParamInput: "Input File",
    ParamOutput: "Output File",
    BtnBrowse: "Browse...",
    ParamCodec: "Video Codec",
    OptCodecAv1: "AV1 10-bit (NVENC)",
    OptCodecHevc: "HEVC Main10 (NVENC)",
    OptCodecAv1Cpu: "AV1 10-bit CPU (libsvtav1)",
    OptCodecHevcCpu: "HEVC 10-bit CPU (libx265)",
    ParamPreset: "Preset",
    OptPresetP1: "P1 Fastest",
    OptPresetP2: "P2 Faster",
    OptPresetP3: "P3 Fast",
    OptPresetP4: "P4 Medium",
    OptPresetP5: "P5 Balanced",
    OptPresetP6: "P6 High Quality",
    OptPresetP7: "P7 Max Quality",
    ParamRateControl: "Rate Control",
    OptRcCq: "CQ Perceptual Quality (Spatial AQ)",
    OptRcVbr: "VBR Variable Bitrate",
    OptRcCbr: "CBR Constant Bitrate",
    OptRcQp: "Constant QP (I/P/B Step)",
    OptRcCrf: "CRF Pure CPU Master",
    ParamBitrate: "Bitrate (kbps)",
    ParamCq: "CQ / QP / CRF Value",
    ParamResolution: "Resolution",
    OptRes4k: "4K UHD",
    OptRes2k: "2K QHD",
    OptResFhd: "FHD 1080p",
    OptResNative: "Native",
    ParamAspect: "Aspect Ratio",
    OptGeomAuto: "Auto Detect",
    OptGeomPillar: "Pillarbox",
    ParamVsrQuality: "VSR Quality",
    OptVsrQ1: "Level 1",
    OptVsrQ2: "Level 2",
    OptVsrQ3: "Level 3",
    OptVsrQ4: "Level 4",
    ParamVsr: "Enable RTX VSR",
    WuiParamFps: "Frame Rate (FRUC)",
    OptFpsOriginal: "Original FPS",
    OptFps60Nvofa: "60 FPS NVOFA",
    OptFps120Nvofa: "120 FPS NVOFA",
    OptFps60Rife: "60 FPS RIFE",
    OptFps120Rife: "120 FPS RIFE",
    ParamPipelineOrder: "Pipeline Topology Order",
    OptPipeVsrFirst: "VSR -> FG (Upscale First - Default)",
    OptPipeFgFirst: "FG -> VSR (Interpolate First)",
    WuiParamRifeEngine: "RIFE Engine",
    ParamPeakNits: "Peak Luminance (nits)",
    WuiParamGpu: "GPU Index",
    WuiParamFrames: "Frame Limit",
    WuiParamLut: "3D LUT",
    ParamAudioCopy: "Audio Passthrough",
    WuiParamHdr10plus: "HDR10+",
    WuiAutoOutput: "Auto Output Name",
    WuiPlayerPlaceholder: "Open media to begin A/B quality review",
    WuiPlayerOpen: "📂 Open",
    WuiAbSplit: "Split",
    WuiAbRemaster: "Remaster",
    WuiAbOriginal: "Original",
    WuiPlayerSend: "⚡ Send to Encoder",
    WuiNleBinTitle: "Project Bin",
    WuiNleTabMedia: "Media",
    WuiNleImport: "+ Import Media",
    WuiNleMonitorTitle: "Program Monitor",
    WuiNleEffectsTitle: "Effect Controls",
    WuiNleGroupTrim: "Time Trim",
    WuiNleInMs: "In Point (ms)",
    WuiNleOutMs: "Out Point (ms)",
    WuiNleExport: "🚀 Export to Encoder Queue",
    WuiNleToolSelect: "V Select",
    WuiNleToolRazor: "C Razor",
    WuiFsGo: "Go",
    WuiFsUp: "Up",
    WuiFsSelectAll: "Select All Videos",
    WuiFsSelect: "Select",
    WuiFoot: "state.json · JSON Pipe (HTTP + SSE)",
    WuiProbeUnavailable: "Probe unavailable",
    WuiReqFailed: "Request failed",
  },
  zh: {
    AppTitle: "CineRTX 纯 GPU 专业转码与非编引擎",
    WuiSubtitle: "WebUI · JSON 状态驱动 · 与后端彻底解耦",
    WuiNavEncoder: "转码器",
    WuiNavPlayer: "播放器",
    WuiNavNle: "非编",
    WuiEngine: "引擎",
    WuiProbeRun: "运行探针",
    LangToggleHint: "Ctrl+L 切换语言",
    StatusReady: "已就绪",
    StatusWorking: "正在转码...",
    StatusStopping: "正在停止转码...",
    StatusCancelled: "任务已取消",
    StatusFailed: "转码失败",
    StatusDonePrefix: "完成",
    StatusSamePath: "输入与输出路径相同，请选择其他文件。",
    StatusDropFileOnly: "请先拖入或选择视频文件。",
    StatusGpuProbing: "正在探测 GPU / NVENC...",
    StatusProbeOk: "硬件探针正常",
    WuiPresetsTitle: "RTX 预设",
    WuiPresetMasterName: "4K 60fps AV1 HDR10 母版",
    WuiPresetMasterMeta: "默认最优 · AV1 CQ22 / 4K / NVOFA 60",
    WuiPresetRifeName: "4K 60fps RIFE 深度学习臻彩",
    WuiPresetRifeMeta: "RIFE v4.25 · VSR->FG · CQ20",
    WuiPresetEsportsName: "4K 120fps 电竞高帧率",
    WuiPresetEsportsMeta: "120 FPS · VSR->FG 极速拓扑",
    WuiPresetCompatName: "2K 60fps HEVC 广泛兼容",
    WuiPresetCompatMeta: "HEVC Main10 CQ22 / 2K / VSR L3",
    WuiPresetCpuMasterName: "原生分辨率 CPU CRF 母版",
    WuiPresetCpuMasterMeta: "libsvtav1 CRF20 · TrueHDR 无损",
    WuiQueueTitle: "编码队列",
    BtnQueueAdd: "+ 加入队列",
    WuiBtnBatchImport: "批量多选导入",
    BtnQueueUp: "↑ 上移",
    BtnQueueDown: "↓ 下移",
    BtnQueueRemove: "- 移除任务",
    WuiBtnQueueRetry: "↻ 重试",
    WuiBtnQueueClear: "清空已完成",
    BtnQueueSkip: "⏭ 跳过当前",
    ButtonStop: "⏹ 停止队列",
    ButtonStart: "▶ 开始转码",
    WuiColStatus: "状态",
    WuiColInput: "输入文件",
    WuiColPreset: "编码预设",
    WuiColFruc: "FRUC",
    WuiColHdr: "HDR 峰值",
    WuiColOutput: "输出",
    WuiColProgress: "进度",
    WuiQueueEmptyTitle: "队列为空",
    WuiQueueEmptyHint: "直接点击「开始转码」以当前配置单任务启动",
    TelemetryTitle: "任务遥测",
    WuiAbViewport: "A/B 对比视口",
    StatThroughput: "吞吐",
    StatFrames: "帧数",
    StatElapsed: "耗时",
    StatZeroCopy: "零拷贝",
    ValueZeroCopyClean: "D2H 0 B - H2D 0 B",
    ValueHostCopies: "检测到主机拷贝",
    ValueRunning: "运行中",
    ValueDash: "—",
    ProgressIdle: "—",
    WuiProbeTitle: "硬件探针",
    WuiReportTitle: "任务报告",
    WuiLogTitle: "引擎日志",
    WuiParamsTitle: "参数",
    WuiInspectorDraft: "草稿模式 · 修改后点击「+ 加入队列」",
    WuiInspectorEditingPrefix: "正在编辑排队任务",
    WuiInspectorReadonlyPrefix: "检视锁定任务",
    WuiBtnQueueDeselect: "返回新任务草稿",
    WuiWarnRemoveRunning: "无法直接删除正在运行的任务，请使用「跳过」或「停止」",
    WuiGroupIo: "输入 / 输出",
    WuiGroupEncoder: "编码器",
    WuiGroupVsrFruc: "超分与补帧",
    WuiGroupHdr: "HDR 色彩科学",
    ParamInput: "输入文件",
    ParamOutput: "输出文件",
    BtnBrowse: "浏览...",
    ParamCodec: "编码格式",
    OptCodecAv1: "AV1 10-bit (NVENC)",
    OptCodecHevc: "HEVC Main10 (NVENC)",
    OptCodecAv1Cpu: "AV1 10-bit CPU (libsvtav1)",
    OptCodecHevcCpu: "HEVC 10-bit CPU (libx265)",
    ParamPreset: "预设",
    OptPresetP1: "P1 最快",
    OptPresetP2: "P2 更快",
    OptPresetP3: "P3 快速",
    OptPresetP4: "P4 中等",
    OptPresetP5: "P5 平衡",
    OptPresetP6: "P6 高质",
    OptPresetP7: "P7 极致",
    ParamRateControl: "码率控制",
    OptRcCq: "CQ 恒定感知画质 (Spatial AQ)",
    OptRcVbr: "VBR 动态码率",
    OptRcCbr: "CBR 恒定码率",
    OptRcQp: "Constant QP (I/P/B 阶梯)",
    OptRcCrf: "CRF 纯 CPU 母版软编",
    ParamBitrate: "码率 (kbps)",
    ParamCq: "CQ / QP / CRF 值",
    ParamResolution: "分辨率",
    OptRes4k: "4K UHD",
    OptRes2k: "2K QHD",
    OptResFhd: "FHD 1080p",
    OptResNative: "原生",
    ParamAspect: "画幅",
    OptGeomAuto: "自动检测",
    OptGeomPillar: "Pillarbox",
    ParamVsrQuality: "VSR 档位",
    OptVsrQ1: "Level 1",
    OptVsrQ2: "Level 2",
    OptVsrQ3: "Level 3",
    OptVsrQ4: "Level 4",
    ParamVsr: "启用 RTX VSR",
    WuiParamFps: "帧率 (FRUC)",
    OptFpsOriginal: "原帧率",
    OptFps60Nvofa: "60 FPS NVOFA",
    OptFps120Nvofa: "120 FPS NVOFA",
    OptFps60Rife: "60 FPS RIFE",
    OptFps120Rife: "120 FPS RIFE",
    ParamPipelineOrder: "管线拓扑顺序",
    OptPipeVsrFirst: "VSR -> FG (先超分后补帧 - 默认)",
    OptPipeFgFirst: "FG -> VSR (先补帧后超分)",
    WuiParamRifeEngine: "RIFE 引擎",
    ParamPeakNits: "峰值亮度 (nits)",
    WuiParamGpu: "GPU 序号",
    WuiParamFrames: "帧数上限",
    WuiParamLut: "3D LUT",
    ParamAudioCopy: "音频直通",
    WuiParamHdr10plus: "HDR10+",
    WuiAutoOutput: "自动输出名",
    WuiPlayerPlaceholder: "打开媒体开始 A/B 画质审看",
    WuiPlayerOpen: "📂 打开",
    WuiAbSplit: "分屏",
    WuiAbRemaster: "重制",
    WuiAbOriginal: "原片",
    WuiPlayerSend: "⚡ 送入转码器",
    WuiNleBinTitle: "素材箱",
    WuiNleTabMedia: "素材",
    WuiNleImport: "+ 导入素材",
    WuiNleMonitorTitle: "节目监视器",
    WuiNleEffectsTitle: "特效控制台",
    WuiNleGroupTrim: "时间裁剪",
    WuiNleInMs: "入点 (ms)",
    WuiNleOutMs: "出点 (ms)",
    WuiNleExport: "🚀 导出至转码器渲染",
    WuiNleToolSelect: "V 选择",
    WuiNleToolRazor: "C 剃刀",
    WuiFsGo: "跳转",
    WuiFsUp: "上级",
    WuiFsSelectAll: "全选视频",
    WuiFsSelect: "选择",
    WuiFoot: "state.json · JSON 通信 (HTTP + SSE)",
    WuiProbeUnavailable: "探针不可用",
    WuiReqFailed: "请求失败",
  },
};

let I18N = {
  en: Object.assign({}, WUI_I18N_DEFAULTS.en),
  zh: Object.assign({}, WUI_I18N_DEFAULTS.zh),
};
let DEFAULTS = {
  input: "",
  output: "",
  codec: "av1",
  preset: 5,
  rateControl: "cq",
  bitrateKbps: 0,
  qp: 22,
  resolution: "4k",
  geometry: "auto",
  vsrEnabled: true,
  vsrQuality: 4,
  peakNits: 1000,
  fpsMode: "60-nvofa",
  pipelineOrder: "vsr-first",
  rifeEngine: "",
  copyAudio: true,
  hdr10plus: false,
  lut: "",
  gpu: 0,
  frames: 0,
};
let PARAMS = [];
const FALLBACK = {};
const SECTION_KEYS = ["ui", "profile", "engine", "queue", "player", "nle"];
const WORKSPACES = ["encoder", "player", "nle"];
const VIDEO_EXT_RE = /\.(mp4|m4v|mov|mkv|webm|avi|ts|m2ts)$/i;

function captureFallback() {
  document.querySelectorAll("[data-i18n]").forEach((el) => {
    FALLBACK[el.dataset.i18n] = el.textContent;
  });
  document.querySelectorAll("[data-i18n-title]").forEach((el) => {
    FALLBACK[el.dataset.i18nTitle] = el.title;
  });
  FALLBACK.AppTitle = document.title;
}

async function loadSchema() {
  const doc = await api("/api/schema", "GET");
  const remoteI18n = doc.i18n || {};
  I18N = {
    en: Object.assign({}, WUI_I18N_DEFAULTS.en, remoteI18n.en || {}),
    zh: Object.assign({}, WUI_I18N_DEFAULTS.zh, remoteI18n.zh || {}),
  };
  DEFAULTS = Object.assign({}, DEFAULTS, doc.defaults || {}, {
    rateControl: (doc.defaults && doc.defaults.rateControl && doc.defaults.rateControl !== "cbr")
      ? doc.defaults.rateControl : "cq",
    qp: (doc.defaults && doc.defaults.qp && doc.defaults.qp !== 24)
      ? doc.defaults.qp : 22,
    pipelineOrder: (doc.defaults && doc.defaults.pipelineOrder) || "vsr-first",
  });
  PARAMS = doc.params || [];
  applyParamBounds();
}

function applyParamBounds() {
  for (const meta of PARAMS) {
    if (meta.type !== "int") continue;
    const el = document.querySelector(`[data-bind="profile.${meta.key}"][type="number"]`);
    if (!el) continue;
    if (Number.isFinite(meta.min)) el.min = String(meta.min);
    if (Number.isFinite(meta.max)) el.max = String(meta.max);
  }
}

/* ========================================================================= *
 *  Store — one JSON document with path get/set + prefix-matched subscribers.
 * ========================================================================= */

const state = {
  version: 1,
  ui: { lang: "zh", autoOutput: true, showLog: true, workspace: "encoder" },
  engine: { path: "CineRTX.exe", workDir: "" },
  profile: Object.assign({}, DEFAULTS),
  queue: [],
  player: {
    input: "", abMode: "split", splitPos: 50, osd: true, loop: true,
    speed: 1.0, audioTrack: 0, subTrack: -1,
  },
  nle: {
    sequenceName: "4K_60FPS_HDR10_Master_Seq", playheadMs: 0, inMs: 0,
    outMs: 10000, zoom: 100, tool: "select", selectedClipId: "clip-1",
    mediaBin: [], tracks: [],
  },
  runtime: {
    probe: null,
    engine: { found: false, resolved: "", version: "" },
    job: {
      status: "idle", startedAt: null, endedAt: null, exitCode: null, error: null,
      progress: { pct: 0, frames: 0, total: 0, fps: 0 },
      report: null, logTail: [],
    },
  },
};

const subscribers = [];

function getPath(path) {
  return path.split(".").reduce((node, key) => (node == null ? undefined : node[key]), state);
}

function setPath(path, value) {
  const keys = path.split(".");
  const last = keys.pop();
  let node = state;
  for (const key of keys) {
    if (typeof node[key] !== "object" || node[key] === null) node[key] = {};
    node = node[key];
  }
  node[last] = value;
  notify(path);
}

function notify(changed) {
  for (const sub of subscribers) {
    if (sub.path === changed ||
        changed.startsWith(sub.path + ".") ||
        sub.path.startsWith(changed + ".")) {
      sub.fn();
    }
  }
}

function subscribe(path, fn) {
  subscribers.push({ path, fn });
  fn();
}

/* ========================================================================= *
 *  Persistence — debounced PUT + guaranteed immediate flushPersist.
 * ========================================================================= */

let persistTimer = null;
let applyingRemote = false;

function ownedSections() {
  return {
    version: state.version,
    ui: state.ui,
    engine: state.engine,
    profile: state.profile,
    player: state.player,
    nle: state.nle,
  };
}

function schedulePersist() {
  if (applyingRemote || persistTimer) return;
  persistTimer = setTimeout(async () => {
    persistTimer = null;
    try {
      await api("/api/state", "PUT", ownedSections());
    } catch (err) {
      setStatusMessage(`${tr("WuiReqFailed")}: ${err.message}`);
    }
  }, 250);
}

async function flushPersist() {
  if (persistTimer) {
    clearTimeout(persistTimer);
    persistTimer = null;
  }
  await api("/api/state", "PUT", ownedSections()).catch(() => {});
}

async function api(path, method, body) {
  const opts = { method, headers: {} };
  if (body !== undefined) {
    opts.headers["Content-Type"] = "application/json";
    opts.body = JSON.stringify(body);
  }
  const res = await fetch(path, opts);
  const doc = await res.json().catch(() => ({}));
  if (!res.ok || doc.ok === false) {
    throw new Error(doc.error || `${res.status}`);
  }
  return doc;
}

function toast(msg, level) {
  setStatusMessage(msg);
  const line = document.getElementById("status-line");
  if (line && level === "warn") {
    line.dataset.toastLevel = "warn";
  }
}

/* ========================================================================= *
 *  Binding engine — DOM <-> state for every [data-bind] element.
 * ========================================================================= */

function kindOf(el) {
  if (el.type === "checkbox") return "bool";
  if (el.tagName === "SELECT") return el.dataset.type === "number" ? "num" : "str";
  if (el.type === "number" || el.type === "range" || el.dataset.type === "number") return "num";
  return "str";
}

function readControl(el) {
  const kind = kindOf(el);
  if (kind === "bool") return el.checked;
  if (kind === "num") {
    const value = Number(el.value);
    if (Number.isNaN(value)) return 0;
    const min = el.min === "" || el.min === undefined ? -Infinity : Number(el.min);
    const max = el.max === "" || el.max === undefined ? Infinity : Number(el.max);
    return Math.min(max, Math.max(min, value));
  }
  return el.value;
}

function writeControl(el, value) {
  const kind = kindOf(el);
  if (kind === "bool") {
    el.checked = Boolean(value);
  } else if (kind === "num") {
    if (document.activeElement !== el) el.value = String(value ?? 0);
  } else if (el.tagName === "SELECT") {
    const text = String(value ?? "");
    const option = el.querySelector ? el.querySelector(`option[value="${CSS.escape(text)}"]`) : null;
    if (el.value !== text && option) el.value = text;
  } else if (document.activeElement !== el) {
    el.value = value == null ? "" : String(value);
  }
}

function installBindings() {
  document.querySelectorAll("[data-bind]").forEach((el) => {
    const path = el.dataset.bind;
    el.addEventListener("input", () => {
      setPath(path, readControl(el));
      runEffects();
      schedulePersist();
    });
    el.addEventListener("change", () => {
      setPath(path, readControl(el));
      runEffects();
      schedulePersist();
    });
    subscribe(path, () => writeControl(el, getPath(path)));
  });
}

/* ========================================================================= *
 *  Derived effects — visibility / disable rules + auto output naming.
 * ========================================================================= */

const whenCache = new WeakMap();

function evalWhen(el, attr) {
  let expr = whenCache.get(el);
  if (expr === undefined) {
    expr = (el.dataset[attr] || "").split("||").map((term) => {
      const negated = term.startsWith("!");
      const [path, ...rest] = term.replace(/^!/, "").split("=");
      return { negated, path, value: rest.join("=") };
    }).filter((term) => term.path);
    whenCache.set(el, expr);
  }
  if (!expr.length) return true;
  return expr.some(({ negated, path, value }) => {
    const actual = getPath(path);
    const hit = String(actual) === value;
    return negated ? !hit : hit;
  });
}

const OUTPUT_SUFFIX = {
  av1: "_remaster_av1.mp4",
  av1_cpu: "_remaster_av1.mp4",
  hevc: "_remaster_hevc.mp4",
  hevc_cpu: "_remaster_hevc.mp4",
};
let prevInput = null;
let prevCodec = null;

function basename(path) {
  const cut = Math.max(path.lastIndexOf("/"), path.lastIndexOf("\\"));
  return cut >= 0 ? path.slice(cut + 1) : path;
}

function autoNameFor(input, codec) {
  if (!input) return "";
  const cut = Math.max(input.lastIndexOf("/"), input.lastIndexOf("\\"));
  const name = basename(input);
  const dot = name.lastIndexOf(".");
  const stem = dot > 0 ? name.slice(0, dot) : name;
  return (cut >= 0 ? input.slice(0, cut + 1) : "") + stem + (OUTPUT_SUFFIX[codec] || OUTPUT_SUFFIX.av1);
}

function looksAutoFor(prevInputValue, output) {
  if (!output) return true;
  if (!prevInputValue) return false;
  const outCut = Math.max(output.lastIndexOf("/"), output.lastIndexOf("\\"));
  const prevCut = Math.max(prevInputValue.lastIndexOf("/"), prevInputValue.lastIndexOf("\\"));
  const sameDir = (prevCut < 0 && outCut < 0) ||
    (prevCut >= 0 && outCut >= 0 &&
     output.slice(0, outCut + 1) === prevInputValue.slice(0, prevCut + 1));
  if (!sameDir) return false;
  const prevName = prevCut >= 0 ? prevInputValue.slice(prevCut + 1) : prevInputValue;
  const prevDot = prevName.lastIndexOf(".");
  const prevStem = prevDot > 0 ? prevName.slice(0, prevDot) : prevName;
  const outName = outCut >= 0 ? output.slice(outCut + 1) : output;
  return /^_remaster_(av1|hevc)\.mp4$/.test(outName.slice(prevStem.length));
}

function autoDeriveOutput() {
  const input = state.profile.input || "";
  const codec = state.profile.codec || "av1";
  if (prevInput === null) {
    prevInput = input;
    prevCodec = codec;
    return;
  }
  const changed = input !== prevInput || codec !== prevCodec;
  if (changed && state.ui.autoOutput &&
      looksAutoFor(prevInput, state.profile.output || "")) {
    setPath("profile.output", autoNameFor(input, codec));
  }
  prevInput = input;
  prevCodec = codec;
}

function runEffects() {
  document.querySelectorAll("[data-visible-when]").forEach((el) => {
    el.classList.toggle("wui-hidden", !evalWhen(el, "visibleWhen"));
  });
  const running = state.runtime.job.status === "running";
  document.querySelectorAll("[data-lock]").forEach((el) => {
    el.disabled = running;
  });
  document.querySelectorAll("[data-disable-when]").forEach((el) => {
    const hit = evalWhen(el, "disableWhen");
    el.classList.toggle("wui-disabled", hit);
    const controls = el.matches("input,select,button")
      ? [el]
      : el.querySelectorAll("input,select,button");
    controls.forEach((control) => {
      control.disabled = hit;
    });
  });
  const btnStart = document.getElementById("btn-start");
  const btnStop = document.getElementById("btn-stop");
  const btnSkip = document.getElementById("btn-skip");
  if (btnStart) btnStart.disabled = running;
  if (btnStop) btnStop.disabled = !running;
  if (btnSkip) btnSkip.disabled = !running;
  const logSec = document.getElementById("log-section");
  if (logSec) logSec.classList.toggle("wui-hidden", !state.ui.showLog);
  autoDeriveOutput();
}

/* ========================================================================= *
 *  Rendering — runtime views (status, progress, tiles, probe, report, log).
 * ========================================================================= */

function tr(id) {
  const table = I18N[state.ui.lang] || I18N.en || {};
  return table[id] ?? (I18N.en || {})[id] ?? FALLBACK[id] ?? id;
}

function applyI18n() {
  const table = I18N[state.ui.lang] || I18N.en || {};
  const resolved = (id) => table[id] != null || (I18N.en || {})[id] != null;
  document.querySelectorAll("[data-i18n]").forEach((el) => {
    const id = el.dataset.i18n;
    if (resolved(id) || FALLBACK[id] != null) el.textContent = tr(id);
  });
  document.querySelectorAll("[data-i18n-title]").forEach((el) => {
    const id = el.dataset.i18nTitle;
    if (resolved(id) || FALLBACK[id] != null) el.title = tr(id);
  });
  document.title = tr("AppTitle");
  document.documentElement.lang = state.ui.lang === "zh" ? "zh" : "en";
  renderInspectorContext();
}

function formatElapsed(ms) {
  if (!Number.isFinite(ms) || ms <= 0) return tr("ValueDash");
  if (ms < 1000) return `${ms.toFixed(0)} ms`;
  const total = ms / 1000;
  if (total < 60) return `${total.toFixed(1)} s`;
  return `${Math.floor(total / 60)}:${String(Math.round(total % 60)).padStart(2, "0")}`;
}

function renderStatus() {
  const job = state.runtime.job || {};
  const status = job.status || "idle";
  document.body.dataset.status = status;
  const line = document.getElementById("status-line");
  if (job.status === "running") {
    line.textContent = tr("StatusWorking");
  } else if (job.status === "done") {
    const report = job.report || {};
    line.textContent = `${tr("StatusDonePrefix")} · ${report.output_codec || ""} ${report.output_width || "?"}x${report.output_height || "?"} · ${formatElapsed(report.elapsed_ms)}`;
  } else if (job.status === "failed") {
    line.textContent = `${tr("StatusFailed")}${job.exitCode != null ? ` (exit ${job.exitCode})` : ""}`;
  } else if (job.status === "cancelled") {
    line.textContent = tr("StatusCancelled");
  } else {
    line.textContent = tr("StatusReady");
  }

  // Engine chip
  const chip = document.getElementById("engine-chip");
  const engine = state.runtime.engine || {};
  const engineState = document.getElementById("engine-state");
  chip.title = engine.resolved || "";
  chip.classList.remove("chip-on", "chip-off", "chip-run");
  if (status === "running") {
    chip.classList.add("chip-run");
    engineState.textContent = tr("ValueRunning");
  } else if (engine.found) {
    chip.classList.add("chip-on");
    engineState.textContent = engine.version || "OK";
  } else {
    chip.classList.add("chip-off");
    engineState.textContent = "not found";
  }

  // Global progress in header
  const gpStatus = document.getElementById("gp-status");
  const gpDetail = document.getElementById("gp-detail");
  const gpPct = document.getElementById("gp-pct");
  const gpCircle = document.getElementById("gp-circle");
  const prog = job.progress || {};
  const pct = Math.max(0, Math.min(100, prog.pct || 0));
  if (gpCircle) {
    const r = Number(gpCircle.getAttribute && gpCircle.getAttribute("r")) || 18;
    const circumference = +(2 * Math.PI * r).toFixed(2); // r=18 -> 113.10
    gpCircle.setAttribute("stroke-dasharray", String(circumference));
    gpCircle.setAttribute("stroke-dashoffset", String(+(circumference - (pct / 100) * circumference).toFixed(2)));
  }

  if (job.status === "running") {
    gpStatus.textContent = tr("StatusWorking");
    gpPct.textContent = `${Math.round(pct)}%`;
    gpDetail.textContent = `${prog.fps || 0} FPS · ${prog.frames || 0}f`;
  } else if (job.status === "done") {
    gpStatus.textContent = tr("StatusDonePrefix") || "完成";
    gpPct.textContent = "✓";
    gpDetail.textContent = formatElapsed((job.report || {}).elapsed_ms);
  } else if (job.status === "failed") {
    gpStatus.textContent = tr("StatusFailed");
    gpPct.textContent = "✗";
    gpDetail.textContent = "";
  } else {
    gpStatus.textContent = tr("StatusReady");
    gpPct.textContent = "—";
    gpDetail.textContent = "—";
  }
}

function renderProgress() {
  const job = state.runtime.job || {};
  const prog = job.progress || {};
  const pct = Math.max(0, Math.min(100, prog.pct || 0));
  document.getElementById("progress-fill").style.width = `${pct}%`;
  let text;
  if (job.status === "running") {
    text = prog.total > 0
      ? `${pct.toFixed(1)}% · ${prog.frames}/${prog.total} · ${prog.fps || 0} FPS`
      : `${prog.frames || 0} frames · ${prog.fps || 0} FPS`;
  } else if (job.status === "done") {
    text = "100%";
  } else {
    text = tr("ProgressIdle");
  }
  document.getElementById("progress-text").textContent = text;
}

function renderTiles() {
  const job = state.runtime.job || {};
  const prog = job.progress || {};
  const report = job.report || {};
  const running = job.status === "running";
  const fpsEl = document.getElementById("tile-fps");
  const framesEl = document.getElementById("tile-frames");
  const elapsedEl = document.getElementById("tile-elapsed");
  const zeroEl = document.getElementById("tile-zero");

  fpsEl.textContent = running
    ? `${prog.fps || 0} FPS`
    : (report.frames_encoded && report.elapsed_ms
        ? `${(report.frames_encoded * 1000 / report.elapsed_ms).toFixed(1)} FPS`
        : tr("ValueDash"));
  framesEl.textContent = running
    ? (prog.total > 0 ? `${prog.frames}/${prog.total}` : `${prog.frames || 0}`)
    : (report.frames_encoded != null
        ? `${report.frames_decoded ?? "—"}/${report.frames_encoded}`
        : tr("ValueDash"));
  elapsedEl.textContent = running
    ? (prog.fps > 0 && prog.frames ? formatElapsed(prog.frames / prog.fps * 1000) : tr("ValueRunning"))
    : (report.elapsed_ms != null ? formatElapsed(report.elapsed_ms) : tr("ValueDash"));
  if (report.zero_copy_main_loop === true) {
    zeroEl.textContent = tr("ValueZeroCopyClean");
  } else if (report.zero_copy_main_loop === false) {
    zeroEl.textContent = tr("ValueHostCopies");
  } else {
    zeroEl.textContent = tr("ValueDash");
  }
  syncQueueProgress(prog, job.status);
}

function renderReport() {
  const report = state.runtime.job && state.runtime.job.report;
  const view = document.getElementById("report-view");
  if (report && Object.keys(report).length) {
    view.textContent = JSON.stringify(report, null, 2);
  } else {
    view.textContent = tr("ValueDash");
  }
}

function renderLog() {
  const tail = (state.runtime.job && state.runtime.job.logTail) || [];
  const view = document.getElementById("log-view");
  const atBottom = view.scrollTop + view.clientHeight >= view.scrollHeight - 8;
  view.textContent = tail.map((entry) =>
    `[${entry.t}] ${entry.src}: ${entry.text}`).join("\n");
  if (atBottom) view.scrollTop = view.scrollHeight;
}

function firstProbeSummary(probe) {
  let summary = "";
  const walk = (node) => {
    if (summary) return;
    if (typeof node === "string" && /rtx|geforce|nvidia|cuda/i.test(node)) {
      summary = node;
    } else if (Array.isArray(node)) {
      node.every(walk);
    } else if (node && typeof node === "object") {
      Object.values(node).every(walk);
    }
    return !summary;
  };
  walk(probe);
  return summary;
}

function renderProbe() {
  const probe = state.runtime.probe;
  const view = document.getElementById("probe-view");
  const quick = document.getElementById("probe-quick");
  if (!probe) {
    view.hidden = true;
    quick.textContent = "";
    return;
  }
  view.hidden = false;
  view.textContent = JSON.stringify(probe, null, 2);
  if (probe.available && probe.probe) {
    quick.textContent = `${tr("StatusProbeOk")}${firstProbeSummary(probe.probe) ? " · " + firstProbeSummary(probe.probe) : ""}`;
  } else {
    quick.textContent = `${tr("WuiProbeUnavailable")}: ${probe.error || ""}`;
  }
}

function renderRuntime() {
  renderStatus();
  renderProgress();
  renderTiles();
  renderReport();
  renderLog();
  renderProbe();
  runEffects();
}

/* ========================================================================= *
 *  i18n + store wiring for ui.lang, workspace hotkeys.
 * ========================================================================= */

function isTypingTarget(event) {
  const tag = event.target && event.target.tagName;
  return tag === "INPUT" || tag === "TEXTAREA" || tag === "SELECT";
}

function installI18nBinding() {
  subscribe("ui.lang", () => {
    applyI18n();
    renderRuntime();
  });
  document.getElementById("btn-lang").addEventListener("click", () => {
    setPath("ui.lang", state.ui.lang === "zh" ? "en" : "zh");
    schedulePersist();
  });
  document.addEventListener("keydown", (event) => {
    if (event.ctrlKey && (event.key === "l" || event.key === "L")) {
      event.preventDefault();
      setPath("ui.lang", state.ui.lang === "zh" ? "en" : "zh");
      schedulePersist();
      return;
    }
    if (event.altKey && ["1", "2", "3"].includes(event.key)) {
      event.preventDefault();
      setWorkspace(WORKSPACES[Number(event.key) - 1]);
      return;
    }
    if (isTypingTarget(event)) return;
    const key = event.key.toLowerCase();
    if (key === "o" && state.ui.workspace === "player") {
      setPath("player.osd", !state.player.osd);
      schedulePersist();
    } else if (state.ui.workspace === "nle" && key === "i") {
      markInPoint();
    } else if (state.ui.workspace === "nle" && key === "o") {
      markOutPoint();
    } else if (state.ui.workspace === "nle" && key === "c") {
      razorSplit();
    } else if (state.ui.workspace === "nle" && key === "v") {
      setNleTool("select");
    }
  });
}

function setStatusMessage(text) {
  const line = document.getElementById("status-line");
  if (line) line.textContent = text;
}

/* ========================================================================= *
 *  Backend events — SSE
 * ========================================================================= */

function connectEvents() {
  const source = new EventSource("/api/events");
  source.onmessage = (event) => {
    let doc;
    try {
      doc = JSON.parse(event.data);
    } catch {
      return;
    }
    if (doc.type === "hello" || doc.type === "state-file") {
      applyingRemote = true;
      try {
        if (doc.state) {
          for (const section of SECTION_KEYS) {
            if (doc.state[section] !== undefined) setPath(section, doc.state[section]);
          }
        }
        if (doc.type === "hello" && doc.runtime) setPath("runtime", doc.runtime);
      } finally {
        applyingRemote = false;
      }
      runEffects();
      renderInspectorContext();
    } else if (doc.type === "runtime") {
      applyingRemote = true;
      try {
        setPath("runtime", doc.runtime);
      } finally {
        applyingRemote = false;
      }
      renderRuntime();
    }
  };
  source.onerror = () => {};
}

/* ========================================================================= *
 *  Workspace switcher
 * ========================================================================= */

function setWorkspace(name) {
  if (!WORKSPACES.includes(name)) return;
  setPath("ui.workspace", name);
  schedulePersist();
}

function renderWorkspace() {
  const ws = WORKSPACES.includes(state.ui.workspace) ? state.ui.workspace : "encoder";
  document.body.dataset.workspace = ws;
  document.querySelectorAll("[data-workspace-tab]").forEach((tab) => {
    tab.classList.toggle("active", tab.dataset.workspaceTab === ws);
  });
}

function installWorkspaces() {
  document.querySelectorAll("[data-workspace-tab]").forEach((tab) => {
    tab.addEventListener("click", () => setWorkspace(tab.dataset.workspaceTab));
  });
  subscribe("ui.workspace", renderWorkspace);
}

/* ========================================================================= *
 *  Encoder shape — presets + multi-file heterogeneous queue.
 * ========================================================================= */

const PRESET_FIELDS = {
  codec: "codec", presetLevel: "preset", rateControl: "rateControl", qp: "qp",
  resolution: "resolution", vsrEnabled: "vsrEnabled", vsrQuality: "vsrQuality",
  fpsMode: "fpsMode", pipelineOrder: "pipelineOrder", peakNits: "peakNits",
};

let selectedQueueId = null;
let queueSeq = 0;

function esc(value) {
  return String(value == null ? "" : value)
    .replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;").replace(/'/g, "&#39;");
}

function applyPreset(card) {
  const next = Object.assign({}, state.profile);
  for (const [attr, key] of Object.entries(PRESET_FIELDS)) {
    const raw = card.dataset[attr];
    if (raw === undefined) continue;
    let value = raw;
    if (raw === "true" || raw === "false") value = raw === "true";
    else if (/^-?\d+$/.test(raw)) value = Number(raw);
    next[key] = value;
  }
  setPath("profile", next);
  document.querySelectorAll("[data-preset]").forEach((el) => {
    el.classList.toggle("active", el === card);
  });
  runEffects();
  schedulePersist();
}

function makeQueueItem(profile, presetName, explicitInputs) {
  const inputs = Array.isArray(explicitInputs) && explicitInputs.length
    ? explicitInputs.filter(Boolean)
    : ((profile.input || "").trim() ? [(profile.input || "").trim()] : []);
  const firstInput = inputs[0] || "";
  const codec = profile.codec || "av1";
  const output = (inputs.length === 1 && profile.output)
    ? profile.output
    : autoNameFor(firstInput, codec);
  queueSeq += 1;
  const name = inputs.length <= 1
    ? basename(firstInput)
    : `${basename(firstInput)} (+${inputs.length - 1} files)`;
  const snapProfile = Object.assign({}, DEFAULTS, profile, {
    input: firstInput,
    output,
  });
  return {
    id: `job-${Date.now()}-${queueSeq}`,
    name,
    inputs,
    input: firstInput,
    output,
    presetName: presetName || "当前配置",
    codec,
    rateControl: snapProfile.rateControl || "cq",
    qp: snapProfile.qp ?? 22,
    resolution: snapProfile.resolution || "4k",
    fpsMode: snapProfile.fpsMode || "60-nvofa",
    pipelineOrder: snapProfile.pipelineOrder || "vsr-first",
    peakNits: snapProfile.peakNits || 1000,
    status: "pending",
    fileCount: inputs.length,
    currentFileIndex: 0,
    currentFilePct: 0,
    totalJobPct: 0,
    pct: 0,
    fps: 0,
    profile: snapProfile,
  };
}

function addQueueItem(profile, presetName, explicitInputs) {
  const inputs = Array.isArray(explicitInputs) && explicitInputs.length
    ? explicitInputs.filter(Boolean)
    : ((profile.input || "").trim() ? [(profile.input || "").trim()] : []);
  if (!inputs.length) {
    setStatusMessage(tr("StatusDropFileOnly"));
    return;
  }
  const localItem = makeQueueItem(profile, presetName, inputs);
  const items = (state.queue || []).slice();
  items.push(localItem);
  setPath("queue", items);
  schedulePersist();
  api("/api/job", "POST", {
    action: "enqueue",
    inputs,
    profile: localItem.profile,
    presetName: localItem.presetName,
  }).catch(() => {});
}

function queueRow(item) {
  const cls = item.id === selectedQueueId ? " class=\"selected\"" : "";
  const pct = Math.max(0, Math.min(100, Number(item.totalJobPct ?? item.pct) || 0));
  const inputs = Array.isArray(item.inputs) && item.inputs.length
    ? item.inputs
    : (item.input ? [item.input] : []);
  const fileCount = inputs.length || 1;
  const inputLabel = fileCount > 1
    ? `${basename(inputs[0] || "")} (+${fileCount - 1} files)`
    : basename(inputs[0] || item.input || "");
  const rcText = String((item.profile && item.profile.rateControl) || item.rateControl || "cq").toUpperCase();
  const qpVal = (item.profile && item.profile.qp) ?? item.qp ?? 22;
  const pipeOrder = (item.profile && item.profile.pipelineOrder) || item.pipelineOrder || "vsr-first";
  const subStep = fileCount > 1 ? ` [${Math.min(fileCount, (item.currentFileIndex || 0) + (item.status === "done" ? 0 : 1))}/${fileCount}]` : "";
  return `<tr data-queue-id="${esc(item.id)}"${cls}>`
    + `<td><span class="badge badge-${esc(item.status)}">${esc(item.status)}</span></td>`
    + `<td class="cell-name" title="${esc(inputs.join("\n"))}">${esc(inputLabel)}</td>`
    + `<td>${esc(String(item.codec || "").toUpperCase())} · ${esc(rcText)}${esc(qpVal)} · ${esc(String(item.resolution || "").toUpperCase())}<br><span class="hint">${esc(item.presetName)} · ${esc(pipeOrder)}</span></td>`
    + `<td>${esc(item.fpsMode)}</td>`
    + `<td>${esc(item.peakNits)} nits</td>`
    + `<td class="cell-name" title="${esc(item.output)}">${esc(basename(item.output || ""))}</td>`
    + `<td><div class="mini-progress"><i style="width:${pct}%"></i></div><span class="hint">${pct.toFixed(0)}%${esc(subStep)}</span></td>`
    + `</tr>`;
}

function renderQueue() {
  const body = document.getElementById("queue-body");
  const empty = document.getElementById("queue-empty");
  const items = state.queue || [];
  if (body) body.innerHTML = items.map(queueRow).join("");
  if (empty) empty.hidden = items.length > 0;
}

function renderInspectorContext() {
  const textEl = document.getElementById("inspector-mode-text");
  const deselectBtn = document.getElementById("btn-queue-deselect");
  if (!textEl || !deselectBtn) return;
  if (!selectedQueueId) {
    textEl.textContent = tr("WuiInspectorDraft");
    deselectBtn.hidden = true;
    return;
  }
  const item = (state.queue || []).find((entry) => entry.id === selectedQueueId);
  if (!item) {
    selectedQueueId = null;
    textEl.textContent = tr("WuiInspectorDraft");
    deselectBtn.hidden = true;
    return;
  }
  deselectBtn.hidden = false;
  if (item.status === "pending") {
    textEl.textContent = `${tr("WuiInspectorEditingPrefix")}: ${item.name || basename(item.input || item.id)}`;
  } else {
    textEl.textContent = `${tr("WuiInspectorReadonlyPrefix")}: ${item.name || basename(item.input || item.id)} (${item.status})`;
  }
}

function deselectQueueItem() {
  if (!selectedQueueId) return;
  selectedQueueId = null;
  renderQueue();
  renderInspectorContext();
}

function selectQueueItem(id) {
  if (selectedQueueId === id) {
    deselectQueueItem();
    return;
  }
  const item = (state.queue || []).find((entry) => entry.id === id);
  if (!item) return;
  selectedQueueId = id;
  applyingRemote = true;
  try {
    setPath("profile", Object.assign({}, DEFAULTS, item.profile || {}));
  } finally {
    applyingRemote = false;
  }
  runEffects();
  renderQueue();
  renderInspectorContext();
}

function syncProfileToQueue() {
  if (!selectedQueueId || applyingRemote || !state.profile || !Object.keys(state.profile).length) return;
  const items = (state.queue || []).slice();
  const index = items.findIndex((entry) => entry.id === selectedQueueId);
  if (index < 0) {
    selectedQueueId = null;
    renderInspectorContext();
    return;
  }
  // CRITICAL INVARIANT: Only "pending" jobs may be live-edited!
  if (items[index].status !== "pending") return;

  const p = Object.assign({}, state.profile);
  const inputs = Array.isArray(items[index].inputs) && items[index].inputs.length
    ? items[index].inputs.slice()
    : (p.input ? [p.input] : []);
  if (inputs.length <= 1 && p.input) inputs[0] = p.input;

  items[index] = Object.assign({}, items[index], {
    profile: p,
    inputs,
    input: inputs[0] || p.input || "",
    output: p.output || autoNameFor(inputs[0] || "", p.codec || "av1"),
    codec: p.codec || "av1",
    rateControl: p.rateControl || "cq",
    qp: p.qp ?? 22,
    resolution: p.resolution || "4k",
    fpsMode: p.fpsMode || "60-nvofa",
    pipelineOrder: p.pipelineOrder || "vsr-first",
    peakNits: p.peakNits || 1000,
  });
  setPath("queue", items);
  schedulePersist();
  api("/api/job", "POST", {
    action: "update",
    jobId: selectedQueueId,
    profile: p,
    inputs,
  }).catch(() => {});
}

function syncQueueProgress(prog, jobStatus) {
  const items = state.queue || [];
  const index = items.findIndex((entry) => entry.status === "running");
  if (index < 0) return;
  items[index].pct = prog.pct || 0;
  items[index].totalJobPct = prog.pct || 0;
  items[index].fps = prog.fps || 0;
  if (jobStatus !== "running" && jobStatus !== "idle") items[index].status = jobStatus;
  renderQueue();
}

function moveSelectedQueueItem(delta) {
  if (!selectedQueueId) return;
  const items = (state.queue || []).slice();
  const idx = items.findIndex((entry) => entry.id === selectedQueueId);
  if (idx < 0 || items[idx].status !== "pending") return;
  const pendingIndices = items
    .map((entry, i) => (entry.status === "pending" ? i : -1))
    .filter((i) => i >= 0);
  const pos = pendingIndices.indexOf(idx);
  if (pos < 0) return;
  const newPos = Math.max(0, Math.min(pendingIndices.length - 1, pos + (delta < 0 ? -1 : 1)));
  const targetIdx = pendingIndices[newPos];
  if (targetIdx === idx) return;
  const [moved] = items.splice(idx, 1);
  items.splice(targetIdx, 0, moved);
  setPath("queue", items);
  schedulePersist();
  api("/api/job", "POST", { action: "move", jobId: selectedQueueId, delta }).catch(() => {});
}

function retryQueueItems() {
  const items = (state.queue || []).slice();
  let changed = false;
  for (const item of items) {
    if (selectedQueueId && item.id !== selectedQueueId) continue;
    if (item.status === "failed" || item.status === "cancelled") {
      item.status = "pending";
      item.pct = 0;
      item.totalJobPct = 0;
      item.currentFileIndex = 0;
      item.currentFilePct = 0;
      changed = true;
    }
  }
  if (!changed) return;
  setPath("queue", items);
  schedulePersist();
  api("/api/job", "POST", {
    action: "retry",
    jobId: selectedQueueId || "",
  }).catch(() => {});
}

function installEncoder() {
  document.querySelectorAll("[data-preset]").forEach((card) => {
    card.addEventListener("click", () => applyPreset(card));
  });
  document.getElementById("btn-queue-add").addEventListener("click", () => {
    addQueueItem(Object.assign({}, state.profile), "当前配置");
  });
  const btnDeselect = document.getElementById("btn-queue-deselect");
  if (btnDeselect) {
    btnDeselect.addEventListener("click", deselectQueueItem);
  }
  const btnUp = document.getElementById("btn-queue-up");
  if (btnUp) {
    btnUp.addEventListener("click", () => moveSelectedQueueItem(-1));
  }
  const btnDown = document.getElementById("btn-queue-down");
  if (btnDown) {
    btnDown.addEventListener("click", () => moveSelectedQueueItem(1));
  }
  document.getElementById("btn-queue-remove").addEventListener("click", () => {
    if (!selectedQueueId) return;
    const target = (state.queue || []).find((j) => j.id === selectedQueueId);
    if (target && target.status === "running") {
      toast(tr("WuiWarnRemoveRunning"), "warn");
      return;
    }
    const removedId = selectedQueueId;
    setPath("queue", (state.queue || []).filter((item) => item.id !== removedId));
    selectedQueueId = null;
    renderInspectorContext();
    schedulePersist();
    api("/api/job", "POST", { action: "remove", jobId: removedId }).catch(() => {});
  });
  const btnRetry = document.getElementById("btn-queue-retry");
  if (btnRetry) {
    btnRetry.addEventListener("click", retryQueueItems);
  }
  document.getElementById("btn-queue-clear").addEventListener("click", () => {
    setPath("queue", (state.queue || []).filter((item) =>
      item.status === "pending" || item.status === "running"));
    if (selectedQueueId && !(state.queue || []).some((j) => j.id === selectedQueueId)) {
      selectedQueueId = null;
    }
    renderInspectorContext();
    schedulePersist();
    api("/api/job", "POST", { action: "clear", completedOnly: true }).catch(() => {});
  });
  const body = document.getElementById("queue-body");
  body.addEventListener("click", (event) => {
    const row = event.target && event.target.closest
      ? event.target.closest("tr[data-queue-id]") : null;
    if (row) selectQueueItem(row.dataset.queueId);
  });
  subscribe("queue", () => {
    renderQueue();
    renderInspectorContext();
  });
  subscribe("profile", () => {
    syncProfileToQueue();
    syncRound2Controls();
  });
}

/* ========================================================================= *
 *  Job actions + probe (Server-Owned Queue Engine)
 * ========================================================================= */

async function startJob() {
  if (state.runtime.job.status === "running") return;
  await flushPersist();
  const hasPending = (state.queue || []).some((item) => item.status === "pending");
  if (!hasPending) {
    if (!state.profile.input) {
      setStatusMessage(tr("StatusDropFileOnly"));
      return;
    }
    if (state.profile.output && state.profile.output === state.profile.input) {
      setStatusMessage(tr("StatusSamePath"));
      return;
    }
  }
  try {
    await api("/api/job", "POST", { action: "start", profile: state.profile });
  } catch (err) {
    setStatusMessage(`${tr("WuiReqFailed")}: ${err.message}`);
  }
}

async function skipJob() {
  try {
    await api("/api/job", "POST", { action: "skip" });
  } catch (err) {
    setStatusMessage(`${tr("WuiReqFailed")}: ${err.message}`);
  }
}

async function stopJob() {
  try {
    await api("/api/job", "POST", { action: "stop" });
    setStatusMessage(tr("StatusStopping"));
  } catch (err) {
    setStatusMessage(`${tr("WuiReqFailed")}: ${err.message}`);
  }
}

async function runProbe() {
  setStatusMessage(tr("StatusGpuProbing"));
  try {
    const doc = await api("/api/probe", "POST", {});
    renderProbe();
    if (doc.result && doc.result.available) setStatusMessage(tr("StatusProbeOk"));
  } catch (err) {
    setStatusMessage(`${tr("WuiReqFailed")}: ${err.message}`);
  }
}

/* ========================================================================= *
 *  Player shape
 * ========================================================================= */

function mediaUrl(path) {
  return "/api/media?path=" + encodeURIComponent(path);
}

function videoById(id) {
  return document.getElementById(id);
}

function attachMedia() {
  const path = state.player.input || "";
  const stage = document.getElementById("player-stage");
  if (stage && stage.classList && stage.classList.toggle) {
    stage.classList.toggle("has-media", Boolean(path));
  }
  const encAb = document.getElementById("encoder-ab");
  if (encAb && encAb.classList && encAb.classList.toggle) {
    encAb.classList.toggle("has-media", Boolean(path));
  }
  for (const id of ["player-video-original", "player-video-remaster", "encoder-video"]) {
    const video = videoById(id);
    if (!video) continue;
    if (path) {
      video.src = mediaUrl(path);
    } else if (video.removeAttribute) {
      video.removeAttribute("src");
    }
    if (typeof video.load === "function") video.load();
  }
  renderOsd();
}

function renderOsd() {
  const el = document.getElementById("player-osd");
  if (!el) return;
  const p = state.profile || {};
  const pl = state.player || {};
  el.hidden = !pl.osd;
  el.textContent = [
    `MEDIA   ${pl.input || "(none)"}`,
    `STAGE   ${String(p.resolution || "4k").toUpperCase()} · ${String(p.codec || "av1").toUpperCase()} 10-bit`,
    `FRUC    ${p.fpsMode || "60-nvofa"} (${p.pipelineOrder || "vsr-first"}) · VSR L${p.vsrQuality || 4}${p.vsrEnabled === false ? " (OFF)" : ""}`,
    `HDR     ${p.peakNits || 1000} nits · HDR10${p.hdr10plus ? "+" : ""}`,
    `GPU     #${p.gpu != null ? p.gpu : 0} · ZERO-COPY MAIN LOOP`,
    `TRACK   A${pl.audioTrack ?? 0} / S${pl.subTrack ?? -1} · ${pl.speed || 1}x`,
  ].join("\n");
}

function renderPlayerSplit() {
  const stage = document.getElementById("player-stage");
  const pos = Math.max(0, Math.min(100, Number(state.player.splitPos) || 0));
  if (stage) {
    if (stage.style.setProperty) stage.style.setProperty("--split", `${pos}%`);
    else stage.style["--split"] = `${pos}%`;
  }
  const handle = document.getElementById("ab-handle");
  if (handle) {
    handle.style.left = `${pos}%`;
    if (handle.setAttribute) handle.setAttribute("aria-valuenow", String(pos));
  }
}

function applyAbMode() {
  const stage = document.getElementById("player-stage");
  if (stage) stage.dataset.abMode = state.player.abMode || "split";
  document.querySelectorAll("[data-ab-mode].capsule").forEach((btn) => {
    btn.classList.toggle("active", btn.dataset.abMode === state.player.abMode);
  });
}

function applyPlaybackRate() {
  const rate = Number(state.player.speed) || 1;
  for (const id of ["player-video-original", "player-video-remaster"]) {
    const video = videoById(id);
    if (video) video.playbackRate = rate;
  }
}

function formatTimecode(ms) {
  const total = Math.max(0, Number(ms) || 0);
  const h = Math.floor(total / 3600000);
  const m = Math.floor(total / 60000) % 60;
  const s = Math.floor(total / 1000) % 60;
  const milli = Math.floor(total % 1000);
  const pad = (n) => String(n).padStart(2, "0");
  return `${pad(h)}:${pad(m)}:${pad(s)}.${String(milli).padStart(3, "0")}`;
}

function frameSeconds() {
  const mode = state.profile.fpsMode || "60-nvofa";
  if (mode.startsWith("120")) return 1 / 120;
  if (mode.startsWith("60")) return 1 / 60;
  return 1 / 30;
}

function bothVideos(fn) {
  for (const id of ["player-video-original", "player-video-remaster"]) {
    const video = videoById(id);
    if (video) fn(video);
  }
}

function playerStep(direction) {
  bothVideos((video) => {
    const base = Number(video.currentTime) || 0;
    video.currentTime = Math.max(0, base + direction * frameSeconds());
  });
}

function playerPlayToggle() {
  const main = videoById("player-video-original");
  if (!main) return;
  if (main.paused) {
    bothVideos((video) => { if (typeof video.play === "function") video.play().catch(() => {}); });
  } else {
    bothVideos((video) => { if (typeof video.pause === "function") video.pause(); });
  }
}

function renderPlayerTimecode() {
  const main = videoById("player-video-original");
  const t = (Number(main && main.currentTime) || 0) * 1000;
  const d = (Number(main && main.duration) || 0) * 1000;
  const tc = document.getElementById("player-tc");
  const dur = document.getElementById("player-duration");
  const frame = document.getElementById("player-frame");
  const scrub = document.getElementById("player-scrub");
  if (tc) tc.textContent = formatTimecode(t);
  if (dur) dur.textContent = d > 0 ? formatTimecode(d) : "00:00:00.000";
  if (frame) frame.textContent = `frame ${Math.round(t / (frameSeconds() * 1000))}`;
  if (scrub && d > 0) scrub.value = String(Math.round(t / d * 1000));
}

function sendPlayerToEncoder() {
  const input = state.player.input || "";
  if (!input) {
    setStatusMessage(tr("StatusDropFileOnly"));
    return;
  }
  setPath("profile.input", input);
  if (state.ui.autoOutput && looksAutoFor(prevInput, state.profile.output || "")) {
    setPath("profile.output", autoNameFor(input, state.profile.codec || "av1"));
  }
  prevInput = input;
  setWorkspace("encoder");
  schedulePersist();
  setStatusMessage(`已送入转码器: ${basename(input)}`);
}

function installPlayer() {
  const main = videoById("player-video-original");
  const remaster = videoById("player-video-remaster");
  if (main && remaster && main.addEventListener) {
    main.addEventListener("timeupdate", () => {
      const drift = Math.abs((Number(main.currentTime) || 0) - (Number(remaster.currentTime) || 0));
      if (drift > 0.08) remaster.currentTime = main.currentTime;
      renderPlayerTimecode();
    });
    main.addEventListener("loadedmetadata", () => {
      remaster.currentTime = main.currentTime || 0;
      renderPlayerTimecode();
    });
  }
  document.getElementById("btn-player-head").addEventListener("click", () => {
    bothVideos((video) => { video.currentTime = 0; });
  });
  document.getElementById("btn-player-prev").addEventListener("click", () => playerStep(-1));
  document.getElementById("btn-player-next").addEventListener("click", () => playerStep(1));
  document.getElementById("btn-player-play").addEventListener("click", playerPlayToggle);
  document.getElementById("btn-player-stop").addEventListener("click", () => {
    bothVideos((video) => {
      if (typeof video.pause === "function") video.pause();
      video.currentTime = 0;
    });
  });
  document.getElementById("btn-player-osd").addEventListener("click", () => {
    setPath("player.osd", !state.player.osd);
    schedulePersist();
  });
  document.getElementById("btn-player-send").addEventListener("click", sendPlayerToEncoder);
  document.getElementById("player-scrub").addEventListener("input", (event) => {
    const main2 = videoById("player-video-original");
    const duration = Number(main2 && main2.duration) || 0;
    if (duration > 0) bothVideos((video) => { video.currentTime = duration * Number(event.target.value) / 1000; });
  });
  document.querySelectorAll("[data-ab-mode].capsule").forEach((btn) => {
    btn.addEventListener("click", () => {
      setPath("player.abMode", btn.dataset.abMode);
      schedulePersist();
    });
  });
  document.querySelectorAll("[data-vsr-level]").forEach((btn) => {
    btn.addEventListener("click", () => {
      setPath("profile.vsrQuality", Number(btn.dataset.vsrLevel));
      setPath("profile.vsrEnabled", true);
      runEffects();
      schedulePersist();
    });
  });
  document.querySelectorAll("[data-fps-mode]").forEach((btn) => {
    btn.addEventListener("click", () => {
      setPath("profile.fpsMode", btn.dataset.fpsMode);
      runEffects();
      schedulePersist();
    });
  });

  const stage = document.getElementById("player-stage");
  const handle = document.getElementById("ab-handle");
  let draggingSplit = false;
  const setSplitFromEvent = (event) => {
    if (!stage || !stage.getBoundingClientRect) return;
    const rect = stage.getBoundingClientRect();
    if (rect.width <= 0) return;
    const pct = Math.max(0, Math.min(100, ((event.clientX - rect.left) / rect.width) * 100));
    setPath("player.splitPos", Math.round(pct));
  };
  if (stage && stage.addEventListener) {
    stage.addEventListener("pointerdown", (event) => {
      if (event.target === handle || event.target === stage || event.target.tagName === "VIDEO") {
        draggingSplit = true;
        if (stage.setPointerCapture && event.pointerId != null) {
          try { stage.setPointerCapture(event.pointerId); } catch {}
        }
        setSplitFromEvent(event);
      }
    });
    stage.addEventListener("pointermove", (event) => {
      if (!draggingSplit) return;
      setSplitFromEvent(event);
    });
    const endDrag = (event) => {
      if (!draggingSplit) return;
      draggingSplit = false;
      if (stage.releasePointerCapture && event.pointerId != null) {
        try { stage.releasePointerCapture(event.pointerId); } catch {}
      }
      schedulePersist();
    };
    stage.addEventListener("pointerup", endDrag);
    stage.addEventListener("pointercancel", endDrag);
  }
  subscribe("player.input", attachMedia);
  subscribe("player.splitPos", renderPlayerSplit);
  subscribe("player.abMode", applyAbMode);
  subscribe("player.osd", renderOsd);
  subscribe("player.speed", applyPlaybackRate);
  subscribe("player", renderOsd);
}

function syncRound2Controls() {
  if (!state.profile || !Object.keys(state.profile).length) return;
  document.querySelectorAll("[data-vsr-level]").forEach((btn) => {
    btn.classList.toggle("active", Number(btn.dataset.vsrLevel) === state.profile.vsrQuality);
  });
  document.querySelectorAll("[data-fps-mode]").forEach((btn) => {
    btn.classList.toggle("active", btn.dataset.fpsMode === state.profile.fpsMode);
  });
  const nits = document.getElementById("nle-nits-value");
  if (nits) nits.textContent = String(state.profile.peakNits ?? 1000);
  renderOsd();
}

/* ========================================================================= *
 *  NLE shape
 * ========================================================================= */

const FLICKS_PER_MS = 705600;
let binTab = "media";

function nleSpan() {
  return Math.max(1000, Number(state.nle.outMs) || 10000);
}

function renderNleTimecode() {
  const ms = Number(state.nle.playheadMs) || 0;
  const frames = Math.floor(ms / 1000 * 60) % 60;
  const pad = (n) => String(n).padStart(2, "0");
  const tc = document.getElementById("nle-tc");
  const flicks = document.getElementById("nle-flicks");
  if (tc) tc.textContent = `${pad(Math.floor(ms / 3600000))}:${pad(Math.floor(ms / 60000) % 60)}:${pad(Math.floor(ms / 1000) % 60)}:${pad(frames)}`;
  if (flicks) flicks.textContent = `${(ms * FLICKS_PER_MS).toLocaleString("en-US")} Flicks`;
  const seq = document.getElementById("nle-seq-name");
  if (seq) seq.textContent = state.nle.sequenceName || "";
}

function renderPlayhead() {
  const span = nleSpan();
  const pct = Math.max(0, Math.min(100, (Number(state.nle.playheadMs) || 0) / span * 100));
  const head = document.getElementById("timeline-playhead");
  if (head) head.style.left = `${pct}%`;
  const inout = document.getElementById("timeline-inout");
  if (inout) {
    const from = Math.max(0, Math.min(100, (Number(state.nle.inMs) || 0) / span * 100));
    const to = Math.max(from, Math.min(100, (Number(state.nle.outMs) || span) / span * 100));
    inout.style.left = `${from}%`;
    inout.style.width = `${to - from}%`;
  }
  const zoom = document.getElementById("nle-zoom-value");
  if (zoom) zoom.textContent = String(state.nle.zoom || 100);
  renderNleTimecode();
}

function renderTimeline() {
  const span = nleSpan();
  const ruler = document.getElementById("timeline-ruler");
  if (ruler) {
    const ticks = [];
    for (let i = 0; i <= 5; i += 1) {
      const ms = Math.round(span * i / 5);
      ticks.push(`<span class="ruler-tick" style="left:${i * 20}%">${(ms / 1000).toFixed(1)}s</span>`);
    }
    ruler.innerHTML = ticks.join("")
      + `<div class="timeline-inout" id="timeline-inout"></div>`
      + `<div class="playhead" id="timeline-playhead"></div>`;
  }
  const host = document.getElementById("timeline-tracks");
  if (host) {
    const rows = (state.nle.tracks || []).map((track) => {
      const clips = (track.clips || []).map((clip) => {
        const left = Math.max(0, Math.min(100, clip.startMs / span * 100));
        const width = Math.max(1, Math.min(100 - left, (clip.endMs - clip.startMs) / span * 100));
        const selected = clip.id === state.nle.selectedClipId ? " selected" : "";
        return `<div class="clip tone-${esc(clip.tone || "green")}${selected}" data-clip-id="${esc(clip.id)}"`
          + ` style="left:${left}%;width:${width}%">${esc(clip.name)}<span class="clip-resize"></span></div>`;
      }).join("");
      return `<div class="track-row" data-track-id="${esc(track.id)}">`
        + `<div class="track-head"><div class="track-title">${esc(track.label)}</div>`
        + `<div class="track-switches">`
        + `<button class="track-switch${track.mute ? " on" : ""}" data-track-switch="mute" data-track-id="${esc(track.id)}">M</button>`
        + `<button class="track-switch${track.lock ? " on" : ""}" data-track-switch="lock" data-track-id="${esc(track.id)}">L</button>`
        + `<button class="track-switch${track.visible ? " on" : ""}" data-track-switch="visible" data-track-id="${esc(track.id)}">V</button>`
        + `</div></div><div class="track-lane" data-track-id="${esc(track.id)}">${clips}</div></div>`;
    }).join("");
    host.innerHTML = rows;
    const zoom = Number(state.nle.zoom) || 100;
    host.style.minWidth = `${Math.round(320 * zoom / 100)}px`;
  }
  renderPlayhead();
}

function renderBin() {
  const host = document.getElementById("bin-list");
  if (!host) return;
  document.querySelectorAll("[data-bin-tab]").forEach((btn) => {
    btn.classList.toggle("active", btn.dataset.binTab === binTab);
  });
  if (binTab === "lut") {
    host.innerHTML = ["CineRTX_Neutral.cube", "HDR10_PQ_1000nits.cube", "Film_Warm_2383.cube"]
      .map((name) => `<div class="bin-item lut-item" data-lut-name="${esc(name)}"><span>${esc(name)}</span><span class="hint">.cube</span></div>`)
      .join("");
    return;
  }
  if (binTab === "fx") {
    host.innerHTML = [
      `<div class="bin-item lut-item" data-fx="vsr"><span>RTX VSR · Level 4</span><span class="hint">NGX</span></div>`,
      `<div class="bin-item lut-item" data-fx="hdr"><span>TrueHDR · 1000 nits</span><span class="hint">PQ</span></div>`,
      `<div class="bin-item lut-item" data-fx="fruc"><span>NVOFA 60 FPS</span><span class="hint">FRUC</span></div>`,
    ].join("");
    return;
  }
  const items = state.nle.mediaBin || [];
  if (!items.length) {
    host.innerHTML = `<div class="bin-empty">素材箱为空</div>`;
    return;
  }
  host.innerHTML = items.map((item, index) =>
    `<div class="bin-item"><span title="${esc(item.path)}">${esc(item.name || basename(item.path))}</span>`
    + `<button class="btn btn-sm" data-bin-action="track" data-bin-index="${index}">+ 上轨</button>`
    + `<button class="btn btn-sm" data-bin-action="monitor" data-bin-index="${index}">监视器</button></div>`).join("");
}

function addToMediaBin(path) {
  if (!path) return;
  const items = (state.nle.mediaBin || []).slice();
  items.push({ path, name: basename(path) });
  setPath("nle.mediaBin", items);
  mountNleVideo(path);
  schedulePersist();
}

function mountNleVideo(path) {
  const nleVp = document.querySelector ? document.querySelector(".nle-viewport") : null;
  if (nleVp && nleVp.classList && nleVp.classList.toggle) {
    nleVp.classList.toggle("has-media", Boolean(path));
  }
  const encAb = document.getElementById("encoder-ab");
  if (encAb && encAb.classList && encAb.classList.toggle) {
    encAb.classList.toggle("has-media", Boolean(path));
  }
  const video = document.getElementById("nle-video");
  if (!video) return;
  if (path) {
    video.src = mediaUrl(path);
  } else if (video.removeAttribute) {
    video.removeAttribute("src");
  }
  if (typeof video.load === "function") video.load();
}

function addClipToV1(item) {
  const tracks = (state.nle.tracks || []).map((track) => Object.assign({}, track, {
    clips: (track.clips || []).map((clip) => Object.assign({}, clip)),
  }));
  const track = tracks.find((entry) => entry.id === "V1");
  if (!track) return;
  const start = (track.clips || []).reduce((max, clip) => Math.max(max, clip.endMs || 0), 0);
  const clip = {
    id: `clip-${Date.now()}`, name: item.name || basename(item.path),
    startMs: start, endMs: start + 10000, tone: "green", path: item.path,
  };
  track.clips = (track.clips || []).concat([clip]);
  setPath("nle.tracks", tracks);
  setPath("nle.outMs", Math.max(Number(state.nle.outMs) || 10000, clip.endMs));
  setPath("nle.selectedClipId", clip.id);
  mountNleVideo(item.path);
  schedulePersist();
}

function setPlayhead(ms) {
  const value = Math.max(0, Math.min(nleSpan(), Number(ms) || 0));
  setPath("nle.playheadMs", value);
  const video = document.getElementById("nle-video");
  if (video) video.currentTime = value / 1000;
  schedulePersist();
}

function markInPoint() {
  setPath("nle.inMs", Math.min(Number(state.nle.playheadMs) || 0, Number(state.nle.outMs) || 10000));
  schedulePersist();
}

function markOutPoint() {
  setPath("nle.outMs", Math.max(Number(state.nle.inMs) || 0, Number(state.nle.playheadMs) || 0) || 10000);
  schedulePersist();
}

function setNleTool(tool) {
  setPath("nle.tool", tool);
  document.querySelectorAll("[data-nle-tool]").forEach((btn) => {
    btn.classList.toggle("active", btn.dataset.nleTool === tool);
  });
  schedulePersist();
}

function razorSplit() {
  const at = Number(state.nle.playheadMs) || 0;
  const tracks = (state.nle.tracks || []).map((track) => Object.assign({}, track, {
    clips: (track.clips || []).map((clip) => Object.assign({}, clip)),
  }));
  let split = false;
  for (const track of tracks) {
    const index = (track.clips || []).findIndex((clip) => clip.id === state.nle.selectedClipId);
    if (index < 0) continue;
    const clip = track.clips[index];
    if (at <= clip.startMs + 50 || at >= clip.endMs - 50) break;
    const right = Object.assign({}, clip, {
      id: `${clip.id}-b${Date.now()}`, startMs: at,
    });
    const left = Object.assign({}, clip, { endMs: at });
    track.clips.splice(index, 1, left, right);
    setPath("nle.tracks", tracks);
    setPath("nle.selectedClipId", right.id);
    split = true;
    break;
  }
  if (split) schedulePersist();
}

function exportSequenceToEncoder() {
  const bin = state.nle.mediaBin || [];
  const input = (bin[0] && bin[0].path) || state.profile.input || "";
  if (!input) {
    setStatusMessage(tr("StatusDropFileOnly"));
    return;
  }
  const codec = state.profile.codec || "av1";
  const profile = Object.assign({}, state.profile, {
    input, output: autoNameFor(input, codec),
  });
  setPath("profile", profile);
  addQueueItem(profile, state.nle.sequenceName || "NLE 序列", [input]);
  setWorkspace("encoder");
  startJob();
}

function installNle() {
  document.querySelectorAll("[data-bin-tab]").forEach((btn) => {
    btn.addEventListener("click", () => { binTab = btn.dataset.binTab; renderBin(); });
  });
  document.getElementById("btn-nle-in").addEventListener("click", markInPoint);
  document.getElementById("btn-nle-out").addEventListener("click", markOutPoint);
  document.getElementById("btn-nle-prev").addEventListener("click", () => setPlayhead((Number(state.nle.playheadMs) || 0) - frameSeconds() * 1000));
  document.getElementById("btn-nle-next").addEventListener("click", () => setPlayhead((Number(state.nle.playheadMs) || 0) + frameSeconds() * 1000));

  const nleVideo = document.getElementById("nle-video");
  if (nleVideo && nleVideo.addEventListener) {
    nleVideo.addEventListener("timeupdate", () => {
      if (!nleVideo.paused) {
        setPath("nle.playheadMs", Math.round((Number(nleVideo.currentTime) || 0) * 1000));
      }
    });
  }
  document.getElementById("btn-nle-play").addEventListener("click", () => {
    if (!nleVideo) return;
    if (nleVideo.paused) {
      if (typeof nleVideo.play === "function") nleVideo.play().catch(() => {});
    } else if (typeof nleVideo.pause === "function") {
      nleVideo.pause();
    }
  });
  document.getElementById("btn-nle-razor").addEventListener("click", razorSplit);
  document.getElementById("btn-nle-export").addEventListener("click", exportSequenceToEncoder);
  document.querySelectorAll("[data-nle-tool]").forEach((btn) => {
    btn.addEventListener("click", () => setNleTool(btn.dataset.nleTool));
  });
  const ruler = document.getElementById("timeline-ruler");
  ruler.addEventListener("pointerdown", (event) => {
    if (!ruler.getBoundingClientRect) return;
    const rect = ruler.getBoundingClientRect();
    const pct = Math.max(0, Math.min(1, (event.clientX - rect.left) / rect.width));
    setPlayhead(pct * nleSpan());
  });
  const tracks = document.getElementById("timeline-tracks");
  tracks.addEventListener("click", (event) => {
    const target = event.target;
    if (!target || !target.closest) return;
    const switchBtn = target.closest("[data-track-switch]");
    if (switchBtn) {
      const id = switchBtn.dataset.trackId;
      const key = switchBtn.dataset.trackSwitch;
      const next = (state.nle.tracks || []).map((track) =>
        track.id === id ? Object.assign({}, track, { [key]: !track[key] }) : track);
      setPath("nle.tracks", next);
      schedulePersist();
      return;
    }
    const clip = target.closest("[data-clip-id]");
    if (clip) {
      setPath("nle.selectedClipId", clip.dataset.clipId);
      if (state.nle.tool === "razor") razorSplit();
      else renderTimeline();
      schedulePersist();
      return;
    }
    const lane = target.closest(".track-lane");
    if (lane && lane.getBoundingClientRect) {
      const rect = lane.getBoundingClientRect();
      const pct = Math.max(0, Math.min(1, (event.clientX - rect.left) / rect.width));
      setPlayhead(pct * nleSpan());
    }
  });
  const bin = document.getElementById("bin-list");
  bin.addEventListener("click", (event) => {
    const target = event.target;
    if (!target || !target.closest) return;
    const lut = target.closest("[data-lut-name]");
    if (lut) {
      setPath("profile.lut", lut.dataset.lutName);
      runEffects();
      schedulePersist();
      return;
    }
    const fx = target.closest("[data-fx]");
    if (fx) {
      const kind = fx.dataset.fx;
      if (kind === "vsr") { setPath("profile.vsrEnabled", true); setPath("profile.vsrQuality", 4); }
      if (kind === "hdr") setPath("profile.peakNits", 1000);
      if (kind === "fruc") setPath("profile.fpsMode", "60-nvofa");
      runEffects();
      schedulePersist();
      return;
    }
    const action = target.closest("[data-bin-action]");
    if (!action) return;
    const item = (state.nle.mediaBin || [])[Number(action.dataset.binIndex)];
    if (!item) return;
    if (action.dataset.binAction === "track") addClipToV1(item);
    else mountNleVideo(item.path);
  });
  subscribe("nle", renderTimeline);
  subscribe("nle.mediaBin", renderBin);
  subscribe("nle.playheadMs", renderPlayhead);
  subscribe("nle.inMs", renderPlayhead);
  subscribe("nle.outMs", renderPlayhead);
  subscribe("nle.zoom", renderPlayhead);
  subscribe("nle.selectedClipId", renderTimeline);
  renderBin();
}

/* ========================================================================= *
 *  Generic JSON path browser (with multi-select for queue-import)
 * ========================================================================= */

const fsCtx = {
  bindPath: null,
  mode: null,
  current: "",
  selectedFile: null,
  selectedFiles: new Set(),
  entries: [],
};

function openBrowser(bindPath, mode) {
  fsCtx.bindPath = bindPath;
  fsCtx.mode = mode || null;
  fsCtx.current = getPath(bindPath) || "";
  fsCtx.selectedFile = null;
  fsCtx.selectedFiles = new Set();
  fsCtx.entries = [];
  document.getElementById("fs-title").textContent =
    `${tr("BtnBrowse")} → ${bindPath}`;
  document.getElementById("fs-path").value = fsCtx.current;
  const selectAllBtn = document.getElementById("fs-select-all");
  if (selectAllBtn) selectAllBtn.hidden = fsCtx.mode !== "queue-import";
  const hintEl = document.getElementById("fs-hint");
  if (hintEl) hintEl.textContent = "";
  document.getElementById("fs-modal").hidden = false;
  loadFsListing(fsCtx.current ? fsCtx.current : "roots");
}

function closeBrowser() {
  document.getElementById("fs-modal").hidden = true;
}

function fsModeAllowsFiles() {
  return fsCtx.bindPath !== null;
}

function updateFsHint() {
  const hintEl = document.getElementById("fs-hint");
  if (!hintEl) return;
  if (fsCtx.mode === "queue-import") {
    hintEl.textContent = fsCtx.selectedFiles.size > 0
      ? `${fsCtx.selectedFiles.size} file(s) selected`
      : "";
  } else {
    hintEl.textContent = fsCtx.selectedFile ? basename(fsCtx.selectedFile) : "";
  }
}

async function loadFsListing(path) {
  const list = document.getElementById("fs-list");
  try {
    const doc = await api(`/api/fs?path=${encodeURIComponent(path)}`, "GET");
    fsCtx.current = doc.path || "";
    fsCtx.entries = doc.entries || [];
    fsCtx.selectedFiles = new Set();
    updateFsHint();
    document.getElementById("fs-path").value = fsCtx.current;
    list.textContent = "";
    if (doc.parent) {
      const up = document.createElement("div");
      up.className = "fs-item";
      up.innerHTML = "<span class='fs-name'>..</span>";
      up.addEventListener("click", () => loadFsListing(doc.parent));
      list.appendChild(up);
    }
    const multiMode = fsCtx.mode === "queue-import";
    for (const entry of fsCtx.entries) {
      const item = document.createElement("div");
      item.className = "fs-item";
      item.dataset.entryPath = entry.path;
      const name = document.createElement("span");
      name.className = "fs-name";
      if (multiMode && entry.type === "file") {
        const chk = document.createElement("input");
        chk.type = "checkbox";
        chk.className = "fs-check";
        chk.addEventListener("click", (ev) => {
          ev.stopPropagation();
          if (chk.checked) fsCtx.selectedFiles.add(entry.path);
          else fsCtx.selectedFiles.delete(entry.path);
          item.classList.toggle("selected", chk.checked);
          fsCtx.selectedFile = entry.path;
          updateFsHint();
        });
        name.appendChild(chk);
      }
      const labelNode = document.createElement("span");
      labelNode.textContent = (entry.type === "dir" ? "\uD83D\uDCC1 " : "\uD83D\uDCC4 ") + entry.name;
      name.appendChild(labelNode);
      const size = document.createElement("span");
      size.className = "fs-size";
      size.textContent = entry.type === "file" ? formatBytes(entry.size) : "";
      item.append(name, size);
      item.addEventListener("click", () => {
        if (entry.type === "dir") {
          loadFsListing(entry.path);
        } else if (multiMode) {
          const chk = item.querySelector ? item.querySelector(".fs-check") : null;
          const nextChecked = !fsCtx.selectedFiles.has(entry.path);
          if (nextChecked) fsCtx.selectedFiles.add(entry.path);
          else fsCtx.selectedFiles.delete(entry.path);
          if (chk) chk.checked = nextChecked;
          item.classList.toggle("selected", nextChecked);
          fsCtx.selectedFile = entry.path;
          document.getElementById("fs-path").value = entry.path;
          updateFsHint();
        } else {
          fsCtx.selectedFile = entry.path;
          document.getElementById("fs-path").value = entry.path;
          list.querySelectorAll(".fs-item").forEach((el) => el.classList.remove("selected"));
          item.classList.add("selected");
          updateFsHint();
        }
      });
      if (fsModeAllowsFiles() && entry.type === "file") {
        item.addEventListener("dblclick", () => {
          selectFromBrowser(entry.path);
        });
      }
      list.appendChild(item);
    }
  } catch (err) {
    list.textContent = `${tr("WuiReqFailed")}: ${err.message}`;
  }
}

function formatBytes(bytes) {
  if (bytes == null) return "";
  const units = ["B", "KB", "MB", "GB", "TB"];
  let value = bytes;
  let unit = 0;
  while (value >= 1024 && unit < units.length - 1) {
    value /= 1024;
    unit += 1;
  }
  return `${value.toFixed(value >= 100 || unit === 0 ? 0 : 1)} ${units[unit]}`;
}

function selectFromBrowser(path) {
  if (fsCtx.mode === "queue-import") {
    const multi = Array.from(fsCtx.selectedFiles);
    const single = path || fsCtx.selectedFile || document.getElementById("fs-path").value.trim();
    const chosen = multi.length > 0 ? multi : (single ? [single] : []);
    if (!chosen.length) return;
    const first = chosen[0];
    const profile = Object.assign({}, DEFAULTS, state.profile, {
      input: first,
      output: autoNameFor(first, state.profile.codec || "av1"),
    });
    const label = chosen.length > 1
      ? `${basename(first)} (+${chosen.length - 1} files)`
      : basename(first);
    addQueueItem(profile, label, chosen);
    closeBrowser();
    return;
  }
  const value = path || document.getElementById("fs-path").value.trim();
  if (!value) return;
  if (fsCtx.mode === "nle-bin") {
    addToMediaBin(value);
  } else if (fsCtx.bindPath != null) {
    setPath(fsCtx.bindPath, value);
    runEffects();
    schedulePersist();
  }
  closeBrowser();
}

function installBrowser() {
  document.querySelectorAll("[data-browse]").forEach((btn) => {
    btn.addEventListener("click", () => openBrowser(btn.dataset.browse, btn.dataset.browseMode));
  });
  document.getElementById("fs-close").addEventListener("click", closeBrowser);
  document.getElementById("fs-modal").addEventListener("click", (event) => {
    if (event.target === event.currentTarget) closeBrowser();
  });
  document.getElementById("fs-go").addEventListener("click", () => {
    loadFsListing(document.getElementById("fs-path").value.trim() || "roots");
  });
  document.getElementById("fs-up").addEventListener("click", () => {
    const base = fsCtx.current || document.getElementById("fs-path").value.trim();
    if (!base) {
      loadFsListing("roots");
      return;
    }
    const cut = Math.max(base.lastIndexOf("\\"), base.lastIndexOf("/"));
    if (cut <= 0) {
      loadFsListing("roots");
      return;
    }
    const parent = base.slice(0, cut);
    loadFsListing(/^[a-zA-Z]:$/.test(parent) ? parent + "\\" : parent);
  });
  const selectAllBtn = document.getElementById("fs-select-all");
  if (selectAllBtn) {
    selectAllBtn.addEventListener("click", () => {
      const list = document.getElementById("fs-list");
      for (const entry of fsCtx.entries) {
        if (entry.type === "file" && VIDEO_EXT_RE.test(entry.name || entry.path)) {
          fsCtx.selectedFiles.add(entry.path);
        }
      }
      if (list && list.querySelectorAll) {
        list.querySelectorAll(".fs-item").forEach((item) => {
          const p = item.dataset.entryPath;
          if (p && fsCtx.selectedFiles.has(p)) {
            item.classList.add("selected");
            const chk = item.querySelector ? item.querySelector(".fs-check") : null;
            if (chk) chk.checked = true;
          }
        });
      }
      updateFsHint();
    });
  }
  document.getElementById("fs-select").addEventListener("click", () => {
    selectFromBrowser(fsCtx.selectedFile);
  });
  document.getElementById("fs-path").addEventListener("keydown", (event) => {
    if (event.key === "Enter") loadFsListing(event.target.value.trim() || "roots");
  });
  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape" && !document.getElementById("fs-modal").hidden) {
      closeBrowser();
    }
  });
}

/* ========================================================================= *
 *  Bootstrap
 * ========================================================================= */

function installStaticHandlers() {
  document.getElementById("btn-start").addEventListener("click", startJob);
  document.getElementById("btn-stop").addEventListener("click", stopJob);
  const btnSkip = document.getElementById("btn-skip");
  if (btnSkip) btnSkip.addEventListener("click", skipJob);
  document.getElementById("btn-probe").addEventListener("click", runProbe);
}

async function boot() {
  captureFallback();
  installBindings();
  installI18nBinding();
  installStaticHandlers();
  installWorkspaces();
  installEncoder();
  installPlayer();
  installNle();
  installBrowser();
  applyI18n();
  try {
    await loadSchema();
    applyI18n();
  } catch (err) {
    setStatusMessage(`${tr("WuiReqFailed")}: ${err.message}`);
  }
  try {
    const doc = await api("/api/state", "GET");
    applyingRemote = true;
    try {
      for (const section of SECTION_KEYS) {
        if (section === "profile") continue;
        if (doc.state && doc.state[section] !== undefined) setPath(section, doc.state[section]);
      }
      setPath("profile", Object.assign({}, DEFAULTS, (doc.state && doc.state.profile) || {}));
      if (doc.state && doc.state.runtime) setPath("runtime", doc.state.runtime);
    } finally {
      applyingRemote = false;
    }
  } catch (err) {
    setStatusMessage(`${tr("WuiReqFailed")}: ${err.message}`);
  }
  applyI18n();
  renderWorkspace();
  renderRuntime();
  runEffects();
  renderInspectorContext();
  syncRound2Controls();
  attachMedia();
  connectEvents();
}

boot();

// Phase 13 (Wave 0-A Step 3.2): SDR JPEG/MJPEG proxy preview polling controller
class PreviewController {
  constructor() {
    this.previewImg = document.getElementById('sdr-proxy-preview');
    this.pollTimer = null;
    this.inFlight = false;
    this.lastObjectUrl = null;
  }
  startSdrProxyStream(fps = 15) {
    if (!this.previewImg) return;
    this.stopSdrProxyStream();
    const intervalMs = Math.max(33, Math.floor(1000 / Math.max(1, fps)));
    this.pollTimer = setInterval(async () => {
      if (this.inFlight || document.hidden) return;
      this.inFlight = true;
      try {
        const res = await fetch(`/api/v1/stream/latest_frame.jpg?t=${Date.now()}`, { cache: 'no-store' });
        if (res.ok) {
          const blob = await res.blob();
          if (blob.size > 0) {
            const url = URL.createObjectURL(blob);
            if (this.lastObjectUrl) URL.revokeObjectURL(this.lastObjectUrl);
            this.lastObjectUrl = url;
            this.previewImg.src = url;
            this.previewImg.style.display = 'block';
          }
        }
      } catch (_) {
        // Silent drop when backend is idle
      } finally {
        this.inFlight = false;
      }
    }, intervalMs);
  }
  stopSdrProxyStream() {
    if (this.pollTimer) {
      clearInterval(this.pollTimer);
      this.pollTimer = null;
    }
    if (this.lastObjectUrl) {
      URL.revokeObjectURL(this.lastObjectUrl);
      this.lastObjectUrl = null;
    }
  }
}
if (typeof window !== 'undefined') {
  window.rtxPreviewCtrl = new PreviewController();
  window.addEventListener('DOMContentLoaded', () => {
    window.rtxPreviewCtrl.startSdrProxyStream(15);
  });
  window.addEventListener('beforeunload', () => {
    window.rtxPreviewCtrl.stopSdrProxyStream();
  });
}
