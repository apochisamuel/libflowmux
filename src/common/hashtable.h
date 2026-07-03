#ifndef VMX_HASHTABLE_H
#define VMX_HASHTABLE_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define VMX_HT_INITIAL_CAP 16
#define VMX_HT_LOAD_FACTOR_NUM 3
#define VMX_HT_LOAD_FACTOR_DEN 4

typedef struct vmx_ht_entry {
    uint64_t          key;
    void             *value;
    struct vmx_ht_entry *next;
    uint32_t          hash;
    bool              occupied;
} vmx_ht_entry_t;

typedef struct {
    vmx_ht_entry_t **buckets;
    uint32_t         capacity;
    uint32_t         count;
    uint32_t         mask;
} vmx_hashtable_t;

vmx_hashtable_t *vmx_ht_create(uint32_t initial_cap);
void             vmx_ht_destroy(vmx_hashtable_t *ht);
int              vmx_ht_insert(vmx_hashtable_t *ht, uint64_t key, void *value);
void            *vmx_ht_lookup(vmx_hashtable_t *ht, uint64_t key);
int              vmx_ht_remove(vmx_hashtable_t *ht, uint64_t key);
int              vmx_ht_resize(vmx_hashtable_t *ht, uint32_t new_cap);
uint32_t         vmx_ht_count(const vmx_hashtable_t *ht);
bool             vmx_ht_contains(vmx_hashtable_t *ht, uint64_t key);

typedef void (*vmx_ht_iter_fn)(uint64_t key, void *value, void *ctx);
void vmx_ht_iterate(vmx_hashtable_t *ht, vmx_ht_iter_fn fn, void *ctx);

#endif
