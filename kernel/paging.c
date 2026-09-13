#include "types.h"
#include "paging.h"
#include "heap.h"

static uint32_t page_directory[1024] __attribute__((aligned(4096)));
static uint32_t page_tables[4][1024] __attribute__((aligned(4096)));
static uint32_t bitmap[128];
#define BITMAP_WORDS 128
#define TOTAL_PAGES (BITMAP_WORDS * 32)

static void bitmap_set(uint32_t page) {
    bitmap[page / 32] |= (1 << (page % 32));
}

static void bitmap_clear(uint32_t page) {
    bitmap[page / 32] &= ~(1 << (page % 32));
}

static int bitmap_test(uint32_t page) {
    return bitmap[page / 32] & (1 << (page % 32));
}

void paging_init(void) {
    for (int i = 0; i < BITMAP_WORDS; i++) bitmap[i] = 0;
    for (int i = 0; i < 1024; i++) page_directory[i] = 0;
    for (int t = 0; t < 4; t++)
        for (int i = 0; i < 1024; i++) page_tables[t][i] = 0;
    for (uint32_t i = 0; i < 256; i++) {
        page_tables[0][i] = (i * 4096) | PAGE_PRESENT | PAGE_WRITE;
        bitmap_set(i);
    }
    page_directory[0] = (uint32_t)&page_tables[0] | PAGE_PRESENT | PAGE_WRITE;
    uint32_t heap_start_page = 0x100;
    uint32_t heap_end_page = 0x200;
    for (uint32_t i = heap_start_page; i < heap_end_page; i++) {
        page_tables[1][i - 256] = (i * 4096) | PAGE_PRESENT | PAGE_WRITE;
        bitmap_set(i);
    }
    page_directory[1] = (uint32_t)&page_tables[1] | PAGE_PRESENT | PAGE_WRITE;
    for (int i = 2; i < 4; i++)
        page_directory[i] = (uint32_t)&page_tables[i] | PAGE_PRESENT | PAGE_WRITE;
    asm volatile(
        "mov %0, %%cr3\n"
        "mov %%cr0, %%eax\n"
        "or $0x80000000, %%eax\n"
        "mov %%eax, %%cr0\n"
        : : "r"((uint32_t)page_directory) : "eax"
    );
}

uint32_t paging_alloc_page(void) {
    for (uint32_t i = 0; i < TOTAL_PAGES; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            return i * 4096;
        }
    }
    return 0;
}

void paging_free_page(uint32_t phys) {
    uint32_t page = phys / 4096;
    if (page < TOTAL_PAGES) bitmap_clear(page);
}

void paging_map(uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t pd_index = virt >> 22;
    uint32_t pt_index = (virt >> 12) & 0x3FF;
    uint32_t* pt = (uint32_t*)(page_directory[pd_index] & ~0xFFF);
    pt[pt_index] = (phys & ~0xFFF) | flags;
    asm volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

void page_fault_handler(uint32_t error_code, uint32_t cr2) {
    (void)error_code;
    (void)cr2;
    asm volatile("cli; hlt");
}
