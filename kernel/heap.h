#ifndef KAEL_HEAP_H
#define KAEL_HEAP_H
#include "types.h"
void heap_init(void);
void* kmalloc(uint32_t size);
void* kmalloc_aligned(uint32_t size);
void kfree(void* ptr);
uint32_t heap_get_used(void);
uint32_t heap_get_free(void);
#endif
