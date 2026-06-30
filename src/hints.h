#ifndef GLYPH_HINTS_H
#define GLYPH_HINTS_H

#include <stdint.h>
#include <stdio.h>

typedef struct {
    uint16_t length;
    uint8_t *bytes;
} HintProgram;

typedef struct {
    uint32_t count;
    HintProgram *programs;
} HintTable;

int hints_alloc_empty(HintTable *hints, uint32_t count);
void hints_free(HintTable *hints);
int hints_read(FILE *fp, HintTable *hints, uint32_t count);
int hints_write(FILE *fp, const HintTable *hints);
int hints_parse_bytes(const uint8_t *data, size_t size, size_t *pos, HintTable *hints, uint32_t count);

#endif
