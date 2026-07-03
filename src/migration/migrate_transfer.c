#include "../common/types.h"
#include "../common/alloc.h"

/*
 * Migration source preparation: reallocates segments to prepare
 * for data transfer to target node.
 *
 * calls vmx_segment_realloc which frees the old segment data buffer.
 * Any cursor that cached a pointer to the old data now holds a
 * dangling pointer.
 */
int vmx_migrate_prepare_source(vmx_session_t *sess, vmx_migration_t *mig) {
    if (!sess || !mig) return VMX_ERR_INVALID_STATE;
    if (mig->phase != VMX_MIGRATE_INITIATED) return VMX_ERR_INVALID_STATE;

    vmx_span_t *span = vmx_span_registry_lookup(sess, mig->span_id);
    if (!span) return VMX_ERR_NOT_FOUND;

    if (!mig->transfer_buf) {
        mig->transfer_buf = vmx_alloc(4096);
        mig->transfer_buf_len = 4096;
    }
    mig->phase = VMX_MIGRATE_PREPARING;

    /* Prepare segments for transfer by reallocating with transfer headers.
     * This adds a migration header prefix to each segment's data buffer,
     * which requires reallocation.
     *
     * Cursors that cached pointers to the old buffer are now dangling. */
    for (uint16_t i = 0; i < span->segment_count; i++) {
        vmx_segment_t *seg = span->segments[i];
        if (!seg || seg->state != VMX_LIFECYCLE_ACTIVE) continue;

        /* Reallocate to add migration header space */
        size_t new_cap = seg->capacity + 64;  /* 64-byte migration header */
        if (new_cap > VMX_MAX_SEGMENT_SIZE) new_cap = VMX_MAX_SEGMENT_SIZE;

        /* This frees the old data and allocates new buffer */
        int rc = vmx_segment_realloc(sess, seg, new_cap);
        if (rc != VMX_OK) {
            mig->phase = VMX_MIGRATE_ABORTED;
            return rc;
        }
    }

    mig->phase = VMX_MIGRATE_PREPARING;
    return VMX_OK;
}

int vmx_migrate_begin_transfer(vmx_session_t *sess, vmx_migration_t *mig) {
    if (!sess || !mig) return VMX_ERR_INVALID_STATE;
    if (mig->phase != VMX_MIGRATE_PREPARING) return VMX_ERR_INVALID_STATE;

    mig->phase = VMX_MIGRATE_TRANSFERRING;
    mig->bytes_transferred = 0;

    return VMX_OK;
}

int vmx_migrate_handle_prepare(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    if (!sess || !f) return VMX_ERR_INVALID_FRAME;

    uint32_t span_id = f->route_id;
    vmx_migration_t *mig = vmx_migrate_find_by_span(sess, span_id);
    if (!mig) return VMX_ERR_NOT_FOUND;

    return vmx_migrate_prepare_source(sess, mig);
}

int vmx_migrate_handle_transfer(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    if (!sess || !f) return VMX_ERR_INVALID_FRAME;

    uint32_t span_id = f->route_id;
    vmx_migration_t *mig = vmx_migrate_find_by_span(sess, span_id);
    if (!mig) return VMX_ERR_NOT_FOUND;

    if (mig->phase == VMX_MIGRATE_PREPARING) {
        int rc = vmx_migrate_begin_transfer(sess, mig);
        if (rc != VMX_OK) return rc;
    }

    if (mig->phase != VMX_MIGRATE_TRANSFERRING) return VMX_ERR_INVALID_STATE;

    /* Simulate transfer progress */
    for (uint8_t i = 0; i < f->parsed_subrecord_count; i++) {
        const vmx_subrecord_t *sub = &f->subrecords[i];
        if (sub->kind == VMX_SUB_DATA) {
            mig->bytes_transferred += sub->payload_len;
        }
    }

    return VMX_OK;
}
