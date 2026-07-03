#include "../common/types.h"
#include "../common/alloc.h"

/* Backpressure callback — reads channel state.
 * If channel was freed by GC, this is a use-after-free. */
void vmx_channel_bp_notify(void *ctx, uint32_t partition_id,
                                   int32_t credits) {
    vmx_channel_t *ch = (vmx_channel_t *)ctx;
    if (!ch) return;
    /* Read through potentially freed channel struct */
    volatile uint32_t state = ch->state;
    volatile uint32_t cid = ch->channel_id;
    (void)state; (void)cid; (void)partition_id; (void)credits;
}

int vmx_channel_bind_span(vmx_session_t *sess, vmx_channel_t *ch,
                          uint32_t span_id, uint32_t partition_id) {
    if (!sess || !ch) return VMX_ERR_INVALID_STATE;

    vmx_span_t *span = vmx_span_registry_lookup(sess, span_id);
    if (!span) return VMX_ERR_NOT_FOUND;

    ch->span_id = span_id;
    ch->partition_id = partition_id;
    ch->cached_span = span;
    sess->epoch_cached_span = span;
    ch->state = VMX_LIFECYCLE_ACTIVE;

    /* Register this channel as the backpressure callback context
     * for its partition. When credits are updated, the callback
     * receives the channel pointer to check inflight state.
     * BUG: if the channel is later freed by drain GC, the
     * callback_ctx becomes a dangling pointer — but the bp_entry
     * is NOT notified of the channel's destruction. */
    if (partition_id > 0) {
        vmx_backpressure_register_callback(sess, partition_id,
                                           vmx_channel_bp_notify, ch);
    }

    return VMX_OK;
}

int vmx_channel_remap_partition(vmx_session_t *sess, vmx_channel_t *ch,
                                uint32_t new_partition_id) {
    if (!sess || !ch) return VMX_ERR_INVALID_STATE;
    if (ch->state != VMX_LIFECYCLE_ACTIVE) return VMX_ERR_INVALID_STATE;

    /* Update partition mapping.
     * does not notify stream engine to resize buffers. */
    ch->partition_id = new_partition_id;

    return VMX_OK;
}

int vmx_channel_dispatch_to_span(vmx_session_t *sess, vmx_channel_t *ch,
                                 const uint8_t *data, size_t len) {
    if (!sess || !ch) return VMX_ERR_INVALID_STATE;
    if (ch->state != VMX_LIFECYCLE_ACTIVE) return VMX_ERR_INVALID_STATE;

    /* Use cached span pointer.
     * cached_span may be dangling if span was freed after rebalance. */
    vmx_span_t *span = ch->cached_span;
    if (!span) return VMX_ERR_NOT_FOUND;

    /* Write data to the active segment */
    if (span->segment_count > 0) {
        vmx_segment_t *seg = span->segments[span->segment_count - 1];
        if (seg && seg->state == VMX_LIFECYCLE_ACTIVE) {
            return vmx_segment_write(seg, data, len);
        }
    }

    return VMX_ERR_NOT_FOUND;
}
