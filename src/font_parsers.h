/*
 * Font Format Parsers - TTF, OTF, and other font formats
 * 
 * Comprehensive font parsing and conversion infrastructure with
 * support for multiple font formats and detailed metadata extraction.
 */

#ifndef GLYPH_FONT_PARSERS_H
#define GLYPH_FONT_PARSERS_H

#include <stdint.h>

/* Font Format Types */
typedef enum {
    FONT_FORMAT_TTF,
    FONT_FORMAT_OTF,
    FONT_FORMAT_WOFF,
    FONT_FORMAT_WOFF2,
    FONT_FORMAT_EOT,
    FONT_FORMAT_GLYPH,
    FONT_FORMAT_BITMAP_PNG,
    FONT_FORMAT_BITMAP_PGM
} font_format_t;

/* Glyph Outline Point */
typedef struct {
    int16_t x;
    int16_t y;
    int16_t on_curve;
} glyph_point_t;

/* Glyph Contour */
typedef struct {
    glyph_point_t *points;
    int point_count;
    int16_t *end_points;
    int contour_count;
} glyph_contour_t;

/* Glyph Metrics */
typedef struct {
    uint32_t glyph_id;
    int16_t x_min;
    int16_t y_min;
    int16_t x_max;
    int16_t y_max;
    int16_t advance_width;
    int16_t left_side_bearing;
    int number_of_contours;
} glyph_metrics_t;

/* Font Metrics */
typedef struct {
    const char *family_name;
    const char *style_name;
    uint16_t units_per_em;
    int16_t ascender;
    int16_t descender;
    int16_t line_gap;
    uint16_t num_glyphs;
    int is_italic;
    int is_monospace;
} font_metrics_t;

/* Font Parser Handle */
typedef struct font_parser_s *font_parser_t;

/* API Functions */

/* Parse font file */
font_parser_t font_parser_create_from_file(const char *path);
font_parser_t font_parser_create_from_buffer(const void *buffer,
                                             size_t size);
void font_parser_destroy(font_parser_t parser);

/* Detect format */
font_format_t font_detect_format(const void *buffer, size_t size);
const char *font_format_to_string(font_format_t format);

/* Font Information */
int font_get_metrics(font_parser_t parser, font_metrics_t *metrics);
uint32_t font_get_glyph_count(font_parser_t parser);
uint32_t font_get_glyph_id(font_parser_t parser, uint32_t codepoint);

/* Glyph Data */
int font_get_glyph_metrics(font_parser_t parser, uint32_t glyph_id,
                           glyph_metrics_t *metrics);
int font_get_glyph_bitmap(font_parser_t parser, uint32_t glyph_id,
                         int ppem, void **bitmap,
                         int *width, int *height, int *stride);
int font_get_glyph_outline(font_parser_t parser, uint32_t glyph_id,
                          glyph_contour_t *contour);

/* Kerning */
int16_t font_get_kerning(font_parser_t parser, uint32_t left_glyph,
                        uint32_t right_glyph);

/* Ligatures */
int font_get_ligatures(font_parser_t parser, uint32_t **ligature_pairs,
                       uint32_t **replacement_glyphs, int *count);

/* OpenType features */
int font_has_feature(font_parser_t parser, const char *feature_tag);
int font_get_feature_list(font_parser_t parser, char **feature_tags,
                         int max_tags);

/* Subsetting */
int font_subset_glyphs(font_parser_t parser, const uint32_t *glyph_ids,
                       int count, void **subset_data,
                       size_t *subset_size);

/* Format Conversion */
int font_convert_format(font_parser_t parser, font_format_t to_format,
                       void **output_data, size_t *output_size);

/* Validation */
int font_validate(font_parser_t parser);
int font_check_glyph_id(font_parser_t parser, uint32_t glyph_id);

/* Batch Processing */
typedef struct {
    uint32_t *glyph_ids;
    int glyph_count;
    int ppem;
    int enable_hinting;
} font_batch_request_t;

int font_batch_render(font_parser_t parser, const font_batch_request_t *req,
                     void **bitmaps, int *widths, int *heights);

/* CFF/Type2 Charstrings */
int font_parse_charstrings(font_parser_t parser, uint32_t glyph_id,
                          uint8_t **charstring, size_t *size);

/* Variable Fonts (Variations) */
int font_has_variations(font_parser_t parser);
int font_get_variation_axes(font_parser_t parser,
                           void **axes,  /* Axis data */
                           int *count);

#endif /* GLYPH_FONT_PARSERS_H */
