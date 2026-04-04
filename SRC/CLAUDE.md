# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

---

# NOS-DOS (NostalgicDOS)

NOS-DOS is a curated, ready-to-run DOS distribution built on FreeDOS, optimized for VMs. It is an opinionated experience layer on top of FreeDOS — not a new OS.

**Tagline:** Boot fast. Work clean. Remember when computing just worked?

## Current Status

**Phase 0 (Foundation) — mostly complete.** The build pipeline is working end-to-end:
`fetch_deps.py` → `mkimage.py` → `mkiso.py` → `boot_test.py` passes in ~2s.

Remaining Phase 0 item: task 0.8 (GitHub Actions CI). The 504MB FAT16 hard disk image
was deferred to Phase 1 — Phase 0 uses a 1.44MB FAT12 floppy embedded as El Torito boot.

**Next phase:** Phase 1 — NOS-DETECT + NOS-MEM. First DOS C source files go in `src/detect/`
and `src/mem/`. Requires Open Watcom (not yet installed).

## Build & Development Commands

> The build system runs on the **host OS** (Linux/macOS/Windows). Nothing in `build/` runs inside DOS.

```bash
# First-time setup: fetch all third-party dependencies
python3 build/fetch_deps.py

# Full build (skip fetch/compile if deps and binaries are already in place)
python3 build/build.py --skip-fetch --skip-compile

# Full build from scratch (requires network + Open Watcom)
python3 build/build.py

# Build individual stages
python3 build/mkimage.py    # Assemble boot sector + create FAT12 floppy image
python3 build/mkiso.py      # Wrap floppy image into El Torito bootable ISO
python3 build/compile.py    # Cross-compile DOS components (no-op until Open Watcom installed)

# Run tests
python3 tests/boot_test.py                     # Boot ISO in QEMU, assert ready in <10s
python3 tests/boot_test.py --verbose           # Show QEMU serial output in real time
python3 tests/boot_test.py --timeout 20        # Override timeout
```

**build.py flags:**
- `--skip-fetch` — assume `dist/thirdparty/` is already populated
- `--skip-compile` — skip Open Watcom compilation (use when Watcom not installed)
- `--only <stage>` — run one stage: `fetch | compile | image | iso`

**Compile a single DOS component** (Open Watcom must be on PATH, `WATCOM` env var set):
```bash
# From component directory, e.g. src/shell/
wmake -f Makefile

# Direct invocation (small model, DOS target):
wcc -ms -bt=dos -fo=obj/foo.obj src/shell/foo.c
wlink system dos name bin/shell.exe file obj/foo.obj
```

**Assemble a NASM TSR or boot sector:**
```bash
# TSR (.COM output):
nasm -f bin src/throttle/throttle.asm -o bin/throttle.com

# Boot sector (FreeDOS boot.asm — requires magic.mac in same dir):
nasm -f bin -DISFAT12=1 dist/thirdparty/freedos/boot.asm -o out/boot12.bin
```

## Architecture

```
USER → NOS-SHELL (TUI) → NPKG | NNET | NPLAY | NBRIDGE | Apps
                              ↓
                    NOS-DETECT / NOS-MEM
                              ↓
                    FreeDOS Kernel + JEMMEX + Drivers
                              ↓
                    Virtual Machine (VBox/VMware/QEMU)
```

### Component Summary

| Component | Language | Role |
|-----------|----------|------|
| NOS-SHELL | Open Watcom C + NASM | Primary TUI — dual-pane Norton Commander-style file manager |
| NPKG | Open Watcom C | Package manager (download, install, remove DOS apps via mTCP) |
| NOS-DETECT | Open Watcom C + NASM | First-boot hardware detection → generates CONFIG.SYS/AUTOEXEC.BAT |
| NOS-MEM | Open Watcom C | Memory profile switcher (STD/MAX/EMS/GAME presets) |
| NNET | Batch + Open Watcom C | Wrappers around mTCP (ping, FTP, IRC, web, DNS, time sync) |
| NOS-BRIDGE | Open Watcom C + NASM | TSR pair for host file exchange (LPT→file, clipboard hotkeys) |
| NOS-PLAY | Open Watcom C | Game launcher — applies memory/speed/sound profile before launch |
| NOS-THROTTLE | NASM (TSR) | INT 08h hook, HLT-based CPU speed limiter with hotkeys |
| Build System | Python 3.11+ | Host-side: fetch deps, compile, create disk images and ISOs |

### Key Data Flows

**First boot:** NOS-DETECT probes hardware (INT 10h/12h/33h, EMS/XMS calls, port scan for SB) → writes `NOS\SYSTEM\NOS-HW.CFG` → generates CONFIG.SYS/AUTOEXEC.BAT from templates in `dist/config/` → reboots.

**Package install:** `NPKG INSTALL [id]` → reads `.npkg` definition from `packages/` → uses `src/npkg/fetch.c` (wraps mTCP HTGET) to download archive → extracts → runs POST-INSTALL batch → writes to `NOS\NPKG\INSTALLED.DB` → registers entry in NOS-SHELL launcher.

**Game launch:** `NOSPLAY [id]` → `src/play/profiles.c` reads .npkg game profile → loads `NOSMEM /GAME` → installs THROTTLE TSR at correct preset → sets sound env vars → exec game → restores on exit.

**TSR lifecycle:** All TSRs (THROTTLE, NOSLPT, NOSCLIP) follow the pattern: PSP → resident code (stays in memory) → init code (runs once then freed). Install with `/<name>`, uninstall with `/<name> /U`. Must leave no memory holes on uninstall.

## Build System Internals

### fetch_deps.py
- Downloads FreeDOS kernel (`KERNL386.SYS` from kernel.zip — renamed to `KERNEL.SYS`)
- Downloads `boot.asm` **and `magic.mac`** from FDOS/kernel GitHub (`magic.mac` is a required include)
- All FreeDOS packages are at `ibiblio.org/.../repositories/1.3/base/` (not `distributions/`)
- JEMMEX pinned to v5.86 (`JemmB_v586.zip`); mTCP pinned to 2025-01-10
- mTCP 2025 release uses lowercase `.exe` filenames; fetch_deps normalises them to uppercase
- Idempotent: already-downloaded archives are skipped; extracted files are always re-extracted

### mkimage.py
- Produces `out/nosdos.img` — 1.44MB FAT12 floppy (Phase 0)
- Assembles `boot.asm` with `nasm -f bin -DISFAT12=1`; output must be exactly 512 bytes
- Formats with `mformat -B out/boot12.bin`, installs files with `mcopy`, sets sys attributes with `mattrib`
- `configparser` cross-section interpolation does not work — paths are resolved directly in Python
- AUTOEXEC.BAT writes `NOS-DOS-READY > COM1` at end of boot (boot test sentinel)

### boot_test.py
- QEMU flags: `-display none -serial stdio -no-reboot -no-shutdown`
- COM1 (`-serial stdio`) carries the boot sentinel; VGA output is not captured
- Detects `NOS-DOS-READY` on COM1, or a DOS prompt pattern as fallback
- Passes in ~2s on QEMU 7.2 (Debian)

## Critical Constraints

### 16-bit Real Mode — No Exceptions
- **Compiler:** Open Watcom C/C++ only, targeting 16-bit DOS. C89/C90 only — no C99 features.
- **Assembler:** NASM only — no MASM/TASM conventions.
- **Memory model:** Small or medium for most components. TSRs: tiny model only.
- **No protected mode** unless explicitly noted for a specific component.

### Conventional Memory Is Sacred
- Hard target: **620KB+ free** in STD profile with all standard TSRs loaded.
- Every TSR must justify its resident footprint in bytes.
- TSRs (THROTTLE < 1KB, NOSCLIP < 1.5KB, NOSLPT < 2KB) — exceed these and investigate.
- No C stdlib in TSRs — use INT 21h/BIOS calls directly.
- Any feature that consumes conventional memory must be disableable via a profile.

### DOS-Side File Rules
- 8.3 filenames only in core system (DOSLFN available but not required)
- CR+LF line endings on all DOS-side files
- ASCII only — no Unicode
- `.COM` preferred for utilities < 64KB; `.EXE` for larger components

### Screen Output
- NOS-SHELL uses direct video memory writes to `B800:0000` (text mode)
- INT 10h fallback for compatibility where direct writes are risky
- 80×25 and 80×50 text modes supported

## Coding Standards

### Open Watcom C

```c
/* File header */
/* NOS-DOS: [component name]
 * [filename] - [brief description]
 * License: GPL-2.0
 */

/* C89/C90 only — no C99 */
/* No // comments — use only block comments */
/* All functions must have explicit return types */
/* Variables declared at top of block */
/* Always check malloc() and file open/read/write returns */
/* Far pointers only when necessary (video memory, UMB access) */
```

Naming:
- Functions: `nos_component_action` (e.g., `nos_shell_draw_panel`)
- Constants: `NOS_COMPONENT_NAME` (e.g., `NOS_SHELL_MAX_FILES`)
- Globals: `g_component_name` (e.g., `g_shell_left_panel`)
- Types: `nos_typename_t` (e.g., `nos_fileentry_t`)

### NASM Assembly

```asm
; File header
; NOS-DOS: [component name]
; [filename] - [brief description]
; License: GPL-2.0

; NASM syntax only — no MASM/TASM
; TSR layout: PSP → resident code → init code (freed post-install)
; Labels: component.action (e.g., throttle.hook_timer)
; Constants: UPPER_SNAKE_CASE
; Document all INT hooks: registers in/out, flags modified
; Preserve ALL registers in interrupt handlers unless intentionally modifying
```

### Batch Files

```batch
@ECHO OFF
REM NOS-DOS: [component]
REM Use REM for comments (not ::), UPPERCASE commands, NOS_ prefix for vars
```

### Build System (Python)

```python
# Python 3.10+ — runs on host only, never on DOS
# pathlib for all paths, type hints on all functions, docstrings on public functions
# No third-party deps in core build (stdlib only)
# External tools via subprocess: qemu-img, genisoimage, wcc, wlink, nasm, mformat, mcopy
```

## Testing Requirements

Every build must pass these before release:
- Boot in QEMU headless → DOS environment ready within 10 seconds
- `MEM /C` → 620KB+ free conventional memory in STD profile
- `NNET STATUS` → connected with IP address (when VM networking available)
- NOS-SHELL → launches, displays both panels, no errors
- NPKG → parses package index without error
- All TSRs → install and uninstall cleanly (no memory leaks, no holes)

## What NOT To Do

- Do NOT write a new DOS kernel — FreeDOS is the kernel
- Do NOT use any compiler other than Open Watcom for DOS-side code
- Do NOT assume 32-bit protected mode anywhere in DOS-side code
- Do NOT add features that consume conventional memory without a disabling profile
- Do NOT bundle copyrighted/abandonware binaries in the repository (NPKG links to third-party sources only)
- Do NOT use AZERTY/QWERTZ defaults — ship US English, user configures locale
- Do NOT target UEFI without CSM — VMs provide BIOS; bare metal UEFI is post-v1

## Key Design Decisions

1. **VM-first:** All decisions optimize for VirtualBox/VMware/QEMU deployment.
2. **Zero-config boot:** Power-on → usable shell with networking in under 10 seconds.
3. **Profiles over configuration:** Users select STD/MAX/EMS/GAME; system generates correct CONFIG.SYS.
4. **Norton Commander paradigm:** Dual-pane, F-key driven, mouse optional — the interaction model the target audience knows.
5. **FreeDOS is the kernel, not the identity:** We ship FreeDOS but the user experience is NOS-DOS.

## Useful References

- [FreeDOS source and docs](https://www.freedos.org/)
- [Open Watcom documentation](https://open-watcom.github.io/)
- [mTCP networking suite](https://www.brutman.com/mTCP/)
- [JEMMEX memory manager](https://github.com/Baron-von-Riedesel/Jemm)
- [RBIL — Ralf Brown's Interrupt List](https://www.ctyme.com/rbown.htm)
- [NASM documentation](https://www.nasm.us/doc/)
- [DOS INT 21h reference](http://www.ctyme.com/intr/int-21.htm)
