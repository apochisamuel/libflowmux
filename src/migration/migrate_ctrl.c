#include "../common/types.h"
#include "../common/alloc.h"

static uint32_t next_migration_id = 1;

int vmx_migrate_initiate(vmx_session_t *sess, uint32_t span_id,
                         uint32_t target_node) {
    if (!sess) return VMX_ERR_INVALID_STATE;
    if (!(sess->capabilities & VMX_CAP_MIGRATION)) return VMX_ERR_INVALID_STATE;
    if (sess->migration_count >= VMX_MAX_MIGRATIONS) return VMX_ERR_FULL;

    vmx_span_t *span = vmx_span_registry_lookup(sess, span_id);
    if (!span) return VMX_ERR_NOT_FOUND;

    vmx_migration_t *mig = vmx_calloc(1, sizeof(vmx_migration_t));
    if (!mig) return VMX_ERR_NOMEM;

    mig->migration_id = next_migration_id++;
    mig->span_id = span_id;
    mig->source_node_id = 0;  /* local */
    mig->target_node_id = target_node;
    mig->phase = VMX_MIGRATE_INITIATED;
    mig->epoch_started = sess->epoch;
    mig->owner_token = span->owner_token;
    mig->active = true;
    mig->total_bytes = span->total_bytes;

    span->migration_id = mig->migration_id;
    span->migration_source = true;
    vmx_span_lifecycle_set_state(span, VMX_LIFECYCLE_MIGRATING);

    sess->migrations[sess->migration_count++] = mig;
    return VMX_OK;
}

int vmx_migrate_abort(vmx_session_t *sess, vmx_migration_t *mig) {
    if (!sess || !mig) return VMX_ERR_INVALID_STATE;

    mig->phase = VMX_MIGRATE_ABORTED;
    mig->active = false;

    vmx_span_t *span = vmx_span_registry_lookup(sess, mig->span_id);
    if (span) {
        span->migration_source = false;
        vmx_span_lifecycle_set_state(span, VMX_LIFECYCLE_ACTIVE);
    }

    return VMX_OK;
}

vmx_migration_t *vmx_migrate_lookup(vmx_session_t *sess, uint32_t migration_id) {
    if (!sess) return NULL;
    for (uint8_t i = 0; i < sess->migration_count; i++) {
        if (sess->migrations[i] &&
            sess->migrations[i]->migration_id == migration_id)
            return sess->migrations[i];
    }
    return NULL;
}

vmx_migration_t *vmx_migrate_find_by_span(vmx_session_t *sess, uint32_t span_id) {
    if (!sess) return NULL;
    for (uint8_t i = 0; i < sess->migration_count; i++) {
        if (sess->migrations[i] && sess->migrations[i]->span_id == span_id &&
            sess->migrations[i]->active)
            return sess->migrations[i];
    }
    return NULL;
}

int vmx_migrate_handle_initiate(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    if (!sess || !f) return VMX_ERR_INVALID_FRAME;
    if (!sess->negotiated) return VMX_ERR_INVALID_STATE;

    uint32_t span_id = f->route_id;
    uint32_t target_node = 0;

    if (f->parsed_subrecord_count > 0) {
        const vmx_subrecord_t *sub = &f->subrecords[0];
        if (sub->kind == VMX_SUB_MIGRATION_STATE && sub->payload_len >= 4) {
            target_node = vmx_read_u32(sub->payload);
        }
    }

    return vmx_migrate_initiate(sess, span_id, target_node);
}

int vmx_migrate_handle_abort(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    if (!sess || !f) return VMX_ERR_INVALID_FRAME;

    uint32_t span_id = f->route_id;
    vmx_migration_t *mig = vmx_migrate_find_by_span(sess, span_id);
    if (!mig) return VMX_ERR_NOT_FOUND;

    return vmx_migrate_abort(sess, mig);
}
