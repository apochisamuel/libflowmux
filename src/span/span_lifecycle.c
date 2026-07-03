#include "../common/types.h"
#include "../common/alloc.h"

int vmx_span_lifecycle_incref(vmx_span_t *span) {
    if (!span) return VMX_ERR_INVALID_STATE;
    if (span->state == VMX_LIFECYCLE_FREED) return VMX_ERR_INVALID_STATE;
    span->refcount++;
    return VMX_OK;
}

int vmx_span_lifecycle_decref(vmx_session_t *sess, vmx_span_t *span) {
    if (!sess || !span) return VMX_ERR_INVALID_STATE;
    span->refcount--;
    if (span->refcount <= 0) {
        /* Free all segments owned by this span */
        vmx_segment_gc_span(sess, span);
        /* Destroy span itself */
        return vmx_span_registry_destroy(sess, span);
    }
    return VMX_OK;
}

int vmx_span_lifecycle_bump_generation(vmx_span_t *span) {
    if (!span) return VMX_ERR_INVALID_STATE;
    span->generation++;
    return VMX_OK;
}

int vmx_span_lifecycle_set_state(vmx_span_t *span, vmx_lifecycle_state_t st) {
    if (!span) return VMX_ERR_INVALID_STATE;
    span->state = st;
    return VMX_OK;
}

int vmx_span_lifecycle_deferred_destroy(vmx_session_t *sess, vmx_span_t *span) {
    if (!sess || !span) return VMX_ERR_INVALID_STATE;
    if (sess->deferred_delete_count >= VMX_MAX_SPANS)
        return VMX_ERR_FULL;

    span->state = VMX_LIFECYCLE_DEFERRED;
    sess->deferred_delete_list[sess->deferred_delete_count++] = span;
    return VMX_OK;
}

int vmx_span_handle_create(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    if (!sess || !f) return VMX_ERR_INVALID_FRAME;
    if (!sess->initialized || !sess->negotiated)
        return VMX_ERR_INVALID_STATE;

    uint32_t span_id = f->route_id;  /* span_id encoded in route_id field */
    uint32_t owner = f->owner_token;

    if (span_id == 0) return VMX_ERR_INVALID_ID;

    vmx_span_t *span = vmx_span_registry_create(sess, span_id, owner);
    if (!span) return VMX_ERR_NOMEM;

    /* Process subrecords for initial metadata */
    for (uint8_t i = 0; i < f->parsed_subrecord_count; i++) {
        const vmx_subrecord_t *sub = &f->subrecords[i];
        if (sub->kind == VMX_SUB_META && sub->payload_len >= 8) {
            span->partition_id = vmx_read_u32(sub->payload);
            span->route_id = vmx_read_u32(sub->payload + 4);
        }
    }

    return VMX_OK;
}

int vmx_span_handle_close(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    if (!sess || !f) return VMX_ERR_INVALID_FRAME;

    uint32_t span_id = f->route_id;
    vmx_span_t *span = vmx_span_registry_lookup(sess, span_id);
    if (!span) return VMX_ERR_NOT_FOUND;
    if (span->owner_token != f->owner_token)
        return VMX_ERR_PERMISSION;

    /* Invalidate all cursors on this span (correct behavior on explicit close) */
    vmx_cursor_invalidate_for_span(sess, span_id);

    span->state = VMX_LIFECYCLE_CLOSING;
    return vmx_span_lifecycle_decref(sess, span);
}

int vmx_span_handle_meta_update(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    if (!sess || !f) return VMX_ERR_INVALID_FRAME;

    uint32_t span_id = f->route_id;
    vmx_span_t *span = vmx_span_registry_lookup(sess, span_id);
    if (!span) return VMX_ERR_NOT_FOUND;

    for (uint8_t i = 0; i < f->parsed_subrecord_count; i++) {
        const vmx_subrecord_t *sub = &f->subrecords[i];
        if (sub->kind == VMX_SUB_META && sub->payload_len >= 8) {
            span->retention_watermark = vmx_read_u64(sub->payload);
        }
        if (sub->kind == VMX_SUB_GEN_STAMP) {
            vmx_span_lifecycle_bump_generation(span);
        }
    }

    return VMX_OK;
}
