#!/usr/bin/env python3
"""
SodiumX Remote Debug Client
Connects to the app's TCP debug server for input injection, screenshots, and log streaming.

Usage:
    python3 tools/remote.py                    # Interactive mode
    python3 tools/remote.py screenshot out.bmp # Single screenshot
    python3 tools/remote.py key right          # Send a keypress
    python3 tools/remote.py logs               # Stream logs until Ctrl+C
    python3 tools/remote.py test               # Run automated test sequence
"""

import socket
import sys
import os
import time

HOST = os.environ.get("LX_HOST", "localhost")
PORT = int(os.environ.get("LX_PORT", "9876"))

class SodiumXRemote:
    def __init__(self, host=HOST, port=PORT):
        self.host = host
        self.port = port
        self.sock = None

    def connect(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(10)
        self.sock.connect((self.host, self.port))
        # Read greeting
        greeting = self.sock.recv(256).decode("utf-8", errors="replace").strip()
        print(f"Connected: {greeting}")
        return self

    def close(self):
        if self.sock:
            self.sock.close()
            self.sock = None

    def send_cmd(self, cmd):
        """Send a command and return the first response line."""
        self.sock.sendall((cmd + "\n").encode())
        time.sleep(0.05)
        resp = self.sock.recv(4096).decode("utf-8", errors="replace").strip()
        return resp

    def key(self, name, delay=0.3):
        """Send a keypress and wait."""
        resp = self.send_cmd(f"key {name}")
        time.sleep(delay)
        return resp

    def status(self):
        return self.send_cmd("status")

    def screenshot(self, path="build/screenshots/remote.bmp"):
        """Request screenshot, receive BMP binary, save to path."""
        os.makedirs(os.path.dirname(path) or ".", exist_ok=True)

        # Use a longer timeout for screenshot transfer
        old_timeout = self.sock.gettimeout()
        self.sock.settimeout(15)

        self.sock.sendall(b"screenshot\n")

        # Read header line "OK <size>\n"
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

        # Read exactly `size` bytes of BMP data
        data = b""
        while len(data) < size:
            chunk = self.sock.recv(min(65536, size - len(data)))
            if not chunk:
                break
            data += chunk

        with open(path, "wb") as f:
            f.write(data)

        self.sock.settimeout(old_timeout)
        print(f"Screenshot saved: {path} ({len(data)} bytes)")
        return path

    def stream_logs(self):
        """Enable log streaming and print to stdout until interrupted."""
        self.send_cmd("log on")
        print("--- Streaming logs (Ctrl+C to stop) ---")
        self.sock.settimeout(0.5)
        try:
            while True:
                try:
                    data = self.sock.recv(4096)
                    if data:
                        print(data.decode("utf-8", errors="replace"), end="", flush=True)
                except socket.timeout:
                    pass
        except KeyboardInterrupt:
            print("\n--- Log streaming stopped ---")
            self.send_cmd("log off")

    def quit_app(self):
        return self.send_cmd("quit")


def run_test_sequence(remote):
    """Automated E2E test: navigate through screens and take screenshots."""
    print("\n=== Running E2E Test Sequence ===\n")

    # 1. Initial state
    print("1. Initial state (Recent page)")
    remote.screenshot("build/screenshots/test_01_initial.bmp")

    # 2. Switch to Games page
    print("2. Switch to Games page (PageDown)")
    remote.key("pagedown")
    remote.screenshot("build/screenshots/test_02_games.bmp")

    # 3. Navigate through games
    print("3. Navigate right x3")
    for i in range(3):
        remote.key("right")
    remote.screenshot("build/screenshots/test_03_nav_right.bmp")

    # 4. Navigate left
    print("4. Navigate left")
    remote.key("left")
    remote.screenshot("build/screenshots/test_04_nav_left.bmp")

    # 5. Open Start menu
    print("5. Open Start menu")
    remote.key("start")
    remote.screenshot("build/screenshots/test_05_start_menu.bmp")

    # 6. Navigate menu
    print("6. Navigate menu down x2")
    remote.key("down")
    remote.key("down")
    remote.screenshot("build/screenshots/test_06_menu_nav.bmp")

    # 7. Close menu
    print("7. Close menu (esc)")
    remote.key("esc")
    remote.screenshot("build/screenshots/test_07_menu_closed.bmp")

    # 8. Status check
    print("8. Status:", remote.status())

    print("\n=== Test Complete ===")
    print(f"Screenshots in: build/screenshots/")


def interactive(remote):
    """Interactive REPL."""
    print("Interactive mode. Commands: key <name>, screenshot [path], status, log on/off, quit, exit")
    while True:
        try:
            cmd = input("lx> ").strip()
        except (EOFError, KeyboardInterrupt):
            break

        if not cmd:
            continue
        if cmd == "exit":
            break
        if cmd.startswith("screenshot"):
            parts = cmd.split(maxsplit=1)
            path = parts[1] if len(parts) > 1 else "build/screenshots/interactive.bmp"
            try:
                remote.screenshot(path)
            except Exception as e:
                print(f"Error: {e}")
        elif cmd == "logs":
            remote.stream_logs()
        else:
            print(remote.send_cmd(cmd))


def main():
    remote = SodiumXRemote()
    try:
        remote.connect()
    except ConnectionRefusedError:
        print(f"Cannot connect to {HOST}:{PORT} — is the app running?")
        sys.exit(1)

    try:
        if len(sys.argv) > 1:
            action = sys.argv[1]
            if action == "screenshot":
                path = sys.argv[2] if len(sys.argv) > 2 else "build/screenshots/remote.bmp"
                remote.screenshot(path)
            elif action == "key":
                if len(sys.argv) > 2:
                    print(remote.key(sys.argv[2]))
                else:
                    print("Usage: remote.py key <name>")
            elif action == "status":
                print(remote.status())
            elif action == "logs":
                remote.stream_logs()
            elif action == "test":
                run_test_sequence(remote)
            elif action == "quit":
                remote.quit_app()
            else:
                print(f"Unknown action: {action}")
        else:
            interactive(remote)
    finally:
        remote.close()


if __name__ == "__main__":
    main()
