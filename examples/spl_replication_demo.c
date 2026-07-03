/*
 * Example: replication workflow.
 * Demonstrates replica set setup, member management, quorum checking,
 * snapshot-based sync, and health monitoring.
 */
#include "../include/vaultmux.h"
#include "../src/common/types.h"
#include "../src/common/util.h"
#include "../src/common/crc.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    printf("libvaultmux replication demo\n");
    printf("===========================\n\n");

    vmx_context_t *ctx = vmx_context_create();
    uint32_t caps = VMX_CAP_CHANNEL | VMX_CAP_STREAM | VMX_CAP_CURSOR |
                    VMX_CAP_REPLICATION | VMX_CAP_MIGRATION;
    vmx_session_t *sess = vmx_session_create(ctx, caps);
    sess->initialized = true;
    sess->negotiated = true;

    printf("Session %u created\n", vmx_session_get_id(sess));

    /* Create span with data */
    vmx_span_t *span = vmx_span_registry_create(sess, 300, 0xAAAA);
    vmx_segment_t *seg = vmx_segment_alloc(sess, 300, 16384);
    uint8_t data[1024];
    for (int i = 0; i < 1024; i++) data[i] = (uint8_t)(i * 7);
    vmx_segment_write(seg, data, 1024);
    printf("Span %u: 1 segment, %zu bytes\n", span->span_id, seg->used);

    /* Add nodes */
    vmx_route_add_node(sess, 1, "node-1.local", 5000);
    vmx_route_add_node(sess, 2, "node-2.local", 5000);
    vmx_route_add_node(sess, 3, "node-3.local", 5000);
    printf("Added 3 nodes to cluster\n");

    /* Setup replica set */
    int rc = vmx_repl_setup(sess, 300, 2);  /* quorum of 2 */
    printf("Replica set created: rc=%d\n", rc);

    vmx_replica_set_t *rs = vmx_repl_find_by_span(sess, 300);
    if (rs) {
        vmx_repl_add_member(sess, rs->replica_set_id, 1);
        vmx_repl_add_member(sess, rs->replica_set_id, 2);
        vmx_repl_add_member(sess, rs->replica_set_id, 3);
        printf("Added 3 members, quorum=%d\n", rs->quorum_size);

        /* Check quorum */
        rc = vmx_repl_check_quorum(sess, rs);
        printf("Quorum check: %s\n", rc == VMX_OK ? "PASSED" : "FAILED");

        /* Snapshot segments for sync */
        rc = vmx_repl_snapshot_segments(sess, rs);
        printf("Snapshot taken: %d segments\n", rs->snap_segment_count);

        /* Serialize a segment */
        rc = vmx_repl_serialize_segment(sess, rs, 0);
        printf("Serialized segment 0: rc=%d\n", rc);
    }

    printf("\nDemo complete.\n");
    vmx_context_destroy(ctx);
    return 0;
}
