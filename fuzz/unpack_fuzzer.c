#include "../src/atlas.h"

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    (void)glyph_unpack(data, size);
    return 0;
}
