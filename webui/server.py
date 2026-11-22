# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
# Copyright (C) 2026 CineRTX contributors
#
# Zero-knowledge JSON pipe + Server-Owned Multi-File Heterogeneous Queue Engine:
# state.json <-> --schema-json / --session-stdio JSONL.
#
# Standard-library only.  Multi-line PEP 8 layout is intentional: readability
# takes precedence over line-count targets (Phase 10 Track 10W-3).

from __future__ import annotations

import argparse
import copy
import json
import os
import string
import subprocess
import sys
import threading
import time
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

HERE = Path(__file__).resolve().parent
STATE_PATH = HERE / "state.json"
SECTIONS = ("ui", "profile", "engine", "queue", "player", "nle")
ARRAY_SECTIONS = ("queue",)
STATIC = {
    "/": ("index.html", "text/html; charset=utf-8"),
    "/index.html": ("index.html", "text/html; charset=utf-8"),
    "/app.js": ("app.js", "text/javascript; charset=utf-8"),
    "/app.css": ("app.css", "text/css; charset=utf-8"),
    "/state.json": ("state.json", "application/json; charset=utf-8"),
}
MEDIA_TYPES = {
    ".mp4": "video/mp4",
    ".m4v": "video/mp4",
    ".mov": "video/quicktime",
    ".mkv": "video/x-matroska",
    ".webm": "video/webm",
    ".avi": "video/x-msvideo",
}
OUTPUT_SUFFIX = {
    "av1": "_remaster_av1.mp4",
    "av1_cpu": "_remaster_av1.mp4",
    "hevc": "_remaster_hevc.mp4",
    "hevc_cpu": "_remaster_hevc.mp4",
}
CHUNK_BYTES = 64 * 1024
MAX_BODY = 4 * 1024 * 1024
state_lock = threading.RLock()

class BridgeError(Exception):
    def __init__(self, msg: str, status: int = 400) -> None:
        super().__init__(msg)
        self.status = status

def stamp() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%S")

def load_state() -> dict:
    with state_lock:
        try:
            doc = json.loads(STATE_PATH.read_text(encoding="utf-8"))
        except (OSError, ValueError) as exc:
            raise BridgeError(f"state.json: {exc}", 500) from exc
        if not isinstance(doc, dict):
            raise BridgeError("state.json must hold an object", 500)
        return doc

def save_state(doc: dict) -> None:
    with state_lock:
        payload = (json.dumps(doc, indent=2, ensure_ascii=False) + "\n").encode("utf-8")
        tmp = STATE_PATH.with_suffix(".json.tmp")
        tmp.write_bytes(payload)
        os.replace(tmp, STATE_PATH)

def derive_output_path(input_path: str, codec: str) -> str:
    if not input_path:
        return ""
    cut = max(input_path.rfind("/"), input_path.rfind("\\"))
    prefix = input_path[: cut + 1] if cut >= 0 else ""
    name = input_path[cut + 1 :] if cut >= 0 else input_path
    dot = name.rfind(".")
    stem = name[:dot] if dot > 0 else name
    suffix = OUTPUT_SUFFIX.get((codec or "av1").lower(), OUTPUT_SUFFIX["av1"])
    return prefix + stem + suffix

def basename_str(path_str: str) -> str:
    cut = max(path_str.rfind("/"), path_str.rfind("\\"))
    return path_str[cut + 1 :] if cut >= 0 else path_str

def job0() -> dict:
    return {
        "status": "idle",
        "startedAt": None,
        "endedAt": None,
        "exitCode": None,
        "error": None,
        "progress": {"pct": 0.0, "frames": 0, "total": 0, "fps": 0.0},
        "report": None,
        "logTail": [],
    }
runtime = {
    "probe": None,
    "engine": {"found": False, "resolved": "", "version": ""},
    "job": job0(),
}
try:
    stored = load_state().get("runtime") or {}
    for key, value in stored.items():
        if isinstance(runtime.get(key), dict) and isinstance(value, dict):
            runtime[key] = {**runtime[key], **value}
        else:
            runtime[key] = value
except (BridgeError, OSError, ValueError):
    pass

rt_lock = threading.Lock()
bus_lock = threading.Lock()
clients: list = []
last_publish = 0.0
_ver_cache: dict = {}

def bus(ev: dict) -> None:
    with bus_lock:
        targets = list(clients)
    for queue in targets:
        queue.append(ev)

def snapshot() -> dict:
    with rt_lock:
        return copy.deepcopy(runtime)

def sections(doc: dict | None = None) -> dict:
    target = doc if doc is not None else load_state()
    return {key: target.get(key) for key in SECTIONS}

def broadcast_state(doc: dict | None = None) -> None:
    target = doc if doc is not None else load_state()
    bus({"type": "state-file", "state": sections(target)})

def house(fn, force: bool = False, persist: bool = False) -> None:
    global last_publish

    with rt_lock:
        fn(runtime)
        now = time.monotonic()
        publish = force or now - last_publish >= 0.25
        if publish:
            last_publish = now
        snap = copy.deepcopy(runtime) if publish or persist else None
    if snap is None:
        return
    if publish:
        bus({"type": "runtime", "runtime": snap})
    if persist:
        try:
            with state_lock:
                doc = load_state()
                doc["runtime"] = snap
                save_state(doc)
        except (BridgeError, OSError):
            pass

def no_window() -> int:
    return getattr(subprocess, "CREATE_NO_WINDOW", 0) if os.name == "nt" else 0

def resolve_engine(doc: dict | None = None) -> tuple[str, bool, str]:
    cfg = (doc or load_state()).get("engine") or {}
    raw = os.environ.get("CINERTX_ENGINE") or (cfg.get("path") or "").strip() or "CineRTX.exe"
    work = (cfg.get("workDir") or "").strip()
    if os.path.isabs(raw):
        candidates = [Path(raw)]
    else:
        candidates = ([Path(work) / raw] if work else [])
        candidates += [HERE / raw, HERE.parent / raw, Path.cwd() / raw]
    path = next((c for c in candidates if c.is_file()), candidates[0])
    if path.is_file() and str(path) not in _ver_cache:
        try:
            out = subprocess.run(
                [str(path), "--version"],
                capture_output=True,
                text=True,
                timeout=15,
                creationflags=no_window(),
            )
            _ver_cache[str(path)] = (out.stdout or out.stderr or "").strip()
        except Exception:
            _ver_cache[str(path)] = ""
    return str(path), path.is_file(), _ver_cache.get(str(path), "")

def engine_json(*args: str, timeout: int = 30) -> dict:
    path, found, _ = resolve_engine()
    if not found:
        raise BridgeError(f"engine not found: {path}", 503)
    try:
        out = subprocess.run(
            [path, *args],
            capture_output=True,
            text=True,
            timeout=timeout,
            encoding="utf-8",
            creationflags=no_window(),
        )
        if out.returncode:
            raise BridgeError(f"{args[0]} exit {out.returncode}: {out.stderr.strip()}", 502)
        return json.loads(out.stdout)
    except (OSError, subprocess.SubprocessError, ValueError) as exc:
        raise BridgeError(f"{args[0]}: {exc}", 502) from exc

def schema() -> dict:
    return engine_json("--schema-json")

def probe() -> dict:
    try:
        result = {
            "available": True,
            "exitCode": 0,
            "probe": engine_json("--probe", "--json", timeout=120),
        }
    except BridgeError as exc:
        result = {"available": False, "error": str(exc)}
    house(lambda rt: rt.update({"probe": result}), force=True, persist=True)
    return result

def apply_event(rt: dict, ev: dict) -> None:
    job = rt["job"]
    kind = ev.get("type")
    if kind == "progress":
        job["progress"].update(
            {
                "pct": ev.get("pct", 0.0),
                "frames": ev.get("frames", 0),
                "total": ev.get("total", 0),
                "fps": ev.get("fps", 0.0),
            }
        )
    elif kind == "log":
        tail = job.setdefault("logTail", [])
        tail.append(
            {
                "t": time.strftime("%H:%M:%S"),
                "src": ev.get("level", "INFO"),
                "text": ev.get("text", ""),
            }
        )
        del tail[:-400]
    elif kind == "report":
        job.update(
            {
                "status": ev.get("status", "failed"),
                "exitCode": ev.get("exitCode"),
                "error": ev.get("error"),
                "report": ev.get("report"),
                "endedAt": stamp(),
            }
        )
        if job["status"] == "done":
            job["progress"]["pct"] = 100.0

def normalize_job_entry(
    raw_inputs: list[str],
    profile: dict,
    preset_name: str = "当前配置",
    job_id: str | None = None,
) -> dict:
    clean_inputs = [str(p).strip() for p in (raw_inputs or []) if str(p).strip()]
    if not clean_inputs and (profile.get("input") or "").strip():
        clean_inputs = [str(profile["input"]).strip()]
    if not clean_inputs:
        raise BridgeError("enqueue requires at least one non-empty input file")
    codec = str(profile.get("codec") or "av1")
    first_input = clean_inputs[0]
    explicit_out = str(profile.get("output") or "").strip()
    if len(clean_inputs) == 1 and explicit_out and explicit_out != first_input:
        output = explicit_out
    else:
        output = derive_output_path(first_input, codec)
    snap_profile = copy.deepcopy(profile)
    snap_profile.setdefault("codec", codec)
    snap_profile.setdefault("preset", 5)
    snap_profile.setdefault("rateControl", "cq")
    snap_profile.setdefault("qp", 22)
    snap_profile.setdefault("resolution", "4k")
    snap_profile.setdefault("vsrEnabled", True)
    snap_profile.setdefault("vsrQuality", 4)
    snap_profile.setdefault("fpsMode", "60-nvofa")
    snap_profile.setdefault("pipelineOrder", "vsr-first")
    snap_profile.setdefault("peakNits", 1000)
    snap_profile["input"] = first_input
    snap_profile["output"] = output
    name = (
        basename_str(first_input)
        if len(clean_inputs) == 1
        else f"{basename_str(first_input)} (+{len(clean_inputs) - 1} files)"
    )
    return {
        "id": job_id or f"job-{int(time.time() * 1000)}",
        "name": name,
        "inputs": clean_inputs,
        "input": first_input,
        "output": output,
        "presetName": preset_name or "当前配置",
        "codec": snap_profile["codec"],
        "rateControl": snap_profile["rateControl"],
        "qp": snap_profile["qp"],
        "resolution": snap_profile["resolution"],
        "fpsMode": snap_profile["fpsMode"],
        "pipelineOrder": snap_profile["pipelineOrder"],
        "peakNits": snap_profile["peakNits"],
        "status": "pending",
        "fileCount": len(clean_inputs),
        "currentFileIndex": 0,
        "currentFilePct": 0.0,
        "totalJobPct": 0.0,
        "pct": 0.0,
        "fps": 0.0,
        "error": None,
        "profile": snap_profile,
    }
class JobRunner:
    def __init__(self) -> None:
        self.lock = threading.RLock()
        self.proc: subprocess.Popen | None = None
        self.worker_thread: threading.Thread | None = None
        self.worker_active = False
        self.stop_all_requested = False
        self.skip_current_requested = False
        self._seq = 0

    def _next_id(self) -> str:
        with self.lock:
            self._seq += 1
            return f"job-{int(time.time() * 1000)}-{self._seq}"

    def status(self) -> str:
        with self.lock:
            if self.worker_active or (self.proc is not None and self.proc.poll() is None):
                return "running"
            return "idle"

    def enqueue(self, payload: dict) -> dict:
        with state_lock:
            doc = load_state()
            base_profile = copy.deepcopy(doc.get("profile") or {})
            req_profile = payload.get("profile")
            if isinstance(req_profile, dict):
                base_profile.update(req_profile)
            raw_inputs = payload.get("inputs")
            if not isinstance(raw_inputs, list):
                single = payload.get("input") or base_profile.get("input") or ""
                raw_inputs = [single] if str(single).strip() else []
            preset_name = str(payload.get("presetName") or "当前配置")
            entry = normalize_job_entry(
                raw_inputs, base_profile, preset_name=preset_name, job_id=self._next_id()
            )
            queue = list(doc.get("queue") or [])
            queue.append(entry)
            doc["queue"] = queue
            save_state(doc)
        broadcast_state(doc)
        return {"enqueued": True, "jobId": entry["id"], "job": entry}

    def update(self, payload: dict) -> dict:
        job_id = str(payload.get("jobId") or "").strip()
        if not job_id:
            raise BridgeError("jobId is required for update")
        with state_lock:
            doc = load_state()
            queue = list(doc.get("queue") or [])
            idx = next((i for i, item in enumerate(queue) if item.get("id") == job_id), -1)
            if idx < 0:
                raise BridgeError(f"job not found: {job_id}", 404)
            target = queue[idx]
            if target.get("status") != "pending":
                raise BridgeError("Only pending jobs can be updated", 409)
            merged_profile = copy.deepcopy(target.get("profile") or {})
            if isinstance(payload.get("profile"), dict):
                merged_profile.update(payload["profile"])
            raw_inputs = payload.get("inputs")
            if not isinstance(raw_inputs, list) or not raw_inputs:
                raw_inputs = list(target.get("inputs") or [])
                if len(raw_inputs) <= 1 and (merged_profile.get("input") or "").strip():
                    raw_inputs = [str(merged_profile["input"]).strip()]
            updated = normalize_job_entry(
                raw_inputs,
                merged_profile,
                preset_name=str(payload.get("presetName") or target.get("presetName") or "当前配置"),
                job_id=job_id,
            )
            queue[idx] = updated
            doc["queue"] = queue
            save_state(doc)
        broadcast_state(doc)
        return {"updated": True, "jobId": job_id, "job": updated}

    def remove(self, payload: dict) -> dict:
        job_id = str(payload.get("jobId") or "").strip()
        if not job_id:
            raise BridgeError("jobId is required for remove")
        with state_lock:
            doc = load_state()
            queue = list(doc.get("queue") or [])
            idx = next((i for i, item in enumerate(queue) if item.get("id") == job_id), -1)
            if idx < 0:
                raise BridgeError(f"job not found: {job_id}", 404)
            if queue[idx].get("status") == "running":
                raise BridgeError("Cannot remove a running job; use skip instead", 409)
            removed = queue.pop(idx)
            doc["queue"] = queue
            save_state(doc)
        broadcast_state(doc)
        return {"removed": True, "jobId": job_id, "job": removed}

    def move(self, payload: dict) -> dict:
        job_id = str(payload.get("jobId") or "").strip()
        delta = int(payload.get("delta") or 0)
        if not job_id:
            raise BridgeError("jobId is required for move")
        with state_lock:
            doc = load_state()
            queue = list(doc.get("queue") or [])
            idx = next((i for i, item in enumerate(queue) if item.get("id") == job_id), -1)
            if idx < 0:
                raise BridgeError(f"job not found: {job_id}", 404)
            if queue[idx].get("status") != "pending":
                raise BridgeError("Only pending jobs can be reordered", 409)
            if delta != 0:
                pending_indices = [
                    i for i, item in enumerate(queue) if item.get("status") == "pending"
                ]
                pos = pending_indices.index(idx)
                new_pos = max(0, min(len(pending_indices) - 1, pos + (-1 if delta < 0 else 1)))
                target_idx = pending_indices[new_pos]
                if target_idx != idx:
                    item = queue.pop(idx)
                    queue.insert(target_idx, item)
                    doc["queue"] = queue
                    save_state(doc)
        broadcast_state(doc)
        return {"moved": True, "jobId": job_id}

    def retry(self, payload: dict) -> dict:
        job_id = str(payload.get("jobId") or "").strip()
        with state_lock:
            doc = load_state()
            queue = list(doc.get("queue") or [])
            count = 0
            for item in queue:
                if job_id and item.get("id") != job_id:
                    continue
                if item.get("status") in ("failed", "cancelled"):
                    item["status"] = "pending"
                    item["currentFileIndex"] = 0
                    item["currentFilePct"] = 0.0
                    item["totalJobPct"] = 0.0
                    item["pct"] = 0.0
                    item["fps"] = 0.0
                    item["error"] = None
                    count += 1
            if job_id and count == 0:
                raise BridgeError("Job not found or not in failed/cancelled state", 409)
            doc["queue"] = queue
            save_state(doc)
        broadcast_state(doc)
        return {"retried": count}

    def clear(self, payload: dict) -> dict:
        completed_only = bool(payload.get("completedOnly", True))
        with state_lock:
            doc = load_state()
            queue = list(doc.get("queue") or [])
            if completed_only:
                kept = [
                    item
                    for item in queue
                    if item.get("status") not in ("done", "completed", "failed", "cancelled")
                ]
            else:
                kept = [item for item in queue if item.get("status") == "running"]
            doc["queue"] = kept
            save_state(doc)
        broadcast_state(doc)
        return {"cleared": len(queue) - len(kept)}

    def start(self, payload: dict | None = None) -> dict:
        payload = payload or {}
        with self.lock:
            if self.worker_active or (self.proc is not None and self.proc.poll() is None):
                raise BridgeError("a job is already running", 409)
            with state_lock:
                doc = load_state()
                if isinstance(payload.get("profile"), dict):
                    doc["profile"] = payload["profile"]
                    save_state(doc)
                queue = list(doc.get("queue") or [])
                has_pending = any(item.get("status") == "pending" for item in queue)
                if not has_pending:
                    prof = doc.get("profile") or {}
                    if not (prof.get("input") or "").strip():
                        raise BridgeError("profile.input is empty")
            self.stop_all_requested = False
            self.skip_current_requested = False
            self.worker_active = True
            job = job0()
            job.update({"status": "running", "startedAt": stamp()})
            house(lambda rt: rt.update({"job": job}), force=True, persist=True)
            self.worker_thread = threading.Thread(
                target=self._queue_worker, args=(has_pending,), daemon=True
            )
            self.worker_thread.start()
        return {"started": True}

    def skip(self) -> dict:
        with self.lock:
            self.skip_current_requested = True
            proc = self.proc
        if proc is None or proc.poll() is not None:
            return {"skipped": False, "reason": "no running job"}
        self._terminate_proc(proc)
        return {"skipped": True}

    def stop(self) -> dict:
        with self.lock:
            self.stop_all_requested = True
            self.skip_current_requested = True
            proc = self.proc
        if proc is None or proc.poll() is not None:
            return {"cancelled": False, "stopped": True, "reason": "no running job"}
        self._terminate_proc(proc)
        return {"cancelled": True, "stopped": True}

    cancel = stop

    def _terminate_proc(self, proc: subprocess.Popen) -> None:
        try:
            if proc.stdin:
                proc.stdin.write("cancel\n")
                proc.stdin.flush()
        except (OSError, ValueError):
            pass
        deadline = time.monotonic() + 5.0
        while proc.poll() is None and time.monotonic() < deadline:
            time.sleep(0.05)
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()

    def _claim_next_pending_job(self) -> dict | None:
        with state_lock:
            doc = load_state()
            queue = list(doc.get("queue") or [])
            for item in queue:
                if item.get("status") == "pending":
                    item["status"] = "running"
                    item["currentFileIndex"] = 0
                    item["currentFilePct"] = 0.0
                    item["totalJobPct"] = 0.0
                    item["pct"] = 0.0
                    item["error"] = None
                    doc["queue"] = queue
                    save_state(doc)
                    broadcast_state(doc)
                    return copy.deepcopy(item)
        return None

    def _update_queue_job(self, job_id: str, updater) -> None:
        with state_lock:
            try:
                doc = load_state()
                queue = list(doc.get("queue") or [])
                for item in queue:
                    if item.get("id") == job_id:
                        updater(item)
                        break
                doc["queue"] = queue
                save_state(doc)
            except (BridgeError, OSError):
                return
        broadcast_state(doc)

    def _queue_worker(self, queue_mode: bool) -> None:
        last_status = "done"
        last_exit = 0
        last_error = None
        last_report = None
        try:
            if not queue_mode:
                doc = load_state()
                prof = copy.deepcopy(doc.get("profile") or {})
                inp = str(prof.get("input") or "").strip()
                codec = str(prof.get("codec") or "av1")
                outp = str(prof.get("output") or "").strip() or derive_output_path(inp, codec)
                prof["input"] = inp
                prof["output"] = outp
                last_status, last_exit, last_error, last_report = self._run_single_file(
                    doc, prof, None, 0, 1
                )
                return

            while True:
                with self.lock:
                    if self.stop_all_requested:
                        last_status = "cancelled"
                        break
                    self.skip_current_requested = False

                current_job = self._claim_next_pending_job()
                if current_job is None:
                    break

                job_id = str(current_job.get("id"))
                inputs = list(current_job.get("inputs") or [])
                if not inputs and current_job.get("input"):
                    inputs = [str(current_job["input"])]
                total_files = max(1, len(inputs))
                base_profile = copy.deepcopy(current_job.get("profile") or {})
                codec = str(base_profile.get("codec") or current_job.get("codec") or "av1")
                job_failed = False
                job_cancelled = False
                job_err_msg = None

                for file_idx, input_path in enumerate(inputs):
                    with self.lock:
                        if self.stop_all_requested or self.skip_current_requested:
                            job_cancelled = True
                            break

                    file_profile = copy.deepcopy(base_profile)
                    file_profile["input"] = input_path
                    if total_files == 1 and (base_profile.get("output") or "").strip():
                        file_profile["output"] = str(base_profile["output"]).strip()
                    else:
                        file_profile["output"] = derive_output_path(input_path, codec)

                    def mark_file_start(item: dict, idx: int = file_idx, fpath: str = input_path, opath: str = file_profile["output"]) -> None:
                        item["currentFileIndex"] = idx
                        item["currentFilePct"] = 0.0
                        item["input"] = fpath
                        item["output"] = opath

                    self._update_queue_job(job_id, mark_file_start)

                    doc = load_state()
                    status, exit_code, err_text, rep = self._run_single_file(
                        doc, file_profile, job_id, file_idx, total_files
                    )
                    last_exit = exit_code
                    last_report = rep

                    with self.lock:
                        skipped_or_stopped = self.skip_current_requested or self.stop_all_requested

                    if skipped_or_stopped or status == "cancelled":
                        job_cancelled = True
                        job_err_msg = err_text or "cancelled"
                        break
                    if status != "done":
                        job_failed = True
                        job_err_msg = err_text or f"exit {exit_code}"
                        break

                if job_cancelled:
                    final_job_status = "cancelled"
                    last_status = "cancelled"
                    last_error = job_err_msg
                elif job_failed:
                    final_job_status = "failed"
                    last_status = "failed"
                    last_error = job_err_msg
                else:
                    final_job_status = "done"
                    last_status = "done"
                    last_error = None

                def mark_job_done(item: dict) -> None:
                    item["status"] = final_job_status
                    if final_job_status == "done":
                        item["currentFileIndex"] = total_files
                        item["currentFilePct"] = 100.0
                        item["totalJobPct"] = 100.0
                        item["pct"] = 100.0
                    item["error"] = job_err_msg

                self._update_queue_job(job_id, mark_job_done)

                with self.lock:
                    if self.stop_all_requested:
                        last_status = "cancelled"
                        break
        finally:
            def finalize_rt(rt: dict) -> None:
                rt["job"].update(
                    {
                        "status": last_status,
                        "exitCode": last_exit,
                        "error": last_error,
                        "report": last_report if last_report is not None else rt["job"].get("report"),
                        "endedAt": stamp(),
                    }
                )
                if last_status == "done":
                    rt["job"]["progress"]["pct"] = 100.0

            with self.lock:
                self.proc = None
                self.worker_active = False
            house(finalize_rt, force=True, persist=True)

    def _run_single_file(
        self,
        doc: dict,
        file_profile: dict,
        job_id: str | None,
        file_idx: int,
        total_files: int,
    ) -> tuple[str, int | None, str | None, dict | None]:
        engine, found, _ = resolve_engine(doc)
        if not found:
            err_msg = f"engine not found: {engine}"
            house(
                lambda rt: apply_event(
                    rt, {"type": "log", "level": "ERROR", "text": err_msg}
                ),
                force=True,
            )
            return "failed", 127, err_msg, None

        session_doc = copy.deepcopy(doc)
        session_doc["profile"] = file_profile

        def reset_running(rt: dict) -> None:
            rt["job"]["status"] = "running"
            rt["job"]["error"] = None

        house(reset_running, force=True)

        try:
            proc = subprocess.Popen(
                [engine, "--session-stdio"],
                cwd=str(HERE),
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                encoding="utf-8",
                errors="replace",
                creationflags=no_window(),
            )
        except OSError as exc:
            return "failed", 127, str(exc), None

        with self.lock:
            self.proc = proc

        try:
            if proc.stdin:
                proc.stdin.write(json.dumps(session_doc, ensure_ascii=False) + "\n")
                proc.stdin.flush()
        except OSError:
            pass

        def drain_stderr() -> None:
            if not proc.stderr:
                return
            for line in proc.stderr:
                if line.strip():
                    event = {"type": "log", "level": "engine", "text": line.rstrip()}
                    house(lambda rt: apply_event(rt, event))

        watcher = threading.Thread(target=drain_stderr, daemon=True)
        watcher.start()

        file_status = "running"
        file_exit: int | None = None
        file_error: str | None = None
        file_report: dict | None = None

        if proc.stdout:
            for raw in proc.stdout:
                line = raw.strip()
                if not line:
                    continue
                try:
                    event = json.loads(line)
                except ValueError:
                    event = {"type": "log", "level": "WARN", "text": line}
                kind = event.get("type")
                if kind == "progress":
                    pct = float(event.get("pct") or 0.0)
                    fps = float(event.get("fps") or 0.0)
                    total_pct = ((file_idx * 100.0) + pct) / max(1, total_files)
                    scaled_event = dict(event)
                    scaled_event["pct"] = total_pct
                    house(lambda rt, ev=scaled_event: apply_event(rt, ev))
                    if job_id:
                        def upd_prog(
                            item: dict,
                            idx: int = file_idx,
                            fpct: float = pct,
                            tpct: float = total_pct,
                            ffps: float = fps,
                        ) -> None:
                            item["currentFileIndex"] = idx
                            item["currentFilePct"] = round(fpct, 2)
                            item["totalJobPct"] = round(tpct, 2)
                            item["pct"] = round(tpct, 2)
                            item["fps"] = round(ffps, 2)

                        self._update_queue_job(job_id, upd_prog)
                elif kind == "report":
                    file_status = str(event.get("status") or "failed")
                    file_exit = event.get("exitCode")
                    file_error = event.get("error")
                    file_report = event.get("report")
                    if file_status == "done" and job_id:
                        total_pct = ((file_idx + 1) * 100.0) / max(1, total_files)

                        def upd_done(item: dict, idx: int = file_idx, tpct: float = total_pct) -> None:
                            item["currentFileIndex"] = idx + 1
                            item["currentFilePct"] = 100.0
                            item["totalJobPct"] = round(tpct, 2)
                            item["pct"] = round(tpct, 2)

                        self._update_queue_job(job_id, upd_done)
                    try:
                        if proc.stdin:
                            proc.stdin.write("quit\n")
                            proc.stdin.flush()
                    except OSError:
                        pass
                    break
                else:
                    house(lambda rt, ev=event: apply_event(rt, ev))

        watcher.join(timeout=2)
        code = proc.wait()
        with self.lock:
            if self.proc is proc:
                self.proc = None
            was_cancelled = self.skip_current_requested or self.stop_all_requested

        if was_cancelled:
            return "cancelled", code, "cancelled", file_report
        if file_status == "running":
            if code == 0:
                return "done", 0, None, file_report
            return "failed", code, "engine exited without a report event", file_report
        return file_status, (file_exit if file_exit is not None else code), file_error, file_report

JOB = JobRunner()

def state_doc() -> dict:
    doc = load_state()
    path, found, version = resolve_engine(doc)
    house(
        lambda rt: rt["engine"].update(
            {"found": found, "resolved": path, "version": version}
        )
    )
    doc["runtime"] = snapshot()
    return doc

def list_dir(raw: str) -> dict:
    if not raw or raw == "roots":
        if os.name == "nt":
            roots = [
                {
                    "name": f"{c}:\\",
                    "path": f"{c}:\\",
                    "type": "dir",
                    "size": None,
                    "mtime": None,
                }
                for c in string.ascii_uppercase
                if os.path.exists(f"{c}:\\")
            ]
        else:
            roots = [{"name": "/", "path": "/", "type": "dir", "size": None, "mtime": None}]
        return {"path": "", "parent": None, "entries": roots}
    target = Path(raw)
    if not target.exists():
        raise BridgeError(f"path not found: {raw}")
    if target.is_file():
        target = target.parent
    entries = []
    try:
        for child in sorted(target.iterdir(), key=lambda p: (not p.is_dir(), p.name.lower())):
            try:
                is_dir = child.is_dir()
                entries.append(
                    {
                        "name": child.name,
                        "path": str(child),
                        "type": "dir" if is_dir else "file",
                        "size": None if is_dir else child.stat().st_size,
                        "mtime": None,
                    }
                )
            except OSError:
                pass
    except OSError as exc:
        raise BridgeError(f"cannot list {target}: {exc}") from exc
    parent = str(target.parent) if target.parent != target else None
    return {"path": str(target), "parent": parent, "entries": entries[:2000]}

def parse_range(header: str, size: int) -> tuple[int, int] | None:
    """Return an inclusive byte window, or None for an unsatisfiable range."""
    if not header.startswith("bytes="):
        return None
    spec = header[6:].split(",", 1)[0].strip()
    first, sep, last = spec.partition("-")
    if not sep:
        return None
    try:
        if first:
            start = int(first)
            end = int(last) if last else size - 1
        else:
            if not last:
                return None
            start = max(0, size - int(last))
            end = size - 1
    except ValueError:
        return None
    if start < 0 or start >= size or end < start:
        return None
    return start, min(end, size - 1)

class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "CineRTX-WebUI-Bridge/2.2"

    def log_message(self, fmt, *args) -> None:
        pass

    def _json(self, obj: dict, status: int = 200) -> None:
        body = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _fail(self, exc: Exception) -> None:
        self._json({"ok": False, "error": str(exc)}, getattr(exc, "status", 400))

    def _body(self) -> bytes:
        size = int(self.headers.get("Content-Length") or 0)
        if size > MAX_BODY:
            raise BridgeError("request body too large", 413)
        return self.rfile.read(size) if size else b""

    def _static(self, route: str) -> None:
        name, content_type = STATIC[route]
        body = (HERE / name).read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _media(self, raw: str) -> None:
        """Stream a local video with HTTP Range support for HTML5 <video>."""
        path = Path(raw or "")
        if not path.is_file():
            raise BridgeError(f"media not found: {raw}", 404)
        size = path.stat().st_size
        content_type = MEDIA_TYPES.get(path.suffix.lower(), "application/octet-stream")
        status = 200
        start, end = 0, size - 1
        header = self.headers.get("Range")
        if header:
            window = parse_range(header, size)
            if window is None:
                self.send_response(416)
                self.send_header("Content-Range", f"bytes */{size}")
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
            start, end = window
            status = 206
        length = max(0, end - start + 1)
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Content-Length", str(length))
        if status == 206:
            self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
        self.end_headers()
        try:
            with path.open("rb") as src:
                src.seek(start)
                remaining = length
                while remaining > 0:
                    block = src.read(min(CHUNK_BYTES, remaining))
                    if not block:
                        break
                    self.wfile.write(block)
                    remaining -= len(block)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError, OSError):
            return

    def _put_state(self) -> None:
        payload = json.loads(self._body().decode("utf-8"))
        with state_lock:
            doc = load_state()
            for section in SECTIONS:
                if section not in payload:
                    continue
                expected = list if section in ARRAY_SECTIONS else dict
                if not isinstance(payload[section], expected):
                    raise BridgeError(f"section '{section}' must be a {expected.__name__}")
                doc[section] = payload[section]
            save_state(doc)
        self._json({"ok": True})

    def _job(self) -> None:
        payload = json.loads(self._body().decode("utf-8") or "{}")
        action = payload.get("action", "start")
        if action == "enqueue":
            self._json({"ok": True, **JOB.enqueue(payload)})
        elif action == "update":
            self._json({"ok": True, **JOB.update(payload)})
        elif action == "remove":
            self._json({"ok": True, **JOB.remove(payload)})
        elif action == "move":
            self._json({"ok": True, **JOB.move(payload)})
        elif action == "retry":
            self._json({"ok": True, **JOB.retry(payload)})
        elif action == "clear":
            self._json({"ok": True, **JOB.clear(payload)})
        elif action == "skip":
            self._json({"ok": True, **JOB.skip()})
        elif action == "start":
            self._json({"ok": True, **JOB.start(payload)})
        elif action in ("stop", "cancel"):
            self._json({"ok": True, **JOB.stop()})
        else:
            raise BridgeError(
                "action must be one of: enqueue, update, remove, move, skip, retry, clear, start, stop, cancel"
            )

    def _dispatch(self) -> None:
        parsed = urlparse(self.path)
        route = parsed.path
        try:
            if self.command == "GET" and route in STATIC:
                self._static(route)
            elif self.command == "GET" and route == "/api/state":
                self._json({"ok": True, "state": state_doc()})
            elif self.command == "GET" and route == "/api/schema":
                self._json({"ok": True, **schema()})
            elif self.command == "GET" and route == "/api/events":
                self._events()
            elif self.command == "GET" and route == "/api/fs":
                raw = parse_qs(parsed.query).get("path", [""])[0]
                self._json({"ok": True, **list_dir(raw)})
            elif self.command == "GET" and route == "/api/media":
                raw = parse_qs(parsed.query).get("path", [""])[0]
                self._media(raw)
            elif self.command == "PUT" and route == "/api/state":
                self._put_state()
            elif self.command == "POST" and route == "/api/probe":
                self._json({"ok": True, "result": probe()})
            elif self.command == "POST" and route == "/api/job":
                self._job()
            else:
                self._json({"ok": False, "error": f"unknown route {route}"}, 404)
        except Exception as exc:
            self._fail(exc)

    do_GET = do_PUT = do_POST = _dispatch

    def _events(self) -> None:
        queue = deque(maxlen=512)
        with bus_lock:
            clients.append(queue)
        try:
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self._sse({"type": "hello", "state": sections(), "runtime": snapshot()})
            while True:
                try:
                    self._sse(queue.popleft())
                except IndexError:
                    time.sleep(0.25)
                self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError, OSError):
            pass
        finally:
            with bus_lock:
                if queue in clients:
                    clients.remove(queue)

    def _sse(self, ev: dict) -> None:
        body = f"data: {json.dumps(ev, ensure_ascii=False)}\n\n".encode("utf-8")
        self.wfile.write(body)

def main() -> int:
    global STATE_PATH

    parser = argparse.ArgumentParser(description="CineRTX WebUI JSON bridge")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8787)
    parser.add_argument("--engine")
    parser.add_argument("--state")
    args = parser.parse_args()
    if args.state:
        STATE_PATH = Path(args.state).resolve()
    if args.engine:
        os.environ["CINERTX_ENGINE"] = args.engine
    if not STATE_PATH.exists():
        print(f"FATAL: state document not found at {STATE_PATH}", file=sys.stderr)
        return 2
    httpd = ThreadingHTTPServer((args.host, args.port), Handler)
    httpd.daemon_threads = True
    print(
        f"CineRTX WebUI bridge -> http://{args.host}:{args.port}/  state={STATE_PATH}",
        flush=True,
    )
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
