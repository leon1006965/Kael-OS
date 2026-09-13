[bits 32]

section .multiboot
align 4
    dd 0x1BADB002                    ; magic
    dd 0x00                          ; flags
    dd -(0x1BADB002 + 0x00)          ; checksum

section .text
global _start
extern kernel_main

_start:
    mov esp, 0x90000
    push ebx        ; multiboot info pointer
    push eax        ; multiboot magic
    call kernel_main
.hang:
    cli
    hlt
    jmp .hang
