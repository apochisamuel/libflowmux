#include "../common/types.h"

/*
 * Per-channel throughput tracking and bandwidth estimation.
 */

typedef struct {
    uint32_t channel_id;
    uint64_t bytes_sent;
    uint64_t bytes_received;
    uint32_t ops_completed;
    uint32_t ops_failed;
    uint32_t avg_latency_us;
    uint32_t peak_latency_us;
    uint32_t inflight_ops;
    uint32_t throughput_kbps;
} vmx_channel_stats_t;

int vmx_channel_collect_stats(const vmx_session_t *sess, uint32_t channel_id,
                              vmx_channel_stats_t *stats) {
    if (!sess || !stats) return -1;
    memset(stats, 0, sizeof(*stats));

    vmx_channel_t *ch = NULL;
    for (uint16_t i = 0; i < sess->channel_count; i++) {
        if (sess->channels[i] && sess->channels[i]->channel_id == channel_id) {
            ch = sess->channels[i];
            break;
        }
    }
    if (!ch) return -1;

    stats->channel_id = ch->channel_id;
    stats->inflight_ops = ch->inflight_count;

    /* Estimate throughput from bound span */
    vmx_span_t *span = vmx_span_registry_lookup((vmx_session_t *)sess, ch->span_id);
    if (span) {
        stats->bytes_sent = span->total_bytes;
    }

    return 0;
}

/*
 * Compute channel utilization: 0-1000.
 */
int vmx_channel_utilization(const vmx_channel_t *ch) {
    if (!ch) return 0;
    if (ch->state != VMX_LIFECYCLE_ACTIVE) return 0;

    /* Estimate from inflight ops */
    int util = (int)(ch->inflight_count * 100);
    if (util > 1000) util = 1000;
    return util;
}
