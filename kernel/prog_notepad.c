#include "types.h"

extern void vga_fill_rect(int x, int y, int w, int h, uint8_t color);
extern void vga_draw_rect(int x, int y, int w, int h, uint8_t color);
extern void vga_clear(uint8_t color);
extern void font_draw_string(int x, int y, const char* s, uint8_t color);
extern void font_draw_string_bg(int x, int y, const char* s, uint8_t fg, uint8_t bg);
extern void font_draw_char_bg(int x, int y, char c, uint8_t fg, uint8_t bg);
extern char kbd_getchar(void);

#define PAD_COLS 36
#define PAD_ROWS 18

static char pad[PAD_ROWS][PAD_COLS + 1];
static int cur_x = 0, cur_y = 0;
static int scroll = 0;
static char filename[32] = "untitled";

static void pad_draw(void) {
    vga_clear(1);
    vga_fill_rect(10, 5, 300, 180, 15);
    vga_fill_rect(11, 6, 298, 178, 0);
    vga_fill_rect(11, 6, 298, 10, 9);
    font_draw_string(14, 7, "Notepad", 15);
    font_draw_string(80, 7, filename, 15);
    vga_fill_rect(297, 7, 8, 8, 4);
    font_draw_string(298, 8, "X", 15);
    vga_fill_rect(11, 176, 298, 8, 7);
    font_draw_string(14, 177, "Arrows=move Tab=type", 15);
    int y = 18;
    for (int r = 0; r < PAD_ROWS && y < 172; r++) {
        for (int c = 0; c < PAD_COLS; c++) {
            char ch = pad[r + scroll][c];
            if (ch == 0) ch = ' ';
            uint8_t bg = (r + scroll == cur_y && c == cur_x) ? 9 : 0;
            font_draw_char_bg(14 + c * 8, y, ch, 15, bg);
        }
        y += 9;
    }
}

void program_notepad(void) {
    cur_x = 0; cur_y = 0; scroll = 0;
    for (int r = 0; r < PAD_ROWS; r++)
        for (int c = 0; c <= PAD_COLS; c++)
            pad[r][c] = 0;
    pad_draw();
    while (1) {
        char c = kbd_getchar();
        if (!c) continue;
        if (c == 0x11) { if (cur_y > 0) cur_y--; }
        else if (c == 0x12) { if (cur_y < PAD_ROWS - 1) cur_y++; }
        else if (c == 0x13) { if (cur_x > 0) cur_x--; }
        else if (c == 0x14) { if (cur_x < PAD_COLS - 1) cur_x++; }
        else if (c == '\t') {
            pad[cur_y][cur_x] = ' ';
            cur_x++;
            if (cur_x >= PAD_COLS) { cur_x = 0; cur_y++; }
        }
        else if (c == '\n' || c == '\r') {
            cur_x = 0;
            cur_y++;
            if (cur_y >= PAD_ROWS) cur_y = PAD_ROWS - 1;
        }
        else if (c == 8) {
            if (cur_x > 0) cur_x--;
            else if (cur_y > 0) { cur_y--; cur_x = PAD_COLS - 1; }
            pad[cur_y][cur_x] = 0;
        }
        else {
            pad[cur_y][cur_x] = c;
            cur_x++;
            if (cur_x >= PAD_COLS) { cur_x = 0; cur_y++; }
            if (cur_y >= PAD_ROWS) cur_y = PAD_ROWS - 1;
        }
        if (cur_y >= scroll + PAD_ROWS) scroll = cur_y - PAD_ROWS + 1;
        if (cur_y < scroll) scroll = cur_y;
        pad_draw();
    }
}
