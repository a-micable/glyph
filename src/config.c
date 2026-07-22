#include "config.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static int glyph_config_parse_bool(const char *value, int *result) {
    if (!value || !result) {
        return 0;
    }
    if (strcasecmp(value, "1") == 0 || strcasecmp(value, "true") == 0 || strcasecmp(value, "yes") == 0 || strcasecmp(value, "on") == 0) {
        *result = 1;
        return 1;
    }
    if (strcasecmp(value, "0") == 0 || strcasecmp(value, "false") == 0 || strcasecmp(value, "no") == 0 || strcasecmp(value, "off") == 0) {
        *result = 0;
        return 1;
    }
    return 0;
}

int glyph_config_init(GlyphConfig *config) {
    if (!config) {
        return 0;
    }
    config->log_level = GLYPH_LOG_INFO;
    config->log_format = GLYPH_LOG_FORMAT_TEXT;
    config->verbose = 0;
    config->output_dir = NULL;
    return 1;
}

int glyph_config_load_env(GlyphConfig *config) {
    if (!config) {
        return 0;
    }

    const char *level = getenv("GLYPH_LOG_LEVEL");
    if (level) {
        int ok;
        GlyphLogLevel parsed = glyph_log_level_from_name(level, &ok);
        if (ok) {
            config->log_level = parsed;
        }
    }

    const char *format = getenv("GLYPH_LOG_FORMAT");
    if (format) {
        GlyphLogFormat parsed;
        if (glyph_log_format_from_name(format, &parsed)) {
            config->log_format = parsed;
        }
    }

    const char *verbose = getenv("GLYPH_VERBOSE");
    if (verbose) {
        int value;
        if (glyph_config_parse_bool(verbose, &value)) {
            config->verbose = value;
        }
    }

    config->output_dir = getenv("GLYPH_OUTPUT_DIR");
    return 1;
}

int glyph_config_parse_args(GlyphConfig *config,
                           int *argc,
                           char ***argv,
                           char *error,
                           size_t error_cap) {
    if (!config || !argc || !argv) {
        return 0;
    }

    int write_index = 0;
    for (int read_index = 0; read_index < *argc; ++read_index) {
        char *arg = (*argv)[read_index];
        if (!arg) {
            continue;
        }

        if (strcmp(arg, "--log-level") == 0 || strcmp(arg, "-l") == 0) {
            if (read_index + 1 >= *argc) {
                if (error && error_cap) {
                    snprintf(error, error_cap, "missing value for %s", arg);
                }
                return 0;
            }
            int ok;
            config->log_level = glyph_log_level_from_name((*argv)[++read_index], &ok);
            if (!ok) {
                if (error && error_cap) {
                    snprintf(error, error_cap, "invalid log level '%s'", (*argv)[read_index]);
                }
                return 0;
            }
            continue;
        }

        if (strcmp(arg, "--log-format") == 0) {
            if (read_index + 1 >= *argc) {
                if (error && error_cap) {
                    snprintf(error, error_cap, "missing value for %s", arg);
                }
                return 0;
            }
            if (!glyph_log_format_from_name((*argv)[++read_index], &config->log_format)) {
                if (error && error_cap) {
                    snprintf(error, error_cap, "invalid log format '%s'", (*argv)[read_index]);
                }
                return 0;
            }
            continue;
        }

        if (strcmp(arg, "--verbose") == 0 || strcmp(arg, "-v") == 0) {
            config->verbose = 1;
            continue;
        }

        if (strcmp(arg, "--output-dir") == 0) {
            if (read_index + 1 >= *argc) {
                if (error && error_cap) {
                    snprintf(error, error_cap, "missing value for %s", arg);
                }
                return 0;
            }
            config->output_dir = (*argv)[++read_index];
            continue;
        }

        (*argv)[write_index++] = arg;
    }

    *argc = write_index;
    return 1;
}
