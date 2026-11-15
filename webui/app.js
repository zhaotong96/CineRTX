// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 CineRTX contributors
//
// CineRTX WebUI application — single JS file, zero dependencies.
//
// Architecture: ONE JSON document (`state`, mirrored 1:1 by state.json on the
// bridge) is the single source of truth.  Three workstation shapes (encoder /
// player / nle) are views over that same tree, sharing the same binding engine,
// the same kParamSchema contract and the same SSE runtime events:
//
//   DOM input  -> set(path, value) -> debounced PUT /api/state  (persist)
//   backend    -> SSE JSON events -> state.runtime merge         (render)
//   media      -> GET /api/media?path=... (HTTP Range 206)       (<video>)
//
// The UI knows nothing about processes, argv or the engine binary; it only
// ever exchanges JSON documents with /api/*.

"use strict";

/* ========================================================================= *
 *  i18n & parameter metadata are owned by the C++ SSOT:
 *    rtx::exportI18nJson()   -> src/core/i18n.cpp (113 bilingual ids)
 *    rtx::exportSchemaJson() -> src/core/param_schema.cpp (19 params)
 *  boot() fetches GET /api/schema; no translation or range is duplicated here.
 *  FALLBACK keeps the markup's own text when the engine binary is unavailable.
 * ========================================================================= */

let I18N = { en: {}, zh: {} };
let DEFAULTS = {};
let PARAMS = [];
const FALLBACK = {};
const SECTION_KEYS = ["ui", "profile", "engine", "queue", "player", "nle"];
const WORKSPACES = ["encoder", "player", "nle"];

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
  I18N = doc.i18n || { en: {}, zh: {} };
  DEFAULTS = doc.defaults || {};
  PARAMS = doc.params || [];
  applyParamBounds();
}

function applyParamBounds() {
  // Ranges come from kParamSchema (include/rtx/param_schema.h:78-98), never UI code.
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
  profile: {},
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
 *  Persistence — debounced PUT of the browser-owned sections.
 * ========================================================================= */

let persistTimer = null;
let applyingRemote = false;

function ownedSections() {
  return {
    version: state.version,
    ui: state.ui,
    engine: state.engine,
    profile: state.profile,
    queue: state.queue,
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
    await api("/api/state", "PUT", ownedSections()).catch(() => {});
  }
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
    // Both events: selectOption/checkbox surfaces differ in what they dispatch;
    // setPath is idempotent and schedulePersist is debounced, so double fires
    // are harmless while a missed event is not.
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

const OUTPUT_SUFFIX = { av1: "_remaster_av1.mp4", hevc: "_remaster_hevc.mp4" };
// Previous input/codec snapshots: an output counts as "auto-generated" when it
// matches <stem(prevInput)>_remaster_<av1|hevc>.mp4 in the same folder.  The
// pattern survives page reloads (unlike an in-memory flag) because the loaded
// state itself carries the evidence.
let prevInput = null;
let prevCodec = null;

function basename(path) {
  const cut = Math.max(path.lastIndexOf("/"), path.lastIndexOf("\\"));
  return cut >= 0 ? path.slice(cut + 1) : path;
}

function autoNameFor(input, codec) {
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
  const sameDir = prevCut >= 0 && outCut >= 0 &&
    output.slice(0, outCut + 1) === prevInputValue.slice(0, prevCut + 1);
  if (!sameDir) return false;
  const prevName = prevInputValue.slice(prevCut + 1);
  const prevDot = prevName.lastIndexOf(".");
  const prevStem = prevDot > 0 ? prevName.slice(0, prevDot) : prevName;
  const outName = output.slice(outCut + 1);
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
    const hit = evalWhen(el, "disableWhen");  // condition true -> control disabled
    el.classList.toggle("wui-disabled", hit);
    const controls = el.matches("input,select,button")
      ? [el]
      : el.querySelectorAll("input,select,button");
    controls.forEach((control) => {
      control.disabled = running || hit;
    });
  });
  document.getElementById("btn-start").disabled = running;
  document.getElementById("btn-stop").disabled = !running;
  document.getElementById("log-section").classList.toggle("wui-hidden", !state.ui.showLog);
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
}

function renderProgress() {
  const job = state.runtime.job || {};
  const prog = job.progress || {};
  const pct = Math.max(0, Math.min(100, prog.pct || 0));
  document.getElementById("progress-fill").style.width = `${pct}%`;
  let text;
  if (job.status === "running") {
    text = prog.total > 0
      ? `${pct.toFixed(1)}% · ${tr("StatFrames")} ${prog.frames}/${prog.total} · ${prog.fps || 0} FPS`
      : `${prog.frames || 0} ${tr("StatFrames")} · ${prog.fps || 0} FPS`;
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
  // Generic summary: surface the first device/GPU-ish string in the document.
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
  queuePump();
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
    renderRuntime();  // re-render composed strings in the new language
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
  document.getElementById("status-line").textContent = text;
}

/* ========================================================================= *
 *  Backend events — SSE; every payload is a JSON document.
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
  source.onerror = () => {
    /* EventSource reconnects automatically; chip stays as-is. */
  };
}

/* ========================================================================= *
 *  Workspace switcher — one state key drives all three shapes.
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
 *  Encoder shape — presets + batch queue.
 * ========================================================================= */

const PRESET_FIELDS = {
  codec: "codec", presetLevel: "preset", resolution: "resolution",
  vsrEnabled: "vsrEnabled", vsrQuality: "vsrQuality",
  fpsMode: "fpsMode", peakNits: "peakNits",
};

let selectedQueueId = null;
let queueSeq = 0;
let lastJobStatus = "idle";

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

function makeQueueItem(profile, presetName) {
  const input = profile.input || "";
  const codec = profile.codec || "av1";
  const output = profile.output || autoNameFor(input, codec);
  queueSeq += 1;
  return {
    id: `job-${Date.now()}-${queueSeq}`,
    input, output, presetName: presetName || "当前配置",
    codec, resolution: profile.resolution || "4k",
    fpsMode: profile.fpsMode || "60-nvofa",
    peakNits: profile.peakNits || 1000,
    status: "pending", pct: 0, fps: 0,
    profile: Object.assign({}, profile, { input, output }),
  };
}

function addQueueItem(profile, presetName) {
  if (!(profile.input || "").trim()) {
    setStatusMessage(tr("StatusDropFileOnly"));
    return;
  }
  const items = (state.queue || []).slice();
  items.push(makeQueueItem(profile, presetName));
  setPath("queue", items);
  schedulePersist();
}

function queueRow(item) {
  const cls = item.id === selectedQueueId ? " class=\"selected\"" : "";
  const pct = Math.max(0, Math.min(100, Number(item.pct) || 0));
  return `<tr data-queue-id="${esc(item.id)}"${cls}>`
    + `<td><span class="badge badge-${esc(item.status)}">${esc(item.status)}</span></td>`
    + `<td class="cell-name" title="${esc(item.input)}">${esc(basename(item.input || ""))}</td>`
    + `<td>${esc(String(item.codec || "").toUpperCase())} · ${esc(String(item.resolution || "").toUpperCase())}<br><span class="hint">${esc(item.presetName)}</span></td>`
    + `<td>${esc(item.fpsMode)}</td>`
    + `<td>${esc(item.peakNits)} nits</td>`
    + `<td class="cell-name" title="${esc(item.output)}">${esc(basename(item.output || ""))}</td>`
    + `<td><div class="mini-progress"><i style="width:${pct}%"></i></div><span class="hint">${pct.toFixed(0)}%</span></td>`
    + `</tr>`;
}

function renderQueue() {
  const body = document.getElementById("queue-body");
  const empty = document.getElementById("queue-empty");
  const items = state.queue || [];
  if (body) body.innerHTML = items.map(queueRow).join("");
  if (empty) empty.hidden = items.length > 0;
}

function selectQueueItem(id) {
  const item = (state.queue || []).find((entry) => entry.id === id);
  if (!item) return;
  selectedQueueId = id;
  setPath("profile", Object.assign({}, DEFAULTS, item.profile || {}));
  renderQueue();
}

function syncProfileToQueue() {
  if (!selectedQueueId || applyingRemote || !state.profile || !Object.keys(state.profile).length) return;
  const items = (state.queue || []).slice();
  const index = items.findIndex((entry) => entry.id === selectedQueueId);
  if (index < 0) return;
  const item = Object.assign({}, items[index], {
    profile: Object.assign({}, state.profile),
    input: state.profile.input,
    output: state.profile.output,
    codec: state.profile.codec,
    resolution: state.profile.resolution,
    fpsMode: state.profile.fpsMode,
    peakNits: state.profile.peakNits,
  });
  items[index] = item;
  state.queue = items;  // no notify: avoid re-entrant binding writes
  renderQueue();
}

function syncQueueProgress(prog, jobStatus) {
  const items = state.queue || [];
  const index = items.findIndex((entry) => entry.status === "running");
  if (index < 0) return;
  items[index].pct = prog.pct || 0;
  items[index].fps = prog.fps || 0;
  if (jobStatus !== "running") items[index].status = jobStatus;
  renderQueue();
}

function queuePump() {
  const current = (state.runtime.job || {}).status || "idle";
  if (current === lastJobStatus) return;
  if (lastJobStatus === "running" && current !== "running") {
    const items = (state.queue || []).slice();
    const index = items.findIndex((entry) => entry.status === "running");
    if (index >= 0) {
      items[index].status = current;
      items[index].pct = current === "done" ? 100 : items[index].pct;
      setPath("queue", items);
      schedulePersist();
    }
    const next = (state.queue || []).find((entry) => entry.status === "pending");
    if (next && current !== "cancelled") {
      setTimeout(() => startQueueItem(next.id), 350);
    }
  }
  lastJobStatus = current;
}

async function startQueueItem(id) {
  if ((state.runtime.job || {}).status === "running") return;
  const items = (state.queue || []).slice();
  const index = items.findIndex((entry) => entry.id === id);
  if (index < 0) return;
  items[index].status = "running";
  setPath("queue", items);
  const profile = Object.assign({}, DEFAULTS, items[index].profile || {});
  setPath("profile", profile);
  await flushPersist();
  try {
    await api("/api/job", "POST", { action: "start", profile });
  } catch (err) {
    setStatusMessage(`${tr("WuiReqFailed")}: ${err.message}`);
  }
}

function installEncoder() {
  document.querySelectorAll("[data-preset]").forEach((card) => {
    card.addEventListener("click", () => applyPreset(card));
  });
  document.getElementById("btn-queue-add").addEventListener("click", () => {
    addQueueItem(Object.assign({}, state.profile), "当前配置");
  });
  document.getElementById("btn-queue-remove").addEventListener("click", () => {
    if (!selectedQueueId) return;
    setPath("queue", (state.queue || []).filter((item) => item.id !== selectedQueueId));
    selectedQueueId = null;
    schedulePersist();
  });
  document.getElementById("btn-queue-clear").addEventListener("click", () => {
    setPath("queue", (state.queue || []).filter((item) => item.status !== "done"));
    schedulePersist();
  });
  const body = document.getElementById("queue-body");
  body.addEventListener("click", (event) => {
    const row = event.target && event.target.closest
      ? event.target.closest("tr[data-queue-id]") : null;
    if (row) selectQueueItem(row.dataset.queueId);
  });
  subscribe("queue", renderQueue);
  subscribe("profile", () => {
    syncProfileToQueue();
    syncRound2Controls();
  });
}

/* ========================================================================= *
 *  Job actions + probe (queue-aware, backward compatible with single job).
 * ========================================================================= */

async function startJob() {
  if (state.runtime.job.status === "running") return;
  await flushPersist();
  const pending = (state.queue || []).filter((item) => item.status === "pending" || item.status === "running");
  if (pending.length) {
    await startQueueItem(pending[0].id);
    return;
  }
  if (!state.profile.input) {
    setStatusMessage(tr("StatusDropFileOnly"));
    return;
  }
  if (state.profile.output && state.profile.output === state.profile.input) {
    setStatusMessage(tr("StatusSamePath"));
    return;
  }
  try {
    await api("/api/job", "POST", { action: "start" });
  } catch (err) {
    setStatusMessage(`${tr("WuiReqFailed")}: ${err.message}`);
  }
}

async function stopJob() {
  try {
    await api("/api/job", "POST", { action: "cancel" });
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
 *  Player shape — /api/media Range stream, A/B split wipe, transport.
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
  if (stage && stage.classList.toggle) stage.classList.toggle("has-media", Boolean(path));
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
    `FRUC    ${p.fpsMode || "60-nvofa"} · VSR L${p.vsrQuality || 4}${p.vsrEnabled === false ? " (OFF)" : ""}`,
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
  const setSplitFromEvent = (event) => {
    if (!stage || !stage.getBoundingClientRect) return;
    const rect = stage.getBoundingClientRect();
    const pct = Math.max(0, Math.min(100, (event.clientX - rect.left) / rect.width * 100));
    setPath("player.splitPos", Math.round(pct));
    schedulePersist();
  };
  if (stage && stage.addEventListener) {
    stage.addEventListener("pointerdown", (event) => {
      const handle = document.getElementById("ab-handle");
      if (event.target === handle || event.target === stage) setSplitFromEvent(event);
    });
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
 *  NLE shape — media bin, Flicks timecode, 4-track timeline, razor.
 * ========================================================================= */

const FLICKS_PER_MS = 705600;  // 705,600,000 Hz / 1000
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
  if (flicks) flicks.textContent = `${(ms * FLICKS_PER_MS).toLocaleString("en-US")} Flicks · 705,600,000 Hz`;
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
    host.innerHTML = `<div class="bin-empty">素材箱为空 · 点击「+ 导入素材」从本机磁盘添加</div>`;
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
  const video = document.getElementById("nle-video");
  if (!video) return;
  video.src = mediaUrl(path);
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
  addQueueItem(profile, state.nle.sequenceName || "NLE 序列");
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
  document.getElementById("btn-nle-play").addEventListener("click", () => {
    const video = document.getElementById("nle-video");
    if (video && typeof video.play === "function") video.play().catch(() => {});
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
 *  Generic JSON path browser (uses only /api/fs JSON).
 * ========================================================================= */

const fsCtx = { bindPath: null, mode: null, current: "", selectedFile: null };

function openBrowser(bindPath, mode) {
  fsCtx.bindPath = bindPath;
  fsCtx.mode = mode || null;
  fsCtx.current = getPath(bindPath) || "";
  fsCtx.selectedFile = null;
  document.getElementById("fs-title").textContent =
    `${tr("BtnBrowse")} → ${bindPath}`;
  document.getElementById("fs-path").value = fsCtx.current;
  document.getElementById("fs-modal").hidden = false;
  loadFsListing(fsCtx.current ? fsCtx.current : "roots");
}

function closeBrowser() {
  document.getElementById("fs-modal").hidden = true;
}

function fsModeAllowsFiles() {
  return fsCtx.bindPath !== null;  // all current targets are file paths
}

async function loadFsListing(path) {
  const list = document.getElementById("fs-list");
  try {
    const doc = await api(`/api/fs?path=${encodeURIComponent(path)}`, "GET");
    fsCtx.current = doc.path || "";
    document.getElementById("fs-path").value = fsCtx.current;
    list.textContent = "";
    if (doc.parent) {
      const up = document.createElement("div");
      up.className = "fs-item";
      up.innerHTML = "<span class='fs-name'>..</span>";
      up.addEventListener("click", () => loadFsListing(doc.parent));
      list.appendChild(up);
    }
    for (const entry of doc.entries || []) {
      const item = document.createElement("div");
      item.className = "fs-item";
      const name = document.createElement("span");
      name.className = "fs-name";
      name.textContent = (entry.type === "dir" ? "\uD83D\uDCC1 " : "\uD83D\uDCC4 ") + entry.name;
      const size = document.createElement("span");
      size.className = "fs-size";
      size.textContent = entry.type === "file" ? formatBytes(entry.size) : "";
      item.append(name, size);
      item.addEventListener("click", () => {
        if (entry.type === "dir") {
          loadFsListing(entry.path);
        } else {
          fsCtx.selectedFile = entry.path;
          document.getElementById("fs-path").value = entry.path;
          list.querySelectorAll(".fs-item").forEach((el) => el.classList.remove("selected"));
          item.classList.add("selected");
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
  const value = path || document.getElementById("fs-path").value.trim();
  if (!value) return;
  if (fsCtx.mode === "nle-bin") {
    addToMediaBin(value);
  } else if (fsCtx.mode === "queue-import") {
    const profile = Object.assign({}, DEFAULTS, state.profile, {
      input: value, output: autoNameFor(value, state.profile.codec || "av1"),
    });
    addQueueItem(profile, basename(value));
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
    await loadSchema();  // GET /api/schema -> C++ SSOT i18n + defaults + params
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
  syncRound2Controls();
  attachMedia();
  connectEvents();
}

boot();
