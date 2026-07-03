#include "../common/types.h"
#include "../common/alloc.h"

int vmx_repl_snapshot_segments(vmx_session_t *sess, vmx_replica_set_t *rs) {
    if (!sess || !rs) return VMX_ERR_INVALID_STATE;

    vmx_span_t *span = vmx_span_registry_lookup(sess, rs->span_id);
    if (!span) return VMX_ERR_NOT_FOUND;

    /* Snapshot raw segment pointers. */
    if (rs->snap_segments) vmx_free(rs->snap_segments);
    rs->snap_segments = vmx_calloc(span->segment_count, sizeof(vmx_segment_t *));
    if (!rs->snap_segments) return VMX_ERR_NOMEM;

    for (uint16_t i = 0; i < span->segment_count; i++) {
        rs->snap_segments[i] = span->segments[i];
    }

    /* Also cache raw data pointers for fast hash verification.
     * BUG: these point directly to segment data buffers. If segments
     * are compacted (data freed) between sync_begin and sync_data,
     * these become dangling pointers. */
    for (uint16_t i = 0; i < span->segment_count && i < 16; i++) {
        if (span->segments[i] && span->segments[i]->data) {
            sess->repl_data_cache[sess->repl_data_cache_count] = span->segments[i]->data;
            sess->repl_data_cache_len[sess->repl_data_cache_count] = span->segments[i]->used;
            sess->repl_data_cache_count++;
        }
    }
    rs->snap_segment_count = span->segment_count;
    rs->snap_generation = span->generation;
    rs->snap_active = true;

    return VMX_OK;
}

int vmx_repl_snapshot_replica_set(vmx_session_t *sess, vmx_replica_set_t *rs) {
    if (!sess || !rs) return VMX_ERR_INVALID_STATE;

    vmx_span_t *span = vmx_span_registry_lookup(sess, rs->span_id);
    if (!span) return VMX_ERR_NOT_FOUND;

    /* Snapshot span metadata pointer. */
    rs->snap_span = span;
    rs->snap_generation = span->generation;
    rs->snap_active = true;

    return VMX_OK;
}

int vmx_repl_serialize_segment(vmx_session_t *sess, vmx_replica_set_t *rs,
                               uint32_t seg_idx) {
    if (!sess || !rs) return VMX_ERR_INVALID_STATE;
    if (!rs->snap_active) return VMX_ERR_INVALID_STATE;
    if (seg_idx >= rs->snap_segment_count) return VMX_ERR_INVALID_ID;

    /* First check repl_data_cache for fast path. BUG: cache may be stale. */
    if (seg_idx < sess->repl_data_cache_count && sess->repl_data_cache[seg_idx]) {
        volatile uint32_t h = vmx_fnv1a(sess->repl_data_cache[seg_idx],
                                         sess->repl_data_cache_len[seg_idx]);
        (void)h;
    }

    /* Read segment data through snapshot pointer. */
    vmx_segment_t *seg = rs->snap_segments[seg_idx];
    if (!seg || !seg->data) return VMX_ERR_NOT_FOUND;

    uint32_t hash = vmx_fnv1a(seg->data, seg->used);
    (void)hash;

    return VMX_OK;
}

int vmx_repl_handle_sync_begin(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    if (!sess || !f) return VMX_ERR_INVALID_FRAME;
    uint32_t span_id = f->route_id;
    vmx_replica_set_t *rs = vmx_repl_find_by_span(sess, span_id);
    if (!rs) return VMX_ERR_NOT_FOUND;

    vmx_repl_snapshot_replica_set(sess, rs);
    return vmx_repl_snapshot_segments(sess, rs);
}

int vmx_repl_handle_sync_data(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    if (!sess || !f) return VMX_ERR_INVALID_FRAME;
    uint32_t span_id = f->route_id;
    vmx_replica_set_t *rs = vmx_repl_find_by_span(sess, span_id);
    if (!rs) return VMX_ERR_NOT_FOUND;

    for (uint8_t i = 0; i < f->parsed_subrecord_count; i++) {
        if (f->subrecords[i].kind == VMX_SUB_SEGMENT_REF &&
            f->subrecords[i].payload_len >= 4) {
            uint32_t idx = vmx_read_u32(f->subrecords[i].payload);
            vmx_repl_serialize_segment(sess, rs, idx);
        }
    }
    return VMX_OK;
}
