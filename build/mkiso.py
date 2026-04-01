#!/usr/bin/env python3
"""NOS-DOS: Build system
mkiso.py - Create a bootable El Torito ISO from the floppy disk image.

The floppy image (nosdos.img) is embedded as the El Torito boot image
using 1.44MB floppy emulation. This is the broadest-compatible boot
method for BIOS-based VMs (VirtualBox, VMware, QEMU).

Additional files placed in the ISO root:
  /README.TXT    - brief description
  /NOS/          - NOS-DOS documentation and skeleton (read-only reference)

Requires: genisoimage (or mkisofs — both supported)
"""

import configparser
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

BUILD_DIR = Path(__file__).parent.resolve()
ROOT_DIR = BUILD_DIR.parent

config = configparser.ConfigParser()
config.read(BUILD_DIR / "config.ini")

OUT_DIR = ROOT_DIR / "out"
DIST_DIR = ROOT_DIR / "dist"

FLOPPY_IMG = OUT_DIR / "nosdos.img"
ISO_IMG = OUT_DIR / "nosdos.iso"

README_TXT = b"""\
NOS-DOS (NostalgicDOS)
======================
Boot fast. Work clean. Remember when computing just worked?

NOS-DOS is a curated FreeDOS distribution for virtual machines.
This ISO boots directly to the NOS-DOS environment.

For more information: https://github.com/your-org/nosdos
"""


def log(msg: str) -> None:
    print(f"[mkiso] {msg}", flush=True)


def find_genisoimage() -> str | None:
    """Return the path to genisoimage or mkisofs, whichever is available."""
    for tool in ("genisoimage", "mkisofs", "xorriso"):
        if shutil.which(tool):
            return tool
    return None


def run(cmd: list[str], check: bool = True, **kwargs) -> subprocess.CompletedProcess:
    log(f"  $ {' '.join(str(c) for c in cmd)}")
    return subprocess.run(cmd, check=check, **kwargs)


def build_iso_root(tmp_dir: Path) -> bool:
    """Assemble the ISO directory tree in tmp_dir."""
    # README at ISO root
    (tmp_dir / "README.TXT").write_bytes(README_TXT)

    # Copy on-disk docs if they exist (populated in later phases)
    docs_src = DIST_DIR / "docs"
    if docs_src.exists():
        docs_dst = tmp_dir / "DOCS"
        docs_dst.mkdir()
        for f in docs_src.glob("*"):
            shutil.copy2(f, docs_dst / f.name.upper())

    return True


def create_iso_genisoimage(tool: str, iso_root: Path) -> bool:
    """Create ISO using genisoimage (or mkisofs)."""
    result = run(
        [
            tool,
            "-o", str(ISO_IMG),
            "-b", str(FLOPPY_IMG.name),  # El Torito boot image (in ISO root)
            "-c", "BOOT.CAT",            # boot catalog
            "-no-emul-boot",             # no emulation — but we use floppy image
            # Actually: for 1.44MB floppy emulation, omit -no-emul-boot
            # and set the media type correctly. Overriding below.
            "-V", "NOS-DOS",             # volume label
            "-J",                        # Joliet extensions (Windows-compatible)
            "-r",                        # Rock Ridge extensions (long names on Linux)
            "-iso-level", "2",
            str(iso_root),
        ],
        check=False,
        capture_output=True,
    )
    # genisoimage exits non-zero on warnings sometimes; check stderr
    if result.returncode not in (0, 1):
        log(f"ERROR: {tool} exited {result.returncode}")
        if result.stderr:
            log(result.stderr.decode(errors="replace"))
        return False
    return True


def create_iso_with_floppy_boot(tool: str, iso_root: Path) -> bool:
    """Create a bootable ISO using the floppy image as El Torito 1.44MB boot."""
    # Copy floppy image into the ISO root so genisoimage can find it
    boot_image_in_root = iso_root / FLOPPY_IMG.name
    shutil.copy2(FLOPPY_IMG, boot_image_in_root)

    # For floppy emulation El Torito, genisoimage needs:
    #   -b <boot image relative to ISO root>
    #   NO -no-emul-boot flag (floppy emulation is default when image matches floppy size)
    result = run(
        [
            tool,
            "-o", str(ISO_IMG),
            "-b", FLOPPY_IMG.name,
            "-c", "BOOT.CAT",
            "-V", "NOS-DOS",
            "-J",
            "-r",
            "-iso-level", "2",
            str(iso_root),
        ],
        check=False,
        capture_output=True,
    )

    stdout = result.stdout.decode(errors="replace") if result.stdout else ""
    stderr = result.stderr.decode(errors="replace") if result.stderr else ""

    if result.returncode not in (0, 1):
        log(f"ERROR: {tool} exited {result.returncode}")
        if stderr:
            log(stderr)
        return False

    # Log any warnings
    for line in stderr.splitlines():
        if line.strip():
            log(f"  {line}")

    return True


def main() -> int:
    tool = find_genisoimage()
    if not tool:
        log("ERROR: genisoimage, mkisofs, or xorriso not found")
        return 1

    if not FLOPPY_IMG.exists():
        log(f"ERROR: floppy image not found: {FLOPPY_IMG}")
        log("Run mkimage.py first.")
        return 1

    OUT_DIR.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="nosdos_iso_") as tmp:
        iso_root = Path(tmp)
        log("Building ISO directory tree...")
        if not build_iso_root(iso_root):
            return 1

        log(f"Creating bootable ISO using {tool}...")
        if not create_iso_with_floppy_boot(tool, iso_root):
            return 1

    if not ISO_IMG.exists():
        log(f"ERROR: ISO not created at expected path: {ISO_IMG}")
        return 1

    size_mb = ISO_IMG.stat().st_size / (1024 * 1024)
    log(f"ISO ready: {ISO_IMG} ({size_mb:.1f} MB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
