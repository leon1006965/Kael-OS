[bits 32]

global idt_install
global sti_enable
extern keyboard_handler
extern mouse_handler

%macro IOWAIT 0
    out 0x80, al
%endmacro

section .text

default_isr:
    pusha
    mov al, 0x20
    out 0x20, al
    popa
    iret

exc_isr:
    cli
.h: hlt
    jmp .h

idt_install:
    push ebx
    push edi

    xor eax, eax
    mov ax, cs
    mov [cs_value], ax

    cld
    mov edi, idt_data
    mov ecx, 512
    xor eax, eax
    rep stosd

    ; all entries -> default_isr
    mov eax, default_isr
    mov edx, eax
    shr edx, 16
    movzx ebx, word [cs_value]
    mov edi, idt_data
    mov ecx, 256
.set_loop:
    mov word [edi + 0], ax
    mov word [edi + 2], bx
    mov byte [edi + 4], 0
    mov byte [edi + 5], 0x8E
    mov word [edi + 6], dx
    add edi, 8
    dec ecx
    jnz .set_loop

    ; CPU exceptions 0-31 -> exc_isr (halt)
    mov eax, exc_isr
    mov edx, eax
    shr edx, 16
    mov edi, idt_data
    mov ecx, 32
.exc_loop:
    mov word [edi + 0], ax
    mov word [edi + 6], dx
    add edi, 8
    dec ecx
    jnz .exc_loop

    ; INT 33 keyboard
    mov eax, isr1
    mov word [idt_data + 33*8 + 0], ax
    mov word [idt_data + 33*8 + 2], bx
    mov byte [idt_data + 33*8 + 4], 0
    mov byte [idt_data + 33*8 + 5], 0x8E
    shr eax, 16
    mov word [idt_data + 33*8 + 6], ax

    ; INT 44 mouse
    mov eax, isr12
    mov word [idt_data + 44*8 + 0], ax
    mov word [idt_data + 44*8 + 2], bx
    mov byte [idt_data + 44*8 + 4], 0
    mov byte [idt_data + 44*8 + 5], 0x8E
    shr eax, 16
    mov word [idt_data + 44*8 + 6], ax

    ; Remap PIC with I/O delays
    mov al, 0x11
    out 0x20, al
    IOWAIT
    out 0xA0, al
    IOWAIT
    mov al, 0x20
    out 0x21, al
    IOWAIT
    mov al, 0x28
    out 0xA1, al
    IOWAIT
    mov al, 0x04
    out 0x21, al
    IOWAIT
    mov al, 0x02
    out 0xA1, al
    IOWAIT
    mov al, 0x01
    out 0x21, al
    IOWAIT
    out 0xA1, al
    IOWAIT
    mov al, 0xFF
    out 0x21, al
    out 0xA1, al

    lidt [idt_desc]

    pop edi
    pop ebx
    ret

sti_enable:
    sti
    ret

global isr1
isr1:
    pusha
    cld
    call keyboard_handler
    mov al, 0x20
    out 0x20, al
    popa
    iret

global isr12
isr12:
    pusha
    cld
    call mouse_handler
    mov al, 0x20
    out 0xA0, al
    out 0x20, al
    popa
    iret

section .data
cs_value:
    dw 0
idt_data:
    times 256 * 8 db 0
idt_desc:
    dw 256 * 8 - 1
    dd idt_data
