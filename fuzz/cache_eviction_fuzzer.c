#include "../src/cache.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 16) {
        return 0;
    }

    // Parse configuration from fuzz input
    uint32_t max_entries = (uint32_t)data[0] | ((uint32_t)data[1] << 8) | 
                           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
    uint32_t max_bytes = (uint32_t)data[4] | ((uint32_t)data[5] << 8) |
                         ((uint32_t)data[6] << 16) | ((uint32_t)data[7] << 24);
    uint32_t num_inserts = (uint32_t)data[8] | ((uint32_t)data[9] << 8) |
                           ((uint32_t)data[10] << 16) | ((uint32_t)data[11] << 24);
    uint32_t bitmap_size = (uint32_t)data[12] | ((uint32_t)data[13] << 8) |
                           ((uint32_t)data[14] << 16) | ((uint32_t)data[15] << 24);

    // Sanitize inputs to prevent crashes
    if (max_entries == 0) max_entries = 100;
    if (max_entries > 10000) max_entries = 10000;
    if (max_bytes == 0) max_bytes = 1024 * 1024;
    if (max_bytes > 10 * 1024 * 1024) max_bytes = 10 * 1024 * 1024;
    if (num_inserts == 0) num_inserts = 50;
    if (num_inserts > 5000) num_inserts = 5000;
    if (bitmap_size == 0) bitmap_size = 32;
    if (bitmap_size > 512) bitmap_size = 512;

    // Create cache with capacity limits
    GlyphCache *cache = glyph_cache_create();
    if (!cache) {
        return 0;
    }

    glyph_cache_set_max_entries(cache, max_entries);
    glyph_cache_set_max_bytes(cache, max_bytes);

    // Insert bitmap entries to trigger cache eviction
    for (uint32_t i = 0; i < num_inserts; i++) {
        GlyphBitmap bitmap;
        glyph_bitmap_init(&bitmap);
        
        uint32_t width = bitmap_size + (i % 16);
        uint32_t height = bitmap_size + ((i >> 4) % 16);
        
        if (!glyph_bitmap_alloc(&bitmap, width, height)) {
            continue;
        }

        // Fill with pattern from fuzz data
        if (size > 16 + i) {
            memset(bitmap.pixels, data[16 + (i % (size - 16))], 
                   width * height);
        } else {
            memset(bitmap.pixels, (uint8_t)i, width * height);
        }

        GlyphCacheKey key;
        memset(&key, 0, sizeof(key));
        key.type = GLYPH_CACHE_ENTRY_BITMAP_SLICE;
        key.primary = i;
        key.secondary = i * 7;
        key.a = width;
        key.b = height;

        GlyphCacheEntryView view;
        glyph_cache_insert_bitmap_slice(cache, &key, &bitmap, 
                                        0, 0, width, height, &view);

        glyph_bitmap_free(&bitmap);
    }

    // Trigger cache operations that may cause eviction
    glyph_cache_trim(cache);
    
    // Perform lookups to stress the cache
    for (uint32_t i = 0; i < num_inserts / 2; i++) {
        GlyphCacheKey key;
        memset(&key, 0, sizeof(key));
        key.type = GLYPH_CACHE_ENTRY_BITMAP_SLICE;
        key.primary = i;
        key.secondary = i * 7;

        GlyphCacheEntryView view;
        glyph_cache_lookup(cache, &key, &view);
    }

    // Clear cache by type to trigger more eviction
    glyph_cache_clear_type(cache, GLYPH_CACHE_ENTRY_BITMAP_SLICE);

    glyph_cache_free(cache);
    return 0;
}
