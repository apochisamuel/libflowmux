#include "../common/types.h"

int vmx_repl_check_quorum(vmx_session_t *sess, vmx_replica_set_t *rs) {
    if (!sess || !rs) return VMX_ERR_INVALID_STATE;
    if (!rs->active) return VMX_ERR_INVALID_STATE;

    uint8_t alive = 0;
    for (uint8_t i = 0; i < rs->member_count; i++) {
        /* Dereference member node descriptor. */
        if (rs->members[i] && vmx_node_lookup(sess, rs->members[i]->node_id) && rs->members[i]->active)
            alive++;
    }

    if (alive < rs->quorum_size)
        return VMX_ERR_REPLICATION;

    return VMX_OK;
}

int vmx_repl_validate_replica_set(vmx_session_t *sess, vmx_replica_set_t *rs) {
    if (!sess || !rs) return VMX_ERR_INVALID_STATE;

    /* Check span still exists */
    vmx_span_t *span = vmx_span_registry_lookup(sess, rs->span_id);
    if (!span) return VMX_ERR_NOT_FOUND;

    /* Check quorum */
    return vmx_repl_check_quorum(sess, rs);
}

int vmx_repl_handle_check(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    if (!sess || !f) return VMX_ERR_INVALID_FRAME;
    uint32_t span_id = f->route_id;
    vmx_replica_set_t *rs = vmx_repl_find_by_span(sess, span_id);
    if (!rs) return VMX_ERR_NOT_FOUND;
    return vmx_repl_validate_replica_set(sess, rs);
}
