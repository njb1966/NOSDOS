#!/usr/bin/env bash
# NOS-DOS QEMU HDD boot script (Linux / macOS)
# Boots directly from the installed NOS-DOS hard disk image.
# Use this AFTER running the installer with run-qemu.sh.
#
# Creates ~/NOS-DOS-Share and mounts it as a vvfat drive (accessible as D:
# via SUBST or directly; use NBRIDGE MOUNT to map to H:).

SHARE="$HOME/NOS-DOS-Share"
mkdir -p "$SHARE"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

qemu-system-i386 \
  -m 64 \
  -boot c \
  -drive file="$SCRIPT_DIR/nosdos.hdd",format=raw,index=0,media=disk \
  -drive if=ide,index=1,format=vvfat,file=fat:rw:"$SHARE" \
  -audiodev alsa,id=snd0 -device sb16,audiodev=snd0 \
  -net nic,model=pcnet \
  -net user \
  -vga std \
  -display gtk,zoom-to-fit=on \
  "$@"
