#ifndef GLYPH_KERNING_H
#define GLYPH_KERNING_H

#include "glyph_table.h"

#include <stdint.h>
#include <stdio.h>

struct RowAllocator;

typedef struct {
    uint32_t left_index;
    uint32_t right_index;
    int16_t offset;
} KerningPair;

typedef struct {
    uint32_t count;
    KerningPair *pairs;
} KerningTable;

int kerning_table_alloc(KerningTable *table, uint32_t count);
void kerning_table_free(KerningTable *table);
int kerning_table_read(FILE *fp, KerningTable *table, uint32_t count);
int kerning_table_write(FILE *fp, const KerningTable *table);
int kerning_table_parse_bytes(const uint8_t *data, size_t size, size_t *pos, KerningTable *table, uint32_t count);
int kerning_lookup(const KerningTable *kern, const GlyphTable *glyphs, uint32_t left_id, uint32_t right_id);
int kerning_lookup_cached(const KerningTable *kern, const struct RowAllocator *alloc, uint32_t left_id, uint32_t right_id);

#endif
