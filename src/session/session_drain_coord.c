#include "../common/types.h"
#include "../common/alloc.h"

/*
 * Session drain coordinator: orchestrates graceful shutdown
 * of all session resources in dependency order.
 *
 * Drain order: streams → cursors → channels → leases →
 *              checkpoints → migrations → replicas → segments → spans
 */

typedef enum {
    VMX_DRAIN_STREAMS     = 0,
    VMX_DRAIN_CURSORS     = 1,
    VMX_DRAIN_CHANNELS    = 2,
    VMX_DRAIN_LEASES      = 3,
    VMX_DRAIN_CHECKPOINTS = 4,
    VMX_DRAIN_MIGRATIONS  = 5,
    VMX_DRAIN_REPLICAS    = 6,
    VMX_DRAIN_SEGMENTS    = 7,
    VMX_DRAIN_SPANS       = 8,
    VMX_DRAIN_COMPLETE    = 9
} vmx_drain_phase_t;

typedef struct {
    vmx_drain_phase_t phase;
    uint32_t items_remaining;
    uint32_t items_drained;
    uint32_t errors;
    bool     force_mode;
} vmx_drain_state_t;

int vmx_drain_init(vmx_drain_state_t *state, bool force) {
    if (!state) return -1;
    memset(state, 0, sizeof(*state));
    state->phase = VMX_DRAIN_STREAMS;
    state->force_mode = force;
    return 0;
}

int vmx_drain_step(vmx_session_t *sess, vmx_drain_state_t *state) {
    if (!sess || !state) return -1;
    if (state->phase == VMX_DRAIN_COMPLETE) return 0;

    state->items_remaining = 0;
    state->items_drained = 0;

    switch (state->phase) {
    case VMX_DRAIN_STREAMS:
        for (uint16_t i = 0; i < sess->stream_count; i++) {
            if (sess->streams[i] && sess->streams[i]->active) {
                vmx_stream_close(sess, sess->streams[i]);
                state->items_drained++;
            }
        }
        state->phase = VMX_DRAIN_CURSORS;
        break;

    case VMX_DRAIN_CURSORS:
        for (uint16_t i = 0; i < sess->cursor_count; i++) {
            if (sess->cursors[i] && sess->cursors[i]->active) {
                vmx_cursor_close(sess, sess->cursors[i]);
                state->items_drained++;
            }
        }
        state->phase = VMX_DRAIN_CHANNELS;
        break;

    case VMX_DRAIN_CHANNELS:
        for (uint16_t i = 0; i < sess->channel_count; i++) {
            if (sess->channels[i] &&
                sess->channels[i]->state == VMX_LIFECYCLE_ACTIVE) {
                sess->channels[i]->state = VMX_LIFECYCLE_DRAINING;
                if (sess->drain_count < VMX_MAX_CHANNELS) {
                    sess->drain_list[sess->drain_count++] = sess->channels[i];
                }
                state->items_drained++;
            }
        }
        state->phase = VMX_DRAIN_LEASES;
        break;

    case VMX_DRAIN_LEASES:
        for (uint16_t i = 0; i < sess->lease_count; i++) {
            if (sess->leases[i] && sess->leases[i]->active) {
                vmx_lease_force_release(sess, sess->leases[i]);
                state->items_drained++;
            }
        }
        state->phase = VMX_DRAIN_CHECKPOINTS;
        break;

    case VMX_DRAIN_CHECKPOINTS:
        for (uint8_t i = 0; i < sess->checkpoint_count; i++) {
            if (sess->checkpoints[i] && sess->checkpoints[i]->active) {
                sess->checkpoints[i]->active = false;
                if (sess->checkpoints[i]->listener_active)
                    vmx_checkpoint_cleanup_listener(sess, sess->checkpoints[i]);
                state->items_drained++;
            }
        }
        state->phase = VMX_DRAIN_MIGRATIONS;
        break;

    case VMX_DRAIN_MIGRATIONS:
        for (uint8_t i = 0; i < sess->migration_count; i++) {
            if (sess->migrations[i] && sess->migrations[i]->active) {
                vmx_migrate_abort(sess, sess->migrations[i]);
                state->items_drained++;
            }
        }
        state->phase = VMX_DRAIN_REPLICAS;
        break;

    case VMX_DRAIN_REPLICAS:
        for (uint8_t i = 0; i < sess->replica_set_count; i++) {
            if (sess->replica_sets[i] && sess->replica_sets[i]->active) {
                sess->replica_sets[i]->active = false;
                sess->replica_sets[i]->snap_active = false;
                state->items_drained++;
            }
        }
        state->phase = VMX_DRAIN_SEGMENTS;
        break;

    case VMX_DRAIN_SEGMENTS:
        /* GC all segments */
        vmx_session_gc_drained_channels(sess);
        state->phase = VMX_DRAIN_SPANS;
        break;

    case VMX_DRAIN_SPANS:
        for (uint16_t i = 0; i < sess->span_count; i++) {
            if (sess->spans[i] && sess->spans[i]->state == VMX_LIFECYCLE_ACTIVE) {
                vmx_cursor_invalidate_for_span(sess, sess->spans[i]->span_id);
                state->items_drained++;
            }
        }
        state->phase = VMX_DRAIN_COMPLETE;
        break;

    case VMX_DRAIN_COMPLETE:
        break;
    }

    return (int)state->items_drained;
}

int vmx_drain_run_all(vmx_session_t *sess, vmx_drain_state_t *state) {
    if (!sess || !state) return -1;

    int total = 0;
    while (state->phase != VMX_DRAIN_COMPLETE) {
        int rc = vmx_drain_step(sess, state);
        if (rc < 0) return rc;
        total += rc;
    }
    return total;
}

bool vmx_drain_is_complete(const vmx_drain_state_t *state) {
    return state && state->phase == VMX_DRAIN_COMPLETE;
}

const char *vmx_drain_phase_name(vmx_drain_phase_t phase) {
    switch (phase) {
    case VMX_DRAIN_STREAMS:     return "streams";
    case VMX_DRAIN_CURSORS:     return "cursors";
    case VMX_DRAIN_CHANNELS:    return "channels";
    case VMX_DRAIN_LEASES:      return "leases";
    case VMX_DRAIN_CHECKPOINTS: return "checkpoints";
    case VMX_DRAIN_MIGRATIONS:  return "migrations";
    case VMX_DRAIN_REPLICAS:    return "replicas";
    case VMX_DRAIN_SEGMENTS:    return "segments";
    case VMX_DRAIN_SPANS:       return "spans";
    case VMX_DRAIN_COMPLETE:    return "complete";
    default:                    return "unknown";
    }
}
