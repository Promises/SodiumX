#!/usr/bin/env python3
"""
LithiumX E2E Test Suite
Automated UI testing via the remote debug server.
Validates state via `status` command and takes screenshots for visual review.

Usage:
    python3 tools/test_e2e.py          # Run all tests
    python3 tools/test_e2e.py --quick  # Quick smoke test only
"""

import sys
import os
import time
import subprocess

sys.path.insert(0, os.path.dirname(__file__))
from remote import LithiumXRemote

SCREENSHOT_DIR = "build/screenshots/e2e"
FAILURES = []
PASSES = []


def screenshot(remote, name):
    """Take screenshot and convert to PNG for review."""
    os.makedirs(SCREENSHOT_DIR, exist_ok=True)
    bmp_path = f"{SCREENSHOT_DIR}/{name}.bmp"
    png_path = f"{SCREENSHOT_DIR}/{name}.png"
    try:
        remote.screenshot(bmp_path)
        subprocess.run(
            ["ffmpeg", "-y", "-i", bmp_path, png_path, "-loglevel", "quiet"],
            capture_output=True,
        )
        if os.path.exists(png_path):
            os.remove(bmp_path)
    except Exception as e:
        print(f"  Screenshot failed: {e}")


def check(name, condition, detail=""):
    """Assert a condition, track pass/fail."""
    if condition:
        PASSES.append(name)
        print(f"  ✓ {name}")
    else:
        FAILURES.append(f"{name}: {detail}")
        print(f"  ✗ {name} — {detail}")


def parse_status(remote):
    """Parse status response into a dict."""
    raw = remote.send_cmd("status")
    if not raw.startswith("OK "):
        return {}
    parts = raw[3:].strip()
    result = {}
    for token in parts.split():
        if "=" in token:
            k, v = token.split("=", 1)
            result[k] = v.strip('"')
    return result


# ── Test Cases ──


def test_initial_state(remote):
    """App should auto-switch to Games page if Recent is empty."""
    print("\n[TEST] Initial State")
    time.sleep(1)  # Wait for DB scan threads
    s = parse_status(remote)
    check("Auto-switch to non-empty page", s.get("page", "").endswith("/Games"),
          f"page={s.get('page')}")
    check("Tab synced", s.get("tab") in ("0", "1"),
          f"tab={s.get('tab')}")
    sel_total = s.get("sel", "0/0")
    total = int(sel_total.split("/")[1]) if "/" in sel_total else 0
    check("Library has items", total > 0, f"sel={sel_total}")
    check("Focused title not empty", len(s.get("title", "")) > 0,
          f"title={s.get('title')}")
    check("Menu closed", s.get("menu") == "closed", f"menu={s.get('menu')}")
    screenshot(remote, "01_initial")


def test_navigation(remote):
    """Arrow key navigation should update selection and title."""
    print("\n[TEST] Rail Navigation")

    # Reset to first tile
    for _ in range(15):
        remote.key("left", delay=0.05)
    time.sleep(0.3)

    s_before = parse_status(remote)
    sel_before = int(s_before.get("sel", "1/0").split("/")[0])

    remote.key("right", delay=0.4)
    s_after = parse_status(remote)
    sel_after = int(s_after.get("sel", "1/0").split("/")[0])

    check("Right increments selection", sel_after == sel_before + 1,
          f"{sel_before} → {sel_after}")
    check("Title changed", s_after.get("title") != s_before.get("title"),
          f"'{s_before.get('title')}' → '{s_after.get('title')}'")

    remote.key("right", delay=0.4)
    screenshot(remote, "02_nav_right")

    # Now go left and verify it decrements
    s_before_left = parse_status(remote)
    sel_before_left = int(s_before_left.get("sel", "0/0").split("/")[0])
    remote.key("left", delay=0.4)
    s_left = parse_status(remote)
    sel_left = int(s_left.get("sel", "0/0").split("/")[0])
    check("Left decrements selection", sel_left == sel_before_left - 1,
          f"{sel_before_left} → {sel_left}")
    screenshot(remote, "03_nav_left")


def test_fast_scroll(remote):
    """LT/RT should jump by 6 tiles."""
    print("\n[TEST] Fast Scroll")
    # Go to first tile
    for _ in range(10):
        remote.key("left", delay=0.1)
    time.sleep(0.3)

    s_start = parse_status(remote)
    sel_start = int(s_start.get("sel", "1/0").split("/")[0])

    remote.key("rt", delay=0.4)
    s_after = parse_status(remote)
    sel_after = int(s_after.get("sel", "1/0").split("/")[0])

    jump = sel_after - sel_start
    check("RT jumps forward", jump >= 3, f"jumped {jump} tiles")
    screenshot(remote, "04_fast_scroll")


def test_page_switch(remote):
    """PageDown/PageUp should switch between TOML pages."""
    print("\n[TEST] Page Switch")

    s_before = parse_status(remote)
    page_before = s_before.get("page", "")

    remote.key("pagedown", delay=0.5)
    s_after = parse_status(remote)
    page_after = s_after.get("page", "")

    check("Page changed", page_after != page_before,
          f"'{page_before}' → '{page_after}'")

    # Switch back
    remote.key("pagedown", delay=0.5)
    s_back = parse_status(remote)
    check("Page cycles back", s_back.get("page") == page_before,
          f"expected '{page_before}', got '{s_back.get('page')}'")
    screenshot(remote, "05_page_switch")


def test_start_menu(remote):
    """START should open/close the menu overlay."""
    print("\n[TEST] Start Menu")

    remote.key("start", delay=0.5)
    s_open = parse_status(remote)
    check("Menu opens", s_open.get("menu") == "open",
          f"menu={s_open.get('menu')}")
    screenshot(remote, "06_menu_open")

    # Navigate down in menu
    remote.key("down", delay=0.3)
    remote.key("down", delay=0.3)
    screenshot(remote, "07_menu_nav")

    # Close menu
    remote.key("esc", delay=0.5)
    s_closed = parse_status(remote)
    check("Menu closes on ESC", s_closed.get("menu") == "closed",
          f"menu={s_closed.get('menu')}")

    # Toggle: open then close via START
    remote.key("start", delay=0.5)
    s1 = parse_status(remote)
    check("Menu opens via START", s1.get("menu") == "open")
    remote.key("start", delay=0.5)
    s2 = parse_status(remote)
    check("Menu closes via START toggle", s2.get("menu") == "closed",
          f"menu={s2.get('menu')}")
    screenshot(remote, "08_menu_toggled")


def test_backdrop(remote):
    """Backdrop should update when navigating (visual check only)."""
    print("\n[TEST] Backdrop Blur")
    # Navigate to ensure art is loaded
    for _ in range(3):
        remote.key("right", delay=0.5)
    time.sleep(1)  # Wait for JPEG decode
    screenshot(remote, "09_backdrop")
    print("  ⓘ Visual check: verify blurred boxart behind tiles in screenshot")


def test_edge_cases(remote):
    """Edge case: navigate past start and end of library."""
    print("\n[TEST] Edge Cases")

    # Navigate to first tile
    for _ in range(15):
        remote.key("left", delay=0.05)
    time.sleep(0.3)
    s_first = parse_status(remote)
    sel_first = int(s_first.get("sel", "1/0").split("/")[0])
    check("Can't go below first tile", sel_first == 1, f"sel={sel_first}")

    # Navigate to last tile
    for _ in range(15):
        remote.key("right", delay=0.05)
    time.sleep(0.3)
    s_last = parse_status(remote)
    sel_parts = s_last.get("sel", "0/0").split("/")
    sel_last = int(sel_parts[0])
    total = int(sel_parts[1])
    check("Can't go past last tile", sel_last == total, f"sel={sel_last}/{total}")
    screenshot(remote, "10_edge_last")


# ── Runner ──


def main():
    quick = "--quick" in sys.argv

    remote = LithiumXRemote()
    try:
        remote.connect()
    except ConnectionRefusedError:
        print("Cannot connect — is the app running?")
        sys.exit(1)

    print("=" * 50)
    print("LithiumX E2E Test Suite")
    print("=" * 50)

    test_initial_state(remote)
    test_navigation(remote)

    if not quick:
        test_fast_scroll(remote)
        test_page_switch(remote)
        test_start_menu(remote)
        test_backdrop(remote)
        test_edge_cases(remote)

    remote.close()

    print("\n" + "=" * 50)
    print(f"Results: {len(PASSES)} passed, {len(FAILURES)} failed")
    if FAILURES:
        print("\nFailures:")
        for f in FAILURES:
            print(f"  ✗ {f}")
        sys.exit(1)
    else:
        print("All tests passed!")
        print(f"Screenshots in: {SCREENSHOT_DIR}/")
        sys.exit(0)


if __name__ == "__main__":
    main()
