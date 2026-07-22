#ifndef GLYPH_CONFIG_H
#define GLYPH_CONFIG_H

#include "logging.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    GlyphLogLevel log_level;
    GlyphLogFormat log_format;
    int verbose;
    const char *output_dir;
} GlyphConfig;

int glyph_config_init(GlyphConfig *config);
int glyph_config_load_env(GlyphConfig *config);
int glyph_config_parse_args(GlyphConfig *config,
                           int *argc,
                           char ***argv,
                           char *error,
                           size_t error_cap);

#ifdef __cplusplus
}
#endif

#endif
