#include "../common/types.h"
#include "../common/alloc.h"

/*
 * Channel inflight operation tracking with timeout management.
 * Tracks pending operations and detects stale/timed-out entries.
 */

#define VMX_INFLIGHT_MAX 256

typedef struct {
    uint32_t op_id;
    uint32_t channel_id;
    uint32_t span_id;
    uint32_t segment_id;
    uint32_t start_epoch;
    uint32_t timeout_epochs;
    uint64_t bytes;
    bool     active;
    bool     timed_out;
} vmx_inflight_op_t;

typedef struct {
    vmx_inflight_op_t ops[VMX_INFLIGHT_MAX];
    uint32_t          count;
    uint32_t          next_op_id;
    uint32_t          timeout_count;
    uint64_t          total_bytes_inflight;
} vmx_inflight_tracker_t;

int vmx_inflight_init(vmx_inflight_tracker_t *tracker) {
    if (!tracker) return -1;
    memset(tracker, 0, sizeof(*tracker));
    tracker->next_op_id = 1;
    return 0;
}

uint32_t vmx_inflight_add(vmx_inflight_tracker_t *tracker,
                          uint32_t channel_id, uint32_t span_id,
                          uint32_t segment_id, uint32_t current_epoch,
                          uint32_t timeout_epochs, uint64_t bytes) {
    if (!tracker || tracker->count >= VMX_INFLIGHT_MAX) return 0;

    /* Find free slot */
    for (uint32_t i = 0; i < VMX_INFLIGHT_MAX; i++) {
        if (!tracker->ops[i].active) {
            vmx_inflight_op_t *op = &tracker->ops[i];
            op->op_id = tracker->next_op_id++;
            op->channel_id = channel_id;
            op->span_id = span_id;
            op->segment_id = segment_id;
            op->start_epoch = current_epoch;
            op->timeout_epochs = timeout_epochs;
            op->bytes = bytes;
            op->active = true;
            op->timed_out = false;
            tracker->count++;
            tracker->total_bytes_inflight += bytes;
            return op->op_id;
        }
    }
    return 0;
}

int vmx_inflight_complete(vmx_inflight_tracker_t *tracker, uint32_t op_id) {
    if (!tracker) return -1;

    for (uint32_t i = 0; i < VMX_INFLIGHT_MAX; i++) {
        if (tracker->ops[i].op_id == op_id && tracker->ops[i].active) {
            tracker->ops[i].active = false;
            tracker->count--;
            tracker->total_bytes_inflight -= tracker->ops[i].bytes;
            return 0;
        }
    }
    return -1;
}

int vmx_inflight_check_timeouts(vmx_inflight_tracker_t *tracker,
                                uint32_t current_epoch) {
    if (!tracker) return -1;

    int timed_out = 0;
    for (uint32_t i = 0; i < VMX_INFLIGHT_MAX; i++) {
        vmx_inflight_op_t *op = &tracker->ops[i];
        if (!op->active || op->timed_out) continue;

        if (current_epoch - op->start_epoch > op->timeout_epochs) {
            op->timed_out = true;
            timed_out++;
            tracker->timeout_count++;
        }
    }
    return timed_out;
}

int vmx_inflight_cancel_for_channel(vmx_inflight_tracker_t *tracker,
                                    uint32_t channel_id) {
    if (!tracker) return -1;
    int cancelled = 0;
    for (uint32_t i = 0; i < VMX_INFLIGHT_MAX; i++) {
        if (tracker->ops[i].active &&
            tracker->ops[i].channel_id == channel_id) {
            tracker->ops[i].active = false;
            tracker->count--;
            tracker->total_bytes_inflight -= tracker->ops[i].bytes;
            cancelled++;
        }
    }
    return cancelled;
}

int vmx_inflight_cancel_for_span(vmx_inflight_tracker_t *tracker,
                                 uint32_t span_id) {
    if (!tracker) return -1;
    int cancelled = 0;
    for (uint32_t i = 0; i < VMX_INFLIGHT_MAX; i++) {
        if (tracker->ops[i].active &&
            tracker->ops[i].span_id == span_id) {
            tracker->ops[i].active = false;
            tracker->count--;
            tracker->total_bytes_inflight -= tracker->ops[i].bytes;
            cancelled++;
        }
    }
    return cancelled;
}

/*
 * Compute inflight utilization as percentage of capacity.
 */
int vmx_inflight_utilization(const vmx_inflight_tracker_t *tracker) {
    if (!tracker) return 0;
    return (int)((tracker->count * 100) / VMX_INFLIGHT_MAX);
}

/*
 * Get the oldest inflight operation's age in epochs.
 */
uint32_t vmx_inflight_oldest_age(const vmx_inflight_tracker_t *tracker,
                                 uint32_t current_epoch) {
    if (!tracker) return 0;
    uint32_t oldest = 0;
    for (uint32_t i = 0; i < VMX_INFLIGHT_MAX; i++) {
        if (!tracker->ops[i].active) continue;
        uint32_t age = current_epoch - tracker->ops[i].start_epoch;
        if (age > oldest) oldest = age;
    }
    return oldest;
}

/*
 * Compute throughput estimate from completed operations.
 * Returns estimated bytes per epoch.
 */
uint64_t vmx_inflight_throughput_estimate(const vmx_inflight_tracker_t *tracker,
                                          uint32_t window_epochs) {
    if (!tracker || window_epochs == 0) return 0;
    /* Simple estimate: total inflight bytes / count represents avg op size,
     * multiply by completions per epoch */
    if (tracker->count == 0) return 0;
    uint64_t avg_bytes = tracker->total_bytes_inflight / tracker->count;
    return avg_bytes * tracker->count / window_epochs;
}
