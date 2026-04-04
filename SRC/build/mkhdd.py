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
FREEDOS_DIR  = THIRDPARTY / "freedos"
PACKAGES_DIR = ROOT_DIR / "packages"

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
              "NOS/NPKG", "NOS/NPKG/DEFS", "NOS/NPKG/CACHE",
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
        (THIRDPARTY / "jemmex"  / "JEMMEX.EXE",   "NOS/SYSTEM/"),
        (THIRDPARTY / "ctmouse" / "CTMOUSE.EXE",   "NOS/SYSTEM/"),
        (THIRDPARTY / "pcntpk"  / "PCNTPK.COM",    "NOS/SYSTEM/"),
        (DIST_DIR   / "config"  / "CONFIG.TPL",    "NOS/SYSTEM/"),
        (DIST_DIR   / "config"  / "AUTOEXEC.TPL",  "NOS/SYSTEM/"),
    ]
    # Optional — compiled by Open Watcom; absent when --skip-compile
    optional = [
        (ROOT_DIR / "src" / "detect"   / "bin" / "DETECT.EXE",  "NOS/SYSTEM/"),
        (ROOT_DIR / "src" / "mem"      / "bin" / "NOSMEM.EXE",   "NOS/SYSTEM/"),
        (ROOT_DIR / "src" / "shell"    / "bin" / "SHELL.EXE",    "NOS/SHELL/"),
        (ROOT_DIR / "src" / "net"      / "bin" / "NNET.EXE",     "NOS/SYSTEM/"),
        (ROOT_DIR / "src" / "npkg"     / "bin" / "NPKG.EXE",     "NOS/SYSTEM/"),
        (ROOT_DIR / "src" / "play"     / "bin" / "NOSPLAY.EXE",  "NOS/SHELL/"),
        (ROOT_DIR / "src" / "throttle" / "bin" / "THROTTLE.COM", "NOS/SYSTEM/"),
        (ROOT_DIR / "src" / "throttle" / "bin" / "TCTL.EXE",     "NOS/SYSTEM/"),
        (ROOT_DIR / "src" / "bridge"   / "bin" / "NOSLPT.COM",   "NOS/SYSTEM/"),
        (ROOT_DIR / "src" / "bridge"   / "bin" / "NOSCLIP.COM",  "NOS/SYSTEM/"),
        (ROOT_DIR / "src" / "bridge"   / "bin" / "NBRIDGE.EXE",  "NOS/SYSTEM/"),
        (ROOT_DIR / "src" / "install"  / "bin" / "INSTALL.EXE",  "NOS/SYSTEM/"),
    ]
    # mTCP networking suite (present after fetch_deps.py)
    mtcp_tools = [
        "DHCP.EXE", "PING.EXE", "HTGET.EXE", "FTP.EXE",
        "IRCJR.EXE", "TELNET.EXE", "DNSTEST.EXE", "SNTP.EXE",
    ]
    for tool in mtcp_tools:
        src = THIRDPARTY / "mtcp" / tool
        if src.exists():
            optional.append((src, "NOS/SYSTEM/"))
        else:
            log(f"  INFO: mTCP tool not found (fetch_deps.py needed?): {tool}")
    # On-disk documentation (dist/docs/*.TXT → NOS/DOCS/)
    docs_dir = DIST_DIR / "docs"
    if docs_dir.is_dir():
        for doc in sorted(docs_dir.glob("*.TXT")):
            optional.append((doc, "NOS/DOCS/"))
    else:
        log("  INFO: dist/docs/ not found — documentation not installed")

    # Batch file wrappers for common commands
    bat_optional = [
        (ROOT_DIR / "dist" / "bat" / "NPING.BAT",   "NOS/SHELL/"),
        (ROOT_DIR / "dist" / "bat" / "NWEB.BAT",    "NOS/SHELL/"),
        (ROOT_DIR / "dist" / "bat" / "NFTP.BAT",    "NOS/SHELL/"),
        (ROOT_DIR / "dist" / "bat" / "NIRC.BAT",    "NOS/SHELL/"),
        (ROOT_DIR / "dist" / "bat" / "NTELNET.BAT", "NOS/SHELL/"),
        (ROOT_DIR / "dist" / "bat" / "NTIME.BAT",   "NOS/SHELL/"),
    ]
    for src, dst in bat_optional:
        if src.exists():
            optional.append((src, dst))
        else:
            log(f"  INFO: batch wrapper not found: {src.name}")
    # FreeDOS kernel and utilities
    freedos_files = [
        (FREEDOS_DIR / "KERNEL.SYS",  ""),
        (FREEDOS_DIR / "COMMAND.COM", ""),
        (DIST_DIR / "utils" / "MEM.EXE",    "NOS/SYSTEM/"),
        (DIST_DIR / "utils" / "CHKDSK.EXE", "NOS/SYSTEM/"),
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
# Step 5 — bundle NPKG package definitions
# ---------------------------------------------------------------------------

def install_npkg_defs(img_path: Path) -> bool:
    """Pre-install package index and .npkg definitions so NPKG works offline.

    Copies packages/packages.idx  → NOS/NPKG/CACHE/PKGS.IDX
    Copies packages/*/*.npkg      → NOS/NPKG/DEFS/<ID>.NPK

    The .NPK extension (8.3 compatible) matches the filename NPKG uses when
    caching downloaded definitions (see install.h: NOS_INSTALL_DEFS_DIR).
    With these files pre-installed, NPKG SEARCH, INFO, and INSTALL work
    out of the box without requiring a network connection for NPKG UPDATE.
    """
    spec = mspec(img_path)

    if not PACKAGES_DIR.is_dir():
        log("  INFO: packages/ directory not found — skipping NPKG bundling")
        return True

    # Pre-cache the master index as PKGS.IDX
    idx_src = PACKAGES_DIR / "packages.idx"
    if idx_src.exists():
        result = run(
            ["mcopy", "-i", spec, str(idx_src), "::NOS/NPKG/CACHE/PKGS.IDX"],
            check=False,
        )
        if result.returncode != 0:
            log("WARNING: mcopy failed for packages.idx — NPKG UPDATE required")
        else:
            log(f"  bundled packages.idx → NOS/NPKG/CACHE/PKGS.IDX")
    else:
        log("  INFO: packages/packages.idx not found — run build/mkindex.py first")

    # Pre-cache each .npkg definition as <ID>.NPK
    # Match both .NPKG (repo convention) and .npkg (case-insensitive fallback)
    count = 0
    seen: set = set()
    all_npkg = sorted(PACKAGES_DIR.rglob("*.NPKG")) + sorted(PACKAGES_DIR.rglob("*.npkg"))
    for npkg_file in all_npkg:
        if npkg_file.stem.upper() in seen:
            continue
        seen.add(npkg_file.stem.upper())
        stem = npkg_file.stem.upper()          # e.g. DOOM
        dest = f"::NOS/NPKG/DEFS/{stem}.NPK"
        result = run(
            ["mcopy", "-i", spec, str(npkg_file), dest],
            check=False,
        )
        if result.returncode == 0:
            count += 1
        else:
            log(f"WARNING: mcopy failed for {npkg_file.name} — skipping")

    if count > 0:
        log(f"  bundled {count} .npkg definition(s) → NOS/NPKG/DEFS/")
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

    log("Bundling NPKG package definitions...")
    if not install_npkg_defs(HDD_IMG):
        return 1

    size_mb = HDD_IMG.stat().st_size / (1024 * 1024)
    log(f"HDD image ready: {HDD_IMG} ({size_mb:.1f} MB)")

    # ---- Blank installer-target disk ----
    # Same geometry and FAT16 format as nosdos.hdd, but no files.
    # Used by the VBox and VMware installer workflows: attach this image
    # as the C: drive alongside nosdos.iso.  The installer finds C: already
    # partitioned and formatted, skips FORMAT failure on blank partitions,
    # and copies files directly from the ISO.
    blank_img = OUT_DIR / "nosdos-blank.hdd"
    log(f"Creating blank installer-target HDD ({HDD_SIZE_BYTES // (1024*1024)} MB, FAT16)...")
    if not create_hdd_image(blank_img):
        return 1
    if not format_partition(blank_img):
        return 1
    log(f"Blank HDD image ready: {blank_img}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
