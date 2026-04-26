#!/usr/bin/env python3
"""Log daemon — continuously connects to Xbox, streams logs to a file.
Sends periodic pings to detect crashes. Reconnects automatically.

Usage: LX_HOST=192.168.3.211 python3 tools/log_daemon.py [logfile]

Read logs:  tail -f /tmp/lx_daemon.log
Send cmd:   echo "status" > /tmp/lx_daemon.cmd
"""
import socket, os, sys, time, threading, select

HOST = os.environ.get("LX_HOST", "192.168.3.211")
PORT = int(os.environ.get("LX_PORT", "9876"))
LOG_FILE = sys.argv[1] if len(sys.argv) > 1 else "/tmp/lx_daemon.log"
CMD_FILE = "/tmp/lx_daemon.cmd"
PING_INTERVAL = 3  # seconds between pings

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
            sock.settimeout(None)

            banner = sock.recv(256).decode(errors="replace").strip()
            log(f"Connected: {banner}")

            sock.sendall(b"log on\n")

            dead = threading.Event()
            last_pong = [time.time()]

            # Ping thread — sends ping every PING_INTERVAL, detects missing pongs
            def ping_thread():
                while not dead.is_set():
                    dead.wait(PING_INTERVAL)
                    if dead.is_set():
                        break
                    # Check if we got a pong since last ping
                    if time.time() - last_pong[0] > PING_INTERVAL * 3:
                        log("CRASH DETECTED — no pong received")
                        dead.set()
                        try: sock.shutdown(socket.SHUT_RDWR)
                        except: pass
                        break
                    try:
                        sock.sendall(b"ping\n")
                    except:
                        dead.set()
                        break

            # Command file watcher
            def cmd_watcher():
                while not dead.is_set():
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

            t_ping = threading.Thread(target=ping_thread, daemon=True)
            t_cmd = threading.Thread(target=cmd_watcher, daemon=True)
            t_ping.start()
            t_cmd.start()

            # Stream with select()
            while not dead.is_set():
                ready, _, _ = select.select([sock], [], [], 1.0)
                if ready:
                    try:
                        data = sock.recv(4096)
                        if not data:
                            log("Connection closed by remote")
                            break
                        text = data.decode(errors="replace")
                        for line_text in text.splitlines():
                            stripped = line_text.strip()
                            if stripped == "pong":
                                last_pong[0] = time.time()
                            elif stripped:
                                log(stripped)
                    except (ConnectionResetError, BrokenPipeError, OSError):
                        log("Connection reset")
                        break

            dead.set()
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

        log("Reconnecting in 2s...")
        time.sleep(2)

if __name__ == "__main__":
    run()
