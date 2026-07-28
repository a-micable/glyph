#include "bitmap.h"
#include "cache.h"
#include "edit.h"
#include "layout.h"
#include "manifest.h"
#include "pack_plan.h"
#include "row_alloc.h"
#include "scene.h"
#include "validate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void check_int(const char *name, int condition) {
    if (!condition) {
        fprintf(stderr, "check failed: %s\n", name);
        failures++;
    }
}

static void make_table(GlyphTable *table) {
    check_int("glyph_table_alloc", glyph_table_alloc(table, 5));
    table->entries[0].id = 32;
    table->entries[0].width = 4;
    table->entries[0].height = 4;
    table->entries[0].advance = 4;
    table->entries[1].id = 65;
    table->entries[1].width = 7;
    table->entries[1].height = 9;
    table->entries[1].advance = 8;
    table->entries[2].id = 66;
    table->entries[2].width = 6;
    table->entries[2].height = 8;
    table->entries[2].advance = 7;
    table->entries[3].id = 67;
    table->entries[3].width = 5;
    table->entries[3].height = 10;
    table->entries[3].advance = 6;
    table->entries[4].id = 86;
    table->entries[4].width = 8;
    table->entries[4].height = 8;
    table->entries[4].advance = 8;
}

static void make_file(GlyphFile *file) {
    memset(file, 0, sizeof(*file));
    make_table(&file->glyphs);
    check_int("kerning alloc", kerning_table_alloc(&file->kerning, 3));
    file->kerning.pairs[0].left_index = 1;
    file->kerning.pairs[0].right_index = 4;
    file->kerning.pairs[0].offset = -1;
    file->kerning.pairs[1].left_index = 1;
    file->kerning.pairs[1].right_index = 2;
    file->kerning.pairs[1].offset = 1;
    file->kerning.pairs[2].left_index = 2;
    file->kerning.pairs[2].right_index = 3;
    file->kerning.pairs[2].offset = 0;

    file->atlas_width = 24;
    file->atlas_height = 18;
    file->row_count = 2;
    file->rows = (RowDescriptor *)calloc(file->row_count, sizeof(RowDescriptor));
    file->placements = (GlyphPlacement *)calloc(file->glyphs.count, sizeof(GlyphPlacement));
    file->atlas_pixels = (uint8_t *)calloc((size_t)file->atlas_width * file->atlas_height, 1);
    check_int("file allocations", file->rows && file->placements && file->atlas_pixels);

    file->rows[0].y = 0;
    file->rows[0].height = 10;
    file->rows[0].used = 22;
    file->rows[1].y = 10;
    file->rows[1].height = 8;
    file->rows[1].used = 14;

    file->placements[0].x = 0;
    file->placements[0].y = 0;
    file->placements[1].x = 4;
    file->placements[1].y = 0;
    file->placements[2].x = 11;
    file->placements[2].y = 0;
    file->placements[3].x = 17;
    file->placements[3].y = 0;
    file->placements[4].x = 0;
    file->placements[4].y = 10;

    for (uint32_t i = 0; i < file->glyphs.count; i++) {
        GlyphEntry *g = &file->glyphs.entries[i];
        GlyphPlacement *p = &file->placements[i];
        g->bitmap_offset = p->y * file->atlas_width + p->x;
        for (uint32_t y = 0; y < g->height; y++) {
            for (uint32_t x = 0; x < g->width; x++) {
                file->atlas_pixels[(size_t)(p->y + y) * file->atlas_width + p->x + x] =
                    (uint8_t)(60 + i * 30 + x + y);
            }
        }
    }
}

static void test_bitmap_basic(void) {
    GlyphBitmap a;
    GlyphBitmap b;
    GlyphBitmap c;
    GlyphBitmapRect bounds;
    char preview[512];
    uint32_t diff = 0;
    uint8_t delta = 0;

    glyph_bitmap_init(&a);
    glyph_bitmap_init(&b);
    glyph_bitmap_init(&c);
    check_int("bitmap alloc a", glyph_bitmap_alloc(&a, 8, 8));
    check_int("bitmap alloc b", glyph_bitmap_alloc(&b, 8, 8));
    glyph_bitmap_fill(&a, 10);
    glyph_bitmap_draw_line(&a, 0, 0, 7, 7, 255);
    glyph_bitmap_draw_rect(&a, 1, 1, 6, 5, 128);
    glyph_bitmap_fill_rect(&b, 2, 2, 3, 3, 200);
    check_int("bitmap blit", glyph_bitmap_blit(&a, &b, 1, 1, GLYPH_BITMAP_BLIT_MAX, 255));
    check_int("bitmap copy", glyph_bitmap_copy(&c, &a));
    check_int("bitmap compare equal", glyph_bitmap_compare(&a, &c, &diff, &delta) && diff == 0 && delta == 0);
    check_int("bitmap trim", glyph_bitmap_trim_bounds(&a, 11, &bounds));
    check_int("trim nonempty", bounds.width > 0 && bounds.height > 0);
    check_int("ascii preview", glyph_bitmap_ascii_preview(&a, preview, sizeof(preview), 8, 8) > 0);
    glyph_bitmap_threshold(&c, 127, 0, 255);
    glyph_bitmap_invert(&c);
    glyph_bitmap_normalize(&a);
    check_int("bitmap hash", glyph_bitmap_hash64(&a) != 0);
    glyph_bitmap_free(&a);
    glyph_bitmap_free(&b);
    glyph_bitmap_free(&c);
}

static void test_bitmap_transforms(void) {
    GlyphBitmap a;
    GlyphBitmap b;
    GlyphBitmap c;
    glyph_bitmap_init(&a);
    glyph_bitmap_init(&b);
    glyph_bitmap_init(&c);
    check_int("transform alloc", glyph_bitmap_alloc(&a, 5, 4));
    for (uint32_t y = 0; y < a.height; y++) {
        for (uint32_t x = 0; x < a.width; x++) {
            glyph_bitmap_set(&a, x, y, (uint8_t)(x * 20 + y * 30));
        }
    }
    check_int("resize nearest", glyph_bitmap_resize_nearest(&b, &a, 10, 8));
    check_int("resize dims", b.width == 10 && b.height == 8);
    check_int("rotate cw", glyph_bitmap_rotate_cw(&c, &a));
    check_int("rotate dims", c.width == a.height && c.height == a.width);
    check_int("flip h", glyph_bitmap_flip_horizontal(&a));
    check_int("flip v", glyph_bitmap_flip_vertical(&a));
    check_int("dilate", glyph_bitmap_dilate(&c, &a, 1));
    check_int("erode", glyph_bitmap_erode(&b, &a, 1));
    check_int("blur", glyph_bitmap_box_blur(&c, &a, 1));
    glyph_bitmap_free(&a);
    glyph_bitmap_free(&b);
    glyph_bitmap_free(&c);
}

static void test_pack_plan(void) {
    GlyphTable table;
    GlyphPackPlanOptions options;
    GlyphPackPlan *plan = NULL;
    GlyphPackCandidateSet candidates;
    uint32_t widths[8];
    uint32_t count = 0;
    uint32_t bad = 0;
    GlyphPackPlacement placement;
    memset(&candidates, 0, sizeof(candidates));
    memset(&placement, 0, sizeof(placement));
    make_table(&table);
    glyph_pack_plan_options_default(&options);
    options.atlas_width = 16;
    options.sort_mode = GLYPH_PACK_SORT_HEIGHT_DESC;
    check_int("estimate widths", glyph_pack_plan_estimate_widths(&table, 8, 64, widths, 8, &count));
    check_int("estimated count", count > 0);
    check_int("pack create", glyph_pack_plan_create(&table, &options, &plan));
    check_int("plan metrics", plan && plan->atlas_width == 16 && plan->row_count > 0);
    check_int("plan bounds", glyph_pack_plan_verify_bounds(plan, &table, &bad));
    check_int("find placement", glyph_pack_plan_find_id(plan, 65, &placement));
    check_int("placement id", placement.glyph_id == 65);
    check_int("compare widths", glyph_pack_plan_compare_widths(&table, widths, count, &options, &candidates));
    check_int("candidate count", candidates.count == count);
    check_int("candidate best", candidates.best_index < candidates.count);
    glyph_pack_candidate_set_free(&candidates);
    glyph_pack_plan_free(plan);
    glyph_table_free(&table);
}

static void test_layout_and_validation(void) {
    GlyphFile file;
    GlyphLayout layout;
    GlyphLayoutOptions options;
    GlyphValidationDiagnostics diagnostics;
    GlyphValidationReport report;
    uint8_t *pixels;
    size_t hit;

    make_file(&file);
    glyph_layout_init(&layout);
    glyph_layout_options_default(&options);
    options.wrap_width = 32;
    options.align = GLYPH_LAYOUT_ALIGN_LEFT;
    check_int("layout shape", glyph_layout_shape(&file, "AV\nABC", &options, &layout));
    check_int("layout lines", layout.line_count == 2);
    check_int("layout glyphs", layout.glyph_count == 5);
    hit = glyph_layout_hit_test(&layout, layout.glyphs[0].x, layout.glyphs[0].y);
    check_int("layout hit", hit != GLYPH_LAYOUT_HIT_NONE);
    pixels = (uint8_t *)calloc((size_t)64 * 32, 1);
    check_int("render buffer alloc", pixels != NULL);
    check_int("layout render", glyph_layout_render_to_buffer(&file, &layout, pixels, 64, 32, 0, 0));
    free(pixels);

    glyph_validation_diagnostics_init(&diagnostics);
    glyph_validation_report_init(&report);
    check_int("validate file", glyph_validate_file(&file, &diagnostics, &report));
    check_int("validate no errors", report.error_count == 0);
    glyph_validation_diagnostics_free(&diagnostics);
    glyph_layout_free(&layout);
    glyph_file_free(&file);
}

static void test_manifest_selection(void) {
    GlyphFile file;
    GlyphManifest manifest;
    GlyphSelectionSet selection;
    GlyphSubsetPlan plan;
    char error[256];
    memset(&manifest, 0, sizeof(manifest));
    memset(&selection, 0, sizeof(selection));
    memset(&plan, 0, sizeof(plan));
    make_file(&file);
    check_int("manifest from file", glyph_manifest_from_file(&file, &manifest, error, sizeof(error)));
    check_int("manifest glyph count", manifest.glyph_count == file.glyphs.count);
    check_int("manifest find", glyph_manifest_find_glyph(&manifest, 65) != NULL);
    check_int("selection parse ids", glyph_selection_parse_ids("65,66,86", &selection, error, sizeof(error)));
    check_int("selection contains", glyph_selection_contains(&selection, 86));
    check_int("subset plan", glyph_subset_plan_from_file(&file, &selection, &plan, error, sizeof(error)));
    check_int("subset glyph count", plan.glyph_count == 3);
    glyph_subset_plan_free(&plan);
    glyph_selection_free(&selection);
    glyph_manifest_free(&manifest);
    glyph_file_free(&file);
}

static void test_edit_operations(void) {
    GlyphFile file;
    GlyphTable subset;
    KerningTable kern;
    HintTable hints;
    GlyphEditSummary summary;
    GlyphEditValidationReport validation;
    GlyphTableDiff diff;
    uint32_t *old_to_new = NULL;
    uint32_t keep_ids[] = {65, 66, 86};
    GlyphIdRemap remaps[] = {{65, 165}, {86, 186}};
    GlyphAdvanceAdjustment advances[] = {{165, GLYPH_EDIT_ADVANCE_ADD, 2}, {66, GLYPH_EDIT_ADVANCE_SET, 9}};
    GlyphBoundsTranslation translations[] = {{165, 1, -1}, {186, -1, 1}};
    GlyphEditScript script;

    memset(&subset, 0, sizeof(subset));
    memset(&kern, 0, sizeof(kern));
    memset(&hints, 0, sizeof(hints));
    memset(&script, 0, sizeof(script));
    glyph_edit_summary_init(&summary);
    glyph_edit_validation_report_init(&validation);
    glyph_edit_table_diff_init(&diff);
    make_file(&file);

    check_int("subset remap", glyph_edit_compute_subset_remap(&file.glyphs, keep_ids, 3, &old_to_new, NULL));
    check_int("copy subset table", glyph_edit_copy_subset_glyph_table(&file.glyphs, old_to_new, file.glyphs.count, &subset, &summary));
    check_int("filter kerning", glyph_edit_filter_kerning(&file.kerning, old_to_new, file.glyphs.count, &kern, &summary));
    check_int("copy hints empty", glyph_edit_filter_hints(&file.hints, old_to_new, file.glyphs.count, &hints, &summary));
    check_int("apply id remap", glyph_edit_apply_id_remap(&subset, remaps, 2, &summary));
    check_int("advance adjust", glyph_edit_apply_advance_adjustments(&subset, advances, 2, &summary));
    check_int("bounds translate", glyph_edit_translate_bounds(&subset, translations, 2, &summary));
    check_int("compare tables", glyph_edit_compare_tables(&file.glyphs, &subset, &diff));
    check_int("diff removed", diff.removed_count >= 2);

    script.id_remaps = remaps;
    script.id_remap_count = 2;
    script.subset_ids = keep_ids;
    script.subset_id_count = 3;
    script.advance_adjustments = advances;
    script.advance_adjustment_count = 2;
    script.bounds_translations = translations;
    script.bounds_translation_count = 2;
    script.require_unique_glyph_ids = 1;
    script.require_subset_ids_exist = 1;
    check_int("validate script", glyph_edit_validate_script(&file, &script, &validation));
    check_int("script valid", validation.valid);

    free(old_to_new);
    glyph_table_free(&subset);
    kerning_table_free(&kern);
    hints_free(&hints);
    glyph_file_free(&file);
}

static void test_row_alloc_reload_refreshes_cache(void) {
    GlyphTable first;
    GlyphTable second;
    RowAllocator alloc;
    uint32_t x = 0;
    uint32_t y = 0;

    memset(&first, 0, sizeof(first));
    memset(&second, 0, sizeof(second));
    row_alloc_init(&alloc, 16);
    check_int("row place a", row_alloc_place(&alloc, 4, 4, &x, &y));
    check_int("row place b", row_alloc_place(&alloc, 4, 4, &x, &y));
    check_int("row table first", glyph_table_alloc(&first, 2));
    first.entries[0].id = 10;
    first.entries[0].width = 4;
    first.entries[0].height = 4;
    first.entries[0].bitmap_offset = 0;
    first.entries[1].id = 11;
    first.entries[1].width = 4;
    first.entries[1].height = 4;
    first.entries[1].bitmap_offset = 4;
    check_int("row cache first", row_alloc_cache_glyphs(&alloc, &first));
    check_int("row cache first id", row_alloc_cached_glyph(&alloc, 0)->id == 10);

    check_int("row table second", glyph_table_alloc(&second, 1));
    second.entries[0].id = 99;
    second.entries[0].width = 4;
    second.entries[0].height = 4;
    second.entries[0].bitmap_offset = 0;
    glyph_table_free(&first);
    check_int("row reload second", row_alloc_reload_without_full_reset(&alloc, &second));
    check_int("row cache second count", row_alloc_cached_glyph(&alloc, 1) == NULL);
    check_int("row cache second id", row_alloc_cached_glyph(&alloc, 0)->id == 99);

    glyph_table_free(&second);
    row_alloc_destroy(&alloc);
}

static void test_scene_lifecycle_documents(void) {
    const char delete_parent[] =
        "scene 64 64\n"
        "nodes 4\n"
        "node 1 root 0 0 0 64 64 \"root\"\n"
        "node 2 group 1 0 0 32 32 \"parent\"\n"
        "node 3 group 2 1 1 16 16 \"child\"\n"
        "node 4 glyph 3 2 2 8 8 \"leaf\"\n"
        "ops 3\n"
        "op snapshot\n"
        "op delete 2\n"
        "op serialize\n";
    const char stale_audit[] =
        "scene 64 64\n"
        "nodes 1\n"
        "node 1 root 0 0 0 64 64 \"root\"\n"
        "ops 7\n"
        "op register 7 alpha \"old\" 1 2\n"
        "op audit 7\n"
        "op deregister 7\n"
        "op promote\n"
        "op register 9 beta \"new\" 3 4\n"
        "op replay 5\n"
        "op snapshot\n";
    const char live_audit[] =
        "scene 64 64\n"
        "nodes 1\n"
        "node 1 root 0 0 0 64 64 \"root\"\n"
        "ops 4\n"
        "op register 7 alpha \"old\" 1 2\n"
        "op audit 7\n"
        "op replay 5\n"
        "op snapshot\n";

    check_int("scene delete parent subtree",
              glyph_scene_run_document((const uint8_t *)delete_parent, strlen(delete_parent)) == GLYPH_SCENE_RESULT_OK);
    check_int("scene live audit replay",
              glyph_scene_run_document((const uint8_t *)live_audit, strlen(live_audit)) == GLYPH_SCENE_RESULT_OK);
    check_int("scene stale audit rejects reused slot",
              glyph_scene_run_document((const uint8_t *)stale_audit, strlen(stale_audit)) == GLYPH_SCENE_RESULT_REJECT);
}

static void test_cache_file_invalidation_removes_dependents(void) {
    GlyphFile file;
    GlyphCache cache;
    GlyphCacheKey file_key;
    GlyphCacheKey slice_key;
    GlyphCacheKey surface_key;
    GlyphBitmap bitmap;
    GlyphBitmapRect rect;
    GlyphLayoutOptions options;
    GlyphCacheValidationReport report;
    size_t removed;

    make_file(&file);
    glyph_cache_init(&cache);
    glyph_bitmap_init(&bitmap);
    rect.x = 0;
    rect.y = 0;
    rect.width = 4;
    rect.height = 4;
    glyph_layout_options_default(&options);

    check_int("cache create", glyph_cache_create(&cache, 16, 4096));
    check_int("cache bitmap alloc", glyph_bitmap_alloc(&bitmap, 4, 4));
    glyph_bitmap_fill(&bitmap, 77);

    file_key = glyph_cache_key_file_pointer(&file, 1);
    slice_key = glyph_cache_key_bitmap_slice(&file, 65, rect, 2);
    surface_key = glyph_cache_key_layout_surface(&file, "AV", &options, 16, 8, 1, 2);
    check_int("cache insert file", glyph_cache_insert_file(&cache, &file_key, &file));
    check_int("cache insert slice", glyph_cache_insert_bitmap_slice(&cache, &slice_key, 65, rect, &bitmap));
    check_int("cache insert surface", glyph_cache_insert_layout_surface(&cache, &surface_key, &options, 1, 2, &bitmap));
    check_int("cache validate before remove", glyph_cache_validate(&cache, &report));
    check_int("cache has three entries", glyph_cache_entry_count(&cache) == 3);

    removed = glyph_cache_remove_file(&cache, &file);
    check_int("cache remove file dependents", removed == 3);
    check_int("cache empty after owner invalidation", glyph_cache_entry_count(&cache) == 0);
    check_int("cache byte count reset", glyph_cache_byte_count(&cache) == 0);
    check_int("cache validate after remove", glyph_cache_validate(&cache, &report));
    check_int("cache slice gone", !glyph_cache_contains(&cache, &slice_key));
    check_int("cache surface gone", !glyph_cache_contains(&cache, &surface_key));

    glyph_bitmap_free(&bitmap);
    glyph_cache_free(&cache);
    glyph_file_free(&file);
}

int main(void) {
    test_bitmap_basic();
    test_bitmap_transforms();
    test_pack_plan();
    test_layout_and_validation();
    test_manifest_selection();
    test_edit_operations();
    test_row_alloc_reload_refreshes_cache();
    test_scene_lifecycle_documents();
    test_cache_file_invalidation_removes_dependents();
    if (failures) {
        fprintf(stderr, "%d module test failure(s)\n", failures);
        return 1;
    }
    return 0;
}
