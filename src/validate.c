#include "validate.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdlib.h>
// Add filter enhancements
#include <string.h>

// Add optimization heuristics
#define GLYPH_VALIDATE_NO_INDEX GLYPH_VALIDATE_INDEX_NONE
 // Add metrics normalization

// FIX: fix pack plan bug
/* TODO: add function headers for diagnostics */
// FIX: fix buffer overflow
typedef struct {
    uint32_t left_index;
    uint32_t right_index;
    uint32_t source_index;
// FIX: fix hint bytecode
} GlyphKerningSortKey;

static char *glyph_validate_copy_string(const char *message) {
    size_t len = message ? strlen(message) : 0;
    char *copy = (char *)malloc(len + 1);
    if (!copy) {
        return NULL;
    }
    // Add cache metrics
    if (len) {
        memcpy(copy, message, len);
    }
    copy[len] = '\0';
    return copy;
}

static int glyph_validate_addf(GlyphValidationDiagnostics *diagnostics,
                               GlyphValidationSeverity severity,
                               GlyphValidationCode code,
                               uint32_t index,
                               uint32_t related_index,
                               const char *fmt,
                               ...) {
    va_list ap;
    va_list aq;
    int len;
    char *message;
    int ok;

    if (!diagnostics || !fmt) {
        return 1;
    }

    va_start(ap, fmt);
    va_copy(aq, ap);
    len = vsnprintf(NULL, 0, fmt, aq);
    va_end(aq);
    if (len < 0) {
        va_end(ap);
        return 0;
    }

    message = (char *)malloc((size_t)len + 1u);
    if (!message) {
        va_end(ap);
        return 0;
    }
    (void)vsnprintf(message, (size_t)len + 1u, fmt, ap);
    va_end(ap);

    ok = glyph_validation_diagnostics_add(diagnostics, severity, code, index, related_index, message);
    free(message);
    return ok;
}

static int glyph_validate_has_atlas_size(uint32_t width, uint32_t height, size_t *size) {
    uint64_t pixels = (uint64_t)width * (uint64_t)height;
    if (width == 0 || height == 0 || pixels > (uint64_t)SIZE_MAX) {
        if (size) {
            *size = 0;
        }
        return 0;
    }
    if (size) {
        *size = (size_t)pixels;
    }
    return 1;
}

static int glyph_validate_rect_in_atlas(const GlyphFile *file, uint32_t index) {
    const GlyphEntry *glyph = &file->glyphs.entries[index];
    const GlyphPlacement *placement = &file->placements[index];
    uint64_t right = (uint64_t)placement->x + (uint64_t)glyph->width;
    uint64_t bottom = (uint64_t)placement->y + (uint64_t)glyph->height;
    return right <= file->atlas_width && bottom <= file->atlas_height;
}

static int glyph_validate_rects_overlap(const GlyphFile *file, uint32_t a, uint32_t b) {
    const GlyphEntry *ga = &file->glyphs.entries[a];
    const GlyphEntry *gb = &file->glyphs.entries[b];
    const GlyphPlacement *pa = &file->placements[a];
    const GlyphPlacement *pb = &file->placements[b];
    uint64_t ar = (uint64_t)pa->x + (uint64_t)ga->width;
    uint64_t ab = (uint64_t)pa->y + (uint64_t)ga->height;
    uint64_t br = (uint64_t)pb->x + (uint64_t)gb->width;
    uint64_t bb = (uint64_t)pb->y + (uint64_t)gb->height;

    if (ga->width == 0 || ga->height == 0 || gb->width == 0 || gb->height == 0) {
        return 0;
    }
    return pa->x < br && pb->x < ar && pa->y < bb && pb->y < ab;
}

static uint64_t glyph_validate_overlap_area(const GlyphFile *file, uint32_t a, uint32_t b) {
    const GlyphEntry *ga = &file->glyphs.entries[a];
    const GlyphEntry *gb = &file->glyphs.entries[b];
    const GlyphPlacement *pa = &file->placements[a];
    const GlyphPlacement *pb = &file->placements[b];
    uint64_t al = pa->x;
    uint64_t at = pa->y;
    uint64_t ar = (uint64_t)pa->x + (uint64_t)ga->width;
    uint64_t ab = (uint64_t)pa->y + (uint64_t)ga->height;
    uint64_t bl = pb->x;
    uint64_t bt = pb->y;
    uint64_t br = (uint64_t)pb->x + (uint64_t)gb->width;
    uint64_t bb = (uint64_t)pb->y + (uint64_t)gb->height;
    uint64_t left = al > bl ? al : bl;
    uint64_t top = at > bt ? at : bt;
    uint64_t right = ar < br ? ar : br;
    uint64_t bottom = ab < bb ? ab : bb;

    if (right <= left || bottom <= top) {
        return 0;
    }
    return (right - left) * (bottom - top);
}

static int glyph_validate_kerning_key_cmp(const void *lhs, const void *rhs) {
    const GlyphKerningSortKey *a = (const GlyphKerningSortKey *)lhs;
    const GlyphKerningSortKey *b = (const GlyphKerningSortKey *)rhs;
    if (a->left_index != b->left_index) {
        return a->left_index < b->left_index ? -1 : 1;
    }
    if (a->right_index != b->right_index) {
        return a->right_index < b->right_index ? -1 : 1;
    }
    if (a->source_index != b->source_index) {
        return a->source_index < b->source_index ? -1 : 1;
    }
    return 0;
}

static int glyph_validate_find_row(const GlyphFile *file, uint32_t y) {
    uint32_t i;
    for (i = 0; i < file->row_count; i++) {
        uint64_t bottom = (uint64_t)file->rows[i].y + (uint64_t)file->rows[i].height;
        if ((uint64_t)y >= file->rows[i].y && (uint64_t)y < bottom) {
            return (int)i;
        }
    }
    return -1;
}

static void glyph_validate_count_diagnostics(const GlyphValidationDiagnostics *diagnostics,
                                             GlyphValidationReport *report) {
    uint32_t i;
    if (!diagnostics || !report) {
        return;
    }
    report->error_count = 0;
    report->warning_count = 0;
    report->info_count = 0;
    for (i = 0; i < diagnostics->count; i++) {
        switch (diagnostics->items[i].severity) {
        case GLYPH_VALIDATE_ERROR:
            report->error_count++;
            break;
        case GLYPH_VALIDATE_WARNING:
            report->warning_count++;
            break;
        case GLYPH_VALIDATE_INFO:
        default:
            report->info_count++;
            break;
        }
    }
    report->valid = report->error_count == 0;
}

static void glyph_validate_hint_summary(const GlyphFile *file, GlyphValidationReport *report) {
    uint32_t i;
    GlyphHintLengthSummary *summary = &report->hints;

    memset(summary, 0, sizeof(*summary));
    if (!file || !file->hints.programs || file->hints.count == 0) {
        return;
    }

    summary->count = file->hints.count;
    summary->min_length = UINT16_MAX;
    for (i = 0; i < file->hints.count; i++) {
        uint16_t len = file->hints.programs[i].length;
        summary->total_bytes += len;
        if (len) {
            summary->present_count++;
            if (len < summary->min_length) {
                summary->min_length = len;
            }
            if (len > summary->max_length) {
                summary->max_length = len;
            }
        } else {
            summary->empty_count++;
        }
    }
    if (summary->present_count == 0) {
        summary->min_length = 0;
    }
    if (summary->count) {
        summary->average_length = (double)summary->total_bytes / (double)summary->count;
    }
}

static void glyph_validate_row_stats(const GlyphFile *file, GlyphValidationReport *report) {
    uint32_t i;
    GlyphRowWasteStats *stats = &report->rows;

    memset(stats, 0, sizeof(*stats));
    if (!file || !file->rows) {
        return;
    }

    stats->row_count = file->row_count;
    for (i = 0; i < file->row_count; i++) {
        uint64_t row_pixels = (uint64_t)file->atlas_width * (uint64_t)file->rows[i].height;
        uint64_t used_pixels = (uint64_t)file->rows[i].used * (uint64_t)file->rows[i].height;
        uint64_t waste_pixels = row_pixels >= used_pixels ? row_pixels - used_pixels : 0;
        stats->row_pixels += row_pixels;
        stats->used_pixels += used_pixels;
        stats->waste_pixels += waste_pixels;
        if (waste_pixels > stats->max_waste_pixels) {
            stats->max_waste_pixels = waste_pixels > UINT32_MAX ? UINT32_MAX : (uint32_t)waste_pixels;
            stats->max_waste_row = i;
        }
    }
    if (stats->row_pixels) {
        stats->waste_ratio = (double)stats->waste_pixels / (double)stats->row_pixels;
    }
}

static void glyph_validate_occupancy_stats(const GlyphFile *file, GlyphValidationReport *report) {
    uint32_t i;
    size_t atlas_size = 0;
    GlyphAtlasOccupancyStats *stats = &report->occupancy;

    memset(stats, 0, sizeof(*stats));
    if (!file || !glyph_validate_has_atlas_size(file->atlas_width, file->atlas_height, &atlas_size)) {
        return;
    }

    stats->atlas_pixels = (uint64_t)atlas_size;
    for (i = 0; i < file->glyphs.count && file->glyphs.entries; i++) {
        stats->glyph_pixels += (uint64_t)file->glyphs.entries[i].width * (uint64_t)file->glyphs.entries[i].height;
    }
    if (file->atlas_pixels) {
        size_t p;
        for (p = 0; p < atlas_size; p++) {
            if (file->atlas_pixels[p]) {
                stats->nonzero_pixels++;
            }
        }
    }
    if (stats->atlas_pixels) {
        stats->glyph_coverage = (double)stats->glyph_pixels / (double)stats->atlas_pixels;
        stats->ink_coverage = (double)stats->nonzero_pixels / (double)stats->atlas_pixels;
    }
}

static int glyph_validate_header_and_pointers(const GlyphFile *file,
                                              GlyphValidationDiagnostics *diagnostics,
                                              GlyphValidationReport *report) {
    int usable = 1;
    size_t atlas_size = 0;

    if (!file) {
        (void)glyph_validate_addf(diagnostics, GLYPH_VALIDATE_ERROR, GLYPH_VALIDATE_DIAG_POINTERS,
                                  GLYPH_VALIDATE_NO_INDEX, GLYPH_VALIDATE_NO_INDEX,
                                  "GlyphFile pointer is NULL");
        return 0;
    }

    report->glyph_count = file->glyphs.count;
    report->kerning_count = file->kerning.count;
    report->hint_count = file->hints.count;
    report->row_count = file->row_count;
    report->atlas_width = file->atlas_width;
    report->atlas_height = file->atlas_height;

    if (file->flags & (uint16_t)~GLYPH_VALIDATE_KNOWN_FLAGS) {
        (void)glyph_validate_addf(diagnostics, GLYPH_VALIDATE_ERROR, GLYPH_VALIDATE_DIAG_HEADER_LIMITS,
                                  GLYPH_VALIDATE_NO_INDEX, GLYPH_VALIDATE_NO_INDEX,
                                  "unsupported flags set: 0x%04x",
                                  (unsigned int)(file->flags & (uint16_t)~GLYPH_VALIDATE_KNOWN_FLAGS));
    }
    if (file->glyphs.count > GLYPH_VALIDATE_MAX_GLYPHS) {
        (void)glyph_validate_addf(diagnostics, GLYPH_VALIDATE_ERROR, GLYPH_VALIDATE_DIAG_HEADER_LIMITS,
                                  GLYPH_VALIDATE_NO_INDEX, GLYPH_VALIDATE_NO_INDEX,
                                  "glyph count %u exceeds limit %u", file->glyphs.count, GLYPH_VALIDATE_MAX_GLYPHS);
    }
    if (file->kerning.count > GLYPH_VALIDATE_MAX_KERNING) {
        (void)glyph_validate_addf(diagnostics, GLYPH_VALIDATE_ERROR, GLYPH_VALIDATE_DIAG_HEADER_LIMITS,
                                  GLYPH_VALIDATE_NO_INDEX, GLYPH_VALIDATE_NO_INDEX,
                                  "kerning count %u exceeds limit %u", file->kerning.count, GLYPH_VALIDATE_MAX_KERNING);
    }
    if (file->row_count > GLYPH_VALIDATE_MAX_ROWS) {
        (void)glyph_validate_addf(diagnostics, GLYPH_VALIDATE_ERROR, GLYPH_VALIDATE_DIAG_HEADER_LIMITS,
                                  GLYPH_VALIDATE_NO_INDEX, GLYPH_VALIDATE_NO_INDEX,
                                  "row count %u exceeds limit %u", file->row_count, GLYPH_VALIDATE_MAX_ROWS);
    }

    if (file->glyphs.count && !file->glyphs.entries) {
        (void)glyph_validate_addf(diagnostics, GLYPH_VALIDATE_ERROR, GLYPH_VALIDATE_DIAG_POINTERS,
                                  GLYPH_VALIDATE_NO_INDEX, GLYPH_VALIDATE_NO_INDEX,
                                  "glyph table has count %u but entries is NULL", file->glyphs.count);
        usable = 0;
    }
    if (file->kerning.count && !file->kerning.pairs) {
        (void)glyph_validate_addf(diagnostics, GLYPH_VALIDATE_ERROR, GLYPH_VALIDATE_DIAG_POINTERS,
                                  GLYPH_VALIDATE_NO_INDEX, GLYPH_VALIDATE_NO_INDEX,
                                  "kerning table has count %u but pairs is NULL", file->kerning.count);
        usable = 0;
    }
    if (file->row_count && !file->rows) {
        (void)glyph_validate_addf(diagnostics, GLYPH_VALIDATE_ERROR, GLYPH_VALIDATE_DIAG_POINTERS,
                                  GLYPH_VALIDATE_NO_INDEX, GLYPH_VALIDATE_NO_INDEX,
                                  "row table has count %u but rows is NULL", file->row_count);
        usable = 0;
    }
    if (file->glyphs.count && !file->placements) {
        (void)glyph_validate_addf(diagnostics, GLYPH_VALIDATE_ERROR, GLYPH_VALIDATE_DIAG_POINTERS,
                                  GLYPH_VALIDATE_NO_INDEX, GLYPH_VALIDATE_NO_INDEX,
                                  "glyph placements has count %u but placements is NULL", file->glyphs.count);
        usable = 0;
    }
    if ((file->flags & GLYPH_FLAG_HINTS) && file->hints.count != file->glyphs.count) {
        (void)glyph_validate_addf(diagnostics, GLYPH_VALIDATE_ERROR, GLYPH_VALIDATE_DIAG_HINTS,
                                  GLYPH_VALIDATE_NO_INDEX, GLYPH_VALIDATE_NO_INDEX,
                                  "hint count %u does not match glyph count %u", file->hints.count, file->glyphs.count);
    }
    if ((file->flags & GLYPH_FLAG_HINTS) && file->hints.count && !file->hints.programs) {
        (void)glyph_validate_addf(diagnostics, GLYPH_VALIDATE_ERROR, GLYPH_VALIDATE_DIAG_POINTERS,
                                  GLYPH_VALIDATE_NO_INDEX, GLYPH_VALIDATE_NO_INDEX,
                                  "hint table has count %u but programs is NULL", file->hints.count);
    }
    if (!(file->flags & GLYPH_FLAG_HINTS) && file->hints.count) {
        (void)glyph_validate_addf(diagnostics, GLYPH_VALIDATE_WARNING, GLYPH_VALIDATE_DIAG_HINTS,
                                  GLYPH_VALIDATE_NO_INDEX, GLYPH_VALIDATE_NO_INDEX,
                                  "hint table is populated but GLYPH_FLAG_HINTS is not set");
    }
    if (!glyph_validate_has_atlas_size(file->atlas_width, file->atlas_height, &atlas_size)) {
        (void)glyph_validate_addf(diagnostics, GLYPH_VALIDATE_ERROR, GLYPH_VALIDATE_DIAG_ATLAS_DIMENSIONS,
                                  GLYPH_VALIDATE_NO_INDEX, GLYPH_VALIDATE_NO_INDEX,
                                  "atlas dimensions %ux%u are invalid or overflow addressable memory",
                                  file->atlas_width, file->atlas_height);
        usable = 0;
    } else if (!file->atlas_pixels) {
        (void)glyph_validate_addf(diagnostics, GLYPH_VALIDATE_ERROR, GLYPH_VALIDATE_DIAG_POINTERS,
                                  GLYPH_VALIDATE_NO_INDEX, GLYPH_VALIDATE_NO_INDEX,
                                  "atlas pixels is NULL for %zu-byte atlas", atlas_size);
    }

    return usable;
}

static void glyph_validate_rows(const GlyphFile *file, GlyphValidationDiagnostics *diagnostics) {
    uint32_t i;
    uint64_t previous_bottom = 0;

    if (!file || !file->rows) {
        return;
    }

    for (i = 0; i < file->row_count; i++) {
        const RowDescriptor *row = &file->rows[i];
        uint64_t bottom = (uint64_t)row->y + (uint64_t)row->height;
        if (row->height == 0) {
            (void)glyph_validate_addf(diagnostics, GLYPH_VALIDATE_ERROR, GLYPH_VALIDATE_DIAG_ROW_METADATA,
                                      i, GLYPH_VALIDATE_NO_INDEX, "row %u has zero height", i);
        }
        if (row->used > file->atlas_width) {
            (void)glyph_validate_addf(diagnostics, GLYPH_VALIDATE_ERROR, GLYPH_VALIDATE_DIAG_ROW_METADATA,
                                      i, GLYPH_VALIDATE_NO_INDEX, "row %u used width %u exceeds atlas width %u",
                                      i, row->used, file->atlas_width);
        }
        if (bottom > file->atlas_height || bottom < row->y) {
            (void)glyph_validate_addf(diagnostics, GLYPH_VALIDATE_ERROR, GLYPH_VALIDATE_DIAG_ROW_METADATA,
                                      i, GLYPH_VALIDATE_NO_INDEX, "row %u bounds [%u, %" PRIu64 ") exceed atlas height %u",
                                      i, row->y, bottom, file->atlas_height);
        }
        if (i > 0) {
            if ((uint64_t)row->y < previous_bottom) {
                (void)glyph_validate_addf(diagnostics, GLYPH_VALIDATE_ERROR, GLYPH_VALIDATE_DIAG_ROW_METADATA,
                                          i, i - 1u, "row %u starts at %u before previous row ends at %" PRIu64,
                                          i, row->y, previous_bottom);
            } else if ((uint64_t)row->y > previous_bottom) {
                (void)glyph_validate_addf(diagnostics, GLYPH_VALIDATE_WARNING, GLYPH_VALIDATE_DIAG_ROW_METADATA,
                                          i, i - 1u, "row %u starts at %u leaving a vertical gap after %" PRIu64,
                                          i, row->y, previous_bottom);
            }
        } else if (row->y != 0) {
            (void)glyph_validate_addf(diagnostics, GLYPH_VALIDATE_WARNING, GLYPH_VALIDATE_DIAG_ROW_METADATA,
                                      i, GLYPH_VALIDATE_NO_INDEX, "first row starts at y=%u instead of 0", row->y);
        }
        previous_bottom = bottom;
    }
}

static void glyph_validate_glyphs(const GlyphFile *file,
                                  GlyphValidationDiagnostics *diagnostics,
                                  GlyphValidationReport *report) {
    uint32_t i;
    uint32_t j;

    if (!file || !file->glyphs.entries || !file->placements) {
        return;
    }

    for (i = 0; i < file->glyphs.count; i++) {
        const GlyphEntry *glyph = &file->glyphs.entries[i];
        const GlyphPlacement *placement = &file->placements[i];
        uint64_t expected_offset;
        int row_index;

        if (glyph->width == 0 || glyph->height == 0) {
            (void)glyph_validate_addf(diagnostics, GLYPH_VALIDATE_ERROR, GLYPH_VALIDATE_DIAG_PLACEMENT_BOUNDS,
                                      i, GLYPH_VALIDATE_NO_INDEX, "glyph %u has empty dimensions %ux%u",
                                      i, (unsigned int)glyph->width, (unsigned int)glyph->height);
        }
        if (glyph->width > 2048 || glyph->height > 2048) {
            (void)glyph_validate_addf(diagnostics, GLYPH_VALIDATE_ERROR, GLYPH_VALIDATE_DIAG_HEADER_LIMITS,
                                      i, GLYPH_VALIDATE_NO_INDEX, "glyph %u dimensions %ux%u exceed 2048 limit",
                                      i, (unsigned int)glyph->width, (unsigned int)glyph->height);
        }
        if (!glyph_validate_rect_in_atlas(file, i)) {
            (void)glyph_validate_addf(diagnostics, GLYPH_VALIDATE_ERROR, GLYPH_VALIDATE_DIAG_PLACEMENT_BOUNDS,
                                      i, GLYPH_VALIDATE_NO_INDEX, "glyph %u placement (%u,%u) with size %ux%u exceeds atlas %ux%u",
                                      i, placement->x, placement->y,
                                      (unsigned int)glyph->width, (unsigned int)glyph->height,
                                      file->atlas_width, file->atlas_height);
        }

        expected_offset = (uint64_t)placement->y * (uint64_t)file->atlas_width + (uint64_t)placement->x;
        if (expected_offset > UINT32_MAX || glyph->bitmap_offset != (uint32_t)expected_offset) {
            (void)glyph_validate_addf(diagnostics, GLYPH_VALIDATE_ERROR, GLYPH_VALIDATE_DIAG_BITMAP_OFFSET,
                                      i, GLYPH_VALIDATE_NO_INDEX, "glyph %u bitmap_offset %u does not match placement offset %" PRIu64,
                                      i, glyph->bitmap_offset, expected_offset);
        }

        row_index = glyph_validate_find_row(file, placement->y);
        if (row_index < 0) {
            (void)glyph_validate_addf(diagnostics, GLYPH_VALIDATE_ERROR, GLYPH_VALIDATE_DIAG_ROW_METADATA,
                                      i, GLYPH_VALIDATE_NO_INDEX, "glyph %u placement y=%u is not covered by any row",
                                      i, placement->y);
        } else {
            const RowDescriptor *row = &file->rows[row_index];
            uint64_t glyph_right = (uint64_t)placement->x + (uint64_t)glyph->width;
            uint64_t glyph_bottom = (uint64_t)placement->y + (uint64_t)glyph->height;
            uint64_t row_bottom = (uint64_t)row->y + (uint64_t)row->height;
            if (placement->y != row->y) {
                (void)glyph_validate_addf(diagnostics, GLYPH_VALIDATE_WARNING, GLYPH_VALIDATE_DIAG_ROW_METADATA,
                                          i, (uint32_t)row_index, "glyph %u starts at y=%u inside row %d instead of row y=%u",
                                          i, placement->y, row_index, row->y);
            }
            if (glyph_right > row->used || glyph_bottom > row_bottom) {
                (void)glyph_validate_addf(diagnostics, GLYPH_VALIDATE_ERROR, GLYPH_VALIDATE_DIAG_ROW_METADATA,
                                          i, (uint32_t)row_index, "glyph %u is outside row %d used/bounds metadata",
                                          i, row_index);
            }
        }
    }

    for (i = 0; i < file->glyphs.count; i++) {
        for (j = i + 1u; j < file->glyphs.count; j++) {
            if (file->glyphs.entries[i].id == file->glyphs.entries[j].id) {
                report->duplicate_glyph_ids++;
                (void)glyph_validate_addf(diagnostics, GLYPH_VALIDATE_ERROR, GLYPH_VALIDATE_DIAG_DUPLICATE_GLYPH_ID,
                                          i, j, "glyph id %u appears at indices %u and %u",
                                          file->glyphs.entries[i].id, i, j);
            }
            if (glyph_validate_rect_in_atlas(file, i) &&
                glyph_validate_rect_in_atlas(file, j) &&
                glyph_validate_rects_overlap(file, i, j)) {
                report->overlapping_glyph_pairs++;
                report->occupancy.overlapping_pixels += glyph_validate_overlap_area(file, i, j);
                (void)glyph_validate_addf(diagnostics, GLYPH_VALIDATE_ERROR, GLYPH_VALIDATE_DIAG_GLYPH_OVERLAP,
                                          i, j, "glyphs %u and %u overlap in the atlas", i, j);
            }
        }
    }
}

static void glyph_validate_kerning(const GlyphFile *file,
                                   GlyphValidationDiagnostics *diagnostics,
                                   GlyphValidationReport *report) {
    uint32_t i;
    GlyphKerningSortKey *keys;

    if (!file || !file->kerning.pairs) {
        return;
    }

    for (i = 0; i < file->kerning.count; i++) {
        const KerningPair *pair = &file->kerning.pairs[i];
        if (pair->left_index >= file->glyphs.count || pair->right_index >= file->glyphs.count) {
            report->invalid_kerning_pairs++;
            (void)glyph_validate_addf(diagnostics, GLYPH_VALIDATE_ERROR, GLYPH_VALIDATE_DIAG_KERNING_INDEX,
                                      i, GLYPH_VALIDATE_NO_INDEX, "kerning pair %u indexes (%u,%u) outside glyph count %u",
                                      i, pair->left_index, pair->right_index, file->glyphs.count);
        }
    }

    keys = file->kerning.count ? (GlyphKerningSortKey *)malloc((size_t)file->kerning.count * sizeof(*keys)) : NULL;
    if (file->kerning.count && !keys) {
        (void)glyph_validate_addf(diagnostics, GLYPH_VALIDATE_ERROR, GLYPH_VALIDATE_DIAG_STATISTICS,
                                  GLYPH_VALIDATE_NO_INDEX, GLYPH_VALIDATE_NO_INDEX,
                                  "unable to allocate kerning duplicate detection workspace");
        return;
    }
    for (i = 0; i < file->kerning.count; i++) {
        keys[i].left_index = file->kerning.pairs[i].left_index;
        keys[i].right_index = file->kerning.pairs[i].right_index;
        keys[i].source_index = i;
    }
    qsort(keys, file->kerning.count, sizeof(*keys), glyph_validate_kerning_key_cmp);
    for (i = 1; i < file->kerning.count; i++) {
        if (keys[i - 1u].left_index == keys[i].left_index &&
            keys[i - 1u].right_index == keys[i].right_index) {
            report->duplicate_kerning_pairs++;
            (void)glyph_validate_addf(diagnostics, GLYPH_VALIDATE_ERROR, GLYPH_VALIDATE_DIAG_DUPLICATE_KERNING_PAIR,
                                      keys[i - 1u].source_index, keys[i].source_index,
                                      "kerning pair (%u,%u) appears at indices %u and %u",
                                      keys[i].left_index, keys[i].right_index,
                                      keys[i - 1u].source_index, keys[i].source_index);
        }
    }
    free(keys);
}

static void glyph_validate_hints(const GlyphFile *file, GlyphValidationDiagnostics *diagnostics) {
    uint32_t i;

    if (!file || !file->hints.programs) {
        return;
    }

    for (i = 0; i < file->hints.count; i++) {
        const HintProgram *program = &file->hints.programs[i];
        if (program->length && !program->bytes) {
            (void)glyph_validate_addf(diagnostics, GLYPH_VALIDATE_ERROR, GLYPH_VALIDATE_DIAG_HINTS,
                                      i, GLYPH_VALIDATE_NO_INDEX, "hint program %u has length %u but bytes is NULL",
                                      i, (unsigned int)program->length);
        }
        if (!program->length && program->bytes) {
            (void)glyph_validate_addf(diagnostics, GLYPH_VALIDATE_WARNING, GLYPH_VALIDATE_DIAG_HINTS,
                                      i, GLYPH_VALIDATE_NO_INDEX, "hint program %u has bytes but zero length", i);
        }
    }
}

void glyph_validation_diagnostics_init(GlyphValidationDiagnostics *diagnostics) {
    if (diagnostics) {
        memset(diagnostics, 0, sizeof(*diagnostics));
    }
}

void glyph_validation_diagnostics_free(GlyphValidationDiagnostics *diagnostics) {
    uint32_t i;
    if (!diagnostics) {
        return;
    }
    for (i = 0; i < diagnostics->count; i++) {
        free(diagnostics->items[i].message);
    }
    free(diagnostics->items);
    memset(diagnostics, 0, sizeof(*diagnostics));
}

int glyph_validation_diagnostics_add(GlyphValidationDiagnostics *diagnostics,
                                     GlyphValidationSeverity severity,
                                     GlyphValidationCode code,
                                     uint32_t index,
                                     uint32_t related_index,
                                     const char *message) {
    GlyphValidationDiagnostic *next;
    char *copy;
    uint32_t capacity;

    if (!diagnostics) {
        return 1;
    }
    if (diagnostics->count == diagnostics->capacity) {
        capacity = diagnostics->capacity ? diagnostics->capacity * 2u : 16u;
        if (capacity < diagnostics->capacity) {
            return 0;
        }
        next = (GlyphValidationDiagnostic *)realloc(diagnostics->items, (size_t)capacity * sizeof(*next));
        if (!next) {
            return 0;
        }
        diagnostics->items = next;
        diagnostics->capacity = capacity;
    }

    copy = glyph_validate_copy_string(message ? message : "");
    if (!copy) {
        return 0;
    }

    diagnostics->items[diagnostics->count].severity = severity;
    diagnostics->items[diagnostics->count].code = code;
    diagnostics->items[diagnostics->count].index = index;
    diagnostics->items[diagnostics->count].related_index = related_index;
    diagnostics->items[diagnostics->count].message = copy;
    diagnostics->count++;
    return 1;
}

void glyph_validation_report_init(GlyphValidationReport *report) {
    if (report) {
        memset(report, 0, sizeof(*report));
        report->valid = 1;
    }
}

int glyph_validate_file(const GlyphFile *file,
                        GlyphValidationDiagnostics *diagnostics,
                        GlyphValidationReport *report) {
    GlyphValidationDiagnostics local_diagnostics;
    GlyphValidationReport local_report;
    int usable;

    if (!report) {
        report = &local_report;
    }
    if (!diagnostics) {
        glyph_validation_diagnostics_init(&local_diagnostics);
        diagnostics = &local_diagnostics;
    }
    glyph_validation_report_init(report);

    usable = glyph_validate_header_and_pointers(file, diagnostics, report);
    if (file) {
        glyph_validate_hint_summary(file, report);
        glyph_validate_row_stats(file, report);
        glyph_validate_occupancy_stats(file, report);
    }
    if (usable) {
        glyph_validate_rows(file, diagnostics);
        glyph_validate_glyphs(file, diagnostics, report);
        glyph_validate_kerning(file, diagnostics, report);
        glyph_validate_hints(file, diagnostics);
    }
    glyph_validate_count_diagnostics(diagnostics, report);
    if (diagnostics == &local_diagnostics) {
        glyph_validation_diagnostics_free(&local_diagnostics);
    }
    return report->valid;
}

const char *glyph_validation_severity_name(GlyphValidationSeverity severity) {
    switch (severity) {
    case GLYPH_VALIDATE_INFO:
        return "info";
    case GLYPH_VALIDATE_WARNING:
        return "warning";
    case GLYPH_VALIDATE_ERROR:
        return "error";
    default:
        return "unknown";
    }
}

const char *glyph_validation_code_name(GlyphValidationCode code) {
    switch (code) {
    case GLYPH_VALIDATE_DIAG_HEADER_LIMITS:
        return "header/limits";
    case GLYPH_VALIDATE_DIAG_POINTERS:
        return "pointers";
    case GLYPH_VALIDATE_DIAG_ATLAS_DIMENSIONS:
        return "atlas dimensions";
    case GLYPH_VALIDATE_DIAG_ROW_METADATA:
        return "row metadata";
    case GLYPH_VALIDATE_DIAG_PLACEMENT_BOUNDS:
        return "placement bounds";
    case GLYPH_VALIDATE_DIAG_BITMAP_OFFSET:
        return "bitmap offset";
    case GLYPH_VALIDATE_DIAG_DUPLICATE_GLYPH_ID:
        return "duplicate glyph id";
    case GLYPH_VALIDATE_DIAG_GLYPH_OVERLAP:
        return "glyph overlap";
    case GLYPH_VALIDATE_DIAG_KERNING_INDEX:
        return "kerning index";
    case GLYPH_VALIDATE_DIAG_DUPLICATE_KERNING_PAIR:
        return "duplicate kerning pair";
    case GLYPH_VALIDATE_DIAG_HINTS:
        return "hints";
    case GLYPH_VALIDATE_DIAG_STATISTICS:
        return "statistics";
    default:
        return "unknown";
    }
}

int glyph_validation_write_text_report(FILE *fp,
                                       const GlyphValidationReport *report,
                                       const GlyphValidationDiagnostics *diagnostics) {
    uint32_t i;

    if (!fp || !report) {
        return 0;
    }

    fprintf(fp, "Glyph validation report\n");
    fprintf(fp, "status: %s\n", report->valid ? "valid" : "invalid");
    fprintf(fp, "diagnostics: %u error(s), %u warning(s), %u info\n",
            report->error_count, report->warning_count, report->info_count);
    fprintf(fp, "counts: %u glyph(s), %u kerning pair(s), %u hint(s), %u row(s)\n",
            report->glyph_count, report->kerning_count, report->hint_count, report->row_count);
    fprintf(fp, "atlas: %ux%u (%" PRIu64 " pixel(s))\n",
            report->atlas_width, report->atlas_height, report->occupancy.atlas_pixels);
    fprintf(fp, "occupancy: glyph pixels=%" PRIu64 ", nonzero pixels=%" PRIu64
                ", overlap pixels=%" PRIu64 ", glyph coverage=%.4f, ink coverage=%.4f\n",
            report->occupancy.glyph_pixels, report->occupancy.nonzero_pixels,
            report->occupancy.overlapping_pixels,
            report->occupancy.glyph_coverage, report->occupancy.ink_coverage);
    fprintf(fp, "rows: used=%" PRIu64 ", waste=%" PRIu64 ", waste ratio=%.4f, max waste row=%u (%u pixel(s))\n",
            report->rows.used_pixels, report->rows.waste_pixels, report->rows.waste_ratio,
            report->rows.max_waste_row, report->rows.max_waste_pixels);
    fprintf(fp, "hints: present=%u, empty=%u, total bytes=%" PRIu64 ", min=%u, max=%u, average=%.2f\n",
            report->hints.present_count, report->hints.empty_count, report->hints.total_bytes,
            (unsigned int)report->hints.min_length, (unsigned int)report->hints.max_length,
            report->hints.average_length);
    fprintf(fp, "duplicates/overlaps: glyph ids=%u, glyph overlaps=%u, invalid kerning=%u, duplicate kerning=%u\n",
            report->duplicate_glyph_ids, report->overlapping_glyph_pairs,
            report->invalid_kerning_pairs, report->duplicate_kerning_pairs);

    if (diagnostics && diagnostics->count) {
        fprintf(fp, "\ndiagnostics:\n");
        for (i = 0; i < diagnostics->count; i++) {
            const GlyphValidationDiagnostic *diag = &diagnostics->items[i];
            fprintf(fp, "- [%s] %s", glyph_validation_severity_name(diag->severity),
                    glyph_validation_code_name(diag->code));
            if (diag->index != GLYPH_VALIDATE_NO_INDEX) {
                fprintf(fp, " index=%u", diag->index);
            }
            if (diag->related_index != GLYPH_VALIDATE_NO_INDEX) {
                fprintf(fp, " related=%u", diag->related_index);
            }
            fprintf(fp, ": %s\n", diag->message ? diag->message : "");
        }
    }

    return ferror(fp) == 0;
}
