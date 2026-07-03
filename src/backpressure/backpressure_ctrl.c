#include "../common/types.h"
#include "../common/alloc.h"

int vmx_backpressure_add_partition(vmx_session_t *sess, uint32_t partition_id,
                                  int32_t initial_credits, int32_t max_credits) {
    if (!sess) return VMX_ERR_INVALID_STATE;
    if (sess->bp_entry_count >= VMX_MAX_PARTITIONS) return VMX_ERR_FULL;

    vmx_bp_partition_entry_t *entry = vmx_calloc(1, sizeof(vmx_bp_partition_entry_t));
    if (!entry) return VMX_ERR_NOMEM;

    entry->partition_id = partition_id;
    entry->credits = initial_credits;
    entry->max_credits = max_credits > 0 ? max_credits : 100;
    entry->window_size = 64;
    entry->active = true;

    sess->bp_entries[sess->bp_entry_count++] = entry;
    return VMX_OK;
}

int vmx_backpressure_remove_partition(vmx_session_t *sess, uint32_t partition_id) {
    if (!sess) return VMX_ERR_INVALID_STATE;
    for (uint8_t i = 0; i < sess->bp_entry_count; i++) {
        if (sess->bp_entries[i] &&
            sess->bp_entries[i]->partition_id == partition_id) {
            vmx_free(sess->bp_entries[i]);
            sess->bp_entries[i] = NULL;
            return VMX_OK;
        }
    }
    return VMX_ERR_NOT_FOUND;
}

int vmx_backpressure_register_callback(vmx_session_t *sess, uint32_t partition_id,
                                       vmx_bp_callback_fn fn, void *ctx) {
    if (!sess) return VMX_ERR_INVALID_STATE;
    for (uint8_t i = 0; i < sess->bp_entry_count; i++) {
        if (sess->bp_entries[i] &&
            sess->bp_entries[i]->partition_id == partition_id) {
            sess->bp_entries[i]->callback = fn;
            sess->bp_entries[i]->callback_ctx = ctx;
            return VMX_OK;
        }
    }
    return VMX_ERR_NOT_FOUND;
}
