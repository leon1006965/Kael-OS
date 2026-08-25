#include "types.h"

static inline uint8_t inb(uint16_t port) {
    uint8_t r; asm volatile("inb %1,%0" : "=a"(r) : "Nd"(port)); return r;
}

#define KBD_BUFFER_SIZE 128
static char kbd_buffer[KBD_BUFFER_SIZE];
static int kbd_head = 0;
static int kbd_tail = 0;
static int shift_held = 0;
static int tab_held = 0;

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

void keyboard_handler(void) {
    uint8_t scancode = inb(0x60);

    if (scancode == 0x2A || scancode == 0x36) { shift_held = 1; return; }
    if (scancode == 0xAA || scancode == 0xB6) { shift_held = 0; return; }

    if (scancode == 0x0F) { tab_held = 1; }
    if (scancode == 0x8F) { tab_held = 0; return; }

    if (scancode & 0x80) return;

    char c = shift_held ? scancode_shift_table[scancode] : scancode_table[scancode];

    if (scancode == 0x48) c = 0x11;
    if (scancode == 0x50) c = 0x12;
    if (scancode == 0x4B) c = 0x13;
    if (scancode == 0x4D) c = 0x14;

    if (c == 0) return;

    asm volatile("cli");
    int next = (kbd_head + 1) % KBD_BUFFER_SIZE;
    if (next != kbd_tail) {
        kbd_buffer[kbd_head] = c;
        kbd_head = next;
    }
    asm volatile("sti");
}

char kbd_getchar(void) {
    asm volatile("cli");
    if (kbd_head == kbd_tail) { asm volatile("sti"); return 0; }
    char c = kbd_buffer[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUFFER_SIZE;
    asm volatile("sti");
    return c;
}

int kbd_tab_held(void) {
    return tab_held;
}
