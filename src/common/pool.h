#ifndef VMX_POOL_H
#define VMX_POOL_H
#include <stdint.h>
#include <stddef.h>

typedef struct vmx_pool_block {
    struct vmx_pool_block *next;
} vmx_pool_block_t;

typedef struct {
    vmx_pool_block_t *free_list;
    uint8_t          *arena;
    size_t            block_size;
    size_t            block_count;
    size_t            arena_size;
    size_t            used_count;
} vmx_pool_t;

vmx_pool_t *vmx_pool_create(size_t block_size, size_t block_count);
void        vmx_pool_destroy(vmx_pool_t *pool);
void       *vmx_pool_alloc(vmx_pool_t *pool);
void        vmx_pool_free(vmx_pool_t *pool, void *ptr);
size_t      vmx_pool_used(const vmx_pool_t *pool);
size_t      vmx_pool_available(const vmx_pool_t *pool);

#endif
