#include "pool.h"
#include "alloc.h"
#include "bitops.h"
#include <string.h>

vmx_pool_t *vmx_pool_create(size_t block_size, size_t block_count) {
    if (block_size < sizeof(vmx_pool_block_t))
        block_size = sizeof(vmx_pool_block_t);

    /* Align block size to 8 bytes */
    block_size = vmx_align_up((uint32_t)block_size, 8);

    size_t arena_size = block_size * block_count;
    if (arena_size == 0) return NULL;

    vmx_pool_t *pool = vmx_calloc(1, sizeof(vmx_pool_t));
    if (!pool) return NULL;

    pool->arena = vmx_alloc(arena_size);
    if (!pool->arena) { vmx_free(pool); return NULL; }

    memset(pool->arena, 0, arena_size);
    pool->block_size = block_size;
    pool->block_count = block_count;
    pool->arena_size = arena_size;
    pool->used_count = 0;

    /* Build free list */
    pool->free_list = NULL;
    for (size_t i = 0; i < block_count; i++) {
        vmx_pool_block_t *blk = (vmx_pool_block_t *)(pool->arena + i * block_size);
        blk->next = pool->free_list;
        pool->free_list = blk;
    }

    return pool;
}

void vmx_pool_destroy(vmx_pool_t *pool) {
    if (!pool) return;
    vmx_free(pool->arena);
    vmx_free(pool);
}

void *vmx_pool_alloc(vmx_pool_t *pool) {
    if (!pool || !pool->free_list) return NULL;

    vmx_pool_block_t *blk = pool->free_list;
    pool->free_list = blk->next;
    pool->used_count++;

    memset(blk, 0, pool->block_size);
    return blk;
}

void vmx_pool_free(vmx_pool_t *pool, void *ptr) {
    if (!pool || !ptr) return;

    /* Verify ptr is within arena bounds */
    uint8_t *p = (uint8_t *)ptr;
    if (p < pool->arena || p >= pool->arena + pool->arena_size) return;

    vmx_pool_block_t *blk = (vmx_pool_block_t *)ptr;
    blk->next = pool->free_list;
    pool->free_list = blk;
    pool->used_count--;
}

size_t vmx_pool_used(const vmx_pool_t *pool) {
    return pool ? pool->used_count : 0;
}

size_t vmx_pool_available(const vmx_pool_t *pool) {
    return pool ? pool->block_count - pool->used_count : 0;
}
