; NOS-DOS: MBR chainloader
; mbr.asm - minimal MBR: find active partition, load its VBR via LBA, jump.
;
; KEY DESIGN NOTE: INT 13h AH=42h loads the VBR to 0x7C00, which overwrites
; this MBR code entirely.  All variables and the DAP must therefore live on
; the stack (SS:SP < 0x7C00) so they are not clobbered before use.
; DL (drive number) is an INPUT to INT 13h and is NOT modified by the call,
; so it survives across the read and can be passed directly to the VBR.
;
; Assembled with: nasm -f bin mbr.asm -o mbr.bin
; License: GPL-2.0

BITS 16
ORG 0x7C00

start:
    cli
    xor     ax, ax
    mov     ss, ax
    mov     sp, 0x7BE0          ; stack well below 0x7C00 — DAP lives here
    sti
    mov     ds, ax
    mov     es, ax
    ; DL = drive number passed by BIOS (0x80 for first HDD) — keep it

    ; Scan partition table (0x7C00 + 446 = 0x7DBE) for the active entry
    mov     bx, 0x7DBE
    mov     cx, 4
.search:
    cmp     byte [bx], 0x80
    je      .found
    add     bx, 16
    loop    .search

    ; No active partition — print message and halt
    mov     si, msg_nopart
    call    print
    jmp     $

.found:
    ; Build Disk Address Packet on the stack.
    ; Stack grows DOWN; after 8 pushes (16 bytes) SP = 0x7BD0.
    ; Layout at SP (low addr = first byte BIOS reads):
    ;   +0  size=0x10, reserved=0x00
    ;   +2  count=1
    ;   +4  buf_off=0x7C00
    ;   +6  buf_seg=0x0000
    ;   +8  LBA bits 0-15
    ;   +10 LBA bits 16-31
    ;   +12 LBA bits 32-47  (always 0 for < 2TB)
    ;   +14 LBA bits 48-63  (always 0)
    push    word 0              ; LBA[48-63]
    push    word 0              ; LBA[32-47]
    push    word [bx+10]        ; LBA[16-31] from partition entry
    push    word [bx+8]         ; LBA[0-15]  from partition entry
    push    word 0x0000         ; buffer segment
    push    word 0x7C00         ; buffer offset  — load VBR here
    push    word 0x0001         ; sector count = 1
    push    word 0x0010         ; DAP size = 16 (0x10), reserved = 0

    mov     si, sp              ; SI -> DAP (INT 13h reads it from DS:SI)
    mov     ah, 0x42
    int     0x13                ; *** loads VBR to 0x7C00 — MBR is now gone ***

    ; After this point [bx], [si], msg_* etc. are all invalid — VBR data is there.
    ; DL is unchanged (it was an input parameter).
    ; Jump straight to the VBR.
    jmp     0x0000:0x7C00

; -----------------------------------------------------------------------
; print — output NUL-terminated string at DS:SI via INT 10h TTY
; (Only reachable before INT 13h, while MBR code is still intact)
; -----------------------------------------------------------------------
print:
    lodsb
    or      al, al
    jz      .done
    mov     ah, 0x0E
    xor     bx, bx
    int     0x10
    jmp     print
.done:
    ret

msg_nopart  db  "No active partition", 0x0D, 0x0A, 0

times 446-($-$$) db 0       ; pad to partition table boundary
                             ; partition table (64 bytes) + 0xAA55 follow on disk
