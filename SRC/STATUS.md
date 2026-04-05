# NOS-DOS Project Status
Last updated: 2026-04-04

## Where We Are

Full installer → boot cycle is working end-to-end in VirtualBox:
1. Boot nosdos.iso from blank VM
2. Installer runs, FORMATs C:, copies files, writes FreeDOS PBR, writes MBR
3. Remove ISO, reboot → FreeDOS boots cleanly from HDD
4. NOS-SHELL launches → F9 launcher works → ADDAPP.EXE adds apps → verified with Rogue

## What Was Just Fixed (this session)

### 1. Post-install boot failure (.Error!)
**Root cause:** `mformat` writes `BPB_HiddSec=0` when using the `@@offset` image syntax.
The FreeDOS partition boot record uses `BPB_HiddSec` to compute the absolute LBA of the
root directory. With 0 instead of 63, it read the FAT area instead of the root directory,
couldn't find `KERNEL.SYS`, and printed `.Error!`.

**Fixes:**
- `build/mkhdd.py`: added `-H 63` to both `mformat` calls
- `src/install/install.c`: `nos_write_fat16_pbr()` now explicitly patches `BPB_HiddSec`
  (bytes 28-31) to 63 after reading the existing PBR sector, defensive against FORMAT.EXE
  copying the wrong value

**New installer code (install.c):**
- `nos_dap_t` / `nos_write_fat16_pbr()`: uses INT 13h AH=42h/43h to read-modify-write
  LBA 63; preserves FORMAT's BPB bytes 3-61, overlays boot16_code jump + boot logic
- `boot16.h`: 512-byte FreeDOS FAT16 PBR embedded as `boot16_code[]` array
  (compiled with `nasm -f bin -DISFAT16=1 boot.asm -o boot16.bin`)

### 2. ADDAPP pipe corruption
**Root cause:** DOS `COMMAND.COM` interprets `|` in expanded variables as pipe operators.
`ECHO %NOS_APP%` where `NOS_APP=Rogue|C:\GAMES\ROGUE|...` tried to pipe to the path.
Only the label was written to `LAUNCHER.CFG`; dir and exec were lost.

**Fix:** `src/shell/addapp.c` → `ADDAPP.EXE`
Replaces `dist/bat/ADDAPP.BAT`. Opens `C:\NOS\SHELL\LAUNCHER.CFG` in append mode and
writes `Label|Dir|Exec\r\n` directly via `fprintf`. No shell interpretation.

## Current Build Commands

```bash
# In retrodev distrobox:
export WATCOM=~/dos/tools/WATCOM
export PATH=$WATCOM/binl64:$PATH
export INCLUDE=$WATCOM/h

# Compile everything
python3 build/compile.py

# Or just the shell (includes ADDAPP.EXE now):
cd src/shell && wmake -f Makefile

# Rebuild disk images:
python3 build/mkhdd.py
python3 build/mkiso.py

# Rebuild VMDK for VirtualBox:
VBoxManage convertfromraw out/nosdos-blank.hdd out/nosdos-blank.vmdk --format VMDK
# or fallback:
qemu-img convert -f raw -O vmdk out/nosdos-blank.hdd out/nosdos-blank.vmdk
```

## Launcher System (FULLY WORKING)

**LAUNCHER.CFG** at `C:\NOS\SHELL\LAUNCHER.CFG`:
```
# NOS-DOS Launcher Configuration
# Format: Label|Dir|Exec  (3-field)
# Or:     Label|Exec      (2-field, no chdir)
Rogue|C:\GAMES\ROGUE|C:\GAMES\ROGUE\ROGUE.EXE
```

**ADDAPP.EXE** (`NOS/SHELL/ADDAPP.EXE`):
```
ADDAPP Rogue C:\GAMES\ROGUE C:\GAMES\ROGUE\ROGUE.EXE
```

**NPKG INSTALL** auto-populates with `#NPKG:<id>` marker + entry.
Games route through `NOSPLAY.EXE <id>` for CPU/memory/sound profile management.

**F9 in NOS-SHELL**: reads config fresh each open, scrollable list, Enter launches,
chdir + system() to run, shell repaints on return.

## Next Steps

### Immediate: NPKG Package Hosting
The package manager is built and working. The blocker is that ZIP archives aren't
uploaded yet. When ready:

1. Verify each ZIP in `SRC/staging/` (contents match what .npkg expects)
2. Upload ZIPs to `http://nosdos.njb1966.com/dist/<package>/`
3. Update `URL1=` in each `packages/<category>/<name>.NPKG`
4. Run `python3 build/mkindex.py` to regenerate `packages/packages.idx`
5. Commit and push → GitHub Actions deploys to server automatically
6. Test: `NPKG UPDATE` then `NPKG INSTALL DOOM` in a live VM

### Package list to prep:
- **Games (shareware):** DOOM, Wolf3D, Duke3D, Heretic, Commander Keen ep1,
  Descent, Quake, Tyrian 2000, Rogue (already works locally)
- **Productivity:** Galaxy Write, QBasic 1.1, As-Easy-As, SC spreadsheet
- **Programming:** Turbo Pascal 7, Turbo C 3.0
- **Commercial (from floppies):** user is preparing these from physical media

### After NPKG hosting:
- Phase 4: NOS-DETECT refinement (hardware detection on first boot)
- NOSPLAY.EXE: test with actual games once packages are hosted
- CI: GitHub Actions for automated build + boot test (task 0.8, still open)

## Key Files Reference

| File | Purpose |
|------|---------|
| `src/install/install.c` | TUI installer; nos_write_fat16_pbr() |
| `src/install/boot16.h` | FreeDOS FAT16 PBR as C byte array |
| `src/shell/launcher.c` | F9 launcher dialog; lau_load/lau_exec |
| `src/shell/addapp.c` | ADDAPP.EXE — appends to LAUNCHER.CFG |
| `dist/shell/LAUNCHER.CFG` | Default (empty) launcher config shipped in images |
| `dist/bat/` | Other batch wrappers (NPING, NWEB, etc.) — ADDAPP.BAT removed |
| `build/mkhdd.py` | Creates nosdos.hdd + nosdos-blank.hdd |
| `build/mkiso.py` | Creates nosdos.iso (El Torito installer) |
| `packages/` | .NPKG package definitions (29 packages) |
| `packages/packages.idx` | Package index (regenerate with mkindex.py) |
