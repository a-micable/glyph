#ifndef GLYPH_LOGGING_H
#define GLYPH_LOGGING_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GLYPH_LOG_TRACE = 0,
    GLYPH_LOG_DEBUG,
    GLYPH_LOG_INFO,
    GLYPH_LOG_NOTE,
    GLYPH_LOG_WARNING,
    GLYPH_LOG_ERROR,
    GLYPH_LOG_FATAL,
    GLYPH_LOG_LEVEL_COUNT
} GlyphLogLevel;

typedef enum {
    GLYPH_LOG_FORMAT_TEXT = 0,
    GLYPH_LOG_FORMAT_JSON
} GlyphLogFormat;

typedef struct {
    FILE *out;
    GlyphLogLevel min_level;
    GlyphLogFormat format;
    int initialized;
} GlyphLogger;

int glyph_logger_init(GlyphLogger *logger,
                      FILE *out,
                      GlyphLogLevel min_level,
                      GlyphLogFormat format);
int glyph_logger_log(const GlyphLogger *logger,
                     GlyphLogLevel level,
                     const char *format,
                     ...);
const char *glyph_log_level_name(GlyphLogLevel level);
GlyphLogLevel glyph_log_level_from_name(const char *name, int *ok);
int glyph_log_format_from_name(const char *name, GlyphLogFormat *format);

#ifdef __cplusplus
}
#endif

#endif
