; NOS-DOS: NOS-BRIDGE
; noslpt.asm - LPT1 print interceptor TSR.
;
; Hooks INT 17h.  All LPT1 character output (AH=00h, DX=0) is captured into
; a 512-byte ring buffer.  The buffer is flushed to the next available file
; H:\PRINT\PRINTnnn.PRN whenever:
;   - a form-feed character (0x0C) is received, or
;   - the buffer fills to capacity.
;
; INT 17h is a software interrupt (not a hardware IRQ), so calling INT 21h
; file I/O from inside the hook is safe -- no DOS reentrancy hazard.
;
; Usage:
;   NOSLPT          -- install TSR
;   NOSLPT /U       -- uninstall (only if no later TSR chained INT 17h)
;   NOSLPT /S       -- show status (installed/not, buffer fill, file counter)
;
; Resident footprint: < 900 bytes.
;
; License: GPL-2.0

cpu 8086
bits 16
org 0x100           ; COM file

; -----------------------------------------------------------------------
; Resident section begins at CS:0x100
; -----------------------------------------------------------------------

; Signature at a fixed offset (0x100) for detection by later NOSLPT runs.
SIGNATURE   equ 0x100
SIG_STR     db  'NOSLPT', 0, 0      ; 8 bytes, padded

; Old INT 17h vector (segment:offset, stored little-endian)
old_int17   dd  0                   ; 4 bytes

; Print buffer
BUFSIZE     equ 512
buf_len     dw  0
print_buf   times BUFSIZE db 0

; Print file counter (001-999, wraps)
file_ctr    dw  1

; File path template: "H:\PRINT\PRINTnnn.PRN" + NUL
; Digit positions: offset 15, 16, 17 within the string (0-based)
file_path   db  'H:\PRINT\PRINT000.PRN', 0

FILE_PATH_D1 equ (file_path - $$) + 15   ; offset of hundreds digit
FILE_PATH_D2 equ (file_path - $$) + 16   ; offset of tens digit
FILE_PATH_D3 equ (file_path - $$) + 17   ; offset of units digit

; -----------------------------------------------------------------------
; INT 17h hook
; -----------------------------------------------------------------------
int17_hook:
    cmp  ah, 0x00           ; print character?
    jne  .passthrough
    cmp  dx, 0              ; LPT1?
    jne  .passthrough

    ; Call the original handler so the physical port still sees the char.
    ; This preserves AX (char in AL) for our buffer write below.
    push ax
    pushf
    call far [cs:old_int17]
    pop  ax                 ; restore AL (char), AH (status from original)
    push ax

    ; Set DS = CS so we can access resident data directly.
    push bx
    push si
    push ds
    push cs
    pop  ds

    ; Store character in buffer.
    mov  si, [buf_len]
    cmp  si, BUFSIZE
    jae  .need_flush        ; buffer full — flush first

    lea  bx, [print_buf]
    mov  [bx + si], al
    inc  word [buf_len]

    ; Flush on form feed.
    cmp  al, 0x0C
    je   .flush_now
    jmp  .done

.need_flush:
    call flush_buffer       ; empties buffer, resets buf_len
    lea  bx, [print_buf]
    mov  byte [bx], al
    mov  word [buf_len], 1
    jmp  .done

.flush_now:
    call flush_buffer

.done:
    pop  ds
    pop  si
    pop  bx
    pop  ax
    iret

.passthrough:
    jmp  far [cs:old_int17]

; -----------------------------------------------------------------------
; flush_buffer -- write resident buffer to H:\PRINT\PRINTnnn.PRN
; Clobbers: AX, BX, CX, DX (caller must save if needed)
; Called from INT 17h hook with DS=CS already set.
; -----------------------------------------------------------------------
flush_buffer:
    cmp  word [buf_len], 0
    je   .flush_ret         ; nothing to write

    ; Update filename digits from file_ctr.
    mov  ax, [file_ctr]
    call update_filename

    ; INT 21h AH=3Ch: create/truncate file.
    mov  ah, 0x3C
    xor  cx, cx             ; normal attributes
    lea  dx, [file_path]
    int  0x21
    jc   .flush_ret         ; I/O error — discard buffer silently

    mov  bx, ax             ; file handle

    ; INT 21h AH=40h: write buffer.
    mov  ah, 0x40
    lea  dx, [print_buf]
    mov  cx, [buf_len]
    int  0x21

    ; INT 21h AH=3Eh: close.
    mov  ah, 0x3E
    int  0x21

    ; Advance counter (1-999, wrap).
    inc  word [file_ctr]
    cmp  word [file_ctr], 1000
    jb   .reset_buf
    mov  word [file_ctr], 1

.reset_buf:
    mov  word [buf_len], 0

.flush_ret:
    ret

; -----------------------------------------------------------------------
; update_filename -- encode AX (1-999) as three decimal digits in file_path
; -----------------------------------------------------------------------
update_filename:
    ; hundreds
    xor  dx, dx
    mov  cx, 100
    div  cx                 ; AX=hundreds, DX=remainder
    add  al, '0'
    mov  [FILE_PATH_D1], al

    ; tens
    mov  ax, dx
    xor  dx, dx
    mov  cx, 10
    div  cx                 ; AX=tens, DX=units
    add  al, '0'
    mov  [FILE_PATH_D2], al

    ; units
    mov  al, dl
    add  al, '0'
    mov  [FILE_PATH_D3], al

    ret

; -----------------------------------------------------------------------
; End of resident section — marker used for TSR size calculation.
; -----------------------------------------------------------------------
resident_end:

; -----------------------------------------------------------------------
; Initialisation section (freed after install via INT 27h)
; -----------------------------------------------------------------------
init:
    ; Set DS = CS.
    push cs
    pop  ds

    ; Check for /U or /S switch (argv starts at 0x81, length at 0x80).
    mov  cl, [0x80]         ; command-tail length
    xor  ch, ch
    jcxz .no_args           ; short jump to nearby trampoline

    ; Scan for '/' in tail.
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
    or   al, 0x20           ; lowercase
    cmp  al, 'u'
    je   .uninstall
    cmp  al, 's'
    je   .status
    jmp  .install

; ---- /U: uninstall ----
.uninstall:
    ; Read current INT 17h vector.
    mov  ax, 0x3517
    int  0x21               ; ES:BX = current vector
    ; Check that it still points to our hook.
    mov  ax, es
    push cs
    pop  bx
    cmp  ax, bx
    jne  .cant_uninstall

    ; Restore original vector.
    push ds
    lds  dx, [old_int17]
    mov  ax, 0x2517
    int  0x21
    pop  ds

    ; Free our memory block (PSP segment).
    mov  ah, 0x49
    mov  es, [0x002C]       ; env segment (PSP offset 0x2C = environment segment)
    ; Actually free the PSP segment itself -- MCB is at PSP-1.
    ; INT 21h AH=49h: ES = segment to free.
    push cs
    pop  es
    int  0x21

    mov  dx, msg_uninstalled
    mov  ah, 0x09
    int  0x21
    int  0x20               ; exit

.cant_uninstall:
    mov  dx, msg_cant_uninstall
    mov  ah, 0x09
    int  0x21
    int  0x20

; ---- /S: status ----
.status:
    ; Check whether we are already installed by looking at INT 17h vector.
    mov  ax, 0x3517
    int  0x21
    mov  ax, es
    push cs
    pop  bx
    cmp  ax, bx
    je   .status_self       ; we are the current hook

    ; Check if signature is in INT 17h handler's segment.
    cmp  word [es:SIGNATURE], 'NO'
    jne  .status_not_installed
    cmp  word [es:SIGNATURE+2], 'SL'
    jne  .status_not_installed

.status_self:
    mov  dx, msg_status_on
    mov  ah, 0x09
    int  0x21
    int  0x20

.status_not_installed:
    mov  dx, msg_status_off
    mov  ah, 0x09
    int  0x21
    int  0x20

; ---- install ----
.install:
    ; Check if already installed (INT 17h vector points somewhere with our sig).
    mov  ax, 0x3517
    int  0x21
    cmp  word [es:SIGNATURE], 'NO'
    jne  .do_install
    cmp  word [es:SIGNATURE+2], 'SL'
    jne  .do_install
    ; Already installed.
    mov  dx, msg_already
    mov  ah, 0x09
    int  0x21
    int  0x20

.do_install:
    ; Ensure H:\PRINT directory exists.
    mov  ah, 0x39
    lea  dx, [print_dir]
    int  0x21               ; ignore error (may already exist)

    ; Save current INT 17h vector.
    mov  ax, 0x3517
    int  0x21
    mov  [old_int17],     bx
    mov  [old_int17 + 2], es

    ; Install our hook.
    push ds
    push cs
    pop  ds
    mov  dx, int17_hook
    mov  ax, 0x2517
    int  0x21
    pop  ds

    ; Print installed message.
    mov  dx, msg_installed
    mov  ah, 0x09
    int  0x21

    ; Go resident: INT 27h, DX = first byte past resident section.
    mov  dx, resident_end
    int  0x27

; -----------------------------------------------------------------------
; Read-only data (init section -- freed after install)
; -----------------------------------------------------------------------
print_dir        db  'H:\PRINT', 0
msg_installed    db  'NOSLPT: LPT1 interceptor installed.', 0x0D, 0x0A
                 db  'Output goes to H:\PRINT\PRINTnnn.PRN', 0x0D, 0x0A, '$'
msg_already      db  'NOSLPT: already installed.', 0x0D, 0x0A, '$'
msg_uninstalled  db  'NOSLPT: removed.', 0x0D, 0x0A, '$'
msg_cant_uninstall db 'NOSLPT: cannot remove -- a later TSR has chained INT 17h.', 0x0D, 0x0A, '$'
msg_status_on    db  'NOSLPT: installed (INT 17h hooked).', 0x0D, 0x0A, '$'
msg_status_off   db  'NOSLPT: not installed.', 0x0D, 0x0A, '$'
