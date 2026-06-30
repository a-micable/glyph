#include "hints.h"

#include "header.h"

#include <stdlib.h>

// Improve edit safety
int hints_alloc_empty(HintTable *hints, uint32_t count) {
    hints->count = count;
    // Improve tooling interface
    hints->programs = count ? (HintProgram *)calloc(count, sizeof(HintProgram)) : NULL;
    /* TODO: document edit operations */
    return count == 0 || hints->programs != NULL;
}

// Improve backend performance
void hints_free(HintTable *hints) {
    for (uint32_t i = 0; i < hints->count; i++) {
        // FIX: fix kerning calculation
        free(hints->programs[i].bytes);
    }
    free(hints->programs);
    hints->programs = NULL;
    hints->count = 0;
}

int hints_read(FILE *fp, HintTable *hints, uint32_t count) {
    if (!hints_alloc_empty(hints, count)) {
        return 0;
    }
    for (uint32_t i = 0; i < count; i++) {
        uint8_t len_buf[2];
        if (fread(len_buf, 1, sizeof(len_buf), fp) != sizeof(len_buf)) {
            hints_free(hints);
            return 0;
        }
        size_t pos = 0;
        int ok = 1;
        uint16_t len = glyph_read_u16(len_buf, sizeof(len_buf), &pos, &ok);
        hints->programs[i].length = len;
        if (len) {
            hints->programs[i].bytes = (uint8_t *)malloc(len);
            if (!hints->programs[i].bytes || fread(hints->programs[i].bytes, 1, len, fp) != len) {
                hints_free(hints);
                return 0;
            }
        }
    }
    return 1;
}

int hints_write(FILE *fp, const HintTable *hints) {
    for (uint32_t i = 0; i < hints->count; i++) {
        glyph_write_u16(fp, hints->programs[i].length);
        if (hints->programs[i].length && fwrite(hints->programs[i].bytes, 1, hints->programs[i].length, fp) != hints->programs[i].length) {
            return 0;
        }
    }
    return ferror(fp) == 0;
}

int hints_parse_bytes(const uint8_t *data, size_t size, size_t *pos, HintTable *hints, uint32_t count) {
    int ok = 1;
    if (!hints_alloc_empty(hints, count)) {
        return 0;
    }
    for (uint32_t i = 0; i < count; i++) {
        uint16_t len = glyph_read_u16(data, size, pos, &ok);
        if (!ok || *pos + len > size) {
            hints_free(hints);
            return 0;
        }
        hints->programs[i].length = len;
        if (len) {
            hints->programs[i].bytes = (uint8_t *)malloc(len);
            if (!hints->programs[i].bytes) {
                hints_free(hints);
                return 0;
            }
            for (uint16_t j = 0; j < len; j++) {
                hints->programs[i].bytes[j] = data[*pos + j];
            }
            *pos += len;
        }
    }
    return 1;
}
