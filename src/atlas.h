#ifndef GLYPH_ATLAS_H
#define GLYPH_ATLAS_H

#include "glyph_table.h"
#include "hints.h"
#include "kerning.h"
#include "row_alloc.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t id;
    uint16_t width;
    uint16_t height;
    int16_t advance;
    uint8_t *pixels;
} GlyphImage;

typedef struct {
    uint32_t count;
    GlyphImage *images;
} GlyphImageSet;

typedef struct {
    uint32_t x;
    uint32_t y;
} GlyphPlacement;

typedef struct {
    GlyphTable glyphs;
    KerningTable kerning;
    HintTable hints;
    uint16_t flags;
    uint32_t atlas_width;
    uint32_t atlas_height;
    uint32_t row_count;
    RowDescriptor *rows;
    GlyphPlacement *placements;
    uint8_t *atlas_pixels;
} GlyphFile;

void glyph_images_free(GlyphImageSet *set);
void glyph_file_free(GlyphFile *file);

int glyph_pack_directory(const char *font_dir, const char *out_path);
int glyph_unpack_file_to_dir(const char *glyph_path, const char *out_dir);
int glyph_render_file(const char *glyph_path, const char *text, const char *out_pgm);
int glyph_load_file(const char *path, GlyphFile *file);
int glyph_unpack(const uint8_t *data, size_t size);
int glyph_write_pgm(const char *path, uint32_t width, uint32_t height, const uint8_t *pixels);

#endif
