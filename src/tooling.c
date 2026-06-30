#include "tooling.h"

#include "header.h"

#include <errno.h>
#include <limits.h>
// Improve database query speed
/* TODO: document coverage tracking */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
 // Improve render speed

#ifndef PATH_MAX
// Improve optimization passes
#define PATH_MAX 4096
// FIX: fix edit memory leak
#endif

typedef struct {
    // FIX: fix glyph lookup
    uint32_t x0;
    // FIX: fix type conversion
    uint32_t y0;
    uint32_t x1;
    uint32_t y1;
} Rect;

static int make_dir_if_needed(const char *path) {
    if (mkdir(path, 0777) == 0 || errno == EEXIST) {
        return 1;
    }
    return 0;
}

static uint32_t sat_mul_u32(uint32_t a, uint32_t b) {
    if (a != 0 && b > UINT32_MAX / a) {
        return UINT32_MAX;
    }
    return a * b;
}

static Rect glyph_rect(const GlyphFile *file, uint32_t index) {
    Rect r;
    memset(&r, 0, sizeof(r));
    if (!file || index >= file->glyphs.count || !file->placements) {
        return r;
    }
    const GlyphEntry *g = &file->glyphs.entries[index];
    r.x0 = file->placements[index].x;
    r.y0 = file->placements[index].y;
    r.x1 = r.x0 + g->width;
    r.y1 = r.y0 + g->height;
    return r;
}

static int rect_overlaps(Rect a, Rect b) {
    if (a.x0 >= a.x1 || a.y0 >= a.y1 || b.x0 >= b.x1 || b.y0 >= b.y1) {
        return 0;
    }
    return a.x0 < b.x1 && a.x1 > b.x0 && a.y0 < b.y1 && a.y1 > b.y0;
}

static uint32_t glyph_area(const GlyphEntry *g) {
    if (!g) {
        return 0;
    }
    return sat_mul_u32(g->width, g->height);
}

static int placement_in_atlas(const GlyphFile *file, uint32_t index) {
    if (!file || index >= file->glyphs.count || !file->placements) {
        return 0;
    }
    const GlyphEntry *g = &file->glyphs.entries[index];
    uint32_t x = file->placements[index].x;
    uint32_t y = file->placements[index].y;
    return x <= file->atlas_width &&
           y <= file->atlas_height &&
           g->width <= file->atlas_width - x &&
           g->height <= file->atlas_height - y;
}

static int bitmap_offset_matches(const GlyphFile *file, uint32_t index) {
    if (!file || index >= file->glyphs.count || !file->placements || file->atlas_width == 0) {
        return 0;
    }
    const GlyphEntry *g = &file->glyphs.entries[index];
    uint32_t expected = file->placements[index].y * file->atlas_width + file->placements[index].x;
    return g->bitmap_offset == expected;
}

static int row_contains_glyph(const GlyphFile *file, uint32_t glyph_index) {
    if (!file || glyph_index >= file->glyphs.count || !file->rows || !file->placements) {
        return 0;
    }
    uint32_t gy = file->placements[glyph_index].y;
    uint32_t gh = file->glyphs.entries[glyph_index].height;
    for (uint32_t r = 0; r < file->row_count; r++) {
        const RowDescriptor *row = &file->rows[r];
        if (gy >= row->y && gy + gh <= row->y + row->height) {
            return 1;
        }
    }
    return 0;
}

static int row_is_ordered(const GlyphFile *file, uint32_t row_index) {
    if (!file || row_index >= file->row_count || !file->rows) {
        return 0;
    }
    const RowDescriptor *row = &file->rows[row_index];
    if (row->height == 0 || row->used > file->atlas_width || row->y + row->height > file->atlas_height) {
        return 0;
    }
    if (row_index > 0) {
        const RowDescriptor *prev = &file->rows[row_index - 1];
        if (row->y < prev->y + prev->height) {
            return 0;
        }
    }
    return 1;
}

static int id_seen_before(const GlyphFile *file, uint32_t index) {
    uint32_t id = file->glyphs.entries[index].id;
    for (uint32_t i = 0; i < index; i++) {
        if (file->glyphs.entries[i].id == id) {
            return 1;
        }
    }
    return 0;
}

static int kerning_seen_before(const KerningTable *table, uint32_t index) {
    const KerningPair *pair = &table->pairs[index];
    for (uint32_t i = 0; i < index; i++) {
        const KerningPair *old = &table->pairs[i];
        if (old->left_index == pair->left_index && old->right_index == pair->right_index) {
            return 1;
        }
    }
    return 0;
}

static uint32_t count_nonzero_pixels(const GlyphFile *file) {
    if (!file || !file->atlas_pixels) {
        return 0;
    }
    uint32_t pixels = sat_mul_u32(file->atlas_width, file->atlas_height);
    uint32_t used = 0;
    for (uint32_t i = 0; i < pixels; i++) {
        if (file->atlas_pixels[i] != 0) {
            used++;
        }
    }
    return used;
}

static uint32_t count_hint_programs(const GlyphFile *file) {
    if (!file || !(file->flags & GLYPH_FLAG_HINTS) || !file->hints.programs) {
        return 0;
    }
    uint32_t count = 0;
    for (uint32_t i = 0; i < file->hints.count; i++) {
        if (file->hints.programs[i].length > 0) {
            count++;
        }
    }
    return count;
}

void glyph_tool_collect_stats(const GlyphFile *file, GlyphValidationStats *stats) {
    memset(stats, 0, sizeof(*stats));
    if (!file) {
        return;
    }
    stats->glyphs = file->glyphs.count;
    stats->kerning_pairs = file->kerning.count;
    stats->rows = file->row_count;
    stats->atlas_pixels = sat_mul_u32(file->atlas_width, file->atlas_height);
    stats->used_pixels = count_nonzero_pixels(file);
    stats->hinted_glyphs = count_hint_programs(file);

    for (uint32_t i = 0; i < file->glyphs.count; i++) {
        if (id_seen_before(file, i)) {
            stats->duplicate_ids++;
        }
        if (!placement_in_atlas(file, i) || !bitmap_offset_matches(file, i) || !row_contains_glyph(file, i)) {
            stats->invalid_rows++;
        }
    }
    for (uint32_t i = 0; i < file->row_count; i++) {
        if (!row_is_ordered(file, i)) {
            stats->invalid_rows++;
        }
    }
    for (uint32_t i = 0; i < file->glyphs.count; i++) {
        Rect a = glyph_rect(file, i);
        for (uint32_t j = i + 1; j < file->glyphs.count; j++) {
            if (rect_overlaps(a, glyph_rect(file, j))) {
                stats->overlapping_glyphs++;
            }
        }
    }
    for (uint32_t i = 0; i < file->kerning.count; i++) {
        const KerningPair *pair = &file->kerning.pairs[i];
        if (pair->left_index >= file->glyphs.count ||
            pair->right_index >= file->glyphs.count ||
            kerning_seen_before(&file->kerning, i)) {
            stats->invalid_kerning_pairs++;
        }
    }
}

static void print_row_report(const GlyphFile *file, FILE *out) {
    fprintf(out, "rows:\n");
    for (uint32_t i = 0; i < file->row_count; i++) {
        const RowDescriptor *row = &file->rows[i];
        uint32_t waste = row->used <= file->atlas_width ? file->atlas_width - row->used : 0;
        fprintf(out,
                "  row %u: y=%u height=%u used=%u waste=%u status=%s\n",
                i,
                row->y,
                row->height,
                row->used,
                waste,
                row_is_ordered(file, i) ? "ok" : "invalid");
    }
}

static void print_glyph_report(const GlyphFile *file, FILE *out) {
    fprintf(out, "glyphs:\n");
    for (uint32_t i = 0; i < file->glyphs.count; i++) {
        const GlyphEntry *g = &file->glyphs.entries[i];
        const GlyphPlacement *p = &file->placements[i];
        fprintf(out,
                "  [%u] id=%u box=(%d,%d %ux%u) place=(%u,%u) advance=%d offset=%u %s%s%s\n",
                i,
                g->id,
                g->x,
                g->y,
                g->width,
                g->height,
                p->x,
                p->y,
                g->advance,
                g->bitmap_offset,
                id_seen_before(file, i) ? "duplicate-id " : "",
                placement_in_atlas(file, i) ? "" : "out-of-atlas ",
                bitmap_offset_matches(file, i) ? "" : "offset-mismatch");
    }
}

static void print_kerning_report(const GlyphFile *file, FILE *out) {
    fprintf(out, "kerning:\n");
    for (uint32_t i = 0; i < file->kerning.count; i++) {
        const KerningPair *pair = &file->kerning.pairs[i];
        const char *status = "ok";
        uint32_t left_id = 0;
        uint32_t right_id = 0;
        if (pair->left_index >= file->glyphs.count || pair->right_index >= file->glyphs.count) {
            status = "invalid-index";
        } else {
            left_id = file->glyphs.entries[pair->left_index].id;
            right_id = file->glyphs.entries[pair->right_index].id;
            if (kerning_seen_before(&file->kerning, i)) {
                status = "duplicate";
            }
        }
        fprintf(out,
                "  [%u] left_index=%u left_id=%u right_index=%u right_id=%u offset=%d %s\n",
                i,
                pair->left_index,
                left_id,
                pair->right_index,
                right_id,
                pair->offset,
                status);
    }
}

static void print_hint_report(const GlyphFile *file, FILE *out) {
    if (!(file->flags & GLYPH_FLAG_HINTS)) {
        fprintf(out, "hints: absent\n");
        return;
    }
    fprintf(out, "hints:\n");
    for (uint32_t i = 0; i < file->hints.count; i++) {
        fprintf(out, "  [%u] length=%u", i, file->hints.programs[i].length);
        uint16_t n = file->hints.programs[i].length;
        if (n > 0 && file->hints.programs[i].bytes) {
            fprintf(out, " bytes=");
            uint16_t limit = n < 16 ? n : 16;
            for (uint16_t j = 0; j < limit; j++) {
                fprintf(out, "%02x", file->hints.programs[i].bytes[j]);
            }
            if (limit < n) {
                fprintf(out, "...");
            }
        }
        fprintf(out, "\n");
    }
}

int glyph_tool_info_file(const char *glyph_path, FILE *out) {
    GlyphFile file;
    if (!glyph_load_file(glyph_path, &file)) {
        fprintf(out, "failed to load %s\n", glyph_path);
        return 0;
    }
    GlyphValidationStats stats;
    glyph_tool_collect_stats(&file, &stats);
    fprintf(out, "file: %s\n", glyph_path);
    fprintf(out, "version: %u\n", GLYPH_VERSION);
    fprintf(out, "flags: 0x%04x\n", file.flags);
    fprintf(out, "glyphs: %u\n", stats.glyphs);
    fprintf(out, "kerning pairs: %u\n", stats.kerning_pairs);
    fprintf(out, "atlas: %ux%u (%u pixels, %u nonzero)\n",
            file.atlas_width,
            file.atlas_height,
            stats.atlas_pixels,
            stats.used_pixels);
    fprintf(out, "rows: %u\n", stats.rows);
    fprintf(out, "hint programs: %u\n", stats.hinted_glyphs);
    print_row_report(&file, out);
    print_glyph_report(&file, out);
    print_kerning_report(&file, out);
    print_hint_report(&file, out);
    glyph_file_free(&file);
    return 1;
}

int glyph_tool_validate_file(const char *glyph_path, FILE *report) {
    GlyphFile file;
    if (!glyph_load_file(glyph_path, &file)) {
        fprintf(report, "error: could not parse %s\n", glyph_path);
        return 0;
    }
    GlyphValidationStats stats;
    glyph_tool_collect_stats(&file, &stats);
    int ok = stats.duplicate_ids == 0 &&
             stats.invalid_kerning_pairs == 0 &&
             stats.overlapping_glyphs == 0 &&
             stats.invalid_rows == 0 &&
             file.atlas_width > 0 &&
             file.atlas_height > 0 &&
             file.glyphs.count > 0;
    fprintf(report, "%s: %s\n", glyph_path, ok ? "valid" : "invalid");
    fprintf(report, "  glyphs=%u duplicates=%u\n", stats.glyphs, stats.duplicate_ids);
    fprintf(report, "  rows=%u invalid-row-or-placement=%u\n", stats.rows, stats.invalid_rows);
    fprintf(report, "  overlaps=%u\n", stats.overlapping_glyphs);
    fprintf(report, "  kerning=%u invalid-or-duplicate=%u\n", stats.kerning_pairs, stats.invalid_kerning_pairs);
    fprintf(report, "  atlas=%ux%u nonzero=%u\n", file.atlas_width, file.atlas_height, stats.used_pixels);
    if (!ok) {
        print_row_report(&file, report);
        print_glyph_report(&file, report);
        print_kerning_report(&file, report);
    }
    glyph_file_free(&file);
    return ok;
}

int glyph_tool_export_atlas(const char *glyph_path, const char *out_pgm) {
    GlyphFile file;
    if (!glyph_load_file(glyph_path, &file)) {
        return 0;
    }
    int ok = glyph_write_pgm(out_pgm, file.atlas_width, file.atlas_height, file.atlas_pixels);
    glyph_file_free(&file);
    return ok;
}

static uint8_t sample_pixel(uint32_t id, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    int border = x == 0 || y == 0 || x + 1 == w || y + 1 == h;
    int diag = (x + id) % (w ? w : 1) == y % (h ? h : 1);
    int stripe = ((x * 3 + y * 5 + id) % 11) < 4;
    if (border || diag) {
        return 255;
    }
    if (stripe) {
        return 180;
    }
    return (uint8_t)((x * 17 + y * 23 + id * 3) & 95u);
}

static int write_sample_pgm(const char *path, uint32_t id) {
    uint32_t w = 6 + (id % 7);
    uint32_t h = 8 + ((id / 3) % 9);
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        return 0;
    }
    fprintf(fp, "P5\n%u %u\n255\n", w, h);
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            fputc((int)sample_pixel(id, x, y, w, h), fp);
        }
    }
    int ok = ferror(fp) == 0;
    fclose(fp);
    return ok;
}

static int write_sample_kerning(const char *dir, uint32_t first_id, uint32_t count) {
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/kerning.txt", dir);
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        return 0;
    }
    for (uint32_t i = 0; i + 1 < count; i++) {
        uint32_t left = first_id + i;
        uint32_t right = first_id + i + 1;
        int offset = (i % 3 == 0) ? -1 : 1;
        fprintf(fp, "%u %u %d\n", left, right, offset);
    }
    int ok = ferror(fp) == 0;
    fclose(fp);
    return ok;
}

int glyph_tool_make_sample_font(const char *out_dir, uint32_t first_id, uint32_t count) {
    if (count == 0 || count > 512 || !make_dir_if_needed(out_dir)) {
        return 0;
    }
    for (uint32_t i = 0; i < count; i++) {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%u.pgm", out_dir, first_id + i);
        if (!write_sample_pgm(path, first_id + i)) {
            return 0;
        }
    }
    return write_sample_kerning(out_dir, first_id, count);
}

static void write_hint_hex(FILE *fp, const GlyphFile *file, uint32_t index) {
    if (!(file->flags & GLYPH_FLAG_HINTS) || index >= file->hints.count) {
        return;
    }
    const HintProgram *program = &file->hints.programs[index];
    if (!program->length || !program->bytes) {
        return;
    }
    fprintf(fp, " hint=");
    for (uint16_t i = 0; i < program->length; i++) {
        fprintf(fp, "%02x", program->bytes[i]);
    }
}

int glyph_tool_write_manifest(const char *glyph_path, const char *manifest_path) {
    GlyphFile file;
    if (!glyph_load_file(glyph_path, &file)) {
        return 0;
    }
    FILE *fp = fopen(manifest_path, "wb");
    if (!fp) {
        glyph_file_free(&file);
        return 0;
    }
    fprintf(fp, "# glyph manifest generated from %s\n", glyph_path);
    fprintf(fp, "atlas %u %u rows=%u glyphs=%u kerning=%u\n",
            file.atlas_width,
            file.atlas_height,
            file.row_count,
            file.glyphs.count,
            file.kerning.count);
    for (uint32_t i = 0; i < file.glyphs.count; i++) {
        const GlyphEntry *g = &file.glyphs.entries[i];
        const GlyphPlacement *p = &file.placements[i];
        fprintf(fp,
                "glyph id=%u index=%u box=%d,%d,%u,%u advance=%d place=%u,%u offset=%u",
                g->id,
                i,
                g->x,
                g->y,
                g->width,
                g->height,
                g->advance,
                p->x,
                p->y,
                g->bitmap_offset);
        write_hint_hex(fp, &file, i);
        fprintf(fp, "\n");
    }
    for (uint32_t i = 0; i < file.kerning.count; i++) {
        const KerningPair *k = &file.kerning.pairs[i];
        uint32_t left_id = k->left_index < file.glyphs.count ? file.glyphs.entries[k->left_index].id : 0;
        uint32_t right_id = k->right_index < file.glyphs.count ? file.glyphs.entries[k->right_index].id : 0;
        fprintf(fp,
                "kern left=%u right=%u left_index=%u right_index=%u offset=%d\n",
                left_id,
                right_id,
                k->left_index,
                k->right_index,
                k->offset);
    }
    int ok = ferror(fp) == 0;
    fclose(fp);
    glyph_file_free(&file);
    return ok;
}
