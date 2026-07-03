#include "../common/types.h"

int vmx_backpressure_check_window(vmx_session_t *sess, uint32_t partition_id,
                                  bool *allowed) {
    if (!sess || !allowed) return VMX_ERR_INVALID_STATE;
    *allowed = false;

    for (uint8_t i = 0; i < sess->bp_entry_count; i++) {
        vmx_bp_partition_entry_t *e = sess->bp_entries[i];
        if (!e || e->partition_id != partition_id) continue;
        if (!e->active) continue;
        *allowed = (e->credits > 0);
        return VMX_OK;
    }
    return VMX_ERR_NOT_FOUND;
}

int vmx_backpressure_get_credit_balance(vmx_session_t *sess, uint32_t partition_id,
                                        int32_t *balance) {
    if (!sess || !balance) return VMX_ERR_INVALID_STATE;
    for (uint8_t i = 0; i < sess->bp_entry_count; i++) {
        vmx_bp_partition_entry_t *e = sess->bp_entries[i];
        if (e && e->partition_id == partition_id && e->active) {
            *balance = e->credits;
            return VMX_OK;
        }
    }
    return VMX_ERR_NOT_FOUND;
}
