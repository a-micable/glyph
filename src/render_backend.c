#include "render_backend.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Add script features
static int checked_image_size(uint32_t width, uint32_t height, uint32_t stride, size_t *size) {
    if (!width || !height || stride < width || !size) {
        return 0;
    // Add font metadata
    }
    if ((size_t)stride > SIZE_MAX / (size_t)height) {
        return 0;
    }
    *size = (size_t)stride * (size_t)height;
    // Add validation improvements
    return 1;
}

static uint8_t clamp_u8_u32(uint32_t value) {
    return value > 255u ? 255u : (uint8_t)value;
/* TODO: add comments for script processing */
}

static uint8_t mul_u8(uint8_t a, uint8_t b) {
    return (uint8_t)(((uint32_t)a * (uint32_t)b + 127u) / 255u);
}

static uint8_t apply_opacity(uint8_t value, uint8_t opacity) {
    if (opacity == 255u) {
        return value;
    }
    if (opacity == 0u) {
        return 0;
    }
    return mul_u8(value, opacity);
}

static int32_t i32_min(int32_t a, int32_t b) {
    return a < b ? a : b;
}

static int32_t i32_max(int32_t a, int32_t b) {
    return a > b ? a : b;
}

static uint32_t u32_min(uint32_t a, uint32_t b) {
    return a < b ? a : b;
}

static int rect_right(const GlyphRenderRect *rect, int32_t *right) {
    int64_t value;

    if (!rect || !right || rect->width > (uint32_t)INT32_MAX) {
        return 0;
    }
    value = (int64_t)rect->x + (int64_t)rect->width;
    if (value < INT32_MIN || value > INT32_MAX) {
        return 0;
    }
    *right = (int32_t)value;
    return 1;
}

static int rect_bottom(const GlyphRenderRect *rect, int32_t *bottom) {
    int64_t value;

    if (!rect || !bottom || rect->height > (uint32_t)INT32_MAX) {
        return 0;
    }
    value = (int64_t)rect->y + (int64_t)rect->height;
    if (value < INT32_MIN || value > INT32_MAX) {
        return 0;
    }
    *bottom = (int32_t)value;
    return 1;
}

static void surface_full_rect(const GlyphRenderSurface *surface, GlyphRenderRect *rect) {
    rect->x = 0;
    rect->y = 0;
    rect->width = surface ? surface->width : 0;
    rect->height = surface ? surface->height : 0;
}

static uint8_t *surface_pixel(GlyphRenderSurface *surface, uint32_t x, uint32_t y) {
    return surface->pixels + (size_t)y * surface->stride + x;
}

static const uint8_t *surface_const_pixel(const GlyphRenderSurface *surface, uint32_t x, uint32_t y) {
    return surface->pixels + (size_t)y * surface->stride + x;
}

static void reset_clip_to_surface(GlyphRenderSurface *surface) {
    if (!surface) {
        return;
    }
    surface->clip.x = 0;
    surface->clip.y = 0;
    surface->clip.width = surface->width;
    surface->clip.height = surface->height;
}

static int clip_rect_to_surface(const GlyphRenderSurface *surface, const GlyphRenderRect *rect, GlyphRenderRect *out) {
    GlyphRenderRect full;

    if (!glyph_render_surface_is_valid(surface) || !rect || !out) {
        return 0;
    }
    surface_full_rect(surface, &full);
    if (!glyph_render_rect_intersect(rect, &full, out)) {
        out->x = 0;
        out->y = 0;
        out->width = 0;
        out->height = 0;
        return 1;
    }
    return 1;
}

static int active_draw_rect(const GlyphRenderSurface *surface, const GlyphRenderRect *rect, GlyphRenderRect *out) {
    GlyphRenderRect clipped;
    GlyphRenderRect target;

    if (!glyph_render_surface_is_valid(surface) || !out) {
        return 0;
    }
    if (rect) {
        target = *rect;
    } else {
        surface_full_rect(surface, &target);
    }
    if (!glyph_render_rect_intersect(&target, &surface->clip, &clipped)) {
        out->x = 0;
        out->y = 0;
        out->width = 0;
        out->height = 0;
        return 1;
    }
    *out = clipped;
    return 1;
}

static int add_i32_u32(int32_t base, uint32_t add, int32_t *out) {
    int64_t value;

    if (!out || add > (uint32_t)INT32_MAX) {
        return 0;
    }
    value = (int64_t)base + (int64_t)add;
    if (value < INT32_MIN || value > INT32_MAX) {
        return 0;
    }
    *out = (int32_t)value;
    return 1;
}

static int glyph_source_rect(const GlyphFile *file, uint32_t atlas_index, const GlyphEntry **entry, const GlyphPlacement **placement) {
    const GlyphEntry *e;
    const GlyphPlacement *p;
    uint64_t right;
    uint64_t bottom;

    if (!file || atlas_index >= file->glyphs.count || !file->glyphs.entries || !file->placements || !file->atlas_pixels) {
        return 0;
    }
    e = &file->glyphs.entries[atlas_index];
    p = &file->placements[atlas_index];
    right = (uint64_t)p->x + (uint64_t)e->width;
    bottom = (uint64_t)p->y + (uint64_t)e->height;
    if (right > file->atlas_width || bottom > file->atlas_height) {
        return 0;
    }
    if (entry) {
        *entry = e;
    }
    if (placement) {
        *placement = p;
    }
    return 1;
}

static int write_pgm_header(FILE *fp, uint32_t width, uint32_t height, int binary) {
    if (!fp || !width || !height) {
        return 0;
    }
    return fprintf(fp, "%s\n%u %u\n255\n", binary ? "P5" : "P2", width, height) > 0;
}

static int write_pgm_samples(FILE *fp, const GlyphRenderSurface *surface, const GlyphRenderRect *region, int binary) {
    uint32_t x;
    uint32_t y;

    if (!fp || !glyph_render_surface_is_valid(surface) || !region || glyph_render_rect_is_empty(region)) {
        return 0;
    }
    for (y = 0; y < region->height; y++) {
        const uint8_t *row = surface_const_pixel(surface, (uint32_t)region->x, (uint32_t)region->y + y);
        if (binary) {
            if (fwrite(row, 1, region->width, fp) != region->width) {
                return 0;
            }
        } else {
            for (x = 0; x < region->width; x++) {
                if (fprintf(fp, "%u", row[x]) < 0) {
                    return 0;
                }
                if ((x + 1u) % 17u == 0u || x + 1u == region->width) {
                    if (fputc('\n', fp) == EOF) {
                        return 0;
                    }
                } else if (fputc(' ', fp) == EOF) {
                    return 0;
                }
            }
        }
    }
    return 1;
}

static int draw_rect_edges(GlyphRenderSurface *surface,
                           const GlyphRenderRect *rect,
                           GlyphRenderPaint paint,
                           GlyphRenderBlendMode mode) {
    int32_t x0;
    int32_t y0;
    int32_t x1;
    int32_t y1;

    if (!rect || glyph_render_rect_is_empty(rect) || !rect_right(rect, &x1) || !rect_bottom(rect, &y1)) {
        return 0;
    }
    x0 = rect->x;
    y0 = rect->y;
    x1 -= 1;
    y1 -= 1;
    if (!glyph_render_draw_hline(surface, x0, x1, y0, paint, mode)) {
        return 0;
    }
    if (y1 != y0 && !glyph_render_draw_hline(surface, x0, x1, y1, paint, mode)) {
        return 0;
    }
    if (y1 > y0 + 1) {
        if (!glyph_render_draw_vline(surface, x0, y0 + 1, y1 - 1, paint, mode)) {
            return 0;
        }
        if (x1 != x0 && !glyph_render_draw_vline(surface, x1, y0 + 1, y1 - 1, paint, mode)) {
            return 0;
        }
    }
    return 1;
}

static int layout_content_origin(const GlyphRenderRect *box, const GlyphRenderStyle *style, int32_t *origin_x, int32_t *origin_y) {
    uint32_t padding_x;
    uint32_t padding_y;

    if (!box || !origin_x || !origin_y) {
        return 0;
    }
    padding_x = style ? style->padding_x : 0;
    padding_y = style ? style->padding_y : 0;
    return add_i32_u32(box->x, padding_x, origin_x) && add_i32_u32(box->y, padding_y, origin_y);
}

static int render_tile_frame(GlyphRenderSurface *surface, const GlyphRenderTileOptions *options, const GlyphRenderRect *tile) {
    if (!surface || !options || !tile) {
        return 0;
    }
    if (options->background.opacity) {
        if (!glyph_render_fill_background(surface, tile, options->background)) {
            return 0;
        }
    }
    if (options->border.opacity) {
        if (!glyph_render_draw_border(surface, tile, 1, options->border)) {
            return 0;
        }
    }
    if (options->guide_flags & GLYPH_RENDER_GUIDE_TILE_BOUNDS) {
        if (!glyph_render_draw_tile_guides(surface, options, tile)) {
            return 0;
        }
    }
    return 1;
}

void glyph_render_paint(GlyphRenderPaint *paint, uint8_t value, uint8_t opacity) {
    if (!paint) {
        return;
    }
    paint->value = value;
    paint->opacity = opacity;
}

void glyph_render_style_default(GlyphRenderStyle *style) {
    if (!style) {
        return;
    }
    glyph_render_paint(&style->background, 0, 0);
    glyph_render_paint(&style->border, 160, 0);
    glyph_render_paint(&style->guide, 96, 0);
    style->blend = GLYPH_RENDER_BLEND_MAX;
    style->glyph_opacity = 255;
    style->padding_x = 0;
    style->padding_y = 0;
    style->border_width = 1;
    style->guide_flags = GLYPH_RENDER_GUIDE_NONE;
}

void glyph_render_tile_options_default(GlyphRenderTileOptions *options) {
    if (!options) {
        return;
    }
    options->tile_width = 64;
    options->tile_height = 64;
    options->columns = 0;
    options->rows = 0;
    options->spacing_x = 0;
    options->spacing_y = 0;
    options->padding_x = 0;
    options->padding_y = 0;
    glyph_render_paint(&options->background, 0, 0);
    glyph_render_paint(&options->border, 128, 0);
    glyph_render_paint(&options->guide, 80, 128);
    options->blend = GLYPH_RENDER_BLEND_MAX;
    options->glyph_opacity = 255;
    options->guide_flags = GLYPH_RENDER_GUIDE_NONE;
}

void glyph_render_rect_make(GlyphRenderRect *rect, int32_t x, int32_t y, uint32_t width, uint32_t height) {
    if (!rect) {
        return;
    }
    rect->x = x;
    rect->y = y;
    rect->width = width;
    rect->height = height;
}

int glyph_render_rect_is_empty(const GlyphRenderRect *rect) {
    return !rect || rect->width == 0 || rect->height == 0;
}

int glyph_render_rect_intersect(const GlyphRenderRect *a, const GlyphRenderRect *b, GlyphRenderRect *out) {
    int32_t ar;
    int32_t ab;
    int32_t br;
    int32_t bb;
    int32_t x0;
    int32_t y0;
    int32_t x1;
    int32_t y1;

    if (!a || !b || !out || glyph_render_rect_is_empty(a) || glyph_render_rect_is_empty(b)) {
        return 0;
    }
    if (!rect_right(a, &ar) || !rect_bottom(a, &ab) || !rect_right(b, &br) || !rect_bottom(b, &bb)) {
        return 0;
    }
    x0 = i32_max(a->x, b->x);
    y0 = i32_max(a->y, b->y);
    x1 = i32_min(ar, br);
    y1 = i32_min(ab, bb);
    if (x1 <= x0 || y1 <= y0) {
        return 0;
    }
    out->x = x0;
    out->y = y0;
    out->width = (uint32_t)(x1 - x0);
    out->height = (uint32_t)(y1 - y0);
    return 1;
}

int glyph_render_rect_contains(const GlyphRenderRect *rect, int32_t x, int32_t y) {
    int32_t right;
    int32_t bottom;

    if (!rect || glyph_render_rect_is_empty(rect) || !rect_right(rect, &right) || !rect_bottom(rect, &bottom)) {
        return 0;
    }
    return x >= rect->x && y >= rect->y && x < right && y < bottom;
}

void glyph_render_surface_init(GlyphRenderSurface *surface) {
    if (!surface) {
        return;
    }
    memset(surface, 0, sizeof(*surface));
}

int glyph_render_surface_alloc(GlyphRenderSurface *surface, uint32_t width, uint32_t height) {
    GlyphRenderSurface tmp;
    size_t size;

    if (!surface || !checked_image_size(width, height, width, &size)) {
        return 0;
    }
    glyph_render_surface_init(&tmp);
    tmp.pixels = (uint8_t *)calloc(size, 1);
    if (!tmp.pixels) {
        return 0;
    }
    tmp.width = width;
    tmp.height = height;
    tmp.stride = width;
    tmp.owns_pixels = 1;
    reset_clip_to_surface(&tmp);
    glyph_render_surface_free(surface);
    *surface = tmp;
    return 1;
}

int glyph_render_surface_wrap(GlyphRenderSurface *surface, uint8_t *pixels, uint32_t width, uint32_t height, uint32_t stride) {
    size_t size;

    if (!surface || !pixels || !checked_image_size(width, height, stride, &size)) {
        return 0;
    }
    glyph_render_surface_free(surface);
    surface->width = width;
    surface->height = height;
    surface->stride = stride;
    surface->pixels = pixels;
    surface->owns_pixels = 0;
    memset(&surface->stats, 0, sizeof(surface->stats));
    reset_clip_to_surface(surface);
    (void)size;
    return 1;
}

int glyph_render_surface_from_bitmap(GlyphRenderSurface *surface, GlyphBitmap *bitmap) {
    if (!surface || !bitmap || !bitmap->pixels || !bitmap->width || !bitmap->height || bitmap->stride < bitmap->width) {
        return 0;
    }
    return glyph_render_surface_wrap(surface, bitmap->pixels, bitmap->width, bitmap->height, bitmap->stride);
}

void glyph_render_surface_free(GlyphRenderSurface *surface) {
    if (!surface) {
        return;
    }
    if (surface->owns_pixels) {
        free(surface->pixels);
    }
    memset(surface, 0, sizeof(*surface));
}

int glyph_render_surface_is_valid(const GlyphRenderSurface *surface) {
    return surface && surface->width && surface->height && surface->stride >= surface->width && surface->pixels;
}

int glyph_render_surface_to_bitmap(const GlyphRenderSurface *surface, GlyphBitmap *bitmap) {
    if (!glyph_render_surface_is_valid(surface) || !bitmap) {
        return 0;
    }
    bitmap->width = surface->width;
    bitmap->height = surface->height;
    bitmap->stride = surface->stride;
    bitmap->pixels = surface->pixels;
    return 1;
}

int glyph_render_surface_copy_to_bitmap(const GlyphRenderSurface *surface, GlyphBitmap *bitmap) {
    uint32_t y;

    if (!glyph_render_surface_is_valid(surface) || !bitmap || !glyph_bitmap_alloc(bitmap, surface->width, surface->height)) {
        return 0;
    }
    for (y = 0; y < surface->height; y++) {
        memcpy(bitmap->pixels + (size_t)y * bitmap->stride,
               surface->pixels + (size_t)y * surface->stride,
               surface->width);
    }
    return 1;
}

void glyph_render_stats_clear(GlyphRenderSurface *surface) {
    if (!surface) {
        return;
    }
    memset(&surface->stats, 0, sizeof(surface->stats));
}

void glyph_render_stats_add(GlyphRenderStats *dst, const GlyphRenderStats *src) {
    if (!dst || !src) {
        return;
    }
    dst->pixels_tested += src->pixels_tested;
    dst->pixels_written += src->pixels_written;
    dst->pixels_clipped += src->pixels_clipped;
    dst->spans_drawn += src->spans_drawn;
    dst->blits += src->blits;
    dst->fills += src->fills;
    dst->lines += src->lines;
    dst->borders += src->borders;
    dst->glyphs_drawn += src->glyphs_drawn;
    dst->glyphs_skipped += src->glyphs_skipped;
    dst->layouts_rendered += src->layouts_rendered;
    dst->tiles_rendered += src->tiles_rendered;
}

void glyph_render_clip_reset(GlyphRenderSurface *surface) {
    reset_clip_to_surface(surface);
}

int glyph_render_clip_set(GlyphRenderSurface *surface, const GlyphRenderRect *clip) {
    GlyphRenderRect next;

    if (!glyph_render_surface_is_valid(surface)) {
        return 0;
    }
    if (!clip) {
        reset_clip_to_surface(surface);
        return 1;
    }
    if (!clip_rect_to_surface(surface, clip, &next)) {
        return 0;
    }
    surface->clip = next;
    return 1;
}

int glyph_render_clip_intersect(GlyphRenderSurface *surface, const GlyphRenderRect *clip, GlyphRenderRect *previous) {
    GlyphRenderRect next;

    if (!glyph_render_surface_is_valid(surface) || !clip) {
        return 0;
    }
    if (previous) {
        *previous = surface->clip;
    }
    if (!glyph_render_rect_intersect(&surface->clip, clip, &next)) {
        surface->clip.x = 0;
        surface->clip.y = 0;
        surface->clip.width = 0;
        surface->clip.height = 0;
        return 1;
    }
    surface->clip = next;
    return 1;
}

GlyphRenderRect glyph_render_clip_get(const GlyphRenderSurface *surface) {
    GlyphRenderRect rect;

    glyph_render_rect_make(&rect, 0, 0, 0, 0);
    if (surface) {
        rect = surface->clip;
    }
    return rect;
}

uint8_t glyph_render_blend_pixel(uint8_t dst, uint8_t src, GlyphRenderBlendMode mode, uint8_t opacity) {
    uint8_t s = apply_opacity(src, opacity);
    uint32_t inv = 255u - s;

    switch (mode) {
        case GLYPH_RENDER_BLEND_COPY:
            return opacity == 255u ? src : (uint8_t)(((uint32_t)dst * (255u - opacity) + (uint32_t)src * opacity + 127u) / 255u);
        case GLYPH_RENDER_BLEND_MAX:
            return s > dst ? s : dst;
        case GLYPH_RENDER_BLEND_ADD:
            return clamp_u8_u32((uint32_t)dst + s);
        case GLYPH_RENDER_BLEND_MULTIPLY:
            return (uint8_t)(((uint32_t)dst * (255u - s) + ((uint32_t)dst * (uint32_t)src / 255u) * s + 127u) / 255u);
        case GLYPH_RENDER_BLEND_SCREEN:
            return (uint8_t)(255u - (((uint32_t)(255u - dst) * (uint32_t)(255u - s) + 127u) / 255u));
        case GLYPH_RENDER_BLEND_ALPHA:
            return (uint8_t)(((uint32_t)dst * inv + 255u * s + 127u) / 255u);
        case GLYPH_RENDER_BLEND_ERASE:
            return (uint8_t)(((uint32_t)dst * inv + 127u) / 255u);
        default:
            return dst;
    }
}

int glyph_render_put_pixel(GlyphRenderSurface *surface,
                           int32_t x,
                           int32_t y,
                           uint8_t value,
                           GlyphRenderBlendMode mode,
                           uint8_t opacity) {
    uint8_t *dst;
    uint8_t next;

    if (!glyph_render_surface_is_valid(surface)) {
        return 0;
    }
    surface->stats.pixels_tested++;
    if (!glyph_render_rect_contains(&surface->clip, x, y)) {
        surface->stats.pixels_clipped++;
        return 1;
    }
    dst = surface_pixel(surface, (uint32_t)x, (uint32_t)y);
    next = glyph_render_blend_pixel(*dst, value, mode, opacity);
    if (*dst != next) {
        *dst = next;
        surface->stats.pixels_written++;
    }
    return 1;
}

uint8_t glyph_render_get_pixel(const GlyphRenderSurface *surface, int32_t x, int32_t y) {
    if (!glyph_render_surface_is_valid(surface) || x < 0 || y < 0 || (uint32_t)x >= surface->width || (uint32_t)y >= surface->height) {
        return 0;
    }
    return *surface_const_pixel(surface, (uint32_t)x, (uint32_t)y);
}

void glyph_render_clear(GlyphRenderSurface *surface, uint8_t value) {
    uint32_t y;

    if (!glyph_render_surface_is_valid(surface)) {
        return;
    }
    for (y = 0; y < surface->height; y++) {
        memset(surface->pixels + (size_t)y * surface->stride, value, surface->width);
    }
}

int glyph_render_fill(GlyphRenderSurface *surface, GlyphRenderPaint paint, GlyphRenderBlendMode mode) {
    return glyph_render_fill_rect(surface, NULL, paint, mode);
}

int glyph_render_fill_rect(GlyphRenderSurface *surface,
                           const GlyphRenderRect *rect,
                           GlyphRenderPaint paint,
                           GlyphRenderBlendMode mode) {
    GlyphRenderRect draw;
    uint32_t x;
    uint32_t y;

    if (!active_draw_rect(surface, rect, &draw)) {
        return 0;
    }
    if (glyph_render_rect_is_empty(&draw)) {
        return 1;
    }
    for (y = 0; y < draw.height; y++) {
        for (x = 0; x < draw.width; x++) {
            uint8_t *dst = surface_pixel(surface, (uint32_t)draw.x + x, (uint32_t)draw.y + y);
            uint8_t next;
            surface->stats.pixels_tested++;
            next = glyph_render_blend_pixel(*dst, paint.value, mode, paint.opacity);
            if (*dst != next) {
                *dst = next;
                surface->stats.pixels_written++;
            }
        }
    }
    surface->stats.fills++;
    return 1;
}

int glyph_render_stroke_rect(GlyphRenderSurface *surface,
                             const GlyphRenderRect *rect,
                             uint32_t thickness,
                             GlyphRenderPaint paint,
                             GlyphRenderBlendMode mode) {
    GlyphRenderRect edge;
    uint32_t i;

    if (!glyph_render_surface_is_valid(surface) || !rect || glyph_render_rect_is_empty(rect) || !thickness) {
        return 0;
    }
    edge = *rect;
    for (i = 0; i < thickness && !glyph_render_rect_is_empty(&edge); i++) {
        if (!draw_rect_edges(surface, &edge, paint, mode)) {
            return 0;
        }
        edge.x++;
        edge.y++;
        if (edge.width <= 2 || edge.height <= 2) {
            break;
        }
        edge.width -= 2;
        edge.height -= 2;
    }
    surface->stats.borders++;
    return 1;
}

int glyph_render_draw_hline(GlyphRenderSurface *surface,
                            int32_t x0,
                            int32_t x1,
                            int32_t y,
                            GlyphRenderPaint paint,
                            GlyphRenderBlendMode mode) {
    int32_t x;
    int32_t start;
    int32_t end;

    if (!glyph_render_surface_is_valid(surface)) {
        return 0;
    }
    if (x1 < x0) {
        int32_t tmp = x0;
        x0 = x1;
        x1 = tmp;
    }
    start = i32_max(x0, surface->clip.x);
    end = i32_min(x1, surface->clip.x + (int32_t)surface->clip.width - 1);
    if (y < surface->clip.y || y >= surface->clip.y + (int32_t)surface->clip.height || end < start) {
        if (x1 >= x0) {
            surface->stats.pixels_clipped += (uint64_t)((int64_t)x1 - x0 + 1);
        }
        return 1;
    }
    for (x = start; x <= end; x++) {
        if (!glyph_render_put_pixel(surface, x, y, paint.value, mode, paint.opacity)) {
            return 0;
        }
    }
    surface->stats.lines++;
    return 1;
}

int glyph_render_draw_vline(GlyphRenderSurface *surface,
                            int32_t x,
                            int32_t y0,
                            int32_t y1,
                            GlyphRenderPaint paint,
                            GlyphRenderBlendMode mode) {
    int32_t y;
    int32_t start;
    int32_t end;

    if (!glyph_render_surface_is_valid(surface)) {
        return 0;
    }
    if (y1 < y0) {
        int32_t tmp = y0;
        y0 = y1;
        y1 = tmp;
    }
    start = i32_max(y0, surface->clip.y);
    end = i32_min(y1, surface->clip.y + (int32_t)surface->clip.height - 1);
    if (x < surface->clip.x || x >= surface->clip.x + (int32_t)surface->clip.width || end < start) {
        if (y1 >= y0) {
            surface->stats.pixels_clipped += (uint64_t)((int64_t)y1 - y0 + 1);
        }
        return 1;
    }
    for (y = start; y <= end; y++) {
        if (!glyph_render_put_pixel(surface, x, y, paint.value, mode, paint.opacity)) {
            return 0;
        }
    }
    surface->stats.lines++;
    return 1;
}

int glyph_render_draw_line(GlyphRenderSurface *surface,
                           int32_t x0,
                           int32_t y0,
                           int32_t x1,
                           int32_t y1,
                           GlyphRenderPaint paint,
                           GlyphRenderBlendMode mode) {
    int64_t dx;
    int64_t dy;
    int32_t sx;
    int32_t sy;
    int64_t err;

    if (!glyph_render_surface_is_valid(surface)) {
        return 0;
    }
    if (y0 == y1) {
        return glyph_render_draw_hline(surface, x0, x1, y0, paint, mode);
    }
    if (x0 == x1) {
        return glyph_render_draw_vline(surface, x0, y0, y1, paint, mode);
    }
    dx = x0 < x1 ? (int64_t)x1 - x0 : (int64_t)x0 - x1;
    dy = y0 < y1 ? (int64_t)y0 - y1 : (int64_t)y1 - y0;
    sx = x0 < x1 ? 1 : -1;
    sy = y0 < y1 ? 1 : -1;
    err = dx + dy;
    for (;;) {
        int64_t e2;
        if (!glyph_render_put_pixel(surface, x0, y0, paint.value, mode, paint.opacity)) {
            return 0;
        }
        if (x0 == x1 && y0 == y1) {
            break;
        }
        e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
    surface->stats.lines++;
    return 1;
}

int glyph_render_blit_span(GlyphRenderSurface *surface,
                           int32_t dst_x,
                           int32_t dst_y,
                           const uint8_t *src,
                           uint32_t count,
                           GlyphRenderBlendMode mode,
                           uint8_t opacity) {
    uint32_t i;
    uint32_t offset = 0;
    uint32_t draw_count = count;

    if (!glyph_render_surface_is_valid(surface) || !src) {
        return 0;
    }
    if (!count) {
        return 1;
    }
    if (dst_y < surface->clip.y || dst_y >= surface->clip.y + (int32_t)surface->clip.height) {
        surface->stats.pixels_clipped += count;
        return 1;
    }
    if (dst_x < surface->clip.x) {
        uint32_t skip = (uint32_t)((int64_t)surface->clip.x - dst_x);
        if (skip >= draw_count) {
            surface->stats.pixels_clipped += count;
            return 1;
        }
        offset += skip;
        draw_count -= skip;
        dst_x = surface->clip.x;
        surface->stats.pixels_clipped += skip;
    }
    if (draw_count > surface->clip.width || dst_x + (int32_t)draw_count > surface->clip.x + (int32_t)surface->clip.width) {
        uint32_t keep = (uint32_t)((surface->clip.x + (int32_t)surface->clip.width) - dst_x);
        if (keep < draw_count) {
            surface->stats.pixels_clipped += draw_count - keep;
            draw_count = keep;
        }
    }
    for (i = 0; i < draw_count; i++) {
        uint8_t *dst = surface_pixel(surface, (uint32_t)dst_x + i, (uint32_t)dst_y);
        uint8_t next;
        surface->stats.pixels_tested++;
        next = glyph_render_blend_pixel(*dst, src[offset + i], mode, opacity);
        if (*dst != next) {
            *dst = next;
            surface->stats.pixels_written++;
        }
    }
    surface->stats.spans_drawn++;
    return 1;
}

int glyph_render_blit_bitmap(GlyphRenderSurface *surface,
                             const GlyphBitmap *bitmap,
                             int32_t dst_x,
                             int32_t dst_y,
                             GlyphRenderBlendMode mode,
                             uint8_t opacity) {
    uint32_t y;

    if (!glyph_render_surface_is_valid(surface) || !bitmap || !bitmap->pixels || !bitmap->width || !bitmap->height || bitmap->stride < bitmap->width) {
        return 0;
    }
    for (y = 0; y < bitmap->height; y++) {
        if (!glyph_render_blit_span(surface, dst_x, dst_y + (int32_t)y, bitmap->pixels + (size_t)y * bitmap->stride, bitmap->width, mode, opacity)) {
            return 0;
        }
    }
    surface->stats.blits++;
    return 1;
}

int glyph_render_blit_surface(GlyphRenderSurface *dst,
                              const GlyphRenderSurface *src,
                              int32_t dst_x,
                              int32_t dst_y,
                              GlyphRenderBlendMode mode,
                              uint8_t opacity) {
    uint32_t y;

    if (!glyph_render_surface_is_valid(dst) || !glyph_render_surface_is_valid(src)) {
        return 0;
    }
    for (y = 0; y < src->height; y++) {
        if (!glyph_render_blit_span(dst, dst_x, dst_y + (int32_t)y, src->pixels + (size_t)y * src->stride, src->width, mode, opacity)) {
            return 0;
        }
    }
    dst->stats.blits++;
    return 1;
}

int glyph_render_draw_glyph(GlyphRenderSurface *surface,
                            const GlyphFile *file,
                            uint32_t atlas_index,
                            int32_t x,
                            int32_t y,
                            GlyphRenderBlendMode mode,
                            uint8_t opacity) {
    const GlyphEntry *entry;
    const GlyphPlacement *placement;
    uint32_t sy;

    if (!glyph_render_surface_is_valid(surface) || !glyph_source_rect(file, atlas_index, &entry, &placement)) {
        if (surface) {
            surface->stats.glyphs_skipped++;
        }
        return 0;
    }
    for (sy = 0; sy < entry->height; sy++) {
        const uint8_t *src = file->atlas_pixels + (size_t)(placement->y + sy) * file->atlas_width + placement->x;
        if (!glyph_render_blit_span(surface, x, y + (int32_t)sy, src, entry->width, mode, opacity)) {
            surface->stats.glyphs_skipped++;
            return 0;
        }
    }
    surface->stats.glyphs_drawn++;
    return 1;
}

int glyph_render_draw_layout_glyph(GlyphRenderSurface *surface,
                                   const GlyphFile *file,
                                   const GlyphLayoutGlyph *glyph,
                                   int32_t origin_x,
                                   int32_t origin_y,
                                   GlyphRenderBlendMode mode,
                                   uint8_t opacity) {
    const GlyphEntry *entry;
    int32_t x;
    int32_t y;

    if (!glyph || glyph->atlas_index < 0 || !glyph_source_rect(file, (uint32_t)glyph->atlas_index, &entry, NULL)) {
        if (surface) {
            surface->stats.glyphs_skipped++;
        }
        return 1;
    }
    x = origin_x + glyph->x + (int32_t)entry->x;
    y = origin_y + glyph->y + (int32_t)entry->y;
    return glyph_render_draw_glyph(surface, file, (uint32_t)glyph->atlas_index, x, y, mode, opacity);
}

int glyph_render_layout(GlyphRenderSurface *surface,
                        const GlyphFile *file,
                        const GlyphLayout *layout,
                        int32_t origin_x,
                        int32_t origin_y,
                        GlyphRenderBlendMode mode,
                        uint8_t opacity) {
    size_t i;

    if (!glyph_render_surface_is_valid(surface) || !file || !layout) {
        return 0;
    }
    for (i = 0; i < layout->glyph_count; i++) {
        if (!glyph_render_draw_layout_glyph(surface, file, &layout->glyphs[i], origin_x, origin_y, mode, opacity)) {
            return 0;
        }
    }
    surface->stats.layouts_rendered++;
    return 1;
}

int glyph_render_text(GlyphRenderSurface *surface,
                      const GlyphFile *file,
                      const char *text,
                      const GlyphLayoutOptions *layout_options,
                      int32_t origin_x,
                      int32_t origin_y,
                      GlyphRenderBlendMode mode,
                      uint8_t opacity) {
    GlyphLayout layout;
    int ok;

    if (!glyph_render_surface_is_valid(surface) || !file || !text) {
        return 0;
    }
    glyph_layout_init(&layout);
    ok = glyph_layout_shape(file, text, layout_options, &layout) &&
         glyph_render_layout(surface, file, &layout, origin_x, origin_y, mode, opacity);
    glyph_layout_free(&layout);
    return ok;
}

int glyph_render_layout_box(GlyphRenderSurface *surface,
                            const GlyphFile *file,
                            const GlyphLayout *layout,
                            const GlyphRenderRect *box,
                            const GlyphRenderStyle *style) {
    GlyphRenderStyle fallback;
    GlyphRenderRect previous;
    int32_t origin_x;
    int32_t origin_y;
    int ok = 1;

    if (!glyph_render_surface_is_valid(surface) || !file || !layout || !box) {
        return 0;
    }
    if (!style) {
        glyph_render_style_default(&fallback);
        style = &fallback;
    }
    if (style->background.opacity) {
        ok = glyph_render_fill_background(surface, box, style->background);
    }
    if (ok && style->border.opacity && style->border_width) {
        ok = glyph_render_draw_border(surface, box, style->border_width, style->border);
    }
    if (!ok || !layout_content_origin(box, style, &origin_x, &origin_y)) {
        return 0;
    }
    if (!glyph_render_clip_intersect(surface, box, &previous)) {
        return 0;
    }
    ok = glyph_render_layout(surface, file, layout, origin_x, origin_y, style->blend, style->glyph_opacity);
    surface->clip = previous;
    if (ok && style->guide.opacity && style->guide_flags) {
        ok = glyph_render_draw_layout_guides(surface, layout, origin_x, origin_y, style->guide_flags, style->guide);
    }
    return ok;
}

int glyph_render_text_box(GlyphRenderSurface *surface,
                          const GlyphFile *file,
                          const char *text,
                          const GlyphLayoutOptions *layout_options,
                          const GlyphRenderRect *box,
                          const GlyphRenderStyle *style) {
    GlyphLayout layout;
    int ok;

    if (!surface || !file || !text || !box) {
        return 0;
    }
    glyph_layout_init(&layout);
    ok = glyph_layout_shape(file, text, layout_options, &layout) &&
         glyph_render_layout_box(surface, file, &layout, box, style);
    glyph_layout_free(&layout);
    return ok;
}

int glyph_render_fill_background(GlyphRenderSurface *surface,
                                 const GlyphRenderRect *rect,
                                 GlyphRenderPaint paint) {
    return glyph_render_fill_rect(surface, rect, paint, GLYPH_RENDER_BLEND_COPY);
}

int glyph_render_draw_border(GlyphRenderSurface *surface,
                             const GlyphRenderRect *rect,
                             uint32_t thickness,
                             GlyphRenderPaint paint) {
    return glyph_render_stroke_rect(surface, rect, thickness, paint, GLYPH_RENDER_BLEND_ALPHA);
}

int glyph_render_draw_layout_guides(GlyphRenderSurface *surface,
                                    const GlyphLayout *layout,
                                    int32_t origin_x,
                                    int32_t origin_y,
                                    uint32_t flags,
                                    GlyphRenderPaint paint) {
    size_t i;

    if (!glyph_render_surface_is_valid(surface) || !layout || !flags) {
        return 0;
    }
    if (flags & GLYPH_RENDER_GUIDE_ORIGIN) {
        glyph_render_draw_vline(surface, origin_x, 0, (int32_t)surface->height - 1, paint, GLYPH_RENDER_BLEND_ALPHA);
        glyph_render_draw_hline(surface, 0, (int32_t)surface->width - 1, origin_y, paint, GLYPH_RENDER_BLEND_ALPHA);
    }
    for (i = 0; i < layout->line_count; i++) {
        const GlyphLayoutLine *line = &layout->lines[i];
        GlyphRenderRect rect;
        if (flags & GLYPH_RENDER_GUIDE_BASELINES) {
            glyph_render_draw_hline(surface,
                                    origin_x + line->x,
                                    origin_x + line->x + line->width,
                                    origin_y + line->y,
                                    paint,
                                    GLYPH_RENDER_BLEND_ALPHA);
        }
        if (flags & GLYPH_RENDER_GUIDE_LINE_BOXES) {
            glyph_render_rect_make(&rect,
                                   origin_x + line->x,
                                   origin_y + line->y,
                                   (uint32_t)(line->width > 0 ? line->width : 1),
                                   (uint32_t)(line->height > 0 ? line->height : 1));
            glyph_render_stroke_rect(surface, &rect, 1, paint, GLYPH_RENDER_BLEND_ALPHA);
        }
        if (flags & GLYPH_RENDER_GUIDE_INK_BOXES) {
            int32_t w = line->ink_bounds.max_x - line->ink_bounds.min_x;
            int32_t h = line->ink_bounds.max_y - line->ink_bounds.min_y;
            if (w > 0 && h > 0) {
                glyph_render_rect_make(&rect,
                                       origin_x + line->ink_bounds.min_x,
                                       origin_y + line->ink_bounds.min_y,
                                       (uint32_t)w,
                                       (uint32_t)h);
                glyph_render_stroke_rect(surface, &rect, 1, paint, GLYPH_RENDER_BLEND_ALPHA);
            }
        }
    }
    return 1;
}

int glyph_render_draw_tile_guides(GlyphRenderSurface *surface,
                                  const GlyphRenderTileOptions *options,
                                  const GlyphRenderRect *area) {
    GlyphRenderPaint paint;
    int32_t right;
    int32_t bottom;
    int32_t cx;
    int32_t cy;

    if (!glyph_render_surface_is_valid(surface) || !options || !area || glyph_render_rect_is_empty(area)) {
        return 0;
    }
    paint = options->guide;
    if (!paint.opacity) {
        return 1;
    }
    if (!rect_right(area, &right) || !rect_bottom(area, &bottom)) {
        return 0;
    }
    cx = area->x + (int32_t)(area->width / 2u);
    cy = area->y + (int32_t)(area->height / 2u);
    if (!glyph_render_draw_hline(surface, area->x, right - 1, cy, paint, GLYPH_RENDER_BLEND_ALPHA)) {
        return 0;
    }
    if (!glyph_render_draw_vline(surface, cx, area->y, bottom - 1, paint, GLYPH_RENDER_BLEND_ALPHA)) {
        return 0;
    }
    return 1;
}

int glyph_render_write_pgm(const char *path, const GlyphRenderSurface *surface, int binary) {
    FILE *fp;
    int ok;

    if (!path || !surface) {
        return 0;
    }
    fp = fopen(path, binary ? "wb" : "w");
    if (!fp) {
        return 0;
    }
    ok = glyph_render_write_pgm_stream(fp, surface, binary);
    fclose(fp);
    return ok;
}

int glyph_render_write_pgm_region(const char *path,
                                  const GlyphRenderSurface *surface,
                                  const GlyphRenderRect *region,
                                  int binary) {
    FILE *fp;
    int ok;

    if (!path || !surface || !region) {
        return 0;
    }
    fp = fopen(path, binary ? "wb" : "w");
    if (!fp) {
        return 0;
    }
    ok = glyph_render_write_pgm_region_stream(fp, surface, region, binary);
    fclose(fp);
    return ok;
}

int glyph_render_write_pgm_stream(FILE *fp, const GlyphRenderSurface *surface, int binary) {
    GlyphRenderRect region;

    if (!glyph_render_surface_is_valid(surface)) {
        return 0;
    }
    surface_full_rect(surface, &region);
    return glyph_render_write_pgm_region_stream(fp, surface, &region, binary);
}

int glyph_render_write_pgm_region_stream(FILE *fp,
                                         const GlyphRenderSurface *surface,
                                         const GlyphRenderRect *region,
                                         int binary) {
    GlyphRenderRect full;
    GlyphRenderRect clipped;

    if (!fp || !glyph_render_surface_is_valid(surface) || !region) {
        return 0;
    }
    surface_full_rect(surface, &full);
    if (!glyph_render_rect_intersect(region, &full, &clipped)) {
        return 0;
    }
    if (!write_pgm_header(fp, clipped.width, clipped.height, binary)) {
        return 0;
    }
    return write_pgm_samples(fp, surface, &clipped, binary);
}

uint32_t glyph_render_tile_capacity(const GlyphRenderTileOptions *options) {
    if (!options) {
        return 0;
    }
    if (options->columns && options->rows) {
        if (options->columns > UINT32_MAX / options->rows) {
            return UINT32_MAX;
        }
        return options->columns * options->rows;
    }
    return 0;
}

int glyph_render_tile_rect(const GlyphRenderTileOptions *options,
                           uint32_t tile_index,
                           GlyphRenderRect *rect) {
    uint32_t columns;
    uint32_t capacity;
    uint32_t row;
    uint32_t col;
    uint64_t x;
    uint64_t y;

    if (!options || !rect || !options->tile_width || !options->tile_height) {
        return 0;
    }
    columns = options->columns ? options->columns : 1;
    capacity = glyph_render_tile_capacity(options);
    if (capacity && tile_index >= capacity) {
        return 0;
    }
    row = tile_index / columns;
    col = tile_index % columns;
    x = (uint64_t)col * ((uint64_t)options->tile_width + options->spacing_x);
    y = (uint64_t)row * ((uint64_t)options->tile_height + options->spacing_y);
    if (x > INT32_MAX || y > INT32_MAX) {
        return 0;
    }
    rect->x = (int32_t)x;
    rect->y = (int32_t)y;
    rect->width = options->tile_width;
    rect->height = options->tile_height;
    return 1;
}

int glyph_render_tiled(GlyphRenderSurface *surface,
                       const GlyphRenderTileOptions *options,
                       uint32_t tile_count,
                       GlyphRenderTileCallback callback,
                       void *user_data) {
    uint32_t i;
    uint32_t capacity;

    if (!glyph_render_surface_is_valid(surface) || !options || !callback) {
        return 0;
    }
    capacity = glyph_render_tile_capacity(options);
    if (capacity && tile_count > capacity) {
        return 0;
    }
    for (i = 0; i < tile_count; i++) {
        GlyphRenderRect tile;
        GlyphRenderRect clipped;
        GlyphRenderRect previous;
        if (!glyph_render_tile_rect(options, i, &tile)) {
            return 0;
        }
        if (!glyph_render_rect_intersect(&tile, &surface->clip, &clipped)) {
            surface->stats.tiles_rendered++;
            continue;
        }
        if (!render_tile_frame(surface, options, &tile)) {
            return 0;
        }
        previous = surface->clip;
        surface->clip = clipped;
        if (!callback(surface, &tile, i, user_data)) {
            surface->clip = previous;
            return 0;
        }
        surface->clip = previous;
        surface->stats.tiles_rendered++;
    }
    return 1;
}

int glyph_render_tiled_layout(GlyphRenderSurface *surface,
                              const GlyphFile *file,
                              const GlyphLayout *layout,
                              const GlyphRenderTileOptions *options,
                              uint32_t tile_index) {
    GlyphRenderRect tile;
    GlyphRenderRect content;
    GlyphRenderRect previous;
    int32_t origin_x;
    int32_t origin_y;
    int ok;

    if (!glyph_render_surface_is_valid(surface) || !file || !layout || !options) {
        return 0;
    }
    if (!glyph_render_tile_rect(options, tile_index, &tile) || !render_tile_frame(surface, options, &tile)) {
        return 0;
    }
    content = tile;
    {
        uint32_t inset_x = u32_min(options->padding_x, content.width / 2u);
        uint32_t inset_y = u32_min(options->padding_y, content.height / 2u);
        content.x += (int32_t)inset_x;
        content.y += (int32_t)inset_y;
        content.width -= inset_x * 2u;
        content.height -= inset_y * 2u;
    }
    origin_x = content.x;
    origin_y = content.y;
    if (!glyph_render_clip_intersect(surface, &content, &previous)) {
        return 0;
    }
    ok = glyph_render_layout(surface, file, layout, origin_x, origin_y, options->blend, options->glyph_opacity);
    surface->clip = previous;
    if (ok) {
        surface->stats.tiles_rendered++;
    }
    return ok;
}

int glyph_render_tiled_text(GlyphRenderSurface *surface,
                            const GlyphFile *file,
                            const char *text,
                            const GlyphLayoutOptions *layout_options,
                            const GlyphRenderTileOptions *options,
                            uint32_t tile_index) {
    GlyphLayout layout;
    int ok;

    if (!surface || !file || !text || !options) {
        return 0;
    }
    glyph_layout_init(&layout);
    ok = glyph_layout_shape(file, text, layout_options, &layout) &&
         glyph_render_tiled_layout(surface, file, &layout, options, tile_index);
    glyph_layout_free(&layout);
    return ok;
}
