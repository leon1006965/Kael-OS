#include "types.h"

void vga_putpixel(int x, int y, uint8_t color) {
    *(volatile uint8_t*)(0xA0000 + y * 320 + x) = color;
}

uint8_t vga_getpixel(int x, int y) {
    return *(volatile uint8_t*)(0xA0000 + y * 320 + x);
}

void vga_fill_rect(int x, int y, int w, int h, uint8_t color) {
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            *(volatile uint8_t*)(0xA0000 + j * 320 + i) = color;
}

void vga_draw_rect(int x, int y, int w, int h, uint8_t color) {
    for (int i = x; i < x + w; i++) {
        *(volatile uint8_t*)(0xA0000 + y * 320 + i) = color;
        *(volatile uint8_t*)(0xA0000 + (y + h - 1) * 320 + i) = color;
    }
    for (int j = y; j < y + h; j++) {
        *(volatile uint8_t*)(0xA0000 + j * 320 + x) = color;
        *(volatile uint8_t*)(0xA0000 + j * 320 + x + w - 1) = color;
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
