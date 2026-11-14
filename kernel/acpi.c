#include "net.h"

#define RSDP_SIG            "RSD PTR "
#define SDT_HEADER_LEN      36u
#define FADT_DSDT           40u
#define FADT_SMI_CMD        48u
#define FADT_ACPI_ENABLE    52u
#define FADT_PM1A_CNT       64u
#define FADT_PM1B_CNT       68u
#define FADT_PM1_CNT_LEN    89u
#define FADT_X_PM1A_CNT     172u
#define FADT_X_PM1B_CNT     184u
#define PM1_SLP_TYP_MASK    (7u << 10)
#define PM1_SLP_EN          (1u << 13)
#define PM1_SCI_EN          1u

typedef struct __attribute__((packed)) {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
} rsdp_t;

typedef struct __attribute__((packed)) {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    uint8_t rest[26];
} sdt_header_t;

typedef struct {
    uint16_t port;
    uint8_t io_space;
} acpi_port_t;

static uint8_t checksum(const volatile uint8_t* p, uint32_t len) {
    uint8_t sum = 0;
    while (len--) sum = (uint8_t)(sum + *p++);
    return sum;
}

static int is_rsdp(uint32_t address) {
    const volatile uint8_t* p = (const volatile uint8_t*)address;
    return memcmp((const void*)p, RSDP_SIG, 8) == 0 && checksum(p, 20) == 0;
}

static uint32_t find_rsdp(void) {
    uint16_t ebda_segment;
    asm volatile("movw 0x40e, %0" : "=r"(ebda_segment));
    uint32_t ebda = (uint32_t)ebda_segment << 4;
    if (ebda >= 0x400 && ebda < 0xA0000) {
        uint32_t end = ebda + 1024;
        if (end > 0xA0000) end = 0xA0000;
        for (uint32_t p = ebda; p + 20 <= end; p += 16)
            if (is_rsdp(p)) return p;
    }
    for (uint32_t p = 0xE0000; p + 20 <= 0x100000; p += 16)
        if (is_rsdp(p)) return p;
    return 0;
}

static uint32_t find_fadt(uint32_t rsdt_address) {
    if (!rsdt_address) return 0;
    const volatile sdt_header_t* rsdt =
        (const volatile sdt_header_t*)rsdt_address;
    if (memcmp((const void*)rsdt->signature, "RSDT", 4) != 0 ||
        rsdt->length < SDT_HEADER_LEN || rsdt->length > 0x10000 ||
        checksum((const volatile uint8_t*)rsdt, rsdt->length) != 0)
        return 0;

    uint32_t count = (rsdt->length - SDT_HEADER_LEN) / 4;
    const volatile uint32_t* entries =
        (const volatile uint32_t*)((const volatile uint8_t*)rsdt + SDT_HEADER_LEN);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t address = entries[i];
        if (!address) continue;
        const volatile sdt_header_t* h = (const volatile sdt_header_t*)address;
        if (memcmp((const void*)h->signature, "FACP", 4) == 0 &&
            h->length >= 90 && h->length <= 0x10000 &&
            checksum((const volatile uint8_t*)h, h->length) == 0)
            return address;
    }
    return 0;
}

static int aml_integer(const uint8_t** cursor, const uint8_t* end,
                       uint32_t* value) {
    if (*cursor >= end) return 0;
    uint8_t op = *(*cursor)++;
    switch (op) {
    case 0x00: *value = 0; return 1; /* ZeroOp */
    case 0x01: *value = 1; return 1; /* OneOp */
    case 0x0A: /* BytePrefix */
        if (*cursor + 1 > end) return 0;
        *value = *(*cursor)++;
        return 1;
    case 0x0B: /* WordPrefix */
        if (*cursor + 2 > end) return 0;
        *value = (uint32_t)(*cursor)[0] | ((uint32_t)(*cursor)[1] << 8);
        *cursor += 2;
        return 1;
    case 0x0C: /* DWordPrefix */
        if (*cursor + 4 > end) return 0;
        memcpy(value, *cursor, 4);
        *cursor += 4;
        return 1;
    default:
        return 0;
    }
}

static int find_s5_types(uint32_t dsdt_address, uint8_t* type_a,
                         uint8_t* type_b) {
    if (!dsdt_address) return 0;
    const volatile sdt_header_t* dsdt =
        (const volatile sdt_header_t*)dsdt_address;
    if (memcmp((const void*)dsdt->signature, "DSDT", 4) != 0 ||
        dsdt->length < SDT_HEADER_LEN || dsdt->length > 0x100000)
        return 0;

    const uint8_t* bytes = (const uint8_t*)dsdt;
    const uint8_t* end = bytes + dsdt->length;
    for (const uint8_t* p = bytes + SDT_HEADER_LEN; p + 8 < end; p++) {
        if (p[0] != 0x08 || memcmp(p + 1, "_S5_", 4) != 0) continue;
        const uint8_t* q = p + 5;
        const uint8_t* limit = q + 12 < end ? q + 12 : end;
        while (q < limit && *q != 0x12) q++; /* PackageOp */
        if (q >= limit) continue;

        q++; /* PkgLength */
        if (q >= end) continue;
        const uint8_t* package_start = q;
        uint8_t lead = *q++;
        uint32_t follow = lead >> 6;
        uint32_t pkglen;
        if (!follow) {
            pkglen = lead & 0x3F;
        } else {
            if (q + follow > end) continue;
            pkglen = lead & 0x0F;
            for (uint32_t n = 0; n < follow; n++)
                pkglen |= (uint32_t)*q++ << (4 + 8 * n);
        }
        const uint8_t* package_end = package_start + pkglen;
        if (package_end > end || q >= package_end) continue;
        uint8_t elements = *q++;
        uint32_t a, b;
        if (elements < 2 || !aml_integer(&q, package_end, &a) ||
            !aml_integer(&q, package_end, &b) || a > 7 || b > 7)
            continue;
        *type_a = (uint8_t)a;
        *type_b = (uint8_t)b;
        return 1;
    }
    return 0;
}

static acpi_port_t fadt_io_port(const volatile uint8_t* fadt,
                                uint32_t legacy_offset,
                                uint32_t extended_offset) {
    acpi_port_t result = { 0, 0 };
    uint32_t address = *(const volatile uint32_t*)(fadt + legacy_offset);
    if (address && address <= 0xFFFF) {
        result.port = (uint16_t)address;
        result.io_space = 1;
        return result;
    }

    uint32_t fadt_length = *(const volatile uint32_t*)(fadt + 4);
    if (fadt_length < extended_offset + 12) return result;
    uint8_t space = fadt[extended_offset];
    uint64_t extended = *(const volatile uint64_t*)(fadt + extended_offset + 4);
    if (space == 1 && extended && extended <= 0xFFFF) {
        result.port = (uint16_t)extended;
        result.io_space = 1;
    }
    return result;
}

/* Return nonzero when the ACPI S5 transition was issued. */
int acpi_poweroff(void) {
    uint32_t rsdp_address = find_rsdp();
    if (!rsdp_address) return 0;
    const volatile rsdp_t* rsdp = (const volatile rsdp_t*)rsdp_address;
    uint32_t fadt_address = find_fadt(rsdp->rsdt_address);
    if (!fadt_address) return 0;

    const volatile uint8_t* fadt = (const volatile uint8_t*)fadt_address;
    if (*(const volatile uint32_t*)(fadt + 4) < FADT_PM1_CNT_LEN + 1)
        return 0;
    uint32_t dsdt = *(const volatile uint32_t*)(fadt + FADT_DSDT);
    uint8_t s5a, s5b;
    if (!find_s5_types(dsdt, &s5a, &s5b)) return 0;

    acpi_port_t pm1a = fadt_io_port(fadt, FADT_PM1A_CNT, FADT_X_PM1A_CNT);
    acpi_port_t pm1b = fadt_io_port(fadt, FADT_PM1B_CNT, FADT_X_PM1B_CNT);
    if (!pm1a.io_space) return 0;
    uint8_t control_len = fadt[FADT_PM1_CNT_LEN];
    if (control_len < 2) control_len = 2;

    if (!(inw(pm1a.port) & PM1_SCI_EN)) {
        uint32_t smi_port = *(const volatile uint32_t*)(fadt + FADT_SMI_CMD);
        uint8_t enable = fadt[FADT_ACPI_ENABLE];
        if (smi_port && smi_port <= 0xFFFF && enable) {
            outb((uint16_t)smi_port, enable);
            unsigned guard = 1000000;
            while (guard-- && !(inw(pm1a.port) & PM1_SCI_EN)) { }
        }
    }

    uint32_t mask = PM1_SLP_TYP_MASK | PM1_SLP_EN;
    if (control_len >= 4) {
        uint32_t value = inl(pm1a.port);
        value = (value & ~mask) | ((uint32_t)s5a << 10) | PM1_SLP_EN;
        outl(pm1a.port, value);
        if (pm1b.io_space) {
            value = inl(pm1b.port);
            value = (value & ~mask) | ((uint32_t)s5b << 10) | PM1_SLP_EN;
            outl(pm1b.port, value);
        }
    } else {
        uint16_t value = inw(pm1a.port);
        value = (uint16_t)((value & ~mask) | ((uint16_t)s5a << 10) | PM1_SLP_EN);
        outw(pm1a.port, value);
        if (pm1b.io_space) {
            value = inw(pm1b.port);
            value = (uint16_t)((value & ~mask) | ((uint16_t)s5b << 10) | PM1_SLP_EN);
            outw(pm1b.port, value);
        }
    }
    return 1;
}
