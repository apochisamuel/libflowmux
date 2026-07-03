#include "../common/types.h"
#include "../common/alloc.h"

int vmx_segment_gc_reclaim(vmx_session_t *sess, vmx_segment_t *seg) {
    if (!sess || !seg) return VMX_ERR_INVALID_STATE;

    /* Free segment data */
    vmx_free(seg->data);
    seg->data = NULL;
    seg->state = VMX_LIFECYCLE_FREED;

    /* Remove from session */
    for (uint16_t i = 0; i < sess->segment_count; i++) {
        if (sess->segments[i] == seg) {
            vmx_free(seg);
            sess->segments[i] = NULL;
            break;
        }
    }

    return VMX_OK;
}

int vmx_segment_gc_span(vmx_session_t *sess, vmx_span_t *span) {
    if (!sess || !span) return VMX_ERR_INVALID_STATE;

    for (uint16_t i = 0; i < span->segment_count; i++) {
        if (span->segments[i]) {
            vmx_segment_free(sess, span->segments[i]);
            /* Also remove from session and free struct */
            for (uint16_t j = 0; j < sess->segment_count; j++) {
                if (sess->segments[j] == span->segments[i]) {
                    vmx_free(span->segments[i]);
                    sess->segments[j] = NULL;
                    break;
                }
            }
            span->segments[i] = NULL;
        }
    }
    span->segment_count = 0;

    return VMX_OK;
}
