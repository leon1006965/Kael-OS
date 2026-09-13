#ifndef KAEL_GDT_H
#define KAEL_GDT_H
#include "types.h"
void gdt_install(void);
void tss_set_kernel_stack(uint32_t esp0);
#endif
