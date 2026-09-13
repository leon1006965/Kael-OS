#include "types.h"
#include "fat.h"
#include "ata.h"
#include "heap.h"

static fat_bpb_t bpb;
static uint8_t* fat_table = NULL;
static uint32_t data_start;
static uint8_t* sector_buf = NULL;
fat_file_t open_files[FAT_MAX_FILES];

static inline void memset32(void* dst, uint32_t val, uint32_t count) {
    uint32_t* d = (uint32_t*)dst;
    for (uint32_t i = 0; i < count; i++) d[i] = val;
}

static uint32_t cluster_next(uint32_t cluster) {
    if (bpb.fat_size == 0) return 0xFFFFFFFF;
    uint32_t fat_offset = cluster * 2;
    uint32_t fat_sector = bpb.reserved_sectors + (fat_offset / bpb.bytes_per_sector);
    uint32_t ent_offset = fat_offset % bpb.bytes_per_sector;
    uint16_t* table = (uint16_t*)(fat_table + fat_sector * bpb.bytes_per_sector + ent_offset);
    uint32_t next = *table & 0x0FFF;
    if (next >= 0x0FF8) return 0xFFFFFFFF;
    return next;
}

static uint32_t cluster_next16(uint32_t cluster) {
    uint32_t fat_offset = cluster * 2;
    uint32_t fat_sector = bpb.reserved_sectors + (fat_offset / bpb.bytes_per_sector);
    uint32_t ent_offset = fat_offset % bpb.bytes_per_sector;
    uint16_t* table = (uint16_t*)(fat_table + fat_sector * bpb.bytes_per_sector + ent_offset);
    uint32_t next = *table;
    if (next >= 0xFFF8) return 0xFFFFFFFF;
    return next;
}

static uint32_t cluster_to_lba(uint32_t cluster) {
    return data_start + (cluster - 2) * bpb.sectors_per_cluster;
}

static void read_sector(uint32_t lba) {
    ata_read_sectors(lba, 1, sector_buf);
}

void fat_init(uint8_t drive) {
    bpb.drive = drive;
    sector_buf = (uint8_t*)kmalloc(512);
    if (!sector_buf) return;
    read_sector(0);
    uint8_t* b = sector_buf;
    bpb.bytes_per_sector = b[11] | (b[12] << 8);
    bpb.sectors_per_cluster = b[13];
    bpb.reserved_sectors = b[14] | (b[15] << 8);
    bpb.num_fats = b[16];
    bpb.root_entries = b[17] | (b[18] << 8);
    bpb.total_sectors = b[19] | (b[20] << 8);
    bpb.media_desc = b[21];
    bpb.sectors_per_fat = b[22] | (b[23] << 8);
    bpb.sectors_per_track = b[24] | (b[25] << 8);
    bpb.heads = b[26] | (b[27] << 8);
    bpb.hidden_sectors = b[28] | (b[29] << 8) | (b[30] << 16) | (b[31] << 24);
    bpb.total_sectors_large = b[32] | (b[33] << 8) | (b[34] << 16) | (b[35] << 24);
    bpb.fat_size = bpb.sectors_per_fat;
    uint32_t root_dir_sectors = (bpb.root_entries * 32 + bpb.bytes_per_sector - 1) / bpb.bytes_per_sector;
    data_start = bpb.reserved_sectors + bpb.num_fats * bpb.fat_size + root_dir_sectors;
    uint32_t fat_size_bytes = bpb.fat_size * bpb.bytes_per_sector;
    fat_table = (uint8_t*)kmalloc(fat_size_bytes);
    if (!fat_table) return;
    for (uint32_t i = 0; i < bpb.fat_size; i++) {
        ata_read_sectors(bpb.reserved_sectors + i, 1, fat_table + i * bpb.bytes_per_sector);
    }
    for (int i = 0; i < FAT_MAX_FILES; i++) open_files[i].used = 0;
}

static void parse_name(const char* path, char* out) {
    int i = 0;
    while (*path && *path != '/') { path++; }
    if (*path == '/') path++;
    while (*path && *path != '/' && i < 11) {
        if (*path == '.') { while (i < 8) out[i++] = ' '; path++; continue; }
        out[i++] = (*path >= 'a' && *path <= 'z') ? *path - 32 : *path;
        path++;
    }
    while (i < 11) out[i++] = ' ';
}

static int name_match(const char* a, const char* b) {
    for (int i = 0; i < 11; i++) if (a[i] != b[i]) return 0;
    return 1;
}

static fat_entry_t read_root_entry(int index) {
    fat_entry_t e = {0};
    uint32_t root_lba = bpb.reserved_sectors + bpb.num_fats * bpb.fat_size;
    uint32_t entries_per_sector = bpb.bytes_per_sector / 32;
    uint32_t sector = root_lba + index / entries_per_sector;
    uint32_t offset = (index % entries_per_sector) * 32;
    read_sector(sector);
    uint8_t* rec = sector_buf + offset;
    if (rec[0] == 0x00 || rec[0] == 0xE5) return e;
    if (rec[11] == 0x0F) return e;
    for (int i = 0; i < 8; i++) e.name[i] = rec[i];
    for (int i = 0; i < 3; i++) e.name[8 + i] = rec[8 + i];
    e.attr = rec[11];
    e.first_cluster = rec[26] | (rec[27] << 8);
    e.size = rec[28] | (rec[29] << 8) | (rec[30] << 16) | (rec[31] << 24);
    e.is_dir = (e.attr & 0x10) != 0;
    return e;
}

int fat_list(const char* path, fat_entry_t* entries, int max_entries) {
    int count = 0;
    int root_count = bpb.root_entries;
    for (int i = 0; i < root_count && count < max_entries; i++) {
        fat_entry_t e = read_root_entry(i);
        if (e.name[0] == 0) break;
        entries[count++] = e;
    }
    return count;
}

fat_file_t* fat_open(const char* path) {
    char target[11];
    parse_name(path, target);
    int root_count = bpb.root_entries;
    for (int i = 0; i < root_count; i++) {
        fat_entry_t e = read_root_entry(i);
        if (e.name[0] == 0) break;
        if (name_match(e.name, target)) {
            for (int f = 0; f < FAT_MAX_FILES; f++) {
                if (!open_files[f].used) {
                    open_files[f].used = 1;
                    open_files[f].is_dir = e.is_dir;
                    open_files[f].first_cluster = e.first_cluster;
                    open_files[f].size = e.size;
                    open_files[f].pos = 0;
                    return &open_files[f];
                }
            }
        }
    }
    return NULL;
}

int fat_read(fat_file_t* fd, void* buf, uint32_t count) {
    if (!fd || !fd->used) return -1;
    if (fd->pos >= fd->size) return 0;
    if (fd->pos + count > fd->size) count = fd->size - fd->pos;
    uint8_t* dst = (uint8_t*)buf;
    uint32_t cluster = fd->first_cluster;
    uint32_t bytes_per_cluster = bpb.sectors_per_cluster * bpb.bytes_per_sector;
    uint32_t skip = fd->pos / bytes_per_cluster;
    uint32_t offset_in_cluster = fd->pos % bytes_per_cluster;
    for (uint32_t i = 0; i < skip && cluster != 0xFFFFFFFF; i++)
        cluster = (bpb.fat_size == 0) ? cluster_next(cluster) : cluster_next16(cluster);
    uint32_t read = 0;
    while (read < count && cluster != 0xFFFFFFFF) {
        uint32_t lba = cluster_to_lba(cluster);
        for (uint32_t s = 0; s < bpb.sectors_per_cluster && read < count; s++) {
            read_sector(lba + s);
            uint32_t start = (read == 0) ? offset_in_cluster : 0;
            uint32_t len = bpb.bytes_per_sector - start;
            if (len > count - read) len = count - read;
            for (uint32_t i = 0; i < len; i++) dst[read + i] = sector_buf[start + i];
            read += len;
        }
        cluster = (bpb.fat_size == 0) ? cluster_next(cluster) : cluster_next16(cluster);
    }
    fd->pos += read;
    return read;
}

int fat_write(fat_file_t* fd, const void* buf, uint32_t count) {
    (void)fd; (void)buf; (void)count;
    return -1;
}

void fat_close(fat_file_t* fd) {
    if (fd) fd->used = 0;
}

int fat_mkdir(const char* path) { (void)path; return -1; }
int fat_touch(const char* path) { (void)path; return -1; }
int fat_rm(const char* path) { (void)path; return -1; }
