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
