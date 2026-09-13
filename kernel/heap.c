#include "types.h"
#include "heap.h"

extern uint32_t __bss_end;

#define HEAP_START  (((uint32_t)&__bss_end + 0x1000) & ~0xFFF)
#define HEAP_SIZE   0x100000
#define HEAP_END    (HEAP_START + HEAP_SIZE)

typedef struct block {
    uint32_t size;
    int free;
    struct block* next;
} block_t;

static block_t* free_list = NULL;
static uint32_t heap_used = 0;

void heap_init(void) {
    free_list = (block_t*)HEAP_START;
    free_list->size = HEAP_SIZE - sizeof(block_t);
    free_list->free = 1;
    free_list->next = NULL;
    heap_used = sizeof(block_t);
}

static block_t* find_free(uint32_t size) {
    block_t* cur = free_list;
    while (cur) {
        if (cur->free && cur->size >= size) return cur;
        cur = cur->next;
    }
    return NULL;
}

void* kmalloc(uint32_t size) {
    if (size == 0) return NULL;
    block_t* blk = find_free(size);
    if (!blk) return NULL;
    if (blk->size >= size + sizeof(block_t) + 4) {
        block_t* split = (block_t*)((uint8_t*)blk + sizeof(block_t) + size);
        split->size = blk->size - size - sizeof(block_t);
        split->free = 1;
        split->next = blk->next;
        blk->next = split;
        blk->size = size;
    }
    blk->free = 0;
    heap_used += blk->size + sizeof(block_t);
    return (void*)((uint8_t*)blk + sizeof(block_t));
}

void* kmalloc_aligned(uint32_t size) {
    uint32_t raw = (uint32_t)kmalloc(size + 4096);
    if (!raw) return NULL;
    uint32_t aligned = (raw + 4095) & ~4095;
    return (void*)aligned;
}

static block_t* get_block(void* ptr) {
    return (block_t*)((uint8_t*)ptr - sizeof(block_t));
}

static void try_coalesce(void) {
    block_t* cur = free_list;
    while (cur && cur->next) {
        if (cur->free && cur->next->free) {
            cur->size += sizeof(block_t) + cur->next->size;
            cur->next = cur->next->next;
        } else {
            cur = cur->next;
        }
    }
}

void kfree(void* ptr) {
    if (!ptr) return;
    block_t* blk = get_block(ptr);
    blk->free = 1;
    heap_used -= blk->size + sizeof(block_t);
    try_coalesce();
}

uint32_t heap_get_used(void) { return heap_used; }
uint32_t heap_get_free(void) { return HEAP_SIZE - heap_used; }
