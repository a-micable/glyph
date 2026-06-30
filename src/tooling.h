#ifndef GLYPH_TOOLING_H
#define GLYPH_TOOLING_H

#include "atlas.h"

#include <stdint.h>
#include <stdio.h>

typedef struct {
    uint32_t glyphs;
    uint32_t duplicate_ids;
    uint32_t kerning_pairs;
    uint32_t invalid_kerning_pairs;
    uint32_t overlapping_glyphs;
    uint32_t rows;
    uint32_t invalid_rows;
    uint32_t atlas_pixels;
    uint32_t used_pixels;
    uint32_t hinted_glyphs;
} GlyphValidationStats;

int glyph_tool_validate_file(const char *glyph_path, FILE *report);
int glyph_tool_info_file(const char *glyph_path, FILE *out);
int glyph_tool_export_atlas(const char *glyph_path, const char *out_pgm);
int glyph_tool_make_sample_font(const char *out_dir, uint32_t first_id, uint32_t count);
int glyph_tool_write_manifest(const char *glyph_path, const char *manifest_path);
void glyph_tool_collect_stats(const GlyphFile *file, GlyphValidationStats *stats);

#endif
