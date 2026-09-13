#include "types.h"

extern void vga_clear(uint8_t color);
extern void vga_set_palette(uint8_t idx, uint8_t r, uint8_t g, uint8_t b);
extern void vga_fill_rect(int x, int y, int w, int h, uint8_t color);
extern void vga_draw_rect(int x, int y, int w, int h, uint8_t color);
extern void vga_wait_vsync(void);
extern void font_draw_string(int x, int y, const char* s, uint8_t color);
extern void font_draw_string_bg(int x, int y, const char* s, uint8_t fg, uint8_t bg);
extern void font_draw_char_bg(int x, int y, char c, uint8_t fg, uint8_t bg);
extern char kbd_getchar(void);

extern void program_cube(void);
extern void program_notepad(void);
extern void program_fm(void);
extern void program_calc(void);

static int kael_strcmp(const char* a, const char* b) {
    while (*a && *b) { if (*a != *b) return *a - *b; a++; b++; }
    return *a - *b;
}

static int kael_strlen(const char* s) {
    int i = 0;
    while (s[i]) i++;
    return i;
}

static void kael_strcpy(char* dst, const char* src) {
    while (*src) *dst++ = *src++;
    *dst = '\0';
}

static void kael_strcat(char* dst, const char* src) {
    while (*dst) dst++;
    while (*src) *dst++ = *src++;
    *dst = '\0';
}

static int kael_strncmp(const char* a, const char* b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return a[i] - b[i];
        if (a[i] == '\0') return 0;
    }
    return 0;
}

static void kael_itoa(int val, char* buf) {
    if (val < 0) { *buf++ = '-'; val = -val; }
    if (val == 0) { *buf++ = '0'; *buf = '\0'; return; }
    char tmp[12];
    int i = 0;
    while (val > 0) { tmp[i++] = '0' + (val % 10); val /= 10; }
    for (int j = 0; j < i; j++) buf[j] = tmp[i - 1 - j];
    buf[i] = '\0';
}

#define MAX_LINES 24
#define LINE_LEN 40
#define MAX_INPUT 60

static char lines[MAX_LINES][LINE_LEN + 1];
static int line_count = 0;
static int scroll_offset = 0;
static char cmd_buf[MAX_INPUT + 1];
static int cmd_pos = 0;

#define MAX_PATH 64
static char cwd[MAX_PATH] = "/";

static void add_line(const char* text) {
    if (line_count >= MAX_LINES) {
        for (int i = 0; i < MAX_LINES - 1; i++)
            for (int j = 0; j <= LINE_LEN; j++) lines[i][j] = lines[i + 1][j];
        line_count = MAX_LINES - 1;
    }
    int i = 0;
    while (text[i] && i < LINE_LEN) { lines[line_count][i] = text[i]; i++; }
    lines[line_count][i] = '\0';
    line_count++;
    scroll_offset = (line_count > 19) ? line_count - 19 : 0;
}

static void draw_screen(void) {
    int y = 0;
    int end = scroll_offset + 19;
    if (end > line_count) end = line_count;
    for (int i = scroll_offset; i < end; i++) {
        vga_fill_rect(0, y, 320, 10, 0);
        font_draw_string_bg(0, y, lines[i], 15, 0);
        y += 10;
    }
    vga_fill_rect(0, y, 320, 200 - y, 0);
    char prompt[80];
    kael_strcpy(prompt, cwd);
    kael_strcat(prompt, "> ");
    font_draw_string_bg(0, 190, prompt, 10, 0);
    int px = kael_strlen(prompt) * 8;
    for (int i = 0; i < cmd_pos; i++) {
        char buf[2] = { cmd_buf[i], 0 };
        font_draw_string_bg(px + i * 8, 190, buf, 15, 0);
    }
    char cursor[2] = { '_', 0 };
    font_draw_string_bg(px + cmd_pos * 8, 190, cursor, 15, 0);
}

#define MAX_FS_ENTRIES 32
#define MAX_NAME 16

typedef enum { FS_FILE, FS_DIR } fs_type_t;

typedef struct {
    char name[MAX_NAME];
    fs_type_t type;
    char parent[MAX_PATH];
    char content[256];
    int in_use;
} fs_entry_t;

static fs_entry_t fs[MAX_FS_ENTRIES];
static int fs_count = 0;

static void fs_init(void) {
    for (int i = 0; i < MAX_FS_ENTRIES; i++) fs[i].in_use = 0;
    fs_count = 0;

    kael_strcpy(fs[0].name, "/");
    fs[0].type = FS_DIR;
    kael_strcpy(fs[0].parent, "");
    fs[0].in_use = 1;
    fs_count = 1;

    kael_strcpy(fs[1].name, "readme.txt");
    fs[1].type = FS_FILE;
    kael_strcpy(fs[1].parent, "/");
    kael_strcpy(fs[1].content, "Welcome to Kael OS! A lightweight DOS-like system.");
    fs[1].in_use = 1;
    fs_count = 2;

    kael_strcpy(fs[2].name, "docs");
    fs[2].type = FS_DIR;
    kael_strcpy(fs[2].parent, "/");
    fs[2].in_use = 1;
    fs_count = 3;

    kael_strcpy(fs[3].name, "programs");
    fs[3].type = FS_DIR;
    kael_strcpy(fs[3].parent, "/");
    fs[3].in_use = 1;
    fs_count = 4;

    kael_strcpy(fs[4].name, "hello.txt");
    fs[4].type = FS_FILE;
    kael_strcpy(fs[4].parent, "/docs/");
    kael_strcpy(fs[4].content, "Kael OS documentation.");
    fs[4].in_use = 1;
    fs_count = 5;

    kael_strcpy(fs[5].name, "notes.txt");
    fs[5].type = FS_FILE;
    kael_strcpy(fs[5].parent, "/docs/");
    kael_strcpy(fs[5].content, "TODO: add more features.");
    fs[5].in_use = 1;
    fs_count = 6;

    kael_strcpy(fs[6].name, "system");
    fs[6].type = FS_DIR;
    kael_strcpy(fs[6].parent, "/");
    fs[6].in_use = 1;
    fs_count = 7;

    kael_strcpy(fs[7].name, "version.txt");
    fs[7].type = FS_FILE;
    kael_strcpy(fs[7].parent, "/system/");
    kael_strcpy(fs[7].content, "Kael OS v1.0\nKernel: Aether\nBoot: Prometheus");
    fs[7].in_use = 1;
    fs_count = 8;
}

static fs_entry_t* fs_find(const char* name, const char* parent) {
    for (int i = 0; i < MAX_FS_ENTRIES; i++) {
        if (fs[i].in_use && kael_strcmp(fs[i].name, name) == 0 && kael_strcmp(fs[i].parent, parent) == 0)
            return &fs[i];
    }
    return 0;
}

static int fs_add(const char* name, fs_type_t type, const char* parent) {
    for (int i = 0; i < MAX_FS_ENTRIES; i++) {
        if (!fs[i].in_use) {
            kael_strcpy(fs[i].name, name);
            fs[i].type = type;
            kael_strcpy(fs[i].parent, parent);
            fs[i].content[0] = '\0';
            fs[i].in_use = 1;
            return 1;
        }
    }
    return 0;
}

static int fs_remove(const char* name, const char* parent) {
    for (int i = 0; i < MAX_FS_ENTRIES; i++) {
        if (fs[i].in_use && kael_strcmp(fs[i].name, name) == 0 && kael_strcmp(fs[i].parent, parent) == 0) {
            fs[i].in_use = 0;
            return 1;
        }
    }
    return 0;
}

static int fs_count_entries(const char* parent) {
    int c = 0;
    for (int i = 0; i < MAX_FS_ENTRIES; i++)
        if (fs[i].in_use && kael_strcmp(fs[i].parent, parent) == 0) c++;
    return c;
}

static void cmd_help(void) {
    add_line("=== System ===");
    add_line(" help      - show this");
    add_line(" ver       - version info");
    add_line(" fetch     - system info");
    add_line(" whoami    - current user");
    add_line(" hostname  - system name");
    add_line(" uptime    - uptime (boot)");
    add_line(" halt      - shutdown");
    add_line(" clear     - clear screen");
    add_line("=== Files ===");
    add_line(" ls        - list files");
    add_line(" cd <dir>  - change dir");
    add_line(" mkdir <n> - make directory");
    add_line(" touch <n> - create file");
    add_line(" rm <n>    - remove file");
    add_line(" cat <f>   - read file");
    add_line(" write <f> <t> - write file");
    add_line(" cp <f> <d>    - copy file");
    add_line(" mv <f> <d>    - move file");
    add_line(" size <f>  - file size");
    add_line("=== Other ===");
    add_line(" echo <t>  - print text");
    add_line(" date      - current time");
    add_line(" pwd       - current dir");
    add_line(" tree      - dir tree");
    add_line(" calc      - calculator");
    add_line(" cube      - spinner");
    add_line(" notepad   - text editor");
    add_line(" fm        - file manager");
}

static void cmd_ls(void) {
    char buf[64];
    int found = 0;
    int dirs = 0, files = 0;
    for (int i = 0; i < MAX_FS_ENTRIES; i++) {
        if (fs[i].in_use && kael_strcmp(fs[i].parent, cwd) == 0) {
            found = 1;
            if (fs[i].type == FS_DIR) {
                kael_strcpy(buf, "[");
                kael_strcat(buf, fs[i].name);
                kael_strcat(buf, "]");
                dirs++;
            } else {
                kael_strcpy(buf, " ");
                kael_strcat(buf, fs[i].name);
                files++;
            }
            add_line(buf);
        }
    }
    if (!found) add_line("  (empty)");
}

static void cmd_cd(const char* arg) {
    if (arg[0] == '\0') {
        kael_strcpy(cwd, "/");
        return;
    }
    if (kael_strcmp(arg, "..") == 0) {
        int len = kael_strlen(cwd);
        if (len <= 1) return;
        cwd[len - 1] = '\0';
        while (len > 2 && cwd[len - 1] != '/') { cwd[len - 1] = '\0'; len--; }
        if (len > 1) cwd[len - 1] = '\0';
        return;
    }
    fs_entry_t* e = fs_find(arg, cwd);
    if (e && e->type == FS_DIR) {
        kael_strcpy(cwd, arg);
        kael_strcat(cwd, "/");
    } else { add_line("Not a directory."); }
}

static void cmd_mkdir(const char* arg) {
    if (arg[0] == '\0') { add_line("Usage: mkdir <name>"); return; }
    if (fs_find(arg, cwd)) { add_line("Already exists."); return; }
    if (fs_add(arg, FS_DIR, cwd)) add_line("Created.");
    else add_line(" filesystem full.");
}

static void cmd_touch(const char* arg) {
    if (arg[0] == '\0') { add_line("Usage: touch <name>"); return; }
    if (fs_find(arg, cwd)) { add_line("Already exists."); return; }
    if (fs_add(arg, FS_FILE, cwd)) add_line("Created.");
    else add_line(" filesystem full.");
}

static void cmd_rm(const char* arg) {
    if (arg[0] == '\0') { add_line("Usage: rm <name>"); return; }
    if (fs_remove(arg, cwd)) add_line("Removed.");
    else add_line("Not found.");
}

static void cmd_cat(const char* arg) {
    if (arg[0] == '\0') { add_line("Usage: cat <file>"); return; }
    fs_entry_t* e = fs_find(arg, cwd);
    if (!e) { add_line("Not found."); return; }
    if (e->type == FS_DIR) { add_line("Is a directory."); return; }
    if (e->content[0] == '\0') add_line("  (empty file)");
    else add_line(e->content);
}

static void cmd_write(const char* args) {
    const char* space = 0;
    const char* p = args;
    while (*p) { if (*p == ' ') { space = p; break; } p++; }
    if (!space) { add_line("Usage: write <file> <text>"); return; }
    char name[MAX_NAME];
    int ni = 0;
    while (args < space && ni < MAX_NAME - 1) name[ni++] = *args++;
    name[ni] = '\0';
    fs_entry_t* e = fs_find(name, cwd);
    if (!e || e->type == FS_DIR) { add_line("File not found."); return; }
    space++;
    kael_strcpy(e->content, space);
    add_line("Written.");
}

static void cmd_echo(const char* arg) {
    add_line(arg);
}

static int starts_with(const char* s, const char* prefix) {
    while (*prefix) { if (*s != *prefix) return 0; s++; prefix++; }
    return 1;
}

static void process_command(void) {
    add_line(cmd_buf);

    if (kael_strcmp(cmd_buf, "help") == 0) {
        cmd_help();
    } else if (kael_strcmp(cmd_buf, "clear") == 0) {
        line_count = 0;
        scroll_offset = 0;
    } else if (kael_strcmp(cmd_buf, "ver") == 0) {
        add_line("Kael OS v1.0");
        add_line("Kernel: Aether 32-bit x86");
        add_line("Boot: Prometheus LBA MBR");
        add_line("Display: VGA 320x200");
    } else if (kael_strcmp(cmd_buf, "fetch") == 0) {
        add_line("     Kael OS v1.0");
        add_line("Kernel: Aether 32-bit");
        add_line("Boot:   Prometheus");
        add_line("Display: VGA 320x200");
        add_line("Shell:  KaelTerm");
    } else if (kael_strcmp(cmd_buf, "whoami") == 0) {
        add_line("root");
    } else if (kael_strcmp(cmd_buf, "hostname") == 0) {
        add_line("kael");
    } else if (kael_strcmp(cmd_buf, "uptime") == 0) {
        add_line("System is running.");
    } else if (kael_strcmp(cmd_buf, "pwd") == 0) {
        add_line(cwd);
    } else if (kael_strcmp(cmd_buf, "date") == 0) {
        add_line("2026-08-26 (estimated)");
    } else if (kael_strcmp(cmd_buf, "ls") == 0) {
        cmd_ls();
    } else if (kael_strcmp(cmd_buf, "tree") == 0) {
        add_line("/");
        for (int i = 0; i < MAX_FS_ENTRIES; i++) {
            if (fs[i].in_use && fs[i].type == FS_DIR && kael_strcmp(fs[i].parent, "/") == 0 && kael_strcmp(fs[i].name, "/") != 0) {
                char buf[40];
                kael_strcpy(buf, "|-- ");
                kael_strcat(buf, fs[i].name);
                kael_strcat(buf, "/");
                add_line(buf);
                for (int j = 0; j < MAX_FS_ENTRIES; j++) {
                    if (fs[j].in_use && kael_strcmp(fs[j].parent, fs[i].name) == 0) {
                        char sub[40];
                        kael_strcpy(sub, "|   |-- ");
                        kael_strcat(sub, fs[j].name);
                        add_line(sub);
                    }
                }
            }
        }
    } else if (starts_with(cmd_buf, "cd ")) {
        cmd_cd(cmd_buf + 3);
    } else if (kael_strcmp(cmd_buf, "cd") == 0) {
        cmd_cd("");
    } else if (starts_with(cmd_buf, "mkdir ")) {
        cmd_mkdir(cmd_buf + 6);
    } else if (starts_with(cmd_buf, "touch ")) {
        cmd_touch(cmd_buf + 6);
    } else if (starts_with(cmd_buf, "rm ")) {
        cmd_rm(cmd_buf + 3);
    } else if (starts_with(cmd_buf, "cat ")) {
        cmd_cat(cmd_buf + 4);
    } else if (starts_with(cmd_buf, "write ")) {
        cmd_write(cmd_buf + 6);
    } else if (starts_with(cmd_buf, "cp ")) {
        const char* args = cmd_buf + 3;
        const char* space = 0;
        const char* p = args;
        while (*p) { if (*p == ' ') { space = p; break; } p++; }
        if (!space) { add_line("Usage: cp <source> <dest>"); }
        else {
            char name[MAX_NAME];
            int ni = 0;
            while (args < space && ni < MAX_NAME - 1) name[ni++] = *args++;
            name[ni] = '\0';
            space++;
            fs_entry_t* src = fs_find(name, cwd);
            if (!src || src->type == FS_DIR) add_line("Source not found.");
            else if (fs_find(space, cwd)) add_line("Dest exists.");
            else if (fs_add(space, FS_FILE, cwd)) {
                fs_entry_t* dst = fs_find(space, cwd);
                if (dst) kael_strcpy(dst->content, src->content);
                add_line("Copied.");
            } else add_line(" filesystem full.");
        }
    } else if (starts_with(cmd_buf, "mv ")) {
        const char* args = cmd_buf + 3;
        const char* space = 0;
        const char* p = args;
        while (*p) { if (*p == ' ') { space = p; break; } p++; }
        if (!space) { add_line("Usage: mv <source> <dest>"); }
        else {
            char name[MAX_NAME];
            int ni = 0;
            while (args < space && ni < MAX_NAME - 1) name[ni++] = *args++;
            name[ni] = '\0';
            space++;
            fs_entry_t* src = fs_find(name, cwd);
            if (!src) add_line("Source not found.");
            else if (fs_find(space, cwd)) add_line("Dest exists.");
            else { kael_strcpy(src->name, space); add_line("Moved."); }
        }
    } else if (starts_with(cmd_buf, "size ")) {
        if (cmd_buf[5] == '\0') add_line("Usage: size <file>");
        else {
            fs_entry_t* e = fs_find(cmd_buf + 5, cwd);
            if (!e) add_line("Not found.");
            else if (e->type == FS_DIR) add_line("Is a directory.");
            else { char sz[16]; kael_itoa(kael_strlen(e->content), sz); kael_strcat(sz, " bytes"); add_line(sz); }
        }
    } else if (starts_with(cmd_buf, "echo ")) {
        cmd_echo(cmd_buf + 5);
    } else if (kael_strcmp(cmd_buf, "echo") == 0) {
        add_line("");
    } else if (kael_strcmp(cmd_buf, "cube") == 0) {
        program_cube();
        line_count = 0; scroll_offset = 0;
        add_line("Returned from spinner.");
    } else if (kael_strcmp(cmd_buf, "calc") == 0) {
        program_calc();
        line_count = 0; scroll_offset = 0;
        add_line("Returned from calculator.");
    } else if (kael_strcmp(cmd_buf, "notepad") == 0) {
        program_notepad();
        line_count = 0; scroll_offset = 0;
        add_line("Returned from notepad.");
    } else if (kael_strcmp(cmd_buf, "fm") == 0) {
        program_fm();
        line_count = 0; scroll_offset = 0;
        add_line("Returned from file manager.");
    } else if (kael_strcmp(cmd_buf, "halt") == 0) {
        vga_clear(0);
        font_draw_string(80, 95, "System halted.", 15);
        while (1) asm volatile("hlt");
    } else {
        add_line("Unknown command. Type 'help'.");
    }
}

void desktop_run(void) {
    vga_set_palette(0, 0, 0, 0);
    vga_set_palette(1, 0, 0, 42);
    vga_set_palette(7, 42, 42, 42);
    vga_set_palette(8, 21, 21, 21);
    vga_set_palette(9, 21, 21, 63);
    vga_set_palette(10, 21, 63, 21);
    vga_set_palette(12, 63, 21, 21);
    vga_set_palette(15, 63, 63, 63);
    vga_set_palette(4, 42, 0, 0);

    fs_init();

    add_line("Kael OS v1.0");
    add_line("Type 'help' for commands.");
    add_line("");

    draw_screen();

    while (1) {
        char c = kbd_getchar();
        if (!c) continue;

        if (c == '\t') {
            draw_screen();
            continue;
        }

        if (c == '\n' || c == '\r') {
            cmd_buf[cmd_pos] = '\0';
            if (cmd_pos > 0) process_command();
            cmd_pos = 0;
            cmd_buf[0] = '\0';
        } else if (c == 8) {
            if (cmd_pos > 0) cmd_pos--;
        } else if (c == 0x11 || c == 0x12) {
        } else {
            if (cmd_pos < MAX_INPUT) cmd_buf[cmd_pos++] = c;
        }
        draw_screen();
    }
}
