#ifndef GLYPH_BINIO_H
#define GLYPH_BINIO_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BINO_CHUNK_HEADER_SIZE 12u
#define BINO_DEFAULT_HEX_COLUMNS 16u
#define BINO_MAX_HEX_COLUMNS 32u
#define BINO_ERROR_MESSAGE_SIZE 128u

typedef enum {
    BINO_OK = 0,
    BINO_ERROR_INVALID_ARGUMENT,
    BINO_ERROR_OUT_OF_BOUNDS,
    BINO_ERROR_EOF,
    BINO_ERROR_IO,
    BINO_ERROR_OVERFLOW,
    BINO_ERROR_SEEK,
    BINO_ERROR_TELL,
    BINO_ERROR_FORMAT,
    BINO_ERROR_BAD_MAGIC,
    BINO_ERROR_BAD_LENGTH,
    BINO_ERROR_BAD_CRC,
    BINO_ERROR_NO_SPACE,
    BINO_ERROR_TOO_LARGE
} BinoStatus;

typedef enum {
    BINO_ENDIAN_LITTLE = 0,
    BINO_ENDIAN_BIG = 1
} BinoEndian;

typedef struct {
    BinoStatus code;
    size_t offset;
    int system_error;
    char message[BINO_ERROR_MESSAGE_SIZE];
} BinoError;

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t pos;
    BinoError error;
} BinoMemReader;

typedef struct {
    uint8_t *data;
    size_t capacity;
    size_t size;
    size_t pos;
    BinoError error;
} BinoMemWriter;

typedef struct {
    FILE *file;
    BinoError error;
} BinoFileReader;

typedef struct {
    FILE *file;
    BinoError error;
} BinoFileWriter;

typedef struct {
    const char *data;
    size_t length;
} BinoStringView;

typedef struct {
    uint32_t rows;
    uint32_t columns;
    uint32_t cell_size;
    uint32_t bytes;
} BinoTableHeader;

typedef struct {
    uint32_t tag;
    uint32_t size;
    uint32_t crc32;
    size_t payload_offset;
} BinoChunkHeader;

typedef struct {
    uint32_t state;
} BinoCrc32;

const char *bino_status_string(BinoStatus status);
int bino_status_failed(BinoStatus status);

void bino_error_clear(BinoError *error);
void bino_error_set(BinoError *error,
                    BinoStatus code,
                    size_t offset,
                    int system_error,
                    const char *message);
void bino_error_copy(BinoError *dst, const BinoError *src);

uint16_t bino_load_u16_le(const void *src);
uint16_t bino_load_u16_be(const void *src);
uint32_t bino_load_u24_le(const void *src);
uint32_t bino_load_u24_be(const void *src);
uint32_t bino_load_u32_le(const void *src);
uint32_t bino_load_u32_be(const void *src);
uint64_t bino_load_u64_le(const void *src);
uint64_t bino_load_u64_be(const void *src);

void bino_store_u16_le(void *dst, uint16_t value);
void bino_store_u16_be(void *dst, uint16_t value);
void bino_store_u24_le(void *dst, uint32_t value);
void bino_store_u24_be(void *dst, uint32_t value);
void bino_store_u32_le(void *dst, uint32_t value);
void bino_store_u32_be(void *dst, uint32_t value);
void bino_store_u64_le(void *dst, uint64_t value);
void bino_store_u64_be(void *dst, uint64_t value);

uint16_t bino_swap_u16(uint16_t value);
uint32_t bino_swap_u32(uint32_t value);
uint64_t bino_swap_u64(uint64_t value);

uint32_t bino_fourcc(char a, char b, char c, char d);
void bino_fourcc_to_string(uint32_t tag, char out[5]);

int bino_checked_add_size(size_t a, size_t b, size_t *out);
int bino_checked_mul_size(size_t a, size_t b, size_t *out);
int bino_range_fits(size_t offset, size_t length, size_t size);

void bino_mem_reader_init(BinoMemReader *reader, const void *data, size_t size);
BinoStatus bino_mem_reader_status(const BinoMemReader *reader);
const BinoError *bino_mem_reader_error(const BinoMemReader *reader);
size_t bino_mem_tell(const BinoMemReader *reader);
size_t bino_mem_remaining(const BinoMemReader *reader);
int bino_mem_eof(const BinoMemReader *reader);
BinoStatus bino_mem_seek(BinoMemReader *reader, size_t offset);
BinoStatus bino_mem_skip(BinoMemReader *reader, size_t count);
BinoStatus bino_mem_align(BinoMemReader *reader, size_t alignment);
BinoStatus bino_mem_read(BinoMemReader *reader, void *dst, size_t count);
BinoStatus bino_mem_peek(const BinoMemReader *reader, void *dst, size_t count);
BinoStatus bino_mem_subreader(BinoMemReader *reader, size_t count, BinoMemReader *out);
BinoStatus bino_mem_read_view(BinoMemReader *reader, size_t count, const uint8_t **out);
BinoStatus bino_mem_read_u8(BinoMemReader *reader, uint8_t *out);
BinoStatus bino_mem_read_i8(BinoMemReader *reader, int8_t *out);
BinoStatus bino_mem_read_u16(BinoMemReader *reader, BinoEndian endian, uint16_t *out);
BinoStatus bino_mem_read_i16(BinoMemReader *reader, BinoEndian endian, int16_t *out);
BinoStatus bino_mem_read_u24(BinoMemReader *reader, BinoEndian endian, uint32_t *out);
BinoStatus bino_mem_read_u32(BinoMemReader *reader, BinoEndian endian, uint32_t *out);
BinoStatus bino_mem_read_i32(BinoMemReader *reader, BinoEndian endian, int32_t *out);
BinoStatus bino_mem_read_u64(BinoMemReader *reader, BinoEndian endian, uint64_t *out);
BinoStatus bino_mem_read_i64(BinoMemReader *reader, BinoEndian endian, int64_t *out);
BinoStatus bino_mem_expect(BinoMemReader *reader, const void *bytes, size_t count);

BinoStatus bino_mem_read_string_u8(BinoMemReader *reader,
                                   size_t max_length,
                                   BinoStringView *out);
BinoStatus bino_mem_read_string_u16(BinoMemReader *reader,
                                    BinoEndian endian,
                                    size_t max_length,
                                    BinoStringView *out);
BinoStatus bino_mem_read_string_u32(BinoMemReader *reader,
                                    BinoEndian endian,
                                    size_t max_length,
                                    BinoStringView *out);
BinoStatus bino_mem_read_cstring(BinoMemReader *reader,
                                 size_t max_scan,
                                 BinoStringView *out);
BinoStatus bino_mem_read_table_header(BinoMemReader *reader,
                                      BinoEndian endian,
                                      uint32_t max_rows,
                                      uint32_t max_columns,
                                      uint32_t max_cell_size,
                                      uint32_t max_bytes,
                                      BinoTableHeader *out);

void bino_mem_writer_init(BinoMemWriter *writer, void *data, size_t capacity);
BinoStatus bino_mem_writer_status(const BinoMemWriter *writer);
const BinoError *bino_mem_writer_error(const BinoMemWriter *writer);
size_t bino_mem_writer_tell(const BinoMemWriter *writer);
size_t bino_mem_writer_size(const BinoMemWriter *writer);
size_t bino_mem_writer_capacity(const BinoMemWriter *writer);
size_t bino_mem_writer_remaining(const BinoMemWriter *writer);
BinoStatus bino_mem_writer_seek(BinoMemWriter *writer, size_t offset);
BinoStatus bino_mem_writer_align(BinoMemWriter *writer, size_t alignment, uint8_t pad);
BinoStatus bino_mem_write(BinoMemWriter *writer, const void *src, size_t count);
BinoStatus bino_mem_write_zeroes(BinoMemWriter *writer, size_t count);
BinoStatus bino_mem_write_repeat(BinoMemWriter *writer, uint8_t value, size_t count);
BinoStatus bino_mem_write_u8(BinoMemWriter *writer, uint8_t value);
BinoStatus bino_mem_write_i8(BinoMemWriter *writer, int8_t value);
BinoStatus bino_mem_write_u16(BinoMemWriter *writer, BinoEndian endian, uint16_t value);
BinoStatus bino_mem_write_i16(BinoMemWriter *writer, BinoEndian endian, int16_t value);
BinoStatus bino_mem_write_u24(BinoMemWriter *writer, BinoEndian endian, uint32_t value);
BinoStatus bino_mem_write_u32(BinoMemWriter *writer, BinoEndian endian, uint32_t value);
BinoStatus bino_mem_write_i32(BinoMemWriter *writer, BinoEndian endian, int32_t value);
BinoStatus bino_mem_write_u64(BinoMemWriter *writer, BinoEndian endian, uint64_t value);
BinoStatus bino_mem_write_i64(BinoMemWriter *writer, BinoEndian endian, int64_t value);
BinoStatus bino_mem_write_string_u8(BinoMemWriter *writer, const void *data, size_t length);
BinoStatus bino_mem_write_string_u16(BinoMemWriter *writer,
                                     BinoEndian endian,
                                     const void *data,
                                     size_t length);
BinoStatus bino_mem_write_string_u32(BinoMemWriter *writer,
                                     BinoEndian endian,
                                     const void *data,
                                     size_t length);
BinoStatus bino_mem_write_cstring(BinoMemWriter *writer, const char *text);
BinoStatus bino_mem_write_table_header(BinoMemWriter *writer,
                                       BinoEndian endian,
                                       const BinoTableHeader *header);

void bino_file_reader_init(BinoFileReader *reader, FILE *file);
BinoStatus bino_file_reader_status(const BinoFileReader *reader);
const BinoError *bino_file_reader_error(const BinoFileReader *reader);
BinoStatus bino_file_tell_reader(BinoFileReader *reader, size_t *out);
BinoStatus bino_file_seek_reader(BinoFileReader *reader, size_t offset);
BinoStatus bino_file_skip_reader(BinoFileReader *reader, size_t count);
BinoStatus bino_file_align_reader(BinoFileReader *reader, size_t alignment);
BinoStatus bino_file_read(BinoFileReader *reader, void *dst, size_t count);
BinoStatus bino_file_read_u8(BinoFileReader *reader, uint8_t *out);
BinoStatus bino_file_read_i8(BinoFileReader *reader, int8_t *out);
BinoStatus bino_file_read_u16(BinoFileReader *reader, BinoEndian endian, uint16_t *out);
BinoStatus bino_file_read_i16(BinoFileReader *reader, BinoEndian endian, int16_t *out);
BinoStatus bino_file_read_u24(BinoFileReader *reader, BinoEndian endian, uint32_t *out);
BinoStatus bino_file_read_u32(BinoFileReader *reader, BinoEndian endian, uint32_t *out);
BinoStatus bino_file_read_i32(BinoFileReader *reader, BinoEndian endian, int32_t *out);
BinoStatus bino_file_read_u64(BinoFileReader *reader, BinoEndian endian, uint64_t *out);
BinoStatus bino_file_read_i64(BinoFileReader *reader, BinoEndian endian, int64_t *out);
BinoStatus bino_file_expect(BinoFileReader *reader, const void *bytes, size_t count);
BinoStatus bino_file_read_string_u8(BinoFileReader *reader,
                                    size_t max_length,
                                    char *buffer,
                                    size_t capacity,
                                    size_t *out_length);
BinoStatus bino_file_read_string_u16(BinoFileReader *reader,
                                     BinoEndian endian,
                                     size_t max_length,
                                     char *buffer,
                                     size_t capacity,
                                     size_t *out_length);
BinoStatus bino_file_read_string_u32(BinoFileReader *reader,
                                     BinoEndian endian,
                                     size_t max_length,
                                     char *buffer,
                                     size_t capacity,
                                     size_t *out_length);
BinoStatus bino_file_read_cstring(BinoFileReader *reader,
                                  size_t max_scan,
                                  char *buffer,
                                  size_t capacity,
                                  size_t *out_length);
BinoStatus bino_file_read_table_header(BinoFileReader *reader,
                                       BinoEndian endian,
                                       uint32_t max_rows,
                                       uint32_t max_columns,
                                       uint32_t max_cell_size,
                                       uint32_t max_bytes,
                                       BinoTableHeader *out);

void bino_file_writer_init(BinoFileWriter *writer, FILE *file);
BinoStatus bino_file_writer_status(const BinoFileWriter *writer);
const BinoError *bino_file_writer_error(const BinoFileWriter *writer);
BinoStatus bino_file_tell_writer(BinoFileWriter *writer, size_t *out);
BinoStatus bino_file_seek_writer(BinoFileWriter *writer, size_t offset);
BinoStatus bino_file_align_writer(BinoFileWriter *writer, size_t alignment, uint8_t pad);
BinoStatus bino_file_write(BinoFileWriter *writer, const void *src, size_t count);
BinoStatus bino_file_write_zeroes(BinoFileWriter *writer, size_t count);
BinoStatus bino_file_write_repeat(BinoFileWriter *writer, uint8_t value, size_t count);
BinoStatus bino_file_write_u8(BinoFileWriter *writer, uint8_t value);
BinoStatus bino_file_write_i8(BinoFileWriter *writer, int8_t value);
BinoStatus bino_file_write_u16(BinoFileWriter *writer, BinoEndian endian, uint16_t value);
BinoStatus bino_file_write_i16(BinoFileWriter *writer, BinoEndian endian, int16_t value);
BinoStatus bino_file_write_u24(BinoFileWriter *writer, BinoEndian endian, uint32_t value);
BinoStatus bino_file_write_u32(BinoFileWriter *writer, BinoEndian endian, uint32_t value);
BinoStatus bino_file_write_i32(BinoFileWriter *writer, BinoEndian endian, int32_t value);
BinoStatus bino_file_write_u64(BinoFileWriter *writer, BinoEndian endian, uint64_t value);
BinoStatus bino_file_write_i64(BinoFileWriter *writer, BinoEndian endian, int64_t value);
BinoStatus bino_file_write_string_u8(BinoFileWriter *writer, const void *data, size_t length);
BinoStatus bino_file_write_string_u16(BinoFileWriter *writer,
                                      BinoEndian endian,
                                      const void *data,
                                      size_t length);
BinoStatus bino_file_write_string_u32(BinoFileWriter *writer,
                                      BinoEndian endian,
                                      const void *data,
                                      size_t length);
BinoStatus bino_file_write_cstring(BinoFileWriter *writer, const char *text);
BinoStatus bino_file_write_table_header(BinoFileWriter *writer,
                                        BinoEndian endian,
                                        const BinoTableHeader *header);

BinoStatus bino_file_tell_checked(FILE *file, size_t *out, BinoError *error);
BinoStatus bino_file_seek_checked(FILE *file, size_t offset, BinoError *error);
BinoStatus bino_file_skip_checked(FILE *file, size_t count, BinoError *error);

void bino_crc32_init(BinoCrc32 *crc);
void bino_crc32_update(BinoCrc32 *crc, const void *data, size_t size);
uint32_t bino_crc32_final(const BinoCrc32 *crc);
uint32_t bino_crc32(const void *data, size_t size);
uint32_t bino_crc32_update_value(uint32_t seed, const void *data, size_t size);

size_t bino_hex_dump_bound(size_t size, unsigned columns);
BinoStatus bino_hex_dump(const void *data,
                         size_t size,
                         size_t base_offset,
                         unsigned columns,
                         char *out,
                         size_t out_capacity,
                         size_t *written);
BinoStatus bino_hex_dump_file(FILE *file,
                              const void *data,
                              size_t size,
                              size_t base_offset,
                              unsigned columns);

BinoStatus bino_mem_read_chunk_header(BinoMemReader *reader,
                                      BinoEndian endian,
                                      BinoChunkHeader *out);
BinoStatus bino_mem_read_chunk_payload(BinoMemReader *reader,
                                       const BinoChunkHeader *header,
                                       size_t max_size,
                                       int validate_crc,
                                       BinoStringView *payload);
BinoStatus bino_mem_skip_chunk_payload(BinoMemReader *reader,
                                       const BinoChunkHeader *header,
                                       size_t max_size);
BinoStatus bino_mem_write_chunk_header(BinoMemWriter *writer,
                                       BinoEndian endian,
                                       const BinoChunkHeader *header);
BinoStatus bino_mem_write_chunk(BinoMemWriter *writer,
                                BinoEndian endian,
                                uint32_t tag,
                                const void *payload,
                                size_t payload_size);
BinoStatus bino_mem_begin_chunk(BinoMemWriter *writer,
                                BinoEndian endian,
                                uint32_t tag,
                                size_t *header_offset);
BinoStatus bino_mem_end_chunk(BinoMemWriter *writer,
                              BinoEndian endian,
                              size_t header_offset);

BinoStatus bino_file_read_chunk_header(BinoFileReader *reader,
                                       BinoEndian endian,
                                       BinoChunkHeader *out);
BinoStatus bino_file_read_chunk_payload(BinoFileReader *reader,
                                        const BinoChunkHeader *header,
                                        size_t max_size,
                                        int validate_crc,
                                        void *buffer,
                                        size_t capacity,
                                        size_t *out_size);
BinoStatus bino_file_skip_chunk_payload(BinoFileReader *reader,
                                        const BinoChunkHeader *header,
                                        size_t max_size);
BinoStatus bino_file_write_chunk_header(BinoFileWriter *writer,
                                        BinoEndian endian,
                                        const BinoChunkHeader *header);
BinoStatus bino_file_write_chunk(BinoFileWriter *writer,
                                 BinoEndian endian,
                                 uint32_t tag,
                                 const void *payload,
                                 size_t payload_size);

#ifdef __cplusplus
}
#endif

#endif
