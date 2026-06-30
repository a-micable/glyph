#ifndef GLYPH_RENDER_BACKEND_H
#define GLYPH_RENDER_BACKEND_H

#include "atlas.h"
#include "bitmap.h"
#include "layout.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GLYPH_RENDER_BLEND_COPY = 0,
    GLYPH_RENDER_BLEND_MAX = 1,
    GLYPH_RENDER_BLEND_ADD = 2,
    GLYPH_RENDER_BLEND_MULTIPLY = 3,
    GLYPH_RENDER_BLEND_SCREEN = 4,
    GLYPH_RENDER_BLEND_ALPHA = 5,
    GLYPH_RENDER_BLEND_ERASE = 6
} GlyphRenderBlendMode;

typedef enum {
    GLYPH_RENDER_GUIDE_NONE = 0,
    GLYPH_RENDER_GUIDE_ORIGIN = 1u << 0,
    GLYPH_RENDER_GUIDE_BASELINES = 1u << 1,
    GLYPH_RENDER_GUIDE_LINE_BOXES = 1u << 2,
    GLYPH_RENDER_GUIDE_INK_BOXES = 1u << 3,
    GLYPH_RENDER_GUIDE_TILE_BOUNDS = 1u << 4,
    GLYPH_RENDER_GUIDE_ALL = 0x1fu
} GlyphRenderGuideFlags;

typedef struct {
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
} GlyphRenderRect;

typedef struct {
    uint8_t value;
    uint8_t opacity;
} GlyphRenderPaint;

typedef struct {
    uint64_t pixels_tested;
    uint64_t pixels_written;
    uint64_t pixels_clipped;
    uint64_t spans_drawn;
    uint64_t blits;
    uint64_t fills;
    uint64_t lines;
    uint64_t borders;
    uint64_t glyphs_drawn;
    uint64_t glyphs_skipped;
    uint64_t layouts_rendered;
    uint64_t tiles_rendered;
} GlyphRenderStats;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint8_t *pixels;
    int owns_pixels;
    GlyphRenderRect clip;
    GlyphRenderStats stats;
} GlyphRenderSurface;

typedef struct {
    GlyphRenderPaint background;
    GlyphRenderPaint border;
    GlyphRenderPaint guide;
    GlyphRenderBlendMode blend;
    uint8_t glyph_opacity;
    uint32_t padding_x;
    uint32_t padding_y;
    uint8_t border_width;
    uint32_t guide_flags;
} GlyphRenderStyle;

typedef struct {
    uint32_t tile_width;
    uint32_t tile_height;
    uint32_t columns;
    uint32_t rows;
    uint32_t spacing_x;
    uint32_t spacing_y;
    uint32_t padding_x;
    uint32_t padding_y;
    GlyphRenderPaint background;
    GlyphRenderPaint border;
    GlyphRenderPaint guide;
    GlyphRenderBlendMode blend;
    uint8_t glyph_opacity;
    uint32_t guide_flags;
} GlyphRenderTileOptions;

typedef int (*GlyphRenderTileCallback)(GlyphRenderSurface *surface,
                                       const GlyphRenderRect *tile,
                                       uint32_t tile_index,
                                       void *user_data);

void glyph_render_paint(GlyphRenderPaint *paint, uint8_t value, uint8_t opacity);
void glyph_render_style_default(GlyphRenderStyle *style);
void glyph_render_tile_options_default(GlyphRenderTileOptions *options);

void glyph_render_rect_make(GlyphRenderRect *rect, int32_t x, int32_t y, uint32_t width, uint32_t height);
int glyph_render_rect_is_empty(const GlyphRenderRect *rect);
int glyph_render_rect_intersect(const GlyphRenderRect *a, const GlyphRenderRect *b, GlyphRenderRect *out);
int glyph_render_rect_contains(const GlyphRenderRect *rect, int32_t x, int32_t y);

void glyph_render_surface_init(GlyphRenderSurface *surface);
int glyph_render_surface_alloc(GlyphRenderSurface *surface, uint32_t width, uint32_t height);
int glyph_render_surface_wrap(GlyphRenderSurface *surface,
                              uint8_t *pixels,
                              uint32_t width,
                              uint32_t height,
                              uint32_t stride);
int glyph_render_surface_from_bitmap(GlyphRenderSurface *surface, GlyphBitmap *bitmap);
void glyph_render_surface_free(GlyphRenderSurface *surface);
int glyph_render_surface_is_valid(const GlyphRenderSurface *surface);
int glyph_render_surface_to_bitmap(const GlyphRenderSurface *surface, GlyphBitmap *bitmap);
int glyph_render_surface_copy_to_bitmap(const GlyphRenderSurface *surface, GlyphBitmap *bitmap);

void glyph_render_stats_clear(GlyphRenderSurface *surface);
void glyph_render_stats_add(GlyphRenderStats *dst, const GlyphRenderStats *src);

void glyph_render_clip_reset(GlyphRenderSurface *surface);
int glyph_render_clip_set(GlyphRenderSurface *surface, const GlyphRenderRect *clip);
int glyph_render_clip_intersect(GlyphRenderSurface *surface, const GlyphRenderRect *clip, GlyphRenderRect *previous);
GlyphRenderRect glyph_render_clip_get(const GlyphRenderSurface *surface);

uint8_t glyph_render_blend_pixel(uint8_t dst,
                                 uint8_t src,
                                 GlyphRenderBlendMode mode,
                                 uint8_t opacity);
int glyph_render_put_pixel(GlyphRenderSurface *surface,
                           int32_t x,
                           int32_t y,
                           uint8_t value,
                           GlyphRenderBlendMode mode,
                           uint8_t opacity);
uint8_t glyph_render_get_pixel(const GlyphRenderSurface *surface, int32_t x, int32_t y);

void glyph_render_clear(GlyphRenderSurface *surface, uint8_t value);
int glyph_render_fill(GlyphRenderSurface *surface,
                      GlyphRenderPaint paint,
                      GlyphRenderBlendMode mode);
int glyph_render_fill_rect(GlyphRenderSurface *surface,
                           const GlyphRenderRect *rect,
                           GlyphRenderPaint paint,
                           GlyphRenderBlendMode mode);
int glyph_render_stroke_rect(GlyphRenderSurface *surface,
                             const GlyphRenderRect *rect,
                             uint32_t thickness,
                             GlyphRenderPaint paint,
                             GlyphRenderBlendMode mode);
int glyph_render_draw_line(GlyphRenderSurface *surface,
                           int32_t x0,
                           int32_t y0,
                           int32_t x1,
                           int32_t y1,
                           GlyphRenderPaint paint,
                           GlyphRenderBlendMode mode);
int glyph_render_draw_hline(GlyphRenderSurface *surface,
                            int32_t x0,
                            int32_t x1,
                            int32_t y,
                            GlyphRenderPaint paint,
                            GlyphRenderBlendMode mode);
int glyph_render_draw_vline(GlyphRenderSurface *surface,
                            int32_t x,
                            int32_t y0,
                            int32_t y1,
                            GlyphRenderPaint paint,
                            GlyphRenderBlendMode mode);

int glyph_render_blit_bitmap(GlyphRenderSurface *surface,
                             const GlyphBitmap *bitmap,
                             int32_t dst_x,
                             int32_t dst_y,
                             GlyphRenderBlendMode mode,
                             uint8_t opacity);
int glyph_render_blit_surface(GlyphRenderSurface *dst,
                              const GlyphRenderSurface *src,
                              int32_t dst_x,
                              int32_t dst_y,
                              GlyphRenderBlendMode mode,
                              uint8_t opacity);
int glyph_render_blit_span(GlyphRenderSurface *surface,
                           int32_t dst_x,
                           int32_t dst_y,
                           const uint8_t *src,
                           uint32_t count,
                           GlyphRenderBlendMode mode,
                           uint8_t opacity);

int glyph_render_draw_glyph(GlyphRenderSurface *surface,
                            const GlyphFile *file,
                            uint32_t atlas_index,
                            int32_t x,
                            int32_t y,
                            GlyphRenderBlendMode mode,
                            uint8_t opacity);
int glyph_render_draw_layout_glyph(GlyphRenderSurface *surface,
                                   const GlyphFile *file,
                                   const GlyphLayoutGlyph *glyph,
                                   int32_t origin_x,
                                   int32_t origin_y,
                                   GlyphRenderBlendMode mode,
                                   uint8_t opacity);
int glyph_render_layout(GlyphRenderSurface *surface,
                        const GlyphFile *file,
                        const GlyphLayout *layout,
                        int32_t origin_x,
                        int32_t origin_y,
                        GlyphRenderBlendMode mode,
                        uint8_t opacity);
int glyph_render_text(GlyphRenderSurface *surface,
                      const GlyphFile *file,
                      const char *text,
                      const GlyphLayoutOptions *layout_options,
                      int32_t origin_x,
                      int32_t origin_y,
                      GlyphRenderBlendMode mode,
                      uint8_t opacity);
int glyph_render_layout_box(GlyphRenderSurface *surface,
                            const GlyphFile *file,
                            const GlyphLayout *layout,
                            const GlyphRenderRect *box,
                            const GlyphRenderStyle *style);
int glyph_render_text_box(GlyphRenderSurface *surface,
                          const GlyphFile *file,
                          const char *text,
                          const GlyphLayoutOptions *layout_options,
                          const GlyphRenderRect *box,
                          const GlyphRenderStyle *style);

int glyph_render_fill_background(GlyphRenderSurface *surface,
                                 const GlyphRenderRect *rect,
                                 GlyphRenderPaint paint);
int glyph_render_draw_border(GlyphRenderSurface *surface,
                             const GlyphRenderRect *rect,
                             uint32_t thickness,
                             GlyphRenderPaint paint);
int glyph_render_draw_layout_guides(GlyphRenderSurface *surface,
                                    const GlyphLayout *layout,
                                    int32_t origin_x,
                                    int32_t origin_y,
                                    uint32_t flags,
                                    GlyphRenderPaint paint);
int glyph_render_draw_tile_guides(GlyphRenderSurface *surface,
                                  const GlyphRenderTileOptions *options,
                                  const GlyphRenderRect *area);

int glyph_render_write_pgm(const char *path, const GlyphRenderSurface *surface, int binary);
int glyph_render_write_pgm_region(const char *path,
                                  const GlyphRenderSurface *surface,
                                  const GlyphRenderRect *region,
                                  int binary);
int glyph_render_write_pgm_stream(FILE *fp, const GlyphRenderSurface *surface, int binary);
int glyph_render_write_pgm_region_stream(FILE *fp,
                                         const GlyphRenderSurface *surface,
                                         const GlyphRenderRect *region,
                                         int binary);

int glyph_render_tiled(GlyphRenderSurface *surface,
                       const GlyphRenderTileOptions *options,
                       uint32_t tile_count,
                       GlyphRenderTileCallback callback,
                       void *user_data);
int glyph_render_tiled_layout(GlyphRenderSurface *surface,
                              const GlyphFile *file,
                              const GlyphLayout *layout,
                              const GlyphRenderTileOptions *options,
                              uint32_t tile_index);
int glyph_render_tiled_text(GlyphRenderSurface *surface,
                            const GlyphFile *file,
                            const char *text,
                            const GlyphLayoutOptions *layout_options,
                            const GlyphRenderTileOptions *options,
                            uint32_t tile_index);
int glyph_render_tile_rect(const GlyphRenderTileOptions *options,
                           uint32_t tile_index,
                           GlyphRenderRect *rect);
uint32_t glyph_render_tile_capacity(const GlyphRenderTileOptions *options);

#ifdef __cplusplus
}
#endif

#endif
