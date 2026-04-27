#!/usr/bin/env python3
"""
LithiumX Save Game Backup Server

Receives save game backups from Xbox via custom TCP protocol.
Manages historical snapshots using hardlink-based deduplication.

Usage:
    python3 tools/backup_server.py [--port 9877] [--backup-dir ./backups]

Snapshot layout:
    backups/
    ├── latest -> 2026-04-27-143022/     (symlink to newest complete snapshot)
    ├── 2026-04-27-143022/               (complete snapshot)
    │   ├── UDATA/4541000a/save1.dat     (real file or hardlink)
    │   └── TDATA/...
    ├── temp/                            (in-progress, promoted on DONE)
    └── sessions/
        └── <session_id>.json            (tracks resume state)
"""

import argparse
import hashlib
import json
import logging
import os
import socket
import struct
import sys
import threading
import time
from datetime import datetime
from pathlib import Path

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    datefmt="%H:%M:%S",
)
log = logging.getLogger("backup")


def normalize_path(p: str) -> str:
    """Convert Windows backslash paths to POSIX forward slash."""
    return p.replace("\\", "/")


class BackupSession:
    """Tracks state for one Xbox client connection."""

    def __init__(self, conn: socket.socket, addr, backup_dir: Path):
        self.conn = conn
        self.addr = addr
        self.backup_dir = backup_dir
        self.temp_dir = backup_dir / "temp"
        self.sessions_dir = backup_dir / "sessions"
        self.session_id = None
        self.need_list: list[str] = []
        self.partial_path: str = ""
        self.partial_offset: int = 0
        self.files_received: int = 0
        self.manifest_mtimes: dict[str, int] = {}  # path -> mtime from Xbox

    def handle(self):
        """Main handler for a client connection."""
        try:
            self._handle_connection()
        except (ConnectionResetError, BrokenPipeError, OSError) as e:
            log.warning("Client %s disconnected: %s", self.addr, e)
        finally:
            self.conn.close()
            log.info("Client %s closed", self.addr)

    def _handle_connection(self):
        log.info("Client connected: %s", self.addr)

        # Read first line to determine mode
        line = self._recv_line()
        if not line:
            return

        if line.startswith("RESUME "):
            self._handle_resume(line)
            self._receive_files()
        elif line.startswith("MANIFEST "):
            self._handle_manifest(line)
            self._receive_files()
        elif line.startswith("LIST "):
            self._handle_list(line)
        elif line.startswith("RESTORE "):
            self._handle_restore(line)
        else:
            log.warning("Unknown command: %s", line)

    def _handle_resume(self, line: str):
        """Handle RESUME <partial_path> <offset>"""
        parts = line.split(" ", 2)
        if len(parts) < 3:
            self._send_line("NEW")
            return

        partial_path = parts[1]
        partial_offset = int(parts[2])

        # Generate session ID from client address
        self.session_id = self._make_session_id()
        session_file = self.sessions_dir / f"{self.session_id}.json"

        if not session_file.exists():
            log.info("No session found for %s, requesting fresh manifest", self.addr)
            self._send_line("NEW")
            # Wait for MANIFEST command
            line = self._recv_line()
            if line and line.startswith("MANIFEST "):
                self._handle_manifest(line)
            return

        # Load session
        with open(session_file) as f:
            session = json.load(f)

        self.need_list = session.get("need_list", [])
        self.partial_path = partial_path

        # Check how much of the partial file we already have in temp
        temp_file = self.temp_dir / partial_path
        if temp_file.exists():
            actual_size = temp_file.stat().st_size
            self.partial_offset = actual_size
            log.info("Resuming %s at offset %d (client had %d)",
                     partial_path, actual_size, partial_offset)
        else:
            self.partial_offset = 0

        self._send_line(f"GO {self.partial_offset}")

    def _handle_manifest(self, line: str):
        """Handle MANIFEST <count> followed by file entries."""
        count = int(line.split(" ", 1)[1])
        log.info("Receiving manifest: %d files", count)

        manifest = []
        for _ in range(count):
            entry_line = self._recv_line()
            if not entry_line:
                break
            parts = entry_line.split("\t")
            if len(parts) >= 3:
                norm_path = normalize_path(parts[0])
                manifest.append({
                    "path": norm_path,
                    "size": int(parts[1]),
                    "mtime": int(parts[2]),
                })
                self.manifest_mtimes[norm_path] = int(parts[2])

        # Diff against latest snapshot
        need = self._compute_need(manifest)

        # Sort: newest mtime first
        need.sort(key=lambda e: e["mtime"], reverse=True)

        self.need_list = [e["path"] for e in need]
        log.info("Need %d files (of %d total)", len(self.need_list), len(manifest))

        # Send NEED list
        self._send_line(f"NEED {len(self.need_list)}")
        for path in self.need_list:
            self._send_line(path)

        # Save session for potential resume
        self.session_id = self._make_session_id()
        self._save_session()

        # Prepare temp dir
        self.temp_dir.mkdir(parents=True, exist_ok=True)

    def _compute_need(self, manifest: list[dict]) -> list[dict]:
        """Compare manifest against latest snapshot, return changed files."""
        latest = self._get_latest_snapshot()
        if not latest:
            return manifest  # First backup — need everything

        need = []
        for entry in manifest:
            snap_file = latest / entry["path"]
            if not snap_file.exists():
                need.append(entry)
                continue

            stat = snap_file.stat()
            if stat.st_size != entry["size"]:
                need.append(entry)
                continue

            # Compare mtime (rough — Xbox timestamps may drift)
            if abs(stat.st_mtime - entry["mtime"]) > 2:
                need.append(entry)

        return need

    def _receive_files(self):
        """Receive FILE commands until DONE or ABORT."""
        while True:
            line = self._recv_line()
            if not line:
                break

            if line == "DONE":
                self._promote_snapshot()
                break
            elif line == "ABORT":
                log.info("Client aborted after %d files", self.files_received)
                self._save_session()
                break
            elif line.startswith("FILE "):
                self._receive_file(line)
            else:
                log.warning("Unexpected command during transfer: %s", line)

    def _receive_file(self, header: str):
        """Handle FILE <path>\t<size>\t<offset>"""
        parts = header[5:].split("\t")
        if len(parts) < 3:
            log.warning("Malformed FILE header: %s", header)
            return

        rel_path = normalize_path(parts[0])
        size = int(parts[1])
        offset = int(parts[2])

        dest = self.temp_dir / rel_path
        dest.parent.mkdir(parents=True, exist_ok=True)

        mode = "r+b" if offset > 0 and dest.exists() else "wb"
        with open(dest, mode) as f:
            if offset > 0:
                f.seek(offset)
            received = 0
            while received < size:
                chunk_size = min(4096, size - received)
                data = self._recv_exactly(chunk_size)
                if not data:
                    log.warning("Connection lost during file %s at %d/%d",
                                rel_path, received, size)
                    # Update session with partial progress
                    self.partial_path = rel_path
                    self.partial_offset = offset + received
                    self._save_session()
                    return
                f.write(data)
                received += len(data)

        # Preserve Xbox mtime so future diffs work correctly
        xbox_mtime = self.manifest_mtimes.get(rel_path, 0)
        if xbox_mtime > 0:
            os.utime(dest, (xbox_mtime, xbox_mtime))

        self.files_received += 1

        # Remove from need list
        if rel_path in self.need_list:
            self.need_list.remove(rel_path)

        log.info("  [%d] %s (%d bytes, offset %d)",
                 self.files_received, rel_path, size, offset)

    def _find_title_dir(self, snap: Path, title_id: str) -> Path | None:
        """Find the title directory, case-insensitive."""
        tid_lower = title_id.lower()
        for root_name in ("UDATA", "TDATA"):
            root = snap / root_name
            if not root.is_dir():
                continue
            for d in root.iterdir():
                if d.is_dir() and d.name.lower() == tid_lower:
                    return d
        return None

    def _handle_list(self, line: str):
        """Handle LIST <title_id> — return snapshots containing saves for this game."""
        title_id = line.split(" ", 1)[1].strip()
        log.info("LIST request for title_id=%s", title_id)

        results = []
        for entry in sorted(self.backup_dir.iterdir(), reverse=True):
            if not entry.is_dir() or entry.name in ("temp", "sessions", "latest"):
                continue
            # Check if this snapshot has files for the title (case-insensitive)
            game_dir = self._find_title_dir(entry, title_id)
            if game_dir:
                file_count = sum(1 for f in game_dir.rglob("*") if f.is_file())
                total_size = sum(f.stat().st_size for f in game_dir.rglob("*") if f.is_file())
                results.append({
                    "snapshot": entry.name,
                    "files": file_count,
                    "size": total_size,
                })

        self._send_line(f"SNAPSHOTS {len(results)}")
        for r in results:
            self._send_line(f"{r['snapshot']}\t{r['files']}\t{r['size']}")
        log.info("Listed %d snapshots for %s", len(results), title_id)

    def _handle_restore(self, line: str):
        """Handle RESTORE <snapshot> <title_id> — send all files for a game back to client."""
        parts = line.split(" ", 2)
        if len(parts) < 3:
            self._send_line("ERR missing args")
            return

        snap_name = parts[1]
        title_id = parts[2].strip()
        log.info("RESTORE request: snapshot=%s title_id=%s", snap_name, title_id)

        # Resolve snapshot
        if snap_name == "latest":
            latest_link = self.backup_dir / "latest"
            if latest_link.is_symlink():
                snap = self.backup_dir / os.readlink(latest_link)
            else:
                self._send_line("ERR no latest snapshot")
                return
        else:
            snap = self.backup_dir / snap_name

        if not snap.is_dir():
            self._send_line("ERR snapshot not found")
            return

        # Collect files for this title (case-insensitive lookup)
        files = []
        tid_lower = title_id.lower()
        for root_name in ("UDATA", "TDATA"):
            root = snap / root_name
            if not root.is_dir():
                continue
            for d in root.iterdir():
                if d.is_dir() and d.name.lower() == tid_lower:
                    for f in d.rglob("*"):
                        if f.is_file():
                            rel = f.relative_to(snap)
                            files.append((f, str(rel)))

        self._send_line(f"RESTORE_BEGIN {len(files)}")

        for abs_path, rel_path in files:
            data = abs_path.read_bytes()
            self._send_line(f"RFILE {rel_path}\t{len(data)}")
            try:
                self.conn.sendall(data)
            except (BrokenPipeError, OSError):
                log.warning("Connection lost during restore of %s", rel_path)
                return

        self._send_line("RESTORE_DONE")
        log.info("Restored %d files for %s from %s", len(files), title_id, snap_name)

    def _promote_snapshot(self):
        """Promote temp/ to a timestamped snapshot with hardlinks from previous."""
        timestamp = datetime.now().strftime("%Y-%m-%d-%H%M%S")
        snap_dir = self.backup_dir / timestamp
        latest = self._get_latest_snapshot()

        if not self.temp_dir.exists() or not any(self.temp_dir.rglob("*")):
            # Nothing received — maybe everything was up to date
            log.info("No files to snapshot")
            snapshot_name = "up-to-date"
        else:
            snap_dir.mkdir(parents=True, exist_ok=True)

            # First: hardlink everything from latest snapshot (unchanged files)
            if latest:
                for src_file in latest.rglob("*"):
                    if src_file.is_file():
                        rel = src_file.relative_to(latest)
                        dst = snap_dir / rel
                        # Don't overwrite if temp has a newer version
                        if not (self.temp_dir / rel).exists():
                            dst.parent.mkdir(parents=True, exist_ok=True)
                            try:
                                os.link(src_file, dst)
                            except OSError:
                                # Cross-device — fall back to copy
                                import shutil
                                shutil.copy2(src_file, dst)

            # Then: move new/changed files from temp into snapshot
            for tmp_file in self.temp_dir.rglob("*"):
                if tmp_file.is_file():
                    rel = tmp_file.relative_to(self.temp_dir)
                    dst = snap_dir / rel
                    dst.parent.mkdir(parents=True, exist_ok=True)
                    tmp_file.rename(dst)

            # Update latest symlink
            latest_link = self.backup_dir / "latest"
            if latest_link.is_symlink() or latest_link.exists():
                latest_link.unlink()
            latest_link.symlink_to(snap_dir.name)

            snapshot_name = timestamp
            log.info("Snapshot created: %s (%d files received)", timestamp, self.files_received)

        # Clean up
        self._cleanup_temp()
        self._cleanup_session()

        self._send_line(f"OK {snapshot_name}")

    def _get_latest_snapshot(self) -> Path | None:
        latest_link = self.backup_dir / "latest"
        if latest_link.is_symlink():
            target = self.backup_dir / os.readlink(latest_link)
            if target.is_dir():
                return target
        return None

    def _make_session_id(self) -> str:
        return hashlib.md5(f"{self.addr[0]}".encode()).hexdigest()[:8]

    def _save_session(self):
        self.sessions_dir.mkdir(parents=True, exist_ok=True)
        session_file = self.sessions_dir / f"{self.session_id}.json"
        with open(session_file, "w") as f:
            json.dump({
                "need_list": self.need_list,
                "partial_path": self.partial_path,
                "partial_offset": self.partial_offset,
                "timestamp": time.time(),
            }, f)

    def _cleanup_temp(self):
        """Remove temp directory tree."""
        if self.temp_dir.exists():
            import shutil
            shutil.rmtree(self.temp_dir, ignore_errors=True)

    def _cleanup_session(self):
        if self.session_id:
            session_file = self.sessions_dir / f"{self.session_id}.json"
            if session_file.exists():
                session_file.unlink()

    # ── Socket I/O ──

    def _recv_line(self) -> str | None:
        """Read until \\n, return without newline. None on disconnect."""
        buf = bytearray()
        while True:
            try:
                b = self.conn.recv(1)
            except (ConnectionResetError, OSError):
                return None
            if not b:
                return None
            if b == b"\n":
                break
            buf.extend(b)
        return buf.decode("utf-8", errors="replace").rstrip("\r")

    def _recv_exactly(self, n: int) -> bytes | None:
        """Read exactly n bytes. None on disconnect."""
        buf = bytearray()
        while len(buf) < n:
            try:
                chunk = self.conn.recv(n - len(buf))
            except (ConnectionResetError, OSError):
                return None
            if not chunk:
                return None
            buf.extend(chunk)
        return bytes(buf)

    def _send_line(self, msg: str):
        try:
            self.conn.sendall((msg + "\n").encode("utf-8"))
        except (BrokenPipeError, OSError):
            pass


## ═══════════════════════════════════════════════════════════════════
##  REST API — browse snapshots, list games, download/restore saves
## ═══════════════════════════════════════════════════════════════════

from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import unquote
import io
import tarfile
import re


class BackupAPIHandler(BaseHTTPRequestHandler):
    """HTTP handler for the backup REST API."""

    backup_dir: Path  # set by serve()

    def log_message(self, fmt, *args):
        log.info("[API] " + fmt % args)

    def do_GET(self):
        path = unquote(self.path)

        # GET /api/snapshots
        if path == "/api/snapshots":
            return self._list_snapshots()

        # GET /api/snapshots/<name>/games
        m = re.match(r"^/api/snapshots/([^/]+)/games$", path)
        if m:
            return self._list_games(m.group(1))

        # GET /api/snapshots/<name>/games/<title_id>/archive
        m = re.match(r"^/api/snapshots/([^/]+)/games/([^/]+)/archive$", path)
        if m:
            return self._download_game_archive(m.group(1), m.group(2))

        # GET /api/snapshots/<name>/files
        m = re.match(r"^/api/snapshots/([^/]+)/files$", path)
        if m:
            return self._list_files(m.group(1))

        # GET /api/snapshots/<name>/files/<path>
        m = re.match(r"^/api/snapshots/([^/]+)/files/(.+)$", path)
        if m:
            return self._download_file(m.group(1), m.group(2))

        # GET /api/diff/<snap_a>/<snap_b>
        m = re.match(r"^/api/diff/([^/]+)/([^/]+)$", path)
        if m:
            return self._diff_snapshots(m.group(1), m.group(2))

        self._json_response(404, {"error": "not found"})

    def _resolve_snapshot(self, name: str) -> Path | None:
        if name == "latest":
            link = self.backup_dir / "latest"
            if link.is_symlink():
                target = self.backup_dir / os.readlink(link)
                if target.is_dir():
                    return target
            return None
        snap = self.backup_dir / name
        if snap.is_dir() and name not in ("temp", "sessions"):
            return snap
        return None

    def _list_snapshots(self):
        snapshots = []
        for entry in sorted(self.backup_dir.iterdir()):
            if entry.is_dir() and entry.name not in ("temp", "sessions"):
                file_count = sum(1 for _ in entry.rglob("*") if _.is_file())
                total_size = sum(f.stat().st_size for f in entry.rglob("*") if f.is_file())
                snapshots.append({
                    "name": entry.name,
                    "file_count": file_count,
                    "total_size": total_size,
                })
        # Mark which is latest
        latest_link = self.backup_dir / "latest"
        if latest_link.is_symlink():
            latest_name = os.readlink(latest_link)
            for s in snapshots:
                s["is_latest"] = (s["name"] == latest_name)
        self._json_response(200, snapshots)

    def _list_games(self, snap_name: str):
        snap = self._resolve_snapshot(snap_name)
        if not snap:
            return self._json_response(404, {"error": f"snapshot '{snap_name}' not found"})

        games: dict[str, dict] = {}
        for f in snap.rglob("*"):
            if not f.is_file():
                continue
            rel = f.relative_to(snap)
            parts = rel.parts  # e.g. ("UDATA", "4541005b", "57BD267AFF59", "Profile 2")
            if len(parts) < 2:
                continue
            root = parts[0]       # UDATA or TDATA
            title_id = parts[1]   # Xbox title ID
            key = f"{root}/{title_id}"

            if key not in games:
                games[key] = {
                    "title_id": title_id,
                    "root": root,
                    "path": key,
                    "files": [],
                    "total_size": 0,
                }
            stat = f.stat()
            games[key]["files"].append({
                "path": str(rel),
                "size": stat.st_size,
            })
            games[key]["total_size"] += stat.st_size

        result = sorted(games.values(), key=lambda g: g["title_id"])
        self._json_response(200, result)

    def _list_files(self, snap_name: str):
        snap = self._resolve_snapshot(snap_name)
        if not snap:
            return self._json_response(404, {"error": f"snapshot '{snap_name}' not found"})

        files = []
        for f in sorted(snap.rglob("*")):
            if f.is_file():
                stat = f.stat()
                files.append({
                    "path": str(f.relative_to(snap)),
                    "size": stat.st_size,
                    "mtime": int(stat.st_mtime),
                })
        self._json_response(200, files)

    def _download_file(self, snap_name: str, file_path: str):
        snap = self._resolve_snapshot(snap_name)
        if not snap:
            return self._json_response(404, {"error": f"snapshot '{snap_name}' not found"})

        target = snap / file_path
        if not target.is_file() or not target.resolve().is_relative_to(snap.resolve()):
            return self._json_response(404, {"error": "file not found"})

        data = target.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Content-Disposition", f'attachment; filename="{target.name}"')
        self.end_headers()
        self.wfile.write(data)

    def _download_game_archive(self, snap_name: str, title_id: str):
        snap = self._resolve_snapshot(snap_name)
        if not snap:
            return self._json_response(404, {"error": f"snapshot '{snap_name}' not found"})

        # Collect files from both UDATA and TDATA for this title
        files_to_archive = []
        for root_name in ("UDATA", "TDATA"):
            game_dir = snap / root_name / title_id
            if game_dir.is_dir():
                for f in game_dir.rglob("*"):
                    if f.is_file():
                        files_to_archive.append((f, f.relative_to(snap)))

        if not files_to_archive:
            return self._json_response(404, {"error": f"no saves for title '{title_id}'"})

        buf = io.BytesIO()
        with tarfile.open(fileobj=buf, mode="w") as tar:
            for abs_path, rel_path in files_to_archive:
                tar.add(abs_path, arcname=str(rel_path))
        tar_data = buf.getvalue()

        self.send_response(200)
        self.send_header("Content-Type", "application/x-tar")
        self.send_header("Content-Length", str(len(tar_data)))
        self.send_header("Content-Disposition", f'attachment; filename="{title_id}.tar"')
        self.end_headers()
        self.wfile.write(tar_data)

    def _diff_snapshots(self, name_a: str, name_b: str):
        snap_a = self._resolve_snapshot(name_a)
        snap_b = self._resolve_snapshot(name_b)
        if not snap_a:
            return self._json_response(404, {"error": f"snapshot '{name_a}' not found"})
        if not snap_b:
            return self._json_response(404, {"error": f"snapshot '{name_b}' not found"})

        files_a = {str(f.relative_to(snap_a)): f.stat() for f in snap_a.rglob("*") if f.is_file()}
        files_b = {str(f.relative_to(snap_b)): f.stat() for f in snap_b.rglob("*") if f.is_file()}

        added = [{"path": p, "size": files_b[p].st_size} for p in files_b if p not in files_a]
        removed = [{"path": p, "size": files_a[p].st_size} for p in files_a if p not in files_b]
        modified = []
        for p in files_a:
            if p in files_b:
                sa, sb = files_a[p], files_b[p]
                if sa.st_size != sb.st_size or abs(sa.st_mtime - sb.st_mtime) > 2:
                    modified.append({
                        "path": p,
                        "old_size": sa.st_size,
                        "new_size": sb.st_size,
                    })

        self._json_response(200, {
            "from": name_a,
            "to": name_b,
            "added": added,
            "removed": removed,
            "modified": modified,
        })

    def _json_response(self, code: int, data):
        body = json.dumps(data, indent=2).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)


## ═══════════════════════════════════════════════════════════════════
##  Main server — TCP backup protocol + HTTP REST API
## ═══════════════════════════════════════════════════════════════════

def serve(port: int, backup_dir: Path, api_port: int = 9878):
    backup_dir.mkdir(parents=True, exist_ok=True)

    # Start HTTP API server in a background thread
    BackupAPIHandler.backup_dir = backup_dir
    http = HTTPServer(("0.0.0.0", api_port), BackupAPIHandler)
    api_thread = threading.Thread(target=http.serve_forever, daemon=True)
    api_thread.start()
    log.info("REST API listening on port %d", api_port)

    # TCP backup protocol server
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("0.0.0.0", port))
    sock.listen(2)

    log.info("Backup server listening on port %d", port)
    log.info("Backup directory: %s", backup_dir.resolve())

    while True:
        conn, addr = sock.accept()
        session = BackupSession(conn, addr, backup_dir)
        t = threading.Thread(target=session.handle, daemon=True)
        t.start()


def main():
    parser = argparse.ArgumentParser(description="LithiumX Save Game Backup Server")
    parser.add_argument("--port", type=int, default=9877, help="Backup protocol port (default: 9877)")
    parser.add_argument("--api-port", type=int, default=9878, help="REST API port (default: 9878)")
    parser.add_argument("--backup-dir", type=str, default="./backups",
                        help="Directory for storing snapshots (default: ./backups)")
    args = parser.parse_args()

    serve(args.port, Path(args.backup_dir), args.api_port)


if __name__ == "__main__":
    main()
