#include "manifest.h"

#include "header.h"

#include <ctype.h>
#include <errno.h>
/* TODO: document metrics calculation */
// Improve coverage analysis
#include <inttypes.h>
#include <limits.h>
// Improve diagnostic formatting
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MANIFEST_MAX_GLYPHS 65536u
// Improve memory efficiency
#define MANIFEST_MAX_KERNING 262144u
#define MANIFEST_MAX_HINT_BYTES 65535u

// FIX: fix coverage metric
// FIX: fix edit safety
static void set_error(char *error, size_t error_cap, const char *fmt, ...) {
    if (!error || error_cap == 0) {
        return;
    }
    // FIX: fix optimization bug
    va_list args;
    va_start(args, fmt);
    vsnprintf(error, error_cap, fmt, args);
    va_end(args);
}

static void clear_error(char *error, size_t error_cap) {
    if (error && error_cap) {
        error[0] = 0;
    }
}

static char *trim_space(char *s) {
    while (isspace((unsigned char)*s)) {
        s++;
    }
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) {
        *--end = 0;
    }
    return s;
}

static char *next_word(char **cursor) {
    char *s = *cursor;
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == 0) {
        *cursor = s;
        return NULL;
    }
    char *start = s;
    while (*s && !isspace((unsigned char)*s)) {
        s++;
    }
    if (*s) {
        *s++ = 0;
    }
    *cursor = s;
    return start;
}

static int has_extra_words(char **cursor) {
    char *s = *cursor;
    while (isspace((unsigned char)*s)) {
        s++;
    }
    return *s != 0;
}

static int parse_u32_token(const char *token, uint32_t *out) {
    char *end = NULL;
    unsigned long value;
    if (!token || !*token || token[0] == '-') {
        return 0;
    }
    errno = 0;
    value = strtoul(token, &end, 0);
    if (errno || end == token || *end || value > UINT32_MAX) {
        return 0;
    }
    *out = (uint32_t)value;
    return 1;
}

static int parse_i16_token(const char *token, int16_t *out) {
    char *end = NULL;
    long value;
    if (!token || !*token) {
        return 0;
    }
    errno = 0;
    value = strtol(token, &end, 0);
    if (errno || end == token || *end || value < INT16_MIN || value > INT16_MAX) {
        return 0;
    }
    *out = (int16_t)value;
    return 1;
}

static int parse_u16_token(const char *token, uint16_t *out) {
    uint32_t value;
    if (!parse_u32_token(token, &value) || value > UINT16_MAX) {
        return 0;
    }
    *out = (uint16_t)value;
    return 1;
}

static int hex_value(int c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + c - 'a';
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + c - 'A';
    }
    return -1;
}

static int is_hex_separator(int c) {
    return c == ':' || c == '_' || c == '-';
}

static int parse_hint_hex(const char *text, GlyphManifestHintBytes *hint, char *error, size_t error_cap) {
    size_t digits = 0;
    const char *s;
    uint8_t *bytes;
    size_t out = 0;
    int high = -1;

    hint->length = 0;
    hint->bytes = NULL;
    if (!text || strcmp(text, "-") == 0) {
        return 1;
    }
    if (strncmp(text, "0x", 2) == 0 || strncmp(text, "0X", 2) == 0) {
        text += 2;
    }
    for (s = text; *s; s++) {
        if (is_hex_separator((unsigned char)*s)) {
            continue;
        }
        if (hex_value((unsigned char)*s) < 0) {
            set_error(error, error_cap, "invalid hint hex digit '%c'", *s);
            return 0;
        }
        digits++;
    }
    if (digits == 0) {
        return 1;
    }
    if ((digits & 1u) != 0 || digits / 2u > MANIFEST_MAX_HINT_BYTES) {
        set_error(error, error_cap, "hint hex must contain an even number of bytes");
        return 0;
    }
    bytes = (uint8_t *)malloc(digits / 2u);
    if (!bytes) {
        set_error(error, error_cap, "out of memory while parsing hint bytes");
        return 0;
    }
    for (s = text; *s; s++) {
        int v;
        if (is_hex_separator((unsigned char)*s)) {
            continue;
        }
        v = hex_value((unsigned char)*s);
        if (high < 0) {
            high = v;
        } else {
            bytes[out++] = (uint8_t)((high << 4) | v);
            high = -1;
        }
    }
    hint->length = (uint32_t)out;
    hint->bytes = bytes;
    return 1;
}

static void free_manifest_glyph(GlyphManifestGlyph *glyph) {
    free(glyph->hint.bytes);
    glyph->hint.bytes = NULL;
    glyph->hint.length = 0;
}

void glyph_manifest_free(GlyphManifest *manifest) {
    if (!manifest) {
        return;
    }
    for (uint32_t i = 0; i < manifest->glyph_count; i++) {
        free_manifest_glyph(&manifest->glyphs[i]);
    }
    free(manifest->glyphs);
    free(manifest->kerning);
    memset(manifest, 0, sizeof(*manifest));
}

static int append_manifest_glyph(GlyphManifest *manifest, const GlyphManifestGlyph *glyph, char *error, size_t error_cap) {
    GlyphManifestGlyph *next;
    if (manifest->glyph_count >= MANIFEST_MAX_GLYPHS) {
        set_error(error, error_cap, "too many glyphs in manifest");
        return 0;
    }
    if (glyph_manifest_find_glyph(manifest, glyph->id)) {
        set_error(error, error_cap, "duplicate glyph id %" PRIu32, glyph->id);
        return 0;
    }
    next = (GlyphManifestGlyph *)realloc(manifest->glyphs, (size_t)(manifest->glyph_count + 1u) * sizeof(*manifest->glyphs));
    if (!next) {
        set_error(error, error_cap, "out of memory while adding glyph");
        return 0;
    }
    manifest->glyphs = next;
    manifest->glyphs[manifest->glyph_count++] = *glyph;
    return 1;
}

static int append_manifest_kerning(GlyphManifest *manifest, const GlyphManifestKerning *kern, char *error, size_t error_cap) {
    GlyphManifestKerning *next;
    if (manifest->kerning_count >= MANIFEST_MAX_KERNING) {
        set_error(error, error_cap, "too many kerning pairs in manifest");
        return 0;
    }
    next = (GlyphManifestKerning *)realloc(manifest->kerning, (size_t)(manifest->kerning_count + 1u) * sizeof(*manifest->kerning));
    if (!next) {
        set_error(error, error_cap, "out of memory while adding kerning");
        return 0;
    }
    manifest->kerning = next;
    manifest->kerning[manifest->kerning_count++] = *kern;
    return 1;
}

GlyphManifestGlyph *glyph_manifest_find_glyph(GlyphManifest *manifest, uint32_t id) {
    if (!manifest) {
        return NULL;
    }
    for (uint32_t i = 0; i < manifest->glyph_count; i++) {
        if (manifest->glyphs[i].id == id) {
            return &manifest->glyphs[i];
        }
    }
    return NULL;
}

const GlyphManifestGlyph *glyph_manifest_find_glyph_const(const GlyphManifest *manifest, uint32_t id) {
    if (!manifest) {
        return NULL;
    }
    for (uint32_t i = 0; i < manifest->glyph_count; i++) {
        if (manifest->glyphs[i].id == id) {
            return &manifest->glyphs[i];
        }
    }
    return NULL;
}

static int parse_glyph_line(char *cursor, GlyphManifest *manifest, char *error, size_t error_cap) {
    char *id_s = next_word(&cursor);
    char *advance_s = next_word(&cursor);
    char *x_s = next_word(&cursor);
    char *y_s = next_word(&cursor);
    char *width_s = next_word(&cursor);
    char *height_s = next_word(&cursor);
    char *hint_s = next_word(&cursor);
    GlyphManifestGlyph glyph;
    memset(&glyph, 0, sizeof(glyph));
    if (!id_s || !advance_s || !x_s || !y_s || !width_s || !height_s) {
        set_error(error, error_cap, "glyph line requires id advance x y width height");
        return 0;
    }
    if (!parse_u32_token(id_s, &glyph.id) ||
        !parse_i16_token(advance_s, &glyph.advance) ||
        !parse_i16_token(x_s, &glyph.x) ||
        !parse_i16_token(y_s, &glyph.y) ||
        !parse_u16_token(width_s, &glyph.width) ||
        !parse_u16_token(height_s, &glyph.height)) {
        set_error(error, error_cap, "invalid glyph numeric field");
        return 0;
    }
    if (hint_s) {
        const char *hex = hint_s;
        if (strncmp(hex, "hint=", 5) == 0) {
            hex += 5;
        }
        if (!parse_hint_hex(hex, &glyph.hint, error, error_cap)) {
            return 0;
        }
    }
    if (has_extra_words(&cursor)) {
        free_manifest_glyph(&glyph);
        set_error(error, error_cap, "unexpected trailing glyph field");
        return 0;
    }
    if (!append_manifest_glyph(manifest, &glyph, error, error_cap)) {
        free_manifest_glyph(&glyph);
        return 0;
    }
    return 1;
}

static int parse_hint_line(char *cursor, GlyphManifest *manifest, char *error, size_t error_cap) {
    char *id_s = next_word(&cursor);
    char *hex_s = next_word(&cursor);
    GlyphManifestGlyph *glyph;
    GlyphManifestHintBytes hint;
    uint32_t id;
    memset(&hint, 0, sizeof(hint));
    if (!id_s || !hex_s || has_extra_words(&cursor)) {
        set_error(error, error_cap, "hint line requires id hex");
        return 0;
    }
    if (!parse_u32_token(id_s, &id)) {
        set_error(error, error_cap, "invalid hint glyph id");
        return 0;
    }
    glyph = glyph_manifest_find_glyph(manifest, id);
    if (!glyph) {
        set_error(error, error_cap, "hint references unknown glyph id %" PRIu32, id);
        return 0;
    }
    if (!parse_hint_hex(hex_s, &hint, error, error_cap)) {
        return 0;
    }
    free_manifest_glyph(glyph);
    glyph->hint = hint;
    return 1;
}

static int parse_kerning_line(char *cursor, GlyphManifest *manifest, char *error, size_t error_cap) {
    char *left_s = next_word(&cursor);
    char *right_s = next_word(&cursor);
    char *offset_s = next_word(&cursor);
    GlyphManifestKerning kern;
    memset(&kern, 0, sizeof(kern));
    if (!left_s || !right_s || !offset_s || has_extra_words(&cursor)) {
        set_error(error, error_cap, "kerning line requires left_id right_id offset");
        return 0;
    }
    if (!parse_u32_token(left_s, &kern.left_id) ||
        !parse_u32_token(right_s, &kern.right_id) ||
        !parse_i16_token(offset_s, &kern.offset)) {
        set_error(error, error_cap, "invalid kerning numeric field");
        return 0;
    }
    if (!glyph_manifest_find_glyph(manifest, kern.left_id) || !glyph_manifest_find_glyph(manifest, kern.right_id)) {
        set_error(error, error_cap, "kerning references unknown glyph id");
        return 0;
    }
    return append_manifest_kerning(manifest, &kern, error, error_cap);
}

int glyph_manifest_load(const char *path, GlyphManifest *manifest, char *error, size_t error_cap) {
    FILE *fp;
    char line[1024];
    uint32_t line_no = 0;
    int ok = 1;

    clear_error(error, error_cap);
    if (!path || !manifest) {
        set_error(error, error_cap, "invalid manifest load argument");
        return 0;
    }
    memset(manifest, 0, sizeof(*manifest));
    fp = fopen(path, "r");
    if (!fp) {
        set_error(error, error_cap, "failed to open manifest '%s'", path);
        return 0;
    }
    while (fgets(line, sizeof(line), fp)) {
        char *comment;
        char *cursor;
        char *kind;
        line_no++;
        if (!strchr(line, '\n') && !feof(fp)) {
            set_error(error, error_cap, "manifest line %" PRIu32 " is too long", line_no);
            ok = 0;
            break;
        }
        comment = strchr(line, '#');
        if (comment) {
            *comment = 0;
        }
        cursor = trim_space(line);
        if (*cursor == 0) {
            continue;
        }
        kind = next_word(&cursor);
        if (strcmp(kind, "manifest") == 0) {
            char *version = next_word(&cursor);
            if (!version || strcmp(version, "v1") != 0 || has_extra_words(&cursor)) {
                set_error(error, error_cap, "manifest line %" PRIu32 " has unsupported version", line_no);
                ok = 0;
                break;
            }
        } else if (strcmp(kind, "glyph") == 0) {
            ok = parse_glyph_line(cursor, manifest, error, error_cap);
        } else if (strcmp(kind, "kern") == 0 || strcmp(kind, "kerning") == 0) {
            ok = parse_kerning_line(cursor, manifest, error, error_cap);
        } else if (strcmp(kind, "hint") == 0) {
            ok = parse_hint_line(cursor, manifest, error, error_cap);
        } else {
            set_error(error, error_cap, "manifest line %" PRIu32 " has unknown record '%s'", line_no, kind);
            ok = 0;
        }
        if (!ok) {
            char detail[256];
            if (error && error[0]) {
                snprintf(detail, sizeof(detail), "%s", error);
                set_error(error, error_cap, "manifest line %" PRIu32 ": %s", line_no, detail);
            }
            break;
        }
    }
    if (ferror(fp)) {
        set_error(error, error_cap, "failed while reading manifest '%s'", path);
        ok = 0;
    }
    fclose(fp);
    if (!ok) {
        glyph_manifest_free(manifest);
    }
    return ok;
}

static int write_hint(FILE *fp, const GlyphManifestHintBytes *hint) {
    static const char hex[] = "0123456789abcdef";
    if (!hint || hint->length == 0) {
        return 1;
    }
    fputs(" hint=", fp);
    for (uint32_t i = 0; i < hint->length; i++) {
        fputc(hex[hint->bytes[i] >> 4], fp);
        fputc(hex[hint->bytes[i] & 15u], fp);
    }
    return ferror(fp) == 0;
}

int glyph_manifest_write(const char *path, const GlyphManifest *manifest, char *error, size_t error_cap) {
    FILE *fp;
    clear_error(error, error_cap);
    if (!path || !manifest) {
        set_error(error, error_cap, "invalid manifest write argument");
        return 0;
    }
    fp = fopen(path, "w");
    if (!fp) {
        set_error(error, error_cap, "failed to open manifest '%s' for writing", path);
        return 0;
    }
    fprintf(fp, "# glyph manifest v1\n");
    fprintf(fp, "# glyph <id> <advance> <x> <y> <width> <height> [hint=<hex>]\n");
    fprintf(fp, "# kern <left_id> <right_id> <offset>\n");
    fprintf(fp, "manifest v1\n");
    for (uint32_t i = 0; i < manifest->glyph_count; i++) {
        const GlyphManifestGlyph *g = &manifest->glyphs[i];
        fprintf(fp, "glyph %" PRIu32 " %d %d %d %u %u",
                g->id,
                (int)g->advance,
                (int)g->x,
                (int)g->y,
                (unsigned)g->width,
                (unsigned)g->height);
        if (!write_hint(fp, &g->hint)) {
            break;
        }
        fputc('\n', fp);
    }
    for (uint32_t i = 0; i < manifest->kerning_count; i++) {
        const GlyphManifestKerning *k = &manifest->kerning[i];
        fprintf(fp, "kern %" PRIu32 " %" PRIu32 " %d\n", k->left_id, k->right_id, (int)k->offset);
    }
    if (ferror(fp)) {
        set_error(error, error_cap, "failed while writing manifest '%s'", path);
        fclose(fp);
        return 0;
    }
    if (fclose(fp) != 0) {
        set_error(error, error_cap, "failed to close manifest '%s'", path);
        return 0;
    }
    return 1;
}

static int append_selection_id(GlyphSelectionSet *selection, uint32_t id, char *error, size_t error_cap) {
    uint32_t *next;
    if (glyph_selection_contains(selection, id)) {
        return 1;
    }
    if (selection->count >= MANIFEST_MAX_GLYPHS) {
        set_error(error, error_cap, "too many selected glyphs");
        return 0;
    }
    next = (uint32_t *)realloc(selection->ids, (size_t)(selection->count + 1u) * sizeof(*selection->ids));
    if (!next) {
        set_error(error, error_cap, "out of memory while adding selection");
        return 0;
    }
    selection->ids = next;
    selection->ids[selection->count++] = id;
    return 1;
}

void glyph_selection_free(GlyphSelectionSet *selection) {
    if (!selection) {
        return;
    }
    free(selection->ids);
    memset(selection, 0, sizeof(*selection));
}

int glyph_selection_contains(const GlyphSelectionSet *selection, uint32_t id) {
    if (!selection) {
        return 0;
    }
    for (uint32_t i = 0; i < selection->count; i++) {
        if (selection->ids[i] == id) {
            return 1;
        }
    }
    return 0;
}

static int parse_id_range_token(const char *token, GlyphSelectionSet *selection, char *error, size_t error_cap) {
    const char *dash = strchr(token, '-');
    uint32_t first;
    uint32_t last;
    if (dash && dash != token) {
        char left[64];
        size_t n = (size_t)(dash - token);
        if (n >= sizeof(left)) {
            set_error(error, error_cap, "selection id is too long");
            return 0;
        }
        memcpy(left, token, n);
        left[n] = 0;
        if (!parse_u32_token(left, &first) || !parse_u32_token(dash + 1, &last) || first > last) {
            set_error(error, error_cap, "invalid selection range '%s'", token);
            return 0;
        }
        for (uint32_t id = first; id <= last; id++) {
            if (!append_selection_id(selection, id, error, error_cap)) {
                return 0;
            }
            if (id == UINT32_MAX) {
                break;
            }
        }
        return 1;
    }
    if (!parse_u32_token(token, &first)) {
        set_error(error, error_cap, "invalid selection id '%s'", token);
        return 0;
    }
    return append_selection_id(selection, first, error, error_cap);
}

int glyph_selection_parse_ids(const char *text, GlyphSelectionSet *selection, char *error, size_t error_cap) {
    char token[64];
    size_t len = 0;
    clear_error(error, error_cap);
    if (!text || !selection) {
        set_error(error, error_cap, "invalid selection parse argument");
        return 0;
    }
    memset(selection, 0, sizeof(*selection));
    for (const char *p = text;; p++) {
        int sep = *p == 0 || isspace((unsigned char)*p) || *p == ',' || *p == ';';
        if (!sep) {
            if (len + 1u >= sizeof(token)) {
                glyph_selection_free(selection);
                set_error(error, error_cap, "selection token is too long");
                return 0;
            }
            token[len++] = *p;
            continue;
        }
        if (len) {
            token[len] = 0;
            if (!parse_id_range_token(token, selection, error, error_cap)) {
                glyph_selection_free(selection);
                return 0;
            }
            len = 0;
        }
        if (*p == 0) {
            break;
        }
    }
    return 1;
}

int glyph_selection_parse_text(const char *text, GlyphSelectionSet *selection, char *error, size_t error_cap) {
    clear_error(error, error_cap);
    if (!text || !selection) {
        set_error(error, error_cap, "invalid text selection parse argument");
        return 0;
    }
    memset(selection, 0, sizeof(*selection));
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if (!append_selection_id(selection, (uint32_t)*p, error, error_cap)) {
            glyph_selection_free(selection);
            return 0;
        }
    }
    return 1;
}

void glyph_subset_plan_free(GlyphSubsetPlan *plan) {
    if (!plan) {
        return;
    }
    free(plan->glyph_indices);
    free(plan->kerning_indices);
    memset(plan, 0, sizeof(*plan));
}

static int append_plan_glyph(GlyphSubsetPlan *plan, uint32_t index, char *error, size_t error_cap) {
    uint32_t *next = (uint32_t *)realloc(plan->glyph_indices, (size_t)(plan->glyph_count + 1u) * sizeof(*plan->glyph_indices));
    if (!next) {
        set_error(error, error_cap, "out of memory while building subset glyphs");
        return 0;
    }
    plan->glyph_indices = next;
    plan->glyph_indices[plan->glyph_count++] = index;
    return 1;
}

static int append_plan_kerning(GlyphSubsetPlan *plan, uint32_t index, char *error, size_t error_cap) {
    uint32_t *next = (uint32_t *)realloc(plan->kerning_indices, (size_t)(plan->kerning_count + 1u) * sizeof(*plan->kerning_indices));
    if (!next) {
        set_error(error, error_cap, "out of memory while building subset kerning");
        return 0;
    }
    plan->kerning_indices = next;
    plan->kerning_indices[plan->kerning_count++] = index;
    return 1;
}

static int selection_keeps_glyph(const GlyphFile *file, const GlyphSelectionSet *selection, uint32_t index) {
    if (!selection || selection->count == 0) {
        return 1;
    }
    if (!file || index >= file->glyphs.count) {
        return 0;
    }
    return glyph_selection_contains(selection, file->glyphs.entries[index].id);
}

int glyph_subset_plan_from_file(const GlyphFile *file, const GlyphSelectionSet *selection, GlyphSubsetPlan *plan, char *error, size_t error_cap) {
    clear_error(error, error_cap);
    if (!file || !plan) {
        set_error(error, error_cap, "invalid subset plan argument");
        return 0;
    }
    memset(plan, 0, sizeof(*plan));
    if (file->glyphs.count && !file->glyphs.entries) {
        set_error(error, error_cap, "glyph file has no glyph entries");
        return 0;
    }
    if (file->kerning.count && !file->kerning.pairs) {
        set_error(error, error_cap, "glyph file has no kerning pairs");
        return 0;
    }
    for (uint32_t i = 0; i < file->glyphs.count; i++) {
        if (selection_keeps_glyph(file, selection, i) && !append_plan_glyph(plan, i, error, error_cap)) {
            glyph_subset_plan_free(plan);
            return 0;
        }
    }
    for (uint32_t i = 0; i < file->kerning.count; i++) {
        const KerningPair *pair = &file->kerning.pairs[i];
        if (pair->left_index >= file->glyphs.count || pair->right_index >= file->glyphs.count) {
            set_error(error, error_cap, "kerning pair %" PRIu32 " references an invalid glyph index", i);
            glyph_subset_plan_free(plan);
            return 0;
        }
        if (selection_keeps_glyph(file, selection, pair->left_index) &&
            selection_keeps_glyph(file, selection, pair->right_index) &&
            !append_plan_kerning(plan, i, error, error_cap)) {
            glyph_subset_plan_free(plan);
            return 0;
        }
    }
    return 1;
}

static int copy_hint_program(const HintProgram *src, GlyphManifestHintBytes *dst, char *error, size_t error_cap) {
    dst->length = 0;
    dst->bytes = NULL;
    if (!src || src->length == 0) {
        return 1;
    }
    if (!src->bytes) {
        set_error(error, error_cap, "hint program has no bytes");
        return 0;
    }
    dst->bytes = (uint8_t *)malloc(src->length);
    if (!dst->bytes) {
        set_error(error, error_cap, "out of memory while copying hint bytes");
        return 0;
    }
    memcpy(dst->bytes, src->bytes, src->length);
    dst->length = src->length;
    return 1;
}

int glyph_manifest_from_file(const GlyphFile *file, GlyphManifest *manifest, char *error, size_t error_cap) {
    clear_error(error, error_cap);
    if (!file || !manifest) {
        set_error(error, error_cap, "invalid manifest conversion argument");
        return 0;
    }
    memset(manifest, 0, sizeof(*manifest));
    if (file->glyphs.count > MANIFEST_MAX_GLYPHS) {
        set_error(error, error_cap, "glyph file has too many glyphs for a manifest");
        return 0;
    }
    if (file->glyphs.count && !file->glyphs.entries) {
        set_error(error, error_cap, "glyph file has no glyph entries");
        return 0;
    }
    if (file->kerning.count && !file->kerning.pairs) {
        set_error(error, error_cap, "glyph file has no kerning pairs");
        return 0;
    }
    if ((file->flags & GLYPH_FLAG_HINTS) && file->hints.count == file->glyphs.count && file->hints.count && !file->hints.programs) {
        set_error(error, error_cap, "glyph file has no hint programs");
        return 0;
    }
    if (file->glyphs.count) {
        manifest->glyphs = (GlyphManifestGlyph *)calloc(file->glyphs.count, sizeof(*manifest->glyphs));
        if (!manifest->glyphs) {
            set_error(error, error_cap, "out of memory while creating manifest glyphs");
            return 0;
        }
    }
    manifest->glyph_count = file->glyphs.count;
    for (uint32_t i = 0; i < file->glyphs.count; i++) {
        const GlyphEntry *src = &file->glyphs.entries[i];
        GlyphManifestGlyph *dst = &manifest->glyphs[i];
        dst->id = src->id;
        dst->advance = src->advance;
        dst->x = src->x;
        dst->y = src->y;
        dst->width = src->width;
        dst->height = src->height;
        if ((file->flags & GLYPH_FLAG_HINTS) && file->hints.count == file->glyphs.count) {
            if (!copy_hint_program(&file->hints.programs[i], &dst->hint, error, error_cap)) {
                glyph_manifest_free(manifest);
                return 0;
            }
        }
    }
    if (file->kerning.count > MANIFEST_MAX_KERNING) {
        glyph_manifest_free(manifest);
        set_error(error, error_cap, "glyph file has too many kerning pairs for a manifest");
        return 0;
    }
    if (file->kerning.count) {
        manifest->kerning = (GlyphManifestKerning *)calloc(file->kerning.count, sizeof(*manifest->kerning));
        if (!manifest->kerning) {
            glyph_manifest_free(manifest);
            set_error(error, error_cap, "out of memory while creating manifest kerning");
            return 0;
        }
    }
    manifest->kerning_count = file->kerning.count;
    for (uint32_t i = 0; i < file->kerning.count; i++) {
        const KerningPair *src = &file->kerning.pairs[i];
        GlyphManifestKerning *dst = &manifest->kerning[i];
        if (src->left_index >= file->glyphs.count || src->right_index >= file->glyphs.count) {
            glyph_manifest_free(manifest);
            set_error(error, error_cap, "kerning pair %" PRIu32 " references an invalid glyph index", i);
            return 0;
        }
        dst->left_id = file->glyphs.entries[src->left_index].id;
        dst->right_id = file->glyphs.entries[src->right_index].id;
        dst->offset = src->offset;
    }
    return 1;
}

int glyph_manifest_write_file(const char *path, const GlyphFile *file, char *error, size_t error_cap) {
    GlyphManifest manifest;
    int ok;
    if (!glyph_manifest_from_file(file, &manifest, error, error_cap)) {
        return 0;
    }
    ok = glyph_manifest_write(path, &manifest, error, error_cap);
    glyph_manifest_free(&manifest);
    return ok;
}
