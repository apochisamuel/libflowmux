/*
 * Example: span migration workflow.
 * Demonstrates span creation, segment allocation, migration
 * initiation, and handoff completion.
 */
#include "../include/vaultmux.h"
#include "../src/common/types.h"
#include "../src/common/util.h"
#include "../src/common/crc.h"
#include <stdio.h>
#include <string.h>

static size_t build_msg(uint8_t *buf, uint32_t sid, uint32_t epoch,
                        uint8_t type, uint16_t seq, uint16_t ch_id,
                        uint16_t route_id, uint32_t owner) {
    vmx_write_u32(buf, VMX_MAGIC);
    vmx_write_u16(buf + 4, 0x0100);
    vmx_write_u32(buf + 6, sid);
    vmx_write_u32(buf + 10, epoch);
    vmx_write_u16(buf + 14, 1);
    vmx_write_u32(buf + 16, vmx_crc32c(buf, 14));
    size_t off = VMX_WIRE_HEADER_SIZE;

    vmx_write_u16(buf + off, VMX_WIRE_FRAME_HEADER_SIZE);
    buf[off + 2] = type;
    vmx_write_u16(buf + off + 3, seq);
    vmx_write_u32(buf + off + 5, sid);
    vmx_write_u16(buf + off + 9, ch_id);
    vmx_write_u16(buf + off + 11, route_id);
    vmx_write_u32(buf + off + 13, owner);
    vmx_write_u16(buf + off + 17, 0);
    buf[off + 19] = 0;

    return off + VMX_WIRE_FRAME_HEADER_SIZE;
}

int main(void) {
    printf("libvaultmux migration demo\n");
    printf("=========================\n\n");

    vmx_context_t *ctx = vmx_context_create();
    uint32_t caps = VMX_CAP_CHANNEL | VMX_CAP_STREAM | VMX_CAP_CURSOR |
                    VMX_CAP_MIGRATION;
    vmx_session_t *sess = vmx_session_create(ctx, caps);
    uint32_t sid = vmx_session_get_id(sess);

    printf("Session %u created with migration capability\n", sid);

    uint8_t buf[512];

    /* Init session */
    size_t len = build_msg(buf, sid, 0, VMX_FRAME_SESSION_INIT, 0, 0, 0, 0);
    vmx_ingest_frames(sess, buf, len);
    printf("Session initialized\n");

    /* Manually create span and segment for demo */
    vmx_span_t *span = vmx_span_registry_create(sess, 200, 0xCAFE);
    printf("Span %u created\n", span->span_id);

    vmx_segment_t *seg = vmx_segment_alloc(sess, 200, 8192);
    printf("Segment %u attached (%zu bytes capacity)\n",
           seg->segment_id, seg->capacity);

    /* Write some data */
    uint8_t data[256];
    for (int i = 0; i < 256; i++) data[i] = (uint8_t)(i * 37);
    vmx_segment_write(seg, data, 256);
    printf("Wrote 256 bytes to segment\n");

    /* Initiate migration */
    sess->negotiated = true;  /* skip cap_neg for demo */
    sess->capabilities |= VMX_CAP_MIGRATION;
    int rc = vmx_migrate_initiate(sess, 200, 42);
    printf("Migration initiated: rc=%d, target_node=42\n", rc);

    vmx_migration_t *mig = vmx_migrate_find_by_span(sess, 200);
    if (mig) {
        printf("Migration %u: phase=%d, span=%u\n",
               mig->migration_id, mig->phase, mig->span_id);
    }

    printf("\nDone. (Migration would continue with prepare, transfer, handoff)\n");

    vmx_context_destroy(ctx);
    return 0;
}
