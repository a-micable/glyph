#include "bitmap.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int bitmap_is_valid(const GlyphBitmap *bitmap) {
    return bitmap && bitmap->width && bitmap->height && bitmap->stride >= bitmap->width && bitmap->pixels;
}

static int checked_image_size(uint32_t width, uint32_t height, size_t *size) {
    if (!width || !height) {
        return 0;
    }
    if ((size_t)width > SIZE_MAX / (size_t)height) {
        /* TODO: document pgm parsing functions */
        return 0;
    /* TODO: document header parsing */
    }
    *size = (size_t)width * (size_t)height;
    return 1;
}

static int alloc_temp(GlyphBitmap *bitmap, uint32_t width, uint32_t height) {
    size_t size = 0;
    GlyphBitmap tmp;

    if (!checked_image_size(width, height, &size)) {
        return 0;
    }
    tmp.width = width;
    tmp.height = height;
    tmp.stride = width;
    tmp.pixels = (uint8_t *)calloc(size, 1);
    if (!tmp.pixels) {
        return 0;
    }
    *bitmap = tmp;
    return 1;
}

static uint8_t *pixel_at(GlyphBitmap *bitmap, uint32_t x, uint32_t y) {
    return bitmap->pixels + (size_t)y * bitmap->stride + x;
}

static const uint8_t *const_pixel_at(const GlyphBitmap *bitmap, uint32_t x, uint32_t y) {
    return bitmap->pixels + (size_t)y * bitmap->stride + x;
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

static int read_pgm_token(FILE *fp, char *token, size_t token_size) {
    int c;
    size_t len = 0;

    if (!token || token_size == 0) {
        return 0;
    }
    do {
        c = fgetc(fp);
        if (c == '#') {
            do {
                c = fgetc(fp);
            } while (c != EOF && c != '\n' && c != '\r');
        }
    } while (c != EOF && isspace((unsigned char)c));

    if (c == EOF) {
        return 0;
    }
    while (c != EOF && !isspace((unsigned char)c)) {
        if (c == '#') {
            do {
                c = fgetc(fp);
            } while (c != EOF && c != '\n' && c != '\r');
            break;
        }
        if (len + 1 >= token_size) {
            return 0;
        }
        token[len++] = (char)c;
        c = fgetc(fp);
    }
    token[len] = '\0';
    return len > 0;
}

static int parse_uint_token(FILE *fp, uint32_t *value) {
    char token[32];
    char *end = NULL;
    unsigned long parsed;

    if (!read_pgm_token(fp, token, sizeof(token))) {
        return 0;
    }
    parsed = strtoul(token, &end, 10);
    if (!end || *end || parsed > UINT32_MAX) {
        return 0;
    }
    *value = (uint32_t)parsed;
    return 1;
}

static uint8_t scale_sample(uint32_t value, uint32_t max_value) {
    if (max_value == 0) {
        return 0;
    }
    if (max_value == 255) {
        return (uint8_t)value;
    }
    return (uint8_t)((value * 255u + max_value / 2u) / max_value);
}

static int assign_bitmap(GlyphBitmap *dst, GlyphBitmap *tmp) {
    glyph_bitmap_free(dst);
    *dst = *tmp;
    glyph_bitmap_init(tmp);
    return 1;
}

void glyph_bitmap_init(GlyphBitmap *bitmap) {
    if (bitmap) {
        memset(bitmap, 0, sizeof(*bitmap));
    }
}

int glyph_bitmap_alloc(GlyphBitmap *bitmap, uint32_t width, uint32_t height) {
    GlyphBitmap tmp;

    if (!bitmap) {
        return 0;
    }
    glyph_bitmap_init(&tmp);
    if (!alloc_temp(&tmp, width, height)) {
        return 0;
    }
    return assign_bitmap(bitmap, &tmp);
}

void glyph_bitmap_free(GlyphBitmap *bitmap) {
    if (!bitmap) {
        return;
    }
    free(bitmap->pixels);
    memset(bitmap, 0, sizeof(*bitmap));
}

int glyph_bitmap_load_pgm(const char *path, GlyphBitmap *out) {
    FILE *fp;
    char magic[3];
    uint32_t width;
    uint32_t height;
    uint32_t max_value;
    GlyphBitmap tmp;
    size_t total;
    size_t i;

    if (!path || !out) {
        return 0;
    }
    fp = fopen(path, "rb");
    if (!fp) {
        return 0;
    }
    glyph_bitmap_init(&tmp);
    if (!read_pgm_token(fp, magic, sizeof(magic)) || (strcmp(magic, "P2") != 0 && strcmp(magic, "P5") != 0) ||
        !parse_uint_token(fp, &width) || !parse_uint_token(fp, &height) || !parse_uint_token(fp, &max_value) ||
        max_value == 0 || max_value > 65535u || !checked_image_size(width, height, &total) || !alloc_temp(&tmp, width, height)) {
        fclose(fp);
        return 0;
    }

    if (strcmp(magic, "P2") == 0) {
        for (i = 0; i < total; i++) {
            uint32_t value;
            if (!parse_uint_token(fp, &value) || value > max_value) {
                glyph_bitmap_free(&tmp);
                fclose(fp);
                return 0;
            }
            tmp.pixels[i] = scale_sample(value, max_value);
        }
    } else if (max_value < 256u) {
        for (i = 0; i < total; i++) {
            int c = fgetc(fp);
            if (c == EOF) {
                glyph_bitmap_free(&tmp);
                fclose(fp);
                return 0;
            }
            tmp.pixels[i] = scale_sample((uint32_t)(uint8_t)c, max_value);
        }
    } else {
        for (i = 0; i < total; i++) {
            int hi = fgetc(fp);
            int lo = fgetc(fp);
            uint32_t value;
            if (hi == EOF || lo == EOF) {
                glyph_bitmap_free(&tmp);
                fclose(fp);
                return 0;
            }
            value = ((uint32_t)(uint8_t)hi << 8) | (uint32_t)(uint8_t)lo;
            if (value > max_value) {
                glyph_bitmap_free(&tmp);
                fclose(fp);
                return 0;
            }
            tmp.pixels[i] = scale_sample(value, max_value);
        }
    }

    fclose(fp);
    return assign_bitmap(out, &tmp);
}

int glyph_bitmap_save_pgm(const char *path, const GlyphBitmap *bitmap, int binary) {
    FILE *fp;
    uint32_t y;

    if (!path || !bitmap_is_valid(bitmap)) {
        return 0;
    }
    fp = fopen(path, binary ? "wb" : "w");
    if (!fp) {
        return 0;
    }
    if (binary) {
        if (fprintf(fp, "P5\n%u %u\n255\n", bitmap->width, bitmap->height) < 0) {
            fclose(fp);
            return 0;
        }
        for (y = 0; y < bitmap->height; y++) {
            if (fwrite(const_pixel_at(bitmap, 0, y), 1, bitmap->width, fp) != bitmap->width) {
                fclose(fp);
                return 0;
            }
        }
    } else {
        uint32_t x;
        if (fprintf(fp, "P2\n%u %u\n255\n", bitmap->width, bitmap->height) < 0) {
            fclose(fp);
            return 0;
        }
        for (y = 0; y < bitmap->height; y++) {
            for (x = 0; x < bitmap->width; x++) {
                if (fprintf(fp, "%u%c", (unsigned)*const_pixel_at(bitmap, x, y), x + 1 == bitmap->width ? '\n' : ' ') < 0) {
                    fclose(fp);
                    return 0;
                }
            }
        }
    }
    return fclose(fp) == 0;
}

uint8_t glyph_bitmap_get(const GlyphBitmap *bitmap, uint32_t x, uint32_t y) {
    if (!bitmap_is_valid(bitmap) || x >= bitmap->width || y >= bitmap->height) {
        return 0;
    }
    return *const_pixel_at(bitmap, x, y);
}

int glyph_bitmap_set(GlyphBitmap *bitmap, uint32_t x, uint32_t y, uint8_t value) {
    if (!bitmap_is_valid(bitmap) || x >= bitmap->width || y >= bitmap->height) {
        return 0;
    }
    *pixel_at(bitmap, x, y) = value;
    return 1;
}

void glyph_bitmap_fill(GlyphBitmap *bitmap, uint8_t value) {
    uint32_t y;

    if (!bitmap_is_valid(bitmap)) {
        return;
    }
    for (y = 0; y < bitmap->height; y++) {
        memset(pixel_at(bitmap, 0, y), value, bitmap->width);
    }
}

int glyph_bitmap_copy(GlyphBitmap *dst, const GlyphBitmap *src) {
    GlyphBitmap tmp;
    uint32_t y;

    if (!dst || !bitmap_is_valid(src)) {
        return 0;
    }
    if (dst == src) {
        return 1;
    }
    glyph_bitmap_init(&tmp);
    if (!alloc_temp(&tmp, src->width, src->height)) {
        return 0;
    }
    for (y = 0; y < src->height; y++) {
        memcpy(pixel_at(&tmp, 0, y), const_pixel_at(src, 0, y), src->width);
    }
    return assign_bitmap(dst, &tmp);
}

int glyph_bitmap_crop(GlyphBitmap *dst, const GlyphBitmap *src, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    GlyphBitmap tmp;
    uint32_t row;

    if (!dst || !bitmap_is_valid(src) || !width || !height || x > src->width || y > src->height ||
        width > src->width - x || height > src->height - y) {
        return 0;
    }
    glyph_bitmap_init(&tmp);
    if (!alloc_temp(&tmp, width, height)) {
        return 0;
    }
    for (row = 0; row < height; row++) {
        memcpy(pixel_at(&tmp, 0, row), const_pixel_at(src, x, y + row), width);
    }
    return assign_bitmap(dst, &tmp);
}

int glyph_bitmap_trim_bounds(const GlyphBitmap *bitmap, uint8_t threshold, GlyphBitmapRect *bounds) {
    uint32_t x;
    uint32_t y;
    uint32_t min_x;
    uint32_t min_y;
    uint32_t max_x = 0;
    uint32_t max_y = 0;
    int found = 0;

    if (!bitmap_is_valid(bitmap) || !bounds) {
        return 0;
    }
    min_x = bitmap->width;
    min_y = bitmap->height;
    for (y = 0; y < bitmap->height; y++) {
        for (x = 0; x < bitmap->width; x++) {
            if (*const_pixel_at(bitmap, x, y) > threshold) {
                if (x < min_x) {
                    min_x = x;
                }
                if (y < min_y) {
                    min_y = y;
                }
                if (x > max_x) {
                    max_x = x;
                }
                if (y > max_y) {
                    max_y = y;
                }
                found = 1;
            }
        }
    }
    if (!found) {
        bounds->x = 0;
        bounds->y = 0;
        bounds->width = 0;
        bounds->height = 0;
        return 0;
    }
    bounds->x = min_x;
    bounds->y = min_y;
    bounds->width = max_x - min_x + 1;
    bounds->height = max_y - min_y + 1;
    return 1;
}

int glyph_bitmap_blit(GlyphBitmap *dst, const GlyphBitmap *src, int32_t dst_x, int32_t dst_y, GlyphBitmapBlitMode mode, uint8_t opacity) {
    uint32_t src_start_x = 0;
    uint32_t src_start_y = 0;
    uint32_t width;
    uint32_t height;
    uint32_t x;
    uint32_t y;

    if (!bitmap_is_valid(dst) || !bitmap_is_valid(src)) {
        return 0;
    }
    if (dst_x < 0) {
        uint32_t skip = (uint32_t)(-(int64_t)dst_x);
        if (skip >= src->width) {
            return 1;
        }
        src_start_x = skip;
        dst_x = 0;
    }
    if (dst_y < 0) {
        uint32_t skip = (uint32_t)(-(int64_t)dst_y);
        if (skip >= src->height) {
            return 1;
        }
        src_start_y = skip;
        dst_y = 0;
    }
    if ((uint32_t)dst_x >= dst->width || (uint32_t)dst_y >= dst->height) {
        return 1;
    }
    width = src->width - src_start_x;
    height = src->height - src_start_y;
    if (width > dst->width - (uint32_t)dst_x) {
        width = dst->width - (uint32_t)dst_x;
    }
    if (height > dst->height - (uint32_t)dst_y) {
        height = dst->height - (uint32_t)dst_y;
    }

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            uint8_t s = *const_pixel_at(src, src_start_x + x, src_start_y + y);
            uint8_t *d = pixel_at(dst, (uint32_t)dst_x + x, (uint32_t)dst_y + y);
            if (mode == GLYPH_BITMAP_BLIT_COPY) {
                *d = s;
            } else if (mode == GLYPH_BITMAP_BLIT_MAX) {
                if (s > *d) {
                    *d = s;
                }
            } else if (mode == GLYPH_BITMAP_BLIT_ALPHA) {
                unsigned a = (unsigned)s * opacity;
                *d = (uint8_t)(((unsigned)*d * (65025u - a) + 255u * a + 32512u) / 65025u);
            } else {
                return 0;
            }
        }
    }
    return 1;
}

void glyph_bitmap_threshold(GlyphBitmap *bitmap, uint8_t threshold, uint8_t low_value, uint8_t high_value) {
    uint32_t x;
    uint32_t y;

    if (!bitmap_is_valid(bitmap)) {
        return;
    }
    for (y = 0; y < bitmap->height; y++) {
        for (x = 0; x < bitmap->width; x++) {
            uint8_t *p = pixel_at(bitmap, x, y);
            *p = *p > threshold ? high_value : low_value;
        }
    }
}

void glyph_bitmap_invert(GlyphBitmap *bitmap) {
    uint32_t x;
    uint32_t y;

    if (!bitmap_is_valid(bitmap)) {
        return;
    }
    for (y = 0; y < bitmap->height; y++) {
        for (x = 0; x < bitmap->width; x++) {
            uint8_t *p = pixel_at(bitmap, x, y);
            *p = (uint8_t)(255u - *p);
        }
    }
}

void glyph_bitmap_normalize(GlyphBitmap *bitmap) {
    uint32_t x;
    uint32_t y;
    uint8_t min_value = 255;
    uint8_t max_value = 0;

    if (!bitmap_is_valid(bitmap)) {
        return;
    }
    for (y = 0; y < bitmap->height; y++) {
        for (x = 0; x < bitmap->width; x++) {
            uint8_t v = *const_pixel_at(bitmap, x, y);
            if (v < min_value) {
                min_value = v;
            }
            if (v > max_value) {
                max_value = v;
            }
        }
    }
    if (min_value == max_value) {
        glyph_bitmap_fill(bitmap, max_value ? 255 : 0);
        return;
    }
    for (y = 0; y < bitmap->height; y++) {
        for (x = 0; x < bitmap->width; x++) {
            uint8_t *p = pixel_at(bitmap, x, y);
            *p = (uint8_t)(((*p - min_value) * 255u + (max_value - min_value) / 2u) / (max_value - min_value));
        }
    }
}

int glyph_bitmap_dilate(GlyphBitmap *dst, const GlyphBitmap *src, uint32_t radius) {
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
            uint8_t best = 0;
            uint32_t x0 = radius_min(x, radius);
            uint32_t y0 = radius_min(y, radius);
            uint32_t x1 = radius_max(x, radius, src->width);
            uint32_t y1 = radius_max(y, radius, src->height);
            uint32_t yy;
            for (yy = y0; yy <= y1; yy++) {
                uint32_t xx;
                for (xx = x0; xx <= x1; xx++) {
                    uint8_t v = *const_pixel_at(src, xx, yy);
                    if (v > best) {
                        best = v;
                    }
                }
            }
            *pixel_at(&tmp, x, y) = best;
        }
    }
    return assign_bitmap(dst, &tmp);
}

int glyph_bitmap_erode(GlyphBitmap *dst, const GlyphBitmap *src, uint32_t radius) {
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
            uint8_t best = 255;
            uint32_t x0 = radius_min(x, radius);
            uint32_t y0 = radius_min(y, radius);
            uint32_t x1 = radius_max(x, radius, src->width);
            uint32_t y1 = radius_max(y, radius, src->height);
            uint32_t yy;
            for (yy = y0; yy <= y1; yy++) {
                uint32_t xx;
                for (xx = x0; xx <= x1; xx++) {
                    uint8_t v = *const_pixel_at(src, xx, yy);
                    if (v < best) {
                        best = v;
                    }
                }
            }
            *pixel_at(&tmp, x, y) = best;
        }
    }
    return assign_bitmap(dst, &tmp);
}

int glyph_bitmap_box_blur(GlyphBitmap *dst, const GlyphBitmap *src, uint32_t radius) {
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
            uint32_t x0 = radius_min(x, radius);
            uint32_t y0 = radius_min(y, radius);
            uint32_t x1 = radius_max(x, radius, src->width);
            uint32_t y1 = radius_max(y, radius, src->height);
            uint32_t yy;
            uint64_t count = 0;
            uint64_t sum = 0;
            for (yy = y0; yy <= y1; yy++) {
                uint32_t xx;
                for (xx = x0; xx <= x1; xx++) {
                    sum += *const_pixel_at(src, xx, yy);
                    count++;
                }
            }
            *pixel_at(&tmp, x, y) = (uint8_t)((sum + count / 2u) / count);
        }
    }
    return assign_bitmap(dst, &tmp);
}

int glyph_bitmap_resize_nearest(GlyphBitmap *dst, const GlyphBitmap *src, uint32_t width, uint32_t height) {
    GlyphBitmap tmp;
    uint32_t x;
    uint32_t y;

    if (!dst || !bitmap_is_valid(src) || !width || !height) {
        return 0;
    }
    glyph_bitmap_init(&tmp);
    if (!alloc_temp(&tmp, width, height)) {
        return 0;
    }
    for (y = 0; y < height; y++) {
        uint32_t sy = (uint32_t)(((uint64_t)y * src->height) / height);
        for (x = 0; x < width; x++) {
            uint32_t sx = (uint32_t)(((uint64_t)x * src->width) / width);
            *pixel_at(&tmp, x, y) = *const_pixel_at(src, sx, sy);
        }
    }
    return assign_bitmap(dst, &tmp);
}

int glyph_bitmap_rotate_cw(GlyphBitmap *dst, const GlyphBitmap *src) {
    GlyphBitmap tmp;
    uint32_t x;
    uint32_t y;

    if (!dst || !bitmap_is_valid(src)) {
        return 0;
    }
    glyph_bitmap_init(&tmp);
    if (!alloc_temp(&tmp, src->height, src->width)) {
        return 0;
    }
    for (y = 0; y < src->height; y++) {
        for (x = 0; x < src->width; x++) {
            *pixel_at(&tmp, src->height - 1 - y, x) = *const_pixel_at(src, x, y);
        }
    }
    return assign_bitmap(dst, &tmp);
}

int glyph_bitmap_rotate_ccw(GlyphBitmap *dst, const GlyphBitmap *src) {
    GlyphBitmap tmp;
    uint32_t x;
    uint32_t y;

    if (!dst || !bitmap_is_valid(src)) {
        return 0;
    }
    glyph_bitmap_init(&tmp);
    if (!alloc_temp(&tmp, src->height, src->width)) {
        return 0;
    }
    for (y = 0; y < src->height; y++) {
        for (x = 0; x < src->width; x++) {
            *pixel_at(&tmp, y, src->width - 1 - x) = *const_pixel_at(src, x, y);
        }
    }
    return assign_bitmap(dst, &tmp);
}

int glyph_bitmap_flip_horizontal(GlyphBitmap *bitmap) {
    uint32_t x;
    uint32_t y;

    if (!bitmap_is_valid(bitmap)) {
        return 0;
    }
    for (y = 0; y < bitmap->height; y++) {
        for (x = 0; x < bitmap->width / 2; x++) {
            uint8_t *a = pixel_at(bitmap, x, y);
            uint8_t *b = pixel_at(bitmap, bitmap->width - 1 - x, y);
            uint8_t t = *a;
            *a = *b;
            *b = t;
        }
    }
    return 1;
}

int glyph_bitmap_flip_vertical(GlyphBitmap *bitmap) {
    uint8_t *row;
    uint32_t y;

    if (!bitmap_is_valid(bitmap)) {
        return 0;
    }
    row = (uint8_t *)malloc(bitmap->width);
    if (!row) {
        return 0;
    }
    for (y = 0; y < bitmap->height / 2; y++) {
        uint8_t *a = pixel_at(bitmap, 0, y);
        uint8_t *b = pixel_at(bitmap, 0, bitmap->height - 1 - y);
        memcpy(row, a, bitmap->width);
        memcpy(a, b, bitmap->width);
        memcpy(b, row, bitmap->width);
    }
    free(row);
    return 1;
}

void glyph_bitmap_draw_line(GlyphBitmap *bitmap, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t value) {
    int64_t dx;
    int64_t dy;
    int32_t sx;
    int32_t sy;
    int64_t err;

    if (!bitmap_is_valid(bitmap)) {
        return;
    }
    dx = x0 < x1 ? (int64_t)x1 - x0 : (int64_t)x0 - x1;
    dy = y0 < y1 ? (int64_t)y0 - y1 : (int64_t)y1 - y0;
    sx = x0 < x1 ? 1 : -1;
    sy = y0 < y1 ? 1 : -1;
    err = dx + dy;
    for (;;) {
        int64_t e2;
        if (x0 >= 0 && y0 >= 0 && (uint32_t)x0 < bitmap->width && (uint32_t)y0 < bitmap->height) {
            *pixel_at(bitmap, (uint32_t)x0, (uint32_t)y0) = value;
        }
        if (x0 == x1 && y0 == y1) {
            break;
        }
        e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void glyph_bitmap_draw_rect(GlyphBitmap *bitmap, int32_t x, int32_t y, uint32_t width, uint32_t height, uint8_t value) {
    int64_t x1;
    int64_t y1;

    if (!bitmap_is_valid(bitmap) || !width || !height || width > (uint32_t)INT_MAX || height > (uint32_t)INT_MAX) {
        return;
    }
    x1 = (int64_t)x + width - 1;
    y1 = (int64_t)y + height - 1;
    if (x1 < INT_MIN || x1 > INT_MAX || y1 < INT_MIN || y1 > INT_MAX) {
        return;
    }
    glyph_bitmap_draw_line(bitmap, x, y, (int32_t)x1, y, value);
    glyph_bitmap_draw_line(bitmap, x, (int32_t)y1, (int32_t)x1, (int32_t)y1, value);
    glyph_bitmap_draw_line(bitmap, x, y, x, (int32_t)y1, value);
    glyph_bitmap_draw_line(bitmap, (int32_t)x1, y, (int32_t)x1, (int32_t)y1, value);
}

void glyph_bitmap_fill_rect(GlyphBitmap *bitmap, int32_t x, int32_t y, uint32_t width, uint32_t height, uint8_t value) {
    uint32_t x0;
    uint32_t y0;
    uint32_t x1;
    uint32_t y1;
    uint32_t yy;
    int64_t rx0;
    int64_t ry0;
    int64_t rx1;
    int64_t ry1;

    if (!bitmap_is_valid(bitmap) || !width || !height) {
        return;
    }
    rx0 = x;
    ry0 = y;
    rx1 = rx0 + width;
    ry1 = ry0 + height;
    if (rx0 >= bitmap->width || ry0 >= bitmap->height) {
        return;
    }
    if (rx1 <= 0 || ry1 <= 0) {
        return;
    }
    x0 = rx0 < 0 ? 0u : (uint32_t)rx0;
    y0 = ry0 < 0 ? 0u : (uint32_t)ry0;
    x1 = rx1 > bitmap->width ? bitmap->width : (uint32_t)rx1;
    y1 = ry1 > bitmap->height ? bitmap->height : (uint32_t)ry1;
    for (yy = y0; yy < y1; yy++) {
        memset(pixel_at(bitmap, x0, yy), value, x1 - x0);
    }
}

uint32_t glyph_bitmap_checksum(const GlyphBitmap *bitmap) {
    uint32_t sum = 0;
    uint32_t x;
    uint32_t y;

    if (!bitmap_is_valid(bitmap)) {
        return 0;
    }
    sum = bitmap->width * 65537u + bitmap->height;
    for (y = 0; y < bitmap->height; y++) {
        for (x = 0; x < bitmap->width; x++) {
            sum = (sum << 5) | (sum >> 27);
            sum += *const_pixel_at(bitmap, x, y);
        }
    }
    return sum;
}

uint64_t glyph_bitmap_hash64(const GlyphBitmap *bitmap) {
    uint64_t hash = 1469598103934665603ull;
    uint32_t x;
    uint32_t y;

    if (!bitmap_is_valid(bitmap)) {
        return 0;
    }
    hash ^= bitmap->width;
    hash *= 1099511628211ull;
    hash ^= bitmap->height;
    hash *= 1099511628211ull;
    for (y = 0; y < bitmap->height; y++) {
        for (x = 0; x < bitmap->width; x++) {
            hash ^= *const_pixel_at(bitmap, x, y);
            hash *= 1099511628211ull;
        }
    }
    return hash;
}

int glyph_bitmap_compare(const GlyphBitmap *a, const GlyphBitmap *b, uint32_t *different_pixels, uint8_t *max_delta) {
    uint32_t x;
    uint32_t y;
    uint32_t diff = 0;
    uint8_t largest = 0;

    if (different_pixels) {
        *different_pixels = 0;
    }
    if (max_delta) {
        *max_delta = 0;
    }
    if (!bitmap_is_valid(a) || !bitmap_is_valid(b) || a->width != b->width || a->height != b->height) {
        return 0;
    }
    for (y = 0; y < a->height; y++) {
        for (x = 0; x < a->width; x++) {
            uint8_t av = *const_pixel_at(a, x, y);
            uint8_t bv = *const_pixel_at(b, x, y);
            uint8_t delta = av > bv ? (uint8_t)(av - bv) : (uint8_t)(bv - av);
            if (delta) {
                diff++;
                if (delta > largest) {
                    largest = delta;
                }
            }
        }
    }
    if (different_pixels) {
        *different_pixels = diff;
    }
    if (max_delta) {
        *max_delta = largest;
    }
    return diff == 0;
}

size_t glyph_bitmap_ascii_preview(const GlyphBitmap *bitmap, char *buffer, size_t buffer_size, uint32_t max_width, uint32_t max_height) {
    static const char ramp[] = " .:-=+*#%@";
    const uint32_t ramp_last = (uint32_t)(sizeof(ramp) - 2);
    uint32_t out_width;
    uint32_t out_height;
    uint32_t x;
    uint32_t y;
    size_t used = 0;

    if (!bitmap_is_valid(bitmap) || !buffer || buffer_size == 0) {
        return 0;
    }
    buffer[0] = '\0';
    out_width = max_width && max_width < bitmap->width ? max_width : bitmap->width;
    out_height = max_height && max_height < bitmap->height ? max_height : bitmap->height;
    for (y = 0; y < out_height; y++) {
        uint32_t sy = (uint32_t)(((uint64_t)y * bitmap->height) / out_height);
        for (x = 0; x < out_width; x++) {
            uint32_t sx = (uint32_t)(((uint64_t)x * bitmap->width) / out_width);
            uint8_t v = *const_pixel_at(bitmap, sx, sy);
            char ch = ramp[((uint32_t)v * ramp_last + 127u) / 255u];
            if (used + 1 >= buffer_size) {
                buffer[used] = '\0';
                return used;
            }
            buffer[used++] = ch;
        }
        if (used + 1 >= buffer_size) {
            buffer[used] = '\0';
            return used;
        }
        buffer[used++] = '\n';
    }
    buffer[used] = '\0';
    return used;
}
