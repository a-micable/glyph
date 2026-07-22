#include "logging.h"

#include <stdarg.h>
#include <string.h>
#include <time.h>

static const char *glyph_log_level_names[GLYPH_LOG_LEVEL_COUNT] = {
    "trace",
    "debug",
    "info",
    "note",
    "warning",
    "error",
    "fatal"
};

static int glyph_logger_write_timestamp(const GlyphLogger *logger) {
    time_t now = time(NULL);
    struct tm local_time;
#if defined(_MSC_VER)
    localtime_s(&local_time, &now);
#else
    localtime_r(&now, &local_time);
#endif
    char buffer[32];
    if (strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S%z", &local_time) == 0) {
        return 0;
    }
    return fprintf(logger->out, "%s", buffer) >= 0;
}

int glyph_logger_init(GlyphLogger *logger,
                      FILE *out,
                      GlyphLogLevel min_level,
                      GlyphLogFormat format) {
    if (!logger || !out) {
        return 0;
    }
    logger->out = out;
    logger->min_level = min_level;
    logger->format = format;
    logger->initialized = 1;
    return 1;
}

int glyph_logger_log(const GlyphLogger *logger,
                     GlyphLogLevel level,
                     const char *format,
                     ...) {
    if (!logger || !logger->initialized || level < logger->min_level) {
        return 0;
    }

    va_list args;
    va_start(args, format);

    int result = 1;
    if (logger->format == GLYPH_LOG_FORMAT_JSON) {
        if (fprintf(logger->out, "{\"level\":\"%s\",\"message\":\"",
                    glyph_log_level_names[level]) < 0) {
            result = 0;
        } else {
            const char *p = format;
            while (*p && result) {
                if (*p == '"' || *p == '\\') {
                    result = fprintf(logger->out, "\\%c", *p) >= 0;
                } else {
                    result = fprintf(logger->out, "%c", *p) >= 0;
                }
                p++;
            }
            if (result) {
                result = fprintf(logger->out, "\"}
") >= 0;
            }
        }
    } else {
        if (!glyph_logger_write_timestamp(logger)) {
            result = 0;
        }
        if (result) {
            result = fprintf(logger->out, " [%s] ", glyph_log_level_names[level]) >= 0;
        }
        if (result) {
            result = vfprintf(logger->out, format, args) >= 0;
        }
        if (result) {
            result = fprintf(logger->out, "\n") >= 0;
        }
    }

    va_end(args);
    fflush(logger->out);
    return result;
}

const char *glyph_log_level_name(GlyphLogLevel level) {
    if (level < 0 || level >= GLYPH_LOG_LEVEL_COUNT) {
        return "unknown";
    }
    return glyph_log_level_names[level];
}

GlyphLogLevel glyph_log_level_from_name(const char *name, int *ok) {
    if (!name) {
        if (ok) *ok = 0;
        return GLYPH_LOG_INFO;
    }
    for (size_t i = 0; i < sizeof(glyph_log_level_names) / sizeof(glyph_log_level_names[0]); ++i) {
        if (strcasecmp(name, glyph_log_level_names[i]) == 0) {
            if (ok) *ok = 1;
            return (GlyphLogLevel)i;
        }
    }
    if (ok) *ok = 0;
    return GLYPH_LOG_INFO;
}

int glyph_log_format_from_name(const char *name, GlyphLogFormat *format) {
    if (!name || !format) {
        return 0;
    }
    if (strcasecmp(name, "json") == 0) {
        *format = GLYPH_LOG_FORMAT_JSON;
        return 1;
    }
    if (strcasecmp(name, "text") == 0) {
        *format = GLYPH_LOG_FORMAT_TEXT;
        return 1;
    }
    return 0;
}
