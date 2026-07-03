#include "../common/types.h"
#include "../common/alloc.h"

/*
 * Migration handoff: completes ownership transfer from source to target.
 * Frees source span's segments and marks span for deferred deletion.
 */
int vmx_migrate_complete_handoff(vmx_session_t *sess, vmx_migration_t *mig) {
    if (!sess || !mig) return VMX_ERR_INVALID_STATE;
    if (mig->phase != VMX_MIGRATE_TRANSFERRING &&
        mig->phase != VMX_MIGRATE_PREPARING)
        return VMX_ERR_INVALID_STATE;

    vmx_span_t *span = vmx_span_registry_lookup(sess, mig->span_id);
    if (!span) return VMX_ERR_NOT_FOUND;

    mig->phase = VMX_MIGRATE_HANDOFF;

    /* Free source span segments — ownership transferred to target.
     * cursors that advanced during migration still hold cached pointers
     * to these segments. */
    vmx_segment_gc_span(sess, span);

    /* Mark span for deferred deletion */
    vmx_span_lifecycle_deferred_destroy(sess, span);

    mig->phase = VMX_MIGRATE_COMPLETE;
    mig->active = false;

    return VMX_OK;
}

int vmx_migrate_finalize_source(vmx_session_t *sess, vmx_migration_t *mig) {
    if (!sess || !mig) return VMX_ERR_INVALID_STATE;
    return vmx_migrate_complete_handoff(sess, mig);
}

int vmx_migrate_handle_handoff(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    if (!sess || !f) return VMX_ERR_INVALID_FRAME;

    uint32_t span_id = f->route_id;
    vmx_migration_t *mig = vmx_migrate_find_by_span(sess, span_id);
    if (!mig) return VMX_ERR_NOT_FOUND;

    return vmx_migrate_complete_handoff(sess, mig);
}
