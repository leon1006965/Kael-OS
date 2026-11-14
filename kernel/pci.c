#include "net.h"

uint32_t pci_cfg_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off) {
    uint32_t addr = 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                    ((uint32_t)func << 8) | (off & 0xFC);
    outl(0xCF8, addr);
    return inl(0xCFC);
}

void pci_cfg_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off, uint32_t val) {
    uint32_t addr = 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                    ((uint32_t)func << 8) | (off & 0xFC);
    outl(0xCF8, addr);
    outl(0xCFC, val);
}

/* Read-only PCI identity probe; unlike pci_find(), this does not enable
 * decoding or bus mastering. Used to report NICs without a driver. */
int pci_find_id(uint16_t vid, uint16_t did, uint8_t* bus, uint8_t* slot) {
    for (int b = 0; b < 256; b++) {
        for (int s = 0; s < 32; s++) {
            uint32_t id = pci_cfg_read((uint8_t)b, (uint8_t)s, 0, 0);
            if ((id & 0xFFFF) == vid && (id >> 16) == did) {
                *bus = (uint8_t)b;
                *slot = (uint8_t)s;
                return 0;
            }
        }
    }
    return -1;
}

int pci_find(uint16_t vid, uint16_t did, uint8_t* bus, uint8_t* slot, uint16_t* io_base) {
    for (int b = 0; b < 256; b++) {
        for (int s = 0; s < 32; s++) {
            uint32_t id = pci_cfg_read((uint8_t)b, (uint8_t)s, 0, 0);
            if ((id & 0xFFFF) == 0xFFFF || (id >> 16) == 0xFFFF) continue;
            if ((id & 0xFFFF) == vid && (id >> 16) == did) {
                uint32_t bar0 = pci_cfg_read((uint8_t)b, (uint8_t)s, 0, 0x10);
                uint32_t cmd  = pci_cfg_read((uint8_t)b, (uint8_t)s, 0, 0x04);
                pci_cfg_write((uint8_t)b, (uint8_t)s, 0, 0x04, cmd | 0x05); /* IO + bus master */
                *bus = (uint8_t)b;
                *slot = (uint8_t)s;
                *io_base = (uint16_t)(bar0 & 0xFFFC);
                return 0;
            }
        }
    }
    return -1;
}

/* list PCI devices of class 0x02 (network), format "  VVVV:DDDD" */
int pci_list_nics(char lines[][72], int max) {
    int n = 0;
    for (int b = 0; b < 256 && n < max; b++) {
        for (int s = 0; s < 32 && n < max; s++) {
            uint32_t id = pci_cfg_read((uint8_t)b, (uint8_t)s, 0, 0);
            if ((id & 0xFFFF) == 0xFFFF) continue;
            uint32_t classcode = pci_cfg_read((uint8_t)b, (uint8_t)s, 0, 0x08);
            if ((classcode >> 24) != 0x02) continue;
            if ((id & 0xFFFF) == 0x11AB && ((id >> 16) & 0xFFFF) == 0x436C) {
                kstrcpy(lines[n], "  Marvell Yukon-2 88E8072 (11AB:436C)");
                n++;
                continue;
            }
            /* Otherwise show the raw VVVV:DDDD pair. */
            static const char* hex = "0123456789ABCDEF";
            char* p = lines[n];
            *p++ = ' '; *p++ = ' ';
            uint32_t v = id & 0xFFFF, d = (id >> 16) & 0xFFFF;
            for (int shift = 12; shift >= 0; shift -= 4) *p++ = hex[(v >> shift) & 0xF];
            *p++ = ':';
            for (int shift = 12; shift >= 0; shift -= 4) *p++ = hex[(d >> shift) & 0xF];
            *p = 0;
            n++;
        }
    }
    return n;
}
