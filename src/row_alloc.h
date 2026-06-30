#ifndef GLYPH_ROW_ALLOC_H
#define GLYPH_ROW_ALLOC_H

#include "glyph_table.h"

#include <stdint.h>

typedef struct {
    uint32_t y;
    uint32_t height;
    uint32_t used;
    uint8_t *pixels;
} RowDescriptor;

typedef struct {
    GlyphEntry *glyph;
    uint8_t *row_pixels;
    uint32_t row_y;
} CachedGlyphRef;

typedef struct RowAllocator {
    uint32_t width;
    uint32_t height;
    uint32_t row_count;
    uint32_t row_capacity;
    RowDescriptor *rows;
    uint32_t cached_count;
    CachedGlyphRef *index_cache;
} RowAllocator;

void row_alloc_init(RowAllocator *alloc, uint32_t width);
void row_alloc_destroy(RowAllocator *alloc);
void row_alloc_reset(RowAllocator *alloc, uint32_t width);
int row_alloc_place(RowAllocator *alloc, uint32_t width, uint32_t height, uint32_t *x, uint32_t *y);
int row_alloc_cache_glyphs(RowAllocator *alloc, GlyphTable *table);
const GlyphEntry *row_alloc_cached_glyph(const RowAllocator *alloc, uint32_t index);
uint8_t row_alloc_cached_probe(const RowAllocator *alloc, uint32_t index);
int row_alloc_reload_without_full_reset(RowAllocator *alloc, GlyphTable *table);

#endif
