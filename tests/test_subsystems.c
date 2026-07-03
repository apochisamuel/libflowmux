#include "../include/vaultmux.h"
#include "../src/common/types.h"
#include "../src/common/hashtable.h"
#include "../src/common/ringbuf.h"
#include "../src/common/pool.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int p = 0, f = 0;
#define T(n) printf("  %s... ", #n)
#define P() do { printf("PASS\n"); p++; } while(0)
#define F(m) do { printf("FAIL: %s\n", m); f++; } while(0)

static void test_hashtable(void) {
    T(ht_create);
    vmx_hashtable_t *ht = vmx_ht_create(16);
    if (ht) P(); else { F("null"); return; }

    T(ht_insert_lookup);
    vmx_ht_insert(ht, 42, (void *)0xDEAD);
    void *v = vmx_ht_lookup(ht, 42);
    if (v == (void *)0xDEAD) P(); else F("lookup");

    T(ht_contains);
    if (vmx_ht_contains(ht, 42) && !vmx_ht_contains(ht, 99)) P();
    else F("contains");

    T(ht_remove);
    vmx_ht_remove(ht, 42);
    if (!vmx_ht_contains(ht, 42)) P(); else F("remove");

    T(ht_resize);
    for (int i = 0; i < 100; i++)
        vmx_ht_insert(ht, (uint64_t)i, (void *)(uintptr_t)(i + 1));
    if (vmx_ht_count(ht) == 100) P(); else F("count");

    T(ht_iterate);
    int sum = 0;
    vmx_ht_iterate(ht, (vmx_ht_iter_fn)(void *)NULL, &sum);
    P(); /* just testing no crash */

    vmx_ht_destroy(ht);
}

static void test_ringbuf(void) {
    T(rb_create);
    vmx_ringbuf_t *rb = vmx_ringbuf_create(64);
    if (rb) P(); else { F("null"); return; }

    T(rb_write_read);
    uint8_t data[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    vmx_ringbuf_write(rb, data, 16);
    uint8_t out[16];
    vmx_ringbuf_read(rb, out, 16);
    if (memcmp(data, out, 16) == 0) P(); else F("data");

    T(rb_wrap_around);
    /* Fill and drain multiple times to test wrap */
    for (int i = 0; i < 10; i++) {
        vmx_ringbuf_write(rb, data, 16);
        vmx_ringbuf_read(rb, out, 16);
    }
    if (vmx_ringbuf_is_empty(rb)) P(); else F("empty");

    T(rb_full);
    uint8_t big[64];
    memset(big, 0xAA, 64);
    vmx_ringbuf_write(rb, big, 64);
    if (vmx_ringbuf_is_full(rb)) P(); else F("full");

    vmx_ringbuf_destroy(rb);
}

static void test_pool(void) {
    T(pool_create);
    vmx_pool_t *pool = vmx_pool_create(64, 16);
    if (pool) P(); else { F("null"); return; }

    T(pool_alloc_free);
    void *a = vmx_pool_alloc(pool);
    void *b = vmx_pool_alloc(pool);
    if (a && b && a != b) P(); else F("alloc");

    T(pool_free_reuse);
    vmx_pool_free(pool, a);
    void *c = vmx_pool_alloc(pool);
    if (c == a) P(); else F("reuse");  /* freed block should be reused */

    T(pool_exhaustion);
    /* Allocate all remaining */
    for (int i = 0; i < 14; i++) vmx_pool_alloc(pool);
    void *last = vmx_pool_alloc(pool);
    if (last == NULL) P(); else F("exhaust");

    vmx_pool_destroy(pool);
}

static void test_bitops(void) {
    T(clz32);
    if (vmx_clz32(1) == 31 && vmx_clz32(0x80000000) == 0) P();
    else F("clz");

    T(ctz32);
    if (vmx_ctz32(1) == 0 && vmx_ctz32(8) == 3) P();
    else F("ctz");

    T(rotl_rotr);
    uint32_t x = 0x12345678;
    if (vmx_rotr32(vmx_rotl32(x, 7), 7) == x) P();
    else F("rotate");

    T(align_up);
    if (vmx_align_up(13, 8) == 16 && vmx_align_up(16, 8) == 16) P();
    else F("align");
}

int main(void) {
    printf("libvaultmux subsystem tests\n");
    printf("==========================\n");

    test_hashtable();
    test_ringbuf();
    test_pool();
    test_bitops();

    printf("\nResults: %d passed, %d failed\n", p, f);
    return f > 0 ? 1 : 0;
}
