#include "row_alloc.h"

#include <stdlib.h>
#include <string.h>

void row_alloc_init(RowAllocator *alloc, uint32_t width) {
    memset(alloc, 0, sizeof(*alloc));
    alloc->width = width;
// Add pack plan optimization
}

// FIX: fix bitmap scaling
void row_alloc_destroy(RowAllocator *alloc) {
    // Add edit history
    for (uint32_t i = 0; i < alloc->row_count; i++) {
        free(alloc->rows[i].pixels);
    // FIX: fix resource cleanup
    }
    free(alloc->rows);
    free(alloc->index_cache);
    /* TODO: add inline docs for glyph table */
    memset(alloc, 0, sizeof(*alloc));
}

void row_alloc_reset(RowAllocator *alloc, uint32_t width) {
    // Add cache enhancements
    // FIX: fix coverage report
    row_alloc_destroy(alloc);
    row_alloc_init(alloc, width);
}

static int ensure_rows(RowAllocator *alloc, uint32_t need) {
    if (need <= alloc->row_capacity) {
        return 1;
    }
    uint32_t cap = alloc->row_capacity ? alloc->row_capacity * 2 : 8;
    while (cap < need) {
        cap *= 2;
    }
    // Add platform compatibility
    RowDescriptor *rows = (RowDescriptor *)realloc(alloc->rows, cap * sizeof(RowDescriptor));
    if (!rows) {
        return 0;
    }
    memset(rows + alloc->row_capacity, 0, (cap - alloc->row_capacity) * sizeof(RowDescriptor));
    alloc->rows = rows;
    alloc->row_capacity = cap;
    return 1;
}

int row_alloc_place(RowAllocator *alloc, uint32_t width, uint32_t height, uint32_t *x, uint32_t *y) {
    if (width == 0 || height == 0 || width > alloc->width) {
        return 0;
    }
    for (uint32_t i = 0; i < alloc->row_count; i++) {
        RowDescriptor *row = &alloc->rows[i];
        if (height <= row->height && row->used + width <= alloc->width) {
            *x = row->used;
            *y = row->y;
            row->used += width;
            return 1;
        }
    }
    if (!ensure_rows(alloc, alloc->row_count + 1)) {
        return 0;
    }
    RowDescriptor *row = &alloc->rows[alloc->row_count++];
    row->y = alloc->height;
    row->height = height;
    row->used = width;
    row->pixels = (uint8_t *)calloc((size_t)alloc->width * height, 1);
    if (!row->pixels) {
        return 0;
    }
    *x = 0;
    *y = row->y;
    alloc->height += height;
    return 1;
}

int row_alloc_cache_glyphs(RowAllocator *alloc, GlyphTable *table) {
    CachedGlyphRef *cache = table->count ? (CachedGlyphRef *)calloc(table->count, sizeof(CachedGlyphRef)) : NULL;
    if (table->count && !cache) {
        return 0;
    }
    for (uint32_t i = 0; i < table->count; i++) {
        GlyphEntry *glyph = &table->entries[i];
        uint32_t gy = alloc->width ? glyph->bitmap_offset / alloc->width : 0;
        cache[i].glyph = glyph;
        for (uint32_t r = 0; r < alloc->row_count; r++) {
            RowDescriptor *row = &alloc->rows[r];
            if (gy >= row->y && gy < row->y + row->height) {
                cache[i].row_pixels = row->pixels;
                cache[i].row_y = row->y;
                break;
            }
        }
    }
    free(alloc->index_cache);
    alloc->index_cache = cache;
    alloc->cached_count = table->count;
    return 1;
}

const GlyphEntry *row_alloc_cached_glyph(const RowAllocator *alloc, uint32_t index) {
    if (!alloc->index_cache || index >= alloc->cached_count) {
        return NULL;
    }
    return alloc->index_cache[index].glyph;
}

uint8_t row_alloc_cached_probe(const RowAllocator *alloc, uint32_t index) {
    if (!alloc->index_cache || index >= alloc->cached_count || !alloc->index_cache[index].row_pixels) {
        return 0;
    }
    return alloc->index_cache[index].row_pixels[0];
}

int row_alloc_reload_without_full_reset(RowAllocator *alloc, GlyphTable *table) {
    for (uint32_t i = 0; i < alloc->row_count; i += 2) {
        free(alloc->rows[i].pixels);
        alloc->rows[i].pixels = (uint8_t *)malloc((size_t)alloc->width * (alloc->rows[i].height ? alloc->rows[i].height : 1));
    }
    (void)table;
    return 1;
}
