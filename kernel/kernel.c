#include "types.h"
#include "multiboot.h"
#include "serial.h"
#include "ata.h"
#include "mbr.h"

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

static void test_mbr(void) {
    serial_puts("\n=== MBR TEST ===\n");

    /* Create a partition layout:
     * Partition 1: LBA 2048, 65536 sectors (32MB) - FAT32 boot
     * Partition 2: LBA 67584, rest of disk - FAT32 data
     */
    mbr_t mbr;
    mbr_init_empty(&mbr);

    serial_puts("[MBR] Adding partition 1: LBA 2048, 32MB FAT32...\n");
    if (mbr_add_partition(&mbr, 2048, 65536, PART_TYPE_FAT32_LBA, 1) != 0) {
        serial_puts("[MBR] ERROR: Failed to add partition 1\n");
        return;
    }

    serial_puts("[MBR] Adding partition 2: LBA 67584, 224MB FAT32...\n");
    if (mbr_add_partition(&mbr, 67584, 458752, PART_TYPE_FAT32_LBA, 0) != 0) {
        serial_puts("[MBR] ERROR: Failed to add partition 2\n");
        return;
    }

    serial_puts("[MBR] Writing MBR to sector 0...\n");
    if (mbr_write(0, &mbr) != 0) {
        serial_puts("[MBR] ERROR: Write failed\n");
        return;
    }
    serial_puts("[MBR] Write OK\n");

    /* Read back and verify */
    serial_puts("[MBR] Reading back MBR...\n");
    mbr_t verify;
    if (mbr_read(0, &verify) != 0) {
        serial_puts("[MBR] ERROR: Read back failed\n");
        return;
    }

    /* Check signature */
    if (verify.signature != 0xAA55) {
        serial_puts("[MBR] ERROR: Signature mismatch! Got ");
        serial_puthex(verify.signature);
        serial_puts(" expected 0xAA55\n");
        return;
    }
    serial_puts("[MBR] Signature OK (0x55AA)\n");

    /* Check partition 1 */
    if (verify.partitions[0].lba_first != 2048 ||
        verify.partitions[0].sector_count != 65536 ||
        verify.partitions[0].type != PART_TYPE_FAT32_LBA ||
        verify.partitions[0].status != 0x80) {
        serial_puts("[MBR] ERROR: Partition 1 mismatch\n");
        serial_puts("  LBA: ");
        serial_puthex(verify.partitions[0].lba_first);
        serial_puts(" Count: ");
        serial_puthex(verify.partitions[0].sector_count);
        serial_puts(" Type: ");
        serial_puthex(verify.partitions[0].type);
        serial_puts(" Status: ");
        serial_puthex(verify.partitions[0].status);
        serial_puts("\n");
        return;
    }
    serial_puts("[MBR] Partition 1 OK: LBA 2048, 65536 sectors, type 0x0C, bootable\n");

    /* Check partition 2 */
    if (verify.partitions[1].lba_first != 67584 ||
        verify.partitions[1].sector_count != 458752 ||
        verify.partitions[1].type != PART_TYPE_FAT32_LBA ||
        verify.partitions[1].status != 0x00) {
        serial_puts("[MBR] ERROR: Partition 2 mismatch\n");
        return;
    }
    serial_puts("[MBR] Partition 2 OK: LBA 67584, 458752 sectors, type 0x0C\n");

    serial_puts("=== MBR TEST COMPLETE ===\n\n");
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

    /* Run MBR test */
    test_mbr();

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
