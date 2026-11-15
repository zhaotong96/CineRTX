# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
# Copyright (C) 2026 CineRTX contributors
#
# Zero-knowledge JSON pipe: state.json <-> --schema-json / --session-stdio JSONL.
# The bridge duplicates no parameter knowledge, no report grammar and no output
# path rules; validation and derivation live in the frozen C++ SSOT.
#
# Standard-library only.  Multi-line PEP 8 layout is intentional: readability
# takes precedence over line-count targets (round-2 specification, Task 2).

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
MEDIA_TYPES = {".mp4": "video/mp4", ".m4v": "video/mp4", ".mov": "video/quicktime",
               ".mkv": "video/x-matroska", ".webm": "video/webm", ".avi": "video/x-msvideo"}
CHUNK_BYTES = 64 * 1024
MAX_BODY = 4 * 1024 * 1024


class BridgeError(Exception):
    def __init__(self, msg: str, status: int = 400) -> None:
        super().__init__(msg)
        self.status = status


def stamp() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%S")


def load_state() -> dict:
    try:
        doc = json.loads(STATE_PATH.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise BridgeError(f"state.json: {exc}", 500) from exc
    if not isinstance(doc, dict):
        raise BridgeError("state.json must hold an object", 500)
    return doc


def save_state(doc: dict) -> None:
    payload = (json.dumps(doc, indent=2, ensure_ascii=False) + "\n").encode("utf-8")
    tmp = STATE_PATH.with_suffix(".json.tmp")
    tmp.write_bytes(payload)
    os.replace(tmp, STATE_PATH)


def job0() -> dict:
    return {"status": "idle", "startedAt": None, "endedAt": None, "exitCode": None,
            "error": None, "progress": {"pct": 0.0, "frames": 0, "total": 0, "fps": 0.0},
            "report": None, "logTail": []}


runtime = {"probe": None,
           "engine": {"found": False, "resolved": "", "version": ""},
           "job": job0()}
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


def sections() -> dict:
    doc = load_state()
    return {key: doc.get(key) for key in SECTIONS}


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
            out = subprocess.run([str(path), "--version"], capture_output=True, text=True,
                                 timeout=15, creationflags=no_window())
            _ver_cache[str(path)] = (out.stdout or out.stderr or "").strip()
        except Exception:
            _ver_cache[str(path)] = ""
    return str(path), path.is_file(), _ver_cache.get(str(path), "")


def engine_json(*args: str, timeout: int = 30) -> dict:
    path, found, _ = resolve_engine()
    if not found:
        raise BridgeError(f"engine not found: {path}", 503)
    try:
        out = subprocess.run([path, *args], capture_output=True, text=True, timeout=timeout,
                             encoding="utf-8", creationflags=no_window())
        if out.returncode:
            raise BridgeError(f"{args[0]} exit {out.returncode}: {out.stderr.strip()}", 502)
        return json.loads(out.stdout)
    except (OSError, subprocess.SubprocessError, ValueError) as exc:
        raise BridgeError(f"{args[0]}: {exc}", 502) from exc


def schema() -> dict:
    return engine_json("--schema-json")


def probe() -> dict:
    try:
        result = {"available": True, "exitCode": 0,
                  "probe": engine_json("--probe", "--json", timeout=120)}
    except BridgeError as exc:
        result = {"available": False, "error": str(exc)}
    house(lambda rt: rt.update({"probe": result}), force=True, persist=True)
    return result


def apply_event(rt: dict, ev: dict) -> None:
    job = rt["job"]
    kind = ev.get("type")
    if kind == "progress":
        job["progress"].update({"pct": ev.get("pct", 0.0), "frames": ev.get("frames", 0),
                                "total": ev.get("total", 0), "fps": ev.get("fps", 0.0)})
    elif kind == "log":
        tail = job.setdefault("logTail", [])
        tail.append({"t": time.strftime("%H:%M:%S"), "src": ev.get("level", "INFO"),
                     "text": ev.get("text", "")})
        del tail[:-400]
    elif kind == "report":
        job.update({"status": ev.get("status", "failed"), "exitCode": ev.get("exitCode"),
                    "error": ev.get("error"), "report": ev.get("report"),
                    "endedAt": stamp()})
        if job["status"] == "done":
            job["progress"]["pct"] = 100.0


class JobRunner:
    def __init__(self) -> None:
        self.lock = threading.Lock()
        self.proc = None

    def status(self) -> str:
        with self.lock:
            proc = self.proc
        return "running" if proc is not None and proc.poll() is None else "idle"

    def start(self) -> dict:
        with self.lock:
            if self.proc is not None and self.proc.poll() is None:
                raise BridgeError("a job is already running", 409)
            doc = load_state()
            if not (doc.get("profile") or {}).get("input"):
                raise BridgeError("profile.input is empty")
            engine, _, _ = resolve_engine(doc)
            job = job0()
            job.update({"status": "running", "startedAt": stamp()})
            house(lambda rt: rt.update({"job": job}), force=True, persist=True)
            self.proc = subprocess.Popen(
                [engine, "--session-stdio"], cwd=str(HERE), stdin=subprocess.PIPE,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                encoding="utf-8", errors="replace", creationflags=no_window())
            proc = self.proc
            threading.Thread(target=self._pump, args=(proc,), daemon=True).start()
        proc.stdin.write(json.dumps(doc, ensure_ascii=False) + "\n")
        proc.stdin.flush()
        return {"started": True}

    def cancel(self) -> dict:
        with self.lock:
            proc = self.proc
        if proc is None or proc.poll() is not None:
            return {"cancelled": False, "reason": "no running job"}
        try:
            proc.stdin.write("cancel\n")
            proc.stdin.flush()
        except (OSError, ValueError):
            return {"cancelled": False, "reason": "stdin closed"}
        deadline = time.monotonic() + 5.0
        while proc.poll() is None and time.monotonic() < deadline:
            time.sleep(0.05)
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
        return {"cancelled": True}

    def _pump(self, proc) -> None:
        def drain_stderr() -> None:
            for line in proc.stderr:
                if line.strip():
                    event = {"type": "log", "level": "engine", "text": line.rstrip()}
                    house(lambda rt: apply_event(rt, event))

        watcher = threading.Thread(target=drain_stderr, daemon=True)
        watcher.start()
        for raw in proc.stdout:
            line = raw.strip()
            if not line:
                continue
            try:
                event = json.loads(line)
            except ValueError:
                event = {"type": "log", "level": "WARN", "text": line}
            final = event.get("type") == "report"
            house(lambda rt: apply_event(rt, event), force=final, persist=final)
        watcher.join(timeout=2)
        code = proc.wait()

        def close_without_report(rt: dict) -> None:
            if rt["job"]["status"] == "running":
                apply_event(rt, {"type": "report", "status": "failed", "exitCode": code,
                                 "error": "engine exited without a report event"})

        house(close_without_report, force=True, persist=True)
        with self.lock:
            if self.proc is proc:
                self.proc = None


JOB = JobRunner()


def state_doc() -> dict:
    doc = load_state()
    path, found, version = resolve_engine(doc)
    house(lambda rt: rt["engine"].update({"found": found, "resolved": path,
                                          "version": version}))
    doc["runtime"] = snapshot()
    return doc


def list_dir(raw: str) -> dict:
    if not raw or raw == "roots":
        roots = [{"name": f"{c}:\\", "path": f"{c}:\\", "type": "dir",
                  "size": None, "mtime": None}
                 for c in string.ascii_uppercase if os.path.exists(f"{c}:\\")]
        return {"path": "", "parent": None, "entries": roots}
    target = Path(raw)
    if not target.exists():
        raise BridgeError(f"path not found: {raw}")
    if target.is_file():
        target = target.parent
    entries = []
    try:
        for child in target.iterdir():
            try:
                is_dir = child.is_dir()
                entries.append({"name": child.name, "path": str(child),
                                "type": "dir" if is_dir else "file",
                                "size": None if is_dir else child.stat().st_size,
                                "mtime": None})
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
    server_version = "CineRTX-WebUI-Bridge/2.1"

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
        doc = load_state()
        for section in SECTIONS:
            if section not in payload:
                continue
            expected = list if section in ARRAY_SECTIONS else dict
            if not isinstance(payload[section], expected):
                raise BridgeError(f"section '{section}' must be a {expected.__name__}")
            doc[section] = payload[section]  # validation lives in the C++ SSOT
        save_state(doc)
        self._json({"ok": True})

    def _job(self) -> None:
        payload = json.loads(self._body().decode("utf-8") or "{}")
        action = payload.get("action", "start")
        if action == "start":
            if payload.get("profile"):
                doc = load_state()
                doc["profile"] = payload["profile"]
                save_state(doc)
            self._json({"ok": True, **JOB.start()})
        elif action == "cancel":
            self._json({"ok": True, **JOB.cancel()})
        else:
            raise BridgeError("action must be 'start' or 'cancel'")

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
    print(f"CineRTX WebUI bridge -> http://{args.host}:{args.port}/  state={STATE_PATH}",
          flush=True)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
