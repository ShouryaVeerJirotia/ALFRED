#!/usr/bin/env python3
"""Local ALFRED desktop companion: proxy the ESP32 dashboard and stage Codex patches."""

from __future__ import annotations

import argparse
import http.server
import ipaddress
import json
import os
import re
import shutil
import subprocess
import tempfile
import threading
import urllib.error
import urllib.parse
import urllib.request
import uuid
from pathlib import Path

MAX_BODY = 16_000
MAX_WORKSPACE_BYTES = 200 * 1024 * 1024
MAX_SOURCE_FILE_BYTES = 20 * 1024 * 1024
MAX_PATCH_BYTES = 80_000
TASK_TIMEOUT_SECONDS = 600
SECRET_NAME = re.compile(r"(^|/)(\.env($|\.)|\.?ssh($|/)|[^/]*(secret|credential|password|api[-_]?key|private[-_]?key|service[-_]?(role|account)|adminsdk)[^/]*|tokens?($|[-_.])|[^/]+[-_.]tokens?([-_.][^/]*)?|id_(rsa|ed25519|ecdsa)(\..*)?|authorized_keys($|\.))|\.(pem|p12|pfx|key|p8|der|crt|cer|keystore)$", re.I)
SECRET_CONTENT = re.compile(r"AIza[0-9A-Za-z_-]{30,}|AQ\.[A-Za-z0-9_-]{20,}|sb_secret_[A-Za-z0-9_-]{20,}|-----BEGIN (?:RSA |EC |OPENSSH |DSA )?PRIVATE KEY-----|(?:GEMINI|GOOGLE|OPENAI|SUPABASE)(?:_[A-Z0-9]+)*_?(?:API_)?KEYS?\s*[:=]\s*[\"']?[A-Za-z0-9_-]{20,}", re.I)
SKIP_DIRS = {".git", ".venv", "venv", "node_modules", "__pycache__", "work", ".gradle", "build"}
HOP_HEADERS = {"connection", "keep-alive", "proxy-authenticate", "proxy-authorization", "te", "trailers", "transfer-encoding", "upgrade", "host", "content-length"}

JOBS: dict[str, dict] = {}
JOBS_LOCK = threading.RLock()
ROOT = Path.cwd()
UPSTREAM = ""
CODEX = ""
CODEX_LOGIN_OK = False
PORT = 8765


def git(args: list[str], *, cwd: Path, data: bytes | None = None, timeout: int = 30) -> subprocess.CompletedProcess:
    return subprocess.run(["git", *args], cwd=cwd, input=data, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout, check=False)


def run_env() -> dict[str, str]:
    """Keep only the environment Codex needs for local sign-in and command lookup."""
    allowed = {"PATH", "HOME", "USER", "LOGNAME", "LANG", "LC_ALL", "LC_CTYPE", "TMPDIR", "TEMP", "TMP", "TERM", "NO_COLOR", "CODEX_HOME", "USERPROFILE", "APPDATA", "LOCALAPPDATA", "SYSTEMROOT", "WINDIR"}
    return {key: value for key, value in os.environ.items() if key in allowed}


def safe_upstream(raw: str) -> str:
    parsed = urllib.parse.urlsplit(raw)
    if parsed.scheme not in {"http", "https"} or not parsed.hostname or parsed.username or parsed.password or parsed.path not in {"", "/"} or parsed.query or parsed.fragment:
        raise ValueError("Device URL must be a plain local address such as http://alfred.local or http://192.168.1.20")
    host = parsed.hostname.lower()
    try:
        address = ipaddress.ip_address(host)
        if not (address.is_private or address.is_link_local):
            raise ValueError("Device URL must point to a private-network ALFRED device")
    except ValueError as exc:
        if "does not appear" not in str(exc):
            raise
        if not host.endswith(".local"):
            raise ValueError("Use a private IP address or an .local device name") from exc
    return urllib.parse.urlunsplit((parsed.scheme, parsed.netloc, "", "", ""))


def check_workspace(path: Path) -> Path:
    resolved = path.expanduser().resolve(strict=True)
    if not resolved.is_dir():
        raise ValueError("Selected workspace is not a folder")
    result = git(["rev-parse", "--show-toplevel"], cwd=resolved)
    if result.returncode:
        raise ValueError("Selected workspace must be inside a Git project")
    root = Path(result.stdout.decode().strip()).resolve()
    return root


def source_files(root: Path) -> list[Path]:
    result = git(["ls-files", "--cached", "--others", "--exclude-standard", "-z"], cwd=root)
    if result.returncode:
        raise RuntimeError("Could not list project files")
    paths: list[Path] = []
    total = 0
    for raw in result.stdout.split(b"\0"):
        if not raw:
            continue
        rel = Path(os.fsdecode(raw))
        if rel.is_absolute() or ".." in rel.parts or any(part in SKIP_DIRS for part in rel.parts):
            continue
        rel_text = rel.as_posix()
        if SECRET_NAME.search(rel_text):
            continue
        src = root / rel
        try:
            resolved = src.resolve(strict=True)
            resolved.relative_to(root)
            if not resolved.is_file() or src.is_symlink():
                continue
            size = resolved.stat().st_size
            if size > MAX_SOURCE_FILE_BYTES:
                continue
            contents = resolved.read_bytes()
            if b"\0" in contents or SECRET_CONTENT.search(contents.decode("utf-8", errors="replace")):
                continue
        except (OSError, ValueError):
            continue
        total += size
        if total > MAX_WORKSPACE_BYTES:
            raise ValueError("Project source snapshot is over 200 MB. Remove generated files or choose a smaller project.")
        paths.append(rel)
    if not paths:
        raise ValueError("Selected project has no source files to give Codex")
    return paths


def prepare_isolated_copy(folder: Path) -> Path:
    project = folder / "project"
    project.mkdir(parents=True)
    files = source_files(ROOT)
    for rel in files:
        src = ROOT / rel
        dst = project / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
    template = folder / "empty-git-template"
    template.mkdir()
    init = git(["init", "--quiet", f"--template={template}", str(project)], cwd=folder)
    if init.returncode:
        raise RuntimeError("Could not create the isolated project copy")
    hooks = folder / "empty-hooks"
    hooks.mkdir()
    add = git(["-c", f"core.hooksPath={hooks}", "add", "-A"], cwd=project)
    commit = git(["-c", f"core.hooksPath={hooks}", "-c", "user.name=ALFRED temporary workspace", "-c", "user.email=alfred-local@invalid", "-c", "commit.gpgsign=false", "commit", "--quiet", "-m", "Isolated ALFRED task baseline"], cwd=project)
    if add.returncode or commit.returncode:
        raise RuntimeError("Could not prepare the isolated project snapshot")
    return project


def cleanup_job(job: dict) -> None:
    folder = job.get("folder")
    if folder:
        shutil.rmtree(folder, ignore_errors=True)
        job["folder"] = None


def run_codex(job_id: str, prompt: str) -> None:
    with JOBS_LOCK:
        job = JOBS[job_id]
        job["status"] = "running"
    folder = Path(tempfile.mkdtemp(prefix="alfred-codex-"))
    with JOBS_LOCK:
        job["folder"] = str(folder)
    try:
        project = prepare_isolated_copy(folder)
        summary_file = folder / "codex-summary.txt"
        instructions = (
            "You are the coding mode used by ALFRED. Implement the user's request in this isolated project copy. "
            "Read its project instructions first. Do not access credentials, secrets, environment files, private keys, or personal data. "
            "Do not commit, push, publish, deploy, install global software, or change anything outside this isolated copy. "
            "Do not run tests unless the user asked to check or verify the change. If external access, a missing credential, or an unsafe action is needed, stop and explain. "
            "The user will inspect your patch and separately approve applying it to the selected project.\n\n"
            f"User request: {prompt}"
        )
        command = [CODEX, "exec", "--json", "--sandbox", "workspace-write", "--cd", str(project), "--output-last-message", str(summary_file), "-"]
        proc = subprocess.run(command, input=instructions.encode(), stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=TASK_TIMEOUT_SECONDS, env=run_env(), check=False)
        summary = summary_file.read_text(errors="replace").strip() if summary_file.exists() else ""
        if proc.returncode:
            err = proc.stderr.decode(errors="replace")[-4000:].strip()
            raise RuntimeError(err or summary or f"Codex exited with status {proc.returncode}")
        names = git(["diff", "--name-only", "-z", "HEAD"], cwd=project)
        if names.returncode:
            raise RuntimeError("Could not inspect the Codex patch")
        changed = [os.fsdecode(item) for item in names.stdout.split(b"\0") if item]
        if any(Path(item).is_absolute() or ".." in Path(item).parts or SECRET_NAME.search(item) or (project / item).is_symlink() for item in changed):
            raise RuntimeError("Codex changed a path that ALFRED blocks for safety")
        diff = git(["diff", "--binary", "--no-color", "--no-ext-diff", "HEAD"], cwd=project)
        if diff.returncode:
            raise RuntimeError("Could not prepare the review patch")
        if len(diff.stdout) > MAX_PATCH_BYTES:
            raise RuntimeError("Patch is larger than 80 KB. Ask Codex for a smaller, focused change.")
        with JOBS_LOCK:
            job.update(status="review", summary=summary or "Codex finished. Review the patch before applying it.", diff=diff.stdout.decode(errors="replace"))
    except subprocess.TimeoutExpired:
        with JOBS_LOCK:
            job.update(status="failed", error="Codex task exceeded the 10-minute limit.")
        cleanup_job(job)
    except Exception as exc:
        with JOBS_LOCK:
            job.update(status="failed", error=str(exc)[:4000])
        cleanup_job(job)


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


class Handler(http.server.BaseHTTPRequestHandler):
    server_version = "ALFRED-Desktop/0.1"

    def log_message(self, fmt: str, *args) -> None:
        # Avoid logging voice text, prompts, query strings, or local project paths.
        print(f"[ALFRED desktop] {self.command} {self.path.split('?', 1)[0]} — {args[1] if len(args) > 1 else ''}")

    def send_json(self, status: int, payload: dict) -> None:
        data = json.dumps(payload, ensure_ascii=False).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(data)

    def host_ok(self) -> bool:
        return self.headers.get("Host", "").lower() in {f"127.0.0.1:{PORT}", f"localhost:{PORT}"}

    def origin_ok(self) -> bool:
        return self.headers.get("Origin", "").lower() in {f"http://127.0.0.1:{PORT}", f"http://localhost:{PORT}"}

    def do_GET(self) -> None:
        if not self.host_ok():
            self.send_json(403, {"error": "Local companion accepts requests only from this computer."})
            return
        if self.path == "/__alfred/health":
            ready = bool(CODEX and CODEX_LOGIN_OK)
            self.send_json(200, {"ready": ready, "workspace": ROOT.name, "mode": "isolated Codex edits with patch review", "error": "" if ready else "Sign in to Codex CLI on this computer, then restart the companion."})
            return
        if self.path.startswith("/__alfred/tasks/"):
            job_id = self.path.rsplit("/", 1)[-1]
            if not re.fullmatch(r"[0-9a-f-]{36}", job_id):
                self.send_json(400, {"error": "Invalid task id"})
                return
            with JOBS_LOCK:
                job = JOBS.get(job_id)
                if not job:
                    self.send_json(404, {"error": "Task not found. Restart the task from ALFRED."})
                    return
                result = {key: job.get(key) for key in ("status", "summary", "diff", "error") if job.get(key) is not None}
            self.send_json(200, result)
            return
        self.proxy()

    def do_POST(self) -> None:
        if not self.host_ok() or not self.origin_ok():
            self.send_json(403, {"error": "Rejected request: use ALFRED from this computer's local companion page."})
            return
        size = int(self.headers.get("Content-Length", "0") or "0")
        if size < 0 or size > MAX_BODY:
            self.send_json(413, {"error": "Request is too large"})
            return
        raw = self.rfile.read(size)
        if self.path.startswith("/__alfred/"):
            try:
                body = json.loads(raw or b"{}")
            except json.JSONDecodeError:
                self.send_json(400, {"error": "Invalid request"})
                return
            if self.path == "/__alfred/tasks":
                if not CODEX_LOGIN_OK:
                    self.send_json(503, {"error": "Sign in to Codex CLI on this computer, then restart the companion."})
                    return
                task = body.get("task", "") if isinstance(body, dict) else ""
                if not isinstance(task, str) or not task.strip() or len(task) > 1200:
                    self.send_json(400, {"error": "Enter a coding request of up to 1,200 characters."})
                    return
                task_id = str(uuid.uuid4())
                with JOBS_LOCK:
                    JOBS[task_id] = {"status": "queued", "folder": None, "summary": "", "diff": "", "error": ""}
                threading.Thread(target=run_codex, args=(task_id, task.strip()), daemon=True).start()
                self.send_json(202, {"id": task_id, "status": "queued"})
                return
            match = re.fullmatch(r"/__alfred/tasks/([0-9a-f-]{36})/(apply|discard)", self.path)
            if match:
                self.finish_task(match.group(1), match.group(2))
                return
            self.send_json(404, {"error": "Desktop action not found"})
            return
        self.proxy(raw)

    def finish_task(self, task_id: str, action: str) -> None:
        with JOBS_LOCK:
            job = JOBS.get(task_id)
            if not job or job.get("status") != "review":
                self.send_json(409, {"error": "There is no reviewable patch for this task."})
                return
            if action == "discard":
                job["status"] = "discarded"
                cleanup_job(job)
                self.send_json(200, {"status": "discarded"})
                return
            if not job.get("diff"):
                self.send_json(409, {"error": "Codex made no changes to apply."})
                return
            patch = job["diff"].encode()
            check = git(["apply", "--check", "--whitespace=nowarn", "-"], cwd=ROOT, data=patch)
            if check.returncode:
                self.send_json(409, {"error": "The project changed since this patch was made. Discard it and ask Codex to retry against the latest files."})
                return
            applied = git(["apply", "--whitespace=nowarn", "-"], cwd=ROOT, data=patch)
            if applied.returncode:
                self.send_json(500, {"error": "Could not apply the patch: " + applied.stderr.decode(errors="replace")[-1200:]})
                return
            job["status"] = "applied"
            cleanup_job(job)
            self.send_json(200, {"status": "applied", "summary": job.get("summary", "")})

    def proxy(self, raw: bytes | None = None) -> None:
        path = self.path
        if not path.startswith("/") or "\\" in path:
            self.send_json(400, {"error": "Invalid path"})
            return
        url = UPSTREAM + path
        headers = {k: v for k, v in self.headers.items() if k.lower() not in HOP_HEADERS and k.lower() not in {"origin", "referer"}}
        req = urllib.request.Request(url, data=raw, headers=headers, method=self.command)
        # Never pass the private-device request (including Wi-Fi setup POSTs) through a configured web proxy.
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect)
        try:
            response = opener.open(req, timeout=10)
            status, response_headers, data = response.status, response.headers, response.read(12 * 1024 * 1024 + 1)
        except urllib.error.HTTPError as exc:
            status, response_headers, data = exc.code, exc.headers, exc.read(12 * 1024 * 1024 + 1)
        except Exception:
            self.send_json(502, {"error": "ALFRED device is offline or unreachable"})
            return
        if len(data) > 12 * 1024 * 1024:
            self.send_json(502, {"error": "Device response exceeded the local proxy limit"})
            return
        self.send_response(status)
        for key, value in response_headers.items():
            if key.lower() not in HOP_HEADERS:
                self.send_header(key, value)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Referrer-Policy", "no-referrer")
        self.send_header("X-Frame-Options", "DENY")
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(data)


def main() -> None:
    global ROOT, UPSTREAM, CODEX, CODEX_LOGIN_OK, PORT
    parser = argparse.ArgumentParser(description="Serve the ALFRED device dashboard on localhost and connect it to Codex safely.")
    parser.add_argument("--device-url", default="http://alfred.local", help="ALFRED private IP or .local address (default: http://alfred.local)")
    parser.add_argument("--workspace", required=True, help="Git project folder Codex may edit after patch review")
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    PORT = args.port
    if not 1024 <= PORT <= 65535:
        raise SystemExit("Port must be between 1024 and 65535")
    UPSTREAM = safe_upstream(args.device_url)
    ROOT = check_workspace(Path(args.workspace))
    CODEX = shutil.which("codex") or ""
    if not CODEX:
        raise SystemExit("Codex CLI was not found on PATH. Install/sign in to Codex CLI first.")
    login = subprocess.run([CODEX, "login", "status"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=10, env=run_env(), check=False)
    CODEX_LOGIN_OK = login.returncode == 0
    server = http.server.ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
    server.daemon_threads = True
    print(f"ALFRED desktop companion: http://127.0.0.1:{PORT}")
    print(f"Project: {ROOT.name} · Codex edits an isolated copy and waits for patch approval")
    print("Keep this window open while using ALFRED. Press Ctrl+C to stop.")
    try:
        server.serve_forever(poll_interval=0.5)
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        with JOBS_LOCK:
            for job in JOBS.values():
                cleanup_job(job)


if __name__ == "__main__":
    main()
