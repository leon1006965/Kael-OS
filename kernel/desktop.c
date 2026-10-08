#include "types.h"

extern char kbd_getchar(void);
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

/* Classic Mac SE colors */
#define MAC_DESKTOP     ATTR(LIGHTGRAY, LIGHTGRAY)
#define MAC_MENUBAR     ATTR(BLACK, WHITE)
#define MAC_WINDOW_BG   ATTR(BLACK, WHITE)
#define MAC_TITLE_FG    ATTR(BLACK, WHITE)
#define MAC_TITLE_BG    ATTR(BLACK, WHITE)
#define MAC_TITLE_ACT   ATTR(BLACK, LIGHTMAGENTA)
#define MAC_TITLE_INACT ATTR(DARKGRAY, LIGHTGRAY)
#define MAC_BORDER      ATTR(BLACK, LIGHTGRAY)
#define MAC_SHADOW      ATTR(LIGHTGRAY, BLACK)
#define MAC_CLOSE_BG    ATTR(WHITE, BLACK)
#define MAC_MENU_FG     ATTR(BLACK, WHITE)
#define MAC_MENU_BG     ATTR(WHITE, BLACK)
#define MAC_MENU_SEL    ATTR(WHITE, BLACK)

/* Box drawing characters */
#define BOX_TL  0xDA  /* ┌ */
#define BOX_TR  0xBF  /* ┐ */
#define BOX_BL  0xC0  /* └ */
#define BOX_BR  0xD9  /* ┘ */
#define BOX_H   0xC4  /* ─ */
#define BOX_V   0xB3  /* │ */

/* Window types */
#define WIN_NONE 0
#define WIN_TERMINAL 1
#define WIN_CALC 2
#define WIN_FILES 3
#define WIN_ABOUT 4
#define WIN_CLOCK 5
#define WIN_CALENDAR 6

/* CMOS Real-Time Clock */
static inline void outb(uint16_t p, uint8_t v) { asm volatile("outb %0,%1" : : "a"(v), "Nd"(p)); }
static inline uint8_t inb(uint16_t p) { uint8_t r; asm volatile("inb %1,%0" : "=a"(r) : "Nd"(p)); return r; }
static uint8_t cmos_read(uint8_t reg) { outb(0x70, reg); return inb(0x71); }
static int bcd2bin(uint8_t b) { return (b >> 4) * 10 + (b & 0x0F); }
static int g_hour, g_min, g_sec, g_month, g_day, g_year, g_dow;

static void rtc_init(void) {
    g_sec = bcd2bin(cmos_read(0x00));
    g_min = bcd2bin(cmos_read(0x02));
    uint8_t h = cmos_read(0x04);
    g_hour = bcd2bin(h);
    uint8_t sb = cmos_read(0x0B);
    if (!(sb & 0x02)) {
        int pm = (h & 0x80) != 0;
        g_hour = g_hour & 0x7F;
        if (pm && g_hour != 12) g_hour += 12;
        if (!pm && g_hour == 12) g_hour = 0;
    }
    g_dow = bcd2bin(cmos_read(0x06));
    g_day = bcd2bin(cmos_read(0x07));
    g_month = bcd2bin(cmos_read(0x08));
    g_year = 2000 + bcd2bin(cmos_read(0x09));
}

/* Window state */
typedef struct {
    int active, type, x, y, w, h, drag_x, drag_y, focused;
    char title[32];
    char lines[24][72];
    int line_count, scroll;
    char input[64];
    int input_pos;
    char cwd[32];
    char display[24];
    int acc, pending, op, new_num, error;
} window_t;

#define MAX_WINDOWS 6
static window_t wins[MAX_WINDOWS];

/* Menu bar state */
static int menu_bar_open = 0;      /* Which menu is open: 0=none, 1=Apple, 2=File, 3=Edit, 4=View */
static int menu_bar_sel = 0;
static int drag_win = -1;
static int last_mx = -1, last_my = -1;
static uint8_t last_buttons = 0;

/* App-specific menu items */
static const char* apple_menu[] = { "About Kael OS", "Settings...", "-", "Restart", "Shut Down", 0 };
static const char* term_file_menu[] = { "New Window", "-", "Close", 0 };
static const char* term_edit_menu[] = { "Clear", 0 };
static const char* term_shell_menu[] = { "Exit", 0 };
static const char* calc_edit_menu[] = { "Clear", 0 };
static const char* files_file_menu[] = { "New Folder", "New File", "-", "Close", 0 };
static const char* files_view_menu[] = { "as Icons", "as List", 0 };

/* Which menus exist per app type */
static int has_file_menu(int t) { return t == WIN_TERMINAL || t == WIN_FILES; }
static int has_edit_menu(int t) { return t == WIN_TERMINAL || t == WIN_CALC; }
static int has_shell_menu(int t) { return t == WIN_TERMINAL; }
static int has_view_menu(int t) { return t == WIN_FILES; }

/* Terminal filesystem */
typedef struct { char name[16]; int is_dir; char content[256]; int in_use; } fs_entry_t;
#define MAX_FS 32
static fs_entry_t fs[MAX_FS];
static void fs_init(void) {
    for (int i = 0; i < MAX_FS; i++) fs[i].in_use = 0;
    int n = 0;
    fs[n].in_use = 1; kael_strcpy(fs[n].name, "readme.txt"); kael_strcpy(fs[n].content, "Welcome to Kael OS!"); n++;
    fs[n].in_use = 1; fs[n].is_dir = 1; kael_strcpy(fs[n].name, "docs"); n++;
    fs[n].in_use = 1; kael_strcpy(fs[n].name, "hello.txt"); kael_strcpy(fs[n].content, "Hello from Kael!"); n++;
}
static fs_entry_t* fs_find(const char* name) {
    for (int i = 0; i < MAX_FS; i++) if (fs[i].in_use && kael_strcmp(fs[i].name, name) == 0) return &fs[i];
    return 0;
}

/* VGA backbuffer */
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
    inb(0x3DA); outb(0x3C0, 0x30);
    uint8_t m = inb(0x3C1); m &= 0xF7;
    outb(0x3C0, m); inb(0x3DA); outb(0x3C0, 0x20);
}
static void fill_cells(int x, int y, int w, int h, char c, uint8_t attr) {
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) put_cell(x + i, y + j, c, attr);
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

/* Draw box border */
static void draw_box(int x, int y, int w, int h, uint8_t attr) {
    put_cell(x, y, BOX_TL, attr);
    put_cell(x + w - 1, y, BOX_TR, attr);
    put_cell(x, y + h - 1, BOX_BL, attr);
    put_cell(x + w - 1, y + h - 1, BOX_BR, attr);
    for (int i = 1; i < w - 1; i++) {
        put_cell(x + i, y, BOX_H, attr);
        put_cell(x + i, y + h - 1, BOX_H, attr);
    }
    for (int j = 1; j < h - 1; j++) {
        put_cell(x, y + j, BOX_V, attr);
        put_cell(x + w - 1, y + j, BOX_V, attr);
    }
}

/* Window management */
static int win_find_type(int type) {
    for (int i = 0; i < MAX_WINDOWS; i++) if (wins[i].active && wins[i].type == type) return i;
    return -1;
}
static int win_create(int type) {
    int idx = win_find_type(type);
    if (idx >= 0) { wins[idx].focused = 1; for (int i = 0; i < MAX_WINDOWS; i++) if (i != idx) wins[i].focused = 0; return idx; }
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!wins[i].active) {
            wins[i].active = 1; wins[i].type = type; wins[i].focused = 1;
            wins[i].x = 8 + i * 3; wins[i].y = 4 + i * 2;
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
                    wins[i].w = 24; wins[i].h = 14;
                    kael_strcpy(wins[i].title, "Calculator");
                    break;
                case WIN_FILES:
                    wins[i].w = 36; wins[i].h = 12;
                    kael_strcpy(wins[i].title, "File Manager");
                    break;
                case WIN_ABOUT:
                    wins[i].w = 36; wins[i].h = 8;
                    kael_strcpy(wins[i].title, "About Kael OS");
                    break;
                case WIN_CLOCK:
                    wins[i].w = 24; wins[i].h = 7;
                    kael_strcpy(wins[i].title, "Clock");
                    break;
                case WIN_CALENDAR:
                    wins[i].w = 32; wins[i].h = 12;
                    kael_strcpy(wins[i].title, "Calendar");
                    break;
            }
            for (int j = 0; j < MAX_WINDOWS; j++) if (j != i) wins[j].focused = 0;
            return i;
        }
    }
    return -1;
}
static void win_close(int idx) { wins[idx].active = 0; wins[idx].focused = 0; }

/* Get focused window index */
static int get_focused(void) {
    for (int i = 0; i < MAX_WINDOWS; i++) if (wins[i].active && wins[i].focused) return i;
    return -1;
}

/* Draw menu bar at top */
static void draw_menubar(void) {
    int fw = get_focused();
    /* Menu bar background */
    fill_cells(0, 0, COLS, 1, ' ', MAC_MENUBAR);
    /* Apple logo */
    put_cell(0, 0, 0x0E, MAC_MENUBAR);
    /* App name */
    int cx = 2;
    if (fw >= 0) {
        draw_text(cx, 0, wins[fw].title, MAC_MENUBAR);
        cx += kael_strlen(wins[fw].title) + 2;
    }
    /* File menu */
    if (fw >= 0 && has_file_menu(wins[fw].type)) {
        draw_text(cx, 0, "File", MAC_MENUBAR);
        cx += 5;
    }
    /* Edit menu */
    if (fw >= 0 && has_edit_menu(wins[fw].type)) {
        draw_text(cx, 0, "Edit", MAC_MENUBAR);
        cx += 5;
    }
    /* Shell menu */
    if (fw >= 0 && has_shell_menu(wins[fw].type)) {
        draw_text(cx, 0, "Shell", MAC_MENUBAR);
        cx += 6;
    }
    /* View menu */
    if (fw >= 0 && has_view_menu(wins[fw].type)) {
        draw_text(cx, 0, "View", MAC_MENUBAR);
        cx += 5;
    }
    /* Clock on right */
    char cbuf[16]; int v, bi = 0;
    v = g_hour; if (v >= 10) cbuf[bi++] = '0' + v / 10; cbuf[bi++] = '0' + v % 10; cbuf[bi++] = ':';
    v = g_min; if (v >= 10) cbuf[bi++] = '0' + v / 10; cbuf[bi++] = '0' + v % 10;
    cbuf[bi] = 0;
    draw_text(COLS - kael_strlen(cbuf) - 1, 0, cbuf, MAC_MENUBAR);
}

/* Draw dropdown menu */
static void draw_dropdown(int x, const char** items) {
    int maxw = 0, count = 0;
    for (int i = 0; items[i]; i++) { int l = kael_strlen(items[i]); if (l > maxw) maxw = l; count++; }
    maxw += 4;
    int mh = count + 2;
    int my = 1;
    if (my + mh > ROWS - 1) my = ROWS - 1 - mh;
    /* Shadow */
    fill_cells(x + 1, my + 1, maxw, mh, ' ', MAC_SHADOW);
    /* Menu */
    fill_cells(x, my, maxw, mh, ' ', MAC_MENU_BG);
    draw_box(x, my, maxw, mh, ATTR(BLACK, WHITE));
    for (int i = 0; i < count; i++) {
        int iy = my + 1 + i;
        if (kael_strcmp(items[i], "-") == 0) {
            for (int j = 1; j < maxw - 1; j++) put_cell(x + j, iy, BOX_H, ATTR(DARKGRAY, WHITE));
        } else {
            draw_text(x + 2, iy, items[i], MAC_MENU_FG);
        }
    }
}

/* Draw window with Mac SE style */
static void draw_window(int idx) {
    window_t* w = &wins[idx];
    int x = w->x, y = w->y, ww = w->w, hh = w->h;
    uint8_t tb = w->focused ? MAC_TITLE_ACT : MAC_TITLE_INACT;

    /* Shadow (offset 1,1) */
    fill_cells(x + 1, y + 1, ww, hh, ' ', MAC_SHADOW);

    /* Window border */
    draw_box(x, y, ww, hh, MAC_BORDER);

    /* Title bar */
    fill_cells(x + 1, y + 1, ww - 2, 1, ' ', tb);
    draw_text(x + 2, y + 1, w->title, tb);
    /* Close box */
    fill_cells(x + 1, y + 1, 3, 1, ' ', tb);
    put_cell(x + 1, y + 1, 0xFE, tb);

    /* Content area */
    int cx = x + 1, cy = y + 2, cw = ww - 2, ch = hh - 3;

    if (w->type == WIN_TERMINAL) {
        fill_cells(cx, cy, cw, ch, ' ', ATTR(LIGHTGRAY, BLACK));
        int max_lines = ch - 1;
        int start = 0;
        if (w->line_count > max_lines) start = w->line_count - max_lines;
        for (int i = 0; i < max_lines && start + i < w->line_count; i++)
            draw_text(cx, cy + i, w->lines[start + i], ATTR(LIGHTGRAY, BLACK));
        char prompt[80];
        kael_strcpy(prompt, w->cwd); kael_strcat(prompt, "> ");
        draw_text(cx, cy + max_lines, prompt, ATTR(LIGHTGREEN, BLACK));
        draw_text(cx + kael_strlen(prompt), cy + max_lines, w->input, ATTR(WHITE, BLACK));
        put_cell(cx + kael_strlen(prompt) + w->input_pos, cy + max_lines, '_', ATTR(WHITE, BLACK));
    } else if (w->type == WIN_CALC) {
        fill_cells(cx, cy, cw, ch, ' ', ATTR(BLACK, WHITE));
        fill_cells(cx, cy, cw, 2, ' ', ATTR(BLACK, WHITE));
        draw_text(cx + 1, cy + 1, w->display[0] ? w->display : "0", ATTR(BLACK, WHITE));
        const char* btns[12] = {"7","8","9","/","4","5","6","*","1","2","3","-"};
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 4; c++) {
                int bx = cx + 1 + c * ((cw - 2) / 4);
                int by = cy + 2 + r * 2;
                fill_cells(bx, by, (cw-2)/4, 2, ' ', ATTR(BLACK, LIGHTGRAY));
                draw_text_centered(bx, by, (cw-2)/4, btns[r * 4 + c], ATTR(BLACK, LIGHTGRAY));
            }
        }
        const char* b2[5] = {"0",".","C","+","="};
        for (int c = 0; c < 5 && c < (cw-2)/4; c++) {
            int bx = cx + 1 + c * ((cw-2)/4);
            fill_cells(bx, cy + 8, (cw-2)/4, 2, ' ', ATTR(BLACK, LIGHTGRAY));
            draw_text_centered(bx, cy + 8, (cw-2)/4, b2[c], ATTR(BLACK, LIGHTGRAY));
        }
    } else if (w->type == WIN_FILES) {
        fill_cells(cx, cy, cw, ch, ' ', ATTR(BLACK, WHITE));
        draw_text(cx + 1, cy, "Root:", ATTR(DARKGRAY, WHITE));
        int fy = cy + 1;
        for (int i = 0; i < MAX_FS && fy < cy + ch; i++) {
            if (fs[i].in_use) {
                if (fs[i].is_dir) draw_text(cx + 2, fy, fs[i].name, ATTR(BLUE, WHITE));
                else draw_text(cx + 2, fy, fs[i].name, ATTR(BLACK, WHITE));
                fy++;
            }
        }
    } else if (w->type == WIN_ABOUT) {
        fill_cells(cx, cy, cw, ch, ' ', ATTR(BLACK, WHITE));
        draw_text_centered(cx, cy + 1, cw, "Kael OS v2.1", ATTR(BLACK, WHITE));
        draw_text_centered(cx, cy + 3, cw, "Classic Mac Style", ATTR(DARKGRAY, WHITE));
        draw_text_centered(cx, cy + 5, cw, "1987 Kael Computer", ATTR(DARKGRAY, WHITE));
    } else if (w->type == WIN_CLOCK) {
        fill_cells(cx, cy, cw, ch, ' ', ATTR(BLACK, WHITE));
        char buf[16]; int v, bi = 0;
        v = g_hour; if (v >= 10) buf[bi++] = '0' + v / 10; buf[bi++] = '0' + v % 10; buf[bi++] = ':';
        v = g_min; if (v >= 10) buf[bi++] = '0' + v / 10; buf[bi++] = '0' + v % 10; buf[bi++] = ':';
        v = g_sec; if (v >= 10) buf[bi++] = '0' + v / 10; buf[bi++] = '0' + v % 10; buf[bi] = 0;
        draw_text_centered(cx, cy + 1, cw, buf, ATTR(BLACK, WHITE));
        const char* mn[12] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
        char dbuf[24]; kael_strcpy(dbuf, mn[g_month - 1]); kael_strcat(dbuf, " ");
        char tmp[8]; kael_itoa(g_day, tmp); kael_strcat(dbuf, tmp);
        kael_strcat(dbuf, " "); kael_itoa(g_year, tmp); kael_strcat(dbuf, tmp);
        draw_text_centered(cx, cy + 3, cw, dbuf, ATTR(DARKGRAY, WHITE));
    } else if (w->type == WIN_CALENDAR) {
        fill_cells(cx, cy, cw, ch, ' ', ATTR(BLACK, WHITE));
        const char* mnames[12] = {"January","February","March","April","May","June","July","August","September","October","November","December"};
        char title[32]; kael_strcpy(title, mnames[g_month - 1]); kael_strcat(title, " "); kael_itoa(g_year, title + kael_strlen(title));
        draw_text_centered(cx, cy, cw, title, ATTR(BLACK, WHITE));
        draw_text(cx + 1, cy + 2, "Mo Tu We Th Fr Sa Su", ATTR(DARKGRAY, WHITE));
        int dim;
        if (g_month == 2) dim = (g_year % 4 == 0 && g_year % 100 != 0) || g_year % 400 == 0 ? 29 : 28;
        else if (g_month == 4 || g_month == 6 || g_month == 9 || g_month == 11) dim = 30;
        else dim = 31;
        int fd = (g_dow - g_day % 7 + 7) % 7; if (fd == 0) fd = 7;
        int day = 1;
        for (int row = 0; row < 5 && day <= dim; row++) {
            char line[32]; int pos = 0;
            for (int col = 0; col < 7; col++) {
                int cd = day - (fd - 1) + col;
                if (row == 0 && col < fd - 1) { line[pos++]=' ';line[pos++]=' ';line[pos++]=' '; }
                else if (cd >= 1 && cd <= dim) {
                    if (cd == g_day) { line[pos++]='['; if(cd<10)line[pos++]=' '; char db[4];kael_itoa(cd,db);for(int k=0;db[k];k++)line[pos++]=db[k];line[pos++]=']'; }
                    else { if(cd<10)line[pos++]=' '; char db[4];kael_itoa(cd,db);for(int k=0;db[k];k++)line[pos++]=db[k];line[pos++]=' '; }
                    line[pos++]=' ';
                } else { line[pos++]=' ';line[pos++]=' ';line[pos++]=' '; }
            }
            line[pos]=0; draw_text(cx + 1, cy + 3 + row, line, ATTR(BLACK, WHITE));
            day = (row == 0) ? fd + (7-fd) + 1 : day + 7;
        }
    }
}

/* Draw mouse cursor - arrow shape */
static void draw_cursor(int mx, int my) {
    if (mx < 0 || mx >= COLS || my < 0 || my >= ROWS) return;
    uint8_t btn = mouse_get_buttons();
    uint8_t attr = (btn & 1) ? ATTR(BLACK, LIGHTRED) : ATTR(BLACK, WHITE);
    /* Arrow cursor using block elements */
    if (my + 1 < ROWS) put_cell(mx, my, 0x1E, attr);
    if (my + 2 < ROWS) put_cell(mx, my + 1, 0x1E, attr);
    if (my + 1 < ROWS && mx + 1 < COLS) put_cell(mx + 1, my + 1, 0x10, attr);
}

/* Full redraw */
static void redraw(void) {
    fill_cells(0, 0, COLS, ROWS, ' ', MAC_DESKTOP);
    draw_menubar();
    for (int i = 0; i < MAX_WINDOWS; i++) if (wins[i].active) draw_window(i);
    /* Draw open dropdown */
    if (menu_bar_open == 1) draw_dropdown(0, apple_menu);
    else if (menu_bar_open == 2) {
        int fw = get_focused();
        if (fw >= 0 && wins[fw].type == WIN_TERMINAL) draw_dropdown(14, term_file_menu);
        else if (fw >= 0 && wins[fw].type == WIN_FILES) draw_dropdown(14, files_file_menu);
    } else if (menu_bar_open == 3) {
        int fw = get_focused();
        if (fw >= 0 && wins[fw].type == WIN_TERMINAL) draw_dropdown(20, term_edit_menu);
        else if (fw >= 0 && wins[fw].type == WIN_CALC) draw_dropdown(20, calc_edit_menu);
    } else if (menu_bar_open == 4) {
        int fw = get_focused();
        if (fw >= 0 && wins[fw].type == WIN_TERMINAL) draw_dropdown(26, term_shell_menu);
    } else if (menu_bar_open == 5) {
        int fw = get_focused();
        if (fw >= 0 && wins[fw].type == WIN_FILES) draw_dropdown(20, files_view_menu);
    }
    draw_cursor(mouse_get_x(), mouse_get_y());
    present();
}

/* Terminal command processing */
static void term_add_line(window_t* w, const char* text) {
    if (w->line_count >= 24) {
        for (int i = 0; i < 23; i++) for (int j = 0; j < 72; j++) w->lines[i][j] = w->lines[i+1][j];
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
        term_add_line(w, "Commands: ls, cd, cat, echo, clear");
        term_add_line(w, "  ver, fetch, date, cal, halt");
    } else if (kael_strcmp(cmd, "ls") == 0) {
        int found = 0;
        for (int i = 0; i < MAX_FS; i++) if (fs[i].in_use) { found = 1; term_add_line(w, fs[i].name); }
        if (!found) term_add_line(w, "(empty)");
    } else if (starts_with(cmd, "cat ")) {
        fs_entry_t* e = fs_find(cmd + 4);
        if (e && !e->is_dir) term_add_line(w, e->content);
        else term_add_line(w, "Not found.");
    } else if (starts_with(cmd, "echo ")) { term_add_line(w, cmd + 5); }
    else if (kael_strcmp(cmd, "clear") == 0) { w->line_count = 0; }
    else if (kael_strcmp(cmd, "ver") == 0) { term_add_line(w, "Kael OS v2.1 - Classic Mac Style"); }
    else if (kael_strcmp(cmd, "fetch") == 0) {
        term_add_line(w, "  -------------------------");
        term_add_line(w, "  OS:         Kael OS v2.1");
        term_add_line(w, "  Kernel:     Aether 32-bit x86");
        term_add_line(w, "  Bootloader: KaelBoot");
        term_add_line(w, "  Shell:      KaelTerm");
    }
    else if (kael_strcmp(cmd, "date") == 0) {
        char dbuf[40]; char tmp[8];
        kael_itoa(g_month, tmp); kael_strcpy(dbuf, tmp); kael_strcat(dbuf, "/");
        kael_itoa(g_day, tmp); kael_strcat(dbuf, tmp); kael_strcat(dbuf, "/");
        kael_itoa(g_year, tmp); kael_strcat(dbuf, tmp);
        term_add_line(w, dbuf);
    }
    else if (kael_strcmp(cmd, "cal") == 0) {
        term_add_line(w, "Month view - open Calendar app");
    }
    else if (kael_strcmp(cmd, "halt") == 0) {
        fill_cells(0, 0, COLS, ROWS, ' ', ATTR(WHITE, BLACK));
        draw_text_centered(0, 12, COLS, "System halted.", ATTR(WHITE, BLACK));
        present();
        while (1) asm volatile("hlt");
    }
    else if (cmd[0] != 0) { term_add_line(w, "Unknown command."); }
}
static void term_key(window_t* w, char c) {
    if (c == '\n' || c == '\r') { w->input[w->input_pos] = 0; term_process(w, w->input); w->input_pos = 0; w->input[0] = 0; }
    else if (c == 8) { if (w->input_pos > 0) { w->input_pos--; w->input[w->input_pos] = 0; } }
    else if (c >= 32 && c < 127) { if (w->input_pos < 60) { w->input[w->input_pos++] = c; w->input[w->input_pos] = 0; } }
}

/* Calculator */
static void calc_do_op(window_t* w) {
    int r = 0;
    if (w->op == '+') r = w->pending + kael_atoi(w->display);
    else if (w->op == '-') r = w->pending - kael_atoi(w->display);
    else if (w->op == '*') r = w->pending * kael_atoi(w->display);
    else if (w->op == '/') { int d = kael_atoi(w->display); if (d == 0) { kael_strcpy(w->display, "Err"); w->error = 1; return; } r = w->pending / d; }
    w->acc = r; kael_itoa(r, w->display); w->op = 0; w->new_num = 1;
}
static void calc_digit(window_t* w, char d) {
    if (w->error) { w->error = 0; w->display[0] = 0; }
    if (w->new_num) { w->display[0] = 0; w->new_num = 0; }
    int len = kael_strlen(w->display);
    if (len < 20) { w->display[len] = d; w->display[len + 1] = 0; }
}
static void calc_button(window_t* w, const char* label) {
    if (w->error && kael_strcmp(label, "C") != 0) return;
    if (kael_strcmp(label, "C") == 0) { w->display[0] = 0; w->acc = 0; w->pending = 0; w->op = 0; w->new_num = 1; w->error = 0; }
    else if (kael_strcmp(label, "+") == 0 || kael_strcmp(label, "-") == 0 || kael_strcmp(label, "*") == 0 || kael_strcmp(label, "/") == 0) {
        if (w->op && !w->new_num) calc_do_op(w);
        w->pending = kael_atoi(w->display); w->op = label[0]; w->new_num = 1;
    }
    else if (kael_strcmp(label, "=") == 0) { calc_do_op(w); }
    else if (kael_strcmp(label, ".") == 0) { calc_digit(w, '.'); }
    else { calc_digit(w, label[0]); }
}
static void calc_click(window_t* w, int mx, int my) {
    int x = w->x + 1, y = w->y + 2, cw = w->w - 2;
    const char* btns[12] = {"7","8","9","/","4","5","6","*","1","2","3","-"};
    for (int r = 0; r < 3; r++) for (int c = 0; c < 4; c++) {
        int bx = x + 1 + c * ((cw-2)/4), by = y + r * 2;
        if (mx >= bx && mx < bx + (cw-2)/4 && my >= by && my < by + 2) { calc_button(w, btns[r*4+c]); return; }
    }
    const char* b2[5] = {"0",".","C","+","="};
    for (int c = 0; c < 5 && c < (cw-2)/4; c++) {
        int bx = x + 1 + c * ((cw-2)/4), by = y + 6;
        if (mx >= bx && mx < bx + (cw-2)/4 && my >= by && my < by + 2) { calc_button(w, b2[c]); return; }
    }
}

/* Hit test */
static int win_hit(int idx, int mx, int my) {
    window_t* w = &wins[idx];
    if (mx < w->x || mx >= w->x + w->w || my < w->y || my >= w->y + w->h) return 0;
    if (my == w->y + 1 && mx <= w->x + 3) return 3; /* close */
    if (my == w->y + 1) return 2; /* title */
    return 1;
}

/* Menu bar hit test */
static int menubar_hit(int mx, int my) {
    if (my != 0) return 0;
    /* Apple icon */
    if (mx == 0) return 1;
    int fw = get_focused();
    if (fw < 0) return 0;
    int cx = 2 + kael_strlen(wins[fw].title) + 2;
    if (has_file_menu(wins[fw].type)) {
        if (mx >= cx && mx < cx + 4) return 2;
        cx += 5;
    }
    if (has_edit_menu(wins[fw].type)) {
        if (mx >= cx && mx < cx + 4) return 3;
        cx += 5;
    }
    if (has_shell_menu(wins[fw].type)) {
        if (mx >= cx && mx < cx + 5) return 4;
        cx += 6;
    }
    if (has_view_menu(wins[fw].type)) {
        if (mx >= cx && mx < cx + 4) return 5;
    }
    return 0;
}

void desktop_run(void) {
    vga_disable_blink();
    fs_init();
    rtc_init();
    fill_cells(0, 0, COLS, ROWS, ' ', MAC_DESKTOP);
    win_create(WIN_TERMINAL);
    redraw();

    while (1) {
        char c = kbd_getchar();
        int mx = mouse_get_x();
        int my = mouse_get_y();
        uint8_t btns = mouse_get_buttons();
        int moved = (mx != last_mx || my != last_my);
        int pressed = (btns & 1) && !(last_buttons & 1);
        int need_redraw = 0;

        /* Read CMOS RTC */
        g_sec = bcd2bin(cmos_read(0x00));
        g_min = bcd2bin(cmos_read(0x02));
        uint8_t h = cmos_read(0x04); g_hour = bcd2bin(h);
        if (!(cmos_read(0x0B) & 0x02)) { int pm = (h & 0x80) != 0; g_hour = g_hour & 0x7F; if (pm && g_hour != 12) g_hour += 12; if (!pm && g_hour == 12) g_hour = 0; }
        g_dow = bcd2bin(cmos_read(0x06)); g_day = bcd2bin(cmos_read(0x07));
        g_month = bcd2bin(cmos_read(0x08)); g_year = 2000 + bcd2bin(cmos_read(0x09));

        static int last_sec = -1;
        if (g_sec != last_sec || need_redraw || moved || btns != last_buttons) { last_sec = g_sec; need_redraw = 1; }

        /* Keyboard */
        if (c) {
            int fw = get_focused();
            if (c == '\t') {
                int found = 0;
                for (int i = 0; i < MAX_WINDOWS; i++) if (wins[i].active && !wins[i].focused) { wins[i].focused = 1; found = 1; for (int j = 0; j < MAX_WINDOWS; j++) if (j != i) wins[j].focused = 0; break; }
                if (!found) for (int i = 0; i < MAX_WINDOWS; i++) if (wins[i].active) { wins[i].focused = 1; for (int j = 0; j < MAX_WINDOWS; j++) if (j != i) wins[j].focused = 0; break; }
                need_redraw = 1;
            } else if (c == 27) { menu_bar_open = 0; need_redraw = 1; }
            else if (fw >= 0 && wins[fw].type == WIN_TERMINAL) { term_key(&wins[fw], c); need_redraw = 1; }
            else if (fw >= 0 && c == 'q') { win_close(fw); need_redraw = 1; }
        }

        /* Mouse */
        if (pressed) {
            if (menu_bar_open) {
                int item = my - 1;
                if (my == 0) {
                    int hb = menubar_hit(mx, my);
                    if (hb && hb == menu_bar_open) menu_bar_open = 0;
                    else if (hb) { menu_bar_open = hb; need_redraw = 1; }
                    else { menu_bar_open = 0; need_redraw = 1; }
                } else {
                    /* Menu item selected */
                    int fw = get_focused();
                    if (menu_bar_open == 1) {
                        /* Apple menu */
                        if (item == 0) win_create(WIN_ABOUT);
                        else if (item == 1) { /* Settings */ }
                        else if (item == 3) { /* Restart */ }
                        else if (item == 4) { fill_cells(0,0,COLS,ROWS,' ',ATTR(BLACK,BLACK)); draw_text_centered(0,12,COLS,"Goodbye!",ATTR(WHITE,BLACK)); present(); while(1) asm volatile("hlt"); }
                    } else if (menu_bar_open == 2 && fw >= 0) {
                        if (item == 0) win_create(wins[fw].type);
                        else if (item == 2) win_close(fw);
                    } else if (menu_bar_open == 3 && fw >= 0) {
                        if (item == 0) { wins[fw].line_count = 0; }
                    } else if (menu_bar_open == 4 && fw >= 0) {
                        if (item == 0) win_close(fw);
                    }
                    menu_bar_open = 0; need_redraw = 1;
                }
            } else {
                /* Check menu bar */
                int hb = menubar_hit(mx, my);
                if (hb) { menu_bar_open = hb; need_redraw = 1; }
                else {
                    /* Check windows */
                    int hit = -1, hit_type = 0;
                    for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
                        if (!wins[i].active) continue;
                        int ht = win_hit(i, mx, my);
                        if (ht) { hit = i; hit_type = ht; break; }
                    }
                    if (hit >= 0) {
                        if (hit_type == 3) { win_close(hit); need_redraw = 1; }
                        else {
                            for (int i = 0; i < MAX_WINDOWS; i++) wins[i].focused = 0;
                            wins[hit].focused = 1;
                            if (hit_type == 2) { drag_win = hit; wins[hit].drag_x = mx - wins[hit].x; wins[hit].drag_y = my - wins[hit].y; }
                            if (wins[hit].type == WIN_CALC) calc_click(&wins[hit], mx, my);
                            need_redraw = 1;
                        }
                    }
                }
            }
        }

        /* Drag */
        if (drag_win >= 0 && (btns & 1)) {
            window_t* w = &wins[drag_win];
            w->x = mx - w->drag_x; w->y = my - w->drag_y;
            if (w->x < 0) w->x = 0; if (w->y < 1) w->y = 1;
            if (w->x + w->w > COLS) w->x = COLS - w->w;
            if (w->y + w->h > ROWS) w->y = ROWS - w->h;
            need_redraw = 1;
        }
        if (!(btns & 1) && drag_win >= 0) { drag_win = -1; need_redraw = 1; }

        redraw();
        last_mx = mx; last_my = my; last_buttons = btns;
        for (volatile int i = 0; i < 20000; i++);
    }
}
