NOS-DOS (NostalgicDOS)
=====================
A curated, ready-to-run DOS distribution built on FreeDOS.
Optimised for virtual machines. Opinionated. Fast. Clean.

"Boot fast. Work clean. Remember when computing just worked?"

WHAT IS NOS-DOS?
----------------
NOS-DOS is not a new operating system. It is a carefully assembled
experience layer on top of FreeDOS, pre-configured for VMs, with a
Norton Commander-style shell, integrated package management, and
host-bridge file exchange built in.

You get:
  - A fast, clean DOS environment that boots in under 10 seconds
  - NOS-SHELL: dual-pane file manager (F-key driven, mouse optional)
  - NPKG: package manager with 28+ curated DOS applications
  - NNET: networking wrappers around mTCP (ping, FTP, IRC, web)
  - NOS-BRIDGE: seamless file exchange with your host OS
  - NOS-PLAY: game launcher with automatic speed/sound profiles
  - NOS-THROTTLE: CPU speed limiter for timing-sensitive games

DOCUMENTATION
-------------
  README.TXT      This file
  QUICKSTART.TXT  Get started in 5 minutes
  COMMANDS.TXT    All NOS-DOS commands reference
  NPKG.TXT        Package manager guide
  GAMES.TXT       Game compatibility and launch guide
  BRIDGE.TXT      Host bridge (file exchange) setup

Press F1 in NOS-SHELL to open the help viewer.

SYSTEM REQUIREMENTS
-------------------
  RAM:    32 MB (64 MB recommended for games)
  Disk:   504 MB virtual hard disk (included)
  Video:  VGA (all VM platforms provide this)
  CPU:    Any x86 (486 or better recommended)
  VM:     VirtualBox, VMware Workstation/Fusion, or QEMU

CREDITS
-------
NOS-DOS stands on the shoulders of:
  FreeDOS    - The kernel and base utilities  (freedos.org)
  mTCP       - TCP/IP networking suite        (brutman.com/mTCP)
  JEMMEX     - Memory manager                 (github.com/Baron-von-Riedesel/Jemm)
  CTMOUSE    - Mouse driver                   (cutemouse.sourceforge.net)
  Open Watcom - C compiler                    (open-watcom.github.io)

LICENSE
-------
NOS-DOS system code is released under the GNU GPL v2.
Third-party components retain their original licenses.
Package definitions (NPKG) link to external sources only;
no copyrighted software is bundled.

SOURCE CODE
-----------
  https://github.com/nosdos/nosdos
