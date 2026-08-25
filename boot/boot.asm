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

    mov si, msg_boot
    call print_real

    mov ah, 0x41
    mov dl, [boot_drive]
    mov bx, 0x55AA
    int 0x13
    jc .use_chs

    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jmp .read_done

.use_chs:
    mov bx, KERNEL_OFFSET
    mov al, 64
    mov ch, 0
    mov cl, 2
    mov dh, 0
    mov dl, [boot_drive]
    mov ah, 0x02
    int 0x13

.read_done:
    jc disk_error

    mov si, msg_loaded
    call print_real

    mov ax, 0x0013
    int 0x10

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
msg_boot:     db 'Kael OS booting...', 13, 10, 0
msg_loaded:   db 'Kernel loaded.', 13, 10, 0
msg_disk_err: db 'Disk error!', 13, 10, 0

align 4
dap:
    db 0x10
    db 0
    dw 64
    dw KERNEL_OFFSET
    dw 0
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
    jmp KERNEL_OFFSET

times 510 - ($ - $$) db 0
dw 0xAA55
