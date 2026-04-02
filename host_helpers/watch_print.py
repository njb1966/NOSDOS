#!/usr/bin/env python3
"""
NOS-DOS host helper: watch_print.py
Watches H:\\PRINT\\ (or a specified directory) for new .PRN spool files
and optionally converts them to PDF via Ghostscript.

Usage:
    python3 watch_print.py [--dir <path>] [--pdf] [--out <dir>] [--interval <s>]

Options:
    --dir <path>      Shared PRINT folder to watch  [default: auto-detect]
    --pdf             Convert .PRN files to PDF via Ghostscript (gs must be on PATH)
    --out <dir>       Output directory for converted PDFs  [default: same as --dir]
    --interval <s>    Poll interval in seconds  [default: 3]
    --once            Process existing files then exit (no loop)

The script tracks files it has already seen (by name) in a local set so it
only processes each .PRN once per run.  Restart to reprocess.
"""

import argparse
import os
import subprocess
import sys
import time
from pathlib import Path


def find_shared_dir() -> Path | None:
    """Try common VM shared-folder mount points for the PRINT directory."""
    candidates = [
        # Linux VirtualBox / VMware guest additions
        Path("/media/sf_NOSDOS/PRINT"),
        Path("/mnt/hgfs/NOSDOS/PRINT"),
        # macOS
        Path("/Volumes/NOSDOS/PRINT"),
        # Windows host running the script with a mapped drive
        Path("H:/PRINT"),
        Path(r"H:\PRINT"),
    ]
    for c in candidates:
        if c.is_dir():
            return c
    return None


def prn_to_pdf(prn_path: Path, out_dir: Path) -> bool:
    """Convert a PRN file to PDF using Ghostscript.  Returns True on success."""
    pdf_name = prn_path.stem + ".pdf"
    pdf_path = out_dir / pdf_name
    cmd = [
        "gs",
        "-dBATCH",
        "-dNOPAUSE",
        "-dSAFER",
        "-sDEVICE=pdfwrite",
        f"-sOutputFile={pdf_path}",
        str(prn_path),
    ]
    try:
        result = subprocess.run(cmd, capture_output=True, timeout=30)
        if result.returncode == 0:
            print(f"  → converted to {pdf_path}")
            return True
        else:
            print(f"  gs error: {result.stderr.decode(errors='replace').strip()}")
            return False
    except FileNotFoundError:
        print("  Ghostscript (gs) not found on PATH -- skipping PDF conversion.")
        print("  Install with: apt install ghostscript  /  brew install ghostscript")
        return False
    except subprocess.TimeoutExpired:
        print("  Ghostscript timed out.")
        return False


def process_file(prn_path: Path, out_dir: Path, convert_pdf: bool) -> None:
    size = prn_path.stat().st_size
    print(f"[PRINT] {prn_path.name}  ({size} bytes)")
    if convert_pdf:
        prn_to_pdf(prn_path, out_dir)


def watch(watch_dir: Path, out_dir: Path, convert_pdf: bool,
          interval: float, once: bool) -> None:
    seen: set[str] = set()
    print(f"Watching {watch_dir} for .PRN files  (Ctrl+C to stop)")
    if convert_pdf:
        print("PDF conversion enabled.")

    while True:
        try:
            prn_files = sorted(watch_dir.glob("*.PRN")) + sorted(watch_dir.glob("*.prn"))
        except PermissionError:
            print(f"Cannot read {watch_dir} -- check permissions.")
            prn_files = []

        for prn in prn_files:
            if prn.name not in seen:
                seen.add(prn.name)
                process_file(prn, out_dir, convert_pdf)

        if once:
            break
        time.sleep(interval)


def main() -> None:
    parser = argparse.ArgumentParser(description="Watch DOS PRINT spool folder.")
    parser.add_argument("--dir",      help="Path to PRINT spool directory")
    parser.add_argument("--pdf",      action="store_true", help="Convert PRN→PDF")
    parser.add_argument("--out",      help="PDF output directory")
    parser.add_argument("--interval", type=float, default=3.0, help="Poll interval (s)")
    parser.add_argument("--once",     action="store_true", help="Process then exit")
    args = parser.parse_args()

    if args.dir:
        watch_dir = Path(args.dir)
    else:
        watch_dir = find_shared_dir()
        if watch_dir is None:
            print("Cannot auto-detect shared PRINT folder.")
            print("Use --dir to specify its path.")
            sys.exit(1)

    if not watch_dir.is_dir():
        print(f"Directory not found: {watch_dir}")
        sys.exit(1)

    out_dir = Path(args.out) if args.out else watch_dir
    if not out_dir.is_dir():
        out_dir.mkdir(parents=True, exist_ok=True)

    try:
        watch(watch_dir, out_dir, args.pdf, args.interval, args.once)
    except KeyboardInterrupt:
        print("\nStopped.")


if __name__ == "__main__":
    main()
