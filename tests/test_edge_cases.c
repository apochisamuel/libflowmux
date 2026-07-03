/*
 * Edge case tests for libvaultmux protocol handling.
 * Tests boundary conditions, error paths, and unusual sequences.
 */
#include "../include/vaultmux.h"
#include "../src/common/types.h"
#include "../src/common/util.h"
#include "../src/common/crc.h"
#include <stdio.h>
#include <string.h>

static int p = 0, f = 0;
#define T(n) printf("  %s... ", #n)
#define P() do { printf("PASS\n"); p++; } while(0)
#define F(m) do { printf("FAIL: %s\n", m); f++; } while(0)

static void test_empty_input(void) {
    T(empty_input);
    vmx_context_t *ctx = vmx_context_create();
    vmx_session_t *sess = vmx_session_create(ctx, VMX_CAP_CHANNEL);
    int rc = vmx_ingest_frames(sess, NULL, 0);
    if (rc <= 0) P(); else F("accepted null");
    vmx_context_destroy(ctx);
}

static void test_truncated_header(void) {
    T(truncated_header);
    vmx_context_t *ctx = vmx_context_create();
    vmx_session_t *sess = vmx_session_create(ctx, VMX_CAP_CHANNEL);
    uint8_t buf[10] = {0};
    int rc = vmx_ingest_frames(sess, buf, 10);
    if (rc <= 0) P(); else F("accepted truncated");
    vmx_context_destroy(ctx);
}

static void test_bad_magic(void) {
    T(bad_magic);
    vmx_context_t *ctx = vmx_context_create();
    vmx_session_t *sess = vmx_session_create(ctx, VMX_CAP_CHANNEL);
    uint8_t buf[VMX_WIRE_HEADER_SIZE + VMX_WIRE_FRAME_HEADER_SIZE];
    memset(buf, 0xFF, sizeof(buf));
    int rc = vmx_ingest_frames(sess, buf, sizeof(buf));
    if (rc <= 0) P(); else F("accepted bad magic");
    vmx_context_destroy(ctx);
}

static void test_bad_crc(void) {
    T(bad_header_crc);
    vmx_context_t *ctx = vmx_context_create();
    vmx_session_t *sess = vmx_session_create(ctx, VMX_CAP_CHANNEL);
    uint8_t buf[VMX_WIRE_HEADER_SIZE + VMX_WIRE_FRAME_HEADER_SIZE];
    vmx_write_u32(buf, VMX_MAGIC);
    vmx_write_u16(buf + 4, 0x0100);
    vmx_write_u32(buf + 6, 1);
    vmx_write_u32(buf + 10, 0);
    vmx_write_u16(buf + 14, 1);
    vmx_write_u32(buf + 16, 0xDEADBEEF); /* wrong CRC */
    int rc = vmx_ingest_frames(sess, buf, sizeof(buf));
    if (rc <= 0) P(); else F("accepted bad CRC");
    vmx_context_destroy(ctx);
}

static void test_span_double_create(void) {
    T(span_double_create);
    vmx_context_t *ctx = vmx_context_create();
    vmx_session_t *sess = vmx_session_create(ctx, VMX_CAP_CHANNEL);
    sess->initialized = true; sess->negotiated = true;
    vmx_span_t *s1 = vmx_span_registry_create(sess, 42, 0x1111);
    vmx_span_t *s2 = vmx_span_registry_lookup(sess, 42);
    if (s1 == s2 && s1 != NULL) P(); else F("mismatch");
    vmx_context_destroy(ctx);
}

static void test_cursor_no_data(void) {
    T(cursor_materialize_empty);
    vmx_context_t *ctx = vmx_context_create();
    vmx_session_t *sess = vmx_session_create(ctx, VMX_CAP_CHANNEL | VMX_CAP_CURSOR);
    sess->initialized = true; sess->negotiated = true;
    vmx_span_t *span = vmx_span_registry_create(sess, 50, 0x2222);
    vmx_segment_alloc(sess, 50, 1024);
    /* Don't write any data */
    vmx_lease_t *l = vmx_lease_acquire(sess, 50, 0x2222);
    vmx_cursor_t *c = vmx_cursor_open(sess, 50, l->lease_id);
    vmx_cursor_advance_to_segment(sess, c, 0);
    uint8_t buf[256];
    size_t bytes = 0;
    int rc = vmx_cursor_materialize(sess, c, buf, 256, &bytes);
    if (rc == VMX_OK && bytes == 0) P(); else F("non-zero bytes on empty");
    vmx_context_destroy(ctx);
}

static void test_segment_overflow(void) {
    T(segment_write_overflow);
    vmx_context_t *ctx = vmx_context_create();
    vmx_session_t *sess = vmx_session_create(ctx, VMX_CAP_CHANNEL);
    sess->initialized = true; sess->negotiated = true;
    vmx_span_registry_create(sess, 60, 0x3333);
    vmx_segment_t *seg = vmx_segment_alloc(sess, 60, 32);
    uint8_t data[64];
    memset(data, 0xAA, 64);
    int rc = vmx_segment_write(seg, data, 64);
    if (rc != VMX_OK) P(); else F("accepted overflow");
    vmx_context_destroy(ctx);
}

static void test_lease_double_release(void) {
    T(lease_double_release);
    vmx_context_t *ctx = vmx_context_create();
    vmx_session_t *sess = vmx_session_create(ctx, VMX_CAP_CHANNEL);
    sess->initialized = true; sess->negotiated = true;
    vmx_span_registry_create(sess, 70, 0x4444);
    vmx_segment_alloc(sess, 70, 1024);
    vmx_lease_t *l = vmx_lease_acquire(sess, 70, 0x4444);
    vmx_lease_release(sess, l);
    /* Second release should be safe (no crash) */
    int rc = vmx_lease_release(sess, l);
    P(); /* no crash = pass */
    (void)rc;
    vmx_context_destroy(ctx);
}

static void test_channel_bind_nonexistent(void) {
    T(channel_bind_nonexistent_span);
    vmx_context_t *ctx = vmx_context_create();
    vmx_session_t *sess = vmx_session_create(ctx, VMX_CAP_CHANNEL);
    sess->initialized = true; sess->negotiated = true;
    vmx_channel_t *ch = vmx_channel_mux_alloc(sess, 99);
    int rc = vmx_channel_bind_span(sess, ch, 99999, 0);
    if (rc != VMX_OK) P(); else F("bound to nonexistent");
    vmx_context_destroy(ctx);
}

static void test_migration_without_cap(void) {
    T(migrate_without_capability);
    vmx_context_t *ctx = vmx_context_create();
    vmx_session_t *sess = vmx_session_create(ctx, VMX_CAP_CHANNEL);
    sess->initialized = true; sess->negotiated = true;
    vmx_span_registry_create(sess, 80, 0x5555);
    int rc = vmx_migrate_initiate(sess, 80, 42);
    if (rc != VMX_OK) P(); else F("migrated without cap");
    vmx_context_destroy(ctx);
}

static void test_context_null_safety(void) {
    T(null_safety);
    /* All functions should handle NULL gracefully */
    vmx_span_registry_lookup(NULL, 1);
    vmx_lease_lookup(NULL, 1);
    vmx_cursor_lookup(NULL, 1);
    vmx_channel_lookup(NULL, 1);
    vmx_stream_lookup(NULL, 1);
    vmx_migrate_lookup(NULL, 1);
    vmx_repl_lookup(NULL, 1);
    vmx_checkpoint_lookup(NULL, 1);
    vmx_node_lookup(NULL, 1);
    vmx_route_lookup(NULL, 1);
    P();
}

static void test_max_capacity(void) {
    T(max_spans_capacity);
    vmx_context_t *ctx = vmx_context_create();
    vmx_session_t *sess = vmx_session_create(ctx, VMX_CAP_CHANNEL);
    sess->initialized = true; sess->negotiated = true;
    int created = 0;
    for (uint32_t i = 1; i <= VMX_MAX_SPANS + 5; i++) {
        vmx_span_t *s = vmx_span_registry_create(sess, i, 0x6666);
        if (s) created++;
    }
    if (created == VMX_MAX_SPANS) P();
    else F("capacity not enforced");
    vmx_context_destroy(ctx);
}

int main(void) {
    printf("libvaultmux edge case tests\n");
    printf("===========================\n");
    test_empty_input();
    test_truncated_header();
    test_bad_magic();
    test_bad_crc();
    test_span_double_create();
    test_cursor_no_data();
    test_segment_overflow();
    test_lease_double_release();
    test_channel_bind_nonexistent();
    test_migration_without_cap();
    test_context_null_safety();
    test_max_capacity();
    printf("\nResults: %d passed, %d failed\n", p, f);
    return f > 0 ? 1 : 0;
}
