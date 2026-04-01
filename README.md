# NOS-DOS (NostalgicDOS)

> Boot fast. Work clean. Remember when computing just worked?

NOS-DOS is a curated, ready-to-run DOS distribution built on FreeDOS, optimized for virtual machines. It is an opinionated experience layer on top of FreeDOS — not a new OS.

## What It Is

NOS-DOS wraps FreeDOS with:

- **Auto-configuration** — first boot detects hardware and generates the correct `CONFIG.SYS`/`AUTOEXEC.BAT` automatically
- **Memory profiles** — one command switches between STD/MAX/EMS/GAME presets, all targeting 620 KB+ free conventional memory
- **A proper shell** — Norton Commander-style dual-pane TUI (Phase 2, in progress)
- **Integrated networking** — mTCP wrappers that feel native (Phase 3)
- **A package manager** — install DOS software by name, like `npkg install wp51` (Phase 4)

## Current Status

**Phase 0 (Foundation) — complete.**
**Phase 1 (Detection & Memory) — complete.**

The build pipeline runs end-to-end:

```
fetch_deps.py → compile.py → mkimage.py → mkiso.py → boot_test.py PASS (~2s)
```

What works today:
- Bootable El Torito ISO (1.44 MB FAT12 floppy emulation)
- FreeDOS kernel + COMMAND.COM + JEMMEX memory manager
- **NOS-DETECT**: probes conventional/XMS/EMS memory, VGA/VESA, mouse, Sound Blaster, packet driver; writes `NOS-HW.CFG`, generates `CONFIG.SYS`/`AUTOEXEC.BAT` from templates
- **NOS-MEM**: switches between STD/MAX/EMS/GAME memory profiles at runtime
- Headless QEMU boot test with serial sentinel + hardware detection result verification
- GitHub Actions CI (build + boot test on every push)

## Building from Source

### Prerequisites

| Tool | Purpose |
|------|---------|
| Python 3.10+ | Build system (host only) |
| NASM | Assembles FreeDOS boot sector |
| mtools | Manipulates FAT12 image without root |
| genisoimage | Creates El Torito bootable ISO |
| QEMU (i386) | Boot testing |
| Open Watcom C | Cross-compiles 16-bit DOS binaries |

**Install build tools (Debian/Ubuntu):**
```bash
sudo apt-get install nasm mtools genisoimage qemu-system-x86
```

**Install Open Watcom** (for DOS compilation):
```bash
# Download snapshot tarball from https://github.com/open-watcom/open-watcom-v2/releases
wget https://github.com/open-watcom/open-watcom-v2/releases/download/Current-build/ow-snapshot.tar.xz
sudo mkdir -p /opt/watcom
sudo tar -xf ow-snapshot.tar.xz -C /opt/watcom
export WATCOM=/opt/watcom
export PATH=$WATCOM/binl64:$WATCOM/binl:$PATH
export INCLUDE=$WATCOM/h:$WATCOM/h/dos
```

### Build

```bash
# First time: fetch all third-party dependencies
python3 build/fetch_deps.py

# Full build (compile DOS binaries + create image + ISO)
python3 build/build.py

# Build without recompiling (use pre-built or omit DOS binaries)
python3 build/build.py --skip-fetch --skip-compile

# Run the boot test
python3 tests/boot_test.py
python3 tests/boot_test.py --verbose   # show serial output in real time
```

### Individual stages

```bash
python3 build/compile.py    # Cross-compile DOS components (requires Open Watcom)
python3 build/mkimage.py    # Create FAT12 floppy image
python3 build/mkiso.py      # Wrap into bootable ISO
python3 tests/boot_test.py  # Boot in QEMU, assert ready in <10s
```

## Project Structure

```
NOSDOS/
├── build/                  # Host-side build system (Python)
│   ├── build.py            # Master build script
│   ├── compile.py          # Open Watcom cross-compiler driver
│   ├── fetch_deps.py       # Downloads FreeDOS, JEMMEX, CTMOUSE, mTCP
│   ├── mkimage.py          # Creates FAT12 floppy image
│   ├── mkiso.py            # Creates El Torito ISO
│   └── config.ini          # Version pins and paths
├── dist/
│   ├── config/             # DOS config templates
│   │   ├── CONFIG.TPL      # CONFIG.SYS template (NOS-DETECT fills vars)
│   │   └── AUTOEXEC.TPL    # AUTOEXEC.BAT template
│   ├── skeleton/           # DOS directory skeleton (future use)
│   └── thirdparty/         # Downloaded dependencies (git-ignored)
│       ├── freedos/        # KERNEL.SYS, COMMAND.COM, boot.asm
│       ├── jemmex/         # JEMMEX.EXE memory manager
│       ├── ctmouse/        # CTMOUSE.EXE PS/2 mouse driver
│       └── mtcp/           # mTCP networking suite
├── src/                    # DOS-side C source (Open Watcom, 16-bit)
│   ├── detect/             # NOS-DETECT hardware detection
│   │   ├── memory.c/h      # Conventional/XMS/EMS detection
│   │   ├── video.c/h       # VGA/VESA detection
│   │   ├── mouse.c/h       # PS/2 and serial mouse detection
│   │   ├── sound.c/h       # Sound Blaster detection
│   │   ├── network.c/h     # Packet driver detection
│   │   ├── genconf.c/h     # CONFIG.SYS/AUTOEXEC.BAT generator
│   │   ├── detect.c        # Main orchestrator
│   │   └── Makefile
│   ├── mem/                # NOS-MEM memory profile switcher
│   │   ├── nosmem.c
│   │   └── Makefile
│   ├── shell/              # NOS-SHELL TUI (Phase 2)
│   ├── npkg/               # Package manager (Phase 4)
│   ├── net/                # Network wrappers (Phase 3)
│   ├── bridge/             # Host bridge TSR (Phase 5)
│   ├── throttle/           # CPU throttle TSR (Phase 6)
│   └── play/               # Game launcher (Phase 6)
├── tests/
│   └── boot_test.py        # QEMU headless boot + detection result verifier
├── packages/               # NPKG package definitions (Phase 4)
├── out/                    # Build output (git-ignored)
│   ├── nosdos.img          # FAT12 floppy image
│   └── nosdos.iso          # El Torito bootable ISO
├── .github/workflows/
│   └── ci.yml              # GitHub Actions CI
└── PLAN.md                 # Full phased development roadmap
```

## Architecture

```
USER → NOS-SHELL (TUI) → NPKG | NNET | NPLAY | NBRIDGE | Apps
                              ↓
                    NOS-DETECT / NOS-MEM
                              ↓
                    FreeDOS Kernel + JEMMEX + Drivers
                              ↓
                    Virtual Machine (VBox / VMware / QEMU)
```

### Component Summary

| Component | Language | Status | Role |
|-----------|----------|--------|------|
| NOS-DETECT | Open Watcom C | ✅ Phase 1 | First-boot hardware detection → CONFIG.SYS/AUTOEXEC.BAT |
| NOS-MEM | Open Watcom C | ✅ Phase 1 | Memory profile switcher (STD/MAX/EMS/GAME) |
| NOS-SHELL | Open Watcom C + NASM | 📋 Phase 2 | Dual-pane Norton Commander-style TUI |
| NNET | Batch + Open Watcom C | 📋 Phase 3 | mTCP networking wrappers |
| NPKG | Open Watcom C | 📋 Phase 4 | Package manager |
| NOS-BRIDGE | Open Watcom C + NASM | 📋 Phase 5 | Host file exchange TSR |
| NOS-THROTTLE | NASM TSR | 📋 Phase 6 | INT 08h CPU speed limiter |
| NOS-PLAY | Open Watcom C | 📋 Phase 6 | Game launcher with profile management |

## Boot Sequence (Phase 1)

1. BIOS loads FreeDOS boot sector from ISO (El Torito, floppy emulation)
2. FreeDOS kernel loads `CONFIG.SYS` → `DOS=HIGH,UMB`, `DEVICE=JEMMEX.EXE NOEMS X=TEST`
3. `AUTOEXEC.BAT` runs `DETECT.EXE /NOREBOOT`
4. NOS-DETECT probes hardware, writes `NOS-HW.CFG`, generates `CONFIG.SYS`/`AUTOEXEC.BAT` from templates
5. On first boot: `DETECT.EXE` reboots; subsequent boots: generated config loads
6. Boot sentinel `NOS-DOS-READY` written to COM1 (QEMU serial → boot test)

## Memory Profiles (NOS-MEM)

| Profile | JEMMEX | CTMOUSE | FILES | BUFFERS | STACKS | Target |
|---------|--------|---------|-------|---------|--------|--------|
| STD | NOEMS X=TEST | Yes | 40 | 20 | 9,256 | 620 KB+ |
| MAX | NOEMS X=TEST | No | 20 | 10 | 0,0 | 635 KB+ |
| EMS | X=TEST (EMS enabled) | Yes | 40 | 20 | 9,256 | 588 KB+ |
| GAME | NOEMS X=TEST | Yes | 20 | 5 | 0,0 | 630 KB+ |

Usage: `NOSMEM /STD`, `NOSMEM /MAX`, `NOSMEM /EMS`, `NOSMEM /GAME`, `NOSMEM /STATUS`

## Design Principles

1. **VM-first** — all decisions optimize for VirtualBox/VMware/QEMU
2. **Zero-config boot** — power-on → usable shell in under 10 seconds
3. **Profiles over configuration** — select STD/MAX/EMS/GAME; system generates correct `CONFIG.SYS`
4. **FreeDOS is the kernel, not the identity** — we ship FreeDOS but the experience is NOS-DOS
5. **16-bit real mode only** — Open Watcom C89, small memory model, NASM, no protected mode

## Constraints

- Compiler: Open Watcom C only, 16-bit DOS target (`-ms -bt=dos`), C89/C90 only
- Assembler: NASM only — no MASM/TASM syntax
- Conventional memory hard target: **620 KB+ free** in STD profile
- 8.3 filenames, CR+LF line endings, ASCII only on all DOS-side files
- No C stdlib in TSRs — INT 21h/BIOS only

## License

GPL-2.0. See individual source files for headers.

FreeDOS components are under their respective licenses (GPL-2.0 for kernel/COMMAND.COM).
JEMMEX is GPL-2.0. CTMOUSE is GPL-2.0. mTCP is freeware.
