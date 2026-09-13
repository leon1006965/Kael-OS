#include "types.h"
#include "process.h"
#include "fat.h"
#include "elf.h"
#include "heap.h"

extern fat_file_t open_files[];

#define SYS_EXIT    1
#define SYS_READ    3
#define SYS_WRITE   4
#define SYS_OPEN    5
#define SYS_CLOSE   6
#define SYS_EXEC    11
#define SYS_LSEEK   19

extern void vga_putpixel(int x, int y, uint8_t color);
extern void font_draw_char(int x, int y, char c, uint8_t color);

static void sys_write_screen(const char* buf, uint32_t count) {
    static int sx = 0, sy = 0;
    for (uint32_t i = 0; i < count; i++) {
        char c = buf[i];
        if (c == '\n') { sx = 0; sy += 10; if (sy >= 200) sy = 0; continue; }
        font_draw_char(sx, sy, c, 15);
        sx += 8;
        if (sx >= 320) { sx = 0; sy += 10; if (sy >= 200) sy = 0; }
    }
}

static uint32_t sys_exit(uint32_t status) {
    process_exit((int)status);
    return 0;
}

static uint32_t sys_read(uint32_t fd, uint32_t buf, uint32_t count) {
    (void)fd; (void)buf; (void)count;
    return 0;
}

static uint32_t sys_write(uint32_t fd, uint32_t buf, uint32_t count) {
    if (fd == 1) {
        sys_write_screen((const char*)buf, count);
        return count;
    }
    process_t* p = process_current();
    if (p->fd_table[fd] >= 0) {
        fat_file_t* f = NULL;
        extern fat_file_t open_files[];
        for (int i = 0; i < 32; i++) {
            if (open_files[i].used) { f = &open_files[i]; break; }
        }
        if (f) return fat_write(f, (const void*)buf, count);
    }
    return -1;
}

static uint32_t sys_open(uint32_t path, uint32_t flags) {
    (void)flags;
    fat_file_t* f = fat_open((const char*)path);
    if (!f) return -1;
    process_t* p = process_current();
    for (int i = 3; i < 16; i++) {
        if (p->fd_table[i] == -1) {
            p->fd_table[i] = (int)(f - open_files);
            return i;
        }
    }
    return -1;
}

static uint32_t sys_close(uint32_t fd) {
    process_t* p = process_current();
    if (fd < 16) p->fd_table[fd] = -1;
    return 0;
}

static uint32_t sys_exec(uint32_t path_addr, uint32_t argv) {
    (void)argv;
    const char* path = (const char*)path_addr;
    fat_file_t* f = fat_open(path);
    if (!f) return -1;
    uint8_t* buf = (uint8_t*)kmalloc(f->size);
    if (!buf) { fat_close(f); return -1; }
    fat_read(f, buf, f->size);
    fat_close(f);
    uint32_t entry = elf_load(buf, f->size);
    kfree(buf);
    if (entry == 0) return -1;
    process_t* p = process_current();
    p->eip = entry;
    p->esp = p->kernel_stack - 256;
    uint32_t* sp = (uint32_t*)p->esp;
    *(--sp) = 0x202;
    *(--sp) = 0x1B;
    *(--sp) = entry;
    *(--sp) = 0; *(--sp) = 0; *(--sp) = 0; *(--sp) = 0;
    *(--sp) = 0; *(--sp) = 0; *(--sp) = 0; *(--sp) = 0;
    p->esp = (uint32_t)sp;
    return 0;
}

uint32_t syscall_handler(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    switch (eax) {
        case SYS_EXIT:  return sys_exit(ebx);
        case SYS_READ:  return sys_read(ebx, ecx, edx);
        case SYS_WRITE: return sys_write(ebx, ecx, edx);
        case SYS_OPEN:  return sys_open(ebx, ecx);
        case SYS_CLOSE: return sys_close(ebx);
        case SYS_EXEC:  return sys_exec(ebx, ecx);
        default: return (uint32_t)-1;
    }
}
