[bits 16]
[org 0x7C00]

KERNEL_OFFSET equ 0x7E00

start:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    mov bp, sp
    mov [boot_drive], dl

    mov ax, 0x0003
    int 0x10

    ; Probe EDD support before reading. Use chunks of at most 32 sectors and
    ; avoid 64KB DMA boundaries for older BIOS implementations.
    mov ah, 0x41
    mov dl, [boot_drive]
    mov bx, 0x55AA
    int 0x13
    jc .use_chs
    cmp bx, 0xAA55
    jne .use_chs

    mov word [edd_lba], 1
    mov word [edd_left], 256
    mov word [edd_seg], KERNEL_OFFSET >> 4
.read_chunk:
    ; At most 32 sectors per request, and never across a physical 64KB
    ; boundary (required by some BIOS EDD implementations).
    mov ax, [edd_seg]
    and ax, 0x0FFF
    mov bx, 0x1000
    sub bx, ax
    shr bx, 5                               ; available sectors to 64KB boundary
    mov cx, 32
    cmp cx, bx
    jbe .edd_boundary_ok
    mov cx, bx
.edd_boundary_ok:
    cmp cx, [edd_left]
    jbe .edd_count_ok
    mov cx, [edd_left]
.edd_count_ok:
    mov [edd_count], cx
    mov [dap + 2], cx
    mov word [dap + 4], 0
    mov ax, [edd_seg]
    mov [dap + 6], ax
    mov ax, [edd_lba]
    mov [dap + 8], ax
    mov word [dap + 10], 0
    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc disk_error
    mov ax, [edd_count]
    add [edd_lba], ax
    sub [edd_left], ax
    shl ax, 5
    add [edd_seg], ax
    cmp word [edd_left], 0
    jne .read_chunk
    jmp .read_done

.use_chs:
    ; Floppy CHS fallback. Read track-sized chunks so BIOSes that reject
    ; cross-track reads still load the complete kernel.
    mov word [chs_lba], 1
    mov word [chs_left], 256
    mov word [chs_seg], KERNEL_OFFSET >> 4
.chs_read:
    ; LBA -> cylinder/head/sector for 18 sectors per track, 2 heads.
    mov ax, [chs_lba]
    xor dx, dx
    mov bx, 18
    div bx
    mov [chs_track], ax
    mov [chs_sector], dl
    inc byte [chs_sector]

    mov ax, 19
    xor bx, bx
    mov bl, [chs_sector]
    sub ax, bx
    cmp ax, [chs_left]
    jbe .count_ready
    mov ax, [chs_left]
.count_ready:
    mov [chs_count], al

    ; Also keep the transfer below the next physical 64KB DMA boundary.
    mov ax, [chs_seg]
    and ax, 0x0FFF
    mov bx, 0x1000
    sub bx, ax
    shr bx, 5                               ; 512-byte sectors / 16-byte segments
    cmp [chs_count], bl
    jbe .dma_count_ready
    mov [chs_count], bl
.dma_count_ready:

    mov ax, [chs_track]
    xor dx, dx
    mov bx, 2
    div bx
    mov [chs_cylinder], al
    mov [chs_head], dl

    mov ax, [chs_seg]
    mov es, ax
    xor bx, bx
    mov ch, [chs_cylinder]
    mov cl, [chs_sector]
    mov dh, [chs_head]
    mov dl, [boot_drive]
    mov al, [chs_count]
    mov ah, 0x02
    int 0x13
    jc disk_error

    xor ax, ax
    mov al, [chs_count]
    add [chs_lba], ax
    sub [chs_left], ax
    shl ax, 5                                  ; sectors * 512 / 16
    add [chs_seg], ax
    cmp word [chs_left], 0
    jne .chs_read
    xor ax, ax
    mov es, ax

.read_done:
    jc disk_error

    ; Kernel begins with a small real-mode VBE probe/setup stub.
    call 0x0000:KERNEL_OFFSET

    in al, 0x92
    or al, 2
    out 0x92, al

    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:protected_mode_entry

disk_error:
    mov si, msg_disk_err
    call print_real
    jmp $

print_real:
    pusha
.loop:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    mov bh, 0
    int 0x10
    jmp .loop
.done:
    popa
    ret

boot_drive: db 0
edd_lba: dw 0
edd_left: dw 0
edd_seg: dw 0
edd_count: dw 0
chs_lba: dw 0
chs_left: dw 0
chs_seg: dw 0
chs_track: dw 0
chs_sector: db 0
chs_count: db 0
chs_cylinder: db 0
chs_head: db 0
msg_disk_err: db 'Disk error!', 13, 10, 0

align 4
dap:
    db 0x10
    db 0
    dw 32
    dw 0
    dw KERNEL_OFFSET >> 4
    dq 1

gdt_start:
    dq 0
gdt_code:
    dw 0xFFFF, 0x0000
    db 0x00, 10011010b, 11001111b, 0x00
gdt_data:
    dw 0xFFFF, 0x0000
    db 0x00, 10010010b, 11001111b, 0x00
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

[bits 32]
protected_mode_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000
    jmp KERNEL_OFFSET + 128

times 510 - ($ - $$) db 0
dw 0xAA55
