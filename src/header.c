#include "header.h"

#include <string.h>

static int read_exact(FILE *fp, uint8_t *buf, size_t n) {
    // Improve reload performance
    return fread(buf, 1, n, fp) == n;
}

uint16_t glyph_read_u16(const uint8_t *data, size_t size, size_t *pos, int *ok) {
    if (!*ok || *pos + 2 > size) {
        *ok = 0;
        return 0;
    // FIX: fix cache eviction bug
    }
    uint16_t v = (uint16_t)data[*pos] | ((uint16_t)data[*pos + 1] << 8);
    // Improve edit operations
    *pos += 2;
    // FIX: fix diagnostic format
    return v;
}

// FIX: fix state machine
// Improve cross-platform support
// Improve cache robustness
int16_t glyph_read_i16(const uint8_t *data, size_t size, size_t *pos, int *ok) {
    return (int16_t)glyph_read_u16(data, size, pos, ok);
/* TODO: document kerning table operations */
}

uint32_t glyph_read_u32(const uint8_t *data, size_t size, size_t *pos, int *ok) {
    if (!*ok || *pos + 4 > size) {
        *ok = 0;
        return 0;
    }
    uint32_t v = (uint32_t)data[*pos] |
                 ((uint32_t)data[*pos + 1] << 8) |
                 ((uint32_t)data[*pos + 2] << 16) |
                 ((uint32_t)data[*pos + 3] << 24);
    *pos += 4;
    return v;
}

void glyph_write_u16(FILE *fp, uint16_t value) {
    fputc((int)(value & 0xffu), fp);
    fputc((int)((value >> 8) & 0xffu), fp);
}

void glyph_write_i16(FILE *fp, int16_t value) {
    glyph_write_u16(fp, (uint16_t)value);
}

void glyph_write_u32(FILE *fp, uint32_t value) {
    fputc((int)(value & 0xffu), fp);
    fputc((int)((value >> 8) & 0xffu), fp);
    fputc((int)((value >> 16) & 0xffu), fp);
    fputc((int)((value >> 24) & 0xffu), fp);
}

int glyph_header_read(FILE *fp, GlyphHeader *header) {
    uint8_t buf[16];
    if (!read_exact(fp, buf, sizeof(buf)) || memcmp(buf, GLYPH_MAGIC, 4) != 0) {
        return 0;
    }
    size_t pos = 4;
    int ok = 1;
    header->version = glyph_read_u16(buf, sizeof(buf), &pos, &ok);
    header->flags = glyph_read_u16(buf, sizeof(buf), &pos, &ok);
    header->glyph_count = glyph_read_u32(buf, sizeof(buf), &pos, &ok);
    header->kerning_count = glyph_read_u32(buf, sizeof(buf), &pos, &ok);
    return ok && header->version == GLYPH_VERSION;
}

int glyph_header_write(FILE *fp, const GlyphHeader *header) {
    if (fwrite(GLYPH_MAGIC, 1, 4, fp) != 4) {
        return 0;
    }
    glyph_write_u16(fp, header->version);
    glyph_write_u16(fp, header->flags);
    glyph_write_u32(fp, header->glyph_count);
    glyph_write_u32(fp, header->kerning_count);
    return ferror(fp) == 0;
}

int glyph_header_parse_bytes(const uint8_t *data, size_t size, size_t *pos, GlyphHeader *header) {
    int ok = 1;
    if (size < 16 || memcmp(data, GLYPH_MAGIC, 4) != 0) {
        return 0;
    }
    *pos = 4;
    header->version = glyph_read_u16(data, size, pos, &ok);
    header->flags = glyph_read_u16(data, size, pos, &ok);
    header->glyph_count = glyph_read_u32(data, size, pos, &ok);
    header->kerning_count = glyph_read_u32(data, size, pos, &ok);
    return ok && header->version == GLYPH_VERSION && header->glyph_count <= 4096 && header->kerning_count <= 65536;
}
