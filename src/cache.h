#ifndef GLYPH_CACHE_H
#define GLYPH_CACHE_H

#include "atlas.h"
#include "bitmap.h"
#include "layout.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GLYPH_CACHE_KEY_BYTES 64u
#define GLYPH_CACHE_NO_LIMIT ((size_t)-1)

typedef enum {
    GLYPH_CACHE_ENTRY_FILE = 1,
    GLYPH_CACHE_ENTRY_BITMAP_SLICE = 2,
    GLYPH_CACHE_ENTRY_LAYOUT_SURFACE = 3
} GlyphCacheEntryType;

typedef enum {
    GLYPH_CACHE_EVICT_EXPLICIT = 1,
    GLYPH_CACHE_EVICT_REPLACED = 2,
    GLYPH_CACHE_EVICT_CAPACITY = 3,
    GLYPH_CACHE_EVICT_CLEAR = 4,
    GLYPH_CACHE_EVICT_DESTROY = 5
} GlyphCacheEvictReason;

typedef enum {
    GLYPH_CACHE_VALIDATE_OK = 0,
    GLYPH_CACHE_VALIDATE_NULL_CACHE = 1,
    GLYPH_CACHE_VALIDATE_BAD_LIMITS = 2,
    GLYPH_CACHE_VALIDATE_BAD_TABLE = 3,
    GLYPH_CACHE_VALIDATE_BAD_LRU = 4,
    GLYPH_CACHE_VALIDATE_BAD_ACCOUNTING = 5,
    GLYPH_CACHE_VALIDATE_BAD_ENTRY = 6
} GlyphCacheValidationCode;

typedef struct {
    GlyphCacheEntryType type;
    uint64_t primary;
    uint64_t secondary;
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint8_t bytes[GLYPH_CACHE_KEY_BYTES];
    size_t byte_count;
} GlyphCacheKey;

typedef struct {
    uint64_t inserts;
    uint64_t replacements;
    uint64_t lookups;
    uint64_t hits;
    uint64_t misses;
    uint64_t removals;
    uint64_t evictions;
    uint64_t capacity_evictions;
    uint64_t clears;
    uint64_t failed_allocations;
    uint64_t validation_failures;
} GlyphCacheStats;

typedef struct {
    GlyphCacheValidationCode code;
    size_t entry_count;
    size_t byte_count;
    size_t table_count;
    char message[192];
} GlyphCacheValidationReport;

typedef struct {
    const GlyphCacheKey *key;
    GlyphCacheEntryType type;
    size_t charge;
    uint64_t generation;
    uint64_t last_access;
    const GlyphFile *file;
} GlyphCacheFileView;

typedef struct {
    const GlyphCacheKey *key;
    GlyphCacheEntryType type;
    size_t charge;
    uint64_t generation;
    uint64_t last_access;
    const GlyphFile *file;
    uint32_t glyph_id;
    GlyphBitmapRect rect;
    GlyphBitmap bitmap;
} GlyphCacheBitmapSliceView;

typedef struct {
    const GlyphCacheKey *key;
    GlyphCacheEntryType type;
    size_t charge;
    uint64_t generation;
    uint64_t last_access;
    const GlyphFile *file;
    GlyphLayoutOptions options;
    int32_t origin_x;
    int32_t origin_y;
    GlyphBitmap surface;
} GlyphCacheLayoutSurfaceView;

typedef struct {
    const GlyphCacheKey *key;
    GlyphCacheEntryType type;
    size_t charge;
    uint64_t generation;
    uint64_t last_access;
    const GlyphFile *file;
    const GlyphBitmap *bitmap;
    const GlyphBitmapRect *rect;
    const GlyphLayoutOptions *layout_options;
    int32_t origin_x;
    int32_t origin_y;
} GlyphCacheEntryView;

typedef void (*GlyphCacheEvictFn)(void *user_data,
                                  const GlyphCacheEntryView *entry,
                                  GlyphCacheEvictReason reason);

typedef struct GlyphCacheEntry GlyphCacheEntry;

typedef struct {
    size_t max_entries;
    size_t max_bytes;
    size_t table_capacity;
    size_t entry_count;
    size_t byte_count;
    uint64_t clock;
    GlyphCacheEntry **table;
    GlyphCacheEntry *lru_head;
    GlyphCacheEntry *lru_tail;
    GlyphCacheEvictFn evict;
    void *evict_user_data;
    GlyphCacheStats stats;
} GlyphCache;

void glyph_cache_key_init(GlyphCacheKey *key, GlyphCacheEntryType type);
GlyphCacheKey glyph_cache_key_file(const char *path, uint32_t variant);
GlyphCacheKey glyph_cache_key_file_pointer(const GlyphFile *file, uint32_t variant);
GlyphCacheKey glyph_cache_key_bitmap_slice(const GlyphFile *file,
                                           uint32_t glyph_id,
                                           GlyphBitmapRect rect,
                                           uint32_t variant);
GlyphCacheKey glyph_cache_key_layout_surface(const GlyphFile *file,
                                             const char *text,
                                             const GlyphLayoutOptions *options,
                                             uint32_t width,
                                             uint32_t height,
                                             int32_t origin_x,
                                             int32_t origin_y);
uint64_t glyph_cache_hash_bytes(const void *data, size_t size, uint64_t seed);
uint64_t glyph_cache_hash_string(const char *text, uint64_t seed);
uint64_t glyph_cache_hash_layout_options(const GlyphLayoutOptions *options, uint64_t seed);
int glyph_cache_key_equal(const GlyphCacheKey *a, const GlyphCacheKey *b);
int glyph_cache_key_write(FILE *fp, const GlyphCacheKey *key);

void glyph_cache_stats_init(GlyphCacheStats *stats);
double glyph_cache_stats_hit_rate(const GlyphCacheStats *stats);

void glyph_cache_init(GlyphCache *cache);
int glyph_cache_create(GlyphCache *cache, size_t max_entries, size_t max_bytes);
void glyph_cache_free(GlyphCache *cache);
void glyph_cache_set_capacity(GlyphCache *cache, size_t max_entries, size_t max_bytes);
void glyph_cache_set_eviction_callback(GlyphCache *cache, GlyphCacheEvictFn evict, void *user_data);
void glyph_cache_clear(GlyphCache *cache);

size_t glyph_cache_entry_count(const GlyphCache *cache);
size_t glyph_cache_byte_count(const GlyphCache *cache);
size_t glyph_cache_max_entries(const GlyphCache *cache);
size_t glyph_cache_max_bytes(const GlyphCache *cache);
const GlyphCacheStats *glyph_cache_stats(const GlyphCache *cache);
void glyph_cache_reset_stats(GlyphCache *cache);

int glyph_cache_insert_file(GlyphCache *cache, const GlyphCacheKey *key, const GlyphFile *file);
int glyph_cache_insert_bitmap_slice(GlyphCache *cache,
                                    const GlyphCacheKey *key,
                                    uint32_t glyph_id,
                                    GlyphBitmapRect rect,
                                    const GlyphBitmap *bitmap);
int glyph_cache_insert_layout_surface(GlyphCache *cache,
                                      const GlyphCacheKey *key,
                                      const GlyphLayoutOptions *options,
                                      int32_t origin_x,
                                      int32_t origin_y,
                                      const GlyphBitmap *surface);

const GlyphFile *glyph_cache_lookup_file(GlyphCache *cache, const GlyphCacheKey *key);
const GlyphCacheBitmapSliceView *glyph_cache_lookup_bitmap_slice(GlyphCache *cache,
                                                                 const GlyphCacheKey *key);
const GlyphCacheLayoutSurfaceView *glyph_cache_lookup_layout_surface(GlyphCache *cache,
                                                                     const GlyphCacheKey *key);
int glyph_cache_lookup_entry(GlyphCache *cache, const GlyphCacheKey *key, GlyphCacheEntryView *view);

int glyph_cache_peek_file(const GlyphCache *cache, const GlyphCacheKey *key, const GlyphFile **file);
int glyph_cache_peek_bitmap_slice(const GlyphCache *cache,
                                  const GlyphCacheKey *key,
                                  GlyphCacheBitmapSliceView *view);
int glyph_cache_peek_layout_surface(const GlyphCache *cache,
                                    const GlyphCacheKey *key,
                                    GlyphCacheLayoutSurfaceView *view);

int glyph_cache_contains(const GlyphCache *cache, const GlyphCacheKey *key);
int glyph_cache_remove(GlyphCache *cache, const GlyphCacheKey *key);
size_t glyph_cache_remove_file(GlyphCache *cache, const GlyphFile *file);
size_t glyph_cache_remove_type(GlyphCache *cache, GlyphCacheEntryType type);
size_t glyph_cache_evict_to_capacity(GlyphCache *cache);
int glyph_cache_touch(GlyphCache *cache, const GlyphCacheKey *key);

int glyph_cache_validate(const GlyphCache *cache, GlyphCacheValidationReport *report);
const char *glyph_cache_validation_code_name(GlyphCacheValidationCode code);
int glyph_cache_write_report(FILE *fp, const GlyphCache *cache);

#ifdef __cplusplus
}
#endif

#endif
