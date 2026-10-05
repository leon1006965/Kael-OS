#include "mbr.h"
#include "ata.h"
#include "serial.h"

void mbr_init_empty(mbr_t* mbr) {
    uint8_t* p = (uint8_t*)mbr;
    for (int i = 0; i < 512; i++) p[i] = 0;
    mbr->signature = 0xAA55;
}

int mbr_add_partition(mbr_t* mbr, uint32_t lba_start, uint32_t sector_count, uint8_t type, int bootable) {
    for (int i = 0; i < 4; i++) {
        if (mbr->partitions[i].sector_count == 0) {
            mbr->partitions[i].status = bootable ? 0x80 : 0x00;
            /* CHS values: use max values for LBA disks */
            mbr->partitions[i].chs_first[0] = 0xFE;
            mbr->partitions[i].chs_first[1] = 0xFF;
            mbr->partitions[i].chs_first[2] = 0xFF;
            mbr->partitions[i].type = type;
            mbr->partitions[i].chs_last[0] = 0xFE;
            mbr->partitions[i].chs_last[1] = 0xFF;
            mbr->partitions[i].chs_last[2] = 0xFF;
            mbr->partitions[i].lba_first = lba_start;
            mbr->partitions[i].sector_count = sector_count;
            return 0;
        }
    }
    return -1; /* No free partition slot */
}

int mbr_write(uint32_t disk_lba, const mbr_t* mbr) {
    return ata_write_sector(disk_lba, (const uint8_t*)mbr);
}

int mbr_read(uint32_t disk_lba, mbr_t* mbr) {
    return ata_read_sector(disk_lba, (uint8_t*)mbr);
}

void mbr_print(const mbr_t* mbr, void (*print_fn)(const char*)) {
    /* This would need a more flexible print function for hex/dec */
    /* For now, serial output is handled by the caller */
}
