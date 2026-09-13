#include "types.h"
#include "elf.h"
#include "heap.h"

int elf_valid(uint8_t* data) {
    elf32_header_t* hdr = (elf32_header_t*)data;
    if (hdr->magic[0] != 0x7F || hdr->magic[1] != 'E' ||
        hdr->magic[2] != 'L' || hdr->magic[3] != 'F') return 0;
    if (hdr->class != 1) return 0;
    return 1;
}

uint32_t elf_load(uint8_t* data, uint32_t size) {
    elf32_header_t* hdr = (elf32_header_t*)data;
    if (!elf_valid(data)) return 0;
    uint32_t entry = hdr->entry;
    for (int i = 0; i < hdr->phnum; i++) {
        elf32_phdr_t* phdr = (elf32_phdr_t*)(data + hdr->phoff + i * hdr->phentsize);
        if (phdr->type != ELF_PT_LOAD) continue;
        if (phdr->memsz == 0) continue;
        uint8_t* dest = (uint8_t*)phdr->paddr;
        uint32_t filesz = phdr->filesz;
        uint32_t memsz = phdr->memsz;
        if (phdr->paddr + memsz > 0x800000) continue;
        for (uint32_t j = 0; j < memsz; j++) {
            if (j < filesz) dest[j] = data[phdr->offset + j];
            else dest[j] = 0;
        }
    }
    return entry;
}
