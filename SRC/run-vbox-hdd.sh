#!/usr/bin/env bash
# NOS-DOS — Boot from installed hard disk (VirtualBox)
# ----------------------------------------------------
# Run this after installation is complete, or for any future session.
# It ejects the installer ISO (if still attached) and starts the VM.

set -e

VM_NAME="NOS-DOS"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
BOLD='\033[1m'; NC='\033[0m'

info()  { echo -e "${GREEN}[nosdos]${NC} $*"; }
warn()  { echo -e "${YELLOW}[nosdos]${NC} $*"; }
error() { echo -e "${RED}[nosdos] ERROR:${NC} $*" >&2; }

# Check VM exists
if ! VBoxManage showvminfo "$VM_NAME" &>/dev/null; then
    error "VM '$VM_NAME' not found."
    echo ""
    echo "Run ./setup-vbox.sh first to install NOS-DOS."
    exit 1
fi

# Power off if running (ACPI shutdown request)
VM_STATE=$(VBoxManage showvminfo "$VM_NAME" --machinereadable \
    | grep '^VMState=' | cut -d'"' -f2)

if [ "$VM_STATE" = "running" ]; then
    warn "VM is running — sending power-off signal..."
    VBoxManage controlvm "$VM_NAME" poweroff
    sleep 2
fi

# Eject installer ISO (silently — it may already be empty)
VBoxManage storageattach "$VM_NAME" \
    --storagectl IDE --port 1 --device 0 \
    --type dvddrive --medium emptydrive 2>/dev/null || true

# Ensure boot order is HDD-first (DVD slot is now empty, but be explicit)
VBoxManage modifyvm "$VM_NAME" \
    --boot1 disk --boot2 none --boot3 none --boot4 none 2>/dev/null || true

info "Starting NOS-DOS..."
VBoxManage startvm "$VM_NAME" --type gui
