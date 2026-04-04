#!/usr/bin/env bash
# NOS-DOS Quick Setup for VirtualBox (Linux / macOS)
# ---------------------------------------------------
# Run this script once to build NOS-DOS and install it into a VirtualBox VM.
# After installation, use run-vbox-hdd.sh for all future sessions.
#
# Requirements (Ubuntu/Debian):
#   sudo apt install python3 mtools nasm xorriso qemu-utils virtualbox

set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

VM_NAME="NOS-DOS"
BLANK_VMDK="$SCRIPT_DIR/out/nosdos-blank.vmdk"
ISO="$SCRIPT_DIR/out/nosdos.iso"
SHARE="$HOME/NOS-DOS-Share"

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
BOLD='\033[1m'; NC='\033[0m'

info()  { echo -e "${GREEN}[setup]${NC} $*"; }
warn()  { echo -e "${YELLOW}[setup]${NC} $*"; }
error() { echo -e "${RED}[setup] ERROR:${NC} $*" >&2; }
die()   { error "$*"; exit 1; }

# ---------------------------------------------------------------------------
# Dependency check
# ---------------------------------------------------------------------------

echo ""
echo -e "${BOLD}NOS-DOS Setup — VirtualBox${NC}"
echo "=========================="
echo ""
info "Checking dependencies..."

MISSING=0

check_tool() {
    if ! command -v "$1" &>/dev/null; then
        error "Not found: $1"
        MISSING=1
    fi
}

check_tool python3
check_tool mformat        # mtools
check_tool mcopy          # mtools
check_tool nasm
check_tool VBoxManage
check_tool qemu-img       # qemu-utils — mkvm.py uses this to create nosdos-blank.vmdk

if ! command -v xorriso &>/dev/null && ! command -v genisoimage &>/dev/null; then
    error "Not found: xorriso or genisoimage (need one of them)"
    MISSING=1
fi

if [ "$MISSING" -ne 0 ]; then
    echo ""
    echo "Install missing tools and re-run this script."
    echo ""
    echo "  Ubuntu/Debian:"
    echo "    sudo apt install python3 mtools nasm xorriso qemu-utils virtualbox"
    echo ""
    exit 1
fi

info "All dependencies found."

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------

echo ""
info "Fetching third-party dependencies (FreeDOS, JEMMEX, mTCP, CTMOUSE)..."
python3 build/fetch_deps.py

echo ""
info "Building disk images and VMDK (this takes about 30 seconds)..."
python3 build/build.py --skip-compile

# ---------------------------------------------------------------------------
# VirtualBox VM registration
# ---------------------------------------------------------------------------

echo ""
info "Registering VirtualBox VM..."

# Remove any leftover VM from a previous run of this script
VBoxManage unregistervm "$VM_NAME" --delete 2>/dev/null && \
    warn "Removed existing '$VM_NAME' VM (leftover from a previous setup run)." || true

VBoxManage createvm \
    --name "$VM_NAME" \
    --ostype DOS \
    --register

VBoxManage modifyvm "$VM_NAME" \
    --memory 64 \
    --vram 4 \
    --boot1 dvd \
    --boot2 disk \
    --boot3 none \
    --boot4 none \
    --audio-driver default \
    --audiocontroller sb16 \
    --nic1 nat \
    --description "NOS-DOS v1.0 — A curated DOS environment built on FreeDOS."

VBoxManage storagectl "$VM_NAME" \
    --name IDE --add ide --controller PIIX4

# Blank HDD at IDE port 0 — installer writes NOS-DOS here
VBoxManage storageattach "$VM_NAME" \
    --storagectl IDE --port 0 --device 0 \
    --type hdd --medium "$BLANK_VMDK"

# Installer ISO at IDE port 1 (DVD)
VBoxManage storageattach "$VM_NAME" \
    --storagectl IDE --port 1 --device 0 \
    --type dvddrive --medium "$ISO"

# Shared folder (host ↔ DOS file exchange via NBRIDGE)
mkdir -p "$SHARE"
VBoxManage sharedfolder add "$VM_NAME" \
    --name NOSDOS \
    --hostpath "$SHARE" \
    --automount 2>/dev/null || \
    warn "Shared folder setup skipped (may not be supported on this VirtualBox version)."

info "VM '$VM_NAME' registered and configured."

# ---------------------------------------------------------------------------
# Instructions
# ---------------------------------------------------------------------------

echo ""
echo -e "${BOLD}INSTALLATION INSTRUCTIONS${NC}"
echo "-------------------------"
echo ""
echo "The NOS-DOS installer is about to start in a VirtualBox window."
echo "Follow the on-screen prompts — installation takes under a minute."
echo ""
echo -e "${BOLD}When the installer says 'Remove the installation disc':${NC}"
echo ""
echo "  Option A (simplest — do this in the VirtualBox window):"
echo "    Devices -> Optical Drives -> Remove Disk from Virtual Drive"
echo "    Then press Enter in the VM."
echo ""
echo "  Option B (from this terminal):"
echo "    Open a new terminal and run:  ./run-vbox-hdd.sh"
echo "    That will eject the disc and reboot the VM from the hard disk."
echo ""
echo "After first boot from the hard disk, use run-vbox-hdd.sh (or just"
echo "open VirtualBox and start 'NOS-DOS') for all future sessions."
echo ""
read -rp "Press Enter to start the VM..."
echo ""

VBoxManage startvm "$VM_NAME" --type gui
