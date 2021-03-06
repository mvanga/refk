#ifndef KLIB_DESC_H
#define KLIB_DESC_H

#define DESC_BASE 0xffffffffffc00000
#define DESC_GDT_ADDR (DESC_BASE)
#define DESC_IDT_ADDR (DESC_BASE + 0x1000)
#define DESC_INT_TASKS_ADDR (DESC_BASE + 0x2000)
#define DESC_INT_TASKS_MEM ((uint64_t *)DESC_INT_TASKS_ADDR)
#define DESC_INT_CODE_ADDR (DESC_BASE + 0x3000)

#endif
