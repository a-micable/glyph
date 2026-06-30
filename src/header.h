#ifndef GLYPH_HEADER_H
#define GLYPH_HEADER_H

#include <stdint.h>
#include <stdio.h>

#define GLYPH_MAGIC "GLYP"
#define GLYPH_VERSION 1u
#define GLYPH_FLAG_HINTS 0x0001u

typedef struct {
    uint16_t version;
    uint16_t flags;
    uint32_t glyph_count;
    uint32_t kerning_count;
} GlyphHeader;

int glyph_header_read(FILE *fp, GlyphHeader *header);
int glyph_header_write(FILE *fp, const GlyphHeader *header);
int glyph_header_parse_bytes(const uint8_t *data, size_t size, size_t *pos, GlyphHeader *header);

uint16_t glyph_read_u16(const uint8_t *data, size_t size, size_t *pos, int *ok);
int16_t glyph_read_i16(const uint8_t *data, size_t size, size_t *pos, int *ok);
uint32_t glyph_read_u32(const uint8_t *data, size_t size, size_t *pos, int *ok);
void glyph_write_u16(FILE *fp, uint16_t value);
void glyph_write_i16(FILE *fp, int16_t value);
void glyph_write_u32(FILE *fp, uint32_t value);

#endif
