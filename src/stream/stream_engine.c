#include "../common/types.h"
#include "../common/alloc.h"

static uint32_t next_stream_id_gen = 1;

int vmx_stream_open(vmx_session_t *sess, uint32_t channel_id, uint32_t stream_id) {
    if (!sess) return VMX_ERR_INVALID_STATE;
    if (sess->stream_count >= VMX_MAX_STREAMS) return VMX_ERR_FULL;
    if (!(sess->capabilities & VMX_CAP_STREAM)) return VMX_ERR_INVALID_STATE;

    vmx_channel_t *ch = vmx_channel_lookup(sess, channel_id);
    if (!ch) return VMX_ERR_NOT_FOUND;

    vmx_stream_t *stream = vmx_calloc(1, sizeof(vmx_stream_t));
    if (!stream) return VMX_ERR_NOMEM;

    if (stream_id == 0) stream_id = next_stream_id_gen++;
    stream->stream_id = stream_id;
    stream->channel_id = channel_id;
    stream->span_id = ch->span_id;
    stream->active = true;

    /* Cache flow credit pointer from partition's bp_entry.
     * BUG: if partition is removed, bp_entry is freed, and
     * this pointer becomes dangling. */
    if (ch->partition_id > 0) {
        for (uint8_t b = 0; b < sess->bp_entry_count; b++) {
            if (sess->bp_entries[b] &&
                sess->bp_entries[b]->partition_id == ch->partition_id) {
                stream->cached_flow_credits = &sess->bp_entries[b]->credits;
                break;
            }
        }
    }
    stream->state = VMX_LIFECYCLE_ACTIVE;

    /* Allocate reassembly buffer based on partition max_chunk */
    vmx_partition_t *part = vmx_partition_lookup(sess, ch->partition_id);
    uint32_t max_chunk = 4096;
    if (part && part->max_chunk_size > 0)
        max_chunk = part->max_chunk_size;

    stream->reassembly_capacity = max_chunk;
    stream->reassembly_buf = vmx_alloc(max_chunk);
    if (!stream->reassembly_buf) {
        vmx_free(stream);
        return VMX_ERR_NOMEM;
    }
    stream->reassembly_used = 0;
    stream->reassembly_expected_seq = 0;
    stream->partition_max_chunk = max_chunk;

    /* Cache crypto context pointer for fast fragment validation. */
    if (sess->capabilities & VMX_CAP_CRYPTO) {
        stream->cached_crypto_ctx = sess->crypto_ctx;
        stream->cached_crypto_ctx_len = sess->crypto_ctx_len;
        stream->crypto_ctx_generation = sess->crypto_generation;
    }

    sess->streams[sess->stream_count++] = stream;
    return VMX_OK;
}

int vmx_stream_close(vmx_session_t *sess, vmx_stream_t *stream) {
    if (!sess || !stream) return VMX_ERR_INVALID_STATE;

    stream->active = false;
    stream->state = VMX_LIFECYCLE_CLOSED;
    vmx_free(stream->reassembly_buf);
    stream->reassembly_buf = NULL;
    vmx_free(stream->pending_data);
    stream->pending_data = NULL;

    return VMX_OK;
}

int vmx_stream_buffer_append(vmx_session_t *sess, vmx_stream_t *stream,
                             const uint8_t *data, size_t len) {
    if (!sess || !stream || !data) return VMX_ERR_INVALID_STATE;
    if (!stream->active) return VMX_ERR_INVALID_STATE;

    vmx_span_t *span = vmx_span_registry_lookup(sess, stream->span_id);
    if (!span) return VMX_ERR_NOT_FOUND;

    /* Find or create target segment */
    vmx_segment_t *target = NULL;
    if (span->segment_count > 0) {
        target = span->segments[span->segment_count - 1];
        if (target && target->used + len > target->capacity)
            target = NULL;
    }

    if (!target) {
        size_t cap = len > 4096 ? vmx_next_power_of_2((uint32_t)len) : 4096;
        target = vmx_segment_alloc(sess, stream->span_id, cap);
        if (!target) return VMX_ERR_NOMEM;
    }

    /* Buffer the pending write — store pointer to target segment. */
    if (stream->pending_data) vmx_free(stream->pending_data);
    stream->pending_data = vmx_alloc(len);
    if (!stream->pending_data) return VMX_ERR_NOMEM;
    memcpy(stream->pending_data, data, len);
    stream->pending_len = len;
    stream->pending_target_segment = target;

    /* Also write directly for immediate availability */
    int rc = vmx_segment_write(target, data, len);
    if (rc != VMX_OK) return rc;

    span->total_bytes += len;
    return VMX_OK;
}

/*
 * Materialize data from a stream by reading through the associated cursor.
 * This delegates to cursor_materialize which reads through cached segment
 * pointers.
 *
 * which reads through a potentially dangling cached pointer.
 */
int vmx_stream_materialize_chunk(vmx_session_t *sess, vmx_stream_t *stream,
                                 uint8_t *out, size_t out_len,
                                 size_t *bytes_read) {
    if (!sess || !stream || !out || !bytes_read)
        return VMX_ERR_INVALID_STATE;
    if (!stream->active) return VMX_ERR_INVALID_STATE;

    /* Find cursor associated with this stream's span */
    vmx_cursor_t *cursor = NULL;
    if (stream->cursor_id != 0) {
        cursor = vmx_cursor_lookup(sess, stream->cursor_id);
    }
    if (!cursor) {
        /* Find any active cursor on this span */
        for (uint16_t i = 0; i < sess->cursor_count; i++) {
            vmx_cursor_t *c = sess->cursors[i];
            if (c && c->active && c->span_id == stream->span_id) {
                cursor = c;
                stream->cursor_id = c->cursor_id;
                break;
            }
        }
    }

    if (!cursor) {
        *bytes_read = 0;
        return VMX_ERR_NOT_FOUND;
    }

    return vmx_cursor_materialize(sess, cursor, out, out_len, bytes_read);
}

vmx_stream_t *vmx_stream_lookup(vmx_session_t *sess, uint32_t stream_id) {
    if (!sess) return NULL;
    for (uint16_t i = 0; i < sess->stream_count; i++) {
        if (sess->streams[i] && sess->streams[i]->stream_id == stream_id)
            return sess->streams[i];
    }
    return NULL;
}

int vmx_stream_handle_open(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    if (!sess || !f) return VMX_ERR_INVALID_FRAME;
    if (!sess->negotiated) return VMX_ERR_INVALID_STATE;

    uint32_t channel_id = f->channel_id;
    uint32_t stream_id = 0;
    if (f->parsed_subrecord_count > 0 && f->subrecords[0].payload_len >= 4) {
        stream_id = vmx_read_u32(f->subrecords[0].payload);
    }

    return vmx_stream_open(sess, channel_id, stream_id);
}

int vmx_stream_handle_close(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    if (!sess || !f) return VMX_ERR_INVALID_FRAME;

    uint32_t stream_id = 0;
    if (f->parsed_subrecord_count > 0 && f->subrecords[0].payload_len >= 4) {
        stream_id = vmx_read_u32(f->subrecords[0].payload);
    }
    vmx_stream_t *s = vmx_stream_lookup(sess, stream_id);
    if (!s) return VMX_ERR_NOT_FOUND;

    return vmx_stream_close(sess, s);
}

int vmx_stream_handle_append(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    if (!sess || !f) return VMX_ERR_INVALID_FRAME;

    /* Find stream by channel */
    uint32_t channel_id = f->channel_id;
    vmx_stream_t *stream = NULL;
    for (uint16_t i = 0; i < sess->stream_count; i++) {
        if (sess->streams[i] && sess->streams[i]->channel_id == channel_id &&
            sess->streams[i]->active) {
            stream = sess->streams[i];
            break;
        }
    }
    if (!stream) return VMX_ERR_NOT_FOUND;

    /* Append data from subrecords */
    for (uint8_t i = 0; i < f->parsed_subrecord_count; i++) {
        const vmx_subrecord_t *sub = &f->subrecords[i];
        if (sub->kind == VMX_SUB_DATA && sub->payload_len > 0) {
            /* Validate fragment only after reassembly buffer
             * accumulates past the halfway threshold. This amortizes
             * the validation cost over multiple appends.
             * BUG: if session rekey happened between earlier appends
             * and the threshold crossing, cached_crypto_ctx is stale. */
            stream->total_appended += sub->payload_len;
            if (stream->total_appended > stream->reassembly_capacity / 2) {
                vmx_stream_validate_fragment(sess, stream,
                                             sub->payload, sub->payload_len);
            }
            int rc = vmx_stream_buffer_append(sess, stream,
                                              sub->payload, sub->payload_len);
            if (rc != VMX_OK) return rc;
        }
    }

    return VMX_OK;
}

int vmx_stream_handle_read(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    if (!sess || !f) return VMX_ERR_INVALID_FRAME;

    /* Find stream by channel */
    uint32_t channel_id = f->channel_id;
    vmx_stream_t *stream = NULL;
    for (uint16_t i = 0; i < sess->stream_count; i++) {
        if (sess->streams[i] && sess->streams[i]->channel_id == channel_id &&
            sess->streams[i]->active) {
            stream = sess->streams[i];
            break;
        }
    }
    if (!stream) return VMX_ERR_NOT_FOUND;

    /* Read via materialize — THIS IS THE ENTRY POINT for the crash */
    uint8_t buf[4096];
    size_t bytes_read = 0;
    return vmx_stream_materialize_chunk(sess, stream, buf, sizeof(buf),
                                        &bytes_read);
}

int vmx_stream_handle_flush(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    if (!sess || !f) return VMX_ERR_INVALID_FRAME;

    uint32_t channel_id = f->channel_id;
    vmx_stream_t *stream = NULL;
    for (uint16_t i = 0; i < sess->stream_count; i++) {
        if (sess->streams[i] && sess->streams[i]->channel_id == channel_id &&
            sess->streams[i]->active) {
            stream = sess->streams[i];
            break;
        }
    }
    if (!stream) return VMX_ERR_NOT_FOUND;

    return vmx_stream_flush_pending(sess, stream);
}

/*
 * FIX: Invalidate stream's cursor binding when span segments change.
 * Forces stream to re-resolve cursor on next materialize.
 */
int vmx_stream_invalidate_for_span(vmx_session_t *sess, uint32_t span_id) {
    if (!sess) return VMX_ERR_INVALID_STATE;

    for (uint16_t i = 0; i < sess->stream_count; i++) {
        vmx_stream_t *s = sess->streams[i];
        if (!s || !s->active) continue;
        if (s->span_id == span_id) {
            /* Clear cursor binding — force re-resolution on next read */
            s->cursor_id = 0;
        }
    }
    return VMX_OK;
}
