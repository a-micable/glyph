#include "version.h"

#ifndef GLYPH_BUILD_INFO
#define GLYPH_BUILD_INFO __DATE__ " " __TIME__
#endif

#ifndef GLYPH_BUILD_TARGET
#define GLYPH_BUILD_TARGET "local"
#endif

const char *glyph_version_string(void) {
    return GLYPH_VERSION_STRING;
}

const char *glyph_build_info(void) {
    return GLYPH_BUILD_INFO;
}

const char *glyph_build_target(void) {
    return GLYPH_BUILD_TARGET;
}
