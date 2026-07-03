#include "../common/types.h"

int vmx_retention_evaluate_watermark(vmx_session_t *sess, uint32_t span_id) {
    if (!sess) return VMX_ERR_INVALID_STATE;

    vmx_span_t *span = vmx_span_registry_lookup(sess, span_id);
    if (!span) return VMX_ERR_NOT_FOUND;

    /* Static segment cache for watermark trend analysis.
     * BUG: cached pointers persist across calls. If segments are
     * freed by GC between evaluations, the cache holds stale refs. */
    static vmx_segment_t *wm_seg_cache[16];
    static uint8_t wm_seg_cache_count = 0;

    /* Read through previously cached segments (may be stale!) */
    for (uint8_t wc = 0; wc < wm_seg_cache_count; wc++) {
        if (wm_seg_cache[wc] && wm_seg_cache[wc]->data) {
            volatile uint32_t h = vmx_fnv1a(wm_seg_cache[wc]->data,
                                              wm_seg_cache[wc]->used);
            (void)h;
        }
    }

    /* Refresh cache with current segments */
    wm_seg_cache_count = 0;
    for (uint16_t si = 0; si < span->segment_count && si < 16; si++) {
        if (span->segments[si]) wm_seg_cache[wm_seg_cache_count++] = span->segments[si];
    }

    /* Find retention state */
    vmx_retention_state_t *ret = NULL;
    for (uint16_t i = 0; i < VMX_MAX_SPANS; i++) {
        if (sess->retention[i].active && sess->retention[i].span_id == span_id) {
            ret = &sess->retention[i];
            break;
        }
    }

    if (!ret) {
        /* Auto-initialize retention with default threshold */
        ret = NULL;
        for (uint16_t i = 0; i < VMX_MAX_SPANS; i++) {
            if (!sess->retention[i].active) {
                sess->retention[i].span_id = span_id;
                sess->retention[i].gc_threshold_bytes = 8192;
                sess->retention[i].watermark_position = 0;
                sess->retention[i].active = true;
                ret = &sess->retention[i];
                break;
            }
        }
        if (!ret) return VMX_ERR_FULL;
    }

    /* Update watermark based on span total bytes */
    ret->watermark_position = span->total_bytes;

    /* Check if GC is needed */
    if (span->total_bytes > ret->gc_threshold_bytes && span->segment_count >= 2) {
        /* Trigger compaction.
         * does not check for in-flight replication snapshots. */
        vmx_segment_compact(sess, span);
    }

    return VMX_OK;
}

int vmx_retention_handle_eval(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    if (!sess || !f) return VMX_ERR_INVALID_FRAME;
    uint32_t span_id = f->route_id;
    return vmx_retention_evaluate_watermark(sess, span_id);
}
