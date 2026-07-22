/*
 * Advanced Caching System - Multi-Strategy Caching Infrastructure
 * 
 * Supports LRU, LFU, ARC, and adaptive caching strategies with
 * performance metrics, compression, and tiered storage.
 */

#ifndef GLYPH_ADVANCED_CACHE_H
#define GLYPH_ADVANCED_CACHE_H

#include <stdint.h>
#include <time.h>

/* Cache Strategy Types */
typedef enum {
    CACHE_STRATEGY_LRU,          /* Least Recently Used */
    CACHE_STRATEGY_LFU,          /* Least Frequently Used */
    CACHE_STRATEGY_ARC,          /* Adaptive Replacement Cache */
    CACHE_STRATEGY_FIFO,         /* First In First Out */
    CACHE_STRATEGY_RANDOM        /* Random eviction */
} cache_strategy_t;

/* Compression Methods */
typedef enum {
    CACHE_COMPRESSION_NONE,
    CACHE_COMPRESSION_DEFLATE,
    CACHE_COMPRESSION_LZ4,
    CACHE_COMPRESSION_ZSTD
} cache_compression_t;

/* Cache Entry Metadata */
typedef struct {
    uint64_t entry_id;
    const char *key;
    uint32_t key_hash;
    size_t size;
    time_t created_at;
    time_t last_accessed;
    uint64_t access_count;
    int32_t hits;
    int32_t misses;
    cache_compression_t compression;
    int compressed_size;
} cache_entry_meta_t;

/* Cache Statistics */
typedef struct {
    uint64_t total_entries;
    uint64_t total_hits;
    uint64_t total_misses;
    uint64_t total_evictions;
    uint64_t total_bytes;
    uint64_t max_bytes;
    double hit_rate;
    double miss_rate;
    double avg_entry_size;
    time_t cache_created;
    time_t last_eviction;
} cache_stats_t;

/* Cache Configuration */
typedef struct {
    cache_strategy_t strategy;
    size_t max_size;
    size_t max_entries;
    time_t ttl;                  /* Time to live */
    int enable_compression;
    cache_compression_t compression_algo;
    int compression_threshold;   /* Min size to compress */
    int enable_persistence;      /* Save to disk */
    const char *persistence_path;
    int enable_statistics;
    int tier_count;              /* For tiered caching */
} cache_config_t;

/* Cache Handle */
typedef struct cache_s *cache_handle_t;

/* API Functions */
cache_handle_t cache_create(const cache_config_t *config);
void cache_destroy(cache_handle_t cache);

/* Core Operations */
int cache_put(cache_handle_t cache, const char *key, const void *value,
             size_t size);
int cache_get(cache_handle_t cache, const char *key, void **value,
             size_t *size);
int cache_delete(cache_handle_t cache, const char *key);
int cache_contains(cache_handle_t cache, const char *key);

/* Batch Operations */
int cache_put_batch(cache_handle_t cache, const char **keys,
                   const void **values, const size_t *sizes, int count);
int cache_get_batch(cache_handle_t cache, const char **keys,
                   void **values, size_t *sizes, int count);

/* Cache Control */
int cache_clear(cache_handle_t cache);
int cache_evict_lru(cache_handle_t cache);
int cache_evict_oldest(cache_handle_t cache, time_t before);

/* Statistics */
void cache_get_stats(cache_handle_t cache, cache_stats_t *stats);
void cache_reset_stats(cache_handle_t cache);

/* Configuration */
int cache_set_strategy(cache_handle_t cache, cache_strategy_t strategy);
int cache_set_max_size(cache_handle_t cache, size_t size);
int cache_set_ttl(cache_handle_t cache, time_t ttl);

/* Persistence */
int cache_save(cache_handle_t cache, const char *path);
int cache_load(cache_handle_t cache, const char *path);

/* Introspection */
int cache_entry_count(cache_handle_t cache);
size_t cache_memory_usage(cache_handle_t cache);
cache_entry_meta_t *cache_get_entries(cache_handle_t cache, int *count);

/* Callbacks */
typedef void (*cache_eviction_callback_t)(const char *key,
                                          const void *value,
                                          size_t size,
                                          void *context);

int cache_set_eviction_callback(cache_handle_t cache,
                               cache_eviction_callback_t callback,
                               void *context);

/* Tiered Caching */
typedef struct {
    size_t max_size;
    int64_t latency_us;  /* Expected latency in microseconds */
    const char *name;
} cache_tier_t;

cache_handle_t cache_create_tiered(const cache_tier_t *tiers, int tier_count);
int cache_promote_entry(cache_handle_t cache, const char *key, int to_tier);

#endif /* GLYPH_ADVANCED_CACHE_H */
