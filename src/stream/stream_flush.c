#include "../common/types.h"
#include "../common/alloc.h"

/*
 * Stream flush: writes pending buffered data to target segment.
 * pending_target_segment may be freed by retention GC.
 */
int vmx_stream_flush_pending(vmx_session_t *sess, vmx_stream_t *stream) {
    if (!sess || !stream) return VMX_ERR_INVALID_STATE;
    if (!stream->active) return VMX_ERR_INVALID_STATE;

    if (!stream->pending_data || stream->pending_len == 0)
        return VMX_OK;  /* nothing to flush */

    /* Write to pending target segment. */
    vmx_segment_t *target = stream->pending_target_segment;
    if (target && target->data) {
        /* Data was already written during buffer_append, so this is
         * a secondary write path (for durability confirmation). */
        uint32_t hash = vmx_fnv1a(stream->pending_data, stream->pending_len);
        (void)hash;
    }

    vmx_free(stream->pending_data);
    stream->pending_data = NULL;
    stream->pending_len = 0;
    stream->pending_target_segment = NULL;

    return VMX_OK;
}
