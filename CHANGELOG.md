# Changelog

All notable changes to NOS-DOS are documented here.

Format: `[version/milestone] — date` with Added / Changed / Fixed sections.

---

## [Phase 3 — Tasks 3.1/3.2/3.3/3.4/3.5/3.6 — NNET + networking integration] — 2026-04-01

### Added

**NNET (`src/net/`)**

- `mtcpcfg.h/c` — MTCP.CFG reader:
  - `nos_mtcpcfg_t` struct: ipaddr, netmask, gateway, nameserver (each 20 chars), hostname (64), packetint
  - `nos_mtcpcfg_read(path, cfg)` — space-delimited KEY VALUE parser; PACKETINT parsed as hex
  - `MTCPCFG_PATH` constant: `C:\NOS\SYSTEM\MTCP.CFG`
- `status.h/c` — `NNET STATUS` subcommand (task 3.1):
  - Scans INT 60h–80h for Crynwr packet driver signature (`"PKT DRVR"` at handler+2)
  - Reads MTCP.CFG via nos_mtcpcfg_read(); displays IP/netmask/gateway/DNS/hostname
  - Three states: `CONNECTED`, driver present but no IP (run DHCP), `NO NETWORK`
- `nnet.c` — command router (task 3.2):
  - Routes 10 subcommands: STATUS, DHCP, TIME, PING, WEB, FTP, TELNET, IRC, LOOKUP, CONFIG
  - `spawn_tool()` builds `C:\NOS\SYSTEM\<exe> <args>` and calls `system()`
  - Passes argv[2..] as space-joined args string to tools that take arguments
- `Makefile` — wmake rules: mtcpcfg.obj + status.obj + nnet.obj → NNET.EXE

**Batch wrappers (`dist/bat/`)** (task 3.3):

- `NPING.BAT`, `NWEB.BAT`, `NFTP.BAT`, `NIRC.BAT`, `NTELNET.BAT`, `NTIME.BAT`
  — each calls `C:\NOS\SYSTEM\NNET.EXE <subcommand> %1 %2 %3`

### Changed

**NOS-DETECT (`src/detect/`)**

- `genconf.c` — Phase 3 networking integration (tasks 3.4/3.6):
  - `build_pkt_driver_line()` — when network present, now emits a three-line block:
    `SET MTCPCFG=C:\NOS\SYSTEM\MTCP.CFG\r\nDHCP.EXE >NUL\r\nSNTP.EXE >NUL`
    so the generated AUTOEXEC.BAT configures networking and syncs time automatically
    on every boot; when no driver found, emits empty string (line disappears)
  - `write_mtcpcfg()` — new; writes `C:\NOS\SYSTEM\MTCP.CFG` with `PACKETINT XX\r\nHOSTNAME NOS-DOS\r\n`
    when a packet driver was detected; called from `nos_genconf()` after NOS-HW.CFG is written

**Config templates (`dist/config/`)**

- `AUTOEXEC.TPL` — removed standalone `C:\NOS\SYSTEM\SNTP.EXE >NUL` line; SNTP
  is now part of the `{{PKT_DRIVER_LINE}}` expansion, so it only runs when a
  network adapter is present

**NOS-SHELL (`src/shell/`)**

- `shellcfg.h/c` — added `nos_hwcfg_net_present()` (task 3.5):
  - Opens `C:\NOS\SYSTEM\NOS-HW.CFG`, scans for `[NETWORK]` section, returns 1 if `PRESENT=1`
- `shell.c` — network indicator in header (task 3.5):
  - `g_net_present` global initialised from `nos_hwcfg_net_present()` at startup
  - `draw_header()` appends `"  NET"` before the clock when network is configured;
    `"     "` (5 spaces) when not, keeping header width consistent at 80 chars
  - Ctrl+R now also refreshes `g_net_present` (picks up post-DETECT changes)

**Build system**

- `build/mkhdd.py` — Phase 3 file installs:
  - `src/net/bin/NNET.EXE` added to optional installs (NOS/SYSTEM/)
  - mTCP suite added to optional installs (NOS/SYSTEM/): DHCP, PING, HTGET, FTP, IRCJR, TELNET, DNSTEST, SNTP
  - Batch wrappers (NPING/NWEB/NFTP/NIRC/NTELNET/NTIME.BAT) installed to NOS/SHELL/
- `build/mkimage.py` — `src/net/bin/NNET.EXE` added to optional floppy installs (NOS/SYSTEM/)

**Result:** NNET.EXE = ~8 KB, SHELL.EXE = 31 KB, zero warnings.  Boot test passes in 2.4s.  Phase 3 tasks 3.1–3.6 complete; 3.7 (VM platform testing) deferred.

---

## [Phase 2 — Tasks 2.6/2.7/2.8 — launcher, config, status bar] — 2026-04-01

### Added

**NOS-SHELL (`src/shell/`)**

- `launcher.h/c` — F9 application launcher (task 2.6):
  - Reads `C:\NOS\SHELL\LAUNCHER.CFG` (pipe-delimited `Name|Command` lines;
    `#` comments and blank lines ignored; up to 32 entries)
  - Scrollable selection list in a 50-char dialog with CP437 scroll arrows
  - Enter executes the selected command (`system()`); screen saved/restored
  - If file missing or empty: informational "no entries" dialog
- `shellcfg.h/c` — F2 shell configuration (task 2.7):
  - `nos_cfg_load()` — reads `C:\NOS\SHELL\SHELL.CFG` (key=value format)
  - `nos_cfg_save(sort)` — writes sort preference with CR+LF line ending
  - `nos_cfg_sort_dialog(current)` — 4-option selection dialog (Name / Extension /
    Size / Date); returns new sort mode or -1 on Esc
  - Sort is applied to both panels and persisted across restarts

### Changed

- `shell.c`:
  - `draw_header()` — added drive free space (task 2.8): INT 21h/AH=36h gives
    free clusters × sectors/cluster × bytes/sector; displayed as "XMB free" or
    "XKB free" depending on size; shown to the right of conventional memory
  - `action_launch()` (F9) — now calls `nos_launcher_show()` (real launcher)
  - `action_config()` (F2) — new; calls `nos_cfg_sort_dialog()` and saves result
  - `main()` — loads SHELL.CFG at startup; re-reads both panel dirs if sort
    differs from the default (NOS_SORT_NAME) to apply saved preference immediately
  - F2 wired in dispatch switch
- `Makefile` — added `launcher.obj` and `shellcfg.obj`

**Result:** SHELL.EXE = 31 KB, zero warnings.  Phase 2 fully complete.

---

## [Phase 2 — Tasks 2.4/2.9 + F12 fix] — 2026-04-01

### Added

**NOS-SHELL (`src/shell/`)**

- `dialog.h/c` — modal dialog box system (task 2.9):
  - `nos_dlg_msg(title, msg)` — message box with "Press any key" prompt
  - `nos_dlg_confirm(title, msg)` — yes/no dialog; returns 1 for Y, 0 for N/Esc
  - `nos_dlg_input(title, prompt, buf, maxlen)` — single-line text input with
    editable field (backspace, printable ASCII, Enter/Esc); clamps input to
    visible field width (44 chars) so paths always fit
  - All dialogs: 50-char wide double-line box centred on screen; light-gray
    Norton Commander style; title in top border; key hints in blue
- `viewer.h/c` — quick file viewer triggered by F3 (task 2.4):
  - Loads up to 16 KB of file into static buffer (safe within small model)
  - Text mode: builds line index (up to 1000 lines), displays 23 rows at a time
  - Hex mode (F4 toggle): offset + 16 hex bytes + ASCII column per row
  - Navigation: Up/Down, PgUp/PgDn, Home/End; Esc/F3/Enter exits
  - Status bar: line/row count, byte count, truncation notice if file > 16 KB

### Changed

- `shell.c` — wired all F-key actions and fixed F12 shell-out:
  - `action_view()` (F3): calls `nos_viewer_open()` on the highlighted file
  - `action_enter()`: opens viewer for files (previously just returned path)
  - `action_copy()` (F5): dialog pre-filled with other panel path; appends
    filename if destination ends with `\`; real file I/O via `fread`/`fwrite`
  - `action_move()` (F6): tries `rename()` first (same drive); falls back to
    copy + `remove()` for cross-drive moves; directory move via rename only
  - `action_mkdir()` (F7): input dialog for name; `mkdir()` in current panel path
  - `action_delete()` (F8): confirm dialog; `remove()` for files, `rmdir()` for
    directories (fails with message if directory not empty)
  - `action_launch()` (F9): stub message "No applications configured" — satisfies
    Phase 2 exit criterion; full launcher is task 2.6
  - `dos_shell()` (F12): now functional — calls `nos_scr_restore()`, spawns
    `%COMSPEC%` (or `COMMAND.COM`) with `spawnl(P_WAIT, ...)`, then re-inits
    screen and redraws on return; previously was a no-op
- `Makefile`: added `dialog.obj` and `viewer.obj` to OBJS and dependency rules

**Result:** SHELL.EXE = 28 KB, zero warnings.  All Phase 2 exit criteria met.

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
