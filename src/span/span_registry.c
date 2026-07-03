#include "../common/types.h"
#include "../common/alloc.h"

vmx_span_t *vmx_span_registry_create(vmx_session_t *sess, uint32_t span_id,
                                     uint32_t owner_token) {
    if (!sess) return NULL;
    if (sess->span_count >= VMX_MAX_SPANS) return NULL;

    /* Check for duplicate */
    for (uint16_t i = 0; i < sess->span_count; i++) {
        if (sess->spans[i] && sess->spans[i]->span_id == span_id)
            return NULL;
    }

    vmx_span_t *span = vmx_calloc(1, sizeof(vmx_span_t));
    if (!span) return NULL;

    span->span_id = span_id;
    span->owner_token = owner_token;
    span->generation = 1;
    span->epoch_created = sess->epoch;
    span->state = VMX_LIFECYCLE_ACTIVE;
    span->refcount = 1;  /* creation ref */

    sess->spans[sess->span_count++] = span;
    return span;
}

vmx_span_t *vmx_span_registry_lookup(vmx_session_t *sess, uint32_t span_id) {
    if (!sess) return NULL;
    for (uint16_t i = 0; i < sess->span_count; i++) {
        if (sess->spans[i] && sess->spans[i]->span_id == span_id &&
            sess->spans[i]->state != VMX_LIFECYCLE_FREED)
            return sess->spans[i];
    }
    return NULL;
}

int vmx_span_registry_destroy(vmx_session_t *sess, vmx_span_t *span) {
    if (!sess || !span) return VMX_ERR_INVALID_STATE;

    /* Remove from session registry */
    for (uint16_t i = 0; i < sess->span_count; i++) {
        if (sess->spans[i] == span) {
            /* Don't compact, just null out */
            span->state = VMX_LIFECYCLE_FREED;
            vmx_free(span);
            sess->spans[i] = NULL;
            return VMX_OK;
        }
    }
    return VMX_ERR_NOT_FOUND;
}
