#include "../common/types.h"
#include "../common/alloc.h"

int vmx_checkpoint_begin(vmx_session_t *sess, uint32_t span_id,
                         uint32_t ckpt_id) {
    if (!sess) return VMX_ERR_INVALID_STATE;
    if (!(sess->capabilities & VMX_CAP_CHECKPOINT)) return VMX_ERR_INVALID_STATE;
    if (sess->checkpoint_count >= VMX_MAX_CHECKPOINTS) return VMX_ERR_FULL;

    vmx_span_t *span = vmx_span_registry_lookup(sess, span_id);
    if (!span) return VMX_ERR_NOT_FOUND;

    vmx_checkpoint_t *ckpt = vmx_calloc(1, sizeof(vmx_checkpoint_t));
    if (!ckpt) return VMX_ERR_NOMEM;

    ckpt->checkpoint_id = ckpt_id;
    ckpt->span_id = span_id;
    ckpt->phase = VMX_CKPT_IN_PROGRESS;
    ckpt->epoch_started = sess->epoch;
    ckpt->generation_at_start = span->generation;
    ckpt->active = true;

    /* Snapshot segment pointers for this checkpoint. */
    for (uint16_t i = 0; i < span->segment_count && i < VMX_MAX_SEGMENTS; i++) {
        ckpt->snap_segments[i] = span->segments[i];
    }
    ckpt->snap_segment_count = span->segment_count;

    sess->checkpoints[sess->checkpoint_count++] = ckpt;
    return VMX_OK;
}

int vmx_checkpoint_register_listener(vmx_session_t *sess, vmx_checkpoint_t *ckpt,
                                     uint32_t lease_id) {
    if (!sess || !ckpt) return VMX_ERR_INVALID_STATE;

    /* Cache lease_id in listener struct.
     * caches a lease_id it doesn't own. */
    ckpt->listener_lease_id = lease_id;
    ckpt->listener_active = true;

    return VMX_OK;
}

int vmx_checkpoint_cleanup_listener(vmx_session_t *sess, vmx_checkpoint_t *ckpt) {
    if (!sess || !ckpt) return VMX_ERR_INVALID_STATE;
    if (!ckpt->listener_active) return VMX_OK;

    /* Release the cached lease.
     * BUG: this lease may already be released. */
    if (ckpt->listener_lease_id != 0) {
        vmx_lease_t *l = vmx_lease_lookup(sess, ckpt->listener_lease_id);
        if (l) {
            /* Directly decrement span refcount for the cached lease.
             * Does not check lease ownership or active state.
             * If the lease was already released by its owner, this
             * causes a phantom refcount decrement. */
            vmx_span_t *span = vmx_span_registry_lookup(sess, l->span_id);
            if (span && l->active) {
                vmx_span_lifecycle_decref(sess, span);
            }
        }
    }

    ckpt->listener_active = false;
    ckpt->listener_lease_id = 0;

    return VMX_OK;
}

vmx_checkpoint_t *vmx_checkpoint_lookup(vmx_session_t *sess, uint32_t ckpt_id) {
    if (!sess) return NULL;
    for (uint8_t i = 0; i < sess->checkpoint_count; i++) {
        if (sess->checkpoints[i] &&
            sess->checkpoints[i]->checkpoint_id == ckpt_id)
            return sess->checkpoints[i];
    }
    return NULL;
}

int vmx_checkpoint_handle_begin(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    if (!sess || !f) return VMX_ERR_INVALID_FRAME;
    uint32_t span_id = f->route_id;
    uint32_t ckpt_id = 0;
    if (f->parsed_subrecord_count > 0 && f->subrecords[0].payload_len >= 4)
        ckpt_id = vmx_read_u32(f->subrecords[0].payload);
    if (ckpt_id == 0) ckpt_id = sess->checkpoint_count + 1;

    int rc = vmx_checkpoint_begin(sess, span_id, ckpt_id);
    if (rc != VMX_OK) return rc;

    /* Register listener with current lease if any */
    vmx_lease_t *l = vmx_lease_find_by_span(sess, span_id);
    if (l) {
        vmx_checkpoint_t *ckpt = vmx_checkpoint_lookup(sess, ckpt_id);
        if (ckpt)
            vmx_checkpoint_register_listener(sess, ckpt, l->lease_id);
    }

    return VMX_OK;
}

int vmx_checkpoint_handle_flush(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    if (!sess || !f) return VMX_ERR_INVALID_FRAME;
    uint32_t ckpt_id = 0;
    if (f->parsed_subrecord_count > 0 && f->subrecords[0].payload_len >= 4)
        ckpt_id = vmx_read_u32(f->subrecords[0].payload);
    vmx_checkpoint_t *ckpt = vmx_checkpoint_lookup(sess, ckpt_id);
    if (!ckpt) return VMX_ERR_NOT_FOUND;

    int rc = vmx_checkpoint_flush(sess, ckpt);
    if (rc != VMX_OK) return rc;

    /* Cleanup listener on completion */
    vmx_checkpoint_cleanup_listener(sess, ckpt);
    return VMX_OK;
}

int vmx_checkpoint_handle_restore(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    if (!sess || !f) return VMX_ERR_INVALID_FRAME;
    uint32_t ckpt_id = 0;
    if (f->parsed_subrecord_count > 0 && f->subrecords[0].payload_len >= 4)
        ckpt_id = vmx_read_u32(f->subrecords[0].payload);
    vmx_checkpoint_t *ckpt = vmx_checkpoint_lookup(sess, ckpt_id);
    if (!ckpt) return VMX_ERR_NOT_FOUND;
    return vmx_checkpoint_restore(sess, ckpt);
}
