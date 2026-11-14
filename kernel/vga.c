#include "types.h"

extern uint8_t font_get_row(char c, int row);

static volatile uint16_t* const VGA_TEXT = (volatile uint16_t*)0xB8000;
#define VGA_COLS 80
#define VGA_ROWS 25
#define VGA_ATTR(color) ((uint8_t)(color))

static int cursor_x = 0;
static int cursor_y = 0;

#define VBE_INFO_ADDR 0x6000u
#define VBE_READY_ADDR 0x5FF0u
#define TEXT_COLS 80
#define TEXT_ROWS 25

static volatile uint8_t* fb;
static uint32_t fb_width, fb_height, fb_pitch;
static int graphics_active;
static uint16_t previous_cells[TEXT_COLS * TEXT_ROWS];
static int graphics_first = 1;
static int cursor_valid;
static int cursor_needs_redraw;
static int cursor_prev_x, cursor_prev_y;
static int cursor_prev_pressed;

static const uint8_t vga_rgb[16][3] = {
    {0,0,0},       {0,0,170},       {0,170,0},       {0,170,170},
    {170,0,0},     {170,0,170},     {170,85,0},     {170,170,170},
    {85,85,85},    {85,85,255},     {85,255,85},    {85,255,255},
    {255,85,85},   {255,85,255},   {255,255,85},   {255,255,255}
};

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((uint16_t)(r >> 3) << 11) |
                      ((uint16_t)(g >> 2) << 5) | (b >> 3));
}

static uint16_t palette_color(uint8_t idx) {
    return rgb565(vga_rgb[idx & 15][0], vga_rgb[idx & 15][1], vga_rgb[idx & 15][2]);
}

static uint16_t wallpaper_color(uint32_t y) {
    /* Windows-inspired blue-to-white wallpaper gradient. */
    uint32_t den = fb_height > 1 ? fb_height - 1 : 1;
    uint8_t r = (uint8_t)(24 + (216u * y) / den);
    uint8_t g = (uint8_t)(72 + (168u * y) / den);
    uint8_t b = (uint8_t)(150 + (105u * y) / den);
    return rgb565(r, g, b);
}

int vga_graphics_init(void) {
    volatile uint8_t* info = (volatile uint8_t*)VBE_INFO_ADDR;
    if (*(volatile uint8_t*)VBE_READY_ADDR != 0xA5) return 0;

    uint16_t attr = *(volatile uint16_t*)(info + 0);
    fb_width = *(volatile uint16_t*)(info + 18);
    fb_height = *(volatile uint16_t*)(info + 20);
    uint8_t bpp = info[25];
    uint8_t memory_model = info[27];
    uint32_t pitch = *(volatile uint16_t*)(info + 50);
    if (!pitch) pitch = *(volatile uint16_t*)(info + 16);
    uint32_t base = *(volatile uint32_t*)(info + 40);

    if (!(attr & 0x80) || bpp != 16 || memory_model != 6 ||
        fb_width < 640 || fb_height < 480 || pitch < fb_width * 2 || !base)
        return 0;

    fb = (volatile uint8_t*)base;
    fb_pitch = pitch;
    graphics_active = 1;
    graphics_first = 1;
    return 1;
}

int vga_graphics_active(void) { return graphics_active; }

static void put_pixel(int x, int y, uint16_t color) {
    if (x < 0 || y < 0 || (uint32_t)x >= fb_width || (uint32_t)y >= fb_height)
        return;
    *(volatile uint16_t*)(fb + (uint32_t)y * fb_pitch + (uint32_t)x * 2) = color;
}

static void render_cell(const uint16_t* cells, int col, int row) {
    if (col < 0 || col >= TEXT_COLS || row < 0 || row >= TEXT_ROWS) return;
    uint16_t cell = cells[row * TEXT_COLS + col];
    char ch = (char)(cell & 0xFF);
    uint8_t attr = (uint8_t)(cell >> 8);
    uint16_t fg = palette_color(attr & 0x0F);
    uint8_t bgidx = (attr >> 4) & 0x0F;
    int cw = (int)(fb_width / TEXT_COLS);
    int chh = (int)(fb_height / TEXT_ROWS);
    int ox = col * cw, oy = row * chh;

    for (int py = 0; py < chh; py++) {
        uint8_t bits = font_get_row(ch, (py * 8) / chh);
        for (int px = 0; px < cw; px++) {
            int set = 0;
            if ((uint8_t)ch == 0xDB) set = 1;       /* full block */
            else if ((uint8_t)ch == 0xC4) set = (py * 8 / chh) == 4;
            else if ((uint8_t)ch == 0xB3) set = px == cw / 2;
            else if ((uint8_t)ch == 0xB0) set = ((px + py) & 1) == 0;
            else if ((unsigned char)ch >= 32 && (unsigned char)ch < 127)
                set = (bits & (1u << ((px * 8) / cw))) != 0;

            uint16_t bg = bgidx == 3 ? wallpaper_color((uint32_t)(oy + py)) : palette_color(bgidx);
            put_pixel(ox + px, oy + py, set ? fg : bg);
        }
    }
}

void vga_graphics_present(const uint16_t* cells) {
    if (!graphics_active) return;
    for (int i = 0; i < TEXT_COLS * TEXT_ROWS; i++) {
        if (graphics_first || cells[i] != previous_cells[i]) {
            int col = i % TEXT_COLS, row = i / TEXT_COLS;
            render_cell(cells, col, row);
            if (cursor_valid) {
                int cw = (int)(fb_width / TEXT_COLS);
                int chh = (int)(fb_height / TEXT_ROWS);
                int left = cursor_prev_x / cw;
                int right = (cursor_prev_x + 15) / cw;
                int top = cursor_prev_y / chh;
                int bottom = (cursor_prev_y + 17) / chh;
                if (col >= left && col <= right && row >= top && row <= bottom)
                    cursor_needs_redraw = 1;
            }
            previous_cells[i] = cells[i];
        }
    }
    graphics_first = 0;
}

static void draw_pointer_pixels(int x, int y, int pressed) {
    /* Black outline, white (yellow while pressed) fill; points upper-left. */
    static const uint16_t mask[18] = {
        0x8000,0xC000,0xE000,0xF000,0xF800,0xFC00,
        0xFE00,0xFF00,0xFF80,0xFFC0,0xFFE0,0xFFF0,
        0x0FC0,0x0780,0x0780,0x0780,0x0780,0x0FC0
    };
    uint16_t edge = rgb565(0, 0, 0);
    uint16_t fill = pressed ? rgb565(255, 220, 0) : rgb565(255, 255, 255);
    for (int py = 0; py < 18; py++) {
        for (int px = 0; px < 16; px++) {
            uint16_t bit = (uint16_t)(0x8000u >> px);
            if (!(mask[py] & bit)) continue;
            int border = px == 0 || px == 15 || py == 0 || py == 17 ||
                         !(mask[py] & (uint16_t)(bit << 1)) ||
                         !(mask[py] & (uint16_t)(bit >> 1)) ||
                         !(mask[py - 1] & bit) || !(mask[py + 1] & bit);
            put_pixel(x + px, y + py, border ? edge : fill);
        }
    }
}

void vga_graphics_cursor(const uint16_t* cells, int logical_x, int logical_y,
                         int pressed) {
    if (!graphics_active) return;
    int cw = (int)(fb_width / TEXT_COLS);
    int chh = (int)(fb_height / TEXT_ROWS);
    int x = logical_x * (int)fb_width / 640;
    int y = logical_y * (int)fb_height / 400;
    if (cursor_valid) {
        if (x == cursor_prev_x && y == cursor_prev_y &&
            pressed == cursor_prev_pressed && !cursor_needs_redraw)
            return;
        int left = cursor_prev_x / cw;
        int right = (cursor_prev_x + 15) / cw;
        int top = cursor_prev_y / chh;
        int bottom = (cursor_prev_y + 17) / chh;
        for (int row = top; row <= bottom; row++)
            for (int col = left; col <= right; col++)
                render_cell(cells, col, row);
    }
    draw_pointer_pixels(x, y, pressed);
    cursor_prev_x = x;
    cursor_prev_y = y;
    cursor_prev_pressed = pressed;
    cursor_valid = 1;
    cursor_needs_redraw = 0;
}

static void vga_text_putchar_at(int x, int y, char c, uint8_t color) {
    if (x < 0 || x >= VGA_COLS || y < 0 || y >= VGA_ROWS) return;
    VGA_TEXT[y * VGA_COLS + x] = (uint16_t)(uint8_t)c | ((uint16_t)color << 8);
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
