#!/usr/bin/env python3
"""
NOS-DOS build/mkdataiso.py
Create a data ISO from a host directory for transfer into the NOS-DOS VM.

Usage:
  python3 build/mkdataiso.py <source_dir> [--out <iso_path>] [--label <LABEL>]

The ISO can be attached to VirtualBox as a second optical drive (E:, F:, etc.)
alongside the installed NOS-DOS disk. Files can then be copied from the CD
to C: inside the VM.

Typical workflow:
  1. Place files to transfer in a staging directory on the host.
  2. python3 build/mkdataiso.py staging/ --out out/data.iso
  3. In VirtualBox: Devices -> Optical Drives -> attach out/data.iso
     (If the NOS-DOS ISO is still on D:, the data ISO will be E:)
  4. Inside the VM: COPY E:\\*.* C:\\TARGET\\

Requirements:
  genisoimage (or mkisofs) on PATH
"""

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

BUILD_DIR = Path(__file__).parent.resolve()
ROOT_DIR  = BUILD_DIR.parent
OUT_DIR   = ROOT_DIR / "out"


def log(msg: str) -> None:
    print(f"[mkdataiso] {msg}", flush=True)


def find_iso_tool() -> str | None:
    for tool in ("genisoimage", "mkisofs", "xorriso"):
        if shutil.which(tool):
            return tool
    return None


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Create a data transfer ISO for the NOS-DOS VM."
    )
    parser.add_argument("source", help="Directory of files to include in the ISO")
    parser.add_argument("--out",   default=str(OUT_DIR / "data.iso"),
                        help="Output ISO path (default: out/data.iso)")
    parser.add_argument("--label", default="NOSDATA",
                        help="Volume label, max 11 chars (default: NOSDATA)")
    args = parser.parse_args()

    src = Path(args.source)
    out = Path(args.out)
    label = args.label[:11].upper()

    if not src.exists() or not src.is_dir():
        log(f"ERROR: source directory not found: {src}")
        return 1

    iso_tool = find_iso_tool()
    if not iso_tool:
        log("ERROR: genisoimage, mkisofs, or xorriso not found on PATH")
        return 1

    out.parent.mkdir(parents=True, exist_ok=True)

    file_count = sum(1 for _ in src.rglob("*") if _.is_file())
    log(f"Source: {src}  ({file_count} files)")
    log(f"Output: {out}")
    log(f"Label:  {label}")

    cmd = [
        iso_tool,
        "-o", str(out),
        "-V", label,
        "-J",               # Joliet (long filenames visible from host)
        "-iso-level", "2",  # 31-char filenames in primary descriptor
        # No Rock Ridge — DOS CD-ROM drivers (OAKCDROM.SYS) don't need it
        # and some versions mis-parse RR extensions at depth > 2.
        str(src),
    ]

    log(f"  $ {' '.join(cmd)}")
    result = subprocess.run(cmd, capture_output=True)
    stderr = result.stderr.decode(errors="replace") if result.stderr else ""

    if result.returncode not in (0, 1):
        log(f"ERROR: {iso_tool} failed (exit {result.returncode})")
        if stderr:
            log(stderr)
        return 1

    for line in stderr.splitlines():
        if line.strip():
            log(f"  {line}")

    size_mb = out.stat().st_size / (1024 * 1024)
    log(f"ISO ready: {out} ({size_mb:.1f} MB)")
    log("")
    log("To use in VirtualBox:")
    log("  Devices -> Optical Drives -> Choose/Create a Disk Image -> select the ISO")
    log("  The CD will appear as the next available drive letter (D:, E:, etc.)")
    log("")
    log("To use in QEMU, add to the launch command:")
    log(f"  -drive file={out},media=cdrom,readonly=on")

    return 0


if __name__ == "__main__":
    sys.exit(main())
