#include "hashtable.h"
#include "alloc.h"
#include "util.h"

static uint32_t ht_hash(uint64_t key) {
    uint32_t lo = (uint32_t)(key & 0xFFFFFFFF);
    uint32_t hi = (uint32_t)(key >> 32);
    uint32_t h = vmx_hash_mix(lo, hi);
    h ^= h >> 16;
    h *= 0x45d9f3b;
    h ^= h >> 16;
    return h;
}

vmx_hashtable_t *vmx_ht_create(uint32_t initial_cap) {
    if (initial_cap < VMX_HT_INITIAL_CAP)
        initial_cap = VMX_HT_INITIAL_CAP;
    initial_cap = vmx_next_power_of_2(initial_cap);

    vmx_hashtable_t *ht = vmx_calloc(1, sizeof(vmx_hashtable_t));
    if (!ht) return NULL;

    ht->buckets = vmx_calloc(initial_cap, sizeof(vmx_ht_entry_t *));
    if (!ht->buckets) { vmx_free(ht); return NULL; }

    ht->capacity = initial_cap;
    ht->count = 0;
    ht->mask = initial_cap - 1;
    return ht;
}

void vmx_ht_destroy(vmx_hashtable_t *ht) {
    if (!ht) return;
    for (uint32_t i = 0; i < ht->capacity; i++) {
        vmx_ht_entry_t *e = ht->buckets[i];
        while (e) {
            vmx_ht_entry_t *next = e->next;
            vmx_free(e);
            e = next;
        }
    }
    vmx_free(ht->buckets);
    vmx_free(ht);
}

int vmx_ht_insert(vmx_hashtable_t *ht, uint64_t key, void *value) {
    if (!ht) return -1;

    /* Check load factor and resize if needed */
    if (ht->count * VMX_HT_LOAD_FACTOR_DEN >=
        ht->capacity * VMX_HT_LOAD_FACTOR_NUM) {
        vmx_ht_resize(ht, ht->capacity * 2);
    }

    uint32_t h = ht_hash(key);
    uint32_t idx = h & ht->mask;

    /* Check for existing key */
    vmx_ht_entry_t *e = ht->buckets[idx];
    while (e) {
        if (e->key == key && e->occupied) {
            e->value = value;  /* update */
            return 0;
        }
        e = e->next;
    }

    /* Insert new entry at head */
    vmx_ht_entry_t *ne = vmx_calloc(1, sizeof(vmx_ht_entry_t));
    if (!ne) return -1;

    ne->key = key;
    ne->value = value;
    ne->hash = h;
    ne->occupied = true;
    ne->next = ht->buckets[idx];
    ht->buckets[idx] = ne;
    ht->count++;

    return 0;
}

void *vmx_ht_lookup(vmx_hashtable_t *ht, uint64_t key) {
    if (!ht) return NULL;

    uint32_t h = ht_hash(key);
    uint32_t idx = h & ht->mask;

    vmx_ht_entry_t *e = ht->buckets[idx];
    while (e) {
        if (e->key == key && e->occupied)
            return e->value;
        e = e->next;
    }
    return NULL;
}

int vmx_ht_remove(vmx_hashtable_t *ht, uint64_t key) {
    if (!ht) return -1;

    uint32_t h = ht_hash(key);
    uint32_t idx = h & ht->mask;

    vmx_ht_entry_t **pp = &ht->buckets[idx];
    while (*pp) {
        if ((*pp)->key == key && (*pp)->occupied) {
            vmx_ht_entry_t *victim = *pp;
            *pp = victim->next;
            vmx_free(victim);
            ht->count--;
            return 0;
        }
        pp = &(*pp)->next;
    }
    return -1;
}

int vmx_ht_resize(vmx_hashtable_t *ht, uint32_t new_cap) {
    if (!ht || new_cap < ht->count) return -1;
    new_cap = vmx_next_power_of_2(new_cap);

    vmx_ht_entry_t **new_buckets = vmx_calloc(new_cap, sizeof(vmx_ht_entry_t *));
    if (!new_buckets) return -1;

    uint32_t new_mask = new_cap - 1;

    /* Rehash all entries */
    for (uint32_t i = 0; i < ht->capacity; i++) {
        vmx_ht_entry_t *e = ht->buckets[i];
        while (e) {
            vmx_ht_entry_t *next = e->next;
            uint32_t new_idx = e->hash & new_mask;
            e->next = new_buckets[new_idx];
            new_buckets[new_idx] = e;
            e = next;
        }
    }

    vmx_free(ht->buckets);
    ht->buckets = new_buckets;
    ht->capacity = new_cap;
    ht->mask = new_mask;

    return 0;
}

uint32_t vmx_ht_count(const vmx_hashtable_t *ht) {
    return ht ? ht->count : 0;
}

bool vmx_ht_contains(vmx_hashtable_t *ht, uint64_t key) {
    return vmx_ht_lookup(ht, key) != NULL;
}

void vmx_ht_iterate(vmx_hashtable_t *ht, vmx_ht_iter_fn fn, void *ctx) {
    if (!ht || !fn) return;
    for (uint32_t i = 0; i < ht->capacity; i++) {
        vmx_ht_entry_t *e = ht->buckets[i];
        while (e) {
            if (e->occupied) fn(e->key, e->value, ctx);
            e = e->next;
        }
    }
}
