#ifndef GLYPH_FILTERS_H
#define GLYPH_FILTERS_H

#include "bitmap.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GLYPH_FILTER_KERNEL_MAX 49u
#define GLYPH_FILTER_PIPELINE_MAX_STEPS 32u
#define GLYPH_FILTER_LABEL_NONE 0u

typedef enum {
    GLYPH_FILTER_EDGE_CLAMP = 0,
    GLYPH_FILTER_EDGE_ZERO = 1,
    GLYPH_FILTER_EDGE_WRAP = 2,
    GLYPH_FILTER_EDGE_REFLECT = 3
} GlyphFilterEdgeMode;

typedef enum {
    GLYPH_FILTER_CONVOLVE_ABSOLUTE = 1u << 0,
    GLYPH_FILTER_CONVOLVE_NORMALIZE = 1u << 1,
    GLYPH_FILTER_CONVOLVE_PRESERVE_ALPHA = 1u << 2
} GlyphFilterConvolveFlags;

typedef enum {
    GLYPH_FILTER_CONNECT_4 = 4,
    GLYPH_FILTER_CONNECT_8 = 8
} GlyphFilterConnectivity;

typedef enum {
    GLYPH_FILTER_DISTANCE_INSIDE = 0,
    GLYPH_FILTER_DISTANCE_OUTSIDE = 1,
    GLYPH_FILTER_DISTANCE_SIGNED = 2
} GlyphFilterDistanceMode;

typedef enum {
    GLYPH_FILTER_CURVE_LINEAR = 0,
    GLYPH_FILTER_CURVE_GAMMA = 1,
    GLYPH_FILTER_CURVE_CONTRAST = 2,
    GLYPH_FILTER_CURVE_LEVELS = 3,
    GLYPH_FILTER_CURVE_LOOKUP = 4
} GlyphFilterCurveKind;

typedef enum {
    GLYPH_FILTER_STEP_CONVOLVE = 0,
    GLYPH_FILTER_STEP_SHARPEN = 1,
    GLYPH_FILTER_STEP_EMBOSS = 2,
    GLYPH_FILTER_STEP_GAUSSIAN_BLUR = 3,
    GLYPH_FILTER_STEP_SOBEL = 4,
    GLYPH_FILTER_STEP_DISTANCE = 5,
    GLYPH_FILTER_STEP_OUTLINE = 6,
    GLYPH_FILTER_STEP_CURVE = 7,
    GLYPH_FILTER_STEP_THRESHOLD = 8,
    GLYPH_FILTER_STEP_INVERT = 9,
    GLYPH_FILTER_STEP_NORMALIZE = 10,
    GLYPH_FILTER_STEP_DILATE = 11,
    GLYPH_FILTER_STEP_ERODE = 12,
    GLYPH_FILTER_STEP_BOX_BLUR = 13,
    GLYPH_FILTER_STEP_CROP_COMPONENTS = 14
} GlyphFilterStepKind;

typedef struct {
    uint32_t width;
    uint32_t height;
    int32_t anchor_x;
    int32_t anchor_y;
    int32_t divisor;
    int32_t bias;
    int32_t values[GLYPH_FILTER_KERNEL_MAX];
} GlyphFilterKernel;

typedef struct {
    GlyphFilterCurveKind kind;
    double gamma;
    double contrast;
    uint8_t input_black;
    uint8_t input_white;
    uint8_t output_black;
    uint8_t output_white;
    uint8_t midpoint;
    uint8_t table[256];
} GlyphFilterCurve;

typedef struct {
    GlyphBitmapRect bounds;
    uint32_t label;
    uint32_t area;
    uint64_t sum_x;
    uint64_t sum_y;
    uint8_t min_value;
    uint8_t max_value;
} GlyphFilterComponent;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t count;
    uint32_t capacity;
    uint32_t *labels;
    GlyphFilterComponent *components;
} GlyphFilterComponentMap;

typedef struct {
    GlyphFilterStepKind kind;
    union {
        struct {
            GlyphFilterKernel kernel;
            GlyphFilterEdgeMode edge_mode;
            uint32_t flags;
        } convolve;
        struct {
            uint32_t amount;
        } sharpen;
        struct {
            int32_t depth;
            uint8_t midpoint;
        } emboss;
        struct {
            uint32_t radius;
            uint32_t passes;
        } gaussian_blur;
        struct {
            uint8_t threshold;
            int include_diagonals;
        } sobel;
        struct {
            uint8_t threshold;
            uint32_t max_distance;
            GlyphFilterDistanceMode mode;
        } distance;
        struct {
            uint32_t radius;
            uint8_t threshold;
            uint8_t value;
            int outside_only;
        } outline;
        struct {
            GlyphFilterCurve curve;
        } curve;
        struct {
            uint8_t threshold;
            uint8_t low_value;
            uint8_t high_value;
        } threshold;
        struct {
            uint32_t radius;
        } morphology;
        struct {
            uint8_t threshold;
            uint32_t padding;
            uint32_t min_area;
            GlyphFilterConnectivity connectivity;
        } crop_components;
    } params;
} GlyphFilterStep;

typedef struct {
    uint32_t count;
    GlyphFilterStep steps[GLYPH_FILTER_PIPELINE_MAX_STEPS];
} GlyphFilterPipeline;

void glyph_filter_kernel_init(GlyphFilterKernel *kernel);
int glyph_filter_kernel_set(GlyphFilterKernel *kernel,
                            uint32_t width,
                            uint32_t height,
                            const int32_t *values,
                            int32_t divisor,
                            int32_t bias);
int glyph_filter_kernel_box(GlyphFilterKernel *kernel, uint32_t radius);
int glyph_filter_kernel_sharpen(GlyphFilterKernel *kernel, uint32_t amount);
int glyph_filter_kernel_emboss(GlyphFilterKernel *kernel, int32_t depth, uint8_t midpoint);
int glyph_filter_kernel_gaussian3(GlyphFilterKernel *kernel);
int glyph_filter_kernel_sobel_x(GlyphFilterKernel *kernel);
int glyph_filter_kernel_sobel_y(GlyphFilterKernel *kernel);

int glyph_filter_convolve(GlyphBitmap *dst,
                          const GlyphBitmap *src,
                          const GlyphFilterKernel *kernel,
                          GlyphFilterEdgeMode edge_mode,
                          uint32_t flags);
int glyph_filter_sharpen(GlyphBitmap *dst, const GlyphBitmap *src, uint32_t amount);
int glyph_filter_emboss(GlyphBitmap *dst, const GlyphBitmap *src, int32_t depth, uint8_t midpoint);
int glyph_filter_gaussian_blur(GlyphBitmap *dst, const GlyphBitmap *src, uint32_t radius, uint32_t passes);
int glyph_filter_sobel(GlyphBitmap *dst, const GlyphBitmap *src, uint8_t threshold, int include_diagonals);

int glyph_filter_distance_transform(GlyphBitmap *dst,
                                    const GlyphBitmap *src,
                                    uint8_t threshold,
                                    uint32_t max_distance,
                                    GlyphFilterDistanceMode mode);
int glyph_filter_outline(GlyphBitmap *dst,
                         const GlyphBitmap *src,
                         uint32_t radius,
                         uint8_t threshold,
                         uint8_t value,
                         int outside_only);

void glyph_filter_curve_init(GlyphFilterCurve *curve);
int glyph_filter_curve_gamma(GlyphFilterCurve *curve, double gamma);
int glyph_filter_curve_contrast(GlyphFilterCurve *curve, double contrast, uint8_t midpoint);
int glyph_filter_curve_levels(GlyphFilterCurve *curve,
                              uint8_t input_black,
                              uint8_t input_white,
                              uint8_t output_black,
                              uint8_t output_white,
                              double gamma);
int glyph_filter_curve_lookup(GlyphFilterCurve *curve, const uint8_t table[256]);
int glyph_filter_apply_curve(GlyphBitmap *bitmap, const GlyphFilterCurve *curve);
int glyph_filter_apply_gamma(GlyphBitmap *bitmap, double gamma);
int glyph_filter_apply_contrast(GlyphBitmap *bitmap, double contrast, uint8_t midpoint);

uint32_t glyph_filter_flood_fill(GlyphBitmap *bitmap,
                                 uint32_t x,
                                 uint32_t y,
                                 uint8_t low_threshold,
                                 uint8_t high_threshold,
                                 uint8_t fill_value,
                                 GlyphFilterConnectivity connectivity,
                                 GlyphBitmapRect *bounds);

void glyph_filter_component_map_init(GlyphFilterComponentMap *map);
void glyph_filter_component_map_free(GlyphFilterComponentMap *map);
int glyph_filter_label_components(const GlyphBitmap *bitmap,
                                  uint8_t threshold,
                                  GlyphFilterConnectivity connectivity,
                                  GlyphFilterComponentMap *out);
const GlyphFilterComponent *glyph_filter_largest_component(const GlyphFilterComponentMap *map);
int glyph_filter_components_bounds(const GlyphFilterComponentMap *map,
                                   uint32_t min_area,
                                   uint32_t padding,
                                   GlyphBitmapRect *bounds);
int glyph_filter_crop_to_components(GlyphBitmap *dst,
                                    const GlyphBitmap *src,
                                    uint8_t threshold,
                                    uint32_t padding,
                                    uint32_t min_area,
                                    GlyphFilterConnectivity connectivity,
                                    GlyphBitmapRect *bounds);

void glyph_filter_pipeline_init(GlyphFilterPipeline *pipeline);
int glyph_filter_pipeline_add(GlyphFilterPipeline *pipeline, const GlyphFilterStep *step);
int glyph_filter_pipeline_apply(GlyphBitmap *dst, const GlyphBitmap *src, const GlyphFilterPipeline *pipeline);

#ifdef __cplusplus
}
#endif

#endif
