#include "../common/types.h"
#include "../common/alloc.h"

int vmx_handle_drain_deferred(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    if (!sess || !f) return VMX_ERR_INVALID_FRAME;

    /* Process deferred span deletions */
    if (sess->deferred_delete_grace > 0) {
        sess->deferred_delete_grace--;
        return VMX_OK;
    }

    for (uint16_t i = 0; i < sess->deferred_delete_count; i++) {
        vmx_span_t *span = sess->deferred_delete_list[i];
        if (!span) continue;
        if (span->state == VMX_LIFECYCLE_DEFERRED) {
            /* Invalidate cursors on span close */
            vmx_cursor_invalidate_for_span(sess, span->span_id);
            vmx_span_registry_destroy(sess, span);
            sess->deferred_delete_list[i] = NULL;
        }
    }

    /* Compact deferred list */
    uint16_t w = 0;
    for (uint16_t r = 0; r < sess->deferred_delete_count; r++) {
        if (sess->deferred_delete_list[r])
            sess->deferred_delete_list[w++] = sess->deferred_delete_list[r];
    }
    sess->deferred_delete_count = w;

    return VMX_OK;
}

int vmx_handle_gc_trigger(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    if (!sess || !f) return VMX_ERR_INVALID_FRAME;

    /* GC drained channels */
    vmx_session_gc_drained_channels(sess);

    /* Execute any scheduled dispatch. BUG: target may have been
     * freed by gc_drained_channels above. */
    if (sess->scheduled_dispatch_target) {
        vmx_channel_t *tgt = sess->scheduled_dispatch_target;
        volatile uint32_t ch_state = tgt->state;
        volatile uint32_t ch_id = tgt->channel_id;
        (void)ch_state; (void)ch_id;
        sess->scheduled_dispatch_target = NULL;
    }

    /* GC freed segments from session registry */
    uint16_t w = 0;
    for (uint16_t r = 0; r < sess->segment_count; r++) {
        if (sess->segments[r] && sess->segments[r]->state != VMX_LIFECYCLE_FREED) {
            sess->segments[w++] = sess->segments[r];
        }
    }
    for (uint16_t i = w; i < sess->segment_count; i++)
        sess->segments[i] = NULL;
    sess->segment_count = w;

    return VMX_OK;
}
