#include "../src/atlas.h"
#include "../src/reload.h"
#include "../src/tooling.h"
#include "../src/validate.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#define glyph_mkdir(path) _mkdir(path)
#define glyph_rmdir(path) _rmdir(path)
#else
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define glyph_mkdir(path) mkdir(path, 0700)
#define glyph_rmdir(path) rmdir(path)
#endif

enum {
    GLYPH_FUZZ_OP_LOAD_VALIDATE = 1,
    GLYPH_FUZZ_OP_RENDER = 2,
    GLYPH_FUZZ_OP_UNPACK = 3,
    GLYPH_FUZZ_OP_TOOLING = 4,
    GLYPH_FUZZ_OP_RELOAD = 5
};

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t pos;
} GlyphFuzzReader;

static int fuzz_read_u8(GlyphFuzzReader *reader, uint8_t *value) {
    if (reader->pos + 1 > reader->size) {
        return 0;
    }
    *value = reader->data[reader->pos++];
    return 1;
}

static int fuzz_read_u16(GlyphFuzzReader *reader, uint16_t *value) {
    if (reader->pos + 2 > reader->size) {
        return 0;
    }
    *value = (uint16_t)reader->data[reader->pos] |
             ((uint16_t)reader->data[reader->pos + 1] << 8);
    reader->pos += 2;
    return 1;
}

static int fuzz_read_u32(GlyphFuzzReader *reader, uint32_t *value) {
    if (reader->pos + 4 > reader->size) {
        return 0;
    }
    *value = (uint32_t)reader->data[reader->pos] |
             ((uint32_t)reader->data[reader->pos + 1] << 8) |
             ((uint32_t)reader->data[reader->pos + 2] << 16) |
             ((uint32_t)reader->data[reader->pos + 3] << 24);
    reader->pos += 4;
    return 1;
}

static int fuzz_write_file(const char *path, const uint8_t *data, size_t size) {
    FILE *fp = fopen(path, "wb");
    int ok;

    if (!fp) {
        return 0;
    }
    ok = fwrite(data, 1, size, fp) == size;
    fclose(fp);
    return ok;
}

static void fuzz_remove_dir_files(const char *dir_path) {
#if defined(_WIN32)
    (void)dir_path;
#else
    DIR *dir;
    struct dirent *ent;
    char path[512];

    dir = opendir(dir_path);
    if (!dir) {
        return;
    }
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        snprintf(path, sizeof(path), "%s/%s", dir_path, ent->d_name);
        remove(path);
    }
    closedir(dir);
#endif
}

static void fuzz_remove_outputs(const char *root) {
    char path[160];

    snprintf(path, sizeof(path), "%s/unpack", root);
    fuzz_remove_dir_files(path);
    glyph_rmdir(path);

    snprintf(path, sizeof(path), "%s/render.pgm", root);
    remove(path);
    snprintf(path, sizeof(path), "%s/atlas.pgm", root);
    remove(path);
    snprintf(path, sizeof(path), "%s/manifest.txt", root);
    remove(path);
    snprintf(path, sizeof(path), "%s/input.glyph", root);
    remove(path);
    glyph_rmdir(root);
}

static void fuzz_exercise_loaded_file(const char *glyph_path) {
    GlyphFile file;
    GlyphValidationDiagnostics diagnostics;
    GlyphValidationReport report;
    GlyphValidationStats stats;

    if (!glyph_load_file(glyph_path, &file)) {
        return;
    }

    glyph_validation_diagnostics_init(&diagnostics);
    glyph_validation_report_init(&report);
    (void)glyph_validate_file(&file, &diagnostics, &report);
    glyph_tool_collect_stats(&file, &stats);
    glyph_validation_diagnostics_free(&diagnostics);
    glyph_file_free(&file);
}

static void fuzz_exercise_payload(uint8_t op,
                                  const char *root,
                                  const char *glyph_path,
                                  const uint8_t *payload,
                                  size_t payload_size,
                                  const char *text) {
    char out_path[160];
    char unpack_dir[160];

    if (op == GLYPH_FUZZ_OP_RELOAD) {
        uint32_t first_size;
        if (payload_size < 8) {
            return;
        }
        first_size = (uint32_t)payload[0] |
                     ((uint32_t)payload[1] << 8) |
                     ((uint32_t)payload[2] << 16) |
                     ((uint32_t)payload[3] << 24);
        if (first_size == 0 || first_size > payload_size - 4) {
            return;
        }
        (void)glyph_reload_sequence_resolve(payload + 4,
                                            first_size,
                                            payload + 4 + first_size,
                                            payload_size - 4 - first_size);
        return;
    }

    if (!fuzz_write_file(glyph_path, payload, payload_size)) {
        return;
    }

    if (op == GLYPH_FUZZ_OP_LOAD_VALIDATE) {
        fuzz_exercise_loaded_file(glyph_path);
    } else if (op == GLYPH_FUZZ_OP_RENDER) {
        snprintf(out_path, sizeof(out_path), "%s/render.pgm", root);
        (void)glyph_render_file(glyph_path, text, out_path);
    } else if (op == GLYPH_FUZZ_OP_UNPACK) {
        snprintf(unpack_dir, sizeof(unpack_dir), "%s/unpack", root);
        (void)glyph_mkdir(unpack_dir);
        (void)glyph_unpack_file_to_dir(glyph_path, unpack_dir);
    } else if (op == GLYPH_FUZZ_OP_TOOLING) {
        snprintf(out_path, sizeof(out_path), "%s/atlas.pgm", root);
        (void)glyph_tool_export_atlas(glyph_path, out_path);
        snprintf(out_path, sizeof(out_path), "%s/manifest.txt", root);
        (void)glyph_tool_write_manifest(glyph_path, out_path);
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    GlyphFuzzReader reader;
    char root[128];
    char glyph_path[160];
    char text[65];
    uint16_t command_count;
    uint16_t text_len;
    int pid = 0;

    if (size < 12 || memcmp(data, "GFZA", 4) != 0) {
        return 0;
    }

    reader.data = data;
    reader.size = size;
    reader.pos = 4;

    if (!fuzz_read_u16(&reader, &command_count) ||
        !fuzz_read_u16(&reader, &text_len) ||
        text_len > sizeof(text) - 1 ||
        reader.pos + text_len > reader.size) {
        return 0;
    }

    memcpy(text, reader.data + reader.pos, text_len);
    text[text_len] = '\0';
    reader.pos += text_len;

#if !defined(_WIN32)
    pid = (int)getpid();
#endif
    snprintf(root, sizeof(root), "/tmp/glyph-atlas-fuzz-%d", pid);
    snprintf(glyph_path, sizeof(glyph_path), "%s/input.glyph", root);
    (void)glyph_mkdir(root);

    if (command_count > 32) {
        command_count = 32;
    }

    for (uint16_t i = 0; i < command_count; i++) {
        uint8_t op;
        uint8_t reserved;
        uint16_t tag;
        uint32_t payload_size;
        const uint8_t *payload;

        if (!fuzz_read_u8(&reader, &op) ||
            !fuzz_read_u8(&reader, &reserved) ||
            !fuzz_read_u16(&reader, &tag) ||
            !fuzz_read_u32(&reader, &payload_size)) {
            break;
        }
        (void)reserved;
        (void)tag;

        if (payload_size > reader.size - reader.pos) {
            break;
        }

        payload = reader.data + reader.pos;
        reader.pos += payload_size;
        fuzz_exercise_payload(op, root, glyph_path, payload, payload_size, text);
    }

    fuzz_remove_outputs(root);
    return 0;
}
