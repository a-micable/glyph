#ifndef GLYPH_VERSION_H
#define GLYPH_VERSION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GLYPH_VERSION_MAJOR 1u
#define GLYPH_VERSION_MINOR 2u
#define GLYPH_VERSION_PATCH 0u
#define GLYPH_VERSION_STRING "1.2.0"

const char *glyph_version_string(void);
const char *glyph_build_info(void);
const char *glyph_build_target(void);

#ifdef __cplusplus
}
#endif

#endif
