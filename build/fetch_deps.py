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
    """Download FreeCOM (COMMAND.COM replacement) and extract COMMAND.COM."""
    dest_dir.mkdir(parents=True, exist_ok=True)
    archive = dest_dir / "freecom.zip"
    base_url = config["freedos"]["base_url"].rstrip("/")
    url = f"{base_url}/{config['freedos']['freecom_pkg']}"

    if not download(url, archive, CHECKSUMS.get("freecom.zip")):
        return False

    command_com = find_in_zip(archive, "COMMAND.COM")
    if command_com is None:
        log("  ERROR: COMMAND.COM not found inside freecom.zip")
        return False

    out = dest_dir / "COMMAND.COM"
    out.write_bytes(command_com)
    log(f"  extracted → COMMAND.COM ({len(command_com)} bytes)")
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
        log("  WARNING: no mTCP binaries extracted — check zip structure")
        return False

    log(f"  extracted: {', '.join(found)}")
    return True


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
