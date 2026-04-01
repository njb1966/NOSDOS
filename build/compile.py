#!/usr/bin/env python3
"""NOS-DOS: Build system
compile.py - Invoke Open Watcom to cross-compile DOS components.

STUB: Open Watcom is not yet installed. This script logs the commands
that will be run and exits cleanly. When Open Watcom is available,
set WATCOM in the environment or update config.ini.

Expected environment:
  WATCOM=/path/to/watcom   Open Watcom installation root
  PATH must include $WATCOM/binl (Linux host tools: wcc, wlink, wmake)
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path

BUILD_DIR = Path(__file__).parent.resolve()
ROOT_DIR = BUILD_DIR.parent
SRC_DIR = ROOT_DIR / "src"

# Components with a Makefile — built in dependency order
COMPONENTS = [
    "detect",
    "mem",
    "net",
    "bridge",
    "throttle",
    "npkg",
    "play",
    "shell",
]


def log(msg: str) -> None:
    print(f"[compile] {msg}", flush=True)


def check_watcom() -> bool:
    """Return True if Open Watcom wcc is on PATH."""
    return shutil.which("wcc") is not None


def compile_component(name: str, watcom_env: dict[str, str]) -> bool:
    """Run wmake in the component's source directory."""
    comp_dir = SRC_DIR / name
    makefile = comp_dir / "Makefile"
    if not makefile.exists():
        log(f"  skip {name}: no Makefile yet")
        return True

    log(f"  building {name}...")
    result = subprocess.run(
        ["wmake", "-f", "Makefile"],
        cwd=comp_dir,
        env={**os.environ, **watcom_env},
    )
    if result.returncode != 0:
        log(f"  ERROR: {name} failed (wmake exit {result.returncode})")
        return False
    return True


def main() -> int:
    watcom_root = os.environ.get("WATCOM", "")

    if not watcom_root or not check_watcom():
        log("Open Watcom not found — skipping DOS compilation.")
        log("Install Open Watcom and set WATCOM env var to enable.")
        log("  Download: https://open-watcom.github.io/")
        log("  Suggested: export WATCOM=/opt/watcom")
        log("             export PATH=$WATCOM/binl:$PATH")
        log("Compilation skipped — build will use placeholder binaries if present.")
        return 0

    watcom_env = {
        "WATCOM": watcom_root,
        # binl64 = 64-bit Linux host tools; binl = 32-bit fallback
        "PATH": f"{watcom_root}/binl64:{watcom_root}/binl:{os.environ.get('PATH', '')}",
        # h/dos contains <dos.h>, <conio.h> and other DOS-specific headers
        "INCLUDE": f"{watcom_root}/h:{watcom_root}/h/dos",
    }

    log(f"Open Watcom found at: {watcom_root}")
    failures = []
    for name in COMPONENTS:
        if not compile_component(name, watcom_env):
            failures.append(name)

    if failures:
        log(f"Build failures: {', '.join(failures)}")
        return 1

    log("All components compiled successfully.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
