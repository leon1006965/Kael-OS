#ifndef ATA_H
#define ATA_H

#include "types.h"

/* ATA PIO driver interface */
int ata_init(void);
int ata_read_sector(uint32_t lba, uint8_t* buf);
int ata_write_sector(uint32_t lba, const uint8_t* buf);
int ata_read_sectors(uint32_t lba, uint8_t count, uint8_t* buf);
int ata_write_sectors(uint32_t lba, uint8_t count, const uint8_t* buf);
int ata_identify(uint16_t* identify_data);

#endif
