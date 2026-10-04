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
static volatile uint8_t mouse_buttons = 0;
static int mouse_present = 0;
static volatile uint8_t packet[6];
static volatile int packet_idx = 0;
static int expected_packet_len = 3;

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

static void mouse_write_cmd(uint8_t val) {
    ps2_write(0x64, 0xD4);
    for (int i = 0; i < 100; i++) inb(0x80);
    ps2_write(0x60, val);
    for (int i = 0; i < 100; i++) inb(0x80);
}

static int mouse_cmd(uint8_t val) {
    for (int retry = 0; retry < 3; retry++) {
        mouse_write_cmd(val);
        uint8_t r = ps2_read();
        if (r == 0xFA) return 0;
        if (r != 0xFE) return -1;
    }
    return -1;
}

static int mouse_set_rate(uint8_t rate) {
    if (mouse_cmd(0xF3) != 0) return -1;
    return mouse_cmd(rate);
}

static void ps2_flush(void) {
    for (int i = 0; i < 16 && (inb(0x64) & 1); i++) inb(0x60);
}

static int detect_mouse_type(void) {
    if (mouse_cmd(0xFF) != 0) return 0;
    uint8_t st = 0xFF;
    for (int i = 0; i < 20 && st == 0xFF; i++) st = ps2_read();
    if (st != 0xAA) return 0;
    ps2_read();
    ps2_flush();

    if (mouse_set_rate(200) || mouse_set_rate(100) || mouse_set_rate(80))
        return 3;
    if (mouse_cmd(0xF2) != 0) return 3;
    uint8_t id = ps2_read();
    return (id == 0x03) ? 4 : 3;
}

int mouse_init(void) {
    for (int i = 0; i < PS2_TIMEOUT && (inb(0x64) & 1); i++) inb(0x60);
    for (int i = 0; i < PS2_TIMEOUT && (inb(0x64) & 2); i++);

    ps2_write(0x64, 0xAD);
    ps2_write(0x64, 0xA8);
    ps2_write(0x64, 0x20);
    uint8_t config = ps2_read();
    config &= ~0x02;
    config &= ~0x20;
    ps2_write(0x64, 0x60);
    ps2_write(0x60, config);

    int type = detect_mouse_type();
    if (type == 0) { mouse_present = 0; ps2_write(0x64, 0xAE); return -1; }
    expected_packet_len = type;

    mouse_set_rate(100);
    if (mouse_cmd(0xF4) != 0) { mouse_present = 0; ps2_write(0x64, 0xAE); return -1; }

    ps2_write(0x64, 0x20);
    config = ps2_read();
    config |= 0x02;
    ps2_write(0x64, 0x60);
    ps2_write(0x60, config);

    ps2_write(0x64, 0xAE);
    mouse_present = 1;
    return 0;
}

static void mouse_process_packet(void) {
    int dx = 0, dy = 0;
    if (!(packet[0] & 0xC0)) {
        dx = (int)(int8_t)packet[1];
        dy = -(int)(int8_t)packet[2];
    }
    mouse_buttons = packet[0] & 0x07;
    mouse_x += dx;
    mouse_y += dy;
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_x > 79) mouse_x = 79;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_y > 24) mouse_y = 24;
}

void mouse_handler(void) {
    uint8_t st = inb(0x64);
    if ((st & 0x21) == 0x21) {
        uint8_t data = inb(0x60);
        if (packet_idx == 0) {
            if (data & 0x08) { packet[0] = data; packet_idx = 1; }
        } else if (packet_idx < expected_packet_len - 1) {
            packet[packet_idx++] = data;
        } else {
            packet[packet_idx] = data;
            packet_idx = 0;
            mouse_process_packet();
        }
    }
    outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

void mouse_poll(void) {
    uint32_t flags;
    asm volatile("pushfl; pop %0; cli" : "=r"(flags));
    for (int guard = 0; guard < 64; guard++) {
        uint8_t st = inb(0x64);
        if ((st & 0x21) != 0x21) break;
        uint8_t data = inb(0x60);
        if (packet_idx == 0) {
            if (data & 0x08) { packet[0] = data; packet_idx = 1; }
        } else if (packet_idx < expected_packet_len - 1) {
            packet[packet_idx++] = data;
        } else {
            packet[packet_idx] = data;
            packet_idx = 0;
            mouse_process_packet();
        }
    }
    asm volatile("push %0; popfl" : : "r"(flags) : "memory", "cc");
}

int mouse_get_x(void) { mouse_poll(); return mouse_x; }
int mouse_get_y(void) { mouse_poll(); return mouse_y; }
uint8_t mouse_get_buttons(void) { mouse_poll(); return mouse_buttons; }
int mouse_is_present(void) { return mouse_present; }
