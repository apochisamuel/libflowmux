#include "../common/types.h"

/*
 * Replication sync protocol finite state machine.
 * Manages the multi-phase sync process between leader and follower.
 *
 * Phases: INIT → NEGOTIATE → SNAPSHOT → STREAM → CAUGHT_UP → ACTIVE
 */

typedef enum {
    VMX_SYNC_INIT      = 0,
    VMX_SYNC_NEGOTIATE = 1,
    VMX_SYNC_SNAPSHOT  = 2,
    VMX_SYNC_STREAM    = 3,
    VMX_SYNC_CAUGHT_UP = 4,
    VMX_SYNC_ACTIVE    = 5,
    VMX_SYNC_ERROR     = 6,
    VMX_SYNC_STALLED   = 7
} vmx_sync_phase_t;

typedef struct {
    vmx_sync_phase_t phase;
    uint32_t replica_set_id;
    uint32_t leader_id;
    uint32_t follower_id;
    uint64_t leader_offset;
    uint64_t follower_offset;
    uint64_t snapshot_size;
    uint64_t bytes_transferred;
    uint32_t segments_synced;
    uint32_t total_segments;
    uint32_t retries;
    uint32_t max_retries;
    uint32_t stall_epochs;
    uint32_t max_stall_epochs;
    bool     snapshot_complete;
} vmx_sync_fsm_t;

int vmx_sync_fsm_init(vmx_sync_fsm_t *fsm, uint32_t rs_id,
                      uint32_t leader, uint32_t follower) {
    if (!fsm) return -1;
    memset(fsm, 0, sizeof(*fsm));
    fsm->phase = VMX_SYNC_INIT;
    fsm->replica_set_id = rs_id;
    fsm->leader_id = leader;
    fsm->follower_id = follower;
    fsm->max_retries = 3;
    fsm->max_stall_epochs = 10;
    return 0;
}

int vmx_sync_fsm_advance(vmx_sync_fsm_t *fsm, uint64_t bytes_received,
                         uint32_t segments_done) {
    if (!fsm) return -1;

    fsm->bytes_transferred += bytes_received;
    fsm->segments_synced += segments_done;
    fsm->stall_epochs = 0;

    switch (fsm->phase) {
    case VMX_SYNC_INIT:
        fsm->phase = VMX_SYNC_NEGOTIATE;
        break;

    case VMX_SYNC_NEGOTIATE:
        fsm->phase = VMX_SYNC_SNAPSHOT;
        break;

    case VMX_SYNC_SNAPSHOT:
        if (fsm->segments_synced >= fsm->total_segments) {
            fsm->snapshot_complete = true;
            fsm->phase = VMX_SYNC_STREAM;
        }
        break;

    case VMX_SYNC_STREAM:
        /* Check if follower has caught up to leader */
        if (fsm->follower_offset + bytes_received >= fsm->leader_offset) {
            fsm->phase = VMX_SYNC_CAUGHT_UP;
        }
        fsm->follower_offset += bytes_received;
        break;

    case VMX_SYNC_CAUGHT_UP:
        fsm->phase = VMX_SYNC_ACTIVE;
        break;

    case VMX_SYNC_ACTIVE:
        /* Stay active, keep tracking offsets */
        fsm->follower_offset += bytes_received;
        break;

    case VMX_SYNC_ERROR:
        if (fsm->retries < fsm->max_retries) {
            fsm->retries++;
            fsm->phase = VMX_SYNC_SNAPSHOT;
        }
        break;

    case VMX_SYNC_STALLED:
        fsm->phase = VMX_SYNC_STREAM;
        break;
    }

    return 0;
}

int vmx_sync_fsm_on_error(vmx_sync_fsm_t *fsm) {
    if (!fsm) return -1;
    fsm->phase = VMX_SYNC_ERROR;
    return 0;
}

int vmx_sync_fsm_tick(vmx_sync_fsm_t *fsm) {
    if (!fsm) return -1;

    if (fsm->phase == VMX_SYNC_STREAM || fsm->phase == VMX_SYNC_SNAPSHOT) {
        fsm->stall_epochs++;
        if (fsm->stall_epochs > fsm->max_stall_epochs) {
            fsm->phase = VMX_SYNC_STALLED;
        }
    }
    return 0;
}

/*
 * Compute sync progress as percentage.
 */
int vmx_sync_progress(const vmx_sync_fsm_t *fsm) {
    if (!fsm) return 0;

    switch (fsm->phase) {
    case VMX_SYNC_INIT:      return 0;
    case VMX_SYNC_NEGOTIATE: return 5;
    case VMX_SYNC_SNAPSHOT:
        if (fsm->total_segments == 0) return 10;
        return 10 + (int)((fsm->segments_synced * 40) / fsm->total_segments);
    case VMX_SYNC_STREAM:
        if (fsm->leader_offset == 0) return 50;
        return 50 + (int)((fsm->follower_offset * 40) / fsm->leader_offset);
    case VMX_SYNC_CAUGHT_UP: return 95;
    case VMX_SYNC_ACTIVE:    return 100;
    case VMX_SYNC_ERROR:     return -1;
    case VMX_SYNC_STALLED:   return -2;
    default:                 return 0;
    }
}

bool vmx_sync_is_active(const vmx_sync_fsm_t *fsm) {
    return fsm && fsm->phase == VMX_SYNC_ACTIVE;
}

uint64_t vmx_sync_lag(const vmx_sync_fsm_t *fsm) {
    if (!fsm) return UINT64_MAX;
    if (fsm->leader_offset <= fsm->follower_offset) return 0;
    return fsm->leader_offset - fsm->follower_offset;
}
