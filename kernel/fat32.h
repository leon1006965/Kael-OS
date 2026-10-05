#ifndef FAT32_H
#define FAT32_H

#include "types.h"

/* FAT32 Boot Sector (BPB) */
typedef struct {
    uint8_t  jmp_boot[3];      /* Jump instruction */
    char     oem_name[8];      /* OEM name */
    uint16_t bytes_per_sec;    /* Bytes per sector */
    uint8_t  sec_per_clus;     /* Sectors per cluster */
    uint16_t reserved_sec;     /* Reserved sectors */
    uint8_t  num_fats;         /* Number of FATs */
    uint16_t root_entry_cnt;   /* Root directory entries (0 for FAT32) */
    uint16_t total_sec_16;     /* Total sectors (16-bit, 0 for FAT32) */
    uint8_t  media_type;       /* Media descriptor */
    uint16_t fat_size_16;      /* FAT size (16-bit, 0 for FAT32) */
    uint16_t sec_per_trk;      /* Sectors per track */
    uint16_t num_heads;        /* Number of heads */
    uint32_t hidden_sec;       /* Hidden sectors */
    uint32_t total_sec_32;     /* Total sectors (32-bit) */
    uint32_t fat_size_32;      /* FAT size (32-bit) */
    uint16_t ext_flags;        /* Extended flags */
    uint16_t fs_version;       /* Filesystem version */
    uint32_t root_clus;        /* Root directory cluster */
    uint16_t fs_info;          /* FSInfo sector */
    uint16_t bk_boot_sec;      /* Backup boot sector */
    uint8_t  reserved[12];     /* Reserved */
    uint8_t  drive_num;        /* Drive number */
    uint8_t  reserved1;        /* Reserved */
    uint8_t  boot_sig;         /* Boot signature (0x29) */
    uint32_t volume_id;        /* Volume serial number */
    char     volume_label[11]; /* Volume label */
    char     fs_type[8];       /* Filesystem type "FAT32   " */
    uint8_t  boot_code[420];   /* Boot code */
    uint16_t boot_sig2;        /* 0xAA55 */
} __attribute__((packed)) fat32_bpb_t;

/* FAT32 directory entry */
typedef struct {
    char     name[11];         /* Name (8.3 format) */
    uint8_t  attr;             /* Attributes */
    uint8_t  ntres;            /* NT reserved */
    uint8_t  crt_time_tenth;   /* Creation time (tenths) */
    uint16_t crt_time;         /* Creation time */
    uint16_t crt_date;         /* Creation date */
    uint16_t acc_date;         /* Last access date */
    uint16_t fst_clus_hi;      /* First cluster high word */
    uint16_t wrt_time;         /* Write time */
    uint16_t wrt_date;         /* Write date */
    uint16_t fst_clus_lo;      /* First cluster low word */
    uint32_t file_size;        /* File size */
} __attribute__((packed)) fat32_dirent_t;

/* FAT32 FSInfo structure */
typedef struct {
    uint32_t lead_sig;         /* 0x41615252 */
    uint8_t  reserved1[480];   /* Reserved */
    uint32_t struc_sig;        /* 0x61417272 */
    uint32_t free_count;       /* Free clusters */
    uint32_t next_free;        /* Next free cluster */
    uint8_t  reserved2[12];    /* Reserved */
    uint32_t trail_sig;        /* 0xAA550000 */
} __attribute__((packed)) fat32_fsinfo_t;

/* Directory entry attributes */
#define ATTR_READ_ONLY  0x01
#define ATTR_HIDDEN     0x02
#define ATTR_SYSTEM     0x04
#define ATTR_VOLUME_ID  0x08
#define ATTR_DIRECTORY  0x10
#define ATTR_ARCHIVE    0x20
#define ATTR_LFN        0x0F

/* Functions */
int fat32_format(uint32_t part_lba, uint32_t part_sectors);
int fat32_read_bpb(uint32_t part_lba, fat32_bpb_t* bpb);
void fat32_print_bpb(const fat32_bpb_t* bpb);

/* File operations */
typedef struct {
    fat32_bpb_t bpb;
    uint32_t part_lba;
    uint32_t fat_lba;
    uint32_t root_lba;
    uint32_t data_lba;
} fat32_fs_t;

int fat32_mount(uint32_t part_lba, fat32_fs_t* fs);
int fat32_read_file(fat32_fs_t* fs, const char* name, uint8_t* buf, uint32_t max_size, uint32_t* size_out);
int fat32_write_file(fat32_fs_t* fs, const char* name, const uint8_t* buf, uint32_t size);
int fat32_find_file(fat32_fs_t* fs, const char* name, fat32_dirent_t* dirent_out);

#endif
