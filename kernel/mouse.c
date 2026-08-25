#include "types.h"

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t r; asm volatile("inb %1,%0" : "=a"(r) : "Nd"(port)); return r;
}

static int mouse_x = 160, mouse_y = 100;
static uint8_t mouse_buttons = 0;
static uint8_t mouse_cycle = 0;
static int8_t mouse_byte[3];

static void mouse_wait_read(void) {
    for (int i = 0; i < 100000; i++)
        if (inb(0x64) & 1) return;
}

static void mouse_wait_write(void) {
    for (int i = 0; i < 100000; i++)
        if (!(inb(0x64) & 2)) return;
}

void mouse_init(void) {
    mouse_wait_write();
    outb(0x64, 0xA8);
    mouse_wait_write();
    outb(0x64, 0x20);
    mouse_wait_read();
    uint8_t config = inb(0x60);
    config |= 0x02;
    config &= ~0x20;
    mouse_wait_write();
    outb(0x64, 0x20);
    mouse_wait_write();
    outb(0x60, config);
    mouse_wait_write();
    outb(0x64, 0xD4);
    mouse_wait_write();
    outb(0x60, 0xFF);
    for (volatile int i = 0; i < 100000; i++) {}
    mouse_wait_write();
    outb(0x64, 0xD4);
    mouse_wait_write();
    outb(0x60, 0xF4);
    mouse_x = 160;
    mouse_y = 100;
}

void mouse_handler(void) {
    uint8_t data = inb(0x60);
    switch (mouse_cycle) {
        case 0:
            mouse_byte[0] = data;
            if (data & 0x08) mouse_cycle = 1;
            break;
        case 1:
            mouse_byte[1] = data;
            mouse_cycle = 2;
            break;
        case 2:
            mouse_byte[2] = data;
            mouse_x += mouse_byte[1];
            mouse_y -= mouse_byte[2];
            if (mouse_x < 0) mouse_x = 0;
            if (mouse_x > 319) mouse_x = 319;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_y > 199) mouse_y = 199;
            mouse_buttons = mouse_byte[0] & 0x07;
            mouse_cycle = 0;
            break;
    }
}

int mouse_get_x(void) { return mouse_x; }
int mouse_get_y(void) { return mouse_y; }
uint8_t mouse_get_buttons(void) { return mouse_buttons; }
