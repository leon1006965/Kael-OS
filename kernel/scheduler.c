#include "types.h"
#include "process.h"

static inline uint8_t inb(uint16_t port) {
    uint8_t val; asm volatile("inb %1,%0" : "=a"(val) : "Nd"(port)); return val;
}
static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0,%1" : : "a"(val), "Nd"(port));
}

void timer_init(void) {
    uint32_t divisor = 1193182 / 100;
    outb(0x43, 0x36);
    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);
    uint8_t mask = inb(0x21);
    mask &= ~(1 << 0);
    outb(0x21, mask);
}

void timer_handler_c(void) {
}
