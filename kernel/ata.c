#include "types.h"
#include "ata.h"
#include "heap.h"

static inline uint8_t inb(uint16_t port) {
    uint8_t val; asm volatile("inb %1,%0" : "=a"(val) : "Nd"(port)); return val;
}
static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0,%1" : : "a"(val), "Nd"(port));
}
static inline void io_wait(void) { inb(0x3F6); }

#define ATA_DATA       0x1F0
#define ATA_ERROR      0x1F1
#define ATA_SECCOUNT   0x1F2
#define ATA_LBA_LO     0x1F3
#define ATA_LBA_MID    0x1F4
#define ATA_LBA_HI     0x1F5
#define ATA_DRIVE      0x1F6
#define ATA_STATUS     0x1F7
#define ATA_CMD        0x1F7
#define ATA_ALT_STATUS 0x3F6

#define ATA_SR_BSY  0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01

#define ATA_CMD_IDENTIFY 0xEC
#define ATA_CMD_READ     0x20
#define ATA_CMD_WRITE    0x30

static int ata_present = 0;
static uint32_t ata_sectors = 0;

static void ata_wait_ready(void) {
    int timeout = 100000;
    while ((inb(ATA_STATUS) & ATA_SR_BSY) && timeout-- > 0) io_wait();
}

static int ata_wait_drq(void) {
    int timeout = 100000;
    while (timeout-- > 0) {
        uint8_t s = inb(ATA_STATUS);
        if (s & ATA_SR_ERR) return -1;
        if (s & ATA_SR_DRQ) return 0;
        io_wait();
    }
    return -1;
}

void ata_init(void) {
    outb(ATA_DRIVE, 0xA0);
    io_wait();
    outb(ATA_SECCOUNT, 0);
    io_wait();
    outb(ATA_LBA_LO, 0);
    io_wait();
    outb(ATA_CMD, ATA_CMD_IDENTIFY);
    io_wait();
    uint8_t status = inb(ATA_STATUS);
    if (status == 0) { ata_present = 0; return; }
    ata_wait_ready();
    if (ata_wait_drq() < 0) { ata_present = 0; return; }
    uint16_t buf[256];
    for (int i = 0; i < 256; i++) buf[i] = inb(ATA_DATA) | (inb(ATA_DATA) << 8);
    ata_sectors = buf[60] | (buf[61] << 16);
    ata_present = 1;
}

int ata_detect(void) { return ata_present; }
uint32_t ata_get_sectors(void) { return ata_sectors; }

int ata_read_sectors(uint32_t lba, uint32_t count, uint8_t* buf) {
    if (!ata_present) return -1;
    for (uint32_t s = 0; s < count; s++) {
        uint32_t l = lba + s;
        ata_wait_ready();
        outb(ATA_DRIVE, 0xE0 | ((l >> 24) & 0x0F));
        outb(ATA_SECCOUNT, 1);
        outb(ATA_LBA_LO, l & 0xFF);
        outb(ATA_LBA_MID, (l >> 8) & 0xFF);
        outb(ATA_LBA_HI, (l >> 16) & 0xFF);
        outb(ATA_CMD, ATA_CMD_READ);
        if (ata_wait_drq() < 0) return -1;
        uint16_t* dst = (uint16_t*)(buf + s * 512);
        for (int i = 0; i < 256; i++) {
            uint16_t w = inb(ATA_DATA);
            w |= (inb(ATA_DATA) << 8);
            dst[i] = w;
        }
    }
    return 0;
}

int ata_write_sectors(uint32_t lba, uint32_t count, uint8_t* buf) {
    if (!ata_present) return -1;
    for (uint32_t s = 0; s < count; s++) {
        uint32_t l = lba + s;
        ata_wait_ready();
        outb(ATA_DRIVE, 0xE0 | ((l >> 24) & 0x0F));
        outb(ATA_SECCOUNT, 1);
        outb(ATA_LBA_LO, l & 0xFF);
        outb(ATA_LBA_MID, (l >> 8) & 0xFF);
        outb(ATA_LBA_HI, (l >> 16) & 0xFF);
        outb(ATA_CMD, ATA_CMD_WRITE);
        if (ata_wait_drq() < 0) return -1;
        uint16_t* src = (uint16_t*)(buf + s * 512);
        for (int i = 0; i < 256; i++) {
            outb(ATA_DATA, src[i] & 0xFF);
            outb(ATA_DATA, (src[i] >> 8) & 0xFF);
        }
        ata_wait_ready();
    }
    return 0;
}
