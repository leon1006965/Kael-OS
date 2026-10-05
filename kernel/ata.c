#include "ata.h"

/* I/O port functions */
static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t r; asm volatile("inb %1,%0" : "=a"(r) : "Nd"(port)); return r;
}
static inline void outw(uint16_t port, uint16_t val) {
    asm volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint16_t inw(uint16_t port) {
    uint16_t r; asm volatile("inw %1,%0" : "=a"(r) : "Nd"(port)); return r;
}

/* ATA registers */
#define ATA_DATA        0x1F0
#define ATA_ERROR       0x1F1
#define ATA_SECCOUNT    0x1F2
#define ATA_LBA_LO      0x1F3
#define ATA_LBA_MID     0x1F4
#define ATA_LBA_HI      0x1F5
#define ATA_DRIVE_HEAD  0x1F6
#define ATA_STATUS      0x1F7
#define ATA_COMMAND     0x1F7
#define ATA_ALT_STATUS  0x3F6
#define ATA_DEV_CTRL    0x3F6

/* Status bits */
#define ATA_SR_BSY  0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01

/* Commands */
#define ATA_CMD_IDENTIFY   0xEC
#define ATA_CMD_READ_PIO   0x20
#define ATA_CMD_WRITE_PIO  0x30
#define ATA_CMD_CACHE_FLUSH 0xE7

/* Drive select */
#define ATA_DRIVE_MASTER  0xA0
#define ATA_DRIVE_SLAVE   0xB0
#define ATA_DRIVE_LBA     0x40

#define ATA_TIMEOUT 1000000

static int ata_wait_bsy(void) {
    for (int i = 0; i < ATA_TIMEOUT; i++) {
        if (!(inb(ATA_STATUS) & ATA_SR_BSY)) return 0;
    }
    return -1;
}

static int ata_wait_drq(void) {
    for (int i = 0; i < ATA_TIMEOUT; i++) {
        uint8_t st = inb(ATA_STATUS);
        if (st & ATA_SR_ERR) return -1;
        if (st & ATA_SR_DRQ) return 0;
    }
    return -1;
}

static int ata_wait_not_bsy_drq(void) {
    for (int i = 0; i < ATA_TIMEOUT; i++) {
        uint8_t st = inb(ATA_STATUS);
        if (!(st & ATA_SR_BSY) && (st & ATA_SR_DRQ)) return 0;
        if (st & ATA_SR_ERR) return -1;
    }
    return -1;
}

static void ata_select_drive(int slave) {
    outb(ATA_DRIVE_HEAD, (slave ? ATA_DRIVE_SLAVE : ATA_DRIVE_MASTER) | ATA_DRIVE_LBA);
    /* Small delay after drive select */
    inb(ATA_ALT_STATUS);
    inb(ATA_ALT_STATUS);
    inb(ATA_ALT_STATUS);
}

static int ata_wait_ready(void) {
    for (int i = 0; i < ATA_TIMEOUT; i++) {
        uint8_t st = inb(ATA_ALT_STATUS);
        if (!(st & ATA_SR_BSY) && (st & ATA_SR_DRDY)) return 0;
    }
    return -1;
}

int ata_init(void) {
    /* Select master drive */
    ata_select_drive(0);
    if (ata_wait_ready() != 0) return -1;

    /* Disable interrupts */
    outb(ATA_DEV_CTRL, 0x02);
    return 0;
}

int ata_identify(uint16_t* identify_data) {
    ata_select_drive(0);
    if (ata_wait_ready() != 0) return -1;

    outb(ATA_SECCOUNT, 0);
    outb(ATA_LBA_LO, 0);
    outb(ATA_LBA_MID, 0);
    outb(ATA_LBA_HI, 0);
    outb(ATA_COMMAND, ATA_CMD_IDENTIFY);

    /* Wait for BSY to clear */
    if (ata_wait_bsy() != 0) return -1;

    /* Check if drive exists (LBA mid/hi should be 0) */
    if (inb(ATA_LBA_MID) != 0 || inb(ATA_LBA_HI) != 0) return -1;

    /* Wait for DRQ */
    if (ata_wait_drq() != 0) return -1;

    /* Read 256 words */
    for (int i = 0; i < 256; i++)
        identify_data[i] = inw(ATA_DATA);

    return 0;
}

int ata_read_sector(uint32_t lba, uint8_t* buf) {
    ata_select_drive(0);
    if (ata_wait_ready() != 0) return -1;

    /* Disable interrupts during command */
    outb(ATA_DEV_CTRL, 0x02);

    /* Send READ SECTORS command */
    outb(ATA_SECCOUNT, 1);
    outb(ATA_LBA_LO, (uint8_t)(lba & 0xFF));
    outb(ATA_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HI, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_COMMAND, ATA_CMD_READ_PIO);

    /* Wait for data */
    if (ata_wait_not_bsy_drq() != 0) return -1;

    /* Read 256 words = 512 bytes */
    for (int i = 0; i < 256; i++) {
        uint16_t w = inw(ATA_DATA);
        buf[i * 2] = (uint8_t)(w & 0xFF);
        buf[i * 2 + 1] = (uint8_t)(w >> 8);
    }

    return 0;
}

int ata_write_sector(uint32_t lba, const uint8_t* buf) {
    ata_select_drive(0);
    if (ata_wait_ready() != 0) return -1;

    outb(ATA_DEV_CTRL, 0x02);

    /* Wait for BSY to clear before sending command */
    if (ata_wait_bsy() != 0) return -1;

    /* Send WRITE SECTORS command */
    outb(ATA_SECCOUNT, 1);
    outb(ATA_LBA_LO, (uint8_t)(lba & 0xFF));
    outb(ATA_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HI, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_COMMAND, ATA_CMD_WRITE_PIO);

    /* Wait for DRQ */
    if (ata_wait_not_bsy_drq() != 0) return -1;

    /* Write 256 words = 512 bytes */
    for (int i = 0; i < 256; i++) {
        uint16_t w = (uint16_t)buf[i * 2] | ((uint16_t)buf[i * 2 + 1] << 8);
        outw(ATA_DATA, w);
    }

    /* Wait for BSY to clear after write */
    if (ata_wait_bsy() != 0) return -1;

    /* Check for errors */
    if (inb(ATA_STATUS) & ATA_SR_ERR) return -1;

    /* Cache flush */
    outb(ATA_COMMAND, ATA_CMD_CACHE_FLUSH);
    if (ata_wait_bsy() != 0) return -1;

    /* Final wait for ready */
    if (ata_wait_ready() != 0) return -1;

    return 0;
}

int ata_read_sectors(uint32_t lba, uint8_t count, uint8_t* buf) {
    for (uint8_t i = 0; i < count; i++) {
        if (ata_read_sector(lba + i, buf + i * 512) != 0) return -1;
    }
    return 0;
}

int ata_write_sectors(uint32_t lba, uint8_t count, const uint8_t* buf) {
    for (uint8_t i = 0; i < count; i++) {
        if (ata_write_sector(lba + i, buf + i * 512) != 0) return -1;
    }
    return 0;
}
