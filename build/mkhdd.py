#!/usr/bin/env python3
"""NOS-DOS: Build system
mkhdd.py - Create a FAT16 hard disk image (C: drive).

Creates a 32 MB raw disk image with:
  - MBR partition table (single FAT16 partition at sector 63)
  - Partition formatted FAT16 via mformat
  - NOS-DOS directory skeleton (NOS/SYSTEM, NOS/SHELL, etc.)
  - System files: JEMMEX.EXE, CTMOUSE.EXE, DETECT.EXE, NOSMEM.EXE,
                  CONFIG.TPL, AUTOEXEC.TPL
  - FreeDOS kernel files (KERNEL.SYS, COMMAND.COM) for future HDD-boot support

When attached to QEMU alongside the El Torito boot ISO, the BIOS presents
this image as hard disk 0x80, which FreeDOS assigns to drive C:.

Partition geometry (fits within 32 MB):
  Cylinders=64  Heads=16  Sectors/track=63
  Partition sectors = 64*16*63 = 64512  (31.5 MB usable)

Requires: mformat, mcopy, mmd, mattrib (mtools)
"""

import configparser
import shutil
import struct
import subprocess
import sys
from pathlib import Path

BUILD_DIR = Path(__file__).parent.resolve()
ROOT_DIR = BUILD_DIR.parent

config = configparser.ConfigParser()
config.read(BUILD_DIR / "config.ini")

DIST_DIR = ROOT_DIR / "dist"
THIRDPARTY = DIST_DIR / "thirdparty"
OUT_DIR = ROOT_DIR / "out"
FREEDOS_DIR = THIRDPARTY / "freedos"

# ---- Disk geometry ----
# Chosen so CYL * HEADS * SPT fits cleanly inside 32 MB and leaves room for
# the MBR at sector 0 and the standard first-partition offset at sector 63.
HDD_CYL   = 64
HDD_HEADS = 16
HDD_SPT   = 63   # sectors per track

PART_START_SECTOR = 63                              # standard MBR convention
PART_SIZE_SECTORS = HDD_CYL * HDD_HEADS * HDD_SPT  # 64512
HDD_TOTAL_SECTORS = PART_START_SECTOR + PART_SIZE_SECTORS  # 64575
HDD_SIZE_BYTES    = HDD_TOTAL_SECTORS * 512         # ~31.5 MB

PART_OFFSET_BYTES = PART_START_SECTOR * 512         # 32256

HDD_IMG = OUT_DIR / "nosdos.hdd"


def log(msg: str) -> None:
    print(f"[mkhdd] {msg}", flush=True)


def check_tool(name: str) -> bool:
    return shutil.which(name) is not None


def require_tools(*names: str) -> bool:
    missing = [n for n in names if not check_tool(n)]
    if missing:
        log(f"ERROR: required tools not found: {', '.join(missing)}")
        return False
    return True


def run(cmd: list[str], check: bool = True, **kwargs) -> subprocess.CompletedProcess:
    log(f"  $ {' '.join(str(c) for c in cmd)}")
    return subprocess.run(cmd, check=check, **kwargs)


def mspec(img_path: Path) -> str:
    """Return the mtools image specifier pointing to the FAT16 partition."""
    return f"{img_path}@@{PART_OFFSET_BYTES}"


# ---------------------------------------------------------------------------
# Step 1 — raw image with MBR
# ---------------------------------------------------------------------------

def create_hdd_image(img_path: Path) -> bool:
    """Write a zeroed image with an MBR partition table."""
    img_path.parent.mkdir(parents=True, exist_ok=True)

    # Partition table entry (16 bytes):
    #   B  status     : 0x80 = active (bootable)
    #   3s chs_first  : CHS of first sector (H=0, S=1, C=0)
    #   B  part_type  : 0x04 = FAT16 < 32 MB
    #   3s chs_last   : 0xFFFFFF = rely on LBA
    #   I  lba_start  : first sector of partition
    #   I  lba_size   : total sectors in partition
    entry = struct.pack(
        "<B3sB3sII",
        0x80,
        bytes([0x00, 0x01, 0x00]),
        0x04,                       # FAT16 < 32 MB
        bytes([0xFF, 0xFF, 0xFF]),
        PART_START_SECTOR,
        PART_SIZE_SECTORS,
    )

    mbr = bytearray(512)
    mbr[446:462] = entry            # partition entry 0
    mbr[510]     = 0x55             # boot signature
    mbr[511]     = 0xAA

    img = bytearray(HDD_SIZE_BYTES)
    img[0:512] = mbr
    img_path.write_bytes(bytes(img))
    log(f"  created: {img_path} ({HDD_SIZE_BYTES // (1024*1024)} MB, "
        f"{HDD_TOTAL_SECTORS} sectors)")
    return True


# ---------------------------------------------------------------------------
# Step 2 — format partition as FAT16
# ---------------------------------------------------------------------------

def format_partition(img_path: Path) -> bool:
    """Format the partition as FAT16 with explicit geometry."""
    # No -F flag: mformat auto-selects FAT16 for volumes in the ~4MB–2GB range.
    # (-F in mtools means FAT32, not "force FAT16".)
    result = run(
        [
            "mformat",
            "-i", mspec(img_path),
            "-v", "NOS-DOS",
            "-t", str(HDD_CYL),
            "-h", str(HDD_HEADS),
            "-s", str(HDD_SPT),
            "::",
        ],
        check=False,
    )
    if result.returncode != 0:
        log("ERROR: mformat failed")
        return False
    return True


# ---------------------------------------------------------------------------
# Step 3 — directory skeleton
# ---------------------------------------------------------------------------

def create_directory_skeleton(img_path: Path) -> bool:
    """Create the NOS-DOS directory tree on C:."""
    spec = mspec(img_path)
    for d in ["NOS", "NOS/SYSTEM", "NOS/SHELL", "NOS/DOCS",
              "APPS", "GAMES", "USER", "TEMP"]:
        run(["mmd", "-i", spec, f"::{d}"], check=False)
    return True


# ---------------------------------------------------------------------------
# Step 4 — system files
# ---------------------------------------------------------------------------

def install_system_files(img_path: Path) -> bool:
    """Copy NOS-DOS and FreeDOS files to the HDD."""
    spec = mspec(img_path)

    # Required — build fails if absent
    required = [
        (THIRDPARTY / "jemmex"  / "JEMMEX.EXE",  "NOS/SYSTEM/"),
        (THIRDPARTY / "ctmouse" / "CTMOUSE.EXE",  "NOS/SYSTEM/"),
        (DIST_DIR   / "config"  / "CONFIG.TPL",   "NOS/SYSTEM/"),
        (DIST_DIR   / "config"  / "AUTOEXEC.TPL",  "NOS/SYSTEM/"),
    ]
    # Optional — compiled by Open Watcom; absent when --skip-compile
    optional = [
        (ROOT_DIR / "src" / "detect" / "bin" / "DETECT.EXE", "NOS/SYSTEM/"),
        (ROOT_DIR / "src" / "mem"    / "bin" / "NOSMEM.EXE",  "NOS/SYSTEM/"),
        (ROOT_DIR / "src" / "shell"  / "bin" / "SHELL.EXE",   "NOS/SHELL/"),
    ]
    # FreeDOS kernel — for future HDD-boot capability
    freedos_files = [
        (FREEDOS_DIR / "KERNEL.SYS",  ""),
        (FREEDOS_DIR / "COMMAND.COM", ""),
    ]

    for src, dst in required:
        if not src.exists():
            log(f"ERROR: required file not found: {src}")
            log("Run fetch_deps.py first.")
            return False
        result = run(
            ["mcopy", "-i", spec, str(src), f"::{dst}"],
            check=False,
        )
        if result.returncode != 0:
            log(f"ERROR: mcopy failed for {src.name}")
            return False

    for src, dst in optional:
        if not src.exists():
            log(f"  WARNING: optional binary not found (compile stage skipped?): {src.name}")
            continue
        result = run(
            ["mcopy", "-i", spec, str(src), f"::{dst}"],
            check=False,
        )
        if result.returncode != 0:
            log(f"WARNING: mcopy failed for {src.name} — skipping")

    for src, dst in freedos_files:
        if not src.exists():
            log(f"  WARNING: FreeDOS file not found: {src.name}")
            continue
        dest = f"::{dst}{src.name}" if dst else f"::{src.name}"
        result = run(
            ["mcopy", "-i", spec, str(src), dest],
            check=False,
        )
        if result.returncode != 0:
            log(f"WARNING: mcopy failed for {src.name} — skipping")

    # System + hidden + readonly on KERNEL.SYS (best-effort)
    run(["mattrib", "-i", spec, "+s", "+h", "+r", "::KERNEL.SYS"], check=False)

    return True


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> int:
    if not require_tools("mformat", "mcopy", "mmd", "mattrib"):
        return 1

    OUT_DIR.mkdir(parents=True, exist_ok=True)

    log(f"Creating HDD image ({HDD_SIZE_BYTES // (1024*1024)} MB, FAT16)...")
    if not create_hdd_image(HDD_IMG):
        return 1

    log("Formatting FAT16 partition...")
    if not format_partition(HDD_IMG):
        return 1

    log("Creating directory skeleton...")
    if not create_directory_skeleton(HDD_IMG):
        return 1

    log("Installing system files...")
    if not install_system_files(HDD_IMG):
        return 1

    size_mb = HDD_IMG.stat().st_size / (1024 * 1024)
    log(f"HDD image ready: {HDD_IMG} ({size_mb:.1f} MB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
