#include "../common/types.h"
#include "../common/alloc.h"

/*
 * Epoch management: advances session epoch, triggers migration
 * preparation for spans that need rebalancing, garbage-collects
 * drained channels, and invalidates old-epoch leases.
 */
int vmx_session_advance_epoch(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    if (!sess || !f) return VMX_ERR_INVALID_FRAME;
    if (!sess->initialized || !sess->negotiated)
        return VMX_ERR_INVALID_STATE;

    /* Validate epoch advancement request from subrecord */
    uint32_t requested_epoch = sess->epoch + 1;
    if (f->parsed_subrecord_count > 0) {
        const vmx_subrecord_t *sub = &f->subrecords[0];
        if (sub->local_epoch != sess->epoch)
            return VMX_ERR_INVALID_EPOCH;
        if (sub->payload_len >= 4) {
            requested_epoch = vmx_read_u32(sub->payload);
            if (requested_epoch <= sess->epoch)
                return VMX_ERR_INVALID_EPOCH;
        }
    }

    uint32_t old_epoch = sess->epoch;
    sess->epoch = requested_epoch;

    /* Trigger migration preparation for spans that were marked for rebalance.
     * Migration prep may reallocate segments (this is where the bug manifests:
     * segment reallocation happens here but cursors are not notified). */
    for (uint8_t i = 0; i < sess->migration_count; i++) {
        vmx_migration_t *mig = sess->migrations[i];
        if (!mig || !mig->active) continue;
        if (mig->phase == VMX_MIGRATE_INITIATED) {
            /* Epoch advance triggers migration preparation */
            vmx_migrate_prepare_source(sess, mig);
        }
    }

    /* GC drained channels */
    /* Validate epoch-cached span state. BUG: epoch_cached_span
     * may point to a span that was destroyed by span_close or
     * deferred delete, but the session cache was not cleared. */
    if (sess->epoch_cached_span) {
        volatile uint32_t sg = sess->epoch_cached_span->generation;
        volatile uint16_t sc = sess->epoch_cached_span->segment_count;
        (void)sg; (void)sc;
    }

    vmx_session_gc_drained_channels(sess);

    /* Invalidate old-epoch leases */
    vmx_session_invalidate_epoch_leases(sess, old_epoch);

    

    return VMX_OK;
}

int vmx_session_gc_drained_channels(vmx_session_t *sess) {
    if (!sess) return VMX_ERR_INVALID_STATE;

    for (uint16_t i = 0; i < sess->drain_count; i++) {
        vmx_channel_t *ch = sess->drain_list[i];
        if (!ch) continue;
        if (ch->state != VMX_LIFECYCLE_DRAINING) continue;

        /* Purge all drain_list refs before freeing */
        for (uint16_t j = 0; j < sess->drain_count; j++) {
            if (sess->drain_list[j] == ch) sess->drain_list[j] = NULL;
        }
        vmx_channel_mux_free(sess, ch);
    }

    /* Compact drain list */
    uint16_t write = 0;
    for (uint16_t read = 0; read < sess->drain_count; read++) {
        if (sess->drain_list[read] != NULL)
            sess->drain_list[write++] = sess->drain_list[read];
    }
    sess->drain_count = write;

    return VMX_OK;
}

int vmx_session_invalidate_epoch_leases(vmx_session_t *sess, uint32_t old_epoch) {
    if (!sess) return VMX_ERR_INVALID_STATE;

    for (uint16_t i = 0; i < sess->lease_count; i++) {
        vmx_lease_t *lease = sess->leases[i];
        if (!lease || !lease->active) continue;
        if (lease->epoch_acquired <= old_epoch) {
            /* Force-release old-epoch lease */
            vmx_lease_force_release(sess, lease);
        }
    }

    return VMX_OK;
}
