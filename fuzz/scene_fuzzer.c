#include "../src/scene.h"

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    (void)glyph_scene_run_document(data, size);
    return 0;
}
