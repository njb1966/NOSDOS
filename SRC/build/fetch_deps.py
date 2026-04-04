#!/usr/bin/env python3
"""NOS-DOS: Build system
fetch_deps.py - Download and verify third-party dependencies.

Downloads FreeDOS kernel/utilities, JEMMEX, CTMOUSE, and mTCP to
dist/thirdparty/. Idempotent: skips files already present and verified.
All external tools called via subprocess; no third-party Python packages used.
"""

import configparser
import hashlib
import json
import os
import shutil
import subprocess
import sys
import urllib.error
import urllib.request
import zipfile
from pathlib import Path
from typing import Optional

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------

BUILD_DIR = Path(__file__).parent.resolve()
ROOT_DIR = BUILD_DIR.parent
DIST_DIR = ROOT_DIR / "dist" / "thirdparty"

config = configparser.ConfigParser()
config.read(BUILD_DIR / "config.ini")


# ---------------------------------------------------------------------------
# Known-good SHA-256 checksums.
# Populate after a verified first run and commit to pin versions.
# Set to None to skip verification (initial fetch).
# ---------------------------------------------------------------------------

CHECKSUMS: dict[str, Optional[str]] = {
    "boot.asm":           None,   # FreeDOS boot sector source
    "kernel.zip":         None,   # FreeDOS kernel package
    "freecom.zip":        None,   # FreeCOM (COMMAND.COM)
    "ctmouse.zip":        None,   # CuteMouse driver
    "JemmB_v586.zip":     None,   # JEMMEX memory manager
    "mTCP_2025-01-10.zip": None,  # mTCP networking suite
    "gcdrom.zip":         None,   # GCDROM IDE/ATAPI CD-ROM driver
    "shsucdx.zip":        None,   # SHSUCDX MSCDEX replacement
    "format.zip":         None,   # FreeDOS FORMAT utility
    "fdisk.zip":          None,   # FreeDOS FDISK utility
}


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def log(msg: str) -> None:
    print(f"[fetch] {msg}", flush=True)


def sha256_file(path: Path) -> str:
    """Return the SHA-256 hex digest of a file."""
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def verify(path: Path, expected: Optional[str]) -> bool:
    """Return True if file passes checksum or if expected is None."""
    if expected is None:
        return True
    actual = sha256_file(path)
    if actual != expected:
        log(f"CHECKSUM MISMATCH: {path.name}")
        log(f"  expected: {expected}")
        log(f"  actual:   {actual}")
        return False
    return True


def download(url: str, dest: Path, expected_sha256: Optional[str] = None) -> bool:
    """Download url to dest. Skip if already present and verified.

    Returns True on success, False on failure.
    """
    if dest.exists():
        if verify(dest, expected_sha256):
            log(f"  skip (already downloaded): {dest.name}")
            return True
        else:
            log(f"  re-downloading (bad checksum): {dest.name}")
            dest.unlink()

    log(f"  downloading: {url}")
    try:
        req = urllib.request.Request(
            url,
            headers={"User-Agent": "NOS-DOS-Build/0.1 (fetch_deps.py)"},
        )
        with urllib.request.urlopen(req, timeout=120) as resp:
            dest.write_bytes(resp.read())
    except urllib.error.URLError as e:
        log(f"  ERROR downloading {url}: {e}")
        return False

    if not verify(dest, expected_sha256):
        dest.unlink()
        return False

    size_kb = dest.stat().st_size // 1024
    log(f"  saved: {dest.name} ({size_kb}KB)")
    return True


def find_in_zip(archive: Path, filename_upper: str) -> Optional[bytes]:
    """Return raw bytes of the first entry matching filename (case-insensitive)."""
    try:
        with zipfile.ZipFile(archive) as zf:
            for name in zf.namelist():
                if Path(name).name.upper() == filename_upper.upper():
                    return zf.read(name)
    except zipfile.BadZipFile:
        pass
    return None


def extract_zip(archive: Path, dest_dir: Path) -> bool:
    """Extract all files from archive into dest_dir."""
    try:
        with zipfile.ZipFile(archive) as zf:
            zf.extractall(dest_dir)
        return True
    except zipfile.BadZipFile as e:
        log(f"  ERROR extracting {archive}: {e}")
        return False


# ---------------------------------------------------------------------------
# Fetch tasks
# ---------------------------------------------------------------------------

def fetch_boot_sector(dest_dir: Path) -> bool:
    """Download the FreeDOS boot sector source from GitHub.

    boot.asm handles both FAT12 and FAT16 boot sectors.
    magic.mac is a required include file in the same directory.
    Both are assembled by mkimage.py using NASM to produce a 512-byte boot sector.
    """
    dest_dir.mkdir(parents=True, exist_ok=True)

    base = config["freedos"]["kernel_src_url"].rsplit("/", 1)[0]

    ok = download(
        config["freedos"]["kernel_src_url"],
        dest_dir / "boot.asm",
        CHECKSUMS.get("boot.asm"),
    )
    ok = ok and download(
        f"{base}/magic.mac",
        dest_dir / "magic.mac",
        CHECKSUMS.get("magic.mac"),
    )
    return ok


def fetch_freedos_kernel(dest_dir: Path) -> bool:
    """Download the FreeDOS kernel package and extract KERNEL.SYS.

    The package contains KERNL386.SYS (386+ optimized kernel) which we
    rename to KERNEL.SYS — the standard name expected by the boot sector.
    Use KERNL386.SYS for all VM targets (386+ is universally appropriate).
    """
    dest_dir.mkdir(parents=True, exist_ok=True)
    archive = dest_dir / "kernel.zip"
    base_url = config["freedos"]["base_url"].rstrip("/")
    url = f"{base_url}/{config['freedos']['kernel_pkg']}"

    if not download(url, archive, CHECKSUMS.get("kernel.zip")):
        return False

    # Try 386 kernel first, fall back to generic
    kernel_data = find_in_zip(archive, "KERNL386.SYS")
    if kernel_data is None:
        kernel_data = find_in_zip(archive, "KERNEL.SYS")
    if kernel_data is None:
        log("  ERROR: no kernel binary found in kernel.zip")
        log("  Expected: BIN/KERNL386.SYS or BIN/KERNEL.SYS")
        return False

    out = dest_dir / "KERNEL.SYS"
    out.write_bytes(kernel_data)
    log(f"  extracted → KERNEL.SYS ({len(kernel_data)} bytes)")

    # Also extract SYS.COM (useful for manual installs)
    sys_com = find_in_zip(archive, "SYS.COM")
    if sys_com:
        (dest_dir / "SYS.COM").write_bytes(sys_com)
        log(f"  extracted → SYS.COM ({len(sys_com)} bytes)")

    return True


def fetch_freedos_freecom(dest_dir: Path) -> bool:
    """Download FreeCOM and build COMMAND.COM with -fpi87 (no FPU emulation stubs).

    FreeCOM 0.85a is compiled with Watcom FPU emulation (-fpi) by default, which
    generates DA F7 stubs at runtime. These stubs trigger #UD in V86 mode, which
    JEMMEX intercepts instead of reflecting to INT 06h, causing an exception-06
    crash. Rebuilding with -fpi87 eliminates the runtime stubs entirely.

    If Open Watcom (wcc) is not on PATH, falls back to the pre-built binary with
    a warning — that binary will crash under JEMMEX.
    """
    dest_dir.mkdir(parents=True, exist_ok=True)
    archive = dest_dir / "freecom.zip"
    base_url = config["freedos"]["base_url"].rstrip("/")
    url = f"{base_url}/{config['freedos']['freecom_pkg']}"

    if not download(url, archive, CHECKSUMS.get("freecom.zip")):
        return False

    if shutil.which("wcc") is None:
        log("  WARNING: Open Watcom (wcc) not found — using pre-built COMMAND.COM.")
        log("  The pre-built FreeCOM uses Watcom FPU emulation (-fpi) and will")
        log("  crash under JEMMEX V86 mode (exception 06 / DA F7 bug).")
        log("  Install Open Watcom and re-run fetch_deps.py to fix this.")
        command_com = find_in_zip(archive, "COMMAND.COM")
        if command_com is None:
            log("  ERROR: COMMAND.COM not found inside freecom.zip")
            return False
        out = dest_dir / "COMMAND.COM"
        out.write_bytes(command_com)
        log(f"  extracted → COMMAND.COM ({len(command_com)} bytes) [pre-built, JEMMEX-incompatible]")
        return True

    log("  Building FreeCOM from source with -fpi87 (no FPU emulation stubs)...")

    import tempfile
    with tempfile.TemporaryDirectory(prefix="freecom_build_") as build_dir:
        build_path = Path(build_dir)

        # Extract SOURCES.ZIP from inside freecom.zip
        sources_data = find_in_zip(archive, "SOURCES.ZIP")
        if sources_data is None:
            log("  ERROR: SOURCE/FREECOM/SOURCES.ZIP not found in freecom.zip")
            return False

        sources_zip = build_path / "sources.zip"
        sources_zip.write_bytes(sources_data)

        with zipfile.ZipFile(sources_zip) as z:
            z.extractall(build_path)

        # Patch mkfiles/watcom.mak: add -fpi87 to CFLAGS1 and CL
        # -fpi87 = inline x87 instructions without emulation stubs.
        # Eliminates the DA F7 runtime patches that crash under JEMMEX.
        mak_path = build_path / "mkfiles" / "watcom.mak"
        mak = mak_path.read_text()
        mak = mak.replace(
            "CFLAGS1 = -os-s-wx",
            "CFLAGS1 = -os-s-wx-fpi87",
        )
        mak = mak.replace(
            "CL = $(BINPATH)$(DIRSEP)wcl -zq -fo=.obj -bcl=dos",
            "CL = $(BINPATH)$(DIRSEP)wcl -zq -fo=.obj -bcl=dos -fpi87",
        )
        mak_path.write_text(mak)

        # config.mak must exist (copied from config.std)
        (build_path / "config.mak").write_bytes(
            (build_path / "config.std").read_bytes()
        )

        watcom_root = shutil.which("wcc")
        # wcc is at $WATCOM/binl/wcc or $WATCOM/binl64/wcc — resolve root
        watcom_env = dict(os.environ)
        if "WATCOM" not in watcom_env:
            # Infer WATCOM root from wcc path: .../binl/wcc -> ...
            wcc_path = Path(watcom_root).resolve()
            watcom_env["WATCOM"] = str(wcc_path.parent.parent)

        result = subprocess.run(
            ["bash", "build.sh", "wc", "no-xms-swap"],
            cwd=build_path,
            env=watcom_env,
            capture_output=True,
            text=True,
        )

        if result.returncode != 0 or not (build_path / "command.com").exists():
            log("  ERROR: FreeCOM build failed.")
            if result.stdout:
                log("  stdout: " + result.stdout[-400:])
            if result.stderr:
                log("  stderr: " + result.stderr[-400:])
            return False

        command_com_data = (build_path / "command.com").read_bytes()

    out = dest_dir / "COMMAND.COM"
    out.write_bytes(command_com_data)
    log(f"  built → COMMAND.COM ({len(command_com_data)} bytes) [-fpi87, JEMMEX-compatible]")
    return True


def fetch_ctmouse(dest_dir: Path) -> bool:
    """Download CuteMouse and extract CTMOUSE.EXE."""
    dest_dir.mkdir(parents=True, exist_ok=True)
    archive = dest_dir / "ctmouse.zip"
    url = config["ctmouse"]["url"]

    if not download(url, archive, CHECKSUMS.get("ctmouse.zip")):
        return False

    ctmouse_exe = find_in_zip(archive, "CTMOUSE.EXE")
    if ctmouse_exe is None:
        log("  ERROR: CTMOUSE.EXE not found inside ctmouse.zip")
        return False

    out = dest_dir / "CTMOUSE.EXE"
    out.write_bytes(ctmouse_exe)
    log(f"  extracted → CTMOUSE.EXE ({len(ctmouse_exe)} bytes)")
    return True


def fetch_jemmex(dest_dir: Path) -> bool:
    """Download JEMMEX from the pinned GitHub release and extract JEMMEX.EXE.

    Version is pinned in config.ini [jemmex] section.
    Update version, archive_name, and url together when upgrading.
    """
    dest_dir.mkdir(parents=True, exist_ok=True)
    archive_name = config["jemmex"]["archive_name"]
    archive = dest_dir / archive_name
    url = config["jemmex"]["url"]

    if not download(url, archive, CHECKSUMS.get(archive_name)):
        return False

    jemmex_exe = find_in_zip(archive, "JEMMEX.EXE")
    if jemmex_exe is None:
        log("  ERROR: JEMMEX.EXE not found inside Jemm archive")
        return False

    out = dest_dir / "JEMMEX.EXE"
    out.write_bytes(jemmex_exe)
    log(f"  extracted → JEMMEX.EXE ({len(jemmex_exe)} bytes)")
    return True


def fetch_oakcdrom(dest_dir: Path) -> bool:
    """Download GCDROM.SYS — FreeDOS generic IDE/ATAPI CD-ROM driver.

    Needed on the installer boot floppy so the CD-ROM is accessible as
    a drive letter before the installer runs. Uses same /D:MSCD001 syntax
    as the older OAKCDROM.SYS driver. Fetched from the FreeDOS 1.3
    drivers/ directory on ibiblio (not base/).
    """
    dest_dir.mkdir(parents=True, exist_ok=True)
    archive = dest_dir / "gcdrom.zip"
    drivers_url = config["freedos"]["drivers_url"].rstrip("/")
    url = f"{drivers_url}/{config['cdrom_drivers']['gcdrom_pkg']}"

    if not download(url, archive, CHECKSUMS.get("gcdrom.zip")):
        return False

    driver = find_in_zip(archive, "BIN/GCDROM.SYS")
    if driver is None:
        driver = find_in_zip(archive, "GCDROM.SYS")
    if driver is None:
        log("  ERROR: GCDROM.SYS not found inside gcdrom.zip")
        return False

    out = dest_dir / "GCDROM.SYS"
    out.write_bytes(driver)
    log(f"  extracted → GCDROM.SYS ({len(driver)} bytes)")
    return True


def fetch_shsucdx(dest_dir: Path) -> bool:
    """Download SHSUCDX.EXE — freeware MSCDEX replacement for FreeDOS.

    Needed on the installer boot floppy to assign a drive letter to the
    CD-ROM after OAKCDROM.SYS loads the hardware driver.
    """
    dest_dir.mkdir(parents=True, exist_ok=True)
    archive = dest_dir / "shsucdx.zip"
    base_url = config["freedos"]["base_url"].rstrip("/")
    url = f"{base_url}/{config['cdrom_drivers']['shsucdx_pkg']}"

    if not download(url, archive, CHECKSUMS.get("shsucdx.zip")):
        return False

    shsucdx = find_in_zip(archive, "SHSUCDX.COM")
    if shsucdx is None:
        log("  ERROR: SHSUCDX.COM not found inside shsucdx.zip")
        return False

    out = dest_dir / "SHSUCDX.COM"
    out.write_bytes(shsucdx)
    log(f"  extracted → SHSUCDX.COM ({len(shsucdx)} bytes)")
    return True


def fetch_format(dest_dir: Path) -> bool:
    """Download FreeDOS FORMAT.COM.

    Placed at the ISO root so the installer can invoke it to format C:.
    """
    dest_dir.mkdir(parents=True, exist_ok=True)
    archive = dest_dir / "format.zip"
    base_url = config["freedos"]["base_url"].rstrip("/")
    url = f"{base_url}/{config['freedos']['format_pkg']}"

    if not download(url, archive, CHECKSUMS.get("format.zip")):
        return False

    fmt = find_in_zip(archive, "FORMAT.EXE")
    if fmt is None:
        log("  ERROR: FORMAT.EXE not found inside format.zip")
        return False

    out = dest_dir / "FORMAT.EXE"
    out.write_bytes(fmt)
    log(f"  extracted → FORMAT.EXE ({len(fmt)} bytes)")
    return True


def fetch_fdisk(dest_dir: Path) -> bool:
    """Download FreeDOS FDISK.EXE.

    Placed at the ISO root so the user can partition C: if needed before
    running the installer.
    """
    dest_dir.mkdir(parents=True, exist_ok=True)
    archive = dest_dir / "fdisk.zip"
    base_url = config["freedos"]["base_url"].rstrip("/")
    url = f"{base_url}/{config['freedos']['fdisk_pkg']}"

    if not download(url, archive, CHECKSUMS.get("fdisk.zip")):
        return False

    fdisk = find_in_zip(archive, "FDISK.EXE")
    if fdisk is None:
        log("  ERROR: FDISK.EXE not found inside fdisk.zip")
        return False

    out = dest_dir / "FDISK.EXE"
    out.write_bytes(fdisk)
    log(f"  extracted → FDISK.EXE ({len(fdisk)} bytes)")
    return True


def fetch_mtcp(dest_dir: Path) -> bool:
    """Download mTCP networking suite and extract DOS binaries."""
    dest_dir.mkdir(parents=True, exist_ok=True)
    url = config["mtcp"]["url"]
    archive_name = Path(url).name
    archive = dest_dir / archive_name

    if not download(url, archive, CHECKSUMS.get(archive_name)):
        return False

    # mTCP binaries we want on the NOS-DOS image.
    # Names are matched case-insensitively; output files are uppercased.
    wanted = {
        "dhcp.exe":    "DHCP.EXE",     # DHCP client (run at boot)
        "htget.exe":   "HTGET.EXE",    # HTTP downloader (used by NPKG)
        "ping.exe":    "PING.EXE",     # ICMP ping
        "ftp.exe":     "FTP.EXE",      # FTP client
        "ftpclnt.exe": "FTP.EXE",      # alternate name in older releases
        "ircjr.exe":   "IRCJR.EXE",    # IRC client
        "dnstest.exe": "DNSTEST.EXE",  # DNS lookup
        "sntp.exe":    "SNTP.EXE",     # NTP time sync
        "nc.exe":      "NETCAT.EXE",   # Netcat (named nc.exe in 2025 release)
        "netcat.exe":  "NETCAT.EXE",   # alternate name
        "telnet.exe":  "TELNET.EXE",   # Telnet client
    }

    # Extract everything to a temp dir, then copy what we need
    extract_dir = dest_dir / "_mtcp_extract"
    extract_dir.mkdir(exist_ok=True)
    if not extract_zip(archive, extract_dir):
        shutil.rmtree(extract_dir, ignore_errors=True)
        return False

    found = []
    for candidate in extract_dir.rglob("*.exe"):
        key = candidate.name.lower()
        if key in wanted:
            out_name = wanted[key]
            out_path = dest_dir / out_name
            if not out_path.exists():  # first match wins
                shutil.copy2(candidate, out_path)
                found.append(out_name)

    shutil.rmtree(extract_dir, ignore_errors=True)

    if not found:
        # Nothing newly copied — check if files already exist from a prior run
        already = [n for n in set(wanted.values()) if (dest_dir / n).exists()]
        if already:
            log(f"  already present: {', '.join(sorted(already))}")
            return True
        log("  WARNING: no mTCP binaries extracted — check zip structure")
        return False

    log(f"  extracted: {', '.join(found)}")
    return True


def check_pcntpk(dest_dir: Path) -> bool:
    """Verify PCNTPK.COM is present in the vendor directory.

    PCNTPK.COM (AMD PCnet packet driver) cannot be downloaded automatically —
    it must be placed manually at dist/thirdparty/pcntpk/PCNTPK.COM.
    The file is checked into the repository so this should always pass.
    """
    dest_dir.mkdir(parents=True, exist_ok=True)
    path = dest_dir / "PCNTPK.COM"
    if path.exists():
        log(f"  found → PCNTPK.COM ({path.stat().st_size} bytes)")
        return True
    log("  ERROR: PCNTPK.COM not found.")
    log(f"  Place the AMD PCnet packet driver at: {path}")
    return False


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> int:
    log("NOS-DOS dependency fetch starting")
    DIST_DIR.mkdir(parents=True, exist_ok=True)

    steps = [
        ("FreeDOS boot sector (boot.asm)",   lambda: fetch_boot_sector(DIST_DIR / "freedos")),
        ("FreeDOS kernel (KERNEL.SYS)",       lambda: fetch_freedos_kernel(DIST_DIR / "freedos")),
        ("FreeCOM (COMMAND.COM)",             lambda: fetch_freedos_freecom(DIST_DIR / "freedos")),
        ("CuteMouse (CTMOUSE.EXE)",           lambda: fetch_ctmouse(DIST_DIR / "ctmouse")),
        ("JEMMEX",                            lambda: fetch_jemmex(DIST_DIR / "jemmex")),
        ("mTCP networking suite",             lambda: fetch_mtcp(DIST_DIR / "mtcp")),
        ("PCNTPK.COM (PCnet packet driver)",  lambda: check_pcntpk(DIST_DIR / "pcntpk")),
        ("GCDROM.SYS (CD-ROM driver)",        lambda: fetch_oakcdrom(DIST_DIR / "cdrom")),
        ("SHSUCDX.EXE (CD-ROM extensions)",  lambda: fetch_shsucdx(DIST_DIR / "cdrom")),
        ("FORMAT.EXE",                        lambda: fetch_format(DIST_DIR / "freedos")),
        ("FDISK.EXE",                         lambda: fetch_fdisk(DIST_DIR / "freedos")),
    ]

    failures = []
    for label, fn in steps:
        log(f"{label}...")
        if not fn():
            failures.append(label)

    if failures:
        log(f"FAILED: {len(failures)} item(s) could not be fetched:")
        for f in failures:
            log(f"  - {f}")
        log("Check URLs in build/config.ini or verify network connectivity.")
        return 1

    log("All dependencies fetched successfully.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
