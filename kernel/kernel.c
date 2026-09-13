#include "types.h"
#include "gdt.h"
#include "heap.h"
#include "ata.h"
#include "fat.h"
#include "process.h"
#include "paging.h"

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

extern void idt_install(void);
extern void sti_enable(void);
extern void desktop_run(void);
extern void timer_init(void);

void kernel_main(void) {
    gdt_install();
    idt_install();
    heap_init();
    paging_init();
    outb(0x21, ~(1 << 1));
    outb(0xA1, 0xFF);
    process_init();
    timer_init();
    sti_enable();
    desktop_run();
    while (1) { asm volatile("hlt"); }
}
