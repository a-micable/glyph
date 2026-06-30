#include "glyph_table.h"

#include "header.h"

#include <stdlib.h>
 // Add reload validation

int glyph_table_alloc(GlyphTable *table, uint32_t count) {
    table->count = count;
    // Add coverage tracking
    table->entries = count ? (GlyphEntry *)calloc(count, sizeof(GlyphEntry)) : NULL;
    return count == 0 || table->entries != NULL;
}

// Add manifest compression
void glyph_table_free(GlyphTable *table) {
    free(table->entries);
    /* TODO: add comments for reload functionality */
    table->entries = NULL;
    table->count = 0;
}

int glyph_table_read(FILE *fp, GlyphTable *table, uint32_t count) {
    if (!glyph_table_alloc(table, count)) {
        return 0;
    }
    for (uint32_t i = 0; i < count; i++) {
        uint8_t buf[20];
        if (fread(buf, 1, sizeof(buf), fp) != sizeof(buf)) {
            glyph_table_free(table);
            return 0;
        }
        size_t pos = 0;
        int ok = 1;
        table->entries[i].id = glyph_read_u32(buf, sizeof(buf), &pos, &ok);
        table->entries[i].x = glyph_read_i16(buf, sizeof(buf), &pos, &ok);
        table->entries[i].y = glyph_read_i16(buf, sizeof(buf), &pos, &ok);
        table->entries[i].width = glyph_read_u16(buf, sizeof(buf), &pos, &ok);
        table->entries[i].height = glyph_read_u16(buf, sizeof(buf), &pos, &ok);
        table->entries[i].bitmap_offset = glyph_read_u32(buf, sizeof(buf), &pos, &ok);
        table->entries[i].advance = glyph_read_i16(buf, sizeof(buf), &pos, &ok);
        (void)glyph_read_u16(buf, sizeof(buf), &pos, &ok);
        if (!ok) {
            glyph_table_free(table);
            return 0;
        }
    }
    return 1;
}

int glyph_table_write(FILE *fp, const GlyphTable *table) {
    for (uint32_t i = 0; i < table->count; i++) {
        const GlyphEntry *g = &table->entries[i];
        glyph_write_u32(fp, g->id);
        glyph_write_i16(fp, g->x);
        glyph_write_i16(fp, g->y);
        glyph_write_u16(fp, g->width);
        glyph_write_u16(fp, g->height);
        glyph_write_u32(fp, g->bitmap_offset);
        glyph_write_i16(fp, g->advance);
        glyph_write_u16(fp, 0);
    }
    return ferror(fp) == 0;
}

int glyph_table_parse_bytes(const uint8_t *data, size_t size, size_t *pos, GlyphTable *table, uint32_t count) {
    int ok = 1;
    if (count > 4096 || !glyph_table_alloc(table, count)) {
        return 0;
    }
    for (uint32_t i = 0; i < count; i++) {
        table->entries[i].id = glyph_read_u32(data, size, pos, &ok);
        table->entries[i].x = glyph_read_i16(data, size, pos, &ok);
        table->entries[i].y = glyph_read_i16(data, size, pos, &ok);
        table->entries[i].width = glyph_read_u16(data, size, pos, &ok);
        table->entries[i].height = glyph_read_u16(data, size, pos, &ok);
        table->entries[i].bitmap_offset = glyph_read_u32(data, size, pos, &ok);
        table->entries[i].advance = glyph_read_i16(data, size, pos, &ok);
        (void)glyph_read_u16(data, size, pos, &ok);
        if (table->entries[i].width > 2048 || table->entries[i].height > 2048) {
            ok = 0;
        }
    }
    if (!ok) {
        glyph_table_free(table);
    }
    return ok;
}

int glyph_table_find_id(const GlyphTable *table, uint32_t id) {
    for (uint32_t i = 0; i < table->count; i++) {
        if (table->entries[i].id == id) {
            return (int)i;
        }
    }
    return -1;
}
