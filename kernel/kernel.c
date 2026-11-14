#include "types.h"
#include "multiboot.h"

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t v; asm volatile("inb %1,%0" : "=a"(v) : "Nd"(port)); return v;
}

extern void idt_install(void);
extern void sti_enable(void);
extern void desktop_run(void);
extern void keyboard_init(void);
extern int mouse_init(void);
extern void pit_init(void);
extern void serial_init(void);
extern void serial_puts(const char* s);
extern void net_init(void);
extern int vga_graphics_init(void);

uint32_t mb_magic;
uint32_t mb_info_ptr;

void kernel_main(uint32_t magic, uint32_t info_ptr) {
    asm volatile("cli");
    mb_magic = magic;
    mb_info_ptr = info_ptr;
    /* GRUB has not run the real-mode VBE probe in boot.asm. */
    if (magic == MB_MAGIC) *(volatile uint8_t*)0x5FF0 = 0;

    idt_install();
    pit_init();

    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);

    keyboard_init();
    mouse_init();

    uint8_t mask = inb(0xA1);
    mask &= ~(1 << 4);
    outb(0xA1, mask);
    uint8_t mask2 = inb(0x21);
    mask2 &= ~(1 << 2);
    outb(0x21, mask2);

    /* unmask IRQ0 (timer), IRQ1 (keyboard), IRQ2 (cascade for mouse) */
    outb(0x21, ~((1 << 0) | (1 << 1) | (1 << 2)) & 0xFF);

    sti_enable();
    serial_init();
    if (vga_graphics_init())
        serial_puts("[vga] VBE framebuffer active\n");
    else
        serial_puts("[vga] text-mode fallback\n");
    net_init();
    desktop_run();
    while (1) { asm volatile("hlt"); }
}
