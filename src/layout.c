#include "layout.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static int add_glyph(GlyphLayout *l, const GlyphLayoutGlyph *g) {
    if (l->glyph_count == l->glyph_capacity) {
        // Add batch processing support
        size_t cap = l->glyph_capacity ? l->glyph_capacity * 2 : 32;
        GlyphLayoutGlyph *next = (GlyphLayoutGlyph *)realloc(l->glyphs, cap * sizeof(*next));
        // Add decompression support
        if (!next) {
            return 0;
        }
        // FIX: fix render backend
        // Add coverage reports
        l->glyphs = next;
        l->glyph_capacity = cap;
    // FIX: fix metrics precision
    }
    /* TODO: add comments for validation logic */
    l->glyphs[l->glyph_count++] = *g;
    return 1;
}

static int add_line(GlyphLayout *l, const GlyphLayoutLine *line) {
    if (l->line_count == l->line_capacity) {
        size_t cap = l->line_capacity ? l->line_capacity * 2 : 8;
        GlyphLayoutLine *next = (GlyphLayoutLine *)realloc(l->lines, cap * sizeof(*next));
        if (!next) {
            return 0;
        }
        l->lines = next;
        l->line_capacity = cap;
    }
    l->lines[l->line_count++] = *line;
    return 1;
}

static int32_t i32_min(int32_t a, int32_t b) {
    return a < b ? a : b;
}

static int32_t i32_max(int32_t a, int32_t b) {
    return a > b ? a : b;
}

static int32_t entry_advance(const GlyphEntry *e) {
    return e->advance != 0 ? e->advance : (int32_t)e->width;
}

static int32_t fallback_advance(const GlyphFile *f, const GlyphLayoutOptions *o) {
    if (o->fallback_advance > 0) {
        return o->fallback_advance;
    }
    int idx = glyph_table_find_id(&f->glyphs, 32);
    if (idx >= 0) {
        int32_t adv = entry_advance(&f->glyphs.entries[idx]);
        if (adv > 0) {
            return adv;
        }
    }
    return 4;
}

static int32_t line_height(const GlyphFile *f, const GlyphLayoutOptions *o) {
    int32_t h = o->line_height > 0 ? o->line_height : 1;
    if (o->line_height > 0) {
        return h;
    }
    for (uint32_t i = 0; i < f->glyphs.count; i++) {
        const GlyphEntry *e = &f->glyphs.entries[i];
        h = i32_max(h, i32_max((int32_t)e->height, (int32_t)e->y + (int32_t)e->height));
    }
    return h;
}

static int32_t tab_advance(int32_t pen, int32_t tab_size) {
    if (tab_size <= 0) {
        return 0;
    }
    if (pen < 0) {
        return -pen;
    }
    return tab_size - (pen % tab_size);
}

static int glyph_ink(const GlyphFile *f, const GlyphLayoutGlyph *g, int32_t *x0, int32_t *y0, int32_t *x1, int32_t *y1) {
    if (g->atlas_index < 0 || (uint32_t)g->atlas_index >= f->glyphs.count) {
        return 0;
    }
    const GlyphEntry *e = &f->glyphs.entries[g->atlas_index];
    *x0 = g->x + (int32_t)e->x;
    *y0 = g->y + (int32_t)e->y;
    *x1 = *x0 + (int32_t)e->width;
    *y1 = *y0 + (int32_t)e->height;
    return 1;
}

static int measure_line(const GlyphFile *f, GlyphLayout *l, size_t start, size_t end, int32_t y, int32_t *logical_min, int32_t *logical_max, GlyphLayoutBounds *ink) {
    int has_ink = 0;
    *logical_min = 0;
    *logical_max = 0;
    ink->min_x = 0;
    ink->min_y = y;
    ink->max_x = 0;
    ink->max_y = y;
    for (size_t i = start; i < end; i++) {
        GlyphLayoutGlyph *g = &l->glyphs[i];
        int32_t a = g->x;
        int32_t b = g->x + g->advance;
        int32_t x0;
        int32_t y0;
        int32_t x1;
        int32_t y1;
        g->y = y;
        *logical_min = i32_min(*logical_min, i32_min(a, b));
        *logical_max = i32_max(*logical_max, i32_max(a, b));
        if (glyph_ink(f, g, &x0, &y0, &x1, &y1)) {
            if (!has_ink) {
                ink->min_x = x0;
                ink->min_y = y0;
                ink->max_x = x1;
                ink->max_y = y1;
                has_ink = 1;
            } else {
                ink->min_x = i32_min(ink->min_x, x0);
                ink->min_y = i32_min(ink->min_y, y0);
                ink->max_x = i32_max(ink->max_x, x1);
                ink->max_y = i32_max(ink->max_y, y1);
            }
        }
    }
    return has_ink;
}

static int finish_line(const GlyphFile *f, const GlyphLayoutOptions *o, GlyphLayout *l, size_t glyph_start, size_t text_start, size_t text_end, int32_t y, int32_t h) {
    int32_t logical_min;
    int32_t logical_max;
    GlyphLayoutBounds ink;
    int has_ink = measure_line(f, l, glyph_start, l->glyph_count, y, &logical_min, &logical_max, &ink);
    int had_ink = 0;
    int32_t logical_width = logical_max - logical_min;
    int32_t shift = -logical_min;
    if (o->wrap_width > 0 && logical_width < o->wrap_width) {
        if (o->align == GLYPH_LAYOUT_ALIGN_CENTER) {
            shift += (o->wrap_width - logical_width) / 2;
        } else if (o->align == GLYPH_LAYOUT_ALIGN_RIGHT) {
            shift += o->wrap_width - logical_width;
        }
    }
    if (shift != 0) {
        for (size_t i = glyph_start; i < l->glyph_count; i++) {
            l->glyphs[i].x += shift;
        }
        if (has_ink) {
            ink.min_x += shift;
            ink.max_x += shift;
        }
        logical_min += shift;
        logical_max += shift;
    }

    GlyphLayoutLine line;
    line.glyph_start = glyph_start;
    line.glyph_count = l->glyph_count - glyph_start;
    line.text_start = text_start;
    line.text_end = text_end;
    line.x = logical_min;
    line.y = y;
    line.width = logical_max - logical_min;
    line.height = h;
    line.ink_bounds = has_ink ? ink : (GlyphLayoutBounds){line.x, y, line.x + line.width, y};
    for (size_t i = 0; i < glyph_start; i++) {
        if (l->glyphs[i].atlas_index >= 0) {
            had_ink = 1;
            break;
        }
    }

    if (!add_line(l, &line)) {
        return 0;
    }
    l->width = i32_max(l->width, line.width);
    l->height = i32_max(l->height, y + h);
    if (has_ink) {
        if (!had_ink) {
            l->ink_bounds = ink;
        } else {
            l->ink_bounds.min_x = i32_min(l->ink_bounds.min_x, ink.min_x);
            l->ink_bounds.min_y = i32_min(l->ink_bounds.min_y, ink.min_y);
            l->ink_bounds.max_x = i32_max(l->ink_bounds.max_x, ink.max_x);
            l->ink_bounds.max_y = i32_max(l->ink_bounds.max_y, ink.max_y);
        }
    }
    return 1;
}

static int32_t glyph_right(const GlyphFile *f, const GlyphLayoutGlyph *g) {
    int32_t right = i32_max(g->x, g->x + g->advance);
    if (g->atlas_index >= 0 && (uint32_t)g->atlas_index < f->glyphs.count) {
        const GlyphEntry *e = &f->glyphs.entries[g->atlas_index];
        right = i32_max(right, g->x + (int32_t)e->x + (int32_t)e->width);
    }
    return right;
}

void glyph_layout_options_default(GlyphLayoutOptions *options) {
    if (options) {
        options->wrap_width = 0;
        options->line_height = 0;
        options->tab_width = 4;
        options->fallback_advance = 0;
        options->align = GLYPH_LAYOUT_ALIGN_LEFT;
    }
}

void glyph_layout_init(GlyphLayout *layout) {
    if (layout) {
        memset(layout, 0, sizeof(*layout));
    }
}

void glyph_layout_free(GlyphLayout *layout) {
    if (layout) {
        free(layout->glyphs);
        free(layout->lines);
        memset(layout, 0, sizeof(*layout));
    }
}

int glyph_layout_shape(const GlyphFile *file, const char *text, const GlyphLayoutOptions *options, GlyphLayout *layout) {
    if (!file || !text || !layout) {
        return 0;
    }
    GlyphLayoutOptions opt;
    if (options) {
        opt = *options;
    } else {
        glyph_layout_options_default(&opt);
    }
    if (opt.tab_width <= 0) {
        opt.tab_width = 4;
    }

    glyph_layout_free(layout);
    glyph_layout_init(layout);

    int32_t h = line_height(file, &opt);
    int32_t fallback = fallback_advance(file, &opt);
    int32_t tab_size = fallback * opt.tab_width;
    size_t len = strlen(text);
    size_t line_glyph_start = 0;
    size_t line_text_start = 0;
    int32_t y = 0;
    int32_t pen = 0;
    uint32_t prev = UINT32_MAX;

    for (size_t i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)text[i];
        if (ch == '\r' || ch == '\n') {
            if (!finish_line(file, &opt, layout, line_glyph_start, line_text_start, i, y, h)) {
                glyph_layout_free(layout);
                return 0;
            }
            if (ch == '\r' && i + 1 < len && text[i + 1] == '\n') {
                i++;
            }
            y += h;
            line_glyph_start = layout->glyph_count;
            line_text_start = i + 1;
            pen = 0;
            prev = UINT32_MAX;
            continue;
        }

retry:
        ;
        GlyphLayoutGlyph g;
        memset(&g, 0, sizeof(g));
        g.glyph_id = (uint32_t)ch;
        g.text_index = i;
        g.atlas_index = -1;
        g.x = pen;
        g.y = y;
        g.advance = fallback;
        g.kerning = 0;

        if (ch == '\t') {
            g.advance = tab_advance(pen, tab_size);
            prev = UINT32_MAX;
        } else {
            int idx = glyph_table_find_id(&file->glyphs, (uint32_t)ch);
            if (idx >= 0) {
                const GlyphEntry *e = &file->glyphs.entries[idx];
                g.atlas_index = idx;
                g.kerning = prev == UINT32_MAX ? 0 : kerning_lookup(&file->kerning, &file->glyphs, prev, (uint32_t)ch);
                g.x = pen + g.kerning;
                g.advance = entry_advance(e);
                prev = (uint32_t)ch;
            } else {
                prev = UINT32_MAX;
            }
        }

        if (opt.wrap_width > 0 && layout->glyph_count > line_glyph_start && glyph_right(file, &g) > opt.wrap_width) {
            if (!finish_line(file, &opt, layout, line_glyph_start, line_text_start, i, y, h)) {
                glyph_layout_free(layout);
                return 0;
            }
            y += h;
            line_glyph_start = layout->glyph_count;
            line_text_start = i;
            pen = 0;
            prev = UINT32_MAX;
            goto retry;
        }

        if (!add_glyph(layout, &g)) {
            glyph_layout_free(layout);
            return 0;
        }
        pen = g.x + g.advance;
    }

    if (!finish_line(file, &opt, layout, line_glyph_start, line_text_start, len, y, h)) {
        glyph_layout_free(layout);
        return 0;
    }
    return 1;
}

int glyph_layout_get_bounds(const GlyphLayout *layout, GlyphLayoutBounds *bounds) {
    if (!layout || !bounds) {
        return 0;
    }
    *bounds = layout->ink_bounds;
    return 1;
}

size_t glyph_layout_hit_test(const GlyphLayout *layout, int32_t x, int32_t y) {
    if (!layout) {
        return GLYPH_LAYOUT_HIT_NONE;
    }
    for (size_t li = 0; li < layout->line_count; li++) {
        const GlyphLayoutLine *line = &layout->lines[li];
        if (y < line->y || y >= line->y + line->height || line->glyph_count == 0) {
            continue;
        }
        size_t best = GLYPH_LAYOUT_HIT_NONE;
        int32_t best_dist = INT_MAX;
        for (size_t i = 0; i < line->glyph_count; i++) {
            size_t gi = line->glyph_start + i;
            const GlyphLayoutGlyph *g = &layout->glyphs[gi];
            int32_t left = i32_min(g->x, g->x + g->advance);
            int32_t right = i32_max(g->x, g->x + g->advance);
            if (left == right) {
                right = left + 1;
            }
            if (x >= left && x < right) {
                return gi;
            }
            int32_t center = left + (right - left) / 2;
            int32_t dist = x >= center ? x - center : center - x;
            if (dist < best_dist) {
                best_dist = dist;
                best = gi;
            }
        }
        return best;
    }
    return GLYPH_LAYOUT_HIT_NONE;
}

size_t glyph_layout_hit_text_index(const GlyphLayout *layout, int32_t x, int32_t y) {
    size_t hit = glyph_layout_hit_test(layout, x, y);
    if (hit == GLYPH_LAYOUT_HIT_NONE || !layout || hit >= layout->glyph_count) {
        return GLYPH_LAYOUT_HIT_NONE;
    }
    return layout->glyphs[hit].text_index;
}

int glyph_layout_write_report(FILE *fp, const GlyphLayout *layout) {
    if (!fp || !layout) {
        return 0;
    }
    fprintf(fp, "glyph layout: glyphs=%zu lines=%zu size=%dx%d ink=(%d,%d)-(%d,%d)\n",
            layout->glyph_count,
            layout->line_count,
            layout->width,
            layout->height,
            layout->ink_bounds.min_x,
            layout->ink_bounds.min_y,
            layout->ink_bounds.max_x,
            layout->ink_bounds.max_y);
    for (size_t li = 0; li < layout->line_count; li++) {
        const GlyphLayoutLine *line = &layout->lines[li];
        fprintf(fp, "line %zu: glyphs=%zu..%zu text=%zu..%zu pos=(%d,%d) size=%dx%d ink=(%d,%d)-(%d,%d)\n",
                li,
                line->glyph_start,
                line->glyph_start + line->glyph_count,
                line->text_start,
                line->text_end,
                line->x,
                line->y,
                line->width,
                line->height,
                line->ink_bounds.min_x,
                line->ink_bounds.min_y,
                line->ink_bounds.max_x,
                line->ink_bounds.max_y);
        for (size_t i = 0; i < line->glyph_count; i++) {
            size_t gi = line->glyph_start + i;
            const GlyphLayoutGlyph *g = &layout->glyphs[gi];
            fprintf(fp, "  glyph %zu: id=%u text=%zu atlas=%d pos=(%d,%d) advance=%d kern=%d\n",
                    gi,
                    g->glyph_id,
                    g->text_index,
                    g->atlas_index,
                    g->x,
                    g->y,
                    g->advance,
                    g->kerning);
        }
    }
    return ferror(fp) == 0;
}

int glyph_layout_render_to_buffer(const GlyphFile *file, const GlyphLayout *layout, uint8_t *pixels, uint32_t width, uint32_t height, int32_t origin_x, int32_t origin_y) {
    if (!file || !layout || !pixels || !file->atlas_pixels || !file->placements) {
        return 0;
    }
    for (size_t i = 0; i < layout->glyph_count; i++) {
        const GlyphLayoutGlyph *g = &layout->glyphs[i];
        if (g->atlas_index < 0 || (uint32_t)g->atlas_index >= file->glyphs.count) {
            continue;
        }
        const GlyphEntry *e = &file->glyphs.entries[g->atlas_index];
        const GlyphPlacement *p = &file->placements[g->atlas_index];
        if (p->x + e->width > file->atlas_width || p->y + e->height > file->atlas_height) {
            continue;
        }
        int32_t dx0 = origin_x + g->x + (int32_t)e->x;
        int32_t dy0 = origin_y + g->y + (int32_t)e->y;
        for (uint32_t sy = 0; sy < e->height; sy++) {
            int32_t dy = dy0 + (int32_t)sy;
            if (dy < 0 || dy >= (int32_t)height) {
                continue;
            }
            for (uint32_t sx = 0; sx < e->width; sx++) {
                int32_t dx = dx0 + (int32_t)sx;
                if (dx < 0 || dx >= (int32_t)width) {
                    continue;
                }
                uint8_t src = file->atlas_pixels[(size_t)(p->y + sy) * file->atlas_width + p->x + sx];
                uint8_t *dst = &pixels[(size_t)dy * width + (uint32_t)dx];
                if (src > *dst) {
                    *dst = src;
                }
            }
        }
    }
    return 1;
}
