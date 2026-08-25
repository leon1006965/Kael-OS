#include "types.h"

extern void vga_fill_rect(int x, int y, int w, int h, uint8_t color);
extern void vga_draw_rect(int x, int y, int w, int h, uint8_t color);
extern void vga_clear(uint8_t color);
extern void font_draw_string(int x, int y, const char* s, uint8_t color);
extern void font_draw_string_bg(int x, int y, const char* s, uint8_t fg, uint8_t bg);
extern void font_draw_char_bg(int x, int y, char c, uint8_t fg, uint8_t bg);
extern char kbd_getchar(void);

#define MAX_FILES 32
#define MAX_NAME 24
#define MAX_CONTENT 256

static int kael_strcmp(const char* a, const char* b) {
    while (*a && *b) { if (*a != *b) return *a - *b; a++; b++; }
    return *a - *b;
}

static void kael_strcpy(char* d, const char* s) {
    while (*s) *d++ = *s++;
    *d = 0;
}

static int kael_strlen(const char* s) {
    int l = 0;
    while (*s++) l++;
    return l;
}

typedef struct {
    char name[MAX_NAME];
    int is_dir;
    char content[MAX_CONTENT];
    int content_len;
    int used;
    int parent;
} fs_entry_t;

static fs_entry_t fs[MAX_FILES];
static int fs_count = 0;
static int cur_dir = 0;
static int selected = 0;

static void fs_init(void) {
    for (int i = 0; i < MAX_FILES; i++) fs[i].used = 0;
    kael_strcpy(fs[0].name, "/");
    fs[0].is_dir = 1;
    fs[0].used = 1;
    fs[0].parent = -1;
    fs_count = 1;
    int f = fs_count++;
    kael_strcpy(fs[f].name, "readme.txt");
    fs[f].is_dir = 0;
    fs[f].used = 1;
    fs[f].parent = 0;
    kael_strcpy(fs[f].content, "Welcome to Kael OS!");
    fs[f].content_len = 20;
    int d = fs_count++;
    kael_strcpy(fs[d].name, "docs");
    fs[d].is_dir = 1;
    fs[d].used = 1;
    fs[d].parent = 0;
    int d2 = fs_count++;
    kael_strcpy(fs[d2].name, "programs");
    fs[d2].is_dir = 1;
    fs[d2].used = 1;
    fs[d2].parent = 0;
}

static int count_entries(void) {
    int c = 0;
    for (int i = 0; i < fs_count; i++)
        if (fs[i].used && fs[i].parent == cur_dir) c++;
    return c;
}

static void fm_draw(void) {
    vga_clear(1);
    vga_fill_rect(10, 5, 300, 180, 15);
    vga_fill_rect(11, 6, 298, 178, 0);
    vga_fill_rect(11, 6, 298, 10, 9);
    font_draw_string(14, 7, "File Manager", 15);
    vga_fill_rect(11, 176, 298, 8, 7);
    font_draw_string(14, 177, "Up/Dn=select Enter=open Tab=back", 15);
    int y = 18;
    int idx = 0;
    for (int i = 0; i < fs_count && y < 172; i++) {
        if (!fs[i].used || fs[i].parent != cur_dir) continue;
        uint8_t bg = (idx == selected) ? 9 : 0;
        if (fs[i].is_dir) {
            font_draw_string_bg(14, y, "[", 11, bg);
            font_draw_string_bg(22, y, fs[i].name, 11, bg);
        } else {
            font_draw_string_bg(22, y, fs[i].name, 15, bg);
        }
        y += 10;
        idx++;
    }
    if (idx == 0) font_draw_string_bg(14, y, "(empty)", 8, 0);
}

static void fm_show_file(int i) {
    vga_fill_rect(11, 16, 296, 160, 0);
    font_draw_string_bg(14, 18, fs[i].name, 14, 0);
    font_draw_string_bg(14, 28, "----------", 8, 0);
    int y = 38;
    int col = 0;
    for (int j = 0; j < fs[i].content_len && y < 172; j++) {
        char ch = fs[i].content[j];
        if (ch == '\n') {
            col = 0;
            y += 9;
        } else {
            font_draw_char_bg(14 + col * 8, y, ch, 15, 0);
            col++;
        }
    }
    font_draw_string_bg(14, 172, "Press any key to go back", 8, 0);
}

void program_fm(void) {
    fs_init();
    selected = 0;
    fm_draw();
    while (1) {
        char c = kbd_getchar();
        if (!c) continue;
        if (c == 0x11) {
            if (selected > 0) selected--;
            fm_draw();
        } else if (c == 0x12) {
            int max = count_entries() - 1;
            if (selected < max) selected++;
            fm_draw();
        } else if (c == '\n' || c == '\r') {
            int idx = 0;
            for (int i = 0; i < fs_count; i++) {
                if (!fs[i].used || fs[i].parent != cur_dir) continue;
                if (idx == selected) {
                    if (fs[i].is_dir) {
                        cur_dir = i;
                        selected = 0;
                    } else {
                        fm_show_file(i);
                        while (!kbd_getchar()) {}
                    }
                    break;
                }
                idx++;
            }
            fm_draw();
        } else if (c == '\t' || c == 8) {
            if (cur_dir != 0) {
                cur_dir = 0;
                selected = 0;
            } else {
                return;
            }
            fm_draw();
        }
    }
}
