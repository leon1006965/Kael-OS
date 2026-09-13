[bits 32]

global idt_install
global sti_enable
extern keyboard_handler
extern mouse_handler
extern timer_handler_c
extern syscall_handler

section .text

default_isr:
    pusha
    mov al, 0x20
    out 0x20, al
    popa
    iret

idt_install:
    mov edi, idt_data
    mov ecx, 512
    xor eax, eax
    rep stosd
    mov ecx, 256
    mov edi, idt_data
    mov eax, default_isr
.set_loop:
    mov word [edi + 0], ax
    mov word [edi + 2], 0x0008
    mov byte [edi + 4], 0
    mov byte [edi + 5], 0x8E
    mov word [edi + 6], 0
    add edi, 8
    dec ecx
    jnz .set_loop
    mov eax, isr1
    mov word [idt_data + 33*8 + 0], ax
    mov word [idt_data + 33*8 + 2], 0x0008
    mov byte [idt_data + 33*8 + 4], 0
    mov byte [idt_data + 33*8 + 5], 0x8E
    shr eax, 16
    mov word [idt_data + 33*8 + 6], ax
    mov eax, isr12
    mov word [idt_data + 44*8 + 0], ax
    mov word [idt_data + 44*8 + 2], 0x0008
    mov byte [idt_data + 44*8 + 4], 0
    mov byte [idt_data + 44*8 + 5], 0x8E
    shr eax, 16
    mov word [idt_data + 44*8 + 6], ax
    mov eax, isr_timer
    mov word [idt_data + 32*8 + 0], ax
    mov word [idt_data + 32*8 + 2], 0x0008
    mov byte [idt_data + 32*8 + 4], 0
    mov byte [idt_data + 32*8 + 5], 0x8E
    shr eax, 16
    mov word [idt_data + 32*8 + 6], ax
    mov eax, isr_syscall
    mov word [idt_data + 0x80*8 + 0], ax
    mov word [idt_data + 0x80*8 + 2], 0x0008
    mov byte [idt_data + 0x80*8 + 4], 0
    mov byte [idt_data + 0x80*8 + 5], 0xEE
    shr eax, 16
    mov word [idt_data + 0x80*8 + 6], ax
    mov al, 0x11
    out 0x20, al
    out 0xA0, al
    mov al, 0x20
    out 0x21, al
    mov al, 0x28
    out 0xA1, al
    mov al, 0x04
    out 0x21, al
    mov al, 0x02
    out 0xA1, al
    mov al, 0x01
    out 0x21, al
    out 0xA1, al
    mov al, 0xFF
    out 0x21, al
    out 0xA1, al
    lidt [idt_desc]
    ret

sti_enable:
    sti
    ret

global isr1
isr1:
    pusha
    call keyboard_handler
    popa
    mov al, 0x20
    out 0x20, al
    iret

global isr12
isr12:
    pusha
    call mouse_handler
    popa
    mov al, 0x20
    out 0xA0, al
    out 0x20, al
    iret

global isr_timer
isr_timer:
    pusha
    call timer_handler_c
    popa
    mov al, 0x20
    out 0x20, al
    iret

global isr_syscall
isr_syscall:
    pusha
    push edx
    push ecx
    push ebx
    push eax
    call syscall_handler
    add esp, 16
    mov [syscall_ret], eax
    popa
    mov eax, [syscall_ret]
    iret

section .data
idt_data:
    times 256 * 8 db 0
idt_desc:
    dw 256 * 8 - 1
    dd idt_data
syscall_ret:
    dd 0
