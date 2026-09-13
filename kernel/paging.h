#ifndef KAEL_PAGING_H
#define KAEL_PAGING_H
#include "types.h"
#define PAGE_PRESENT  0x01
#define PAGE_WRITE    0x02
#define PAGE_USER     0x04
#define PAGE_SIZE     4096
void paging_init(void);
void paging_map(uint32_t virt, uint32_t phys, uint32_t flags);
uint32_t paging_alloc_page(void);
void paging_free_page(uint32_t phys);
void page_fault_handler(uint32_t error_code, uint32_t cr2);
#endif
