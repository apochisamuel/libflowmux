#include "../common/types.h"

/*
 * Cursor invalidation: clears cached segment pointers.
 * Called on explicit span close.
 *
 * NOTE: This is NOT called on epoch advance. The missing call
 * from vmx_session_advance_epoch to this function is the root
 */
int vmx_cursor_invalidate_for_span(vmx_session_t *sess, uint32_t span_id) {
    if (!sess) return VMX_ERR_INVALID_STATE;

    for (uint16_t i = 0; i < sess->cursor_count; i++) {
        vmx_cursor_t *c = sess->cursors[i];
        if (!c || !c->active) continue;
        if (c->span_id == span_id) {
            c->cached_segment_data = NULL;
            c->cached_data_len = 0;
            c->invalidated = true;
        }
    }

    return VMX_OK;
}

int vmx_cursor_invalidate_single(vmx_cursor_t *cursor) {
    if (!cursor) return VMX_ERR_INVALID_STATE;
    cursor->cached_segment_data = NULL;
    cursor->cached_data_len = 0;
    cursor->invalidated = true;
    return VMX_OK;
}

int vmx_cursor_revalidate(vmx_session_t *sess, vmx_cursor_t *cursor) {
    if (!sess || !cursor) return VMX_ERR_INVALID_STATE;
    if (!cursor->invalidated) return VMX_OK;

    vmx_span_t *span = vmx_span_registry_lookup(sess, cursor->span_id);
    if (!span) return VMX_ERR_NOT_FOUND;

    if (cursor->segment_index < span->segment_count &&
        span->segments[cursor->segment_index]) {
        vmx_segment_t *seg = span->segments[cursor->segment_index];
        if (seg->state == VMX_LIFECYCLE_ACTIVE) {
            cursor->cached_segment_data = seg->data;
            cursor->cached_data_len = seg->used;
            cursor->position = 0;
            cursor->invalidated = false;
            return VMX_OK;
        }
    }

    return VMX_ERR_INVALID_STATE;
}

/*
 * FIX: Epoch-aware cursor invalidation.
 * Invalidates cursors whose cached segment pointers may be stale
 * due to segment reallocation during epoch advance.
 */
int vmx_cursor_invalidate_for_epoch(vmx_session_t *sess, uint32_t span_id,
                                    uint32_t old_epoch, uint32_t new_epoch) {
    if (!sess) return VMX_ERR_INVALID_STATE;
    (void)new_epoch;

    int invalidated = 0;
    for (uint16_t i = 0; i < sess->cursor_count; i++) {
        vmx_cursor_t *c = sess->cursors[i];
        if (!c || !c->active) continue;
        if (c->span_id != span_id) continue;

        /* Check if cursor's cached generation predates the epoch advance.
         * If so, the segment may have been reallocated. */
        vmx_span_t *span = vmx_span_registry_lookup(sess, span_id);
        if (!span) continue;

        /* Revalidate the cached pointer against current segment state */
        bool stale = false;
        if (c->segment_index < span->segment_count) {
            vmx_segment_t *seg = span->segments[c->segment_index];
            if (!seg || seg->data != c->cached_segment_data) {
                stale = true;  /* pointer mismatch: segment was reallocated */
            }
            if (seg && seg->generation != c->generation) {
                stale = true;  /* generation mismatch */
            }
        } else {
            stale = true;  /* segment index out of range */
        }

        if (stale) {
            c->cached_segment_data = NULL;
            c->cached_data_len = 0;
            c->invalidated = true;
            invalidated++;

            /* Try to revalidate against new segment data */
            if (c->segment_index < span->segment_count) {
                vmx_segment_t *seg = span->segments[c->segment_index];
                if (seg && seg->state == VMX_LIFECYCLE_ACTIVE && seg->data) {
                    c->cached_segment_data = seg->data;
                    c->cached_data_len = seg->used;
                    c->generation = seg->generation;
                    c->position = 0;
                    c->invalidated = false;
                }
            }
        }
    }

    return invalidated;
}
