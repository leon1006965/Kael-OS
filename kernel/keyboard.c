#include "types.h"

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t r; asm volatile("inb %1,%0" : "=a"(r) : "Nd"(port)); return r;
}

#define KBD_BUFFER_SIZE 128
static char kbd_buffer[KBD_BUFFER_SIZE];
static int kbd_head = 0;
static int kbd_tail = 0;
static int shift_held = 0;
static int tab_held = 0;
static int e0_prefix = 0;

static const char scancode_table[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', 8, '\t',
    'q','w','e','r','t','y','u','i','o','p','[',']', 13, 0,
    'a','s','d','f','g','h','j','k','l',';','\'','`', 0,'\\',
    'z','x','c','v','b','n','m',',','.','/', 0, '*', 0, ' ',
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0
};

static const char scancode_shift_table[128] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+', 8, '\t',
    'Q','W','E','R','T','Y','U','I','O','P','{','}', 13, 0,
    'A','S','D','F','G','H','J','K','L',':','"','~', 0,'|',
    'Z','X','C','V','B','N','M','<','>','?', 0, '*', 0, ' ',
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0
};

static void kbd_enqueue(char c) {
    int next = (kbd_head + 1) % KBD_BUFFER_SIZE;
    if (next != kbd_tail) {
        kbd_buffer[kbd_head] = c;
        kbd_head = next;
    }
}

void keyboard_init(void) {
    for (int i = 0; i < 100000 && (inb(0x64) & 1); i++) inb(0x60);
    shift_held = 0;
    tab_held = 0;
    e0_prefix = 0;
}

void keyboard_handler(void) {
    uint8_t st = inb(0x64);
    if ((st & 0x21) != 0x01) return;

    uint8_t sc = inb(0x60);

    if (sc == 0xE0) { e0_prefix = 1; return; }
    if (e0_prefix) {
        e0_prefix = 0;
        if (sc == 0x48) { kbd_enqueue(0x11); return; }
        if (sc == 0x50) { kbd_enqueue(0x12); return; }
        if (sc == 0x4B) { kbd_enqueue(0x13); return; }
        if (sc == 0x4D) { kbd_enqueue(0x14); return; }
        return;
    }

    if (sc == 0x2A || sc == 0x36) { shift_held = 1; return; }
    if (sc == 0xAA || sc == 0xB6) { shift_held = 0; return; }
    if (sc == 0x0F) { tab_held = 1; return; }
    if (sc == 0x8F) { tab_held = 0; return; }
    if (sc & 0x80) return;

    char c = shift_held ? scancode_shift_table[sc] : scancode_table[sc];
    if (c == 0) return;
    kbd_enqueue(c);
}

char kbd_getchar(void) {
    if (kbd_head == kbd_tail) return 0;
    char c = kbd_buffer[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUFFER_SIZE;
    return c;
}

int kbd_tab_held(void) {
    return tab_held;
}
