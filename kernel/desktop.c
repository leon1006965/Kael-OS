#include "types.h"

extern void vga_text_clear(uint8_t color);
extern void vga_text_puts(const char* s, uint8_t color);
extern void vga_text_putchar(char c, uint8_t color);
extern void vga_text_set_pos(int x, int y);
extern uint8_t vga_text_make_color(uint8_t fg, uint8_t bg);
extern void vga_set_palette(uint8_t idx, uint8_t r, uint8_t g, uint8_t b);
extern char kbd_getchar(void);

extern int mouse_get_x(void);
extern int mouse_get_y(void);
extern uint8_t mouse_get_buttons(void);

extern void program_cube(void);
extern void program_notepad(void);
extern void program_fm(void);
extern void program_calc(void);

static int kael_strcmp(const char* a, const char* b) {
    while (*a && *b) { if (*a != *b) return *a - *b; a++; b++; }
    return *a - *b;
}

static int kael_strlen(const char* s) { int i = 0; while (s[i]) i++; return i; }

static void kael_strcpy(char* dst, const char* src) {
    while (*src) *dst++ = *src++;
    *dst = '\0';
}

static void kael_strcat(char* dst, const char* src) {
    while (*dst) dst++;
    while (*src) *dst++ = *src++;
    *dst = '\0';
}

static int starts_with(const char* s, const char* prefix) {
    while (*prefix) { if (*s != *prefix) return 0; s++; prefix++; }
    return 1;
}

static void kael_itoa(int v, char* b) {
    if (v < 0) { *b++ = '-'; v = -v; }
    if (v == 0) { *b++ = '0'; *b = '\0'; return; }
    char t[12]; int i = 0;
    while (v > 0) { t[i++] = '0' + (v % 10); v /= 10; }
    for (int j = 0; j < i; j++) b[j] = t[i - 1 - j];
    b[i] = '\0';
}

#define MAX_LINES 24
#define LINE_LEN 80
#define MAX_INPUT 80
#define MAX_FS_ENTRIES 32
#define MAX_NAME 16
#define MAX_PATH 64

static char lines[MAX_LINES][LINE_LEN + 1];
static int line_count = 0, scroll_offset = 0;
static char cmd_buf[MAX_INPUT + 1];
static int cmd_pos = 0;
static char cwd[MAX_PATH] = "/";

static void add_line(const char* text) {
    if (line_count >= MAX_LINES) {
        for (int i = 0; i < MAX_LINES - 1; i++)
            for (int j = 0; j <= LINE_LEN; j++) lines[i][j] = lines[i+1][j];
        line_count = MAX_LINES - 1;
    }
    int i = 0;
    while (text[i] && i < LINE_LEN) { lines[line_count][i] = text[i]; i++; }
    lines[line_count][i] = '\0';
    line_count++;
    if (line_count - scroll_offset > 24) scroll_offset = line_count - 24;
}

static void draw_screen(void) {
    vga_text_clear(0x07);
    int row = 0;
    int end = scroll_offset + 24;
    if (end > line_count) end = line_count;
    for (int i = scroll_offset; i < end; i++) {
        vga_text_set_pos(0, row);
        vga_text_puts(lines[i], 0x0F);
        row++;
    }
    char prompt[80];
    kael_strcpy(prompt, cwd);
    kael_strcat(prompt, "> ");
    vga_text_set_pos(0, 24);
    vga_text_puts(prompt, 0x0A);
    vga_text_puts(cmd_buf, 0x0F);
    vga_text_putchar('_', 0x07);
    vga_text_set_pos(mouse_get_x(), mouse_get_y());
    vga_text_putchar(0xDB, mouse_get_buttons() ? 0x4C : 0x1F);
}

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
    kael_strcpy(fs[0].name, "/"); fs[0].type = FS_DIR; kael_strcpy(fs[0].parent, ""); fs[0].in_use = 1; fs_count = 1;
    kael_strcpy(fs[1].name, "readme.txt"); fs[1].type = FS_FILE; kael_strcpy(fs[1].parent, "/");
    kael_strcpy(fs[1].content, "Welcome to Kael OS! A lightweight DOS-like system."); fs[1].in_use = 1; fs_count = 2;
    kael_strcpy(fs[2].name, "docs"); fs[2].type = FS_DIR; kael_strcpy(fs[2].parent, "/"); fs[2].in_use = 1; fs_count = 3;
    kael_strcpy(fs[3].name, "programs"); fs[3].type = FS_DIR; kael_strcpy(fs[3].parent, "/"); fs[3].in_use = 1; fs_count = 4;
    kael_strcpy(fs[4].name, "hello.txt"); fs[4].type = FS_FILE; kael_strcpy(fs[4].parent, "/docs/");
    kael_strcpy(fs[4].content, "Kael OS documentation."); fs[4].in_use = 1; fs_count = 5;
}

static fs_entry_t* fs_find(const char* name, const char* parent) {
    for (int i = 0; i < MAX_FS_ENTRIES; i++)
        if (fs[i].in_use && kael_strcmp(fs[i].name, name) == 0 && kael_strcmp(fs[i].parent, parent) == 0) return &fs[i];
    return 0;
}

static int fs_add(const char* name, fs_type_t type, const char* parent) {
    for (int i = 0; i < MAX_FS_ENTRIES; i++) {
        if (!fs[i].in_use) {
            kael_strcpy(fs[i].name, name); fs[i].type = type;
            kael_strcpy(fs[i].parent, parent); fs[i].content[0] = '\0';
            fs[i].in_use = 1; return 1;
        }
    }
    return 0;
}

static int fs_remove(const char* name, const char* parent) {
    for (int i = 0; i < MAX_FS_ENTRIES; i++)
        if (fs[i].in_use && kael_strcmp(fs[i].name, name) == 0 && kael_strcmp(fs[i].parent, parent) == 0)
            { fs[i].in_use = 0; return 1; }
    return 0;
}

static void cmd_help(void) {
    add_line("=== System ===");
    add_line("  help      - show this");
    add_line("  ver       - version info");
    add_line("  fetch     - system info");
    add_line("  halt      - shutdown");
    add_line("  clear     - clear screen");
    add_line("=== Files ===");
    add_line("  ls        - list files");
    add_line("  cd <dir>  - change dir");
    add_line("  mkdir <n> - make directory");
    add_line("  touch <n> - create file");
    add_line("  rm <n>    - remove file");
    add_line("  cat <f>   - read file");
    add_line("  write <f> <t> - write file");
    add_line("=== Other ===");
    add_line("  echo <t>  - print text");
    add_line("  calc      - calculator");
    add_line("  cube      - spinner");
    add_line("  notepad   - text editor");
    add_line("  fm        - file manager");
}

static void cmd_ls(void) {
    char buf[64]; int found = 0;
    for (int i = 0; i < MAX_FS_ENTRIES; i++) {
        if (fs[i].in_use && kael_strcmp(fs[i].parent, cwd) == 0) {
            found = 1;
            if (fs[i].type == FS_DIR) { kael_strcpy(buf, "["); kael_strcat(buf, fs[i].name); kael_strcat(buf, "]"); }
            else { kael_strcpy(buf, " "); kael_strcat(buf, fs[i].name); }
            add_line(buf);
        }
    }
    if (!found) add_line("  (empty)");
}

static void cmd_cd(const char* arg) {
    if (arg[0] == '\0') { kael_strcpy(cwd, "/"); return; }
    if (kael_strcmp(arg, "..") == 0) {
        int len = kael_strlen(cwd);
        if (len <= 1) return;
        cwd[len - 1] = '\0';
        while (len > 2 && cwd[len - 1] != '/') { cwd[len - 1] = '\0'; len--; }
        if (len > 1) cwd[len - 1] = '\0';
        return;
    }
    fs_entry_t* e = fs_find(arg, cwd);
    if (e && e->type == FS_DIR) { kael_strcpy(cwd, arg); kael_strcat(cwd, "/"); }
    else add_line("Not a directory.");
}

static void process_command(void) {
    add_line(cmd_buf);
    if (kael_strcmp(cmd_buf, "help") == 0) cmd_help();
    else if (kael_strcmp(cmd_buf, "clear") == 0) { line_count = 0; scroll_offset = 0; }
    else if (kael_strcmp(cmd_buf, "ver") == 0) {
        add_line("Kael OS v2.0 (GRUB)");
        add_line("Kernel: Aether 32-bit x86");
        add_line("Display: VGA text 80x25");
    } else if (kael_strcmp(cmd_buf, "fetch") == 0) {
        add_line("    Kael OS v2.0");
        add_line("Boot: GRUB Multiboot");
        add_line("Display: VGA text 80x25");
        add_line("Shell:   KaelTerm");
    } else if (kael_strcmp(cmd_buf, "ls") == 0) cmd_ls();
    else if (starts_with(cmd_buf, "cd ")) cmd_cd(cmd_buf + 3);
    else if (kael_strcmp(cmd_buf, "cd") == 0) cmd_cd("");
    else if (starts_with(cmd_buf, "mkdir ")) {
        if (cmd_buf[6] == '\0') add_line("Usage: mkdir <name>");
        else if (fs_find(cmd_buf + 6, cwd)) add_line("Already exists.");
        else if (fs_add(cmd_buf + 6, FS_DIR, cwd)) add_line("Created.");
        else add_line(" filesystem full.");
    } else if (starts_with(cmd_buf, "touch ")) {
        if (cmd_buf[6] == '\0') add_line("Usage: touch <name>");
        else if (fs_find(cmd_buf + 6, cwd)) add_line("Already exists.");
        else if (fs_add(cmd_buf + 6, FS_FILE, cwd)) add_line("Created.");
        else add_line(" filesystem full.");
    } else if (starts_with(cmd_buf, "rm ")) {
        if (cmd_buf[3] == '\0') add_line("Usage: rm <name>");
        else if (fs_remove(cmd_buf + 3, cwd)) add_line("Removed.");
        else add_line("Not found.");
    } else if (starts_with(cmd_buf, "cat ")) {
        if (cmd_buf[4] == '\0') add_line("Usage: cat <file>");
        else {
            fs_entry_t* e = fs_find(cmd_buf + 4, cwd);
            if (!e) add_line("Not found.");
            else if (e->type == FS_DIR) add_line("Is a directory.");
            else if (e->content[0] == '\0') add_line("  (empty)");
            else add_line(e->content);
        }
    } else if (starts_with(cmd_buf, "write ")) {
        const char* s = cmd_buf + 6; const char* sp = 0; const char* p = s;
        while (*p) { if (*p == ' ') { sp = p; break; } p++; }
        if (!sp) add_line("Usage: write <file> <text>");
        else { char nm[MAX_NAME]; int ni = 0; while (s < sp && ni < MAX_NAME-1) nm[ni++] = *s++; nm[ni] = '\0';
            fs_entry_t* e = fs_find(nm, cwd); if (!e || e->type == FS_DIR) add_line("File not found.");
            else { sp++; kael_strcpy(e->content, sp); add_line("Written."); } }
    } else if (starts_with(cmd_buf, "echo ")) { add_line(cmd_buf + 5); }
    else if (kael_strcmp(cmd_buf, "echo") == 0) { add_line(""); }
    else if (kael_strcmp(cmd_buf, "halt") == 0) { vga_text_clear(0); vga_text_set_pos(30, 12); vga_text_puts("System halted.", 0x07); while (1) asm volatile("hlt"); }
    else add_line("Unknown command. Type 'help'.");
}

void desktop_run(void) {
    fs_init();
    add_line("Kael OS v2.0 (GRUB)");
    add_line("Type 'help' for commands.");
    add_line("");
    draw_screen();
    while (1) {
        char c = kbd_getchar();
        if (!c) continue;
        if (c == '\t') { draw_screen(); continue; }
        if (c == '\n' || c == '\r') {
            cmd_buf[cmd_pos] = '\0';
            if (cmd_pos > 0) process_command();
            cmd_pos = 0; cmd_buf[0] = '\0';
        } else if (c == 8) { if (cmd_pos > 0) cmd_pos--; }
        else if (c == 0x11 || c == 0x12) { }
        else { if (cmd_pos < MAX_INPUT) cmd_buf[cmd_pos++] = c; }
        draw_screen();
    }
}
