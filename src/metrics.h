#ifndef GLYPH_METRICS_H
#define GLYPH_METRICS_H

#include "atlas.h"

#include <stdint.h>
#include <stdio.h>

typedef struct {
    uint32_t count;
    int has_bounds;
    int32_t min_x;
    int32_t min_y;
    int32_t max_x;
    int32_t max_y;
    uint16_t min_width;
    uint16_t max_width;
    uint16_t min_height;
    uint16_t max_height;
    uint64_t total_area;
} GlyphTableBounds;

typedef struct {
    uint32_t count;
    int16_t min_advance;
    int16_t max_advance;
    int64_t total_advance;
    double mean_advance;
    double variance;
    uint32_t negative_count;
    uint32_t zero_count;
    uint32_t positive_count;
} GlyphAdvanceStats;

typedef struct {
    uint32_t bucket_count;
    uint32_t bucket_size;
    uint32_t max_area;
    uint64_t total_area;
    uint32_t *buckets;
} GlyphAreaHistogram;

typedef struct {
    uint32_t glyph_index;
    uint32_t glyph_id;
    uint32_t atlas_x;
    uint32_t atlas_y;
    uint32_t width;
    uint32_t height;
    uint64_t pixels;
    uint64_t ink_pixels;
    double density;
} GlyphInkDensity;

typedef struct {
    uint32_t row_index;
    uint32_t y;
    uint32_t height;
    uint32_t used;
    uint64_t pixels;
    uint64_t ink_pixels;
    double density;
} GlyphRowInkDensity;

typedef struct {
    uint32_t atlas_width;
    uint32_t atlas_height;
    uint64_t atlas_pixels;
    uint64_t ink_pixels;
    double density;
    uint32_t row_count;
    GlyphRowInkDensity *rows;
    uint32_t glyph_count;
    GlyphInkDensity *glyphs;
} GlyphAtlasInkDensity;

typedef struct {
    uint32_t glyph_count;
    uint32_t pair_count;
    uint32_t valid_pair_count;
    uint32_t invalid_pair_count;
    uint32_t max_in_degree;
    uint32_t max_in_degree_index;
    uint32_t max_out_degree;
    uint32_t max_out_degree_index;
    uint32_t max_total_degree;
    uint32_t max_total_degree_index;
    double mean_in_degree;
    double mean_out_degree;
    double mean_total_degree;
    uint32_t *in_degree;
    uint32_t *out_degree;
} GlyphKerningDegreeStats;

typedef struct {
    uint32_t start_id;
    uint32_t end_id;
    uint64_t count;
} GlyphIdGap;

typedef struct {
    uint32_t count;
    uint32_t min_id;
    uint32_t max_id;
    uint64_t span;
    uint32_t duplicate_count;
    uint32_t gap_count;
    uint64_t missing_id_count;
    GlyphIdGap *gaps;
} GlyphIdRangeStats;

typedef struct {
    uint32_t index;
    uint32_t id;
    int16_t x;
    int16_t y;
    uint16_t width;
    uint16_t height;
    int16_t advance;
    uint32_t bitmap_offset;
    uint32_t atlas_x;
    uint32_t atlas_y;
    uint64_t area;
    uint64_t ink_pixels;
    double ink_density;
    uint32_t kerning_in_degree;
    uint32_t kerning_out_degree;
} GlyphMetricsRecord;

typedef struct {
    int32_t glyph_count_delta;
    int32_t kerning_count_delta;
    int32_t row_count_delta;
    int32_t atlas_width_delta;
    int32_t atlas_height_delta;
    uint32_t added_glyphs;
    uint32_t removed_glyphs;
    uint32_t common_glyphs;
    uint32_t changed_glyph_metrics;
    int64_t total_advance_delta;
    int64_t total_area_delta;
    double atlas_density_delta;
} GlyphMetricsComparison;

typedef struct {
    GlyphTableBounds bounds;
    GlyphAdvanceStats advances;
    GlyphAreaHistogram area_histogram;
    GlyphAtlasInkDensity atlas_density;
    GlyphKerningDegreeStats kerning_degrees;
    GlyphIdRangeStats id_range;
    uint32_t glyph_count;
    GlyphMetricsRecord *glyphs;
} GlyphMetricsReport;

void glyph_metrics_area_histogram_free(GlyphAreaHistogram *histogram);
void glyph_metrics_atlas_ink_density_free(GlyphAtlasInkDensity *density);
void glyph_metrics_kerning_degree_stats_free(GlyphKerningDegreeStats *stats);
void glyph_metrics_id_range_stats_free(GlyphIdRangeStats *stats);
void glyph_metrics_records_free(GlyphMetricsRecord *records);
void glyph_metrics_report_free(GlyphMetricsReport *report);

int glyph_metrics_compute_bounds(const GlyphTable *table, GlyphTableBounds *bounds);
int glyph_metrics_compute_advance_stats(const GlyphTable *table, GlyphAdvanceStats *stats);
int glyph_metrics_compute_area_histogram(const GlyphTable *table, uint32_t bucket_size, GlyphAreaHistogram *histogram);
int glyph_metrics_compute_atlas_ink_density(const GlyphFile *file, GlyphAtlasInkDensity *density);
int glyph_metrics_compute_kerning_degree_stats(const GlyphFile *file, GlyphKerningDegreeStats *stats);
int glyph_metrics_compute_id_range(const GlyphTable *table, GlyphIdRangeStats *stats);
int glyph_metrics_collect_records(const GlyphFile *file, GlyphMetricsRecord **records, uint32_t *count);
int glyph_metrics_build_report(const GlyphFile *file, GlyphMetricsReport *report);
int glyph_metrics_compare_files(const GlyphFile *before, const GlyphFile *after, GlyphMetricsComparison *comparison);
int glyph_metrics_write_text_report(FILE *fp, const GlyphMetricsReport *report);
int glyph_metrics_write_comparison_report(FILE *fp, const GlyphMetricsComparison *comparison);

#endif
