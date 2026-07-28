#ifndef GLYPH_SCENE_H
#define GLYPH_SCENE_H

#include <stddef.h>
#include <stdint.h>

typedef struct GlyphScene GlyphScene;

typedef enum {
    GLYPH_SCENE_RESULT_REJECT = 0,
    GLYPH_SCENE_RESULT_OK = 1
} GlyphSceneResult;

GlyphSceneResult glyph_scene_run_document(const uint8_t *data, size_t size);

#endif
