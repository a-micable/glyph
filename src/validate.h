#ifndef GLYPH_VALIDATE_H
#define GLYPH_VALIDATE_H

#include "atlas.h"
#include "header.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define GLYPH_VALIDATE_MAX_GLYPHS 4096u
#define GLYPH_VALIDATE_MAX_KERNING 65536u
#define GLYPH_VALIDATE_MAX_ROWS 4096u
#define GLYPH_VALIDATE_KNOWN_FLAGS GLYPH_FLAG_HINTS
#define GLYPH_VALIDATE_INDEX_NONE UINT32_MAX

typedef enum {
    GLYPH_VALIDATE_INFO = 0,
    GLYPH_VALIDATE_WARNING = 1,
    GLYPH_VALIDATE_ERROR = 2
} GlyphValidationSeverity;

typedef enum {
    GLYPH_VALIDATE_DIAG_HEADER_LIMITS = 0,
    GLYPH_VALIDATE_DIAG_POINTERS = 1,
    GLYPH_VALIDATE_DIAG_ATLAS_DIMENSIONS = 2,
    GLYPH_VALIDATE_DIAG_ROW_METADATA = 3,
    GLYPH_VALIDATE_DIAG_PLACEMENT_BOUNDS = 4,
    GLYPH_VALIDATE_DIAG_BITMAP_OFFSET = 5,
    GLYPH_VALIDATE_DIAG_DUPLICATE_GLYPH_ID = 6,
    GLYPH_VALIDATE_DIAG_GLYPH_OVERLAP = 7,
    GLYPH_VALIDATE_DIAG_KERNING_INDEX = 8,
    GLYPH_VALIDATE_DIAG_DUPLICATE_KERNING_PAIR = 9,
    GLYPH_VALIDATE_DIAG_HINTS = 10,
    GLYPH_VALIDATE_DIAG_STATISTICS = 11
} GlyphValidationCode;

typedef struct {
    GlyphValidationSeverity severity;
    GlyphValidationCode code;
    uint32_t index;
    uint32_t related_index;
    char *message;
} GlyphValidationDiagnostic;

typedef struct {
    uint32_t count;
    uint32_t capacity;
    GlyphValidationDiagnostic *items;
} GlyphValidationDiagnostics;

typedef struct {
    uint32_t count;
    uint32_t present_count;
    uint32_t empty_count;
    uint16_t min_length;
    uint16_t max_length;
    uint64_t total_bytes;
    double average_length;
} GlyphHintLengthSummary;

typedef struct {
    uint64_t atlas_pixels;
    uint64_t glyph_pixels;
    uint64_t nonzero_pixels;
    uint64_t overlapping_pixels;
    double glyph_coverage;
    double ink_coverage;
} GlyphAtlasOccupancyStats;

typedef struct {
    uint32_t row_count;
    uint32_t max_waste_row;
    uint32_t max_waste_pixels;
    uint64_t row_pixels;
    uint64_t used_pixels;
    uint64_t waste_pixels;
    double waste_ratio;
} GlyphRowWasteStats;

typedef struct {
    int valid;
    uint32_t error_count;
    uint32_t warning_count;
    uint32_t info_count;
    uint32_t glyph_count;
    uint32_t kerning_count;
    uint32_t hint_count;
    uint32_t row_count;
    uint32_t atlas_width;
    uint32_t atlas_height;
    uint32_t duplicate_glyph_ids;
    uint32_t overlapping_glyph_pairs;
    uint32_t invalid_kerning_pairs;
    uint32_t duplicate_kerning_pairs;
    GlyphHintLengthSummary hints;
    GlyphAtlasOccupancyStats occupancy;
    GlyphRowWasteStats rows;
} GlyphValidationReport;

void glyph_validation_diagnostics_init(GlyphValidationDiagnostics *diagnostics);
void glyph_validation_diagnostics_free(GlyphValidationDiagnostics *diagnostics);
int glyph_validation_diagnostics_add(GlyphValidationDiagnostics *diagnostics,
                                     GlyphValidationSeverity severity,
                                     GlyphValidationCode code,
                                     uint32_t index,
                                     uint32_t related_index,
                                     const char *message);

void glyph_validation_report_init(GlyphValidationReport *report);
int glyph_validate_file(const GlyphFile *file,
                        GlyphValidationDiagnostics *diagnostics,
                        GlyphValidationReport *report);
const char *glyph_validation_severity_name(GlyphValidationSeverity severity);
const char *glyph_validation_code_name(GlyphValidationCode code);
int glyph_validation_write_text_report(FILE *fp,
                                       const GlyphValidationReport *report,
                                       const GlyphValidationDiagnostics *diagnostics);

#endif
