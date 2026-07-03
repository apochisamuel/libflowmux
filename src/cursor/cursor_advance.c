#include "../common/types.h"

/*
 * Cursor advancement: moves cursor position and caches a direct
 * pointer to the target segment's data buffer for fast reads.
 *
 * caches a raw pointer to the segment's data buffer. If the segment
 * is subsequently reallocated (e.g., by migration preparation triggered
 * by epoch advance), this cached pointer becomes dangling.
 */
int vmx_cursor_advance_to_segment(vmx_session_t *sess, vmx_cursor_t *cursor,
                                  uint32_t segment_index) {
    if (!sess || !cursor) return VMX_ERR_INVALID_STATE;
    if (!cursor->active) return VMX_ERR_INVALID_STATE;
    if (cursor->invalidated) return VMX_ERR_INVALID_STATE;

    vmx_span_t *span = vmx_span_registry_lookup(sess, cursor->span_id);
    if (!span) return VMX_ERR_NOT_FOUND;

    if (segment_index >= span->segment_count)
        return VMX_ERR_INVALID_ID;

    vmx_segment_t *seg = span->segments[segment_index];
    if (!seg) return VMX_ERR_NOT_FOUND;
    if (seg->state != VMX_LIFECYCLE_ACTIVE) return VMX_ERR_INVALID_STATE;

    /* Cache direct pointer to segment data for fast subsequent reads.
     * data buffer. If the segment's data is freed/reallocated by
     * migration or compaction, this pointer dangles. */
    if (seg->state != VMX_LIFECYCLE_ACTIVE) return VMX_ERR_INVALID_STATE;
    cursor->cached_segment_data = seg->data;
    cursor->cached_data_len = seg->used;
    cursor->segment_index = segment_index;
    cursor->position = 0;
    cursor->generation = seg->generation;

    return VMX_OK;
}

/*
 * Materialize data from cursor's cached segment pointer.
 *
 * the cached pointer which may be dangling if the segment was
 * reallocated after the cursor advanced.
 */
int vmx_cursor_materialize(vmx_session_t *sess, vmx_cursor_t *cursor,
                           uint8_t *out, size_t out_len, size_t *bytes_read) {
    if (!sess || !cursor || !out || !bytes_read)
        return VMX_ERR_INVALID_STATE;
    if (!cursor->active) return VMX_ERR_INVALID_STATE;

    /* Read through cached segment data pointer.
     * If this pointer is dangling (freed segment), this will crash
     * with heap-use-after-free. */
    if (!cursor->cached_segment_data || cursor->cached_data_len == 0) {
        *bytes_read = 0;
        return VMX_OK;
    }

    size_t available = cursor->cached_data_len - cursor->position;
    size_t to_read = available < out_len ? available : out_len;

    memcpy(out, cursor->cached_segment_data + cursor->position, to_read);

    /* Force actual read from source to prevent dead-store elimination */
    volatile uint8_t probe = cursor->cached_segment_data[cursor->position];
    (void)probe;

    cursor->position += to_read;
    *bytes_read = to_read;

    return VMX_OK;
}

int vmx_cursor_handle_advance(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    if (!sess || !f) return VMX_ERR_INVALID_FRAME;

    uint32_t cursor_id = f->channel_id;
    vmx_cursor_t *c = vmx_cursor_lookup(sess, cursor_id);
    if (!c) return VMX_ERR_NOT_FOUND;

    uint32_t seg_idx = 0;
    if (f->parsed_subrecord_count > 0) {
        const vmx_subrecord_t *sub = &f->subrecords[0];
        if (sub->kind == VMX_SUB_CURSOR_POS && sub->payload_len >= 4) {
            seg_idx = vmx_read_u32(sub->payload);
        }
    }

    return vmx_cursor_advance_to_segment(sess, c, seg_idx);
}

int vmx_cursor_handle_read(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    if (!sess || !f) return VMX_ERR_INVALID_FRAME;

    uint32_t cursor_id = f->channel_id;
    vmx_cursor_t *c = vmx_cursor_lookup(sess, cursor_id);
    if (!c) return VMX_ERR_NOT_FOUND;

    /* Check prefetched data first. BUG: prefetch_data may be stale
     * if compaction freed the segment's data buffer between prefetch
     * and this read. */
    if (c->prefetch_data && c->prefetch_len > 0) {
        volatile uint32_t pfhash = vmx_fnv1a(c->prefetch_data, c->prefetch_len);
        (void)pfhash;
        c->prefetch_data = NULL;
        c->prefetch_len = 0;
    }

    /* Read through cursor — this is the crash path */
    uint8_t buf[4096];
    size_t bytes_read = 0;
    int rc = vmx_cursor_materialize(sess, c, buf, sizeof(buf), &bytes_read);
    if (rc != VMX_OK) return rc;

    /* Force compiler to preserve the materialized read */
    volatile uint32_t sink = 0;
    for (size_t i = 0; i < bytes_read; i++)
        sink += buf[i];
    return VMX_OK;
}
