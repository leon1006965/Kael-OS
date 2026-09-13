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
extern void mouse_init(void);

uint32_t mb_magic;
uint32_t mb_info_ptr;

void kernel_main(uint32_t magic, uint32_t info_ptr) {
    mb_magic = magic;
    mb_info_ptr = info_ptr;

    idt_install();

    outb(0x21, ~(1 << 1));   // unmask IRQ1 (keyboard)
    outb(0xA1, 0xFF);        // mask all slave PIC

    mouse_init();

    uint8_t mask = inb(0xA1);
    mask &= ~(1 << 4);       // unmask IRQ12 (mouse) on slave PIC
    outb(0xA1, mask);

    uint8_t mask2 = inb(0x21);
    mask2 &= ~(1 << 2);      // unmask IRQ2 (cascade) on master PIC
    outb(0x21, mask2);

    sti_enable();
    desktop_run();
    while (1) { asm volatile("hlt"); }
}
