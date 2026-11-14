[bits 16]
section .boot16
global vbe_boot_stub
vbe_boot_stub:
    push ds
    push es
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov byte [0x5FF0], 0

    ; Prefer 800x600x16-bit VBE with a linear framebuffer.
    mov ax, 0x4F01
    mov cx, 0x0114
    mov di, 0x6000
    int 0x10
    cmp ax, 0x004F
    jne .try_640
    test word [0x6000], 0x0080
    jz .try_640
    cmp byte [0x6019], 16
    jne .try_640
    mov ax, 0x4F02
    mov bx, 0x4114
    int 0x10
    cmp ax, 0x004F
    jne .try_640
    mov byte [0x5FF0], 0xA5
    jmp .done

.try_640:
    ; Standard fallback: 640x480x16-bit VBE.
    mov ax, 0x4F01
    mov cx, 0x0111
    mov di, 0x6000
    int 0x10
    cmp ax, 0x004F
    jne .text_mode
    test word [0x6000], 0x0080
    jz .text_mode
    cmp byte [0x6019], 16
    jne .text_mode
    mov ax, 0x4F02
    mov bx, 0x4111
    int 0x10
    cmp ax, 0x004F
    jne .text_mode
    mov byte [0x5FF0], 0xA5
    jmp .done

.text_mode:
    mov ax, 0x0003
    int 0x10
.done:
    pop es
    pop ds
    retf
times 128 - ($ - vbe_boot_stub) db 0

[bits 32]

section .text
global _start
extern kernel_main
extern __bss_start
extern __bss_end

_start:
    mov esp, 0x90000
    push ebx
    push eax
    ; zero .bss (GRUB does this for the ISO path; our floppy loader does not,
    ; and real hardware RAM is not guaranteed zero at POST)
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    cld
    rep stosb
    call kernel_main
.hang:
    cli
    hlt
    jmp .hang
