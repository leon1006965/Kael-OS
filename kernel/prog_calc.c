#include "types.h"

extern void vga_fill_rect(int x, int y, int w, int h, uint8_t color);
extern void vga_draw_rect(int x, int y, int w, int h, uint8_t color);
extern void vga_clear(uint8_t color);
extern void font_draw_string(int x, int y, const char* s, uint8_t color);
extern void font_draw_string_bg(int x, int y, const char* s, uint8_t fg, uint8_t bg);
extern char kbd_getchar(void);

static int kael_strcmp(const char* a, const char* b) {
    while (*a && *b) { if (*a != *b) return *a - *b; a++; b++; }
    return *a - *b;
}

static char input[32];
static int input_pos = 0;
static int32_t result = 0;
static int has_result = 0;

static int32_t parse_num(const char* s, int* pos) {
    int32_t n = 0;
    int neg = 0;
    if (s[*pos] == '-') { neg = 1; (*pos)++; }
    while (s[*pos] >= '0' && s[*pos] <= '9') {
        n = n * 10 + (s[*pos] - '0');
        (*pos)++;
    }
    return neg ? -n : n;
}

static void calc_eval(void) {
    int pos = 0;
    int32_t a = parse_num(input, &pos);
    while (input[pos] == ' ') pos++;
    char op = input[pos];
    if (op == 0) { result = a; has_result = 1; return; }
    pos++;
    while (input[pos] == ' ') pos++;
    int32_t b = parse_num(input, &pos);
    if (op == '+') result = a + b;
    else if (op == '-') result = a - b;
    else if (op == '*') result = a * b;
    else if (op == '/') {
        if (b != 0) result = a / b;
        else { result = 0; return; }
    }
    has_result = 1;
}

static void calc_draw(void) {
    vga_clear(1);
    vga_fill_rect(40, 20, 240, 160, 15);
    vga_fill_rect(41, 21, 238, 158, 0);
    vga_fill_rect(41, 21, 238, 10, 9);
    font_draw_string(44, 22, "Calculator", 15);
    font_draw_string_bg(44, 40, ">", 15, 0);
    for (int i = 0; i < input_pos; i++) {
        char buf[2] = { input[i], 0 };
        font_draw_string_bg(52 + i * 8, 40, buf, 15, 0);
    }
    font_draw_string_bg(52 + input_pos * 8, 40, "_", 15, 0);
    if (has_result) {
        font_draw_string_bg(44, 60, "=", 15, 0);
        char numbuf[16];
        int32_t val = result;
        int neg = 0;
        if (val < 0) { neg = 1; val = -val; }
        int i = 15;
        numbuf[i] = '\0';
        if (val == 0) { numbuf[--i] = '0'; }
        else { while (val > 0 && i > 0) { numbuf[--i] = '0' + (val % 10); val /= 10; } }
        if (neg) numbuf[--i] = '-';
        font_draw_string_bg(52, 60, &numbuf[i], 14, 0);
    }
    font_draw_string_bg(44, 90, "Type: a + b", 8, 0);
    font_draw_string_bg(44, 100, "Ops: + - * /", 8, 0);
    font_draw_string_bg(44, 110, "Enter to calculate", 8, 0);
    font_draw_string_bg(44, 120, "Tab to exit", 8, 0);
    vga_fill_rect(41, 170, 238, 8, 7);
    font_draw_string(44, 171, "Calculator v0.1", 15);
}

void program_calc(void) {
    input_pos = 0;
    has_result = 0;
    calc_draw();
    while (1) {
        char c = kbd_getchar();
        if (!c) continue;
        if (c == '\t') return;
        if (c == '\n' || c == '\r') {
            if (input_pos > 0) { calc_eval(); calc_draw(); }
        } else if (c == 8) {
            if (input_pos > 0) input_pos--;
            calc_draw();
        } else if (c == 0x11 || c == 0x12 || c == 0x13 || c == 0x14) {
        } else {
            if (input_pos < 30) input[input_pos++] = c;
            calc_draw();
        }
    }
}
