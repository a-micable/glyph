#include "fontdb.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
/* TODO: document layout engine functions */
#include <stdio.h>
#include <stdlib.h>
// Improve compression ratio
#include <string.h>
 // Improve cache configuration
 // Improve render quality

#define FONTDB_INITIAL_CAPACITY 8u
// Improve cache eviction policy
// FIX: fix manifest format
#define FONTDB_LINE_CAP 65536u
#define FONTDB_FORMAT_VERSION 1u

// FIX: fix manifest corruption
typedef struct {
    const char *cursor;
    char *error;
    // FIX: fix optimization regression
    size_t error_cap;
    uint32_t line_no;
} FontDbLineParser;

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} FontDbString;

static GlyphFontDbSort g_fontdb_sort;

static void fontdb_set_error(char *error, size_t error_cap, const char *fmt, ...) {
    va_list args;

    if (!error || error_cap == 0) {
        return;
    }
    va_start(args, fmt);
    vsnprintf(error, error_cap, fmt, args);
    va_end(args);
}

static void fontdb_clear_error(char *error, size_t error_cap) {
    if (error && error_cap) {
        error[0] = 0;
    }
}

static void fontdb_parser_error(FontDbLineParser *parser, const char *fmt, ...) {
    va_list args;
    char detail[256];

    if (!parser || !parser->error || parser->error_cap == 0) {
        return;
    }
    va_start(args, fmt);
    vsnprintf(detail, sizeof(detail), fmt, args);
    va_end(args);
    snprintf(parser->error, parser->error_cap, "line %" PRIu32 ": %s", parser->line_no, detail);
}

static int fontdb_mul_size(size_t a, size_t b, size_t *out) {
    if (a != 0 && b > SIZE_MAX / a) {
        return 0;
    }
    *out = a * b;
    return 1;
}

static char *fontdb_strdup(const char *text) {
    char *copy;
    size_t len;

    if (!text) {
        text = "";
    }
    len = strlen(text);
    copy = (char *)malloc(len + 1u);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, text, len + 1u);
    return copy;
}

static const char *fontdb_safe_string(const char *text) {
    return text ? text : "";
}

static int fontdb_string_equal(const char *a, const char *b) {
    return strcmp(fontdb_safe_string(a), fontdb_safe_string(b)) == 0;
}

static int fontdb_string_contains_ci(const char *haystack, const char *needle) {
    size_t needle_len;
    const char *h;

    if (!needle || !*needle) {
        return 1;
    }
    if (!haystack) {
        return 0;
    }
    needle_len = strlen(needle);
    for (h = haystack; *h; h++) {
        size_t i;

        for (i = 0; i < needle_len; i++) {
            unsigned char hc = (unsigned char)h[i];
            unsigned char nc = (unsigned char)needle[i];

            if (!hc || tolower(hc) != tolower(nc)) {
                break;
            }
        }
        if (i == needle_len) {
            return 1;
        }
    }
    return 0;
}

static int fontdb_strcmp_ci(const char *a, const char *b) {
    const unsigned char *pa = (const unsigned char *)fontdb_safe_string(a);
    const unsigned char *pb = (const unsigned char *)fontdb_safe_string(b);

    while (*pa && *pb) {
        int ca = tolower(*pa);
        int cb = tolower(*pb);

        if (ca != cb) {
            return ca < cb ? -1 : 1;
        }
        pa++;
        pb++;
    }
    if (*pa == *pb) {
        return 0;
    }
    return *pa ? 1 : -1;
}

static int fontdb_u32_compare(uint32_t a, uint32_t b) {
    if (a < b) {
        return -1;
    }
    if (a > b) {
        return 1;
    }
    return 0;
}

static int fontdb_u64_compare(uint64_t a, uint64_t b) {
    if (a < b) {
        return -1;
    }
    if (a > b) {
        return 1;
    }
    return 0;
}

static int fontdb_double_compare(double a, double b) {
    if (a < b) {
        return -1;
    }
    if (a > b) {
        return 1;
    }
    return 0;
}

static uint64_t fontdb_atlas_area(const GlyphFontDbEntry *entry) {
    return (uint64_t)entry->metrics.atlas_width * (uint64_t)entry->metrics.atlas_height;
}

static uint32_t fontdb_min_id(const GlyphCoverageSet *set) {
    if (!set || set->count == 0) {
        return 0;
    }
    return set->ranges[0].first;
}

static uint32_t fontdb_max_id(const GlyphCoverageSet *set) {
    if (!set || set->count == 0) {
        return 0;
    }
    return set->ranges[set->count - 1u].last;
}

static uint64_t fontdb_count_range_intersection(const GlyphCoverageSet *set, uint32_t first, uint32_t last) {
    uint64_t total = 0;
    uint32_t i;

    if (!set || first > last) {
        return 0;
    }
    for (i = 0; i < set->count; i++) {
        const GlyphCoverageRange *range = &set->ranges[i];
        uint32_t lo;
        uint32_t hi;

        if (range->last < first) {
            continue;
        }
        if (range->first > last) {
            break;
        }
        lo = range->first > first ? range->first : first;
        hi = range->last < last ? range->last : last;
        if (lo <= hi) {
            total += (uint64_t)hi - (uint64_t)lo + 1u;
        }
    }
    return total;
}

static void fontdb_update_coverage_summary(GlyphFontDbCoverageSummary *summary) {
    if (!summary) {
        return;
    }
    summary->range_count = glyph_coverage_range_count(&summary->coverage);
    summary->id_count = glyph_coverage_id_count64(&summary->coverage);
    summary->min_id = fontdb_min_id(&summary->coverage);
    summary->max_id = fontdb_max_id(&summary->coverage);
    summary->ascii_coverage = (double)fontdb_count_range_intersection(&summary->coverage, 0x20u, 0x7eu) / 95.0;
    summary->latin1_coverage = (double)fontdb_count_range_intersection(&summary->coverage, 0x20u, 0xffu) / 224.0;
    summary->bmp_coverage = (double)fontdb_count_range_intersection(&summary->coverage, 0u, 0xffffu) / 65536.0;
}

static int fontdb_snapshot_from_metrics_report(const GlyphFile *file, GlyphFontDbMetricsSnapshot *snapshot) {
    GlyphMetricsReport report;

    memset(&report, 0, sizeof(report));
    if (!glyph_metrics_build_report(file, &report)) {
        glyph_metrics_report_free(&report);
        return 0;
    }
    snapshot->glyph_count = file->glyphs.count;
    snapshot->kerning_count = file->kerning.count;
    snapshot->row_count = file->row_count;
    snapshot->atlas_width = file->atlas_width;
    snapshot->atlas_height = file->atlas_height;
    snapshot->flags = file->flags;
    snapshot->has_bounds = report.bounds.has_bounds;
    snapshot->min_x = report.bounds.min_x;
    snapshot->min_y = report.bounds.min_y;
    snapshot->max_x = report.bounds.max_x;
    snapshot->max_y = report.bounds.max_y;
    snapshot->min_width = report.bounds.min_width;
    snapshot->max_width = report.bounds.max_width;
    snapshot->min_height = report.bounds.min_height;
    snapshot->max_height = report.bounds.max_height;
    snapshot->min_advance = report.advances.min_advance;
    snapshot->max_advance = report.advances.max_advance;
    snapshot->total_advance = report.advances.total_advance;
    snapshot->mean_advance = report.advances.mean_advance;
    snapshot->total_area = report.bounds.total_area;
    snapshot->atlas_pixels = report.atlas_density.atlas_pixels;
    snapshot->ink_pixels = report.atlas_density.ink_pixels;
    snapshot->ink_density = report.atlas_density.density;
    glyph_metrics_report_free(&report);
    return 1;
}

static int fontdb_snapshot_from_file_light(const GlyphFile *file, GlyphFontDbMetricsSnapshot *snapshot) {
    uint32_t i;
    uint64_t total_area = 0;
    int64_t total_advance = 0;

    if (!file || !snapshot || (file->glyphs.count && !file->glyphs.entries)) {
        return 0;
    }
    glyph_fontdb_metrics_snapshot_clear(snapshot);
    snapshot->glyph_count = file->glyphs.count;
    snapshot->kerning_count = file->kerning.count;
    snapshot->row_count = file->row_count;
    snapshot->atlas_width = file->atlas_width;
    snapshot->atlas_height = file->atlas_height;
    snapshot->flags = file->flags;
    snapshot->atlas_pixels = (uint64_t)file->atlas_width * (uint64_t)file->atlas_height;
    for (i = 0; i < file->glyphs.count; i++) {
        const GlyphEntry *glyph = &file->glyphs.entries[i];
        int32_t right = (int32_t)glyph->x + (int32_t)glyph->width;
        int32_t bottom = (int32_t)glyph->y + (int32_t)glyph->height;

        if (!snapshot->has_bounds) {
            snapshot->has_bounds = 1;
            snapshot->min_x = glyph->x;
            snapshot->min_y = glyph->y;
            snapshot->max_x = right;
            snapshot->max_y = bottom;
            snapshot->min_width = glyph->width;
            snapshot->max_width = glyph->width;
            snapshot->min_height = glyph->height;
            snapshot->max_height = glyph->height;
            snapshot->min_advance = glyph->advance;
            snapshot->max_advance = glyph->advance;
        } else {
            if (glyph->x < snapshot->min_x) {
                snapshot->min_x = glyph->x;
            }
            if (glyph->y < snapshot->min_y) {
                snapshot->min_y = glyph->y;
            }
            if (right > snapshot->max_x) {
                snapshot->max_x = right;
            }
            if (bottom > snapshot->max_y) {
                snapshot->max_y = bottom;
            }
            if (glyph->width < snapshot->min_width) {
                snapshot->min_width = glyph->width;
            }
            if (glyph->width > snapshot->max_width) {
                snapshot->max_width = glyph->width;
            }
            if (glyph->height < snapshot->min_height) {
                snapshot->min_height = glyph->height;
            }
            if (glyph->height > snapshot->max_height) {
                snapshot->max_height = glyph->height;
            }
            if (glyph->advance < snapshot->min_advance) {
                snapshot->min_advance = glyph->advance;
            }
            if (glyph->advance > snapshot->max_advance) {
                snapshot->max_advance = glyph->advance;
            }
        }
        total_area += (uint64_t)glyph->width * (uint64_t)glyph->height;
        total_advance += glyph->advance;
    }
    snapshot->total_area = total_area;
    snapshot->total_advance = total_advance;
    if (file->glyphs.count) {
        snapshot->mean_advance = (double)total_advance / (double)file->glyphs.count;
    }
    if (file->atlas_pixels && snapshot->atlas_pixels) {
        uint64_t ink = 0;
        uint64_t p;

        for (p = 0; p < snapshot->atlas_pixels; p++) {
            if (file->atlas_pixels[p]) {
                ink++;
            }
        }
        snapshot->ink_pixels = ink;
        snapshot->ink_density = (double)ink / (double)snapshot->atlas_pixels;
    }
    return 1;
}

static int fontdb_compute_snapshot(const GlyphFile *file, GlyphFontDbMetricsSnapshot *snapshot) {
    if (!file || !snapshot) {
        return 0;
    }
    if (fontdb_snapshot_from_metrics_report(file, snapshot)) {
        return 1;
    }
    return fontdb_snapshot_from_file_light(file, snapshot);
}

static int fontdb_metric_snapshots_equal(const GlyphFontDbMetricsSnapshot *a, const GlyphFontDbMetricsSnapshot *b) {
    return a->glyph_count == b->glyph_count &&
           a->kerning_count == b->kerning_count &&
           a->row_count == b->row_count &&
           a->atlas_width == b->atlas_width &&
           a->atlas_height == b->atlas_height &&
           a->flags == b->flags &&
           a->has_bounds == b->has_bounds &&
           a->min_x == b->min_x &&
           a->min_y == b->min_y &&
           a->max_x == b->max_x &&
           a->max_y == b->max_y &&
           a->min_width == b->min_width &&
           a->max_width == b->max_width &&
           a->min_height == b->min_height &&
           a->max_height == b->max_height &&
           a->min_advance == b->min_advance &&
           a->max_advance == b->max_advance &&
           a->total_advance == b->total_advance &&
           a->total_area == b->total_area &&
           a->atlas_pixels == b->atlas_pixels &&
           a->ink_pixels == b->ink_pixels;
}

static void fontdb_string_init(FontDbString *str) {
    memset(str, 0, sizeof(*str));
}

static void fontdb_string_free(FontDbString *str) {
    if (str) {
        free(str->data);
        memset(str, 0, sizeof(*str));
    }
}

static int fontdb_string_reserve(FontDbString *str, size_t needed) {
    char *next;
    size_t cap;

    if (needed <= str->capacity) {
        return 1;
    }
    cap = str->capacity ? str->capacity : 64u;
    while (cap < needed) {
        if (cap > SIZE_MAX / 2u) {
            cap = needed;
            break;
        }
        cap *= 2u;
    }
    next = (char *)realloc(str->data, cap);
    if (!next) {
        return 0;
    }
    str->data = next;
    str->capacity = cap;
    return 1;
}

static int fontdb_string_append_char(FontDbString *str, char c) {
    if (!fontdb_string_reserve(str, str->length + 2u)) {
        return 0;
    }
    str->data[str->length++] = c;
    str->data[str->length] = 0;
    return 1;
}

static int fontdb_string_append(FontDbString *str, const char *text) {
    size_t len;

    if (!text) {
        text = "";
    }
    len = strlen(text);
    if (!fontdb_string_reserve(str, str->length + len + 1u)) {
        return 0;
    }
    memcpy(str->data + str->length, text, len + 1u);
    str->length += len;
    return 1;
}

static int fontdb_string_append_printf(FontDbString *str, const char *fmt, ...) {
    va_list args;
    va_list copy;
    int needed;

    va_start(args, fmt);
    va_copy(copy, args);
    needed = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (needed < 0) {
        va_end(args);
        return 0;
    }
    if (!fontdb_string_reserve(str, str->length + (size_t)needed + 1u)) {
        va_end(args);
        return 0;
    }
    vsnprintf(str->data + str->length, str->capacity - str->length, fmt, args);
    va_end(args);
    str->length += (size_t)needed;
    return 1;
}

static int fontdb_format_coverage_expr(const GlyphCoverageSet *set, FontDbString *out) {
    uint32_t i;

    if (!set || set->count == 0) {
        return fontdb_string_append(out, "-");
    }
    for (i = 0; i < set->count; i++) {
        const GlyphCoverageRange *range = &set->ranges[i];

        if (i && !fontdb_string_append_char(out, ',')) {
            return 0;
        }
        if (range->first == range->last) {
            if (!fontdb_string_append_printf(out, "U+%04" PRIX32, range->first)) {
                return 0;
            }
        } else if (!fontdb_string_append_printf(out, "U+%04" PRIX32 "-U+%04" PRIX32, range->first, range->last)) {
            return 0;
        }
    }
    return 1;
}

static int fontdb_write_quoted(FILE *fp, const char *text) {
    const unsigned char *s = (const unsigned char *)fontdb_safe_string(text);

    if (fputc('"', fp) == EOF) {
        return 0;
    }
    while (*s) {
        switch (*s) {
        case '\\':
            if (fputs("\\\\", fp) == EOF) {
                return 0;
            }
            break;
        case '"':
            if (fputs("\\\"", fp) == EOF) {
                return 0;
            }
            break;
        case '\n':
            if (fputs("\\n", fp) == EOF) {
                return 0;
            }
            break;
        case '\r':
            if (fputs("\\r", fp) == EOF) {
                return 0;
            }
            break;
        case '\t':
            if (fputs("\\t", fp) == EOF) {
                return 0;
            }
            break;
        default:
            if (*s < 0x20u || *s == 0x7fu) {
                if (fprintf(fp, "\\x%02X", (unsigned int)*s) < 0) {
                    return 0;
                }
            } else if (fputc(*s, fp) == EOF) {
                return 0;
            }
            break;
        }
        s++;
    }
    return fputc('"', fp) != EOF;
}

static void fontdb_skip_space(FontDbLineParser *parser) {
    while (*parser->cursor && isspace((unsigned char)*parser->cursor)) {
        parser->cursor++;
    }
}

static int fontdb_read_word(FontDbLineParser *parser, char *buf, size_t buf_cap) {
    size_t n = 0;

    fontdb_skip_space(parser);
    if (!*parser->cursor) {
        return 0;
    }
    while (*parser->cursor && !isspace((unsigned char)*parser->cursor)) {
        if (n + 1u >= buf_cap) {
            fontdb_parser_error(parser, "token is too long");
            return 0;
        }
        buf[n++] = *parser->cursor++;
    }
    buf[n] = 0;
    return 1;
}

static int fontdb_hex_value(int c) {
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

static int fontdb_read_quoted(FontDbLineParser *parser, char **out) {
    FontDbString str;

    *out = NULL;
    fontdb_skip_space(parser);
    if (*parser->cursor != '"') {
        fontdb_parser_error(parser, "expected quoted string");
        return 0;
    }
    parser->cursor++;
    fontdb_string_init(&str);
    while (*parser->cursor && *parser->cursor != '"') {
        unsigned char c = (unsigned char)*parser->cursor++;

        if (c == '\\') {
            int escaped = (unsigned char)*parser->cursor++;

            if (!escaped) {
                fontdb_string_free(&str);
                fontdb_parser_error(parser, "unterminated escape sequence");
                return 0;
            }
            switch (escaped) {
            case 'n':
                c = '\n';
                break;
            case 'r':
                c = '\r';
                break;
            case 't':
                c = '\t';
                break;
            case '\\':
                c = '\\';
                break;
            case '"':
                c = '"';
                break;
            case 'x': {
                int hi = fontdb_hex_value((unsigned char)parser->cursor[0]);
                int lo = fontdb_hex_value((unsigned char)parser->cursor[1]);

                if (hi < 0 || lo < 0) {
                    fontdb_string_free(&str);
                    fontdb_parser_error(parser, "invalid hex escape");
                    return 0;
                }
                c = (unsigned char)((hi << 4) | lo);
                parser->cursor += 2;
                break;
            }
            default:
                c = (unsigned char)escaped;
                break;
            }
        }
        if (!fontdb_string_append_char(&str, (char)c)) {
            fontdb_string_free(&str);
            fontdb_parser_error(parser, "out of memory while reading string");
            return 0;
        }
    }
    if (*parser->cursor != '"') {
        fontdb_string_free(&str);
        fontdb_parser_error(parser, "unterminated quoted string");
        return 0;
    }
    parser->cursor++;
    if (!str.data) {
        str.data = fontdb_strdup("");
        if (!str.data) {
            fontdb_parser_error(parser, "out of memory while reading string");
            return 0;
        }
    }
    *out = str.data;
    return 1;
}

static int fontdb_read_u32(FontDbLineParser *parser, uint32_t *out) {
    char token[64];
    char *end = NULL;
    unsigned long value;

    if (!fontdb_read_word(parser, token, sizeof(token))) {
        fontdb_parser_error(parser, "expected unsigned integer");
        return 0;
    }
    errno = 0;
    value = strtoul(token, &end, 0);
    if (errno || end == token || *end || value > UINT32_MAX) {
        fontdb_parser_error(parser, "invalid unsigned integer '%s'", token);
        return 0;
    }
    *out = (uint32_t)value;
    return 1;
}

static int fontdb_read_u64(FontDbLineParser *parser, uint64_t *out) {
    char token[64];
    char *end = NULL;
    unsigned long long value;

    if (!fontdb_read_word(parser, token, sizeof(token))) {
        fontdb_parser_error(parser, "expected unsigned integer");
        return 0;
    }
    errno = 0;
    value = strtoull(token, &end, 0);
    if (errno || end == token || *end) {
        fontdb_parser_error(parser, "invalid unsigned integer '%s'", token);
        return 0;
    }
    *out = (uint64_t)value;
    return 1;
}

static int fontdb_read_i32(FontDbLineParser *parser, int32_t *out) {
    char token[64];
    char *end = NULL;
    long value;

    if (!fontdb_read_word(parser, token, sizeof(token))) {
        fontdb_parser_error(parser, "expected signed integer");
        return 0;
    }
    errno = 0;
    value = strtol(token, &end, 0);
    if (errno || end == token || *end || value < INT32_MIN || value > INT32_MAX) {
        fontdb_parser_error(parser, "invalid signed integer '%s'", token);
        return 0;
    }
    *out = (int32_t)value;
    return 1;
}

static int fontdb_read_i64(FontDbLineParser *parser, int64_t *out) {
    char token[64];
    char *end = NULL;
    long long value;

    if (!fontdb_read_word(parser, token, sizeof(token))) {
        fontdb_parser_error(parser, "expected signed integer");
        return 0;
    }
    errno = 0;
    value = strtoll(token, &end, 0);
    if (errno || end == token || *end) {
        fontdb_parser_error(parser, "invalid signed integer '%s'", token);
        return 0;
    }
    *out = (int64_t)value;
    return 1;
}

static int fontdb_read_double(FontDbLineParser *parser, double *out) {
    char token[96];
    char *end = NULL;
    double value;

    if (!fontdb_read_word(parser, token, sizeof(token))) {
        fontdb_parser_error(parser, "expected floating point number");
        return 0;
    }
    errno = 0;
    value = strtod(token, &end);
    if (errno || end == token || *end) {
        fontdb_parser_error(parser, "invalid floating point number '%s'", token);
        return 0;
    }
    *out = value;
    return 1;
}

static int fontdb_line_done(FontDbLineParser *parser) {
    fontdb_skip_space(parser);
    if (*parser->cursor == 0 || *parser->cursor == '#') {
        return 1;
    }
    fontdb_parser_error(parser, "unexpected trailing text");
    return 0;
}

static void fontdb_trim_line(char *line) {
    size_t len = strlen(line);

    while (len && (line[len - 1u] == '\n' || line[len - 1u] == '\r')) {
        line[--len] = 0;
    }
}

void glyph_fontdb_metrics_snapshot_clear(GlyphFontDbMetricsSnapshot *snapshot) {
    if (snapshot) {
        memset(snapshot, 0, sizeof(*snapshot));
    }
}

void glyph_fontdb_coverage_summary_init(GlyphFontDbCoverageSummary *summary) {
    if (!summary) {
        return;
    }
    memset(summary, 0, sizeof(*summary));
    glyph_coverage_init(&summary->coverage);
}

void glyph_fontdb_coverage_summary_free(GlyphFontDbCoverageSummary *summary) {
    if (!summary) {
        return;
    }
    glyph_coverage_free(&summary->coverage);
    memset(summary, 0, sizeof(*summary));
}

void glyph_fontdb_coverage_summary_clear(GlyphFontDbCoverageSummary *summary) {
    if (!summary) {
        return;
    }
    glyph_coverage_clear(&summary->coverage);
    fontdb_update_coverage_summary(summary);
}

int glyph_fontdb_coverage_summary_set(GlyphFontDbCoverageSummary *summary, const GlyphCoverageSet *coverage) {
    if (!summary || !coverage) {
        return 0;
    }
    if (!glyph_coverage_copy(&summary->coverage, coverage)) {
        return 0;
    }
    fontdb_update_coverage_summary(summary);
    return 1;
}

int glyph_fontdb_coverage_summary_from_file(const GlyphFile *file, GlyphFontDbCoverageSummary *summary) {
    GlyphCoverageSet coverage;
    int ok;

    if (!file || !summary) {
        return 0;
    }
    glyph_coverage_init(&coverage);
    ok = glyph_coverage_from_file(file, &coverage);
    if (ok) {
        ok = glyph_fontdb_coverage_summary_set(summary, &coverage);
    }
    glyph_coverage_free(&coverage);
    return ok;
}

void glyph_fontdb_entry_init(GlyphFontDbEntry *entry) {
    if (!entry) {
        return;
    }
    memset(entry, 0, sizeof(*entry));
    glyph_fontdb_coverage_summary_init(&entry->coverage);
}

void glyph_fontdb_entry_free(GlyphFontDbEntry *entry) {
    if (!entry) {
        return;
    }
    free(entry->path);
    free(entry->name);
    free(entry->style);
    glyph_fontdb_coverage_summary_free(&entry->coverage);
    memset(entry, 0, sizeof(*entry));
}

void glyph_fontdb_entry_clear(GlyphFontDbEntry *entry) {
    glyph_fontdb_entry_free(entry);
    glyph_fontdb_entry_init(entry);
}

int glyph_fontdb_entry_copy(GlyphFontDbEntry *dst, const GlyphFontDbEntry *src) {
    GlyphFontDbEntry tmp;

    if (!dst || !src) {
        return 0;
    }
    glyph_fontdb_entry_init(&tmp);
    tmp.id = src->id;
    tmp.user_flags = src->user_flags;
    tmp.metrics = src->metrics;
    if (!glyph_fontdb_entry_set_metadata(&tmp, src->path, src->name, src->style) ||
        !glyph_fontdb_coverage_summary_set(&tmp.coverage, &src->coverage.coverage)) {
        glyph_fontdb_entry_free(&tmp);
        return 0;
    }
    glyph_fontdb_entry_free(dst);
    *dst = tmp;
    return 1;
}

int glyph_fontdb_entry_set_metadata(GlyphFontDbEntry *entry, const char *path, const char *name, const char *style) {
    char *path_copy;
    char *name_copy;
    char *style_copy;

    if (!entry) {
        return 0;
    }
    path_copy = fontdb_strdup(path);
    name_copy = fontdb_strdup(name);
    style_copy = fontdb_strdup(style);
    if (!path_copy || !name_copy || !style_copy) {
        free(path_copy);
        free(name_copy);
        free(style_copy);
        return 0;
    }
    free(entry->path);
    free(entry->name);
    free(entry->style);
    entry->path = path_copy;
    entry->name = name_copy;
    entry->style = style_copy;
    return 1;
}

int glyph_fontdb_entry_set_coverage(GlyphFontDbEntry *entry, const GlyphCoverageSet *coverage) {
    if (!entry) {
        return 0;
    }
    return glyph_fontdb_coverage_summary_set(&entry->coverage, coverage);
}

int glyph_fontdb_entry_set_metrics_from_file(GlyphFontDbEntry *entry, const GlyphFile *file) {
    GlyphFontDbMetricsSnapshot snapshot;

    if (!entry || !file) {
        return 0;
    }
    if (!fontdb_compute_snapshot(file, &snapshot)) {
        return 0;
    }
    entry->metrics = snapshot;
    return 1;
}

int glyph_fontdb_entry_from_file(GlyphFontDbEntry *entry, uint32_t id, const char *path, const char *name, const char *style, const GlyphFile *file) {
    GlyphFontDbEntry tmp;

    if (!entry || !file) {
        return 0;
    }
    glyph_fontdb_entry_init(&tmp);
    tmp.id = id;
    if (!glyph_fontdb_entry_set_metadata(&tmp, path, name, style) ||
        !glyph_fontdb_coverage_summary_from_file(file, &tmp.coverage) ||
        !glyph_fontdb_entry_set_metrics_from_file(&tmp, file)) {
        glyph_fontdb_entry_free(&tmp);
        return 0;
    }
    glyph_fontdb_entry_free(entry);
    *entry = tmp;
    return 1;
}

int glyph_fontdb_entry_has_glyph(const GlyphFontDbEntry *entry, uint32_t glyph_id) {
    if (!entry) {
        return 0;
    }
    return glyph_coverage_contains(&entry->coverage.coverage, glyph_id);
}

int glyph_fontdb_entry_covers(const GlyphFontDbEntry *entry, const GlyphCoverageSet *coverage) {
    uint32_t i;

    if (!entry || !coverage) {
        return 0;
    }
    for (i = 0; i < coverage->count; i++) {
        if (!glyph_coverage_contains_range(&entry->coverage.coverage, coverage->ranges[i].first, coverage->ranges[i].last)) {
            return 0;
        }
    }
    return 1;
}

void glyph_fontdb_init(GlyphFontDb *db) {
    if (!db) {
        return;
    }
    memset(db, 0, sizeof(*db));
    db->next_id = 1u;
}

void glyph_fontdb_free(GlyphFontDb *db) {
    uint32_t i;

    if (!db) {
        return;
    }
    for (i = 0; i < db->count; i++) {
        glyph_fontdb_entry_free(&db->entries[i]);
    }
    free(db->entries);
    memset(db, 0, sizeof(*db));
}

void glyph_fontdb_clear(GlyphFontDb *db) {
    if (!db) {
        return;
    }
    glyph_fontdb_free(db);
    glyph_fontdb_init(db);
}

int glyph_fontdb_reserve(GlyphFontDb *db, uint32_t capacity) {
    GlyphFontDbEntry *next;
    uint32_t cap;
    size_t bytes;
    uint32_t i;

    if (!db) {
        return 0;
    }
    if (capacity <= db->capacity) {
        return 1;
    }
    cap = db->capacity ? db->capacity : FONTDB_INITIAL_CAPACITY;
    while (cap < capacity) {
        if (cap > UINT32_MAX / 2u) {
            cap = capacity;
            break;
        }
        cap *= 2u;
    }
    if (!fontdb_mul_size((size_t)cap, sizeof(*db->entries), &bytes)) {
        return 0;
    }
    next = (GlyphFontDbEntry *)realloc(db->entries, bytes);
    if (!next) {
        return 0;
    }
    db->entries = next;
    for (i = db->capacity; i < cap; i++) {
        glyph_fontdb_entry_init(&db->entries[i]);
    }
    db->capacity = cap;
    return 1;
}

int glyph_fontdb_add(GlyphFontDb *db, const GlyphFontDbEntry *entry, uint32_t *index) {
    uint32_t out_index;

    if (!db || !entry) {
        return 0;
    }
    if (!glyph_fontdb_reserve(db, db->count + 1u)) {
        return 0;
    }
    out_index = db->count;
    glyph_fontdb_entry_clear(&db->entries[out_index]);
    if (!glyph_fontdb_entry_copy(&db->entries[out_index], entry)) {
        glyph_fontdb_entry_init(&db->entries[out_index]);
        return 0;
    }
    if (db->entries[out_index].id == 0) {
        db->entries[out_index].id = db->next_id++;
    } else if (db->entries[out_index].id >= db->next_id) {
        db->next_id = db->entries[out_index].id + 1u;
    }
    db->count++;
    if (index) {
        *index = out_index;
    }
    return 1;
}

int glyph_fontdb_add_file(GlyphFontDb *db, const char *path, const char *name, const char *style, const GlyphFile *file, uint32_t *index) {
    GlyphFontDbEntry entry;
    int ok;

    if (!db || !file) {
        return 0;
    }
    glyph_fontdb_entry_init(&entry);
    ok = glyph_fontdb_entry_from_file(&entry, db->next_id, path, name, style, file) &&
         glyph_fontdb_add(db, &entry, index);
    glyph_fontdb_entry_free(&entry);
    return ok;
}

int glyph_fontdb_remove_index(GlyphFontDb *db, uint32_t index) {
    if (!db || index >= db->count) {
        return 0;
    }
    glyph_fontdb_entry_free(&db->entries[index]);
    if (index + 1u < db->count) {
        memmove(&db->entries[index], &db->entries[index + 1u], (size_t)(db->count - index - 1u) * sizeof(db->entries[0]));
    }
    db->count--;
    glyph_fontdb_entry_init(&db->entries[db->count]);
    return 1;
}

int glyph_fontdb_remove_id(GlyphFontDb *db, uint32_t id) {
    uint32_t index;

    if (!glyph_fontdb_find_id_index(db, id, &index)) {
        return 0;
    }
    return glyph_fontdb_remove_index(db, index);
}

int glyph_fontdb_remove_path(GlyphFontDb *db, const char *path) {
    uint32_t index;

    if (!glyph_fontdb_find_path_index(db, path, &index)) {
        return 0;
    }
    return glyph_fontdb_remove_index(db, index);
}

int glyph_fontdb_find_id_index(const GlyphFontDb *db, uint32_t id, uint32_t *index) {
    uint32_t i;

    if (!db) {
        return 0;
    }
    for (i = 0; i < db->count; i++) {
        if (db->entries[i].id == id) {
            if (index) {
                *index = i;
            }
            return 1;
        }
    }
    return 0;
}

int glyph_fontdb_find_path_index(const GlyphFontDb *db, const char *path, uint32_t *index) {
    uint32_t i;

    if (!db || !path) {
        return 0;
    }
    for (i = 0; i < db->count; i++) {
        if (fontdb_string_equal(db->entries[i].path, path)) {
            if (index) {
                *index = i;
            }
            return 1;
        }
    }
    return 0;
}

int glyph_fontdb_find_name_style_index(const GlyphFontDb *db, const char *name, const char *style, uint32_t *index) {
    uint32_t i;

    if (!db || !name) {
        return 0;
    }
    for (i = 0; i < db->count; i++) {
        if (fontdb_string_equal(db->entries[i].name, name) && fontdb_string_equal(db->entries[i].style, style)) {
            if (index) {
                *index = i;
            }
            return 1;
        }
    }
    return 0;
}

GlyphFontDbEntry *glyph_fontdb_find_id(GlyphFontDb *db, uint32_t id) {
    uint32_t index;

    if (!glyph_fontdb_find_id_index(db, id, &index)) {
        return NULL;
    }
    return &db->entries[index];
}

const GlyphFontDbEntry *glyph_fontdb_find_id_const(const GlyphFontDb *db, uint32_t id) {
    uint32_t index;

    if (!glyph_fontdb_find_id_index(db, id, &index)) {
        return NULL;
    }
    return &db->entries[index];
}

GlyphFontDbEntry *glyph_fontdb_find_path(GlyphFontDb *db, const char *path) {
    uint32_t index;

    if (!glyph_fontdb_find_path_index(db, path, &index)) {
        return NULL;
    }
    return &db->entries[index];
}

const GlyphFontDbEntry *glyph_fontdb_find_path_const(const GlyphFontDb *db, const char *path) {
    uint32_t index;

    if (!glyph_fontdb_find_path_index(db, path, &index)) {
        return NULL;
    }
    return &db->entries[index];
}

GlyphFontDbEntry *glyph_fontdb_find_name_style(GlyphFontDb *db, const char *name, const char *style) {
    uint32_t index;

    if (!glyph_fontdb_find_name_style_index(db, name, style, &index)) {
        return NULL;
    }
    return &db->entries[index];
}

const GlyphFontDbEntry *glyph_fontdb_find_name_style_const(const GlyphFontDb *db, const char *name, const char *style) {
    uint32_t index;

    if (!glyph_fontdb_find_name_style_index(db, name, style, &index)) {
        return NULL;
    }
    return &db->entries[index];
}

void glyph_fontdb_filter_init(GlyphFontDbFilter *filter) {
    if (filter) {
        memset(filter, 0, sizeof(*filter));
        filter->max_glyphs = UINT64_MAX;
        filter->max_ranges = UINT32_MAX;
    }
}

int glyph_fontdb_filter_match(const GlyphFontDbEntry *entry, const GlyphFontDbFilter *filter) {
    if (!entry) {
        return 0;
    }
    if (!filter) {
        return 1;
    }
    if (!fontdb_string_contains_ci(entry->path, filter->path_contains) ||
        !fontdb_string_contains_ci(entry->name, filter->name_contains) ||
        !fontdb_string_contains_ci(entry->style, filter->style_contains)) {
        return 0;
    }
    if (filter->use_requires_glyph && !glyph_fontdb_entry_has_glyph(entry, filter->requires_glyph)) {
        return 0;
    }
    if (filter->requires_coverage && !glyph_fontdb_entry_covers(entry, filter->requires_coverage)) {
        return 0;
    }
    if (entry->coverage.id_count < filter->min_glyphs || entry->coverage.id_count > filter->max_glyphs) {
        return 0;
    }
    if (entry->coverage.range_count < filter->min_ranges || entry->coverage.range_count > filter->max_ranges) {
        return 0;
    }
    if ((entry->user_flags & filter->user_flags_mask) != filter->user_flags_value) {
        return 0;
    }
    return 1;
}

void glyph_fontdb_index_list_init(GlyphFontDbIndexList *list) {
    if (list) {
        memset(list, 0, sizeof(*list));
    }
}

void glyph_fontdb_index_list_free(GlyphFontDbIndexList *list) {
    if (!list) {
        return;
    }
    free(list->indices);
    memset(list, 0, sizeof(*list));
}

void glyph_fontdb_index_list_clear(GlyphFontDbIndexList *list) {
    if (list) {
        list->count = 0;
    }
}

int glyph_fontdb_index_list_add(GlyphFontDbIndexList *list, uint32_t index) {
    uint32_t cap;
    uint32_t *next;
    size_t bytes;

    if (!list) {
        return 0;
    }
    if (list->count == list->capacity) {
        cap = list->capacity ? list->capacity * 2u : FONTDB_INITIAL_CAPACITY;
        if (cap < list->count + 1u) {
            cap = list->count + 1u;
        }
        if (!fontdb_mul_size((size_t)cap, sizeof(*list->indices), &bytes)) {
            return 0;
        }
        next = (uint32_t *)realloc(list->indices, bytes);
        if (!next) {
            return 0;
        }
        list->indices = next;
        list->capacity = cap;
    }
    list->indices[list->count++] = index;
    return 1;
}

int glyph_fontdb_filter(const GlyphFontDb *db, const GlyphFontDbFilter *filter, GlyphFontDbIndexList *out) {
    uint32_t i;

    if (!db || !out) {
        return 0;
    }
    glyph_fontdb_index_list_clear(out);
    for (i = 0; i < db->count; i++) {
        if (glyph_fontdb_filter_match(&db->entries[i], filter) && !glyph_fontdb_index_list_add(out, i)) {
            return 0;
        }
    }
    return 1;
}

int glyph_fontdb_query_glyph(const GlyphFontDb *db, uint32_t glyph_id, GlyphFontDbIndexList *out) {
    GlyphFontDbFilter filter;

    glyph_fontdb_filter_init(&filter);
    filter.use_requires_glyph = 1;
    filter.requires_glyph = glyph_id;
    return glyph_fontdb_filter(db, &filter, out);
}

int glyph_fontdb_query_coverage(const GlyphFontDb *db, const GlyphCoverageSet *coverage, GlyphFontDbIndexList *out) {
    GlyphFontDbFilter filter;

    if (!coverage) {
        return 0;
    }
    glyph_fontdb_filter_init(&filter);
    filter.requires_coverage = coverage;
    return glyph_fontdb_filter(db, &filter, out);
}

int glyph_fontdb_compare_entries(const GlyphFontDbEntry *a, const GlyphFontDbEntry *b, GlyphFontDbSort sort) {
    int result = 0;

    if (!a || !b) {
        result = fontdb_u32_compare(a != NULL, b != NULL);
    } else {
        switch (sort.key) {
        case GLYPH_FONTDB_SORT_ID:
            result = fontdb_u32_compare(a->id, b->id);
            break;
        case GLYPH_FONTDB_SORT_PATH:
            result = fontdb_strcmp_ci(a->path, b->path);
            break;
        case GLYPH_FONTDB_SORT_NAME:
            result = fontdb_strcmp_ci(a->name, b->name);
            if (!result) {
                result = fontdb_strcmp_ci(a->style, b->style);
            }
            break;
        case GLYPH_FONTDB_SORT_STYLE:
            result = fontdb_strcmp_ci(a->style, b->style);
            if (!result) {
                result = fontdb_strcmp_ci(a->name, b->name);
            }
            break;
        case GLYPH_FONTDB_SORT_GLYPH_COUNT:
            result = fontdb_u64_compare(a->coverage.id_count, b->coverage.id_count);
            break;
        case GLYPH_FONTDB_SORT_COVERAGE_FIRST:
            result = fontdb_u32_compare(a->coverage.min_id, b->coverage.min_id);
            break;
        case GLYPH_FONTDB_SORT_COVERAGE_LAST:
            result = fontdb_u32_compare(a->coverage.max_id, b->coverage.max_id);
            break;
        case GLYPH_FONTDB_SORT_ATLAS_AREA:
            result = fontdb_u64_compare(fontdb_atlas_area(a), fontdb_atlas_area(b));
            break;
        case GLYPH_FONTDB_SORT_MEAN_ADVANCE:
            result = fontdb_double_compare(a->metrics.mean_advance, b->metrics.mean_advance);
            break;
        default:
            result = fontdb_u32_compare(a->id, b->id);
            break;
        }
        if (!result) {
            result = fontdb_u32_compare(a->id, b->id);
        }
    }
    return sort.descending ? -result : result;
}

static int fontdb_qsort_compare(const void *lhs, const void *rhs) {
    return glyph_fontdb_compare_entries((const GlyphFontDbEntry *)lhs, (const GlyphFontDbEntry *)rhs, g_fontdb_sort);
}

void glyph_fontdb_sort(GlyphFontDb *db, GlyphFontDbSort sort) {
    if (!db || db->count < 2u) {
        return;
    }
    g_fontdb_sort = sort;
    qsort(db->entries, db->count, sizeof(*db->entries), fontdb_qsort_compare);
}

void glyph_fontdb_duplicate_list_init(GlyphFontDbDuplicateList *list) {
    if (list) {
        memset(list, 0, sizeof(*list));
    }
}

void glyph_fontdb_duplicate_list_free(GlyphFontDbDuplicateList *list) {
    if (!list) {
        return;
    }
    free(list->items);
    memset(list, 0, sizeof(*list));
}

void glyph_fontdb_duplicate_list_clear(GlyphFontDbDuplicateList *list) {
    if (list) {
        list->count = 0;
    }
}

int glyph_fontdb_duplicate_list_add(GlyphFontDbDuplicateList *list, uint32_t first_index, uint32_t second_index, uint32_t flags) {
    uint32_t cap;
    GlyphFontDbDuplicate *next;
    size_t bytes;

    if (!list) {
        return 0;
    }
    if (list->count == list->capacity) {
        cap = list->capacity ? list->capacity * 2u : FONTDB_INITIAL_CAPACITY;
        if (cap < list->count + 1u) {
            cap = list->count + 1u;
        }
        if (!fontdb_mul_size((size_t)cap, sizeof(*list->items), &bytes)) {
            return 0;
        }
        next = (GlyphFontDbDuplicate *)realloc(list->items, bytes);
        if (!next) {
            return 0;
        }
        list->items = next;
        list->capacity = cap;
    }
    list->items[list->count].first_index = first_index;
    list->items[list->count].second_index = second_index;
    list->items[list->count].flags = flags;
    list->count++;
    return 1;
}

uint32_t glyph_fontdb_duplicate_flags(const GlyphFontDbEntry *a, const GlyphFontDbEntry *b) {
    uint32_t flags = 0;

    if (!a || !b) {
        return 0;
    }
    if (*fontdb_safe_string(a->path) && fontdb_string_equal(a->path, b->path)) {
        flags |= GLYPH_FONTDB_DUPLICATE_PATH;
    }
    if (*fontdb_safe_string(a->name) && fontdb_string_equal(a->name, b->name) && fontdb_string_equal(a->style, b->style)) {
        flags |= GLYPH_FONTDB_DUPLICATE_NAME_STYLE;
    }
    if (glyph_coverage_equals(&a->coverage.coverage, &b->coverage.coverage)) {
        flags |= GLYPH_FONTDB_DUPLICATE_COVERAGE;
    }
    if (fontdb_metric_snapshots_equal(&a->metrics, &b->metrics)) {
        flags |= GLYPH_FONTDB_DUPLICATE_METRICS;
    }
    if ((flags & (GLYPH_FONTDB_DUPLICATE_PATH |
                  GLYPH_FONTDB_DUPLICATE_NAME_STYLE |
                  GLYPH_FONTDB_DUPLICATE_COVERAGE |
                  GLYPH_FONTDB_DUPLICATE_METRICS)) ==
        (GLYPH_FONTDB_DUPLICATE_PATH |
         GLYPH_FONTDB_DUPLICATE_NAME_STYLE |
         GLYPH_FONTDB_DUPLICATE_COVERAGE |
         GLYPH_FONTDB_DUPLICATE_METRICS)) {
        flags |= GLYPH_FONTDB_DUPLICATE_EXACT;
    }
    return flags;
}

int glyph_fontdb_find_duplicates(const GlyphFontDb *db, uint32_t match_flags, GlyphFontDbDuplicateList *out) {
    uint32_t i;

    if (!db || !out) {
        return 0;
    }
    glyph_fontdb_duplicate_list_clear(out);
    if (match_flags == 0) {
        match_flags = GLYPH_FONTDB_DUPLICATE_PATH | GLYPH_FONTDB_DUPLICATE_NAME_STYLE;
    }
    for (i = 0; i < db->count; i++) {
        uint32_t j;

        for (j = i + 1u; j < db->count; j++) {
            uint32_t flags = glyph_fontdb_duplicate_flags(&db->entries[i], &db->entries[j]);

            if ((flags & match_flags) == match_flags && !glyph_fontdb_duplicate_list_add(out, i, j, flags)) {
                return 0;
            }
        }
    }
    return 1;
}

static int fontdb_write_entry_line(FILE *fp, const GlyphFontDbEntry *entry) {
    FontDbString coverage;
    int ok;

    fontdb_string_init(&coverage);
    ok = fontdb_format_coverage_expr(&entry->coverage.coverage, &coverage);
    if (!ok) {
        fontdb_string_free(&coverage);
        return 0;
    }
    ok = fprintf(fp, "font %" PRIu32 " ", entry->id) >= 0 &&
         fontdb_write_quoted(fp, entry->path) &&
         fputc(' ', fp) != EOF &&
         fontdb_write_quoted(fp, entry->name) &&
         fputc(' ', fp) != EOF &&
         fontdb_write_quoted(fp, entry->style) &&
         fputc(' ', fp) != EOF &&
         fontdb_write_quoted(fp, coverage.data) &&
         fprintf(fp,
                 " %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32
                 " %" PRId32 " %" PRId32 " %" PRId32 " %" PRId32
                 " %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32
                 " %" PRId16 " %" PRId16 " %" PRId64 " %.17g"
                 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %.17g %" PRIu32 "\n",
                 entry->metrics.glyph_count,
                 entry->metrics.kerning_count,
                 entry->metrics.row_count,
                 entry->metrics.atlas_width,
                 entry->metrics.atlas_height,
                 (uint32_t)entry->metrics.flags,
                 (uint32_t)entry->metrics.has_bounds,
                 entry->metrics.min_x,
                 entry->metrics.min_y,
                 entry->metrics.max_x,
                 entry->metrics.max_y,
                 (uint32_t)entry->metrics.min_width,
                 (uint32_t)entry->metrics.max_width,
                 (uint32_t)entry->metrics.min_height,
                 (uint32_t)entry->metrics.max_height,
                 entry->metrics.min_advance,
                 entry->metrics.max_advance,
                 entry->metrics.total_advance,
                 entry->metrics.mean_advance,
                 entry->metrics.total_area,
                 entry->metrics.atlas_pixels,
                 entry->metrics.ink_pixels,
                 entry->metrics.ink_density,
                 entry->user_flags) >= 0;
    fontdb_string_free(&coverage);
    return ok;
}

int glyph_fontdb_write(FILE *fp, const GlyphFontDb *db) {
    uint32_t i;

    if (!fp || !db) {
        return 0;
    }
    if (fprintf(fp, "glyph-fontdb %u\nnext-id %" PRIu32 "\n", FONTDB_FORMAT_VERSION, db->next_id) < 0) {
        return 0;
    }
    for (i = 0; i < db->count; i++) {
        if (!fontdb_write_entry_line(fp, &db->entries[i])) {
            return 0;
        }
    }
    return ferror(fp) == 0;
}

static int fontdb_parse_font_line(FontDbLineParser *parser, GlyphFontDbEntry *entry) {
    char *path = NULL;
    char *name = NULL;
    char *style = NULL;
    char *coverage_expr = NULL;
    GlyphCoverageSet coverage;
    uint32_t tmp_u32;
    int32_t tmp_i32;
    int ok = 0;

    glyph_coverage_init(&coverage);
    glyph_fontdb_entry_init(entry);
    if (!fontdb_read_u32(parser, &entry->id) ||
        !fontdb_read_quoted(parser, &path) ||
        !fontdb_read_quoted(parser, &name) ||
        !fontdb_read_quoted(parser, &style) ||
        !fontdb_read_quoted(parser, &coverage_expr)) {
        goto done;
    }
    if (!glyph_fontdb_entry_set_metadata(entry, path, name, style)) {
        fontdb_parser_error(parser, "out of memory while setting metadata");
        goto done;
    }
    if (strcmp(coverage_expr, "-") != 0 &&
        !glyph_coverage_parse_expression(coverage_expr, &coverage, parser->error, parser->error_cap)) {
        goto done;
    }
    if (!glyph_fontdb_entry_set_coverage(entry, &coverage)) {
        fontdb_parser_error(parser, "out of memory while setting coverage");
        goto done;
    }
    if (!fontdb_read_u32(parser, &entry->metrics.glyph_count) ||
        !fontdb_read_u32(parser, &entry->metrics.kerning_count) ||
        !fontdb_read_u32(parser, &entry->metrics.row_count) ||
        !fontdb_read_u32(parser, &entry->metrics.atlas_width) ||
        !fontdb_read_u32(parser, &entry->metrics.atlas_height) ||
        !fontdb_read_u32(parser, &tmp_u32)) {
        goto done;
    }
    entry->metrics.flags = (uint16_t)tmp_u32;
    if (!fontdb_read_u32(parser, &tmp_u32)) {
        goto done;
    }
    entry->metrics.has_bounds = (int)tmp_u32;
    if (!fontdb_read_i32(parser, &entry->metrics.min_x) ||
        !fontdb_read_i32(parser, &entry->metrics.min_y) ||
        !fontdb_read_i32(parser, &entry->metrics.max_x) ||
        !fontdb_read_i32(parser, &entry->metrics.max_y)) {
        goto done;
    }
    if (!fontdb_read_u32(parser, &tmp_u32)) {
        goto done;
    }
    entry->metrics.min_width = (uint16_t)tmp_u32;
    if (!fontdb_read_u32(parser, &tmp_u32)) {
        goto done;
    }
    entry->metrics.max_width = (uint16_t)tmp_u32;
    if (!fontdb_read_u32(parser, &tmp_u32)) {
        goto done;
    }
    entry->metrics.min_height = (uint16_t)tmp_u32;
    if (!fontdb_read_u32(parser, &tmp_u32)) {
        goto done;
    }
    entry->metrics.max_height = (uint16_t)tmp_u32;
    if (!fontdb_read_i32(parser, &tmp_i32)) {
        goto done;
    }
    entry->metrics.min_advance = (int16_t)tmp_i32;
    if (!fontdb_read_i32(parser, &tmp_i32)) {
        goto done;
    }
    entry->metrics.max_advance = (int16_t)tmp_i32;
    if (!fontdb_read_i64(parser, &entry->metrics.total_advance) ||
        !fontdb_read_double(parser, &entry->metrics.mean_advance) ||
        !fontdb_read_u64(parser, &entry->metrics.total_area) ||
        !fontdb_read_u64(parser, &entry->metrics.atlas_pixels) ||
        !fontdb_read_u64(parser, &entry->metrics.ink_pixels) ||
        !fontdb_read_double(parser, &entry->metrics.ink_density) ||
        !fontdb_read_u32(parser, &entry->user_flags) ||
        !fontdb_line_done(parser)) {
        goto done;
    }
    fontdb_update_coverage_summary(&entry->coverage);
    ok = 1;

done:
    free(path);
    free(name);
    free(style);
    free(coverage_expr);
    glyph_coverage_free(&coverage);
    if (!ok) {
        glyph_fontdb_entry_free(entry);
    }
    return ok;
}

int glyph_fontdb_read(FILE *fp, GlyphFontDb *db, char *error, size_t error_cap) {
    GlyphFontDb tmp;
    char *line;
    uint32_t line_no = 0;
    int saw_header = 0;
    int ok = 0;

    if (!fp || !db) {
        fontdb_set_error(error, error_cap, "invalid font database read arguments");
        return 0;
    }
    fontdb_clear_error(error, error_cap);
    line = (char *)malloc(FONTDB_LINE_CAP);
    if (!line) {
        fontdb_set_error(error, error_cap, "out of memory while reading font database");
        return 0;
    }
    glyph_fontdb_init(&tmp);
    while (fgets(line, FONTDB_LINE_CAP, fp)) {
        FontDbLineParser parser;
        char word[64];

        line_no++;
        if (strlen(line) == FONTDB_LINE_CAP - 1u && line[FONTDB_LINE_CAP - 2u] != '\n') {
            fontdb_set_error(error, error_cap, "line %" PRIu32 ": line is too long", line_no);
            goto done;
        }
        fontdb_trim_line(line);
        parser.cursor = line;
        parser.error = error;
        parser.error_cap = error_cap;
        parser.line_no = line_no;
        fontdb_skip_space(&parser);
        if (*parser.cursor == 0 || *parser.cursor == '#') {
            continue;
        }
        if (!fontdb_read_word(&parser, word, sizeof(word))) {
            goto done;
        }
        if (strcmp(word, "glyph-fontdb") == 0) {
            uint32_t version;

            if (saw_header) {
                fontdb_parser_error(&parser, "duplicate database header");
                goto done;
            }
            if (!fontdb_read_u32(&parser, &version) || !fontdb_line_done(&parser)) {
                goto done;
            }
            if (version != FONTDB_FORMAT_VERSION) {
                fontdb_parser_error(&parser, "unsupported font database version %" PRIu32, version);
                goto done;
            }
            saw_header = 1;
        } else if (strcmp(word, "next-id") == 0) {
            if (!fontdb_read_u32(&parser, &tmp.next_id) || !fontdb_line_done(&parser)) {
                goto done;
            }
            if (tmp.next_id == 0) {
                tmp.next_id = 1u;
            }
        } else if (strcmp(word, "font") == 0) {
            GlyphFontDbEntry entry;
            uint32_t index;

            if (!saw_header) {
                fontdb_parser_error(&parser, "missing glyph-fontdb header before font entry");
                goto done;
            }
            if (!fontdb_parse_font_line(&parser, &entry)) {
                goto done;
            }
            if (!glyph_fontdb_add(&tmp, &entry, &index)) {
                glyph_fontdb_entry_free(&entry);
                fontdb_set_error(error, error_cap, "line %" PRIu32 ": out of memory while adding font entry", line_no);
                goto done;
            }
            glyph_fontdb_entry_free(&entry);
        } else {
            fontdb_parser_error(&parser, "unknown record '%s'", word);
            goto done;
        }
    }
    if (ferror(fp)) {
        fontdb_set_error(error, error_cap, "error while reading font database");
        goto done;
    }
    if (!saw_header) {
        fontdb_set_error(error, error_cap, "missing glyph-fontdb header");
        goto done;
    }
    glyph_fontdb_free(db);
    *db = tmp;
    glyph_fontdb_init(&tmp);
    ok = 1;

done:
    glyph_fontdb_free(&tmp);
    free(line);
    return ok;
}

int glyph_fontdb_load(const char *path, GlyphFontDb *db, char *error, size_t error_cap) {
    FILE *fp;
    int ok;

    if (!path || !db) {
        fontdb_set_error(error, error_cap, "invalid font database load arguments");
        return 0;
    }
    fp = fopen(path, "rb");
    if (!fp) {
        fontdb_set_error(error, error_cap, "could not open '%s' for reading", path);
        return 0;
    }
    ok = glyph_fontdb_read(fp, db, error, error_cap);
    if (fclose(fp) != 0 && ok) {
        fontdb_set_error(error, error_cap, "could not close '%s' after reading", path);
        ok = 0;
    }
    return ok;
}

int glyph_fontdb_save(const char *path, const GlyphFontDb *db, char *error, size_t error_cap) {
    FILE *fp;
    int ok;

    if (!path || !db) {
        fontdb_set_error(error, error_cap, "invalid font database save arguments");
        return 0;
    }
    fontdb_clear_error(error, error_cap);
    fp = fopen(path, "wb");
    if (!fp) {
        fontdb_set_error(error, error_cap, "could not open '%s' for writing", path);
        return 0;
    }
    ok = glyph_fontdb_write(fp, db);
    if (fclose(fp) != 0 && ok) {
        fontdb_set_error(error, error_cap, "could not close '%s' after writing", path);
        ok = 0;
    }
    if (!ok && error && error_cap && !error[0]) {
        fontdb_set_error(error, error_cap, "could not write '%s'", path);
    }
    return ok;
}

int glyph_fontdb_format_entry(const GlyphFontDbEntry *entry, char *buf, size_t buf_cap) {
    int written;

    if (!entry || !buf || buf_cap == 0) {
        return 0;
    }
    written = snprintf(buf, buf_cap,
                       "%" PRIu32 " \"%s\" \"%s\" style=\"%s\" glyphs=%" PRIu64
                       " ranges=%" PRIu32 " U+%04" PRIX32 "..U+%04" PRIX32
                       " atlas=%" PRIu32 "x%" PRIu32 " mean_advance=%.2f",
                       entry->id,
                       fontdb_safe_string(entry->name),
                       fontdb_safe_string(entry->path),
                       fontdb_safe_string(entry->style),
                       entry->coverage.id_count,
                       entry->coverage.range_count,
                       entry->coverage.min_id,
                       entry->coverage.max_id,
                       entry->metrics.atlas_width,
                       entry->metrics.atlas_height,
                       entry->metrics.mean_advance);
    return written >= 0 && (size_t)written < buf_cap;
}

int glyph_fontdb_write_entry_report(FILE *fp, const GlyphFontDbEntry *entry) {
    if (!fp || !entry) {
        return 0;
    }
    if (fprintf(fp,
                "Font %" PRIu32 "\n"
                "  path: %s\n"
                "  name: %s\n"
                "  style: %s\n"
                "  coverage: %" PRIu64 " glyphs in %" PRIu32 " ranges",
                entry->id,
                fontdb_safe_string(entry->path),
                fontdb_safe_string(entry->name),
                fontdb_safe_string(entry->style),
                entry->coverage.id_count,
                entry->coverage.range_count) < 0) {
        return 0;
    }
    if (entry->coverage.id_count) {
        if (fprintf(fp, " (U+%04" PRIX32 "..U+%04" PRIX32 ")", entry->coverage.min_id, entry->coverage.max_id) < 0) {
            return 0;
        }
    }
    if (fprintf(fp,
                "\n"
                "  ascii: %.1f%%  latin1: %.1f%%  bmp: %.2f%%\n"
                "  metrics: glyphs=%" PRIu32 " kerning=%" PRIu32 " rows=%" PRIu32
                " atlas=%" PRIu32 "x%" PRIu32 " ink=%.2f%%\n"
                "  advances: min=%" PRId16 " max=%" PRId16 " mean=%.2f total=%" PRId64 "\n"
                "  bounds: %s min=(%" PRId32 ",%" PRId32 ") max=(%" PRId32 ",%" PRId32 ")"
                " size=%" PRIu16 "..%" PRIu16 " x %" PRIu16 "..%" PRIu16 "\n",
                entry->coverage.ascii_coverage * 100.0,
                entry->coverage.latin1_coverage * 100.0,
                entry->coverage.bmp_coverage * 100.0,
                entry->metrics.glyph_count,
                entry->metrics.kerning_count,
                entry->metrics.row_count,
                entry->metrics.atlas_width,
                entry->metrics.atlas_height,
                entry->metrics.ink_density * 100.0,
                entry->metrics.min_advance,
                entry->metrics.max_advance,
                entry->metrics.mean_advance,
                entry->metrics.total_advance,
                entry->metrics.has_bounds ? "yes" : "no",
                entry->metrics.min_x,
                entry->metrics.min_y,
                entry->metrics.max_x,
                entry->metrics.max_y,
                entry->metrics.min_width,
                entry->metrics.max_width,
                entry->metrics.min_height,
                entry->metrics.max_height) < 0) {
        return 0;
    }
    return ferror(fp) == 0;
}

int glyph_fontdb_write_report(FILE *fp, const GlyphFontDb *db) {
    uint32_t i;
    uint64_t total_glyphs = 0;
    uint64_t total_ranges = 0;
    uint64_t total_atlas_pixels = 0;
    uint64_t total_ink_pixels = 0;

    if (!fp || !db) {
        return 0;
    }
    for (i = 0; i < db->count; i++) {
        total_glyphs += db->entries[i].coverage.id_count;
        total_ranges += db->entries[i].coverage.range_count;
        total_atlas_pixels += db->entries[i].metrics.atlas_pixels;
        total_ink_pixels += db->entries[i].metrics.ink_pixels;
    }
    if (fprintf(fp,
                "Font database\n"
                "  entries: %" PRIu32 "\n"
                "  next id: %" PRIu32 "\n"
                "  total covered glyph ids: %" PRIu64 "\n"
                "  total coverage ranges: %" PRIu64 "\n"
                "  total atlas pixels: %" PRIu64 "\n"
                "  total ink pixels: %" PRIu64 "\n",
                db->count,
                db->next_id,
                total_glyphs,
                total_ranges,
                total_atlas_pixels,
                total_ink_pixels) < 0) {
        return 0;
    }
    for (i = 0; i < db->count; i++) {
        char line[512];

        if (!glyph_fontdb_format_entry(&db->entries[i], line, sizeof(line))) {
            return 0;
        }
        if (fprintf(fp, "  %s\n", line) < 0) {
            return 0;
        }
    }
    return ferror(fp) == 0;
}

static const char *fontdb_duplicate_flag_names(uint32_t flags, char *buf, size_t cap) {
    int first = 1;

    if (!buf || cap == 0) {
        return "";
    }
    buf[0] = 0;
#define APPEND_FLAG(name) \
    do { \
        size_t len = strlen(buf); \
        snprintf(buf + len, cap > len ? cap - len : 0, "%s%s", first ? "" : "|", name); \
        first = 0; \
    } while (0)
    if (flags & GLYPH_FONTDB_DUPLICATE_PATH) {
        APPEND_FLAG("path");
    }
    if (flags & GLYPH_FONTDB_DUPLICATE_NAME_STYLE) {
        APPEND_FLAG("name-style");
    }
    if (flags & GLYPH_FONTDB_DUPLICATE_COVERAGE) {
        APPEND_FLAG("coverage");
    }
    if (flags & GLYPH_FONTDB_DUPLICATE_METRICS) {
        APPEND_FLAG("metrics");
    }
    if (flags & GLYPH_FONTDB_DUPLICATE_EXACT) {
        APPEND_FLAG("exact");
    }
#undef APPEND_FLAG
    if (first) {
        snprintf(buf, cap, "none");
    }
    return buf;
}

int glyph_fontdb_write_duplicate_report(FILE *fp, const GlyphFontDb *db, const GlyphFontDbDuplicateList *duplicates) {
    uint32_t i;

    if (!fp || !db || !duplicates) {
        return 0;
    }
    if (fprintf(fp, "Font database duplicates: %" PRIu32 "\n", duplicates->count) < 0) {
        return 0;
    }
    for (i = 0; i < duplicates->count; i++) {
        const GlyphFontDbDuplicate *dup = &duplicates->items[i];
        char flags[96];

        if (dup->first_index >= db->count || dup->second_index >= db->count) {
            continue;
        }
        if (fprintf(fp,
                    "  %" PRIu32 " <-> %" PRIu32 " (%s): %s / %s\n",
                    db->entries[dup->first_index].id,
                    db->entries[dup->second_index].id,
                    fontdb_duplicate_flag_names(dup->flags, flags, sizeof(flags)),
                    fontdb_safe_string(db->entries[dup->first_index].name),
                    fontdb_safe_string(db->entries[dup->second_index].name)) < 0) {
            return 0;
        }
    }
    return ferror(fp) == 0;
}
