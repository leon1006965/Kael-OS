#include "fat32.h"
#include "ata.h"
#include "serial.h"

/* Simple memory functions */
static void* mem_set(void* dst, uint8_t val, uint32_t n) {
    uint8_t* d = dst;
    while (n--) *d++ = val;
    return dst;
}

static void* mem_cpy(void* dst, const void* src, uint32_t n) {
    uint8_t* d = dst;
    const uint8_t* s = src;
    while (n--) *d++ = *s++;
    return dst;
}

/* Write a sector of zeros */
static int write_zero_sector(uint32_t lba) {
    uint8_t buf[512];
    mem_set(buf, 0, 512);
    return ata_write_sector(lba, buf);
}

int fat32_format(uint32_t part_lba, uint32_t part_sectors) {
    /* FAT32 parameters */
    uint16_t bytes_per_sec = 512;
    uint8_t  sec_per_clus = 8;  /* 4KB clusters */
    uint16_t reserved_sec = 32;
    uint8_t  num_fats = 2;
    uint16_t root_entry_cnt = 0; /* FAT32 uses clusters */
    uint16_t sec_per_trk = 63;
    uint16_t num_heads = 255;
    uint32_t hidden_sec = part_lba;

    /* Calculate FAT size */
    uint32_t data_sectors = part_sectors - reserved_sec;
    uint32_t clusters = data_sectors / sec_per_clus;
    /* Each FAT sector holds 128 entries (512/4) */
    uint32_t fat_size = (clusters + 2 + 127) / 128;
    
    /* Total sectors */
    uint32_t total_sec = part_sectors;

    serial_puts("[FAT32] Formatting partition at LBA ");
    serial_puthex(part_lba);
    serial_puts(", ");
    serial_putdec(part_sectors);
    serial_puts(" sectors\n");
    serial_puts("[FAT32] Cluster size: ");
    serial_putdec(sec_per_clus * bytes_per_sec);
    serial_puts(" bytes, clusters: ");
    serial_putdec(clusters);
    serial_puts(", FAT size: ");
    serial_putdec(fat_size);
    serial_puts(" sectors\n");

    /* Write boot sector (BPB) */
    fat32_bpb_t bpb;
    mem_set(&bpb, 0, sizeof(bpb));
    bpb.jmp_boot[0] = 0xEB;
    bpb.jmp_boot[1] = 0x58;
    bpb.jmp_boot[2] = 0x90;
    mem_cpy(bpb.oem_name, "KAEL OS ", 8);
    bpb.bytes_per_sec = bytes_per_sec;
    bpb.sec_per_clus = sec_per_clus;
    bpb.reserved_sec = reserved_sec;
    bpb.num_fats = num_fats;
    bpb.root_entry_cnt = root_entry_cnt;
    bpb.total_sec_16 = 0; /* Use 32-bit */
    bpb.media_type = 0xF8;
    bpb.fat_size_16 = 0; /* Use 32-bit */
    bpb.sec_per_trk = sec_per_trk;
    bpb.num_heads = num_heads;
    bpb.hidden_sec = hidden_sec;
    bpb.total_sec_32 = total_sec;
    bpb.fat_size_32 = fat_size;
    bpb.ext_flags = 0;
    bpb.fs_version = 0;
    bpb.root_clus = 2;
    bpb.fs_info = 1;
    bpb.bk_boot_sec = 6;
    bpb.drive_num = 0x80;
    bpb.boot_sig = 0x29;
    bpb.volume_id = 0x12345678;
    mem_cpy(bpb.volume_label, "KAEL OS    ", 11);
    mem_cpy(bpb.fs_type, "FAT32   ", 8);
    bpb.boot_sig2 = 0xAA55;

    serial_puts("[FAT32] Writing boot sector...\n");
    if (ata_write_sector(part_lba, (uint8_t*)&bpb) != 0) {
        serial_puts("[FAT32] ERROR: Failed to write boot sector\n");
        return -1;
    }

    /* Write backup boot sector */
    serial_puts("[FAT32] Writing backup boot sector...\n");
    if (ata_write_sector(part_lba + 6, (uint8_t*)&bpb) != 0) {
        serial_puts("[FAT32] ERROR: Failed to write backup boot sector\n");
        return -1;
    }

    /* Write FSInfo sector */
    serial_puts("[FAT32] Writing FSInfo sector...\n");
    fat32_fsinfo_t fsinfo;
    mem_set(&fsinfo, 0, sizeof(fsinfo));
    fsinfo.lead_sig = 0x41615252;
    fsinfo.struc_sig = 0x61417272;
    fsinfo.free_count = clusters - 2; /* Minus reserved clusters 0 and 1 */
    fsinfo.next_free = 2; /* First free cluster after root */
    fsinfo.trail_sig = 0xAA550000;
    if (ata_write_sector(part_lba + 1, (uint8_t*)&fsinfo) != 0) {
        serial_puts("[FAT32] ERROR: Failed to write FSInfo\n");
        return -1;
    }

    /* Clear reserved sectors (2-31) */
    serial_puts("[FAT32] Clearing reserved sectors...\n");
    for (uint32_t i = 2; i < reserved_sec; i++) {
        if (i == 6) continue; /* Skip backup boot */
        write_zero_sector(part_lba + i);
    }

    /* Write FAT tables */
    serial_puts("[FAT32] Writing FAT tables...\n");
    uint8_t fat_sector[512];
    
    for (int fat = 0; fat < num_fats; fat++) {
        uint32_t fat_lba = part_lba + reserved_sec + (fat * fat_size);
        
        /* First FAT sector: entry 0 = media type, entry 1 = end of chain */
        mem_set(fat_sector, 0, 512);
        fat_sector[0] = 0xF8; /* Media type */
        fat_sector[1] = 0xFF;
        fat_sector[2] = 0xFF;
        fat_sector[3] = 0xFF; /* End of chain marker */
        fat_sector[4] = 0xFF;
        fat_sector[5] = 0xFF;
        fat_sector[6] = 0xFF;
        fat_sector[7] = 0xFF;
        
        /* Root directory cluster (2) = end of chain */
        fat_sector[8] = 0xFF;
        fat_sector[9] = 0xFF;
        fat_sector[10] = 0xFF;
        fat_sector[11] = 0x0F; /* EOF marker */
        
        ata_write_sector(fat_lba, fat_sector);

        /* Clear remaining FAT sectors */
        mem_set(fat_sector, 0, 512);
        for (uint32_t s = 1; s < fat_size; s++) {
            ata_write_sector(fat_lba + s, fat_sector);
        }
    }

    /* Clear root directory cluster (cluster 2) */
    serial_puts("[FAT32] Clearing root directory...\n");
    uint32_t root_lba = part_lba + reserved_sec + (num_fats * fat_size);
    for (uint32_t s = 0; s < sec_per_clus; s++) {
        write_zero_sector(root_lba + s);
    }

    serial_puts("[FAT32] Format complete!\n");
    return 0;
}

int fat32_read_bpb(uint32_t part_lba, fat32_bpb_t* bpb) {
    return ata_read_sector(part_lba, (uint8_t*)bpb);
}

void fat32_print_bpb(const fat32_bpb_t* bpb) {
    serial_puts("FAT32 BPB:\n");
    serial_puts("  OEM: ");
    for (int i = 0; i < 8; i++) serial_putc(bpb->oem_name[i]);
    serial_puts("\n");
    serial_puts("  Bytes/sector: ");
    serial_putdec(bpb->bytes_per_sec);
    serial_puts("\n");
    serial_puts("  Sectors/cluster: ");
    serial_putdec(bpb->sec_per_clus);
    serial_puts("\n");
    serial_puts("  Reserved sectors: ");
    serial_putdec(bpb->reserved_sec);
    serial_puts("\n");
    serial_puts("  Number of FATs: ");
    serial_putdec(bpb->num_fats);
    serial_puts("\n");
    serial_puts("  Total sectors: ");
    serial_putdec(bpb->total_sec_32);
    serial_puts("\n");
    serial_puts("  FAT size: ");
    serial_putdec(bpb->fat_size_32);
    serial_puts("\n");
    serial_puts("  Root cluster: ");
    serial_putdec(bpb->root_clus);
    serial_puts("\n");
    serial_puts("  FS type: ");
    for (int i = 0; i < 8; i++) serial_putc(bpb->fs_type[i]);
    serial_puts("\n");
}
