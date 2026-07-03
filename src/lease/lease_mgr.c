#include "../common/types.h"
#include "../common/alloc.h"

static uint32_t next_lease_id = 1;

vmx_lease_t *vmx_lease_acquire(vmx_session_t *sess, uint32_t span_id,
                               uint32_t owner_token) {
    if (!sess) return NULL;
    if (sess->lease_count >= VMX_MAX_LEASES) return NULL;

    vmx_span_t *span = vmx_span_registry_lookup(sess, span_id);
    if (!span) return NULL;

    vmx_lease_t *lease = vmx_calloc(1, sizeof(vmx_lease_t));
    if (!lease) return NULL;

    lease->lease_id = next_lease_id++;
    lease->span_id = span_id;
    lease->owner_token = owner_token;
    lease->epoch_acquired = sess->epoch;
    lease->generation = span->generation;
    lease->renewal_count = 0;
    lease->active = true;
    lease->force_released = false;

    /* Increment span refcount for lease */
    vmx_span_lifecycle_incref(span);

    sess->leases[sess->lease_count++] = lease;
    return lease;
}

int vmx_lease_release(vmx_session_t *sess, vmx_lease_t *lease) {
    if (!sess || !lease) return VMX_ERR_INVALID_LEASE;
    if (!lease->active) return VMX_ERR_INVALID_STATE;

    lease->active = false;

    /* Decrement span refcount */
    vmx_span_t *span = vmx_span_registry_lookup(sess, lease->span_id);
    if (span) {
        return vmx_span_lifecycle_decref(sess, span);
    }
    return VMX_OK;
}

int vmx_lease_renew(vmx_session_t *sess, vmx_lease_t *lease) {
    if (!sess || !lease) return VMX_ERR_INVALID_LEASE;
    if (!lease->active) return VMX_ERR_INVALID_STATE;

    vmx_span_t *span = vmx_span_registry_lookup(sess, lease->span_id);
    if (!span) return VMX_ERR_NOT_FOUND;

    /* If this lease was coalesced, verify through cached span.
     * BUG: cached_coalesce_span may be dangling if span was
     * destroyed after coalescing. */
    if (lease->cached_coalesce_span) {
        volatile uint32_t gen = lease->cached_coalesce_span->generation;
        (void)gen;
    }
    lease->renewal_count++;
    lease->generation = span->generation;

    /* Every 3 renewals, bump span generation which can trigger compaction. */
    if (lease->renewal_count % 3 == 0) {
        vmx_span_lifecycle_bump_generation(span);
        /* Trigger compaction if span has enough segments */
        if (span->segment_count >= 2) {
            vmx_segment_compact(sess, span);
        }
    }

    return VMX_OK;
}

int vmx_lease_force_release(vmx_session_t *sess, vmx_lease_t *lease) {
    if (!sess || !lease) return VMX_ERR_INVALID_LEASE;
    if (!lease->active) return VMX_OK;  /* already released */

    lease->force_released = true;
    return vmx_lease_release(sess, lease);
}

vmx_lease_t *vmx_lease_lookup(vmx_session_t *sess, uint32_t lease_id) {
    if (!sess) return NULL;
    for (uint16_t i = 0; i < sess->lease_count; i++) {
        if (sess->leases[i] && sess->leases[i]->lease_id == lease_id)
            return sess->leases[i];
    }
    return NULL;
}

vmx_lease_t *vmx_lease_find_by_span(vmx_session_t *sess, uint32_t span_id) {
    if (!sess) return NULL;
    for (uint16_t i = 0; i < sess->lease_count; i++) {
        if (sess->leases[i] && sess->leases[i]->span_id == span_id &&
            sess->leases[i]->active)
            return sess->leases[i];
    }
    return NULL;
}

int vmx_lease_handle_acquire(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    if (!sess || !f) return VMX_ERR_INVALID_FRAME;
    if (!sess->negotiated) return VMX_ERR_INVALID_STATE;

    uint32_t span_id = f->route_id;
    uint32_t owner = f->owner_token;

    vmx_lease_t *lease = vmx_lease_acquire(sess, span_id, owner);
    if (!lease) return VMX_ERR_NOMEM;

    return VMX_OK;
}

int vmx_lease_handle_release(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    if (!sess || !f) return VMX_ERR_INVALID_FRAME;

    /* Find lease by owner token and span */
    uint32_t span_id = f->route_id;
    for (uint16_t i = 0; i < sess->lease_count; i++) {
        vmx_lease_t *l = sess->leases[i];
        if (l && l->active && l->span_id == span_id &&
            l->owner_token == f->owner_token) {
            return vmx_lease_release(sess, l);
        }
    }
    return VMX_ERR_NOT_FOUND;
}

int vmx_lease_handle_renew(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    if (!sess || !f) return VMX_ERR_INVALID_FRAME;

    uint32_t span_id = f->route_id;
    vmx_lease_t *lease = vmx_lease_find_by_span(sess, span_id);
    if (!lease) return VMX_ERR_NOT_FOUND;

    if (f->parsed_subrecord_count > 0) {
        const vmx_subrecord_t *sub = &f->subrecords[0];
        if (sub->lease_token != lease->lease_id)
            return VMX_ERR_INVALID_TOKEN;
    }

    return vmx_lease_renew(sess, lease);
}
