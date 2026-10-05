#include "types.h"

extern void vga_text_clear(uint8_t color);
extern void vga_text_puts(const char* s, uint8_t color);
extern void vga_text_putchar(char c, uint8_t color);
extern void vga_text_set_pos(int x, int y);
extern uint8_t vga_text_make_color(uint8_t fg, uint8_t bg);
extern char kbd_getchar(void);
extern int kbd_tab_held(void);
extern int mouse_get_x(void);
extern int mouse_get_y(void);
extern uint8_t mouse_get_buttons(void);

static void kael_itoa(int v, char* b) {
    if (v < 0) { *b++ = '-'; v = -v; }
    if (v == 0) { *b++ = '0'; *b = '\0'; return; }
    char t[12]; int i = 0;
    while (v > 0) { t[i++] = '0' + (v % 10); v /= 10; }
    for (int j = 0; j < i; j++) b[j] = t[i - 1 - j];
    b[i] = '\0';
}

static int kael_atoi(const char* s) {
    int v = 0, neg = 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return neg ? -v : v;
}

static int kael_strlen(const char* s) { int i = 0; while (s[i]) i++; return i; }
static void kael_strcpy(char* d, const char* s) { while (*s) *d++ = *s++; *d = 0; }
static void kael_strcat(char* d, const char* s) { while (*d) d++; while (*s) *d++ = *s++; *d = 0; }
static int kael_strcmp(const char* a, const char* b) {
    while (*a && *b) { if (*a != *b) return *a - *b; a++; b++; }
    return *a - *b;
}
static int starts_with(const char* s, const char* p) {
    while (*p) { if (*s != *p) return 0; s++; p++; }
    return 1;
}

#define COLS 80
#define ROWS 25

/* VGA color: bg is high nibble, fg is low nibble */
#define BLACK 0
#define BLUE 1
#define GREEN 2
#define CYAN 3
#define RED 4
#define MAGENTA 5
#define BROWN 6
#define LIGHTGRAY 7
#define DARKGRAY 8
#define LIGHTBLUE 9
#define LIGHTGREEN 10
#define LIGHTCYAN 11
#define LIGHTRED 12
#define LIGHTMAGENTA 13
#define YELLOW 14
#define WHITE 15

#define ATTR(fg, bg) ((uint8_t)(((bg) << 4) | (fg)))

#define DESKTOP_BG BLUE
#define TITLE_FG LIGHTGRAY
#define TITLE_BG LIGHTBLUE
#define TITLE_BG_FOCUS LIGHTMAGENTA
#define BTN_FG WHITE
#define BTN_BG DARKGRAY
#define CLOSE_FG WHITE
#define CLOSE_BG RED
#define MENU_FG BLACK
#define MENU_BG LIGHTGRAY
#define MENU_SEL_FG WHITE
#define MENU_SEL_BG BLUE
#define BORDER_FG DARKGRAY
#define CONTENT_FG WHITE
#define CONTENT_BG BLACK

/* Window types */
#define WIN_NONE 0
#define WIN_TERMINAL 1
#define WIN_CALC 2
#define WIN_FILES 3
#define WIN_ABOUT 4

/* Menu items */
#define MENU_ITEMS 5
static const char* menu_labels[MENU_ITEMS] = {
    " Terminal ", " Calculator ", " File Manager ", " About ", " Exit "
};
static int menu_types[MENU_ITEMS] = { WIN_TERMINAL, WIN_CALC, WIN_FILES, WIN_ABOUT, 99 };

/* Window state */
typedef struct {
    int active;
    int type;
    int x, y, w, h;
    int drag_x, drag_y;
    int focused;
    char title[32];
    /* terminal state */
    char lines[24][72];
    int line_count, scroll;
    char input[64];
    int input_pos;
    char cwd[32];
    /* calculator state */
    char display[24];
    int acc;
    int pending;
    int op;
    int new_num;
    int error;
} window_t;

#define MAX_WINDOWS 6
static window_t wins[MAX_WINDOWS];

/* Global UI state */
static int menu_open = 0;
static int menu_sel = 0;
static int drag_win = -1;
static int last_mx = -1, last_my = -1;
static uint8_t last_buttons = 0;

/* Terminal filesystem */
typedef struct {
    char name[16];
    int is_dir;
    char content[256];
    int in_use;
} fs_entry_t;
#define MAX_FS 32
static fs_entry_t fs[MAX_FS];
static void fs_init(void) {
    for (int i = 0; i < MAX_FS; i++) fs[i].in_use = 0;
    int n = 0;
    kael_strcpy(fs[n].name, "readme.txt"); fs[n].is_dir = 0;
    kael_strcpy(fs[n].content, "Welcome to Kael OS!"); fs[n].in_use = 1; n++;
    kael_strcpy(fs[n].name, "docs"); fs[n].is_dir = 1; fs[n].in_use = 1; n++;
    kael_strcpy(fs[n].name, "hello.txt"); fs[n].is_dir = 0;
    kael_strcpy(fs[n].content, "Kael documentation."); fs[n].in_use = 1; n++;
}
static fs_entry_t* fs_find(const char* name) {
    for (int i = 0; i < MAX_FS; i++)
        if (fs[i].in_use && kael_strcmp(fs[i].name, name) == 0) return &fs[i];
    return 0;
}

/* Low-level VGA text cell access with backbuffer */
static inline void outb(uint16_t p, uint8_t v) { asm volatile("outb %0,%1" : : "a"(v), "Nd"(p)); }
static inline uint8_t inb(uint16_t p) { uint8_t r; asm volatile("inb %1,%0" : "=a"(r) : "Nd"(p)); return r; }

static volatile uint16_t* const VGA = (volatile uint16_t*)0xB8000;
static uint16_t back[COLS * ROWS];
static uint16_t shown[COLS * ROWS];

static void put_cell(int x, int y, char c, uint8_t attr) {
    if (x < 0 || x >= COLS || y < 0 || y >= ROWS) return;
    back[y * COLS + x] = (uint16_t)(uint8_t)c | ((uint16_t)attr << 8);
}

static void present(void) {
    for (int i = 0; i < COLS * ROWS; i++) {
        if (back[i] != shown[i]) { VGA[i] = back[i]; shown[i] = back[i]; }
    }
}

static void vga_disable_blink(void) {
    inb(0x3DA);
    outb(0x3C0, 0x30);
    uint8_t m = inb(0x3C1);
    m &= 0xF7;
    outb(0x3C0, m);
    inb(0x3DA);
    outb(0x3C0, 0x20);
}
static void fill_cells(int x, int y, int w, int h, char c, uint8_t attr) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            put_cell(x + i, y + j, c, attr);
}
static void draw_text(int x, int y, const char* s, uint8_t attr) {
    while (*s && x < COLS) { put_cell(x++, y, *s++, attr); }
}
static void draw_text_centered(int x, int y, int w, const char* s, uint8_t attr) {
    int len = kael_strlen(s);
    int sx = x + (w - len) / 2;
    if (sx < x) sx = x;
    draw_text(sx, y, s, attr);
}

/* Window management */
static int win_find_type(int type) {
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (wins[i].active && wins[i].type == type) return i;
    return -1;
}
static int win_create(int type) {
    int idx = win_find_type(type);
    if (idx >= 0) { wins[idx].focused = 1; for (int i = 0; i < MAX_WINDOWS; i++) if (i != idx) wins[i].focused = 0; return idx; }
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!wins[i].active) {
            wins[i].active = 1;
            wins[i].type = type;
            wins[i].focused = 1;
            wins[i].x = 5 + i * 3;
            wins[i].y = 3 + i * 2;
            wins[i].drag_x = -1; wins[i].drag_y = -1;
            wins[i].line_count = 0; wins[i].scroll = 0;
            wins[i].input_pos = 0; wins[i].input[0] = 0;
            kael_strcpy(wins[i].cwd, "/");
            wins[i].display[0] = 0; wins[i].acc = 0; wins[i].pending = 0;
            wins[i].op = 0; wins[i].new_num = 1; wins[i].error = 0;
            switch (type) {
                case WIN_TERMINAL:
                    wins[i].w = 50; wins[i].h = 14;
                    kael_strcpy(wins[i].title, "Terminal");
                    kael_strcpy(wins[i].lines[wins[i].line_count++], "Kael Terminal v1.0");
                    kael_strcpy(wins[i].lines[wins[i].line_count++], "Type 'help' for commands.");
                    kael_strcpy(wins[i].lines[wins[i].line_count++], "");
                    break;
                case WIN_CALC:
                    wins[i].w = 28; wins[i].h = 12;
                    kael_strcpy(wins[i].title, "Calculator");
                    break;
                case WIN_FILES:
                    wins[i].w = 40; wins[i].h = 16;
                    kael_strcpy(wins[i].title, "File Manager");
                    break;
                case WIN_ABOUT:
                    wins[i].w = 42; wins[i].h = 8;
                    kael_strcpy(wins[i].title, "About Kael OS");
                    break;
            }
            for (int j = 0; j < MAX_WINDOWS; j++) if (j != i) wins[j].focused = 0;
            return i;
        }
    }
    return -1;
}
static void win_close(int idx) { wins[idx].active = 0; wins[idx].focused = 0; }

/* Draw desktop background */
static void draw_desktop(void) {
    fill_cells(0, 0, COLS, ROWS, ' ', ATTR(DESKTOP_BG, DESKTOP_BG));
    /* top bar */
    fill_cells(0, 0, COLS, 1, ' ', ATTR(WHITE, BLUE));
    draw_text(1, 0, "Kael OS", ATTR(YELLOW, BLUE));
    draw_text(74, 0, "v2.1", ATTR(LIGHTGRAY, BLUE));
    /* bottom taskbar */
    fill_cells(0, ROWS - 1, COLS, 1, ' ', ATTR(BLACK, DARKGRAY));
    /* Start button */
    fill_cells(0, ROWS - 1, 7, 1, ' ', ATTR(BLACK, GREEN));
    draw_text(1, ROWS - 1, "Start", ATTR(BLACK, GREEN));
}

/* Draw window frame + content */
static void draw_window(int idx) {
    window_t* w = &wins[idx];
    uint8_t tb_bg = w->focused ? TITLE_BG_FOCUS : TITLE_BG;
    uint8_t tb_fg = w->focused ? WHITE : LIGHTGRAY;
    int x = w->x, y = w->y, ww = w->w, hh = w->h;
    /* border */
    fill_cells(x, y, ww, hh, ' ', ATTR(BORDER_FG, CONTENT_BG));
    /* title bar */
    fill_cells(x, y, ww, 1, ' ', ATTR(tb_fg, tb_bg));
    draw_text(x + 1, y, w->title, ATTR(tb_fg, tb_bg));
    /* close button */
    put_cell(x + ww - 2, y, 'X', ATTR(CLOSE_FG, CLOSE_BG));
    /* content area */
    int cx = x + 1, cy = y + 1, cw = ww - 2, ch = hh - 2;

    if (w->type == WIN_TERMINAL) {
        fill_cells(cx, cy, cw, ch, ' ', ATTR(CONTENT_FG, CONTENT_BG));
        int max_lines = ch - 1;
        int start = 0;
        if (w->line_count > max_lines) start = w->line_count - max_lines;
        for (int i = 0; i < max_lines && start + i < w->line_count; i++)
            draw_text(cx, cy + i, w->lines[start + i], ATTR(LIGHTGRAY, CONTENT_BG));
        char prompt[80];
        kael_strcpy(prompt, w->cwd); kael_strcat(prompt, "> ");
        draw_text(cx, cy + max_lines, prompt, ATTR(LIGHTGREEN, CONTENT_BG));
        draw_text(cx + kael_strlen(prompt), cy + max_lines, w->input, ATTR(WHITE, CONTENT_BG));
        put_cell(cx + kael_strlen(prompt) + w->input_pos, cy + max_lines, '_', ATTR(WHITE, CONTENT_BG));
    } else if (w->type == WIN_CALC) {
        fill_cells(cx, cy, cw, ch, ' ', ATTR(CONTENT_FG, CONTENT_BG));
        /* display */
        fill_cells(cx, cy, cw, 2, ' ', ATTR(BLACK, DARKGRAY));
        draw_text(cx + 1, cy, w->display[0] ? w->display : "0", ATTR(YELLOW, DARKGRAY));
        /* buttons grid 4 cols x 3 rows */
        const char* btns[12] = {"7","8","9","/","4","5","6","*","1","2","3","-"};
        const char* btns2[4] = {"0",".","C","+"};
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 4; c++) {
                int bx = cx + c * (cw / 4);
                int by = cy + 2 + r * 2;
                int bw = cw / 4;
                int bh = 2;
                fill_cells(bx, by, bw, bh, ' ', ATTR(WHITE, DARKGRAY));
                draw_text_centered(bx, by, bw, btns[r * 4 + c], ATTR(WHITE, DARKGRAY));
            }
        }
        for (int c = 0; c < 4; c++) {
            int bx = cx + c * (cw / 4);
            int by = cy + 8;
            int bw = cw / 4;
            fill_cells(bx, by, bw, 2, ' ', ATTR(WHITE, DARKGRAY));
            draw_text_centered(bx, by, bw, btns2[c], ATTR(WHITE, DARKGRAY));
        }
    } else if (w->type == WIN_FILES) {
        fill_cells(cx, cy, cw, ch, ' ', ATTR(CONTENT_FG, CONTENT_BG));
        draw_text(cx, cy, "Root Directory:", ATTR(LIGHTCYAN, CONTENT_BG));
        int fy = cy + 1;
        for (int i = 0; i < MAX_FS && fy < cy + ch; i++) {
            if (fs[i].in_use) {
                if (fs[i].is_dir) {
                    draw_text(cx + 1, fy, "[", ATTR(LIGHTBLUE, CONTENT_BG));
                    draw_text(cx + 2, fy, fs[i].name, ATTR(LIGHTBLUE, CONTENT_BG));
                    draw_text(cx + 2 + kael_strlen(fs[i].name), fy, "]", ATTR(LIGHTBLUE, CONTENT_BG));
                } else {
                    draw_text(cx + 2, fy, fs[i].name, ATTR(LIGHTGRAY, CONTENT_BG));
                }
                fy++;
            }
        }
        if (fy == cy + 1) draw_text(cx + 1, fy, "(empty)", ATTR(DARKGRAY, CONTENT_BG));
    } else if (w->type == WIN_ABOUT) {
        fill_cells(cx, cy, cw, ch, ' ', ATTR(CONTENT_FG, CONTENT_BG));
        draw_text_centered(cx, cy, cw, "Kael OS v2.1", ATTR(YELLOW, CONTENT_BG));
        draw_text_centered(cx, cy + 2, cw, "A lightweight hobby OS", ATTR(LIGHTGRAY, CONTENT_BG));
        draw_text_centered(cx, cy + 3, cw, "with GUI and PS/2 input", ATTR(LIGHTGRAY, CONTENT_BG));
        draw_text_centered(cx, cy + 5, cw, "Click X to close", ATTR(DARKGRAY, CONTENT_BG));
    }
}

/* Draw start menu */
static void draw_menu(void) {
    if (!menu_open) return;
    int mx = 0, my = ROWS - 1 - MENU_ITEMS - 1;
    int mw = 20, mh = MENU_ITEMS + 1;
    fill_cells(mx, my, mw, mh, ' ', ATTR(MENU_FG, MENU_BG));
    fill_cells(mx, my, mw, 1, ' ', ATTR(WHITE, BLUE));
    draw_text(mx + 1, my, "Kael OS Menu", ATTR(WHITE, BLUE));
    for (int i = 0; i < MENU_ITEMS; i++) {
        int iy = my + 1 + i;
        uint8_t fg = (i == menu_sel) ? MENU_SEL_FG : MENU_FG;
        uint8_t bg = (i == menu_sel) ? MENU_SEL_BG : MENU_BG;
        fill_cells(mx, iy, mw, 1, ' ', ATTR(fg, bg));
        draw_text(mx + 2, iy, menu_labels[i], ATTR(fg, bg));
    }
}

/* Draw mouse cursor (reverse video block) */
static void draw_cursor(int mx, int my) {
    if (mx >= 0 && mx < COLS && my >= 0 && my < ROWS) {
        uint8_t attr = (mouse_get_buttons() & 1) ? ATTR(BLACK, LIGHTRED) : ATTR(BLACK, WHITE);
        put_cell(mx, my, 0xDB, attr);
    }
}

/* Full redraw */
static void redraw(void) {
    draw_desktop();
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (wins[i].active) draw_window(i);
    draw_menu();
    draw_cursor(mouse_get_x(), mouse_get_y());
    present();
}

/* Terminal command processing */
static void term_add_line(window_t* w, const char* text) {
    if (w->line_count >= 24) {
        for (int i = 0; i < 23; i++)
            for (int j = 0; j < 72; j++) w->lines[i][j] = w->lines[i+1][j];
        w->line_count = 23;
    }
    int i = 0;
    while (text[i] && i < 71) { w->lines[w->line_count][i] = text[i]; i++; }
    w->lines[w->line_count][i] = 0;
    w->line_count++;
}

static void term_process(window_t* w, const char* cmd) {
    term_add_line(w, cmd);
    if (kael_strcmp(cmd, "help") == 0) {
        term_add_line(w, "Commands:");
        term_add_line(w, "  ls, cd <dir>, cat <file>");
        term_add_line(w, "  echo <text>, fetch");
        term_add_line(w, "  ver, whoami");
        term_add_line(w, "  clear, halt");
    } else if (kael_strcmp(cmd, "ls") == 0) {
        int found = 0;
        for (int i = 0; i < MAX_FS; i++) {
            if (fs[i].in_use) {
                found = 1;
                char buf[64];
                if (fs[i].is_dir) { kael_strcpy(buf, "  ["); kael_strcat(buf, fs[i].name); kael_strcat(buf, "]"); }
                else { kael_strcpy(buf, "  "); kael_strcat(buf, fs[i].name); }
                term_add_line(w, buf);
            }
        }
        if (!found) term_add_line(w, "  (empty)");
    } else if (starts_with(cmd, "cat ")) {
        fs_entry_t* e = fs_find(cmd + 4);
        if (!e) term_add_line(w, "File not found.");
        else if (e->is_dir) term_add_line(w, "Is a directory.");
        else term_add_line(w, e->content);
    } else if (starts_with(cmd, "echo ")) {
        term_add_line(w, cmd + 5);
    } else if (kael_strcmp(cmd, "ver") == 0) {
        term_add_line(w, "Kael OS v2.1");
        term_add_line(w, "Kernel: Aether 32-bit x86");
    } else if (kael_strcmp(cmd, "fetch") == 0) {
        term_add_line(w, "  -------------------------");
        term_add_line(w, "  OS:         Kael OS v2.1");
        term_add_line(w, "  Kernel:     Aether 32-bit x86");
        term_add_line(w, "  Bootloader: KaelBoot (custom MBR)");
        term_add_line(w, "  Shell:      KaelTerm");
        term_add_line(w, "  Display:    VGA text 80x25");
    } else if (kael_strcmp(cmd, "whoami") == 0) {
        term_add_line(w, "user@kael");
    } else if (kael_strcmp(cmd, "clear") == 0) {
        w->line_count = 0;
    } else if (kael_strcmp(cmd, "halt") == 0) {
        fill_cells(0, 0, COLS, ROWS, ' ', ATTR(WHITE, BLACK));
        draw_text_centered(0, 12, COLS, "System halted.", ATTR(WHITE, BLACK));
        present();
        while (1) asm volatile("hlt");
    } else if (cmd[0] != 0) {
        term_add_line(w, "Unknown command. Try 'help'.");
    }
}

/* Calculator logic */
static void calc_do_op(window_t* w) {
    int r = 0;
    if (w->op == '+') r = w->pending + kael_atoi(w->display);
    else if (w->op == '-') r = w->pending - kael_atoi(w->display);
    else if (w->op == '*') r = w->pending * kael_atoi(w->display);
    else if (w->op == '/') {
        int d = kael_atoi(w->display);
        if (d == 0) { kael_strcpy(w->display, "Err"); w->error = 1; return; }
        r = w->pending / d;
    }
    w->acc = r;
    kael_itoa(r, w->display);
    w->op = 0;
    w->new_num = 1;
}

static void calc_digit(window_t* w, char d) {
    if (w->error) { w->error = 0; w->display[0] = 0; }
    if (w->new_num) { w->display[0] = 0; w->new_num = 0; }
    if (kael_strlen(w->display) < 20) {
        if (w->display[0] == '0' && d == '.') { /* ok */ }
        else if (w->display[0] == '0' && d != '0' && d != '.') w->display[0] = d;
        else {
            char tmp[24]; kael_strcpy(tmp, w->display);
            kael_strcpy(w->display, tmp);
            int len = kael_strlen(w->display);
            if (len < 22) { w->display[len] = d; w->display[len+1] = 0; }
        }
    }
}

static void calc_button(window_t* w, const char* label) {
    if (w->error && kael_strcmp(label, "C") != 0) return;
    if (kael_strcmp(label, "C") == 0) {
        w->display[0] = 0; w->acc = 0; w->pending = 0; w->op = 0; w->new_num = 1; w->error = 0;
    } else if (kael_strcmp(label, "+") == 0 || kael_strcmp(label, "-") == 0 ||
               kael_strcmp(label, "*") == 0 || kael_strcmp(label, "/") == 0) {
        if (w->op && !w->new_num) calc_do_op(w);
        w->pending = kael_atoi(w->display);
        w->op = label[0];
        w->new_num = 1;
    } else if (kael_strcmp(label, "=") == 0) {
        calc_do_op(w);
    } else if (kael_strcmp(label, ".") == 0) {
        calc_digit(w, '.');
    } else {
        calc_digit(w, label[0]);
    }
}

/* Handle calculator click */
static void calc_click(window_t* w, int mx, int my) {
    int x = w->x + 1, y = w->y + 1, cw = w->w - 2;
    /* buttons area */
    const char* btns[12] = {"7","8","9","/","4","5","6","*","1","2","3","-"};
    const char* btns2[4] = {"0",".","C","+"};
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 4; c++) {
            int bx = x + c * (cw / 4);
            int by = y + 2 + r * 2;
            if (mx >= bx && mx < bx + cw/4 && my >= by && my < by + 2) {
                calc_button(w, btns[r*4+c]);
                return;
            }
        }
    }
    for (int c = 0; c < 4; c++) {
        int bx = x + c * (cw / 4);
        int by = y + 8;
        if (mx >= bx && mx < bx + cw/4 && my >= by && my < by + 2) {
            calc_button(w, btns2[c]);
            return;
        }
    }
    /* = button at bottom-right */
    int eqx = x + 3 * (cw/4), eqy = y + 8;
    if (mx >= eqx && mx < eqx + cw/4 && my >= eqy && my < eqy + 2)
        calc_button(w, "=");
}

/* Keyboard input for terminal */
static void term_key(window_t* w, char c) {
    if (c == '\n' || c == '\r') {
        w->input[w->input_pos] = 0;
        term_process(w, w->input);
        w->input_pos = 0;
        w->input[0] = 0;
    } else if (c == 8) {
        if (w->input_pos > 0) { w->input_pos--; w->input[w->input_pos] = 0; }
    } else if (c == 0x11 || c == 0x12 || c == 0x13 || c == 0x14) {
        /* arrows - ignore for now */
    } else if (c >= 32 && c < 127) {
        if (w->input_pos < 60) { w->input[w->input_pos++] = c; w->input[w->input_pos] = 0; }
    }
}

/* Hit test: which window is at (mx,my), and which part */
/* Returns: 0=nothing, 1=content, 2=title(drag), 3=close button */
static int win_hit(int idx, int mx, int my) {
    window_t* w = &wins[idx];
    if (mx < w->x || mx >= w->x + w->w || my < w->y || my >= w->y + w->h) return 0;
    /* close button */
    if (my == w->y && mx >= w->x + w->w - 2 && mx <= w->x + w->w - 1) return 3;
    /* title bar */
    if (my == w->y) return 2;
    return 1;
}

void desktop_run(void) {
    vga_disable_blink();
    fs_init();
    /* clear screen */
    fill_cells(0, 0, COLS, ROWS, ' ', ATTR(DESKTOP_BG, DESKTOP_BG));

    /* Open a terminal window at boot for immediate use */
    win_create(WIN_TERMINAL);

    redraw();

    while (1) {
        char c = kbd_getchar();
        int mx = mouse_get_x();
        int my = mouse_get_y();
        uint8_t btns = mouse_get_buttons();
        int moved = (mx != last_mx || my != last_my);
        int pressed = (btns & 1) && !(last_buttons & 1);
        int released = !(btns & 1) && (last_buttons & 1);
        int need_redraw = 0;

        /* Keyboard input */
        if (c) {
            /* find focused window */
            int fw = -1;
            for (int i = 0; i < MAX_WINDOWS; i++) if (wins[i].active && wins[i].focused) fw = i;

            if (c == '\t') {
                /* cycle windows */
                int found = 0;
                for (int i = 0; i < MAX_WINDOWS; i++) {
                    if (wins[i].active && !wins[i].focused) { wins[i].focused = 1; found = 1; for (int j = 0; j < MAX_WINDOWS; j++) if (j != i) wins[j].focused = 0; break; }
                }
                if (!found) { for (int i = 0; i < MAX_WINDOWS; i++) if (wins[i].active) { wins[i].focused = 1; for (int j = 0; j < MAX_WINDOWS; j++) if (j != i) wins[j].focused = 0; break; } }
                need_redraw = 1;
            } else if (c == 27) {
                /* Escape - toggle start menu */
                menu_open = !menu_open;
                menu_sel = 0;
                need_redraw = 1;
            } else if (c == '`') {
                /* Backtick - also toggle start menu */
                menu_open = !menu_open;
                menu_sel = 0;
                need_redraw = 1;
            } else if (menu_open) {
                if (c == '\n' || c == '\r') {
                    menu_open = 0;
                    if (menu_types[menu_sel] == 99) { fill_cells(0,0,COLS,ROWS,' ',ATTR(WHITE,BLACK)); draw_text_centered(0,12,COLS,"Goodbye!",ATTR(WHITE,BLACK)); present(); while(1) asm volatile("hlt"); }
                    win_create(menu_types[menu_sel]);
                    need_redraw = 1;
                } else if (c == 0x11) { menu_sel = (menu_sel - 1 + MENU_ITEMS) % MENU_ITEMS; need_redraw = 1; }
                else if (c == 0x12) { menu_sel = (menu_sel + 1) % MENU_ITEMS; need_redraw = 1; }
            } else if (fw >= 0) {
                if (wins[fw].type == WIN_TERMINAL) {
                    term_key(&wins[fw], c);
                    need_redraw = 1;
                } else if (c == 'q') {
                    win_close(fw);
                    need_redraw = 1;
                }
            } else if (c == 'q') {
                /* no window focused - nothing */
            }
        }

        /* Mouse input */
        if (pressed) {
            /* Check start button */
            if (my == ROWS - 1 && mx < 7) {
                menu_open = !menu_open;
                menu_sel = 0;
                need_redraw = 1;
            } else if (menu_open) {
                /* Check menu items */
                int menu_my = ROWS - 1 - MENU_ITEMS - 1;
                if (mx >= 0 && mx < 20 && my >= menu_my && my < menu_my + MENU_ITEMS + 1) {
                    if (my > menu_my) {
                        int item = my - menu_my - 1;
                        if (item >= 0 && item < MENU_ITEMS) {
                            menu_sel = item;
                            menu_open = 0;
                            if (menu_types[item] == 99) { fill_cells(0,0,COLS,ROWS,' ',ATTR(WHITE,BLACK)); draw_text_centered(0,12,COLS,"Goodbye!",ATTR(WHITE,BLACK)); present(); while(1) asm volatile("hlt"); }
                            win_create(menu_types[item]);
                            need_redraw = 1;
                        }
                    } else { menu_open = 0; need_redraw = 1; }
                } else { menu_open = 0; need_redraw = 1; }
            } else {
                /* Check windows (topmost = last in array first) */
                int hit = -1, hit_type = 0;
                for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
                    if (!wins[i].active) continue;
                    int ht = win_hit(i, mx, my);
                    if (ht) { hit = i; hit_type = ht; break; }
                }
                if (hit >= 0) {
                    if (hit_type == 3) { win_close(hit); need_redraw = 1; }
                    else {
                        /* focus window */
                        for (int i = 0; i < MAX_WINDOWS; i++) wins[i].focused = 0;
                        wins[hit].focused = 1;
                        if (hit_type == 2) {
                            drag_win = hit;
                            wins[hit].drag_x = mx - wins[hit].x;
                            wins[hit].drag_y = my - wins[hit].y;
                        }
                        /* calculator button clicks */
                        if (wins[hit].type == WIN_CALC) {
                            calc_click(&wins[hit], mx, my);
                        }
                        need_redraw = 1;
                    }
                }
            }
        }

        /* Drag window */
        if (drag_win >= 0 && (btns & 1)) {
            window_t* w = &wins[drag_win];
            w->x = mx - w->drag_x;
            w->y = my - w->drag_y;
            if (w->x < 0) w->x = 0;
            if (w->y < 1) w->y = 1;
            if (w->x + w->w > COLS) w->x = COLS - w->w;
            if (w->y + w->h > ROWS - 1) w->y = ROWS - 1 - w->h;
            need_redraw = 1;
        }
        if (released && drag_win >= 0) { drag_win = -1; need_redraw = 1; }

        /* Menu hover selection */
        if (menu_open && moved && !pressed) {
            int menu_my = ROWS - 1 - MENU_ITEMS - 1;
            if (mx >= 0 && mx < 20 && my > menu_my && my < menu_my + MENU_ITEMS + 1) {
                int item = my - menu_my - 1;
                if (item >= 0 && item < MENU_ITEMS && item != menu_sel) { menu_sel = item; need_redraw = 1; }
            }
        }

        /* Redraw when UI changed, mouse moved, or buttons changed */
        if (need_redraw || moved || btns != last_buttons)
            redraw();

        last_mx = mx;
        last_my = my;
        last_buttons = btns;

        for (volatile int i = 0; i < 20000; i++);
    }
}
