#ifndef KAEL_ATA_H
#define KAEL_ATA_H
#include "types.h"
void ata_init(void);
int ata_read_sectors(uint32_t lba, uint32_t count, uint8_t* buf);
int ata_write_sectors(uint32_t lba, uint32_t count, uint8_t* buf);
int ata_detect(void);
uint32_t ata_get_sectors(void);
#endif
