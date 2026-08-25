#include "types.h"

extern void vga_fill_rect(int x, int y, int w, int h, uint8_t color);
extern void vga_draw_rect(int x, int y, int w, int h, uint8_t color);
extern void vga_clear(uint8_t color);
extern void vga_set_palette(uint8_t idx, uint8_t r, uint8_t g, uint8_t b);
extern void font_draw_string(int x, int y, const char* s, uint8_t color);
extern void font_draw_string_bg(int x, int y, const char* s, uint8_t fg, uint8_t bg);
extern char kbd_getchar(void);

extern void program_cube(void);
extern void program_notepad(void);
extern void program_fm(void);
extern void program_calc(void);

static int kael_strcmp(const char* a, const char* b) {
    while (*a && *b) { if (*a != *b) return *a - *b; a++; b++; }
    return *a - *b;
}

#define MAX_LINES 20
#define LINE_LEN 34

static int cmd_pos = 0;
static char cmd_buf[128];
static char lines[MAX_LINES][LINE_LEN + 1];
static int line_count = 0;
static int scroll_offset = 0;

static void add_line(const char* text) {
    if (line_count >= MAX_LINES) {
        for (int i = 0; i < MAX_LINES - 1; i++)
            for (int j = 0; j <= LINE_LEN; j++) lines[i][j] = lines[i+1][j];
        line_count = MAX_LINES - 1;
        if (scroll_offset > 0) scroll_offset++;
    }
    int i = 0;
    while (text[i] && i < LINE_LEN) { lines[line_count][i] = text[i]; i++; }
    lines[line_count][i] = '\0';
    line_count++;
    if (line_count - scroll_offset > 12) scroll_offset = line_count - 12;
}

static void draw_frame(void) {
    vga_clear(1);
    vga_fill_rect(20, 10, 280, 150, 15);
    vga_fill_rect(21, 11, 278, 148, 0);
    vga_fill_rect(21, 11, 278, 10, 9);
    font_draw_string(24, 13, "Term 0", 15);
    vga_fill_rect(289, 12, 8, 8, 4);
    font_draw_string(290, 13, "X", 15);
    vga_fill_rect(0, 184, 320, 16, 7);
    vga_draw_rect(0, 184, 320, 1, 8);
    font_draw_string(4, 186, "Kael", 11);
}

static void draw_content(void) {
    vga_fill_rect(22, 22, 276, 128, 0);
    int y = 24;
    int end = scroll_offset + 12;
    if (end > line_count) end = line_count;
    for (int i = scroll_offset; i < end; i++) {
        font_draw_string_bg(28, y, lines[i], 15, 0);
        y += 10;
    }
    font_draw_string_bg(28, 142, "kael>", 15, 0);
    for (int i = 0; i < cmd_pos; i++) {
        char buf[2] = { cmd_buf[i], 0 };
        font_draw_string_bg(68 + i * 8, 142, buf, 15, 0);
    }
    font_draw_string_bg(68 + cmd_pos * 8, 142, "_", 15, 0);
}

void desktop_run(void) {
    vga_set_palette(0,0,0,0);
    vga_set_palette(1,0,0,42);
    vga_set_palette(7,42,42,42);
    vga_set_palette(8,21,21,21);
    vga_set_palette(9,21,21,63);
    vga_set_palette(12,63,21,21);
    vga_set_palette(15,63,63,63);
    vga_set_palette(4,42,0,0);
    vga_set_palette(10,21,63,21);

    add_line("Welcome to Kael OS!");
    add_line("Type 'help' for commands.");
    add_line("Tab=refresh screen");

    draw_frame();
    draw_content();

    while (1) {
        char c = kbd_getchar();
        if (!c) continue;

        if (c == '\t') {
            draw_frame();
            draw_content();
            continue;
        }

        if (c == '\n' || c == '\r') {
            cmd_buf[cmd_pos] = '\0';
            if (cmd_pos > 0) {
                add_line(cmd_buf);
                if (kael_strcmp(cmd_buf, "help") == 0) {
                    add_line("Commands:");
                    add_line(" help  clear  ver  fetch");
                    add_line(" calc  cube  notepad  fm");
                } else if (kael_strcmp(cmd_buf, "cube") == 0) {
                    program_cube();
                    draw_frame();
                    line_count = 0;
                    scroll_offset = 0;
                    add_line("Returned from spinner.");
                } else if (kael_strcmp(cmd_buf, "calc") == 0) {
                    program_calc();
                    draw_frame();
                    line_count = 0;
                    scroll_offset = 0;
                    add_line("Returned from calculator.");
                } else if (kael_strcmp(cmd_buf, "notepad") == 0) {
                    program_notepad();
                    draw_frame();
                    line_count = 0;
                    scroll_offset = 0;
                    add_line("Returned from notepad.");
                } else if (kael_strcmp(cmd_buf, "fm") == 0) {
                    program_fm();
                    draw_frame();
                    line_count = 0;
                    scroll_offset = 0;
                    add_line("Returned from file manager.");
                } else if (kael_strcmp(cmd_buf, "fetch") == 0) {
                    add_line("Kael OS v0.3");
                    add_line("Kernel: Aether 32-bit x86");
                    add_line("Boot: Prometheus LBA MBR");
                    add_line("Display: VGA 320x200");
                } else if (kael_strcmp(cmd_buf, "clear") == 0) {
                    line_count = 0;
                    scroll_offset = 0;
                } else if (kael_strcmp(cmd_buf, "ver") == 0) {
                    add_line("Kael OS v0.3");
                    add_line("Aether kernel");
                    add_line("Prometheus boot");
                } else {
                    add_line("Unknown command.");
                }
            }
            cmd_pos = 0;
            cmd_buf[0] = '\0';
        }
        else if (c == 8) {
            if (cmd_pos > 0) cmd_pos--;
        }
        else {
            if (cmd_pos < 33) cmd_buf[cmd_pos++] = c;
        }
        draw_content();
    }
}
