#include "types.h"

static volatile uint16_t* const VGA_TEXT = (volatile uint16_t*)0xB8000;
#define VGA_COLS 80
#define VGA_ROWS 25
#define VGA_ATTR(color) ((uint8_t)(color))

static int cursor_x = 0;
static int cursor_y = 0;

static void vga_text_putchar_at(int x, int y, char c, uint8_t color) {
    if (x < 0 || x >= VGA_COLS || y < 0 || y >= VGA_ROWS) return;
    VGA_TEXT[y * VGA_COLS + x] = (uint16_t)c | ((uint16_t)color << 8);
}

static void vga_text_scroll(void) {
    for (int y = 1; y < VGA_ROWS; y++)
        for (int x = 0; x < VGA_COLS; x++)
            VGA_TEXT[(y - 1) * VGA_COLS + x] = VGA_TEXT[y * VGA_COLS + x];
    for (int x = 0; x < VGA_COLS; x++)
        VGA_TEXT[(VGA_ROWS - 1) * VGA_COLS + x] = 0x0F20;
}

void vga_text_clear(uint8_t color) {
    for (int i = 0; i < VGA_COLS * VGA_ROWS; i++)
        VGA_TEXT[i] = (uint16_t)color << 8 | ' ';
    cursor_x = 0;
    cursor_y = 0;
}

void vga_text_putchar(char c, uint8_t color) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\r') {
        cursor_x = 0;
    } else if (c == '\t') {
        cursor_x = (cursor_x + 8) & ~7;
    } else if (c == '\b') {
        if (cursor_x > 0) { cursor_x--; vga_text_putchar_at(cursor_x, cursor_y, ' ', color); }
    } else {
        vga_text_putchar_at(cursor_x, cursor_y, c, color);
        cursor_x++;
    }
    if (cursor_x >= VGA_COLS) { cursor_x = 0; cursor_y++; }
    if (cursor_y >= VGA_ROWS) { vga_text_scroll(); cursor_y = VGA_ROWS - 1; }
}

void vga_text_puts(const char* s, uint8_t color) {
    while (*s) { vga_text_putchar(*s, color); s++; }
}

void vga_text_set_pos(int x, int y) { cursor_x = x; cursor_y = y; }

uint8_t vga_text_make_color(uint8_t fg, uint8_t bg) {
    return fg | (bg << 4);
}

void vga_putpixel(int x, int y, uint8_t color) { (void)x; (void)y; (void)color; }
void vga_fill_rect(int x, int y, int w, int h, uint8_t color) { (void)x; (void)y; (void)w; (void)h; (void)color; }
void vga_draw_rect(int x, int y, int w, int h, uint8_t color) { (void)x; (void)y; (void)w; (void)h; (void)color; }
void vga_clear(uint8_t color) { vga_text_clear(color); }
void vga_set_palette(uint8_t idx, uint8_t r, uint8_t g, uint8_t b) { (void)idx; (void)r; (void)g; (void)b; }
void vga_wait_vsync(void) {}
