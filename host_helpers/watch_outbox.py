#!/usr/bin/env python3
"""
NOS-DOS host helper: watch_outbox.py
Watches H:\\OUTBOX\\ for files placed there by DOS and copies them to a
host destination directory (default: ~/Desktop/NOS-DOS-Outbox/).

Usage:
    python3 watch_outbox.py [--dir <path>] [--dest <dir>] [--interval <s>]
                            [--move] [--once]

Options:
    --dir <path>      Shared OUTBOX folder to watch  [default: auto-detect]
    --dest <dir>      Host destination directory  [default: ~/Desktop/NOS-DOS-Outbox]
    --interval <s>    Poll interval in seconds  [default: 3]
    --move            Move files instead of copying (removes from OUTBOX)
    --once            Process existing files then exit (no loop)
"""

import argparse
import os
import shutil
import sys
import time
from pathlib import Path


def find_shared_dir() -> Path | None:
    """Try common VM shared-folder mount points for the OUTBOX directory."""
    candidates = [
        Path("/media/sf_NOSDOS/OUTBOX"),
        Path("/mnt/hgfs/NOSDOS/OUTBOX"),
        Path("/Volumes/NOSDOS/OUTBOX"),
        Path("H:/OUTBOX"),
        Path(r"H:\OUTBOX"),
    ]
    for c in candidates:
        if c.is_dir():
            return c
    return None


def default_dest() -> Path:
    desktop = Path.home() / "Desktop"
    if desktop.is_dir():
        return desktop / "NOS-DOS-Outbox"
    return Path.home() / "NOS-DOS-Outbox"


def process_file(src: Path, dest_dir: Path, move: bool) -> None:
    dest = dest_dir / src.name
    # Avoid overwriting: append a counter if destination exists.
    if dest.exists():
        stem, suffix = src.stem, src.suffix
        i = 1
        while dest.exists():
            dest = dest_dir / f"{stem}_{i}{suffix}"
            i += 1

    try:
        if move:
            shutil.move(str(src), dest)
            print(f"[OUTBOX] moved  {src.name} → {dest}")
        else:
            shutil.copy2(src, dest)
            print(f"[OUTBOX] copied {src.name} → {dest}")
    except (OSError, shutil.Error) as e:
        print(f"[OUTBOX] error processing {src.name}: {e}")


def watch(watch_dir: Path, dest_dir: Path, move: bool,
          interval: float, once: bool) -> None:
    seen: set[str] = set()
    print(f"Watching {watch_dir}  →  {dest_dir}  (Ctrl+C to stop)")

    while True:
        try:
            all_files = [f for f in watch_dir.iterdir() if f.is_file()]
        except PermissionError:
            print(f"Cannot read {watch_dir} -- check permissions.")
            all_files = []

        for f in sorted(all_files):
            if f.name not in seen:
                seen.add(f.name)
                process_file(f, dest_dir, move)

        if once:
            break
        time.sleep(interval)


def main() -> None:
    parser = argparse.ArgumentParser(description="Copy DOS OUTBOX files to host.")
    parser.add_argument("--dir",      help="Path to OUTBOX directory")
    parser.add_argument("--dest",     help="Host destination directory")
    parser.add_argument("--interval", type=float, default=3.0, help="Poll interval (s)")
    parser.add_argument("--move",     action="store_true", help="Move instead of copy")
    parser.add_argument("--once",     action="store_true", help="Process then exit")
    args = parser.parse_args()

    if args.dir:
        watch_dir = Path(args.dir)
    else:
        watch_dir = find_shared_dir()
        if watch_dir is None:
            print("Cannot auto-detect shared OUTBOX folder.")
            print("Use --dir to specify its path.")
            sys.exit(1)

    if not watch_dir.is_dir():
        print(f"Directory not found: {watch_dir}")
        sys.exit(1)

    dest_dir = Path(args.dest) if args.dest else default_dest()
    if not dest_dir.is_dir():
        dest_dir.mkdir(parents=True, exist_ok=True)
        print(f"Created destination: {dest_dir}")

    try:
        watch(watch_dir, dest_dir, args.move, args.interval, args.once)
    except KeyboardInterrupt:
        print("\nStopped.")


if __name__ == "__main__":
    main()
