#include "types.h"
#include "process.h"
#include "heap.h"
#include "gdt.h"

static process_t processes[MAX_PROCESSES];
static int current_pid = 0;
static uint32_t next_pid = 1;
static uint32_t saved_esp;

void process_init(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) processes[i].state = PROCESS_ZOMBIE;
    processes[0].pid = 0;
    processes[0].state = PROCESS_RUNNING;
    processes[0].esp = 0;
    processes[0].kernel_stack = 0x90000;
    processes[0].parent_pid = 0;
    for (int i = 0; i < 16; i++) processes[0].fd_table[i] = -1;
}

process_t* process_current(void) {
    return &processes[current_pid];
}

process_t* process_get(int pid) {
    if (pid < 0 || pid >= MAX_PROCESSES) return NULL;
    if (processes[pid].state == PROCESS_ZOMBIE) return NULL;
    return &processes[pid];
}

int process_create(void (*entry)(void)) {
    int pid = -1;
    for (int i = 1; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROCESS_ZOMBIE) { pid = i; break; }
    }
    if (pid < 0) return -1;
    process_t* p = &processes[pid];
    uint32_t kstack = (uint32_t)kmalloc(0x1000);
    if (!kstack) return -1;
    uint32_t stack_top = kstack + 0x1000;
    uint32_t* sp = (uint32_t*)stack_top;
    *(--sp) = 0x202;
    *(--sp) = 0x08;
    *(--sp) = (uint32_t)entry;
    *(--sp) = 0; *(--sp) = 0; *(--sp) = 0; *(--sp) = 0;
    *(--sp) = 0; *(--sp) = 0; *(--sp) = 0; *(--sp) = 0;
    p->pid = next_pid++;
    p->esp = (uint32_t)sp;
    p->eip = (uint32_t)entry;
    p->page_directory = 0;
    p->kernel_stack = stack_top;
    p->state = PROCESS_RUNNING;
    p->exit_code = 0;
    p->parent_pid = current_pid;
    for (int i = 0; i < 16; i++) p->fd_table[i] = -1;
    return p->pid;
}

void process_exit(int code) {
    process_t* p = process_current();
    p->state = PROCESS_ZOMBIE;
    p->exit_code = code;
    process_scheduler_tick();
}

int process_wait(int pid) {
    if (pid < 0 || pid >= MAX_PROCESSES) return -1;
    process_t* p = &processes[pid];
    while (p->state != PROCESS_ZOMBIE) { asm volatile("sti; hlt"); }
    int code = p->exit_code;
    p->state = PROCESS_ZOMBIE;
    return code;
}

void process_switch(int new_pid) {
    process_t* old = process_current();
    process_t* newp = &processes[new_pid];
    asm volatile("mov %%esp, %0" : "=r"(saved_esp));
    old->esp = saved_esp;
    current_pid = new_pid;
    newp->state = PROCESS_RUNNING;
    tss_set_kernel_stack(newp->kernel_stack);
    asm volatile("mov %0, %%esp" : : "r"(newp->esp));
}

void process_scheduler_tick(void) {
    int start = current_pid;
    for (int i = 1; i < MAX_PROCESSES; i++) {
        int idx = (start + i) % MAX_PROCESSES;
        if (idx == 0) continue;
        if (processes[idx].state == PROCESS_RUNNING) {
            if (idx != current_pid) process_switch(idx);
            return;
        }
    }
}
