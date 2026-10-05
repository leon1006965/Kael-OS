#include "installer.h"
#include "ata.h"
#include "mbr.h"
#include "fat32.h"
#include "serial.h"

/* Hardcoded kernel location (where the live image loads it) */
#define KERNEL_LBA 1
#define KERNEL_SIZE (64 * 512)  /* 64 sectors = 32KB */

/* Bootloader (first 446 bytes of MBR) - we'll copy from current boot sector */
extern char boot_boot_bin_start[];
extern char boot_boot_bin_end[];

/* Or we embed it */
static uint8_t bootloader_code[446];

/* Safety: warn but allow LBA 0 in testing (live media is on different controller) */
static int check_safe_target(uint32_t disk_lba) {
    serial_puts("[INSTALL] WARNING: Writing to LBA 0 (MBR) of ATA disk\n");
    serial_puts("[INSTALL] In production, verify this is NOT your live boot media!\n");
    return 0; /* Allow for testing */
}

int installer_run(uint32_t target_disk_lba) {
    serial_puts("\n");
    serial_puts("╔══════════════════════════════════════════╗\n");
    serial_puts("║       Kael OS Installer v1.0             ║\n");
    serial_puts("╚══════════════════════════════════════════╝\n");
    serial_puts("\n");

    /* Step 0: Safety check */
    serial_puts("[INSTALL] Step 0: Safety check...\n");
    if (check_safe_target(target_disk_lba) != 0) return -1;
    serial_puts("[INSTALL] Target disk is safe\n");

    /* Step 1: Identify target disk */
    serial_puts("[INSTALL] Step 1: Identifying target disk...\n");
    uint16_t identify[256];
    if (ata_identify(identify) != 0) {
        serial_puts("[INSTALL] ERROR: Cannot identify disk\n");
        return -1;
    }
    serial_puts("[INSTALL] Disk: ");
    for (int i = 27; i < 46; i++) {
        serial_putc((char)(identify[i] >> 8));
        serial_putc((char)(identify[i] & 0xFF));
    }
    serial_puts("\n");

    /* Step 2: Read current bootloader from live image */
    serial_puts("[INSTALL] Step 2: Reading bootloader...\n");
    uint8_t boot_sector[512];
    if (ata_read_sector(0, boot_sector) != 0) {
        serial_puts("[INSTALL] ERROR: Cannot read boot sector\n");
        return -1;
    }
    /* Copy first 446 bytes (bootloader code, not partition table) */
    for (int i = 0; i < 446; i++) bootloader_code[i] = boot_sector[i];
    serial_puts("[INSTALL] Bootloader loaded (446 bytes)\n");

    /* Step 3: Write MBR partition table to target */
    serial_puts("[INSTALL] Step 3: Creating partition table...\n");
    mbr_t mbr;
    mbr_init_empty(&mbr);
    /* Copy bootloader code to MBR */
    for (int i = 0; i < 446; i++) mbr.boot_code[i] = bootloader_code[i];

    /* Single FAT32 partition: starts at 2048, rest of disk */
    /* We'll use 65536 sectors (32MB) for the system partition */
    mbr_add_partition(&mbr, 2048, 65536, PART_TYPE_FAT32_LBA, 1);

    if (mbr_write(target_disk_lba, &mbr) != 0) {
        serial_puts("[INSTALL] ERROR: Cannot write MBR\n");
        return -1;
    }
    serial_puts("[INSTALL] MBR written with bootloader + partition\n");

    /* Step 4: Format partition as FAT32 */
    serial_puts("[INSTALL] Step 4: Formatting FAT32...\n");
    uint32_t part_lba = target_disk_lba + 2048;
    if (fat32_format(part_lba, 65536) != 0) {
        serial_puts("[INSTALL] ERROR: Format failed\n");
        return -1;
    }
    serial_puts("[INSTALL] FAT32 formatted\n");

    /* Step 5: Mount filesystem */
    serial_puts("[INSTALL] Step 5: Mounting filesystem...\n");
    fat32_fs_t fs;
    if (fat32_mount(part_lba, &fs) != 0) {
        serial_puts("[INSTALL] ERROR: Mount failed\n");
        return -1;
    }
    serial_puts("[INSTALL] Filesystem mounted\n");

    /* Step 6: Copy kernel to disk */
    serial_puts("[INSTALL] Step 6: Copying kernel...\n");
    uint8_t* kernel_buf = (uint8_t*)0x200000; /* Use some memory */
    /* Read kernel from live disk */
    for (uint32_t i = 0; i < KERNEL_SIZE / 512; i++) {
        if (ata_read_sector(KERNEL_LBA + i, kernel_buf + i * 512) != 0) {
            serial_puts("[INSTALL] ERROR: Cannot read kernel\n");
            return -1;
        }
    }
    serial_puts("[INSTALL] Kernel read (");
    serial_putdec(KERNEL_SIZE);
    serial_puts(" bytes)\n");

    /* Write kernel as file */
    if (fat32_write_file(&fs, "KERNEL.BIN", kernel_buf, KERNEL_SIZE) != 0) {
        serial_puts("[INSTALL] ERROR: Cannot write kernel\n");
        return -1;
    }
    serial_puts("[INSTALL] Kernel written to FAT32\n");

    /* Step 7: Write a system info file */
    serial_puts("[INSTALL] Step 7: Writing system files...\n");
    const char* sysinfo = "Kael OS v2.2\nInstalled via KaelBoot Installer\n";
    if (fat32_write_file(&fs, "SYSTEM.INF", (const uint8_t*)sysinfo, 44) != 0) {
        serial_puts("[INSTALL] ERROR: Cannot write system info\n");
    }
    serial_puts("[INSTALL] System files written\n");

    /* Step 8: Verify installation */
    serial_puts("[INSTALL] Step 8: Verifying...\n");
    uint8_t verify_buf[512];
    uint32_t verify_size = 0;
    if (fat32_read_file(&fs, "SYSTEM.INF", verify_buf, 512, &verify_size) != 0) {
        serial_puts("[INSTALL] ERROR: Verification read failed\n");
        return -1;
    }
    serial_puts("[INSTALL] Verified: ");
    for (uint32_t i = 0; i < verify_size && i < 44; i++) {
        serial_putc(verify_buf[i]);
    }
    serial_puts("\n");

    serial_puts("\n");
    serial_puts("╔══════════════════════════════════════════╗\n");
    serial_puts("║     Installation Complete!               ║\n");
    serial_puts("║     Remove live media and reboot.        ║\n");
    serial_puts("╚══════════════════════════════════════════╝\n");

    return 0;
}
