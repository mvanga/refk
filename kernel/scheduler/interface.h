#ifndef SCHEDULER_INTERFACE_H
#define SCHEDULER_INTERFACE_H

#include <stdint.h>

enum {
    COMM_SET_NAME,
    COMM_GET_NAMED,
    COMM_MAKE_ROOT,
    COMM_SPAWN,
    COMM_SET_STATE,
    COMM_REAP,
};

enum {
    COMM_STATE_RAX,
    COMM_STATE_RBX,
    COMM_STATE_RCX,
    COMM_STATE_RDX,
    COMM_STATE_RSI,
    COMM_STATE_RDI,
    COMM_STATE_RSP,
    COMM_STATE_RBP,
    COMM_STATE_R8,
    COMM_STATE_R9,
    COMM_STATE_R10,
    COMM_STATE_R11,
    COMM_STATE_R12,
    COMM_STATE_R13,
    COMM_STATE_R14,
    COMM_STATE_R15,
    COMM_STATE_RFLAGS,
    COMM_STATE_RIP,
    COMM_STATE_CS,
    COMM_STATE_DS,
    COMM_STATE_ES,
    COMM_STATE_FS,
    COMM_STATE_GS,
    COMM_STATE_SS,
    COMM_STATE_FS_BASE,
    COMM_STATE_GS_BASE,
    COMM_STATE_CR3,
    COMM_STATE
};

typedef struct comm_in_packet_t {
    uint8_t type;
    uint64_t req_id;
    union {
        struct {
            uint64_t task_id;
            char name[32];
        } set_name;
        struct {
            char name[32];
        } get_named;
        struct {
            uint64_t cr3;
        } spawn;
        struct {
            uint64_t task_id;
            uint64_t index;
            uint64_t value;
        } set_state;
        struct {
            uint64_t task_id;
        } reap;
    };
} comm_in_packet_t;

typedef struct comm_out_packet_t {
    uint8_t type;
    uint64_t req_id;
    union {
        struct {
            uint64_t task_id;
        } get_named;
        struct {
            uint64_t task_id;
        } spawn;
    };
} comm_out_packet_t;

#endif