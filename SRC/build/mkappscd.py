#!/usr/bin/env python3
"""Build NOSDOS-APPS data ISO from ~/Downloads/NOSDOS-APPS."""
import shutil
import subprocess
import sys
from pathlib import Path

SRC   = Path("/home/nick/Downloads/NOSDOS-APPS")
STAGE = Path("/tmp/appscd_stage")
OUT   = Path("/home/nick/Downloads/NOSDOS-APPS.iso")


def extract_floppy(img: Path, dest: Path) -> None:
    if not img.exists():
        print(f"  SKIP (not found): {img}")
        return
    dest.mkdir(parents=True, exist_ok=True)
    r = subprocess.run(
        ["mcopy", "-nms", "-i", str(img), "::", str(dest) + "/"],
        capture_output=True, text=True
    )
    if r.returncode not in (0, 1):   # 1 = "No such file or directory" on empty img
        print(f"  WARN mcopy {img.name}: {r.stderr.strip()}")


def copy_loose(src: Path, dest: Path) -> None:
    if not src.exists():
        print(f"  SKIP (not found): {src}")
        return
    shutil.copytree(str(src), str(dest), dirs_exist_ok=True)


# ---- Clean stage ----
if STAGE.exists():
    shutil.rmtree(STAGE)
STAGE.mkdir()

print("[mkappscd] Staging loose-file packages...")

copy_loose(SRC / "DBASE",    STAGE / "DBASE")
copy_loose(SRC / "KEEN",     STAGE / "KEEN")
copy_loose(SRC / "PCTOOLS9", STAGE / "PCTOOLS9")
copy_loose(SRC / "WOLF3D",   STAGE / "WOLF3D")

print("[mkappscd] Extracting FoxPro floppy images...")
for i in range(1, 6):
    extract_floppy(SRC / "FOXPRO" / f"Disk0{i}.img", STAGE / "FOXPRO" / f"DISK{i}")
for i in range(1, 3):
    extract_floppy(SRC / "FOXPRO" / "Connectivity Kit" / f"Disk0{i}.img",
                   STAGE / "FOXPRO" / f"CKIT{i}")
for i in range(1, 4):
    extract_floppy(SRC / "FOXPRO" / "Distribution Kit" / f"Disk0{i}.img",
                   STAGE / "FOXPRO" / f"DKIT{i}")
copy_loose(SRC / "FOXPRO" / "Patches", STAGE / "FOXPRO" / "PATCH")

print("[mkappscd] Extracting Norton Utilities 8 floppy images...")
for i in range(1, 5):
    extract_floppy(SRC / "NU8" / f"Disk0{i}.img", STAGE / "NU8" / f"DISK{i}")

print("[mkappscd] Extracting WordStar 7 floppy images...")
ws7_stage = STAGE / "WS7"
for img in sorted((SRC / "WORDSTAR7").glob("*.IMG")):
    dirname = img.stem[:8].upper()   # WS01, WS02, ... WSLJ4
    extract_floppy(img, ws7_stage / dirname)

print("[mkappscd] Extracting WordPerfect 5.1 floppy images...")
wp51_map = {
    "install.img":  "INSTALL",
    "prog1.img":    "PROG1",
    "printer1.img": "PRNTR1",
    "printer2.img": "PRNTR2",
    "ptrgraph.img": "PTRGPH",
    "spell_th.img": "SPELLTH",
}
for imgname, dirname in wp51_map.items():
    extract_floppy(SRC / "WRDP51" / imgname, STAGE / "WP51" / dirname)

print("[mkappscd] Extracting XTree Gold floppy images...")
for i in range(1, 3):
    extract_floppy(SRC / "XTG301" / f"Disk0{i}.img", STAGE / "XTG" / f"DISK{i}")

# ---- Write README ----
readme = STAGE / "README.TXT"
readme.write_text(
    "NOSDOS-APPS DATA CD\r\n"
    "===================\r\n"
    "\r\n"
    "LOOSE FILES (copy or run directly):\r\n"
    "  D:\\DBASE\\     dBASE IV 2.0          (52 files)\r\n"
    "  D:\\KEEN\\      Commander Keen Ep.1   (run KEEN1.EXE)\r\n"
    "  D:\\PCTOOLS9\\  PC Tools 9            (run PCINSTL.EXE or INSTALL.EXE)\r\n"
    "  D:\\WOLF3D\\    Wolfenstein 3D Sw.    (run INSTALL.EXE)\r\n"
    "\r\n"
    "FLOPPY-BASED INSTALLERS (run INSTALL.EXE, point source to D:\\APP\\DISK1):\r\n"
    "  D:\\FOXPRO\\DISK1..5  FoxPro 2.0  (also CKIT1-2, DKIT1-3, PATCH)\r\n"
    "  D:\\NU8\\DISK1..4     Norton Utilities 8\r\n"
    "  D:\\WS7\\WS01..WS20   WordStar 7.0 (21 disks)\r\n"
    "  D:\\WP51\\INSTALL..   WordPerfect 5.1\r\n"
    "  D:\\XTG\\DISK1..2     XTree Gold\r\n"
    "\r\n"
    "INSTALL PATHS: Use C:\\APPS\\<NAME> for utilities, C:\\GAMES\\<NAME> for games.\r\n"
)

print("[mkappscd] Stage size:")
r = subprocess.run(["du", "-sh", str(STAGE)], capture_output=True, text=True)
print(f"  {r.stdout.strip()}")

print("[mkappscd] Creating ISO...")
subprocess.run([
    "genisoimage",
    "-o", str(OUT),
    "-V", "NOSDOS_APPS",
    "-J",                       # Joliet (long names on host)
    "-joliet-long",             # allow up to 103-char Joliet names
    "-r",                       # Rock Ridge (permissions on Linux host)
    "-D",                       # relaxed deep-dir checking
    str(STAGE),
], check=True)

size_mb = OUT.stat().st_size // (1024 * 1024)
print(f"[mkappscd] Done: {OUT}  ({size_mb} MB)")
