#include "metrics.h"

#include <inttypes.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t id;
    uint32_t index;
} GlyphMetricsIdIndex;

static int glyph_metrics_mul_size(size_t a, size_t b, size_t *out) {
    if (a != 0 && b > SIZE_MAX / a) {
        return 0;
    }
    *out = a * b;
    return 1;
}

static int glyph_metrics_atlas_size(const GlyphFile *file, size_t *size) {
    uint64_t pixels;
    if (!file || file->atlas_width == 0 || file->atlas_height == 0) {
        return 0;
    }
    pixels = (uint64_t)file->atlas_width * (uint64_t)file->atlas_height;
    if (pixels > (uint64_t)SIZE_MAX) {
        return 0;
    }
    *size = (size_t)pixels;
    return 1;
}

static uint64_t glyph_metrics_area(const GlyphEntry *glyph) {
    return (uint64_t)glyph->width * (uint64_t)glyph->height;
}

static int glyph_metrics_rect_intersects_atlas(const GlyphFile *file, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    uint64_t right;
    uint64_t bottom;
    if (!file || width == 0 || height == 0) {
        return 0;
    }
    right = (uint64_t)x + (uint64_t)width;
    bottom = (uint64_t)y + (uint64_t)height;
    return x < file->atlas_width && y < file->atlas_height && right > 0 && bottom > 0;
}

static uint64_t glyph_metrics_count_rect_ink(const GlyphFile *file, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    uint32_t yy;
    uint32_t y_end;
    uint32_t x_end;
    uint64_t ink = 0;
    size_t atlas_size = 0;

    if (!file || !file->atlas_pixels || !glyph_metrics_atlas_size(file, &atlas_size) ||
        !glyph_metrics_rect_intersects_atlas(file, x, y, width, height)) {
        return 0;
    }

    x_end = (uint64_t)x + (uint64_t)width > file->atlas_width ? file->atlas_width : x + width;
    y_end = (uint64_t)y + (uint64_t)height > file->atlas_height ? file->atlas_height : y + height;
    for (yy = y; yy < y_end; yy++) {
        uint32_t xx;
        uint64_t row_offset = (uint64_t)yy * (uint64_t)file->atlas_width;
        for (xx = x; xx < x_end; xx++) {
            uint64_t offset = row_offset + (uint64_t)xx;
            if (offset < (uint64_t)atlas_size && file->atlas_pixels[offset]) {
                ink++;
            }
        }
    }
    return ink;
}

static uint64_t glyph_metrics_count_atlas_ink(const GlyphFile *file) {
    uint64_t ink = 0;
    size_t atlas_size = 0;
    size_t i;

    if (!file || !file->atlas_pixels || !glyph_metrics_atlas_size(file, &atlas_size)) {
        return 0;
    }
    for (i = 0; i < atlas_size; i++) {
        if (file->atlas_pixels[i]) {
            ink++;
        }
    }
    return ink;
}

static int glyph_metrics_id_index_cmp(const void *lhs, const void *rhs) {
    const GlyphMetricsIdIndex *a = (const GlyphMetricsIdIndex *)lhs;
    const GlyphMetricsIdIndex *b = (const GlyphMetricsIdIndex *)rhs;
    if (a->id != b->id) {
        return a->id < b->id ? -1 : 1;
    }
    if (a->index != b->index) {
        return a->index < b->index ? -1 : 1;
    }
    return 0;
}

static int glyph_metrics_u32_cmp(const void *lhs, const void *rhs) {
    uint32_t a = *(const uint32_t *)lhs;
    uint32_t b = *(const uint32_t *)rhs;
    if (a != b) {
        return a < b ? -1 : 1;
    }
    return 0;
}

static int glyph_metrics_find_sorted_id(const GlyphMetricsIdIndex *items, uint32_t count, uint32_t id, uint32_t *index) {
    uint32_t lo = 0;
    uint32_t hi = count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (items[mid].id < id) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    if (lo < count && items[lo].id == id) {
        if (index) {
            *index = items[lo].index;
        }
        return 1;
    }
    return 0;
}

static int glyph_metrics_make_id_index(const GlyphTable *table, GlyphMetricsIdIndex **out_items) {
    GlyphMetricsIdIndex *items;
    size_t bytes;
    uint32_t i;

    *out_items = NULL;
    if (!table || (table->count && !table->entries)) {
        return 0;
    }
    if (table->count == 0) {
        return 1;
    }
    if (!glyph_metrics_mul_size((size_t)table->count, sizeof(*items), &bytes)) {
        return 0;
    }
    items = (GlyphMetricsIdIndex *)malloc(bytes);
    if (!items) {
        return 0;
    }
    for (i = 0; i < table->count; i++) {
        items[i].id = table->entries[i].id;
        items[i].index = i;
    }
    qsort(items, table->count, sizeof(*items), glyph_metrics_id_index_cmp);
    *out_items = items;
    return 1;
}

static int glyph_metrics_glyph_changed(const GlyphFile *before, uint32_t before_index, const GlyphFile *after, uint32_t after_index) {
    const GlyphEntry *a = &before->glyphs.entries[before_index];
    const GlyphEntry *b = &after->glyphs.entries[after_index];
    if (a->x != b->x || a->y != b->y || a->width != b->width || a->height != b->height ||
        a->advance != b->advance || a->bitmap_offset != b->bitmap_offset) {
        return 1;
    }
    if (before->placements && after->placements) {
        const GlyphPlacement *pa = &before->placements[before_index];
        const GlyphPlacement *pb = &after->placements[after_index];
        if (pa->x != pb->x || pa->y != pb->y) {
            return 1;
        }
    }
    return 0;
}

void glyph_metrics_area_histogram_free(GlyphAreaHistogram *histogram) {
    if (histogram) {
        free(histogram->buckets);
        memset(histogram, 0, sizeof(*histogram));
    }
}

void glyph_metrics_atlas_ink_density_free(GlyphAtlasInkDensity *density) {
    if (density) {
        free(density->rows);
        free(density->glyphs);
        memset(density, 0, sizeof(*density));
    }
}

void glyph_metrics_kerning_degree_stats_free(GlyphKerningDegreeStats *stats) {
    if (stats) {
        free(stats->in_degree);
        free(stats->out_degree);
        memset(stats, 0, sizeof(*stats));
    }
}

void glyph_metrics_id_range_stats_free(GlyphIdRangeStats *stats) {
    if (stats) {
        free(stats->gaps);
        memset(stats, 0, sizeof(*stats));
    }
}

void glyph_metrics_records_free(GlyphMetricsRecord *records) {
    free(records);
}

void glyph_metrics_report_free(GlyphMetricsReport *report) {
    if (report) {
        glyph_metrics_area_histogram_free(&report->area_histogram);
        glyph_metrics_atlas_ink_density_free(&report->atlas_density);
        glyph_metrics_kerning_degree_stats_free(&report->kerning_degrees);
        glyph_metrics_id_range_stats_free(&report->id_range);
        free(report->glyphs);
        memset(report, 0, sizeof(*report));
    }
}

int glyph_metrics_compute_bounds(const GlyphTable *table, GlyphTableBounds *bounds) {
    uint32_t i;

    if (!table || !bounds || (table->count && !table->entries)) {
        return 0;
    }
    memset(bounds, 0, sizeof(*bounds));
    bounds->count = table->count;
    if (table->count == 0) {
        return 1;
    }

    bounds->has_bounds = 1;
    bounds->min_width = UINT16_MAX;
    bounds->min_height = UINT16_MAX;
    for (i = 0; i < table->count; i++) {
        const GlyphEntry *glyph = &table->entries[i];
        int32_t left = glyph->x;
        int32_t top = glyph->y;
        int32_t right = (int32_t)glyph->x + (int32_t)glyph->width;
        int32_t bottom = (int32_t)glyph->y + (int32_t)glyph->height;

        if (i == 0 || left < bounds->min_x) {
            bounds->min_x = left;
        }
        if (i == 0 || top < bounds->min_y) {
            bounds->min_y = top;
        }
        if (i == 0 || right > bounds->max_x) {
            bounds->max_x = right;
        }
        if (i == 0 || bottom > bounds->max_y) {
            bounds->max_y = bottom;
        }
        if (glyph->width < bounds->min_width) {
            bounds->min_width = glyph->width;
        }
        if (glyph->width > bounds->max_width) {
            bounds->max_width = glyph->width;
        }
        if (glyph->height < bounds->min_height) {
            bounds->min_height = glyph->height;
        }
        if (glyph->height > bounds->max_height) {
            bounds->max_height = glyph->height;
        }
        bounds->total_area += glyph_metrics_area(glyph);
    }
    return 1;
}

int glyph_metrics_compute_advance_stats(const GlyphTable *table, GlyphAdvanceStats *stats) {
    uint32_t i;
    double sum = 0.0;
    double sum_sq = 0.0;

    if (!table || !stats || (table->count && !table->entries)) {
        return 0;
    }
    memset(stats, 0, sizeof(*stats));
    stats->count = table->count;
    if (table->count == 0) {
        return 1;
    }

    for (i = 0; i < table->count; i++) {
        int16_t advance = table->entries[i].advance;
        double value = (double)advance;
        if (i == 0 || advance < stats->min_advance) {
            stats->min_advance = advance;
        }
        if (i == 0 || advance > stats->max_advance) {
            stats->max_advance = advance;
        }
        stats->total_advance += advance;
        sum += value;
        sum_sq += value * value;
        if (advance < 0) {
            stats->negative_count++;
        } else if (advance == 0) {
            stats->zero_count++;
        } else {
            stats->positive_count++;
        }
    }
    stats->mean_advance = sum / (double)table->count;
    stats->variance = (sum_sq / (double)table->count) - (stats->mean_advance * stats->mean_advance);
    if (stats->variance < 0.0 && stats->variance > -0.000001) {
        stats->variance = 0.0;
    }
    return 1;
}

int glyph_metrics_compute_area_histogram(const GlyphTable *table, uint32_t bucket_size, GlyphAreaHistogram *histogram) {
    uint32_t i;
    uint32_t bucket_count;
    size_t bytes;

    if (!table || !histogram || bucket_size == 0 || (table->count && !table->entries)) {
        return 0;
    }
    memset(histogram, 0, sizeof(*histogram));
    histogram->bucket_size = bucket_size;
    for (i = 0; i < table->count; i++) {
        uint64_t area = glyph_metrics_area(&table->entries[i]);
        histogram->total_area += area;
        if (area > histogram->max_area) {
            histogram->max_area = area > UINT32_MAX ? UINT32_MAX : (uint32_t)area;
        }
    }
    bucket_count = histogram->max_area / bucket_size + 1u;
    if (bucket_count == 0) {
        return 0;
    }
    if (!glyph_metrics_mul_size((size_t)bucket_count, sizeof(*histogram->buckets), &bytes)) {
        return 0;
    }
    histogram->buckets = (uint32_t *)calloc(1u, bytes);
    if (!histogram->buckets) {
        memset(histogram, 0, sizeof(*histogram));
        return 0;
    }
    histogram->bucket_count = bucket_count;
    for (i = 0; i < table->count; i++) {
        uint64_t area = glyph_metrics_area(&table->entries[i]);
        uint32_t bucket = area > UINT32_MAX ? bucket_count - 1u : (uint32_t)area / bucket_size;
        if (bucket >= bucket_count) {
            bucket = bucket_count - 1u;
        }
        histogram->buckets[bucket]++;
    }
    return 1;
}

int glyph_metrics_compute_atlas_ink_density(const GlyphFile *file, GlyphAtlasInkDensity *density) {
    uint32_t i;
    size_t bytes;
    size_t atlas_size = 0;

    if (!file || !density ||
        (file->glyphs.count && (!file->glyphs.entries || !file->placements)) ||
        (file->row_count && !file->rows) ||
        !glyph_metrics_atlas_size(file, &atlas_size)) {
        return 0;
    }
    memset(density, 0, sizeof(*density));
    density->atlas_width = file->atlas_width;
    density->atlas_height = file->atlas_height;
    density->atlas_pixels = (uint64_t)atlas_size;
    density->ink_pixels = glyph_metrics_count_atlas_ink(file);
    if (density->atlas_pixels) {
        density->density = (double)density->ink_pixels / (double)density->atlas_pixels;
    }

    density->row_count = file->row_count;
    if (file->row_count) {
        if (!glyph_metrics_mul_size((size_t)file->row_count, sizeof(*density->rows), &bytes)) {
            return 0;
        }
        density->rows = (GlyphRowInkDensity *)calloc(1u, bytes);
        if (!density->rows) {
            glyph_metrics_atlas_ink_density_free(density);
            return 0;
        }
        for (i = 0; i < file->row_count; i++) {
            const RowDescriptor *row = &file->rows[i];
            GlyphRowInkDensity *out = &density->rows[i];
            uint32_t used = row->used > file->atlas_width ? file->atlas_width : row->used;
            out->row_index = i;
            out->y = row->y;
            out->height = row->height;
            out->used = row->used;
            out->pixels = (uint64_t)used * (uint64_t)row->height;
            out->ink_pixels = glyph_metrics_count_rect_ink(file, 0, row->y, used, row->height);
            if (out->pixels) {
                out->density = (double)out->ink_pixels / (double)out->pixels;
            }
        }
    }

    density->glyph_count = file->glyphs.count;
    if (file->glyphs.count) {
        if (!glyph_metrics_mul_size((size_t)file->glyphs.count, sizeof(*density->glyphs), &bytes)) {
            glyph_metrics_atlas_ink_density_free(density);
            return 0;
        }
        density->glyphs = (GlyphInkDensity *)calloc(1u, bytes);
        if (!density->glyphs) {
            glyph_metrics_atlas_ink_density_free(density);
            return 0;
        }
        for (i = 0; i < file->glyphs.count; i++) {
            const GlyphEntry *glyph = &file->glyphs.entries[i];
            const GlyphPlacement *placement = &file->placements[i];
            GlyphInkDensity *out = &density->glyphs[i];
            out->glyph_index = i;
            out->glyph_id = glyph->id;
            out->atlas_x = placement->x;
            out->atlas_y = placement->y;
            out->width = glyph->width;
            out->height = glyph->height;
            out->pixels = glyph_metrics_area(glyph);
            out->ink_pixels = glyph_metrics_count_rect_ink(file, placement->x, placement->y, glyph->width, glyph->height);
            if (out->pixels) {
                out->density = (double)out->ink_pixels / (double)out->pixels;
            }
        }
    }
    return 1;
}

int glyph_metrics_compute_kerning_degree_stats(const GlyphFile *file, GlyphKerningDegreeStats *stats) {
    uint32_t i;
    size_t bytes;

    if (!file || !stats ||
        (file->glyphs.count && !file->glyphs.entries) ||
        (file->kerning.count && !file->kerning.pairs)) {
        return 0;
    }
    memset(stats, 0, sizeof(*stats));
    stats->glyph_count = file->glyphs.count;
    stats->pair_count = file->kerning.count;
    if (file->glyphs.count == 0) {
        stats->invalid_pair_count = file->kerning.count;
        return file->kerning.count == 0;
    }

    if (!glyph_metrics_mul_size((size_t)file->glyphs.count, sizeof(*stats->in_degree), &bytes)) {
        return 0;
    }
    stats->in_degree = (uint32_t *)calloc(1u, bytes);
    if (!stats->in_degree) {
        return 0;
    }
    stats->out_degree = (uint32_t *)calloc(1u, bytes);
    if (!stats->out_degree) {
        glyph_metrics_kerning_degree_stats_free(stats);
        return 0;
    }

    for (i = 0; i < file->kerning.count; i++) {
        const KerningPair *pair = &file->kerning.pairs[i];
        if (pair->left_index < file->glyphs.count && pair->right_index < file->glyphs.count) {
            stats->out_degree[pair->left_index]++;
            stats->in_degree[pair->right_index]++;
            stats->valid_pair_count++;
        } else {
            stats->invalid_pair_count++;
        }
    }

    for (i = 0; i < file->glyphs.count; i++) {
        uint32_t in_degree = stats->in_degree[i];
        uint32_t out_degree = stats->out_degree[i];
        uint32_t total_degree = in_degree + out_degree;
        if (in_degree > stats->max_in_degree) {
            stats->max_in_degree = in_degree;
            stats->max_in_degree_index = i;
        }
        if (out_degree > stats->max_out_degree) {
            stats->max_out_degree = out_degree;
            stats->max_out_degree_index = i;
        }
        if (total_degree > stats->max_total_degree) {
            stats->max_total_degree = total_degree;
            stats->max_total_degree_index = i;
        }
    }
    stats->mean_in_degree = (double)stats->valid_pair_count / (double)file->glyphs.count;
    stats->mean_out_degree = (double)stats->valid_pair_count / (double)file->glyphs.count;
    stats->mean_total_degree = (double)(stats->valid_pair_count * 2u) / (double)file->glyphs.count;
    return 1;
}

int glyph_metrics_compute_id_range(const GlyphTable *table, GlyphIdRangeStats *stats) {
    uint32_t *ids;
    uint32_t i;
    uint32_t unique_count = 0;
    uint32_t gap_capacity = 0;
    size_t bytes;

    if (!table || !stats || (table->count && !table->entries)) {
        return 0;
    }
    memset(stats, 0, sizeof(*stats));
    stats->count = table->count;
    if (table->count == 0) {
        return 1;
    }
    if (!glyph_metrics_mul_size((size_t)table->count, sizeof(*ids), &bytes)) {
        return 0;
    }
    ids = (uint32_t *)malloc(bytes);
    if (!ids) {
        return 0;
    }
    for (i = 0; i < table->count; i++) {
        ids[i] = table->entries[i].id;
    }
    qsort(ids, table->count, sizeof(*ids), glyph_metrics_u32_cmp);
    stats->min_id = ids[0];
    stats->max_id = ids[table->count - 1u];
    stats->span = (uint64_t)stats->max_id - (uint64_t)stats->min_id + 1u;

    for (i = 0; i < table->count; i++) {
        if (i > 0 && ids[i] == ids[i - 1u]) {
            stats->duplicate_count++;
            continue;
        }
        if (unique_count > 0) {
            uint32_t previous = ids[i - 1u];
            if (ids[i] > previous + 1u) {
                GlyphIdGap *next;
                if (stats->gap_count == gap_capacity) {
                    uint32_t next_capacity = gap_capacity ? gap_capacity * 2u : 8u;
                    if (!glyph_metrics_mul_size((size_t)next_capacity, sizeof(*stats->gaps), &bytes)) {
                        free(ids);
                        glyph_metrics_id_range_stats_free(stats);
                        return 0;
                    }
                    next = (GlyphIdGap *)realloc(stats->gaps, bytes);
                    if (!next) {
                        free(ids);
                        glyph_metrics_id_range_stats_free(stats);
                        return 0;
                    }
                    stats->gaps = next;
                    gap_capacity = next_capacity;
                }
                stats->gaps[stats->gap_count].start_id = previous + 1u;
                stats->gaps[stats->gap_count].end_id = ids[i] - 1u;
                stats->gaps[stats->gap_count].count = (uint64_t)ids[i] - (uint64_t)previous - 1u;
                stats->missing_id_count += stats->gaps[stats->gap_count].count;
                stats->gap_count++;
            }
        }
        unique_count++;
    }
    free(ids);
    return 1;
}

int glyph_metrics_collect_records(const GlyphFile *file, GlyphMetricsRecord **records, uint32_t *count) {
    GlyphMetricsRecord *items = NULL;
    GlyphAtlasInkDensity density;
    GlyphKerningDegreeStats degrees;
    uint32_t i;
    size_t bytes;

    if (!file || !records || !count ||
        (file->glyphs.count && (!file->glyphs.entries || !file->placements))) {
        return 0;
    }
    *records = NULL;
    *count = 0;
    memset(&density, 0, sizeof(density));
    memset(&degrees, 0, sizeof(degrees));

    if (!glyph_metrics_compute_atlas_ink_density(file, &density)) {
        return 0;
    }
    if (!glyph_metrics_compute_kerning_degree_stats(file, &degrees)) {
        glyph_metrics_atlas_ink_density_free(&density);
        return 0;
    }
    if (file->glyphs.count) {
        if (!glyph_metrics_mul_size((size_t)file->glyphs.count, sizeof(*items), &bytes)) {
            glyph_metrics_atlas_ink_density_free(&density);
            glyph_metrics_kerning_degree_stats_free(&degrees);
            return 0;
        }
        items = (GlyphMetricsRecord *)calloc(1u, bytes);
        if (!items) {
            glyph_metrics_atlas_ink_density_free(&density);
            glyph_metrics_kerning_degree_stats_free(&degrees);
            return 0;
        }
    }

    for (i = 0; i < file->glyphs.count; i++) {
        const GlyphEntry *glyph = &file->glyphs.entries[i];
        const GlyphPlacement *placement = &file->placements[i];
        GlyphMetricsRecord *record = &items[i];
        record->index = i;
        record->id = glyph->id;
        record->x = glyph->x;
        record->y = glyph->y;
        record->width = glyph->width;
        record->height = glyph->height;
        record->advance = glyph->advance;
        record->bitmap_offset = glyph->bitmap_offset;
        record->atlas_x = placement->x;
        record->atlas_y = placement->y;
        record->area = glyph_metrics_area(glyph);
        record->ink_pixels = density.glyphs ? density.glyphs[i].ink_pixels : 0;
        record->ink_density = density.glyphs ? density.glyphs[i].density : 0.0;
        record->kerning_in_degree = degrees.in_degree ? degrees.in_degree[i] : 0;
        record->kerning_out_degree = degrees.out_degree ? degrees.out_degree[i] : 0;
    }

    glyph_metrics_atlas_ink_density_free(&density);
    glyph_metrics_kerning_degree_stats_free(&degrees);
    *records = items;
    *count = file->glyphs.count;
    return 1;
}

int glyph_metrics_build_report(const GlyphFile *file, GlyphMetricsReport *report) {
    GlyphMetricsReport tmp;

    if (!file || !report) {
        return 0;
    }
    memset(&tmp, 0, sizeof(tmp));
    if (!glyph_metrics_compute_bounds(&file->glyphs, &tmp.bounds) ||
        !glyph_metrics_compute_advance_stats(&file->glyphs, &tmp.advances) ||
        !glyph_metrics_compute_area_histogram(&file->glyphs, 64u, &tmp.area_histogram) ||
        !glyph_metrics_compute_atlas_ink_density(file, &tmp.atlas_density) ||
        !glyph_metrics_compute_kerning_degree_stats(file, &tmp.kerning_degrees) ||
        !glyph_metrics_compute_id_range(&file->glyphs, &tmp.id_range) ||
        !glyph_metrics_collect_records(file, &tmp.glyphs, &tmp.glyph_count)) {
        glyph_metrics_report_free(&tmp);
        return 0;
    }
    *report = tmp;
    return 1;
}

int glyph_metrics_compare_files(const GlyphFile *before, const GlyphFile *after, GlyphMetricsComparison *comparison) {
    GlyphMetricsIdIndex *before_ids = NULL;
    GlyphMetricsIdIndex *after_ids = NULL;
    GlyphAtlasInkDensity before_density;
    GlyphAtlasInkDensity after_density;
    uint32_t i;

    if (!before || !after || !comparison ||
        (before->glyphs.count && !before->glyphs.entries) ||
        (after->glyphs.count && !after->glyphs.entries)) {
        return 0;
    }
    memset(comparison, 0, sizeof(*comparison));
    memset(&before_density, 0, sizeof(before_density));
    memset(&after_density, 0, sizeof(after_density));

    if (!glyph_metrics_make_id_index(&before->glyphs, &before_ids) ||
        !glyph_metrics_make_id_index(&after->glyphs, &after_ids)) {
        free(before_ids);
        free(after_ids);
        return 0;
    }

    comparison->glyph_count_delta = (int32_t)after->glyphs.count - (int32_t)before->glyphs.count;
    comparison->kerning_count_delta = (int32_t)after->kerning.count - (int32_t)before->kerning.count;
    comparison->row_count_delta = (int32_t)after->row_count - (int32_t)before->row_count;
    comparison->atlas_width_delta = (int32_t)after->atlas_width - (int32_t)before->atlas_width;
    comparison->atlas_height_delta = (int32_t)after->atlas_height - (int32_t)before->atlas_height;

    for (i = 0; i < before->glyphs.count; i++) {
        uint32_t after_index;
        const GlyphEntry *glyph = &before->glyphs.entries[i];
        if (glyph_metrics_find_sorted_id(after_ids, after->glyphs.count, glyph->id, &after_index)) {
            const GlyphEntry *other = &after->glyphs.entries[after_index];
            comparison->common_glyphs++;
            comparison->total_advance_delta += (int64_t)other->advance - (int64_t)glyph->advance;
            comparison->total_area_delta += (int64_t)glyph_metrics_area(other) - (int64_t)glyph_metrics_area(glyph);
            if (glyph_metrics_glyph_changed(before, i, after, after_index)) {
                comparison->changed_glyph_metrics++;
            }
        } else {
            comparison->removed_glyphs++;
        }
    }
    for (i = 0; i < after->glyphs.count; i++) {
        uint32_t before_index;
        if (!glyph_metrics_find_sorted_id(before_ids, before->glyphs.count, after->glyphs.entries[i].id, &before_index)) {
            comparison->added_glyphs++;
        }
    }

    if (glyph_metrics_compute_atlas_ink_density(before, &before_density) &&
        glyph_metrics_compute_atlas_ink_density(after, &after_density)) {
        comparison->atlas_density_delta = after_density.density - before_density.density;
    }
    glyph_metrics_atlas_ink_density_free(&before_density);
    glyph_metrics_atlas_ink_density_free(&after_density);
    free(before_ids);
    free(after_ids);
    return 1;
}

int glyph_metrics_write_text_report(FILE *fp, const GlyphMetricsReport *report) {
    uint32_t i;

    if (!fp || !report) {
        return 0;
    }
    fprintf(fp, "Glyph metrics report\n");
    fprintf(fp, "glyphs: %u\n", report->glyph_count);
    fprintf(fp, "bounds: %s", report->bounds.has_bounds ? "present" : "empty");
    if (report->bounds.has_bounds) {
        fprintf(fp, " min=(%" PRId32 ",%" PRId32 ") max=(%" PRId32 ",%" PRId32 ")",
                report->bounds.min_x, report->bounds.min_y, report->bounds.max_x, report->bounds.max_y);
        fprintf(fp, " width=%u..%u height=%u..%u area=%" PRIu64,
                report->bounds.min_width, report->bounds.max_width,
                report->bounds.min_height, report->bounds.max_height,
                report->bounds.total_area);
    }
    fprintf(fp, "\n");
    fprintf(fp, "advances: min=%d max=%d total=%" PRId64 " mean=%.3f variance=%.3f neg=%u zero=%u pos=%u\n",
            report->advances.min_advance, report->advances.max_advance,
            report->advances.total_advance, report->advances.mean_advance,
            report->advances.variance, report->advances.negative_count,
            report->advances.zero_count, report->advances.positive_count);
    fprintf(fp, "atlas: %ux%u pixels=%" PRIu64 " ink=%" PRIu64 " density=%.6f\n",
            report->atlas_density.atlas_width, report->atlas_density.atlas_height,
            report->atlas_density.atlas_pixels, report->atlas_density.ink_pixels,
            report->atlas_density.density);
    fprintf(fp, "ids: count=%u min=%u max=%u span=%" PRIu64 " duplicates=%u gaps=%u missing=%" PRIu64 "\n",
            report->id_range.count, report->id_range.min_id, report->id_range.max_id,
            report->id_range.span, report->id_range.duplicate_count,
            report->id_range.gap_count, report->id_range.missing_id_count);
    fprintf(fp, "kerning: pairs=%u valid=%u invalid=%u mean-in=%.3f mean-out=%.3f mean-total=%.3f\n",
            report->kerning_degrees.pair_count, report->kerning_degrees.valid_pair_count,
            report->kerning_degrees.invalid_pair_count, report->kerning_degrees.mean_in_degree,
            report->kerning_degrees.mean_out_degree, report->kerning_degrees.mean_total_degree);
    fprintf(fp, "kerning-max: in=%u@%u out=%u@%u total=%u@%u\n",
            report->kerning_degrees.max_in_degree, report->kerning_degrees.max_in_degree_index,
            report->kerning_degrees.max_out_degree, report->kerning_degrees.max_out_degree_index,
            report->kerning_degrees.max_total_degree, report->kerning_degrees.max_total_degree_index);

    fprintf(fp, "\narea histogram bucket-size=%u buckets=%u\n",
            report->area_histogram.bucket_size, report->area_histogram.bucket_count);
    for (i = 0; i < report->area_histogram.bucket_count; i++) {
        uint64_t start = (uint64_t)i * (uint64_t)report->area_histogram.bucket_size;
        uint64_t end = start + report->area_histogram.bucket_size - 1u;
        fprintf(fp, "  [%" PRIu64 "..%" PRIu64 "]: %u\n", start, end, report->area_histogram.buckets[i]);
    }

    fprintf(fp, "\nrow ink density\n");
    for (i = 0; i < report->atlas_density.row_count; i++) {
        const GlyphRowInkDensity *row = &report->atlas_density.rows[i];
        fprintf(fp, "  row %u y=%u height=%u used=%u ink=%" PRIu64 "/%" PRIu64 " density=%.6f\n",
                row->row_index, row->y, row->height, row->used,
                row->ink_pixels, row->pixels, row->density);
    }

    fprintf(fp, "\nglyph records\n");
    for (i = 0; i < report->glyph_count; i++) {
        const GlyphMetricsRecord *glyph = &report->glyphs[i];
        fprintf(fp,
                "  glyph %u id=%u box=(%d,%d %ux%u) atlas=(%u,%u) advance=%d area=%" PRIu64
                " ink=%" PRIu64 " density=%.6f kern-in=%u kern-out=%u\n",
                glyph->index, glyph->id, glyph->x, glyph->y, glyph->width, glyph->height,
                glyph->atlas_x, glyph->atlas_y, glyph->advance, glyph->area,
                glyph->ink_pixels, glyph->ink_density,
                glyph->kerning_in_degree, glyph->kerning_out_degree);
    }
    return ferror(fp) == 0;
}

int glyph_metrics_write_comparison_report(FILE *fp, const GlyphMetricsComparison *comparison) {
    if (!fp || !comparison) {
        return 0;
    }
    fprintf(fp, "Glyph metrics comparison\n");
    fprintf(fp, "glyph-count-delta: %" PRId32 "\n", comparison->glyph_count_delta);
    fprintf(fp, "kerning-count-delta: %" PRId32 "\n", comparison->kerning_count_delta);
    fprintf(fp, "row-count-delta: %" PRId32 "\n", comparison->row_count_delta);
    fprintf(fp, "atlas-width-delta: %" PRId32 "\n", comparison->atlas_width_delta);
    fprintf(fp, "atlas-height-delta: %" PRId32 "\n", comparison->atlas_height_delta);
    fprintf(fp, "added-glyphs: %u\n", comparison->added_glyphs);
    fprintf(fp, "removed-glyphs: %u\n", comparison->removed_glyphs);
    fprintf(fp, "common-glyphs: %u\n", comparison->common_glyphs);
    fprintf(fp, "changed-glyph-metrics: %u\n", comparison->changed_glyph_metrics);
    fprintf(fp, "total-advance-delta: %" PRId64 "\n", comparison->total_advance_delta);
    fprintf(fp, "total-area-delta: %" PRId64 "\n", comparison->total_area_delta);
    fprintf(fp, "atlas-density-delta: %.6f\n", comparison->atlas_density_delta);
    return ferror(fp) == 0;
}
