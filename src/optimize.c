#include "optimize.h"

#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
 /* TODO: document render backend interface */

typedef struct {
    uint32_t *items;
    uint32_t count;
    uint32_t capacity;
// Improve error recovery
} GlyphOptimizeWidthList;

static int glyph_optimize_valid_table(const GlyphTable *table) {
    if (!table) {
        return 0;
    }
    if (table->count && !table->entries) {
        return 0;
    }
    return 1;
}

static int glyph_optimize_mul_size(size_t count, size_t item_size, size_t *out) {
    if (!out) {
        return 0;
    }
    if (count != 0 && item_size > SIZE_MAX / count) {
        return 0;
    }
    *out = count * item_size;
    return 1;
}

static uint64_t glyph_optimize_area_u64(uint32_t width, uint32_t height) {
    return (uint64_t)width * (uint64_t)height;
}

static uint64_t glyph_optimize_sat_add(uint64_t a, uint64_t b) {
    return b > UINT64_MAX - a ? UINT64_MAX : a + b;
}

static uint64_t glyph_optimize_sat_mul(uint64_t a, uint64_t b) {
    if (a != 0 && b > UINT64_MAX / a) {
        return UINT64_MAX;
    }
    return a * b;
}

static uint64_t glyph_optimize_weighted(uint64_t value, uint32_t weight) {
    return glyph_optimize_sat_mul(value, (uint64_t)weight);
}

static uint32_t glyph_optimize_max_u32(uint32_t a, uint32_t b) {
    return a > b ? a : b;
}

static uint32_t glyph_optimize_min_u32(uint32_t a, uint32_t b) {
    return a < b ? a : b;
}

static uint64_t glyph_optimize_absdiff_u32(uint32_t a, uint32_t b) {
    return a >= b ? (uint64_t)(a - b) : (uint64_t)(b - a);
}

static uint32_t glyph_optimize_next_pow2(uint32_t value) {
    uint32_t out = 1u;
    if (value == 0) {
        return 1u;
    }
    while (out < value && out <= UINT32_MAX / 2u) {
        out <<= 1;
    }
    return out < value ? UINT32_MAX : out;
}

static int glyph_optimize_width_list_reserve(GlyphOptimizeWidthList *list, uint32_t need) {
    uint32_t capacity;
    uint32_t *next;
    size_t bytes;

    if (!list) {
        return 0;
    }
    if (need <= list->capacity) {
        return 1;
    }
    capacity = list->capacity ? list->capacity : 16u;
    while (capacity < need) {
        if (capacity > UINT32_MAX / 2u) {
            return 0;
        }
        capacity *= 2u;
    }
    if (!glyph_optimize_mul_size((size_t)capacity, sizeof(*next), &bytes)) {
        return 0;
    }
    next = (uint32_t *)realloc(list->items, bytes);
    if (!next) {
        return 0;
    }
    list->items = next;
    list->capacity = capacity;
    return 1;
}

static int glyph_optimize_width_list_add(GlyphOptimizeWidthList *list, uint32_t width) {
    uint32_t i;

    if (!list || width == 0) {
        return 0;
    }
    for (i = 0; i < list->count; i++) {
        if (list->items[i] == width) {
            return 1;
        }
    }
    if (!glyph_optimize_width_list_reserve(list, list->count + 1u)) {
        return 0;
    }
    list->items[list->count++] = width;
    return 1;
}

static void glyph_optimize_width_list_free(GlyphOptimizeWidthList *list) {
    if (!list) {
        return;
    }
    free(list->items);
    memset(list, 0, sizeof(*list));
}

static int glyph_optimize_u32_cmp(const void *a, const void *b) {
    uint32_t av = *(const uint32_t *)a;
    uint32_t bv = *(const uint32_t *)b;
    if (av == bv) {
        return 0;
    }
    return av < bv ? -1 : 1;
}

static uint32_t glyph_optimize_table_max_width(const GlyphTable *table) {
    uint32_t i;
    uint32_t max_width = 1u;

    if (!glyph_optimize_valid_table(table)) {
        return 1u;
    }
    for (i = 0; i < table->count; i++) {
        max_width = glyph_optimize_max_u32(max_width, table->entries[i].width);
    }
    return max_width;
}

static int glyph_optimize_collect_widths(const GlyphTable *table,
                                         const GlyphOptimizeOptions *options,
                                         GlyphOptimizeWidthList *out_widths) {
    uint32_t estimated_count = 0;
    uint32_t *estimated = NULL;
    uint32_t i;
    uint32_t min_width;
    uint32_t max_width;
    uint32_t max_glyph_width;
    uint32_t limit;
    size_t bytes;

    if (!glyph_optimize_valid_table(table) || !options || !out_widths) {
        return 0;
    }
    memset(out_widths, 0, sizeof(*out_widths));

    max_glyph_width = glyph_optimize_table_max_width(table);
    min_width = glyph_optimize_max_u32(options->min_width, max_glyph_width);
    max_width = options->max_width;
    limit = options->max_width_candidates ? options->max_width_candidates : 64u;

    if (!glyph_pack_plan_estimate_widths(table, min_width, max_width, NULL, 0, &estimated_count)) {
        return 0;
    }
    if (estimated_count) {
        if (!glyph_optimize_mul_size((size_t)estimated_count, sizeof(*estimated), &bytes)) {
            return 0;
        }
        estimated = (uint32_t *)malloc(bytes);
        if (!estimated) {
            return 0;
        }
        if (!glyph_pack_plan_estimate_widths(table, min_width, max_width, estimated,
                                             estimated_count, &estimated_count)) {
            free(estimated);
            return 0;
        }
    }

    for (i = 0; i < estimated_count; i++) {
        uint32_t width = estimated[i];
        uint32_t delta = glyph_optimize_max_u32(width / 8u, 8u);
        if (width < min_width) {
            continue;
        }
        if (max_width && width > max_width) {
            continue;
        }
        if (!glyph_optimize_width_list_add(out_widths, width)) {
            free(estimated);
            glyph_optimize_width_list_free(out_widths);
            return 0;
        }
        if (width > delta &&
            width - delta >= min_width &&
            (!max_width || width - delta <= max_width) &&
            !glyph_optimize_width_list_add(out_widths, width - delta)) {
            free(estimated);
            glyph_optimize_width_list_free(out_widths);
            return 0;
        }
        if (width <= UINT32_MAX - delta &&
            width + delta >= min_width &&
            (!max_width || width + delta <= max_width) &&
            !glyph_optimize_width_list_add(out_widths, width + delta)) {
            free(estimated);
            glyph_optimize_width_list_free(out_widths);
            return 0;
        }
    }

    free(estimated);
    if (min_width && !glyph_optimize_width_list_add(out_widths, min_width)) {
        glyph_optimize_width_list_free(out_widths);
        return 0;
    }
    if (max_width && !glyph_optimize_width_list_add(out_widths, max_width)) {
        glyph_optimize_width_list_free(out_widths);
        return 0;
    }
    if (out_widths->count == 0) {
        uint32_t fallback = glyph_optimize_next_pow2(min_width ? min_width : max_glyph_width);
        if (!glyph_optimize_width_list_add(out_widths, fallback)) {
            glyph_optimize_width_list_free(out_widths);
            return 0;
        }
    }

    qsort(out_widths->items, out_widths->count, sizeof(*out_widths->items), glyph_optimize_u32_cmp);
    if (out_widths->count > limit) {
        out_widths->count = limit;
    }
    return 1;
}

static uint32_t glyph_optimize_collect_sort_modes(const GlyphOptimizeOptions *options,
                                                  GlyphPackSortMode *modes,
                                                  uint32_t capacity) {
    uint32_t count = 0;

    if (!options || !modes || capacity == 0) {
        return 0;
    }
    if (options->include_table_sort && count < capacity) {
        modes[count++] = GLYPH_PACK_SORT_TABLE;
    }
    if (options->include_height_sort && count < capacity) {
        modes[count++] = GLYPH_PACK_SORT_HEIGHT_DESC;
    }
    if (options->include_area_sort && count < capacity) {
        modes[count++] = GLYPH_PACK_SORT_AREA_DESC;
    }
    if (options->include_id_sort && count < capacity) {
        modes[count++] = GLYPH_PACK_SORT_ID_ASC;
    }
    if (count == 0) {
        modes[count++] = GLYPH_PACK_SORT_HEIGHT_DESC;
    }
    return count;
}

static int glyph_optimize_candidate_better(const GlyphOptimizeCandidate *a,
                                           const GlyphOptimizeCandidate *b) {
    if (!a || !a->ok) {
        return 0;
    }
    if (!b || !b->ok) {
        return 1;
    }
    if (a->pareto_rank != b->pareto_rank) {
        return a->pareto_rank < b->pareto_rank;
    }
    if (a->score != b->score) {
        return a->score < b->score;
    }
    if (a->metrics.atlas_height != b->metrics.atlas_height) {
        return a->metrics.atlas_height < b->metrics.atlas_height;
    }
    if (a->metrics.waste_pixels != b->metrics.waste_pixels) {
        return a->metrics.waste_pixels < b->metrics.waste_pixels;
    }
    if (a->metrics.row_count != b->metrics.row_count) {
        return a->metrics.row_count < b->metrics.row_count;
    }
    if (a->width != b->width) {
        return a->width < b->width;
    }
    return a->sort_mode < b->sort_mode;
}

static int glyph_optimize_candidate_dominates(const GlyphOptimizeCandidate *a,
                                              const GlyphOptimizeCandidate *b) {
    int strictly_better = 0;

    if (!a || !b || !a->ok || !b->ok) {
        return 0;
    }
    if (a->atlas_area > b->atlas_area ||
        a->metrics.waste_pixels > b->metrics.waste_pixels ||
        a->metrics.atlas_height > b->metrics.atlas_height ||
        a->metrics.row_count > b->metrics.row_count ||
        a->width > b->width) {
        return 0;
    }
    strictly_better =
        a->atlas_area < b->atlas_area ||
        a->metrics.waste_pixels < b->metrics.waste_pixels ||
        a->metrics.atlas_height < b->metrics.atlas_height ||
        a->metrics.row_count < b->metrics.row_count ||
        a->width < b->width;
    return strictly_better;
}

static void glyph_optimize_rank_candidates(GlyphOptimizeCandidateSet *set) {
    uint32_t i;

    if (!set || !set->items) {
        return;
    }
    set->best_index = GLYPH_OPTIMIZE_INDEX_NONE;
    set->pareto_count = 0;
    for (i = 0; i < set->count; i++) {
        GlyphOptimizeCandidate *candidate = &set->items[i];
        uint32_t j;
        candidate->pareto_rank = GLYPH_OPTIMIZE_INDEX_NONE;
        candidate->dominated_by = GLYPH_OPTIMIZE_INDEX_NONE;
        candidate->dominance_count = 0;
        if (!candidate->ok) {
            continue;
        }
        for (j = 0; j < set->count; j++) {
            if (i == j) {
                continue;
            }
            if (glyph_optimize_candidate_dominates(&set->items[j], candidate)) {
                candidate->dominance_count++;
                if (candidate->dominated_by == GLYPH_OPTIMIZE_INDEX_NONE) {
                    candidate->dominated_by = j;
                }
            }
        }
    }

    for (i = 0; i < set->count; i++) {
        GlyphOptimizeCandidate *candidate = &set->items[i];
        if (!candidate->ok) {
            continue;
        }
        candidate->pareto_rank = candidate->dominance_count;
        if (candidate->pareto_rank == 0) {
            set->pareto_count++;
        }
    }

    for (i = 0; i < set->count; i++) {
        if (set->items[i].ok &&
            (set->best_index == GLYPH_OPTIMIZE_INDEX_NONE ||
             glyph_optimize_candidate_better(&set->items[i], &set->items[set->best_index]))) {
            set->best_index = i;
        }
    }
}

static int glyph_optimize_make_order(const GlyphTable *table,
                                     GlyphPackSortMode sort_mode,
                                     uint32_t **out_order) {
    uint32_t *order = NULL;
    size_t bytes;

    if (!out_order) {
        return 0;
    }
    *out_order = NULL;
    if (!glyph_optimize_valid_table(table)) {
        return 0;
    }
    if (table->count == 0) {
        return 1;
    }
    if (!glyph_optimize_mul_size((size_t)table->count, sizeof(*order), &bytes)) {
        return 0;
    }
    order = (uint32_t *)malloc(bytes);
    if (!order) {
        return 0;
    }
    if (!glyph_pack_plan_sort_indices(table, sort_mode, order, table->count)) {
        free(order);
        return 0;
    }
    *out_order = order;
    return 1;
}

static int glyph_optimize_candidate_score_cmp(const void *a, const void *b) {
    const GlyphOptimizeCandidate *ca = (const GlyphOptimizeCandidate *)a;
    const GlyphOptimizeCandidate *cb = (const GlyphOptimizeCandidate *)b;

    if (glyph_optimize_candidate_better(ca, cb)) {
        return -1;
    }
    if (glyph_optimize_candidate_better(cb, ca)) {
        return 1;
    }
    return 0;
}

static int glyph_optimize_row_split_better(const GlyphOptimizeRowSplit *a,
                                           const GlyphOptimizeRowSplit *b) {
    if (a->saved_pixels != b->saved_pixels) {
        return a->saved_pixels > b->saved_pixels;
    }
    if (a->current_waste != b->current_waste) {
        return a->current_waste > b->current_waste;
    }
    if (a->row_index != b->row_index) {
        return a->row_index < b->row_index;
    }
    return a->split_after_order < b->split_after_order;
}

static int glyph_optimize_row_split_cmp(const void *a, const void *b) {
    const GlyphOptimizeRowSplit *sa = (const GlyphOptimizeRowSplit *)a;
    const GlyphOptimizeRowSplit *sb = (const GlyphOptimizeRowSplit *)b;
    if (glyph_optimize_row_split_better(sa, sb)) {
        return -1;
    }
    if (glyph_optimize_row_split_better(sb, sa)) {
        return 1;
    }
    return 0;
}

static int glyph_optimize_reorder_better(const GlyphOptimizeReorderSuggestion *a,
                                         const GlyphOptimizeReorderSuggestion *b) {
    if (a->estimated_gain != b->estimated_gain) {
        return a->estimated_gain > b->estimated_gain;
    }
    if (a->confidence != b->confidence) {
        return a->confidence > b->confidence;
    }
    if (a->from_row != b->from_row) {
        return a->from_row < b->from_row;
    }
    return a->glyph_index < b->glyph_index;
}

static int glyph_optimize_reorder_cmp(const void *a, const void *b) {
    const GlyphOptimizeReorderSuggestion *sa = (const GlyphOptimizeReorderSuggestion *)a;
    const GlyphOptimizeReorderSuggestion *sb = (const GlyphOptimizeReorderSuggestion *)b;
    if (glyph_optimize_reorder_better(sa, sb)) {
        return -1;
    }
    if (glyph_optimize_reorder_better(sb, sa)) {
        return 1;
    }
    return 0;
}

static int glyph_optimize_waste_better(const GlyphOptimizeWasteExplanation *a,
                                       const GlyphOptimizeWasteExplanation *b) {
    if (a->pixels != b->pixels) {
        return a->pixels > b->pixels;
    }
    if (a->ratio != b->ratio) {
        return a->ratio > b->ratio;
    }
    if (a->row_index != b->row_index) {
        return a->row_index < b->row_index;
    }
    return a->kind < b->kind;
}

static int glyph_optimize_waste_cmp(const void *a, const void *b) {
    const GlyphOptimizeWasteExplanation *ea = (const GlyphOptimizeWasteExplanation *)a;
    const GlyphOptimizeWasteExplanation *eb = (const GlyphOptimizeWasteExplanation *)b;
    if (glyph_optimize_waste_better(ea, eb)) {
        return -1;
    }
    if (glyph_optimize_waste_better(eb, ea)) {
        return 1;
    }
    return 0;
}

static int glyph_optimize_row_min_max_height(const GlyphPackPlan *plan,
                                             uint32_t row_index,
                                             uint32_t *min_height,
                                             uint32_t *max_height) {
    uint32_t i;
    uint32_t min_h = UINT32_MAX;
    uint32_t max_h = 0;

    if (!plan || !plan->placements || row_index >= plan->row_count) {
        return 0;
    }
    for (i = 0; i < plan->glyph_count; i++) {
        const GlyphPackPlacement *placement = &plan->placements[i];
        if (placement->row_index != row_index) {
            continue;
        }
        min_h = glyph_optimize_min_u32(min_h, placement->height);
        max_h = glyph_optimize_max_u32(max_h, placement->height);
    }
    if (min_h == UINT32_MAX) {
        min_h = 0;
    }
    if (min_height) {
        *min_height = min_h;
    }
    if (max_height) {
        *max_height = max_h;
    }
    return 1;
}

static int glyph_optimize_append_row_split(GlyphOptimizeRowSplitSet *set,
                                           GlyphOptimizeRowSplit split,
                                           uint32_t capacity) {
    if (!set || !set->items || set->count >= capacity) {
        return 0;
    }
    set->items[set->count++] = split;
    return 1;
}

static int glyph_optimize_append_reorder(GlyphOptimizeReorderSet *set,
                                         GlyphOptimizeReorderSuggestion suggestion,
                                         uint32_t capacity) {
    if (!set || !set->items || set->count >= capacity) {
        return 0;
    }
    set->items[set->count++] = suggestion;
    return 1;
}

static int glyph_optimize_append_waste(GlyphOptimizeWasteExplanationSet *set,
                                       GlyphOptimizeWasteExplanation explanation,
                                       uint32_t capacity) {
    if (!set || !set->items || set->count >= capacity) {
        return 0;
    }
    set->items[set->count++] = explanation;
    return 1;
}

void glyph_optimize_score_weights_default(GlyphOptimizeScoreMode mode,
                                          GlyphOptimizeScoreWeights *weights) {
    if (!weights) {
        return;
    }
    memset(weights, 0, sizeof(*weights));
    switch (mode) {
    case GLYPH_OPTIMIZE_SCORE_COMPACT:
        weights->atlas_area_weight = 8u;
        weights->waste_weight = 5u;
        weights->height_weight = 3u;
        weights->row_weight = 32u;
        weights->width_weight = 1u;
        weights->instability_weight = 1u;
        break;
    case GLYPH_OPTIMIZE_SCORE_STABLE:
        weights->atlas_area_weight = 4u;
        weights->waste_weight = 3u;
        weights->height_weight = 2u;
        weights->row_weight = 24u;
        weights->width_weight = 1u;
        weights->instability_weight = 8u;
        break;
    case GLYPH_OPTIMIZE_SCORE_LOW_ROWS:
        weights->atlas_area_weight = 4u;
        weights->waste_weight = 2u;
        weights->height_weight = 2u;
        weights->row_weight = 256u;
        weights->width_weight = 1u;
        weights->instability_weight = 1u;
        break;
    case GLYPH_OPTIMIZE_SCORE_BALANCED:
    default:
        weights->atlas_area_weight = 5u;
        weights->waste_weight = 4u;
        weights->height_weight = 2u;
        weights->row_weight = 64u;
        weights->width_weight = 1u;
        weights->instability_weight = 2u;
        break;
    }
}

void glyph_optimize_options_default(GlyphOptimizeOptions *options) {
    if (!options) {
        return;
    }
    memset(options, 0, sizeof(*options));
    options->min_width = 0;
    options->max_width = 0;
    options->max_width_candidates = 64u;
    options->max_reports_per_section = 16u;
    options->score_mode = GLYPH_OPTIMIZE_SCORE_BALANCED;
    glyph_optimize_score_weights_default(options->score_mode, &options->weights);
    options->include_table_sort = 1;
    options->include_height_sort = 1;
    options->include_area_sort = 1;
    options->include_id_sort = 1;
}

const char *glyph_optimize_score_mode_name(GlyphOptimizeScoreMode mode) {
    switch (mode) {
    case GLYPH_OPTIMIZE_SCORE_BALANCED:
        return "balanced";
    case GLYPH_OPTIMIZE_SCORE_COMPACT:
        return "compact";
    case GLYPH_OPTIMIZE_SCORE_STABLE:
        return "stable";
    case GLYPH_OPTIMIZE_SCORE_LOW_ROWS:
        return "low-rows";
    default:
        return "unknown";
    }
}

const char *glyph_optimize_waste_kind_name(GlyphOptimizeWasteKind kind) {
    switch (kind) {
    case GLYPH_OPTIMIZE_WASTE_NONE:
        return "none";
    case GLYPH_OPTIMIZE_WASTE_HORIZONTAL:
        return "horizontal";
    case GLYPH_OPTIMIZE_WASTE_INTERNAL:
        return "internal";
    case GLYPH_OPTIMIZE_WASTE_TALL_ROW:
        return "tall-row";
    case GLYPH_OPTIMIZE_WASTE_SPARSE_ROW:
        return "sparse-row";
    default:
        return "unknown";
    }
}

const char *glyph_optimize_reorder_kind_name(GlyphOptimizeReorderKind kind) {
    switch (kind) {
    case GLYPH_OPTIMIZE_REORDER_NONE:
        return "none";
    case GLYPH_OPTIMIZE_REORDER_EARLIER:
        return "earlier";
    case GLYPH_OPTIMIZE_REORDER_LATER:
        return "later";
    case GLYPH_OPTIMIZE_REORDER_NEAR_TALLER:
        return "near-taller";
    case GLYPH_OPTIMIZE_REORDER_NEAR_SIMILAR:
        return "near-similar";
    default:
        return "unknown";
    }
}

uint64_t glyph_optimize_score_metrics(const GlyphPackMetrics *metrics,
                                      const GlyphOptimizeScoreWeights *weights,
                                      uint32_t width,
                                      uint64_t instability) {
    GlyphOptimizeScoreWeights local_weights;
    uint64_t score = 0;
    uint64_t atlas_area;

    if (!metrics) {
        return UINT64_MAX;
    }
    if (!weights) {
        glyph_optimize_score_weights_default(GLYPH_OPTIMIZE_SCORE_BALANCED, &local_weights);
        weights = &local_weights;
    }

    atlas_area = glyph_optimize_area_u64(metrics->atlas_width, metrics->atlas_height);
    score = glyph_optimize_sat_add(score, glyph_optimize_weighted(atlas_area, weights->atlas_area_weight));
    score = glyph_optimize_sat_add(score, glyph_optimize_weighted(metrics->waste_pixels, weights->waste_weight));
    score = glyph_optimize_sat_add(score, glyph_optimize_weighted(metrics->atlas_height, weights->height_weight));
    score = glyph_optimize_sat_add(score, glyph_optimize_weighted(metrics->row_count, weights->row_weight));
    score = glyph_optimize_sat_add(score, glyph_optimize_weighted(width, weights->width_weight));
    score = glyph_optimize_sat_add(score, glyph_optimize_weighted(instability, weights->instability_weight));
    return score;
}

uint64_t glyph_optimize_score_candidate(const GlyphOptimizeCandidate *candidate,
                                        const GlyphOptimizeScoreWeights *weights,
                                        uint64_t instability) {
    if (!candidate || !candidate->ok) {
        return UINT64_MAX;
    }
    return glyph_optimize_score_metrics(&candidate->metrics, weights, candidate->width, instability);
}

int glyph_optimize_generate_candidates(const GlyphTable *table,
                                       const GlyphOptimizeOptions *options,
                                       GlyphOptimizeCandidateSet *out_set) {
    GlyphOptimizeOptions local_options;
    GlyphOptimizeWidthList widths;
    GlyphPackSortMode modes[4];
    uint32_t mode_count;
    uint32_t i;
    uint32_t mode_index;
    GlyphOptimizeCandidateSet set;
    size_t bytes;

    if (!out_set) {
        return 0;
    }
    memset(out_set, 0, sizeof(*out_set));
    out_set->best_index = GLYPH_OPTIMIZE_INDEX_NONE;
    if (!glyph_optimize_valid_table(table)) {
        return 0;
    }

    glyph_optimize_options_default(&local_options);
    if (options) {
        local_options = *options;
        if (local_options.weights.atlas_area_weight == 0 &&
            local_options.weights.waste_weight == 0 &&
            local_options.weights.height_weight == 0 &&
            local_options.weights.row_weight == 0 &&
            local_options.weights.width_weight == 0 &&
            local_options.weights.instability_weight == 0) {
            glyph_optimize_score_weights_default(local_options.score_mode, &local_options.weights);
        }
    }

    memset(&widths, 0, sizeof(widths));
    if (!glyph_optimize_collect_widths(table, &local_options, &widths)) {
        return 0;
    }
    mode_count = glyph_optimize_collect_sort_modes(&local_options, modes, 4u);
    if (mode_count == 0 || widths.count > UINT32_MAX / mode_count) {
        glyph_optimize_width_list_free(&widths);
        return 0;
    }

    memset(&set, 0, sizeof(set));
    set.best_index = GLYPH_OPTIMIZE_INDEX_NONE;
    set.count = widths.count * mode_count;
    if (!glyph_optimize_mul_size((size_t)set.count, sizeof(*set.items), &bytes)) {
        glyph_optimize_width_list_free(&widths);
        return 0;
    }
    set.items = (GlyphOptimizeCandidate *)calloc(1u, bytes);
    if (!set.items) {
        glyph_optimize_width_list_free(&widths);
        return 0;
    }

    for (mode_index = 0; mode_index < mode_count; mode_index++) {
        uint32_t *order = NULL;
        GlyphPackSortMode sort_mode = modes[mode_index];

        if (!glyph_optimize_make_order(table, sort_mode, &order)) {
            glyph_optimize_candidate_set_free(&set);
            glyph_optimize_width_list_free(&widths);
            return 0;
        }

        for (i = 0; i < widths.count; i++) {
            GlyphPackPlan *plan = NULL;
            GlyphOptimizeCandidate *candidate = &set.items[mode_index * widths.count + i];

            candidate->width = widths.items[i];
            candidate->sort_mode = sort_mode;
            candidate->pareto_rank = GLYPH_OPTIMIZE_INDEX_NONE;
            candidate->dominated_by = GLYPH_OPTIMIZE_INDEX_NONE;
            candidate->ok = glyph_pack_plan_simulate(table, order, table->count,
                                                     widths.items[i], sort_mode, &plan);
            if (candidate->ok && plan) {
                candidate->metrics = plan->metrics;
                candidate->atlas_area = glyph_optimize_area_u64(plan->atlas_width, plan->atlas_height);
                candidate->compact_score = glyph_optimize_score_metrics(&plan->metrics,
                                                                        &local_options.weights,
                                                                        widths.items[i], 0);
                candidate->stable_score = glyph_optimize_score_metrics(&plan->metrics,
                                                                       &local_options.weights,
                                                                       widths.items[i],
                                                                       plan->metrics.row_count);
                candidate->score = candidate->compact_score;
            } else {
                candidate->score = UINT64_MAX;
                candidate->compact_score = UINT64_MAX;
                candidate->stable_score = UINT64_MAX;
            }
            glyph_pack_plan_free(plan);
        }
        free(order);
    }

    glyph_optimize_rank_candidates(&set);
    qsort(set.items, set.count, sizeof(*set.items), glyph_optimize_candidate_score_cmp);
    glyph_optimize_rank_candidates(&set);
    glyph_optimize_width_list_free(&widths);
    *out_set = set;
    return set.best_index != GLYPH_OPTIMIZE_INDEX_NONE;
}

void glyph_optimize_candidate_set_free(GlyphOptimizeCandidateSet *set) {
    if (!set) {
        return;
    }
    free(set->items);
    memset(set, 0, sizeof(*set));
    set->best_index = GLYPH_OPTIMIZE_INDEX_NONE;
}

int glyph_optimize_create_best_plan(const GlyphTable *table,
                                    const GlyphOptimizeCandidateSet *candidates,
                                    GlyphPackPlan **out_plan) {
    const GlyphOptimizeCandidate *best;
    uint32_t *order = NULL;
    int ok;

    if (!out_plan) {
        return 0;
    }
    *out_plan = NULL;
    if (!glyph_optimize_valid_table(table) || !candidates || !candidates->items ||
        candidates->best_index >= candidates->count) {
        return 0;
    }
    best = &candidates->items[candidates->best_index];
    if (!best->ok) {
        return 0;
    }
    if (!glyph_optimize_make_order(table, best->sort_mode, &order)) {
        return 0;
    }
    ok = glyph_pack_plan_simulate(table, order, table->count, best->width,
                                  best->sort_mode, out_plan);
    free(order);
    return ok;
}

int glyph_optimize_analyze_row_splits(const GlyphPackPlan *plan,
                                      uint32_t max_splits,
                                      GlyphOptimizeRowSplitSet *out_splits) {
    GlyphOptimizeRowSplitSet set;
    uint32_t capacity;
    uint32_t row_index;
    size_t bytes;

    if (!out_splits) {
        return 0;
    }
    memset(out_splits, 0, sizeof(*out_splits));
    if (!plan || !plan->rows || !plan->placements || !plan->pack_order) {
        return 0;
    }
    capacity = max_splits ? max_splits : plan->glyph_count;
    if (capacity == 0) {
        return 1;
    }
    if (!glyph_optimize_mul_size((size_t)capacity, sizeof(*set.items), &bytes)) {
        return 0;
    }
    memset(&set, 0, sizeof(set));
    set.items = (GlyphOptimizeRowSplit *)calloc(1u, bytes);
    if (!set.items) {
        return 0;
    }

    for (row_index = 0; row_index < plan->row_count; row_index++) {
        const GlyphPackRowStats *row = &plan->rows[row_index];
        uint32_t order_pos;
        uint32_t seen = 0;
        uint32_t left_used = 0;
        uint32_t left_height = 0;
        uint64_t left_pixels = 0;
        uint64_t total_glyph_pixels = row->glyph_pixels;

        if (row->glyph_count < 2u || set.count >= capacity) {
            continue;
        }
        for (order_pos = 0; order_pos < plan->glyph_count; order_pos++) {
            uint32_t glyph_index = plan->pack_order[order_pos];
            const GlyphPackPlacement *placement;
            uint32_t right_used = 0;
            uint32_t right_height = 0;
            uint64_t right_pixels;
            uint32_t scan;
            GlyphOptimizeRowSplit split;

            if (glyph_index >= plan->glyph_count) {
                continue;
            }
            placement = &plan->placements[glyph_index];
            if (placement->row_index != row_index) {
                continue;
            }
            seen++;
            left_used += placement->width;
            left_height = glyph_optimize_max_u32(left_height, placement->height);
            left_pixels += placement->area;
            if (seen >= row->glyph_count) {
                break;
            }

            for (scan = order_pos + 1u; scan < plan->glyph_count; scan++) {
                uint32_t next_index = plan->pack_order[scan];
                const GlyphPackPlacement *next;
                if (next_index >= plan->glyph_count) {
                    continue;
                }
                next = &plan->placements[next_index];
                if (next->row_index != row_index) {
                    continue;
                }
                right_used += next->width;
                right_height = glyph_optimize_max_u32(right_height, next->height);
            }
            right_pixels = total_glyph_pixels >= left_pixels ? total_glyph_pixels - left_pixels : 0;
            if (right_used == 0 || right_height == 0) {
                continue;
            }

            memset(&split, 0, sizeof(split));
            split.row_index = row_index;
            split.split_after_order = order_pos;
            split.left_glyphs = seen;
            split.right_glyphs = row->glyph_count - seen;
            split.row_height = row->height;
            split.left_used = left_used;
            split.right_used = right_used;
            split.left_height = left_height;
            split.right_height = right_height;
            split.current_pixels = glyph_optimize_area_u64(plan->atlas_width, row->height);
            split.split_pixels = glyph_optimize_area_u64(plan->atlas_width, left_height + right_height);
            split.current_waste = row->waste_pixels;
            split.split_waste = split.split_pixels >= total_glyph_pixels ?
                split.split_pixels - total_glyph_pixels : 0;
            split.saved_pixels = (int64_t)split.current_waste - (int64_t)split.split_waste;
            split.current_waste_ratio = split.current_pixels ?
                (double)split.current_waste / (double)split.current_pixels : 0.0;
            split.split_waste_ratio = split.split_pixels ?
                (double)split.split_waste / (double)split.split_pixels : 0.0;

            if (split.saved_pixels > 0 ||
                (right_pixels > 0 && split.split_waste_ratio < split.current_waste_ratio)) {
                glyph_optimize_append_row_split(&set, split, capacity);
            }
            if (set.count >= capacity) {
                break;
            }
        }
    }

    qsort(set.items, set.count, sizeof(*set.items), glyph_optimize_row_split_cmp);
    *out_splits = set;
    return 1;
}

void glyph_optimize_row_split_set_free(GlyphOptimizeRowSplitSet *set) {
    if (!set) {
        return;
    }
    free(set->items);
    memset(set, 0, sizeof(*set));
}

int glyph_optimize_suggest_reorder(const GlyphTable *table,
                                   const GlyphPackPlan *plan,
                                   uint32_t max_suggestions,
                                   GlyphOptimizeReorderSet *out_suggestions) {
    GlyphOptimizeReorderSet set;
    uint32_t capacity;
    uint32_t row_index;
    size_t bytes;

    if (!out_suggestions) {
        return 0;
    }
    memset(out_suggestions, 0, sizeof(*out_suggestions));
    if (!glyph_optimize_valid_table(table) || !plan || !plan->rows ||
        !plan->placements || !plan->pack_order || plan->glyph_count != table->count) {
        return 0;
    }
    capacity = max_suggestions ? max_suggestions : plan->glyph_count;
    if (capacity == 0) {
        return 1;
    }
    if (!glyph_optimize_mul_size((size_t)capacity, sizeof(*set.items), &bytes)) {
        return 0;
    }
    memset(&set, 0, sizeof(set));
    set.items = (GlyphOptimizeReorderSuggestion *)calloc(1u, bytes);
    if (!set.items) {
        return 0;
    }

    for (row_index = 0; row_index < plan->row_count && set.count < capacity; row_index++) {
        const GlyphPackRowStats *row = &plan->rows[row_index];
        uint32_t min_height = 0;
        uint32_t max_height = 0;
        uint32_t order_pos;

        if (row->glyph_count < 2u) {
            continue;
        }
        glyph_optimize_row_min_max_height(plan, row_index, &min_height, &max_height);
        if (max_height <= min_height) {
            continue;
        }

        for (order_pos = 0; order_pos < plan->glyph_count && set.count < capacity; order_pos++) {
            uint32_t glyph_index = plan->pack_order[order_pos];
            const GlyphPackPlacement *placement;
            GlyphOptimizeReorderSuggestion suggestion;
            uint32_t target_order;
            uint64_t height_gap;

            if (glyph_index >= plan->glyph_count) {
                continue;
            }
            placement = &plan->placements[glyph_index];
            if (placement->row_index != row_index) {
                continue;
            }
            height_gap = glyph_optimize_absdiff_u32(max_height, placement->height);
            if (height_gap == 0 || row->internal_waste_pixels == 0) {
                continue;
            }

            memset(&suggestion, 0, sizeof(suggestion));
            suggestion.glyph_index = glyph_index;
            suggestion.glyph_id = table->entries[glyph_index].id;
            suggestion.from_order = order_pos;
            suggestion.from_row = row_index;
            suggestion.to_row = row_index;
            suggestion.estimated_gain = glyph_optimize_sat_mul((uint64_t)placement->width, height_gap);
            suggestion.confidence = row->waste_pixels ?
                (double)suggestion.estimated_gain / (double)row->waste_pixels : 0.0;

            if (placement->height * 4u < max_height * 3u) {
                target_order = row->first_order + row->glyph_count - 1u;
                suggestion.kind = GLYPH_OPTIMIZE_REORDER_LATER;
            } else if (order_pos > row->first_order) {
                target_order = row->first_order;
                suggestion.kind = GLYPH_OPTIMIZE_REORDER_EARLIER;
            } else {
                target_order = order_pos;
                suggestion.kind = GLYPH_OPTIMIZE_REORDER_NEAR_SIMILAR;
            }
            suggestion.to_order = glyph_optimize_min_u32(target_order, plan->glyph_count ? plan->glyph_count - 1u : 0);
            if (suggestion.estimated_gain > 0) {
                glyph_optimize_append_reorder(&set, suggestion, capacity);
            }
        }
    }

    qsort(set.items, set.count, sizeof(*set.items), glyph_optimize_reorder_cmp);
    *out_suggestions = set;
    return 1;
}

void glyph_optimize_reorder_set_free(GlyphOptimizeReorderSet *set) {
    if (!set) {
        return;
    }
    free(set->items);
    memset(set, 0, sizeof(*set));
}

int glyph_optimize_explain_waste(const GlyphPackPlan *plan,
                                 uint32_t max_explanations,
                                 GlyphOptimizeWasteExplanationSet *out_explanations) {
    GlyphOptimizeWasteExplanationSet set;
    uint32_t capacity;
    uint32_t row_index;
    size_t bytes;

    if (!out_explanations) {
        return 0;
    }
    memset(out_explanations, 0, sizeof(*out_explanations));
    if (!plan || !plan->rows || !plan->placements) {
        return 0;
    }
    capacity = max_explanations ? max_explanations : plan->row_count * 3u;
    if (capacity == 0) {
        return 1;
    }
    if (!glyph_optimize_mul_size((size_t)capacity, sizeof(*set.items), &bytes)) {
        return 0;
    }
    memset(&set, 0, sizeof(set));
    set.items = (GlyphOptimizeWasteExplanation *)calloc(1u, bytes);
    if (!set.items) {
        return 0;
    }

    for (row_index = 0; row_index < plan->row_count && set.count < capacity; row_index++) {
        const GlyphPackRowStats *row = &plan->rows[row_index];
        GlyphOptimizeWasteExplanation explanation;
        uint32_t min_height = 0;
        uint32_t max_height = 0;

        glyph_optimize_row_min_max_height(plan, row_index, &min_height, &max_height);
        if (row->horizontal_waste_pixels) {
            memset(&explanation, 0, sizeof(explanation));
            explanation.row_index = row_index;
            explanation.kind = row->glyph_count == 0 ? GLYPH_OPTIMIZE_WASTE_SPARSE_ROW :
                GLYPH_OPTIMIZE_WASTE_HORIZONTAL;
            explanation.pixels = row->horizontal_waste_pixels;
            explanation.ratio = row->row_pixels ?
                (double)row->horizontal_waste_pixels / (double)row->row_pixels : 0.0;
            explanation.glyph_count = row->glyph_count;
            explanation.row_height = row->height;
            explanation.row_used = row->used;
            explanation.tallest_glyph = max_height;
            explanation.shortest_glyph = min_height;
            glyph_optimize_append_waste(&set, explanation, capacity);
        }
        if (row->internal_waste_pixels && set.count < capacity) {
            memset(&explanation, 0, sizeof(explanation));
            explanation.row_index = row_index;
            explanation.kind = max_height > min_height ? GLYPH_OPTIMIZE_WASTE_TALL_ROW :
                GLYPH_OPTIMIZE_WASTE_INTERNAL;
            explanation.pixels = row->internal_waste_pixels;
            explanation.ratio = row->row_pixels ?
                (double)row->internal_waste_pixels / (double)row->row_pixels : 0.0;
            explanation.glyph_count = row->glyph_count;
            explanation.row_height = row->height;
            explanation.row_used = row->used;
            explanation.tallest_glyph = max_height;
            explanation.shortest_glyph = min_height;
            glyph_optimize_append_waste(&set, explanation, capacity);
        }
        if (row->glyph_count <= 1u && row->waste_pixels && set.count < capacity) {
            memset(&explanation, 0, sizeof(explanation));
            explanation.row_index = row_index;
            explanation.kind = GLYPH_OPTIMIZE_WASTE_SPARSE_ROW;
            explanation.pixels = row->waste_pixels;
            explanation.ratio = row->waste_ratio;
            explanation.glyph_count = row->glyph_count;
            explanation.row_height = row->height;
            explanation.row_used = row->used;
            explanation.tallest_glyph = max_height;
            explanation.shortest_glyph = min_height;
            glyph_optimize_append_waste(&set, explanation, capacity);
        }
    }

    qsort(set.items, set.count, sizeof(*set.items), glyph_optimize_waste_cmp);
    *out_explanations = set;
    return 1;
}

void glyph_optimize_waste_explanation_set_free(GlyphOptimizeWasteExplanationSet *set) {
    if (!set) {
        return;
    }
    free(set->items);
    memset(set, 0, sizeof(*set));
}

int glyph_optimize_compare_placement_stability(const GlyphPackPlan *before,
                                               const GlyphPackPlan *after,
                                               GlyphOptimizePlacementStability *out_stability) {
    GlyphOptimizePlacementStability stability;
    uint32_t count;
    uint32_t i;

    if (!out_stability) {
        return 0;
    }
    memset(out_stability, 0, sizeof(*out_stability));
    if (!before || !after || !before->placements || !after->placements) {
        return 0;
    }

    memset(&stability, 0, sizeof(stability));
    count = glyph_optimize_max_u32(before->glyph_count, after->glyph_count);
    stability.glyph_count = count;
    stability.max_manhattan_glyph_index = GLYPH_OPTIMIZE_INDEX_NONE;

    for (i = 0; i < count; i++) {
        const GlyphPackPlacement *a = i < before->glyph_count ? &before->placements[i] : NULL;
        const GlyphPackPlacement *b = i < after->glyph_count ? &after->placements[i] : NULL;
        uint64_t distance;

        if (!a || a->glyph_index != i) {
            stability.missing_before++;
            continue;
        }
        if (!b || b->glyph_index != i) {
            stability.missing_after++;
            continue;
        }
        stability.matched_glyphs++;
        distance = glyph_optimize_absdiff_u32(a->placement.x, b->placement.x) +
            glyph_optimize_absdiff_u32(a->placement.y, b->placement.y);
        stability.total_manhattan_distance += distance;
        if (distance == 0 && a->row_index == b->row_index) {
            stability.unchanged++;
        } else {
            stability.moved++;
        }
        if (a->row_index != b->row_index) {
            stability.row_changed++;
        }
        if (distance > stability.max_manhattan_distance) {
            stability.max_manhattan_distance = distance > UINT32_MAX ? UINT32_MAX : (uint32_t)distance;
            stability.max_manhattan_glyph_index = i;
        }
    }

    if (stability.matched_glyphs) {
        stability.unchanged_ratio = (double)stability.unchanged / (double)stability.matched_glyphs;
        stability.row_changed_ratio = (double)stability.row_changed / (double)stability.matched_glyphs;
        stability.mean_manhattan_distance =
            (double)stability.total_manhattan_distance / (double)stability.matched_glyphs;
    }
    *out_stability = stability;
    return 1;
}

int glyph_optimize_build_report(const GlyphTable *table,
                                const GlyphOptimizeOptions *options,
                                GlyphOptimizeReport *out_report) {
    GlyphOptimizeOptions local_options;
    GlyphOptimizeReport report;
    uint32_t limit;

    if (!out_report) {
        return 0;
    }
    memset(out_report, 0, sizeof(*out_report));
    if (!glyph_optimize_valid_table(table)) {
        return 0;
    }
    glyph_optimize_options_default(&local_options);
    if (options) {
        local_options = *options;
        if (local_options.max_reports_per_section == 0) {
            local_options.max_reports_per_section = 16u;
        }
        if (local_options.max_width_candidates == 0) {
            local_options.max_width_candidates = 64u;
        }
        if (local_options.weights.atlas_area_weight == 0 &&
            local_options.weights.waste_weight == 0 &&
            local_options.weights.height_weight == 0 &&
            local_options.weights.row_weight == 0 &&
            local_options.weights.width_weight == 0 &&
            local_options.weights.instability_weight == 0) {
            glyph_optimize_score_weights_default(local_options.score_mode, &local_options.weights);
        }
    }
    memset(&report, 0, sizeof(report));
    if (!glyph_optimize_generate_candidates(table, &local_options, &report.candidates)) {
        glyph_optimize_report_free(&report);
        return 0;
    }
    if (!glyph_optimize_create_best_plan(table, &report.candidates, &report.best_plan)) {
        glyph_optimize_report_free(&report);
        return 0;
    }
    limit = local_options.max_reports_per_section;
    if (!glyph_optimize_analyze_row_splits(report.best_plan, limit, &report.row_splits) ||
        !glyph_optimize_suggest_reorder(table, report.best_plan, limit, &report.reorder_suggestions) ||
        !glyph_optimize_explain_waste(report.best_plan, limit, &report.waste_explanations)) {
        glyph_optimize_report_free(&report);
        return 0;
    }
    *out_report = report;
    return 1;
}

void glyph_optimize_report_free(GlyphOptimizeReport *report) {
    if (!report) {
        return;
    }
    glyph_optimize_candidate_set_free(&report->candidates);
    glyph_pack_plan_free(report->best_plan);
    glyph_optimize_row_split_set_free(&report->row_splits);
    glyph_optimize_reorder_set_free(&report->reorder_suggestions);
    glyph_optimize_waste_explanation_set_free(&report->waste_explanations);
    memset(report, 0, sizeof(*report));
}

static void glyph_optimize_write_candidate(FILE *fp,
                                           const GlyphOptimizeCandidate *candidate,
                                           uint32_t index) {
    fprintf(fp,
            "  %u: width=%u sort=%s ok=%d rank=%u score=%" PRIu64
            " atlas=%ux%u area=%" PRIu64 " rows=%u waste=%" PRIu64
            " coverage=%.4f utilization=%.4f",
            index,
            candidate->width,
            glyph_pack_sort_mode_name(candidate->sort_mode),
            candidate->ok,
            candidate->pareto_rank,
            candidate->score,
            candidate->metrics.atlas_width,
            candidate->metrics.atlas_height,
            candidate->atlas_area,
            candidate->metrics.row_count,
            candidate->metrics.waste_pixels,
            candidate->metrics.glyph_coverage,
            candidate->metrics.row_utilization);
    if (candidate->dominated_by != GLYPH_OPTIMIZE_INDEX_NONE) {
        fprintf(fp, " dominated-by=%u", candidate->dominated_by);
    }
    fprintf(fp, "\n");
}

static void glyph_optimize_write_splits(FILE *fp, const GlyphOptimizeRowSplitSet *splits) {
    uint32_t i;

    fprintf(fp, "\nrow split analysis: %u\n", splits ? splits->count : 0);
    if (!splits || !splits->items) {
        return;
    }
    for (i = 0; i < splits->count; i++) {
        const GlyphOptimizeRowSplit *split = &splits->items[i];
        fprintf(fp,
                "  row=%u split-after-order=%u glyphs=%u/%u used=%u/%u heights=%u/%u"
                " waste=%" PRIu64 "->%" PRIu64 " saved=%" PRId64
                " ratio=%.4f->%.4f\n",
                split->row_index,
                split->split_after_order,
                split->left_glyphs,
                split->right_glyphs,
                split->left_used,
                split->right_used,
                split->left_height,
                split->right_height,
                split->current_waste,
                split->split_waste,
                split->saved_pixels,
                split->current_waste_ratio,
                split->split_waste_ratio);
    }
}

static void glyph_optimize_write_reorders(FILE *fp, const GlyphOptimizeReorderSet *reorders) {
    uint32_t i;

    fprintf(fp, "\nreorder suggestions: %u\n", reorders ? reorders->count : 0);
    if (!reorders || !reorders->items) {
        return;
    }
    for (i = 0; i < reorders->count; i++) {
        const GlyphOptimizeReorderSuggestion *suggestion = &reorders->items[i];
        fprintf(fp,
                "  glyph=%u id=%u %s order=%u->%u row=%u->%u gain=%" PRIu64
                " confidence=%.4f\n",
                suggestion->glyph_index,
                suggestion->glyph_id,
                glyph_optimize_reorder_kind_name(suggestion->kind),
                suggestion->from_order,
                suggestion->to_order,
                suggestion->from_row,
                suggestion->to_row,
                suggestion->estimated_gain,
                suggestion->confidence);
    }
}

static void glyph_optimize_write_waste(FILE *fp, const GlyphOptimizeWasteExplanationSet *waste) {
    uint32_t i;

    fprintf(fp, "\nwaste explanations: %u\n", waste ? waste->count : 0);
    if (!waste || !waste->items) {
        return;
    }
    for (i = 0; i < waste->count; i++) {
        const GlyphOptimizeWasteExplanation *explanation = &waste->items[i];
        fprintf(fp,
                "  row=%u kind=%s pixels=%" PRIu64 " ratio=%.4f glyphs=%u"
                " height=%u used=%u tallest=%u shortest=%u\n",
                explanation->row_index,
                glyph_optimize_waste_kind_name(explanation->kind),
                explanation->pixels,
                explanation->ratio,
                explanation->glyph_count,
                explanation->row_height,
                explanation->row_used,
                explanation->tallest_glyph,
                explanation->shortest_glyph);
    }
}

static void glyph_optimize_write_stability(FILE *fp,
                                           const GlyphOptimizePlacementStability *stability) {
    fprintf(fp,
            "\nplacement stability: glyphs=%u matched=%u unchanged=%u moved=%u"
            " row-changed=%u missing-before=%u missing-after=%u"
            " unchanged-ratio=%.4f row-change-ratio=%.4f mean-distance=%.3f"
            " max-distance=%u@%u\n",
            stability->glyph_count,
            stability->matched_glyphs,
            stability->unchanged,
            stability->moved,
            stability->row_changed,
            stability->missing_before,
            stability->missing_after,
            stability->unchanged_ratio,
            stability->row_changed_ratio,
            stability->mean_manhattan_distance,
            stability->max_manhattan_distance,
            stability->max_manhattan_glyph_index);
}

int glyph_optimize_write_text_report(FILE *fp,
                                     const GlyphOptimizeReport *report,
                                     const GlyphPackPlan *baseline) {
    uint32_t i;
    GlyphOptimizePlacementStability stability;

    if (!fp || !report) {
        return 0;
    }
    fprintf(fp, "Glyph atlas optimization report\n");
    fprintf(fp, "candidates: %u\n", report->candidates.count);
    fprintf(fp, "pareto-front: %u\n", report->candidates.pareto_count);
    if (report->candidates.best_index != GLYPH_OPTIMIZE_INDEX_NONE &&
        report->candidates.best_index < report->candidates.count) {
        const GlyphOptimizeCandidate *best = &report->candidates.items[report->candidates.best_index];
        fprintf(fp,
                "best: index=%u width=%u sort=%s score=%" PRIu64
                " atlas=%ux%u rows=%u waste=%" PRIu64 " waste-ratio=%.4f\n",
                report->candidates.best_index,
                best->width,
                glyph_pack_sort_mode_name(best->sort_mode),
                best->score,
                best->metrics.atlas_width,
                best->metrics.atlas_height,
                best->metrics.row_count,
                best->metrics.waste_pixels,
                best->metrics.waste_ratio);
    }

    fprintf(fp, "\ncandidate ranking\n");
    if (report->candidates.items) {
        for (i = 0; i < report->candidates.count; i++) {
            glyph_optimize_write_candidate(fp, &report->candidates.items[i], i);
        }
    }

    if (report->best_plan) {
        fprintf(fp, "\nbest plan summary\n");
        fprintf(fp,
                "  sort=%s atlas=%ux%u glyphs=%u rows=%u glyph-pixels=%" PRIu64
                " row-pixels=%" PRIu64 " waste=%" PRIu64
                " horizontal=%" PRIu64 " internal=%" PRIu64
                " coverage=%.4f utilization=%.4f\n",
                glyph_pack_sort_mode_name(report->best_plan->sort_mode),
                report->best_plan->atlas_width,
                report->best_plan->atlas_height,
                report->best_plan->glyph_count,
                report->best_plan->row_count,
                report->best_plan->metrics.glyph_pixels,
                report->best_plan->metrics.row_pixels,
                report->best_plan->metrics.waste_pixels,
                report->best_plan->metrics.horizontal_waste_pixels,
                report->best_plan->metrics.internal_waste_pixels,
                report->best_plan->metrics.glyph_coverage,
                report->best_plan->metrics.row_utilization);
    }

    glyph_optimize_write_splits(fp, &report->row_splits);
    glyph_optimize_write_reorders(fp, &report->reorder_suggestions);
    glyph_optimize_write_waste(fp, &report->waste_explanations);

    if (baseline && report->best_plan &&
        glyph_optimize_compare_placement_stability(baseline, report->best_plan, &stability)) {
        glyph_optimize_write_stability(fp, &stability);
    }

    return ferror(fp) == 0;
}
