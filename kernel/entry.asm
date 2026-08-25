[bits 32]

section .text
global _start
extern kernel_main

_start:
    mov esp, 0x90000
    call kernel_main
.hang:
    cli
    hlt
    jmp .hang
