#!/usr/bin/env bash
# NOS-DOS QEMU launch script (Linux / macOS)
# Boots the El Torito installer ISO with a blank pre-formatted C: drive.
# Run this script to install NOS-DOS for the first time.
# After installation completes, use run-qemu-hdd.sh to boot from the disk.
#
# Creates ~/NOS-DOS-Share and mounts it as a vvfat drive (accessible as D:
# via SUBST or directly; use NBRIDGE MOUNT to map to H:).

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SHARE="$HOME/NOS-DOS-Share"
mkdir -p "$SHARE"

qemu-system-i386 \
  -m 64 \
  -cdrom "$SCRIPT_DIR/nosdos.iso" \
  -boot d \
  -drive file="$SCRIPT_DIR/nosdos-blank.hdd",format=raw,index=0,media=disk \
  -drive if=ide,index=1,format=vvfat,file=fat:rw:"$SHARE" \
  -audiodev alsa,id=snd0 -device sb16,audiodev=snd0 \
  -net nic,model=pcnet \
  -net user \
  -vga std \
  -display gtk,zoom-to-fit=on \
  "$@"
