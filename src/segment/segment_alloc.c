#include "../common/types.h"
#include "../common/alloc.h"

static uint32_t next_segment_id = 1;

vmx_segment_t *vmx_segment_alloc(vmx_session_t *sess, uint32_t span_id,
                                 size_t capacity) {
    if (!sess) return NULL;
    if (sess->segment_count >= VMX_MAX_SEGMENTS) return NULL;
    if (capacity == 0 || capacity > VMX_MAX_SEGMENT_SIZE) return NULL;

    vmx_span_t *span = vmx_span_registry_lookup(sess, span_id);
    if (!span) return NULL;

    vmx_segment_t *seg = vmx_calloc(1, sizeof(vmx_segment_t));
    if (!seg) return NULL;

    seg->data = vmx_alloc(capacity);
    if (!seg->data) {
        vmx_free(seg);
        return NULL;
    }

    seg->segment_id = next_segment_id++;
    seg->span_id = span_id;
    seg->generation = span->generation;
    seg->capacity = capacity;
    seg->used = 0;
    seg->state = VMX_LIFECYCLE_ACTIVE;
    seg->compacted = false;

    /* Register in span */
    if (span->segment_count < VMX_MAX_SEGMENTS) {
        span->segments[span->segment_count++] = seg;
    }

    /* Register in session */
    sess->segments[sess->segment_count++] = seg;

    return seg;
}

int vmx_segment_free(vmx_session_t *sess, vmx_segment_t *seg) {
    if (!sess || !seg) return VMX_ERR_INVALID_STATE;

    seg->state = VMX_LIFECYCLE_FREED;
    vmx_free(seg->data);
    seg->data = NULL;
    seg->capacity = 0;
    seg->used = 0;

    /* Remove from session registry */
    for (uint16_t i = 0; i < sess->segment_count; i++) {
        if (sess->segments[i] == seg) {
            sess->segments[i] = NULL;
            break;
        }
    }

    /* Don't free the struct yet — callers may still reference it
     * for cleanup. Mark as freed and let GC collect. */
    return VMX_OK;
}

/*
 * Reallocate segment data buffer.
 * This frees the old data and allocates a new buffer.
 * become dangling after this call.
 */
int vmx_segment_realloc(vmx_session_t *sess, vmx_segment_t *seg,
                        size_t new_capacity) {
    if (!sess || !seg) return VMX_ERR_INVALID_STATE;
    if (seg->state != VMX_LIFECYCLE_ACTIVE) return VMX_ERR_INVALID_STATE;
    if (new_capacity == 0 || new_capacity > VMX_MAX_SEGMENT_SIZE)
        return VMX_ERR_INVALID_FRAME;

    /* Allocate new buffer */
    uint8_t *new_data = vmx_alloc(new_capacity);
    if (!new_data) return VMX_ERR_NOMEM;

    /* Copy existing data up to min of used and new_capacity */
    size_t copy_len = seg->used;
    if (copy_len > new_capacity) copy_len = new_capacity;
    if (copy_len > 0)
        memcpy(new_data, seg->data, copy_len);

    /* Free old data — any cached pointers to seg->data are now dangling */
    vmx_free(seg->data);

    seg->data = new_data;
    seg->capacity = new_capacity;
    if (seg->used > new_capacity) seg->used = new_capacity;
    seg->generation++;

    return VMX_OK;
}

int vmx_segment_write(vmx_segment_t *seg, const uint8_t *data, size_t len) {
    if (!seg || !data) return VMX_ERR_INVALID_STATE;
    if (seg->state != VMX_LIFECYCLE_ACTIVE) return VMX_ERR_INVALID_STATE;
    if (seg->used + len > seg->capacity) return VMX_ERR_OVERFLOW;

    memcpy(seg->data + seg->used, data, len);
    seg->used += len;
    return VMX_OK;
}

int vmx_segment_handle_attach(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    if (!sess || !f) return VMX_ERR_INVALID_FRAME;
    if (!sess->negotiated) return VMX_ERR_INVALID_STATE;

    uint32_t span_id = f->route_id;
    vmx_span_t *span = vmx_span_registry_lookup(sess, span_id);
    if (!span) return VMX_ERR_NOT_FOUND;

    /* Extract capacity from subrecord */
    size_t capacity = 4096;  /* default */
    if (f->parsed_subrecord_count > 0) {
        const vmx_subrecord_t *sub = &f->subrecords[0];
        if (sub->kind == VMX_SUB_SEGMENT_REF && sub->payload_len >= 4) {
            uint32_t req_cap = vmx_read_u32(sub->payload);
            if (req_cap > 0 && req_cap <= VMX_MAX_SEGMENT_SIZE)
                capacity = req_cap;
        }
    }

    vmx_segment_t *seg = vmx_segment_alloc(sess, span_id, capacity);
    if (!seg) return VMX_ERR_NOMEM;

    /* Write initial data from subrecord payloads */
    for (uint8_t i = 0; i < f->parsed_subrecord_count; i++) {
        const vmx_subrecord_t *sub = &f->subrecords[i];
        if (sub->kind == VMX_SUB_DATA && sub->payload_len > 0) {
            vmx_segment_write(seg, sub->payload, sub->payload_len);
        }
    }

    return VMX_OK;
}

int vmx_segment_handle_detach(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    if (!sess || !f) return VMX_ERR_INVALID_FRAME;

    uint32_t span_id = f->route_id;
    vmx_span_t *span = vmx_span_registry_lookup(sess, span_id);
    if (!span) return VMX_ERR_NOT_FOUND;

    /* Find segment by index from subrecord */
    if (f->parsed_subrecord_count > 0) {
        const vmx_subrecord_t *sub = &f->subrecords[0];
        if (sub->kind == VMX_SUB_SEGMENT_REF && sub->payload_len >= 4) {
            uint32_t seg_idx = vmx_read_u32(sub->payload);
            if (seg_idx < span->segment_count && span->segments[seg_idx]) {
                vmx_segment_free(sess, span->segments[seg_idx]);
                span->segments[seg_idx] = NULL;
            }
        }
    }

    return VMX_OK;
}
