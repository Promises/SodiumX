#!/usr/bin/env python3
"""Log daemon — continuously connects to Xbox, streams logs to a file.
Run in background. Reconnects automatically on disconnect.

Usage: LX_HOST=192.168.3.211 python3 tools/log_daemon.py [logfile]

Read logs:  cat /tmp/lx_daemon.log
Send cmd:   echo "status" > /tmp/lx_daemon.cmd
"""
import socket, os, sys, time, threading, select

HOST = os.environ.get("LX_HOST", "192.168.3.211")
PORT = int(os.environ.get("LX_PORT", "9876"))
LOG_FILE = sys.argv[1] if len(sys.argv) > 1 else "/tmp/lx_daemon.log"
CMD_FILE = "/tmp/lx_daemon.cmd"

def log(msg):
    ts = time.strftime("%H:%M:%S")
    line = f"[{ts}] {msg}"
    print(line, flush=True)
    with open(LOG_FILE, "a") as f:
        f.write(line + "\n")

def run():
    with open(LOG_FILE, "w") as f:
        f.write("")
    try: os.remove(CMD_FILE)
    except: pass

    while True:
        sock = None
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(3)
            sock.connect((HOST, PORT))
            sock.settimeout(None)  # blocking mode for select

            # Enable TCP keepalive
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)

            banner = sock.recv(256).decode(errors="replace").strip()
            log(f"Connected: {banner}")

            sock.sendall(b"log on\n")

            stop_cmd = threading.Event()
            def cmd_watcher():
                while not stop_cmd.is_set():
                    try:
                        if os.path.exists(CMD_FILE):
                            with open(CMD_FILE, "r") as f:
                                cmd = f.read().strip()
                            os.remove(CMD_FILE)
                            if cmd:
                                log(f">>> {cmd}")
                                try:
                                    sock.sendall((cmd + "\n").encode())
                                except:
                                    break
                    except:
                        pass
                    time.sleep(0.2)

            t = threading.Thread(target=cmd_watcher, daemon=True)
            t.start()

            # Stream with select() for clean disconnect detection
            while True:
                ready, _, _ = select.select([sock], [], [], 2.0)
                if ready:
                    try:
                        data = sock.recv(4096)
                        if not data:
                            log("Connection closed by remote")
                            break
                        text = data.decode(errors="replace")
                        for line in text.splitlines():
                            if line.strip():
                                log(line.strip())
                    except (ConnectionResetError, BrokenPipeError, OSError):
                        log("Connection reset")
                        break

            stop_cmd.set()
            try: sock.close()
            except: pass

        except (ConnectionRefusedError, TimeoutError, OSError):
            pass
        except Exception as e:
            log(f"Error: {e}")
        finally:
            if sock:
                try: sock.close()
                except: pass

        time.sleep(1)

if __name__ == "__main__":
    run()
