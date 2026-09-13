#ifndef KAEL_FAT_H
#define KAEL_FAT_H
#include "types.h"
typedef struct {
    uint8_t  drive;
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entries;
    uint16_t total_sectors;
    uint8_t  media_desc;
    uint16_t sectors_per_fat;
    uint16_t sectors_per_track;
    uint16_t heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_large;
    uint32_t fat_size;
    uint16_t root_cluster;
    uint16_t fsinfo_sector;
} fat_bpb_t;

typedef struct {
    char name[11];
    uint8_t attr;
    uint16_t first_cluster;
    uint32_t size;
    int is_dir;
} fat_entry_t;

typedef struct {
    int used;
    int is_dir;
    uint32_t first_cluster;
    uint32_t size;
    uint32_t pos;
    char path[64];
} fat_file_t;

#define FAT_MAX_FILES 32

void fat_init(uint8_t drive);
int fat_list(const char* path, fat_entry_t* entries, int max_entries);
fat_file_t* fat_open(const char* path);
int fat_read(fat_file_t* fd, void* buf, uint32_t count);
int fat_write(fat_file_t* fd, const void* buf, uint32_t count);
void fat_close(fat_file_t* fd);
int fat_mkdir(const char* path);
int fat_touch(const char* path);
int fat_rm(const char* path);
#endif
