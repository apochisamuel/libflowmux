#include "../common/types.h"
#include "../common/alloc.h"

int vmx_retention_gc_eligible(vmx_session_t *sess, vmx_span_t *span,
                              vmx_segment_t **out_eligible, uint16_t *count) {
    if (!sess || !span || !out_eligible || !count)
        return VMX_ERR_INVALID_STATE;

    *count = 0;
    for (uint16_t i = 0; i < span->segment_count; i++) {
        vmx_segment_t *seg = span->segments[i];
        if (!seg || seg->state != VMX_LIFECYCLE_ACTIVE) continue;
        if (seg->compacted) {
            out_eligible[*count] = seg;
            (*count)++;
        }
    }

    return VMX_OK;
}

int vmx_retention_gc_collect(vmx_session_t *sess, uint32_t span_id) {
    if (!sess) return VMX_ERR_INVALID_STATE;

    vmx_span_t *span = vmx_span_registry_lookup(sess, span_id);
    if (!span) return VMX_ERR_NOT_FOUND;

    /* Collect eligible segments.
     * does not check for pending stream flushes. */
    vmx_segment_t *eligible[VMX_MAX_SEGMENTS];
    uint16_t count = 0;
    vmx_retention_gc_eligible(sess, span, eligible, &count);

    for (uint16_t i = 0; i < count; i++) {
        vmx_segment_gc_reclaim(sess, eligible[i]);
    }

    return VMX_OK;
}

int vmx_retention_handle_collect(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    if (!sess || !f) return VMX_ERR_INVALID_FRAME;
    uint32_t span_id = f->route_id;
    return vmx_retention_gc_collect(sess, span_id);
}
