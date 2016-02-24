#include <stdint.h>

#define PHY_MAP_BASE (uint8_t *)0xffffc00000000000ULL
#define MAX_TASKS 32

typedef struct task_state {
    uint64_t registers[16];
    uint64_t valid;
} task_state_t;

task_state_t tasks[MAX_TASKS];
task_state_t *current_task;

void select_next_task() {
    // simple round-robin for now
    const task_state_t *end = tasks + MAX_TASKS;
    while(1) {
        current_task ++;
        if(current_task == end) current_task = tasks;

        if(current_task->valid) break;
    }
}

extern void yield();
extern void enter_task();

void thread_wrapper(void (*function)()) {
    function();
    current_task->valid = 0;
    yield();
}

void make_thread(void (*function)(), void *stack_memory, uint64_t stack_size) {
    for(int i = 0; i < MAX_TASKS; i ++) {
        if(tasks[i].valid != 0) continue;

        tasks[i].registers[5] = (uint64_t)function;
        tasks[i].registers[7] = (uint64_t)stack_memory + stack_size;
        tasks[i].registers[9] = (uint64_t)thread_wrapper;

        tasks[i].valid = 1;
        break;
    }
}

void kmain(uint64_t __attribute__((unused)) *mem) {
    for(int i = 0; i < 80*24*2; i ++) (PHY_MAP_BASE) [0xb8000 + i] = 0;
    (PHY_MAP_BASE) [0xb8000] = 'a';
    (PHY_MAP_BASE) [0xb8001] = 7;

    for(int i = 0; i < MAX_TASKS; i ++) {
        tasks[i].valid = 0;
    }

    // TODO: create your initial tasks here

    // start with first thread
    current_task = tasks + 0;

    enter_task();

    // should never be reached!
    while(1) {}
}

