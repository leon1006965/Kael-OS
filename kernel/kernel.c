#include "types.h"

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

extern void idt_install(void);
extern void sti_enable(void);
extern void desktop_run(void);

void kernel_main(void) {
    idt_install();
    outb(0x21, ~(1 << 1));
    outb(0xA1, 0xFF);
    sti_enable();
    desktop_run();
    while (1) { asm volatile("hlt"); }
}
