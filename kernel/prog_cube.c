#include "types.h"

extern void vga_fill_rect(int x, int y, int w, int h, uint8_t color);
extern void vga_putpixel(int x, int y, uint8_t color);
extern void font_draw_string(int x, int y, const char* s, uint8_t color);
extern char kbd_getchar(void);

#define PI 31416
#define FIXED_SCALE 10000

static int isin(int angle) {
    angle = angle % (2 * PI);
    if (angle < 0) angle += 2 * PI;
    int neg = 0;
    if (angle > PI) { angle -= PI; neg = 1; }
    if (angle > PI / 2) { angle = PI - angle; neg = !neg; }
    int x = angle;
    int x2 = (x * x) / FIXED_SCALE;
    int x3 = (x2 * x) / FIXED_SCALE;
    int x5 = (x3 * x2) / FIXED_SCALE;
    int result = x - x3 / 6 + x5 / 120;
    return neg ? -result : result;
}

static int icos(int angle) {
    return isin(angle + PI / 2);
}

static void draw_line(int x0, int y0, int x1, int y1, uint8_t color) {
    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    for (int i = 0; i < 300; i++) {
        if (x0 >= 0 && x0 < 320 && y0 >= 0 && y0 < 200)
            vga_putpixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
}

static void draw_circle_points(int cx, int cy, int r, int angle, uint8_t color) {
    int points = 8;
    int prev_x = cx + (r * icos(angle)) / FIXED_SCALE;
    int prev_y = cy + (r * isin(angle)) / FIXED_SCALE;
    for (int i = 1; i <= points; i++) {
        int a = angle + (i * 2 * PI) / points;
        int px = cx + (r * icos(a)) / FIXED_SCALE;
        int py = cy + (r * isin(a)) / FIXED_SCALE;
        draw_line(prev_x, prev_y, px, py, color);
        prev_x = px;
        prev_y = py;
    }
}

void program_cube(void) {
    int angle = 0;
    int radius = 70;
    while (1) {
        vga_fill_rect(0, 0, 320, 200, 0);
        font_draw_string(4, 4, "Math Spinner - Any key to exit", 15);
        draw_circle_points(160, 100, radius, angle, 12);
        draw_circle_points(160, 100, radius - 20, angle + PI / 4, 9);
        draw_circle_points(160, 100, radius - 40, angle + PI / 2, 10);
        draw_circle_points(160, 100, radius - 55, angle + 3 * PI / 4, 11);
        for (int dy = -2; dy <= 2; dy++)
            for (int dx = -2; dx <= 2; dx++)
                vga_putpixel(160 + dx, 100 + dy, 14);
        angle += 80;
        if (angle > 2 * PI) angle -= 2 * PI;
        for (volatile int d = 0; d < 30000; d++) {}
        if (kbd_getchar()) return;
    }
}
