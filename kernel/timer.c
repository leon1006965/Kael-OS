#include "net.h"

static volatile uint32_t g_ticks = 0;

/* called from idt.asm isr0 (IRQ0) */
void timer_handler(void) { g_ticks++; }

uint32_t get_ticks(void) { return g_ticks; }

void pit_init(void) {
    /* PIT channel 0, lo/hi access, mode 3 (square wave), 1000 Hz */
    uint32_t div = 1193182 / 1000;
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(div & 0xFF));
    outb(0x40, (uint8_t)((div >> 8) & 0xFF));
}
