#include "klib/kutil.h"
#include "klib/kmem.h"
#include "klib/task.h"
#include "klib/desc.h"
#include "klib/lapic.h"
#include "klib/kcomm.h"
#include "ioapic.h"

#include "../scheduler/scheduler.h"

const uint8_t scheduler_image[] = {
#include "../images/scheduler.h"
};

void irq_handler(uint64_t vector, uint64_t excode, uint64_t ret_task) {
    void (*transfer)(uint64_t, uint64_t) = (void *)0xffffffffffe00000;

    uint64_t irq = vector - 0x80;
    if(irq == 0x1) {
        uint8_t key = kinb(0x60);
        d_printf("key pressed: %x\n", key);
        // ack receipt
        uint8_t a = kinb(0x61);
        a |= 0x82;
        koutb(0x61, a);
        a &= 0x7f;
        koutb(0x61, a);
    }

    lapic_conditional_eoi(vector);

    transfer(0, ret_task);
    while(1) {}
}

char irq_stack[1024];

void _start() {
    kmem_setup();

    lapic_init();
    lapic_setup();

    // TODO: perform early ACPI tables read for I/O APIC information

    ioapic_init();

    d_printf("boot initialization completed\n");

    // use TASK_MEM(1) for scheduler task
    task_load_elf(TASK_MEM(1), scheduler_image, 0x10000);

    // set up initial scheduler communication page
    uint64_t schpg = kmem_getpage();
    kmem_map(TASK_MEM(1)->cr3, COMM_BASE_ADDRESS, schpg, KMEM_MAP_DATA);
    kmem_map(kmem_current(), COMM_BASE_ADDRESS, schpg, KMEM_MAP_DATA);

    kcomm_t *schedin = (void *)COMM_BASE_ADDRESS;
    kcomm_init(schedin, 0x800);
    kcomm_t *schedout = (void *)(COMM_BASE_ADDRESS + COMM_OUT_OFFSET);
    kcomm_init(schedout, 0x800);

    /* TODO: spawn hardware task */

    // remove the current thread and swap to the scheduler
    // TODO: release the memory associated with this task!
    void (*transfer)(void *, void *) = (void *)0xffffffffffe00000;
    transfer(0, TASK_MEM(1));

    // TODO: delete task
    while(1) {}
}
