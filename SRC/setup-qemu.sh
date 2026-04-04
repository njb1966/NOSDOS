#!/usr/bin/env bash
# NOS-DOS Quick Setup for QEMU (Linux / macOS)
# ---------------------------------------------
# Run this script once to build NOS-DOS and launch the installer.
# After installation, use out/run-qemu-hdd.sh for all future sessions.
#
# Requirements (Ubuntu/Debian):
#   sudo apt install python3 mtools nasm xorriso qemu-system-x86

set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

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
echo -e "${BOLD}NOS-DOS Setup — QEMU${NC}"
echo "===================="
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
check_tool qemu-system-i386

# Accept either xorriso or genisoimage for ISO creation
if ! command -v xorriso &>/dev/null && ! command -v genisoimage &>/dev/null; then
    error "Not found: xorriso or genisoimage (need one of them)"
    MISSING=1
fi

if [ "$MISSING" -ne 0 ]; then
    echo ""
    echo "Install missing tools and re-run this script."
    echo ""
    echo "  Ubuntu/Debian:"
    echo "    sudo apt install python3 mtools nasm xorriso qemu-system-x86"
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
info "Building disk images (this takes about 30 seconds)..."
python3 build/build.py --skip-compile

echo ""
info "Generating QEMU launch scripts..."
python3 build/mkvm.py --no-vbox --no-vmware

echo ""
info "Build complete."

# ---------------------------------------------------------------------------
# Instructions
# ---------------------------------------------------------------------------

echo ""
echo -e "${BOLD}INSTALLATION INSTRUCTIONS${NC}"
echo "-------------------------"
echo ""
echo "The NOS-DOS installer is about to start in a QEMU window."
echo "Follow the on-screen prompts — installation takes under a minute."
echo ""
echo -e "${BOLD}When the installer says 'Remove the installation disc':${NC}"
echo "  1. Close the QEMU window."
echo "  2. Run:  ./out/run-qemu-hdd.sh"
echo ""
echo "run-qemu-hdd.sh boots from the hard disk with no ISO attached."
echo "Use it for every future NOS-DOS session."
echo ""
read -rp "Press Enter to launch the installer..."
echo ""

exec "$SCRIPT_DIR/out/run-qemu.sh"
