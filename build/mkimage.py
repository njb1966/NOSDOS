#!/usr/bin/env python3
"""NOS-DOS: Build system
mkimage.py - Create a bootable FAT12 floppy disk image.

Phase 0: 1.44MB floppy image that boots FreeDOS to a C:\> prompt.
Phase 1+: Will add a 504MB FAT16 hard disk image for VM import.

Process:
  1. Assemble the FreeDOS FAT12 boot sector (fat12.asm) with NASM
  2. Create a 1.44MB raw image with dd-equivalent
  3. Format FAT12 with mformat, embedding the FreeDOS boot sector
  4. Copy KERNEL.SYS and COMMAND.COM via mcopy
  5. Write minimal CONFIG.SYS and AUTOEXEC.BAT
  6. Copy the NOS-DOS skeleton directory structure

Requires: nasm, mtools (mformat, mcopy, mmd, mattrib)
"""

import configparser
import shutil
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
SKELETON_DIR = DIST_DIR / "skeleton"

FREEDOS_DIR = THIRDPARTY / "freedos"
FLOPPY_SIZE_KB = int(config["image"]["floppy_size_kb"])
FLOPPY_IMG = OUT_DIR / "nosdos.img"

# 1.44MB floppy geometry
FLOPPY_SECTORS = 2880       # 80 tracks × 18 sectors × 2 heads
FLOPPY_HEADS = 2
FLOPPY_TRACKS = 80
FLOPPY_SPT = 18             # sectors per track


def log(msg: str) -> None:
    print(f"[mkimage] {msg}", flush=True)


def check_tool(name: str) -> bool:
    """Return True if a tool is available on PATH."""
    return shutil.which(name) is not None


def require_tools(*names: str) -> bool:
    """Check all required tools exist; log and return False if any missing."""
    missing = [n for n in names if not check_tool(n)]
    if missing:
        log(f"ERROR: required tools not found: {', '.join(missing)}")
        return False
    return True


def run(cmd: list[str], check: bool = True, **kwargs) -> subprocess.CompletedProcess:
    """Run a subprocess command, logging it first."""
    log(f"  $ {' '.join(str(c) for c in cmd)}")
    return subprocess.run(cmd, check=check, **kwargs)


def assemble_boot_sector(boot_asm: Path, out_bin: Path) -> bool:
    """Assemble the FreeDOS boot sector (boot.asm) with NASM.

    boot.asm from the FDOS/kernel repo handles FAT12 and FAT16.
    We define BOOT_FAT12=1 and set the working directory so any relative
    includes resolve correctly.
    """
    if not boot_asm.exists():
        log(f"ERROR: boot sector source not found: {boot_asm}")
        log("Run fetch_deps.py first.")
        return False

    result = run(
        [
            "nasm", "-f", "bin",
            "-DISFAT12=1",              # select FAT12 variant (1.44MB floppy)
            str(boot_asm), "-o", str(out_bin),
        ],
        check=False,
        cwd=str(boot_asm.parent),       # resolve any relative includes
    )
    if result.returncode != 0:
        log("ERROR: NASM failed to assemble boot sector")
        log("Hint: boot.asm may require additional include files from the")
        log("      FreeDOS kernel repo. Check NASM output above.")
        return False

    if out_bin.stat().st_size != 512:
        log(f"ERROR: boot sector wrong size: {out_bin.stat().st_size} bytes (expected 512)")
        return False

    log(f"  boot sector assembled: {out_bin} (512 bytes)")
    return True


def create_floppy_image(img_path: Path, boot_sector: Path) -> bool:
    """Create an empty 1.44MB raw image and format it FAT12 with the FreeDOS boot sector."""
    img_path.parent.mkdir(parents=True, exist_ok=True)

    # Create zeroed image (1.44MB = 1474560 bytes)
    img_path.write_bytes(b"\x00" * (FLOPPY_SIZE_KB * 1024))
    log(f"  created: {img_path} ({FLOPPY_SIZE_KB}KB)")

    # mformat: format FAT12, inject our boot sector
    # -i = image file, -f 1440 = 1.44MB floppy, -B = boot sector binary
    # :: = the floppy drive letter (mtools convention for image file)
    result = run(
        [
            "mformat",
            "-i", str(img_path),
            "-f", "1440",
            "-B", str(boot_sector),
            "-v", "NOS-DOS",
            "::",
        ],
        check=False,
    )
    if result.returncode != 0:
        log("ERROR: mformat failed")
        return False

    return True


def install_freedos_files(img_path: Path) -> bool:
    """Copy KERNEL.SYS and COMMAND.COM into the floppy image."""
    kernel_sys = FREEDOS_DIR / "KERNEL.SYS"
    command_com = FREEDOS_DIR / "COMMAND.COM"

    for f in [kernel_sys, command_com]:
        if not f.exists():
            log(f"ERROR: required file not found: {f}")
            log("Run fetch_deps.py first.")
            return False

    # mcopy -i <image> <src> <dst>
    # KERNEL.SYS must be the first file on the disk for boot sector to find it.
    # We copy it first, then set system+hidden+readonly attributes.
    for src in [kernel_sys, command_com]:
        result = run(
            ["mcopy", "-i", str(img_path), str(src), f"::{src.name}"],
            check=False,
        )
        if result.returncode != 0:
            log(f"ERROR: mcopy failed for {src.name}")
            return False

    # Set KERNEL.SYS attributes: system + hidden + readonly (like SYS.COM does)
    result = run(
        ["mattrib", "-i", str(img_path), "+s", "+h", "+r", "::KERNEL.SYS"],
        check=False,
    )
    if result.returncode != 0:
        log("WARNING: mattrib failed — KERNEL.SYS attributes not set")
        # Non-fatal; kernel may still load

    return True


def write_config_files(img_path: Path) -> bool:
    """Write CONFIG.SYS and AUTOEXEC.BAT for Phase 1 boot.

    CONFIG.SYS loads JEMMEX for UMB/XMS management.
    AUTOEXEC.BAT calls DETECT.EXE /NOREBOOT so detection output is
    visible on screen; genconf will fail gracefully (floppy is A:, not C:)
    and AUTOEXEC.BAT continues to write the boot sentinel on COM1.
    """
    config_sys = (
        "REM NOS-DOS Phase 1 CONFIG.SYS\r\n"
        "DOS=HIGH,UMB\r\n"
        "DEVICE=A:\\NOS\\SYSTEM\\JEMMEX.EXE NOEMS X=TEST\r\n"
        "FILES=40\r\n"
        "BUFFERS=20\r\n"
        "STACKS=9,256\r\n"
        "SHELL=COMMAND.COM /P /E:512\r\n"
    )
    autoexec_bat = (
        "@ECHO OFF\r\n"
        "SET PROMPT=$P$G\r\n"
        # Set PATH so DOS commands work from the shell-out prompt.
        # C:\NOS\SYSTEM is always present if the HDD is attached.
        "SET PATH=C:\\;C:\\NOS\\SYSTEM;C:\\NOS\\SHELL;C:\\APPS\r\n"
        "ECHO.\r\n"
        "ECHO  NOS-DOS (NostalgicDOS)\r\n"
        "ECHO.\r\n"
        # Redirect DETECT.EXE stdout to COM1 so the boot test can parse
        # the detection results. Writes generated CONFIG.SYS/AUTOEXEC.BAT
        # to C:\ if a hard disk is attached; silently skipped if not.
        "A:\\NOS\\SYSTEM\\DETECT.EXE /NOREBOOT > COM1\r\n"
        "ECHO.\r\n"
        # Boot sentinel written to COM1 (QEMU -serial stdio captures this)
        "ECHO NOS-DOS-READY > COM1\r\n"
        # Phase 2+: launch shell from C: if present
        # IF EXIST is safe — DOS ignores the line silently when file is absent
        "IF EXIST C:\\NOS\\SHELL\\SHELL.EXE C:\\NOS\\SHELL\\SHELL.EXE\r\n"
    )

    for filename, content in [("CONFIG.SYS", config_sys), ("AUTOEXEC.BAT", autoexec_bat)]:
        tmp = OUT_DIR / filename
        tmp.write_bytes(content.encode("ascii"))
        result = run(
            ["mcopy", "-i", str(img_path), str(tmp), f"::{filename}"],
            check=False,
        )
        tmp.unlink(missing_ok=True)
        if result.returncode != 0:
            log(f"ERROR: mcopy failed for {filename}")
            return False

    return True


def install_nos_system_files(img_path: Path) -> bool:
    """Copy NOS-DOS binaries and templates into NOS\\SYSTEM\\ on the image.

    Required (build fails if absent):
      JEMMEX.EXE, CTMOUSE.EXE  — thirdparty drivers (from fetch_deps.py)
      CONFIG.TPL, AUTOEXEC.TPL  — config templates (from dist/config/)

    Optional (logged as warnings if absent — compile stage may not have run):
      DETECT.EXE                — NOS-DETECT hardware detection
      NOSMEM.EXE                — NOS-MEM profile switcher
    """
    # Required thirdparty drivers.
    # Destination is the directory only (trailing /); mcopy preserves source filename.
    required = [
        (THIRDPARTY / "jemmex" / "JEMMEX.EXE",  "NOS/SYSTEM/"),
        (THIRDPARTY / "ctmouse" / "CTMOUSE.EXE", "NOS/SYSTEM/"),
        (DIST_DIR / "config" / "CONFIG.TPL",     "NOS/SYSTEM/"),
        (DIST_DIR / "config" / "AUTOEXEC.TPL",   "NOS/SYSTEM/"),
        (DIST_DIR / "utils" / "MEM.EXE",    "NOS/SYSTEM/"),
        (DIST_DIR / "utils" / "CHKDSK.EXE", "NOS/SYSTEM/"),
    ]
    # Optional compiled binaries (present only after Open Watcom compile stage)
    optional = [
        (ROOT_DIR / "src" / "detect" / "bin" / "DETECT.EXE", "NOS/SYSTEM/"),
        (ROOT_DIR / "src" / "mem"    / "bin" / "NOSMEM.EXE",  "NOS/SYSTEM/"),
        (ROOT_DIR / "src" / "net"    / "bin" / "NNET.EXE",    "NOS/SYSTEM/"),
    ]

    for src, dst in required:
        if not src.exists():
            log(f"ERROR: required file not found: {src}")
            log("Run fetch_deps.py to download thirdparty dependencies.")
            return False
        result = run(
            ["mcopy", "-i", str(img_path), str(src), f"::{dst}"],
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
            ["mcopy", "-i", str(img_path), str(src), f"::{dst}"],
            check=False,
        )
        if result.returncode != 0:
            log(f"WARNING: mcopy failed for {src.name} — skipping")

    return True


def copy_skeleton(img_path: Path) -> bool:
    """Create the NOS-DOS directory skeleton on the floppy image.

    Phase 0: creates the top-level directory structure only.
    Files will be populated in later phases.
    """
    # Directories to create on the floppy (8.3 names, uppercase).
    # Use forward slashes — mtools on Linux resolves these correctly.
    directories = [
        "NOS",
        "NOS/SYSTEM",
        "NOS/SHELL",
        "NOS/DOCS",
        "APPS",
        "GAMES",
        "USER",
        "TEMP",
    ]

    for d in directories:
        # mmd creates a directory; ignore if already exists (exit 1 is OK)
        run(
            ["mmd", "-i", str(img_path), f"::{d}"],
            check=False,
        )

    return True


def main() -> int:
    if not require_tools("nasm", "mformat", "mcopy", "mmd", "mattrib"):
        return 1

    OUT_DIR.mkdir(parents=True, exist_ok=True)

    # Step 1: Assemble FreeDOS boot sector
    log("Assembling FreeDOS FAT12 boot sector...")
    boot_asm = FREEDOS_DIR / "boot.asm"
    boot_bin = OUT_DIR / "boot12.bin"
    if not assemble_boot_sector(boot_asm, boot_bin):
        return 1

    # Step 2: Create and format floppy image
    log("Creating FAT12 floppy image...")
    if not create_floppy_image(FLOPPY_IMG, boot_bin):
        return 1

    # Step 3: Install FreeDOS kernel and shell
    log("Installing FreeDOS kernel and COMMAND.COM...")
    if not install_freedos_files(FLOPPY_IMG):
        return 1

    # Step 5: Create directory skeleton
    log("Creating NOS-DOS directory skeleton...")
    if not copy_skeleton(FLOPPY_IMG):
        return 1

    # Step 6: Install NOS-DOS system files (drivers, binaries, templates)
    log("Installing NOS-DOS system files...")
    if not install_nos_system_files(FLOPPY_IMG):
        return 1

    # Step 7: Write CONFIG.SYS and AUTOEXEC.BAT
    log("Writing CONFIG.SYS and AUTOEXEC.BAT...")
    if not write_config_files(FLOPPY_IMG):
        return 1

    log(f"Floppy image ready: {FLOPPY_IMG}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
