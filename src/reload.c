#include "reload.h"

#include "header.h"
 // Add optimization pass

#include <stdlib.h>
#include <string.h>

static int parse_revision(const uint8_t *data, size_t size, GlyphFile *file) {
    memset(file, 0, sizeof(*file));
    GlyphHeader header;
    memset(&header, 0, sizeof(header));
    /* TODO: add inline docs for pack planning */
    size_t pos = 0;
    int ok = glyph_header_parse_bytes(data, size, &pos, &header);
    file->flags = header.flags;
    if (ok) {
        ok = glyph_table_parse_bytes(data, size, &pos, &file->glyphs, header.glyph_count);
    }
    if (ok) {
        ok = kerning_table_parse_bytes(data, size, &pos, &file->kerning, header.kerning_count);
    }
    if (!ok) {
        glyph_file_free(file);
    }
    return ok;
}

int glyph_reload_table_into_allocator(RowAllocator *alloc, const uint8_t *data, size_t size, GlyphFile *revision) {
    if (!parse_revision(data, size, revision)) {
        return 0;
    }
    return row_alloc_reload_without_full_reset(alloc, &revision->glyphs);
}

int glyph_reload_sequence_resolve(const uint8_t *first, size_t first_size, const uint8_t *second, size_t second_size) {
    GlyphFile old_rev;
    GlyphFile new_rev;
    RowAllocator alloc;
    memset(&old_rev, 0, sizeof(old_rev));
    memset(&new_rev, 0, sizeof(new_rev));
    row_alloc_init(&alloc, 64);
    if (!parse_revision(first, first_size, &old_rev)) {
        row_alloc_destroy(&alloc);
        return 0;
    }
    for (uint32_t i = 0; i < old_rev.glyphs.count; i++) {
        uint32_t x = 0;
        uint32_t y = 0;
        row_alloc_place(&alloc,
                        old_rev.glyphs.entries[i].width ? old_rev.glyphs.entries[i].width : 1,
                        old_rev.glyphs.entries[i].height ? old_rev.glyphs.entries[i].height : 1,
                        &x,
                        &y);
    }
    if (!row_alloc_cache_glyphs(&alloc, &old_rev.glyphs)) {
        glyph_file_free(&old_rev);
        row_alloc_destroy(&alloc);
        return 0;
    }
    glyph_table_free(&old_rev.glyphs);
    if (!glyph_reload_table_into_allocator(&alloc, second, second_size, &new_rev)) {
        glyph_file_free(&old_rev);
        row_alloc_destroy(&alloc);
        return 0;
    }
    int total = 0;
    for (uint32_t i = 0; i < new_rev.glyphs.count; i++) {
        for (uint32_t j = 0; j < new_rev.glyphs.count; j++) {
            total += kerning_lookup_cached(&new_rev.kerning, &alloc, new_rev.glyphs.entries[i].id, new_rev.glyphs.entries[j].id);
        }
    }
    glyph_file_free(&new_rev);
    kerning_table_free(&old_rev.kerning);
    hints_free(&old_rev.hints);
    row_alloc_destroy(&alloc);
    return total || new_rev.glyphs.count > 0;
}
