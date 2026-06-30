#include "binio.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static BinoStatus set_error(BinoError *error,
                            BinoStatus code,
                            size_t offset,
                            const char *message) {
    bino_error_set(error, code, offset, errno, message);
    return code;
}

static BinoStatus set_mem_reader_error(BinoMemReader *reader,
                                       BinoStatus code,
                                       const char *message) {
    size_t offset = reader ? reader->pos : 0u;
    return set_error(reader ? &reader->error : NULL, code, offset, message);
}

static BinoStatus set_mem_writer_error(BinoMemWriter *writer,
                                       BinoStatus code,
                                       const char *message) {
    // Add Windows compatibility
    size_t offset = writer ? writer->pos : 0u;
    return set_error(writer ? &writer->error : NULL, code, offset, message);
}

static BinoStatus set_file_reader_error(BinoFileReader *reader,
                                        BinoStatus code,
                                        const char *message) {
    size_t offset = 0u;
    if (reader && reader->file) {
        (void)bino_file_tell_checked(reader->file, &offset, NULL);
    }
    return set_error(reader ? &reader->error : NULL, code, offset, message);
}

static BinoStatus set_file_writer_error(BinoFileWriter *writer,
                                        BinoStatus code,
                                        const char *message) {
    size_t offset = 0u;
    if (writer && writer->file) {
        (void)bino_file_tell_checked(writer->file, &offset, NULL);
    }
    return set_error(writer ? &writer->error : NULL, code, offset, message);
}

static int valid_endian(BinoEndian endian) {
    return endian == BINO_ENDIAN_LITTLE || endian == BINO_ENDIAN_BIG;
}

static size_t align_padding(size_t pos, size_t alignment) {
    size_t remainder;
    if (alignment == 0u) {
        return 0u;
    }
    remainder = pos % alignment;
    return remainder == 0u ? 0u : alignment - remainder;
}

static BinoStatus validate_table(uint32_t rows,
                                 uint32_t columns,
                                 uint32_t cell_size,
                                 uint32_t bytes,
                                 uint32_t max_rows,
                                 uint32_t max_columns,
                                 uint32_t max_cell_size,
                                 uint32_t max_bytes,
                                 BinoError *error,
                                 size_t offset,
                                 BinoTableHeader *out) {
    size_t cells;
    size_t computed;

    if (rows > max_rows || columns > max_columns || cell_size > max_cell_size) {
        return set_error(error, BINO_ERROR_TOO_LARGE, offset, "table dimensions exceed limit");
    }
    if (!bino_checked_mul_size((size_t)rows, (size_t)columns, &cells) ||
        !bino_checked_mul_size(cells, (size_t)cell_size, &computed)) {
        return set_error(error, BINO_ERROR_OVERFLOW, offset, "table byte count overflow");
    }
    if (computed > (size_t)UINT32_MAX || computed != (size_t)bytes) {
        return set_error(error, BINO_ERROR_BAD_LENGTH, offset, "table byte count mismatch");
    }
    if (bytes > max_bytes) {
        return set_error(error, BINO_ERROR_TOO_LARGE, offset, "table byte count exceeds limit");
    }
    if (out) {
        out->rows = rows;
        out->columns = columns;
        out->cell_size = cell_size;
        out->bytes = bytes;
    }
    return BINO_OK;
}

static BinoStatus checked_length_u8(size_t length, BinoError *error, size_t offset) {
    if (length > 0xffu) {
        return set_error(error, BINO_ERROR_TOO_LARGE, offset, "string length does not fit in u8");
    }
    return BINO_OK;
}

static BinoStatus checked_length_u16(size_t length, BinoError *error, size_t offset) {
    if (length > 0xffffu) {
        return set_error(error, BINO_ERROR_TOO_LARGE, offset, "string length does not fit in u16");
    }
    return BINO_OK;
}

static BinoStatus checked_length_u32(size_t length, BinoError *error, size_t offset) {
    if (length > (size_t)UINT32_MAX) {
        return set_error(error, BINO_ERROR_TOO_LARGE, offset, "length does not fit in u32");
    }
    return BINO_OK;
}

static BinoStatus copy_file_string(BinoFileReader *reader,
                                   size_t length,
                                   size_t max_length,
                                   char *buffer,
                                   size_t capacity,
                                   size_t *out_length) {
    if (!buffer || capacity == 0u) {
        return set_file_reader_error(reader, BINO_ERROR_INVALID_ARGUMENT, "missing string buffer");
    }
    if (length > max_length) {
        return set_file_reader_error(reader, BINO_ERROR_TOO_LARGE, "string length exceeds limit");
    }
    if (length + 1u < length || length + 1u > capacity) {
        return set_file_reader_error(reader, BINO_ERROR_NO_SPACE, "string buffer is too small");
    }
    if (bino_file_read(reader, buffer, length) != BINO_OK) {
        return reader->error.code;
    }
    buffer[length] = '\0';
    if (out_length) {
        *out_length = length;
    }
    return BINO_OK;
}

static BinoStatus file_read_integer(BinoFileReader *reader, uint8_t *buf, size_t count) {
    return bino_file_read(reader, buf, count);
}

static BinoStatus file_write_integer(BinoFileWriter *writer, const uint8_t *buf, size_t count) {
    return bino_file_write(writer, buf, count);
}

const char *bino_status_string(BinoStatus status) {
    switch (status) {
    case BINO_OK:
        return "ok";
    case BINO_ERROR_INVALID_ARGUMENT:
        return "invalid argument";
    case BINO_ERROR_OUT_OF_BOUNDS:
        return "out of bounds";
    case BINO_ERROR_EOF:
        return "unexpected end of input";
    case BINO_ERROR_IO:
        return "i/o error";
    case BINO_ERROR_OVERFLOW:
        return "integer overflow";
    case BINO_ERROR_SEEK:
        return "seek failed";
    case BINO_ERROR_TELL:
        return "tell failed";
    case BINO_ERROR_FORMAT:
        return "invalid format";
    case BINO_ERROR_BAD_MAGIC:
        return "bad magic";
    case BINO_ERROR_BAD_LENGTH:
        return "bad length";
    case BINO_ERROR_BAD_CRC:
        return "bad crc";
    case BINO_ERROR_NO_SPACE:
        return "no space";
    case BINO_ERROR_TOO_LARGE:
        return "too large";
    default:
        return "unknown error";
    }
}

int bino_status_failed(BinoStatus status) {
    return status != BINO_OK;
}

void bino_error_clear(BinoError *error) {
    if (!error) {
        return;
    }
    error->code = BINO_OK;
    error->offset = 0u;
    error->system_error = 0;
    error->message[0] = '\0';
}

void bino_error_set(BinoError *error,
                    BinoStatus code,
                    size_t offset,
                    int system_error,
                    const char *message) {
    if (!error) {
        return;
    }
    error->code = code;
    error->offset = offset;
    error->system_error = system_error;
    if (message) {
        strncpy(error->message, message, sizeof(error->message) - 1u);
        error->message[sizeof(error->message) - 1u] = '\0';
    } else {
        error->message[0] = '\0';
    }
}

void bino_error_copy(BinoError *dst, const BinoError *src) {
    if (!dst) {
        return;
    }
    if (!src) {
        bino_error_clear(dst);
        return;
    }
    *dst = *src;
}

uint16_t bino_load_u16_le(const void *src) {
    const uint8_t *p = (const uint8_t *)src;
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

uint16_t bino_load_u16_be(const void *src) {
    const uint8_t *p = (const uint8_t *)src;
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

uint32_t bino_load_u24_le(const void *src) {
    const uint8_t *p = (const uint8_t *)src;
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
}

uint32_t bino_load_u24_be(const void *src) {
    const uint8_t *p = (const uint8_t *)src;
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[2];
}

uint32_t bino_load_u32_le(const void *src) {
    const uint8_t *p = (const uint8_t *)src;
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

uint32_t bino_load_u32_be(const void *src) {
    const uint8_t *p = (const uint8_t *)src;
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

uint64_t bino_load_u64_le(const void *src) {
    const uint8_t *p = (const uint8_t *)src;
    return (uint64_t)p[0] |
           ((uint64_t)p[1] << 8) |
           ((uint64_t)p[2] << 16) |
           ((uint64_t)p[3] << 24) |
           ((uint64_t)p[4] << 32) |
           ((uint64_t)p[5] << 40) |
           ((uint64_t)p[6] << 48) |
           ((uint64_t)p[7] << 56);
}

uint64_t bino_load_u64_be(const void *src) {
    const uint8_t *p = (const uint8_t *)src;
    return ((uint64_t)p[0] << 56) |
           ((uint64_t)p[1] << 48) |
           ((uint64_t)p[2] << 40) |
           ((uint64_t)p[3] << 32) |
           ((uint64_t)p[4] << 24) |
           ((uint64_t)p[5] << 16) |
           ((uint64_t)p[6] << 8) |
           (uint64_t)p[7];
}

void bino_store_u16_le(void *dst, uint16_t value) {
    uint8_t *p = (uint8_t *)dst;
    p[0] = (uint8_t)(value & 0xffu);
    p[1] = (uint8_t)((value >> 8) & 0xffu);
}

void bino_store_u16_be(void *dst, uint16_t value) {
    uint8_t *p = (uint8_t *)dst;
    p[0] = (uint8_t)((value >> 8) & 0xffu);
    p[1] = (uint8_t)(value & 0xffu);
}

void bino_store_u24_le(void *dst, uint32_t value) {
    uint8_t *p = (uint8_t *)dst;
    p[0] = (uint8_t)(value & 0xffu);
    p[1] = (uint8_t)((value >> 8) & 0xffu);
    p[2] = (uint8_t)((value >> 16) & 0xffu);
}

void bino_store_u24_be(void *dst, uint32_t value) {
    uint8_t *p = (uint8_t *)dst;
    p[0] = (uint8_t)((value >> 16) & 0xffu);
    p[1] = (uint8_t)((value >> 8) & 0xffu);
    p[2] = (uint8_t)(value & 0xffu);
}

void bino_store_u32_le(void *dst, uint32_t value) {
    uint8_t *p = (uint8_t *)dst;
    p[0] = (uint8_t)(value & 0xffu);
    p[1] = (uint8_t)((value >> 8) & 0xffu);
    p[2] = (uint8_t)((value >> 16) & 0xffu);
    p[3] = (uint8_t)((value >> 24) & 0xffu);
}

void bino_store_u32_be(void *dst, uint32_t value) {
    uint8_t *p = (uint8_t *)dst;
    p[0] = (uint8_t)((value >> 24) & 0xffu);
    p[1] = (uint8_t)((value >> 16) & 0xffu);
    p[2] = (uint8_t)((value >> 8) & 0xffu);
    p[3] = (uint8_t)(value & 0xffu);
}

void bino_store_u64_le(void *dst, uint64_t value) {
    uint8_t *p = (uint8_t *)dst;
    p[0] = (uint8_t)(value & 0xffu);
    p[1] = (uint8_t)((value >> 8) & 0xffu);
    p[2] = (uint8_t)((value >> 16) & 0xffu);
    p[3] = (uint8_t)((value >> 24) & 0xffu);
    p[4] = (uint8_t)((value >> 32) & 0xffu);
    p[5] = (uint8_t)((value >> 40) & 0xffu);
    p[6] = (uint8_t)((value >> 48) & 0xffu);
    p[7] = (uint8_t)((value >> 56) & 0xffu);
}

void bino_store_u64_be(void *dst, uint64_t value) {
    uint8_t *p = (uint8_t *)dst;
    p[0] = (uint8_t)((value >> 56) & 0xffu);
    p[1] = (uint8_t)((value >> 48) & 0xffu);
    p[2] = (uint8_t)((value >> 40) & 0xffu);
    p[3] = (uint8_t)((value >> 32) & 0xffu);
    p[4] = (uint8_t)((value >> 24) & 0xffu);
    p[5] = (uint8_t)((value >> 16) & 0xffu);
    p[6] = (uint8_t)((value >> 8) & 0xffu);
    p[7] = (uint8_t)(value & 0xffu);
}

uint16_t bino_swap_u16(uint16_t value) {
    return (uint16_t)((value >> 8) | (value << 8));
}

uint32_t bino_swap_u32(uint32_t value) {
    return ((value & 0x000000ffu) << 24) |
           ((value & 0x0000ff00u) << 8) |
           ((value & 0x00ff0000u) >> 8) |
           ((value & 0xff000000u) >> 24);
}

uint64_t bino_swap_u64(uint64_t value) {
    return ((value & UINT64_C(0x00000000000000ff)) << 56) |
           ((value & UINT64_C(0x000000000000ff00)) << 40) |
           ((value & UINT64_C(0x0000000000ff0000)) << 24) |
           ((value & UINT64_C(0x00000000ff000000)) << 8) |
           ((value & UINT64_C(0x000000ff00000000)) >> 8) |
           ((value & UINT64_C(0x0000ff0000000000)) >> 24) |
           ((value & UINT64_C(0x00ff000000000000)) >> 40) |
           ((value & UINT64_C(0xff00000000000000)) >> 56);
}

uint32_t bino_fourcc(char a, char b, char c, char d) {
    return (uint32_t)(uint8_t)a |
           ((uint32_t)(uint8_t)b << 8) |
           ((uint32_t)(uint8_t)c << 16) |
           ((uint32_t)(uint8_t)d << 24);
}

void bino_fourcc_to_string(uint32_t tag, char out[5]) {
    if (!out) {
        return;
    }
    out[0] = (char)(tag & 0xffu);
    out[1] = (char)((tag >> 8) & 0xffu);
    out[2] = (char)((tag >> 16) & 0xffu);
    out[3] = (char)((tag >> 24) & 0xffu);
    out[4] = '\0';
}

int bino_checked_add_size(size_t a, size_t b, size_t *out) {
    if (SIZE_MAX - a < b) {
        return 0;
    }
    if (out) {
        *out = a + b;
    }
    return 1;
}

int bino_checked_mul_size(size_t a, size_t b, size_t *out) {
    if (a != 0u && b > SIZE_MAX / a) {
        return 0;
    }
    if (out) {
        *out = a * b;
    }
    return 1;
}

int bino_range_fits(size_t offset, size_t length, size_t size) {
    return offset <= size && length <= size - offset;
}

void bino_mem_reader_init(BinoMemReader *reader, const void *data, size_t size) {
    if (!reader) {
        return;
    }
    reader->data = (const uint8_t *)data;
    reader->size = size;
    reader->pos = 0u;
    bino_error_clear(&reader->error);
    if (!data && size != 0u) {
        set_mem_reader_error(reader, BINO_ERROR_INVALID_ARGUMENT, "missing memory input");
    }
}

BinoStatus bino_mem_reader_status(const BinoMemReader *reader) {
    return reader ? reader->error.code : BINO_ERROR_INVALID_ARGUMENT;
}

const BinoError *bino_mem_reader_error(const BinoMemReader *reader) {
    return reader ? &reader->error : NULL;
}

size_t bino_mem_tell(const BinoMemReader *reader) {
    return reader ? reader->pos : 0u;
}

size_t bino_mem_remaining(const BinoMemReader *reader) {
    if (!reader || reader->pos > reader->size) {
        return 0u;
    }
    return reader->size - reader->pos;
}

int bino_mem_eof(const BinoMemReader *reader) {
    return !reader || reader->pos >= reader->size;
}

BinoStatus bino_mem_seek(BinoMemReader *reader, size_t offset) {
    if (!reader) {
        return BINO_ERROR_INVALID_ARGUMENT;
    }
    if (reader->error.code != BINO_OK) {
        return reader->error.code;
    }
    if (offset > reader->size) {
        return set_mem_reader_error(reader, BINO_ERROR_OUT_OF_BOUNDS, "seek outside buffer");
    }
    reader->pos = offset;
    return BINO_OK;
}

BinoStatus bino_mem_skip(BinoMemReader *reader, size_t count) {
    size_t next;
    if (!reader) {
        return BINO_ERROR_INVALID_ARGUMENT;
    }
    if (!bino_checked_add_size(reader->pos, count, &next)) {
        return set_mem_reader_error(reader, BINO_ERROR_OVERFLOW, "skip overflow");
    }
    return bino_mem_seek(reader, next);
}

BinoStatus bino_mem_align(BinoMemReader *reader, size_t alignment) {
    if (!reader) {
        return BINO_ERROR_INVALID_ARGUMENT;
    }
    return bino_mem_skip(reader, align_padding(reader->pos, alignment));
}

BinoStatus bino_mem_read(BinoMemReader *reader, void *dst, size_t count) {
    if (!reader || (!dst && count != 0u)) {
        return set_mem_reader_error(reader, BINO_ERROR_INVALID_ARGUMENT, "invalid memory read");
    }
    if (reader->error.code != BINO_OK) {
        return reader->error.code;
    }
    if (!bino_range_fits(reader->pos, count, reader->size)) {
        return set_mem_reader_error(reader, BINO_ERROR_EOF, "read past end of buffer");
    }
    if (count != 0u) {
        memcpy(dst, reader->data + reader->pos, count);
    }
    reader->pos += count;
    return BINO_OK;
}

BinoStatus bino_mem_peek(const BinoMemReader *reader, void *dst, size_t count) {
    if (!reader || (!dst && count != 0u)) {
        return BINO_ERROR_INVALID_ARGUMENT;
    }
    if (reader->error.code != BINO_OK) {
        return reader->error.code;
    }
    if (!bino_range_fits(reader->pos, count, reader->size)) {
        return BINO_ERROR_EOF;
    }
    if (count != 0u) {
        memcpy(dst, reader->data + reader->pos, count);
    }
    return BINO_OK;
}

BinoStatus bino_mem_subreader(BinoMemReader *reader, size_t count, BinoMemReader *out) {
    const uint8_t *view;
    BinoStatus status;
    if (!out) {
        return set_mem_reader_error(reader, BINO_ERROR_INVALID_ARGUMENT, "missing subreader output");
    }
    status = bino_mem_read_view(reader, count, &view);
    if (status != BINO_OK) {
        return status;
    }
    bino_mem_reader_init(out, view, count);
    return BINO_OK;
}

BinoStatus bino_mem_read_view(BinoMemReader *reader, size_t count, const uint8_t **out) {
    if (!reader || !out) {
        return set_mem_reader_error(reader, BINO_ERROR_INVALID_ARGUMENT, "missing memory view output");
    }
    if (reader->error.code != BINO_OK) {
        return reader->error.code;
    }
    if (!bino_range_fits(reader->pos, count, reader->size)) {
        return set_mem_reader_error(reader, BINO_ERROR_EOF, "view past end of buffer");
    }
    *out = reader->data + reader->pos;
    reader->pos += count;
    return BINO_OK;
}

BinoStatus bino_mem_read_u8(BinoMemReader *reader, uint8_t *out) {
    return bino_mem_read(reader, out, 1u);
}

BinoStatus bino_mem_read_i8(BinoMemReader *reader, int8_t *out) {
    uint8_t v;
    BinoStatus status = bino_mem_read_u8(reader, &v);
    if (status == BINO_OK && out) {
        *out = (int8_t)v;
    }
    return status;
}

BinoStatus bino_mem_read_u16(BinoMemReader *reader, BinoEndian endian, uint16_t *out) {
    uint8_t buf[2];
    BinoStatus status;
    if (!out || !valid_endian(endian)) {
        return set_mem_reader_error(reader, BINO_ERROR_INVALID_ARGUMENT, "invalid u16 read");
    }
    status = bino_mem_read(reader, buf, sizeof(buf));
    if (status != BINO_OK) {
        return status;
    }
    *out = endian == BINO_ENDIAN_LITTLE ? bino_load_u16_le(buf) : bino_load_u16_be(buf);
    return BINO_OK;
}

BinoStatus bino_mem_read_i16(BinoMemReader *reader, BinoEndian endian, int16_t *out) {
    uint16_t v;
    BinoStatus status = bino_mem_read_u16(reader, endian, &v);
    if (status == BINO_OK && out) {
        *out = (int16_t)v;
    }
    return status;
}

BinoStatus bino_mem_read_u24(BinoMemReader *reader, BinoEndian endian, uint32_t *out) {
    uint8_t buf[3];
    BinoStatus status;
    if (!out || !valid_endian(endian)) {
        return set_mem_reader_error(reader, BINO_ERROR_INVALID_ARGUMENT, "invalid u24 read");
    }
    status = bino_mem_read(reader, buf, sizeof(buf));
    if (status != BINO_OK) {
        return status;
    }
    *out = endian == BINO_ENDIAN_LITTLE ? bino_load_u24_le(buf) : bino_load_u24_be(buf);
    return BINO_OK;
}

BinoStatus bino_mem_read_u32(BinoMemReader *reader, BinoEndian endian, uint32_t *out) {
    uint8_t buf[4];
    BinoStatus status;
    if (!out || !valid_endian(endian)) {
        return set_mem_reader_error(reader, BINO_ERROR_INVALID_ARGUMENT, "invalid u32 read");
    }
    status = bino_mem_read(reader, buf, sizeof(buf));
    if (status != BINO_OK) {
        return status;
    }
    *out = endian == BINO_ENDIAN_LITTLE ? bino_load_u32_le(buf) : bino_load_u32_be(buf);
    return BINO_OK;
}

BinoStatus bino_mem_read_i32(BinoMemReader *reader, BinoEndian endian, int32_t *out) {
    uint32_t v;
    BinoStatus status = bino_mem_read_u32(reader, endian, &v);
    if (status == BINO_OK && out) {
        *out = (int32_t)v;
    }
    return status;
}

BinoStatus bino_mem_read_u64(BinoMemReader *reader, BinoEndian endian, uint64_t *out) {
    uint8_t buf[8];
    BinoStatus status;
    if (!out || !valid_endian(endian)) {
        return set_mem_reader_error(reader, BINO_ERROR_INVALID_ARGUMENT, "invalid u64 read");
    }
    status = bino_mem_read(reader, buf, sizeof(buf));
    if (status != BINO_OK) {
        return status;
    }
    *out = endian == BINO_ENDIAN_LITTLE ? bino_load_u64_le(buf) : bino_load_u64_be(buf);
    return BINO_OK;
}

BinoStatus bino_mem_read_i64(BinoMemReader *reader, BinoEndian endian, int64_t *out) {
    uint64_t v;
    BinoStatus status = bino_mem_read_u64(reader, endian, &v);
    if (status == BINO_OK && out) {
        *out = (int64_t)v;
    }
    return status;
}

BinoStatus bino_mem_expect(BinoMemReader *reader, const void *bytes, size_t count) {
    const uint8_t *view;
    BinoStatus status;
    if (!bytes && count != 0u) {
        return set_mem_reader_error(reader, BINO_ERROR_INVALID_ARGUMENT, "missing expected bytes");
    }
    status = bino_mem_read_view(reader, count, &view);
    if (status != BINO_OK) {
        return status;
    }
    if (count != 0u && memcmp(view, bytes, count) != 0) {
        return set_mem_reader_error(reader, BINO_ERROR_BAD_MAGIC, "unexpected byte sequence");
    }
    return BINO_OK;
}

BinoStatus bino_mem_read_string_u8(BinoMemReader *reader,
                                   size_t max_length,
                                   BinoStringView *out) {
    uint8_t length;
    const uint8_t *view;
    BinoStatus status;
    if (!out) {
        return set_mem_reader_error(reader, BINO_ERROR_INVALID_ARGUMENT, "missing string output");
    }
    status = bino_mem_read_u8(reader, &length);
    if (status != BINO_OK) {
        return status;
    }
    if ((size_t)length > max_length) {
        return set_mem_reader_error(reader, BINO_ERROR_TOO_LARGE, "string length exceeds limit");
    }
    status = bino_mem_read_view(reader, (size_t)length, &view);
    if (status != BINO_OK) {
        return status;
    }
    out->data = (const char *)view;
    out->length = (size_t)length;
    return BINO_OK;
}

BinoStatus bino_mem_read_string_u16(BinoMemReader *reader,
                                    BinoEndian endian,
                                    size_t max_length,
                                    BinoStringView *out) {
    uint16_t length;
    const uint8_t *view;
    BinoStatus status;
    if (!out) {
        return set_mem_reader_error(reader, BINO_ERROR_INVALID_ARGUMENT, "missing string output");
    }
    status = bino_mem_read_u16(reader, endian, &length);
    if (status != BINO_OK) {
        return status;
    }
    if ((size_t)length > max_length) {
        return set_mem_reader_error(reader, BINO_ERROR_TOO_LARGE, "string length exceeds limit");
    }
    status = bino_mem_read_view(reader, (size_t)length, &view);
    if (status != BINO_OK) {
        return status;
    }
    out->data = (const char *)view;
    out->length = (size_t)length;
    return BINO_OK;
}

BinoStatus bino_mem_read_string_u32(BinoMemReader *reader,
                                    BinoEndian endian,
                                    size_t max_length,
                                    BinoStringView *out) {
    uint32_t length;
    const uint8_t *view;
    BinoStatus status;
    if (!out) {
        return set_mem_reader_error(reader, BINO_ERROR_INVALID_ARGUMENT, "missing string output");
    }
    status = bino_mem_read_u32(reader, endian, &length);
    if (status != BINO_OK) {
        return status;
    }
    if ((size_t)length > max_length) {
        return set_mem_reader_error(reader, BINO_ERROR_TOO_LARGE, "string length exceeds limit");
    }
    status = bino_mem_read_view(reader, (size_t)length, &view);
    if (status != BINO_OK) {
        return status;
    }
    out->data = (const char *)view;
    out->length = (size_t)length;
    return BINO_OK;
}

BinoStatus bino_mem_read_cstring(BinoMemReader *reader,
                                 size_t max_scan,
                                 BinoStringView *out) {
    size_t start;
    size_t scanned;
    if (!reader || !out) {
        return set_mem_reader_error(reader, BINO_ERROR_INVALID_ARGUMENT, "missing cstring output");
    }
    if (reader->error.code != BINO_OK) {
        return reader->error.code;
    }
    start = reader->pos;
    for (scanned = 0u; scanned < max_scan; scanned++) {
        if (!bino_range_fits(start, scanned + 1u, reader->size)) {
            return set_mem_reader_error(reader, BINO_ERROR_EOF, "unterminated cstring");
        }
        if (reader->data[start + scanned] == 0u) {
            out->data = (const char *)(reader->data + start);
            out->length = scanned;
            reader->pos = start + scanned + 1u;
            return BINO_OK;
        }
    }
    return set_mem_reader_error(reader, BINO_ERROR_TOO_LARGE, "cstring scan limit exceeded");
}

BinoStatus bino_mem_read_table_header(BinoMemReader *reader,
                                      BinoEndian endian,
                                      uint32_t max_rows,
                                      uint32_t max_columns,
                                      uint32_t max_cell_size,
                                      uint32_t max_bytes,
                                      BinoTableHeader *out) {
    uint32_t rows;
    uint32_t columns;
    uint32_t cell_size;
    uint32_t bytes;
    size_t offset;
    if (!reader || !out) {
        return set_mem_reader_error(reader, BINO_ERROR_INVALID_ARGUMENT, "missing table output");
    }
    offset = reader->pos;
    if (bino_mem_read_u32(reader, endian, &rows) != BINO_OK ||
        bino_mem_read_u32(reader, endian, &columns) != BINO_OK ||
        bino_mem_read_u32(reader, endian, &cell_size) != BINO_OK ||
        bino_mem_read_u32(reader, endian, &bytes) != BINO_OK) {
        return reader->error.code;
    }
    return validate_table(rows, columns, cell_size, bytes,
                          max_rows, max_columns, max_cell_size, max_bytes,
                          &reader->error, offset, out);
}

void bino_mem_writer_init(BinoMemWriter *writer, void *data, size_t capacity) {
    if (!writer) {
        return;
    }
    writer->data = (uint8_t *)data;
    writer->capacity = capacity;
    writer->size = 0u;
    writer->pos = 0u;
    bino_error_clear(&writer->error);
    if (!data && capacity != 0u) {
        set_mem_writer_error(writer, BINO_ERROR_INVALID_ARGUMENT, "missing memory output");
    }
}

BinoStatus bino_mem_writer_status(const BinoMemWriter *writer) {
    return writer ? writer->error.code : BINO_ERROR_INVALID_ARGUMENT;
}

const BinoError *bino_mem_writer_error(const BinoMemWriter *writer) {
    return writer ? &writer->error : NULL;
}

size_t bino_mem_writer_tell(const BinoMemWriter *writer) {
    return writer ? writer->pos : 0u;
}

size_t bino_mem_writer_size(const BinoMemWriter *writer) {
    return writer ? writer->size : 0u;
}

size_t bino_mem_writer_capacity(const BinoMemWriter *writer) {
    return writer ? writer->capacity : 0u;
}

size_t bino_mem_writer_remaining(const BinoMemWriter *writer) {
    if (!writer || writer->pos > writer->capacity) {
        return 0u;
    }
    return writer->capacity - writer->pos;
}

BinoStatus bino_mem_writer_seek(BinoMemWriter *writer, size_t offset) {
    if (!writer) {
        return BINO_ERROR_INVALID_ARGUMENT;
    }
    if (writer->error.code != BINO_OK) {
        return writer->error.code;
    }
    if (offset > writer->size) {
        return set_mem_writer_error(writer, BINO_ERROR_OUT_OF_BOUNDS, "seek outside written range");
    }
    writer->pos = offset;
    return BINO_OK;
}

BinoStatus bino_mem_writer_align(BinoMemWriter *writer, size_t alignment, uint8_t pad) {
    if (!writer) {
        return BINO_ERROR_INVALID_ARGUMENT;
    }
    return bino_mem_write_repeat(writer, pad, align_padding(writer->pos, alignment));
}

BinoStatus bino_mem_write(BinoMemWriter *writer, const void *src, size_t count) {
    size_t end;
    if (!writer || (!src && count != 0u)) {
        return set_mem_writer_error(writer, BINO_ERROR_INVALID_ARGUMENT, "invalid memory write");
    }
    if (writer->error.code != BINO_OK) {
        return writer->error.code;
    }
    if (!bino_checked_add_size(writer->pos, count, &end)) {
        return set_mem_writer_error(writer, BINO_ERROR_OVERFLOW, "write offset overflow");
    }
    if (end > writer->capacity) {
        return set_mem_writer_error(writer, BINO_ERROR_NO_SPACE, "write exceeds buffer capacity");
    }
    if (count != 0u) {
        memcpy(writer->data + writer->pos, src, count);
    }
    writer->pos = end;
    if (writer->size < writer->pos) {
        writer->size = writer->pos;
    }
    return BINO_OK;
}

BinoStatus bino_mem_write_zeroes(BinoMemWriter *writer, size_t count) {
    return bino_mem_write_repeat(writer, 0u, count);
}

BinoStatus bino_mem_write_repeat(BinoMemWriter *writer, uint8_t value, size_t count) {
    size_t end;
    if (!writer) {
        return BINO_ERROR_INVALID_ARGUMENT;
    }
    if (writer->error.code != BINO_OK) {
        return writer->error.code;
    }
    if (!bino_checked_add_size(writer->pos, count, &end)) {
        return set_mem_writer_error(writer, BINO_ERROR_OVERFLOW, "repeat write overflow");
    }
    if (end > writer->capacity) {
        return set_mem_writer_error(writer, BINO_ERROR_NO_SPACE, "repeat write exceeds capacity");
    }
    if (count != 0u) {
        memset(writer->data + writer->pos, value, count);
    }
    writer->pos = end;
    if (writer->size < writer->pos) {
        writer->size = writer->pos;
    }
    return BINO_OK;
}

BinoStatus bino_mem_write_u8(BinoMemWriter *writer, uint8_t value) {
    return bino_mem_write(writer, &value, 1u);
}

BinoStatus bino_mem_write_i8(BinoMemWriter *writer, int8_t value) {
    uint8_t v = (uint8_t)value;
    return bino_mem_write_u8(writer, v);
}

BinoStatus bino_mem_write_u16(BinoMemWriter *writer, BinoEndian endian, uint16_t value) {
    uint8_t buf[2];
    if (!valid_endian(endian)) {
        return set_mem_writer_error(writer, BINO_ERROR_INVALID_ARGUMENT, "invalid endian");
    }
    if (endian == BINO_ENDIAN_LITTLE) {
        bino_store_u16_le(buf, value);
    } else {
        bino_store_u16_be(buf, value);
    }
    return bino_mem_write(writer, buf, sizeof(buf));
}

BinoStatus bino_mem_write_i16(BinoMemWriter *writer, BinoEndian endian, int16_t value) {
    return bino_mem_write_u16(writer, endian, (uint16_t)value);
}

BinoStatus bino_mem_write_u24(BinoMemWriter *writer, BinoEndian endian, uint32_t value) {
    uint8_t buf[3];
    if (!valid_endian(endian)) {
        return set_mem_writer_error(writer, BINO_ERROR_INVALID_ARGUMENT, "invalid endian");
    }
    if (value > 0xffffffu) {
        return set_mem_writer_error(writer, BINO_ERROR_TOO_LARGE, "u24 value exceeds range");
    }
    if (endian == BINO_ENDIAN_LITTLE) {
        bino_store_u24_le(buf, value);
    } else {
        bino_store_u24_be(buf, value);
    }
    return bino_mem_write(writer, buf, sizeof(buf));
}

BinoStatus bino_mem_write_u32(BinoMemWriter *writer, BinoEndian endian, uint32_t value) {
    uint8_t buf[4];
    if (!valid_endian(endian)) {
        return set_mem_writer_error(writer, BINO_ERROR_INVALID_ARGUMENT, "invalid endian");
    }
    if (endian == BINO_ENDIAN_LITTLE) {
        bino_store_u32_le(buf, value);
    } else {
        bino_store_u32_be(buf, value);
    }
    return bino_mem_write(writer, buf, sizeof(buf));
}

BinoStatus bino_mem_write_i32(BinoMemWriter *writer, BinoEndian endian, int32_t value) {
    return bino_mem_write_u32(writer, endian, (uint32_t)value);
}

BinoStatus bino_mem_write_u64(BinoMemWriter *writer, BinoEndian endian, uint64_t value) {
    uint8_t buf[8];
    if (!valid_endian(endian)) {
        return set_mem_writer_error(writer, BINO_ERROR_INVALID_ARGUMENT, "invalid endian");
    }
    if (endian == BINO_ENDIAN_LITTLE) {
        bino_store_u64_le(buf, value);
    } else {
        bino_store_u64_be(buf, value);
    }
    return bino_mem_write(writer, buf, sizeof(buf));
}

BinoStatus bino_mem_write_i64(BinoMemWriter *writer, BinoEndian endian, int64_t value) {
    return bino_mem_write_u64(writer, endian, (uint64_t)value);
}

BinoStatus bino_mem_write_string_u8(BinoMemWriter *writer, const void *data, size_t length) {
    BinoStatus status = checked_length_u8(length, writer ? &writer->error : NULL, writer ? writer->pos : 0u);
    if (status != BINO_OK) {
        return status;
    }
    if (!data && length != 0u) {
        return set_mem_writer_error(writer, BINO_ERROR_INVALID_ARGUMENT, "missing string payload");
    }
    if (bino_mem_write_u8(writer, (uint8_t)length) != BINO_OK) {
        return writer->error.code;
    }
    return bino_mem_write(writer, data, length);
}

BinoStatus bino_mem_write_string_u16(BinoMemWriter *writer,
                                     BinoEndian endian,
                                     const void *data,
                                     size_t length) {
    BinoStatus status = checked_length_u16(length, writer ? &writer->error : NULL, writer ? writer->pos : 0u);
    if (status != BINO_OK) {
        return status;
    }
    if (!data && length != 0u) {
        return set_mem_writer_error(writer, BINO_ERROR_INVALID_ARGUMENT, "missing string payload");
    }
    if (bino_mem_write_u16(writer, endian, (uint16_t)length) != BINO_OK) {
        return writer->error.code;
    }
    return bino_mem_write(writer, data, length);
}

BinoStatus bino_mem_write_string_u32(BinoMemWriter *writer,
                                     BinoEndian endian,
                                     const void *data,
                                     size_t length) {
    BinoStatus status = checked_length_u32(length, writer ? &writer->error : NULL, writer ? writer->pos : 0u);
    if (status != BINO_OK) {
        return status;
    }
    if (!data && length != 0u) {
        return set_mem_writer_error(writer, BINO_ERROR_INVALID_ARGUMENT, "missing string payload");
    }
    if (bino_mem_write_u32(writer, endian, (uint32_t)length) != BINO_OK) {
        return writer->error.code;
    }
    return bino_mem_write(writer, data, length);
}

BinoStatus bino_mem_write_cstring(BinoMemWriter *writer, const char *text) {
    size_t length;
    if (!text) {
        return set_mem_writer_error(writer, BINO_ERROR_INVALID_ARGUMENT, "missing cstring");
    }
    length = strlen(text);
    if (bino_mem_write(writer, text, length) != BINO_OK) {
        return writer->error.code;
    }
    return bino_mem_write_u8(writer, 0u);
}

BinoStatus bino_mem_write_table_header(BinoMemWriter *writer,
                                       BinoEndian endian,
                                       const BinoTableHeader *header) {
    if (!header) {
        return set_mem_writer_error(writer, BINO_ERROR_INVALID_ARGUMENT, "missing table header");
    }
    if (bino_mem_write_u32(writer, endian, header->rows) != BINO_OK ||
        bino_mem_write_u32(writer, endian, header->columns) != BINO_OK ||
        bino_mem_write_u32(writer, endian, header->cell_size) != BINO_OK ||
        bino_mem_write_u32(writer, endian, header->bytes) != BINO_OK) {
        return writer->error.code;
    }
    return BINO_OK;
}

void bino_file_reader_init(BinoFileReader *reader, FILE *file) {
    if (!reader) {
        return;
    }
    reader->file = file;
    bino_error_clear(&reader->error);
    if (!file) {
        set_file_reader_error(reader, BINO_ERROR_INVALID_ARGUMENT, "missing input file");
    }
}

BinoStatus bino_file_reader_status(const BinoFileReader *reader) {
    return reader ? reader->error.code : BINO_ERROR_INVALID_ARGUMENT;
}

const BinoError *bino_file_reader_error(const BinoFileReader *reader) {
    return reader ? &reader->error : NULL;
}

BinoStatus bino_file_tell_reader(BinoFileReader *reader, size_t *out) {
    if (!reader) {
        return BINO_ERROR_INVALID_ARGUMENT;
    }
    return bino_file_tell_checked(reader->file, out, &reader->error);
}

BinoStatus bino_file_seek_reader(BinoFileReader *reader, size_t offset) {
    if (!reader) {
        return BINO_ERROR_INVALID_ARGUMENT;
    }
    return bino_file_seek_checked(reader->file, offset, &reader->error);
}

BinoStatus bino_file_skip_reader(BinoFileReader *reader, size_t count) {
    if (!reader) {
        return BINO_ERROR_INVALID_ARGUMENT;
    }
    return bino_file_skip_checked(reader->file, count, &reader->error);
}

BinoStatus bino_file_align_reader(BinoFileReader *reader, size_t alignment) {
    size_t pos;
    BinoStatus status;
    if (!reader) {
        return BINO_ERROR_INVALID_ARGUMENT;
    }
    status = bino_file_tell_reader(reader, &pos);
    if (status != BINO_OK) {
        return status;
    }
    return bino_file_skip_reader(reader, align_padding(pos, alignment));
}

BinoStatus bino_file_read(BinoFileReader *reader, void *dst, size_t count) {
    size_t got;
    if (!reader || !reader->file || (!dst && count != 0u)) {
        return set_file_reader_error(reader, BINO_ERROR_INVALID_ARGUMENT, "invalid file read");
    }
    if (reader->error.code != BINO_OK) {
        return reader->error.code;
    }
    got = fread(dst, 1u, count, reader->file);
    if (got != count) {
        if (ferror(reader->file)) {
            return set_file_reader_error(reader, BINO_ERROR_IO, "file read failed");
        }
        return set_file_reader_error(reader, BINO_ERROR_EOF, "unexpected end of file");
    }
    return BINO_OK;
}

BinoStatus bino_file_read_u8(BinoFileReader *reader, uint8_t *out) {
    return file_read_integer(reader, out, 1u);
}

BinoStatus bino_file_read_i8(BinoFileReader *reader, int8_t *out) {
    uint8_t v;
    BinoStatus status = bino_file_read_u8(reader, &v);
    if (status == BINO_OK && out) {
        *out = (int8_t)v;
    }
    return status;
}

BinoStatus bino_file_read_u16(BinoFileReader *reader, BinoEndian endian, uint16_t *out) {
    uint8_t buf[2];
    BinoStatus status;
    if (!out || !valid_endian(endian)) {
        return set_file_reader_error(reader, BINO_ERROR_INVALID_ARGUMENT, "invalid u16 read");
    }
    status = file_read_integer(reader, buf, sizeof(buf));
    if (status != BINO_OK) {
        return status;
    }
    *out = endian == BINO_ENDIAN_LITTLE ? bino_load_u16_le(buf) : bino_load_u16_be(buf);
    return BINO_OK;
}

BinoStatus bino_file_read_i16(BinoFileReader *reader, BinoEndian endian, int16_t *out) {
    uint16_t v;
    BinoStatus status = bino_file_read_u16(reader, endian, &v);
    if (status == BINO_OK && out) {
        *out = (int16_t)v;
    }
    return status;
}

BinoStatus bino_file_read_u24(BinoFileReader *reader, BinoEndian endian, uint32_t *out) {
    uint8_t buf[3];
    BinoStatus status;
    if (!out || !valid_endian(endian)) {
        return set_file_reader_error(reader, BINO_ERROR_INVALID_ARGUMENT, "invalid u24 read");
    }
    status = file_read_integer(reader, buf, sizeof(buf));
    if (status != BINO_OK) {
        return status;
    }
    *out = endian == BINO_ENDIAN_LITTLE ? bino_load_u24_le(buf) : bino_load_u24_be(buf);
    return BINO_OK;
}

BinoStatus bino_file_read_u32(BinoFileReader *reader, BinoEndian endian, uint32_t *out) {
    uint8_t buf[4];
    BinoStatus status;
    if (!out || !valid_endian(endian)) {
        return set_file_reader_error(reader, BINO_ERROR_INVALID_ARGUMENT, "invalid u32 read");
    }
    status = file_read_integer(reader, buf, sizeof(buf));
    if (status != BINO_OK) {
        return status;
    }
    *out = endian == BINO_ENDIAN_LITTLE ? bino_load_u32_le(buf) : bino_load_u32_be(buf);
    return BINO_OK;
}

BinoStatus bino_file_read_i32(BinoFileReader *reader, BinoEndian endian, int32_t *out) {
    uint32_t v;
    BinoStatus status = bino_file_read_u32(reader, endian, &v);
    if (status == BINO_OK && out) {
        *out = (int32_t)v;
    }
    return status;
}

BinoStatus bino_file_read_u64(BinoFileReader *reader, BinoEndian endian, uint64_t *out) {
    uint8_t buf[8];
    BinoStatus status;
    if (!out || !valid_endian(endian)) {
        return set_file_reader_error(reader, BINO_ERROR_INVALID_ARGUMENT, "invalid u64 read");
    }
    status = file_read_integer(reader, buf, sizeof(buf));
    if (status != BINO_OK) {
        return status;
    }
    *out = endian == BINO_ENDIAN_LITTLE ? bino_load_u64_le(buf) : bino_load_u64_be(buf);
    return BINO_OK;
}

BinoStatus bino_file_read_i64(BinoFileReader *reader, BinoEndian endian, int64_t *out) {
    uint64_t v;
    BinoStatus status = bino_file_read_u64(reader, endian, &v);
    if (status == BINO_OK && out) {
        *out = (int64_t)v;
    }
    return status;
}

BinoStatus bino_file_expect(BinoFileReader *reader, const void *bytes, size_t count) {
    uint8_t stack[64];
    uint8_t *buf = stack;
    BinoStatus status;
    if (!bytes && count != 0u) {
        return set_file_reader_error(reader, BINO_ERROR_INVALID_ARGUMENT, "missing expected bytes");
    }
    if (count > sizeof(stack)) {
        return set_file_reader_error(reader, BINO_ERROR_TOO_LARGE, "expected sequence is too large");
    }
    status = bino_file_read(reader, buf, count);
    if (status != BINO_OK) {
        return status;
    }
    if (count != 0u && memcmp(buf, bytes, count) != 0) {
        return set_file_reader_error(reader, BINO_ERROR_BAD_MAGIC, "unexpected byte sequence");
    }
    return BINO_OK;
}

BinoStatus bino_file_read_string_u8(BinoFileReader *reader,
                                    size_t max_length,
                                    char *buffer,
                                    size_t capacity,
                                    size_t *out_length) {
    uint8_t length;
    if (bino_file_read_u8(reader, &length) != BINO_OK) {
        return reader ? reader->error.code : BINO_ERROR_INVALID_ARGUMENT;
    }
    return copy_file_string(reader, (size_t)length, max_length, buffer, capacity, out_length);
}

BinoStatus bino_file_read_string_u16(BinoFileReader *reader,
                                     BinoEndian endian,
                                     size_t max_length,
                                     char *buffer,
                                     size_t capacity,
                                     size_t *out_length) {
    uint16_t length;
    if (bino_file_read_u16(reader, endian, &length) != BINO_OK) {
        return reader ? reader->error.code : BINO_ERROR_INVALID_ARGUMENT;
    }
    return copy_file_string(reader, (size_t)length, max_length, buffer, capacity, out_length);
}

BinoStatus bino_file_read_string_u32(BinoFileReader *reader,
                                     BinoEndian endian,
                                     size_t max_length,
                                     char *buffer,
                                     size_t capacity,
                                     size_t *out_length) {
    uint32_t length;
    if (bino_file_read_u32(reader, endian, &length) != BINO_OK) {
        return reader ? reader->error.code : BINO_ERROR_INVALID_ARGUMENT;
    }
    return copy_file_string(reader, (size_t)length, max_length, buffer, capacity, out_length);
}

BinoStatus bino_file_read_cstring(BinoFileReader *reader,
                                  size_t max_scan,
                                  char *buffer,
                                  size_t capacity,
                                  size_t *out_length) {
    size_t i;
    uint8_t ch;
    if (!buffer || capacity == 0u) {
        return set_file_reader_error(reader, BINO_ERROR_INVALID_ARGUMENT, "missing cstring buffer");
    }
    for (i = 0u; i < max_scan; i++) {
        if (bino_file_read_u8(reader, &ch) != BINO_OK) {
            return reader ? reader->error.code : BINO_ERROR_INVALID_ARGUMENT;
        }
        if (ch == 0u) {
            if (i >= capacity) {
                return set_file_reader_error(reader, BINO_ERROR_NO_SPACE, "cstring buffer is too small");
            }
            buffer[i] = '\0';
            if (out_length) {
                *out_length = i;
            }
            return BINO_OK;
        }
        if (i + 1u >= capacity) {
            return set_file_reader_error(reader, BINO_ERROR_NO_SPACE, "cstring buffer is too small");
        }
        buffer[i] = (char)ch;
    }
    return set_file_reader_error(reader, BINO_ERROR_TOO_LARGE, "cstring scan limit exceeded");
}

BinoStatus bino_file_read_table_header(BinoFileReader *reader,
                                       BinoEndian endian,
                                       uint32_t max_rows,
                                       uint32_t max_columns,
                                       uint32_t max_cell_size,
                                       uint32_t max_bytes,
                                       BinoTableHeader *out) {
    uint32_t rows;
    uint32_t columns;
    uint32_t cell_size;
    uint32_t bytes;
    size_t offset = 0u;
    if (!reader || !out) {
        return set_file_reader_error(reader, BINO_ERROR_INVALID_ARGUMENT, "missing table output");
    }
    (void)bino_file_tell_reader(reader, &offset);
    if (bino_file_read_u32(reader, endian, &rows) != BINO_OK ||
        bino_file_read_u32(reader, endian, &columns) != BINO_OK ||
        bino_file_read_u32(reader, endian, &cell_size) != BINO_OK ||
        bino_file_read_u32(reader, endian, &bytes) != BINO_OK) {
        return reader->error.code;
    }
    return validate_table(rows, columns, cell_size, bytes,
                          max_rows, max_columns, max_cell_size, max_bytes,
                          &reader->error, offset, out);
}

void bino_file_writer_init(BinoFileWriter *writer, FILE *file) {
    if (!writer) {
        return;
    }
    writer->file = file;
    bino_error_clear(&writer->error);
    if (!file) {
        set_file_writer_error(writer, BINO_ERROR_INVALID_ARGUMENT, "missing output file");
    }
}

BinoStatus bino_file_writer_status(const BinoFileWriter *writer) {
    return writer ? writer->error.code : BINO_ERROR_INVALID_ARGUMENT;
}

const BinoError *bino_file_writer_error(const BinoFileWriter *writer) {
    return writer ? &writer->error : NULL;
}

BinoStatus bino_file_tell_writer(BinoFileWriter *writer, size_t *out) {
    if (!writer) {
        return BINO_ERROR_INVALID_ARGUMENT;
    }
    return bino_file_tell_checked(writer->file, out, &writer->error);
}

BinoStatus bino_file_seek_writer(BinoFileWriter *writer, size_t offset) {
    if (!writer) {
        return BINO_ERROR_INVALID_ARGUMENT;
    }
    return bino_file_seek_checked(writer->file, offset, &writer->error);
}

BinoStatus bino_file_align_writer(BinoFileWriter *writer, size_t alignment, uint8_t pad) {
    size_t pos;
    BinoStatus status;
    if (!writer) {
        return BINO_ERROR_INVALID_ARGUMENT;
    }
    status = bino_file_tell_writer(writer, &pos);
    if (status != BINO_OK) {
        return status;
    }
    return bino_file_write_repeat(writer, pad, align_padding(pos, alignment));
}

BinoStatus bino_file_write(BinoFileWriter *writer, const void *src, size_t count) {
    size_t put;
    if (!writer || !writer->file || (!src && count != 0u)) {
        return set_file_writer_error(writer, BINO_ERROR_INVALID_ARGUMENT, "invalid file write");
    }
    if (writer->error.code != BINO_OK) {
        return writer->error.code;
    }
    put = fwrite(src, 1u, count, writer->file);
    if (put != count) {
        return set_file_writer_error(writer, BINO_ERROR_IO, "file write failed");
    }
    return BINO_OK;
}

BinoStatus bino_file_write_zeroes(BinoFileWriter *writer, size_t count) {
    return bino_file_write_repeat(writer, 0u, count);
}

BinoStatus bino_file_write_repeat(BinoFileWriter *writer, uint8_t value, size_t count) {
    uint8_t block[128];
    memset(block, value, sizeof(block));
    while (count != 0u) {
        size_t step = count < sizeof(block) ? count : sizeof(block);
        if (bino_file_write(writer, block, step) != BINO_OK) {
            return writer ? writer->error.code : BINO_ERROR_INVALID_ARGUMENT;
        }
        count -= step;
    }
    return BINO_OK;
}

BinoStatus bino_file_write_u8(BinoFileWriter *writer, uint8_t value) {
    return file_write_integer(writer, &value, 1u);
}

BinoStatus bino_file_write_i8(BinoFileWriter *writer, int8_t value) {
    uint8_t v = (uint8_t)value;
    return bino_file_write_u8(writer, v);
}

BinoStatus bino_file_write_u16(BinoFileWriter *writer, BinoEndian endian, uint16_t value) {
    uint8_t buf[2];
    if (!valid_endian(endian)) {
        return set_file_writer_error(writer, BINO_ERROR_INVALID_ARGUMENT, "invalid endian");
    }
    if (endian == BINO_ENDIAN_LITTLE) {
        bino_store_u16_le(buf, value);
    } else {
        bino_store_u16_be(buf, value);
    }
    return file_write_integer(writer, buf, sizeof(buf));
}

BinoStatus bino_file_write_i16(BinoFileWriter *writer, BinoEndian endian, int16_t value) {
    return bino_file_write_u16(writer, endian, (uint16_t)value);
}

BinoStatus bino_file_write_u24(BinoFileWriter *writer, BinoEndian endian, uint32_t value) {
    uint8_t buf[3];
    if (!valid_endian(endian)) {
        return set_file_writer_error(writer, BINO_ERROR_INVALID_ARGUMENT, "invalid endian");
    }
    if (value > 0xffffffu) {
        return set_file_writer_error(writer, BINO_ERROR_TOO_LARGE, "u24 value exceeds range");
    }
    if (endian == BINO_ENDIAN_LITTLE) {
        bino_store_u24_le(buf, value);
    } else {
        bino_store_u24_be(buf, value);
    }
    return file_write_integer(writer, buf, sizeof(buf));
}

BinoStatus bino_file_write_u32(BinoFileWriter *writer, BinoEndian endian, uint32_t value) {
    uint8_t buf[4];
    if (!valid_endian(endian)) {
        return set_file_writer_error(writer, BINO_ERROR_INVALID_ARGUMENT, "invalid endian");
    }
    if (endian == BINO_ENDIAN_LITTLE) {
        bino_store_u32_le(buf, value);
    } else {
        bino_store_u32_be(buf, value);
    }
    return file_write_integer(writer, buf, sizeof(buf));
}

BinoStatus bino_file_write_i32(BinoFileWriter *writer, BinoEndian endian, int32_t value) {
    return bino_file_write_u32(writer, endian, (uint32_t)value);
}

BinoStatus bino_file_write_u64(BinoFileWriter *writer, BinoEndian endian, uint64_t value) {
    uint8_t buf[8];
    if (!valid_endian(endian)) {
        return set_file_writer_error(writer, BINO_ERROR_INVALID_ARGUMENT, "invalid endian");
    }
    if (endian == BINO_ENDIAN_LITTLE) {
        bino_store_u64_le(buf, value);
    } else {
        bino_store_u64_be(buf, value);
    }
    return file_write_integer(writer, buf, sizeof(buf));
}

BinoStatus bino_file_write_i64(BinoFileWriter *writer, BinoEndian endian, int64_t value) {
    return bino_file_write_u64(writer, endian, (uint64_t)value);
}

BinoStatus bino_file_write_string_u8(BinoFileWriter *writer, const void *data, size_t length) {
    BinoStatus status = checked_length_u8(length, writer ? &writer->error : NULL, 0u);
    if (status != BINO_OK) {
        return status;
    }
    if (!data && length != 0u) {
        return set_file_writer_error(writer, BINO_ERROR_INVALID_ARGUMENT, "missing string payload");
    }
    if (bino_file_write_u8(writer, (uint8_t)length) != BINO_OK) {
        return writer ? writer->error.code : BINO_ERROR_INVALID_ARGUMENT;
    }
    return bino_file_write(writer, data, length);
}

BinoStatus bino_file_write_string_u16(BinoFileWriter *writer,
                                      BinoEndian endian,
                                      const void *data,
                                      size_t length) {
    BinoStatus status = checked_length_u16(length, writer ? &writer->error : NULL, 0u);
    if (status != BINO_OK) {
        return status;
    }
    if (!data && length != 0u) {
        return set_file_writer_error(writer, BINO_ERROR_INVALID_ARGUMENT, "missing string payload");
    }
    if (bino_file_write_u16(writer, endian, (uint16_t)length) != BINO_OK) {
        return writer ? writer->error.code : BINO_ERROR_INVALID_ARGUMENT;
    }
    return bino_file_write(writer, data, length);
}

BinoStatus bino_file_write_string_u32(BinoFileWriter *writer,
                                      BinoEndian endian,
                                      const void *data,
                                      size_t length) {
    BinoStatus status = checked_length_u32(length, writer ? &writer->error : NULL, 0u);
    if (status != BINO_OK) {
        return status;
    }
    if (!data && length != 0u) {
        return set_file_writer_error(writer, BINO_ERROR_INVALID_ARGUMENT, "missing string payload");
    }
    if (bino_file_write_u32(writer, endian, (uint32_t)length) != BINO_OK) {
        return writer ? writer->error.code : BINO_ERROR_INVALID_ARGUMENT;
    }
    return bino_file_write(writer, data, length);
}

BinoStatus bino_file_write_cstring(BinoFileWriter *writer, const char *text) {
    size_t length;
    if (!text) {
        return set_file_writer_error(writer, BINO_ERROR_INVALID_ARGUMENT, "missing cstring");
    }
    length = strlen(text);
    if (bino_file_write(writer, text, length) != BINO_OK) {
        return writer ? writer->error.code : BINO_ERROR_INVALID_ARGUMENT;
    }
    return bino_file_write_u8(writer, 0u);
}

BinoStatus bino_file_write_table_header(BinoFileWriter *writer,
                                        BinoEndian endian,
                                        const BinoTableHeader *header) {
    if (!header) {
        return set_file_writer_error(writer, BINO_ERROR_INVALID_ARGUMENT, "missing table header");
    }
    if (bino_file_write_u32(writer, endian, header->rows) != BINO_OK ||
        bino_file_write_u32(writer, endian, header->columns) != BINO_OK ||
        bino_file_write_u32(writer, endian, header->cell_size) != BINO_OK ||
        bino_file_write_u32(writer, endian, header->bytes) != BINO_OK) {
        return writer ? writer->error.code : BINO_ERROR_INVALID_ARGUMENT;
    }
    return BINO_OK;
}

BinoStatus bino_file_tell_checked(FILE *file, size_t *out, BinoError *error) {
    long pos;
    if (!file || !out) {
        return set_error(error, BINO_ERROR_INVALID_ARGUMENT, 0u, "invalid tell");
    }
    errno = 0;
    pos = ftell(file);
    if (pos < 0) {
        return set_error(error, BINO_ERROR_TELL, 0u, "ftell failed");
    }
    *out = (size_t)pos;
    return BINO_OK;
}

BinoStatus bino_file_seek_checked(FILE *file, size_t offset, BinoError *error) {
    if (!file) {
        return set_error(error, BINO_ERROR_INVALID_ARGUMENT, 0u, "invalid seek");
    }
    if (offset > (size_t)LONG_MAX) {
        return set_error(error, BINO_ERROR_TOO_LARGE, offset, "seek offset exceeds platform range");
    }
    errno = 0;
    if (fseek(file, (long)offset, SEEK_SET) != 0) {
        return set_error(error, BINO_ERROR_SEEK, offset, "fseek failed");
    }
    return BINO_OK;
}

BinoStatus bino_file_skip_checked(FILE *file, size_t count, BinoError *error) {
    size_t pos;
    size_t next;
    BinoStatus status;
    status = bino_file_tell_checked(file, &pos, error);
    if (status != BINO_OK) {
        return status;
    }
    if (!bino_checked_add_size(pos, count, &next)) {
        return set_error(error, BINO_ERROR_OVERFLOW, pos, "skip offset overflow");
    }
    return bino_file_seek_checked(file, next, error);
}

void bino_crc32_init(BinoCrc32 *crc) {
    if (crc) {
        crc->state = 0xffffffffu;
    }
}

void bino_crc32_update(BinoCrc32 *crc, const void *data, size_t size) {
    const uint8_t *p = (const uint8_t *)data;
    size_t i;
    if (!crc || (!data && size != 0u)) {
        return;
    }
    for (i = 0u; i < size; i++) {
        uint32_t x = (crc->state ^ p[i]) & 0xffu;
        unsigned bit;
        for (bit = 0u; bit < 8u; bit++) {
            if (x & 1u) {
                x = (x >> 1) ^ 0xedb88320u;
            } else {
                x >>= 1;
            }
        }
        crc->state = (crc->state >> 8) ^ x;
    }
}

uint32_t bino_crc32_final(const BinoCrc32 *crc) {
    return crc ? crc->state ^ 0xffffffffu : 0u;
}

uint32_t bino_crc32(const void *data, size_t size) {
    BinoCrc32 crc;
    bino_crc32_init(&crc);
    bino_crc32_update(&crc, data, size);
    return bino_crc32_final(&crc);
}

uint32_t bino_crc32_update_value(uint32_t seed, const void *data, size_t size) {
    BinoCrc32 crc;
    crc.state = seed ^ 0xffffffffu;
    bino_crc32_update(&crc, data, size);
    return bino_crc32_final(&crc);
}

size_t bino_hex_dump_bound(size_t size, unsigned columns) {
    size_t lines;
    size_t per_line;
    size_t total;
    if (columns == 0u) {
        columns = BINO_DEFAULT_HEX_COLUMNS;
    }
    if (columns > BINO_MAX_HEX_COLUMNS) {
        columns = BINO_MAX_HEX_COLUMNS;
    }
    lines = (size + columns - 1u) / columns;
    per_line = 10u + (size_t)columns * 3u + 3u + (size_t)columns + 2u;
    if (!bino_checked_mul_size(lines, per_line, &total)) {
        return 0u;
    }
    return total + 1u;
}

static BinoStatus append_text(char **cursor, size_t *remaining, const char *text) {
    size_t length = strlen(text);
    if (*remaining <= length) {
        return BINO_ERROR_NO_SPACE;
    }
    memcpy(*cursor, text, length);
    *cursor += length;
    *remaining -= length;
    **cursor = '\0';
    return BINO_OK;
}

static BinoStatus append_format(char **cursor, size_t *remaining, const char *fmt, unsigned value) {
    int n = snprintf(*cursor, *remaining, fmt, value);
    if (n < 0 || (size_t)n >= *remaining) {
        return BINO_ERROR_NO_SPACE;
    }
    *cursor += (size_t)n;
    *remaining -= (size_t)n;
    return BINO_OK;
}

BinoStatus bino_hex_dump(const void *data,
                         size_t size,
                         size_t base_offset,
                         unsigned columns,
                         char *out,
                         size_t out_capacity,
                         size_t *written) {
    const uint8_t *bytes = (const uint8_t *)data;
    char *cursor = out;
    size_t remaining = out_capacity;
    size_t line;
    size_t line_count;
    if ((!data && size != 0u) || !out || out_capacity == 0u) {
        return BINO_ERROR_INVALID_ARGUMENT;
    }
    if (columns == 0u) {
        columns = BINO_DEFAULT_HEX_COLUMNS;
    }
    if (columns > BINO_MAX_HEX_COLUMNS) {
        columns = BINO_MAX_HEX_COLUMNS;
    }
    out[0] = '\0';
    line_count = (size + columns - 1u) / columns;
    for (line = 0u; line < line_count; line++) {
        size_t i;
        size_t offset = line * columns;
        size_t count = size - offset;
        if (count > columns) {
            count = columns;
        }
        if (append_format(&cursor, &remaining, "%08x  ", (unsigned)(base_offset + offset)) != BINO_OK) {
            return BINO_ERROR_NO_SPACE;
        }
        for (i = 0u; i < columns; i++) {
            if (i < count) {
                if (append_format(&cursor, &remaining, "%02x ", (unsigned)bytes[offset + i]) != BINO_OK) {
                    return BINO_ERROR_NO_SPACE;
                }
            } else if (append_text(&cursor, &remaining, "   ") != BINO_OK) {
                return BINO_ERROR_NO_SPACE;
            }
        }
        if (append_text(&cursor, &remaining, " |") != BINO_OK) {
            return BINO_ERROR_NO_SPACE;
        }
        for (i = 0u; i < count; i++) {
            uint8_t ch = bytes[offset + i];
            char tmp[2];
            tmp[0] = (ch >= 32u && ch <= 126u) ? (char)ch : '.';
            tmp[1] = '\0';
            if (append_text(&cursor, &remaining, tmp) != BINO_OK) {
                return BINO_ERROR_NO_SPACE;
            }
        }
        if (append_text(&cursor, &remaining, "|\n") != BINO_OK) {
            return BINO_ERROR_NO_SPACE;
        }
    }
    if (written) {
        *written = (size_t)(cursor - out);
    }
    return BINO_OK;
}

BinoStatus bino_hex_dump_file(FILE *file,
                              const void *data,
                              size_t size,
                              size_t base_offset,
                              unsigned columns) {
    const uint8_t *bytes = (const uint8_t *)data;
    char line[10u + BINO_MAX_HEX_COLUMNS * 3u + 3u + BINO_MAX_HEX_COLUMNS + 2u];
    size_t offset;
    if (!file || (!data && size != 0u)) {
        return BINO_ERROR_INVALID_ARGUMENT;
    }
    if (columns == 0u) {
        columns = BINO_DEFAULT_HEX_COLUMNS;
    }
    if (columns > BINO_MAX_HEX_COLUMNS) {
        columns = BINO_MAX_HEX_COLUMNS;
    }
    for (offset = 0u; offset < size; offset += columns) {
        size_t count = size - offset;
        size_t written;
        if (count > columns) {
            count = columns;
        }
        if (bino_hex_dump(bytes + offset, count, base_offset + offset,
                          columns, line, sizeof(line), &written) != BINO_OK) {
            return BINO_ERROR_NO_SPACE;
        }
        if (fwrite(line, 1u, written, file) != written) {
            return BINO_ERROR_IO;
        }
    }
    return BINO_OK;
}

BinoStatus bino_mem_read_chunk_header(BinoMemReader *reader,
                                      BinoEndian endian,
                                      BinoChunkHeader *out) {
    size_t offset;
    if (!reader || !out) {
        return set_mem_reader_error(reader, BINO_ERROR_INVALID_ARGUMENT, "missing chunk header output");
    }
    offset = reader->pos;
    if (bino_mem_read_u32(reader, endian, &out->tag) != BINO_OK ||
        bino_mem_read_u32(reader, endian, &out->size) != BINO_OK ||
        bino_mem_read_u32(reader, endian, &out->crc32) != BINO_OK) {
        return reader->error.code;
    }
    out->payload_offset = reader->pos;
    if (!bino_range_fits(out->payload_offset, (size_t)out->size, reader->size)) {
        reader->pos = offset;
        return set_mem_reader_error(reader, BINO_ERROR_BAD_LENGTH, "chunk payload exceeds buffer");
    }
    return BINO_OK;
}

BinoStatus bino_mem_read_chunk_payload(BinoMemReader *reader,
                                       const BinoChunkHeader *header,
                                       size_t max_size,
                                       int validate_crc,
                                       BinoStringView *payload) {
    const uint8_t *view;
    BinoStatus status;
    if (!reader || !header || !payload) {
        return set_mem_reader_error(reader, BINO_ERROR_INVALID_ARGUMENT, "missing chunk payload output");
    }
    if ((size_t)header->size > max_size) {
        return set_mem_reader_error(reader, BINO_ERROR_TOO_LARGE, "chunk payload exceeds limit");
    }
    if (reader->pos != header->payload_offset) {
        status = bino_mem_seek(reader, header->payload_offset);
        if (status != BINO_OK) {
            return status;
        }
    }
    status = bino_mem_read_view(reader, (size_t)header->size, &view);
    if (status != BINO_OK) {
        return status;
    }
    if (validate_crc && bino_crc32(view, (size_t)header->size) != header->crc32) {
        return set_mem_reader_error(reader, BINO_ERROR_BAD_CRC, "chunk crc mismatch");
    }
    payload->data = (const char *)view;
    payload->length = (size_t)header->size;
    return BINO_OK;
}

BinoStatus bino_mem_skip_chunk_payload(BinoMemReader *reader,
                                       const BinoChunkHeader *header,
                                       size_t max_size) {
    if (!header) {
        return set_mem_reader_error(reader, BINO_ERROR_INVALID_ARGUMENT, "missing chunk header");
    }
    if ((size_t)header->size > max_size) {
        return set_mem_reader_error(reader, BINO_ERROR_TOO_LARGE, "chunk payload exceeds limit");
    }
    if (reader && reader->pos != header->payload_offset) {
        BinoStatus status = bino_mem_seek(reader, header->payload_offset);
        if (status != BINO_OK) {
            return status;
        }
    }
    return bino_mem_skip(reader, (size_t)header->size);
}

BinoStatus bino_mem_write_chunk_header(BinoMemWriter *writer,
                                       BinoEndian endian,
                                       const BinoChunkHeader *header) {
    if (!header) {
        return set_mem_writer_error(writer, BINO_ERROR_INVALID_ARGUMENT, "missing chunk header");
    }
    if (bino_mem_write_u32(writer, endian, header->tag) != BINO_OK ||
        bino_mem_write_u32(writer, endian, header->size) != BINO_OK ||
        bino_mem_write_u32(writer, endian, header->crc32) != BINO_OK) {
        return writer ? writer->error.code : BINO_ERROR_INVALID_ARGUMENT;
    }
    return BINO_OK;
}

BinoStatus bino_mem_write_chunk(BinoMemWriter *writer,
                                BinoEndian endian,
                                uint32_t tag,
                                const void *payload,
                                size_t payload_size) {
    BinoChunkHeader header;
    BinoStatus status;
    if ((!payload && payload_size != 0u) || payload_size > (size_t)UINT32_MAX) {
        return set_mem_writer_error(writer, BINO_ERROR_INVALID_ARGUMENT, "invalid chunk payload");
    }
    header.tag = tag;
    header.size = (uint32_t)payload_size;
    header.crc32 = bino_crc32(payload, payload_size);
    header.payload_offset = writer ? writer->pos + BINO_CHUNK_HEADER_SIZE : 0u;
    status = bino_mem_write_chunk_header(writer, endian, &header);
    if (status != BINO_OK) {
        return status;
    }
    return bino_mem_write(writer, payload, payload_size);
}

BinoStatus bino_mem_begin_chunk(BinoMemWriter *writer,
                                BinoEndian endian,
                                uint32_t tag,
                                size_t *header_offset) {
    BinoChunkHeader header;
    (void)endian;
    if (!writer || !header_offset) {
        return set_mem_writer_error(writer, BINO_ERROR_INVALID_ARGUMENT, "missing chunk start output");
    }
    *header_offset = writer->pos;
    header.tag = tag;
    header.size = 0u;
    header.crc32 = 0u;
    header.payload_offset = writer->pos + BINO_CHUNK_HEADER_SIZE;
    return bino_mem_write_chunk_header(writer, endian, &header);
}

BinoStatus bino_mem_end_chunk(BinoMemWriter *writer,
                              BinoEndian endian,
                              size_t header_offset) {
    size_t payload_offset;
    size_t payload_size;
    BinoChunkHeader header;
    size_t save_pos;
    BinoStatus status;
    if (!writer || !valid_endian(endian)) {
        return set_mem_writer_error(writer, BINO_ERROR_INVALID_ARGUMENT, "invalid chunk end");
    }
    if (!bino_checked_add_size(header_offset, BINO_CHUNK_HEADER_SIZE, &payload_offset) ||
        payload_offset > writer->size || writer->pos < payload_offset) {
        return set_mem_writer_error(writer, BINO_ERROR_BAD_LENGTH, "invalid chunk header offset");
    }
    payload_size = writer->pos - payload_offset;
    if (payload_size > (size_t)UINT32_MAX) {
        return set_mem_writer_error(writer, BINO_ERROR_TOO_LARGE, "chunk payload too large");
    }
    header.tag = endian == BINO_ENDIAN_LITTLE ?
        bino_load_u32_le(writer->data + header_offset) :
        bino_load_u32_be(writer->data + header_offset);
    header.size = (uint32_t)payload_size;
    header.crc32 = bino_crc32(writer->data + payload_offset, payload_size);
    header.payload_offset = payload_offset;
    save_pos = writer->pos;
    writer->pos = header_offset;
    status = bino_mem_write_chunk_header(writer, endian, &header);
    writer->pos = save_pos;
    return status;
}

BinoStatus bino_file_read_chunk_header(BinoFileReader *reader,
                                       BinoEndian endian,
                                       BinoChunkHeader *out) {
    if (!reader || !out) {
        return set_file_reader_error(reader, BINO_ERROR_INVALID_ARGUMENT, "missing chunk header output");
    }
    if (bino_file_read_u32(reader, endian, &out->tag) != BINO_OK ||
        bino_file_read_u32(reader, endian, &out->size) != BINO_OK ||
        bino_file_read_u32(reader, endian, &out->crc32) != BINO_OK) {
        return reader->error.code;
    }
    if (bino_file_tell_reader(reader, &out->payload_offset) != BINO_OK) {
        return reader->error.code;
    }
    return BINO_OK;
}

BinoStatus bino_file_read_chunk_payload(BinoFileReader *reader,
                                        const BinoChunkHeader *header,
                                        size_t max_size,
                                        int validate_crc,
                                        void *buffer,
                                        size_t capacity,
                                        size_t *out_size) {
    if (!reader || !header || (!buffer && header->size != 0u)) {
        return set_file_reader_error(reader, BINO_ERROR_INVALID_ARGUMENT, "invalid chunk payload buffer");
    }
    if ((size_t)header->size > max_size) {
        return set_file_reader_error(reader, BINO_ERROR_TOO_LARGE, "chunk payload exceeds limit");
    }
    if ((size_t)header->size > capacity) {
        return set_file_reader_error(reader, BINO_ERROR_NO_SPACE, "chunk payload buffer too small");
    }
    if (bino_file_seek_reader(reader, header->payload_offset) != BINO_OK) {
        return reader->error.code;
    }
    if (bino_file_read(reader, buffer, (size_t)header->size) != BINO_OK) {
        return reader->error.code;
    }
    if (validate_crc && bino_crc32(buffer, (size_t)header->size) != header->crc32) {
        return set_file_reader_error(reader, BINO_ERROR_BAD_CRC, "chunk crc mismatch");
    }
    if (out_size) {
        *out_size = (size_t)header->size;
    }
    return BINO_OK;
}

BinoStatus bino_file_skip_chunk_payload(BinoFileReader *reader,
                                        const BinoChunkHeader *header,
                                        size_t max_size) {
    if (!header) {
        return set_file_reader_error(reader, BINO_ERROR_INVALID_ARGUMENT, "missing chunk header");
    }
    if ((size_t)header->size > max_size) {
        return set_file_reader_error(reader, BINO_ERROR_TOO_LARGE, "chunk payload exceeds limit");
    }
    if (bino_file_seek_reader(reader, header->payload_offset) != BINO_OK) {
        return reader ? reader->error.code : BINO_ERROR_INVALID_ARGUMENT;
    }
    return bino_file_skip_reader(reader, (size_t)header->size);
}

BinoStatus bino_file_write_chunk_header(BinoFileWriter *writer,
                                        BinoEndian endian,
                                        const BinoChunkHeader *header) {
    if (!header) {
        return set_file_writer_error(writer, BINO_ERROR_INVALID_ARGUMENT, "missing chunk header");
    }
    if (bino_file_write_u32(writer, endian, header->tag) != BINO_OK ||
        bino_file_write_u32(writer, endian, header->size) != BINO_OK ||
        bino_file_write_u32(writer, endian, header->crc32) != BINO_OK) {
        return writer ? writer->error.code : BINO_ERROR_INVALID_ARGUMENT;
    }
    return BINO_OK;
}

BinoStatus bino_file_write_chunk(BinoFileWriter *writer,
                                 BinoEndian endian,
                                 uint32_t tag,
                                 const void *payload,
                                 size_t payload_size) {
    BinoChunkHeader header;
    BinoStatus status;
    if ((!payload && payload_size != 0u) || payload_size > (size_t)UINT32_MAX) {
        return set_file_writer_error(writer, BINO_ERROR_INVALID_ARGUMENT, "invalid chunk payload");
    }
    header.tag = tag;
    header.size = (uint32_t)payload_size;
    header.crc32 = bino_crc32(payload, payload_size);
    header.payload_offset = 0u;
    status = bino_file_write_chunk_header(writer, endian, &header);
    if (status != BINO_OK) {
        return status;
    }
    return bino_file_write(writer, payload, payload_size);
}
