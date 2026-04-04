# NOS-DOS Development Plan

## Milestone Overview

```
Phase 0: Foundation          [Weeks 1-2]    Build system + bootable FreeDOS base
Phase 1: Detection & Memory  [Weeks 3-5]    NOS-DETECT + NOS-MEM + auto-config
Phase 2: The Shell           [Weeks 6-10]   NOS-SHELL core functionality
Phase 3: Networking          [Weeks 11-13]  NNET wrappers + integrated connectivity
Phase 4: Package Manager     [Weeks 14-17]  NPKG + initial package definitions
Phase 5: Host Bridge         [Weeks 18-20]  NOS-BRIDGE file exchange + printing
Phase 6: Game Support        [Weeks 21-23]  NOS-PLAY + NOS-THROTTLE
Phase 7: Polish & Release    [Weeks 24-26]  Documentation, testing, VM images
```

---

## Phase 0: Foundation [Weeks 1-2]

**Goal:** Reproducible build system that produces a bootable FreeDOS disk image with a clean directory structure.

### Tasks

- [x] **0.1** Set up repository structure per CLAUDE.md spec
- [x] **0.2** Write `build/fetch_deps.py` — downloads FreeDOS kernel, FreeCOM, JEMMEX, CTMOUSE, mTCP from official sources
  - Idempotent (skips already-downloaded files)
  - Checksum slots defined; populated on first verified run
  - Stores in `dist/thirdparty/{freedos,ctmouse,jemmex,mtcp}/`
- [x] **0.3** Write `build/mkimage.py` — creates a bootable FAT12 floppy image (1.44MB)
  - **NOTE:** Intentionally deferred 504MB FAT16 hard disk to Phase 1. Phase 0 uses a 1.44MB floppy image embedded as El Torito boot — simpler, no loopback/root required, proves the pipeline end-to-end. Hard disk image is task 1.x.
  - Assembles FreeDOS `boot.asm` + `magic.mac` with NASM (`-DISFAT12=1`)
  - Formats with `mformat -B`, installs kernel + shell via `mcopy`, sets attributes with `mattrib`
  - AUTOEXEC.BAT writes `NOS-DOS-READY > COM1` as boot sentinel for the test harness
- [x] **0.4** Write `build/mkiso.py` — creates bootable El Torito ISO (floppy emulation, 1.44MB)
- [x] **0.5** Write `build/build.py` — master script: fetch → compile → image → iso
  - Flags: `--skip-fetch`, `--skip-compile`, `--only <stage>`
- [x] **0.6** Write `tests/boot_test.py` — boots ISO in QEMU headless, captures COM1 serial output, detects `NOS-DOS-READY` sentinel within 10 seconds; **PASSING in ~2s**
- [x] **0.7** Create `build/compile.py` stub — detects Open Watcom on PATH, logs skip if absent; ready to invoke `wmake` per component when Watcom is installed
- [x] **0.8** Create GitHub Actions CI workflow: fetch → build → boot test

### Exit Criteria

- [x] `python build/build.py` produces a bootable ISO
- [x] ISO boots in QEMU and reaches working DOS environment (sentinel detected in ~2s)
- [ ] ISO boots in VirtualBox and reaches `C:\>` prompt *(not yet tested)*
- [x] CI pipeline passes

### Phase 2 Exit Criteria

- [x] Shell compiles clean (zero warnings), SHELL.EXE = 15 KB
- [x] SHELL.EXE present on C:\NOS\SHELL\ in HDD image
- [x] Boot test passes with HDD + shell in 2.4s
- [x] Shell renders dual panels visually *(verified via QEMU GTK display)*
- [x] Can navigate directories, F5/F6/F7/F8 dialogs functional
- [x] F12 drops to DOS prompt and returns on EXIT
- [ ] 620 KB+ conventional memory with shell loaded *(manual verification pending)*

---

## Phase 1: Detection & Memory [Weeks 3-5]

**Goal:** System auto-detects hardware and configures memory/drivers without user intervention.

### Tasks

- [x] **1.1** Implement `src/detect/memory.c` — detect total conventional, XMS, and EMS memory via INT 12h, XMS driver calls, EMS driver calls
- [x] **1.2** Implement `src/detect/video.c` — detect video adapter type (VGA/SVGA/VESA) via INT 10h and VESA BIOS calls
- [x] **1.3** Implement `src/detect/mouse.c` — detect mouse presence via INT 33h
- [x] **1.4** Implement `src/detect/sound.c` — detect Sound Blaster by checking BLASTER env var, then probing standard ports (220h, 240h)
- [x] **1.5** Implement `src/detect/network.c` — detect packet driver presence at INT 60h-80h
- [x] **1.6** Implement `src/detect/genconf.c` — generates CONFIG.SYS and AUTOEXEC.BAT from detected hardware
  - Templates in `dist/config/CONFIG.TPL` and `AUTOEXEC.TPL`
  - Fills in correct driver paths, IRQs, memory settings
  - Generates `NOS\SYSTEM\NOS-HW.CFG` master hardware profile
- [x] **1.7** Implement `src/detect/detect.c` — main orchestrator, calls all detection modules, writes config, displays summary screen
- [x] **1.8** Implement `src/mem/nosmem.c` — memory profile switcher
  - `NOSMEM /STD` — standard profile (all TSRs, 620KB+ target)
  - `NOSMEM /MAX` — maximum conventional (strips non-essential TSRs, 635KB+ target)
  - `NOSMEM /EMS` — EMS emphasis (configures JEMMEX to provide EMS)
  - `NOSMEM /GAME` — game profile (sound + mouse only, maximum conventional)
  - Modifies CONFIG.SYS and prompts for reboot
- [x] **1.9** Integrate NOS-DETECT into boot sequence — runs on first boot, writes config, reboots into configured system
- [x] **1.10** Add detection results to boot test — verify conventional memory, VGA, and hardware status reported from COM1 serial output

### Exit Criteria

- [x] First boot: NOS-DETECT runs, detects VM hardware, generates configs; output visible on COM1
- [x] JEMMEX loads successfully; 639 KB conventional memory free in QEMU (exceeds 560 KB minimum)
- [x] Boot test parses and reports: conventional KB, XMS KB, VGA adapter, mouse/sound/network status
- [ ] `NOSMEM /MAX` reconfigures and achieves 635 KB+ *(manual verification, deferred to Phase 2 milestone)*
- [ ] All detection works in VirtualBox and VMware *(not yet tested)*

### Pre-Phase-2 Infrastructure

- [x] **1.x** Create `build/mkhdd.py` — 31.5 MB FAT16 hard disk image with MBR partition table
  - Python-written MBR (struct-packed partition entry, LBA addressing)
  - `mformat @@offset` for in-place FAT16 format without root access
  - Directory skeleton: `NOS/SYSTEM/`, `NOS/SHELL/`, `NOS/DOCS/`, `APPS/`, `GAMES/`, `USER/`, `TEMP/`
  - Pre-installs: JEMMEX.EXE, CTMOUSE.EXE, DETECT.EXE, NOSMEM.EXE, CONFIG.TPL, AUTOEXEC.TPL, KERNEL.SYS, COMMAND.COM
  - QEMU: attached as `-drive index=0,media=disk` → BIOS drive 0x80 → FreeDOS C:
- [x] Integrated `hdd` stage into `build/build.py` (between `image` and `iso`); added `--skip-hdd` flag
- [x] Updated `tests/boot_test.py` — auto-detects and attaches HDD; `--no-hdd` escapes to floppy-only mode
- [x] Updated `build/mkimage.py` AUTOEXEC.BAT — adds `IF EXIST C:\NOS\SHELL\SHELL.EXE` launch hook for Phase 2+
- [x] Boot test passes in 2.4s with HDD attached; DETECT.EXE writes to C:\ during the session

---

## Phase 2: The Shell [Weeks 6-10]

**Goal:** Norton Commander-style dual-pane TUI that serves as the primary user interface.

### Tasks

- [x] **2.1** Implement `src/shell/screen.c` — direct video memory rendering engine
  - Far pointer to B800:0000; all drawing bypasses INT 10h except init/cursor
  - `nos_scr_init()` detects mode via INT 10h/0Fh and INT 10h/1130h; saves/restores cursor shape
  - `putchar`, `puts`, `putn`, `fill`, `hline`, `box`, `dbox`; attribute macro `NOS_ATTR(fg,bg)`
  - 80×25 and 80×50 text mode support; CP437 box-drawing character constants
- [x] **2.2** Implement `src/shell/input.c` — keyboard and mouse input
  - Unified event model: `nos_inp_poll()` returns KEY, MOUSE, or NONE without blocking
  - Keyboard: INT 21h/0Bh for non-blocking peek (avoids ZF dependency); INT 16h/00h to read
  - Extended keys encoded as `0x100 | scan_code` matching `NOS_KEY_*` constants
  - Mouse: INT 33h/0000h detect, INT 33h/0003h read; pixel→cell conversion; click-edge detection
- [x] **2.3** Implement `src/shell/panel.c` — dual-pane file panel
  - INT 21h FindFirst/FindNext (AH=4Eh/4Fh) with custom DTA
  - Sorts: name, ext, size, date; `..` always first, dirs before files
  - Scroll bar indicator; `nos_panel_enter()` navigates directories and returns file paths
  - `nos_panel_set_drive()` via INT 21h/0Eh; file size/date formatting in panel rows
- [x] **2.4** Implement `src/shell/viewer.c` — quick file viewer (F3)
- [x] **2.5** Implement `src/shell/shell.c` — main shell loop
  - 80×25 layout: header (row 0), dual panels (rows 1-22), command row (23), F-key bar (24)
  - Header shows path, free conventional memory (INT 12h), live clock (INT 1Ah)
  - F-key bar: F1-F10 labels; F5 Copy, F6 Move, F7 MkDir, F8 Del (stubs), F10/Alt+F4 Quit, F12 shell-out
  - Tab switches active panel; Ctrl+R refreshes; arrow/PgUp/PgDn/Home/End navigate
- [x] **2.6** Implement `src/shell/launcher.c` — application launcher (F9)
- [x] **2.7** Implement `src/shell/shellcfg.c` — shell configuration (F2): sort order dialog + SHELL.CFG persistence
- [x] **2.8** Status bar enhancements — drive free space in header (INT 21h/36h)
- [x] **2.9** Dialog system — modal confirmations, text input, list selection
- [x] **2.10** Integrate shell into boot — AUTOEXEC.BAT IF EXIST hook for C:\NOS\SHELL\SHELL.EXE

### Exit Criteria

- Shell boots and displays dual panes with file listings
- Can navigate directories, view files, copy/move/delete
- F9 shows launcher (even if empty initially)
- F12 drops to DOS prompt, EXIT returns to shell
- Status bar shows live memory and clock
- Keyboard and mouse both work
- Responsive — no perceptible lag on any operation

---

## Phase 3: Networking [Weeks 11-13]

**Goal:** Wrapper commands that make mTCP networking feel integrated and easy.

### Tasks

- [x] **3.1** Implement `src/net/status.c` — `NNET STATUS`
  - Reads MTCP.CFG via nos_mtcpcfg_read(); scans INT 60h-80h for Crynwr packet driver signature
  - Three states: CONNECTED (pkt+IP), driver-no-IP (run DHCP), NO NETWORK
- [x] **3.2** Implement `src/net/nnet.c` — command router
  - `NNET STATUS` → nos_status_show()
  - `NNET DHCP` → DHCP.EXE; `NNET TIME` → SNTP.EXE; `NNET CONFIG` → EDIT.COM
  - `NNET PING/WEB/FTP/TELNET/IRC/LOOKUP` → corresponding mTCP tool in C:\NOS\SYSTEM\
- [x] **3.3** Create batch wrappers: NPING.BAT, NWEB.BAT, NFTP.BAT, NIRC.BAT, NTELNET.BAT, NTIME.BAT
- [x] **3.4** Auto-time-sync on boot — `{{PKT_DRIVER_LINE}}` in AUTOEXEC.TPL expands to SET MTCPCFG + DHCP + SNTP when network present
- [x] **3.5** Network status in NOS-SHELL header — reads NOS-HW.CFG via nos_hwcfg_net_present(); shows "NET" in status bar; updated on Ctrl+R
- [x] **3.6** MTCP.CFG auto-generator in NOS-DETECT — genconf.c write_mtcpcfg() emits PACKETINT + HOSTNAME when packet driver detected
- [ ] **3.7** Test all networking in VirtualBox (NAT + bridged), VMware (NAT), QEMU (user networking) *(manual, deferred)*

### Exit Criteria

- `NNET STATUS` shows correct IP configuration on all three VM platforms
- `NNET PING 8.8.8.8` works
- `NNET WEB` can fetch a text web page
- `NNET FTP` can connect to a public FTP server
- `NNET IRC` can connect to an IRC server and join a channel
- Time syncs on boot
- Shell status bar reflects network state

---

## Phase 4: Package Manager [Weeks 14-17]

**Goal:** NPKG can search, download, install, and remove DOS software packages.

### Tasks

- [x] **4.1** Define NPKG package format spec — document in `packages/README.md`
  - INI-style format as described in CLAUDE.md stack overview
  - Sections: [PACKAGE], [CONFIGURE], [POST-INSTALL], [REMOVE]
  - Standardized fields with validation rules
- [x] **4.2** Implement `src/npkg/index.c` — package index parser
  - Downloads `packages.idx` from repository URL (HTTP via mTCP)
  - Parses master index into searchable in-memory structure
  - Caches locally in `NOS\NPKG\CACHE\packages.idx`
- [x] **4.3** Implement `src/npkg/fetch.c` — HTTP download engine
  - Wraps mTCP HTGET for file downloads
  - Progress indication (bytes downloaded / total)
  - Resume support if possible (HTTP Range headers)
  - Handles redirects (archive.org uses them heavily)
- [x] **4.4** Implement `src/npkg/install.c` — package installer
  - Downloads .ZIP/.ARJ from URL specified in .npkg definition
  - Extracts to InstallDir using UNZIP/UNARJ
  - Runs POST-INSTALL batch script if specified
  - Copies configuration presets
  - Adds to PATH if specified
  - Registers with shell launcher
- [x] **4.5** Implement `src/npkg/registry.c` — installed package database
  - Simple flat file: `NOS\NPKG\INSTALLED.DB`
  - Tracks: package ID, version, install directory, install date
  - Used for removal and upgrade detection
- [x] **4.6** Implement `src/npkg/npkg.c` — main command router
  - `NPKG SEARCH [term]` — search package index by name/category/description
  - `NPKG INFO [id]` — show detailed package information
  - `NPKG INSTALL [id]` — full install workflow
  - `NPKG REMOVE [id]` — remove package, clean up launcher entries
  - `NPKG LIST` — show installed packages
  - `NPKG UPDATE` — refresh package index from repository
  - `NPKG PROFILE [id]` — show memory/hardware requirements
- [x] **4.7** Write initial package definitions (at least 20)
  - Word Processing: wp51, wstar7, galaxy, xywrite
  - Spreadsheets: lotus123, aseasy, sc
  - Databases: dbase, foxpro
  - Programming: tp70, tc30, qbasic, masm
  - Communications: telix, procomm
  - Utilities: norton, pctools, xtree
  - Games: doom, doom2, wolf3d, prince, commander_keen
- [x] **4.8** Set up package repository — static HTTP server (GitHub Pages or similar) hosting .npkg files and packages.idx
- [x] **4.9** Integrate NPKG into NOS-SHELL — F9 launcher auto-populates from installed packages
- [x] **4.10** Handle archive.org download quirks — redirects, throttling, retry logic

### Exit Criteria

- `NPKG UPDATE` successfully downloads package index
- `NPKG SEARCH word` finds WordPerfect and other word processors
- `NPKG INSTALL galaxy` downloads, extracts, configures Galaxy Write, adds to launcher
- `NPKG REMOVE galaxy` cleanly removes it
- `NPKG LIST` shows installed packages
- At least 5 packages fully tested end-to-end (download through launch)
- F9 launcher reflects installed packages

---

## Phase 5: Host Bridge [Weeks 18-20]

**Goal:** Seamless file exchange between DOS VM and host OS.

### Tasks

- [x] **5.1** Implement shared folder auto-mount
  - Detect VirtualBox shared folders (via VBoxSF or pre-mounted)
  - Detect VMware shared folders (via HGFS or pre-mounted)
  - QEMU: document 9P or FAT folder sharing setup
  - Map to H:\ drive letter consistently
  - Fall back to instructions if auto-detect fails
- [x] **5.2** Create directory convention on H:\
  - `H:\INBOX\` — host puts files here for DOS
  - `H:\OUTBOX\` — DOS puts files here for host
  - `H:\PRINT\` — print output goes here
  - `H:\CLIP\` — clipboard exchange file
  - Auto-create directories if they don't exist
- [x] **5.3** Implement `src/bridge/noslpt.asm` — LPT1 print interceptor TSR
  - Hooks INT 17h (printer services)
  - Captures all LPT1 output to `H:\PRINT\PRINT001.PRN` (auto-incrementing)
  - Minimal resident footprint (< 2KB)
  - Install/uninstall cleanly
- [x] **5.4** Implement `src/bridge/nosclip.asm` — clipboard exchange TSR
  - Hotkey: Ctrl+Shift+C writes selected text (from screen) to `H:\CLIP\CLIP.TXT`
  - Hotkey: Ctrl+Shift+V reads `H:\CLIP\CLIP.TXT` and injects as keystrokes
  - Hooks INT 09h + INT 08h (deferred I/O via InDOS check)
  - Minimal resident footprint (< 1.5KB)
- [x] **5.5** Implement `src/bridge/bridge.c` — bridge management utility
  - `NBRIDGE STATUS` — show H:\ mount status, print queue, clipboard state
  - `NBRIDGE MOUNT` — attempt to mount shared folder (VBox → VMware → instructions)
  - `NBRIDGE DIRS` — create directory structure on H:\
  - `NBRIDGE PRINT [file]` — manually send file to print folder
  - `NBRIDGE CLIP GET/PUT/CLEAR` — clipboard read/write/clear
- [x] **5.6** Write host-side helper scripts (optional, for convenience)
  - `host_helpers/watch_print.py` — watches PRINT folder, converts PRN to PDF via Ghostscript
  - `host_helpers/watch_outbox.py` — copies OUTBOX files to host desktop
  - Documented but not required — NOS-DOS works without them
- [x] **5.7** Integrate bridge status into NOS-SHELL status bar

### Exit Criteria

- H:\ drive accessible in NOS-SHELL file panels
- Can copy file from C:\ to H:\OUTBOX\ and it appears on host
- Can place file in H:\INBOX\ from host and access it in DOS
- Print from WordPerfect generates .PRN file in H:\PRINT\
- Clipboard TSR installs with < 2KB resident footprint
- Bridge works on VirtualBox and VMware; documented for QEMU

---

## Phase 6: Game Support [Weeks 21-23]

**Goal:** Games can be installed via NPKG and launched with correct speed/memory/sound settings.

### Tasks

- [x] **6.1** Implement `src/throttle/throttle.asm` — CPU throttle TSR
  - Hooks INT 08h (busy-wait delay loop per tick) + INT 09h (hotkeys)
  - Presets: OFF, SLOW100, SLOW66, SLOW33, SLOW10, SLOW477 (levels 0-5)
  - Hotkeys: Ctrl+Alt+KP+/KP-/0 to adjust level live
  - Shared data at fixed offsets (CS:0x100) for TCTL.EXE access
  - Resident footprint: < 700 bytes
- [x] **6.2** Implement `src/throttle/tctl.c` — throttle control utility
  - `TCTL SET [preset]` — set speed level by name or number
  - `TCTL STATUS` — show current level and all preset counts
  - `TCTL CALIBRATE` — measure timer tick, compute and write preset values
- [x] **6.3** Implement `src/play/profiles.c` — game profile loader
  - Lightweight .npkg parser (no NPKG stack dependency)
  - Reads [LAUNCHER] Exec/Dir and [GAME] CPUPreset/MemProfile/SoundEnv/Notes
- [x] **6.4** Implement `src/play/nosplay.c` — game launcher
  - Interactive mode (no args): numbered list → pick to launch
  - `NOSPLAY <id>` — apply profile then launch (NOSMEM + THROTTLE + SET)
  - `NOSPLAY LIST` — text list of installed games
  - `NOSPLAY INFO <id>` — show game profile details
  - Removes THROTTLE on exit if NOSPLAY installed it
- [x] **6.5** Write game package definitions (10 total)
  - Phase 4: DOOM, DOOM2, WOLF3D, PRINCE, KEEN1
  - Phase 6: HERETIC, DUKE3D, QUAKE, DESCENT, TYRIAN
  - packages.idx regenerated: 28 entries total
- [x] **6.6** Integrate NOS-PLAY into NOS-SHELL launcher — Games via NOSPLAY
  - install.c: packages with [GAME] section route through NOSPLAY in LAUNCHER.CFG
  - F9 game entries run `NOSPLAY.EXE <id>` (applies full profile before exec)
- [ ] **6.7** Test timing-sensitive games (Prince of Persia, early Sierra games, Wing Commander) across all three VM platforms *(manual, deferred to Phase 7)*

### Exit Criteria

- `THROTTLE` TSR installs and reduces effective CPU speed
- Hotkeys adjust speed in real-time
- `NOSPLAY DOOM` launches DOOM with correct sound and memory configuration
- `NOSPLAY PRINCE` launches Prince of Persia at playable speed
- At least 5 games fully tested and playable
- Throttle TSR < 1KB resident
- Clean uninstall on game exit — system returns to previous state

---

## Phase 7: Polish & Release [Weeks 24-26]

**Goal:** Documentation, testing, VM appliance builds, and public release.

### Tasks

- [x] **7.1** Write on-disk documentation
  - `NOS\DOCS\README.TXT` — welcome and overview
  - `NOS\DOCS\QUICKSTART.TXT` — 1-page getting started guide
  - `NOS\DOCS\COMMANDS.TXT` — all NOS-DOS commands reference
  - `NOS\DOCS\NPKG.TXT` — package manager guide
  - `NOS\DOCS\GAMES.TXT` — game compatibility notes
  - `NOS\DOCS\BRIDGE.TXT` — host bridge setup guide
  - All viewable from F1 Help in NOS-SHELL
- [x] **7.2** Write first-boot welcome screen
  - Shown once on first boot after NOS-DETECT completes
  - Shows system summary, quick key reference, how to get help
  - "Press any key to enter NOS-DOS"
- [x] **7.3** Implement `build/mkvm.py` — VM appliance generator
  - VirtualBox OVA: 504MB disk, 32MB RAM, SB16 emulation, NAT networking, shared folder configured
  - VMware VMX: equivalent settings
  - QEMU launch script: equivalent settings
  - All appliances boot directly to NOS-SHELL
- [ ] **7.4** Full compatibility test matrix
  - Test on: VirtualBox (Win/Mac/Linux), VMware Workstation/Fusion, QEMU (Linux/Mac)
  - Test: boot, shell, networking, NPKG install, 5 productivity apps, 5 games
  - Document results in compatibility matrix
- [x] **7.5** Performance and memory audit
  - Conventional memory: 639 KB free (target 620 KB+) — verified via DETECT/COM1 output
  - Boot time: 2.4s in QEMU headless (target < 10s) — boot_test.py passes
  - Shell renders and responds correctly — verified via VNC/QEMU display
  - MEM.EXE and CHKDSK.EXE not bundled (FreeDOS util package not fetched); add via fetch_deps if needed
- [x] **7.6** Create NOS-DOS website (static site, can be GitHub Pages)
  - Landing page with pitch and screenshots
  - Download page with OVA/VMX/ISO
  - Quick start guide
  - Package directory (browsable online)
  - Community links (GitHub, IRC channel)
- [x] **7.7** Write GitHub README.md
  - Project description
  - Screenshots
  - Quick start (download OVA → import → boot)
  - Building from source
  - Contributing guide
  - License (GPL-2.0)
- [x] **7.8** Create CONTRIBUTING.md
  - How to add packages (write .npkg, test, PR)
  - How to report compatibility issues
  - Coding standards (reference CLAUDE.md)
  - How to build and test locally
- [ ] **7.9** Tag v1.0 release
  - GitHub release with OVA, VMX, QEMU IMG, ISO
  - Release notes with known issues and compatibility matrix
- [ ] **7.10** Announce
  - r/retrobattlestations, r/retrocomputing, r/freedos, r/dos
  - Hacker News
  - DOS/retro computing forums
  - vogons.org

### Exit Criteria

- Download OVA → Import to VirtualBox → Boot → Working NOS-DOS in under 2 minutes
- All documentation accessible from within DOS (F1 help)
- At least 20 packages in repository
- At least 5 productivity apps and 5 games confirmed working
- Zero critical bugs in issue tracker
- Website live with downloads

---

## Future Phases (Post v1.0)

### v1.1 — Community & Content

- [ ] Community-contributed package definitions
- [ ] Expanded game compatibility database (target 50+ games)
- [ ] Theme system for NOS-SHELL (color schemes)
- [ ] Multiple language support for shell UI

### v1.2 — Enhanced Connectivity

- [ ] SSH client integration (SSH2DOS)
- [ ] Gopher browser integration
- [ ] BBS door game support
- [ ] Telnet to MUD/BBS services

### v1.3 — Advanced Features

- [ ] FAT32 support for larger disk images
- [ ] Long filename display in shell (via DOSLFN)
- [ ] RAM drive for temporary files
- [ ] Disk caching (AZAZC or similar)
- [ ] Sound Blaster emulation for VMs that only provide AC97/HDA

### v2.0 — Bare Metal Focus

- [ ] USB keyboard/mouse support via BIOS
- [ ] USB mass storage driver
- [ ] Expanded NIC driver support for common real hardware
- [ ] Boot from USB flash drive
- [ ] UEFI boot stub (if feasible)

---

## Risk Register

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Open Watcom doesn't compile a needed construct | Medium | Medium | Keep code simple C89; have NASM fallback for critical routines |
| mTCP can't handle archive.org redirects | Medium | High | Implement redirect following in fetch.c; fall back to manual download instructions |
| VM shared folders inconsistent across platforms | High | Medium | Document per-platform setup; file exchange also works via network (FTP to host) |
| Conventional memory target (620KB) not achievable | Low | High | Profile every TSR aggressively; make all TSRs optional via profiles |
| Copyright holders object to package definitions | Low | High | Definitions link to third-party sources only; respond promptly to takedowns; maintain legal FAQ |
| QEMU timing too different from VBox/VMware | Medium | Medium | Per-VM calibration in NOS-THROTTLE; document known quirks |
| Target audience too small for sustainable community | Medium | Medium | Focus on making contribution easy (just write .npkg files); low maintenance burden by design |
