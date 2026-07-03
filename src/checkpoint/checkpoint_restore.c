#include "../common/types.h"

int vmx_checkpoint_restore(vmx_session_t *sess, vmx_checkpoint_t *ckpt) {
    if (!sess || !ckpt) return VMX_ERR_INVALID_STATE;

    ckpt->phase = VMX_CKPT_RESTORING;

    vmx_span_t *span = vmx_span_registry_lookup(sess, ckpt->span_id);
    if (!span) return VMX_ERR_NOT_FOUND;

    /* Restore segment state from snapshot */
    for (uint16_t i = 0; i < ckpt->snap_segment_count; i++) {
        vmx_segment_t *seg = ckpt->snap_segments[i];
        if (seg && seg->state != VMX_LIFECYCLE_FREED) {
            seg->generation = ckpt->generation_at_start;
        }
    }

    span->generation = ckpt->generation_at_start;
    ckpt->phase = VMX_CKPT_COMPLETE;
    return VMX_OK;
}
