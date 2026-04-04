# NPKG Package Format Specification

> Version: 1.0
> Applies to: NOS-DOS NPKG v1.x

---

## Overview

An NPKG package is a plain-text INI-style definition file (`.npkg` extension) that
describes a piece of DOS software: where to download it, how to install it, what
hardware it requires, and how to launch it.  Package definitions live in this
repository under `packages/<category>/`.  The NPKG tool downloads definitions on
demand and executes the workflow described in each file.

NPKG does **not** bundle the software itself.  It links to canonical third-party
sources (archive.org mirrors, publisher FTP archives, etc.) and downloads at
install time.  This keeps the repository free of copyrighted binaries.

---

## Repository Layout

```
packages/
  README.md                   <- this file (the spec)
  packages.idx                <- master index, regenerated from all .npkg files
  communications/
    TELIX.NPKG
    PROCOMM.NPKG
  databases/
    DBASE3.NPKG
    FOXPRO.NPKG
  games/
    DOOM.NPKG
    WOLF3D.NPKG
    PRINCE.NPKG
    KEEN1.NPKG
    DOOM2.NPKG
  programming/
    TC30.NPKG
    TP70.NPKG
    QBASIC.NPKG
    MASM611.NPKG
  spreadsheets/
    LOTUS123.NPKG
    ASEASY.NPKG
    SC.NPKG
  utilities/
    NORTON.NPKG
    PCTOOLS.NPKG
    XTREE.NPKG
  wordprocessing/
    GALAXY.NPKG
    WP51.NPKG
    WSTAR7.NPKG
    XYWRITE.NPKG
```

### Filename Rules

- **Package ID**: 1–8 uppercase ASCII characters (`[A-Z0-9_]`).  Matches the
  basename of the `.npkg` file (`DOOM` → `DOOM.NPKG`).
- **Extension**: `.npkg` (4-char extension — requires DOSLFN when cached on the
  DOS HDD; the NPKG tool handles this transparently by also accepting `.NPK`).
- **Category directory names**: lowercase, ASCII only.

---

## packages.idx — Master Index File

`packages.idx` is a tab-separated flat file, one record per package.  It is the
only file NPKG downloads during `NPKG UPDATE`; individual `.npkg` files are
fetched on demand.

### Format

```
ID<TAB>Category<TAB>Name<TAB>Version<TAB>Description<TAB>SizeKB<TAB>License
```

- Fields separated by a single ASCII tab (`\t`, 0x09).
- No quoted fields; no embedded tabs in any field value.
- Lines terminated by CR+LF (`\r\n`).
- First line is a header: `#NPKG-INDEX-1.0` (starts with `#`, skipped by parser).
- Lines beginning with `#` are comments and must be skipped.
- Maximum line length: 255 characters.
- File is sorted by ID, ascending, case-insensitive.

### Field Constraints

| Field       | Max Length | Notes                                          |
|-------------|-----------|------------------------------------------------|
| ID          | 8         | Uppercase; matches .npkg basename              |
| Category    | 16        | Matches subdirectory name                      |
| Name        | 40        | Display name; ASCII only                       |
| Version     | 12        | Free-form version string                       |
| Description | 60        | One-line summary for search results display    |
| SizeKB      | 6         | Download size in KB (integer); 0 if unknown    |
| License     | 12        | freeware, shareware, commercial, gpl, bsd, mit |

### Example

```
#NPKG-INDEX-1.0
DOOM	games	DOOM Shareware	1.9	First-person shooter shareware episode	2335	shareware
GALAXY	wordprocessing	Galaxy Write	4.0	Lightweight full-featured word processor	456	freeware
LOTUS123	spreadsheets	Lotus 1-2-3	2.01J	Classic DOS spreadsheet	1240	commercial
TC30	programming	Turbo C	3.0	Borland C IDE and compiler	2100	freeware
```

---

## .npkg File Format

### General Rules

- Plain ASCII text; CR+LF or LF line endings both accepted.
- INI-style: `Key=Value` pairs grouped under `[SECTION]` headers.
- Section headers and key names are case-insensitive.
- Lines beginning with `;` or `#` are comments and are ignored.
- Blank lines are ignored.
- Values must not contain TAB characters.
- Values must not contain `=` except as part of the value (first `=` on the line
  is the key/value separator).
- Keys not recognised within a section are silently ignored (forward compatibility).
- All sections except `[PACKAGE]` are optional.

### Sections

```
[PACKAGE]       Required  Package identity and metadata
[HARDWARE]      Optional  Hardware and memory requirements
[SOURCE]        Required  Where to download the archive
[INSTALL]       Required  How to install it
[POST-INSTALL]  Optional  Batch lines run after extraction
[LAUNCHER]      Optional  NOS-SHELL F9 integration
[GAME]          Optional  Game-specific settings (read by NOSPLAY)
[REMOVE]        Optional  Batch lines run on uninstall
```

---

### [PACKAGE] — Identity and Metadata

| Key         | Required | Max | Description                                       |
|-------------|----------|-----|---------------------------------------------------|
| ID          | Yes      | 8   | Unique package identifier; uppercase; `[A-Z0-9_]` |
| Name        | Yes      | 40  | Full display name                                 |
| Version     | Yes      | 12  | Version string (free-form)                        |
| Category    | Yes      | 16  | `wordprocessing` `spreadsheet` `database`         |
|             |          |     | `programming` `communications` `utility` `game`   |
| Description | Yes      | 60  | One-line summary for search results               |
| Author      | No       | 40  | Original author or publisher                      |
| Year        | No       | 4   | Four-digit release year                           |
| License     | Yes      | 12  | `freeware` `shareware` `commercial` `gpl`         |
|             |          |     | `bsd` `mit` `public domain`                       |
| Requires    | No       | 80  | Space-separated list of prerequisite package IDs  |
| Tags        | No       | 80  | Space-separated tags for search (`text editor`)   |

**Validation:**
- `ID` must match the `.npkg` filename basename exactly (case-insensitive).
- `Category` must match one of the listed values.
- `License` must match one of the listed values.
- `Requires` IDs must each be ≤8 chars, `[A-Z0-9_]`.

---

### [HARDWARE] — Requirements

All numeric fields default to 0 if the section or key is absent (meaning "no
requirement").

| Key      | Type    | Default | Description                                        |
|----------|---------|---------|----------------------------------------------------|
| Memory   | integer | 0       | Minimum conventional memory required (KB)          |
| EMS      | integer | 0       | EMS memory required (KB); 0 = not needed           |
| XMS      | integer | 0       | XMS memory required (KB); 0 = not needed           |
| Sound    | string  | none    | `none` `speaker` `adlib` `sb` `sb16` `gus`         |
| Mouse    | boolean | no      | `yes` or `no`                                      |
| VGA      | boolean | no      | `yes` = VGA required; `no` = CGA/EGA compatible    |
| CPUSpeed | string  | any     | `slow` (≤10MHz) `medium` (≤66MHz) `fast` (any)    |
| Drives   | integer | 1       | Number of drive letters required during install    |
| DiskKB   | integer | 0       | Installed disk space required (KB)                 |

**Boolean fields:** `yes`, `true`, `1` → true; `no`, `false`, `0` → false.
Case-insensitive.

---

### [SOURCE] — Download Information

| Key     | Required | Max  | Description                                          |
|---------|----------|------|------------------------------------------------------|
| URL1    | Yes      | 255  | Primary download URL (HTTP only — mTCP HTGET)        |
| URL2    | No       | 255  | Fallback URL if URL1 fails                           |
| URL3    | No       | 255  | Second fallback URL                                  |
| Archive | Yes      | 64   | Filename to save the downloaded archive as           |
| Bytes   | No       | 10   | Download size in bytes (for progress display)        |
| MD5     | No       | 32   | MD5 hex digest for archive integrity check           |
| Redirect| No       | 3    | `yes` if URL follows HTTP redirects (archive.org)   |

**URL constraints:**
- HTTP only.  HTTPS is not supported by mTCP HTGET.
- archive.org URLs frequently redirect: set `Redirect=yes` for these.
- `Archive` must be a valid 8.3 filename when the `.npkg` file is used on DOS
  without DOSLFN.  Use the short name if the real filename is longer.

---

### [INSTALL] — Installation Procedure

| Key        | Required | Max | Description                                         |
|------------|----------|-----|-----------------------------------------------------|
| InstallDir | Yes      | 64  | Target directory on C: (e.g. `C:\APPS\DOOM`)        |
| Extract    | No       | 3   | `yes` (default) — extract the archive after download|
| Extractor  | No       | 8   | `unzip` (default) `unarj` `lha` `pkzip` `copy`     |
| StripDir   | No       | 3   | `yes` — strip the top-level archive directory       |
| SetPath    | No       | 3   | `yes` — add `InstallDir` to PATH in AUTOEXEC.BAT    |
| SetVar     | No       | 128 | `VAR=value` pairs, one per line, set in AUTOEXEC.BAT|

**`Extractor` values:**
- `unzip` — calls `UNZIP.EXE -o <archive> -d <InstallDir>`
- `unarj` — calls `UNARJ.EXE e <archive>` (must `cd` to `InstallDir` first)
- `lha`   — calls `LHA.EXE e <archive>` (must `cd` to `InstallDir` first)
- `pkzip` — calls `PKUNZIP.EXE -o <archive> <InstallDir>`
- `copy`  — copies the archive as-is to `InstallDir` (for .COM/.EXE singles)

**`SetVar`** — one assignment per logical line; use `\n` within the value to
encode multiple assignments:

```ini
SetVar=DOOMWADDIR=C:\APPS\DOOM
```

Multiple `SetVar` keys are allowed; all are applied in order.

---

### [POST-INSTALL] — Post-installation Commands

This section contains DOS batch commands executed after extraction, in the order
they appear.  Each non-blank, non-comment line is one batch statement.

```ini
[POST-INSTALL]
; Create required directories
MD C:\APPS\DOOM\SAVE
MD C:\APPS\DOOM\DEMO
; Copy default config
COPY C:\NOS\NPKG\DEFS\DOOM.CFG C:\APPS\DOOM\DEFAULT.CFG
```

**Constraints:**
- Lines are executed via `COMMAND.COM /C <line>` — full DOS command syntax.
- Maximum line length: 127 characters (DOS COMMAND.COM limit).
- Variables like `%INSTALLDIR%`, `%NPKGID%`, `%NPKGVER%` are expanded before
  execution (substituted by NPKG before calling COMMAND.COM):

| Variable      | Expands to                                           |
|---------------|------------------------------------------------------|
| `%INSTALLDIR%`| Value of `InstallDir` from `[INSTALL]`               |
| `%NPKGID%`    | Package ID (e.g. `DOOM`)                             |
| `%NPKGVER%`   | Package version string                               |
| `%NPKGDIR%`   | `C:\NOS\NPKG\` — NPKG data root                      |
| `%DEFSDIR%`   | `C:\NOS\NPKG\DEFS\` — cached definition files        |

- The POST-INSTALL block runs with the current directory set to `InstallDir`.
- A non-zero exit code from any line is reported as a warning but does not abort
  installation (batch does not propagate exit codes reliably across all versions).

---

### [LAUNCHER] — NOS-SHELL F9 Integration

| Key   | Required | Max | Description                                          |
|-------|----------|-----|------------------------------------------------------|
| Label | Yes      | 20  | Display name in the F9 launcher menu                 |
| Exec  | Yes      | 64  | Command to run when selected                         |
| Dir   | No       | 64  | Working directory (default: `InstallDir`)            |
| Icon  | No       | 3   | CP437 character code (decimal) for the menu icon     |

If `[LAUNCHER]` is absent, the package is not added to the F9 launcher.

**`Icon` codes** (suggested conventions):

| Code | Char | Meaning        |
|------|------|----------------|
| 254  | ■    | Default / misc |
| 229  | σ    | Programming    |
| 227  | π    | Math/Spreadsheet|
| 248  | °    | Utility        |
| 2    | ☻    | Game           |
| 1    | ☺    | Communications |

---

### [GAME] — Game-Specific Settings

Only relevant when `Category=game`.  Read by `NOSPLAY` (Phase 6).

| Key        | Required | Max | Description                                          |
|------------|----------|-----|------------------------------------------------------|
| CPUPreset  | Yes      | 10  | `SLOW477` `SLOW10` `SLOW33` `SLOW66` `SLOW100` `OFF` |
| MemProfile | Yes      | 4   | `STD` `MAX` `EMS` `GAME`                             |
| SoundEnv   | No       | 80  | `BLASTER=A220 I5 D1 H5 P330 T6` (full BLASTER var)  |
| MusicExt   | No       | 4   | `OPL` `GUS` `MIDI` `none`                            |
| Notes      | No       | 200 | Compatibility notes; shown by `NOSPLAY INFO`         |
| SaveDir    | No       | 64  | Save game directory (for backup by NOSPLAY)          |

**`CPUPreset` mapping** (nominal MHz target):

| Preset   | Target   | Notes                                     |
|----------|----------|-------------------------------------------|
| SLOW477  | ~4.77 MHz| XT-speed games (original Commander Keen)  |
| SLOW10   | ~10 MHz  | Early 286 games                           |
| SLOW33   | ~33 MHz  | 386/486 era (Wolfenstein 3D, DOOM)        |
| SLOW66   | ~66 MHz  | High-speed 486 (DOOM 2, Dark Forces)      |
| SLOW100  | ~100 MHz | Pentium era                               |
| OFF      | Full     | No throttle — runs at VM full speed       |

---

### [REMOVE] — Uninstall Commands

Batch lines executed during `NPKG REMOVE`, in order.  Same variable substitution
rules as `[POST-INSTALL]`.

```ini
[REMOVE]
DELTREE /Y %INSTALLDIR%
```

**Constraints:**
- NPKG automatically removes the launcher entry and the registry record.
  `[REMOVE]` only needs to handle filesystem cleanup and env var rollback.
- NPKG does **not** automatically remove `SetPath` or `SetVar` additions from
  AUTOEXEC.BAT.  If the package modified AUTOEXEC.BAT, `[REMOVE]` must undo it.
- If `[REMOVE]` is absent, NPKG removes the launcher entry and registry record
  but leaves the installed files in place (with a warning to the user).

---

## Complete Annotated Example

```ini
; DOOM.NPKG — DOOM Shareware v1.9
; Category: games
; NOS-DOS package definition v1.0

[PACKAGE]
ID=DOOM
Name=DOOM Shareware
Version=1.9
Category=game
Description=id Software's classic first-person shooter, episode 1 shareware
Author=id Software
Year=1993
License=shareware
Tags=fps shooter action 3d

[HARDWARE]
Memory=530
EMS=0
XMS=2048
Sound=sb
Mouse=no
VGA=yes
CPUSpeed=medium
DiskKB=6000

[SOURCE]
URL1=https://archive.org/download/doom-shareware-1.9/doom19s.zip
URL2=https://archive.org/download/DoomShareware/doom19s.zip
Archive=DOOM19S.ZIP
Bytes=2397849
MD5=90facab21eefe84cd43461896f9e6a97
Redirect=yes

[INSTALL]
InstallDir=C:\APPS\DOOM
Extract=yes
Extractor=unzip
StripDir=no
SetPath=no
SetVar=DOOMWADDIR=C:\APPS\DOOM

[POST-INSTALL]
; Create save directory
MD %INSTALLDIR%\SAVE
; Report success
ECHO DOOM installed to %INSTALLDIR%.

[LAUNCHER]
Label=DOOM Shareware
Exec=C:\APPS\DOOM\DOOM.EXE
Dir=C:\APPS\DOOM
Icon=2

[GAME]
CPUPreset=SLOW33
MemProfile=GAME
SoundEnv=BLASTER=A220 I5 D1 H5 P330 T6
MusicExt=OPL
Notes=Requires 530KB+ conventional and 2MB XMS. Run NOSMEM /GAME first.
SaveDir=C:\APPS\DOOM\SAVE

[REMOVE]
DELTREE /Y %INSTALLDIR%
```

---

## Validation Rules Summary

A `.npkg` file is **valid** if:

1. `[PACKAGE]` section present with all required keys populated.
2. `[SOURCE]` section present with `URL1` and `Archive`.
3. `[INSTALL]` section present with `InstallDir`.
4. `ID` value matches the `.npkg` filename basename (case-insensitive).
5. `Category` is one of the defined category values.
6. `License` is one of the defined license values.
7. `Extractor` (if present) is one of the defined extractor values.
8. `CPUPreset` (if present) is one of the defined preset values.
9. `MemProfile` (if present) is one of `STD`, `MAX`, `EMS`, `GAME`.
10. All field values fit within the documented maximum lengths.
11. All batch lines in `[POST-INSTALL]` and `[REMOVE]` are ≤127 characters.
12. `Bytes` (if present) is a positive integer.
13. `MD5` (if present) is exactly 32 hex characters.
14. No required field contains only whitespace.

---

## packages.idx Generation

`packages.idx` is generated by the host-side tool `build/mkindex.py` (to be
written in Phase 4).  It scans all `.npkg` files under `packages/`, validates
each, extracts the index fields, sorts by ID, and writes `packages.idx`.

Running `python3 build/mkindex.py` regenerates the index.  The index must be
committed alongside any `.npkg` changes.  CI enforces this.

### Index Entry Mapping

| Index field | Source                           |
|-------------|----------------------------------|
| ID          | `[PACKAGE] ID`                   |
| Category    | `[PACKAGE] Category`             |
| Name        | `[PACKAGE] Name`                 |
| Version     | `[PACKAGE] Version`              |
| Description | `[PACKAGE] Description`          |
| SizeKB      | `[SOURCE] Bytes` ÷ 1024, rounded |
| License     | `[PACKAGE] License`              |

---

## Adding a New Package

1. Choose a package ID (≤8 chars, uppercase, unique in the index).
2. Create `packages/<category>/ID.NPKG`.
3. Fill in all required sections following this spec.
4. Verify the download URL works and the archive extracts cleanly.
5. Run `python3 build/mkindex.py` to regenerate `packages.idx`.
6. Run `python3 tests/pkg_validate.py packages/<category>/ID.NPKG` (Phase 4 test).
7. Submit a pull request with both the `.npkg` file and the updated `packages.idx`.

---

## Design Decisions

**Why INI-style?**  Readable and writable by both the DOS NPKG C parser and
host-side Python tools.  No binary format needed.  Easy to diff in version
control.  DOS `C89` string parsing of `KEY=VALUE` lines is trivial to implement.

**Why no bundled binaries?**  Copyright.  Almost all classic DOS software is
still under copyright.  NPKG links to third-party archives; the user downloads
at install time.  NPKG definitions themselves carry no legal risk.

**Why archive.org as primary source?**  Largest, most stable collection of
preserved DOS software.  Freeware and shareware episodes are hosted with implicit
or explicit permission.  Commercial titles are handled on a case-by-case basis.

**Why HTTP and not FTP?**  mTCP HTGET is the most reliable download tool
available in the DOS environment.  FTP support exists but HTTP is simpler for
automated installs.  All archive.org content is available over HTTP.

**Why one `.npkg` file per package?**  Keeps definitions atomic and independently
versionable.  Easy to review changes, easy to add new packages without touching
existing ones.  The `packages.idx` file aggregates them for searching.
```
