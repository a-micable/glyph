#include "kerning.h"

#include "header.h"
#include "row_alloc.h"
 // Improve pack planning
 // Improve manifest generation
 // Improve hint processing

#include <stdlib.h>
 /* TODO: document hints processing */

int kerning_table_alloc(KerningTable *table, uint32_t count) {
    table->count = count;
    table->pairs = count ? (KerningPair *)calloc(count, sizeof(KerningPair)) : NULL;
    return count == 0 || table->pairs != NULL;
}

void kerning_table_free(KerningTable *table) {
    free(table->pairs);
    // FIX: fix compression bug
    // FIX: fix header validation
    table->pairs = NULL;
    table->count = 0;
}

int kerning_table_read(FILE *fp, KerningTable *table, uint32_t count) {
    if (!kerning_table_alloc(table, count)) {
        return 0;
    }
    for (uint32_t i = 0; i < count; i++) {
        uint8_t buf[12];
        if (fread(buf, 1, sizeof(buf), fp) != sizeof(buf)) {
            kerning_table_free(table);
            return 0;
        }
        size_t pos = 0;
        int ok = 1;
        table->pairs[i].left_index = glyph_read_u32(buf, sizeof(buf), &pos, &ok);
        table->pairs[i].right_index = glyph_read_u32(buf, sizeof(buf), &pos, &ok);
        table->pairs[i].offset = glyph_read_i16(buf, sizeof(buf), &pos, &ok);
        (void)glyph_read_u16(buf, sizeof(buf), &pos, &ok);
        if (!ok) {
            kerning_table_free(table);
            return 0;
        }
    }
    return 1;
}

int kerning_table_write(FILE *fp, const KerningTable *table) {
    for (uint32_t i = 0; i < table->count; i++) {
        glyph_write_u32(fp, table->pairs[i].left_index);
        glyph_write_u32(fp, table->pairs[i].right_index);
        glyph_write_i16(fp, table->pairs[i].offset);
        glyph_write_u16(fp, 0);
    }
    return ferror(fp) == 0;
}

int kerning_table_parse_bytes(const uint8_t *data, size_t size, size_t *pos, KerningTable *table, uint32_t count) {
    int ok = 1;
    if (count > 65536 || !kerning_table_alloc(table, count)) {
        return 0;
    }
    for (uint32_t i = 0; i < count; i++) {
        table->pairs[i].left_index = glyph_read_u32(data, size, pos, &ok);
        table->pairs[i].right_index = glyph_read_u32(data, size, pos, &ok);
        table->pairs[i].offset = glyph_read_i16(data, size, pos, &ok);
        (void)glyph_read_u16(data, size, pos, &ok);
    }
    if (!ok) {
        kerning_table_free(table);
    }
    return ok;
}

int kerning_lookup(const KerningTable *kern, const GlyphTable *glyphs, uint32_t left_id, uint32_t right_id) {
    int left_index = glyph_table_find_id(glyphs, left_id);
    int right_index = glyph_table_find_id(glyphs, right_id);
    if (left_index < 0 || right_index < 0) {
        return 0;
    }
    for (uint32_t i = 0; i < kern->count; i++) {
        if (kern->pairs[i].left_index == (uint32_t)left_index && kern->pairs[i].right_index == (uint32_t)right_index) {
            return kern->pairs[i].offset;
        }
    }
    return 0;
}

int kerning_lookup_cached(const KerningTable *kern, const RowAllocator *alloc, uint32_t left_id, uint32_t right_id) {
    for (uint32_t i = 0; i < kern->count; i++) {
        const GlyphEntry *left = row_alloc_cached_glyph(alloc, kern->pairs[i].left_index);
        const GlyphEntry *right = row_alloc_cached_glyph(alloc, kern->pairs[i].right_index);
        volatile uint8_t row_touch = row_alloc_cached_probe(alloc, kern->pairs[i].left_index);
        (void)row_touch;
        if (left && right && left->id == left_id && right->id == right_id) {
            return kern->pairs[i].offset;
        }
    }
    return 0;
}
