#include <stdint.h>
#include <stddef.h>

#include "klib/kcomm.h"
#include "klib/avl.h"
#include "klib/kutil.h"
#include "klib/task.h"
#include "klib/synch.h"
#include "klib/sheap.h"

#include "listen.h"
#include "interface.h"
#include "id.h"
#include "mman.h"
#include "task.h"
#include "synch.h"

typedef struct {
    uint64_t task_id;
    task_info_t *info;
} queue_entry;

queue_entry queue[100];
int queue_size;

static void remove_from_queue(uint64_t id);

// task data structures
static int process(queue_entry *q) {
    sched_in_packet_t in;
    sched_out_packet_t status;
    uint64_t in_size = sizeof(in);
    int ret = 0;

    while(!kcomm_get(q->info->sin, &in, &in_size)) {
        // reset in_size
        in_size = sizeof(in);

        ret = 1;
        status.type = in.type;
        status.req_id = in.req_id;
        status.result = 0;

        switch(in.type) {
        case SCHED_FORWARD: {
            uint64_t length = in_size - offsetof(sched_in_packet_t,
                forward.data);
            if(in.forward.length < length) length = in.forward.length;
            task_info_t *tinfo = sched_get_info(in.forward.task_id);
            if(tinfo) {
                status.result = kcomm_put(tinfo->gin, in.forward.data, length);
            }
            else status.result = -1;
            break;
        }
        case SCHED_WAIT: {
            // TODO: find object
            synchobj_t *obj = 0;
            synch_wait(obj, in.wait.value);
            break;
        }
        case SCHED_WAKE: {
            // TODO: find object
            synchobj_t *obj = 0;
            synch_wake(obj, in.wake.value, in.wake.count);
            break;
        }
        case SCHED_MAP_ANONYMOUS: {
            uint64_t id = in.map_anonymous.root_id;
            if(id == 0) id = q->info->root_id;
            status.result = mman_anonymous(id, in.map_anonymous.address,
                in.map_anonymous.size);
            break;
        }
        case SCHED_MAP_PHYSICAL: {
            uint64_t id = in.map_physical.root_id;
            if(id == 0) id = q->info->root_id;
            status.result = mman_physical(id, in.map_physical.address,
                in.map_physical.phy_addr, in.map_physical.size);
            break;
        }
        case SCHED_MAP_MIRROR: {
            uint64_t id = in.map_mirror.root_id;
            if(id == 0) id = q->info->root_id;
            status.result =
                mman_mirror(id, in.map_mirror.address, in.map_mirror.oroot_id,
                    in.map_mirror.oaddress, in.map_mirror.size);
            break;
        }
        case SCHED_UNMAP: {
            uint64_t id = in.unmap.root_id;
            if(id == 0) id = q->info->root_id;
            status.result = mman_unmap(id, in.unmap.address,
                in.unmap.size);
            break;
        }
        case SCHED_SET_NAME: {
            in.set_name.name[31] = 0;
            uint64_t id = in.set_name.task_id;
            if(id == 0) id = q->task_id;
            sched_set_name(id, in.set_name.name);
            break;
        }
        case SCHED_GET_NAMED: {
            in.get_named.name[31] = 0;
            status.type = SCHED_GET_NAMED;
            status.get_named.task_id = sched_named_task(in.get_named.name);
            break;
        }
        case SCHED_SPAWN: {
            uint64_t root_id = in.spawn.root_id;
            task_info_t *info = sheap_alloc(sizeof(*info));
            uint64_t task_id = sched_task_create(root_id, info);

            status.spawn.root_id = root_id;
            status.spawn.task_id = task_id;

            queue[queue_size].task_id = task_id;
            queue[queue_size].info = info;
            queue_size ++;

            break;
        }
        case SCHED_SET_STATE: {
            sched_set_state(in.set_state.task_id, in.set_state.index,
                in.set_state.value);
            break;
        }
        case SCHED_REAP: {
            uint64_t id = in.reap.task_id;
            if(id == 0) {
                id = q->task_id;
                // definitely don't send status update if reaping self...
                status.req_id = 0;
            }
            task_info_t *info = q->info;
            sched_task_reap(id);
            remove_from_queue(id);
            sheap_free(info);

            break;
        }
        default:
            d_printf("Unknown sched_in packet type! %x\n", in.type);
            break;
        }

        if(status.req_id != 0) {
            kcomm_put(q->info->sout, &status, sizeof(status));
        }
    }

    return ret;
}

static void remove_from_queue(uint64_t id) {
    for(int i = 0; i < queue_size; i ++) {
        if(queue[i].task_id == id) {
            queue[i] = queue[queue_size-1];
            queue_size --;
            break;
        }
    }
}

void listen(task_state_t *hw_task) {
    queue_size = 0;

    {
        task_info_t *info = sheap_alloc(sizeof(*info));
        // add hw task to queue
        queue[queue_size].task_id = sched_task_attach(hw_task, info);
        queue[queue_size].info = info;
        queue_size ++;

        hw_task->state |= TASK_STATE_RUNNABLE;
    }

    while(1) {
        int any = 0;
        for(int i = 0; i < queue_size; i ++) {
            any |= process(queue + i);
        }
        if(!any) {
            __asm__ __volatile__("int $0xff");
        }
    }
}

void process_for(uint64_t task_id) {
    for(int i = 0; i < queue_size; i ++) {
        if(queue[i].task_id == task_id) {
            process(queue + i);
            break;
        }
    }
}
