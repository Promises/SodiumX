#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["mcp"]
# ///
"""
SodiumX MCP Server
Exposes the Xbox remote debug protocol as MCP tools for AI assistants.

Run with:  uv run tools/mcp_server.py
"""

import base64
import os
import socket
import subprocess
import tempfile
import time

from mcp.server.fastmcp import FastMCP

server = FastMCP(
    "SodiumX",
    instructions=(
        "Control and inspect a SodiumX Xbox dashboard via its TCP debug server. "
        "Use 'status' to check current state, 'screenshot' to see the screen, "
        "'key' to navigate, and 'reload' to redeploy after code changes."
    ),
)

XBOX_IP = os.environ.get("XBOX_IP", "192.168.3.211")
XBOX_PORT = int(os.environ.get("XBOX_PORT", "9876"))


class XboxConnection:
    """Manages a TCP connection to the SodiumX debug server."""

    def __init__(self):
        self.sock = None

    def connect(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(10)
        self.sock.connect((XBOX_IP, XBOX_PORT))
        greeting = self.sock.recv(256).decode("utf-8", errors="replace").strip()
        return greeting

    def close(self):
        if self.sock:
            self.sock.close()
            self.sock = None

    def send_cmd(self, cmd: str) -> str:
        self.sock.sendall((cmd + "\n").encode())
        time.sleep(0.05)
        resp = self.sock.recv(4096).decode("utf-8", errors="replace").strip()
        return resp

    def recv_screenshot(self) -> bytes:
        old_timeout = self.sock.gettimeout()
        self.sock.settimeout(15)
        self.sock.sendall(b"screenshot\n")

        header = b""
        while b"\n" not in header:
            chunk = self.sock.recv(1)
            if not chunk:
                raise IOError("Connection closed during screenshot header")
            header += chunk

        header_str = header.decode().strip()
        if not header_str.startswith("OK "):
            raise IOError(f"Screenshot failed: {header_str}")

        size = int(header_str.split()[1])
        data = b""
        while len(data) < size:
            chunk = self.sock.recv(min(65536, size - len(data)))
            if not chunk:
                break
            data += chunk

        self.sock.settimeout(old_timeout)
        return data


def _connect() -> XboxConnection:
    conn = XboxConnection()
    conn.connect()
    return conn


def _bmp_to_png(bmp_data: bytes, resolution: int = 480) -> bytes:
    """Convert BMP bytes to a downscaled PNG using ffmpeg."""
    with tempfile.NamedTemporaryFile(suffix=".bmp", delete=False) as bmp_f:
        bmp_f.write(bmp_data)
        bmp_path = bmp_f.name

    png_path = bmp_path.replace(".bmp", ".png")
    try:
        if resolution == 360:
            scale = "640:360"
        elif resolution == 480:
            scale = "854:480"
        else:
            scale = f"{resolution}:-1"

        subprocess.run(
            ["ffmpeg", "-y", "-i", bmp_path, "-vf", f"scale={scale}", png_path, "-loglevel", "quiet"],
            check=True,
            capture_output=True,
        )
        with open(png_path, "rb") as f:
            return f.read()
    finally:
        for p in (bmp_path, png_path):
            try:
                os.unlink(p)
            except OSError:
                pass


@server.tool()
def screenshot(resolution: int = 480) -> str:
    """
    Capture a screenshot from the Xbox screen and save as PNG.

    Returns the file path to the saved PNG. Use the Read tool to view it.

    Args:
        resolution: Target height in pixels (360, 480, or 720). Default 480.
    """
    conn = _connect()
    try:
        bmp_data = conn.recv_screenshot()
    finally:
        conn.close()

    png_data = _bmp_to_png(bmp_data, resolution)

    out_path = "/tmp/lx_xbox.png"
    with open(out_path, "wb") as f:
        f.write(png_data)

    return f"Screenshot saved to {out_path} ({len(png_data)} bytes)"


@server.tool()
def status() -> str:
    """
    Query a structured snapshot of the current UI state.

    Returns an INI-like text snapshot with sections for each active UI layer:
    - [rail]: Tab, page, selected tile, nearby tile names
    - [menu]: Main menu items with selection marker (only when menu is open)
    - [panel]: Settings/info panel sections with active marker (only when a panel is open)

    Each section lists items with [SELECTED] or [ACTIVE] markers to show focus.
    This is designed to reduce the need for screenshots when navigating.
    """
    conn = _connect()
    try:
        return conn.send_cmd("status")
    finally:
        conn.close()


@server.tool()
def key(name: str, count: int = 1, delay: float = 0.3) -> str:
    """
    Send a controller/keyboard input to the dashboard.

    Args:
        name: Key name — one of: a, b, x, y, start, back, up, down, left,
              right, white, black, lt, rt
        count: Number of times to press (default 1).
        delay: Seconds to wait between presses (default 0.3).
    """
    conn = _connect()
    try:
        result = ""
        for i in range(count):
            result = conn.send_cmd(f"key {name}")
            if i < count - 1:
                time.sleep(delay)
        return result
    finally:
        conn.close()


@server.tool()
def reload(debug: bool = False, rebuild_db: bool = False) -> str:
    """
    Deploy and reload the test XBE on Xbox.

    Launches F:\\Apps\\testing\\default.xbe on the Xbox.

    Args:
        debug: If True, new instance waits for a log client before proceeding.
        rebuild_db: If True, new instance deletes the database and rescans all games.
    """
    flags = []
    if debug:
        flags.append("--debug")
    if rebuild_db:
        flags.append("--rebuild-db")

    cmd = "reload" + (" " + " ".join(flags) if flags else "")

    conn = _connect()
    try:
        return conn.send_cmd(cmd)
    finally:
        conn.close()


@server.tool()
def launch(xbe_path: str) -> str:
    """
    Launch an arbitrary XBE file on the Xbox.

    Args:
        xbe_path: Full Xbox path to the XBE, e.g. "F:\\Games\\Halo\\default.xbe"
    """
    conn = _connect()
    try:
        return conn.send_cmd(f"launch {xbe_path}")
    finally:
        conn.close()


@server.tool()
def send_command(command: str) -> str:
    """
    Send a raw command to the debug server.

    For advanced usage when the specific tools don't cover your need.
    See the protocol: key, screenshot, status, log on/off, reload, launch, quit.

    Args:
        command: Raw command string to send (e.g. "log on", "quit").
    """
    conn = _connect()
    try:
        return conn.send_cmd(command)
    finally:
        conn.close()


@server.tool()
def build_and_deploy(skip_build: bool = False) -> str:
    """
    Run the rapid.sh build-deploy-reload cycle.

    Builds the Xbox binary via Docker, FTPs it to the Xbox, and reloads.

    Args:
        skip_build: If True, skip the Docker build and just redeploy the existing XBE.
    """
    project_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    cmd = [os.path.join(project_root, "rapid.sh")]
    if skip_build:
        cmd.append("--skip-build")

    result = subprocess.run(
        cmd,
        capture_output=True,
        text=True,
        timeout=120,
        cwd=project_root,
    )

    output = result.stdout
    if result.returncode != 0:
        output += f"\nSTDERR:\n{result.stderr}"
        return f"FAILED (exit {result.returncode}):\n{output}"

    return f"OK\n{output}"


if __name__ == "__main__":
    server.run()
