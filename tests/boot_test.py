#!/usr/bin/env python3
"""NOS-DOS: Tests
boot_test.py - QEMU headless boot test.

Boots the NOS-DOS ISO in qemu-system-i386 with serial output redirected
to stdout and asserts that a DOS prompt (C:\> or A:\>) appears within
the configured timeout.

AUTOEXEC.BAT redirects DETECT.EXE stdout to COM1, so the boot test also
parses the detection results from the serial stream and reports:
  - Conventional memory detected (must be >= CONV_MEM_MIN_KB)
  - XMS memory detected (warning only if absent)

Uses QEMU's -serial stdio to capture all serial output.

Exit codes:
  0 - PASSED: sentinel detected, memory checks passed
  1 - FAILED: timeout, boot error, or memory below minimum
  2 - ERROR: QEMU not found or ISO missing

Usage:
  python tests/boot_test.py [--iso PATH] [--timeout SECONDS] [--verbose]
"""

import argparse
import configparser
import os
import re
import select
import subprocess
import sys
import time
from pathlib import Path

TESTS_DIR = Path(__file__).parent.resolve()
ROOT_DIR = TESTS_DIR.parent
BUILD_DIR = ROOT_DIR / "build"

config = configparser.ConfigParser()
config.read(BUILD_DIR / "config.ini")

DEFAULT_ISO = ROOT_DIR / "out" / "nosdos.iso"
DEFAULT_TIMEOUT = int(config.get("qemu", "boot_timeout_s", fallback="10"))
QEMU_BIN = config.get("qemu", "binary", fallback="qemu-system-i386")

# Patterns that indicate a successful DOS boot.
# AUTOEXEC.BAT writes "NOS-DOS-READY" to COM1 (the QEMU serial port) at the
# end of boot. We also accept a raw DOS prompt as a fallback.
PROMPT_PATTERNS = [
    re.compile(r"NOS-DOS-READY"),      # sentinel written by AUTOEXEC.BAT to COM1
    re.compile(r"[A-Z]:\\>"),          # standard DOS prompt: C:\>
    re.compile(r"[A-Z]:\\\w+\\?>"),    # prompt with path: C:\NOS>
]

# Patterns that indicate a fatal error
FAILURE_PATTERNS = [
    re.compile(r"Non-System disk"),
    re.compile(r"Invalid system disk"),
    re.compile(r"No bootable device"),
    re.compile(r"BOOT FAILURE"),
]

# Detection result patterns (parsed from DETECT.EXE output on COM1)
_RE_CONV  = re.compile(r"Conventional\s*:\s*(\d+)\s*KB")
_RE_XMS   = re.compile(r"XMS\s*:\s*(\d+)\s*KB")
_RE_VGA   = re.compile(r"Adapter\s*:\s*([^\r\n]+)", re.IGNORECASE)
# Mouse/Sound/Network all use "Status :" — disambiguate by anchoring to section header.
# [^\[] matches any char including newlines, so it spans lines but stops before the next [ header.
_RE_MOUSE = re.compile(r"\[\s*Mouse\s*\][^\[]*?Status\s*:\s*([^\r\n]+)", re.IGNORECASE)
_RE_SOUND = re.compile(r"\[\s*Sound\s*\][^\[]*?Status\s*:\s*([^\r\n]+)", re.IGNORECASE)
_RE_NET   = re.compile(r"\[\s*Network\s*\][^\[]*?Status\s*:\s*([^\r\n]+)", re.IGNORECASE)

# Minimum conventional memory expected in QEMU (-m 16, JEMMEX loaded)
CONV_MEM_MIN_KB = 560


def log(msg: str, verbose: bool = False, force: bool = False) -> None:
    if verbose or force:
        print(msg, flush=True)


def build_qemu_cmd(iso: Path) -> list[str]:
    """Build the QEMU command line for headless boot testing.

    AUTOEXEC.BAT writes "NOS-DOS-READY" to COM1 at end of boot.
    QEMU captures COM1 via -serial stdio, which this process reads from stdout.
    The VGA display is suppressed (no window needed for CI).
    """
    return [
        QEMU_BIN,
        "-cdrom", str(iso),
        "-boot", "d",           # boot from CD-ROM
        "-m", "16",             # 16MB RAM (realistic for DOS)
        "-no-reboot",           # exit on triple fault rather than reboot loop
        "-no-shutdown",         # keep process alive after DOS HALT
        "-display", "none",     # headless — no graphical window
        "-serial", "stdio",     # COM1 → this process's stdout
    ]


def scan_output(proc: subprocess.Popen, timeout: float, verbose: bool) -> tuple[bool, str]:
    """Read QEMU output, looking for a DOS prompt or failure pattern.

    Returns (success: bool, captured_output: str).
    """
    captured = []
    deadline = time.monotonic() + timeout
    fd = proc.stdout.fileno()  # type: ignore[union-attr]

    while time.monotonic() < deadline:
        remaining = deadline - time.monotonic()
        ready, _, _ = select.select([fd], [], [], min(remaining, 0.5))

        if proc.poll() is not None:
            # QEMU exited
            tail = proc.stdout.read()  # type: ignore[union-attr]
            if tail:
                captured.append(tail.decode(errors="replace"))
            break

        if not ready:
            continue

        chunk = os.read(fd, 4096)
        if not chunk:
            break

        text = chunk.decode(errors="replace")
        captured.append(text)

        if verbose:
            print(text, end="", flush=True)

        full_output = "".join(captured)

        for pat in FAILURE_PATTERNS:
            if pat.search(full_output):
                return False, full_output

        for pat in PROMPT_PATTERNS:
            if pat.search(full_output):
                return True, full_output

    return False, "".join(captured)


def check_detection_results(output: str) -> bool:
    """Parse DETECT.EXE results from serial output and assert minimums.

    Returns True if all required checks pass, False if any hard check fails.
    Prints a formatted detection summary regardless.
    """
    passed = True

    print("[boot_test] --- Detection results ---")

    # Conventional memory (hard requirement)
    m = _RE_CONV.search(output)
    if m:
        conv_kb = int(m.group(1))
        status = "OK" if conv_kb >= CONV_MEM_MIN_KB else "FAIL"
        print(f"[boot_test]   Conventional : {conv_kb} KB  [{status}]"
              f"  (min {CONV_MEM_MIN_KB} KB)")
        if conv_kb < CONV_MEM_MIN_KB:
            passed = False
    else:
        print("[boot_test]   Conventional : NOT FOUND in output  [WARN]")
        print("[boot_test]   (DETECT.EXE may not have redirected to COM1)")

    # XMS (warning only — JEMMEX may not load in all environments)
    m = _RE_XMS.search(output)
    if m:
        print(f"[boot_test]   XMS          : {m.group(1)} KB  [OK]")
    else:
        print("[boot_test]   XMS          : not detected  [WARN]")

    # Informational fields — reported but never fail the test
    for label, pattern in [("VGA", _RE_VGA), ("Mouse", _RE_MOUSE),
                             ("Sound", _RE_SOUND), ("Network", _RE_NET)]:
        m = pattern.search(output)
        val = m.group(1).strip() if m else "?"
        print(f"[boot_test]   {label:<13}: {val}")

    print("[boot_test] ---")
    return passed


def main() -> int:
    parser = argparse.ArgumentParser(description="NOS-DOS QEMU boot test")
    parser.add_argument("--iso", type=Path, default=DEFAULT_ISO,
                        help=f"ISO to test (default: {DEFAULT_ISO})")
    parser.add_argument("--timeout", type=int, default=DEFAULT_TIMEOUT,
                        help=f"Seconds to wait for prompt (default: {DEFAULT_TIMEOUT})")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Print QEMU output in real time")
    args = parser.parse_args()

    # Pre-flight checks
    import shutil
    if not shutil.which(QEMU_BIN):
        print(f"ERROR: {QEMU_BIN} not found on PATH", file=sys.stderr)
        return 2

    if not args.iso.exists():
        print(f"ERROR: ISO not found: {args.iso}", file=sys.stderr)
        print("Run: python build/build.py", file=sys.stderr)
        return 2

    size_mb = args.iso.stat().st_size / (1024 * 1024)
    print(f"[boot_test] Testing: {args.iso} ({size_mb:.1f} MB)")
    print(f"[boot_test] Timeout: {args.timeout}s | QEMU: {QEMU_BIN}")

    cmd = build_qemu_cmd(args.iso)
    if args.verbose:
        print(f"[boot_test] Command: {' '.join(str(c) for c in cmd)}")

    t0 = time.monotonic()
    try:
        proc = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
    except FileNotFoundError:
        print(f"ERROR: {QEMU_BIN} not found", file=sys.stderr)
        return 2

    try:
        success, output = scan_output(proc, args.timeout, args.verbose)
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()

    elapsed = time.monotonic() - t0

    if success:
        print(f"[boot_test] PASSED — DOS prompt detected in {elapsed:.1f}s")
        detection_ok = check_detection_results(output)
        if not detection_ok:
            print("[boot_test] FAILED — detection result check failed")
            return 1
        return 0
    else:
        print(f"[boot_test] FAILED — no DOS prompt within {args.timeout}s")
        if not args.verbose and output:
            lines = output.splitlines()
            print("[boot_test] Last output:")
            for line in lines[-20:]:
                print(f"  {line}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
