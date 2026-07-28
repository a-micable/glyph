#include "filters.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define FILTER_DISTANCE_INF 0x3fffffff
typedef struct {
    uint32_t x;
    uint32_t y;
} FilterPoint;

static int bitmap_is_valid(const GlyphBitmap *bitmap) {
    return bitmap && bitmap->width && bitmap->height && bitmap->stride >= bitmap->width && bitmap->pixels;
}
static int checked_image_size(uint32_t width, uint32_t height, size_t *size) {
    if (!width || !height) {
        return 0;
    }
    if ((size_t)width > SIZE_MAX / (size_t)height) {
        return 0;
    }
    *size = (size_t)width * (size_t)height;
    return 1;
}

static int checked_count_size(size_t count, size_t item_size, size_t *size) {
    if (item_size && count > SIZE_MAX / item_size) {
        return 0;
    }
    *size = count * item_size;
    return 1;
}

static uint8_t *pixel_at(GlyphBitmap *bitmap, uint32_t x, uint32_t y) {
    return bitmap->pixels + (size_t)y * bitmap->stride + x;
}

static const uint8_t *const_pixel_at(const GlyphBitmap *bitmap, uint32_t x, uint32_t y) {
    return bitmap->pixels + (size_t)y * bitmap->stride + x;
}

static int alloc_temp(GlyphBitmap *bitmap, uint32_t width, uint32_t height) {
    size_t size = 0;

    if (!checked_image_size(width, height, &size)) {
        return 0;
    }
    bitmap->width = width;
    bitmap->height = height;
    bitmap->stride = width;
    bitmap->pixels = (uint8_t *)calloc(size, 1);
    return bitmap->pixels != NULL;
}

static int assign_bitmap(GlyphBitmap *dst, GlyphBitmap *tmp) {
    glyph_bitmap_free(dst);
    *dst = *tmp;
    glyph_bitmap_init(tmp);
    return 1;
}

static uint8_t clamp_u8_int(int64_t value) {
    if (value <= 0) {
        return 0;
    }
    if (value >= 255) {
        return 255;
    }
    return (uint8_t)value;
}

static uint8_t clamp_u8_double(double value) {
    if (value <= 0.0) {
        return 0;
    }
    if (value >= 255.0) {
        return 255;
    }
    return (uint8_t)(value + 0.5);
}

static int kernel_is_valid(const GlyphFilterKernel *kernel) {
    uint32_t count;

    if (!kernel || !kernel->width || !kernel->height || kernel->width > 7u || kernel->height > 7u) {
        return 0;
    }
    count = kernel->width * kernel->height;
    if (count > GLYPH_FILTER_KERNEL_MAX) {
        return 0;
    }
    if (kernel->anchor_x < 0 || kernel->anchor_y < 0) {
        return 0;
    }
    if ((uint32_t)kernel->anchor_x >= kernel->width || (uint32_t)kernel->anchor_y >= kernel->height) {
        return 0;
    }
    return 1;
}

static int64_t abs_i64(int64_t value) {
    return value < 0 ? -value : value;
}

static int normalize_coord(int32_t value, uint32_t limit, GlyphFilterEdgeMode edge_mode, uint32_t *out) {
    int32_t max = (int32_t)limit - 1;

    if (value >= 0 && (uint32_t)value < limit) {
        *out = (uint32_t)value;
        return 1;
    }
    if (edge_mode == GLYPH_FILTER_EDGE_ZERO) {
        return 0;
    }
    if (edge_mode == GLYPH_FILTER_EDGE_CLAMP) {
        *out = value < 0 ? 0u : (uint32_t)max;
        return 1;
    }
    if (edge_mode == GLYPH_FILTER_EDGE_WRAP) {
        int32_t m = (int32_t)limit;
        int32_t v = value % m;
        if (v < 0) {
            v += m;
        }
        *out = (uint32_t)v;
        return 1;
    }
    if (edge_mode == GLYPH_FILTER_EDGE_REFLECT) {
        int32_t period;
        int32_t v;

        if (limit == 1u) {
            *out = 0;
            return 1;
        }
        period = (int32_t)(limit * 2u - 2u);
        v = value % period;
        if (v < 0) {
            v += period;
        }
        if (v >= (int32_t)limit) {
            v = period - v;
        }
        *out = (uint32_t)v;
        return 1;
    }
    return 0;
}

static uint8_t sample_edge(const GlyphBitmap *bitmap, int32_t x, int32_t y, GlyphFilterEdgeMode edge_mode) {
    uint32_t sx;
    uint32_t sy;

    if (!normalize_coord(x, bitmap->width, edge_mode, &sx) || !normalize_coord(y, bitmap->height, edge_mode, &sy)) {
        return 0;
    }
    return *const_pixel_at(bitmap, sx, sy);
}

static int sum_kernel_values(const GlyphFilterKernel *kernel, int64_t *positive_sum, int64_t *absolute_sum) {
    uint32_t i;
    uint32_t count;
    int64_t pos = 0;
    int64_t abs_sum = 0;

    if (!kernel_is_valid(kernel)) {
        return 0;
    }
    count = kernel->width * kernel->height;
    for (i = 0; i < count; i++) {
        int32_t value = kernel->values[i];
        if (value > 0) {
            pos += value;
        }
        abs_sum += abs_i64(value);
    }
    if (positive_sum) {
        *positive_sum = pos;
    }
    if (absolute_sum) {
        *absolute_sum = abs_sum;
    }
    return 1;
}

static uint32_t radius_min(uint32_t value, uint32_t radius) {
    return value > radius ? value - radius : 0;
}

static uint32_t radius_max(uint32_t value, uint32_t radius, uint32_t limit) {
    uint32_t last = limit - 1;

    if (radius >= last - value) {
        return last;
    }
    return value + radius;
}

static uint32_t rect_right(const GlyphBitmapRect *rect) {
    return rect->x + rect->width;
}

static uint32_t rect_bottom(const GlyphBitmapRect *rect) {
    return rect->y + rect->height;
}

static void rect_clear(GlyphBitmapRect *rect) {
    if (rect) {
        rect->x = 0;
        rect->y = 0;
        rect->width = 0;
        rect->height = 0;
    }
}

static void rect_set_point(GlyphBitmapRect *rect, uint32_t x, uint32_t y) {
    rect->x = x;
    rect->y = y;
    rect->width = 1;
    rect->height = 1;
}

static void rect_include_point(GlyphBitmapRect *rect, uint32_t x, uint32_t y) {
    uint32_t right;
    uint32_t bottom;

    if (!rect->width || !rect->height) {
        rect_set_point(rect, x, y);
        return;
    }
    right = rect_right(rect);
    bottom = rect_bottom(rect);
    if (x < rect->x) {
        rect->width += rect->x - x;
        rect->x = x;
    } else if (x >= right) {
        rect->width = x - rect->x + 1;
    }
    if (y < rect->y) {
        rect->height += rect->y - y;
        rect->y = y;
    } else if (y >= bottom) {
        rect->height = y - rect->y + 1;
    }
}

static void rect_include_rect(GlyphBitmapRect *dst, const GlyphBitmapRect *src) {
    if (!src || !src->width || !src->height) {
        return;
    }
    rect_include_point(dst, src->x, src->y);
    rect_include_point(dst, src->x + src->width - 1, src->y + src->height - 1);
}

static void rect_pad_clamped(GlyphBitmapRect *rect, uint32_t padding, uint32_t width, uint32_t height) {
    uint32_t right;
    uint32_t bottom;

    if (!rect || !rect->width || !rect->height) {
        return;
    }
    right = rect_right(rect);
    bottom = rect_bottom(rect);
    rect->x = rect->x > padding ? rect->x - padding : 0;
    rect->y = rect->y > padding ? rect->y - padding : 0;
    right = right + padding < right ? width : right + padding;
    bottom = bottom + padding < bottom ? height : bottom + padding;
    if (right > width) {
        right = width;
    }
    if (bottom > height) {
        bottom = height;
    }
    rect->width = right > rect->x ? right - rect->x : 0;
    rect->height = bottom > rect->y ? bottom - rect->y : 0;
}

void glyph_filter_kernel_init(GlyphFilterKernel *kernel) {
    if (kernel) {
        memset(kernel, 0, sizeof(*kernel));
    }
}

int glyph_filter_kernel_set(GlyphFilterKernel *kernel,
                            uint32_t width,
                            uint32_t height,
                            const int32_t *values,
                            int32_t divisor,
                            int32_t bias) {
    uint32_t count;
    uint32_t i;

    if (!kernel || !values || !width || !height || width > 7u || height > 7u) {
        return 0;
    }
    count = width * height;
    if (count > GLYPH_FILTER_KERNEL_MAX) {
        return 0;
    }
    glyph_filter_kernel_init(kernel);
    kernel->width = width;
    kernel->height = height;
    kernel->anchor_x = (int32_t)(width / 2u);
    kernel->anchor_y = (int32_t)(height / 2u);
    kernel->divisor = divisor;
    kernel->bias = bias;
    for (i = 0; i < count; i++) {
        kernel->values[i] = values[i];
    }
    return 1;
}

int glyph_filter_kernel_box(GlyphFilterKernel *kernel, uint32_t radius) {
    uint32_t width;
    uint32_t height;
    uint32_t count;
    uint32_t i;

    if (!kernel || radius > 3u) {
        return 0;
    }
    width = radius * 2u + 1u;
    height = width;
    count = width * height;
    glyph_filter_kernel_init(kernel);
    kernel->width = width;
    kernel->height = height;
    kernel->anchor_x = (int32_t)radius;
    kernel->anchor_y = (int32_t)radius;
    kernel->divisor = (int32_t)count;
    for (i = 0; i < count; i++) {
        kernel->values[i] = 1;
    }
    return 1;
}

int glyph_filter_kernel_sharpen(GlyphFilterKernel *kernel, uint32_t amount) {
    int32_t values[9];
    int32_t a;

    if (!kernel || amount > 64u) {
        return 0;
    }
    a = (int32_t)(amount ? amount : 1u);
    values[0] = 0;
    values[1] = -a;
    values[2] = 0;
    values[3] = -a;
    values[4] = 4 * a + 1;
    values[5] = -a;
    values[6] = 0;
    values[7] = -a;
    values[8] = 0;
    return glyph_filter_kernel_set(kernel, 3, 3, values, 1, 0);
}

int glyph_filter_kernel_emboss(GlyphFilterKernel *kernel, int32_t depth, uint8_t midpoint) {
    int32_t values[9];

    if (!kernel || depth < -64 || depth > 64) {
        return 0;
    }
    if (depth == 0) {
        depth = 1;
    }
    values[0] = -2 * depth;
    values[1] = -depth;
    values[2] = 0;
    values[3] = -depth;
    values[4] = 1;
    values[5] = depth;
    values[6] = 0;
    values[7] = depth;
    values[8] = 2 * depth;
    return glyph_filter_kernel_set(kernel, 3, 3, values, 1, midpoint);
}

int glyph_filter_kernel_gaussian3(GlyphFilterKernel *kernel) {
    static const int32_t values[9] = {
        1, 2, 1,
        2, 4, 2,
        1, 2, 1
    };

    return glyph_filter_kernel_set(kernel, 3, 3, values, 16, 0);
}

int glyph_filter_kernel_sobel_x(GlyphFilterKernel *kernel) {
    static const int32_t values[9] = {
        -1, 0, 1,
        -2, 0, 2,
        -1, 0, 1
    };

    return glyph_filter_kernel_set(kernel, 3, 3, values, 1, 0);
}

int glyph_filter_kernel_sobel_y(GlyphFilterKernel *kernel) {
    static const int32_t values[9] = {
        -1, -2, -1,
         0,  0,  0,
         1,  2,  1
    };

    return glyph_filter_kernel_set(kernel, 3, 3, values, 1, 0);
}

int glyph_filter_convolve(GlyphBitmap *dst,
                          const GlyphBitmap *src,
                          const GlyphFilterKernel *kernel,
                          GlyphFilterEdgeMode edge_mode,
                          uint32_t flags) {
    GlyphBitmap tmp;
    uint32_t x;
    uint32_t y;
    int64_t divisor;

    if (!dst || !bitmap_is_valid(src) || !kernel_is_valid(kernel)) {
        return 0;
    }
    glyph_bitmap_init(&tmp);
    if (!alloc_temp(&tmp, src->width, src->height)) {
        return 0;
    }
    divisor = kernel->divisor;
    if ((flags & GLYPH_FILTER_CONVOLVE_NORMALIZE) || divisor == 0) {
        int64_t pos = 0;
        int64_t abs_sum = 0;
        if (!sum_kernel_values(kernel, &pos, &abs_sum)) {
            glyph_bitmap_free(&tmp);
            return 0;
        }
        divisor = pos ? pos : abs_sum;
        if (!divisor) {
            divisor = 1;
        }
    }
    for (y = 0; y < src->height; y++) {
        for (x = 0; x < src->width; x++) {
            int64_t sum = 0;
            uint32_t ky;
            for (ky = 0; ky < kernel->height; ky++) {
                uint32_t kx;
                for (kx = 0; kx < kernel->width; kx++) {
                    uint32_t index = ky * kernel->width + kx;
                    int32_t sx = (int32_t)x + (int32_t)kx - kernel->anchor_x;
                    int32_t sy = (int32_t)y + (int32_t)ky - kernel->anchor_y;
                    sum += (int64_t)sample_edge(src, sx, sy, edge_mode) * kernel->values[index];
                }
            }
            if (flags & GLYPH_FILTER_CONVOLVE_ABSOLUTE) {
                sum = abs_i64(sum);
            }
            if (divisor != 1) {
                if (sum >= 0) {
                    sum = (sum + divisor / 2) / divisor;
                } else {
                    sum = (sum - divisor / 2) / divisor;
                }
            }
            sum += kernel->bias;
            *pixel_at(&tmp, x, y) = clamp_u8_int(sum);
        }
    }
    return assign_bitmap(dst, &tmp);
}

int glyph_filter_sharpen(GlyphBitmap *dst, const GlyphBitmap *src, uint32_t amount) {
    GlyphFilterKernel kernel;

    if (!glyph_filter_kernel_sharpen(&kernel, amount)) {
        return 0;
    }
    return glyph_filter_convolve(dst, src, &kernel, GLYPH_FILTER_EDGE_CLAMP, 0);
}

int glyph_filter_emboss(GlyphBitmap *dst, const GlyphBitmap *src, int32_t depth, uint8_t midpoint) {
    GlyphFilterKernel kernel;

    if (!glyph_filter_kernel_emboss(&kernel, depth, midpoint)) {
        return 0;
    }
    return glyph_filter_convolve(dst, src, &kernel, GLYPH_FILTER_EDGE_CLAMP, 0);
}

int glyph_filter_gaussian_blur(GlyphBitmap *dst, const GlyphBitmap *src, uint32_t radius, uint32_t passes) {
    GlyphBitmap a;
    GlyphBitmap b;
    GlyphFilterKernel kernel;
    uint32_t pass;

    if (!dst || !bitmap_is_valid(src) || radius > 3u) {
        return 0;
    }
    if (passes == 0) {
        return glyph_bitmap_copy(dst, src);
    }
    glyph_bitmap_init(&a);
    glyph_bitmap_init(&b);
    if (radius == 0) {
        if (!glyph_filter_kernel_gaussian3(&kernel)) {
            return 0;
        }
    } else if (!glyph_filter_kernel_box(&kernel, radius)) {
        return 0;
    }
    if (!glyph_bitmap_copy(&a, src)) {
        return 0;
    }
    for (pass = 0; pass < passes; pass++) {
        if (!glyph_filter_convolve(&b, &a, &kernel, GLYPH_FILTER_EDGE_CLAMP, 0)) {
            glyph_bitmap_free(&a);
            glyph_bitmap_free(&b);
            return 0;
        }
        glyph_bitmap_free(&a);
        a = b;
        glyph_bitmap_init(&b);
    }
    glyph_bitmap_free(dst);
    *dst = a;
    return 1;
}

static int sobel_sample(const GlyphBitmap *src, uint32_t x, uint32_t y, int include_diagonals) {
    int gx;
    int gy;
    int mag;

    gx = -(int)sample_edge(src, (int32_t)x - 1, (int32_t)y - 1, GLYPH_FILTER_EDGE_CLAMP) +
          (int)sample_edge(src, (int32_t)x + 1, (int32_t)y - 1, GLYPH_FILTER_EDGE_CLAMP) -
          2 * (int)sample_edge(src, (int32_t)x - 1, (int32_t)y, GLYPH_FILTER_EDGE_CLAMP) +
          2 * (int)sample_edge(src, (int32_t)x + 1, (int32_t)y, GLYPH_FILTER_EDGE_CLAMP) -
          (int)sample_edge(src, (int32_t)x - 1, (int32_t)y + 1, GLYPH_FILTER_EDGE_CLAMP) +
          (int)sample_edge(src, (int32_t)x + 1, (int32_t)y + 1, GLYPH_FILTER_EDGE_CLAMP);
    gy = -(int)sample_edge(src, (int32_t)x - 1, (int32_t)y - 1, GLYPH_FILTER_EDGE_CLAMP) -
          2 * (int)sample_edge(src, (int32_t)x, (int32_t)y - 1, GLYPH_FILTER_EDGE_CLAMP) -
          (int)sample_edge(src, (int32_t)x + 1, (int32_t)y - 1, GLYPH_FILTER_EDGE_CLAMP) +
          (int)sample_edge(src, (int32_t)x - 1, (int32_t)y + 1, GLYPH_FILTER_EDGE_CLAMP) +
          2 * (int)sample_edge(src, (int32_t)x, (int32_t)y + 1, GLYPH_FILTER_EDGE_CLAMP) +
          (int)sample_edge(src, (int32_t)x + 1, (int32_t)y + 1, GLYPH_FILTER_EDGE_CLAMP);
    if (include_diagonals) {
        int gd1 = -(int)sample_edge(src, (int32_t)x - 1, (int32_t)y, GLYPH_FILTER_EDGE_CLAMP) -
                  2 * (int)sample_edge(src, (int32_t)x - 1, (int32_t)y - 1, GLYPH_FILTER_EDGE_CLAMP) -
                  (int)sample_edge(src, (int32_t)x, (int32_t)y - 1, GLYPH_FILTER_EDGE_CLAMP) +
                  (int)sample_edge(src, (int32_t)x, (int32_t)y + 1, GLYPH_FILTER_EDGE_CLAMP) +
                  2 * (int)sample_edge(src, (int32_t)x + 1, (int32_t)y + 1, GLYPH_FILTER_EDGE_CLAMP) +
                  (int)sample_edge(src, (int32_t)x + 1, (int32_t)y, GLYPH_FILTER_EDGE_CLAMP);
        int gd2 = -(int)sample_edge(src, (int32_t)x, (int32_t)y - 1, GLYPH_FILTER_EDGE_CLAMP) -
                  2 * (int)sample_edge(src, (int32_t)x + 1, (int32_t)y - 1, GLYPH_FILTER_EDGE_CLAMP) -
                  (int)sample_edge(src, (int32_t)x + 1, (int32_t)y, GLYPH_FILTER_EDGE_CLAMP) +
                  (int)sample_edge(src, (int32_t)x - 1, (int32_t)y, GLYPH_FILTER_EDGE_CLAMP) +
                  2 * (int)sample_edge(src, (int32_t)x - 1, (int32_t)y + 1, GLYPH_FILTER_EDGE_CLAMP) +
                  (int)sample_edge(src, (int32_t)x, (int32_t)y + 1, GLYPH_FILTER_EDGE_CLAMP);
        mag = abs(gx) + abs(gy) + (abs(gd1) + abs(gd2)) / 2;
        mag /= 6;
    } else {
        mag = (abs(gx) + abs(gy)) / 4;
    }
    return mag > 255 ? 255 : mag;
}

int glyph_filter_sobel(GlyphBitmap *dst, const GlyphBitmap *src, uint8_t threshold, int include_diagonals) {
    GlyphBitmap tmp;
    uint32_t x;
    uint32_t y;

    if (!dst || !bitmap_is_valid(src)) {
        return 0;
    }
    glyph_bitmap_init(&tmp);
    if (!alloc_temp(&tmp, src->width, src->height)) {
        return 0;
    }
    for (y = 0; y < src->height; y++) {
        for (x = 0; x < src->width; x++) {
            uint8_t value = (uint8_t)sobel_sample(src, x, y, include_diagonals);
            *pixel_at(&tmp, x, y) = threshold ? (value >= threshold ? 255u : 0u) : value;
        }
    }
    return assign_bitmap(dst, &tmp);
}

static int alloc_distance_buffer(const GlyphBitmap *bitmap, int32_t **out) {
    size_t count;
    size_t size;

    if (!checked_image_size(bitmap->width, bitmap->height, &count) ||
        !checked_count_size(count, sizeof(int32_t), &size)) {
        return 0;
    }
    *out = (int32_t *)malloc(size);
    return *out != NULL;
}

static void seed_distance_buffer(int32_t *dist, const GlyphBitmap *src, uint8_t threshold, int target_foreground) {
    uint32_t x;
    uint32_t y;
    size_t i = 0;

    for (y = 0; y < src->height; y++) {
        for (x = 0; x < src->width; x++, i++) {
            int foreground = *const_pixel_at(src, x, y) > threshold;
            dist[i] = foreground == target_foreground ? 0 : FILTER_DISTANCE_INF;
        }
    }
}

static void run_distance_pass(int32_t *dist, uint32_t width, uint32_t height) {
    uint32_t x;
    uint32_t y;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            size_t i = (size_t)y * width + x;
            int32_t best = dist[i];
            if (x > 0 && dist[i - 1] + 10 < best) {
                best = dist[i - 1] + 10;
            }
            if (y > 0 && dist[i - width] + 10 < best) {
                best = dist[i - width] + 10;
            }
            if (x > 0 && y > 0 && dist[i - width - 1] + 14 < best) {
                best = dist[i - width - 1] + 14;
            }
            if (x + 1 < width && y > 0 && dist[i - width + 1] + 14 < best) {
                best = dist[i - width + 1] + 14;
            }
            dist[i] = best;
        }
    }
    for (y = height; y > 0; y--) {
        for (x = width; x > 0; x--) {
            uint32_t xx = x - 1;
            uint32_t yy = y - 1;
            size_t i = (size_t)yy * width + xx;
            int32_t best = dist[i];
            if (xx + 1 < width && dist[i + 1] + 10 < best) {
                best = dist[i + 1] + 10;
            }
            if (yy + 1 < height && dist[i + width] + 10 < best) {
                best = dist[i + width] + 10;
            }
            if (xx + 1 < width && yy + 1 < height && dist[i + width + 1] + 14 < best) {
                best = dist[i + width + 1] + 14;
            }
            if (xx > 0 && yy + 1 < height && dist[i + width - 1] + 14 < best) {
                best = dist[i + width - 1] + 14;
            }
            dist[i] = best;
        }
    }
}

static uint8_t distance_to_u8(int32_t dist, uint32_t max_distance) {
    int32_t max_scaled = max_distance ? (int32_t)(max_distance * 10u) : 10;

    if (dist >= FILTER_DISTANCE_INF / 2) {
        return 0;
    }
    if (dist >= max_scaled) {
        return 255;
    }
    return (uint8_t)(((uint32_t)dist * 255u + (uint32_t)max_scaled / 2u) / (uint32_t)max_scaled);
}

int glyph_filter_distance_transform(GlyphBitmap *dst,
                                    const GlyphBitmap *src,
                                    uint8_t threshold,
                                    uint32_t max_distance,
                                    GlyphFilterDistanceMode mode) {
    GlyphBitmap tmp;
    int32_t *inside = NULL;
    int32_t *outside = NULL;
    uint32_t x;
    uint32_t y;

    if (!dst || !bitmap_is_valid(src)) {
        return 0;
    }
    glyph_bitmap_init(&tmp);
    if (!alloc_temp(&tmp, src->width, src->height)) {
        return 0;
    }
    if ((mode == GLYPH_FILTER_DISTANCE_INSIDE || mode == GLYPH_FILTER_DISTANCE_SIGNED) &&
        !alloc_distance_buffer(src, &inside)) {
        glyph_bitmap_free(&tmp);
        return 0;
    }
    if ((mode == GLYPH_FILTER_DISTANCE_OUTSIDE || mode == GLYPH_FILTER_DISTANCE_SIGNED) &&
        !alloc_distance_buffer(src, &outside)) {
        free(inside);
        glyph_bitmap_free(&tmp);
        return 0;
    }
    if (inside) {
        seed_distance_buffer(inside, src, threshold, 0);
        run_distance_pass(inside, src->width, src->height);
    }
    if (outside) {
        seed_distance_buffer(outside, src, threshold, 1);
        run_distance_pass(outside, src->width, src->height);
    }
    for (y = 0; y < src->height; y++) {
        for (x = 0; x < src->width; x++) {
            size_t i = (size_t)y * src->width + x;
            uint8_t value = 0;
            if (mode == GLYPH_FILTER_DISTANCE_INSIDE) {
                value = 255u - distance_to_u8(inside[i], max_distance);
            } else if (mode == GLYPH_FILTER_DISTANCE_OUTSIDE) {
                value = 255u - distance_to_u8(outside[i], max_distance);
            } else {
                int32_t signed_dist = outside[i] - inside[i];
                int32_t max_scaled = max_distance ? (int32_t)(max_distance * 10u) : 10;
                if (signed_dist < -max_scaled) {
                    signed_dist = -max_scaled;
                } else if (signed_dist > max_scaled) {
                    signed_dist = max_scaled;
                }
                value = (uint8_t)((signed_dist + max_scaled) * 255 / (max_scaled * 2));
            }
            *pixel_at(&tmp, x, y) = value;
        }
    }
    free(inside);
    free(outside);
    return assign_bitmap(dst, &tmp);
}

int glyph_filter_outline(GlyphBitmap *dst,
                         const GlyphBitmap *src,
                         uint32_t radius,
                         uint8_t threshold,
                         uint8_t value,
                         int outside_only) {
    GlyphBitmap tmp;
    uint32_t x;
    uint32_t y;

    if (!dst || !bitmap_is_valid(src) || radius > 255u) {
        return 0;
    }
    glyph_bitmap_init(&tmp);
    if (!alloc_temp(&tmp, src->width, src->height)) {
        return 0;
    }
    for (y = 0; y < src->height; y++) {
        for (x = 0; x < src->width; x++) {
            int center = *const_pixel_at(src, x, y) > threshold;
            int seen_ink = 0;
            int seen_clear = 0;
            uint32_t x0 = radius_min(x, radius);
            uint32_t y0 = radius_min(y, radius);
            uint32_t x1 = radius_max(x, radius, src->width);
            uint32_t y1 = radius_max(y, radius, src->height);
            uint32_t yy;
            for (yy = y0; yy <= y1; yy++) {
                uint32_t xx;
                for (xx = x0; xx <= x1; xx++) {
                    int ink;
                    int64_t dx = (int64_t)xx - x;
                    int64_t dy = (int64_t)yy - y;
                    if ((uint64_t)(dx * dx + dy * dy) > (uint64_t)radius * radius) {
                        continue;
                    }
                    ink = *const_pixel_at(src, xx, yy) > threshold;
                    seen_ink |= ink;
                    seen_clear |= !ink;
                }
            }
            if (seen_ink && seen_clear && (!outside_only || !center)) {
                *pixel_at(&tmp, x, y) = value;
            } else if (!outside_only) {
                *pixel_at(&tmp, x, y) = center ? *const_pixel_at(src, x, y) : 0;
            }
        }
    }
    return assign_bitmap(dst, &tmp);
}

void glyph_filter_curve_init(GlyphFilterCurve *curve) {
    uint32_t i;

    if (!curve) {
        return;
    }
    memset(curve, 0, sizeof(*curve));
    curve->kind = GLYPH_FILTER_CURVE_LINEAR;
    curve->gamma = 1.0;
    curve->contrast = 1.0;
    curve->input_white = 255;
    curve->output_white = 255;
    curve->midpoint = 128;
    for (i = 0; i < 256u; i++) {
        curve->table[i] = (uint8_t)i;
    }
}

int glyph_filter_curve_gamma(GlyphFilterCurve *curve, double gamma) {
    uint32_t i;

    if (!curve || gamma <= 0.0) {
        return 0;
    }
    glyph_filter_curve_init(curve);
    curve->kind = GLYPH_FILTER_CURVE_GAMMA;
    curve->gamma = gamma;
    for (i = 0; i < 256u; i++) {
        double v = (double)i / 255.0;
        curve->table[i] = clamp_u8_double(pow(v, 1.0 / gamma) * 255.0);
    }
    return 1;
}

int glyph_filter_curve_contrast(GlyphFilterCurve *curve, double contrast, uint8_t midpoint) {
    uint32_t i;

    if (!curve || contrast < -255.0 || contrast > 255.0) {
        return 0;
    }
    glyph_filter_curve_init(curve);
    curve->kind = GLYPH_FILTER_CURVE_CONTRAST;
    curve->contrast = contrast;
    curve->midpoint = midpoint;
    for (i = 0; i < 256u; i++) {
        double factor;
        double value;
        if (contrast >= 255.0) {
            value = i < midpoint ? 0.0 : 255.0;
        } else {
            factor = (259.0 * (contrast + 255.0)) / (255.0 * (259.0 - contrast));
            value = factor * ((double)i - midpoint) + midpoint;
        }
        curve->table[i] = clamp_u8_double(value);
    }
    return 1;
}

int glyph_filter_curve_levels(GlyphFilterCurve *curve,
                              uint8_t input_black,
                              uint8_t input_white,
                              uint8_t output_black,
                              uint8_t output_white,
                              double gamma) {
    uint32_t i;

    if (!curve || input_black >= input_white || gamma <= 0.0) {
        return 0;
    }
    glyph_filter_curve_init(curve);
    curve->kind = GLYPH_FILTER_CURVE_LEVELS;
    curve->input_black = input_black;
    curve->input_white = input_white;
    curve->output_black = output_black;
    curve->output_white = output_white;
    curve->gamma = gamma;
    for (i = 0; i < 256u; i++) {
        double t;
        double value;
        if (i <= input_black) {
            t = 0.0;
        } else if (i >= input_white) {
            t = 1.0;
        } else {
            t = (double)(i - input_black) / (double)(input_white - input_black);
        }
        t = pow(t, 1.0 / gamma);
        value = output_black + t * ((double)output_white - output_black);
        curve->table[i] = clamp_u8_double(value);
    }
    return 1;
}

int glyph_filter_curve_lookup(GlyphFilterCurve *curve, const uint8_t table[256]) {
    if (!curve || !table) {
        return 0;
    }
    glyph_filter_curve_init(curve);
    curve->kind = GLYPH_FILTER_CURVE_LOOKUP;
    memcpy(curve->table, table, 256);
    return 1;
}

int glyph_filter_apply_curve(GlyphBitmap *bitmap, const GlyphFilterCurve *curve) {
    uint32_t x;
    uint32_t y;

    if (!bitmap_is_valid(bitmap) || !curve) {
        return 0;
    }
    for (y = 0; y < bitmap->height; y++) {
        for (x = 0; x < bitmap->width; x++) {
            uint8_t *p = pixel_at(bitmap, x, y);
            *p = curve->table[*p];
        }
    }
    return 1;
}

int glyph_filter_apply_gamma(GlyphBitmap *bitmap, double gamma) {
    GlyphFilterCurve curve;

    if (!glyph_filter_curve_gamma(&curve, gamma)) {
        return 0;
    }
    return glyph_filter_apply_curve(bitmap, &curve);
}

int glyph_filter_apply_contrast(GlyphBitmap *bitmap, double contrast, uint8_t midpoint) {
    GlyphFilterCurve curve;

    if (!glyph_filter_curve_contrast(&curve, contrast, midpoint)) {
        return 0;
    }
    return glyph_filter_apply_curve(bitmap, &curve);
}

static int point_push(FilterPoint *stack, size_t capacity, size_t *top, uint32_t x, uint32_t y) {
    if (*top >= capacity) {
        return 0;
    }
    stack[*top].x = x;
    stack[*top].y = y;
    (*top)++;
    return 1;
}

static int value_in_range(uint8_t value, uint8_t low, uint8_t high) {
    return value >= low && value <= high;
}

static int flood_push(GlyphBitmap *bitmap,
                      uint8_t *visited,
                      FilterPoint *stack,
                      size_t capacity,
                      size_t *top,
                      uint32_t x,
                      uint32_t y,
                      uint8_t low_threshold,
                      uint8_t high_threshold) {
    size_t index;

    if (x >= bitmap->width || y >= bitmap->height) {
        return 1;
    }
    index = (size_t)y * bitmap->width + x;
    if (visited[index]) {
        return 1;
    }
    if (!value_in_range(*const_pixel_at(bitmap, x, y), low_threshold, high_threshold)) {
        return 1;
    }
    if (!point_push(stack, capacity, top, x, y)) {
        return 0;
    }
    visited[index] = 1;
    return 1;
}

uint32_t glyph_filter_flood_fill(GlyphBitmap *bitmap,
                                 uint32_t x,
                                 uint32_t y,
                                 uint8_t low_threshold,
                                 uint8_t high_threshold,
                                 uint8_t fill_value,
                                 GlyphFilterConnectivity connectivity,
                                 GlyphBitmapRect *bounds) {
    FilterPoint *stack;
    uint8_t *visited;
    size_t capacity;
    size_t top = 0;
    uint32_t filled = 0;
    uint8_t target;

    rect_clear(bounds);
    if (!bitmap_is_valid(bitmap) || x >= bitmap->width || y >= bitmap->height || low_threshold > high_threshold ||
        (connectivity != GLYPH_FILTER_CONNECT_4 && connectivity != GLYPH_FILTER_CONNECT_8)) {
        return 0;
    }
    target = *const_pixel_at(bitmap, x, y);
    if (!value_in_range(target, low_threshold, high_threshold) || target == fill_value) {
        return 0;
    }
    if (!checked_image_size(bitmap->width, bitmap->height, &capacity)) {
        return 0;
    }
    {
        size_t stack_size = 0;
        if (!checked_count_size(capacity, sizeof(*stack), &stack_size)) {
            return 0;
        }
        stack = (FilterPoint *)malloc(stack_size);
    }
    if (!stack) {
        return 0;
    }
    visited = (uint8_t *)calloc(capacity, 1);
    if (!visited) {
        free(stack);
        return 0;
    }
    if (!flood_push(bitmap, visited, stack, capacity, &top, x, y, low_threshold, high_threshold)) {
        free(visited);
        free(stack);
        return 0;
    }
    while (top) {
        FilterPoint p = stack[--top];
        uint8_t *sample;
        sample = pixel_at(bitmap, p.x, p.y);
        *sample = fill_value;
        filled++;
        rect_include_point(bounds, p.x, p.y);
        if (p.x > 0 &&
            !flood_push(bitmap, visited, stack, capacity, &top, p.x - 1, p.y, low_threshold, high_threshold)) {
            break;
        }
        if (p.x + 1 < bitmap->width &&
            !flood_push(bitmap, visited, stack, capacity, &top, p.x + 1, p.y, low_threshold, high_threshold)) {
            break;
        }
        if (p.y > 0 &&
            !flood_push(bitmap, visited, stack, capacity, &top, p.x, p.y - 1, low_threshold, high_threshold)) {
            break;
        }
        if (p.y + 1 < bitmap->height &&
            !flood_push(bitmap, visited, stack, capacity, &top, p.x, p.y + 1, low_threshold, high_threshold)) {
            break;
        }
        if (connectivity == GLYPH_FILTER_CONNECT_8) {
            if (p.x > 0 && p.y > 0 &&
                !flood_push(bitmap, visited, stack, capacity, &top, p.x - 1, p.y - 1, low_threshold, high_threshold)) {
                break;
            }
            if (p.x + 1 < bitmap->width && p.y > 0 &&
                !flood_push(bitmap, visited, stack, capacity, &top, p.x + 1, p.y - 1, low_threshold, high_threshold)) {
                break;
            }
            if (p.x > 0 && p.y + 1 < bitmap->height &&
                !flood_push(bitmap, visited, stack, capacity, &top, p.x - 1, p.y + 1, low_threshold, high_threshold)) {
                break;
            }
            if (p.x + 1 < bitmap->width && p.y + 1 < bitmap->height &&
                !flood_push(bitmap, visited, stack, capacity, &top, p.x + 1, p.y + 1, low_threshold, high_threshold)) {
                break;
            }
        }
    }
    free(visited);
    free(stack);
    return filled;
}

void glyph_filter_component_map_init(GlyphFilterComponentMap *map) {
    if (map) {
        memset(map, 0, sizeof(*map));
    }
}

void glyph_filter_component_map_free(GlyphFilterComponentMap *map) {
    if (!map) {
        return;
    }
    free(map->labels);
    free(map->components);
    glyph_filter_component_map_init(map);
}

static int component_map_alloc(GlyphFilterComponentMap *map, uint32_t width, uint32_t height, uint32_t capacity) {
    size_t pixel_count;
    size_t labels_size;
    size_t components_size;

    if (!checked_image_size(width, height, &pixel_count) ||
        pixel_count > UINT32_MAX ||
        !checked_count_size(pixel_count, sizeof(uint32_t), &labels_size) ||
        !checked_count_size(capacity + 1u, sizeof(GlyphFilterComponent), &components_size)) {
        return 0;
    }
    map->labels = (uint32_t *)calloc(1, labels_size);
    map->components = (GlyphFilterComponent *)calloc(1, components_size);
    if (!map->labels || !map->components) {
        glyph_filter_component_map_free(map);
        return 0;
    }
    map->width = width;
    map->height = height;
    map->capacity = capacity;
    return 1;
}

static int component_map_grow(GlyphFilterComponentMap *map) {
    uint32_t new_capacity = map->capacity ? map->capacity * 2u : 16u;
    size_t size;
    GlyphFilterComponent *items;

    if (new_capacity < map->capacity || !checked_count_size(new_capacity + 1u, sizeof(*items), &size)) {
        return 0;
    }
    items = (GlyphFilterComponent *)realloc(map->components, size);
    if (!items) {
        return 0;
    }
    memset(items + map->capacity + 1u, 0, (new_capacity - map->capacity) * sizeof(*items));
    map->components = items;
    map->capacity = new_capacity;
    return 1;
}

static uint32_t find_root(uint32_t *parents, uint32_t label) {
    uint32_t root = label;

    while (parents[root] != root) {
        root = parents[root];
    }
    while (parents[label] != label) {
        uint32_t next = parents[label];
        parents[label] = root;
        label = next;
    }
    return root;
}

static void union_labels(uint32_t *parents, uint32_t a, uint32_t b) {
    uint32_t ra = find_root(parents, a);
    uint32_t rb = find_root(parents, b);

    if (ra == rb) {
        return;
    }
    if (ra < rb) {
        parents[rb] = ra;
    } else {
        parents[ra] = rb;
    }
}

static int add_neighbor_label(uint32_t *neighbors, uint32_t *count, uint32_t label) {
    uint32_t i;

    if (!label) {
        return 1;
    }
    for (i = 0; i < *count; i++) {
        if (neighbors[i] == label) {
            return 1;
        }
    }
    neighbors[*count] = label;
    (*count)++;
    return 1;
}

static int remap_labels(GlyphFilterComponentMap *map, uint32_t *parents) {
    uint32_t *remap;
    uint32_t next = 0;
    size_t size;
    uint32_t i;

    if (!checked_count_size(map->count + 1u, sizeof(uint32_t), &size)) {
        return 0;
    }
    remap = (uint32_t *)calloc(1, size);
    if (!remap) {
        return 0;
    }
    for (i = 1; i <= map->count; i++) {
        uint32_t root = find_root(parents, i);
        if (!remap[root]) {
            next++;
            remap[root] = next;
            memset(&map->components[next], 0, sizeof(map->components[next]));
            map->components[next].label = next;
            map->components[next].min_value = 255;
        }
        remap[i] = remap[root];
    }
    for (i = 0; i < map->width * map->height; i++) {
        uint32_t label = map->labels[i];
        map->labels[i] = label ? remap[label] : 0;
    }
    map->count = next;
    free(remap);
    return 1;
}

static void accumulate_components(const GlyphBitmap *bitmap, GlyphFilterComponentMap *map) {
    uint32_t x;
    uint32_t y;

    for (y = 0; y < bitmap->height; y++) {
        for (x = 0; x < bitmap->width; x++) {
            uint32_t label = map->labels[(size_t)y * map->width + x];
            if (label) {
                GlyphFilterComponent *component = &map->components[label];
                uint8_t value = *const_pixel_at(bitmap, x, y);
                component->area++;
                component->sum_x += x;
                component->sum_y += y;
                if (value < component->min_value) {
                    component->min_value = value;
                }
                if (value > component->max_value) {
                    component->max_value = value;
                }
                rect_include_point(&component->bounds, x, y);
            }
        }
    }
}

int glyph_filter_label_components(const GlyphBitmap *bitmap,
                                  uint8_t threshold,
                                  GlyphFilterConnectivity connectivity,
                                  GlyphFilterComponentMap *out) {
    GlyphFilterComponentMap map;
    uint32_t *parents = NULL;
    size_t parent_size;
    uint32_t label_count = 0;
    uint32_t x;
    uint32_t y;

    if (!bitmap_is_valid(bitmap) || !out ||
        (connectivity != GLYPH_FILTER_CONNECT_4 && connectivity != GLYPH_FILTER_CONNECT_8)) {
        return 0;
    }
    glyph_filter_component_map_init(&map);
    if (!component_map_alloc(&map, bitmap->width, bitmap->height, 16u)) {
        return 0;
    }
    if (!checked_count_size(map.capacity + 1u, sizeof(uint32_t), &parent_size)) {
        glyph_filter_component_map_free(&map);
        return 0;
    }
    parents = (uint32_t *)calloc(1, parent_size);
    if (!parents) {
        glyph_filter_component_map_free(&map);
        return 0;
    }
    for (y = 0; y < bitmap->height; y++) {
        for (x = 0; x < bitmap->width; x++) {
            uint32_t neighbors[4];
            uint32_t neighbor_count = 0;
            uint32_t chosen;
            size_t index = (size_t)y * bitmap->width + x;
            uint32_t i;
            if (*const_pixel_at(bitmap, x, y) <= threshold) {
                continue;
            }
            if (x > 0) {
                add_neighbor_label(neighbors, &neighbor_count, map.labels[index - 1]);
            }
            if (y > 0) {
                add_neighbor_label(neighbors, &neighbor_count, map.labels[index - bitmap->width]);
            }
            if (connectivity == GLYPH_FILTER_CONNECT_8) {
                if (x > 0 && y > 0) {
                    add_neighbor_label(neighbors, &neighbor_count, map.labels[index - bitmap->width - 1]);
                }
                if (x + 1 < bitmap->width && y > 0) {
                    add_neighbor_label(neighbors, &neighbor_count, map.labels[index - bitmap->width + 1]);
                }
            }
            if (!neighbor_count) {
                if (label_count + 1u > map.capacity) {
                    uint32_t old_capacity = map.capacity;
                    uint32_t *new_parents;
                    if (!component_map_grow(&map) ||
                        !checked_count_size(map.capacity + 1u, sizeof(uint32_t), &parent_size)) {
                        free(parents);
                        glyph_filter_component_map_free(&map);
                        return 0;
                    }
                    new_parents = (uint32_t *)realloc(parents, parent_size);
                    if (!new_parents) {
                        free(parents);
                        glyph_filter_component_map_free(&map);
                        return 0;
                    }
                    parents = new_parents;
                    memset(parents + old_capacity + 1u, 0, (map.capacity - old_capacity) * sizeof(uint32_t));
                }
                label_count++;
                parents[label_count] = label_count;
                chosen = label_count;
            } else {
                chosen = neighbors[0];
                for (i = 1; i < neighbor_count; i++) {
                    union_labels(parents, chosen, neighbors[i]);
                }
            }
            map.labels[index] = chosen;
        }
    }
    map.count = label_count;
    if (!remap_labels(&map, parents)) {
        free(parents);
        glyph_filter_component_map_free(&map);
        return 0;
    }
    accumulate_components(bitmap, &map);
    free(parents);
    glyph_filter_component_map_free(out);
    *out = map;
    return 1;
}

const GlyphFilterComponent *glyph_filter_largest_component(const GlyphFilterComponentMap *map) {
    const GlyphFilterComponent *best = NULL;
    uint32_t i;

    if (!map || !map->components) {
        return NULL;
    }
    for (i = 1; i <= map->count; i++) {
        const GlyphFilterComponent *component = &map->components[i];
        if (!best || component->area > best->area) {
            best = component;
        }
    }
    return best;
}

int glyph_filter_components_bounds(const GlyphFilterComponentMap *map,
                                   uint32_t min_area,
                                   uint32_t padding,
                                   GlyphBitmapRect *bounds) {
    uint32_t i;

    if (!map || !bounds || !map->components) {
        return 0;
    }
    rect_clear(bounds);
    for (i = 1; i <= map->count; i++) {
        const GlyphFilterComponent *component = &map->components[i];
        if (component->area >= min_area) {
            rect_include_rect(bounds, &component->bounds);
        }
    }
    if (!bounds->width || !bounds->height) {
        return 0;
    }
    rect_pad_clamped(bounds, padding, map->width, map->height);
    return bounds->width && bounds->height;
}

int glyph_filter_crop_to_components(GlyphBitmap *dst,
                                    const GlyphBitmap *src,
                                    uint8_t threshold,
                                    uint32_t padding,
                                    uint32_t min_area,
                                    GlyphFilterConnectivity connectivity,
                                    GlyphBitmapRect *bounds) {
    GlyphFilterComponentMap map;
    GlyphBitmapRect rect;
    int ok;

    if (!dst || !bitmap_is_valid(src)) {
        return 0;
    }
    glyph_filter_component_map_init(&map);
    if (!glyph_filter_label_components(src, threshold, connectivity, &map)) {
        return 0;
    }
    ok = glyph_filter_components_bounds(&map, min_area, padding, &rect);
    glyph_filter_component_map_free(&map);
    if (!ok) {
        rect_clear(bounds);
        return 0;
    }
    if (bounds) {
        *bounds = rect;
    }
    return glyph_bitmap_crop(dst, src, rect.x, rect.y, rect.width, rect.height);
}

void glyph_filter_pipeline_init(GlyphFilterPipeline *pipeline) {
    if (pipeline) {
        memset(pipeline, 0, sizeof(*pipeline));
    }
}

int glyph_filter_pipeline_add(GlyphFilterPipeline *pipeline, const GlyphFilterStep *step) {
    if (!pipeline || !step || pipeline->count >= GLYPH_FILTER_PIPELINE_MAX_STEPS) {
        return 0;
    }
    pipeline->steps[pipeline->count++] = *step;
    return 1;
}

static int apply_pipeline_step(GlyphBitmap *bitmap, const GlyphFilterStep *step) {
    GlyphBitmap tmp;
    int ok = 0;

    glyph_bitmap_init(&tmp);
    switch (step->kind) {
    case GLYPH_FILTER_STEP_CONVOLVE:
        ok = glyph_filter_convolve(&tmp,
                                   bitmap,
                                   &step->params.convolve.kernel,
                                   step->params.convolve.edge_mode,
                                   step->params.convolve.flags);
        break;
    case GLYPH_FILTER_STEP_SHARPEN:
        ok = glyph_filter_sharpen(&tmp, bitmap, step->params.sharpen.amount);
        break;
    case GLYPH_FILTER_STEP_EMBOSS:
        ok = glyph_filter_emboss(&tmp, bitmap, step->params.emboss.depth, step->params.emboss.midpoint);
        break;
    case GLYPH_FILTER_STEP_GAUSSIAN_BLUR:
        ok = glyph_filter_gaussian_blur(&tmp,
                                        bitmap,
                                        step->params.gaussian_blur.radius,
                                        step->params.gaussian_blur.passes);
        break;
    case GLYPH_FILTER_STEP_SOBEL:
        ok = glyph_filter_sobel(&tmp, bitmap, step->params.sobel.threshold, step->params.sobel.include_diagonals);
        break;
    case GLYPH_FILTER_STEP_DISTANCE:
        ok = glyph_filter_distance_transform(&tmp,
                                             bitmap,
                                             step->params.distance.threshold,
                                             step->params.distance.max_distance,
                                             step->params.distance.mode);
        break;
    case GLYPH_FILTER_STEP_OUTLINE:
        ok = glyph_filter_outline(&tmp,
                                  bitmap,
                                  step->params.outline.radius,
                                  step->params.outline.threshold,
                                  step->params.outline.value,
                                  step->params.outline.outside_only);
        break;
    case GLYPH_FILTER_STEP_CURVE:
        ok = glyph_filter_apply_curve(bitmap, &step->params.curve.curve);
        return ok;
    case GLYPH_FILTER_STEP_THRESHOLD:
        glyph_bitmap_threshold(bitmap,
                               step->params.threshold.threshold,
                               step->params.threshold.low_value,
                               step->params.threshold.high_value);
        return 1;
    case GLYPH_FILTER_STEP_INVERT:
        glyph_bitmap_invert(bitmap);
        return 1;
    case GLYPH_FILTER_STEP_NORMALIZE:
        glyph_bitmap_normalize(bitmap);
        return 1;
    case GLYPH_FILTER_STEP_DILATE:
        ok = glyph_bitmap_dilate(&tmp, bitmap, step->params.morphology.radius);
        break;
    case GLYPH_FILTER_STEP_ERODE:
        ok = glyph_bitmap_erode(&tmp, bitmap, step->params.morphology.radius);
        break;
    case GLYPH_FILTER_STEP_BOX_BLUR:
        ok = glyph_bitmap_box_blur(&tmp, bitmap, step->params.morphology.radius);
        break;
    case GLYPH_FILTER_STEP_CROP_COMPONENTS:
        ok = glyph_filter_crop_to_components(&tmp,
                                             bitmap,
                                             step->params.crop_components.threshold,
                                             step->params.crop_components.padding,
                                             step->params.crop_components.min_area,
                                             step->params.crop_components.connectivity,
                                             NULL);
        break;
    default:
        return 0;
    }
    if (!ok) {
        glyph_bitmap_free(&tmp);
        return 0;
    }
    glyph_bitmap_free(bitmap);
    *bitmap = tmp;
    return 1;
}

int glyph_filter_pipeline_apply(GlyphBitmap *dst, const GlyphBitmap *src, const GlyphFilterPipeline *pipeline) {
    GlyphBitmap work;
    uint32_t i;

    if (!dst || !bitmap_is_valid(src) || !pipeline || pipeline->count > GLYPH_FILTER_PIPELINE_MAX_STEPS) {
        return 0;
    }
    glyph_bitmap_init(&work);
    if (!glyph_bitmap_copy(&work, src)) {
        return 0;
    }
    for (i = 0; i < pipeline->count; i++) {
        if (!apply_pipeline_step(&work, &pipeline->steps[i])) {
            glyph_bitmap_free(&work);
            return 0;
        }
    }
    return assign_bitmap(dst, &work);
}
