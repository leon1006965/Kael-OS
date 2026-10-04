#include "types.h"

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t r; asm volatile("inb %1,%0" : "=a"(r) : "Nd"(port)); return r;
}
static inline void io_wait(void) { outb(0x80, 0); }

#define PS2_TIMEOUT 100000

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
static int ps2_read(void) {
    if (ps2_wait_read() != 0) return -1;
    return inb(0x60);
}
static void ps2_write(uint16_t port, uint8_t val) {
    ps2_wait_write();
    outb(port, val);
}
static void ps2_flush(void) {
    for (int i = 0; i < 16 && (inb(0x64) & 1); i++) inb(0x60);
}

static int mouse_cmd(uint8_t val) {
    for (int retry = 0; retry < 3; retry++) {
        ps2_write(0x64, 0xD4);
        io_wait();
        ps2_write(0x60, val);
        int r = ps2_read();
        if (r == 0xFA) return 0;
        if (r != 0xFE) return -1;
    }
    return -1;
}

static volatile int mouse_x = 40;
static volatile int mouse_y = 12;
static volatile uint8_t mouse_buttons = 0;
static volatile uint8_t packet[3];
static volatile int packet_idx = 0;
static int acc_x = 0, acc_y = 0;
static int mouse_present = 0;

int mouse_init(void) {
    ps2_flush();
    ps2_write(0x64, 0xAD);
    ps2_write(0x64, 0xA8);
    ps2_flush();

    ps2_write(0x64, 0x20);
    int c = ps2_read();
    if (c < 0) { ps2_write(0x64, 0xAE); return -1; }
    uint8_t config = (uint8_t)c;
    config &= ~0x02;
    config &= ~0x20;
    config &= ~0x10;  // make sure keyboard clock is enabled
    ps2_write(0x64, 0x60);
    ps2_write(0x60, config);

    if (mouse_cmd(0xFF) == 0) {
        int st = -1;
        for (int i = 0; i < 20 && st < 0; i++) st = ps2_read();
        if (st == 0xAA) ps2_read();
    }
    ps2_flush();

    mouse_cmd(0xF6);
    if (mouse_cmd(0xF4) != 0) {
        ps2_write(0x64, 0xAE);
        return -1;
    }
    ps2_flush();

    ps2_write(0x64, 0x20);
    c = ps2_read();
    config = (c < 0) ? config : (uint8_t)c;
    config |= 0x02;
    ps2_write(0x64, 0x60);
    ps2_write(0x60, config);

    ps2_write(0x64, 0xAE);
    packet_idx = 0;
    mouse_present = 1;
    return 0;
}

static void process_packet(void) {
    uint8_t s = packet[0];
    if (s & 0xC0) return;

    int dx = packet[1] - ((s << 4) & 0x100);
    int dy = -(packet[2] - ((s << 3) & 0x100));

    mouse_buttons = s & 0x07;

    acc_x += dx;  mouse_x += acc_x / 8;   acc_x %= 8;
    acc_y += dy;  mouse_y += acc_y / 16;  acc_y %= 16;

    if (mouse_x < 0) mouse_x = 0;
    if (mouse_x > 79) mouse_x = 79;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_y > 24) mouse_y = 24;
}

static void feed(uint8_t data) {
    if (packet_idx == 0) {
        if (!(data & 0x08)) return;
        packet[0] = data;
        packet_idx = 1;
    } else if (packet_idx == 1) {
        packet[1] = data;
        packet_idx = 2;
    } else {
        packet[2] = data;
        packet_idx = 0;
        process_packet();
    }
}

static void drain(void) {
    for (int guard = 0; guard < 64; guard++) {
        if ((inb(0x64) & 0x21) != 0x21) break;
        feed(inb(0x60));
    }
}

void mouse_handler(void) {
    drain();
    outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

void mouse_poll(void) {
    uint32_t flags;
    asm volatile("pushfl; pop %0; cli" : "=r"(flags));
    drain();
    asm volatile("push %0; popfl" : : "r"(flags) : "memory", "cc");
}

int mouse_get_x(void) { mouse_poll(); return mouse_x; }
int mouse_get_y(void) { mouse_poll(); return mouse_y; }
uint8_t mouse_get_buttons(void) { mouse_poll(); return mouse_buttons; }
int mouse_is_present(void) { return mouse_present; }
