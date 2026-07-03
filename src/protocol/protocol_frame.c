#include "../common/types.h"

int vmx_frame_dispatch(vmx_session_t *sess, const vmx_parsed_frame_t *frame) {
    if (!sess || !frame) return VMX_ERR_INVALID_FRAME;
    if (frame->frame_type == VMX_FRAME_PADDING) return VMX_OK;

    /* Validate session_id if session is initialized */
    if (sess->initialized && frame->session_id != 0 &&
        frame->session_id != sess->session_id)
        return VMX_ERR_INVALID_ID;

    /* Validate epoch: frame epoch must be <= session epoch */
    /* (encoded in parsed header, checked before dispatch) */

    switch (frame->frame_type) {
    /* Session frames */
    case VMX_FRAME_SESSION_INIT:       return vmx_session_handle_init(sess, frame);
    case VMX_FRAME_SESSION_CAP_NEG:    return vmx_session_handle_cap_neg(sess, frame);
    case VMX_FRAME_SESSION_EPOCH_ADV:  return vmx_session_advance_epoch(sess, frame);
    case VMX_FRAME_SESSION_REKEY:      return vmx_session_rotate_context(sess, frame);
    case VMX_FRAME_SESSION_CLOSE:      return vmx_session_handle_close(sess, frame);

    /* Span frames */
    case VMX_FRAME_SPAN_CREATE:        return vmx_span_handle_create(sess, frame);
    case VMX_FRAME_SPAN_CLOSE:         return vmx_span_handle_close(sess, frame);
    case VMX_FRAME_SPAN_META_UPDATE:   return vmx_span_handle_meta_update(sess, frame);

    /* Segment frames */
    case VMX_FRAME_SEGMENT_ATTACH:     return vmx_segment_handle_attach(sess, frame);
    case VMX_FRAME_SEGMENT_DETACH:     return vmx_segment_handle_detach(sess, frame);

    /* Channel frames */
    case VMX_FRAME_CHANNEL_OPEN:       return vmx_channel_handle_open(sess, frame);
    case VMX_FRAME_CHANNEL_CLOSE:      return vmx_channel_handle_close(sess, frame);
    case VMX_FRAME_CHANNEL_BIND:       return vmx_channel_handle_bind(sess, frame);
    case VMX_FRAME_CHANNEL_REMAP:      return vmx_channel_handle_remap(sess, frame);
    case VMX_FRAME_CHANNEL_DRAIN:      return vmx_channel_handle_drain(sess, frame);

    /* Lease frames */
    case VMX_FRAME_LEASE_ACQUIRE:      return vmx_lease_handle_acquire(sess, frame);
    case VMX_FRAME_LEASE_RELEASE:      return vmx_lease_handle_release(sess, frame);
    case VMX_FRAME_LEASE_RENEW:        return vmx_lease_handle_renew(sess, frame);

    /* Stream frames */
    case VMX_FRAME_STREAM_OPEN:        return vmx_stream_handle_open(sess, frame);
    case VMX_FRAME_STREAM_CLOSE:       return vmx_stream_handle_close(sess, frame);
    case VMX_FRAME_STREAM_APPEND:      return vmx_stream_handle_append(sess, frame);
    case VMX_FRAME_STREAM_READ:        return vmx_stream_handle_read(sess, frame);
    case VMX_FRAME_STREAM_FLUSH:       return vmx_stream_handle_flush(sess, frame);

    /* Cursor frames */
    case VMX_FRAME_CURSOR_OPEN:        return vmx_cursor_handle_open(sess, frame);
    case VMX_FRAME_CURSOR_CLOSE:       return vmx_cursor_handle_close(sess, frame);
    case VMX_FRAME_CURSOR_ADVANCE:     return vmx_cursor_handle_advance(sess, frame);
    case VMX_FRAME_CURSOR_READ:        return vmx_cursor_handle_read(sess, frame);

    /* Replication frames */
    case VMX_FRAME_REPL_SETUP:         return vmx_repl_handle_setup(sess, frame);
    case VMX_FRAME_REPL_ADD_MEMBER:    return vmx_repl_handle_add_member(sess, frame);
    case VMX_FRAME_REPL_REMOVE_MEMBER: return vmx_repl_handle_remove_member(sess, frame);
    case VMX_FRAME_REPL_SYNC_BEGIN:    return vmx_repl_handle_sync_begin(sess, frame);
    case VMX_FRAME_REPL_SYNC_DATA:     return vmx_repl_handle_sync_data(sess, frame);
    case VMX_FRAME_REPL_CHECK:         return vmx_repl_handle_check(sess, frame);

    /* Migration frames */
    case VMX_FRAME_MIGRATE_INITIATE:   return vmx_migrate_handle_initiate(sess, frame);
    case VMX_FRAME_MIGRATE_PREPARE:    return vmx_migrate_handle_prepare(sess, frame);
    case VMX_FRAME_MIGRATE_TRANSFER:   return vmx_migrate_handle_transfer(sess, frame);
    case VMX_FRAME_MIGRATE_HANDOFF:    return vmx_migrate_handle_handoff(sess, frame);
    case VMX_FRAME_MIGRATE_ABORT:      return vmx_migrate_handle_abort(sess, frame);

    /* Checkpoint frames */
    case VMX_FRAME_CHECKPOINT_BEGIN:    return vmx_checkpoint_handle_begin(sess, frame);
    case VMX_FRAME_CHECKPOINT_FLUSH:   return vmx_checkpoint_handle_flush(sess, frame);
    case VMX_FRAME_CHECKPOINT_RESTORE: return vmx_checkpoint_handle_restore(sess, frame);

    /* Retention frames */
    case VMX_FRAME_RETENTION_EVAL:     return vmx_retention_handle_eval(sess, frame);
    case VMX_FRAME_RETENTION_COLLECT:  return vmx_retention_handle_collect(sess, frame);

    /* Route frames */
    case VMX_FRAME_ROUTE_ADVERTISE:    return vmx_route_handle_advertise(sess, frame);
    case VMX_FRAME_ROUTE_WITHDRAW:     return vmx_route_handle_withdraw(sess, frame);
    case VMX_FRAME_ROUTE_REBALANCE:    return vmx_route_handle_rebalance(sess, frame);
    case VMX_FRAME_ROUTE_NODE_JOIN:    return vmx_route_handle_node_join(sess, frame);
    case VMX_FRAME_ROUTE_NODE_DEPART:  return vmx_route_handle_node_depart(sess, frame);

    /* Partition frames */
    case VMX_FRAME_PARTITION_ADD:      return vmx_partition_handle_add(sess, frame);
    case VMX_FRAME_PARTITION_REMOVE:   return vmx_partition_handle_remove(sess, frame);

    /* Backpressure frames */
    case VMX_FRAME_BP_CREDIT_UPDATE:   return vmx_backpressure_handle_credit(sess, frame);
    case VMX_FRAME_BP_WINDOW_ADJUST:   return vmx_backpressure_handle_window(sess, frame);

    /* Deferred/GC frames */
    case VMX_FRAME_DRAIN_DEFERRED:     return vmx_handle_drain_deferred(sess, frame);
    case VMX_FRAME_GC_TRIGGER:         return vmx_handle_gc_trigger(sess, frame);

    default:
        return VMX_ERR_INVALID_FRAME;
    }
}

/* Main ingestion entry point */
int vmx_ingest_frames(vmx_session_t *sess, const uint8_t *data, size_t len) {
    if (!sess || !data || len == 0) return VMX_ERR_INVALID_FRAME;

    /* Parse session header */
    vmx_parsed_header_t hdr;
    int rc = vmx_protocol_parse_header(data, len, &hdr);
    if (rc != VMX_OK) return rc;

    /* Session initialization: accept first valid header's session_id */
    if (!sess->initialized) {
        /* Will be initialized by SESSION_INIT frame */
    } else {
        /* Validate session ID matches */
        if (hdr.session_id != sess->session_id)
            return VMX_ERR_INVALID_ID;
        /* Validate epoch is current or advance */
        if (hdr.epoch < sess->epoch)
            return VMX_ERR_INVALID_EPOCH;
    }

    size_t offset = VMX_WIRE_HEADER_SIZE;
    int frames_processed = 0;

    for (uint16_t i = 0; i < hdr.frame_count && offset < len; i++) {
        if (offset + VMX_WIRE_FRAME_HEADER_SIZE > len)
            break;

        vmx_parsed_frame_t frame;
        rc = vmx_protocol_parse_frame(data + offset, len - offset, &frame);
        if (rc != VMX_OK)
            break;

        /* Validate sequence if session is active */
        if (sess->initialized && frame.frame_type != VMX_FRAME_PADDING) {
            if (frame.sequence != sess->next_sequence &&
                frame.frame_type != VMX_FRAME_SESSION_INIT)
                break;  /* sequence gap */
        }

        rc = vmx_frame_dispatch(sess, &frame);
        if (rc != VMX_OK)
            break;

        if (frame.frame_type != VMX_FRAME_PADDING)
            sess->next_sequence = frame.sequence + 1;

        offset += frame.total_length;
        frames_processed++;
    }

    return frames_processed > 0 ? frames_processed : VMX_ERR_PROTOCOL;
}
