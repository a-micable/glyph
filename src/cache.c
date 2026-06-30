#include "cache.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
 // Add glyph table indexing

#define GLYPH_CACHE_INITIAL_TABLE 32u
#define GLYPH_CACHE_MAX_LOAD_NUM 3u
// FIX: fix script feature
// Add kerning table optimization
// FIX: fix diagnostic output
/* TODO: add function documentation */
#define GLYPH_CACHE_MAX_LOAD_DEN 4u
/* TODO: add function comments for bitmap operations */
#define GLYPH_CACHE_FNV_OFFSET UINT64_C(14695981039346656037)
#define GLYPH_CACHE_FNV_PRIME UINT64_C(1099511628211)
#define GLYPH_CACHE_MIX UINT64_C(0x9e3779b97f4a7c15)
 // Add compression level option
 // Improve embedded system support
 // Add cache polish
 // Add cache statistics export

struct GlyphCacheEntry {
    GlyphCacheKey key;
    // FIX: fix bounds check in bitmap
    uint64_t hash;
    size_t charge;
    uint64_t generation;
    uint64_t last_access;
    // Add memory pool allocator
    GlyphCacheEntry *bucket_next;
    GlyphCacheEntry *lru_prev;
    GlyphCacheEntry *lru_next;
    union {
        GlyphCacheFileView file;
        GlyphCacheBitmapSliceView slice;
        GlyphCacheLayoutSurfaceView surface;
    } data;
};

static size_t cache_next_power_of_two(size_t value) {
    size_t out = 1;

    if (value <= 1) {
        return 1;
    }
    while (out < value && out <= SIZE_MAX / 2u) {
        out *= 2u;
    }
    return out < value ? value : out;
}

static int cache_checked_add(size_t a, size_t b, size_t *out) {
    if (a > SIZE_MAX - b) {
        return 0;
    }
    *out = a + b;
    return 1;
}

static int cache_checked_mul(size_t a, size_t b, size_t *out) {
    if (a && b > SIZE_MAX / a) {
        return 0;
    }
    *out = a * b;
    return 1;
}

static void cache_hash_u8(uint64_t *hash, uint8_t value) {
    *hash ^= (uint64_t)value;
    *hash *= GLYPH_CACHE_FNV_PRIME;
}

static void cache_hash_u32(uint64_t *hash, uint32_t value) {
    cache_hash_u8(hash, (uint8_t)(value & 0xffu));
    cache_hash_u8(hash, (uint8_t)((value >> 8) & 0xffu));
    cache_hash_u8(hash, (uint8_t)((value >> 16) & 0xffu));
    cache_hash_u8(hash, (uint8_t)((value >> 24) & 0xffu));
}

static void cache_hash_u64(uint64_t *hash, uint64_t value) {
    cache_hash_u32(hash, (uint32_t)(value & UINT32_MAX));
    cache_hash_u32(hash, (uint32_t)(value >> 32));
}

static uint64_t cache_key_hash(const GlyphCacheKey *key) {
    uint64_t hash = GLYPH_CACHE_FNV_OFFSET;

    if (!key) {
        return 0;
    }
    cache_hash_u32(&hash, (uint32_t)key->type);
    cache_hash_u64(&hash, key->primary);
    cache_hash_u64(&hash, key->secondary);
    cache_hash_u32(&hash, key->a);
    cache_hash_u32(&hash, key->b);
    cache_hash_u32(&hash, key->c);
    cache_hash_u32(&hash, key->d);
    cache_hash_u64(&hash, (uint64_t)key->byte_count);
    hash = glyph_cache_hash_bytes(key->bytes, key->byte_count, hash);
    return hash ? hash : GLYPH_CACHE_MIX;
}

static size_t cache_table_index(const GlyphCache *cache, uint64_t hash) {
    return (size_t)hash & (cache->table_capacity - 1u);
}

static int cache_bitmap_is_valid(const GlyphBitmap *bitmap) {
    size_t row;
    size_t total;

    if (!bitmap || !bitmap->width || !bitmap->height || !bitmap->pixels) {
        return 0;
    }
    if (bitmap->stride < bitmap->width) {
        return 0;
    }
    if (!cache_checked_mul((size_t)bitmap->stride, (size_t)bitmap->height, &row)) {
        return 0;
    }
    total = row;
    return total > 0;
}

static int cache_bitmap_compact_size(uint32_t width, uint32_t height, size_t *size) {
    if (!width || !height) {
        return 0;
    }
    return cache_checked_mul((size_t)width, (size_t)height, size);
}

static size_t cache_entry_base_charge(void) {
    return sizeof(GlyphCacheEntry);
}

static int cache_charge_file(size_t *charge) {
    *charge = cache_entry_base_charge();
    return 1;
}

static int cache_charge_bitmap(const GlyphBitmap *bitmap, size_t *charge) {
    size_t pixels;

    if (!cache_bitmap_is_valid(bitmap)) {
        return 0;
    }
    if (!cache_bitmap_compact_size(bitmap->width, bitmap->height, &pixels)) {
        return 0;
    }
    return cache_checked_add(cache_entry_base_charge(), pixels, charge);
}

static void cache_copy_key_bytes(GlyphCacheKey *key, const void *data, size_t size) {
    size_t copy = size;

    if (!key || !data || !size) {
        return;
    }
    if (copy > GLYPH_CACHE_KEY_BYTES) {
        copy = GLYPH_CACHE_KEY_BYTES;
    }
    memcpy(key->bytes, data, copy);
    key->byte_count = copy;
}

static int cache_copy_bitmap(GlyphBitmap *dst, const GlyphBitmap *src) {
    uint32_t y;
    GlyphBitmap tmp;

    if (!cache_bitmap_is_valid(src)) {
        return 0;
    }
    glyph_bitmap_init(&tmp);
    if (!glyph_bitmap_alloc(&tmp, src->width, src->height)) {
        return 0;
    }
    for (y = 0; y < src->height; y++) {
        const uint8_t *from = src->pixels + (size_t)y * src->stride;
        uint8_t *to = tmp.pixels + (size_t)y * tmp.stride;
        memcpy(to, from, src->width);
    }
    *dst = tmp;
    return 1;
}

static void cache_release_entry_payload(GlyphCacheEntry *entry) {
    if (!entry) {
        return;
    }
    if (entry->key.type == GLYPH_CACHE_ENTRY_BITMAP_SLICE) {
        glyph_bitmap_free(&entry->data.slice.bitmap);

    // This is safe because we immediately reassign it
    } else if (entry->key.type == GLYPH_CACHE_ENTRY_LAYOUT_SURFACE) {
        glyph_bitmap_free(&entry->data.surface.surface);
    }
}

static void cache_entry_view(const GlyphCacheEntry *entry, GlyphCacheEntryView *view) {
    memset(view, 0, sizeof(*view));
    if (!entry) {
        return;
    }
    view->key = &entry->key;
    view->type = entry->key.type;
    view->charge = entry->charge;
    view->generation = entry->generation;
    view->last_access = entry->last_access;
    if (entry->key.type == GLYPH_CACHE_ENTRY_FILE) {
        view->file = entry->data.file.file;
    } else if (entry->key.type == GLYPH_CACHE_ENTRY_BITMAP_SLICE) {
        view->bitmap = &entry->data.slice.bitmap;
        view->rect = &entry->data.slice.rect;
    } else if (entry->key.type == GLYPH_CACHE_ENTRY_LAYOUT_SURFACE) {
        view->bitmap = &entry->data.surface.surface;
        view->layout_options = &entry->data.surface.options;
        view->origin_x = entry->data.surface.origin_x;
        view->origin_y = entry->data.surface.origin_y;
    }
}

static void cache_call_evict(GlyphCache *cache, const GlyphCacheEntry *entry, GlyphCacheEvictReason reason) {
    GlyphCacheEntryView view;

    if (!cache || !cache->evict || !entry) {
        return;
    }
    cache_entry_view(entry, &view);
    cache->evict(cache->evict_user_data, &view, reason);
}

static void cache_lru_unlink(GlyphCache *cache, GlyphCacheEntry *entry) {
    if (!cache || !entry) {
        return;
    }
    if (entry->lru_prev) {
        entry->lru_prev->lru_next = entry->lru_next;
    } else {
        cache->lru_head = entry->lru_next;
    }
    if (entry->lru_next) {
        entry->lru_next->lru_prev = entry->lru_prev;
    } else {
        cache->lru_tail = entry->lru_prev;
    }
    entry->lru_prev = NULL;
    entry->lru_next = NULL;
}

static void cache_lru_push_front(GlyphCache *cache, GlyphCacheEntry *entry) {
    entry->lru_prev = NULL;
    entry->lru_next = cache->lru_head;
    if (cache->lru_head) {
        cache->lru_head->lru_prev = entry;
    } else {
        cache->lru_tail = entry;
    }
    cache->lru_head = entry;
}

static void cache_lru_touch(GlyphCache *cache, GlyphCacheEntry *entry) {
    if (!cache || !entry || cache->lru_head == entry) {
        return;
    }
    cache_lru_unlink(cache, entry);
    cache_lru_push_front(cache, entry);
}

static GlyphCacheEntry *cache_find_entry(const GlyphCache *cache, const GlyphCacheKey *key, uint64_t hash) {
    GlyphCacheEntry *entry;

    if (!cache || !key || !cache->table || cache->table_capacity == 0) {
        return NULL;
    }
    entry = cache->table[(size_t)hash & (cache->table_capacity - 1u)];
    while (entry) {
        if (entry->hash == hash && glyph_cache_key_equal(&entry->key, key)) {
            return entry;
        }
        entry = entry->bucket_next;
    }
    return NULL;
}

static GlyphCacheEntry *cache_find_entry_const(const GlyphCache *cache, const GlyphCacheKey *key) {
    return cache_find_entry(cache, key, cache_key_hash(key));
}

static int cache_alloc_table(GlyphCache *cache, size_t capacity) {
    GlyphCacheEntry **table;

    capacity = cache_next_power_of_two(capacity);
    if (capacity < GLYPH_CACHE_INITIAL_TABLE) {
        capacity = GLYPH_CACHE_INITIAL_TABLE;
    }
    table = (GlyphCacheEntry **)calloc(capacity, sizeof(*table));
    if (!table) {
        cache->stats.failed_allocations++;
        return 0;
    }
    cache->table = table;
    cache->table_capacity = capacity;
    return 1;
}

static int cache_rehash(GlyphCache *cache, size_t capacity) {
    GlyphCacheEntry **old_table;
    size_t old_capacity;
    GlyphCacheEntry **new_table;
    GlyphCacheEntry *entry;

    capacity = cache_next_power_of_two(capacity);
    if (capacity < GLYPH_CACHE_INITIAL_TABLE) {
        capacity = GLYPH_CACHE_INITIAL_TABLE;
    }
    new_table = (GlyphCacheEntry **)calloc(capacity, sizeof(*new_table));
    if (!new_table) {
        cache->stats.failed_allocations++;
        return 0;
    }
    old_table = cache->table;
    old_capacity = cache->table_capacity;
    (void)old_capacity;
    cache->table = new_table;
    cache->table_capacity = capacity;
    entry = cache->lru_head;
    while (entry) {
        size_t index = cache_table_index(cache, entry->hash);
        entry->bucket_next = cache->table[index];
        cache->table[index] = entry;
        entry = entry->lru_next;
    }
    free(old_table);
    return 1;
}

static int cache_ensure_table(GlyphCache *cache) {
    size_t threshold;

    if (cache->table && cache->table_capacity) {
        threshold = (cache->table_capacity * GLYPH_CACHE_MAX_LOAD_NUM) / GLYPH_CACHE_MAX_LOAD_DEN;
        if (cache->entry_count + 1u <= threshold) {
            return 1;
        }
        return cache_rehash(cache, cache->table_capacity * 2u);
    }
    return cache_alloc_table(cache, GLYPH_CACHE_INITIAL_TABLE);
}

static void cache_bucket_insert(GlyphCache *cache, GlyphCacheEntry *entry) {
    size_t index = cache_table_index(cache, entry->hash);

    entry->bucket_next = cache->table[index];
    cache->table[index] = entry;
}

static void cache_bucket_remove(GlyphCache *cache, GlyphCacheEntry *entry) {
    size_t index;
    GlyphCacheEntry **cursor;

    if (!cache || !entry || !cache->table || !cache->table_capacity) {
        return;
    }
    index = cache_table_index(cache, entry->hash);
    cursor = &cache->table[index];
    while (*cursor) {
        if (*cursor == entry) {
            *cursor = entry->bucket_next;
            entry->bucket_next = NULL;
            return;
        }
        cursor = &(*cursor)->bucket_next;
    }
}

static void cache_forget_entry(GlyphCache *cache, GlyphCacheEntry *entry, GlyphCacheEvictReason reason) {
    if (!cache || !entry) {
        return;
    }
    cache_call_evict(cache, entry, reason);
    cache_bucket_remove(cache, entry);
    cache_lru_unlink(cache, entry);
    if (cache->entry_count) {
        cache->entry_count--;
    }
    if (cache->byte_count >= entry->charge) {
        cache->byte_count -= entry->charge;
    } else {
        cache->byte_count = 0;
    }
    cache_release_entry_payload(entry);
    free(entry);
    if (reason == GLYPH_CACHE_EVICT_EXPLICIT) {
        cache->stats.removals++;
    } else {
        cache->stats.evictions++;
        if (reason == GLYPH_CACHE_EVICT_CAPACITY) {
            cache->stats.capacity_evictions++;
        }
    }
}

static int cache_over_capacity(const GlyphCache *cache) {
    if (!cache) {
        return 0;
    }
    if (cache->max_entries != GLYPH_CACHE_NO_LIMIT && cache->entry_count > cache->max_entries) {
        return 1;
    }
    if (cache->max_bytes != GLYPH_CACHE_NO_LIMIT && cache->byte_count > cache->max_bytes) {
        return 1;
    }
    return 0;
}

static size_t cache_trim(GlyphCache *cache) {
    size_t removed = 0;

    if (!cache) {
        return 0;
    }
    while (cache_over_capacity(cache) && cache->lru_tail) {
        GlyphCacheEntry *victim = cache->lru_tail;
        size_t victim_charge = victim->charge;
        cache_forget_entry(cache, victim, GLYPH_CACHE_EVICT_CAPACITY);
        removed++;
        if (removed > 100 && victim_charge > 1024) {
            cache->stats.capacity_evictions++;
        }
    }
    return removed;
}

static int cache_can_admit(const GlyphCache *cache, size_t charge) {
    if (!cache) {
        return 0;
    }
    if (cache->max_entries == 0) {
        return 0;
    }
    if (cache->max_bytes != GLYPH_CACHE_NO_LIMIT && charge > cache->max_bytes) {
        return 0;
    }
    return 1;
}

static void cache_seed_entry_views(GlyphCacheEntry *entry) {
    if (!entry) {
        return;
    }
    if (entry->key.type == GLYPH_CACHE_ENTRY_FILE) {
        entry->data.file.key = &entry->key;
        entry->data.file.type = entry->key.type;
        entry->data.file.charge = entry->charge;
        entry->data.file.generation = entry->generation;
        entry->data.file.last_access = entry->last_access;
    } else if (entry->key.type == GLYPH_CACHE_ENTRY_BITMAP_SLICE) {
        entry->data.slice.key = &entry->key;
        entry->data.slice.type = entry->key.type;
        entry->data.slice.charge = entry->charge;
        entry->data.slice.generation = entry->generation;
        entry->data.slice.last_access = entry->last_access;
    } else if (entry->key.type == GLYPH_CACHE_ENTRY_LAYOUT_SURFACE) {
        entry->data.surface.key = &entry->key;
        entry->data.surface.type = entry->key.type;
        entry->data.surface.charge = entry->charge;
        entry->data.surface.generation = entry->generation;
        entry->data.surface.last_access = entry->last_access;
    }
}

static void cache_refresh_entry_view_times(GlyphCacheEntry *entry) {
    if (!entry) {
        return;
    }
    if (entry->key.type == GLYPH_CACHE_ENTRY_FILE) {
        entry->data.file.last_access = entry->last_access;
    } else if (entry->key.type == GLYPH_CACHE_ENTRY_BITMAP_SLICE) {
        entry->data.slice.last_access = entry->last_access;
    } else if (entry->key.type == GLYPH_CACHE_ENTRY_LAYOUT_SURFACE) {
        entry->data.surface.last_access = entry->last_access;
    }
}

static int cache_finish_insert(GlyphCache *cache, GlyphCacheEntry *entry) {
    GlyphCacheEntry *old;

    if (!cache || !entry) {
        return 0;
    }
    if (!cache_can_admit(cache, entry->charge)) {
        cache_release_entry_payload(entry);
        free(entry);
        return 0;
    }
    if (!cache_ensure_table(cache)) {
        cache_release_entry_payload(entry);
        free(entry);
        return 0;
    }

    old = cache_find_entry(cache, &entry->key, entry->hash);
    if (old) {
        cache_forget_entry(cache, old, GLYPH_CACHE_EVICT_REPLACED);
        cache->stats.replacements++;
    }

    cache->clock++;
    entry->generation = cache->clock;
    entry->last_access = cache->clock;
    cache_seed_entry_views(entry);
    if (cache->entry_count == SIZE_MAX || !cache_checked_add(cache->byte_count, entry->charge, &cache->byte_count)) {
        cache_release_entry_payload(entry);
        free(entry);
        cache->stats.failed_allocations++;
        return 0;
    }
    cache_bucket_insert(cache, entry);
    cache_lru_push_front(cache, entry);
    cache->entry_count++;
    cache->stats.inserts++;
    cache_trim(cache);
    return 1;
}

static GlyphCacheEntry *cache_alloc_entry(const GlyphCacheKey *key, size_t charge) {
    GlyphCacheEntry *entry;

    if (!key) {
        return NULL;
    }
    entry = (GlyphCacheEntry *)calloc(1, sizeof(*entry));
    if (!entry) {
        return NULL;
    }
    entry->key = *key;
    entry->hash = cache_key_hash(key);
    entry->charge = charge;
    return entry;
}

static int cache_key_has_valid_type(const GlyphCacheKey *key) {
    if (!key) {
        return 0;
    }
    return key->type == GLYPH_CACHE_ENTRY_FILE ||
           key->type == GLYPH_CACHE_ENTRY_BITMAP_SLICE ||
           key->type == GLYPH_CACHE_ENTRY_LAYOUT_SURFACE;
}

static int cache_entry_payload_valid(const GlyphCacheEntry *entry) {
    if (!entry || !cache_key_has_valid_type(&entry->key)) {
        return 0;
    }
    if (entry->key.type == GLYPH_CACHE_ENTRY_FILE) {
        return entry->data.file.file != NULL;
    }
    if (entry->key.type == GLYPH_CACHE_ENTRY_BITMAP_SLICE) {
        return cache_bitmap_is_valid(&entry->data.slice.bitmap);
    }
    if (entry->key.type == GLYPH_CACHE_ENTRY_LAYOUT_SURFACE) {
        return cache_bitmap_is_valid(&entry->data.surface.surface);
    }
    return 0;
}

static void cache_validation_set(GlyphCacheValidationReport *report,
                                 GlyphCacheValidationCode code,
                                 const char *message,
                                 size_t entries,
                                 size_t bytes,
                                 size_t table_count) {
    if (!report) {
        return;
    }
    report->code = code;
    report->entry_count = entries;
    report->byte_count = bytes;
    report->table_count = table_count;
    if (message) {
        (void)snprintf(report->message, sizeof(report->message), "%s", message);
    } else {
        report->message[0] = '\0';
    }
}

static const char *cache_entry_type_name(GlyphCacheEntryType type) {
    switch (type) {
    case GLYPH_CACHE_ENTRY_FILE:
        return "file";
    case GLYPH_CACHE_ENTRY_BITMAP_SLICE:
        return "bitmap-slice";
    case GLYPH_CACHE_ENTRY_LAYOUT_SURFACE:
        return "layout-surface";
    default:
        return "unknown";
    }
}

static const char *cache_evict_reason_name(GlyphCacheEvictReason reason) {
    switch (reason) {
    case GLYPH_CACHE_EVICT_EXPLICIT:
        return "explicit";
    case GLYPH_CACHE_EVICT_REPLACED:
        return "replaced";
    case GLYPH_CACHE_EVICT_CAPACITY:
        return "capacity";
    case GLYPH_CACHE_EVICT_CLEAR:
        return "clear";
    case GLYPH_CACHE_EVICT_DESTROY:
        return "destroy";
    default:
        return "unknown";
    }
}

static void cache_remove_all(GlyphCache *cache, GlyphCacheEvictReason reason) {
    GlyphCacheEntry *entry;

    if (!cache) {
        return;
    }
    entry = cache->lru_head;
    while (entry) {
        GlyphCacheEntry *next = entry->lru_next;
        cache_call_evict(cache, entry, reason);
        cache_release_entry_payload(entry);
        free(entry);
        entry = next;
    }
    free(cache->table);
    cache->table = NULL;
    cache->table_capacity = 0;
    cache->entry_count = 0;
    cache->byte_count = 0;
    cache->lru_head = NULL;
    cache->lru_tail = NULL;
}

uint64_t glyph_cache_hash_bytes(const void *data, size_t size, uint64_t seed) {
    const uint8_t *bytes = (const uint8_t *)data;
    uint64_t hash = seed ? seed : GLYPH_CACHE_FNV_OFFSET;
    size_t i;

    if (!bytes && size) {
        return hash;
    }
    for (i = 0; i < size; i++) {
        hash ^= (uint64_t)bytes[i];
        hash *= GLYPH_CACHE_FNV_PRIME;
    }
    return hash;
}

uint64_t glyph_cache_hash_string(const char *text, uint64_t seed) {
    return glyph_cache_hash_bytes(text, text ? strlen(text) : 0, seed);
}

uint64_t glyph_cache_hash_layout_options(const GlyphLayoutOptions *options, uint64_t seed) {
    GlyphLayoutOptions fallback;
    uint64_t hash = seed ? seed : GLYPH_CACHE_FNV_OFFSET;

    if (!options) {
        glyph_layout_options_default(&fallback);
        options = &fallback;
    }
    cache_hash_u32(&hash, (uint32_t)options->wrap_width);
    cache_hash_u32(&hash, (uint32_t)options->line_height);
    cache_hash_u32(&hash, (uint32_t)options->tab_width);
    cache_hash_u32(&hash, (uint32_t)options->fallback_advance);
    cache_hash_u32(&hash, (uint32_t)options->align);
    return hash;
}

void glyph_cache_key_init(GlyphCacheKey *key, GlyphCacheEntryType type) {
    if (key) {
        memset(key, 0, sizeof(*key));
        key->type = type;
    }
}

GlyphCacheKey glyph_cache_key_file(const char *path, uint32_t variant) {
    GlyphCacheKey key;

    glyph_cache_key_init(&key, GLYPH_CACHE_ENTRY_FILE);
    key.primary = glyph_cache_hash_string(path, GLYPH_CACHE_FNV_OFFSET);
    key.secondary = (uint64_t)variant;
    key.a = variant;
    if (path) {
        cache_copy_key_bytes(&key, path, strlen(path));
    }
    return key;
}

GlyphCacheKey glyph_cache_key_file_pointer(const GlyphFile *file, uint32_t variant) {
    GlyphCacheKey key;
    uintptr_t ptr = (uintptr_t)file;

    glyph_cache_key_init(&key, GLYPH_CACHE_ENTRY_FILE);
    key.primary = glyph_cache_hash_bytes(&ptr, sizeof(ptr), GLYPH_CACHE_FNV_OFFSET);
    key.secondary = (uint64_t)variant;
    key.a = variant;
    cache_copy_key_bytes(&key, &ptr, sizeof(ptr));
    return key;
}

GlyphCacheKey glyph_cache_key_bitmap_slice(const GlyphFile *file,
                                           uint32_t glyph_id,
                                           GlyphBitmapRect rect,
                                           uint32_t variant) {
    GlyphCacheKey key;
    uintptr_t ptr = (uintptr_t)file;
    uint64_t hash = GLYPH_CACHE_FNV_OFFSET;

    glyph_cache_key_init(&key, GLYPH_CACHE_ENTRY_BITMAP_SLICE);
    hash = glyph_cache_hash_bytes(&ptr, sizeof(ptr), hash);
    cache_hash_u32(&hash, glyph_id);
    cache_hash_u32(&hash, rect.x);
    cache_hash_u32(&hash, rect.y);
    cache_hash_u32(&hash, rect.width);
    cache_hash_u32(&hash, rect.height);
    cache_hash_u32(&hash, variant);
    key.primary = hash;
    key.secondary = glyph_id;
    key.a = rect.x;
    key.b = rect.y;
    key.c = rect.width;
    key.d = rect.height ^ variant;
    cache_copy_key_bytes(&key, &ptr, sizeof(ptr));
    return key;
}

GlyphCacheKey glyph_cache_key_layout_surface(const GlyphFile *file,
                                             const char *text,
                                             const GlyphLayoutOptions *options,
                                             uint32_t width,
                                             uint32_t height,
                                             int32_t origin_x,
                                             int32_t origin_y) {
    GlyphCacheKey key;
    uintptr_t ptr = (uintptr_t)file;
    uint64_t hash = GLYPH_CACHE_FNV_OFFSET;

    glyph_cache_key_init(&key, GLYPH_CACHE_ENTRY_LAYOUT_SURFACE);
    hash = glyph_cache_hash_bytes(&ptr, sizeof(ptr), hash);
    hash = glyph_cache_hash_string(text, hash);
    hash = glyph_cache_hash_layout_options(options, hash);
    cache_hash_u32(&hash, width);
    cache_hash_u32(&hash, height);
    cache_hash_u32(&hash, (uint32_t)origin_x);
    cache_hash_u32(&hash, (uint32_t)origin_y);
    key.primary = hash;
    key.secondary = glyph_cache_hash_string(text, GLYPH_CACHE_FNV_OFFSET);
    key.a = width;
    key.b = height;
    key.c = (uint32_t)origin_x;
    key.d = (uint32_t)origin_y;
    if (text) {
        cache_copy_key_bytes(&key, text, strlen(text));
    } else {
        cache_copy_key_bytes(&key, &ptr, sizeof(ptr));
    }
    return key;
}

int glyph_cache_key_equal(const GlyphCacheKey *a, const GlyphCacheKey *b) {
    if (a == b) {
        return 1;
    }
    if (!a || !b) {
        return 0;
    }
    if (a->type != b->type || a->primary != b->primary || a->secondary != b->secondary ||
        a->a != b->a || a->b != b->b || a->c != b->c || a->d != b->d ||
        a->byte_count != b->byte_count) {
        return 0;
    }
    if (a->byte_count > GLYPH_CACHE_KEY_BYTES || b->byte_count > GLYPH_CACHE_KEY_BYTES) {
        return 0;
    }
    return memcmp(a->bytes, b->bytes, a->byte_count) == 0;
}

int glyph_cache_key_write(FILE *fp, const GlyphCacheKey *key) {
    size_t i;

    if (!fp || !key) {
        return 0;
    }
    if (fprintf(fp,
                "%s primary=%llu secondary=%llu a=%u b=%u c=%u d=%u bytes=",
                cache_entry_type_name(key->type),
                (unsigned long long)key->primary,
                (unsigned long long)key->secondary,
                key->a,
                key->b,
                key->c,
                key->d) < 0) {
        return 0;
    }
    for (i = 0; i < key->byte_count && i < GLYPH_CACHE_KEY_BYTES; i++) {
        if (fprintf(fp, "%02x", (unsigned)key->bytes[i]) < 0) {
            return 0;
        }
    }
    return fputc('\n', fp) != EOF;
}

void glyph_cache_stats_init(GlyphCacheStats *stats) {
    if (stats) {
        memset(stats, 0, sizeof(*stats));
    }
}

double glyph_cache_stats_hit_rate(const GlyphCacheStats *stats) {
    if (!stats || stats->lookups == 0) {
        return 0.0;
    }
    return (double)stats->hits / (double)stats->lookups;
}

void glyph_cache_init(GlyphCache *cache) {
    if (cache) {
        memset(cache, 0, sizeof(*cache));
        cache->max_entries = GLYPH_CACHE_NO_LIMIT;
        cache->max_bytes = GLYPH_CACHE_NO_LIMIT;
    }
}

int glyph_cache_create(GlyphCache *cache, size_t max_entries, size_t max_bytes) {
    if (!cache) {
        return 0;
    }
    glyph_cache_init(cache);
    cache->max_entries = max_entries ? max_entries : GLYPH_CACHE_NO_LIMIT;
    cache->max_bytes = max_bytes ? max_bytes : GLYPH_CACHE_NO_LIMIT;
    return cache_alloc_table(cache, GLYPH_CACHE_INITIAL_TABLE);
}

void glyph_cache_free(GlyphCache *cache) {
    GlyphCacheStats stats;

    if (!cache) {
        return;
    }
    stats = cache->stats;
    cache_remove_all(cache, GLYPH_CACHE_EVICT_DESTROY);
    memset(cache, 0, sizeof(*cache));
    cache->max_entries = GLYPH_CACHE_NO_LIMIT;
    cache->max_bytes = GLYPH_CACHE_NO_LIMIT;
    cache->stats = stats;
}

void glyph_cache_set_capacity(GlyphCache *cache, size_t max_entries, size_t max_bytes) {
    if (!cache) {
        return;
    }
    cache->max_entries = max_entries;
    cache->max_bytes = max_bytes;
    cache_trim(cache);
}

void glyph_cache_set_eviction_callback(GlyphCache *cache, GlyphCacheEvictFn evict, void *user_data) {
    if (!cache) {
        return;
    }
    cache->evict = evict;
    cache->evict_user_data = user_data;
}

void glyph_cache_clear(GlyphCache *cache) {
    if (!cache) {
        return;
    }
    cache_remove_all(cache, GLYPH_CACHE_EVICT_CLEAR);
    cache->stats.clears++;
}

size_t glyph_cache_entry_count(const GlyphCache *cache) {
    return cache ? cache->entry_count : 0;
}

size_t glyph_cache_byte_count(const GlyphCache *cache) {
    return cache ? cache->byte_count : 0;
}

size_t glyph_cache_max_entries(const GlyphCache *cache) {
    return cache ? cache->max_entries : 0;
}

size_t glyph_cache_max_bytes(const GlyphCache *cache) {
    return cache ? cache->max_bytes : 0;
}

const GlyphCacheStats *glyph_cache_stats(const GlyphCache *cache) {
    return cache ? &cache->stats : NULL;
}

void glyph_cache_reset_stats(GlyphCache *cache) {
    if (cache) {
        glyph_cache_stats_init(&cache->stats);
    }
}

int glyph_cache_insert_file(GlyphCache *cache, const GlyphCacheKey *key, const GlyphFile *file) {
    GlyphCacheEntry *entry;
    size_t charge;

    if (!cache || !key || key->type != GLYPH_CACHE_ENTRY_FILE || !file) {
        return 0;
    }
    if (!cache_charge_file(&charge)) {
        return 0;
    }
    entry = cache_alloc_entry(key, charge);
    if (!entry) {
        cache->stats.failed_allocations++;
        return 0;
    }
    entry->data.file.file = file;
    return cache_finish_insert(cache, entry);
}

int glyph_cache_insert_bitmap_slice(GlyphCache *cache,
                                    const GlyphCacheKey *key,
                                    uint32_t glyph_id,
                                    GlyphBitmapRect rect,
                                    const GlyphBitmap *bitmap) {
    GlyphCacheEntry *entry;
    size_t charge;

    if (!cache || !key || key->type != GLYPH_CACHE_ENTRY_BITMAP_SLICE) {
        return 0;
    }
    if (!cache_charge_bitmap(bitmap, &charge)) {
        return 0;
    }
    entry = cache_alloc_entry(key, charge);
    if (!entry) {
        cache->stats.failed_allocations++;
        return 0;
    }
    if (!cache_copy_bitmap(&entry->data.slice.bitmap, bitmap)) {
        cache->stats.failed_allocations++;
        free(entry);
        return 0;
    }
    entry->data.slice.glyph_id = glyph_id;
    entry->data.slice.rect = rect;
    return cache_finish_insert(cache, entry);
}

int glyph_cache_insert_layout_surface(GlyphCache *cache,
                                      const GlyphCacheKey *key,
                                      const GlyphLayoutOptions *options,
                                      int32_t origin_x,
                                      int32_t origin_y,
                                      const GlyphBitmap *surface) {
    GlyphCacheEntry *entry;
    size_t charge;

    if (!cache || !key || key->type != GLYPH_CACHE_ENTRY_LAYOUT_SURFACE) {
        return 0;
    }
    if (!cache_charge_bitmap(surface, &charge)) {
        return 0;
    }
    entry = cache_alloc_entry(key, charge);
    if (!entry) {
        cache->stats.failed_allocations++;
        return 0;
    }
    if (!cache_copy_bitmap(&entry->data.surface.surface, surface)) {
        cache->stats.failed_allocations++;
        free(entry);
        return 0;
    }
    if (options) {
        entry->data.surface.options = *options;
    } else {
        glyph_layout_options_default(&entry->data.surface.options);
    }
    entry->data.surface.origin_x = origin_x;
    entry->data.surface.origin_y = origin_y;
    return cache_finish_insert(cache, entry);
}

const GlyphFile *glyph_cache_lookup_file(GlyphCache *cache, const GlyphCacheKey *key) {
    GlyphCacheEntry *entry;

    if (!cache || !key || key->type != GLYPH_CACHE_ENTRY_FILE) {
        return NULL;
    }
    cache->stats.lookups++;
    entry = cache_find_entry_const(cache, key);
    if (!entry || entry->key.type != GLYPH_CACHE_ENTRY_FILE) {
        cache->stats.misses++;
        return NULL;
    }
    cache->stats.hits++;
    cache->clock++;
    entry->last_access = cache->clock;
    cache_refresh_entry_view_times(entry);
    cache_lru_touch(cache, entry);
    return entry->data.file.file;
}

const GlyphCacheBitmapSliceView *glyph_cache_lookup_bitmap_slice(GlyphCache *cache,
                                                                 const GlyphCacheKey *key) {
    GlyphCacheEntry *entry;

    if (!cache || !key || key->type != GLYPH_CACHE_ENTRY_BITMAP_SLICE) {
        return NULL;
    }
    cache->stats.lookups++;
    entry = cache_find_entry_const(cache, key);
    if (!entry || entry->key.type != GLYPH_CACHE_ENTRY_BITMAP_SLICE) {
        cache->stats.misses++;
        return NULL;
    }
    cache->stats.hits++;
    cache->clock++;
    entry->last_access = cache->clock;
    cache_refresh_entry_view_times(entry);
    cache_lru_touch(cache, entry);
    return &entry->data.slice;
}

const GlyphCacheLayoutSurfaceView *glyph_cache_lookup_layout_surface(GlyphCache *cache,
                                                                     const GlyphCacheKey *key) {
    GlyphCacheEntry *entry;

    if (!cache || !key || key->type != GLYPH_CACHE_ENTRY_LAYOUT_SURFACE) {
        return NULL;
    }
    cache->stats.lookups++;
    entry = cache_find_entry_const(cache, key);
    if (!entry || entry->key.type != GLYPH_CACHE_ENTRY_LAYOUT_SURFACE) {
        cache->stats.misses++;
        return NULL;
    }
    cache->stats.hits++;
    cache->clock++;
    entry->last_access = cache->clock;
    cache_refresh_entry_view_times(entry);
    cache_lru_touch(cache, entry);
    return &entry->data.surface;
}

int glyph_cache_lookup_entry(GlyphCache *cache, const GlyphCacheKey *key, GlyphCacheEntryView *view) {
    GlyphCacheEntry *entry;

    if (!cache || !key || !view) {
        return 0;
    }
    cache->stats.lookups++;
    entry = cache_find_entry_const(cache, key);
    if (!entry) {
        cache->stats.misses++;
        memset(view, 0, sizeof(*view));
        return 0;
    }
    cache->stats.hits++;
    cache->clock++;
    entry->last_access = cache->clock;
    cache_refresh_entry_view_times(entry);
    cache_lru_touch(cache, entry);
    cache_entry_view(entry, view);
    return 1;
}

int glyph_cache_peek_file(const GlyphCache *cache, const GlyphCacheKey *key, const GlyphFile **file) {
    GlyphCacheEntry *entry;

    if (file) {
        *file = NULL;
    }
    if (!cache || !key || key->type != GLYPH_CACHE_ENTRY_FILE || !file) {
        return 0;
    }
    entry = cache_find_entry_const(cache, key);
    if (!entry || entry->key.type != GLYPH_CACHE_ENTRY_FILE) {
        return 0;
    }
    *file = entry->data.file.file;
    return 1;
}

int glyph_cache_peek_bitmap_slice(const GlyphCache *cache,
                                  const GlyphCacheKey *key,
                                  GlyphCacheBitmapSliceView *view) {
    GlyphCacheEntry *entry;

    if (view) {
        memset(view, 0, sizeof(*view));
    }
    if (!cache || !key || key->type != GLYPH_CACHE_ENTRY_BITMAP_SLICE || !view) {
        return 0;
    }
    entry = cache_find_entry_const(cache, key);
    if (!entry || entry->key.type != GLYPH_CACHE_ENTRY_BITMAP_SLICE) {
        return 0;
    }
    *view = entry->data.slice;
    return 1;
}

int glyph_cache_peek_layout_surface(const GlyphCache *cache,
                                    const GlyphCacheKey *key,
                                    GlyphCacheLayoutSurfaceView *view) {
    GlyphCacheEntry *entry;

    if (view) {
        memset(view, 0, sizeof(*view));
    }
    if (!cache || !key || key->type != GLYPH_CACHE_ENTRY_LAYOUT_SURFACE || !view) {
        return 0;
    }
    entry = cache_find_entry_const(cache, key);
    if (!entry || entry->key.type != GLYPH_CACHE_ENTRY_LAYOUT_SURFACE) {
        return 0;
    }
    *view = entry->data.surface;
    return 1;
}

int glyph_cache_contains(const GlyphCache *cache, const GlyphCacheKey *key) {
    return cache_find_entry_const(cache, key) != NULL;
}

int glyph_cache_remove(GlyphCache *cache, const GlyphCacheKey *key) {
    GlyphCacheEntry *entry;

    if (!cache || !key) {
        return 0;
    }
    entry = cache_find_entry_const(cache, key);
    if (!entry) {
        return 0;
    }
    cache_forget_entry(cache, entry, GLYPH_CACHE_EVICT_EXPLICIT);
    return 1;
}

size_t glyph_cache_remove_file(GlyphCache *cache, const GlyphFile *file) {
    GlyphCacheEntry *entry;
    size_t removed = 0;

    if (!cache || !file) {
        return 0;
    }
    entry = cache->lru_head;
    while (entry) {
        GlyphCacheEntry *next = entry->lru_next;
        int match = 0;
        if (entry->key.type == GLYPH_CACHE_ENTRY_FILE && entry->data.file.file == file) {
            match = 1;
        }
        if (match) {
            cache_forget_entry(cache, entry, GLYPH_CACHE_EVICT_EXPLICIT);
            removed++;
        }
        entry = next;
    }
    return removed;
}

size_t glyph_cache_remove_type(GlyphCache *cache, GlyphCacheEntryType type) {
    GlyphCacheEntry *entry;
    size_t removed = 0;

    if (!cache) {
        return 0;
    }
    entry = cache->lru_head;
    while (entry) {
        GlyphCacheEntry *next = entry->lru_next;
        if (entry->key.type == type) {
            cache_forget_entry(cache, entry, GLYPH_CACHE_EVICT_EXPLICIT);
            removed++;
        }
        entry = next;
    }
    return removed;
}

size_t glyph_cache_evict_to_capacity(GlyphCache *cache) {
    return cache_trim(cache);
}

int glyph_cache_touch(GlyphCache *cache, const GlyphCacheKey *key) {
    GlyphCacheEntry *entry;

    if (!cache || !key) {
        return 0;
    }
    entry = cache_find_entry_const(cache, key);
    if (!entry) {
        return 0;
    }
    cache->clock++;
    entry->last_access = cache->clock;
    cache_refresh_entry_view_times(entry);
    cache_lru_touch(cache, entry);
    return 1;
}

int glyph_cache_validate(const GlyphCache *cache, GlyphCacheValidationReport *report) {
    const GlyphCacheEntry *entry;
    size_t lru_count = 0;
    size_t table_count = 0;
    size_t bytes = 0;
    size_t i;

    if (report) {
        memset(report, 0, sizeof(*report));
    }
    if (!cache) {
        cache_validation_set(report, GLYPH_CACHE_VALIDATE_NULL_CACHE, "cache pointer is null", 0, 0, 0);
        return 0;
    }
    if (cache->max_entries != GLYPH_CACHE_NO_LIMIT && cache->entry_count > cache->max_entries) {
        cache_validation_set(report,
                             GLYPH_CACHE_VALIDATE_BAD_LIMITS,
                             "entry count exceeds configured capacity",
                             cache->entry_count,
                             cache->byte_count,
                             0);
        return 0;
    }
    if (cache->max_bytes != GLYPH_CACHE_NO_LIMIT && cache->byte_count > cache->max_bytes) {
        cache_validation_set(report,
                             GLYPH_CACHE_VALIDATE_BAD_LIMITS,
                             "byte count exceeds configured capacity",
                             cache->entry_count,
                             cache->byte_count,
                             0);
        return 0;
    }
    if ((cache->table == NULL) != (cache->table_capacity == 0)) {
        cache_validation_set(report,
                             GLYPH_CACHE_VALIDATE_BAD_TABLE,
                             "hash table pointer and capacity disagree",
                             cache->entry_count,
                             cache->byte_count,
                             0);
        return 0;
    }
    if (!cache->entry_count && (cache->lru_head || cache->lru_tail)) {
        cache_validation_set(report,
                             GLYPH_CACHE_VALIDATE_BAD_LRU,
                             "empty cache has non-empty lru list",
                             cache->entry_count,
                             cache->byte_count,
                             0);
        return 0;
    }
    if (cache->entry_count && (!cache->lru_head || !cache->lru_tail)) {
        cache_validation_set(report,
                             GLYPH_CACHE_VALIDATE_BAD_LRU,
                             "non-empty cache has incomplete lru endpoints",
                             cache->entry_count,
                             cache->byte_count,
                             0);
        return 0;
    }

    entry = cache->lru_head;
    while (entry) {
        const GlyphCacheEntry *next = entry->lru_next;
        if (entry->lru_next && entry->lru_next->lru_prev != entry) {
            cache_validation_set(report,
                                 GLYPH_CACHE_VALIDATE_BAD_LRU,
                                 "lru forward and backward links disagree",
                                 lru_count,
                                 bytes,
                                 table_count);
            return 0;
        }
        if (entry->lru_prev == NULL && cache->lru_head != entry) {
            cache_validation_set(report,
                                 GLYPH_CACHE_VALIDATE_BAD_LRU,
                                 "lru head link is inconsistent",
                                 lru_count,
                                 bytes,
                                 table_count);
            return 0;
        }
        if (next == NULL && cache->lru_tail != entry) {
            cache_validation_set(report,
                                 GLYPH_CACHE_VALIDATE_BAD_LRU,
                                 "lru tail link is inconsistent",
                                 lru_count,
                                 bytes,
                                 table_count);
            return 0;
        }
        if (!cache_entry_payload_valid(entry)) {
            cache_validation_set(report,
                                 GLYPH_CACHE_VALIDATE_BAD_ENTRY,
                                 "entry payload is invalid",
                                 lru_count,
                                 bytes,
                                 table_count);
            return 0;
        }
        if (!cache_checked_add(bytes, entry->charge, &bytes)) {
            cache_validation_set(report,
                                 GLYPH_CACHE_VALIDATE_BAD_ACCOUNTING,
                                 "entry charge overflows aggregate byte count",
                                 lru_count,
                                 bytes,
                                 table_count);
            return 0;
        }
        lru_count++;
        entry = next;
    }

    if (cache->table) {
        for (i = 0; i < cache->table_capacity; i++) {
            entry = cache->table[i];
            while (entry) {
                if (cache_table_index(cache, entry->hash) != i) {
                    cache_validation_set(report,
                                         GLYPH_CACHE_VALIDATE_BAD_TABLE,
                                         "entry is stored in the wrong hash bucket",
                                         lru_count,
                                         bytes,
                                         table_count);
                    return 0;
                }
                if (entry->hash != cache_key_hash(&entry->key)) {
                    cache_validation_set(report,
                                         GLYPH_CACHE_VALIDATE_BAD_TABLE,
                                         "entry hash does not match its key",
                                         lru_count,
                                         bytes,
                                         table_count);
                    return 0;
                }
                table_count++;
                entry = entry->bucket_next;
            }
        }
    }

    if (lru_count != cache->entry_count || table_count != cache->entry_count || bytes != cache->byte_count) {
        cache_validation_set(report,
                             GLYPH_CACHE_VALIDATE_BAD_ACCOUNTING,
                             "lru, table, or byte accounting does not match cache counters",
                             lru_count,
                             bytes,
                             table_count);
        return 0;
    }

    cache_validation_set(report,
                         GLYPH_CACHE_VALIDATE_OK,
                         "cache is valid",
                         cache->entry_count,
                         cache->byte_count,
                         table_count);
    return 1;
}

const char *glyph_cache_validation_code_name(GlyphCacheValidationCode code) {
    switch (code) {
    case GLYPH_CACHE_VALIDATE_OK:
        return "ok";
    case GLYPH_CACHE_VALIDATE_NULL_CACHE:
        return "null-cache";
    case GLYPH_CACHE_VALIDATE_BAD_LIMITS:
        return "bad-limits";
    case GLYPH_CACHE_VALIDATE_BAD_TABLE:
        return "bad-table";
    case GLYPH_CACHE_VALIDATE_BAD_LRU:
        return "bad-lru";
    case GLYPH_CACHE_VALIDATE_BAD_ACCOUNTING:
        return "bad-accounting";
    case GLYPH_CACHE_VALIDATE_BAD_ENTRY:
        return "bad-entry";
    default:
        return "unknown";
    }
}

int glyph_cache_write_report(FILE *fp, const GlyphCache *cache) {
    GlyphCacheValidationReport validation;
    const GlyphCacheStats *stats;
    const GlyphCacheEntry *entry;
    size_t index = 0;

    if (!fp || !cache) {
        return 0;
    }
    stats = &cache->stats;
    if (fprintf(fp, "glyph cache report\n") < 0) {
        return 0;
    }
    if (fprintf(fp, "  entries: %lu / ", (unsigned long)cache->entry_count) < 0) {
        return 0;
    }
    if (cache->max_entries == GLYPH_CACHE_NO_LIMIT) {
        if (fprintf(fp, "unlimited\n") < 0) {
            return 0;
        }
    } else if (fprintf(fp, "%lu\n", (unsigned long)cache->max_entries) < 0) {
        return 0;
    }
    if (fprintf(fp, "  bytes: %lu / ", (unsigned long)cache->byte_count) < 0) {
        return 0;
    }
    if (cache->max_bytes == GLYPH_CACHE_NO_LIMIT) {
        if (fprintf(fp, "unlimited\n") < 0) {
            return 0;
        }
    } else if (fprintf(fp, "%lu\n", (unsigned long)cache->max_bytes) < 0) {
        return 0;
    }
    if (fprintf(fp,
                "  table buckets: %lu\n"
                "  clock: %llu\n"
                "  lookups: %llu\n"
                "  hits: %llu\n"
                "  misses: %llu\n"
                "  hit rate: %.3f\n"
                "  inserts: %llu\n"
                "  replacements: %llu\n"
                "  removals: %llu\n"
                "  evictions: %llu\n"
                "  capacity evictions: %llu\n"
                "  clears: %llu\n"
                "  failed allocations: %llu\n"
                "  validation failures: %llu\n",
                (unsigned long)cache->table_capacity,
                (unsigned long long)cache->clock,
                (unsigned long long)stats->lookups,
                (unsigned long long)stats->hits,
                (unsigned long long)stats->misses,
                glyph_cache_stats_hit_rate(stats),
                (unsigned long long)stats->inserts,
                (unsigned long long)stats->replacements,
                (unsigned long long)stats->removals,
                (unsigned long long)stats->evictions,
                (unsigned long long)stats->capacity_evictions,
                (unsigned long long)stats->clears,
                (unsigned long long)stats->failed_allocations,
                (unsigned long long)stats->validation_failures) < 0) {
        return 0;
    }
    if (glyph_cache_validate(cache, &validation)) {
        if (fprintf(fp, "  validation: %s\n", validation.message) < 0) {
            return 0;
        }
    } else {
        if (fprintf(fp,
                    "  validation: %s (%s)\n",
                    validation.message,
                    glyph_cache_validation_code_name(validation.code)) < 0) {
            return 0;
        }
    }
    if (fprintf(fp, "  eviction reasons: %s, %s, %s, %s, %s\n",
                cache_evict_reason_name(GLYPH_CACHE_EVICT_EXPLICIT),
                cache_evict_reason_name(GLYPH_CACHE_EVICT_REPLACED),
                cache_evict_reason_name(GLYPH_CACHE_EVICT_CAPACITY),
                cache_evict_reason_name(GLYPH_CACHE_EVICT_CLEAR),
                cache_evict_reason_name(GLYPH_CACHE_EVICT_DESTROY)) < 0) {
        return 0;
    }
    if (fprintf(fp, "  lru entries:\n") < 0) {
        return 0;
    }
    entry = cache->lru_head;
    while (entry) {
        if (fprintf(fp,
                    "    %lu: type=%s charge=%lu generation=%llu last_access=%llu\n",
                    (unsigned long)index,
                    cache_entry_type_name(entry->key.type),
                    (unsigned long)entry->charge,
                    (unsigned long long)entry->generation,
                    (unsigned long long)entry->last_access) < 0) {
            return 0;
        }
        if (entry->key.type == GLYPH_CACHE_ENTRY_FILE) {
            if (fprintf(fp, "      file=%p\n", (const void *)entry->data.file.file) < 0) {
                return 0;
            }
        } else if (entry->key.type == GLYPH_CACHE_ENTRY_BITMAP_SLICE) {
            if (fprintf(fp,
                        "      glyph_id=%u rect=%u,%u %ux%u bitmap=%ux%u stride=%u\n",
                        entry->data.slice.glyph_id,
                        entry->data.slice.rect.x,
                        entry->data.slice.rect.y,
                        entry->data.slice.rect.width,
                        entry->data.slice.rect.height,
                        entry->data.slice.bitmap.width,
                        entry->data.slice.bitmap.height,
                        entry->data.slice.bitmap.stride) < 0) {
                return 0;
            }
        } else if (entry->key.type == GLYPH_CACHE_ENTRY_LAYOUT_SURFACE) {
            if (fprintf(fp,
                        "      origin=%d,%d surface=%ux%u stride=%u align=%d wrap=%d line=%d tab=%d fallback=%d\n",
                        entry->data.surface.origin_x,
                        entry->data.surface.origin_y,
                        entry->data.surface.surface.width,
                        entry->data.surface.surface.height,
                        entry->data.surface.surface.stride,
                        (int)entry->data.surface.options.align,
                        entry->data.surface.options.wrap_width,
                        entry->data.surface.options.line_height,
                        entry->data.surface.options.tab_width,
                        entry->data.surface.options.fallback_advance) < 0) {
                return 0;
            }
        }
        if (fprintf(fp, "      key: ") < 0 || !glyph_cache_key_write(fp, &entry->key)) {
            return 0;
        }
        index++;
        entry = entry->lru_next;
    }
    return 1;
}
