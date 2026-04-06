#!/usr/bin/env bash
# NOS-DOS rebuild helper — recompiles changed components, rebuilds disk
# images, and preserves the VirtualBox VMDK UUID so the registered VM
# does not need to be reconfigured after each rebuild.
#
# Usage:  ./rebuild.sh [--skip-compile]
#
# Runs compile step inside retrodev distrobox; image/VMDK steps on host.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VMDK="$SCRIPT_DIR/out/nosdos-blank.vmdk"
FLAT="$SCRIPT_DIR/out/nosdos-blank-flat.vmdk"
HDD="$SCRIPT_DIR/out/nosdos-blank.hdd"

SKIP_COMPILE=0
for arg in "$@"; do
    [[ "$arg" == "--skip-compile" ]] && SKIP_COMPILE=1
done

# ---- Step 1: compile (local — no distrobox) ----
if [[ $SKIP_COMPILE -eq 0 ]]; then
    echo "[rebuild] Compiling..."
    export WATCOM="${WATCOM:-$HOME/dos/tools/WATCOM}"
    export PATH="$WATCOM/binl64:$PATH"
    export INCLUDE="$WATCOM/h"
    for makefile in "$SCRIPT_DIR"/src/*/Makefile; do
        dir=$(dirname "$makefile")
        echo "  [wmake] $dir"
        (cd "$dir" && wmake -f Makefile 2>&1) || true
    done
fi

# ---- Step 2: build disk images ----
echo "[rebuild] Building disk images..."
python3 "$SCRIPT_DIR/build/mkhdd.py"
python3 "$SCRIPT_DIR/build/mkiso.py"

# ---- Step 3: preserve VMDK UUID ----
# If VirtualBox already has a VMDK registered at this path, capture its
# UUID before deleting the file so we can re-stamp the new VMDK.
OLD_UUID=""
if [[ -f "$VMDK" ]]; then
    OLD_UUID=$(VBoxManage showmediuminfo "$VMDK" 2>/dev/null \
               | grep -i "^UUID:" | awk '{print $2}' || true)
fi

# ---- Step 4: convert raw → VMDK ----
echo "[rebuild] Converting HDD to VMDK..."
rm -f "$VMDK" "$FLAT"
VBoxManage convertfromraw "$HDD" "$VMDK" --format VMDK --variant Fixed

# ---- Step 5: re-stamp UUID ----
if [[ -n "$OLD_UUID" ]]; then
    echo "[rebuild] Restoring VMDK UUID: $OLD_UUID"
    VBoxManage internalcommands sethduuid "$VMDK" "$OLD_UUID"
else
    echo "[rebuild] No prior UUID found — VirtualBox will need to re-register the VMDK."
fi

echo "[rebuild] Done. ISO: out/nosdos.iso  VMDK: out/nosdos-blank.vmdk"
