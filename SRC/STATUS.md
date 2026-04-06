# NOS-DOS Project Status
Last updated: 2026-04-06

## Platform Focus

**QEMU is the primary development and test target.**
VirtualBox and VMware support is deferred until the QEMU build is complete and stable.

---

## What Is Working (QEMU, as of 2026-04-06)

Full install → boot → shell → packages cycle verified end-to-end under QEMU:

1. `run-qemu.sh` boots installer ISO against blank HDD
2. Installer formats C:, copies files, writes FreeDOS PBR and MBR
3. `run-qemu-hdd.sh` boots installed HDD
4. FreeDOS kernel → JEMMEX → DETECT.EXE → NOS-SHELL splash
5. F-keys (F1–F10, F12) work; F4 Edit launches EDIT.EXE on selected file
6. F9 launcher works; games route through NOSPLAY.EXE
7. NNET STATUS: connected via PCnet + mTCP DHCP
8. NPKG UPDATE: fetches index from nosdos.njb1966.com (29 packages)
9. NPKG INSTALL: downloads, extracts, registers in launcher — tested with Rogue, dBASE IV
10. Game launch: NOSPLAY applies memory/CPU profile; Rogue enters and exits cleanly

---

## Fixes Applied This Session (2026-04-06)

### QEMU run scripts — hardcoded paths
Both `run-qemu.sh` and `run-qemu-hdd.sh` had `/home/nick/projects/retro/NOSDOS/...`
hardcoded. Fixed to use `$SCRIPT_DIR` so scripts work from any location or clone path.

### QEMU display — zoom-to-fit
Switched from `-display sdl` to `-display gtk,zoom-to-fit=on` so the window is freely
resizable. `zoom-to-fit` is a GTK option, not SDL.

### MBR — boot code zeroed
`nosdos.hdd` sector 0 had a valid partition table but all-zero boot code. Wrote a custom
NASM chainloader (`out/mbr.asm` → `mbr.bin`, exactly 446 bytes).

**Critical design note:** INT 13h AH=42h loads the VBR to 0x7C00, overwriting the MBR
including any variables. The DAP must be built on the stack (SP=0x7BE0, below 0x7C00)
so it is not clobbered before the BIOS reads it. DL (drive number) is preserved across
INT 13h and is passed directly to the VBR without re-reading from memory.

### VBR — mtools placeholder, not FreeDOS boot sector
`mformat` left an "MTOO4048" OEM VBR that cannot search the root directory for
KERNEL.SYS. Fixed by grafting `dist/thirdparty/freedos/boot16.bin` boot code (bytes
62–511) onto the existing BPB (bytes 11–61). `BPB_HiddSec` (bytes 28–31) must be 63
(the partition LBA start); this was already correct here.
This is the same logic as `src/install/install.c:nos_write_fat16_pbr()`.

### Missing CONFIG.SYS / AUTOEXEC.BAT after install
The installer's copy step did not land CONFIG.SYS and AUTOEXEC.BAT on C:\.
Extracted from the ISO with `isoinfo` and injected via `mtools mcopy`.
NOS/SYSTEM/ was already fully populated by the installer.

### F4 Edit — not implemented
`shell.c` listed F4=Edit in the F-key bar but had no handler. Added `action_edit()`:
clears screen, `spawnl(P_WAIT, "C:\\NOS\\SYSTEM\\EDIT.EXE", ...)` on the selected file,
reinits screen on return. Wired to `NOS_KEY_F4` in the dispatch loop.

### NPKG — archive.org URLs fail (HTTPS, throttling)
All 25 packages still pointed to archive.org over HTTPS. mTCP HTGET does not support
HTTPS. Updated every affected `.NPKG` to `http://nosdos.njb1966.com/dist/<ID>/<ID>.ZIP`.
Rogue and three others were already hosted; 25 packages updated this session.

### NPKG — no download progress feedback
Large files (dBASE IV took ~4 minutes) gave no indication of progress. Fixed in
`fetch.c`:
- `nos_fetch_archive()`: prints file size before starting
  (`Size: N MB -- large download, please stand by...`)
- `nos_fetch_url()`: added `-v` flag to HTGET for verbose transfer output

### NOSPLAY — unnecessary reboot (profile already active)
NOSPLAY called `NOSMEM /STD` unconditionally. NOSMEM always reboots, so the game never
launched. Fixed in `nosplay.c`: reads `C:\NOS\SYSTEM\PROFILE.DAT` before calling NOSMEM;
if the requested profile is already active, skips NOSMEM entirely. If a profile change
IS needed, NOSMEM still reboots (no queue mechanism yet).

### NOSPLAY — THROTTLE.COM crashes JEMMEX (exception 0E)
THROTTLE.COM hooks INT 08h directly from V86 mode. Under JEMMEX this triggers a
protection fault (exception 0Eh). Fixed in `nosplay.c`: added `vcpi_present()` which
probes INT 67h AH=DEh AL=00h — if a VCPI server responds (AH=00h), THROTTLE installation
is skipped with a warning. This affects all profiles since all four NOSMEM profiles load
JEMMEX. A proper fix (VCPI-aware THROTTLE) is a future task.

---

## Build Commands

```bash
export WATCOM=~/dos/tools/WATCOM
export PATH=$WATCOM/binl64:$PATH
export INCLUDE=$WATCOM/h

# Build individual components
cd src/shell  && wmake -f Makefile   # SHELL.EXE, ADDAPP.EXE
cd src/npkg   && wmake -f Makefile   # NPKG.EXE
cd src/play   && wmake -f Makefile   # NOSPLAY.EXE
cd src/mem    && wmake -f Makefile   # NOSMEM.EXE
cd src/install && wmake -f Makefile  # INSTALL.EXE

# Inject a compiled binary into the running HDD image (QEMU must be closed)
MTOOLSRC=/dev/null mtools -c mdel -i out/nosdos.hdd@@$((63*512)) ::/NOS/SYSTEM/NPKG.EXE
MTOOLSRC=/dev/null mtools -c mcopy -i out/nosdos.hdd@@$((63*512)) \
    src/npkg/bin/NPKG.EXE ::/NOS/SYSTEM/NPKG.EXE

# Rebuild full disk images (blank HDD + ISO)
python3 build/mkhdd.py
python3 build/mkiso.py

# QEMU launch (from anywhere)
bash ~/projects/retro/NOSDOS/SRC/out/run-qemu-hdd.sh   # boot installed HDD
bash ~/projects/retro/NOSDOS/SRC/out/run-qemu.sh        # boot installer ISO
```

---

## Package Hosting Status

All 29 packages now point to `http://nosdos.njb1966.com/dist/<ID>/<ID>.ZIP`.

**Uploaded and verified:**

| ID | Name | Size |
|----|------|------|
| ROGUE | Rogue | 46 KB |
| ROTT | Rise of the Triad SW | 3.6 MB |
| THEDRAW | TheDraw 4.63 | 296 KB |
| DBASE4 | dBASE IV 2.0 | ~2.6 MB |

**Pending upload (files to prepare and upload to server):**

| ID | Category | Upload path |
|----|----------|-------------|
| FOXPRO | databases | /dist/FOXPRO/FOXPRO.ZIP |
| DESCENT | game | /dist/DESCENT/DESCENT.ZIP |
| DOOM | game | /dist/DOOM/DOOM.ZIP |
| DOOM2 | game | /dist/DOOM2/DOOM2.ZIP |
| DUKE3D | game | /dist/DUKE3D/DUKE3D.ZIP |
| HERETIC | game | /dist/HERETIC/HERETIC.ZIP |
| KEEN1 | game | /dist/KEEN1/KEEN1.ZIP |
| PRINCE | game | /dist/PRINCE/PRINCE.ZIP |
| QUAKE | game | /dist/QUAKE/QUAKE.ZIP |
| TYRIAN | game | /dist/TYRIAN/TYRIAN.ZIP |
| WOLF3D | game | /dist/WOLF3D/WOLF3D.ZIP |
| MASM611 | programming | /dist/MASM611/MASM611.ZIP |
| QBASIC | programming | /dist/QBASIC/QBASIC.ZIP |
| TC30 | programming | /dist/TC30/TC30.ZIP |
| TP70 | programming | /dist/TP70/TP70.ZIP |
| ASEASY | spreadsheets | /dist/ASEASY/ASEASY.ZIP |
| LOTUS123 | spreadsheets | /dist/LOTUS123/LOTUS123.ZIP |
| SC | spreadsheets | /dist/SC/SC.ZIP |
| NORTON | utilities | /dist/NORTON/NORTON.ZIP |
| PCTOOLS | utilities | /dist/PCTOOLS/PCTOOLS.ZIP |
| XTREE | utilities | /dist/XTREE/XTREE.ZIP |
| GALAXY | wordprocessing | /dist/GALAXY/GALAXY.ZIP |
| WP51 | wordprocessing | /dist/WP51/WP51.ZIP |
| WSTAR7 | wordprocessing | /dist/WSTAR7/WSTAR7.ZIP |
| XYWRITE | wordprocessing | /dist/XYWRITE/XYWRITE.ZIP |

Once each file is uploaded, set `Bytes=<exact_size>` in the corresponding `.NPKG`
and update the HDD's `NOS/NPKG/DEFS/` copy via mtools.

---

## Known Issues / Future Work

| Issue | Notes |
|-------|-------|
| THROTTLE.COM crashes under JEMMEX | INT 08h hook blocked in V86 mode; VCPI-aware rewrite needed |
| Profile change requires reboot | No post-reboot game queue mechanism; game won't auto-launch after profile switch |
| Installer copy step incomplete | CONFIG.SYS/AUTOEXEC.BAT not copied during install; fixed manually this session — root cause in install.c to be investigated |
| VBox / VMware support | Deferred — QEMU must be complete first |
| GitHub Actions CI (task 0.8) | Still open |
| Bytes= fields missing | 25 packages have Bytes=0 until files are uploaded and measured |

---

## Key Files Reference

| File | Purpose |
|------|---------|
| `out/run-qemu.sh` | Boot installer ISO in QEMU |
| `out/run-qemu-hdd.sh` | Boot installed HDD in QEMU |
| `out/mbr.asm` | Custom MBR chainloader (DAP-on-stack design) |
| `src/install/install.c` | TUI installer; nos_write_fat16_pbr() |
| `src/install/boot16.h` | FreeDOS FAT16 PBR as C byte array |
| `src/shell/shell.c` | Main shell; F-key dispatch including F4 Edit |
| `src/shell/launcher.c` | F9 launcher dialog |
| `src/npkg/fetch.c` | HTGET wrapper; size warning + verbose flag |
| `src/npkg/install.c` | Package install engine |
| `src/play/nosplay.c` | Game launcher; VCPI check; profile skip logic |
| `src/mem/nosmem.c` | Memory profile switcher |
| `packages/` | 29 .NPKG package definitions |
| `packages/packages.idx` | Package index (regenerate with build/mkindex.py) |
