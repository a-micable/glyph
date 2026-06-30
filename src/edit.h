#ifndef GLYPH_EDIT_H
#define GLYPH_EDIT_H

#include "atlas.h"
#include "glyph_table.h"
#include "hints.h"
#include "kerning.h"

#include <stdint.h>
#include <stdio.h>

#define GLYPH_EDIT_INDEX_NONE UINT32_MAX

typedef enum {
    GLYPH_EDIT_ADVANCE_SET = 0,
    GLYPH_EDIT_ADVANCE_ADD = 1
} GlyphEditAdvanceMode;

typedef struct {
    uint32_t old_id;
    uint32_t new_id;
} GlyphIdRemap;

typedef struct {
    uint32_t glyph_id;
    GlyphEditAdvanceMode mode;
    int16_t value;
} GlyphAdvanceAdjustment;

typedef struct {
    uint32_t glyph_id;
    int16_t dx;
    int16_t dy;
} GlyphBoundsTranslation;

typedef struct {
    const GlyphIdRemap *id_remaps;
    uint32_t id_remap_count;
    const uint32_t *subset_ids;
    uint32_t subset_id_count;
    const GlyphAdvanceAdjustment *advance_adjustments;
    uint32_t advance_adjustment_count;
    const GlyphBoundsTranslation *bounds_translations;
    uint32_t bounds_translation_count;
    int require_unique_glyph_ids;
    int require_subset_ids_exist;
} GlyphEditScript;

typedef struct {
    uint32_t glyphs_seen;
    uint32_t glyphs_kept;
    uint32_t glyphs_removed;
    uint32_t ids_remapped;
    uint32_t missing_id_remaps;
    uint32_t duplicate_id_remaps;
    uint32_t advance_adjusted;
    uint32_t advance_clamped;
    uint32_t bounds_translated;
    uint32_t bounds_clamped;
    uint32_t kerning_seen;
    uint32_t kerning_kept;
    uint32_t kerning_removed;
    uint32_t hints_seen;
    uint32_t hints_copied;
    uint32_t hint_bytes_copied;
} GlyphEditSummary;

typedef struct {
    int valid;
    uint32_t errors;
    uint32_t warnings;
    uint32_t duplicate_glyph_ids;
    uint32_t duplicate_remap_sources;
    uint32_t duplicate_remap_targets;
    uint32_t duplicate_subset_ids;
    uint32_t missing_source_ids;
    uint32_t missing_subset_ids;
    uint32_t invalid_kerning_pairs;
    uint32_t invalid_hint_programs;
    uint32_t overflowed_values;
    char message[256];
} GlyphEditValidationReport;

typedef struct {
    uint32_t old_count;
    uint32_t new_count;
    uint32_t common_count;
    uint32_t added_count;
    uint32_t removed_count;
    uint32_t reordered_count;
    uint32_t changed_bounds_count;
    uint32_t changed_metrics_count;
    uint32_t changed_advance_count;
    uint32_t duplicate_old_ids;
    uint32_t duplicate_new_ids;
} GlyphTableDiff;

void glyph_edit_summary_init(GlyphEditSummary *summary);
void glyph_edit_validation_report_init(GlyphEditValidationReport *report);
void glyph_edit_table_diff_init(GlyphTableDiff *diff);

int glyph_edit_apply_id_remap(GlyphTable *table,
                              const GlyphIdRemap *remaps,
                              uint32_t remap_count,
                              GlyphEditSummary *summary);

int glyph_edit_compute_index_remap(uint32_t old_count,
                                   const uint32_t *keep_indices,
                                   uint32_t keep_count,
                                   uint32_t **old_to_new,
                                   uint32_t *new_count);

int glyph_edit_compute_subset_remap(const GlyphTable *table,
                                    const uint32_t *keep_ids,
                                    uint32_t keep_count,
                                    uint32_t **old_to_new,
                                    uint32_t *new_count);

int glyph_edit_copy_subset_glyph_table(const GlyphTable *src,
                                       const uint32_t *old_to_new,
                                       uint32_t old_count,
                                       GlyphTable *dst,
                                       GlyphEditSummary *summary);

int glyph_edit_filter_kerning(const KerningTable *src,
                              const uint32_t *old_to_new,
                              uint32_t old_count,
                              KerningTable *dst,
                              GlyphEditSummary *summary);

int glyph_edit_copy_hints(const HintTable *src, HintTable *dst);

int glyph_edit_filter_hints(const HintTable *src,
                            const uint32_t *old_to_new,
                            uint32_t old_count,
                            HintTable *dst,
                            GlyphEditSummary *summary);

int glyph_edit_apply_advance_adjustments(GlyphTable *table,
                                         const GlyphAdvanceAdjustment *adjustments,
                                         uint32_t adjustment_count,
                                         GlyphEditSummary *summary);

int glyph_edit_translate_bounds(GlyphTable *table,
                                const GlyphBoundsTranslation *translations,
                                uint32_t translation_count,
                                GlyphEditSummary *summary);

int glyph_edit_validate_script(const GlyphFile *file,
                               const GlyphEditScript *script,
                               GlyphEditValidationReport *report);

int glyph_edit_write_summary_report(FILE *fp,
                                    const GlyphEditSummary *summary,
                                    const GlyphEditValidationReport *validation,
                                    const GlyphTableDiff *diff);

int glyph_edit_compare_tables(const GlyphTable *old_table,
                              const GlyphTable *new_table,
                              GlyphTableDiff *diff);

#endif
