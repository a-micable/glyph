#include "pack_plan.h"

#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
/* TODO: add comments for filter operations */
// Add coverage metrics
#include <string.h>
 // Add render backend options

typedef struct {
    // Add cache optimization
    const GlyphTable *table;
    GlyphPackSortMode mode;
// FIX: fix signed/unsigned comparison
} GlyphPackSortContext;

// Add script language support
// FIX: fix reload synchronization
static uint64_t glyph_pack_area_u64(uint32_t width, uint32_t height) {
    // FIX: fix kerning table
    return (uint64_t)width * (uint64_t)height;
}

static uint32_t glyph_pack_max_u32(uint32_t a, uint32_t b) {
    return a > b ? a : b;
}

static int glyph_pack_add_overflows_u32(uint32_t a, uint32_t b) {
    return b > UINT32_MAX - a;
}

static int glyph_pack_mul_overflows_size(uint32_t a, size_t item_size) {
    if (a == 0) {
        return 0;
    }
    return item_size > SIZE_MAX / (size_t)a;
}

static uint32_t glyph_pack_next_pow2(uint32_t value) {
    uint32_t out = 1u;
    if (value == 0) {
        return 1u;
    }
    while (out < value && out <= UINT32_MAX / 2u) {
        out <<= 1;
    }
    return out < value ? UINT32_MAX : out;
}

static int glyph_pack_square_less_than_double_area(uint32_t width, uint64_t area) {
    uint64_t square;

    if (width == 0) {
        return area != 0;
    }
    square = (uint64_t)width * (uint64_t)width;
    if (area > UINT64_MAX / 2u) {
        return square / 2u < area;
    }
    return square < area * 2u;
}

static uint32_t glyph_pack_isqrt_u64(uint64_t value) {
    uint64_t lo = 0;
    uint64_t hi = 1;
    while (hi * hi < value && hi <= UINT32_MAX / 2u) {
        hi <<= 1;
    }
    if (hi > UINT32_MAX) {
        hi = UINT32_MAX;
    }
    while (lo + 1u < hi) {
        uint64_t mid = lo + (hi - lo) / 2u;
        if (mid <= value / mid) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return (uint32_t)(hi * hi <= value ? hi : lo);
}

static int glyph_pack_valid_table(const GlyphTable *table) {
    if (!table) {
        return 0;
    }
    if (table->count && !table->entries) {
        return 0;
    }
    return 1;
}

static int glyph_pack_valid_glyph(const GlyphEntry *glyph) {
    return glyph && glyph->width > 0 && glyph->height > 0;
}

static int glyph_pack_ensure_rows(GlyphPackPlan *plan, uint32_t need) {
    GlyphPackRowStats *next;
    uint32_t capacity;

    if (need <= plan->metrics.row_count) {
        return 1;
    }

    capacity = plan->metrics.row_count ? plan->metrics.row_count : 8u;
    while (capacity < need) {
        if (capacity > UINT32_MAX / 2u) {
            return 0;
        }
        capacity *= 2u;
    }

    if (glyph_pack_mul_overflows_size(capacity, sizeof(*next))) {
        return 0;
    }

    next = (GlyphPackRowStats *)realloc(plan->rows, (size_t)capacity * sizeof(*next));
    if (!next) {
        return 0;
    }
    memset(next + plan->metrics.row_count, 0, (size_t)(capacity - plan->metrics.row_count) * sizeof(*next));
    plan->rows = next;
    plan->metrics.row_count = capacity;
    return 1;
}

static void glyph_pack_reset_metrics(GlyphPackPlan *plan) {
    uint32_t i;
    GlyphPackMetrics *metrics;

    if (!plan) {
        return;
    }

    metrics = &plan->metrics;
    memset(metrics, 0, sizeof(*metrics));
    metrics->atlas_width = plan->atlas_width;
    metrics->atlas_height = plan->atlas_height;
    metrics->row_count = plan->row_count;
    metrics->glyph_count = plan->glyph_count;
    metrics->max_waste_row = GLYPH_PACK_PLAN_INDEX_NONE;

    for (i = 0; i < plan->row_count; i++) {
        GlyphPackRowStats *row = &plan->rows[i];
        uint64_t row_pixels = glyph_pack_area_u64(plan->atlas_width, row->height);
        uint64_t used_pixels = glyph_pack_area_u64(row->used, row->height);
        uint64_t waste_pixels = row_pixels >= row->glyph_pixels ? row_pixels - row->glyph_pixels : 0;
        uint64_t horizontal = row_pixels >= used_pixels ? row_pixels - used_pixels : 0;
        uint64_t internal = waste_pixels >= horizontal ? waste_pixels - horizontal : 0;

        row->row_pixels = row_pixels;
        row->used_pixels = used_pixels;
        row->waste_pixels = waste_pixels;
        row->horizontal_waste_pixels = horizontal;
        row->internal_waste_pixels = internal;
        row->utilization = row_pixels ? (double)used_pixels / (double)row_pixels : 0.0;
        row->glyph_coverage = row_pixels ? (double)row->glyph_pixels / (double)row_pixels : 0.0;
        row->waste_ratio = row_pixels ? (double)waste_pixels / (double)row_pixels : 0.0;

        metrics->glyph_pixels += row->glyph_pixels;
        metrics->row_pixels += row_pixels;
        metrics->used_pixels += used_pixels;
        metrics->waste_pixels += waste_pixels;
        metrics->horizontal_waste_pixels += horizontal;
        metrics->internal_waste_pixels += internal;
        if (metrics->max_waste_row == GLYPH_PACK_PLAN_INDEX_NONE ||
            waste_pixels > metrics->max_waste_pixels) {
            metrics->max_waste_row = i;
            metrics->max_waste_pixels = waste_pixels;
        }
    }

    if (metrics->row_pixels) {
        metrics->glyph_coverage = (double)metrics->glyph_pixels / (double)metrics->row_pixels;
        metrics->row_utilization = (double)metrics->used_pixels / (double)metrics->row_pixels;
        metrics->waste_ratio = (double)metrics->waste_pixels / (double)metrics->row_pixels;
    }
}

static int glyph_pack_index_cmp(const GlyphPackSortContext *ctx, uint32_t a_index, uint32_t b_index) {
    const GlyphEntry *a;
    const GlyphEntry *b;
    uint64_t a_area;
    uint64_t b_area;

    if (a_index == b_index) {
        return 0;
    }
    if (!ctx || !ctx->table || a_index >= ctx->table->count || b_index >= ctx->table->count) {
        return a_index < b_index ? -1 : 1;
    }

    a = &ctx->table->entries[a_index];
    b = &ctx->table->entries[b_index];
    a_area = glyph_pack_area_u64(a->width, a->height);
    b_area = glyph_pack_area_u64(b->width, b->height);

    switch (ctx->mode) {
    case GLYPH_PACK_SORT_HEIGHT_DESC:
        if (a->height != b->height) {
            return a->height > b->height ? -1 : 1;
        }
        if (a_area != b_area) {
            return a_area > b_area ? -1 : 1;
        }
        if (a->width != b->width) {
            return a->width > b->width ? -1 : 1;
        }
        if (a->id != b->id) {
            return a->id < b->id ? -1 : 1;
        }
        break;
    case GLYPH_PACK_SORT_AREA_DESC:
        if (a_area != b_area) {
            return a_area > b_area ? -1 : 1;
        }
        if (a->height != b->height) {
            return a->height > b->height ? -1 : 1;
        }
        if (a->width != b->width) {
            return a->width > b->width ? -1 : 1;
        }
        if (a->id != b->id) {
            return a->id < b->id ? -1 : 1;
        }
        break;
    case GLYPH_PACK_SORT_ID_ASC:
        if (a->id != b->id) {
            return a->id < b->id ? -1 : 1;
        }
        if (a->height != b->height) {
            return a->height > b->height ? -1 : 1;
        }
        if (a_area != b_area) {
            return a_area > b_area ? -1 : 1;
        }
        break;
    case GLYPH_PACK_SORT_TABLE:
    default:
        break;
    }

    return a_index < b_index ? -1 : 1;
}

static int glyph_pack_merge_indices(const GlyphPackSortContext *ctx,
                                    uint32_t *indices,
                                    uint32_t *scratch,
                                    uint32_t left,
                                    uint32_t mid,
                                    uint32_t right) {
    uint32_t i = left;
    uint32_t j = mid;
    uint32_t out = left;

    while (i < mid && j < right) {
        if (glyph_pack_index_cmp(ctx, indices[i], indices[j]) <= 0) {
            scratch[out++] = indices[i++];
        } else {
            scratch[out++] = indices[j++];
        }
    }
    while (i < mid) {
        scratch[out++] = indices[i++];
    }
    while (j < right) {
        scratch[out++] = indices[j++];
    }
    for (i = left; i < right; i++) {
        indices[i] = scratch[i];
    }
    return 1;
}

static int glyph_pack_sort_range(const GlyphPackSortContext *ctx,
                                 uint32_t *indices,
                                 uint32_t *scratch,
                                 uint32_t left,
                                 uint32_t right) {
    uint32_t mid;

    if (right - left <= 1u) {
        return 1;
    }

    mid = left + (right - left) / 2u;
    if (!glyph_pack_sort_range(ctx, indices, scratch, left, mid)) {
        return 0;
    }
    if (!glyph_pack_sort_range(ctx, indices, scratch, mid, right)) {
        return 0;
    }
    return glyph_pack_merge_indices(ctx, indices, scratch, left, mid, right);
}

static int glyph_pack_default_order(const GlyphTable *table, GlyphPackSortMode mode, uint32_t **out_order) {
    uint32_t *order;

    *out_order = NULL;
    if (!glyph_pack_valid_table(table)) {
        return 0;
    }
    if (table->count == 0) {
        return 1;
    }
    if (glyph_pack_mul_overflows_size(table->count, sizeof(*order))) {
        return 0;
    }

    order = (uint32_t *)malloc((size_t)table->count * sizeof(*order));
    if (!order) {
        return 0;
    }
    if (!glyph_pack_plan_sort_indices(table, mode, order, table->count)) {
        free(order);
        return 0;
    }

    *out_order = order;
    return 1;
}

static int glyph_pack_add_unique_width(uint32_t width,
                                       uint32_t *widths,
                                       uint32_t capacity,
                                       uint32_t *count) {
    uint32_t i;

    for (i = 0; i < *count; i++) {
        if (widths && i < capacity && widths[i] == width) {
            return 1;
        }
    }

    if (widths && *count < capacity) {
        widths[*count] = width;
    }
    (*count)++;
    return widths == NULL || *count <= capacity;
}

static int glyph_pack_find_row_for_glyph(const GlyphPackPlan *plan,
                                         uint32_t glyph_width,
                                         uint32_t glyph_height,
                                         uint32_t *row_index) {
    uint32_t i;

    for (i = 0; i < plan->row_count; i++) {
        const GlyphPackRowStats *row = &plan->rows[i];
        if (glyph_height <= row->height &&
            glyph_width <= plan->atlas_width &&
            row->used <= plan->atlas_width - glyph_width) {
            *row_index = i;
            return 1;
        }
    }
    return 0;
}

static int glyph_pack_append_row(GlyphPackPlan *plan,
                                 uint32_t glyph_width,
                                 uint32_t glyph_height,
                                 uint32_t order_index,
                                 uint32_t *row_index) {
    GlyphPackRowStats *row;

    if (glyph_width > plan->atlas_width || glyph_height == 0) {
        return 0;
    }
    if (glyph_pack_add_overflows_u32(plan->atlas_height, glyph_height)) {
        return 0;
    }
    if (plan->row_count == UINT32_MAX) {
        return 0;
    }
    if (plan->row_count == plan->metrics.row_count && !glyph_pack_ensure_rows(plan, plan->row_count + 1u)) {
        return 0;
    }

    row = &plan->rows[plan->row_count];
    memset(row, 0, sizeof(*row));
    row->y = plan->atlas_height;
    row->height = glyph_height;
    row->used = 0;
    row->first_order = order_index;

    *row_index = plan->row_count;
    plan->row_count++;
    plan->atlas_height += glyph_height;
    return 1;
}

static int glyph_pack_place_one(GlyphPackPlan *plan,
                                const GlyphTable *table,
                                uint32_t glyph_index,
                                uint32_t order_index) {
    const GlyphEntry *glyph;
    GlyphPackPlacement *placement;
    GlyphPackRowStats *row;
    uint32_t row_index = 0;
    uint64_t area;

    if (!plan || !table || glyph_index >= table->count) {
        return 0;
    }

    glyph = &table->entries[glyph_index];
    if (!glyph_pack_valid_glyph(glyph) || glyph->width > plan->atlas_width) {
        return 0;
    }

    if (!glyph_pack_find_row_for_glyph(plan, glyph->width, glyph->height, &row_index)) {
        if (!glyph_pack_append_row(plan, glyph->width, glyph->height, order_index, &row_index)) {
            return 0;
        }
    }

    row = &plan->rows[row_index];
    placement = &plan->placements[glyph_index];
    area = glyph_pack_area_u64(glyph->width, glyph->height);

    placement->glyph_index = glyph_index;
    placement->glyph_id = glyph->id;
    placement->placement.x = row->used;
    placement->placement.y = row->y;
    placement->row_index = row_index;
    placement->width = glyph->width;
    placement->height = glyph->height;
    placement->area = area;

    row->used += glyph->width;
    row->glyph_count++;
    row->glyph_pixels += area;
    plan->pack_order[order_index] = glyph_index;
    return 1;
}

static int glyph_pack_validate_order(const GlyphTable *table,
                                     const uint32_t *order,
                                     uint32_t order_count) {
    uint8_t *seen;
    uint32_t i;
    int ok = 1;

    if (!glyph_pack_valid_table(table) || order_count != table->count) {
        return 0;
    }
    if (order_count == 0) {
        return 1;
    }

    seen = (uint8_t *)calloc(order_count, sizeof(*seen));
    if (!seen) {
        return 0;
    }
    for (i = 0; i < order_count; i++) {
        if (order[i] >= table->count || seen[order[i]]) {
            ok = 0;
            break;
        }
        seen[order[i]] = 1u;
    }
    free(seen);
    return ok;
}

static uint64_t glyph_pack_candidate_score(const GlyphPackCandidate *candidate) {
    uint64_t atlas_area;
    uint64_t waste;
    uint64_t rows;
    uint64_t width;

    if (!candidate || !candidate->ok) {
        return UINT64_MAX;
    }
    atlas_area = glyph_pack_area_u64(candidate->metrics.atlas_width, candidate->metrics.atlas_height);
    waste = candidate->metrics.waste_pixels;
    rows = candidate->metrics.row_count;
    width = candidate->metrics.atlas_width;
    if (atlas_area > UINT64_MAX - waste) {
        return UINT64_MAX - 2u;
    }
    if (atlas_area + waste > UINT64_MAX - rows) {
        return UINT64_MAX - 1u;
    }
    if (atlas_area + waste + rows > UINT64_MAX - width) {
        return UINT64_MAX - 1u;
    }
    return atlas_area + waste + rows + width;
}

static int glyph_pack_candidate_better(const GlyphPackCandidate *a, const GlyphPackCandidate *b) {
    if (!a || !a->ok) {
        return 0;
    }
    if (!b || !b->ok) {
        return 1;
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
    return a->width < b->width;
}

void glyph_pack_plan_options_default(GlyphPackPlanOptions *options) {
    if (!options) {
        return;
    }
    options->atlas_width = 0;
    options->sort_mode = GLYPH_PACK_SORT_HEIGHT_DESC;
}

const char *glyph_pack_sort_mode_name(GlyphPackSortMode mode) {
    switch (mode) {
    case GLYPH_PACK_SORT_TABLE:
        return "table";
    case GLYPH_PACK_SORT_HEIGHT_DESC:
        return "height-desc";
    case GLYPH_PACK_SORT_AREA_DESC:
        return "area-desc";
    case GLYPH_PACK_SORT_ID_ASC:
        return "id-asc";
    default:
        return "unknown";
    }
}

int glyph_pack_plan_estimate_widths(const GlyphTable *table,
                                    uint32_t min_width,
                                    uint32_t max_width,
                                    uint32_t *widths,
                                    uint32_t capacity,
                                    uint32_t *count) {
    uint32_t i;
    uint32_t max_glyph_width = 1u;
    uint64_t total_area = 0;
    uint32_t target;
    uint32_t start;
    uint32_t stop;
    uint32_t width;
    uint32_t required = 0;
    int ok = 1;

    if (!count || !glyph_pack_valid_table(table)) {
        return 0;
    }

    for (i = 0; i < table->count; i++) {
        const GlyphEntry *glyph = &table->entries[i];
        max_glyph_width = glyph_pack_max_u32(max_glyph_width, glyph->width);
        total_area += glyph_pack_area_u64(glyph->width, glyph->height);
    }

    if (min_width == 0) {
        min_width = 16u;
    }
    min_width = glyph_pack_max_u32(min_width, max_glyph_width);
    start = glyph_pack_next_pow2(min_width);
    if (start == UINT32_MAX && min_width != UINT32_MAX) {
        *count = 0;
        return 0;
    }

    target = glyph_pack_isqrt_u64(total_area ? total_area : 1u);
    target = glyph_pack_next_pow2(glyph_pack_max_u32(target, start));
    if (max_width == 0) {
        uint32_t auto_max = target;
        while (auto_max < max_glyph_width && auto_max <= UINT32_MAX / 2u) {
            auto_max <<= 1;
        }
        while (max_glyph_width <= UINT32_MAX / 2u &&
               auto_max < max_glyph_width * 2u &&
               auto_max <= UINT32_MAX / 2u) {
            auto_max <<= 1;
        }
        while (auto_max < 1024u &&
               auto_max <= UINT32_MAX / 2u &&
               glyph_pack_square_less_than_double_area(auto_max, total_area)) {
            auto_max <<= 1;
        }
        max_width = glyph_pack_max_u32(auto_max, start);
    }
    if (max_width < start) {
        max_width = start;
    }
    stop = glyph_pack_next_pow2(max_width);
    if (stop < max_width) {
        stop = max_width;
    }

    width = start;
    while (width <= stop) {
        if (!glyph_pack_add_unique_width(width, widths, capacity, &required)) {
            ok = 0;
        }
        if (width > UINT32_MAX / 2u) {
            break;
        }
        width <<= 1;
    }

    *count = required;
    return ok;
}

int glyph_pack_plan_sort_indices(const GlyphTable *table,
                                 GlyphPackSortMode mode,
                                 uint32_t *indices,
                                 uint32_t count) {
    uint32_t i;
    uint32_t *scratch;
    GlyphPackSortContext ctx;

    if (!glyph_pack_valid_table(table) || count != table->count || (count && !indices)) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        indices[i] = i;
    }
    if (count < 2u || mode == GLYPH_PACK_SORT_TABLE) {
        return 1;
    }
    if (glyph_pack_mul_overflows_size(count, sizeof(*scratch))) {
        return 0;
    }
    scratch = (uint32_t *)malloc((size_t)count * sizeof(*scratch));
    if (!scratch) {
        return 0;
    }

    ctx.table = table;
    ctx.mode = mode;
    i = glyph_pack_sort_range(&ctx, indices, scratch, 0, count);
    free(scratch);
    return (int)i;
}

int glyph_pack_plan_simulate(const GlyphTable *table,
                             const uint32_t *order,
                             uint32_t order_count,
                             uint32_t atlas_width,
                             GlyphPackSortMode sort_mode,
                             GlyphPackPlan **out_plan) {
    GlyphPackPlan *plan;
    uint32_t i;

    if (!out_plan) {
        return 0;
    }
    *out_plan = NULL;
    if (!glyph_pack_valid_table(table) || atlas_width == 0) {
        return 0;
    }
    if (!order && order_count != 0) {
        return 0;
    }
    if (!order && order_count == 0) {
        order_count = table->count;
    }
    if (order && !glyph_pack_validate_order(table, order, order_count)) {
        return 0;
    }
    if (!order && order_count != table->count) {
        return 0;
    }

    plan = (GlyphPackPlan *)calloc(1, sizeof(*plan));
    if (!plan) {
        return 0;
    }
    plan->atlas_width = atlas_width;
    plan->glyph_count = table->count;
    plan->sort_mode = sort_mode;
    plan->metrics.max_waste_row = GLYPH_PACK_PLAN_INDEX_NONE;

    if (table->count) {
        if (glyph_pack_mul_overflows_size(table->count, sizeof(*plan->placements)) ||
            glyph_pack_mul_overflows_size(table->count, sizeof(*plan->pack_order))) {
            glyph_pack_plan_free(plan);
            return 0;
        }
        plan->placements = (GlyphPackPlacement *)calloc(table->count, sizeof(*plan->placements));
        plan->pack_order = (uint32_t *)malloc((size_t)table->count * sizeof(*plan->pack_order));
        if (!plan->placements || !plan->pack_order) {
            glyph_pack_plan_free(plan);
            return 0;
        }
        for (i = 0; i < table->count; i++) {
            plan->placements[i].glyph_index = GLYPH_PACK_PLAN_INDEX_NONE;
            plan->placements[i].row_index = GLYPH_PACK_PLAN_INDEX_NONE;
            plan->pack_order[i] = GLYPH_PACK_PLAN_INDEX_NONE;
        }
    }

    plan->metrics.row_count = 0;
    if (!glyph_pack_ensure_rows(plan, 8u)) {
        glyph_pack_plan_free(plan);
        return 0;
    }
    plan->metrics.row_count = 8u;

    for (i = 0; i < table->count; i++) {
        uint32_t glyph_index = order ? order[i] : i;
        if (!glyph_pack_place_one(plan, table, glyph_index, i)) {
            glyph_pack_plan_free(plan);
            return 0;
        }
    }

    glyph_pack_reset_metrics(plan);
    *out_plan = plan;
    return 1;
}

int glyph_pack_plan_create(const GlyphTable *table,
                           const GlyphPackPlanOptions *options,
                           GlyphPackPlan **out_plan) {
    GlyphPackPlanOptions local_options;
    uint32_t *order = NULL;
    uint32_t width = 0;
    int ok;

    if (!out_plan) {
        return 0;
    }
    *out_plan = NULL;
    if (!glyph_pack_valid_table(table)) {
        return 0;
    }

    glyph_pack_plan_options_default(&local_options);
    if (options) {
        local_options = *options;
    }

    width = local_options.atlas_width;
    if (width == 0) {
        uint32_t count = 0;
        uint32_t *widths = NULL;
        if (!glyph_pack_plan_estimate_widths(table, 0, 0, NULL, 0, &count) || count == 0) {
            return 0;
        }
        if (glyph_pack_mul_overflows_size(count, sizeof(*widths))) {
            return 0;
        }
        widths = (uint32_t *)malloc((size_t)count * sizeof(*widths));
        if (!widths) {
            return 0;
        }
        if (!glyph_pack_plan_estimate_widths(table, 0, 0, widths, count, &count) || count == 0) {
            free(widths);
            return 0;
        }
        width = widths[count > 1u ? count / 2u : 0u];
        free(widths);
    }

    if (!glyph_pack_default_order(table, local_options.sort_mode, &order)) {
        return 0;
    }
    ok = glyph_pack_plan_simulate(table, order, table->count, width, local_options.sort_mode, out_plan);
    free(order);
    return ok;
}

void glyph_pack_plan_free(GlyphPackPlan *plan) {
    if (!plan) {
        return;
    }
    free(plan->rows);
    free(plan->placements);
    free(plan->pack_order);
    memset(plan, 0, sizeof(*plan));
    free(plan);
}

int glyph_pack_plan_compare_widths(const GlyphTable *table,
                                   const uint32_t *widths,
                                   uint32_t width_count,
                                   const GlyphPackPlanOptions *options,
                                   GlyphPackCandidateSet *out_set) {
    GlyphPackPlanOptions local_options;
    GlyphPackCandidateSet set;
    uint32_t *order = NULL;
    uint32_t i;

    if (!out_set) {
        return 0;
    }
    memset(out_set, 0, sizeof(*out_set));
    if (!glyph_pack_valid_table(table) || !widths || width_count == 0) {
        return 0;
    }
    if (glyph_pack_mul_overflows_size(width_count, sizeof(*set.items))) {
        return 0;
    }

    glyph_pack_plan_options_default(&local_options);
    if (options) {
        local_options = *options;
    }
    if (!glyph_pack_default_order(table, local_options.sort_mode, &order)) {
        return 0;
    }

    memset(&set, 0, sizeof(set));
    set.best_index = GLYPH_PACK_PLAN_INDEX_NONE;
    set.items = (GlyphPackCandidate *)calloc(width_count, sizeof(*set.items));
    if (!set.items) {
        free(order);
        return 0;
    }
    set.count = width_count;

    for (i = 0; i < width_count; i++) {
        GlyphPackPlan *plan = NULL;
        GlyphPackCandidate *candidate = &set.items[i];
        candidate->width = widths[i];
        candidate->sort_mode = local_options.sort_mode;
        candidate->ok = glyph_pack_plan_simulate(table, order, table->count, widths[i],
                                                 local_options.sort_mode, &plan);
        if (candidate->ok && plan) {
            candidate->metrics = plan->metrics;
            candidate->score = glyph_pack_candidate_score(candidate);
            if (set.best_index == GLYPH_PACK_PLAN_INDEX_NONE ||
                glyph_pack_candidate_better(candidate, &set.items[set.best_index])) {
                set.best_index = i;
            }
        } else {
            candidate->score = UINT64_MAX;
        }
        glyph_pack_plan_free(plan);
    }

    free(order);
    *out_set = set;
    return set.best_index != GLYPH_PACK_PLAN_INDEX_NONE;
}

void glyph_pack_candidate_set_free(GlyphPackCandidateSet *set) {
    if (!set) {
        return;
    }
    free(set->items);
    memset(set, 0, sizeof(*set));
    set->best_index = GLYPH_PACK_PLAN_INDEX_NONE;
}

int glyph_pack_plan_row_stats(const GlyphPackPlan *plan,
                              uint32_t row_index,
                              GlyphPackRowStats *out_stats) {
    if (!plan || !out_stats || row_index >= plan->row_count || !plan->rows) {
        return 0;
    }
    *out_stats = plan->rows[row_index];
    return 1;
}

int glyph_pack_plan_verify_bounds(const GlyphPackPlan *plan,
                                  const GlyphTable *table,
                                  uint32_t *first_bad_index) {
    uint32_t i;
    uint32_t previous_bottom = 0;

    if (first_bad_index) {
        *first_bad_index = GLYPH_PACK_PLAN_INDEX_NONE;
    }
    if (!plan || !glyph_pack_valid_table(table) || plan->glyph_count != table->count) {
        return 0;
    }
    if (plan->glyph_count && (!plan->placements || !plan->pack_order)) {
        return 0;
    }
    if (plan->row_count && !plan->rows) {
        return 0;
    }

    for (i = 0; i < plan->row_count; i++) {
        const GlyphPackRowStats *row = &plan->rows[i];
        uint64_t bottom = (uint64_t)row->y + (uint64_t)row->height;
        if (row->height == 0 ||
            row->used > plan->atlas_width ||
            bottom > plan->atlas_height ||
            (i > 0 && row->y < previous_bottom)) {
            if (first_bad_index) {
                *first_bad_index = GLYPH_PACK_PLAN_INDEX_NONE;
            }
            return 0;
        }
        previous_bottom = (uint32_t)bottom;
    }

    for (i = 0; i < table->count; i++) {
        const GlyphEntry *glyph = &table->entries[i];
        const GlyphPackPlacement *placement = &plan->placements[i];
        uint64_t right = (uint64_t)placement->placement.x + (uint64_t)glyph->width;
        uint64_t bottom = (uint64_t)placement->placement.y + (uint64_t)glyph->height;

        if (placement->glyph_index != i ||
            placement->glyph_id != glyph->id ||
            placement->width != glyph->width ||
            placement->height != glyph->height ||
            placement->row_index >= plan->row_count ||
            right > plan->atlas_width ||
            bottom > plan->atlas_height ||
            placement->placement.y != plan->rows[placement->row_index].y ||
            right > plan->rows[placement->row_index].used) {
            if (first_bad_index) {
                *first_bad_index = i;
            }
            return 0;
        }
    }

    return 1;
}

const GlyphPackPlacement *glyph_pack_plan_placement_for_index(const GlyphPackPlan *plan,
                                                              uint32_t glyph_index) {
    if (!plan || glyph_index >= plan->glyph_count || !plan->placements) {
        return NULL;
    }
    if (plan->placements[glyph_index].glyph_index != glyph_index) {
        return NULL;
    }
    return &plan->placements[glyph_index];
}

int glyph_pack_plan_find_id(const GlyphPackPlan *plan,
                            uint32_t glyph_id,
                            GlyphPackPlacement *out_placement) {
    uint32_t i;

    if (!plan || !plan->placements) {
        return 0;
    }
    for (i = 0; i < plan->glyph_count; i++) {
        const GlyphPackPlacement *placement = &plan->placements[i];
        if (placement->glyph_index != GLYPH_PACK_PLAN_INDEX_NONE &&
            placement->glyph_id == glyph_id) {
            if (out_placement) {
                *out_placement = *placement;
            }
            return 1;
        }
    }
    return 0;
}

int glyph_pack_plan_write_report(FILE *fp, const GlyphPackPlan *plan) {
    uint32_t i;

    if (!fp || !plan) {
        return 0;
    }

    fprintf(fp, "Glyph pack plan\n");
    fprintf(fp, "sort: %s\n", glyph_pack_sort_mode_name(plan->sort_mode));
    fprintf(fp, "atlas: %ux%u\n", plan->atlas_width, plan->atlas_height);
    fprintf(fp, "glyphs: %u\n", plan->glyph_count);
    fprintf(fp, "rows: %u\n", plan->row_count);
    fprintf(fp,
            "pixels: glyph=%" PRIu64 " row=%" PRIu64 " used=%" PRIu64
            " waste=%" PRIu64 " horizontal-waste=%" PRIu64 " internal-waste=%" PRIu64 "\n",
            plan->metrics.glyph_pixels,
            plan->metrics.row_pixels,
            plan->metrics.used_pixels,
            plan->metrics.waste_pixels,
            plan->metrics.horizontal_waste_pixels,
            plan->metrics.internal_waste_pixels);
    fprintf(fp,
            "ratios: glyph-coverage=%.4f row-utilization=%.4f waste=%.4f\n",
            plan->metrics.glyph_coverage,
            plan->metrics.row_utilization,
            plan->metrics.waste_ratio);
    if (plan->metrics.max_waste_row != GLYPH_PACK_PLAN_INDEX_NONE) {
        fprintf(fp,
                "max-waste-row: %u (%" PRIu64 " pixels)\n",
                plan->metrics.max_waste_row,
                plan->metrics.max_waste_pixels);
    }

    fprintf(fp, "\nrows:\n");
    for (i = 0; i < plan->row_count; i++) {
        const GlyphPackRowStats *row = &plan->rows[i];
        fprintf(fp,
                "  row %u: y=%u height=%u used=%u glyphs=%u glyph-pixels=%" PRIu64
                " waste=%" PRIu64 " horizontal=%" PRIu64 " internal=%" PRIu64
                " utilization=%.4f coverage=%.4f\n",
                i,
                row->y,
                row->height,
                row->used,
                row->glyph_count,
                row->glyph_pixels,
                row->waste_pixels,
                row->horizontal_waste_pixels,
                row->internal_waste_pixels,
                row->utilization,
                row->glyph_coverage);
    }

    fprintf(fp, "\nglyphs:\n");
    for (i = 0; i < plan->glyph_count; i++) {
        uint32_t glyph_index = plan->pack_order ? plan->pack_order[i] : i;
        const GlyphPackPlacement *placement;
        if (glyph_index >= plan->glyph_count) {
            fprintf(fp, "  order %u: invalid-index=%u\n", i, glyph_index);
            continue;
        }
        placement = &plan->placements[glyph_index];
        fprintf(fp,
                "  order %u: index=%u id=%u row=%u place=(%u,%u) size=%ux%u area=%" PRIu64 "\n",
                i,
                placement->glyph_index,
                placement->glyph_id,
                placement->row_index,
                placement->placement.x,
                placement->placement.y,
                placement->width,
                placement->height,
                placement->area);
    }

    return ferror(fp) == 0;
}
