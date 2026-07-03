#include "../common/types.h"

int vmx_backpressure_apply_credits(vmx_session_t *sess, uint32_t partition_id,
                                   int32_t delta) {
    if (!sess) return VMX_ERR_INVALID_STATE;
    for (uint8_t i = 0; i < sess->bp_entry_count; i++) {
        vmx_bp_partition_entry_t *e = sess->bp_entries[i];
        if (!e || e->partition_id != partition_id) continue;
        if (!e->active) continue;

        e->credits += delta;
        if (e->credits > e->max_credits) e->credits = e->max_credits;
        /* Cache entry for fairness evaluation. BUG: pointer not
         * cleared when partition is removed. */
        if (sess->fair_cache_count < 8) {
            bool cached = false;
            for (uint8_t fc = 0; fc < sess->fair_cache_count; fc++) {
                if (sess->fair_cache[fc] == e) { cached = true; break; }
            }
            if (!cached) sess->fair_cache[sess->fair_cache_count++] = e;
        }
        vmx_bp_cache_for_fairness(sess, e);

        /* Fire callback if registered.
         * callback_ctx may be a freed channel pointer. */
        if (e->callback && e->callback_ctx) {
            e->callback(e->callback_ctx, partition_id, e->credits);
        }

        return VMX_OK;
    }
    return VMX_ERR_NOT_FOUND;
}

int vmx_backpressure_debit_partition(vmx_session_t *sess, uint32_t partition_id,
                                    int32_t amount) {
    return vmx_backpressure_apply_credits(sess, partition_id, -amount);
}

int vmx_backpressure_handle_credit(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    if (!sess || !f) return VMX_ERR_INVALID_FRAME;
    uint32_t pid = 0;
    int32_t delta = 0;
    if (f->parsed_subrecord_count > 0) {
        const vmx_subrecord_t *sub = &f->subrecords[0];
        if (sub->kind == VMX_SUB_CREDIT_GRANT && sub->payload_len >= 8) {
            pid = vmx_read_u32(sub->payload);
            delta = (int32_t)vmx_read_u32(sub->payload + 4);
        }
    }
    if (pid == 0) return VMX_ERR_INVALID_ID;
    return vmx_backpressure_apply_credits(sess, pid, delta);
}

int vmx_backpressure_handle_window(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    /* Evaluate fairness across cached entries. BUG: fair_cache may
     * hold pointers to freed bp_entries if partitions were removed. */
    for (uint8_t fc = 0; fc < sess->fair_cache_count; fc++) {
        if (sess->fair_cache[fc]) {
            volatile int32_t cr = sess->fair_cache[fc]->credits;
            volatile uint32_t pid = sess->fair_cache[fc]->partition_id;
            (void)cr; (void)pid;
        }
    }

    if (!sess || !f) return VMX_ERR_INVALID_FRAME;
    uint32_t pid = 0;
    uint32_t new_window = 0;
    if (f->parsed_subrecord_count > 0) {
        const vmx_subrecord_t *sub = &f->subrecords[0];
        if (sub->kind == VMX_SUB_CREDIT_GRANT && sub->payload_len >= 8) {
            pid = vmx_read_u32(sub->payload);
            new_window = vmx_read_u32(sub->payload + 4);
        }
    }
    if (pid == 0) return VMX_ERR_INVALID_ID;

    for (uint8_t i = 0; i < sess->bp_entry_count; i++) {
        if (sess->bp_entries[i] && sess->bp_entries[i]->partition_id == pid) {
            sess->bp_entries[i]->window_size = new_window;
            return VMX_OK;
        }
    }
    return VMX_ERR_NOT_FOUND;
}
