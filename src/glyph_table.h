#ifndef GLYPH_TABLE_H
#define GLYPH_TABLE_H

#include <stdint.h>
#include <stdio.h>

typedef struct {
    uint32_t id;
    int16_t x;
    int16_t y;
    uint16_t width;
    uint16_t height;
    uint32_t bitmap_offset;
    int16_t advance;
} GlyphEntry;

typedef struct {
    uint32_t count;
    GlyphEntry *entries;
} GlyphTable;

int glyph_table_alloc(GlyphTable *table, uint32_t count);
void glyph_table_free(GlyphTable *table);
int glyph_table_read(FILE *fp, GlyphTable *table, uint32_t count);
int glyph_table_write(FILE *fp, const GlyphTable *table);
int glyph_table_parse_bytes(const uint8_t *data, size_t size, size_t *pos, GlyphTable *table, uint32_t count);
int glyph_table_find_id(const GlyphTable *table, uint32_t id);

#endif
