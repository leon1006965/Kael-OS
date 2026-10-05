#include "serial.h"

#define COM1 0x3F8

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t r; asm volatile("inb %1,%0" : "=a"(r) : "Nd"(port)); return r;
}

void serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}

void serial_putc(char c) {
    while (!(inb(COM1 + 5) & 0x20));
    outb(COM1, c);
}

void serial_puts(const char* s) {
    while (*s) {
        if (*s == '\n') serial_putc('\r');
        serial_putc(*s++);
    }
}

void serial_puthex(uint64_t val) {
    serial_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        uint8_t nib = (val >> i) & 0xF;
        serial_putc(nib < 10 ? '0' + nib : 'A' + nib - 10);
    }
}

void serial_putdec(uint64_t val) {
    if (val == 0) { serial_putc('0'); return; }
    char buf[20];
    int i = 0;
    while (val > 0) { buf[i++] = '0' + (val % 10); val /= 10; }
    while (i > 0) serial_putc(buf[--i]);
}
