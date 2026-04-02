#!/usr/bin/env python3
"""NOS-DOS: Build system
mkiso.py - Create the NOS-DOS installer ISO.

Produces a bootable El Torito ISO (out/nosdos.iso) that installs NOS-DOS
onto a blank hard disk when booted in a virtual machine.

Boot flow:
  1. BIOS loads the El Torito floppy image (out/install.img).
  2. FreeDOS kernel boots from the floppy.
  3. CONFIG.SYS loads JEMMEX + OAKCDROM.SYS (CD-ROM hardware driver).
  4. AUTOEXEC.BAT loads SHSUCDX.EXE (assigns drive letter D: to the CD).
  5. AUTOEXEC.BAT launches D:\\INSTALL.EXE (the TUI installer).
  6. The installer FORMATs C:, then copies D:\\INSTALL\\*.* to C:\\.
  7. User reboots from C:; first-boot DETECT.EXE completes hardware setup.

ISO structure:
  install.img     El Torito boot image (minimal installer floppy)
  BOOT.CAT        El Torito catalog
  NOSCD.ID        CD marker (installer searches for this to locate the disc)
  FORMAT.COM      FreeDOS FORMAT (invoked by installer for C: format step)
  FDISK.EXE       FreeDOS FDISK  (available at the DOS prompt if needed)
  INSTALL.EXE     NOS-INSTALL TUI installer
  INSTALL/        NOS-DOS files to be copied to C:\ by the installer
    KERNEL.SYS
    COMMAND.COM
    CONFIG.SYS    bootstrapping config (DETECT.EXE replaces on first boot)
    AUTOEXEC.BAT  bootstrapping autoexec (DETECT.EXE replaces on first boot)
    NOS/SYSTEM/   JEMMEX, CTMOUSE, DETECT, NOSMEM, NNET, MEM, mTCP tools...
    NOS/SHELL/    SHELL.EXE, batch wrappers
    NOS/DOCS/     documentation
    APPS/         empty placeholder
    GAMES/        empty placeholder
    USER/         empty placeholder
    TEMP/         empty placeholder

Installer floppy (install.img) contents:
  KERNEL.SYS      FreeDOS kernel
  COMMAND.COM     FreeCOM command interpreter
  JEMMEX.EXE      Memory manager (loaded by CONFIG.SYS for UMB/XMS)
  OAKCDROM.SYS    IDE/ATAPI CD-ROM hardware driver
  SHSUCDX.EXE     CD-ROM drive-letter extension (MSCDEX replacement)
  CONFIG.SYS      Loads JEMMEX + OAKCDROM
  AUTOEXEC.BAT    Loads SHSUCDX, launches D:\\INSTALL.EXE

Requires: nasm, genisoimage (or mkisofs), mtools (mformat, mcopy, mmd, mattrib)
"""

import configparser
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

BUILD_DIR = Path(__file__).parent.resolve()
ROOT_DIR  = BUILD_DIR.parent

config = configparser.ConfigParser()
config.read(BUILD_DIR / "config.ini")

OUT_DIR     = ROOT_DIR / "out"
DIST_DIR    = ROOT_DIR / "dist"
THIRDPARTY  = DIST_DIR / "thirdparty"
FREEDOS_DIR = THIRDPARTY / "freedos"
CDROM_DIR   = THIRDPARTY / "cdrom"
SRC_DIR     = ROOT_DIR / "src"

FLOPPY_IMG  = OUT_DIR / "install.img"   # minimal installer boot floppy
ISO_IMG     = OUT_DIR / "nosdos.iso"    # final installer ISO

# Floppy geometry (1.44 MB)
FLOPPY_SIZE_KB = 1440
FLOPPY_SECTORS = 2880
FLOPPY_HEADS   = 2
FLOPPY_TRACKS  = 80
FLOPPY_SPT     = 18


# ---------------------------------------------------------------------------
# Installer boot floppy CONFIG.SYS and AUTOEXEC.BAT
# ---------------------------------------------------------------------------

INSTALLER_CONFIG_SYS = (
    "REM NOS-DOS Installer Boot\r\n"
    "DOS=HIGH,UMB\r\n"
    "DEVICE=A:\\JEMMEX.EXE NOEMS X=TEST\r\n"
    "DEVICE=A:\\OAKCDROM.SYS /D:MSCD001\r\n"
    "FILES=30\r\n"
    "BUFFERS=10\r\n"
    "STACKS=9,256\r\n"
    "SHELL=COMMAND.COM /P /E:256\r\n"
)

# INSTALLER_AUTOEXEC_BAT is generated dynamically in build_installer_floppy()
# based on which CD-ROM extension (SHSUCDX / MSCDEX) is available.

# ---------------------------------------------------------------------------
# Bootstrapping CONFIG.SYS / AUTOEXEC.BAT installed onto C:\
# DETECT.EXE replaces these with hardware-specific versions on first boot.
# ---------------------------------------------------------------------------

INSTALLED_CONFIG_SYS = (
    "REM NOS-DOS Configuration\r\n"
    "REM Written by NOS-INSTALL. DETECT.EXE updates on first boot.\r\n"
    "DOS=HIGH,UMB\r\n"
    "DEVICE=C:\\NOS\\SYSTEM\\JEMMEX.EXE NOEMS X=TEST\r\n"
    "FILES=40\r\n"
    "BUFFERS=20\r\n"
    "STACKS=9,256\r\n"
    "SHELL=C:\\COMMAND.COM C:\\ /P /E:512\r\n"
)

INSTALLED_AUTOEXEC_BAT = (
    "@ECHO OFF\r\n"
    "REM NOS-DOS Startup\r\n"
    "REM DETECT.EXE will replace this file on first boot.\r\n"
    "SET PROMPT=$P$G\r\n"
    "SET PATH=C:\\;C:\\NOS\\SYSTEM;C:\\NOS\\SHELL;C:\\APPS\r\n"
    "C:\\NOS\\SYSTEM\\DETECT.EXE\r\n"
    "IF EXIST C:\\NOS\\SHELL\\SHELL.EXE C:\\NOS\\SHELL\\SHELL.EXE\r\n"
)

# Marker file — installer searches for this to identify the CD drive letter
NOSCD_ID_CONTENT = (
    b"NOS-DOS Installation Disc\r\n"
    b"This file identifies the NOS-DOS installation CD-ROM.\r\n"
    b"Do not delete.\r\n"
)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def log(msg: str) -> None:
    print(f"[mkiso] {msg}", flush=True)


def find_genisoimage() -> str | None:
    for tool in ("genisoimage", "mkisofs", "xorriso"):
        if shutil.which(tool):
            return tool
    return None


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


def mspec(img: Path) -> str:
    """mtools image specifier for the raw floppy image."""
    return str(img)


# ---------------------------------------------------------------------------
# Step 1: Build minimal installer boot floppy
# ---------------------------------------------------------------------------

def find_cdex() -> tuple[Path | None, str]:
    """Find a CD-ROM extension binary (SHSUCDX or MSCDEX).

    Returns (path, name_on_floppy) or (None, '') if neither is found.
    SHSUCDX is preferred (accepts .COM or .EXE); MSCDEX is a fallback.
    """
    candidates = [
        (CDROM_DIR / "SHSUCDX.COM", "SHSUCDX.COM"),
        (CDROM_DIR / "SHSUCDX.EXE", "SHSUCDX.EXE"),
        (CDROM_DIR / "MSCDEX.EXE",  "MSCDEX.EXE"),
        (CDROM_DIR / "MSCDEX.COM",  "MSCDEX.COM"),
    ]
    for path, name in candidates:
        if path.exists():
            return path, name
    return None, ""


def build_installer_floppy() -> bool:
    """Create install.img: minimal FreeDOS floppy with CD-ROM support."""
    log("Building installer boot floppy...")

    kernel_sys  = FREEDOS_DIR / "KERNEL.SYS"
    command_com = FREEDOS_DIR / "COMMAND.COM"
    sys_com     = FREEDOS_DIR / "SYS.COM"
    boot_asm    = FREEDOS_DIR / "boot.asm"
    jemmex_exe  = THIRDPARTY / "jemmex" / "JEMMEX.EXE"
    oakcdrom    = CDROM_DIR / "OAKCDROM.SYS"

    # SHSUCDX / MSCDEX: optional — build proceeds without it but will warn.
    cdex_path, cdex_name = find_cdex()
    if not cdex_path:
        log("  WARNING: SHSUCDX.EXE and MSCDEX.EXE not found in dist/thirdparty/cdrom/")
        log("  The installer floppy will boot but the CD-ROM may not get a drive letter.")
        log("  Fetch SHSUCDX: run fetch_deps.py, or place SHSUCDX.EXE in dist/thirdparty/cdrom/")

    # Verify hard-required files
    required = [kernel_sys, command_com, sys_com, boot_asm, jemmex_exe, oakcdrom]
    for f in required:
        if not f.exists():
            log(f"ERROR: required file not found: {f}")
            log("Run fetch_deps.py first.")
            return False

    # Assemble boot sector
    boot_bin = OUT_DIR / "boot12.bin"
    result = run(
        ["nasm", "-f", "bin", "-DISFAT12=1",
         str(boot_asm), "-o", str(boot_bin)],
        check=False, cwd=str(boot_asm.parent),
    )
    if result.returncode != 0:
        log("ERROR: NASM failed to assemble boot sector")
        return False
    if boot_bin.stat().st_size != 512:
        log(f"ERROR: boot sector wrong size: {boot_bin.stat().st_size} bytes")
        return False

    # Create and format floppy image
    FLOPPY_IMG.write_bytes(b"\x00" * (FLOPPY_SIZE_KB * 1024))
    result = run(
        ["mformat", "-i", str(FLOPPY_IMG), "-f", "1440",
         "-B", str(boot_bin), "-v", "NOSDOS-INST", "::"],
        check=False,
    )
    if result.returncode != 0:
        log("ERROR: mformat failed")
        return False

    # Copy FreeDOS kernel (must be first file — the boot sector finds it by name)
    for src in [kernel_sys, command_com]:
        result = run(
            ["mcopy", "-i", str(FLOPPY_IMG), str(src), f"::{src.name}"],
            check=False,
        )
        if result.returncode != 0:
            log(f"ERROR: mcopy failed for {src.name}")
            return False

    run(["mattrib", "-i", str(FLOPPY_IMG), "+s", "+h", "+r", "::KERNEL.SYS"],
        check=False)

    # Copy SYS.COM — used by installer to write the FreeDOS boot sector to C:
    # after the file-copy step, guaranteeing C: is bootable regardless of
    # whether FORMAT /S successfully transferred the system.
    result = run(
        ["mcopy", "-i", str(FLOPPY_IMG), str(sys_com), "::SYS.COM"],
        check=False,
    )
    if result.returncode != 0:
        log("ERROR: mcopy failed for SYS.COM")
        return False

    # Copy required drivers
    for src in [jemmex_exe, oakcdrom]:
        result = run(
            ["mcopy", "-i", str(FLOPPY_IMG), str(src), f"::{src.name}"],
            check=False,
        )
        if result.returncode != 0:
            log(f"ERROR: mcopy failed for {src.name}")
            return False

    # Copy CD-ROM extension if available
    if cdex_path:
        result = run(
            ["mcopy", "-i", str(FLOPPY_IMG), str(cdex_path), f"::{cdex_name}"],
            check=False,
        )
        if result.returncode != 0:
            log(f"WARNING: mcopy failed for {cdex_name} — CD drive letter may not be assigned")
            cdex_name = ""  # treat as absent for AUTOEXEC generation

    # Build AUTOEXEC.BAT dynamically based on available cdex binary
    if cdex_name:
        cdex_cmd = f"A:\\{cdex_name} /D:MSCD001 /L:D\r\n"
    else:
        cdex_cmd = "REM CD-ROM extension not available -- add SHSUCDX.EXE to dist/thirdparty/cdrom/\r\n"

    autoexec = (
        "@ECHO OFF\r\n"
        + cdex_cmd +
        "SET PATH=A:\\\r\n"
        "CLS\r\n"
        "D:\\INSTALL.EXE\r\n"
    )

    # Write CONFIG.SYS
    tmp_cfg = OUT_DIR / "_inst_config.sys"
    tmp_cfg.write_bytes(INSTALLER_CONFIG_SYS.encode("ascii"))
    result = run(
        ["mcopy", "-i", str(FLOPPY_IMG), str(tmp_cfg), "::CONFIG.SYS"],
        check=False,
    )
    tmp_cfg.unlink(missing_ok=True)
    if result.returncode != 0:
        log("ERROR: mcopy failed for CONFIG.SYS")
        return False

    # Write AUTOEXEC.BAT
    tmp_bat = OUT_DIR / "_inst_autoexec.bat"
    tmp_bat.write_bytes(autoexec.encode("ascii"))
    result = run(
        ["mcopy", "-i", str(FLOPPY_IMG), str(tmp_bat), "::AUTOEXEC.BAT"],
        check=False,
    )
    tmp_bat.unlink(missing_ok=True)
    if result.returncode != 0:
        log("ERROR: mcopy failed for AUTOEXEC.BAT")
        return False

    size_kb = FLOPPY_IMG.stat().st_size // 1024
    log(f"Installer floppy ready: {FLOPPY_IMG} ({size_kb} KB)")
    return True


# ---------------------------------------------------------------------------
# Step 2: Stage the ISO data area
# ---------------------------------------------------------------------------

def stage_iso_root(iso_root: Path) -> bool:
    """Populate the ISO directory tree in iso_root."""

    # ---- Root-level files ----

    # CD marker: installer detects the disc by searching for this file
    (iso_root / "NOSCD.ID").write_bytes(NOSCD_ID_CONTENT)

    # FORMAT.COM and FDISK.EXE at ISO root (accessible from D:\)
    # FORMAT and FDISK — accept .EXE or .COM; always place on ISO as .EXE
    def find_dos_tool(name: str) -> Path | None:
        for ext in (".EXE", ".COM"):
            p = CDROM_DIR / (name + ext)
            if p.exists():
                return p
        return None

    for iso_name, src in [
        ("FORMAT.EXE", find_dos_tool("FORMAT")),
        ("FDISK.EXE",  find_dos_tool("FDISK")),
    ]:
        if src is not None:
            shutil.copy2(src, iso_root / iso_name)
        else:
            log(f"  WARNING: {iso_name} not found in dist/thirdparty/cdrom/")

    # INSTALL.EXE (compiled by Open Watcom; warn if absent — non-fatal)
    install_exe = SRC_DIR / "install" / "bin" / "INSTALL.EXE"
    if install_exe.exists():
        shutil.copy2(install_exe, iso_root / "INSTALL.EXE")
    else:
        log("  WARNING: INSTALL.EXE not found — run compile.py with Open Watcom")

    # ---- INSTALL\ tree (files that get copied to C:\) ----

    inst_dir = iso_root / "INSTALL"
    inst_dir.mkdir()

    # FreeDOS kernel files → C:\
    for name, src in [
        ("KERNEL.SYS",  FREEDOS_DIR / "KERNEL.SYS"),
        ("COMMAND.COM", FREEDOS_DIR / "COMMAND.COM"),
    ]:
        if src.exists():
            shutil.copy2(src, inst_dir / name)
        else:
            log(f"  WARNING: {name} not found")

    # Bootstrapping CONFIG.SYS and AUTOEXEC.BAT → C:\
    (inst_dir / "CONFIG.SYS").write_bytes(INSTALLED_CONFIG_SYS.encode("ascii"))
    (inst_dir / "AUTOEXEC.BAT").write_bytes(INSTALLED_AUTOEXEC_BAT.encode("ascii"))

    # NOS directory skeleton
    for subdir in ["NOS", "NOS/SYSTEM", "NOS/SHELL", "NOS/DOCS",
                   "APPS", "GAMES", "USER", "TEMP"]:
        (inst_dir / subdir.replace("/", "\\").replace("\\", "/")).mkdir(
            parents=True, exist_ok=True
        )

    nos_sys = inst_dir / "NOS" / "SYSTEM"
    nos_sh  = inst_dir / "NOS" / "SHELL"
    nos_doc = inst_dir / "NOS" / "DOCS"

    # Required system files
    # NOTE: CTMOUSE.EXE intentionally excluded — CuteMouse 2.1 uses LOCK on a
    # register operand (LOCK SHL AX,2) which is #UD on 386+ under JEMMEX V86 mode.
    required_sys = [
        THIRDPARTY / "jemmex"  / "JEMMEX.EXE",
        DIST_DIR   / "config"  / "CONFIG.TPL",
        DIST_DIR   / "config"  / "AUTOEXEC.TPL",
        DIST_DIR   / "config"  / "MTCP.TPL",
        DIST_DIR   / "utils"   / "MEM.EXE",
        DIST_DIR   / "utils"   / "CHKDSK.EXE",
    ]
    for src in required_sys:
        if src.exists():
            shutil.copy2(src, nos_sys / src.name.upper())
        else:
            log(f"  WARNING: {src.name} not found — run fetch_deps.py")

    # Optional compiled NOS-DOS binaries
    optional_sys = [
        SRC_DIR / "detect" / "bin" / "DETECT.EXE",
        SRC_DIR / "mem"    / "bin" / "NOSMEM.EXE",
        SRC_DIR / "net"    / "bin" / "NNET.EXE",
        SRC_DIR / "bridge" / "bin" / "NBRIDGE.EXE",
        SRC_DIR / "play"   / "bin" / "NOSPLAY.EXE",
        SRC_DIR / "npkg"   / "bin" / "NPKG.EXE",
        SRC_DIR / "throttle"/ "bin"/ "THROTTLE.COM",
        SRC_DIR / "throttle"/ "bin"/ "TCTL.EXE",
    ]
    for src in optional_sys:
        if src.exists():
            shutil.copy2(src, nos_sys / src.name.upper())
        else:
            log(f"  INFO: optional binary not built: {src.name}")

    # Shell binaries
    shell_exe = SRC_DIR / "shell" / "bin" / "SHELL.EXE"
    if shell_exe.exists():
        shutil.copy2(shell_exe, nos_sh / "SHELL.EXE")
    else:
        log("  INFO: SHELL.EXE not built (compile step needed)")

    # Batch wrappers
    bat_dir = DIST_DIR / "bat"
    if bat_dir.is_dir():
        for bat in sorted(bat_dir.glob("*.BAT")):
            shutil.copy2(bat, nos_sh / bat.name.upper())

    # mTCP networking tools
    mtcp_tools = [
        "DHCP.EXE", "PING.EXE", "HTGET.EXE", "FTP.EXE",
        "IRCJR.EXE", "TELNET.EXE", "DNSTEST.EXE", "SNTP.EXE",
        "NETCAT.EXE",
    ]
    for tool in mtcp_tools:
        src = THIRDPARTY / "mtcp" / tool
        if src.exists():
            shutil.copy2(src, nos_sys / tool)

    # Documentation
    docs_dir = DIST_DIR / "docs"
    if docs_dir.is_dir():
        for doc in sorted(docs_dir.glob("*.TXT")):
            shutil.copy2(doc, nos_doc / doc.name.upper())

    log(f"ISO data tree staged at {iso_root}")
    return True


# ---------------------------------------------------------------------------
# Step 3: Create the ISO with El Torito boot
# ---------------------------------------------------------------------------

def create_iso(tool: str, iso_root: Path) -> bool:
    """Invoke genisoimage to build the bootable ISO."""
    # Copy installer floppy into the ISO root so genisoimage can embed it
    boot_img_in_root = iso_root / FLOPPY_IMG.name
    shutil.copy2(FLOPPY_IMG, boot_img_in_root)

    result = run(
        [
            tool,
            "-o", str(ISO_IMG),
            "-b", FLOPPY_IMG.name,   # El Torito boot image (floppy emulation)
            "-c", "BOOT.CAT",        # boot catalog
            "-V", "NOS-DOS",         # volume label
            "-J",                    # Joliet (Windows/macOS compatibility)
            # NOTE: Rock Ridge (-r) intentionally omitted.
            # DOS CD-ROM drivers (OAKCDROM.SYS / SHSUCDX) use only the primary
            # ISO 9660 descriptor and do not benefit from Rock Ridge.  Rock Ridge
            # adds System Use Area bytes to every directory record, making entries
            # longer and variable; some older DOS drivers mis-parse these extended
            # records at directory depth > 2, which causes hangs during file reads.
            "-iso-level", "2",       # allows 31-char filenames in primary PVD
            str(iso_root),
        ],
        check=False,
        capture_output=True,
    )

    stderr = result.stderr.decode(errors="replace") if result.stderr else ""

    if result.returncode not in (0, 1):
        log(f"ERROR: {tool} exited {result.returncode}")
        if stderr:
            log(stderr)
        return False

    for line in stderr.splitlines():
        if line.strip():
            log(f"  {line}")

    return True


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> int:
    if not require_tools("nasm", "mformat", "mcopy", "mattrib"):
        return 1

    iso_tool = find_genisoimage()
    if not iso_tool:
        log("ERROR: genisoimage, mkisofs, or xorriso not found")
        return 1

    OUT_DIR.mkdir(parents=True, exist_ok=True)

    # Step 1: minimal installer boot floppy
    if not build_installer_floppy():
        return 1

    # Step 2 + 3: ISO data area + genisoimage
    with tempfile.TemporaryDirectory(prefix="nosdos_iso_") as tmp:
        iso_root = Path(tmp)

        log("Staging ISO data area...")
        if not stage_iso_root(iso_root):
            return 1

        log(f"Creating installer ISO using {iso_tool}...")
        if not create_iso(iso_tool, iso_root):
            return 1

    if not ISO_IMG.exists():
        log(f"ERROR: ISO not created at expected path: {ISO_IMG}")
        return 1

    size_mb = ISO_IMG.stat().st_size / (1024 * 1024)
    log(f"Installer ISO ready: {ISO_IMG} ({size_mb:.1f} MB)")
    log("")
    log("To install NOS-DOS in VirtualBox:")
    log("  1. Create a new VM (Other / DOS, 32 MB RAM)")
    log("  2. Attach nosdos.iso as an IDE optical drive")
    log("  3. Add a new empty virtual hard disk (128 MB+ recommended)")
    log("  4. Set boot order: Optical first, Hard Disk second")
    log("  5. Start the VM and follow the on-screen installer")
    return 0


if __name__ == "__main__":
    sys.exit(main())
