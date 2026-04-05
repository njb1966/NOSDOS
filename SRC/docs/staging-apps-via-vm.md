# Staging Applications via the NOS-DOS VM

This document covers the workflow for:
1. Installing DOS applications inside the NOS-DOS VirtualBox VM
2. Extracting the installed files back to the host staging area
3. Packaging them as NPKG-ready ZIPs and publishing them

---

## Overview

Some applications (WordPerfect, FoxPro, WordStar, Norton Utilities, etc.) ship
as floppy disk images and require a proper DOS installer to run.  Rather than
trying to script the installation, we boot into the NOS-DOS VM, run the
installers from a data CD, then extract the installed trees from the disk image
back to the host.

---

## Prerequisites

- NOS-DOS VM running in VirtualBox with the blank disk attached
- `retrodev` distrobox available on the host
- SSH access to `website` (contabo server) configured

---

## Step 1 — The Data CD

A pre-built data ISO is stored at:

```
~/Downloads/NOSDOS-APPS.iso  (54 MB)
```

To rebuild it if needed (run inside retrodev):

```bash
distrobox enter retrodev -- python3 build/mkappscd.py
```

The script is at `build/mkappscd.py`.

### CD Contents

| Directory | App | Notes |
|-----------|-----|-------|
| `D:\DBASE\` | dBASE IV 2.0 | Loose files — copy directly |
| `D:\FOXPRO\DISK1`–`5` | FoxPro 2.0 | Floppy images extracted |
| `D:\FOXPRO\CKIT1`–`2` | FoxPro Connectivity Kit | |
| `D:\FOXPRO\DKIT1`–`3` | FoxPro Distribution Kit | |
| `D:\FOXPRO\PATCH\` | FoxPro Patches | Loose EXEs |
| `D:\KEEN\` | Commander Keen Ep.1 | Loose files |
| `D:\NU8\DISK1`–`4` | Norton Utilities 8 | Floppy images extracted |
| `D:\PCTOOLS9\` | PC Tools 9 | Loose files (233 files) |
| `D:\WOLF3D\` | Wolfenstein 3D Shareware | Loose files |
| `D:\WP51\INSTALL` | WordPerfect 5.1 — install disk | |
| `D:\WP51\PROG1` | WordPerfect 5.1 — program disk 1 | |
| `D:\WP51\PRNTR1`–`2` | WordPerfect 5.1 — printer disks | |
| `D:\WP51\PTRGPH` | WordPerfect 5.1 — printer graphics | |
| `D:\WP51\SPELLTH` | WordPerfect 5.1 — spell/thesaurus | |
| `D:\WS7\WS01`–`WS20` | WordStar 7.0 — 20 floppy disks | |
| `D:\WS7\WSLJ4` | WordStar 7.0 — LJ4 printer disk | |
| `D:\XTG\DISK1`–`2` | XTree Gold | Floppy images extracted |

---

## Step 2 — Attach the ISO to the VM

1. VirtualBox → select NOS-DOS VM → **Settings → Storage**
2. Click the CD/DVD drive entry → **"Choose a disk file..."**
3. Select `~/Downloads/NOSDOS-APPS.iso`
4. OK → Start VM

The CD-ROM is assigned **D:** by SHSUCDX.COM on boot.

---

## Step 3 — Install Applications in DOS

Boot into NOS-DOS and install each application.  Use the paths below.

### Install target conventions

| App type | Install path |
|----------|-------------|
| Utilities / productivity | `C:\APPS\<NAME>` |
| Games | `C:\GAMES\<NAME>` |

### Per-application install notes

#### DBASE IV (`C:\APPS\DBASE4`)
Already hosted — skip unless reinstalling.

#### Commander Keen Episode 1 (`C:\GAMES\KEEN1`)
```dos
MD C:\GAMES\KEEN1
COPY D:\KEEN\*.* C:\GAMES\KEEN1\
```
No installer — direct copy.

#### Wolfenstein 3D Shareware (`C:\GAMES\WOLF3D`)
```dos
D:\WOLF3D\INSTALL.EXE
```
Point install dir to `C:\GAMES\WOLF3D` when prompted.

#### PC Tools 9 (`C:\APPS\PCTOOLS9`)
```dos
D:\PCTOOLS9\INSTALL.EXE
```
Install to `C:\APPS\PCTOOLS9`.

#### Norton Utilities 8 (`C:\APPS\NORTON`)
```dos
D:\NU8\DISK1\INSTALL.EXE
```
When asked for disk 2, type `D:\NU8\DISK2` (and so on for disk 3, 4).
Install to `C:\APPS\NORTON`.

#### WordPerfect 5.1 (`C:\APPS\WP51`)
```dos
D:\WP51\INSTALL\INSTALL.EXE
```
When asked "Install from:", change `A:` → `D:\WP51\INSTALL`.
When prompted for subsequent disks, type `D:\WP51\PROG1`, `D:\WP51\PRNTR1`, etc.
Install to `C:\APPS\WP51`.

#### FoxPro 2.0 (`C:\APPS\FOXPRO`)
```dos
D:\FOXPRO\DISK1\INSTALL.EXE
```
When asked for disk source, type `D:\FOXPRO\DISK1` through `D:\FOXPRO\DISK5`.
Install to `C:\APPS\FOXPRO`.

#### WordStar 7.0 (`C:\APPS\WSTAR7`)
```dos
D:\WS7\WS01\INSTALL.EXE
```
When prompted for each disk, type `D:\WS7\WS02`, `D:\WS7\WS03`, etc.
Install to `C:\APPS\WSTAR7`.

#### XTree Gold (`C:\APPS\XTREE`)
```dos
D:\XTG\DISK1\INSTALL.EXE
```
When asked for disk 2, type `D:\XTG\DISK2`.
Install to `C:\APPS\XTREE`.

---

## Step 4 — Extract Installed Files to Host Staging

**Shut down the VM first.**  Then run in a terminal on the host (mtools must be
available — use retrodev if not on the host):

```bash
cd ~/projects/retro/NOSDOS/SRC
distrobox enter retrodev -- bash

./host_helpers/extract_from_vm.sh GAMES/KEEN1   KEEN1
./host_helpers/extract_from_vm.sh GAMES/WOLF3D  WOLF3D
./host_helpers/extract_from_vm.sh APPS/PCTOOLS9 PCTOOLS9
./host_helpers/extract_from_vm.sh APPS/NORTON   NORTON
./host_helpers/extract_from_vm.sh APPS/WP51     WP51
./host_helpers/extract_from_vm.sh APPS/FOXPRO   FOXPRO
./host_helpers/extract_from_vm.sh APPS/WSTAR7   WSTAR7
./host_helpers/extract_from_vm.sh APPS/XTREE    XTREE
```

Each call:
- Reads the directory from `out/nosdos-blank-flat.vmdk` at partition offset 32256
- Writes the files to `staging/<NAME>/`
- Prints next steps on completion

---

## Step 5 — Package and Publish Each App

Repeat these steps for each extracted staging directory.  Run inside retrodev.

### 5a. Create the ZIP

```bash
cd ~/projects/retro/NOSDOS/SRC/staging
zip -j <NAME>.ZIP <NAME>/*
# e.g.:
zip -j WP51.ZIP WP51/*
```

Check the size:
```bash
wc -c <NAME>.ZIP
```

### 5b. Upload to the package server

```bash
ssh website "mkdir -p /var/www/html/nosdos.njb1966.com/public_html/dist/<NAME>"
scp staging/<NAME>.ZIP website:/var/www/html/nosdos.njb1966.com/public_html/dist/<NAME>/<NAME>.ZIP

# Verify:
ssh website "curl -sI http://nosdos.njb1966.com/dist/<NAME>/<NAME>.ZIP | head -1"
```

### 5c. Update the .npkg definition

Edit `packages/<category>/<NAME>.NPKG` — update the `[SOURCE]` section:

```ini
[SOURCE]
URL1=http://nosdos.njb1966.com/dist/<NAME>/<NAME>.ZIP
Archive=<NAME>.ZIP
Bytes=<exact byte count from wc -c>
Redirect=no
```

Remove `URL2=` and any old archive.org URLs.

### 5d. Regenerate the index and push

```bash
cd ~/projects/retro/NOSDOS/SRC
python3 build/mkindex.py --verbose    # must show 0 errors
git add packages/
git commit -m "feat(npkg): host <NAME> ZIP; update URL1 and Bytes"
git push
```

GitHub Actions deploys the updated `.npkg` files and `packages.idx` automatically.

### 5e. Mark as done

Move the staged loose-files directory to `staging/DONE/`:
```bash
mv staging/<NAME> staging/DONE/
```

---

## Packages Remaining (as of 2026-04-05)

These are the packages in `packages/` that still point to archive.org and need
to go through this workflow:

| NPKG ID | .npkg category | Expected install path | Source on CD |
|---------|---------------|----------------------|--------------|
| KEEN1 | games | `C:\GAMES\KEEN1` | `D:\KEEN\` |
| WOLF3D | games | `C:\GAMES\WOLF3D` | `D:\WOLF3D\` |
| PCTOOLS | utilities | `C:\APPS\PCTOOLS9` | `D:\PCTOOLS9\` |
| NORTON | utilities | `C:\APPS\NORTON` | `D:\NU8\` |
| WP51 | wordprocessing | `C:\APPS\WP51` | `D:\WP51\` |
| FOXPRO | databases | `C:\APPS\FOXPRO` | `D:\FOXPRO\` |
| WSTAR7 | wordprocessing | `C:\APPS\WSTAR7` | `D:\WS7\` |
| XTREE | utilities | `C:\APPS\XTREE` | `D:\XTG\` |

Not yet sourced (no files in staging or NOSDOS-APPS):
- DOOM, DOOM2, DESCENT, DUKE3D, HERETIC, QUAKE, TYRIAN — shareware, fetch from archive.org
- PRINCE, LOTUS123, MASM611, QBASIC, TC30, TP70, ASEASY, SC, GALAXY, XYWRITE — need to source

---

## Appendix: mkappscd.py

The ISO build script is at `build/mkappscd.py`.  It reads from
`~/Downloads/NOSDOS-APPS/` and writes to `~/Downloads/NOSDOS-APPS.iso`.

To rebuild:
```bash
distrobox enter retrodev -- python3 build/mkappscd.py
```
