#ifndef GLYPH_FONTDB_H
#define GLYPH_FONTDB_H

#include "atlas.h"
#include "coverage.h"
#include "metrics.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GLYPH_FONTDB_DUPLICATE_PATH 0x00000001u
#define GLYPH_FONTDB_DUPLICATE_NAME_STYLE 0x00000002u
#define GLYPH_FONTDB_DUPLICATE_COVERAGE 0x00000004u
#define GLYPH_FONTDB_DUPLICATE_METRICS 0x00000008u
#define GLYPH_FONTDB_DUPLICATE_EXACT 0x80000000u

typedef enum {
    GLYPH_FONTDB_SORT_ID = 0,
    GLYPH_FONTDB_SORT_PATH,
    GLYPH_FONTDB_SORT_NAME,
    GLYPH_FONTDB_SORT_STYLE,
    GLYPH_FONTDB_SORT_GLYPH_COUNT,
    GLYPH_FONTDB_SORT_COVERAGE_FIRST,
    GLYPH_FONTDB_SORT_COVERAGE_LAST,
    GLYPH_FONTDB_SORT_ATLAS_AREA,
    GLYPH_FONTDB_SORT_MEAN_ADVANCE
} GlyphFontDbSortKey;

typedef struct {
    GlyphFontDbSortKey key;
    int descending;
} GlyphFontDbSort;

typedef struct {
    uint32_t glyph_count;
    uint32_t kerning_count;
    uint32_t row_count;
    uint32_t atlas_width;
    uint32_t atlas_height;
    uint16_t flags;
    int has_bounds;
    int32_t min_x;
    int32_t min_y;
    int32_t max_x;
    int32_t max_y;
    uint16_t min_width;
    uint16_t max_width;
    uint16_t min_height;
    uint16_t max_height;
    int16_t min_advance;
    int16_t max_advance;
    int64_t total_advance;
    double mean_advance;
    uint64_t total_area;
    uint64_t atlas_pixels;
    uint64_t ink_pixels;
    double ink_density;
} GlyphFontDbMetricsSnapshot;

typedef struct {
    GlyphCoverageSet coverage;
    uint32_t range_count;
    uint64_t id_count;
    uint32_t min_id;
    uint32_t max_id;
    double ascii_coverage;
    double latin1_coverage;
    double bmp_coverage;
} GlyphFontDbCoverageSummary;

typedef struct {
    uint32_t id;
    char *path;
    char *name;
    char *style;
    GlyphFontDbCoverageSummary coverage;
    GlyphFontDbMetricsSnapshot metrics;
    uint32_t user_flags;
} GlyphFontDbEntry;

typedef struct {
    uint32_t count;
    uint32_t capacity;
    GlyphFontDbEntry *entries;
    uint32_t next_id;
} GlyphFontDb;

typedef struct {
    const char *path_contains;
    const char *name_contains;
    const char *style_contains;
    const GlyphCoverageSet *requires_coverage;
    uint32_t requires_glyph;
    int use_requires_glyph;
    uint64_t min_glyphs;
    uint64_t max_glyphs;
    uint32_t min_ranges;
    uint32_t max_ranges;
    uint32_t user_flags_mask;
    uint32_t user_flags_value;
} GlyphFontDbFilter;

typedef struct {
    uint32_t count;
    uint32_t capacity;
    uint32_t *indices;
} GlyphFontDbIndexList;

typedef struct {
    uint32_t first_index;
    uint32_t second_index;
    uint32_t flags;
} GlyphFontDbDuplicate;

typedef struct {
    uint32_t count;
    uint32_t capacity;
    GlyphFontDbDuplicate *items;
} GlyphFontDbDuplicateList;

void glyph_fontdb_metrics_snapshot_clear(GlyphFontDbMetricsSnapshot *snapshot);
void glyph_fontdb_coverage_summary_init(GlyphFontDbCoverageSummary *summary);
void glyph_fontdb_coverage_summary_free(GlyphFontDbCoverageSummary *summary);
void glyph_fontdb_coverage_summary_clear(GlyphFontDbCoverageSummary *summary);
int glyph_fontdb_coverage_summary_set(GlyphFontDbCoverageSummary *summary, const GlyphCoverageSet *coverage);
int glyph_fontdb_coverage_summary_from_file(const GlyphFile *file, GlyphFontDbCoverageSummary *summary);

void glyph_fontdb_entry_init(GlyphFontDbEntry *entry);
void glyph_fontdb_entry_free(GlyphFontDbEntry *entry);
void glyph_fontdb_entry_clear(GlyphFontDbEntry *entry);
int glyph_fontdb_entry_copy(GlyphFontDbEntry *dst, const GlyphFontDbEntry *src);
int glyph_fontdb_entry_set_metadata(GlyphFontDbEntry *entry, const char *path, const char *name, const char *style);
int glyph_fontdb_entry_set_coverage(GlyphFontDbEntry *entry, const GlyphCoverageSet *coverage);
int glyph_fontdb_entry_set_metrics_from_file(GlyphFontDbEntry *entry, const GlyphFile *file);
int glyph_fontdb_entry_from_file(GlyphFontDbEntry *entry, uint32_t id, const char *path, const char *name, const char *style, const GlyphFile *file);
int glyph_fontdb_entry_has_glyph(const GlyphFontDbEntry *entry, uint32_t glyph_id);
int glyph_fontdb_entry_covers(const GlyphFontDbEntry *entry, const GlyphCoverageSet *coverage);

void glyph_fontdb_init(GlyphFontDb *db);
void glyph_fontdb_free(GlyphFontDb *db);
void glyph_fontdb_clear(GlyphFontDb *db);
int glyph_fontdb_reserve(GlyphFontDb *db, uint32_t capacity);
int glyph_fontdb_add(GlyphFontDb *db, const GlyphFontDbEntry *entry, uint32_t *index);
int glyph_fontdb_add_file(GlyphFontDb *db, const char *path, const char *name, const char *style, const GlyphFile *file, uint32_t *index);
int glyph_fontdb_remove_index(GlyphFontDb *db, uint32_t index);
int glyph_fontdb_remove_id(GlyphFontDb *db, uint32_t id);
int glyph_fontdb_remove_path(GlyphFontDb *db, const char *path);

GlyphFontDbEntry *glyph_fontdb_find_id(GlyphFontDb *db, uint32_t id);
const GlyphFontDbEntry *glyph_fontdb_find_id_const(const GlyphFontDb *db, uint32_t id);
GlyphFontDbEntry *glyph_fontdb_find_path(GlyphFontDb *db, const char *path);
const GlyphFontDbEntry *glyph_fontdb_find_path_const(const GlyphFontDb *db, const char *path);
GlyphFontDbEntry *glyph_fontdb_find_name_style(GlyphFontDb *db, const char *name, const char *style);
const GlyphFontDbEntry *glyph_fontdb_find_name_style_const(const GlyphFontDb *db, const char *name, const char *style);
int glyph_fontdb_find_id_index(const GlyphFontDb *db, uint32_t id, uint32_t *index);
int glyph_fontdb_find_path_index(const GlyphFontDb *db, const char *path, uint32_t *index);
int glyph_fontdb_find_name_style_index(const GlyphFontDb *db, const char *name, const char *style, uint32_t *index);

void glyph_fontdb_filter_init(GlyphFontDbFilter *filter);
int glyph_fontdb_filter_match(const GlyphFontDbEntry *entry, const GlyphFontDbFilter *filter);
void glyph_fontdb_index_list_init(GlyphFontDbIndexList *list);
void glyph_fontdb_index_list_free(GlyphFontDbIndexList *list);
void glyph_fontdb_index_list_clear(GlyphFontDbIndexList *list);
int glyph_fontdb_index_list_add(GlyphFontDbIndexList *list, uint32_t index);
int glyph_fontdb_filter(const GlyphFontDb *db, const GlyphFontDbFilter *filter, GlyphFontDbIndexList *out);
int glyph_fontdb_query_glyph(const GlyphFontDb *db, uint32_t glyph_id, GlyphFontDbIndexList *out);
int glyph_fontdb_query_coverage(const GlyphFontDb *db, const GlyphCoverageSet *coverage, GlyphFontDbIndexList *out);

void glyph_fontdb_sort(GlyphFontDb *db, GlyphFontDbSort sort);
int glyph_fontdb_compare_entries(const GlyphFontDbEntry *a, const GlyphFontDbEntry *b, GlyphFontDbSort sort);

void glyph_fontdb_duplicate_list_init(GlyphFontDbDuplicateList *list);
void glyph_fontdb_duplicate_list_free(GlyphFontDbDuplicateList *list);
void glyph_fontdb_duplicate_list_clear(GlyphFontDbDuplicateList *list);
int glyph_fontdb_duplicate_list_add(GlyphFontDbDuplicateList *list, uint32_t first_index, uint32_t second_index, uint32_t flags);
uint32_t glyph_fontdb_duplicate_flags(const GlyphFontDbEntry *a, const GlyphFontDbEntry *b);
int glyph_fontdb_find_duplicates(const GlyphFontDb *db, uint32_t match_flags, GlyphFontDbDuplicateList *out);

int glyph_fontdb_load(const char *path, GlyphFontDb *db, char *error, size_t error_cap);
int glyph_fontdb_save(const char *path, const GlyphFontDb *db, char *error, size_t error_cap);
int glyph_fontdb_read(FILE *fp, GlyphFontDb *db, char *error, size_t error_cap);
int glyph_fontdb_write(FILE *fp, const GlyphFontDb *db);

int glyph_fontdb_format_entry(const GlyphFontDbEntry *entry, char *buf, size_t buf_cap);
int glyph_fontdb_write_entry_report(FILE *fp, const GlyphFontDbEntry *entry);
int glyph_fontdb_write_report(FILE *fp, const GlyphFontDb *db);
int glyph_fontdb_write_duplicate_report(FILE *fp, const GlyphFontDb *db, const GlyphFontDbDuplicateList *duplicates);

#ifdef __cplusplus
}
#endif

#endif
