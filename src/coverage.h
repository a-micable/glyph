#ifndef GLYPH_COVERAGE_H
#define GLYPH_COVERAGE_H

#include "atlas.h"
#include "manifest.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    uint32_t first;
    uint32_t last;
} GlyphCoverageRange;

typedef struct {
    uint32_t count;
    uint32_t capacity;
    GlyphCoverageRange *ranges;
} GlyphCoverageSet;

typedef struct {
    uint32_t available_ids;
    uint32_t requested_ids;
    uint32_t present_ids;
    uint32_t missing_ids;
    uint32_t available_ranges;
    uint32_t requested_ranges;
    uint32_t present_ranges;
    uint32_t missing_ranges;
    double coverage;
} GlyphCoverageStats;

typedef struct {
    GlyphCoverageSet available;
    GlyphCoverageSet requested;
    GlyphCoverageSet present;
    GlyphCoverageSet missing;
    GlyphCoverageStats stats;
} GlyphCoverageReport;

void glyph_coverage_init(GlyphCoverageSet *set);
void glyph_coverage_free(GlyphCoverageSet *set);
void glyph_coverage_clear(GlyphCoverageSet *set);
int glyph_coverage_copy(GlyphCoverageSet *dst, const GlyphCoverageSet *src);

int glyph_coverage_is_empty(const GlyphCoverageSet *set);
uint32_t glyph_coverage_range_count(const GlyphCoverageSet *set);
uint64_t glyph_coverage_id_count64(const GlyphCoverageSet *set);
uint32_t glyph_coverage_id_count(const GlyphCoverageSet *set);
int glyph_coverage_contains(const GlyphCoverageSet *set, uint32_t id);
int glyph_coverage_contains_range(const GlyphCoverageSet *set, uint32_t first, uint32_t last);

int glyph_coverage_add_id(GlyphCoverageSet *set, uint32_t id);
int glyph_coverage_add_range(GlyphCoverageSet *set, uint32_t first, uint32_t last);
int glyph_coverage_add_set(GlyphCoverageSet *dst, const GlyphCoverageSet *src);
int glyph_coverage_remove_id(GlyphCoverageSet *set, uint32_t id);
int glyph_coverage_remove_range(GlyphCoverageSet *set, uint32_t first, uint32_t last);
int glyph_coverage_remove_set(GlyphCoverageSet *dst, const GlyphCoverageSet *src);

int glyph_coverage_union(const GlyphCoverageSet *a, const GlyphCoverageSet *b, GlyphCoverageSet *out);
int glyph_coverage_intersection(const GlyphCoverageSet *a, const GlyphCoverageSet *b, GlyphCoverageSet *out);
int glyph_coverage_difference(const GlyphCoverageSet *a, const GlyphCoverageSet *b, GlyphCoverageSet *out);
int glyph_coverage_equals(const GlyphCoverageSet *a, const GlyphCoverageSet *b);

int glyph_coverage_from_file(const GlyphFile *file, GlyphCoverageSet *out);
int glyph_coverage_from_manifest(const GlyphManifest *manifest, GlyphCoverageSet *out);
int glyph_coverage_from_selection(const GlyphSelectionSet *selection, GlyphCoverageSet *out);
int glyph_coverage_from_text(const char *text, GlyphCoverageSet *out, char *error, size_t error_cap);

/*
 * Parses a compact coverage expression into a normalized range set.
 *
 * Supported atoms:
 *   65, 0x41, U+0041, 65-90, U+0041..U+005A
 *   'A', '\n', '\u0041', "\u0041BC", [nested expression]
 *
 * Operators, in descending precedence:
 *   &             intersection
 *   -             difference (binary, with whitespace around range-like values)
 *   | + , space   union
 *
 * Dash ranges bind inside atoms when written without whitespace, for example
 * U+0041-U+005A. Parentheses may be used for grouping.
 */
int glyph_coverage_parse_expression(const char *expr, GlyphCoverageSet *out, char *error, size_t error_cap);

void glyph_coverage_report_init(GlyphCoverageReport *report);
void glyph_coverage_report_free(GlyphCoverageReport *report);
int glyph_coverage_compare(const GlyphCoverageSet *available, const GlyphCoverageSet *requested, GlyphCoverageReport *report);
int glyph_coverage_compare_text(const GlyphFile *file, const char *text, GlyphCoverageReport *report, char *error, size_t error_cap);
int glyph_coverage_compare_expression(const GlyphFile *file, const char *expr, GlyphCoverageReport *report, char *error, size_t error_cap);
int glyph_coverage_compare_manifest_text(const GlyphManifest *manifest, const char *text, GlyphCoverageReport *report, char *error, size_t error_cap);

GlyphCoverageStats glyph_coverage_stats(const GlyphCoverageSet *available, const GlyphCoverageSet *requested);

int glyph_coverage_write_set(FILE *fp, const GlyphCoverageSet *set);
int glyph_coverage_write_missing(FILE *fp, const GlyphCoverageSet *missing);
int glyph_coverage_write_report(FILE *fp, const GlyphCoverageReport *report);
int glyph_coverage_format_set(const GlyphCoverageSet *set, char *buf, size_t buf_cap);
int glyph_coverage_format_stats(const GlyphCoverageStats *stats, char *buf, size_t buf_cap);

#endif
