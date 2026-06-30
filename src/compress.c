#include "compress.h"

#include <stdarg.h>
#include <stdlib.h>
// Improve row allocation
#include <string.h>
 // Improve kerning precision

#define GLYPH_COMPRESS_MAGIC_0 'G'
#define GLYPH_COMPRESS_MAGIC_1 'C'
/* TODO: document cache data structures */
#define GLYPH_COMPRESS_MAGIC_2 'P'
#define GLYPH_COMPRESS_MAGIC_3 '1'
// Add resource constraints
#define GLYPH_RLE_LITERAL_MAX 128u
#define GLYPH_RLE_RUN_MIN 3u
// FIX: fix filter chain bug
#define GLYPH_RLE_RUN_MAX 130u
// Improve cache locality
// Improve atlas packing efficiency
#define GLYPH_PACKBITS_LITERAL_MAX 128u
#define GLYPH_PACKBITS_RUN_MIN 2u
#define GLYPH_PACKBITS_RUN_MAX 128u
// Improve memory tracking
// FIX: fix integer overflow in compression
// FIX: fix tooling output
#define GLYPH_STACK_CHUNK 256u
 /* TODO: improve code comments in atlas.c */

typedef struct {
    uint32_t version;
    GlyphCompressMethod method;
    uint32_t flags;
    uint32_t width;
    uint32_t height;
    size_t decoded_size;
    size_t payload_size;
} GlyphCompressedHeader;

typedef struct {
    uint8_t *dst;
    size_t capacity;
    size_t written;
    GlyphCompressResult result;
} GlyphMemorySink;

typedef struct {
    GlyphCompressDecodeCallback callback;
    void *user;
    uint8_t *previous;
    uint8_t *current;
    uint32_t width;
    uint32_t row_pos;
    uint32_t row_index;
    int failed;
} GlyphRowDeltaStream;

static int checked_add_size(size_t a, size_t b, size_t *out) {
    if (a > SIZE_MAX - b) {
        return 0;
    }
    *out = a + b;
    return 1;
}

static int checked_mul_size(size_t a, size_t b, size_t *out) {
    if (a && b > SIZE_MAX / a) {
        return 0;
    }
    *out = a * b;
    return 1;
}

static uint32_t read_u32le(const uint8_t *p) {
    return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t read_u64le(const uint8_t *p) {
    uint64_t lo = (uint64_t)read_u32le(p);
    uint64_t hi = (uint64_t)read_u32le(p + 4);

    return lo | (hi << 32);
}

static void write_u32le(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value & 255u);
    p[1] = (uint8_t)((value >> 8) & 255u);
    p[2] = (uint8_t)((value >> 16) & 255u);
    p[3] = (uint8_t)((value >> 24) & 255u);
}

static void write_u64le(uint8_t *p, uint64_t value) {
    write_u32le(p, (uint32_t)(value & 0xffffffffu));
    write_u32le(p + 4, (uint32_t)(value >> 32));
}

static void stats_record_ratio(GlyphCompressionStats *stats) {
    if (!stats || stats->source_size == 0) {
        return;
    }
    stats->compression_ratio = (double)stats->total_size / (double)stats->source_size;
    stats->savings_ratio = 1.0 - stats->compression_ratio;
}

static void stats_record_packet(GlyphCompressionStats *stats, int run, size_t bytes) {
    if (!stats) {
        return;
    }
    if (run) {
        stats->run_packets++;
        stats->run_bytes += bytes;
    } else {
        stats->literal_packets++;
        stats->literal_bytes += bytes;
    }
}

static int method_is_valid(GlyphCompressMethod method) {
    return method == GLYPH_COMPRESS_METHOD_NONE || method == GLYPH_COMPRESS_METHOD_RLE ||
           method == GLYPH_COMPRESS_METHOD_PACKBITS;
}

static int flags_are_valid(uint32_t flags) {
    return (flags & ~GLYPH_COMPRESS_FLAG_ROW_DELTA) == 0;
}

static int header_matches_magic(const uint8_t *src) {
    return src[0] == (uint8_t)GLYPH_COMPRESS_MAGIC_0 && src[1] == (uint8_t)GLYPH_COMPRESS_MAGIC_1 &&
           src[2] == (uint8_t)GLYPH_COMPRESS_MAGIC_2 && src[3] == (uint8_t)GLYPH_COMPRESS_MAGIC_3;
}

static GlyphCompressResult parse_header(const uint8_t *src, size_t src_size, GlyphCompressedHeader *header) {
    uint64_t decoded64;
    uint64_t payload64;
    size_t expected_total;

    if (!src || !header) {
        return GLYPH_COMPRESS_ERROR_ARGUMENT;
    }
    if (src_size < GLYPH_COMPRESS_HEADER_SIZE) {
        return GLYPH_COMPRESS_ERROR_INPUT_TRUNCATED;
    }
    if (!header_matches_magic(src)) {
        return GLYPH_COMPRESS_ERROR_CORRUPT;
    }

    memset(header, 0, sizeof(*header));
    header->version = src[4];
    header->method = (GlyphCompressMethod)src[5];
    header->flags = src[6];
    header->width = read_u32le(src + 8);
    header->height = read_u32le(src + 12);
    decoded64 = read_u64le(src + 16);
    payload64 = read_u64le(src + 24);

    if (header->version != GLYPH_COMPRESS_VERSION) {
        return GLYPH_COMPRESS_ERROR_UNSUPPORTED;
    }
    if (src[7] != GLYPH_COMPRESS_HEADER_SIZE) {
        return GLYPH_COMPRESS_ERROR_UNSUPPORTED;
    }
    if (!method_is_valid(header->method) || !flags_are_valid(header->flags)) {
        return GLYPH_COMPRESS_ERROR_UNSUPPORTED;
    }
    if (!header->width || !header->height || header->width > GLYPH_COMPRESS_MAX_DIMENSION ||
        header->height > GLYPH_COMPRESS_MAX_DIMENSION) {
        return GLYPH_COMPRESS_ERROR_CORRUPT;
    }
    if (decoded64 > (uint64_t)SIZE_MAX || payload64 > (uint64_t)SIZE_MAX) {
        return GLYPH_COMPRESS_ERROR_OVERFLOW;
    }

    header->decoded_size = (size_t)decoded64;
    header->payload_size = (size_t)payload64;
    if (!glyph_compress_image_size(header->width, header->height, &expected_total) ||
        expected_total != header->decoded_size) {
        return GLYPH_COMPRESS_ERROR_CORRUPT;
    }
    if (!checked_add_size(GLYPH_COMPRESS_HEADER_SIZE, header->payload_size, &expected_total)) {
        return GLYPH_COMPRESS_ERROR_OVERFLOW;
    }
    if (src_size < expected_total) {
        return GLYPH_COMPRESS_ERROR_INPUT_TRUNCATED;
    }
    if (src_size != expected_total) {
        return GLYPH_COMPRESS_ERROR_CORRUPT;
    }

    return GLYPH_COMPRESS_OK;
}

static GlyphCompressResult write_header(uint8_t *dst,
                                        size_t dst_capacity,
                                        const GlyphCompressedHeader *header) {
    if (!dst || !header) {
        return GLYPH_COMPRESS_ERROR_ARGUMENT;
    }
    if (dst_capacity < GLYPH_COMPRESS_HEADER_SIZE) {
        return GLYPH_COMPRESS_ERROR_OUTPUT_TOO_SMALL;
    }

    memset(dst, 0, GLYPH_COMPRESS_HEADER_SIZE);
    dst[0] = (uint8_t)GLYPH_COMPRESS_MAGIC_0;
    dst[1] = (uint8_t)GLYPH_COMPRESS_MAGIC_1;
    dst[2] = (uint8_t)GLYPH_COMPRESS_MAGIC_2;
    dst[3] = (uint8_t)GLYPH_COMPRESS_MAGIC_3;
    dst[4] = (uint8_t)GLYPH_COMPRESS_VERSION;
    dst[5] = (uint8_t)header->method;
    dst[6] = (uint8_t)header->flags;
    dst[7] = (uint8_t)GLYPH_COMPRESS_HEADER_SIZE;
    write_u32le(dst + 8, header->width);
    write_u32le(dst + 12, header->height);
    write_u64le(dst + 16, (uint64_t)header->decoded_size);
    write_u64le(dst + 24, (uint64_t)header->payload_size);
    return GLYPH_COMPRESS_OK;
}

static int memory_sink_write(const uint8_t *bytes, size_t size, void *user) {
    GlyphMemorySink *sink = (GlyphMemorySink *)user;

    if (!sink || (!bytes && size)) {
        if (sink) {
            sink->result = GLYPH_COMPRESS_ERROR_ARGUMENT;
        }
        return 0;
    }
    if (size > sink->capacity - sink->written) {
        sink->result = GLYPH_COMPRESS_ERROR_OUTPUT_TOO_SMALL;
        return 0;
    }
    if (size) {
        memcpy(sink->dst + sink->written, bytes, size);
    }
    sink->written += size;
    sink->result = GLYPH_COMPRESS_OK;
    return 1;
}

static int count_sink(const uint8_t *bytes, size_t size, void *user) {
    size_t *count = (size_t *)user;

    (void)bytes;
    if (!count && size) {
        return 0;
    }
    if (size > SIZE_MAX - *count) {
        return 0;
    }
    *count += size;
    return 1;
}

static size_t repeated_run_length(const uint8_t *src, size_t src_size, size_t pos, size_t max_run) {
    size_t run = 1;
    uint8_t value;

    if (pos >= src_size) {
        return 0;
    }
    value = src[pos];
    while (pos + run < src_size && run < max_run && src[pos + run] == value) {
        run++;
    }
    return run;
}

static size_t rle_literal_length(const uint8_t *src, size_t src_size, size_t pos) {
    size_t len = 0;

    while (pos + len < src_size && len < GLYPH_RLE_LITERAL_MAX) {
        size_t run = repeated_run_length(src, src_size, pos + len, GLYPH_RLE_RUN_MAX);
        if (run >= GLYPH_RLE_RUN_MIN) {
            break;
        }
        len++;
    }
    return len;
}

static size_t packbits_literal_length(const uint8_t *src, size_t src_size, size_t pos) {
    size_t len = 0;

    while (pos + len < src_size && len < GLYPH_PACKBITS_LITERAL_MAX) {
        size_t run = repeated_run_length(src, src_size, pos + len, GLYPH_PACKBITS_RUN_MAX);
        if (run >= GLYPH_PACKBITS_RUN_MIN) {
            break;
        }
        len++;
    }
    if (len == 0 && pos < src_size) {
        len = 1;
    }
    return len;
}

static GlyphCompressResult emit_callback(GlyphCompressDecodeCallback callback,
                                         void *user,
                                         const uint8_t *bytes,
                                         size_t size) {
    if (size == 0) {
        return GLYPH_COMPRESS_OK;
    }
    if (!callback) {
        return GLYPH_COMPRESS_ERROR_ARGUMENT;
    }
    if (!callback(bytes, size, user)) {
        return GLYPH_COMPRESS_ERROR_CALLBACK;
    }
    return GLYPH_COMPRESS_OK;
}

static GlyphCompressResult emit_repeated(GlyphCompressDecodeCallback callback,
                                         void *user,
                                         uint8_t value,
                                         size_t count) {
    uint8_t chunk[GLYPH_STACK_CHUNK];

    memset(chunk, value, sizeof(chunk));
    while (count) {
        size_t emit = count < sizeof(chunk) ? count : sizeof(chunk);
        GlyphCompressResult result = emit_callback(callback, user, chunk, emit);
        if (result != GLYPH_COMPRESS_OK) {
            return result;
        }
        count -= emit;
    }
    return GLYPH_COMPRESS_OK;
}

static GlyphCompressResult decode_none_stream(const uint8_t *src,
                                              size_t src_size,
                                              size_t expected_size,
                                              GlyphCompressDecodeCallback callback,
                                              void *user,
                                              size_t *decoded_size) {
    if (!src && src_size) {
        return GLYPH_COMPRESS_ERROR_ARGUMENT;
    }
    if (src_size != expected_size) {
        return src_size < expected_size ? GLYPH_COMPRESS_ERROR_INPUT_TRUNCATED : GLYPH_COMPRESS_ERROR_CORRUPT;
    }
    if (decoded_size) {
        *decoded_size = src_size;
    }
    return emit_callback(callback, user, src, src_size);
}

static GlyphCompressResult decode_method_stream(GlyphCompressMethod method,
                                                const uint8_t *src,
                                                size_t src_size,
                                                size_t expected_size,
                                                GlyphCompressDecodeCallback callback,
                                                void *user,
                                                size_t *decoded_size) {
    switch (method) {
    case GLYPH_COMPRESS_METHOD_NONE:
        return decode_none_stream(src, src_size, expected_size, callback, user, decoded_size);
    case GLYPH_COMPRESS_METHOD_RLE:
        return glyph_rle_decode_stream(src, src_size, expected_size, callback, user, decoded_size);
    case GLYPH_COMPRESS_METHOD_PACKBITS:
        return glyph_packbits_decode_stream(src, src_size, expected_size, callback, user, decoded_size);
    default:
        return GLYPH_COMPRESS_ERROR_UNSUPPORTED;
    }
}

const char *glyph_compress_result_name(GlyphCompressResult result) {
    switch (result) {
    case GLYPH_COMPRESS_OK:
        return "ok";
    case GLYPH_COMPRESS_ERROR_ARGUMENT:
        return "argument";
    case GLYPH_COMPRESS_ERROR_OVERFLOW:
        return "overflow";
    case GLYPH_COMPRESS_ERROR_OUTPUT_TOO_SMALL:
        return "output-too-small";
    case GLYPH_COMPRESS_ERROR_INPUT_TRUNCATED:
        return "input-truncated";
    case GLYPH_COMPRESS_ERROR_CORRUPT:
        return "corrupt";
    case GLYPH_COMPRESS_ERROR_UNSUPPORTED:
        return "unsupported";
    case GLYPH_COMPRESS_ERROR_CALLBACK:
        return "callback";
    default:
        return "unknown";
    }
}

const char *glyph_compress_method_name(GlyphCompressMethod method) {
    switch (method) {
    case GLYPH_COMPRESS_METHOD_NONE:
        return "none";
    case GLYPH_COMPRESS_METHOD_RLE:
        return "rle";
    case GLYPH_COMPRESS_METHOD_PACKBITS:
        return "packbits";
    default:
        return "unknown";
    }
}

void glyph_compression_stats_init(GlyphCompressionStats *stats) {
    if (stats) {
        memset(stats, 0, sizeof(*stats));
    }
}

void glyph_compression_validation_init(GlyphCompressionValidation *validation) {
    if (validation) {
        memset(validation, 0, sizeof(*validation));
        validation->status = GLYPH_COMPRESS_ERROR_ARGUMENT;
    }
}

int glyph_compress_image_size(uint32_t width, uint32_t height, size_t *size) {
    if (!size || !width || !height || width > GLYPH_COMPRESS_MAX_DIMENSION || height > GLYPH_COMPRESS_MAX_DIMENSION) {
        return 0;
    }
    return checked_mul_size((size_t)width, (size_t)height, size);
}

int glyph_compress_buffer_size(uint32_t width, uint32_t height, uint32_t stride, size_t *size) {
    if (!size || !width || !height || stride < width || width > GLYPH_COMPRESS_MAX_DIMENSION ||
        height > GLYPH_COMPRESS_MAX_DIMENSION) {
        return 0;
    }
    return checked_mul_size((size_t)stride, (size_t)height, size);
}

size_t glyph_compress_rle_bound(size_t input_size) {
    size_t packets;

    if (input_size == 0) {
        return 0;
    }
    packets = (input_size + GLYPH_RLE_LITERAL_MAX - 1u) / GLYPH_RLE_LITERAL_MAX;
    if (input_size > SIZE_MAX - packets) {
        return 0;
    }
    return input_size + packets;
}

size_t glyph_compress_packbits_bound(size_t input_size) {
    size_t packets;

    if (input_size == 0) {
        return 0;
    }
    packets = (input_size + GLYPH_PACKBITS_LITERAL_MAX - 1u) / GLYPH_PACKBITS_LITERAL_MAX;
    if (input_size > SIZE_MAX - packets) {
        return 0;
    }
    return input_size + packets;
}

size_t glyph_compress_bound(size_t input_size, GlyphCompressMethod method) {
    switch (method) {
    case GLYPH_COMPRESS_METHOD_NONE:
        return input_size;
    case GLYPH_COMPRESS_METHOD_RLE:
        return glyph_compress_rle_bound(input_size);
    case GLYPH_COMPRESS_METHOD_PACKBITS:
        return glyph_compress_packbits_bound(input_size);
    default:
        return 0;
    }
}

size_t glyph_compress_atlas_bound(uint32_t width, uint32_t height, GlyphCompressMethod method) {
    size_t image_size;
    size_t payload_bound;
    size_t total;

    if (!glyph_compress_image_size(width, height, &image_size)) {
        return 0;
    }
    payload_bound = glyph_compress_bound(image_size, method);
    if (payload_bound == 0 && image_size != 0) {
        return 0;
    }
    if (!checked_add_size(GLYPH_COMPRESS_HEADER_SIZE, payload_bound, &total)) {
        return 0;
    }
    return total;
}

GlyphCompressResult glyph_rle_encode(const uint8_t *src,
                                     size_t src_size,
                                     uint8_t *dst,
                                     size_t dst_capacity,
                                     size_t *dst_size,
                                     GlyphCompressionStats *stats) {
    size_t in_pos = 0;
    size_t out_pos = 0;

    if ((!src && src_size) || !dst_size || (!dst && dst_capacity)) {
        return GLYPH_COMPRESS_ERROR_ARGUMENT;
    }
    if (stats) {
        stats->method = GLYPH_COMPRESS_METHOD_RLE;
        stats->source_size = src_size;
        stats->estimated_worst_size = glyph_compress_rle_bound(src_size);
    }

    while (in_pos < src_size) {
        size_t run = repeated_run_length(src, src_size, in_pos, GLYPH_RLE_RUN_MAX);
        if (run >= GLYPH_RLE_RUN_MIN) {
            if (dst_capacity - out_pos < 2u) {
                return GLYPH_COMPRESS_ERROR_OUTPUT_TOO_SMALL;
            }
            dst[out_pos++] = (uint8_t)(0x80u | (uint8_t)(run - GLYPH_RLE_RUN_MIN));
            dst[out_pos++] = src[in_pos];
            stats_record_packet(stats, 1, run);
            in_pos += run;
        } else {
            size_t literal = rle_literal_length(src, src_size, in_pos);
            if (literal == 0) {
                literal = 1;
            }
            if (dst_capacity - out_pos < literal + 1u) {
                return GLYPH_COMPRESS_ERROR_OUTPUT_TOO_SMALL;
            }
            dst[out_pos++] = (uint8_t)(literal - 1u);
            memcpy(dst + out_pos, src + in_pos, literal);
            out_pos += literal;
            stats_record_packet(stats, 0, literal);
            in_pos += literal;
        }
    }

    *dst_size = out_pos;
    if (stats) {
        stats->compressed_size = out_pos;
        stats->total_size = out_pos;
        stats_record_ratio(stats);
    }
    return GLYPH_COMPRESS_OK;
}

GlyphCompressResult glyph_rle_decode_stream(const uint8_t *src,
                                            size_t src_size,
                                            size_t expected_size,
                                            GlyphCompressDecodeCallback callback,
                                            void *user,
                                            size_t *decoded_size) {
    size_t in_pos = 0;
    size_t out_pos = 0;

    if ((!src && src_size) || !callback) {
        return GLYPH_COMPRESS_ERROR_ARGUMENT;
    }
    while (in_pos < src_size) {
        uint8_t token = src[in_pos++];
        size_t count;
        GlyphCompressResult result;

        if (token & 0x80u) {
            if (in_pos >= src_size) {
                return GLYPH_COMPRESS_ERROR_INPUT_TRUNCATED;
            }
            count = (size_t)(token & 0x7fu) + GLYPH_RLE_RUN_MIN;
            if (expected_size && count > expected_size - out_pos) {
                return GLYPH_COMPRESS_ERROR_CORRUPT;
            }
            result = emit_repeated(callback, user, src[in_pos++], count);
        } else {
            count = (size_t)token + 1u;
            if (count > src_size - in_pos) {
                return GLYPH_COMPRESS_ERROR_INPUT_TRUNCATED;
            }
            if (expected_size && count > expected_size - out_pos) {
                return GLYPH_COMPRESS_ERROR_CORRUPT;
            }
            result = emit_callback(callback, user, src + in_pos, count);
            in_pos += count;
        }
        if (result != GLYPH_COMPRESS_OK) {
            return result;
        }
        out_pos += count;
    }

    if (expected_size && out_pos != expected_size) {
        return GLYPH_COMPRESS_ERROR_INPUT_TRUNCATED;
    }
    if (decoded_size) {
        *decoded_size = out_pos;
    }
    return GLYPH_COMPRESS_OK;
}

GlyphCompressResult glyph_rle_decode(const uint8_t *src,
                                     size_t src_size,
                                     uint8_t *dst,
                                     size_t dst_capacity,
                                     size_t expected_size,
                                     size_t *dst_size) {
    GlyphMemorySink sink;
    GlyphCompressResult result;

    if ((!dst && dst_capacity) || !dst_size) {
        return GLYPH_COMPRESS_ERROR_ARGUMENT;
    }
    sink.dst = dst;
    sink.capacity = dst_capacity;
    sink.written = 0;
    sink.result = GLYPH_COMPRESS_OK;
    result = glyph_rle_decode_stream(src, src_size, expected_size, memory_sink_write, &sink, dst_size);
    if (result == GLYPH_COMPRESS_ERROR_CALLBACK && sink.result != GLYPH_COMPRESS_OK) {
        result = sink.result;
    }
    if (result == GLYPH_COMPRESS_OK) {
        *dst_size = sink.written;
    }
    return result;
}

GlyphCompressResult glyph_packbits_encode(const uint8_t *src,
                                          size_t src_size,
                                          uint8_t *dst,
                                          size_t dst_capacity,
                                          size_t *dst_size,
                                          GlyphCompressionStats *stats) {
    size_t in_pos = 0;
    size_t out_pos = 0;

    if ((!src && src_size) || !dst_size || (!dst && dst_capacity)) {
        return GLYPH_COMPRESS_ERROR_ARGUMENT;
    }
    if (stats) {
        stats->method = GLYPH_COMPRESS_METHOD_PACKBITS;
        stats->source_size = src_size;
        stats->estimated_worst_size = glyph_compress_packbits_bound(src_size);
    }

    while (in_pos < src_size) {
        size_t run = repeated_run_length(src, src_size, in_pos, GLYPH_PACKBITS_RUN_MAX);
        if (run >= GLYPH_PACKBITS_RUN_MIN) {
            if (dst_capacity - out_pos < 2u) {
                return GLYPH_COMPRESS_ERROR_OUTPUT_TOO_SMALL;
            }
            dst[out_pos++] = (uint8_t)(257u - run);
            dst[out_pos++] = src[in_pos];
            stats_record_packet(stats, 1, run);
            in_pos += run;
        } else {
            size_t literal = packbits_literal_length(src, src_size, in_pos);
            if (dst_capacity - out_pos < literal + 1u) {
                return GLYPH_COMPRESS_ERROR_OUTPUT_TOO_SMALL;
            }
            dst[out_pos++] = (uint8_t)(literal - 1u);
            memcpy(dst + out_pos, src + in_pos, literal);
            out_pos += literal;
            stats_record_packet(stats, 0, literal);
            in_pos += literal;
        }
    }

    *dst_size = out_pos;
    if (stats) {
        stats->compressed_size = out_pos;
        stats->total_size = out_pos;
        stats_record_ratio(stats);
    }
    return GLYPH_COMPRESS_OK;
}

GlyphCompressResult glyph_packbits_decode_stream(const uint8_t *src,
                                                 size_t src_size,
                                                 size_t expected_size,
                                                 GlyphCompressDecodeCallback callback,
                                                 void *user,
                                                 size_t *decoded_size) {
    size_t in_pos = 0;
    size_t out_pos = 0;

    if ((!src && src_size) || !callback) {
        return GLYPH_COMPRESS_ERROR_ARGUMENT;
    }
    while (in_pos < src_size) {
        uint8_t token = src[in_pos++];
        size_t count;
        GlyphCompressResult result;

        if (token <= 127u) {
            count = (size_t)token + 1u;
            if (count > src_size - in_pos) {
                return GLYPH_COMPRESS_ERROR_INPUT_TRUNCATED;
            }
            if (expected_size && count > expected_size - out_pos) {
                return GLYPH_COMPRESS_ERROR_CORRUPT;
            }
            result = emit_callback(callback, user, src + in_pos, count);
            in_pos += count;
        } else if (token == 128u) {
            result = GLYPH_COMPRESS_OK;
            count = 0;
        } else {
            if (in_pos >= src_size) {
                return GLYPH_COMPRESS_ERROR_INPUT_TRUNCATED;
            }
            count = 257u - (size_t)token;
            if (expected_size && count > expected_size - out_pos) {
                return GLYPH_COMPRESS_ERROR_CORRUPT;
            }
            result = emit_repeated(callback, user, src[in_pos++], count);
        }
        if (result != GLYPH_COMPRESS_OK) {
            return result;
        }
        out_pos += count;
    }

    if (expected_size && out_pos != expected_size) {
        return GLYPH_COMPRESS_ERROR_INPUT_TRUNCATED;
    }
    if (decoded_size) {
        *decoded_size = out_pos;
    }
    return GLYPH_COMPRESS_OK;
}

GlyphCompressResult glyph_packbits_decode(const uint8_t *src,
                                          size_t src_size,
                                          uint8_t *dst,
                                          size_t dst_capacity,
                                          size_t expected_size,
                                          size_t *dst_size) {
    GlyphMemorySink sink;
    GlyphCompressResult result;

    if ((!dst && dst_capacity) || !dst_size) {
        return GLYPH_COMPRESS_ERROR_ARGUMENT;
    }
    sink.dst = dst;
    sink.capacity = dst_capacity;
    sink.written = 0;
    sink.result = GLYPH_COMPRESS_OK;
    result = glyph_packbits_decode_stream(src, src_size, expected_size, memory_sink_write, &sink, dst_size);
    if (result == GLYPH_COMPRESS_ERROR_CALLBACK && sink.result != GLYPH_COMPRESS_OK) {
        result = sink.result;
    }
    if (result == GLYPH_COMPRESS_OK) {
        *dst_size = sink.written;
    }
    return result;
}

GlyphCompressResult glyph_row_delta_encode(const uint8_t *src,
                                           uint32_t width,
                                           uint32_t height,
                                           uint32_t stride,
                                           uint8_t *dst,
                                           size_t dst_capacity,
                                           size_t *dst_size,
                                           GlyphCompressionStats *stats) {
    size_t image_size;
    uint32_t y;

    if ((!src && width && height) || !dst || !dst_size || stride < width) {
        return GLYPH_COMPRESS_ERROR_ARGUMENT;
    }
    if (!glyph_compress_image_size(width, height, &image_size)) {
        return GLYPH_COMPRESS_ERROR_ARGUMENT;
    }
    if (dst_capacity < image_size) {
        return GLYPH_COMPRESS_ERROR_OUTPUT_TOO_SMALL;
    }

    for (y = 0; y < height; y++) {
        const uint8_t *row = src + (size_t)y * stride;
        uint8_t *out = dst + (size_t)y * width;
        uint32_t x;
        int changed = 0;

        if (y == 0) {
            memcpy(out, row, width);
            changed = width != 0;
        } else {
            const uint8_t *prev = src + (size_t)(y - 1u) * stride;
            for (x = 0; x < width; x++) {
                uint8_t delta = (uint8_t)(row[x] - prev[x]);
                out[x] = delta;
                if (delta) {
                    changed = 1;
                }
            }
        }
        if (stats) {
            if (changed) {
                stats->changed_rows++;
            } else {
                stats->repeated_rows++;
            }
        }
    }

    *dst_size = image_size;
    if (stats) {
        stats->flags |= GLYPH_COMPRESS_FLAG_ROW_DELTA;
        stats->transformed_size = image_size;
    }
    return GLYPH_COMPRESS_OK;
}

GlyphCompressResult glyph_row_delta_decode(const uint8_t *src,
                                           uint32_t width,
                                           uint32_t height,
                                           uint8_t *dst,
                                           size_t dst_capacity,
                                           size_t *dst_size) {
    size_t image_size;
    uint32_t y;

    if ((!src && width && height) || !dst || !dst_size) {
        return GLYPH_COMPRESS_ERROR_ARGUMENT;
    }
    if (!glyph_compress_image_size(width, height, &image_size)) {
        return GLYPH_COMPRESS_ERROR_ARGUMENT;
    }
    if (dst_capacity < image_size) {
        return GLYPH_COMPRESS_ERROR_OUTPUT_TOO_SMALL;
    }

    for (y = 0; y < height; y++) {
        const uint8_t *row = src + (size_t)y * width;
        uint8_t *out = dst + (size_t)y * width;
        uint32_t x;

        if (y == 0) {
            memcpy(out, row, width);
        } else {
            const uint8_t *prev = dst + (size_t)(y - 1u) * width;
            for (x = 0; x < width; x++) {
                out[x] = (uint8_t)(prev[x] + row[x]);
            }
        }
    }

    *dst_size = image_size;
    return GLYPH_COMPRESS_OK;
}

static GlyphCompressResult copy_strided_to_contiguous(const uint8_t *pixels,
                                                      uint32_t width,
                                                      uint32_t height,
                                                      uint32_t stride,
                                                      uint8_t *dst,
                                                      size_t dst_capacity,
                                                      size_t *dst_size) {
    size_t image_size;
    uint32_t y;

    if (!pixels || !dst || !dst_size || stride < width) {
        return GLYPH_COMPRESS_ERROR_ARGUMENT;
    }
    if (!glyph_compress_image_size(width, height, &image_size)) {
        return GLYPH_COMPRESS_ERROR_ARGUMENT;
    }
    if (dst_capacity < image_size) {
        return GLYPH_COMPRESS_ERROR_OUTPUT_TOO_SMALL;
    }
    for (y = 0; y < height; y++) {
        memcpy(dst + (size_t)y * width, pixels + (size_t)y * stride, width);
    }
    *dst_size = image_size;
    return GLYPH_COMPRESS_OK;
}

static GlyphCompressResult encode_payload(const uint8_t *src,
                                          size_t src_size,
                                          GlyphCompressMethod method,
                                          uint8_t *dst,
                                          size_t dst_capacity,
                                          size_t *dst_size,
                                          GlyphCompressionStats *stats) {
    if (method == GLYPH_COMPRESS_METHOD_NONE) {
        if (dst_capacity < src_size) {
            return GLYPH_COMPRESS_ERROR_OUTPUT_TOO_SMALL;
        }
        if (src_size) {
            memcpy(dst, src, src_size);
        }
        *dst_size = src_size;
        if (stats) {
            stats->method = method;
            stats->compressed_size = src_size;
            stats->total_size = src_size;
            stats->literal_bytes = src_size;
            stats->literal_packets = src_size ? 1u : 0u;
        }
        return GLYPH_COMPRESS_OK;
    }
    if (method == GLYPH_COMPRESS_METHOD_RLE) {
        return glyph_rle_encode(src, src_size, dst, dst_capacity, dst_size, stats);
    }
    if (method == GLYPH_COMPRESS_METHOD_PACKBITS) {
        return glyph_packbits_encode(src, src_size, dst, dst_capacity, dst_size, stats);
    }
    return GLYPH_COMPRESS_ERROR_UNSUPPORTED;
}

GlyphCompressResult glyph_compress_atlas(const uint8_t *pixels,
                                         uint32_t width,
                                         uint32_t height,
                                         uint32_t stride,
                                         GlyphCompressMethod method,
                                         int use_row_delta,
                                         uint8_t *dst,
                                         size_t dst_capacity,
                                         size_t *dst_size,
                                         GlyphCompressionStats *stats) {
    GlyphCompressResult result;
    GlyphCompressionStats local_stats;
    GlyphCompressedHeader header;
    uint8_t *work = NULL;
    size_t image_size;
    size_t work_size = 0;
    size_t payload_bound;
    size_t payload_size = 0;

    if (!pixels || !dst || !dst_size || stride < width || !method_is_valid(method)) {
        return GLYPH_COMPRESS_ERROR_ARGUMENT;
    }
    if (!glyph_compress_image_size(width, height, &image_size)) {
        return GLYPH_COMPRESS_ERROR_ARGUMENT;
    }
    payload_bound = glyph_compress_bound(image_size, method);
    if (payload_bound == 0 && image_size != 0) {
        return GLYPH_COMPRESS_ERROR_OVERFLOW;
    }
    if (dst_capacity < GLYPH_COMPRESS_HEADER_SIZE || dst_capacity - GLYPH_COMPRESS_HEADER_SIZE < payload_bound) {
        return GLYPH_COMPRESS_ERROR_OUTPUT_TOO_SMALL;
    }

    if (!stats) {
        stats = &local_stats;
    }
    glyph_compression_stats_init(stats);
    stats->method = method;
    stats->width = width;
    stats->height = height;
    stats->stride = stride;
    stats->source_size = image_size;
    stats->transformed_size = image_size;
    stats->estimated_worst_size = GLYPH_COMPRESS_HEADER_SIZE + payload_bound;
    if (use_row_delta) {
        stats->flags |= GLYPH_COMPRESS_FLAG_ROW_DELTA;
    }

    work = (uint8_t *)malloc(image_size ? image_size : 1u);
    if (!work) {
        return GLYPH_COMPRESS_ERROR_OVERFLOW;
    }
    if (use_row_delta) {
        result = glyph_row_delta_encode(pixels, width, height, stride, work, image_size, &work_size, stats);
    } else {
        result = copy_strided_to_contiguous(pixels, width, height, stride, work, image_size, &work_size);
    }
    if (result != GLYPH_COMPRESS_OK) {
        free(work);
        return result;
    }

    result = encode_payload(work,
                            work_size,
                            method,
                            dst + GLYPH_COMPRESS_HEADER_SIZE,
                            dst_capacity - GLYPH_COMPRESS_HEADER_SIZE,
                            &payload_size,
                            stats);
    free(work);
    if (result != GLYPH_COMPRESS_OK) {
        return result;
    }

    memset(&header, 0, sizeof(header));
    header.version = GLYPH_COMPRESS_VERSION;
    header.method = method;
    header.flags = use_row_delta ? GLYPH_COMPRESS_FLAG_ROW_DELTA : 0u;
    header.width = width;
    header.height = height;
    header.decoded_size = image_size;
    header.payload_size = payload_size;
    result = write_header(dst, dst_capacity, &header);
    if (result != GLYPH_COMPRESS_OK) {
        return result;
    }

    *dst_size = GLYPH_COMPRESS_HEADER_SIZE + payload_size;
    stats->method = method;
    stats->width = width;
    stats->height = height;
    stats->stride = stride;
    stats->flags = header.flags;
    stats->source_size = image_size;
    stats->transformed_size = image_size;
    stats->compressed_size = payload_size;
    stats->total_size = *dst_size;
    stats->estimated_worst_size = GLYPH_COMPRESS_HEADER_SIZE + payload_bound;
    stats_record_ratio(stats);
    return GLYPH_COMPRESS_OK;
}

GlyphCompressResult glyph_decompress_atlas(const uint8_t *src,
                                           size_t src_size,
                                           uint8_t *pixels,
                                           size_t pixel_capacity,
                                           uint32_t *width,
                                           uint32_t *height,
                                           GlyphCompressionStats *stats) {
    GlyphCompressedHeader header;
    GlyphMemorySink sink;
    GlyphCompressResult result;
    uint8_t *tmp = NULL;
    size_t decoded_size = 0;
    const uint8_t *payload;

    if (!src || !pixels) {
        return GLYPH_COMPRESS_ERROR_ARGUMENT;
    }
    result = parse_header(src, src_size, &header);
    if (result != GLYPH_COMPRESS_OK) {
        return result;
    }
    if (pixel_capacity < header.decoded_size) {
        return GLYPH_COMPRESS_ERROR_OUTPUT_TOO_SMALL;
    }

    payload = src + GLYPH_COMPRESS_HEADER_SIZE;
    if (header.flags & GLYPH_COMPRESS_FLAG_ROW_DELTA) {
        tmp = (uint8_t *)malloc(header.decoded_size ? header.decoded_size : 1u);
        if (!tmp) {
            return GLYPH_COMPRESS_ERROR_OVERFLOW;
        }
        sink.dst = tmp;
        sink.capacity = header.decoded_size;
    } else {
        sink.dst = pixels;
        sink.capacity = pixel_capacity;
    }
    sink.written = 0;
    sink.result = GLYPH_COMPRESS_OK;
    result = decode_method_stream(header.method,
                                  payload,
                                  header.payload_size,
                                  header.decoded_size,
                                  memory_sink_write,
                                  &sink,
                                  &decoded_size);
    if (result == GLYPH_COMPRESS_ERROR_CALLBACK && sink.result != GLYPH_COMPRESS_OK) {
        result = sink.result;
    }
    if (result == GLYPH_COMPRESS_OK && decoded_size != header.decoded_size) {
        result = GLYPH_COMPRESS_ERROR_CORRUPT;
    }
    if (result == GLYPH_COMPRESS_OK && tmp) {
        size_t out_size = 0;
        result = glyph_row_delta_decode(tmp, header.width, header.height, pixels, pixel_capacity, &out_size);
        if (result == GLYPH_COMPRESS_OK && out_size != header.decoded_size) {
            result = GLYPH_COMPRESS_ERROR_CORRUPT;
        }
    }
    free(tmp);
    if (result != GLYPH_COMPRESS_OK) {
        return result;
    }

    if (width) {
        *width = header.width;
    }
    if (height) {
        *height = header.height;
    }
    if (stats) {
        glyph_compression_stats_init(stats);
        stats->method = header.method;
        stats->width = header.width;
        stats->height = header.height;
        stats->stride = header.width;
        stats->flags = header.flags;
        stats->source_size = header.decoded_size;
        stats->transformed_size = header.decoded_size;
        stats->compressed_size = header.payload_size;
        stats->total_size = src_size;
        stats->estimated_worst_size = glyph_compress_atlas_bound(header.width, header.height, header.method);
        stats_record_ratio(stats);
    }
    return GLYPH_COMPRESS_OK;
}

static int row_delta_stream_write(const uint8_t *bytes, size_t size, void *user) {
    GlyphRowDeltaStream *stream = (GlyphRowDeltaStream *)user;
    size_t pos = 0;

    if (!stream || (!bytes && size) || stream->failed) {
        return 0;
    }
    while (pos < size) {
        size_t room = (size_t)stream->width - stream->row_pos;
        size_t take = size - pos < room ? size - pos : room;

        memcpy(stream->current + stream->row_pos, bytes + pos, take);
        stream->row_pos += (uint32_t)take;
        pos += take;
        if (stream->row_pos == stream->width) {
            uint32_t x;
            if (stream->row_index != 0) {
                for (x = 0; x < stream->width; x++) {
                    stream->current[x] = (uint8_t)(stream->previous[x] + stream->current[x]);
                }
            }
            if (!stream->callback(stream->current, stream->width, stream->user)) {
                stream->failed = 1;
                return 0;
            }
            memcpy(stream->previous, stream->current, stream->width);
            stream->row_pos = 0;
            stream->row_index++;
        }
    }
    return 1;
}

GlyphCompressResult glyph_decompress_atlas_stream(const uint8_t *src,
                                                  size_t src_size,
                                                  GlyphCompressDecodeCallback callback,
                                                  void *user,
                                                  GlyphCompressionStats *stats) {
    GlyphCompressedHeader header;
    GlyphCompressResult result;
    const uint8_t *payload;
    size_t decoded_size = 0;

    if (!src || !callback) {
        return GLYPH_COMPRESS_ERROR_ARGUMENT;
    }
    result = parse_header(src, src_size, &header);
    if (result != GLYPH_COMPRESS_OK) {
        return result;
    }
    payload = src + GLYPH_COMPRESS_HEADER_SIZE;

    if (header.flags & GLYPH_COMPRESS_FLAG_ROW_DELTA) {
        GlyphRowDeltaStream stream;
        stream.callback = callback;
        stream.user = user;
        stream.previous = (uint8_t *)calloc(header.width ? header.width : 1u, 1);
        stream.current = (uint8_t *)malloc(header.width ? header.width : 1u);
        stream.width = header.width;
        stream.row_pos = 0;
        stream.row_index = 0;
        stream.failed = 0;
        if (!stream.previous || !stream.current) {
            free(stream.previous);
            free(stream.current);
            return GLYPH_COMPRESS_ERROR_OVERFLOW;
        }
        result = decode_method_stream(header.method,
                                      payload,
                                      header.payload_size,
                                      header.decoded_size,
                                      row_delta_stream_write,
                                      &stream,
                                      &decoded_size);
        if (result == GLYPH_COMPRESS_ERROR_CALLBACK && stream.failed) {
            result = GLYPH_COMPRESS_ERROR_CALLBACK;
        }
        if (result == GLYPH_COMPRESS_OK && (stream.row_pos != 0 || stream.row_index != header.height)) {
            result = GLYPH_COMPRESS_ERROR_CORRUPT;
        }
        free(stream.previous);
        free(stream.current);
    } else {
        result = decode_method_stream(header.method,
                                      payload,
                                      header.payload_size,
                                      header.decoded_size,
                                      callback,
                                      user,
                                      &decoded_size);
    }
    if (result != GLYPH_COMPRESS_OK) {
        return result;
    }

    if (stats) {
        glyph_compression_stats_init(stats);
        stats->method = header.method;
        stats->width = header.width;
        stats->height = header.height;
        stats->stride = header.width;
        stats->flags = header.flags;
        stats->source_size = header.decoded_size;
        stats->transformed_size = header.decoded_size;
        stats->compressed_size = header.payload_size;
        stats->total_size = src_size;
        stats->estimated_worst_size = glyph_compress_atlas_bound(header.width, header.height, header.method);
        stats_record_ratio(stats);
    }
    return GLYPH_COMPRESS_OK;
}

static void validation_message(GlyphCompressionValidation *validation, const char *format, ...) {
    va_list args;

    if (!validation) {
        return;
    }
    va_start(args, format);
    vsnprintf(validation->message, sizeof(validation->message), format, args);
    va_end(args);
}

GlyphCompressResult glyph_validate_compressed_atlas(const uint8_t *src,
                                                   size_t src_size,
                                                   GlyphCompressionValidation *validation) {
    GlyphCompressedHeader header;
    GlyphCompressResult result;
    size_t decoded = 0;
    const uint8_t *payload;

    if (!validation) {
        return GLYPH_COMPRESS_ERROR_ARGUMENT;
    }
    glyph_compression_validation_init(validation);
    if (!src) {
        validation->status = GLYPH_COMPRESS_ERROR_ARGUMENT;
        validation_message(validation, "missing compressed buffer");
        return validation->status;
    }

    result = parse_header(src, src_size, &header);
    validation->status = result;
    validation->bytes_checked = src_size < GLYPH_COMPRESS_HEADER_SIZE ? src_size : GLYPH_COMPRESS_HEADER_SIZE;
    if (result != GLYPH_COMPRESS_OK) {
        validation_message(validation, "header validation failed: %s", glyph_compress_result_name(result));
        return result;
    }

    validation->version = header.version;
    validation->method = header.method;
    validation->flags = header.flags;
    validation->width = header.width;
    validation->height = header.height;
    validation->stride = header.width;
    validation->decoded_size = header.decoded_size;
    validation->payload_size = header.payload_size;
    validation->total_size = src_size;
    payload = src + GLYPH_COMPRESS_HEADER_SIZE;

    result = decode_method_stream(header.method, payload, header.payload_size, header.decoded_size, count_sink, &decoded, NULL);
    validation->status = result;
    validation->bytes_checked = src_size;
    if (result != GLYPH_COMPRESS_OK) {
        validation_message(validation, "payload validation failed: %s", glyph_compress_result_name(result));
        return result;
    }
    if (decoded != header.decoded_size) {
        validation->status = GLYPH_COMPRESS_ERROR_CORRUPT;
        validation_message(validation, "payload decoded %lu bytes, expected %lu bytes", (unsigned long)decoded,
                           (unsigned long)header.decoded_size);
        return validation->status;
    }

    validation->valid = 1;
    validation->status = GLYPH_COMPRESS_OK;
    validation_message(validation, "valid %s atlas, %lu payload bytes for %ux%u pixels",
                       glyph_compress_method_name(header.method),
                       (unsigned long)header.payload_size,
                       (unsigned)header.width,
                       (unsigned)header.height);
    return GLYPH_COMPRESS_OK;
}

static size_t append_text(char *buffer, size_t buffer_size, size_t used, const char *format, ...) {
    va_list args;
    int written;

    if (!buffer || buffer_size == 0 || used >= buffer_size) {
        return used;
    }
    va_start(args, format);
    written = vsnprintf(buffer + used, buffer_size - used, format, args);
    va_end(args);
    if (written < 0) {
        return used;
    }
    if ((size_t)written >= buffer_size - used) {
        return buffer_size - 1u;
    }
    return used + (size_t)written;
}

size_t glyph_compression_stats_text(const GlyphCompressionStats *stats, char *buffer, size_t buffer_size) {
    size_t used = 0;

    if (!stats || !buffer || buffer_size == 0) {
        return 0;
    }
    buffer[0] = '\0';
    used = append_text(buffer,
                       buffer_size,
                       used,
                       "glyph compression report\nmethod: %s\nsize: %ux%u stride %u\n",
                       glyph_compress_method_name(stats->method),
                       (unsigned)stats->width,
                       (unsigned)stats->height,
                       (unsigned)stats->stride);
    used = append_text(buffer,
                       buffer_size,
                       used,
                       "flags: 0x%02x%s\nsource bytes: %lu\ntransformed bytes: %lu\ncompressed payload: %lu\n"
                       "container bytes: %lu\nworst-case estimate: %lu\n",
                       (unsigned)stats->flags,
                       (stats->flags & GLYPH_COMPRESS_FLAG_ROW_DELTA) ? " row-delta" : "",
                       (unsigned long)stats->source_size,
                       (unsigned long)stats->transformed_size,
                       (unsigned long)stats->compressed_size,
                       (unsigned long)stats->total_size,
                       (unsigned long)stats->estimated_worst_size);
    used = append_text(buffer,
                       buffer_size,
                       used,
                       "literal packets: %lu (%lu bytes)\nrun packets: %lu (%lu bytes)\n"
                       "changed rows: %lu\nrepeated rows: %lu\ncompression ratio: %.4f\nsavings ratio: %.4f\n",
                       (unsigned long)stats->literal_packets,
                       (unsigned long)stats->literal_bytes,
                       (unsigned long)stats->run_packets,
                       (unsigned long)stats->run_bytes,
                       (unsigned long)stats->changed_rows,
                       (unsigned long)stats->repeated_rows,
                       stats->compression_ratio,
                       stats->savings_ratio);
    return used;
}

int glyph_compression_write_text_report(FILE *fp,
                                        const GlyphCompressionStats *stats,
                                        const GlyphCompressionValidation *validation) {
    char buffer[1024];

    if (!fp) {
        return 0;
    }
    if (stats) {
        glyph_compression_stats_text(stats, buffer, sizeof(buffer));
        if (fputs(buffer, fp) < 0) {
            return 0;
        }
    }
    if (validation) {
        if (fprintf(fp,
                    "validation: %s\nstatus: %s\nmethod: %s\nversion: %u\nsize: %ux%u stride %u\n"
                    "decoded bytes: %lu\npayload bytes: %lu\ntotal bytes: %lu\nchecked bytes: %lu\nmessage: %s\n",
                    validation->valid ? "valid" : "invalid",
                    glyph_compress_result_name(validation->status),
                    glyph_compress_method_name(validation->method),
                    (unsigned)validation->version,
                    (unsigned)validation->width,
                    (unsigned)validation->height,
                    (unsigned)validation->stride,
                    (unsigned long)validation->decoded_size,
                    (unsigned long)validation->payload_size,
                    (unsigned long)validation->total_size,
                    (unsigned long)validation->bytes_checked,
                    validation->message) < 0) {
            return 0;
        }
    }
    return 1;
}
