#include "atlas.h"
#include "tooling.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#define GLYPH_MAIN_ATTR __attribute__((weak))
#else
#define GLYPH_MAIN_ATTR
#endif

static void usage(FILE *fp) {
    fprintf(fp,
            "usage:\n"
            "  glyph pack <font_dir> <out.glyph>\n"
            "  glyph unpack <in.glyph> <output_dir>\n"
            "  glyph render <in.glyph> <text> <preview.pgm>\n"
            "  glyph info <in.glyph>\n"
            "  glyph validate <in.glyph>\n"
            "  glyph atlas <in.glyph> <atlas.pgm>\n"
            "  glyph manifest <in.glyph> <manifest.txt>\n"
            "  glyph sample <output_dir> [first_id] [count]\n");
}

int GLYPH_MAIN_ATTR main(int argc, char **argv) {
    if (argc < 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        usage(argc < 2 ? stderr : stdout);
        return argc < 2 ? 1 : 0;
    }

    if (strcmp(argv[1], "pack") == 0) {
        if (argc != 4) {
            usage(stderr);
            return 1;
        }
        if (!glyph_pack_directory(argv[2], argv[3])) {
            fprintf(stderr, "glyph: failed to pack '%s'\n", argv[2]);
            return 1;
        }
        return 0;
    }

    if (strcmp(argv[1], "unpack") == 0) {
        if (argc != 4) {
            usage(stderr);
            return 1;
        }
        if (!glyph_unpack_file_to_dir(argv[2], argv[3])) {
            fprintf(stderr, "glyph: failed to unpack '%s'\n", argv[2]);
            return 1;
        }
        return 0;
    }

    if (strcmp(argv[1], "render") == 0) {
        if (argc != 5) {
            usage(stderr);
            return 1;
        }
        if (!glyph_render_file(argv[2], argv[3], argv[4])) {
            fprintf(stderr, "glyph: failed to render '%s'\n", argv[2]);
            return 1;
        }
        return 0;
    }

    if (strcmp(argv[1], "info") == 0) {
        if (argc != 3) {
            usage(stderr);
            return 1;
        }
        return glyph_tool_info_file(argv[2], stdout) ? 0 : 1;
    }

    if (strcmp(argv[1], "validate") == 0) {
        if (argc != 3) {
            usage(stderr);
            return 1;
        }
        return glyph_tool_validate_file(argv[2], stdout) ? 0 : 1;
    }

    if (strcmp(argv[1], "atlas") == 0) {
        if (argc != 4) {
            usage(stderr);
            return 1;
        }
        if (!glyph_tool_export_atlas(argv[2], argv[3])) {
            fprintf(stderr, "glyph: failed to export atlas '%s'\n", argv[2]);
            return 1;
        }
        return 0;
    }

    if (strcmp(argv[1], "manifest") == 0) {
        if (argc != 4) {
            usage(stderr);
            return 1;
        }
        if (!glyph_tool_write_manifest(argv[2], argv[3])) {
            fprintf(stderr, "glyph: failed to write manifest for '%s'\n", argv[2]);
            return 1;
        }
        return 0;
    }

    if (strcmp(argv[1], "sample") == 0) {
        if (argc < 3 || argc > 5) {
            usage(stderr);
            return 1;
        }
        uint32_t first_id = argc >= 4 ? (uint32_t)strtoul(argv[3], NULL, 10) : 32u;
        uint32_t count = argc >= 5 ? (uint32_t)strtoul(argv[4], NULL, 10) : 96u;
        if (!glyph_tool_make_sample_font(argv[2], first_id, count)) {
            fprintf(stderr, "glyph: failed to create sample font '%s'\n", argv[2]);
            return 1;
        }
        return 0;
    }

    usage(stderr);
    return 1;
}
