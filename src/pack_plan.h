#ifndef GLYPH_PACK_PLAN_H
#define GLYPH_PACK_PLAN_H

#include "atlas.h"
#include "glyph_table.h"

#include <stdint.h>
#include <stdio.h>

#define GLYPH_PACK_PLAN_INDEX_NONE UINT32_MAX

typedef enum {
    GLYPH_PACK_SORT_TABLE = 0,
    GLYPH_PACK_SORT_HEIGHT_DESC = 1,
    GLYPH_PACK_SORT_AREA_DESC = 2,
    GLYPH_PACK_SORT_ID_ASC = 3
} GlyphPackSortMode;

typedef struct {
    uint32_t atlas_width;
    GlyphPackSortMode sort_mode;
} GlyphPackPlanOptions;

typedef struct {
    uint32_t y;
    uint32_t height;
    uint32_t used;
    uint32_t glyph_count;
    uint32_t first_order;
    uint64_t glyph_pixels;
    uint64_t row_pixels;
    uint64_t used_pixels;
    uint64_t waste_pixels;
    uint64_t horizontal_waste_pixels;
    uint64_t internal_waste_pixels;
    double utilization;
    double glyph_coverage;
    double waste_ratio;
} GlyphPackRowStats;

typedef struct {
    uint32_t glyph_index;
    uint32_t glyph_id;
    GlyphPlacement placement;
    uint32_t row_index;
    uint32_t width;
    uint32_t height;
    uint64_t area;
} GlyphPackPlacement;

typedef struct {
    uint32_t atlas_width;
    uint32_t atlas_height;
    uint32_t row_count;
    uint32_t glyph_count;
    uint64_t glyph_pixels;
    uint64_t row_pixels;
    uint64_t used_pixels;
    uint64_t waste_pixels;
    uint64_t horizontal_waste_pixels;
    uint64_t internal_waste_pixels;
    uint32_t max_waste_row;
    uint64_t max_waste_pixels;
    double glyph_coverage;
    double row_utilization;
    double waste_ratio;
} GlyphPackMetrics;

typedef struct {
    uint32_t atlas_width;
    uint32_t atlas_height;
    uint32_t row_count;
    uint32_t glyph_count;
    GlyphPackSortMode sort_mode;
    GlyphPackMetrics metrics;
    GlyphPackRowStats *rows;
    GlyphPackPlacement *placements;
    uint32_t *pack_order;
} GlyphPackPlan;

typedef struct {
    uint32_t width;
    int ok;
    GlyphPackSortMode sort_mode;
    GlyphPackMetrics metrics;
    uint64_t score;
} GlyphPackCandidate;

typedef struct {
    uint32_t count;
    uint32_t best_index;
    GlyphPackCandidate *items;
} GlyphPackCandidateSet;

void glyph_pack_plan_options_default(GlyphPackPlanOptions *options);
const char *glyph_pack_sort_mode_name(GlyphPackSortMode mode);

int glyph_pack_plan_estimate_widths(const GlyphTable *table,
                                    uint32_t min_width,
                                    uint32_t max_width,
                                    uint32_t *widths,
                                    uint32_t capacity,
                                    uint32_t *count);

int glyph_pack_plan_sort_indices(const GlyphTable *table,
                                 GlyphPackSortMode mode,
                                 uint32_t *indices,
                                 uint32_t count);

int glyph_pack_plan_create(const GlyphTable *table,
                           const GlyphPackPlanOptions *options,
                           GlyphPackPlan **out_plan);

int glyph_pack_plan_simulate(const GlyphTable *table,
                             const uint32_t *order,
                             uint32_t order_count,
                             uint32_t atlas_width,
                             GlyphPackSortMode sort_mode,
                             GlyphPackPlan **out_plan);

void glyph_pack_plan_free(GlyphPackPlan *plan);

int glyph_pack_plan_compare_widths(const GlyphTable *table,
                                   const uint32_t *widths,
                                   uint32_t width_count,
                                   const GlyphPackPlanOptions *options,
                                   GlyphPackCandidateSet *out_set);

void glyph_pack_candidate_set_free(GlyphPackCandidateSet *set);

int glyph_pack_plan_row_stats(const GlyphPackPlan *plan,
                              uint32_t row_index,
                              GlyphPackRowStats *out_stats);

int glyph_pack_plan_verify_bounds(const GlyphPackPlan *plan,
                                  const GlyphTable *table,
                                  uint32_t *first_bad_index);

const GlyphPackPlacement *glyph_pack_plan_placement_for_index(const GlyphPackPlan *plan,
                                                              uint32_t glyph_index);

int glyph_pack_plan_find_id(const GlyphPackPlan *plan,
                            uint32_t glyph_id,
                            GlyphPackPlacement *out_placement);

int glyph_pack_plan_write_report(FILE *fp, const GlyphPackPlan *plan);

#endif
