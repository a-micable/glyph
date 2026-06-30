#ifndef GLYPH_OPTIMIZE_H
#define GLYPH_OPTIMIZE_H

#include "glyph_table.h"
#include "pack_plan.h"

#include <stdint.h>
#include <stdio.h>

#define GLYPH_OPTIMIZE_INDEX_NONE UINT32_MAX

typedef enum {
    GLYPH_OPTIMIZE_SCORE_BALANCED = 0,
    GLYPH_OPTIMIZE_SCORE_COMPACT = 1,
    GLYPH_OPTIMIZE_SCORE_STABLE = 2,
    GLYPH_OPTIMIZE_SCORE_LOW_ROWS = 3
} GlyphOptimizeScoreMode;

typedef enum {
    GLYPH_OPTIMIZE_WASTE_NONE = 0,
    GLYPH_OPTIMIZE_WASTE_HORIZONTAL = 1,
    GLYPH_OPTIMIZE_WASTE_INTERNAL = 2,
    GLYPH_OPTIMIZE_WASTE_TALL_ROW = 3,
    GLYPH_OPTIMIZE_WASTE_SPARSE_ROW = 4
} GlyphOptimizeWasteKind;

typedef enum {
    GLYPH_OPTIMIZE_REORDER_NONE = 0,
    GLYPH_OPTIMIZE_REORDER_EARLIER = 1,
    GLYPH_OPTIMIZE_REORDER_LATER = 2,
    GLYPH_OPTIMIZE_REORDER_NEAR_TALLER = 3,
    GLYPH_OPTIMIZE_REORDER_NEAR_SIMILAR = 4
} GlyphOptimizeReorderKind;

typedef struct {
    uint32_t atlas_area_weight;
    uint32_t waste_weight;
    uint32_t height_weight;
    uint32_t row_weight;
    uint32_t width_weight;
    uint32_t instability_weight;
} GlyphOptimizeScoreWeights;

typedef struct {
    uint32_t min_width;
    uint32_t max_width;
    uint32_t max_width_candidates;
    uint32_t max_reports_per_section;
    GlyphOptimizeScoreMode score_mode;
    GlyphOptimizeScoreWeights weights;
    int include_table_sort;
    int include_height_sort;
    int include_area_sort;
    int include_id_sort;
} GlyphOptimizeOptions;

typedef struct {
    uint32_t width;
    GlyphPackSortMode sort_mode;
    int ok;
    uint32_t pareto_rank;
    uint32_t dominated_by;
    uint32_t dominance_count;
    GlyphPackMetrics metrics;
    uint64_t atlas_area;
    uint64_t compact_score;
    uint64_t stable_score;
    uint64_t score;
} GlyphOptimizeCandidate;

typedef struct {
    uint32_t count;
    uint32_t best_index;
    uint32_t pareto_count;
    GlyphOptimizeCandidate *items;
} GlyphOptimizeCandidateSet;

typedef struct {
    uint32_t row_index;
    uint32_t split_after_order;
    uint32_t left_glyphs;
    uint32_t right_glyphs;
    uint32_t row_height;
    uint32_t left_used;
    uint32_t right_used;
    uint32_t left_height;
    uint32_t right_height;
    uint64_t current_pixels;
    uint64_t split_pixels;
    uint64_t current_waste;
    uint64_t split_waste;
    int64_t saved_pixels;
    double current_waste_ratio;
    double split_waste_ratio;
} GlyphOptimizeRowSplit;

typedef struct {
    uint32_t count;
    GlyphOptimizeRowSplit *items;
} GlyphOptimizeRowSplitSet;

typedef struct {
    uint32_t glyph_index;
    uint32_t glyph_id;
    uint32_t from_order;
    uint32_t to_order;
    uint32_t from_row;
    uint32_t to_row;
    GlyphOptimizeReorderKind kind;
    uint64_t estimated_gain;
    double confidence;
} GlyphOptimizeReorderSuggestion;

typedef struct {
    uint32_t count;
    GlyphOptimizeReorderSuggestion *items;
} GlyphOptimizeReorderSet;

typedef struct {
    uint32_t row_index;
    GlyphOptimizeWasteKind kind;
    uint64_t pixels;
    double ratio;
    uint32_t glyph_count;
    uint32_t row_height;
    uint32_t row_used;
    uint32_t tallest_glyph;
    uint32_t shortest_glyph;
} GlyphOptimizeWasteExplanation;

typedef struct {
    uint32_t count;
    GlyphOptimizeWasteExplanation *items;
} GlyphOptimizeWasteExplanationSet;

typedef struct {
    uint32_t glyph_count;
    uint32_t matched_glyphs;
    uint32_t unchanged;
    uint32_t moved;
    uint32_t row_changed;
    uint32_t missing_before;
    uint32_t missing_after;
    uint64_t total_manhattan_distance;
    uint32_t max_manhattan_distance;
    uint32_t max_manhattan_glyph_index;
    double unchanged_ratio;
    double row_changed_ratio;
    double mean_manhattan_distance;
} GlyphOptimizePlacementStability;

typedef struct {
    GlyphOptimizeCandidateSet candidates;
    GlyphPackPlan *best_plan;
    GlyphOptimizeRowSplitSet row_splits;
    GlyphOptimizeReorderSet reorder_suggestions;
    GlyphOptimizeWasteExplanationSet waste_explanations;
} GlyphOptimizeReport;

void glyph_optimize_score_weights_default(GlyphOptimizeScoreMode mode,
                                          GlyphOptimizeScoreWeights *weights);
void glyph_optimize_options_default(GlyphOptimizeOptions *options);
const char *glyph_optimize_score_mode_name(GlyphOptimizeScoreMode mode);
const char *glyph_optimize_waste_kind_name(GlyphOptimizeWasteKind kind);
const char *glyph_optimize_reorder_kind_name(GlyphOptimizeReorderKind kind);

uint64_t glyph_optimize_score_metrics(const GlyphPackMetrics *metrics,
                                      const GlyphOptimizeScoreWeights *weights,
                                      uint32_t width,
                                      uint64_t instability);
uint64_t glyph_optimize_score_candidate(const GlyphOptimizeCandidate *candidate,
                                        const GlyphOptimizeScoreWeights *weights,
                                        uint64_t instability);

int glyph_optimize_generate_candidates(const GlyphTable *table,
                                       const GlyphOptimizeOptions *options,
                                       GlyphOptimizeCandidateSet *out_set);
void glyph_optimize_candidate_set_free(GlyphOptimizeCandidateSet *set);

int glyph_optimize_create_best_plan(const GlyphTable *table,
                                    const GlyphOptimizeCandidateSet *candidates,
                                    GlyphPackPlan **out_plan);

int glyph_optimize_analyze_row_splits(const GlyphPackPlan *plan,
                                      uint32_t max_splits,
                                      GlyphOptimizeRowSplitSet *out_splits);
void glyph_optimize_row_split_set_free(GlyphOptimizeRowSplitSet *set);

int glyph_optimize_suggest_reorder(const GlyphTable *table,
                                   const GlyphPackPlan *plan,
                                   uint32_t max_suggestions,
                                   GlyphOptimizeReorderSet *out_suggestions);
void glyph_optimize_reorder_set_free(GlyphOptimizeReorderSet *set);

int glyph_optimize_explain_waste(const GlyphPackPlan *plan,
                                 uint32_t max_explanations,
                                 GlyphOptimizeWasteExplanationSet *out_explanations);
void glyph_optimize_waste_explanation_set_free(GlyphOptimizeWasteExplanationSet *set);

int glyph_optimize_compare_placement_stability(const GlyphPackPlan *before,
                                               const GlyphPackPlan *after,
                                               GlyphOptimizePlacementStability *out_stability);

int glyph_optimize_build_report(const GlyphTable *table,
                                const GlyphOptimizeOptions *options,
                                GlyphOptimizeReport *out_report);
void glyph_optimize_report_free(GlyphOptimizeReport *report);

int glyph_optimize_write_text_report(FILE *fp,
                                     const GlyphOptimizeReport *report,
                                     const GlyphPackPlan *baseline);

#endif
