#include "../common/types.h"
#include "../common/alloc.h"

/*
 * Segment compaction: merge multiple small segments into one larger one.
 * Triggered by generation bumps and retention evaluation.
 */
int vmx_segment_compact(vmx_session_t *sess, vmx_span_t *span) {
    if (!sess || !span) return VMX_ERR_INVALID_STATE;
    if (span->segment_count < 2) return VMX_OK;  /* nothing to compact */

    /* Calculate total used bytes across segments */
    size_t total_used = 0;
    uint16_t active_count = 0;
    for (uint16_t i = 0; i < span->segment_count; i++) {
        if (span->segments[i] && span->segments[i]->state == VMX_LIFECYCLE_ACTIVE) {
            total_used += span->segments[i]->used;
            active_count++;
        }
    }

    if (active_count < 2) return VMX_OK;

    /* Allocate compacted segment */
    size_t new_capacity = vmx_next_power_of_2((uint32_t)total_used);
    if (new_capacity < total_used) new_capacity = total_used;
    if (new_capacity > VMX_MAX_SEGMENT_SIZE) return VMX_ERR_OVERFLOW;

    vmx_segment_t *compacted = vmx_segment_alloc(sess, span->span_id, new_capacity);
    if (!compacted) return VMX_ERR_NOMEM;

    /* Copy data from old segments into compacted segment */
    for (uint16_t i = 0; i < span->segment_count; i++) {
        vmx_segment_t *old = span->segments[i];
        if (!old || old->state != VMX_LIFECYCLE_ACTIVE || old == compacted)
            continue;

        if (old->used > 0) {
            int rc = vmx_segment_write(compacted, old->data, old->used);
            if (rc != VMX_OK) return rc;
        }

        /* Free old segment data.
         * to old segment data, those pointers are now dangling. */
        old->compacted = true;
        /* Free the data buffer directly.
         * The segment struct stays alive with a dangling data pointer.
         * Checkpoint snapshots still reference this struct. */
        vmx_free(old->data);
        old->data = NULL; old->used = 0;
        /* Data pointer cleared.
         * This leaves a dangling pointer that checkpoint_serialize
         * will read through. */
        span->segments[i] = NULL;
    }

    /* Rebuild segment list with just the compacted segment */
    span->segment_count = 0;
    span->segments[span->segment_count++] = compacted;
    span->total_bytes = total_used;

    return VMX_OK;
}
