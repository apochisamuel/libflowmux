/*
 * Example: basic frame ingestion workflow.
 * Demonstrates session creation, capability negotiation, and span management.
 */
#include "../include/vaultmux.h"
#include "../src/common/types.h"
#include "../src/common/util.h"
#include "../src/common/crc.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    printf("libvaultmux ingest demo\n");
    printf("======================\n\n");

    /* Create context and session */
    vmx_context_t *ctx = vmx_context_create();
    uint32_t caps = VMX_CAP_CHANNEL | VMX_CAP_STREAM | VMX_CAP_CURSOR;
    vmx_session_t *sess = vmx_session_create(ctx, caps);

    printf("Session created: id=%u, epoch=%u\n",
           vmx_session_get_id(sess), vmx_session_get_epoch(sess));

    /* Build a minimal valid session init + cap_neg message */
    uint8_t msg[512];
    size_t off = 0;

    /* Header */
    uint32_t sid = vmx_session_get_id(sess);
    vmx_write_u32(msg, VMX_MAGIC);
    vmx_write_u16(msg + 4, 0x0100);
    vmx_write_u32(msg + 6, sid);
    vmx_write_u32(msg + 10, 0);
    vmx_write_u16(msg + 14, 1);
    vmx_write_u32(msg + 16, vmx_crc32c(msg, 14));
    off = VMX_WIRE_HEADER_SIZE;

    /* Session init frame */
    uint16_t frame_len = VMX_WIRE_FRAME_HEADER_SIZE;
    vmx_write_u16(msg + off, frame_len);
    msg[off + 2] = VMX_FRAME_SESSION_INIT;
    vmx_write_u16(msg + off + 3, 0);
    vmx_write_u32(msg + off + 5, sid);
    vmx_write_u16(msg + off + 9, 0);
    vmx_write_u16(msg + off + 11, 0);
    vmx_write_u32(msg + off + 13, 0);
    vmx_write_u16(msg + off + 17, 0);
    msg[off + 19] = 0;

    int rc = vmx_ingest_frames(sess, msg, off + frame_len);
    printf("Ingested %d frames (session init)\n", rc);
    printf("Session initialized: id=%u\n", vmx_session_get_id(sess));

    vmx_context_destroy(ctx);
    printf("\nDone.\n");
    return 0;
}
