; NOS-DOS: NOS-BRIDGE
; nosclip.asm - Clipboard exchange TSR.
;
; Hooks INT 09h (keyboard hardware) to detect hotkeys.
; Defers all file I/O to INT 08h (18.2 Hz timer) after checking the InDOS
; flag -- this is the standard safe pattern for TSRs that need to call
; INT 21h from a hardware interrupt context.
;
;   Ctrl+Shift+C  -- capture current screen line at cursor → H:\CLIP\CLIP.TXT
;   Ctrl+Shift+V  -- inject H:\CLIP\CLIP.TXT contents as keystrokes
;
; Usage:
;   NOSCLIP         -- install TSR
;   NOSCLIP /U      -- uninstall
;   NOSCLIP /S      -- status
;
; Resident footprint: < 1.2 KB.
;
; Notes:
;   - Screen capture reads the character row where the cursor sits at the
;     time of the hotkey press (from BIOS data area 0x0450 / 0x0451).
;   - Paste stuffs keys via INT 16h AH=05h (BIOS keyboard write).  The BIOS
;     typeahead buffer holds 15 characters; longer clips are injected in
;     chunks across successive INT 08h ticks.
;   - NBRIDGE CLIP GET / PUT access H:\CLIP\CLIP.TXT directly without going
;     through the TSR.
;
; License: GPL-2.0

cpu 8086
bits 16
org 0x100

; -----------------------------------------------------------------------
; Resident section
; -----------------------------------------------------------------------

; Signature at CS:0x100 for detection.
SIG_STR     db  'NOSCLIP', 0        ; 8 bytes

; Saved vectors
old_int08   dd  0
old_int09   dd  0

; InDOS pointer (far pointer to DOS InDOS byte, set at install time)
indos_ptr   dd  0

; Action flag: 0=idle, 1=copy-pending, 2=paste-pending
action_flag db  0

; Cursor row saved at hotkey time (0-based)
cursor_row  db  0

; Clipboard buffer (256 bytes + length word + paste position)
CLIPSIZE    equ 256
clip_buf    times CLIPSIZE db 0
clip_len    dw  0
paste_pos   dw  0           ; next byte to inject during paste

; -----------------------------------------------------------------------
; Shift state constants (BIOS data area 0x0417)
; BIOS_KBD_SEG = 0x0040, BIOS_KBD_FLAGS = 0x0017
; Bit 0 = RShift, Bit 1 = LShift, Bit 2 = Ctrl, Bit 3 = Alt
; -----------------------------------------------------------------------
SHIFT_CTRL  equ 0x04
SHIFT_L     equ 0x02
SHIFT_R     equ 0x01
SHIFT_MASK  equ 0x0F        ; mask out lock-key bits

; -----------------------------------------------------------------------
; INT 09h hook -- keyboard hardware interrupt
; -----------------------------------------------------------------------
int09_hook:
    push ax
    in   al, 0x60           ; latch scan code before original handler runs
    push cs
    pop  ax
    ; We need to save scancode; keep it in a register across the call.
    ; Use a resident scratch byte instead (CS-relative access safe here).
    push ax
    push ds
    push cs
    pop  ds
    in   al, 0x60           ; re-read -- value still latched
    ; Note: reading 0x60 twice is benign; value is held until EOI.
    mov  [scratch_scan], al
    pop  ds
    pop  ax

    ; Chain to original handler first (it processes the key normally).
    pushf
    call far [cs:old_int09]

    ; Now inspect the latched scan code.
    push ax
    push bx
    push ds
    push cs
    pop  ds

    mov  al, [scratch_scan]
    test al, 0x80           ; key release? (bit 7 set)
    jnz  .done09

    ; Read shift state from BIOS data area.
    push es
    xor  bx, bx
    mov  es, bx
    mov  ah, [es:0x0417]    ; BIOS keyboard flags
    pop  es

    ; Require Ctrl (bit 2) + at least one Shift (bit 0 or 1), no Alt (bit 3).
    mov  bl, ah
    and  bl, SHIFT_MASK
    test bl, SHIFT_CTRL
    jz   .done09            ; Ctrl not held
    mov  bh, bl
    and  bh, SHIFT_L | SHIFT_R
    jz   .done09            ; no Shift held
    test bl, 0x08           ; Alt held?
    jnz  .done09            ; ignore if Alt also down

    ; Ctrl+Shift is held.  Check scan code.
    cmp  al, 0x2E           ; C scan code
    je   .do_copy
    cmp  al, 0x2F           ; V scan code
    je   .do_paste
    jmp  .done09

.do_copy:
    ; Save cursor row from BIOS (page 0 cursor at 0x0450/0x0451: DH=row, DL=col).
    push es
    xor  bx, bx
    mov  es, bx
    mov  al, [es:0x0451]    ; row of page-0 cursor (high byte of word at 0x0450)
    pop  es
    mov  [cursor_row], al
    mov  byte [action_flag], 1
    jmp  .done09

.do_paste:
    mov  word [paste_pos], 0
    mov  byte [action_flag], 2

.done09:
    pop  ds
    pop  bx
    pop  ax
    iret

; Scratch byte for scan code (only used inside int09_hook).
scratch_scan  db  0

; -----------------------------------------------------------------------
; INT 08h hook -- timer tick (18.2 Hz)
; -----------------------------------------------------------------------
int08_hook:
    ; Chain to original first (maintains system clock, etc.).
    pushf
    call far [cs:old_int08]

    ; Check action flag quickly before touching any data.
    push ax
    push ds
    push cs
    pop  ds

    cmp  byte [action_flag], 0
    je   .done08

    ; Check InDOS flag -- skip if DOS is busy.
    push es
    push bx
    les  bx, [indos_ptr]
    cmp  byte [es:bx], 0
    pop  bx
    pop  es
    jne  .done08

    ; Safe to act.
    call do_action

.done08:
    pop  ds
    pop  ax
    iret

; -----------------------------------------------------------------------
; do_action -- perform copy or paste (called with DS=CS, InDOS=0)
; -----------------------------------------------------------------------
do_action:
    cmp  byte [action_flag], 1
    je   .copy
    cmp  byte [action_flag], 2
    je   .paste
    ret

; ---- Copy: read current screen line at cursor_row → CLIP.TXT ----
.copy:
    mov  byte [action_flag], 0  ; clear immediately (prevent re-entry)

    ; Point ES:BX at the video line: B800:0000 + row*160
    ; Each cell = 2 bytes (char, attr).  80 cells = 160 bytes/row.
    mov  ax, 0xB800
    mov  es, ax
    xor  bx, bx
    mov  al, [cursor_row]
    xor  ah, ah
    mov  cx, 160
    mul  cx                 ; AX = row * 160
    mov  bx, ax             ; ES:BX → start of screen row

    ; Extract 80 characters (every other byte) into clip_buf.
    push di
    push si
    lea  di, [clip_buf]
    mov  cx, 80
.copy_loop:
    mov  al, [es:bx]        ; character byte
    mov  [di], al
    inc  bx
    inc  bx                 ; skip attribute byte
    inc  di
    loop .copy_loop

    ; Strip trailing spaces.
    mov  cx, 80
    lea  di, [clip_buf + 79]
.strip_loop:
    cmp  byte [di], ' '
    jne  .strip_done
    dec  di
    loop .strip_loop
.strip_done:
    ; cx went from 80 down; length = 80 - cx, but we decremented di each pass.
    ; Recalculate: length = di - clip_buf + 1
    lea  si, [clip_buf]
    mov  ax, di
    sub  ax, si
    inc  ax                 ; AX = effective length (0 if blank line)
    jz   .copy_write        ; nothing to write
    mov  [clip_len], ax

.copy_write:
    ; Write clip_buf to H:\CLIP\CLIP.TXT
    mov  ah, 0x3C
    xor  cx, cx
    lea  dx, [clip_path]
    int  0x21
    jc   .copy_done

    mov  bx, ax             ; handle
    mov  ah, 0x40
    lea  dx, [clip_buf]
    mov  cx, [clip_len]
    int  0x21

    ; Append CR+LF
    mov  ah, 0x40
    lea  dx, [crlf]
    mov  cx, 2
    int  0x21

    mov  ah, 0x3E
    int  0x21

.copy_done:
    pop  si
    pop  di
    ret

; ---- Paste: read CLIP.TXT → stuff keyboard buffer in chunks ----
.paste:
    ; On first call (paste_pos=0), load the file into clip_buf.
    cmp  word [paste_pos], 0
    jne  .inject

    ; Open H:\CLIP\CLIP.TXT
    mov  ah, 0x3D
    xor  al, al             ; read-only
    lea  dx, [clip_path]
    int  0x21
    jc   .paste_done_clear  ; file not found

    mov  bx, ax             ; handle
    mov  ah, 0x3F
    lea  dx, [clip_buf]
    mov  cx, CLIPSIZE
    int  0x21
    jc   .paste_close

    mov  [clip_len], ax     ; bytes actually read

.paste_close:
    mov  ah, 0x3E
    int  0x21

    ; Replace CR/LF bytes with spaces so they inject as text.
    push si
    lea  si, [clip_buf]
    mov  cx, [clip_len]
.cr_loop:
    cmp  byte [si], 0x0D
    je   .cr_rep
    cmp  byte [si], 0x0A
    jne  .cr_next
.cr_rep:
    mov  byte [si], ' '
.cr_next:
    inc  si
    loop .cr_loop
    pop  si

.inject:
    ; Stuff up to 8 chars per tick via INT 16h AH=05h.
    ; (BIOS buffer holds 15; leaving headroom avoids overrun.)
    push si
    lea  si, [clip_buf]
    mov  bx, [paste_pos]
    mov  cx, 8              ; chars per tick
.stuff_loop:
    cmp  bx, [clip_len]
    jae  .stuff_done
    mov  al, [si + bx]
    mov  ah, 0x05           ; keyboard write
    xor  ch, ch             ; scan code 0 (extended/undefined)
    mov  cl, al             ; ASCII char
    int  0x16
    cmp  al, 1              ; 1 = buffer full
    je   .stuff_done
    inc  bx
    loop .stuff_loop

.stuff_done:
    mov  [paste_pos], bx
    pop  si

    ; Check if we've injected everything.
    mov  ax, [paste_pos]
    cmp  ax, [clip_len]
    jb   .paste_ret         ; more to inject next tick

.paste_done_clear:
    mov  byte [action_flag], 0
.paste_ret:
    ret

; -----------------------------------------------------------------------
; Read-only resident data
; -----------------------------------------------------------------------
clip_path   db  'H:\CLIP\CLIP.TXT', 0
crlf        db  0x0D, 0x0A

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

    ; Check args.
    mov  cl, [0x80]
    xor  ch, ch
    jcxz .no_args           ; short jump to nearby trampoline

    mov  si, 0x81
.scan:
    cmp  byte [si], '/'
    je   .got_slash
    inc  si
    loop .scan
.no_args:
    jmp  .install

.got_slash:
    inc  si
    mov  al, [si]
    or   al, 0x20
    cmp  al, 'u'
    je   .uninstall
    cmp  al, 's'
    je   .status
    jmp  .install

; ---- /U uninstall ----
.uninstall:
    ; Verify INT 09h still points to us.
    mov  ax, 0x3509
    int  0x21
    mov  ax, es
    push cs
    pop  bx
    cmp  ax, bx
    jne  .cant_uninstall

    ; Restore INT 09h.
    push ds
    lds  dx, [old_int09]
    mov  ax, 0x2509
    int  0x21
    pop  ds

    ; Restore INT 08h.
    push ds
    lds  dx, [old_int08]
    mov  ax, 0x2508
    int  0x21
    pop  ds

    ; Free resident memory.
    mov  ah, 0x49
    push cs
    pop  es
    int  0x21

    mov  dx, msg_uninstalled
    mov  ah, 0x09
    int  0x21
    int  0x20

.cant_uninstall:
    mov  dx, msg_cant_uninstall
    mov  ah, 0x09
    int  0x21
    int  0x20

; ---- /S status ----
.status:
    mov  ax, 0x3509
    int  0x21
    mov  ax, es
    push cs
    pop  bx
    cmp  ax, bx
    je   .status_on
    ; Check signature in handler's segment.
    cmp  word [es:0x100], 'NO'
    jne  .status_off
    cmp  word [es:0x102], 'SC'
    jne  .status_off
.status_on:
    mov  dx, msg_status_on
    mov  ah, 0x09
    int  0x21
    int  0x20
.status_off:
    mov  dx, msg_status_off
    mov  ah, 0x09
    int  0x21
    int  0x20

; ---- install ----
.install:
    ; Check if already installed.
    mov  ax, 0x3509
    int  0x21
    cmp  word [es:0x100], 'NO'
    jne  .do_install
    cmp  word [es:0x102], 'SC'
    jne  .do_install
    mov  dx, msg_already
    mov  ah, 0x09
    int  0x21
    int  0x20

.do_install:
    ; Ensure H:\CLIP directory exists.
    mov  ah, 0x39
    lea  dx, [clip_dir]
    int  0x21

    ; Get InDOS pointer (INT 21h AH=34h → ES:BX).
    mov  ah, 0x34
    int  0x21
    mov  [indos_ptr],     bx
    mov  [indos_ptr + 2], es

    ; Save and hook INT 08h.
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

    ; Save and hook INT 09h.
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

    mov  dx, msg_installed
    mov  ah, 0x09
    int  0x21

    mov  dx, resident_end
    int  0x27

; -----------------------------------------------------------------------
; Init-section data (freed with code)
; -----------------------------------------------------------------------
clip_dir         db  'H:\CLIP', 0
msg_installed    db  'NOSCLIP: clipboard TSR installed.', 0x0D, 0x0A
                 db  'Ctrl+Shift+C = copy line  Ctrl+Shift+V = paste', 0x0D, 0x0A, '$'
msg_already      db  'NOSCLIP: already installed.', 0x0D, 0x0A, '$'
msg_uninstalled  db  'NOSCLIP: removed.', 0x0D, 0x0A, '$'
msg_cant_uninstall db 'NOSCLIP: cannot remove -- INT 09h chain modified.', 0x0D, 0x0A, '$'
msg_status_on    db  'NOSCLIP: installed (INT 08h + INT 09h hooked).', 0x0D, 0x0A, '$'
msg_status_off   db  'NOSCLIP: not installed.', 0x0D, 0x0A, '$'
