#ifndef KAEL_ELF_H
#define KAEL_ELF_H
#include "types.h"
typedef struct {
    uint8_t  magic[4];
    uint8_t  class;
    uint8_t  data;
    uint8_t  version;
    uint8_t  osabi;
    uint8_t  padding[8];
    uint16_t type;
    uint16_t machine;
    uint32_t version2;
    uint32_t entry;
    uint32_t phoff;
    uint32_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} __attribute__((packed)) elf32_header_t;

typedef struct {
    uint32_t type;
    uint32_t offset;
    uint32_t vaddr;
    uint32_t paddr;
    uint32_t filesz;
    uint32_t memsz;
    uint32_t flags;
    uint32_t align;
} __attribute__((packed)) elf32_phdr_t;

#define ELF_PT_LOAD 1
#define ELF_MAGIC0 0x7F

uint32_t elf_load(uint8_t* data, uint32_t size);
int elf_valid(uint8_t* data);
#endif
