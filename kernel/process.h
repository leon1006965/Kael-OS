#ifndef KAEL_PROCESS_H
#define KAEL_PROCESS_H
#include "types.h"
#define MAX_PROCESSES 64
#define PROCESS_RUNNING  0
#define PROCESS_BLOCKED  1
#define PROCESS_ZOMBIE   2
#define PROCESS_READY    3
typedef struct {
    uint32_t pid;
    uint32_t esp;
    uint32_t eip;
    uint32_t page_directory;
    uint32_t kernel_stack;
    int state;
    int exit_code;
    uint32_t parent_pid;
    char cwd[64];
    int fd_table[16];
} process_t;
void process_init(void);
int process_create(void (*entry)(void));
void process_exit(int code);
int process_wait(int pid);
process_t* process_get(int pid);
process_t* process_current(void);
void process_scheduler_tick(void);
#endif
