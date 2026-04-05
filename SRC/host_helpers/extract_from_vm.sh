#!/usr/bin/env bash
# NOS-DOS: extract installed app directory from VM disk image.
#
# After running DOS installers in the VM and shutting it down, use this
# script to pull a directory tree out of the flat VMDK into staging/.
#
# Usage:
#   ./host_helpers/extract_from_vm.sh <DOS-path> <staging-name>
#
# Examples:
#   ./host_helpers/extract_from_vm.sh APPS/WP51    WP51
#   ./host_helpers/extract_from_vm.sh GAMES/WOLF3D WOLF3D
#   ./host_helpers/extract_from_vm.sh APPS/FOXPRO  FOXPRO
#
# Requires mtools (available in retrodev: distrobox enter retrodev -- bash).
# Run on the HOST (not inside the VM).  VM must be shut down.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FLAT="$SCRIPT_DIR/out/nosdos-blank-flat.vmdk"
STAGING="$SCRIPT_DIR/staging"
PART_OFFSET=32256   # 63 sectors * 512 bytes — FAT16 partition start

if [[ $# -lt 2 ]]; then
    echo "Usage: $0 <DOS-path> <staging-name>"
    echo "  DOS-path:     path inside C: drive, e.g. APPS/WP51"
    echo "  staging-name: output folder name in staging/, e.g. WP51"
    exit 1
fi

DOS_PATH="${1%%/}"          # strip trailing slash
DEST_NAME="$2"
DEST="$STAGING/$DEST_NAME"
IMG_SPEC="${FLAT}@@${PART_OFFSET}"

if [[ ! -f "$FLAT" ]]; then
    echo "ERROR: flat VMDK not found: $FLAT"
    echo "       Run VBoxManage convertfromraw or ./rebuild.sh first."
    exit 1
fi

# Verify the path exists in the image
if ! mdir -i "$IMG_SPEC" "::${DOS_PATH}" &>/dev/null; then
    echo "ERROR: ::${DOS_PATH} not found in $FLAT"
    echo "Available top-level directories:"
    mdir -i "$IMG_SPEC" :: 2>/dev/null | grep "^[A-Z]" || true
    exit 1
fi

if [[ -d "$DEST" ]]; then
    echo "WARNING: $DEST already exists — contents will be merged/overwritten."
    read -r -p "Continue? [y/N] " ans
    [[ "${ans,,}" == "y" ]] || exit 0
fi

mkdir -p "$DEST"
echo "[extract] Copying ::${DOS_PATH}/ → $DEST/"
mcopy -snm -i "$IMG_SPEC" "::${DOS_PATH}/" "$DEST/"
echo "[extract] Done. $(find "$DEST" | wc -l) items in $DEST"
echo ""
echo "Next steps:"
echo "  1. Verify contents look right: ls $DEST/"
echo "  2. Zip it (in retrodev): cd staging && zip -j ${DEST_NAME}.ZIP ${DEST_NAME}/*"
echo "  3. Upload: scp staging/${DEST_NAME}.ZIP website:/var/www/html/nosdos.njb1966.com/public_html/dist/${DEST_NAME}/${DEST_NAME}.ZIP"
echo "  4. Update packages/<cat>/${DEST_NAME}.NPKG — set URL1 and Bytes="
echo "  5. Run: python3 build/mkindex.py && git add -p && git commit && git push"
