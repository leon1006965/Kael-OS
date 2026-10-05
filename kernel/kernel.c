#include "types.h"
#include "multiboot.h"
#include "serial.h"
#include "ata.h"

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t v; asm volatile("inb %1,%0" : "=a"(v) : "Nd"(port)); return v;
}

extern void idt_install(void);
extern void sti_enable(void);
extern void desktop_run(void);
extern void keyboard_init(void);
extern int mouse_init(void);

uint32_t mb_magic;
uint32_t mb_info_ptr;

static void test_ata(void) {
    serial_puts("\n=== ATA TEST ===\n");

    /* Step 1: Init ATA */
    serial_puts("[ATA] Initializing...\n");
    if (ata_init() != 0) {
        serial_puts("[ATA] ERROR: Init failed!\n");
        return;
    }
    serial_puts("[ATA] Init OK\n");

    /* Step 2: Identify disk */
    serial_puts("[ATA] Identifying disk...\n");
    uint16_t identify[256];
    if (ata_identify(identify) != 0) {
        serial_puts("[ATA] ERROR: Identify failed!\n");
        return;
    }
    serial_puts("[ATA] Disk identified\n");
    serial_puts("[ATA] Model: ");
    for (int i = 27; i < 46; i++) {
        serial_putc((char)(identify[i] >> 8));
        serial_putc((char)(identify[i] & 0xFF));
    }
    serial_puts("\n");

    /* Step 3: Read sector 0 */
    serial_puts("[ATA] Reading sector 0...\n");
    uint8_t sector0[512];
    if (ata_read_sector(0, sector0) != 0) {
        serial_puts("[ATA] ERROR: Read failed!\n");
        return;
    }
    serial_puts("[ATA] Sector 0 data (hex dump):\n");

    /* Print first 64 bytes as hex */
    for (int i = 0; i < 64; i += 16) {
        serial_puts("  ");
        serial_puthex(i);
        serial_puts(": ");
        for (int j = 0; j < 16; j++) {
            uint8_t b = sector0[i + j];
            serial_putc("0123456789ABCDEF"[b >> 4]);
            serial_putc("0123456789ABCDEF"[b & 0xF]);
            serial_putc(' ');
        }
        serial_puts("\n");
    }

    /* Step 4: Write test pattern to sector 2048 (safe location) */
    serial_puts("[ATA] Writing test pattern to sector 2048...\n");
    uint8_t test_data[512];
    for (int i = 0; i < 512; i++) test_data[i] = (uint8_t)(i & 0xFF);
    if (ata_write_sector(2048, test_data) != 0) {
        serial_puts("[ATA] ERROR: Write failed!\n");
        return;
    }
    serial_puts("[ATA] Write OK\n");

    /* Step 5: Read back and verify */
    serial_puts("[ATA] Reading back sector 2048...\n");
    uint8_t verify[512];
    if (ata_read_sector(2048, verify) != 0) {
        serial_puts("[ATA] ERROR: Read back failed!\n");
        return;
    }
    int match = 1;
    for (int i = 0; i < 512; i++) {
        if (test_data[i] != verify[i]) {
            serial_puts("[ATA] ERROR: Mismatch at byte ");
            serial_putdec(i);
            serial_puts("\n");
            match = 0;
            break;
        }
    }
    if (match) serial_puts("[ATA] Verify OK - data matches!\n");

    serial_puts("=== ATA TEST COMPLETE ===\n\n");
}

void kernel_main(uint32_t magic, uint32_t info_ptr) {
    asm volatile("cli");
    mb_magic = magic;
    mb_info_ptr = info_ptr;

    /* Init serial for debug output */
    serial_init();
    serial_puts("\nKael OS v2.2 - ATA Test Build\n");
    serial_puts("==============================\n");

    /* Run ATA test before anything else */
    test_ata();

    /* Continue with normal boot */
    serial_puts("[BOOT] Starting normal boot...\n");
    idt_install();

    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);

    keyboard_init();
    mouse_init();

    uint8_t mask = inb(0xA1);
    mask &= ~(1 << 4);
    outb(0xA1, mask);
    uint8_t mask2 = inb(0x21);
    mask2 &= ~(1 << 2);
    outb(0x21, mask2);

    outb(0x21, ~(1 << 1) & ~(1 << 2));

    sti_enable();
    serial_puts("[BOOT] Desktop starting...\n");
    desktop_run();
    while (1) { asm volatile("hlt"); }
}
