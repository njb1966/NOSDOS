# Contributing to NOS-DOS

Thank you for your interest in contributing. NOS-DOS is a small, focused project
and the bar for contributions is: does it make DOS computing more enjoyable in a VM?

---

## Ways to Contribute

- **Add a package** — the most common and most welcome contribution
- **Report a compatibility issue** — we want to know what doesn't work
- **Improve documentation** — fix errors, add clarity, add examples
- **Fix a bug** — see the issue tracker
- **Improve a DOS component** — NOS-SHELL, NPKG, NNET, etc.

---

## Adding a Package

A package definition is a plain-text `.NPKG` file in `packages/`. It tells NPKG
where to download the software, how to install it, and how to launch it.

### 1. Write the .NPKG file

Create `packages/<category>/<ID>.NPKG` (uppercase ID, 8 chars max):

```ini
[PACKAGE]
ID=MYPROG
Name=My DOS Program
Version=1.0
Category=utility
Description=One-line description of what this program does.
License=Freeware
Author=Original Author
Year=1993

[HARDWARE]
MinMemKB=512
SoundOptional=YES

[SOURCE]
URL1=https://archive.org/download/some-collection/myprog.zip
URL2=https://www.somesite.com/mirror/myprog.zip
ArchiveFormat=ZIP
ArchiveFile=myprog.zip

[INSTALL]
InstallDir=C:\APPS\MYPROG
ExeFile=MYPROG.EXE

[POST-INSTALL]
; Optional: batch commands run after extraction
; SET MYPROG=C:\APPS\MYPROG

[LAUNCHER]
Name=My DOS Program
Dir=C:\APPS\MYPROG
Exec=MYPROG.EXE

[REMOVE]
RemoveDir=C:\APPS\MYPROG
```

**For games**, add a `[GAME]` section:

```ini
[GAME]
CPUPreset=SLOW33
MemProfile=GAME
SoundEnv=BLASTER=A220 I5 D1 T4
Notes=Requires EMS for full game; shareware episode only.
```

`CPUPreset` values: `OFF`, `SLOW10`, `SLOW33`, `SLOW66`, `SLOW100`, `SLOW477`

### 2. Test locally

```bash
# Build the HDD image with your package definition present
python3 build/build.py --skip-fetch --skip-compile

# In QEMU, try:
NPKG UPDATE
NPKG INFO MYPROG
NPKG INSTALL MYPROG
```

The install is successful if:
- The program launches from the NOS-SHELL F9 launcher
- `NPKG LIST` shows the package as installed
- `NPKG REMOVE MYPROG` removes it cleanly

### 3. Update packages.idx

After adding or modifying a package, regenerate the index:

```bash
python3 build/mkindex.py   # (or update packages/packages.idx manually)
```

The format is tab-separated: `ID\tName\tVersion\tCategory\tDescription`

### 4. Open a pull request

- One package per PR is preferred
- Title: `feat(packages): add MYPROG — My DOS Program`
- Confirm in the PR that you verified the download URL is accessible

---

## Reporting Compatibility Issues

Open an issue with:

1. **VM platform and version** (VirtualBox 7.0 on Windows 11, etc.)
2. **What you tried** (exact command or action)
3. **What happened** (error message, crash, wrong output)
4. **What you expected**

For TSR-related issues (memory, install/uninstall), include `MEM /C` output if possible.

---

## Coding Standards

NOS-DOS DOS-side code has strict constraints. Read [CLAUDE.md](CLAUDE.md) for the
full specification. The short version:

### Open Watcom C

```c
/* File header — always include component name and license */
/* NOS-DOS: [component]
 * [filename] - [description]
 * License: GPL-2.0
 */

/* C89/C90 only — no C99 features (no // comments, no stdint.h, no bool) */
/* Variables declared at top of block */
/* All functions have explicit return types */
/* Always check malloc() and file I/O return values */
```

**Naming:**
- Functions: `nos_component_action()` (e.g., `nos_shell_draw_panel`)
- Constants: `NOS_COMPONENT_NAME` (e.g., `NOS_SHELL_MAX_FILES`)
- Types: `nos_typename_t` (e.g., `nos_fileentry_t`)

**Compile flags:** `-ms -bt=dos -q -w4 -e25 -zq -ox`

### NASM Assembly (TSRs)

```asm
; NOS-DOS: [component]
; [filename] - [description]
; License: GPL-2.0

; NASM syntax only — no MASM/TASM
; TSR layout: PSP → resident code → init code (freed after install)
; Preserve ALL registers in interrupt handlers
; Document all INT hooks: registers in/out, flags modified
```

**TSR memory limits:**
- THROTTLE: < 700 bytes resident
- NOSCLIP: < 1.5 KB resident
- NOSLPT: < 2 KB resident

Any TSR that exceeds its limit must be justified with a memory analysis.

### Build System (Python)

```python
# Python 3.10+, stdlib only (no third-party deps in build/)
# pathlib for all paths
# Type hints on all public functions
# Docstrings on all public functions
```

---

## How to Build and Test Locally

### Prerequisites

```bash
# Debian/Ubuntu
sudo apt-get install nasm mtools genisoimage qemu-system-x86

# Open Watcom (for DOS compilation)
wget https://github.com/open-watcom/open-watcom-v2/releases/download/Current-build/ow-snapshot.tar.xz
sudo mkdir -p /opt/watcom && sudo tar -xf ow-snapshot.tar.xz -C /opt/watcom
export WATCOM=/opt/watcom
export PATH=$WATCOM/binl64:$WATCOM/binl:$PATH
export INCLUDE=$WATCOM/h:$WATCOM/h/dos
```

### Full build

```bash
python3 build/fetch_deps.py          # Download FreeDOS, JEMMEX, CTMOUSE, mTCP
python3 build/build.py               # Compile + images + ISO
python3 tests/boot_test.py           # Must pass in < 10s
python3 tests/boot_test.py --verbose # Show serial output
```

### Compile a single component

```bash
cd src/shell
wmake -f Makefile        # Build SHELL.EXE
wmake -f Makefile clean  # Clean build
```

### NASM TSR

```bash
nasm -f bin src/throttle/throttle.asm -o bin/throttle.com
```

---

## Pull Request Guidelines

- **One logical change per PR.** A bug fix and a new feature belong in separate PRs.
- **Reference the PLAN.md task number** if applicable (e.g., "Implements task 4.9").
- **All CI checks must pass.** The CI runs `build.py --skip-compile` + `boot_test.py`.
- **No copyrighted/abandonware binaries** in the repository. NPKG links to third-party
  sources; it never bundles them.
- **DOS-side files must use CR+LF line endings** and 8.3 filenames.
- **Commit format:** `<type>(<scope>): <subject>` — see [CLAUDE.md](CLAUDE.md).

---

## Community

- **GitHub Issues** — bug reports, feature requests, compatibility reports
- **GitHub Discussions** — general questions, ideas, show and tell
- **IRC** — `#nosdos` on Libera.Chat (for retro-appropriate discussion)
- **vogons.org** — for compatibility reports and game testing

---

## License

By contributing, you agree that your contributions will be licensed under GPL-2.0,
the same license that covers NOS-DOS.
