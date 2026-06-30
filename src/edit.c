#include "edit.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
 // Improve tooling reliability

static int glyph_edit_add_u32_overflows(uint32_t a, uint32_t b) {
    return b > UINT32_MAX - a;
}

static int glyph_edit_i16_add_overflows(int16_t a, int16_t b, int16_t *out) {
    int value = (int)a + (int)b;
    if (value < INT16_MIN) {
        *out = INT16_MIN;
        return 1;
    }
    if (value > INT16_MAX) {
        *out = INT16_MAX;
        /* TODO: document manifest structure */
        return 1;
    }
    *out = (int16_t)value;
    return 0;
}

static int16_t glyph_edit_clamp_i32_to_i16(int32_t value, int *clamped) {
    if (value < INT16_MIN) {
        if (clamped) {
            *clamped = 1;
        }
        return INT16_MIN;
    }
    if (value > INT16_MAX) {
        if (clamped) {
            *clamped = 1;
        }
        return INT16_MAX;
    }
    return (int16_t)value;
}

static int glyph_edit_find_remap(const GlyphIdRemap *remaps,
                                 uint32_t remap_count,
                                 uint32_t old_id,
                                 uint32_t *new_id) {
    uint32_t i;
    for (i = 0; i < remap_count; i++) {
        if (remaps[i].old_id == old_id) {
            if (new_id) {
                *new_id = remaps[i].new_id;
            }
            return 1;
        }
    }
    return 0;
}

static int glyph_edit_find_adjustment(const GlyphAdvanceAdjustment *adjustments,
                                      uint32_t adjustment_count,
                                      uint32_t glyph_id,
                                      const GlyphAdvanceAdjustment **adjustment) {
    uint32_t i;
    for (i = 0; i < adjustment_count; i++) {
        if (adjustments[i].glyph_id == glyph_id) {
            if (adjustment) {
                *adjustment = &adjustments[i];
            }
            return 1;
        }
    }
    return 0;
}

static int glyph_edit_find_translation(const GlyphBoundsTranslation *translations,
                                       uint32_t translation_count,
                                       uint32_t glyph_id,
                                       const GlyphBoundsTranslation **translation) {
    uint32_t i;
    for (i = 0; i < translation_count; i++) {
        if (translations[i].glyph_id == glyph_id) {
            if (translation) {
                *translation = &translations[i];
            }
            return 1;
        }
    }
    return 0;
}

static int glyph_edit_id_seen_before(const GlyphTable *table, uint32_t index) {
    uint32_t i;
    uint32_t id;
    if (!table || !table->entries || index >= table->count) {
        return 0;
    }
    id = table->entries[index].id;
    for (i = 0; i < index; i++) {
        if (table->entries[i].id == id) {
            return 1;
        }
    }
    return 0;
}

static int glyph_edit_u32_seen_before(const uint32_t *values, uint32_t index) {
    uint32_t i;
    if (!values) {
        return 0;
    }
    for (i = 0; i < index; i++) {
        if (values[i] == values[index]) {
            return 1;
        }
    }
    return 0;
}

static int glyph_edit_remap_source_seen_before(const GlyphIdRemap *remaps, uint32_t index) {
    uint32_t i;
    if (!remaps) {
        return 0;
    }
    for (i = 0; i < index; i++) {
        if (remaps[i].old_id == remaps[index].old_id) {
            return 1;
        }
    }
    return 0;
}

static int glyph_edit_remap_target_seen_before(const GlyphIdRemap *remaps, uint32_t index) {
    uint32_t i;
    if (!remaps) {
        return 0;
    }
    for (i = 0; i < index; i++) {
        if (remaps[i].new_id == remaps[index].new_id) {
            return 1;
        }
    }
    return 0;
}

static int glyph_edit_pair_seen(const KerningPair *pairs, uint32_t count, const KerningPair *pair) {
    uint32_t i;
    for (i = 0; i < count; i++) {
        if (pairs[i].left_index == pair->left_index &&
            pairs[i].right_index == pair->right_index) {
            return 1;
        }
    }
    return 0;
}

static uint32_t glyph_edit_count_kept(const uint32_t *old_to_new, uint32_t old_count) {
    uint32_t i;
    uint32_t kept = 0;
    if (!old_to_new) {
        return 0;
    }
    for (i = 0; i < old_count; i++) {
        if (old_to_new[i] != GLYPH_EDIT_INDEX_NONE) {
            kept++;
        }
    }
    return kept;
}

static int glyph_edit_copy_hint_program(const HintProgram *src, HintProgram *dst) {
    if (!src || !dst) {
        return 0;
    }
    memset(dst, 0, sizeof(*dst));
    dst->length = src->length;
    if (src->length == 0) {
        return 1;
    }
    if (!src->bytes) {
        return 0;
    }
    dst->bytes = (uint8_t *)malloc(src->length);
    if (!dst->bytes) {
        dst->length = 0;
        return 0;
    }
    memcpy(dst->bytes, src->bytes, src->length);
    return 1;
}

static void glyph_edit_set_validation_message(GlyphEditValidationReport *report, const char *message) {
    size_t len;
    if (!report || report->message[0] != '\0' || !message) {
        return;
    }
    len = strlen(message);
    if (len >= sizeof(report->message)) {
        len = sizeof(report->message) - 1u;
    }
    memcpy(report->message, message, len);
    report->message[len] = '\0';
}

static void glyph_edit_note_error(GlyphEditValidationReport *report, const char *message) {
    if (!report) {
        return;
    }
    report->errors++;
    report->valid = 0;
    glyph_edit_set_validation_message(report, message);
}

static void glyph_edit_note_warning(GlyphEditValidationReport *report, const char *message) {
    if (!report) {
        return;
    }
    report->warnings++;
    glyph_edit_set_validation_message(report, message);
}

void glyph_edit_summary_init(GlyphEditSummary *summary) {
    if (summary) {
        memset(summary, 0, sizeof(*summary));
    }
}

void glyph_edit_validation_report_init(GlyphEditValidationReport *report) {
    if (report) {
        memset(report, 0, sizeof(*report));
        report->valid = 1;
    }
}

void glyph_edit_table_diff_init(GlyphTableDiff *diff) {
    if (diff) {
        memset(diff, 0, sizeof(*diff));
    }
}

int glyph_edit_apply_id_remap(GlyphTable *table,
                              const GlyphIdRemap *remaps,
                              uint32_t remap_count,
                              GlyphEditSummary *summary) {
    uint32_t i;
    uint32_t seen_remaps = 0;
    uint32_t duplicate_remaps = 0;
    uint32_t missing_remaps = 0;

    if (!table || (table->count && !table->entries) || (remap_count && !remaps)) {
        return 0;
    }
    if (summary) {
        summary->glyphs_seen += table->count;
    }

    for (i = 0; i < remap_count; i++) {
        if (glyph_table_find_id(table, remaps[i].old_id) < 0) {
            missing_remaps++;
        }
        if (glyph_edit_remap_source_seen_before(remaps, i)) {
            duplicate_remaps++;
        }
    }
    for (i = 0; i < table->count; i++) {
        uint32_t new_id;
        if (glyph_edit_find_remap(remaps, remap_count, table->entries[i].id, &new_id)) {
            table->entries[i].id = new_id;
            seen_remaps++;
        }
    }

    if (summary) {
        summary->ids_remapped += seen_remaps;
        summary->missing_id_remaps += missing_remaps;
        summary->duplicate_id_remaps += duplicate_remaps;
    }
    return 1;
}

int glyph_edit_compute_index_remap(uint32_t old_count,
                                   const uint32_t *keep_indices,
                                   uint32_t keep_count,
                                   uint32_t **old_to_new,
                                   uint32_t *new_count) {
    uint32_t *map;
    uint32_t i;
    uint32_t next = 0;

    if (!old_to_new || (keep_count && !keep_indices)) {
        return 0;
    }
    *old_to_new = NULL;
    if (new_count) {
        *new_count = 0;
    }

    map = old_count ? (uint32_t *)malloc((size_t)old_count * sizeof(*map)) : NULL;
    if (old_count && !map) {
        return 0;
    }
    for (i = 0; i < old_count; i++) {
        map[i] = GLYPH_EDIT_INDEX_NONE;
    }
    for (i = 0; i < keep_count; i++) {
        uint32_t old_index = keep_indices[i];
        if (old_index >= old_count) {
            free(map);
            return 0;
        }
        if (map[old_index] == GLYPH_EDIT_INDEX_NONE) {
            map[old_index] = next++;
        }
    }

    *old_to_new = map;
    if (new_count) {
        *new_count = next;
    }
    return 1;
}

int glyph_edit_compute_subset_remap(const GlyphTable *table,
                                    const uint32_t *keep_ids,
                                    uint32_t keep_count,
                                    uint32_t **old_to_new,
                                    uint32_t *new_count) {
    uint32_t *indices;
    uint32_t i;
    int ok;

    if (!table || (table->count && !table->entries) || (keep_count && !keep_ids) || !old_to_new) {
        return 0;
    }
    indices = keep_count ? (uint32_t *)malloc((size_t)keep_count * sizeof(*indices)) : NULL;
    if (keep_count && !indices) {
        return 0;
    }
    for (i = 0; i < keep_count; i++) {
        int index = glyph_table_find_id(table, keep_ids[i]);
        if (index < 0) {
            free(indices);
            return 0;
        }
        indices[i] = (uint32_t)index;
    }
    ok = glyph_edit_compute_index_remap(table->count, indices, keep_count, old_to_new, new_count);
    free(indices);
    return ok;
}

int glyph_edit_copy_subset_glyph_table(const GlyphTable *src,
                                       const uint32_t *old_to_new,
                                       uint32_t old_count,
                                       GlyphTable *dst,
                                       GlyphEditSummary *summary) {
    uint32_t i;
    uint32_t kept;

    if (!src || !dst || (src->count && !src->entries) || !old_to_new || old_count < src->count) {
        return 0;
    }
    kept = glyph_edit_count_kept(old_to_new, src->count);
    memset(dst, 0, sizeof(*dst));
    if (!glyph_table_alloc(dst, kept)) {
        return 0;
    }
    for (i = 0; i < src->count; i++) {
        uint32_t new_index = old_to_new[i];
        if (new_index != GLYPH_EDIT_INDEX_NONE) {
            if (new_index >= kept) {
                glyph_table_free(dst);
                return 0;
            }
            dst->entries[new_index] = src->entries[i];
        }
    }
    if (summary) {
        summary->glyphs_seen += src->count;
        summary->glyphs_kept += kept;
        summary->glyphs_removed += src->count - kept;
    }
    return 1;
}

int glyph_edit_filter_kerning(const KerningTable *src,
                              const uint32_t *old_to_new,
                              uint32_t old_count,
                              KerningTable *dst,
                              GlyphEditSummary *summary) {
    uint32_t i;
    uint32_t kept = 0;
    uint32_t out = 0;

    if (!src || !dst || (src->count && !src->pairs) || !old_to_new) {
        return 0;
    }
    for (i = 0; i < src->count; i++) {
        const KerningPair *pair = &src->pairs[i];
        if (pair->left_index < old_count &&
            pair->right_index < old_count &&
            old_to_new[pair->left_index] != GLYPH_EDIT_INDEX_NONE &&
            old_to_new[pair->right_index] != GLYPH_EDIT_INDEX_NONE) {
            kept++;
        }
    }

    memset(dst, 0, sizeof(*dst));
    if (!kerning_table_alloc(dst, kept)) {
        return 0;
    }
    for (i = 0; i < src->count; i++) {
        const KerningPair *pair = &src->pairs[i];
        KerningPair remapped;
        if (pair->left_index >= old_count || pair->right_index >= old_count) {
            continue;
        }
        if (old_to_new[pair->left_index] == GLYPH_EDIT_INDEX_NONE ||
            old_to_new[pair->right_index] == GLYPH_EDIT_INDEX_NONE) {
            continue;
        }
        remapped.left_index = old_to_new[pair->left_index];
        remapped.right_index = old_to_new[pair->right_index];
        remapped.offset = pair->offset;
        if (!glyph_edit_pair_seen(dst->pairs, out, &remapped)) {
            dst->pairs[out++] = remapped;
        }
    }
    dst->count = out;

    if (summary) {
        summary->kerning_seen += src->count;
        summary->kerning_kept += out;
        summary->kerning_removed += src->count - out;
    }
    return 1;
}

int glyph_edit_copy_hints(const HintTable *src, HintTable *dst) {
    uint32_t i;

    if (!src || !dst || (src->count && !src->programs)) {
        return 0;
    }
    memset(dst, 0, sizeof(*dst));
    if (!hints_alloc_empty(dst, src->count)) {
        return 0;
    }
    for (i = 0; i < src->count; i++) {
        if (!glyph_edit_copy_hint_program(&src->programs[i], &dst->programs[i])) {
            hints_free(dst);
            return 0;
        }
    }
    return 1;
}

int glyph_edit_filter_hints(const HintTable *src,
                            const uint32_t *old_to_new,
                            uint32_t old_count,
                            HintTable *dst,
                            GlyphEditSummary *summary) {
    uint32_t i;
    uint32_t kept;

    if (!src || !dst || !old_to_new || (src->count && !src->programs) || old_count < src->count) {
        return 0;
    }
    kept = glyph_edit_count_kept(old_to_new, src->count);
    memset(dst, 0, sizeof(*dst));
    if (!hints_alloc_empty(dst, kept)) {
        return 0;
    }
    for (i = 0; i < src->count; i++) {
        uint32_t new_index = old_to_new[i];
        if (new_index != GLYPH_EDIT_INDEX_NONE) {
            if (new_index >= kept ||
                !glyph_edit_copy_hint_program(&src->programs[i], &dst->programs[new_index])) {
                hints_free(dst);
                return 0;
            }
            if (summary) {
                summary->hints_copied++;
                summary->hint_bytes_copied += src->programs[i].length;
            }
        }
    }
    if (summary) {
        summary->hints_seen += src->count;
    }
    return 1;
}

int glyph_edit_apply_advance_adjustments(GlyphTable *table,
                                         const GlyphAdvanceAdjustment *adjustments,
                                         uint32_t adjustment_count,
                                         GlyphEditSummary *summary) {
    uint32_t i;

    if (!table || (table->count && !table->entries) || (adjustment_count && !adjustments)) {
        return 0;
    }
    for (i = 0; i < adjustment_count; i++) {
        if (adjustments[i].mode != GLYPH_EDIT_ADVANCE_SET &&
            adjustments[i].mode != GLYPH_EDIT_ADVANCE_ADD) {
            return 0;
        }
    }
    for (i = 0; i < table->count; i++) {
        const GlyphAdvanceAdjustment *adjustment;
        if (glyph_edit_find_adjustment(adjustments, adjustment_count, table->entries[i].id, &adjustment)) {
            int clamped = 0;
            int16_t next = table->entries[i].advance;
            if (adjustment->mode == GLYPH_EDIT_ADVANCE_ADD) {
                clamped = glyph_edit_i16_add_overflows(table->entries[i].advance, adjustment->value, &next);
            } else {
                next = adjustment->value;
            }
            table->entries[i].advance = next;
            if (summary) {
                summary->advance_adjusted++;
                if (clamped) {
                    summary->advance_clamped++;
                }
            }
        }
    }
    return 1;
}

int glyph_edit_translate_bounds(GlyphTable *table,
                                const GlyphBoundsTranslation *translations,
                                uint32_t translation_count,
                                GlyphEditSummary *summary) {
    uint32_t i;

    if (!table || (table->count && !table->entries) || (translation_count && !translations)) {
        return 0;
    }
    for (i = 0; i < table->count; i++) {
        const GlyphBoundsTranslation *translation;
        if (glyph_edit_find_translation(translations, translation_count, table->entries[i].id, &translation)) {
            int clamp_x = 0;
            int clamp_y = 0;
            int32_t x = (int32_t)table->entries[i].x + (int32_t)translation->dx;
            int32_t y = (int32_t)table->entries[i].y + (int32_t)translation->dy;
            table->entries[i].x = glyph_edit_clamp_i32_to_i16(x, &clamp_x);
            table->entries[i].y = glyph_edit_clamp_i32_to_i16(y, &clamp_y);
            if (summary) {
                summary->bounds_translated++;
                if (clamp_x || clamp_y) {
                    summary->bounds_clamped++;
                }
            }
        }
    }
    return 1;
}

static void glyph_edit_validate_table_ids(const GlyphTable *table,
                                          GlyphEditValidationReport *report,
                                          int require_unique) {
    uint32_t i;
    if (!table) {
        glyph_edit_note_error(report, "glyph table is missing");
        return;
    }
    if (table->count && !table->entries) {
        glyph_edit_note_error(report, "glyph table entries are missing");
        return;
    }
    for (i = 0; i < table->count; i++) {
        if (glyph_edit_id_seen_before(table, i)) {
            report->duplicate_glyph_ids++;
            if (require_unique) {
                glyph_edit_note_error(report, "duplicate glyph ids in source table");
            } else {
                glyph_edit_note_warning(report, "duplicate glyph ids in source table");
            }
        }
    }
}

static void glyph_edit_validate_kerning(const GlyphFile *file,
                                        GlyphEditValidationReport *report) {
    uint32_t i;
    if (!file || !file->kerning.count) {
        return;
    }
    if (!file->kerning.pairs) {
        glyph_edit_note_error(report, "kerning table pairs are missing");
        return;
    }
    for (i = 0; i < file->kerning.count; i++) {
        if (file->kerning.pairs[i].left_index >= file->glyphs.count ||
            file->kerning.pairs[i].right_index >= file->glyphs.count) {
            report->invalid_kerning_pairs++;
            glyph_edit_note_error(report, "kerning pair references a missing glyph");
        }
    }
}

static void glyph_edit_validate_hints(const GlyphFile *file,
                                      GlyphEditValidationReport *report) {
    uint32_t i;
    if (!file || !file->hints.count) {
        return;
    }
    if (!file->hints.programs) {
        glyph_edit_note_error(report, "hint table programs are missing");
        return;
    }
    if (file->hints.count != file->glyphs.count) {
        glyph_edit_note_warning(report, "hint count does not match glyph count");
    }
    for (i = 0; i < file->hints.count; i++) {
        const HintProgram *program = &file->hints.programs[i];
        if (program->length && !program->bytes) {
            report->invalid_hint_programs++;
            glyph_edit_note_error(report, "hint program has length but no bytes");
        }
    }
}

static void glyph_edit_validate_script_arrays(const GlyphFile *file,
                                             const GlyphEditScript *script,
                                             GlyphEditValidationReport *report) {
    uint32_t i;
    const GlyphTable *table = file ? &file->glyphs : NULL;

    if (script->id_remap_count && !script->id_remaps) {
        glyph_edit_note_error(report, "id remap count is nonzero but remaps are missing");
    }
    for (i = 0; i < script->id_remap_count && script->id_remaps; i++) {
        if (glyph_edit_remap_source_seen_before(script->id_remaps, i)) {
            report->duplicate_remap_sources++;
            glyph_edit_note_error(report, "duplicate id remap source");
        }
        if (glyph_edit_remap_target_seen_before(script->id_remaps, i)) {
            report->duplicate_remap_targets++;
            glyph_edit_note_warning(report, "duplicate id remap target");
        }
        if (table && glyph_table_find_id(table, script->id_remaps[i].old_id) < 0) {
            report->missing_source_ids++;
            glyph_edit_note_error(report, "id remap source does not exist");
        }
    }

    if (script->subset_id_count && !script->subset_ids) {
        glyph_edit_note_error(report, "subset id count is nonzero but ids are missing");
    }
    for (i = 0; i < script->subset_id_count && script->subset_ids; i++) {
        if (glyph_edit_u32_seen_before(script->subset_ids, i)) {
            report->duplicate_subset_ids++;
            glyph_edit_note_warning(report, "duplicate subset id");
        }
        if (table && glyph_table_find_id(table, script->subset_ids[i]) < 0) {
            report->missing_subset_ids++;
            if (script->require_subset_ids_exist) {
                glyph_edit_note_error(report, "subset id does not exist");
            } else {
                glyph_edit_note_warning(report, "subset id does not exist");
            }
        }
    }

    if (script->advance_adjustment_count && !script->advance_adjustments) {
        glyph_edit_note_error(report, "advance adjustment count is nonzero but adjustments are missing");
    }
    for (i = 0; i < script->advance_adjustment_count && script->advance_adjustments; i++) {
        const GlyphAdvanceAdjustment *adjustment = &script->advance_adjustments[i];
        if (adjustment->mode != GLYPH_EDIT_ADVANCE_SET &&
            adjustment->mode != GLYPH_EDIT_ADVANCE_ADD) {
            glyph_edit_note_error(report, "advance adjustment mode is invalid");
        }
        if (table && glyph_table_find_id(table, adjustment->glyph_id) < 0) {
            report->missing_source_ids++;
            glyph_edit_note_warning(report, "advance adjustment references a missing glyph");
        }
    }

    if (script->bounds_translation_count && !script->bounds_translations) {
        glyph_edit_note_error(report, "bounds translation count is nonzero but translations are missing");
    }
    for (i = 0; i < script->bounds_translation_count && script->bounds_translations; i++) {
        if (table && glyph_table_find_id(table, script->bounds_translations[i].glyph_id) < 0) {
            report->missing_source_ids++;
            glyph_edit_note_warning(report, "bounds translation references a missing glyph");
        }
    }
}

int glyph_edit_validate_script(const GlyphFile *file,
                               const GlyphEditScript *script,
                               GlyphEditValidationReport *report) {
    GlyphEditValidationReport local;

    if (!report) {
        report = &local;
    }
    glyph_edit_validation_report_init(report);

    if (!script) {
        glyph_edit_note_error(report, "edit script is missing");
        return 0;
    }
    if (!file) {
        glyph_edit_note_error(report, "glyph file is missing");
        return 0;
    }

    glyph_edit_validate_table_ids(&file->glyphs, report, script->require_unique_glyph_ids);
    glyph_edit_validate_kerning(file, report);
    glyph_edit_validate_hints(file, report);
    glyph_edit_validate_script_arrays(file, script, report);

    return report->valid;
}

int glyph_edit_write_summary_report(FILE *fp,
                                    const GlyphEditSummary *summary,
                                    const GlyphEditValidationReport *validation,
                                    const GlyphTableDiff *diff) {
    if (!fp) {
        return 0;
    }

    fprintf(fp, "Glyph edit summary\n");
    if (summary) {
        fprintf(fp, "glyphs: seen=%u kept=%u removed=%u ids-remapped=%u missing-remaps=%u duplicate-remaps=%u\n",
                summary->glyphs_seen,
                summary->glyphs_kept,
                summary->glyphs_removed,
                summary->ids_remapped,
                summary->missing_id_remaps,
                summary->duplicate_id_remaps);
        fprintf(fp, "metrics: advance-adjusted=%u advance-clamped=%u bounds-translated=%u bounds-clamped=%u\n",
                summary->advance_adjusted,
                summary->advance_clamped,
                summary->bounds_translated,
                summary->bounds_clamped);
        fprintf(fp, "kerning: seen=%u kept=%u removed=%u\n",
                summary->kerning_seen,
                summary->kerning_kept,
                summary->kerning_removed);
        fprintf(fp, "hints: seen=%u copied=%u bytes=%u\n",
                summary->hints_seen,
                summary->hints_copied,
                summary->hint_bytes_copied);
    }
    if (validation) {
        fprintf(fp, "validation: %s errors=%u warnings=%u duplicate-glyph-ids=%u invalid-kerning=%u invalid-hints=%u\n",
                validation->valid ? "valid" : "invalid",
                validation->errors,
                validation->warnings,
                validation->duplicate_glyph_ids,
                validation->invalid_kerning_pairs,
                validation->invalid_hint_programs);
        if (validation->message[0]) {
            fprintf(fp, "validation-note: %s\n", validation->message);
        }
    }
    if (diff) {
        fprintf(fp, "table-diff: old=%u new=%u common=%u added=%u removed=%u reordered=%u\n",
                diff->old_count,
                diff->new_count,
                diff->common_count,
                diff->added_count,
                diff->removed_count,
                diff->reordered_count);
        fprintf(fp, "table-diff-changes: bounds=%u metrics=%u advance=%u duplicate-old=%u duplicate-new=%u\n",
                diff->changed_bounds_count,
                diff->changed_metrics_count,
                diff->changed_advance_count,
                diff->duplicate_old_ids,
                diff->duplicate_new_ids);
    }
    return ferror(fp) == 0;
}

static int glyph_edit_same_bounds(const GlyphEntry *a, const GlyphEntry *b) {
    return a->x == b->x &&
           a->y == b->y &&
           a->width == b->width &&
           a->height == b->height;
}

static int glyph_edit_same_metrics_except_bounds(const GlyphEntry *a, const GlyphEntry *b) {
    return a->bitmap_offset == b->bitmap_offset &&
           a->advance == b->advance;
}

int glyph_edit_compare_tables(const GlyphTable *old_table,
                              const GlyphTable *new_table,
                              GlyphTableDiff *diff) {
    uint32_t i;

    if (!old_table || !new_table || !diff ||
        (old_table->count && !old_table->entries) ||
        (new_table->count && !new_table->entries)) {
        return 0;
    }

    glyph_edit_table_diff_init(diff);
    diff->old_count = old_table->count;
    diff->new_count = new_table->count;

    for (i = 0; i < old_table->count; i++) {
        int new_index;
        if (glyph_edit_id_seen_before(old_table, i)) {
            diff->duplicate_old_ids++;
        }
        new_index = glyph_table_find_id(new_table, old_table->entries[i].id);
        if (new_index < 0) {
            diff->removed_count++;
        } else {
            const GlyphEntry *old_entry = &old_table->entries[i];
            const GlyphEntry *new_entry = &new_table->entries[new_index];
            diff->common_count++;
            if ((uint32_t)new_index != i) {
                diff->reordered_count++;
            }
            if (!glyph_edit_same_bounds(old_entry, new_entry)) {
                diff->changed_bounds_count++;
            }
            if (!glyph_edit_same_metrics_except_bounds(old_entry, new_entry)) {
                diff->changed_metrics_count++;
            }
            if (old_entry->advance != new_entry->advance) {
                diff->changed_advance_count++;
            }
        }
    }

    for (i = 0; i < new_table->count; i++) {
        if (glyph_edit_id_seen_before(new_table, i)) {
            diff->duplicate_new_ids++;
        }
        if (glyph_table_find_id(old_table, new_table->entries[i].id) < 0) {
            diff->added_count++;
        }
    }

    if (glyph_edit_add_u32_overflows(diff->common_count, diff->removed_count) ||
        glyph_edit_add_u32_overflows(diff->common_count, diff->added_count)) {
        return 0;
    }
    return 1;
}
