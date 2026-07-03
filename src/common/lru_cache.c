#include "alloc.h"
#include "util.h"
#include <string.h>
#include <stdbool.h>

/*
 * LRU (Least Recently Used) cache with O(1) lookup and eviction.
 * Used by cursor prefetch and segment content caching.
 *
 * Implementation: doubly-linked list + hash array for O(1) operations.
 */

#define VMX_LRU_HASH_BUCKETS 64
#define VMX_LRU_MAX_ENTRIES  256

typedef struct vmx_lru_node {
    uint64_t key;
    void    *value;
    struct vmx_lru_node *prev;
    struct vmx_lru_node *next;
    struct vmx_lru_node *hash_next;
    uint32_t hash_bucket;
    bool     occupied;
} vmx_lru_node_t;

typedef struct {
    vmx_lru_node_t  nodes[VMX_LRU_MAX_ENTRIES];
    vmx_lru_node_t *hash_heads[VMX_LRU_HASH_BUCKETS];
    vmx_lru_node_t *head;  /* most recently used */
    vmx_lru_node_t *tail;  /* least recently used */
    vmx_lru_node_t *free_list;
    uint32_t        count;
    uint32_t        capacity;
    uint64_t        hits;
    uint64_t        misses;
    uint64_t        evictions;
} vmx_lru_cache_t;

static uint32_t lru_hash(uint64_t key) {
    uint32_t h = (uint32_t)(key ^ (key >> 32));
    h = ((h >> 16) ^ h) * 0x45d9f3b;
    h = ((h >> 16) ^ h) * 0x45d9f3b;
    h = (h >> 16) ^ h;
    return h % VMX_LRU_HASH_BUCKETS;
}

int vmx_lru_init(vmx_lru_cache_t *cache, uint32_t capacity) {
    if (!cache) return -1;
    memset(cache, 0, sizeof(*cache));
    if (capacity > VMX_LRU_MAX_ENTRIES) capacity = VMX_LRU_MAX_ENTRIES;
    cache->capacity = capacity;

    /* Build free list */
    for (uint32_t i = 0; i < capacity; i++) {
        cache->nodes[i].next = cache->free_list;
        cache->free_list = &cache->nodes[i];
    }
    return 0;
}

static void lru_remove_from_list(vmx_lru_cache_t *cache, vmx_lru_node_t *node) {
    if (node->prev) node->prev->next = node->next;
    else cache->head = node->next;
    if (node->next) node->next->prev = node->prev;
    else cache->tail = node->prev;
    node->prev = node->next = NULL;
}

static void lru_push_front(vmx_lru_cache_t *cache, vmx_lru_node_t *node) {
    node->next = cache->head;
    node->prev = NULL;
    if (cache->head) cache->head->prev = node;
    cache->head = node;
    if (!cache->tail) cache->tail = node;
}

static void lru_remove_from_hash(vmx_lru_cache_t *cache, vmx_lru_node_t *node) {
    uint32_t bucket = node->hash_bucket;
    vmx_lru_node_t **pp = &cache->hash_heads[bucket];
    while (*pp) {
        if (*pp == node) { *pp = node->hash_next; return; }
        pp = &(*pp)->hash_next;
    }
}

void *vmx_lru_get(vmx_lru_cache_t *cache, uint64_t key) {
    if (!cache) return NULL;
    uint32_t bucket = lru_hash(key);
    vmx_lru_node_t *node = cache->hash_heads[bucket];
    while (node) {
        if (node->key == key && node->occupied) {
            /* Move to front (most recently used) */
            lru_remove_from_list(cache, node);
            lru_push_front(cache, node);
            cache->hits++;
            return node->value;
        }
        node = node->hash_next;
    }
    cache->misses++;
    return NULL;
}

int vmx_lru_put(vmx_lru_cache_t *cache, uint64_t key, void *value) {
    if (!cache) return -1;

    /* Check if key already exists */
    uint32_t bucket = lru_hash(key);
    vmx_lru_node_t *existing = cache->hash_heads[bucket];
    while (existing) {
        if (existing->key == key && existing->occupied) {
            existing->value = value;
            lru_remove_from_list(cache, existing);
            lru_push_front(cache, existing);
            return 0;
        }
        existing = existing->hash_next;
    }

    /* Need a new node */
    vmx_lru_node_t *node = cache->free_list;
    if (!node) {
        /* Evict LRU (tail) */
        node = cache->tail;
        if (!node) return -1;
        lru_remove_from_list(cache, node);
        lru_remove_from_hash(cache, node);
        node->occupied = false;
        cache->count--;
        cache->evictions++;
    } else {
        cache->free_list = node->next;
        node->next = NULL;
    }

    /* Insert new entry */
    node->key = key;
    node->value = value;
    node->occupied = true;
    node->hash_bucket = bucket;
    node->hash_next = cache->hash_heads[bucket];
    cache->hash_heads[bucket] = node;
    lru_push_front(cache, node);
    cache->count++;

    return 0;
}

int vmx_lru_remove(vmx_lru_cache_t *cache, uint64_t key) {
    if (!cache) return -1;
    uint32_t bucket = lru_hash(key);
    vmx_lru_node_t *node = cache->hash_heads[bucket];
    while (node) {
        if (node->key == key && node->occupied) {
            lru_remove_from_list(cache, node);
            lru_remove_from_hash(cache, node);
            node->occupied = false;
            node->next = cache->free_list;
            cache->free_list = node;
            cache->count--;
            return 0;
        }
        node = node->hash_next;
    }
    return -1;
}

void vmx_lru_clear(vmx_lru_cache_t *cache) {
    if (!cache) return;
    memset(cache->hash_heads, 0, sizeof(cache->hash_heads));
    cache->head = cache->tail = NULL;
    cache->free_list = NULL;
    cache->count = 0;
    for (uint32_t i = 0; i < cache->capacity; i++) {
        cache->nodes[i].occupied = false;
        cache->nodes[i].prev = cache->nodes[i].next = NULL;
        cache->nodes[i].hash_next = NULL;
        cache->nodes[i].next = cache->free_list;
        cache->free_list = &cache->nodes[i];
    }
}

int vmx_lru_hit_rate(const vmx_lru_cache_t *cache) {
    if (!cache) return 0;
    uint64_t total = cache->hits + cache->misses;
    if (total == 0) return 0;
    return (int)((cache->hits * 1000) / total);
}

uint32_t vmx_lru_count(const vmx_lru_cache_t *cache) {
    return cache ? cache->count : 0;
}

int vmx_lru_utilization(const vmx_lru_cache_t *cache) {
    if (!cache || cache->capacity == 0) return 0;
    return (int)((cache->count * 1000) / cache->capacity);
}

/*
 * Batch insert: efficiently insert multiple key-value pairs.
 * Avoids repeated overhead of individual inserts.
 */
int vmx_lru_batch_insert(vmx_lru_cache_t *cache,
                         const uint64_t *keys,
                         void **values,
                         uint32_t count) {
    if (!cache || !keys || !values) return -1;
    int inserted = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (vmx_lru_put(cache, keys[i], values[i]) == 0)
            inserted++;
    }
    return inserted;
}

/*
 * Batch lookup: check presence of multiple keys at once.
 * out_results[i] is non-NULL if keys[i] was found.
 */
int vmx_lru_batch_lookup(vmx_lru_cache_t *cache,
                         const uint64_t *keys,
                         void **out_results,
                         uint32_t count) {
    if (!cache || !keys || !out_results) return -1;
    int found = 0;
    for (uint32_t i = 0; i < count; i++) {
        out_results[i] = vmx_lru_get(cache, keys[i]);
        if (out_results[i]) found++;
    }
    return found;
}

/*
 * Compute cache efficiency report.
 * Returns a score 0-100 based on hit rate, utilization, and eviction rate.
 */
int vmx_lru_efficiency_score(const vmx_lru_cache_t *cache) {
    if (!cache) return 0;
    int score = 0;

    /* Hit rate contribution (0-40) */
    int hr = vmx_lru_hit_rate(cache);
    score += (hr * 40) / 1000;

    /* Utilization contribution (0-30) */
    int util = vmx_lru_utilization(cache);
    score += (util * 30) / 1000;

    /* Low eviction rate contribution (0-30) */
    uint64_t total_ops = cache->hits + cache->misses;
    if (total_ops > 0) {
        int evict_rate = (int)((cache->evictions * 1000) / total_ops);
        int evict_score = 30 - (evict_rate * 30) / 1000;
        if (evict_score < 0) evict_score = 0;
        score += evict_score;
    } else {
        score += 30;
    }

    return score > 100 ? 100 : score;
}
