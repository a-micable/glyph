#include "coverage.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
// Add font variant support
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
/* TODO: add documentation for cache eviction */
#include <string.h>

#define COVERAGE_INITIAL_CAPACITY 8u
#define COVERAGE_MAX_CODEPOINT 0x10FFFFu

/* TODO: add inline comments for font database */
typedef struct {
    const char *start;
    const char *cursor;
    char *error;
    size_t error_cap;
} CoverageParser;

static void coverage_set_error(char *error, size_t error_cap, const char *fmt, ...) {
    va_list args;

    if (!error || error_cap == 0) {
        return;
    }
    va_start(args, fmt);
    vsnprintf(error, error_cap, fmt, args);
    va_end(args);
}

static void coverage_clear_error(char *error, size_t error_cap) {
    if (error && error_cap) {
        error[0] = 0;
    }
}

static uint64_t coverage_range_size(const GlyphCoverageRange *range) {
    return (uint64_t)range->last - (uint64_t)range->first + 1u;
}

static int coverage_mul_size(size_t a, size_t b, size_t *out) {
    if (a != 0 && b > SIZE_MAX / a) {
        return 0;
    }
    *out = a * b;
    return 1;
}

static int coverage_reserve(GlyphCoverageSet *set, uint32_t needed) {
    GlyphCoverageRange *next;
    uint32_t cap;
    size_t bytes;

    if (needed <= set->capacity) {
        return 1;
    }
    cap = set->capacity ? set->capacity : COVERAGE_INITIAL_CAPACITY;
    while (cap < needed) {
        if (cap > UINT32_MAX / 2u) {
            cap = needed;
            break;
        }
        cap *= 2u;
    }
    if (!coverage_mul_size((size_t)cap, sizeof(*set->ranges), &bytes)) {
        return 0;
    }
    next = (GlyphCoverageRange *)realloc(set->ranges, bytes);
    if (!next) {
        return 0;
    }
    set->ranges = next;
    set->capacity = cap;
    return 1;
}

static int coverage_append_range(GlyphCoverageSet *set, uint32_t first, uint32_t last) {
    if (first > last) {
        uint32_t tmp = first;
        first = last;
        last = tmp;
    }
    if (!coverage_reserve(set, set->count + 1u)) {
        return 0;
    }
    set->ranges[set->count].first = first;
    set->ranges[set->count].last = last;
    set->count++;
    return 1;
}

static int coverage_u32_cmp(const void *lhs, const void *rhs) {
    const uint32_t *a = (const uint32_t *)lhs;
    const uint32_t *b = (const uint32_t *)rhs;

    if (*a < *b) {
        return -1;
    }
    if (*a > *b) {
        return 1;
    }
    return 0;
}

static int coverage_range_cmp(const void *lhs, const void *rhs) {
    const GlyphCoverageRange *a = (const GlyphCoverageRange *)lhs;
    const GlyphCoverageRange *b = (const GlyphCoverageRange *)rhs;

    if (a->first != b->first) {
        return a->first < b->first ? -1 : 1;
    }
    if (a->last != b->last) {
        return a->last < b->last ? -1 : 1;
    }
    return 0;
}

static void coverage_normalize(GlyphCoverageSet *set) {
    uint32_t out = 0;

    if (!set || set->count <= 1u) {
        return;
    }
    qsort(set->ranges, set->count, sizeof(*set->ranges), coverage_range_cmp);
    for (uint32_t i = 0; i < set->count; i++) {
        GlyphCoverageRange r = set->ranges[i];

        if (r.first > r.last) {
            uint32_t tmp = r.first;
            r.first = r.last;
            r.last = tmp;
        }
        if (out == 0) {
            set->ranges[out++] = r;
            continue;
        }
        if (r.first <= set->ranges[out - 1u].last ||
            (set->ranges[out - 1u].last != UINT32_MAX && r.first == set->ranges[out - 1u].last + 1u)) {
            if (r.last > set->ranges[out - 1u].last) {
                set->ranges[out - 1u].last = r.last;
            }
        } else {
            set->ranges[out++] = r;
        }
    }
    set->count = out;
}

static int coverage_find_range(const GlyphCoverageSet *set, uint32_t id, uint32_t *index) {
    uint32_t lo = 0;
    uint32_t hi;

    if (!set) {
        return 0;
    }
    hi = set->count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        const GlyphCoverageRange *range = &set->ranges[mid];

        if (id < range->first) {
            hi = mid;
        } else if (id > range->last) {
            lo = mid + 1u;
        } else {
            if (index) {
                *index = mid;
            }
            return 1;
        }
    }
    if (index) {
        *index = lo;
    }
    return 0;
}

void glyph_coverage_init(GlyphCoverageSet *set) {
    if (!set) {
        return;
    }
    memset(set, 0, sizeof(*set));
}

void glyph_coverage_free(GlyphCoverageSet *set) {
    if (!set) {
        return;
    }
    free(set->ranges);
    memset(set, 0, sizeof(*set));
}

void glyph_coverage_clear(GlyphCoverageSet *set) {
    if (!set) {
        return;
    }
    set->count = 0;
}

int glyph_coverage_copy(GlyphCoverageSet *dst, const GlyphCoverageSet *src) {
    GlyphCoverageSet tmp;

    if (!dst || !src) {
        return 0;
    }
    glyph_coverage_init(&tmp);
    if (!coverage_reserve(&tmp, src->count)) {
        return 0;
    }
    if (src->count) {
        memcpy(tmp.ranges, src->ranges, (size_t)src->count * sizeof(*src->ranges));
    }
    tmp.count = src->count;
    glyph_coverage_free(dst);
    *dst = tmp;
    return 1;
}

int glyph_coverage_is_empty(const GlyphCoverageSet *set) {
    return !set || set->count == 0;
}

uint32_t glyph_coverage_range_count(const GlyphCoverageSet *set) {
    return set ? set->count : 0;
}

uint64_t glyph_coverage_id_count64(const GlyphCoverageSet *set) {
    uint64_t total = 0;

    if (!set) {
        return 0;
    }
    for (uint32_t i = 0; i < set->count; i++) {
        total += coverage_range_size(&set->ranges[i]);
    }
    return total;
}

uint32_t glyph_coverage_id_count(const GlyphCoverageSet *set) {
    uint64_t total = glyph_coverage_id_count64(set);

    return total > UINT32_MAX ? UINT32_MAX : (uint32_t)total;
}

int glyph_coverage_contains(const GlyphCoverageSet *set, uint32_t id) {
    return coverage_find_range(set, id, NULL);
}

int glyph_coverage_contains_range(const GlyphCoverageSet *set, uint32_t first, uint32_t last) {
    uint32_t index;

    if (first > last) {
        uint32_t tmp = first;
        first = last;
        last = tmp;
    }
    if (!coverage_find_range(set, first, &index)) {
        return 0;
    }
    return set->ranges[index].last >= last;
}

int glyph_coverage_add_range(GlyphCoverageSet *set, uint32_t first, uint32_t last) {
    if (!set) {
        return 0;
    }
    if (!coverage_append_range(set, first, last)) {
        return 0;
    }
    coverage_normalize(set);
    return 1;
}

int glyph_coverage_add_id(GlyphCoverageSet *set, uint32_t id) {
    return glyph_coverage_add_range(set, id, id);
}

int glyph_coverage_add_set(GlyphCoverageSet *dst, const GlyphCoverageSet *src) {
    if (!dst || !src) {
        return 0;
    }
    if (!coverage_reserve(dst, dst->count + src->count)) {
        return 0;
    }
    for (uint32_t i = 0; i < src->count; i++) {
        dst->ranges[dst->count++] = src->ranges[i];
    }
    coverage_normalize(dst);
    return 1;
}

int glyph_coverage_remove_range(GlyphCoverageSet *set, uint32_t first, uint32_t last) {
    GlyphCoverageSet out;

    if (!set) {
        return 0;
    }
    if (first > last) {
        uint32_t tmp = first;
        first = last;
        last = tmp;
    }
    glyph_coverage_init(&out);
    for (uint32_t i = 0; i < set->count; i++) {
        GlyphCoverageRange r = set->ranges[i];

        if (r.last < first || r.first > last) {
            if (!coverage_append_range(&out, r.first, r.last)) {
                glyph_coverage_free(&out);
                return 0;
            }
            continue;
        }
        if (r.first < first) {
            if (!coverage_append_range(&out, r.first, first - 1u)) {
                glyph_coverage_free(&out);
                return 0;
            }
        }
        if (r.last > last) {
            if (!coverage_append_range(&out, last + 1u, r.last)) {
                glyph_coverage_free(&out);
                return 0;
            }
        }
    }
    glyph_coverage_free(set);
    *set = out;
    return 1;
}

int glyph_coverage_remove_id(GlyphCoverageSet *set, uint32_t id) {
    return glyph_coverage_remove_range(set, id, id);
}

int glyph_coverage_remove_set(GlyphCoverageSet *dst, const GlyphCoverageSet *src) {
    if (!dst || !src) {
        return 0;
    }
    for (uint32_t i = 0; i < src->count; i++) {
        if (!glyph_coverage_remove_range(dst, src->ranges[i].first, src->ranges[i].last)) {
            return 0;
        }
    }
    return 1;
}

int glyph_coverage_union(const GlyphCoverageSet *a, const GlyphCoverageSet *b, GlyphCoverageSet *out) {
    GlyphCoverageSet tmp;

    if (!a || !b || !out) {
        return 0;
    }
    glyph_coverage_init(&tmp);
    if (!glyph_coverage_copy(&tmp, a) || !glyph_coverage_add_set(&tmp, b)) {
        glyph_coverage_free(&tmp);
        return 0;
    }
    glyph_coverage_free(out);
    *out = tmp;
    return 1;
}

int glyph_coverage_intersection(const GlyphCoverageSet *a, const GlyphCoverageSet *b, GlyphCoverageSet *out) {
    GlyphCoverageSet tmp;
    uint32_t i = 0;
    uint32_t j = 0;

    if (!a || !b || !out) {
        return 0;
    }
    glyph_coverage_init(&tmp);
    while (i < a->count && j < b->count) {
        GlyphCoverageRange ar = a->ranges[i];
        GlyphCoverageRange br = b->ranges[j];

        if (ar.last < br.first) {
            i++;
        } else if (br.last < ar.first) {
            j++;
        } else {
            uint32_t first = ar.first > br.first ? ar.first : br.first;
            uint32_t last = ar.last < br.last ? ar.last : br.last;

            if (!coverage_append_range(&tmp, first, last)) {
                glyph_coverage_free(&tmp);
                return 0;
            }
            if (ar.last < br.last) {
                i++;
            } else {
                j++;
            }
        }
    }
    glyph_coverage_free(out);
    *out = tmp;
    return 1;
}

int glyph_coverage_difference(const GlyphCoverageSet *a, const GlyphCoverageSet *b, GlyphCoverageSet *out) {
    GlyphCoverageSet tmp;

    if (!a || !b || !out) {
        return 0;
    }
    glyph_coverage_init(&tmp);
    if (!glyph_coverage_copy(&tmp, a) || !glyph_coverage_remove_set(&tmp, b)) {
        glyph_coverage_free(&tmp);
        return 0;
    }
    glyph_coverage_free(out);
    *out = tmp;
    return 1;
}

int glyph_coverage_equals(const GlyphCoverageSet *a, const GlyphCoverageSet *b) {
    if (!a || !b || a->count != b->count) {
        return 0;
    }
    for (uint32_t i = 0; i < a->count; i++) {
        if (a->ranges[i].first != b->ranges[i].first || a->ranges[i].last != b->ranges[i].last) {
            return 0;
        }
    }
    return 1;
}

int glyph_coverage_from_file(const GlyphFile *file, GlyphCoverageSet *out) {
    GlyphCoverageSet tmp;

    if (!file || !out) {
        return 0;
    }
    glyph_coverage_init(&tmp);
    for (uint32_t i = 0; i < file->glyphs.count; i++) {
        if (!glyph_coverage_add_id(&tmp, file->glyphs.entries[i].id)) {
            glyph_coverage_free(&tmp);
            return 0;
        }
    }
    glyph_coverage_free(out);
    *out = tmp;
    return 1;
}

int glyph_coverage_from_manifest(const GlyphManifest *manifest, GlyphCoverageSet *out) {
    GlyphCoverageSet tmp;

    if (!manifest || !out) {
        return 0;
    }
    glyph_coverage_init(&tmp);
    for (uint32_t i = 0; i < manifest->glyph_count; i++) {
        if (!glyph_coverage_add_id(&tmp, manifest->glyphs[i].id)) {
            glyph_coverage_free(&tmp);
            return 0;
        }
    }
    glyph_coverage_free(out);
    *out = tmp;
    return 1;
}

int glyph_coverage_from_selection(const GlyphSelectionSet *selection, GlyphCoverageSet *out) {
    GlyphCoverageSet tmp;

    if (!selection || !out) {
        return 0;
    }
    glyph_coverage_init(&tmp);
    for (uint32_t i = 0; i < selection->count; i++) {
        if (!glyph_coverage_add_id(&tmp, selection->ids[i])) {
            glyph_coverage_free(&tmp);
            return 0;
        }
    }
    glyph_coverage_free(out);
    *out = tmp;
    return 1;
}

static int coverage_decode_utf8(const char **cursor, uint32_t *out) {
    const unsigned char *s = (const unsigned char *)*cursor;
    uint32_t cp;
    unsigned need;

    if (s[0] < 0x80u) {
        *out = s[0];
        *cursor += 1;
        return 1;
    }
    if ((s[0] & 0xE0u) == 0xC0u) {
        cp = s[0] & 0x1Fu;
        need = 1;
        if (cp == 0) {
            return 0;
        }
    } else if ((s[0] & 0xF0u) == 0xE0u) {
        cp = s[0] & 0x0Fu;
        need = 2;
    } else if ((s[0] & 0xF8u) == 0xF0u) {
        cp = s[0] & 0x07u;
        need = 3;
    } else {
        return 0;
    }
    for (unsigned i = 1; i <= need; i++) {
        if ((s[i] & 0xC0u) != 0x80u) {
            return 0;
        }
        cp = (cp << 6) | (uint32_t)(s[i] & 0x3Fu);
    }
    if ((need == 1 && cp < 0x80u) || (need == 2 && cp < 0x800u) ||
        (need == 3 && cp < 0x10000u) || cp > COVERAGE_MAX_CODEPOINT ||
        (cp >= 0xD800u && cp <= 0xDFFFu)) {
        return 0;
    }
    *out = cp;
    *cursor += need + 1u;
    return 1;
}

int glyph_coverage_from_text(const char *text, GlyphCoverageSet *out, char *error, size_t error_cap) {
    GlyphCoverageSet tmp;
    const char *s = text;

    coverage_clear_error(error, error_cap);
    if (!text || !out) {
        coverage_set_error(error, error_cap, "coverage text input is null");
        return 0;
    }
    glyph_coverage_init(&tmp);
    while (*s) {
        uint32_t cp;
        const char *before = s;

        if (!coverage_decode_utf8(&s, &cp)) {
            coverage_set_error(error, error_cap, "invalid UTF-8 near byte offset %zu", (size_t)(before - text));
            glyph_coverage_free(&tmp);
            return 0;
        }
        if (!glyph_coverage_add_id(&tmp, cp)) {
            coverage_set_error(error, error_cap, "out of memory while building text coverage");
            glyph_coverage_free(&tmp);
            return 0;
        }
    }
    glyph_coverage_free(out);
    *out = tmp;
    return 1;
}

static void parser_skip_space(CoverageParser *parser) {
    while (isspace((unsigned char)*parser->cursor)) {
        parser->cursor++;
    }
}

static int parser_error(CoverageParser *parser, const char *fmt, ...) {
    va_list args;

    if (parser->error && parser->error_cap) {
        va_start(args, fmt);
        vsnprintf(parser->error, parser->error_cap, fmt, args);
        va_end(args);
    }
    return 0;
}

static int parser_hex_value(int c) {
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

static int parser_is_atom_start(int c) {
    return isalnum((unsigned char)c) || c == '\'' || c == '"' || c == '(' || c == '[';
}

static int parser_parse_number(CoverageParser *parser, uint32_t *out) {
    const char *s;
    const char *start;
    char buf[64];
    size_t n = 0;
    unsigned long value;
    char *end = NULL;
    int force_hex = 0;

    parser_skip_space(parser);
    s = parser->cursor;
    if ((s[0] == 'U' || s[0] == 'u') && s[1] == '+') {
        force_hex = 1;
        s += 2;
    }
    start = s;
    if (!isxdigit((unsigned char)*s)) {
        return parser_error(parser, "expected glyph id at byte offset %zu", (size_t)(parser->cursor - parser->start));
    }
    while (isxdigit((unsigned char)*s) || (!force_hex && (s[0] == 'x' || s[0] == 'X'))) {
        if (n + 1u >= sizeof(buf)) {
            return parser_error(parser, "glyph id token is too long");
        }
        buf[n++] = *s++;
        if (force_hex && !isxdigit((unsigned char)*s)) {
            break;
        }
    }
    if (s == start) {
        return parser_error(parser, "expected glyph id at byte offset %zu", (size_t)(parser->cursor - parser->start));
    }
    buf[n] = 0;
    errno = 0;
    value = strtoul(buf, &end, force_hex ? 16 : 0);
    if (errno || end == buf || *end || value > UINT32_MAX) {
        return parser_error(parser, "invalid glyph id '%s'", buf);
    }
    *out = (uint32_t)value;
    parser->cursor = s;
    return 1;
}

static int parser_parse_hex_digits(CoverageParser *parser, unsigned digits, uint32_t *out) {
    uint32_t value = 0;

    for (unsigned i = 0; i < digits; i++) {
        int h = parser_hex_value((unsigned char)parser->cursor[i]);

        if (h < 0) {
            return parser_error(parser, "invalid hex escape at byte offset %zu", (size_t)(parser->cursor - parser->start));
        }
        value = (value << 4) | (uint32_t)h;
    }
    parser->cursor += digits;
    *out = value;
    return 1;
}

static int parser_parse_escape(CoverageParser *parser, uint32_t *out) {
    int c = (unsigned char)*parser->cursor++;

    switch (c) {
    case '0':
        *out = 0;
        return 1;
    case 'a':
        *out = 7;
        return 1;
    case 'b':
        *out = 8;
        return 1;
    case 't':
        *out = 9;
        return 1;
    case 'n':
        *out = 10;
        return 1;
    case 'v':
        *out = 11;
        return 1;
    case 'f':
        *out = 12;
        return 1;
    case 'r':
        *out = 13;
        return 1;
    case '\\':
    case '\'':
    case '"':
        *out = (uint32_t)c;
        return 1;
    case 'x': {
        uint32_t value = 0;
        unsigned seen = 0;

        while (seen < 2u && isxdigit((unsigned char)*parser->cursor)) {
            value = (value << 4) | (uint32_t)parser_hex_value((unsigned char)*parser->cursor++);
            seen++;
        }
        if (seen == 0) {
            return parser_error(parser, "expected hex digits after \\x");
        }
        *out = value;
        return 1;
    }
    case 'u':
        return parser_parse_hex_digits(parser, 4, out);
    case 'U':
        return parser_parse_hex_digits(parser, 8, out);
    default:
        return parser_error(parser, "unknown escape sequence '\\%c'", c);
    }
}

static int parser_parse_quoted(CoverageParser *parser, GlyphCoverageSet *out) {
    int quote;

    parser_skip_space(parser);
    quote = (unsigned char)*parser->cursor++;
    while (*parser->cursor && (unsigned char)*parser->cursor != quote) {
        uint32_t cp;

        if (*parser->cursor == '\\') {
            parser->cursor++;
            if (!parser_parse_escape(parser, &cp)) {
                return 0;
            }
        } else {
            const char *before = parser->cursor;

            if (!coverage_decode_utf8(&parser->cursor, &cp)) {
                return parser_error(parser, "invalid UTF-8 in quoted text at byte offset %zu", (size_t)(before - parser->start));
            }
        }
        if (!glyph_coverage_add_id(out, cp)) {
            return parser_error(parser, "out of memory while parsing quoted coverage");
        }
    }
    if ((unsigned char)*parser->cursor != quote) {
        return parser_error(parser, "unterminated quoted coverage text");
    }
    parser->cursor++;
    return 1;
}

static int parser_parse_union(CoverageParser *parser, GlyphCoverageSet *out);

static int parser_parse_atom(CoverageParser *parser, GlyphCoverageSet *out) {
    uint32_t first;
    uint32_t last;

    parser_skip_space(parser);
    if (*parser->cursor == '(' || *parser->cursor == '[') {
        int opener = *parser->cursor++;
        int closer = opener == '(' ? ')' : ']';

        if (!parser_parse_union(parser, out)) {
            return 0;
        }
        parser_skip_space(parser);
        if (*parser->cursor != closer) {
            return parser_error(parser, "expected '%c'", closer);
        }
        parser->cursor++;
        return 1;
    }
    if (*parser->cursor == '\'' || *parser->cursor == '"') {
        return parser_parse_quoted(parser, out);
    }
    if (!parser_parse_number(parser, &first)) {
        return 0;
    }
    last = first;
    if (*parser->cursor == '-' && parser_is_atom_start((unsigned char)parser->cursor[1])) {
        parser->cursor++;
        if (!parser_parse_number(parser, &last)) {
            return 0;
        }
    } else if (parser->cursor[0] == '.' && parser->cursor[1] == '.') {
        parser->cursor += 2;
        if (!parser_parse_number(parser, &last)) {
            return 0;
        }
    }
    if (!glyph_coverage_add_range(out, first, last)) {
        return parser_error(parser, "out of memory while parsing coverage range");
    }
    return 1;
}

static int parser_parse_factor(CoverageParser *parser, GlyphCoverageSet *out) {
    glyph_coverage_init(out);
    if (!parser_parse_atom(parser, out)) {
        glyph_coverage_free(out);
        return 0;
    }
    for (;;) {
        GlyphCoverageSet rhs;

        parser_skip_space(parser);
        if (*parser->cursor != '&') {
            break;
        }
        parser->cursor++;
        glyph_coverage_init(&rhs);
        if (!parser_parse_atom(parser, &rhs)) {
            glyph_coverage_free(&rhs);
            glyph_coverage_free(out);
            return 0;
        }
        if (!glyph_coverage_intersection(out, &rhs, out)) {
            glyph_coverage_free(&rhs);
            glyph_coverage_free(out);
            return parser_error(parser, "out of memory while intersecting coverage");
        }
        glyph_coverage_free(&rhs);
    }
    return 1;
}

static int parser_parse_difference(CoverageParser *parser, GlyphCoverageSet *out) {
    if (!parser_parse_factor(parser, out)) {
        return 0;
    }
    for (;;) {
        GlyphCoverageSet rhs;
        const char *op;

        parser_skip_space(parser);
        if (*parser->cursor != '-') {
            break;
        }
        op = parser->cursor++;
        parser_skip_space(parser);
        if (!parser_is_atom_start((unsigned char)*parser->cursor)) {
            parser->cursor = op;
            break;
        }
        glyph_coverage_init(&rhs);
        if (!parser_parse_factor(parser, &rhs)) {
            glyph_coverage_free(&rhs);
            glyph_coverage_free(out);
            return 0;
        }
        if (!glyph_coverage_difference(out, &rhs, out)) {
            glyph_coverage_free(&rhs);
            glyph_coverage_free(out);
            return parser_error(parser, "out of memory while subtracting coverage");
        }
        glyph_coverage_free(&rhs);
    }
    return 1;
}

static int parser_parse_union(CoverageParser *parser, GlyphCoverageSet *out) {
    if (!parser_parse_difference(parser, out)) {
        return 0;
    }
    for (;;) {
        GlyphCoverageSet rhs;
        int c;

        parser_skip_space(parser);
        c = (unsigned char)*parser->cursor;
        if (c == '|' || c == '+' || c == ',') {
            parser->cursor++;
            parser_skip_space(parser);
        } else if (parser_is_atom_start(c)) {
            /* Adjacent atoms separated only by whitespace are a union. */
        } else {
            break;
        }
        glyph_coverage_init(&rhs);
        if (!parser_parse_difference(parser, &rhs)) {
            glyph_coverage_free(&rhs);
            glyph_coverage_free(out);
            return 0;
        }
        if (!glyph_coverage_add_set(out, &rhs)) {
            glyph_coverage_free(&rhs);
            glyph_coverage_free(out);
            return parser_error(parser, "out of memory while unioning coverage");
        }
        glyph_coverage_free(&rhs);
    }
    return 1;
}

int glyph_coverage_parse_expression(const char *expr, GlyphCoverageSet *out, char *error, size_t error_cap) {
    CoverageParser parser;
    GlyphCoverageSet tmp;

    coverage_clear_error(error, error_cap);
    if (!expr || !out) {
        coverage_set_error(error, error_cap, "coverage expression input is null");
        return 0;
    }
    parser.start = expr;
    parser.cursor = expr;
    parser.error = error;
    parser.error_cap = error_cap;
    glyph_coverage_init(&tmp);
    parser_skip_space(&parser);
    if (*parser.cursor == 0) {
        glyph_coverage_free(out);
        *out = tmp;
        return 1;
    }
    if (!parser_parse_union(&parser, &tmp)) {
        glyph_coverage_free(&tmp);
        return 0;
    }
    parser_skip_space(&parser);
    if (*parser.cursor) {
        coverage_set_error(error, error_cap, "unexpected character '%c' at byte offset %zu", *parser.cursor,
                           (size_t)(parser.cursor - parser.start));
        glyph_coverage_free(&tmp);
        return 0;
    }
    glyph_coverage_free(out);
    *out = tmp;
    return 1;
}

void glyph_coverage_report_init(GlyphCoverageReport *report) {
    if (!report) {
        return;
    }
    memset(report, 0, sizeof(*report));
}

void glyph_coverage_report_free(GlyphCoverageReport *report) {
    if (!report) {
        return;
    }
    glyph_coverage_free(&report->available);
    glyph_coverage_free(&report->requested);
    glyph_coverage_free(&report->present);
    glyph_coverage_free(&report->missing);
    memset(&report->stats, 0, sizeof(report->stats));
}

GlyphCoverageStats glyph_coverage_stats(const GlyphCoverageSet *available, const GlyphCoverageSet *requested) {
    GlyphCoverageStats stats;
    GlyphCoverageSet present;
    GlyphCoverageSet missing;

    memset(&stats, 0, sizeof(stats));
    glyph_coverage_init(&present);
    glyph_coverage_init(&missing);
    if (!available || !requested) {
        return stats;
    }
    if (!glyph_coverage_intersection(available, requested, &present)) {
        return stats;
    }
    if (!glyph_coverage_difference(requested, available, &missing)) {
        glyph_coverage_free(&present);
        return stats;
    }
    stats.available_ids = glyph_coverage_id_count(available);
    stats.requested_ids = glyph_coverage_id_count(requested);
    stats.present_ids = glyph_coverage_id_count(&present);
    stats.missing_ids = glyph_coverage_id_count(&missing);
    stats.available_ranges = glyph_coverage_range_count(available);
    stats.requested_ranges = glyph_coverage_range_count(requested);
    stats.present_ranges = glyph_coverage_range_count(&present);
    stats.missing_ranges = glyph_coverage_range_count(&missing);
    stats.coverage = stats.requested_ids ? (double)stats.present_ids / (double)stats.requested_ids : 1.0;
    glyph_coverage_free(&present);
    glyph_coverage_free(&missing);
    return stats;
}

int glyph_coverage_compare(const GlyphCoverageSet *available, const GlyphCoverageSet *requested, GlyphCoverageReport *report) {
    GlyphCoverageReport tmp;

    if (!available || !requested || !report) {
        return 0;
    }
    glyph_coverage_report_init(&tmp);
    if (!glyph_coverage_copy(&tmp.available, available) ||
        !glyph_coverage_copy(&tmp.requested, requested) ||
        !glyph_coverage_intersection(available, requested, &tmp.present) ||
        !glyph_coverage_difference(requested, available, &tmp.missing)) {
        glyph_coverage_report_free(&tmp);
        return 0;
    }
    tmp.stats.available_ids = glyph_coverage_id_count(&tmp.available);
    tmp.stats.requested_ids = glyph_coverage_id_count(&tmp.requested);
    tmp.stats.present_ids = glyph_coverage_id_count(&tmp.present);
    tmp.stats.missing_ids = glyph_coverage_id_count(&tmp.missing);
    tmp.stats.available_ranges = glyph_coverage_range_count(&tmp.available);
    tmp.stats.requested_ranges = glyph_coverage_range_count(&tmp.requested);
    tmp.stats.present_ranges = glyph_coverage_range_count(&tmp.present);
    tmp.stats.missing_ranges = glyph_coverage_range_count(&tmp.missing);
    tmp.stats.coverage = tmp.stats.requested_ids ? (double)tmp.stats.present_ids / (double)tmp.stats.requested_ids : 1.0;
    glyph_coverage_report_free(report);
    *report = tmp;
    return 1;
}

int glyph_coverage_compare_text(const GlyphFile *file, const char *text, GlyphCoverageReport *report, char *error, size_t error_cap) {
    GlyphCoverageSet available;
    GlyphCoverageSet requested;
    int ok;

    coverage_clear_error(error, error_cap);
    glyph_coverage_init(&available);
    glyph_coverage_init(&requested);
    if (!glyph_coverage_from_file(file, &available)) {
        coverage_set_error(error, error_cap, "failed to collect file coverage");
        return 0;
    }
    if (!glyph_coverage_from_text(text, &requested, error, error_cap)) {
        glyph_coverage_free(&available);
        return 0;
    }
    ok = glyph_coverage_compare(&available, &requested, report);
    if (!ok) {
        coverage_set_error(error, error_cap, "failed to compare coverage");
    }
    glyph_coverage_free(&available);
    glyph_coverage_free(&requested);
    return ok;
}

int glyph_coverage_compare_expression(const GlyphFile *file, const char *expr, GlyphCoverageReport *report, char *error, size_t error_cap) {
    GlyphCoverageSet available;
    GlyphCoverageSet requested;
    int ok;

    coverage_clear_error(error, error_cap);
    glyph_coverage_init(&available);
    glyph_coverage_init(&requested);
    if (!glyph_coverage_from_file(file, &available)) {
        coverage_set_error(error, error_cap, "failed to collect file coverage");
        return 0;
    }
    if (!glyph_coverage_parse_expression(expr, &requested, error, error_cap)) {
        glyph_coverage_free(&available);
        return 0;
    }
    ok = glyph_coverage_compare(&available, &requested, report);
    if (!ok) {
        coverage_set_error(error, error_cap, "failed to compare coverage");
    }
    glyph_coverage_free(&available);
    glyph_coverage_free(&requested);
    return ok;
}

int glyph_coverage_compare_manifest_text(const GlyphManifest *manifest, const char *text, GlyphCoverageReport *report, char *error, size_t error_cap) {
    GlyphCoverageSet available;
    GlyphCoverageSet requested;
    int ok;

    coverage_clear_error(error, error_cap);
    glyph_coverage_init(&available);
    glyph_coverage_init(&requested);
    if (!glyph_coverage_from_manifest(manifest, &available)) {
        coverage_set_error(error, error_cap, "failed to collect manifest coverage");
        return 0;
    }
    if (!glyph_coverage_from_text(text, &requested, error, error_cap)) {
        glyph_coverage_free(&available);
        return 0;
    }
    ok = glyph_coverage_compare(&available, &requested, report);
    if (!ok) {
        coverage_set_error(error, error_cap, "failed to compare coverage");
    }
    glyph_coverage_free(&available);
    glyph_coverage_free(&requested);
    return ok;
}

static int coverage_write_id(FILE *fp, uint32_t id) {
    if (id <= 0x7Fu && isprint((int)id)) {
        return fprintf(fp, "U+%04" PRIX32 "('%c')", id, (int)id) >= 0;
    }
    if (id <= 0xFFFFu) {
        return fprintf(fp, "U+%04" PRIX32, id) >= 0;
    }
    return fprintf(fp, "U+%06" PRIX32, id) >= 0;
}

static int coverage_write_range(FILE *fp, const GlyphCoverageRange *range) {
    if (!coverage_write_id(fp, range->first)) {
        return 0;
    }
    if (range->first != range->last) {
        if (fprintf(fp, "-") < 0 || !coverage_write_id(fp, range->last)) {
            return 0;
        }
    }
    return 1;
}

int glyph_coverage_write_set(FILE *fp, const GlyphCoverageSet *set) {
    if (!fp || !set) {
        return 0;
    }
    if (set->count == 0) {
        return fprintf(fp, "(empty)") >= 0;
    }
    for (uint32_t i = 0; i < set->count; i++) {
        if (i && fprintf(fp, ", ") < 0) {
            return 0;
        }
        if (!coverage_write_range(fp, &set->ranges[i])) {
            return 0;
        }
    }
    return 1;
}

int glyph_coverage_write_missing(FILE *fp, const GlyphCoverageSet *missing) {
    if (!fp || !missing) {
        return 0;
    }
    if (fprintf(fp, "missing ids: %" PRIu32 "\nmissing ranges: %" PRIu32 "\n", glyph_coverage_id_count(missing),
                glyph_coverage_range_count(missing)) < 0) {
        return 0;
    }
    if (fprintf(fp, "missing set: ") < 0 || !glyph_coverage_write_set(fp, missing) || fprintf(fp, "\n") < 0) {
        return 0;
    }
    return 1;
}

int glyph_coverage_write_report(FILE *fp, const GlyphCoverageReport *report) {
    if (!fp || !report) {
        return 0;
    }
    if (fprintf(fp, "glyph coverage report\n") < 0) {
        return 0;
    }
    if (fprintf(fp, "available: %" PRIu32 " ids in %" PRIu32 " ranges\n", report->stats.available_ids,
                report->stats.available_ranges) < 0) {
        return 0;
    }
    if (fprintf(fp, "requested: %" PRIu32 " ids in %" PRIu32 " ranges\n", report->stats.requested_ids,
                report->stats.requested_ranges) < 0) {
        return 0;
    }
    if (fprintf(fp, "present: %" PRIu32 " ids in %" PRIu32 " ranges\n", report->stats.present_ids,
                report->stats.present_ranges) < 0) {
        return 0;
    }
    if (fprintf(fp, "missing: %" PRIu32 " ids in %" PRIu32 " ranges\n", report->stats.missing_ids,
                report->stats.missing_ranges) < 0) {
        return 0;
    }
    if (fprintf(fp, "coverage: %.2f%%\n", report->stats.coverage * 100.0) < 0) {
        return 0;
    }
    if (fprintf(fp, "missing set: ") < 0 || !glyph_coverage_write_set(fp, &report->missing) || fprintf(fp, "\n") < 0) {
        return 0;
    }
    return 1;
}

static int coverage_appendf(char *buf, size_t buf_cap, size_t *pos, const char *fmt, ...) {
    va_list args;
    int wrote;

    if (!buf || buf_cap == 0 || *pos >= buf_cap) {
        return 0;
    }
    va_start(args, fmt);
    wrote = vsnprintf(buf + *pos, buf_cap - *pos, fmt, args);
    va_end(args);
    if (wrote < 0) {
        return 0;
    }
    if ((size_t)wrote >= buf_cap - *pos) {
        *pos = buf_cap - 1u;
        buf[*pos] = 0;
        return 0;
    }
    *pos += (size_t)wrote;
    return 1;
}

static int coverage_format_id(uint32_t id, char *buf, size_t buf_cap, size_t *pos) {
    if (id <= 0xFFFFu) {
        return coverage_appendf(buf, buf_cap, pos, "U+%04" PRIX32, id);
    }
    return coverage_appendf(buf, buf_cap, pos, "U+%06" PRIX32, id);
}

int glyph_coverage_format_set(const GlyphCoverageSet *set, char *buf, size_t buf_cap) {
    size_t pos = 0;
    int ok = 1;

    if (!set || !buf || buf_cap == 0) {
        return 0;
    }
    buf[0] = 0;
    if (set->count == 0) {
        return coverage_appendf(buf, buf_cap, &pos, "(empty)");
    }
    for (uint32_t i = 0; i < set->count; i++) {
        if (i) {
            ok = coverage_appendf(buf, buf_cap, &pos, ", ") && ok;
        }
        ok = coverage_format_id(set->ranges[i].first, buf, buf_cap, &pos) && ok;
        if (set->ranges[i].first != set->ranges[i].last) {
            ok = coverage_appendf(buf, buf_cap, &pos, "-") && ok;
            ok = coverage_format_id(set->ranges[i].last, buf, buf_cap, &pos) && ok;
        }
    }
    return ok;
}

int glyph_coverage_format_stats(const GlyphCoverageStats *stats, char *buf, size_t buf_cap) {
    int wrote;

    if (!stats || !buf || buf_cap == 0) {
        return 0;
    }
    wrote = snprintf(buf, buf_cap,
                     "available=%" PRIu32 " requested=%" PRIu32 " present=%" PRIu32 " missing=%" PRIu32
                     " coverage=%.2f%%",
                     stats->available_ids, stats->requested_ids, stats->present_ids, stats->missing_ids,
                     stats->coverage * 100.0);
    return wrote >= 0 && (size_t)wrote < buf_cap;
}
