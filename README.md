# NOS-DOS (NostalgicDOS)

> **Boot fast. Work clean. Remember when computing just worked?**

NOS-DOS is a curated, ready-to-run DOS environment built on FreeDOS, optimized for virtual machines.
It is an opinionated experience layer — not a new OS.

[![CI](https://github.com/your-org/nosdos/actions/workflows/ci.yml/badge.svg)](https://github.com/your-org/nosdos/actions/workflows/ci.yml)
[![License: GPL-2.0](https://img.shields.io/badge/license-GPL--2.0-blue)](LICENSE)
[![Release](https://img.shields.io/github/v/release/your-org/nosdos)](https://github.com/your-org/nosdos/releases)

---

## What It Is

Import the OVA, start the VM, and you have a properly configured DOS system with:

- **Auto-configuration** — first boot detects your VM's hardware and generates `CONFIG.SYS`/`AUTOEXEC.BAT`
- **Dual-pane shell** — Norton Commander-style TUI with F-key operations and a built-in app launcher
- **Memory profiles** — one command (`NOSMEM /STD`, `/MAX`, `/EMS`, `/GAME`) targets 620 KB+ free
- **Integrated networking** — `NNET DHCP` gets you online; ping, FTP, IRC, web, time sync built in
- **Package manager** — `NPKG INSTALL WP51` downloads, extracts, and registers WordPerfect 5.1
- **Host bridge** — shared folder maps to `H:\`; copy files in/out, print to host, sync clipboard
- **Game launcher** — NOSPLAY applies the right memory/throttle/sound settings before any game
- **CPU throttle** — tame modern VM speed for timing-sensitive DOS games; live hotkey adjustment

## Quick Start

### Option A: VirtualBox OVA (recommended)

1. [Download `nosdos.ova`](https://github.com/your-org/nosdos/releases/latest)
2. VirtualBox → **File → Import Appliance** → select the OVA → Import
3. Start the VM — NOS-SHELL appears in under 10 seconds
4. Press `F12` for a DOS prompt, then `NNET DHCP` to get networking
5. `NPKG UPDATE` then `NPKG INSTALL <id>` to install software

### Option B: QEMU

```bash
# Linux / macOS
unzip nosdos-qemu.zip
./run-qemu.sh

# Windows
run-qemu.bat
```

### Option C: VMware

Unzip `nosdos-vmware.zip` and open `nosdos.vmx` in VMware Workstation or Fusion.

## Key Commands

| Command | What it does |
|---------|-------------|
| `NNET DHCP` | Obtain IP address via DHCP |
| `NNET STATUS` | Show network status and IP |
| `NPKG UPDATE` | Refresh package index |
| `NPKG SEARCH <term>` | Search packages |
| `NPKG INSTALL <id>` | Install a package |
| `NPKG REMOVE <id>` | Remove a package |
| `NPKG LIST` | List installed packages |
| `NOSMEM /STD` | Standard memory profile (620 KB+) |
| `NOSMEM /GAME` | Game memory profile (630 KB+) |
| `NOSPLAY <id>` | Launch a game with correct settings |
| `NOSPLAY LIST` | List installed games |
| `TCTL CALIBRATE` | Calibrate CPU throttle for this VM |
| `NBRIDGE MOUNT` | Mount host shared folder to H:\ |
| `NBRIDGE DIRS` | Create H:\INBOX, OUTBOX, PRINT, CLIP |

## Building from Source

### Prerequisites

| Tool | Purpose | Install |
|------|---------|---------|
| Python 3.10+ | Build system | system package |
| NASM | Boot sector assembly | `apt install nasm` |
| mtools | FAT image manipulation | `apt install mtools` |
| genisoimage | El Torito ISO creation | `apt install genisoimage` |
| QEMU (i386) | Boot testing | `apt install qemu-system-x86` |
| Open Watcom C | 16-bit DOS cross-compiler | see below |

**Install Open Watcom:**

```bash
wget https://github.com/open-watcom/open-watcom-v2/releases/download/Current-build/ow-snapshot.tar.xz
sudo mkdir -p /opt/watcom && sudo tar -xf ow-snapshot.tar.xz -C /opt/watcom
export WATCOM=/opt/watcom
export PATH=$WATCOM/binl64:$WATCOM/binl:$PATH
export INCLUDE=$WATCOM/h:$WATCOM/h/dos
```

### Build

```bash
# Fetch FreeDOS, JEMMEX, CTMOUSE, mTCP
python3 build/fetch_deps.py

# Full build: compile + floppy image + HDD image + ISO
python3 build/build.py

# Skip recompile if binaries are already built
python3 build/build.py --skip-fetch --skip-compile

# Generate VM appliances (requires qemu-img; VBoxManage optional)
python3 build/mkvm.py --no-vbox   # VMware + QEMU scripts only

# Run the boot test
python3 tests/boot_test.py
python3 tests/boot_test.py --verbose
```

**Individual stages:**

```bash
python3 build/compile.py   # Compile all DOS components (requires Open Watcom)
python3 build/mkimage.py   # FAT12 floppy image
python3 build/mkhdd.py     # FAT16 hard disk image (C: drive)
python3 build/mkiso.py     # El Torito bootable ISO
python3 build/mkvm.py      # VirtualBox OVA + VMware bundle + QEMU scripts
```

**Compile a single component (from its source directory):**

```bash
cd src/shell
wmake -f Makefile
```

## Project Structure

```
NOSDOS/
├── build/                  # Host-side build system (Python 3.10+)
│   ├── build.py            # Master orchestrator
│   ├── compile.py          # Open Watcom driver
│   ├── fetch_deps.py       # Downloads FreeDOS, JEMMEX, CTMOUSE, mTCP
│   ├── mkimage.py          # FAT12 floppy image
│   ├── mkhdd.py            # FAT16 hard disk image (C: drive)
│   ├── mkiso.py            # El Torito bootable ISO
│   └── mkvm.py             # VM appliance generator
├── dist/
│   ├── config/             # CONFIG.SYS / AUTOEXEC.BAT templates
│   ├── docs/               # On-disk documentation (*.TXT for C:\NOS\DOCS\)
│   ├── bat/                # Batch wrappers for NNET commands
│   └── thirdparty/         # Downloaded deps (git-ignored)
├── src/                    # DOS-side source (Open Watcom C + NASM, 16-bit)
│   ├── detect/             # NOS-DETECT — hardware detection + config generation
│   ├── mem/                # NOS-MEM — memory profile switcher
│   ├── shell/              # NOS-SHELL — dual-pane TUI file manager
│   ├── net/                # NNET — mTCP networking wrappers
│   ├── npkg/               # NPKG — package manager
│   ├── bridge/             # NOS-BRIDGE — host file exchange TSRs
│   ├── throttle/           # NOS-THROTTLE — INT 08h CPU speed limiter TSR
│   └── play/               # NOS-PLAY — game launcher with profile management
├── packages/               # NPKG package definitions (.NPKG files + index)
├── host_helpers/           # Host-side Python scripts (print watcher, outbox sync)
├── website/                # Static website (GitHub Pages)
├── tests/
│   └── boot_test.py        # Headless QEMU boot test (passes in ~2.4s)
└── .github/workflows/
    └── ci.yml              # GitHub Actions CI
```

## Architecture

```
USER → NOS-SHELL (TUI) ──────────────── F9: App Launcher
          │                                     │
          ├── F12: DOS Prompt              NPKG | NNET | NOSPLAY | NBRIDGE
          └── Status bar (mem, IP, H:\)         │
                                         NOS-DETECT / NOS-MEM
                                                 │
                                   FreeDOS Kernel + JEMMEX + Drivers
                                                 │
                              Virtual Machine (VBox / VMware / QEMU)
```

### Components

| Component | Language | Role |
|-----------|----------|------|
| NOS-DETECT | Open Watcom C | First-boot hardware detection → `CONFIG.SYS`/`AUTOEXEC.BAT` |
| NOS-MEM | Open Watcom C | Memory profile switcher (STD/MAX/EMS/GAME) |
| NOS-SHELL | Open Watcom C | Dual-pane TUI — file manager + app launcher |
| NNET | Batch + Open Watcom C | mTCP wrappers (DHCP, ping, HTTP, FTP, IRC, time) |
| NPKG | Open Watcom C | Package manager with archive.org download + retry |
| NOSLPT | NASM TSR | INT 17h hook — captures LPT1 print output to `H:\PRINT\` |
| NOSCLIP | NASM TSR | INT 09h + INT 08h — Ctrl+Shift+C/V clipboard exchange |
| NOS-THROTTLE | NASM TSR | INT 08h busy-wait + INT 09h hotkeys — CPU speed limiter |
| TCTL | Open Watcom C | Throttle control — status, preset calibration, live set |
| NOS-PLAY | Open Watcom C | Game launcher — NOSMEM + THROTTLE + sound env per game |

### Memory Targets

| Profile | Command | Free Conventional | Use Case |
|---------|---------|-------------------|----------|
| STD | `NOSMEM /STD` | 620 KB+ | General use |
| MAX | `NOSMEM /MAX` | 635 KB+ | Memory-hungry apps |
| EMS | `NOSMEM /EMS` | 588 KB+ | EMS-dependent software |
| GAME | `NOSMEM /GAME` | 630 KB+ | Games (mouse + sound only) |

## Packages (v1.0)

28 packages across productivity, programming, communications, and games.
Full list in [`packages/packages.idx`](packages/packages.idx).

**Word processors:** wp51, wstar7, galaxy, xywrite
**Spreadsheets:** lotus123, aseasy, sc
**Databases:** dbase, foxpro
**Programming:** tp70, tc30, qbasic, masm
**Communications:** telix, procomm
**Utilities:** norton, pctools, xtree
**Games:** doom, doom2, wolf3d, prince, keen1, heretic, duke3d, quake, descent, tyrian

## Constraints (for contributors)

- **Compiler:** Open Watcom C only — 16-bit DOS target (`-ms -bt=dos`), C89/C90 only
- **Assembler:** NASM only — no MASM/TASM syntax
- **Memory:** Hard target: 620 KB+ free conventional in STD profile
- **DOS files:** 8.3 filenames, CR+LF line endings, ASCII only
- **TSRs:** No C stdlib — INT 21h/BIOS only; must uninstall without memory holes

See [CLAUDE.md](CLAUDE.md) for the full coding standards and [CONTRIBUTING.md](CONTRIBUTING.md) to get started.

## License

GPL-2.0. See individual source files for headers.

Third-party components: FreeDOS (GPL-2.0), JEMMEX (GPL-2.0), CTMOUSE (GPL-2.0), mTCP (freeware).
