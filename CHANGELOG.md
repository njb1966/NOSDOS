# Changelog

All notable changes to NOS-DOS are documented here.

Format: `[version/milestone] — date` with Added / Changed / Fixed sections.

---

## [Phase 2 — Tasks 2.1/2.2/2.3/2.5] — 2026-04-01

### Added

**NOS-SHELL (`src/shell/`)**

- `screen.h/c` — direct video memory renderer (B800:0000 far pointer):
  - `nos_scr_init()`: detects cols via INT 10h/0Fh, rows via INT 10h/1130h, saves cursor shape
  - `nos_scr_restore()`: restores cursor shape and repositions for clean DOS exit
  - `putchar`, `puts`, `putn` (fixed-width with space padding), `fill`, `hline`, `box`, `dbox`
  - `NOS_ATTR(fg,bg)` macro; 16 colour constants; CP437 box-drawing constants (single + double line)
  - `nos_scr_hide_cursor()`: positions cursor off-screen to eliminate redraw flicker
  - C89 fix: `nos_scr_fill()` had mixed declaration/statement — all vars moved to block top
- `input.h/c` — unified keyboard + mouse event model:
  - `nos_inp_poll()` non-blocking, `nos_inp_wait()` blocking; both return `nos_event_t`
  - Keyboard: INT 21h/AH=0Bh for non-blocking status (avoids ZF which WORDREGS lacks);
    INT 16h/AH=00h to read; extended keys encoded as `0x100 | scan`
  - Mouse: INT 33h detect/read; pixel→cell via char width/height; click-edge detection
  - `NOS_KEY_*` constants for F1-F12, arrows, Alt+F1/F2/F4/F7/F10, Ctrl+A/C/D/R/X
- `panel.h/c` — file panel with FindFirst/FindNext:
  - Custom DTA (static `nos_dta_t`); `find_first`/`find_next` use `intdosx` with carry in `r.x.cflag`
  - `..` inserted manually (always first); dirs sorted before files; `qsort` on remaining entries
  - Sort modes: NAME, EXT, SIZE, DATE; scroll-bar indicator on right border
  - `nos_panel_enter()`: descends/ascends directory tree; returns file path for viewer hook
  - `nos_panel_set_drive()`: INT 21h/0Eh drive select; verifies with AH=47h
  - Panel draw: active/inactive colour schemes; fixed-width name+size+date columns
- `shell.c` — main loop:
  - 80×25 layout: header row 0, panels rows 1-22, command row 23, F-key bar row 24
  - Header: path centred, free KB (INT 12h), clock HH:MM:SS (INT 1Ah/00h)
  - F-key bar: 10 labelled keys (F1-F10) with alternating colour
  - Dispatch: Tab switches panel, Ctrl+R refreshes, all navigation keys, F10/Alt+F4 quit
  - F5/F6/F7/F8 action stubs ready for dialog implementation
- `Makefile` — wmake rules for all four objects → `SHELL.EXE`
  - OBJS on single line (wmake on Linux rejects backslash-continuation in OBJS)

**Build integration**
- `build/mkhdd.py` — `SHELL.EXE` added to optional install list (`NOS/SHELL/`)

**Result:** SHELL.EXE = 15 KB, zero warnings. Boot test passes in 2.4s with shell on HDD.

---

## [Pre-Phase-2 HDD infrastructure] — 2026-04-01

### Added

- `build/mkhdd.py` — creates a 31.5 MB FAT16 hard disk image (C: drive) without requiring root:
  - Writes MBR partition table in Python (`struct.pack`): single FAT16 partition at sector 63 (LBA)
  - Formats with `mformat -i image@@32256` (mtools `@@offset` syntax targets the partition area)
  - Geometry: 64 cyl × 16 heads × 63 spt = 64,512 sectors, auto-selects FAT16 (no `-F` flag; that means FAT32 in mtools)
  - Creates directory skeleton: `NOS/SYSTEM/`, `NOS/SHELL/`, `NOS/DOCS/`, `APPS/`, `GAMES/`, `USER/`, `TEMP/`
  - Pre-installs all system files: JEMMEX.EXE, CTMOUSE.EXE, DETECT.EXE (optional), NOSMEM.EXE (optional), CONFIG.TPL, AUTOEXEC.TPL, KERNEL.SYS, COMMAND.COM
  - KERNEL.SYS gets `mattrib +s +h +r` (future HDD-boot support)
- `build/build.py` — added `hdd` stage between `image` and `iso`; `--skip-hdd` flag; updated summary output to show HDD path and size
- `build/config.ini` — added `[hdd]` section with geometry note
- `tests/boot_test.py` — `build_qemu_cmd()` accepts optional `hdd` path; attaches as `-drive file=...,format=raw,index=0,media=disk` (BIOS drive 0x80 → FreeDOS C:); `--hdd`/`--no-hdd` CLI flags; prints HDD status in header
- `build/mkimage.py` — floppy AUTOEXEC.BAT now includes `IF EXIST C:\NOS\SHELL\SHELL.EXE ...` hook so Phase 2 shell launches automatically when present on C:
- `.github/workflows/ci.yml` — boot test timeout raised to 20s; artifact now uploads both `nosdos.iso` and `nosdos.hdd`

### Fixed

- `build/mkhdd.py`: mtools `-F` flag means FAT32, not "force FAT16" — removed; mformat auto-selects FAT16 for ~31 MB volume

---

## [Phase 1 complete] — 2026-04-01

### Added

**NOS-DETECT (`src/detect/`)**
- `memory.c/h` — detects conventional memory (INT 12h), XMS (INT 2Fh/4310h + fn 08h), EMS (INT 67h/42h)
- `video.c/h` — detects VGA/VESA via INT 10h/1A00h and INT 10h/4F00h; reads DCC code, VESA version, current mode
- `mouse.c/h` — detects PS/2 and serial mouse via INT 33h/0000h and INT 33h/0024h; reads button count, driver version, IRQ
- `sound.c/h` — reads `BLASTER` environment variable; falls back to DSP reset probe on ports 0x220/0x240/0x260/0x280
- `network.c/h` — scans INT 60h–80h for Crynwr packet driver signature (`"PKT DRVR"` at handler+2)
- `genconf.c/h` — template substitution engine; reads `CONFIG.TPL`/`AUTOEXEC.TPL`, fills `{{VAR}}` placeholders, writes `CONFIG.SYS`, `AUTOEXEC.BAT`, and `NOS-HW.CFG` (INI-style hardware profile)
- `detect.c` — main orchestrator: first-boot guard (checks `NOS-HW.CFG`), calls all five detection modules, calls `nos_genconf()`, prompts for reboot; `/NOREBOOT` flag skips reboot for testing
- `Makefile` — wmake rules for all seven objects → `DETECT.EXE`

**NOS-MEM (`src/mem/`)**
- `nosmem.c` — memory profile switcher; parses minimal INI subset of `NOS-HW.CFG`; writes `CONFIG.SYS` from `g_profiles[]` table (STD/MAX/EMS/GAME); writes `PROFILE.DAT`; reboots via INT 19h
- `Makefile` — wmake rules for `NOSMEM.EXE`

**Config templates (`dist/config/`)**
- `CONFIG.TPL` — Phase 1 template with `{{JEMMEX_OPTS}}` and `{{MOUSE_LINE}}` substitution variables
- `AUTOEXEC.TPL` — Phase 1 template with `{{BLASTER_LINE}}` and `{{PKT_DRIVER_LINE}}` substitution variables

**Build system**
- `build/compile.py` — fixed three bugs: `check_watcom()` now uses `shutil.which` (wcc returns non-zero for `--version`); PATH now includes `binl64` before `binl` for 64-bit Linux Watcom; INCLUDE now includes `h/dos` for `<dos.h>` and `<conio.h>`
- `build/mkimage.py` — added `install_nos_system_files()`: copies JEMMEX.EXE, CTMOUSE.EXE, CONFIG.TPL, AUTOEXEC.TPL (required), DETECT.EXE, NOSMEM.EXE (optional) into `NOS/SYSTEM/` on floppy; updated `write_config_files()` for Phase 1 boot (loads JEMMEX, runs DETECT.EXE, writes sentinel); fixed mmd/mcopy paths to use forward slashes (mtools on Linux does not accept backslashes in image paths)

**CI / Testing**
- `.github/workflows/ci.yml` — GitHub Actions CI: ubuntu-latest, installs nasm/mtools/genisoimage/qemu-system-x86, caches `dist/thirdparty/` keyed on config.ini + fetch_deps.py hash, runs fetch → build (--skip-compile) → boot test (15 s timeout), uploads ISO artifact for 7 days
- `tests/boot_test.py` — added `check_detection_results()`: parses DETECT.EXE output from COM1 serial stream; checks conventional memory ≥ 560 KB (hard fail); reports XMS, VGA, Mouse, Sound, Network as informational; section-anchored regex patterns disambiguate shared `Status :` key across Mouse/Sound/Network sections

### Fixed
- `compile.py`: `wcc --version` exits non-zero — switched detection to `shutil.which`
- `compile.py`: Watcom 64-bit host binaries in `binl64/`, not `binl/` — PATH ordering corrected
- `compile.py`: `<dos.h>` not found — `INCLUDE` now includes `$WATCOM/h/dos`
- `mkimage.py`: mcopy failed for backslash paths on Linux — all mtools paths use forward slashes
- `mkimage.py`: mmd created directories with backslash names that mcopy with forward slashes couldn't find — both mmd and mcopy now use forward slashes consistently
- `memory.c`: Open Watcom W200 "uninitialized" warnings on inline-asm output variables — initialized to 0 before `__asm {}` block

---

## [Phase 0 complete] — 2026-03-xx

### Added

**Build system**
- `build/fetch_deps.py` — idempotent downloader: FreeDOS kernel + COMMAND.COM + boot.asm + magic.mac (GitHub), JEMMEX v5.86, CTMOUSE (ibiblio), mTCP 2025-01-10 (brutman.com); normalizes mTCP filenames to uppercase
- `build/mkimage.py` — creates 1.44 MB FAT12 floppy: assembles FreeDOS `boot.asm` with NASM (`-DISFAT12=1`, must be exactly 512 bytes), formats with `mformat -B`, copies KERNEL.SYS + COMMAND.COM via `mcopy`, sets sys+hidden+readonly attributes with `mattrib`, writes minimal CONFIG.SYS and AUTOEXEC.BAT
- `build/mkiso.py` — wraps floppy image into El Torito bootable ISO via `genisoimage -b nosdos.img -no-emul-boot` ... with `-boot-load-size 4 -boot-info-table`
  *(actual flags match genisoimage floppy emulation convention)*
- `build/build.py` — master script: fetch → compile → image → iso; flags: `--skip-fetch`, `--skip-compile`, `--only <stage>`
- `build/compile.py` — stub: detects Open Watcom on PATH, logs skip if absent; `wmake`-based per-component build
- `build/config.ini` — all version pins (JEMMEX v5.86, mTCP 2025-01-10) and paths

**Tests**
- `tests/boot_test.py` — headless QEMU boot test: `-display none -serial stdio -no-reboot -no-shutdown`; scans COM1 for `NOS-DOS-READY` sentinel within configured timeout; also accepts raw `C:\>` prompt as fallback; detects known boot failure strings as hard errors; passes in ~2 s on QEMU 7.2 (Debian)

**Skeleton**
- `dist/config/CONFIG.TPL` — minimal Phase 0 CONFIG.SYS skeleton
- `dist/config/AUTOEXEC.TPL` — minimal Phase 0 AUTOEXEC.BAT skeleton
- Directory structure: `src/{detect,mem,shell,npkg,net,bridge,throttle,play}/`, `packages/`, `docs/`
