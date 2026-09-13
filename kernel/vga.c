#include "types.h"

static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    asm volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

void vga_putpixel(int x, int y, uint8_t color) {
    if (x < 0 || x >= 320 || y < 0 || y >= 200) return;
    *(volatile uint8_t*)(0xA0000 + y * 320 + x) = color;
}

uint8_t vga_getpixel(int x, int y) {
    if (x < 0 || x >= 320 || y < 0 || y >= 200) return 0;
    return *(volatile uint8_t*)(0xA0000 + y * 320 + x);
}

void vga_fill_rect(int x, int y, int w, int h, uint8_t color) {
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            if (i >= 0 && i < 320 && j >= 0 && j < 200)
                *(volatile uint8_t*)(0xA0000 + j * 320 + i) = color;
}

void vga_draw_rect(int x, int y, int w, int h, uint8_t color) {
    for (int i = x; i < x + w; i++) {
        if (i >= 0 && i < 320) {
            if (y >= 0 && y < 200)
                *(volatile uint8_t*)(0xA0000 + y * 320 + i) = color;
            if (y + h - 1 >= 0 && y + h - 1 < 200)
                *(volatile uint8_t*)(0xA0000 + (y + h - 1) * 320 + i) = color;
        }
    }
    for (int j = y; j < y + h; j++) {
        if (j >= 0 && j < 200) {
            if (x >= 0 && x < 320)
                *(volatile uint8_t*)(0xA0000 + j * 320 + x) = color;
            if (x + w - 1 >= 0 && x + w - 1 < 320)
                *(volatile uint8_t*)(0xA0000 + j * 320 + x + w - 1) = color;
        }
    }
}

void vga_clear(uint8_t color) {
    for (int i = 0; i < 320 * 200; i++)
        *(volatile uint8_t*)(0xA0000 + i) = color;
}

void vga_set_palette(uint8_t idx, uint8_t r, uint8_t g, uint8_t b) {
    asm volatile("outb %0, %1" : : "a"(idx), "Nd"((uint16_t)0x3C8));
    asm volatile("outb %0, %1" : : "a"(r), "Nd"((uint16_t)0x3C9));
    asm volatile("outb %0, %1" : : "a"(g), "Nd"((uint16_t)0x3C9));
    asm volatile("outb %0, %1" : : "a"(b), "Nd"((uint16_t)0x3C9));
}

void vga_wait_vsync(void) {
    while (inb(0x3DA) & 0x08);
    while (!(inb(0x3DA) & 0x08));
}
