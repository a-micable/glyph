#ifndef GLYPH_MANIFEST_H
#define GLYPH_MANIFEST_H

#include "atlas.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t length;
    uint8_t *bytes;
} GlyphManifestHintBytes;

typedef struct {
    uint32_t id;
    int16_t advance;
    int16_t x;
    int16_t y;
    uint16_t width;
    uint16_t height;
    GlyphManifestHintBytes hint;
} GlyphManifestGlyph;

typedef struct {
    uint32_t left_id;
    uint32_t right_id;
    int16_t offset;
} GlyphManifestKerning;

typedef struct {
    uint32_t glyph_count;
    GlyphManifestGlyph *glyphs;
    uint32_t kerning_count;
    GlyphManifestKerning *kerning;
} GlyphManifest;

typedef struct {
    uint32_t count;
    uint32_t *ids;
} GlyphSelectionSet;

typedef struct {
    uint32_t glyph_count;
    uint32_t *glyph_indices;
    uint32_t kerning_count;
    uint32_t *kerning_indices;
} GlyphSubsetPlan;

void glyph_manifest_free(GlyphManifest *manifest);
int glyph_manifest_load(const char *path, GlyphManifest *manifest, char *error, size_t error_cap);
int glyph_manifest_write(const char *path, const GlyphManifest *manifest, char *error, size_t error_cap);
GlyphManifestGlyph *glyph_manifest_find_glyph(GlyphManifest *manifest, uint32_t id);
const GlyphManifestGlyph *glyph_manifest_find_glyph_const(const GlyphManifest *manifest, uint32_t id);

void glyph_selection_free(GlyphSelectionSet *selection);
int glyph_selection_parse_ids(const char *text, GlyphSelectionSet *selection, char *error, size_t error_cap);
int glyph_selection_parse_text(const char *text, GlyphSelectionSet *selection, char *error, size_t error_cap);
int glyph_selection_contains(const GlyphSelectionSet *selection, uint32_t id);

void glyph_subset_plan_free(GlyphSubsetPlan *plan);
int glyph_subset_plan_from_file(const GlyphFile *file, const GlyphSelectionSet *selection, GlyphSubsetPlan *plan, char *error, size_t error_cap);

int glyph_manifest_from_file(const GlyphFile *file, GlyphManifest *manifest, char *error, size_t error_cap);
int glyph_manifest_write_file(const char *path, const GlyphFile *file, char *error, size_t error_cap);

#endif
