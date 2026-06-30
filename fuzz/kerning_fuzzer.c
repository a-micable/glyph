#include "../src/reload.h"

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 6) {
        return 0;
    }

    uint32_t first_size = (uint32_t)data[0] |
                          ((uint32_t)data[1] << 8) |
                          ((uint32_t)data[2] << 16) |
                          ((uint32_t)data[3] << 24);
    first_size %= (uint32_t)(size - 4);
    if (first_size == 0 || 4 + first_size >= size) {
        return 0;
    }

    const uint8_t *first = data + 4;
    const uint8_t *second = data + 4 + first_size;
    size_t second_size = size - 4 - first_size;
    (void)glyph_reload_sequence_resolve(first, first_size, second, second_size);
    return 0;
}
