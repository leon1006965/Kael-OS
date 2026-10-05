#ifndef MBR_H
#define MBR_H

#include "types.h"

/* MBR partition entry (16 bytes) */
typedef struct {
    uint8_t  status;        /* 0x80 = bootable, 0x00 = not */
    uint8_t  chs_first[3];  /* CHS first sector (we use 0xFE 0xFF 0xFF for LBA) */
    uint8_t  type;          /* Partition type */
    uint8_t  chs_last[3];   /* CHS last sector */
    uint32_t lba_first;     /* LBA first sector */
    uint32_t sector_count;  /* Number of sectors */
} __attribute__((packed)) mbr_entry_t;

/* MBR structure (512 bytes) */
typedef struct {
    uint8_t  boot_code[446];     /* Boot code (0-445) */
    mbr_entry_t partitions[4];   /* Partition table (446-509) */
    uint16_t signature;          /* 0xAA55 (510-511) */
} __attribute__((packed)) mbr_t;

/* Partition type codes */
#define PART_TYPE_FAT32_LBA  0x0C
#define PART_TYPE_FAT32_CHS  0x0B
#define PART_TYPE_NTFS       0x07
#define PART_TYPE_LINUX      0x83
#define PART_TYPE_EXTENDED   0x05

/* Functions */
void mbr_init_empty(mbr_t* mbr);
int mbr_add_partition(mbr_t* mbr, uint32_t lba_start, uint32_t sector_count, uint8_t type, int bootable);
int mbr_write(uint32_t disk_lba, const mbr_t* mbr);
int mbr_read(uint32_t disk_lba, mbr_t* mbr);
void mbr_print(const mbr_t* mbr, void (*print_fn)(const char*));

#endif
