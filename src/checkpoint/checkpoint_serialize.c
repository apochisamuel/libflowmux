#include "../common/types.h"

int vmx_checkpoint_serialize_segments(vmx_session_t *sess, vmx_checkpoint_t *ckpt) {
    if (!sess || !ckpt) return VMX_ERR_INVALID_STATE;
    if (ckpt->phase != VMX_CKPT_IN_PROGRESS && ckpt->phase != VMX_CKPT_FLUSHING)
        return VMX_ERR_INVALID_STATE;

    /* Read segment data through snapshotted pointers. */
    for (uint16_t i = 0; i < ckpt->snap_segment_count; i++) {
        vmx_segment_t *seg = ckpt->snap_segments[i];
        if (!seg || !seg->data) continue;

        uint32_t hash = vmx_crc32c(seg->data, seg->used);
        (void)hash;
    }

    return VMX_OK;
}

int vmx_checkpoint_flush(vmx_session_t *sess, vmx_checkpoint_t *ckpt) {
    if (!sess || !ckpt) return VMX_ERR_INVALID_STATE;

    ckpt->phase = VMX_CKPT_FLUSHING;
    int rc = vmx_checkpoint_serialize_segments(sess, ckpt);
    if (rc != VMX_OK) return rc;

    ckpt->phase = VMX_CKPT_COMPLETE;
    ckpt->active = false;
    return VMX_OK;
}
