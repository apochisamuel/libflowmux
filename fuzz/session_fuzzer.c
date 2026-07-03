/*
 * Session fuzzer harness for libvaultmux.
 * Exercises: protocol, session, lease, retention, span, segment,
 *            replication, checkpoint subsystems.
 */
#include "../include/vaultmux.h"
#include "../src/common/types.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < VMX_WIRE_HEADER_SIZE + VMX_WIRE_FRAME_HEADER_SIZE)
        return 0;

    vmx_context_t *ctx = vmx_context_create();
    if (!ctx) return 0;

    uint32_t caps = VMX_CAP_CHANNEL | VMX_CAP_STREAM | VMX_CAP_CURSOR | VMX_CAP_REPLICATION | VMX_CAP_MIGRATION | VMX_CAP_CHECKPOINT | VMX_CAP_RETENTION | VMX_CAP_BACKPRESSURE | VMX_CAP_CRYPTO;
    vmx_session_t *sess = vmx_session_create(ctx, caps);
    if (!sess) {
        vmx_context_destroy(ctx);
        return 0;
    }

    vmx_ingest_frames(sess, data, size);

    vmx_session_destroy(sess);
    free(ctx);
    return 0;
}
