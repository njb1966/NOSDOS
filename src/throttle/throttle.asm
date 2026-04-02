; NOS-DOS: NOS-THROTTLE
; throttle.asm - CPU speed limiter TSR.
;
; Hooks INT 08h (18.2 Hz system timer).  After chaining to the original
; handler it executes a calibrated busy-wait loop, burning CPU time and
; reducing the effective throughput visible to applications.
;
; The busy-wait is: outer_count * 1024 iterations of a tight DEC/JNZ loop.
; outer_count=0 means no throttling.  TCTL CALIBRATE computes the correct
; values for the host CPU speed; placeholder defaults are sized for a
; modern VM (1–3 GHz) running Prince of Persia / early DOS games.
;
; Shared data layout (CS-relative, used by TCTL.EXE via MK_FP):
;   CS:0x100  signature "THROTTLE" (8 bytes)
;   CS:0x108  speed_level  (word)  0=OFF 1=SLOW100 2=SLOW66 3=SLOW33
;                                  4=SLOW10 5=SLOW477
;   CS:0x10A  delay_count  (word)  current outer loop count
;   CS:0x10C  presets[6]   (word×6, 12 bytes)
;   CS:0x118  old_int08    (dword)
;   CS:0x11C  old_int09    (dword)
;
; Hotkeys (checked in INT 09h hook):
;   Ctrl+Alt+KP_Plus   -- increase throttle level (more slowdown)
;   Ctrl+Alt+KP_Minus  -- decrease throttle level (less slowdown)
;   Ctrl+Alt+0         -- set level 0 (OFF, no delay)
;
; Usage:
;   THROTTLE          -- install
;   THROTTLE /U       -- uninstall
;   THROTTLE /S       -- show current level
;   THROTTLE /L<n>    -- install and set level n (0-5) immediately
;
; Resident footprint: < 700 bytes.
; License: GPL-2.0

cpu 8086
bits 16
org 0x100

; -----------------------------------------------------------------------
; Shared data area (offsets documented above for TCTL.EXE)
; -----------------------------------------------------------------------

; CS:0x100
signature    db  'THROTTLE'       ; 8 bytes, no NUL needed

; CS:0x108
speed_level  dw  0                ; current preset index (0-5)

; CS:0x10A
delay_count  dw  0                ; outer loop count for current level

; CS:0x10C  presets[6]: outer counts per level
;   Default placeholder values assume ~1 GHz VM.
;   TCTL CALIBRATE overwrites these with measured values.
;   Preset 0 (OFF):      0
;   Preset 1 (SLOW100):  0        (no reduction -- same as OFF)
;   Preset 2 (SLOW66):   5400     (~33% waste, ~66% effective)
;   Preset 3 (SLOW33):   10800    (~67% waste, ~33% effective)
;   Preset 4 (SLOW10):   16200    (~90% waste, ~10% effective)
;   Preset 5 (SLOW477):  17900    (~99% waste, ~1% effective)
presets      dw  0, 0, 5400, 10800, 16200, 17900

; CS:0x118
old_int08    dd  0

; CS:0x11C
old_int09    dd  0

; Scratch byte for INT 09h scan latch.
scratch_scan db  0

; -----------------------------------------------------------------------
; INT 08h hook -- timer tick
; -----------------------------------------------------------------------
int08_hook:
    ; Chain to original first (maintains system time, INT 1Ch, etc.)
    pushf
    call far [cs:old_int08]

    ; Skip delay if level = 0 (OFF).
    cmp  word [cs:delay_count], 0
    je   .done08

    ; Busy-wait: outer_count * 1024 DEC/JNZ iterations.
    push ax
    push cx
    push dx

    mov  ax, [cs:delay_count]
.outer:
    mov  cx, 1024
.inner:
    dec  cx
    jnz  .inner
    dec  ax
    jnz  .outer

    pop  dx
    pop  cx
    pop  ax

.done08:
    iret

; -----------------------------------------------------------------------
; INT 09h hook -- keyboard hardware interrupt
; -----------------------------------------------------------------------
int09_hook:
    ; Latch scan code before calling original.
    push ax
    push ds
    push cs
    pop  ds
    in   al, 0x60
    mov  [scratch_scan], al
    pop  ds
    pop  ax

    ; Chain to original.
    pushf
    call far [cs:old_int09]

    ; Process our hotkey.
    push ax
    push bx
    push ds
    push es
    push cs
    pop  ds

    mov  al, [scratch_scan]
    test al, 0x80           ; key release?
    jnz  .done09

    ; Check Ctrl+Alt: BIOS flags at 0000:0417, bits 2 (Ctrl) and 3 (Alt).
    xor  bx, bx
    mov  es, bx
    mov  ah, [es:0x0417]
    and  ah, 0x0C           ; Ctrl=bit2, Alt=bit3
    cmp  ah, 0x0C
    jne  .done09

    ; Ctrl+Alt held.  Check scan codes.
    cmp  al, 0x4E           ; KP +
    je   .level_up
    cmp  al, 0x4A           ; KP -
    je   .level_down
    cmp  al, 0x0B           ; main keyboard '0'
    je   .level_off
    jmp  .done09

.level_up:
    mov  ax, [speed_level]
    cmp  ax, 5
    jae  .done09
    inc  ax
    jmp  .set_level

.level_down:
    mov  ax, [speed_level]
    test ax, ax
    jz   .done09
    dec  ax
    jmp  .set_level

.level_off:
    xor  ax, ax

.set_level:
    mov  [speed_level], ax
    ; Look up preset delay count.
    shl  ax, 1              ; word index → byte offset
    lea  bx, [presets]
    add  bx, ax
    mov  ax, [bx]
    mov  [delay_count], ax

.done09:
    pop  es
    pop  ds
    pop  bx
    pop  ax
    iret

; -----------------------------------------------------------------------
; End of resident section
; -----------------------------------------------------------------------
resident_end:

; -----------------------------------------------------------------------
; Initialisation section (freed after install)
; -----------------------------------------------------------------------
init:
    push cs
    pop  ds

    ; Parse command tail for switches.
    mov  cl, [0x80]
    xor  ch, ch
    jcxz .no_args           ; short jump to nearby trampoline

    mov  si, 0x81
.scan:
    cmp  byte [si], '/'
    je   .got_slash
    inc  si
    loop .scan
    jmp  .install

.got_slash:
    inc  si
    mov  al, [si]
    or   al, 0x20           ; lowercase
    cmp  al, 'u'
    je   .uninstall
    cmp  al, 's'
    je   .show_status
    cmp  al, 'l'
    je   .set_init_level
    jmp  .install

.no_args:
    jmp  .install           ; trampoline for jcxz (short jump target)

; ---- /U uninstall ----
.uninstall:
    mov  ax, 0x3508
    int  0x21               ; ES:BX = current INT 08h vector
    mov  ax, es
    push cs
    pop  bx
    cmp  ax, bx
    jne  .cant_uninstall

    push ds
    lds  dx, [old_int08]
    mov  ax, 0x2508
    int  0x21
    pop  ds

    push ds
    lds  dx, [old_int09]
    mov  ax, 0x2509
    int  0x21
    pop  ds

    mov  ah, 0x49
    push cs
    pop  es
    int  0x21

    mov  dx, msg_removed
    mov  ah, 0x09
    int  0x21
    int  0x20

.cant_uninstall:
    mov  dx, msg_cant_remove
    mov  ah, 0x09
    int  0x21
    int  0x20

; ---- /S status ----
.show_status:
    mov  ax, 0x3508
    int  0x21
    mov  ax, es
    push cs
    pop  bx
    cmp  ax, bx
    je   .print_status
    cmp  word [es:0x100], 'TH'
    jne  .not_installed
    cmp  word [es:0x102], 'RO'
    jne  .not_installed
.print_status:
    ; Print "THROTTLE: level N (PRESET_NAME)"
    push cs
    pop  es
    mov  ax, [es:0x108]     ; speed_level in resident copy
    mov  bx, ax
    shl  bx, 1
    lea  si, [preset_names]
    add  si, bx
    mov  si, [si]           ; pointer to name string
    mov  dx, msg_status_pfx
    mov  ah, 0x09
    int  0x21
    mov  dx, si
    mov  ah, 0x09
    int  0x21
    mov  dx, crlf
    mov  ah, 0x09
    int  0x21
    int  0x20
.not_installed:
    mov  dx, msg_not_installed
    mov  ah, 0x09
    int  0x21
    int  0x20

; ---- /Ln set initial level ----
.set_init_level:
    inc  si
    mov  al, [si]
    sub  al, '0'
    jb   .install
    cmp  al, 5
    ja   .install
    mov  [init_level], al
    jmp  .install

; ---- install ----
.install:
    ; Already installed?
    mov  ax, 0x3508
    int  0x21
    cmp  word [es:0x100], 'TH'
    jne  .do_install
    cmp  word [es:0x102], 'RO'
    jne  .do_install
    mov  dx, msg_already
    mov  ah, 0x09
    int  0x21
    int  0x20

.do_install:
    ; Save + hook INT 08h.
    mov  ax, 0x3508
    int  0x21
    mov  [old_int08],     bx
    mov  [old_int08 + 2], es
    push ds
    push cs
    pop  ds
    mov  dx, int08_hook
    mov  ax, 0x2508
    int  0x21
    pop  ds

    ; Save + hook INT 09h.
    mov  ax, 0x3509
    int  0x21
    mov  [old_int09],     bx
    mov  [old_int09 + 2], es
    push ds
    push cs
    pop  ds
    mov  dx, int09_hook
    mov  ax, 0x2509
    int  0x21
    pop  ds

    ; Apply initial level if /Ln given.
    mov  al, [init_level]
    cbw
    mov  [speed_level], ax
    shl  ax, 1
    lea  bx, [presets]
    add  bx, ax
    mov  ax, [bx]
    mov  [delay_count], ax

    mov  dx, msg_installed
    mov  ah, 0x09
    int  0x21

    mov  dx, resident_end
    int  0x27

; -----------------------------------------------------------------------
; Init-section data
; -----------------------------------------------------------------------
init_level      db  0           ; default: OFF

; Preset name pointers (word table, 6 entries).
preset_names    dw  name_off, name_slow100, name_slow66
                dw  name_slow33, name_slow10, name_slow477

name_off      db  'OFF$'
name_slow100  db  'SLOW100$'
name_slow66   db  'SLOW66$'
name_slow33   db  'SLOW33$'
name_slow10   db  'SLOW10$'
name_slow477  db  'SLOW477$'

msg_installed    db  'THROTTLE: installed (level 0 = OFF).', 0x0D, 0x0A
                 db  '  Ctrl+Alt+KP+/-  adjust level', 0x0D, 0x0A
                 db  '  Ctrl+Alt+0      disable', 0x0D, 0x0A, '$'
msg_already      db  'THROTTLE: already installed.', 0x0D, 0x0A, '$'
msg_removed      db  'THROTTLE: removed.', 0x0D, 0x0A, '$'
msg_cant_remove  db  'THROTTLE: cannot remove -- INT 08h chain modified.', 0x0D, 0x0A, '$'
msg_not_installed db 'THROTTLE: not installed.', 0x0D, 0x0A, '$'
msg_status_pfx   db  'THROTTLE: level $'
crlf             db  0x0D, 0x0A, '$'
