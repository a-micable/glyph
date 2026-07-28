/*
 * Advanced Cache Implementation - Multi-Strategy Caching
 *
 * Cache entries are stored in a hash table and linked LRU list. The public
 * API exposes cache operations, statistics, persistence hooks, and strategy
 * configuration.
 */

#include "advanced_cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#define CACHE_HASHTABLE_SIZE 10007  /* Prime number for better distribution */
#define CACHE_DEFAULT_MAX_SIZE (100 * 1024 * 1024)  /* 100MB */
#define CACHE_DEFAULT_MAX_ENTRIES 100000

typedef struct cache_entry_s {
    char key[256];
    uint32_t key_hash;
    void *value;
    size_t size;
    time_t created_at;
    time_t last_accessed;
    uint64_t access_count;
    int compressed;
    struct cache_entry_s *prev;
    struct cache_entry_s *next;
    struct cache_entry_s *hash_next;
} cache_entry_t;

typedef struct cache_s {
    cache_config_t config;
    
    cache_entry_t **hash_table;
    cache_entry_t *lru_head;
    cache_entry_t *lru_tail;
    
    size_t current_size;
    int entry_count;
    
    cache_stats_t stats;
    
    pthread_rwlock_t lock;
    
    cache_eviction_callback_t eviction_callback;
    void *eviction_context;
} cache_t_impl;

/* Hash function */
static uint32_t hash_key(const char *key) {
    uint32_t hash = 5381;
    int c;
    while ((c = *key++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

/* LRU management */
static void move_to_front(cache_t_impl *cache, cache_entry_t *entry) {
    if (entry == cache->lru_head) return;
    
    if (entry->prev) entry->prev->next = entry->next;
    if (entry->next) entry->next->prev = entry->prev;
    if (entry == cache->lru_tail) cache->lru_tail = entry->prev;
    
    entry->prev = NULL;
    entry->next = cache->lru_head;
    if (cache->lru_head) cache->lru_head->prev = entry;
    cache->lru_head = entry;
    
    if (!cache->lru_tail) cache->lru_tail = entry;
}

static void remove_entry(cache_t_impl *cache, cache_entry_t *entry) {
    if (entry->prev) entry->prev->next = entry->next;
    if (entry->next) entry->next->prev = entry->prev;
    if (entry == cache->lru_head) cache->lru_head = entry->next;
    if (entry == cache->lru_tail) cache->lru_tail = entry->prev;
}

/* Public API */
cache_handle_t cache_create(const cache_config_t *config) {
    cache_t_impl *cache = calloc(1, sizeof(cache_t_impl));
    if (!cache) return NULL;
    
    if (config) {
        memcpy(&cache->config, config, sizeof(cache_config_t));
    } else {
        cache->config.strategy = CACHE_STRATEGY_LRU;
        cache->config.max_size = CACHE_DEFAULT_MAX_SIZE;
        cache->config.max_entries = CACHE_DEFAULT_MAX_ENTRIES;
        cache->config.ttl = 3600;  /* 1 hour */
    }
    
    cache->hash_table = calloc(CACHE_HASHTABLE_SIZE, sizeof(cache_entry_t *));
    if (!cache->hash_table) {
        free(cache);
        return NULL;
    }
    
    pthread_rwlock_init(&cache->lock, NULL);
    cache->stats.cache_created = time(NULL);
    cache->stats.max_bytes = cache->config.max_size;
    
    return (cache_handle_t)cache;
}

void cache_destroy(cache_handle_t handle) {
    cache_t_impl *cache = (cache_t_impl *)handle;
    if (!cache) return;
    
    pthread_rwlock_wrlock(&cache->lock);
    
    cache_entry_t *entry = cache->lru_head;
    while (entry) {
        cache_entry_t *next = entry->next;
        free(entry->value);
        free(entry);
        entry = next;
    }
    
    free(cache->hash_table);
    pthread_rwlock_unlock(&cache->lock);
    pthread_rwlock_destroy(&cache->lock);
    free(cache);
}

int cache_put(cache_handle_t handle, const char *key, const void *value,
             size_t size) {
    cache_t_impl *cache = (cache_t_impl *)handle;
    if (!cache || !key || !value || size <= 0) return -1;
    
    pthread_rwlock_wrlock(&cache->lock);
    
    uint32_t hash = hash_key(key);
    uint32_t idx = hash % CACHE_HASHTABLE_SIZE;
    
    cache_entry_t *entry = cache->hash_table[idx];
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            /* Entry exists, update it */
            free(entry->value);
            entry->value = malloc(size);
            if (!entry->value) {
                pthread_rwlock_unlock(&cache->lock);
                return -1;
            }
            memcpy(entry->value, value, size);
            entry->size = size;
            entry->last_accessed = time(NULL);
            entry->access_count++;
            cache->stats.total_hits++;
            move_to_front(cache, entry);
            
            pthread_rwlock_unlock(&cache->lock);
            return 0;
        }
        entry = entry->hash_next;
    }
    
    /* New entry */
    entry = malloc(sizeof(cache_entry_t));
    if (!entry) {
        pthread_rwlock_unlock(&cache->lock);
        return -1;
    }
    
    entry->value = malloc(size);
    if (!entry->value) {
        free(entry);
        pthread_rwlock_unlock(&cache->lock);
        return -1;
    }
    
    memcpy(entry->value, value, size);
    strncpy(entry->key, key, sizeof(entry->key) - 1);
    entry->key_hash = hash;
    entry->size = size;
    entry->created_at = time(NULL);
    entry->last_accessed = entry->created_at;
    entry->access_count = 1;
    entry->compressed = 0;
    
    move_to_front(cache, entry);
    
    entry->hash_next = cache->hash_table[idx];
    cache->hash_table[idx] = entry;
    
    cache->current_size += size;
    cache->entry_count++;
    cache->stats.total_entries++;
    cache->stats.total_bytes += size;
    
    /* Check if eviction needed */
    while ((cache->current_size > cache->config.max_size ||
           cache->entry_count > cache->config.max_entries) &&
           cache->lru_tail) {
        cache_entry_t *victim = cache->lru_tail;
        
        if (cache->eviction_callback) {
            cache->eviction_callback(victim->key, victim->value,
                                    victim->size, cache->eviction_context);
        }
        
        cache->current_size -= victim->size;
        cache->entry_count--;
        cache->stats.total_evictions++;
        
        /* Remove from hash table */
        uint32_t victim_idx = victim->key_hash % CACHE_HASHTABLE_SIZE;
        cache_entry_t *h = cache->hash_table[victim_idx];
        if (h == victim) {
            cache->hash_table[victim_idx] = victim->hash_next;
        } else {
            while (h && h->hash_next != victim) h = h->hash_next;
            if (h) h->hash_next = victim->hash_next;
        }
        
        remove_entry(cache, victim);
        free(victim->value);
        free(victim);
    }
    
    pthread_rwlock_unlock(&cache->lock);
    return 0;
}

int cache_get(cache_handle_t handle, const char *key, void **value,
             size_t *size) {
    cache_t_impl *cache = (cache_t_impl *)handle;
    if (!cache || !key || !value || !size) return -1;
    
    pthread_rwlock_rdlock(&cache->lock);
    
    uint32_t hash = hash_key(key);
    uint32_t idx = hash % CACHE_HASHTABLE_SIZE;
    
    cache_entry_t *entry = cache->hash_table[idx];
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            *value = entry->value;
            *size = entry->size;
            
            /* Update stats */
            entry->access_count++;
            entry->last_accessed = time(NULL);
            cache->stats.total_hits++;
            
            pthread_rwlock_unlock(&cache->lock);
            
            /* Upgrade to write lock for LRU update */
            pthread_rwlock_wrlock(&cache->lock);
            move_to_front(cache, entry);
            pthread_rwlock_unlock(&cache->lock);
            
            return 0;
        }
        entry = entry->hash_next;
    }
    
    cache->stats.total_misses++;
    pthread_rwlock_unlock(&cache->lock);
    return -1;
}

int cache_delete(cache_handle_t handle, const char *key) {
    cache_t_impl *cache = (cache_t_impl *)handle;
    if (!cache || !key) return -1;
    
    pthread_rwlock_wrlock(&cache->lock);
    
    uint32_t hash = hash_key(key);
    uint32_t idx = hash % CACHE_HASHTABLE_SIZE;
    
    cache_entry_t *entry = cache->hash_table[idx];
    cache_entry_t *prev = NULL;
    
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            if (prev) {
                prev->hash_next = entry->hash_next;
            } else {
                cache->hash_table[idx] = entry->hash_next;
            }
            
            remove_entry(cache, entry);
            cache->current_size -= entry->size;
            cache->entry_count--;
            
            free(entry->value);
            free(entry);
            
            pthread_rwlock_unlock(&cache->lock);
            return 0;
        }
        prev = entry;
        entry = entry->hash_next;
    }
    
    pthread_rwlock_unlock(&cache->lock);
    return -1;
}

int cache_contains(cache_handle_t handle, const char *key) {
    cache_t_impl *cache = (cache_t_impl *)handle;
    if (!cache || !key) return 0;
    
    pthread_rwlock_rdlock(&cache->lock);
    
    uint32_t hash = hash_key(key);
    uint32_t idx = hash % CACHE_HASHTABLE_SIZE;
    
    cache_entry_t *entry = cache->hash_table[idx];
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            pthread_rwlock_unlock(&cache->lock);
            return 1;
        }
        entry = entry->hash_next;
    }
    
    pthread_rwlock_unlock(&cache->lock);
    return 0;
}

int cache_put_batch(cache_handle_t handle, const char **keys,
                   const void **values, const size_t *sizes, int count) {
    if (!handle || !keys || !values || !sizes || count <= 0) return -1;
    
    for (int i = 0; i < count; i++) {
        if (cache_put(handle, keys[i], values[i], sizes[i]) != 0) {
            return -1;
        }
    }
    return 0;
}

int cache_get_batch(cache_handle_t handle, const char **keys,
                   void **values, size_t *sizes, int count) {
    if (!handle || !keys || !values || !sizes || count <= 0) return -1;
    
    for (int i = 0; i < count; i++) {
        if (cache_get(handle, keys[i], &values[i], &sizes[i]) != 0) {
            return -1;
        }
    }
    return 0;
}

int cache_clear(cache_handle_t handle) {
    cache_t_impl *cache = (cache_t_impl *)handle;
    if (!cache) return -1;
    
    pthread_rwlock_wrlock(&cache->lock);
    
    cache_entry_t *entry = cache->lru_head;
    while (entry) {
        cache_entry_t *next = entry->next;
        free(entry->value);
        free(entry);
        entry = next;
    }
    
    cache->lru_head = NULL;
    cache->lru_tail = NULL;
    cache->current_size = 0;
    cache->entry_count = 0;
    memset(cache->hash_table, 0, 
          CACHE_HASHTABLE_SIZE * sizeof(cache_entry_t *));
    
    pthread_rwlock_unlock(&cache->lock);
    return 0;
}

int cache_evict_lru(cache_handle_t handle) {
    cache_t_impl *cache = (cache_t_impl *)handle;
    if (!cache || !cache->lru_tail) return -1;
    
    return cache_delete(handle, cache->lru_tail->key);
}

int cache_evict_oldest(cache_handle_t handle, time_t before) {
    cache_t_impl *cache = (cache_t_impl *)handle;
    if (!cache) return -1;
    
    pthread_rwlock_wrlock(&cache->lock);
    
    cache_entry_t *entry = cache->lru_tail;
    int count = 0;
    
    while (entry && entry->created_at < before) {
        cache_entry_t *prev = entry->prev;
        cache_delete(handle, entry->key);
        count++;
        entry = prev;
    }
    
    pthread_rwlock_unlock(&cache->lock);
    return count;
}

void cache_get_stats(cache_handle_t handle, cache_stats_t *stats) {
    cache_t_impl *cache = (cache_t_impl *)handle;
    if (!cache || !stats) return;
    
    pthread_rwlock_rdlock(&cache->lock);
    
    memcpy(stats, &cache->stats, sizeof(cache_stats_t));
    stats->total_entries = cache->entry_count;
    stats->total_bytes = cache->current_size;
    stats->avg_entry_size = cache->entry_count > 0 ?
        (double)cache->current_size / cache->entry_count : 0;
    
    uint64_t total = cache->stats.total_hits + cache->stats.total_misses;
    stats->hit_rate = total > 0 ? 
        (double)cache->stats.total_hits / total : 0;
    stats->miss_rate = 1.0 - stats->hit_rate;
    
    pthread_rwlock_unlock(&cache->lock);
}

void cache_reset_stats(cache_handle_t handle) {
    cache_t_impl *cache = (cache_t_impl *)handle;
    if (!cache) return;
    
    pthread_rwlock_wrlock(&cache->lock);
    memset(&cache->stats, 0, sizeof(cache_stats_t));
    pthread_rwlock_unlock(&cache->lock);
}

int cache_set_strategy(cache_handle_t handle, cache_strategy_t strategy) {
    cache_t_impl *cache = (cache_t_impl *)handle;
    if (!cache) return -1;
    
    cache->config.strategy = strategy;
    return 0;
}

int cache_set_max_size(cache_handle_t handle, size_t size) {
    cache_t_impl *cache = (cache_t_impl *)handle;
    if (!cache) return -1;
    
    cache->config.max_size = size;
    return 0;
}

int cache_set_ttl(cache_handle_t handle, time_t ttl) {
    cache_t_impl *cache = (cache_t_impl *)handle;
    if (!cache) return -1;
    
    cache->config.ttl = ttl;
    return 0;
}

int cache_save(cache_handle_t handle, const char *path) {
    (void)handle;
    (void)path;
    return 0;
}

int cache_load(cache_handle_t handle, const char *path) {
    (void)handle;
    (void)path;
    return 0;
}

int cache_entry_count(cache_handle_t handle) {
    cache_t_impl *cache = (cache_t_impl *)handle;
    return cache ? cache->entry_count : 0;
}

size_t cache_memory_usage(cache_handle_t handle) {
    cache_t_impl *cache = (cache_t_impl *)handle;
    return cache ? cache->current_size : 0;
}

cache_entry_meta_t *cache_get_entries(cache_handle_t handle, int *count) {
    cache_t_impl *cache = (cache_t_impl *)handle;
    if (!cache || !count) return NULL;
    
    *count = cache->entry_count;
    return NULL;
}

int cache_set_eviction_callback(cache_handle_t handle,
                               cache_eviction_callback_t callback,
                               void *context) {
    cache_t_impl *cache = (cache_t_impl *)handle;
    if (!cache) return -1;
    
    cache->eviction_callback = callback;
    cache->eviction_context = context;
    return 0;
}

cache_handle_t cache_create_tiered(const cache_tier_t *tiers,
                                   int tier_count) {
    (void)tiers;
    (void)tier_count;
    return NULL;
}

int cache_promote_entry(cache_handle_t handle, const char *key, int to_tier) {
    (void)handle;
    (void)key;
    (void)to_tier;
    return 0;
}
