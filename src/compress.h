#ifndef GLYPH_COMPRESS_H
#define GLYPH_COMPRESS_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define GLYPH_COMPRESS_MAGIC_SIZE 4u
#define GLYPH_COMPRESS_HEADER_SIZE 32u
#define GLYPH_COMPRESS_VERSION 1u
#define GLYPH_COMPRESS_MAX_DIMENSION 65535u
#define GLYPH_COMPRESS_FLAG_ROW_DELTA 0x01u

typedef enum {
    GLYPH_COMPRESS_OK = 0,
    GLYPH_COMPRESS_ERROR_ARGUMENT = -1,
    GLYPH_COMPRESS_ERROR_OVERFLOW = -2,
    GLYPH_COMPRESS_ERROR_OUTPUT_TOO_SMALL = -3,
    GLYPH_COMPRESS_ERROR_INPUT_TRUNCATED = -4,
    GLYPH_COMPRESS_ERROR_CORRUPT = -5,
    GLYPH_COMPRESS_ERROR_UNSUPPORTED = -6,
    GLYPH_COMPRESS_ERROR_CALLBACK = -7
} GlyphCompressResult;

typedef enum {
    GLYPH_COMPRESS_METHOD_NONE = 0,
    GLYPH_COMPRESS_METHOD_RLE = 1,
    GLYPH_COMPRESS_METHOD_PACKBITS = 2
} GlyphCompressMethod;

typedef struct {
    GlyphCompressMethod method;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t flags;
    size_t source_size;
    size_t transformed_size;
    size_t compressed_size;
    size_t total_size;
    size_t estimated_worst_size;
    size_t literal_packets;
    size_t run_packets;
    size_t literal_bytes;
    size_t run_bytes;
    size_t repeated_rows;
    size_t changed_rows;
    double compression_ratio;
    double savings_ratio;
} GlyphCompressionStats;

typedef struct {
    int valid;
    GlyphCompressResult status;
    GlyphCompressMethod method;
    uint32_t version;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t flags;
    size_t decoded_size;
    size_t payload_size;
    size_t total_size;
    size_t bytes_checked;
    char message[160];
} GlyphCompressionValidation;

typedef int (*GlyphCompressDecodeCallback)(const uint8_t *bytes, size_t size, void *user);

const char *glyph_compress_result_name(GlyphCompressResult result);
const char *glyph_compress_method_name(GlyphCompressMethod method);

void glyph_compression_stats_init(GlyphCompressionStats *stats);
void glyph_compression_validation_init(GlyphCompressionValidation *validation);

int glyph_compress_image_size(uint32_t width, uint32_t height, size_t *size);
int glyph_compress_buffer_size(uint32_t width, uint32_t height, uint32_t stride, size_t *size);
size_t glyph_compress_rle_bound(size_t input_size);
size_t glyph_compress_packbits_bound(size_t input_size);
size_t glyph_compress_bound(size_t input_size, GlyphCompressMethod method);
size_t glyph_compress_atlas_bound(uint32_t width, uint32_t height, GlyphCompressMethod method);

GlyphCompressResult glyph_rle_encode(const uint8_t *src,
                                     size_t src_size,
                                     uint8_t *dst,
                                     size_t dst_capacity,
                                     size_t *dst_size,
                                     GlyphCompressionStats *stats);
GlyphCompressResult glyph_rle_decode(const uint8_t *src,
                                     size_t src_size,
                                     uint8_t *dst,
                                     size_t dst_capacity,
                                     size_t expected_size,
                                     size_t *dst_size);
GlyphCompressResult glyph_rle_decode_stream(const uint8_t *src,
                                            size_t src_size,
                                            size_t expected_size,
                                            GlyphCompressDecodeCallback callback,
                                            void *user,
                                            size_t *decoded_size);

GlyphCompressResult glyph_packbits_encode(const uint8_t *src,
                                          size_t src_size,
                                          uint8_t *dst,
                                          size_t dst_capacity,
                                          size_t *dst_size,
                                          GlyphCompressionStats *stats);
GlyphCompressResult glyph_packbits_decode(const uint8_t *src,
                                          size_t src_size,
                                          uint8_t *dst,
                                          size_t dst_capacity,
                                          size_t expected_size,
                                          size_t *dst_size);
GlyphCompressResult glyph_packbits_decode_stream(const uint8_t *src,
                                                 size_t src_size,
                                                 size_t expected_size,
                                                 GlyphCompressDecodeCallback callback,
                                                 void *user,
                                                 size_t *decoded_size);

GlyphCompressResult glyph_row_delta_encode(const uint8_t *src,
                                           uint32_t width,
                                           uint32_t height,
                                           uint32_t stride,
                                           uint8_t *dst,
                                           size_t dst_capacity,
                                           size_t *dst_size,
                                           GlyphCompressionStats *stats);
GlyphCompressResult glyph_row_delta_decode(const uint8_t *src,
                                           uint32_t width,
                                           uint32_t height,
                                           uint8_t *dst,
                                           size_t dst_capacity,
                                           size_t *dst_size);

GlyphCompressResult glyph_compress_atlas(const uint8_t *pixels,
                                         uint32_t width,
                                         uint32_t height,
                                         uint32_t stride,
                                         GlyphCompressMethod method,
                                         int use_row_delta,
                                         uint8_t *dst,
                                         size_t dst_capacity,
                                         size_t *dst_size,
                                         GlyphCompressionStats *stats);
GlyphCompressResult glyph_decompress_atlas(const uint8_t *src,
                                           size_t src_size,
                                           uint8_t *pixels,
                                           size_t pixel_capacity,
                                           uint32_t *width,
                                           uint32_t *height,
                                           GlyphCompressionStats *stats);
GlyphCompressResult glyph_decompress_atlas_stream(const uint8_t *src,
                                                  size_t src_size,
                                                  GlyphCompressDecodeCallback callback,
                                                  void *user,
                                                  GlyphCompressionStats *stats);
GlyphCompressResult glyph_validate_compressed_atlas(const uint8_t *src,
                                                   size_t src_size,
                                                   GlyphCompressionValidation *validation);

size_t glyph_compression_stats_text(const GlyphCompressionStats *stats, char *buffer, size_t buffer_size);
int glyph_compression_write_text_report(FILE *fp,
                                        const GlyphCompressionStats *stats,
                                        const GlyphCompressionValidation *validation);

#endif
