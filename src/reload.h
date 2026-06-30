#ifndef GLYPH_RELOAD_H
#define GLYPH_RELOAD_H

#include "atlas.h"
#include "row_alloc.h"

#include <stddef.h>
#include <stdint.h>

int glyph_reload_table_into_allocator(RowAllocator *alloc, const uint8_t *data, size_t size, GlyphFile *revision);
int glyph_reload_sequence_resolve(const uint8_t *first, size_t first_size, const uint8_t *second, size_t second_size);

#endif
