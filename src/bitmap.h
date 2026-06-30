#ifndef GLYPH_BITMAP_H
#define GLYPH_BITMAP_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint8_t *pixels;
} GlyphBitmap;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} GlyphBitmapRect;

typedef enum {
    GLYPH_BITMAP_BLIT_COPY = 0,
    GLYPH_BITMAP_BLIT_MAX = 1,
    GLYPH_BITMAP_BLIT_ALPHA = 2
} GlyphBitmapBlitMode;

/* Initialize or zero destination bitmaps before passing them to functions that allocate output. */
void glyph_bitmap_init(GlyphBitmap *bitmap);
int glyph_bitmap_alloc(GlyphBitmap *bitmap, uint32_t width, uint32_t height);
void glyph_bitmap_free(GlyphBitmap *bitmap);

int glyph_bitmap_load_pgm(const char *path, GlyphBitmap *out);
int glyph_bitmap_save_pgm(const char *path, const GlyphBitmap *bitmap, int binary);

uint8_t glyph_bitmap_get(const GlyphBitmap *bitmap, uint32_t x, uint32_t y);
int glyph_bitmap_set(GlyphBitmap *bitmap, uint32_t x, uint32_t y, uint8_t value);
void glyph_bitmap_fill(GlyphBitmap *bitmap, uint8_t value);

int glyph_bitmap_copy(GlyphBitmap *dst, const GlyphBitmap *src);
int glyph_bitmap_crop(GlyphBitmap *dst, const GlyphBitmap *src, uint32_t x, uint32_t y, uint32_t width, uint32_t height);
int glyph_bitmap_trim_bounds(const GlyphBitmap *bitmap, uint8_t threshold, GlyphBitmapRect *bounds);

int glyph_bitmap_blit(GlyphBitmap *dst, const GlyphBitmap *src, int32_t dst_x, int32_t dst_y, GlyphBitmapBlitMode mode, uint8_t opacity);
void glyph_bitmap_threshold(GlyphBitmap *bitmap, uint8_t threshold, uint8_t low_value, uint8_t high_value);
void glyph_bitmap_invert(GlyphBitmap *bitmap);
void glyph_bitmap_normalize(GlyphBitmap *bitmap);

int glyph_bitmap_dilate(GlyphBitmap *dst, const GlyphBitmap *src, uint32_t radius);
int glyph_bitmap_erode(GlyphBitmap *dst, const GlyphBitmap *src, uint32_t radius);
int glyph_bitmap_box_blur(GlyphBitmap *dst, const GlyphBitmap *src, uint32_t radius);
int glyph_bitmap_resize_nearest(GlyphBitmap *dst, const GlyphBitmap *src, uint32_t width, uint32_t height);

int glyph_bitmap_rotate_cw(GlyphBitmap *dst, const GlyphBitmap *src);
int glyph_bitmap_rotate_ccw(GlyphBitmap *dst, const GlyphBitmap *src);
int glyph_bitmap_flip_horizontal(GlyphBitmap *bitmap);
int glyph_bitmap_flip_vertical(GlyphBitmap *bitmap);

void glyph_bitmap_draw_line(GlyphBitmap *bitmap, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t value);
void glyph_bitmap_draw_rect(GlyphBitmap *bitmap, int32_t x, int32_t y, uint32_t width, uint32_t height, uint8_t value);
void glyph_bitmap_fill_rect(GlyphBitmap *bitmap, int32_t x, int32_t y, uint32_t width, uint32_t height, uint8_t value);

uint32_t glyph_bitmap_checksum(const GlyphBitmap *bitmap);
uint64_t glyph_bitmap_hash64(const GlyphBitmap *bitmap);
int glyph_bitmap_compare(const GlyphBitmap *a, const GlyphBitmap *b, uint32_t *different_pixels, uint8_t *max_delta);

size_t glyph_bitmap_ascii_preview(const GlyphBitmap *bitmap, char *buffer, size_t buffer_size, uint32_t max_width, uint32_t max_height);

#endif
