#include "../common/types.h"
#include "../common/alloc.h"

int vmx_partition_add(vmx_session_t *sess, uint32_t partition_id,
                      uint32_t max_chunk, uint32_t window_size) {
    if (!sess) return VMX_ERR_INVALID_STATE;
    if (sess->partition_count >= VMX_MAX_PARTITIONS) return VMX_ERR_FULL;

    vmx_partition_t *part = vmx_calloc(1, sizeof(vmx_partition_t));
    if (!part) return VMX_ERR_NOMEM;

    part->partition_id = partition_id;
    part->max_chunk_size = max_chunk > 0 ? max_chunk : 4096;
    part->credit_balance = 100;
    part->window_size = window_size > 0 ? window_size : 64;
    part->active = true;

    sess->partitions[sess->partition_count++] = part;

    /* Also initialize backpressure tracking for this partition.
     * Channels that later bind to this partition will register
     * themselves as the callback context for credit notifications. */
    vmx_backpressure_add_partition(sess, partition_id, 100, 200);

    return VMX_OK;
}

int vmx_partition_remove(vmx_session_t *sess, uint32_t partition_id) {
    if (!sess) return VMX_ERR_INVALID_STATE;
    for (uint8_t i = 0; i < sess->partition_count; i++) {
        if (sess->partitions[i] &&
            sess->partitions[i]->partition_id == partition_id) {
            vmx_backpressure_remove_partition(sess, partition_id);
            vmx_free(sess->partitions[i]);
            sess->partitions[i] = NULL;
            return VMX_OK;
        }
    }
    return VMX_ERR_NOT_FOUND;
}

vmx_partition_t *vmx_partition_lookup(vmx_session_t *sess, uint32_t partition_id) {
    if (!sess) return NULL;
    for (uint8_t i = 0; i < sess->partition_count; i++) {
        if (sess->partitions[i] &&
            sess->partitions[i]->partition_id == partition_id &&
            sess->partitions[i]->active)
            return sess->partitions[i];
    }
    return NULL;
}

int vmx_partition_handle_add(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    if (!sess || !f) return VMX_ERR_INVALID_FRAME;
    uint32_t pid = 0, max_chunk = 4096, window = 64;
    if (f->parsed_subrecord_count > 0) {
        const vmx_subrecord_t *sub = &f->subrecords[0];
        if (sub->kind == VMX_SUB_PARTITION_DESC && sub->payload_len >= 12) {
            pid = vmx_read_u32(sub->payload);
            max_chunk = vmx_read_u32(sub->payload + 4);
            window = vmx_read_u32(sub->payload + 8);
        }
    }
    if (pid == 0) return VMX_ERR_INVALID_ID;
    return vmx_partition_add(sess, pid, max_chunk, window);
}

int vmx_partition_handle_remove(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    if (!sess || !f) return VMX_ERR_INVALID_FRAME;
    uint32_t pid = 0;
    if (f->parsed_subrecord_count > 0 && f->subrecords[0].payload_len >= 4)
        pid = vmx_read_u32(f->subrecords[0].payload);
    if (pid == 0) return VMX_ERR_INVALID_ID;
    return vmx_partition_remove(sess, pid);
}
