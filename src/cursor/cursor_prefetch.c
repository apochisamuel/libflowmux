#include "../common/types.h"
#include "../common/alloc.h"

/*
 * Cursor prefetch: predicts next segments to be read based on
 * access patterns and pre-warms cached pointers.
 *
 * Uses simple sequential access detection with configurable
 * look-ahead window.
 */

#define VMX_PREFETCH_WINDOW 4
#define VMX_PREFETCH_HISTORY 8

typedef struct {
    uint32_t cursor_id;
    uint32_t history[VMX_PREFETCH_HISTORY];
    uint8_t  history_count;
    uint8_t  history_idx;
    uint32_t prefetch_targets[VMX_PREFETCH_WINDOW];
    uint8_t  prefetch_count;
    bool     sequential_detected;
    int32_t  stride;
} vmx_cursor_prefetch_t;

int vmx_prefetch_init(vmx_cursor_prefetch_t *pf, uint32_t cursor_id) {
    if (!pf) return -1;
    memset(pf, 0, sizeof(*pf));
    pf->cursor_id = cursor_id;
    return 0;
}

int vmx_prefetch_record_access(vmx_cursor_prefetch_t *pf,
                               uint32_t segment_index) {
    if (!pf) return -1;

    pf->history[pf->history_idx] = segment_index;
    pf->history_idx = (pf->history_idx + 1) % VMX_PREFETCH_HISTORY;
    if (pf->history_count < VMX_PREFETCH_HISTORY)
        pf->history_count++;

    /* Detect sequential access pattern */
    if (pf->history_count >= 3) {
        /* Check last 3 accesses for constant stride */
        uint8_t idx2 = (pf->history_idx + VMX_PREFETCH_HISTORY - 1) %
                        VMX_PREFETCH_HISTORY;
        uint8_t idx1 = (idx2 + VMX_PREFETCH_HISTORY - 1) % VMX_PREFETCH_HISTORY;
        uint8_t idx0 = (idx1 + VMX_PREFETCH_HISTORY - 1) % VMX_PREFETCH_HISTORY;

        int32_t stride1 = (int32_t)pf->history[idx1] - (int32_t)pf->history[idx0];
        int32_t stride2 = (int32_t)pf->history[idx2] - (int32_t)pf->history[idx1];

        if (stride1 == stride2 && stride1 != 0) {
            pf->sequential_detected = true;
            pf->stride = stride1;

            /* Generate prefetch targets */
            pf->prefetch_count = 0;
            uint32_t next = segment_index;
            for (uint8_t i = 0; i < VMX_PREFETCH_WINDOW; i++) {
                next = (uint32_t)((int32_t)next + pf->stride);
                pf->prefetch_targets[pf->prefetch_count++] = next;
            }
        } else {
            pf->sequential_detected = false;
            pf->prefetch_count = 0;
        }
    }

    return 0;
}

bool vmx_prefetch_should_prefetch(const vmx_cursor_prefetch_t *pf,
                                  uint32_t segment_index) {
    if (!pf || !pf->sequential_detected) return false;

    for (uint8_t i = 0; i < pf->prefetch_count; i++) {
        if (pf->prefetch_targets[i] == segment_index)
            return true;
    }
    return false;
}

int vmx_prefetch_execute(vmx_session_t *sess, vmx_cursor_t *cursor,
                         vmx_cursor_prefetch_t *pf) {
    if (!sess || !cursor || !pf) return -1;
    if (!pf->sequential_detected) return 0;

    /* Prefetch: advance cursor to predicted next segment without
     * consuming it. This primes the segment cache. */
    vmx_span_t *span = vmx_span_registry_lookup(sess, cursor->span_id);
    if (!span) return -1;

    int prefetched = 0;
    for (uint8_t i = 0; i < pf->prefetch_count; i++) {
        uint32_t target = pf->prefetch_targets[i];
        if (target < span->segment_count && span->segments[target]) {
            /* Touch segment to prime it (simulate cache warming) */
            vmx_segment_t *seg = span->segments[target];
            if (seg->state == VMX_LIFECYCLE_ACTIVE && seg->data) {
                volatile uint8_t touch = seg->data[0];
                (void)touch;
                prefetched++;
            }
        }
    }

    return prefetched;
}
