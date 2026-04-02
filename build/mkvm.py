#!/usr/bin/env python3
"""
NOS-DOS build/mkvm.py
Generate VM appliance files from the NOS-DOS HDD image.

Outputs (in out/):
  nosdos.ova          VirtualBox appliance (OVA)
  nosdos-vmware/      VMware bundle (VMX + VMDK)
  run-qemu.sh         QEMU launch script (Linux/macOS)
  run-qemu.bat        QEMU launch script (Windows)

Requirements:
  VirtualBox OVA:    VBoxManage on PATH
  VMware:            qemu-img on PATH  (converts VMDK format)
  QEMU scripts:      no external tools needed (just writes text)

Usage:
  python3 build/mkvm.py [--hdd <path>] [--out <dir>] [--no-vbox] [--no-vmware]
"""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path


# -----------------------------------------------------------------------
# Defaults
# -----------------------------------------------------------------------

HDD_IMAGE   = "out/nosdos.hdd"
ISO_IMAGE   = "out/nosdos.iso"
OUT_DIR     = "out"
VM_NAME     = "NOS-DOS"
VM_RAM_MB   = 64
VM_VRAM_MB  = 4
VM_DESC     = "NOS-DOS v1.0 — A curated DOS environment built on FreeDOS."


# -----------------------------------------------------------------------
# Helpers
# -----------------------------------------------------------------------

def run(cmd: list[str], check: bool = True) -> subprocess.CompletedProcess:
    print(f"  $ {' '.join(cmd)}")
    return subprocess.run(cmd, check=check)


def require_tool(name: str) -> bool:
    if shutil.which(name):
        return True
    print(f"  WARNING: '{name}' not found on PATH — skipping this target.")
    return False


# -----------------------------------------------------------------------
# VirtualBox OVA
# -----------------------------------------------------------------------

def make_vbox_ova(hdd: Path, out_dir: Path) -> None:
    print("\n[mkvm] Building VirtualBox OVA...")

    if not require_tool("VBoxManage"):
        return

    vm_name    = VM_NAME
    vmdk_path  = out_dir / "nosdos-vbox.vmdk"
    ova_path   = out_dir / "nosdos.ova"

    # Convert raw HDD image to VMDK.
    run(["qemu-img", "convert", "-f", "raw", "-O", "vmdk",
         str(hdd), str(vmdk_path)])

    # Unregister any leftover VM from a previous run (ignore error).
    subprocess.run(["VBoxManage", "unregistervm", vm_name, "--delete"],
                   capture_output=True)

    # Create VM.
    run(["VBoxManage", "createvm",
         "--name", vm_name,
         "--ostype", "DOS",
         "--register"])

    # Memory and display.
    run(["VBoxManage", "modifyvm", vm_name,
         "--memory",    str(VM_RAM_MB),
         "--vram",      str(VM_VRAM_MB),
         "--boot1",     "disk",
         "--boot2",     "none",
         "--boot3",     "none",
         "--boot4",     "none",
         "--audio-driver", "default",
         "--audiocontroller", "sb16",
         "--nic1",      "nat",
         "--description", VM_DESC])

    # Storage controller + disk.
    run(["VBoxManage", "storagectl", vm_name,
         "--name", "IDE", "--add", "ide", "--controller", "PIIX4"])
    run(["VBoxManage", "storageattach", vm_name,
         "--storagectl", "IDE",
         "--port", "0", "--device", "0",
         "--type", "hdd",
         "--medium", str(vmdk_path)])

    # Shared folder (NOSDOS → H:\).
    share_dir = Path.home() / "NOS-DOS-Share"
    share_dir.mkdir(exist_ok=True)
    run(["VBoxManage", "sharedfolder", "add", vm_name,
         "--name", "NOSDOS",
         "--hostpath", str(share_dir),
         "--automount-point", ""])

    # Export as OVA.
    run(["VBoxManage", "export", vm_name,
         "--output", str(ova_path),
         "--ovf20",
         "--manifest"])

    # Clean up: unregister (keep OVA).
    subprocess.run(["VBoxManage", "unregistervm", vm_name, "--delete"],
                   capture_output=True)

    vmdk_path.unlink(missing_ok=True)
    print(f"  OVA written to: {ova_path}")


# -----------------------------------------------------------------------
# VMware bundle
# -----------------------------------------------------------------------

def make_vmware(hdd: Path, out_dir: Path) -> None:
    print("\n[mkvm] Building VMware bundle...")

    if not require_tool("qemu-img"):
        return

    vm_dir  = out_dir / "nosdos-vmware"
    vm_dir.mkdir(exist_ok=True)

    vmdk    = vm_dir / "nosdos.vmdk"
    vmx     = vm_dir / "nosdos.vmx"

    # Convert raw image to VMDK (VMware compatible stream-optimised).
    run(["qemu-img", "convert", "-f", "raw", "-O", "vmdk",
         "-o", "subformat=streamOptimized",
         str(hdd), str(vmdk)])

    # Write VMX.
    vmx_content = f""".encoding = "UTF-8"
config.version = "8"
virtualHW.version = "9"
displayName = "{VM_NAME}"
annotation = "{VM_DESC}"

guestOS = "dos"
memsize = "{VM_RAM_MB}"
numvcpus = "1"

# IDE hard disk
ide0:0.present = "TRUE"
ide0:0.fileName = "nosdos.vmdk"
ide0:0.deviceType = "disk"

# Sound Blaster 16 emulation
sound.present = "TRUE"
sound.virtualDev = "sb16"
sound.autodetect = "TRUE"

# NAT networking
ethernet0.present = "TRUE"
ethernet0.connectionType = "nat"
ethernet0.virtualDev = "vlance"

# Display
svga.vramSize = "{VM_VRAM_MB * 1024 * 1024}"

# Shared folders (HGFS)
sharedFolder.option = "followSymlinks"
sharedFolder0.present = "TRUE"
sharedFolder0.enabled = "TRUE"
sharedFolder0.readAccess = "TRUE"
sharedFolder0.writeAccess = "TRUE"
sharedFolder0.hostPath = "$HOME/NOS-DOS-Share"
sharedFolder0.guestName = "NOSDOS"
sharedFolder.maxNum = "1"

# Boot order: disk only
bios.bootOrder = "hdd"
"""
    vmx.write_text(vmx_content)

    # Ensure share directory exists on host.
    share_readme = vm_dir.parent / "VMWARE-README.TXT"
    share_readme.write_text(
        "To use the host bridge:\n"
        "1. Create ~/NOS-DOS-Share on your host.\n"
        "2. In VMware: VM > Settings > Options > Shared Folders > Add\n"
        "   Name: NOSDOS, Path: ~/NOS-DOS-Share\n"
        "3. In DOS: NBRIDGE MOUNT\n"
    )

    print(f"  VMware bundle written to: {vm_dir}/")
    print(f"  Open {vmx} in VMware Workstation or Fusion.")


# -----------------------------------------------------------------------
# QEMU launch scripts
# -----------------------------------------------------------------------

def make_qemu_scripts(hdd: Path, out_dir: Path) -> None:
    print("\n[mkvm] Writing QEMU launch scripts...")

    hdd_abs = hdd.resolve()
    iso_abs = (hdd.parent / "nosdos.iso").resolve()

    sh_path  = out_dir / "run-qemu.sh"
    bat_path = out_dir / "run-qemu.bat"

    sh_content = f"""#!/usr/bin/env bash
# NOS-DOS QEMU launch script (Linux / macOS)
# Boots the El Torito ISO; nosdos.hdd is the C: drive.
# Creates ~/NOS-DOS-Share and mounts it as a vvfat drive (accessible as D:
# via SUBST or directly; use NBRIDGE MOUNT to map to H:).

SHARE="$HOME/NOS-DOS-Share"
mkdir -p "$SHARE"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

qemu-system-i386 \\
  -m {VM_RAM_MB} \\
  -cdrom "{iso_abs}" \\
  -boot d \\
  -drive file="{hdd_abs}",format=raw,index=0,media=disk \\
  -drive if=ide,index=1,format=vvfat,file=fat:rw:"$SHARE" \\
  -audiodev alsa,id=snd0 -device sb16,audiodev=snd0 \\
  -net nic,model=ne2k_isa \\
  -net user \\
  -vga std \\
  -display sdl \\
  "$@"
"""

    bat_content = f"""@ECHO OFF
REM NOS-DOS QEMU launch script (Windows)
REM Boots the El Torito ISO; nosdos.hdd is the C: drive.
REM Adjust QEMU_EXE path to match your installation.

SET QEMU_EXE=C:\\Program Files\\qemu\\qemu-system-i386.exe
SET ISO={iso_abs}
SET HDD={hdd_abs}
SET SHARE=%USERPROFILE%\\NOS-DOS-Share

IF NOT EXIST "%SHARE%" MKDIR "%SHARE%"

"%QEMU_EXE%" ^
  -m {VM_RAM_MB} ^
  -cdrom "%ISO%" ^
  -boot d ^
  -drive file="%HDD%",format=raw,index=0,media=disk ^
  -drive if=ide,index=1,format=vvfat,file=fat:rw:"%SHARE%" ^
  -audiodev dsound,id=snd0 -device sb16,audiodev=snd0 ^
  -net nic,model=ne2k_isa ^
  -net user ^
  -vga std ^
  -display sdl
"""

    sh_path.write_text(sh_content)
    sh_path.chmod(0o755)
    bat_path.write_text(bat_content)

    print(f"  {sh_path}  (Linux/macOS)")
    print(f"  {bat_path}  (Windows)")
    print("  Run the script to launch NOS-DOS in QEMU.")


# -----------------------------------------------------------------------
# Main
# -----------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(description="Generate NOS-DOS VM appliances.")
    parser.add_argument("--hdd",       default=HDD_IMAGE, help="Path to HDD image")
    parser.add_argument("--out",       default=OUT_DIR,   help="Output directory")
    parser.add_argument("--no-vbox",   action="store_true", help="Skip VirtualBox OVA")
    parser.add_argument("--no-vmware", action="store_true", help="Skip VMware bundle")
    parser.add_argument("--no-qemu",   action="store_true", help="Skip QEMU scripts")
    args = parser.parse_args()

    hdd     = Path(args.hdd)
    out_dir = Path(args.out)

    if not hdd.exists():
        print(f"ERROR: HDD image not found: {hdd}")
        print("Run 'python3 build/build.py' first.")
        return 1

    out_dir.mkdir(parents=True, exist_ok=True)

    if not args.no_qemu:
        make_qemu_scripts(hdd, out_dir)

    if not args.no_vmware:
        make_vmware(hdd, out_dir)

    if not args.no_vbox:
        make_vbox_ova(hdd, out_dir)

    print("\n[mkvm] Done.")
    print(f"  Outputs in: {out_dir.resolve()}/")
    return 0


if __name__ == "__main__":
    sys.exit(main())
