#include "net.h"

#define COM1 0x3F8

void serial_init(void) {
    outb(COM1 + 1, 0x00);   /* disable interrupts */
    outb(COM1 + 3, 0x80);   /* DLAB on */
    outb(COM1 + 0, 0x01);   /* divisor lo: 115200 baud */
    outb(COM1 + 1, 0x00);   /* divisor hi */
    outb(COM1 + 3, 0x03);   /* 8N1 */
    outb(COM1 + 2, 0xC7);   /* FIFO on, clear, 14-byte threshold */
    outb(COM1 + 4, 0x0B);   /* DTR, RTS, OUT2 */
}

static void ser_put(char c) {
    if (c == '\n') ser_put('\r');
    for (int i = 0; i < 200000; i++) {
        if (inb(COM1 + 5) & 0x20) break;
    }
    outb(COM1, (uint8_t)c);
}

void serial_puts(const char* s) {
    while (*s) ser_put(*s++);
}
