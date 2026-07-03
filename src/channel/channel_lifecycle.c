#include "../common/types.h"
#include "../common/alloc.h"

int vmx_channel_handle_open(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    if (!sess || !f) return VMX_ERR_INVALID_FRAME;
    if (!sess->negotiated) return VMX_ERR_INVALID_STATE;

    uint32_t channel_id = f->channel_id;
    if (channel_id == 0) return VMX_ERR_INVALID_ID;

    vmx_channel_t *ch = vmx_channel_mux_alloc(sess, channel_id);
    if (!ch) return VMX_ERR_NOMEM;

    return VMX_OK;
}

int vmx_channel_handle_close(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    if (!sess || !f) return VMX_ERR_INVALID_FRAME;

    uint32_t channel_id = f->channel_id;
    vmx_channel_t *ch = vmx_channel_lookup(sess, channel_id);
    if (!ch) return VMX_ERR_NOT_FOUND;

    ch->state = VMX_LIFECYCLE_CLOSING;
    /* Release associated lease if any */
    if (ch->lease_id != 0) {
        vmx_lease_t *l = vmx_lease_lookup(sess, ch->lease_id);
        if (l && l->active) {
            vmx_lease_release(sess, l);
        }
    }

    ch->state = VMX_LIFECYCLE_CLOSED;
    return VMX_OK;
}

int vmx_channel_handle_bind(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    if (!sess || !f) return VMX_ERR_INVALID_FRAME;

    uint32_t channel_id = f->channel_id;
    vmx_channel_t *ch = vmx_channel_lookup(sess, channel_id);
    if (!ch) return VMX_ERR_NOT_FOUND;

    uint32_t span_id = f->route_id;
    uint32_t partition_id = 0;

    if (f->parsed_subrecord_count > 0) {
        const vmx_subrecord_t *sub = &f->subrecords[0];
        if (sub->kind == VMX_SUB_PARTITION_DESC && sub->payload_len >= 4) {
            partition_id = vmx_read_u32(sub->payload);
        }
    }

    return vmx_channel_bind_span(sess, ch, span_id, partition_id);
}

int vmx_channel_handle_remap(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    if (!sess || !f) return VMX_ERR_INVALID_FRAME;

    uint32_t channel_id = f->channel_id;
    vmx_channel_t *ch = vmx_channel_lookup(sess, channel_id);
    if (!ch) return VMX_ERR_NOT_FOUND;

    uint32_t new_partition = 0;
    if (f->parsed_subrecord_count > 0) {
        const vmx_subrecord_t *sub = &f->subrecords[0];
        if (sub->kind == VMX_SUB_PARTITION_DESC && sub->payload_len >= 4) {
            new_partition = vmx_read_u32(sub->payload);
        }
    }

    return vmx_channel_remap_partition(sess, ch, new_partition);
}

int vmx_channel_handle_drain(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    if (!sess || !f) return VMX_ERR_INVALID_FRAME;

    uint32_t channel_id = f->channel_id;
    vmx_channel_t *ch = vmx_channel_lookup(sess, channel_id);
    if (!ch) return VMX_ERR_NOT_FOUND;

    if (ch->state == VMX_LIFECYCLE_DRAINING) return VMX_OK;
    ch->state = VMX_LIFECYCLE_DRAINING;
    /* Dedup check — prevent same pointer appearing twice */
    for (uint16_t d = 0; d < sess->drain_count; d++) {
        if (sess->drain_list[d] == ch) return VMX_OK;
    }
    if (sess->drain_count < VMX_MAX_CHANNELS) {
        sess->drain_list[sess->drain_count++] = ch;
    }

    return VMX_OK;
}
