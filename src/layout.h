#ifndef GLYPH_LAYOUT_H
#define GLYPH_LAYOUT_H

#include "atlas.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define GLYPH_LAYOUT_HIT_NONE ((size_t)-1)

typedef enum {
    GLYPH_LAYOUT_ALIGN_LEFT = 0,
    GLYPH_LAYOUT_ALIGN_CENTER = 1,
    GLYPH_LAYOUT_ALIGN_RIGHT = 2
} GlyphLayoutAlign;

typedef struct {
    int32_t wrap_width;
    int32_t line_height;
    int32_t tab_width;
    int32_t fallback_advance;
    GlyphLayoutAlign align;
} GlyphLayoutOptions;

typedef struct {
    int32_t min_x;
    int32_t min_y;
    int32_t max_x;
    int32_t max_y;
} GlyphLayoutBounds;

typedef struct {
    uint32_t glyph_id;
    size_t text_index;
    int32_t atlas_index;
    int32_t x;
    int32_t y;
    int32_t advance;
    int32_t kerning;
} GlyphLayoutGlyph;

typedef struct {
    size_t glyph_start;
    size_t glyph_count;
    size_t text_start;
    size_t text_end;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    GlyphLayoutBounds ink_bounds;
} GlyphLayoutLine;

typedef struct {
    GlyphLayoutGlyph *glyphs;
    size_t glyph_count;
    size_t glyph_capacity;
    GlyphLayoutLine *lines;
    size_t line_count;
    size_t line_capacity;
    int32_t width;
    int32_t height;
    GlyphLayoutBounds ink_bounds;
} GlyphLayout;

void glyph_layout_options_default(GlyphLayoutOptions *options);
void glyph_layout_init(GlyphLayout *layout);
void glyph_layout_free(GlyphLayout *layout);

int glyph_layout_shape(const GlyphFile *file, const char *text, const GlyphLayoutOptions *options, GlyphLayout *layout);
int glyph_layout_get_bounds(const GlyphLayout *layout, GlyphLayoutBounds *bounds);
size_t glyph_layout_hit_test(const GlyphLayout *layout, int32_t x, int32_t y);
size_t glyph_layout_hit_text_index(const GlyphLayout *layout, int32_t x, int32_t y);
int glyph_layout_write_report(FILE *fp, const GlyphLayout *layout);
int glyph_layout_render_to_buffer(const GlyphFile *file,
                                  const GlyphLayout *layout,
                                  uint8_t *pixels,
                                  uint32_t width,
                                  uint32_t height,
                                  int32_t origin_x,
                                  int32_t origin_y);

#endif
