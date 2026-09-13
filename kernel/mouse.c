#include "types.h"

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t r; asm volatile("inb %1,%0" : "=a"(r) : "Nd"(port)); return r;
}

#define PS2_TIMEOUT 100000

static volatile int mouse_x = 40;
static volatile int mouse_y = 12;
static volatile int mouse_z = 0;
static volatile uint8_t mouse_buttons = 0;
static int mouse_has_wheel = 0;
static int mouse_present = 0;
static volatile uint8_t packet[4];
static volatile int packet_idx = 0;

static int ps2_wait_write(void) {
    for (int i = 0; i < PS2_TIMEOUT; i++)
        if (!(inb(0x64) & 2)) return 0;
    return -1;
}

static int ps2_wait_read(void) {
    for (int i = 0; i < PS2_TIMEOUT; i++)
        if (inb(0x64) & 1) return 0;
    return -1;
}

static uint8_t ps2_read(void) {
    if (ps2_wait_read() != 0) return 0xFF;
    return inb(0x60);
}

static void ps2_write(uint16_t port, uint8_t val) {
    ps2_wait_write();
    outb(port, val);
}

static void mouse_write(uint8_t val) {
    ps2_write(0x64, 0xD4);
    ps2_write(0x60, val);
}

static int mouse_enable_wheel(void) {
    mouse_write(0xF3); ps2_read();
    mouse_write(200);  ps2_read();
    mouse_write(0xF3); ps2_read();
    mouse_write(100);  ps2_read();
    mouse_write(0xF3); ps2_read();
    mouse_write(80);   ps2_read();
    mouse_write(0xF2); ps2_read();
    uint8_t id = ps2_read();
    return id == 0x03;
}

int mouse_init(void) {
    for (int i = 0; i < PS2_TIMEOUT && (inb(0x64) & 1); i++) inb(0x60);
    for (int i = 0; i < PS2_TIMEOUT && (inb(0x64) & 2); i++);

    ps2_write(0x64, 0xA8);

    ps2_write(0x64, 0x20);
    uint8_t config = ps2_read();
    config |= 0x02;
    config |= 0x20;
    ps2_write(0x64, 0x60);
    ps2_write(0x60, config);

    mouse_write(0xF6);
    ps2_read();

    mouse_has_wheel = mouse_enable_wheel();

    mouse_write(0xF4);
    if (ps2_read() != 0xFA) {
        mouse_present = 0;
        return -1;
    }

    mouse_present = 1;
    return 0;
}

static void mouse_process_byte(uint8_t data) {
    int last = mouse_has_wheel ? 3 : 2;

    if (packet_idx == 0) {
        if (!(data & 0x08)) return;
        packet[0] = data;
        packet_idx = 1;
    } else if (packet_idx < last) {
        packet[packet_idx++] = data;
    } else {
        packet[packet_idx] = data;
        packet_idx = 0;

        int buttons = packet[0] & 0x07;
        int dx = 0, dy = 0;
        if (!(packet[0] & 0xC0)) {
            dx = (int)(int8_t)packet[1];
            dy = -(int)(int8_t)packet[2];
        }
        mouse_buttons = buttons;
        mouse_x += dx;
        mouse_y += dy;
        if (mouse_x < 0) mouse_x = 0;
        if (mouse_x > 79) mouse_x = 79;
        if (mouse_y < 0) mouse_y = 0;
        if (mouse_y > 24) mouse_y = 24;

        if (mouse_has_wheel) {
            int8_t z = (int8_t)(packet[3] << 4) >> 4;
            mouse_z += (z > 0) ? 1 : (z < 0 ? -1 : 0);
        }
    }
}

void mouse_handler(void) {
    uint8_t st = inb(0x64);
    if ((st & 0x21) != 0x21) return;
    mouse_process_byte(inb(0x60));
}

void mouse_poll(void) {
    uint32_t flags;
    asm volatile("pushfl; pop %0; cli" : "=r"(flags));
    for (int guard = 0; guard < 64; guard++) {
        uint8_t st = inb(0x64);
        if ((st & 0x21) != 0x21) break;
        mouse_process_byte(inb(0x60));
    }
    asm volatile("push %0; popfl" : : "r"(flags) : "memory", "cc");
}

int mouse_get_x(void) { mouse_poll(); return mouse_x; }
int mouse_get_y(void) { mouse_poll(); return mouse_y; }
uint8_t mouse_get_buttons(void) { mouse_poll(); return mouse_buttons; }
int mouse_is_present(void) { return mouse_present; }
