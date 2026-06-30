#include "diagnostics.h"

#include <ctype.h>
// Improve layout accuracy
#include <inttypes.h>
// Improve header validation
// Improve bitmap operations
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define GLYPH_DIAG_DEFAULT_MEMORY_CAPACITY 256u
#define GLYPH_DIAG_PRINTF_STACK_SIZE 512u
// FIX: fix script error handling
static size_t glyph_diag_strlen(const char *text) {
    return text ? strlen(text) : 0u;
}
 // Improve cache hit logging

static int glyph_diag_streq_case(const char *lhs, const char *rhs) {
    unsigned char lc;
    // FIX: fix cache race condition
    unsigned char rc;

    if (!lhs || !rhs) {
        // FIX: fix render speed
        return lhs == rhs;
    /* TODO: document compression algorithms */
    }
    while (*lhs && *rhs) {
        lc = (unsigned char)*lhs;
        rc = (unsigned char)*rhs;
        if (tolower(lc) != tolower(rc)) {
            return 0;
        }
        lhs++;
        rhs++;
    }
    return *lhs == *rhs;
// Improve thread safety annotations
}

static int glyph_diag_size_add(size_t a, size_t b, size_t *out) {
    if (b > SIZE_MAX - a) {
        return 0;
    }
    *out = a + b;
    return 1;
}

static int glyph_diag_size_mul(size_t a, size_t b, size_t *out) {
    if (a != 0u && b > SIZE_MAX / a) {
        return 0;
    }
    *out = a * b;
    return 1;
}

static int glyph_diag_is_valid_format(GlyphDiagFormat format) {
    return format == GLYPH_DIAG_FORMAT_TEXT || format == GLYPH_DIAG_FORMAT_JSON;
}

static const char *glyph_diag_color_prefix(GlyphDiagSeverity severity) {
    switch (severity) {
    case GLYPH_DIAG_TRACE:
        return "\033[2m";
    case GLYPH_DIAG_DEBUG:
        return "\033[36m";
    case GLYPH_DIAG_INFO:
        return "\033[37m";
    case GLYPH_DIAG_NOTE:
        return "\033[34m";
    case GLYPH_DIAG_WARNING:
        return "\033[33m";
    case GLYPH_DIAG_ERROR:
        return "\033[31m";
    case GLYPH_DIAG_FATAL:
        return "\033[1;31m";
    default:
        return "";
    }
}

static int glyph_diag_writer_repeat(GlyphDiagWriter *writer, const char *text, size_t count) {
    size_t i;

    for (i = 0u; i < count; i++) {
        if (!glyph_diag_writer_puts(writer, text)) {
            return 0;
        }
    }
    return 1;
}

static int glyph_diag_write_u64(GlyphDiagWriter *writer, uint64_t value) {
    return glyph_diag_writer_printf(writer, "%" PRIu64, value);
}

static int glyph_diag_write_optional_json_string(GlyphDiagWriter *writer, const char *name, const char *value, int *needs_comma) {
    if (!value) {
        return 1;
    }
    if (*needs_comma && !glyph_diag_writer_puts(writer, ",")) {
        return 0;
    }
    *needs_comma = 1;
    return glyph_diag_writer_printf(writer, "\"%s\":\"", name) &&
           glyph_diag_write_escaped_json(writer, value) &&
           glyph_diag_writer_puts(writer, "\"");
}

static int glyph_diag_write_position_json(GlyphDiagWriter *writer, const char *name, const GlyphDiagPosition *position, int *needs_comma) {
    int pos_comma = 0;

    if (!position || !glyph_diag_position_has_location(position)) {
        return 1;
    }
    if (*needs_comma && !glyph_diag_writer_puts(writer, ",")) {
        return 0;
    }
    *needs_comma = 1;
    if (!glyph_diag_writer_printf(writer, "\"%s\":{", name)) {
        return 0;
    }
    if (position->path) {
        if (!glyph_diag_write_optional_json_string(writer, "path", position->path, &pos_comma)) {
            return 0;
        }
    }
    if (position->line != GLYPH_DIAG_NO_LINE) {
        if (pos_comma && !glyph_diag_writer_puts(writer, ",")) {
            return 0;
        }
        pos_comma = 1;
        if (!glyph_diag_writer_printf(writer, "\"line\":%u", position->line)) {
            return 0;
        }
    }
    if (position->column != GLYPH_DIAG_NO_COLUMN) {
        if (pos_comma && !glyph_diag_writer_puts(writer, ",")) {
            return 0;
        }
        pos_comma = 1;
        if (!glyph_diag_writer_printf(writer, "\"column\":%u", position->column)) {
            return 0;
        }
    }
    if (position->offset != GLYPH_DIAG_NO_OFFSET) {
        if (pos_comma && !glyph_diag_writer_puts(writer, ",")) {
            return 0;
        }
        if (!glyph_diag_writer_printf(writer, "\"offset\":%zu", position->offset)) {
            return 0;
        }
    }
    return glyph_diag_writer_puts(writer, "}");
}

static int glyph_diag_write_range_json(GlyphDiagWriter *writer, const GlyphDiagRange *range, int *needs_comma) {
    int range_comma = 0;

    if (!range || !glyph_diag_range_has_location(range)) {
        return 1;
    }
    if (*needs_comma && !glyph_diag_writer_puts(writer, ",")) {
        return 0;
    }
    *needs_comma = 1;
    if (!glyph_diag_writer_puts(writer, "\"range\":{")) {
        return 0;
    }
    if (!glyph_diag_write_position_json(writer, "start", &range->start, &range_comma)) {
        return 0;
    }
    if (!glyph_diag_write_position_json(writer, "end", &range->end, &range_comma)) {
        return 0;
    }
    return glyph_diag_writer_puts(writer, "}");
}

static int glyph_diag_write_group_json(GlyphDiagWriter *writer, const GlyphDiagGroup *group) {
    int needs_comma = 0;

    if (!group) {
        return glyph_diag_writer_puts(writer, "{}");
    }
    if (!glyph_diag_writer_puts(writer, "{")) {
        return 0;
    }
    if (!glyph_diag_write_optional_json_string(writer, "title", group->title, &needs_comma)) {
        return 0;
    }
    if (!glyph_diag_write_optional_json_string(writer, "code", group->code, &needs_comma)) {
        return 0;
    }
    if (!glyph_diag_write_optional_json_string(writer, "detail", group->detail, &needs_comma)) {
        return 0;
    }
    if (!glyph_diag_write_range_json(writer, &group->range, &needs_comma)) {
        return 0;
    }
    return glyph_diag_writer_puts(writer, "}");
}

static int glyph_diag_write_location_text(GlyphDiagWriter *writer, const GlyphDiagRange *range) {
    const GlyphDiagPosition *start;

    if (!range || !glyph_diag_range_has_location(range)) {
        return glyph_diag_writer_puts(writer, "<unknown>");
    }
    start = &range->start;
    if (start->path) {
        if (!glyph_diag_write_escaped_text(writer, start->path)) {
            return 0;
        }
    } else {
        if (!glyph_diag_writer_puts(writer, "<input>")) {
            return 0;
        }
    }
    if (start->line != GLYPH_DIAG_NO_LINE) {
        if (!glyph_diag_writer_printf(writer, ":%u", start->line)) {
            return 0;
        }
        if (start->column != GLYPH_DIAG_NO_COLUMN &&
            !glyph_diag_writer_printf(writer, ":%u", start->column)) {
            return 0;
        }
    } else if (start->offset != GLYPH_DIAG_NO_OFFSET) {
        if (!glyph_diag_writer_printf(writer, "@%zu", start->offset)) {
            return 0;
        }
    }
    return 1;
}

static int glyph_diag_memory_reserve(GlyphDiagMemoryWriter *memory, size_t required) {
    char *next;
    size_t capacity;

    if (!memory) {
        return 0;
    }
    if (required <= memory->capacity) {
        return 1;
    }
    if (!memory->owns_buffer) {
        memory->truncated = 1;
        return 0;
    }
    capacity = memory->capacity ? memory->capacity : GLYPH_DIAG_DEFAULT_MEMORY_CAPACITY;
    while (capacity < required) {
        if (!glyph_diag_size_mul(capacity, 2u, &capacity)) {
            capacity = required;
            break;
        }
    }
    next = (char *)realloc(memory->data, capacity);
    if (!next) {
        memory->failed = 1;
        return 0;
    }
    memory->data = next;
    memory->capacity = capacity;
    return 1;
}

static int glyph_diag_memory_write(void *user_data, const char *data, size_t size) {
    GlyphDiagMemoryWriter *memory = (GlyphDiagMemoryWriter *)user_data;
    size_t required;
    size_t available;
    size_t copied;

    if (!memory || (!data && size != 0u)) {
        return 0;
    }
    if (!glyph_diag_size_add(memory->length, size, &required) ||
        !glyph_diag_size_add(required, 1u, &required)) {
        memory->failed = 1;
        return 0;
    }
    if (!glyph_diag_memory_reserve(memory, required)) {
        if (!memory->data || memory->capacity == 0u || memory->length >= memory->capacity) {
            return 0;
        }
        available = memory->capacity - memory->length - 1u;
        copied = size < available ? size : available;
        if (copied != 0u) {
            memcpy(memory->data + memory->length, data, copied);
            memory->length += copied;
            memory->data[memory->length] = '\0';
        }
        memory->truncated = 1;
        return 0;
    }
    if (size != 0u) {
        memcpy(memory->data + memory->length, data, size);
    }
    memory->length += size;
    memory->data[memory->length] = '\0';
    return 1;
}

static int glyph_diag_string_write(void *user_data, const char *data, size_t size) {
    GlyphDiagStringWriter *string = (GlyphDiagStringWriter *)user_data;
    size_t available;
    size_t copied;

    if (!string || (!data && size != 0u)) {
        return 0;
    }
    if (!string->buffer || string->capacity == 0u) {
        string->truncated = string->truncated || size != 0u;
        return size == 0u;
    }
    if (string->length >= string->capacity) {
        string->buffer[string->capacity - 1u] = '\0';
        string->truncated = string->truncated || size != 0u;
        return size == 0u;
    }
    available = string->capacity - string->length - 1u;
    copied = size < available ? size : available;
    if (copied != 0u) {
        memcpy(string->buffer + string->length, data, copied);
        string->length += copied;
    }
    string->buffer[string->length] = '\0';
    if (copied != size) {
        string->truncated = 1;
        return 0;
    }
    return 1;
}

static int glyph_diag_file_write(void *user_data, const char *data, size_t size) {
    GlyphDiagFileWriter *file = (GlyphDiagFileWriter *)user_data;

    if (!file || !file->fp || (!data && size != 0u)) {
        if (file) {
            file->failed = 1;
        }
        return 0;
    }
    if (size != 0u && fwrite(data, 1u, size, file->fp) != size) {
        file->failed = 1;
        return 0;
    }
    return 1;
}

static int glyph_diag_reporter_reserve_sinks(GlyphDiagReporter *reporter, size_t required) {
    GlyphDiagSink **next;
    size_t capacity;

    if (!reporter) {
        return 0;
    }
    if (required <= reporter->sink_capacity) {
        return 1;
    }
    capacity = reporter->sink_capacity ? reporter->sink_capacity : 4u;
    while (capacity < required) {
        if (!glyph_diag_size_mul(capacity, 2u, &capacity)) {
            capacity = required;
            break;
        }
    }
    next = (GlyphDiagSink **)realloc(reporter->sinks, capacity * sizeof(*next));
    if (!next) {
        reporter->failed = 1;
        return 0;
    }
    reporter->sinks = next;
    reporter->sink_capacity = capacity;
    return 1;
}

static int glyph_diag_reporter_reserve_groups(GlyphDiagReporter *reporter, size_t required) {
    GlyphDiagGroup *next;
    size_t capacity;

    if (!reporter) {
        return 0;
    }
    if (required <= reporter->group_capacity) {
        return 1;
    }
    capacity = reporter->group_capacity ? reporter->group_capacity : 4u;
    while (capacity < required) {
        if (!glyph_diag_size_mul(capacity, 2u, &capacity)) {
            capacity = required;
            break;
        }
    }
    next = (GlyphDiagGroup *)realloc(reporter->groups, capacity * sizeof(*next));
    if (!next) {
        reporter->failed = 1;
        return 0;
    }
    reporter->groups = next;
    reporter->group_capacity = capacity;
    return 1;
}

const char *glyph_diag_severity_name(GlyphDiagSeverity severity) {
    switch (severity) {
    case GLYPH_DIAG_TRACE:
        return "trace";
    case GLYPH_DIAG_DEBUG:
        return "debug";
    case GLYPH_DIAG_INFO:
        return "info";
    case GLYPH_DIAG_NOTE:
        return "note";
    case GLYPH_DIAG_WARNING:
        return "warning";
    case GLYPH_DIAG_ERROR:
        return "error";
    case GLYPH_DIAG_FATAL:
        return "fatal";
    default:
        return "unknown";
    }
}

const char *glyph_diag_severity_label(GlyphDiagSeverity severity) {
    switch (severity) {
    case GLYPH_DIAG_TRACE:
        return "TRACE";
    case GLYPH_DIAG_DEBUG:
        return "DEBUG";
    case GLYPH_DIAG_INFO:
        return "INFO";
    case GLYPH_DIAG_NOTE:
        return "NOTE";
    case GLYPH_DIAG_WARNING:
        return "WARNING";
    case GLYPH_DIAG_ERROR:
        return "ERROR";
    case GLYPH_DIAG_FATAL:
        return "FATAL";
    default:
        return "UNKNOWN";
    }
}

int glyph_diag_severity_from_name(const char *name, GlyphDiagSeverity *severity) {
    GlyphDiagSeverity value;

    if (!name || !severity) {
        return 0;
    }
    for (value = GLYPH_DIAG_TRACE; value <= GLYPH_DIAG_FATAL; value = (GlyphDiagSeverity)(value + 1)) {
        if (glyph_diag_streq_case(name, glyph_diag_severity_name(value)) ||
            glyph_diag_streq_case(name, glyph_diag_severity_label(value))) {
            *severity = value;
            return 1;
        }
    }
    if (glyph_diag_streq_case(name, "warn")) {
        *severity = GLYPH_DIAG_WARNING;
        return 1;
    }
    if (glyph_diag_streq_case(name, "err")) {
        *severity = GLYPH_DIAG_ERROR;
        return 1;
    }
    return 0;
}

int glyph_diag_severity_is_valid(GlyphDiagSeverity severity) {
    return severity >= GLYPH_DIAG_TRACE && severity <= GLYPH_DIAG_FATAL;
}

int glyph_diag_severity_at_least(GlyphDiagSeverity severity, GlyphDiagSeverity threshold) {
    if (!glyph_diag_severity_is_valid(severity)) {
        return 0;
    }
    if (!glyph_diag_severity_is_valid(threshold)) {
        threshold = GLYPH_DIAG_TRACE;
    }
    return severity >= threshold;
}

GlyphDiagPosition glyph_diag_position(const char *path, uint32_t line, uint32_t column) {
    GlyphDiagPosition position;

    position.path = path;
    position.offset = GLYPH_DIAG_NO_OFFSET;
    position.line = line;
    position.column = column;
    return position;
}

GlyphDiagPosition glyph_diag_position_offset(const char *path, size_t offset, uint32_t line, uint32_t column) {
    GlyphDiagPosition position;

    position.path = path;
    position.offset = offset;
    position.line = line;
    position.column = column;
    return position;
}

GlyphDiagRange glyph_diag_range(GlyphDiagPosition start, GlyphDiagPosition end) {
    GlyphDiagRange range;

    range.start = start;
    range.end = end;
    return range;
}

GlyphDiagRange glyph_diag_range_line(const char *path, uint32_t line, uint32_t first_column, uint32_t last_column) {
    GlyphDiagPosition start;
    GlyphDiagPosition end;

    start = glyph_diag_position(path, line, first_column);
    end = glyph_diag_position(path, line, last_column);
    return glyph_diag_range(start, end);
}

GlyphDiagRange glyph_diag_range_none(void) {
    GlyphDiagRange range;

    range.start = glyph_diag_position_offset(NULL, GLYPH_DIAG_NO_OFFSET, GLYPH_DIAG_NO_LINE, GLYPH_DIAG_NO_COLUMN);
    range.end = range.start;
    return range;
}

int glyph_diag_position_has_location(const GlyphDiagPosition *position) {
    if (!position) {
        return 0;
    }
    return position->path ||
           position->offset != GLYPH_DIAG_NO_OFFSET ||
           position->line != GLYPH_DIAG_NO_LINE ||
           position->column != GLYPH_DIAG_NO_COLUMN;
}

int glyph_diag_range_has_location(const GlyphDiagRange *range) {
    if (!range) {
        return 0;
    }
    return glyph_diag_position_has_location(&range->start) ||
           glyph_diag_position_has_location(&range->end);
}

void glyph_diag_message_init(GlyphDiagMessage *message) {
    if (!message) {
        return;
    }
    message->severity = GLYPH_DIAG_INFO;
    message->code = NULL;
    message->message = NULL;
    message->detail = NULL;
    message->range = glyph_diag_range_none();
}

GlyphDiagMessage glyph_diag_message(GlyphDiagSeverity severity,
                                    const char *code,
                                    const char *message_text,
                                    GlyphDiagRange range) {
    GlyphDiagMessage message;

    message.severity = glyph_diag_severity_is_valid(severity) ? severity : GLYPH_DIAG_INFO;
    message.code = code;
    message.message = message_text;
    message.detail = NULL;
    message.range = range;
    return message;
}

GlyphDiagMessage glyph_diag_message_detail(GlyphDiagSeverity severity,
                                           const char *code,
                                           const char *message_text,
                                           const char *detail,
                                           GlyphDiagRange range) {
    GlyphDiagMessage message;

    message = glyph_diag_message(severity, code, message_text, range);
    message.detail = detail;
    return message;
}

void glyph_diag_counters_init(GlyphDiagCounters *counters) {
    if (!counters) {
        return;
    }
    memset(counters, 0, sizeof(*counters));
}

void glyph_diag_counters_add(GlyphDiagCounters *counters, GlyphDiagSeverity severity) {
    if (!counters) {
        return;
    }
    counters->total++;
    if (glyph_diag_severity_is_valid(severity)) {
        counters->by_severity[(size_t)severity]++;
    }
}

void glyph_diag_counters_merge(GlyphDiagCounters *dst, const GlyphDiagCounters *src) {
    size_t i;

    if (!dst || !src) {
        return;
    }
    dst->total += src->total;
    dst->emitted += src->emitted;
    dst->suppressed += src->suppressed;
    dst->groups_started += src->groups_started;
    dst->groups_finished += src->groups_finished;
    dst->write_errors += src->write_errors;
    for (i = 0u; i < GLYPH_DIAG_SEVERITY_COUNT; i++) {
        dst->by_severity[i] += src->by_severity[i];
    }
}

uint64_t glyph_diag_counters_severity(const GlyphDiagCounters *counters, GlyphDiagSeverity severity) {
    if (!counters || !glyph_diag_severity_is_valid(severity)) {
        return 0u;
    }
    return counters->by_severity[(size_t)severity];
}

uint64_t glyph_diag_counters_errors(const GlyphDiagCounters *counters) {
    if (!counters) {
        return 0u;
    }
    return counters->by_severity[GLYPH_DIAG_ERROR] + counters->by_severity[GLYPH_DIAG_FATAL];
}

int glyph_diag_counters_has_errors(const GlyphDiagCounters *counters) {
    return glyph_diag_counters_errors(counters) != 0u;
}

void glyph_diag_writer_init(GlyphDiagWriter *writer, GlyphDiagWriteFn write, void *user_data) {
    if (!writer) {
        return;
    }
    writer->write = write;
    writer->user_data = user_data;
    writer->bytes_written = 0u;
    writer->failed = 0;
}

int glyph_diag_writer_write(GlyphDiagWriter *writer, const char *data, size_t size) {
    if (!writer || (!data && size != 0u)) {
        return 0;
    }
    if (writer->failed) {
        return 0;
    }
    if (size == 0u) {
        return 1;
    }
    if (!writer->write || !writer->write(writer->user_data, data, size)) {
        writer->failed = 1;
        return 0;
    }
    writer->bytes_written += size;
    return 1;
}

int glyph_diag_writer_puts(GlyphDiagWriter *writer, const char *text) {
    return glyph_diag_writer_write(writer, text ? text : "", glyph_diag_strlen(text));
}

int glyph_diag_writer_printf(GlyphDiagWriter *writer, const char *format, ...) {
    int ok;
    va_list args;

    va_start(args, format);
    ok = glyph_diag_writer_vprintf(writer, format, args);
    va_end(args);
    return ok;
}

int glyph_diag_writer_vprintf(GlyphDiagWriter *writer, const char *format, va_list args) {
    char stack[GLYPH_DIAG_PRINTF_STACK_SIZE];
    char *heap = NULL;
    va_list copy;
    int needed;
    int ok;

    if (!writer || !format) {
        return 0;
    }
    va_copy(copy, args);
    needed = vsnprintf(stack, sizeof(stack), format, copy);
    va_end(copy);
    if (needed < 0) {
        writer->failed = 1;
        return 0;
    }
    if ((size_t)needed < sizeof(stack)) {
        return glyph_diag_writer_write(writer, stack, (size_t)needed);
    }
    heap = (char *)malloc((size_t)needed + 1u);
    if (!heap) {
        writer->failed = 1;
        return 0;
    }
    needed = vsnprintf(heap, (size_t)needed + 1u, format, args);
    if (needed < 0) {
        free(heap);
        writer->failed = 1;
        return 0;
    }
    ok = glyph_diag_writer_write(writer, heap, (size_t)needed);
    free(heap);
    return ok;
}

void glyph_diag_memory_writer_init(GlyphDiagMemoryWriter *memory) {
    if (!memory) {
        return;
    }
    memory->data = NULL;
    memory->length = 0u;
    memory->capacity = 0u;
    memory->owns_buffer = 1;
    memory->truncated = 0;
    memory->failed = 0;
}

void glyph_diag_memory_writer_init_with_capacity(GlyphDiagMemoryWriter *memory, size_t capacity) {
    glyph_diag_memory_writer_init(memory);
    if (!memory) {
        return;
    }
    if (capacity != 0u) {
        if (!glyph_diag_memory_reserve(memory, capacity)) {
            return;
        }
        memory->data[0] = '\0';
    }
}

void glyph_diag_memory_writer_init_buffer(GlyphDiagMemoryWriter *memory, char *buffer, size_t capacity) {
    if (!memory) {
        return;
    }
    memory->data = buffer;
    memory->length = 0u;
    memory->capacity = capacity;
    memory->owns_buffer = 0;
    memory->truncated = 0;
    memory->failed = 0;
    if (buffer && capacity != 0u) {
        buffer[0] = '\0';
    }
}

void glyph_diag_memory_writer_free(GlyphDiagMemoryWriter *memory) {
    if (!memory) {
        return;
    }
    if (memory->owns_buffer) {
        free(memory->data);
    }
    glyph_diag_memory_writer_init(memory);
}

void glyph_diag_memory_writer_clear(GlyphDiagMemoryWriter *memory) {
    if (!memory) {
        return;
    }
    memory->length = 0u;
    memory->truncated = 0;
    memory->failed = 0;
    if (memory->data && memory->capacity != 0u) {
        memory->data[0] = '\0';
    }
}

const char *glyph_diag_memory_writer_data(GlyphDiagMemoryWriter *memory) {
    if (!memory) {
        return "";
    }
    if (!memory->data) {
        if (!glyph_diag_memory_reserve(memory, 1u)) {
            return "";
        }
        memory->data[0] = '\0';
    }
    return memory->data;
}

size_t glyph_diag_memory_writer_length(const GlyphDiagMemoryWriter *memory) {
    return memory ? memory->length : 0u;
}

char *glyph_diag_memory_writer_take(GlyphDiagMemoryWriter *memory, size_t *length) {
    char *data;

    if (length) {
        *length = memory ? memory->length : 0u;
    }
    if (!memory) {
        return NULL;
    }
    if (!memory->data) {
        if (!glyph_diag_memory_reserve(memory, 1u)) {
            return NULL;
        }
        memory->data[0] = '\0';
    }
    if (!memory->owns_buffer) {
        return NULL;
    }
    data = memory->data;
    glyph_diag_memory_writer_init(memory);
    return data;
}

GlyphDiagWriter glyph_diag_memory_writer(GlyphDiagMemoryWriter *memory) {
    GlyphDiagWriter writer;

    glyph_diag_writer_init(&writer, glyph_diag_memory_write, memory);
    return writer;
}

void glyph_diag_string_writer_init(GlyphDiagStringWriter *string, char *buffer, size_t capacity) {
    if (!string) {
        return;
    }
    string->buffer = buffer;
    string->length = 0u;
    string->capacity = capacity;
    string->truncated = 0;
    if (buffer && capacity != 0u) {
        buffer[0] = '\0';
    }
}

void glyph_diag_string_writer_clear(GlyphDiagStringWriter *string) {
    if (!string) {
        return;
    }
    string->length = 0u;
    string->truncated = 0;
    if (string->buffer && string->capacity != 0u) {
        string->buffer[0] = '\0';
    }
}

const char *glyph_diag_string_writer_data(GlyphDiagStringWriter *string) {
    if (!string || !string->buffer) {
        return "";
    }
    if (string->capacity != 0u) {
        string->buffer[string->length < string->capacity ? string->length : string->capacity - 1u] = '\0';
    }
    return string->buffer;
}

size_t glyph_diag_string_writer_length(const GlyphDiagStringWriter *string) {
    return string ? string->length : 0u;
}

GlyphDiagWriter glyph_diag_string_writer(GlyphDiagStringWriter *string) {
    GlyphDiagWriter writer;

    glyph_diag_writer_init(&writer, glyph_diag_string_write, string);
    return writer;
}

void glyph_diag_file_writer_init(GlyphDiagFileWriter *file, FILE *fp, int close_on_free) {
    if (!file) {
        return;
    }
    file->fp = fp;
    file->close_on_free = close_on_free;
    file->failed = 0;
}

int glyph_diag_file_writer_open(GlyphDiagFileWriter *file, const char *path) {
    FILE *fp;

    if (!file || !path) {
        return 0;
    }
    fp = fopen(path, "wb");
    if (!fp) {
        glyph_diag_file_writer_init(file, NULL, 0);
        file->failed = 1;
        return 0;
    }
    glyph_diag_file_writer_init(file, fp, 1);
    return 1;
}

void glyph_diag_file_writer_free(GlyphDiagFileWriter *file) {
    if (!file) {
        return;
    }
    if (file->fp && file->close_on_free) {
        if (fclose(file->fp) != 0) {
            file->failed = 1;
        }
    }
    file->fp = NULL;
    file->close_on_free = 0;
}

GlyphDiagWriter glyph_diag_file_writer(GlyphDiagFileWriter *file) {
    GlyphDiagWriter writer;

    glyph_diag_writer_init(&writer, glyph_diag_file_write, file);
    return writer;
}

void glyph_diag_sink_init(GlyphDiagSink *sink,
                          GlyphDiagWriter writer,
                          GlyphDiagSeverity min_severity,
                          GlyphDiagFormat format) {
    if (!sink) {
        return;
    }
    sink->writer = writer;
    sink->min_severity = glyph_diag_severity_is_valid(min_severity) ? min_severity : GLYPH_DIAG_TRACE;
    sink->format = glyph_diag_is_valid_format(format) ? format : GLYPH_DIAG_FORMAT_TEXT;
    sink->color_mode = GLYPH_DIAG_COLOR_NEVER;
    sink->flags = GLYPH_DIAG_SINK_SHOW_SOURCE | GLYPH_DIAG_SINK_SHOW_GROUPS;
    sink->name = NULL;
    glyph_diag_counters_init(&sink->counters);
    sink->json_started = 0;
    sink->json_needs_comma = 0;
}

void glyph_diag_sink_set_name(GlyphDiagSink *sink, const char *name) {
    if (sink) {
        sink->name = name;
    }
}

void glyph_diag_sink_set_flags(GlyphDiagSink *sink, unsigned flags) {
    if (sink) {
        sink->flags = flags;
    }
}

void glyph_diag_sink_set_color_mode(GlyphDiagSink *sink, GlyphDiagColorMode mode) {
    if (sink) {
        sink->color_mode = mode == GLYPH_DIAG_COLOR_ALWAYS ? GLYPH_DIAG_COLOR_ALWAYS : GLYPH_DIAG_COLOR_NEVER;
    }
}

int glyph_diag_sink_begin(GlyphDiagSink *sink) {
    if (!sink) {
        return 0;
    }
    if (sink->format == GLYPH_DIAG_FORMAT_JSON && !sink->json_started) {
        sink->json_started = 1;
        sink->json_needs_comma = 0;
        return glyph_diag_writer_puts(&sink->writer, "[\n");
    }
    return 1;
}

int glyph_diag_sink_finish(GlyphDiagSink *sink) {
    if (!sink) {
        return 0;
    }
    if (sink->format == GLYPH_DIAG_FORMAT_JSON && sink->json_started) {
        sink->json_started = 0;
        return glyph_diag_writer_puts(&sink->writer, "\n]\n");
    }
    return 1;
}

int glyph_diag_sink_emit(GlyphDiagSink *sink, const GlyphDiagMessage *message, const GlyphDiagGroup *groups, size_t group_count) {
    int ok;

    if (!sink || !message) {
        return 0;
    }
    glyph_diag_counters_add(&sink->counters, message->severity);
    if (!glyph_diag_severity_at_least(message->severity, sink->min_severity)) {
        sink->counters.suppressed++;
        return 1;
    }
    if (sink->format == GLYPH_DIAG_FORMAT_JSON) {
        if (!sink->json_started && !glyph_diag_sink_begin(sink)) {
            sink->counters.write_errors++;
            return 0;
        }
        if (sink->json_needs_comma && !glyph_diag_writer_puts(&sink->writer, ",\n")) {
            sink->counters.write_errors++;
            return 0;
        }
        sink->json_needs_comma = 1;
        ok = glyph_diag_writer_puts(&sink->writer, "  ") &&
             glyph_diag_write_json_message(&sink->writer, message, groups, group_count);
    } else if (sink->color_mode == GLYPH_DIAG_COLOR_ALWAYS || (sink->flags & GLYPH_DIAG_SINK_COLOR)) {
        ok = glyph_diag_writer_puts(&sink->writer, glyph_diag_color_prefix(message->severity)) &&
             glyph_diag_write_text_message(&sink->writer, message) &&
             glyph_diag_writer_puts(&sink->writer, "\033[0m");
    } else {
        ok = glyph_diag_write_text_message(&sink->writer, message);
    }
    if (!ok) {
        sink->counters.write_errors++;
        return 0;
    }
    sink->counters.emitted++;
    return 1;
}

int glyph_diag_sink_group_begin(GlyphDiagSink *sink, const GlyphDiagGroup *group, size_t depth) {
    if (!sink) {
        return 0;
    }
    sink->counters.groups_started++;
    if (sink->format != GLYPH_DIAG_FORMAT_TEXT || !(sink->flags & GLYPH_DIAG_SINK_SHOW_GROUPS)) {
        return 1;
    }
    if (!glyph_diag_writer_repeat(&sink->writer, "  ", depth)) {
        sink->counters.write_errors++;
        return 0;
    }
    if (!glyph_diag_writer_puts(&sink->writer, "begin ")) {
        sink->counters.write_errors++;
        return 0;
    }
    if (group && group->code) {
        if (!glyph_diag_writer_printf(&sink->writer, "[%s] ", group->code)) {
            sink->counters.write_errors++;
            return 0;
        }
    }
    if (!glyph_diag_write_escaped_text(&sink->writer, group && group->title ? group->title : "diagnostics")) {
        sink->counters.write_errors++;
        return 0;
    }
    if (group && group->detail) {
        if (!glyph_diag_writer_puts(&sink->writer, ": ") ||
            !glyph_diag_write_escaped_text(&sink->writer, group->detail)) {
            sink->counters.write_errors++;
            return 0;
        }
    }
    if (!glyph_diag_writer_puts(&sink->writer, "\n")) {
        sink->counters.write_errors++;
        return 0;
    }
    return 1;
}

int glyph_diag_sink_group_end(GlyphDiagSink *sink, const GlyphDiagGroup *group, size_t depth) {
    if (!sink) {
        return 0;
    }
    sink->counters.groups_finished++;
    if (sink->format != GLYPH_DIAG_FORMAT_TEXT || !(sink->flags & GLYPH_DIAG_SINK_SHOW_GROUPS)) {
        return 1;
    }
    if (!glyph_diag_writer_repeat(&sink->writer, "  ", depth)) {
        sink->counters.write_errors++;
        return 0;
    }
    if (!glyph_diag_writer_puts(&sink->writer, "end ") ||
        !glyph_diag_write_escaped_text(&sink->writer, group && group->title ? group->title : "diagnostics") ||
        !glyph_diag_writer_puts(&sink->writer, "\n")) {
        sink->counters.write_errors++;
        return 0;
    }
    return 1;
}

int glyph_diag_sink_write_counters(GlyphDiagSink *sink, const GlyphDiagCounters *counters) {
    int ok;

    if (!sink || !counters) {
        return 0;
    }
    if (!(sink->flags & GLYPH_DIAG_SINK_SHOW_COUNTERS)) {
        return 1;
    }
    if (sink->format == GLYPH_DIAG_FORMAT_JSON) {
        if (!sink->json_started && !glyph_diag_sink_begin(sink)) {
            sink->counters.write_errors++;
            return 0;
        }
        if (sink->json_needs_comma && !glyph_diag_writer_puts(&sink->writer, ",\n")) {
            sink->counters.write_errors++;
            return 0;
        }
        sink->json_needs_comma = 1;
        ok = glyph_diag_writer_puts(&sink->writer, "  {\"type\":\"counters\",\"counters\":") &&
             glyph_diag_write_json_counters(&sink->writer, counters) &&
             glyph_diag_writer_puts(&sink->writer, "}");
    } else {
        ok = glyph_diag_write_text_counters(&sink->writer, counters);
    }
    if (!ok) {
        sink->counters.write_errors++;
        return 0;
    }
    return 1;
}

void glyph_diag_reporter_init(GlyphDiagReporter *reporter) {
    if (!reporter) {
        return;
    }
    reporter->sinks = NULL;
    reporter->sink_count = 0u;
    reporter->sink_capacity = 0u;
    glyph_diag_counters_init(&reporter->counters);
    reporter->min_severity = GLYPH_DIAG_TRACE;
    reporter->flags = 0u;
    reporter->groups = NULL;
    reporter->group_count = 0u;
    reporter->group_capacity = 0u;
    reporter->failed = 0;
}

void glyph_diag_reporter_free(GlyphDiagReporter *reporter) {
    if (!reporter) {
        return;
    }
    free(reporter->sinks);
    free(reporter->groups);
    glyph_diag_reporter_init(reporter);
}

int glyph_diag_reporter_add_sink(GlyphDiagReporter *reporter, GlyphDiagSink *sink) {
    size_t required;

    if (!reporter || !sink) {
        return 0;
    }
    if (!glyph_diag_size_add(reporter->sink_count, 1u, &required) ||
        !glyph_diag_reporter_reserve_sinks(reporter, required)) {
        reporter->failed = 1;
        return 0;
    }
    reporter->sinks[reporter->sink_count++] = sink;
    return 1;
}

void glyph_diag_reporter_clear_sinks(GlyphDiagReporter *reporter) {
    if (!reporter) {
        return;
    }
    reporter->sink_count = 0u;
}

void glyph_diag_reporter_set_min_severity(GlyphDiagReporter *reporter, GlyphDiagSeverity min_severity) {
    if (reporter && glyph_diag_severity_is_valid(min_severity)) {
        reporter->min_severity = min_severity;
    }
}

void glyph_diag_reporter_set_flags(GlyphDiagReporter *reporter, unsigned flags) {
    if (reporter) {
        reporter->flags = flags;
    }
}

const GlyphDiagCounters *glyph_diag_reporter_counters(const GlyphDiagReporter *reporter) {
    return reporter ? &reporter->counters : NULL;
}

int glyph_diag_reporter_begin(GlyphDiagReporter *reporter) {
    size_t i;
    int ok = 1;

    if (!reporter) {
        return 0;
    }
    for (i = 0u; i < reporter->sink_count; i++) {
        if (!glyph_diag_sink_begin(reporter->sinks[i])) {
            ok = 0;
        }
    }
    reporter->failed = reporter->failed || !ok;
    return ok;
}

int glyph_diag_reporter_finish(GlyphDiagReporter *reporter) {
    size_t i;
    int ok = 1;

    if (!reporter) {
        return 0;
    }
    for (i = 0u; i < reporter->sink_count; i++) {
        if (!glyph_diag_sink_write_counters(reporter->sinks[i], &reporter->counters)) {
            ok = 0;
        }
        if (!glyph_diag_sink_finish(reporter->sinks[i])) {
            ok = 0;
        }
    }
    reporter->failed = reporter->failed || !ok;
    return ok;
}

int glyph_diag_reporter_push_group(GlyphDiagReporter *reporter, const GlyphDiagGroup *group) {
    GlyphDiagGroup empty;
    size_t i;
    size_t depth;
    int ok = 1;

    if (!reporter) {
        return 0;
    }
    if (!group) {
        empty.title = "diagnostics";
        empty.code = NULL;
        empty.detail = NULL;
        empty.range = glyph_diag_range_none();
        group = &empty;
    }
    if (!glyph_diag_reporter_reserve_groups(reporter, reporter->group_count + 1u)) {
        reporter->failed = 1;
        return 0;
    }
    depth = reporter->group_count;
    reporter->groups[reporter->group_count++] = *group;
    reporter->counters.groups_started++;
    for (i = 0u; i < reporter->sink_count; i++) {
        if (!glyph_diag_sink_group_begin(reporter->sinks[i], group, depth)) {
            ok = 0;
        }
    }
    reporter->failed = reporter->failed || !ok;
    return ok;
}

int glyph_diag_reporter_pop_group(GlyphDiagReporter *reporter) {
    GlyphDiagGroup group;
    size_t i;
    size_t depth;
    int ok = 1;

    if (!reporter || reporter->group_count == 0u) {
        return 0;
    }
    depth = reporter->group_count - 1u;
    group = reporter->groups[depth];
    reporter->group_count = depth;
    reporter->counters.groups_finished++;
    for (i = 0u; i < reporter->sink_count; i++) {
        if (!glyph_diag_sink_group_end(reporter->sinks[i], &group, depth)) {
            ok = 0;
        }
    }
    reporter->failed = reporter->failed || !ok;
    return ok;
}

int glyph_diag_reporter_emit(GlyphDiagReporter *reporter, const GlyphDiagMessage *message) {
    size_t i;
    int ok = 1;

    if (!reporter || !message) {
        return 0;
    }
    glyph_diag_counters_add(&reporter->counters, message->severity);
    if (!glyph_diag_severity_at_least(message->severity, reporter->min_severity)) {
        reporter->counters.suppressed++;
        return 1;
    }
    for (i = 0u; i < reporter->sink_count; i++) {
        if (!glyph_diag_sink_emit(reporter->sinks[i], message, reporter->groups, reporter->group_count)) {
            ok = 0;
        }
    }
    if (ok) {
        reporter->counters.emitted++;
    } else {
        reporter->counters.write_errors++;
        reporter->failed = 1;
    }
    return ok;
}

int glyph_diag_reporter_emitf(GlyphDiagReporter *reporter,
                              GlyphDiagSeverity severity,
                              const char *code,
                              GlyphDiagRange range,
                              const char *format,
                              ...) {
    int ok;
    va_list args;

    va_start(args, format);
    ok = glyph_diag_reporter_vemitf(reporter, severity, code, range, format, args);
    va_end(args);
    return ok;
}

int glyph_diag_reporter_vemitf(GlyphDiagReporter *reporter,
                               GlyphDiagSeverity severity,
                               const char *code,
                               GlyphDiagRange range,
                               const char *format,
                               va_list args) {
    char stack[GLYPH_DIAG_PRINTF_STACK_SIZE];
    char *heap = NULL;
    const char *text;
    va_list copy;
    int needed;
    int ok;
    GlyphDiagMessage message;

    if (!reporter || !format) {
        return 0;
    }
    va_copy(copy, args);
    needed = vsnprintf(stack, sizeof(stack), format, copy);
    va_end(copy);
    if (needed < 0) {
        reporter->failed = 1;
        return 0;
    }
    if ((size_t)needed < sizeof(stack)) {
        text = stack;
    } else {
        heap = (char *)malloc((size_t)needed + 1u);
        if (!heap) {
            reporter->failed = 1;
            return 0;
        }
        needed = vsnprintf(heap, (size_t)needed + 1u, format, args);
        if (needed < 0) {
            free(heap);
            reporter->failed = 1;
            return 0;
        }
        text = heap;
    }
    message = glyph_diag_message(severity, code, text, range);
    ok = glyph_diag_reporter_emit(reporter, &message);
    free(heap);
    return ok;
}

int glyph_diag_write_escaped_text(GlyphDiagWriter *writer, const char *text) {
    static const char hex[] = "0123456789abcdef";
    const unsigned char *p;
    char escape[4];

    if (!writer) {
        return 0;
    }
    if (!text) {
        return 1;
    }
    for (p = (const unsigned char *)text; *p; p++) {
        switch (*p) {
        case '\n':
            if (!glyph_diag_writer_puts(writer, "\\n")) {
                return 0;
            }
            break;
        case '\r':
            if (!glyph_diag_writer_puts(writer, "\\r")) {
                return 0;
            }
            break;
        case '\t':
            if (!glyph_diag_writer_puts(writer, "\\t")) {
                return 0;
            }
            break;
        default:
            if (*p < 32u || *p == 127u) {
                escape[0] = '\\';
                escape[1] = 'x';
                escape[2] = hex[*p >> 4u];
                escape[3] = hex[*p & 15u];
                if (!glyph_diag_writer_write(writer, escape, sizeof(escape))) {
                    return 0;
                }
            } else if (!glyph_diag_writer_write(writer, (const char *)p, 1u)) {
                return 0;
            }
            break;
        }
    }
    return 1;
}

int glyph_diag_write_escaped_json(GlyphDiagWriter *writer, const char *text) {
    static const char hex[] = "0123456789abcdef";
    const unsigned char *p;
    char escape[6];

    if (!writer) {
        return 0;
    }
    if (!text) {
        return 1;
    }
    for (p = (const unsigned char *)text; *p; p++) {
        switch (*p) {
        case '"':
            if (!glyph_diag_writer_puts(writer, "\\\"")) {
                return 0;
            }
            break;
        case '\\':
            if (!glyph_diag_writer_puts(writer, "\\\\")) {
                return 0;
            }
            break;
        case '\b':
            if (!glyph_diag_writer_puts(writer, "\\b")) {
                return 0;
            }
            break;
        case '\f':
            if (!glyph_diag_writer_puts(writer, "\\f")) {
                return 0;
            }
            break;
        case '\n':
            if (!glyph_diag_writer_puts(writer, "\\n")) {
                return 0;
            }
            break;
        case '\r':
            if (!glyph_diag_writer_puts(writer, "\\r")) {
                return 0;
            }
            break;
        case '\t':
            if (!glyph_diag_writer_puts(writer, "\\t")) {
                return 0;
            }
            break;
        default:
            if (*p < 32u) {
                escape[0] = '\\';
                escape[1] = 'u';
                escape[2] = '0';
                escape[3] = '0';
                escape[4] = hex[*p >> 4u];
                escape[5] = hex[*p & 15u];
                if (!glyph_diag_writer_write(writer, escape, sizeof(escape))) {
                    return 0;
                }
            } else if (!glyph_diag_writer_write(writer, (const char *)p, 1u)) {
                return 0;
            }
            break;
        }
    }
    return 1;
}

int glyph_diag_write_text_message(GlyphDiagWriter *writer, const GlyphDiagMessage *message) {
    if (!writer || !message) {
        return 0;
    }
    if (!glyph_diag_write_location_text(writer, &message->range) ||
        !glyph_diag_writer_puts(writer, ": ") ||
        !glyph_diag_writer_puts(writer, glyph_diag_severity_name(message->severity))) {
        return 0;
    }
    if (message->code) {
        if (!glyph_diag_writer_puts(writer, "[") ||
            !glyph_diag_write_escaped_text(writer, message->code) ||
            !glyph_diag_writer_puts(writer, "]")) {
            return 0;
        }
    }
    if (!glyph_diag_writer_puts(writer, ": ")) {
        return 0;
    }
    if (!glyph_diag_write_escaped_text(writer, message->message ? message->message : "")) {
        return 0;
    }
    if (message->detail) {
        if (!glyph_diag_writer_puts(writer, "\n  detail: ") ||
            !glyph_diag_write_escaped_text(writer, message->detail)) {
            return 0;
        }
    }
    return glyph_diag_writer_puts(writer, "\n");
}

int glyph_diag_write_json_message(GlyphDiagWriter *writer,
                                  const GlyphDiagMessage *message,
                                  const GlyphDiagGroup *groups,
                                  size_t group_count) {
    size_t i;
    int needs_comma = 0;

    if (!writer || !message) {
        return 0;
    }
    if (!glyph_diag_writer_puts(writer, "{\"type\":\"diagnostic\"")) {
        return 0;
    }
    needs_comma = 1;
    if (!glyph_diag_write_optional_json_string(writer, "severity", glyph_diag_severity_name(message->severity), &needs_comma)) {
        return 0;
    }
    if (!glyph_diag_write_optional_json_string(writer, "code", message->code, &needs_comma)) {
        return 0;
    }
    if (!glyph_diag_write_optional_json_string(writer, "message", message->message, &needs_comma)) {
        return 0;
    }
    if (!glyph_diag_write_optional_json_string(writer, "detail", message->detail, &needs_comma)) {
        return 0;
    }
    if (!glyph_diag_write_range_json(writer, &message->range, &needs_comma)) {
        return 0;
    }
    if (groups && group_count != 0u) {
        if (needs_comma && !glyph_diag_writer_puts(writer, ",")) {
            return 0;
        }
        if (!glyph_diag_writer_puts(writer, "\"groups\":[")) {
            return 0;
        }
        for (i = 0u; i < group_count; i++) {
            if (i != 0u && !glyph_diag_writer_puts(writer, ",")) {
                return 0;
            }
            if (!glyph_diag_write_group_json(writer, &groups[i])) {
                return 0;
            }
        }
        if (!glyph_diag_writer_puts(writer, "]")) {
            return 0;
        }
    }
    return glyph_diag_writer_puts(writer, "}");
}

int glyph_diag_write_text_counters(GlyphDiagWriter *writer, const GlyphDiagCounters *counters) {
    size_t i;

    if (!writer || !counters) {
        return 0;
    }
    if (!glyph_diag_writer_puts(writer, "diagnostics: total=") ||
        !glyph_diag_write_u64(writer, counters->total) ||
        !glyph_diag_writer_puts(writer, " emitted=") ||
        !glyph_diag_write_u64(writer, counters->emitted) ||
        !glyph_diag_writer_puts(writer, " suppressed=") ||
        !glyph_diag_write_u64(writer, counters->suppressed) ||
        !glyph_diag_writer_puts(writer, " write-errors=") ||
        !glyph_diag_write_u64(writer, counters->write_errors) ||
        !glyph_diag_writer_puts(writer, "\n")) {
        return 0;
    }
    for (i = 0u; i < GLYPH_DIAG_SEVERITY_COUNT; i++) {
        if (!glyph_diag_writer_puts(writer, "  ") ||
            !glyph_diag_writer_puts(writer, glyph_diag_severity_name((GlyphDiagSeverity)i)) ||
            !glyph_diag_writer_puts(writer, "=") ||
            !glyph_diag_write_u64(writer, counters->by_severity[i]) ||
            !glyph_diag_writer_puts(writer, "\n")) {
            return 0;
        }
    }
    return 1;
}

int glyph_diag_write_json_counters(GlyphDiagWriter *writer, const GlyphDiagCounters *counters) {
    size_t i;

    if (!writer || !counters) {
        return 0;
    }
    if (!glyph_diag_writer_puts(writer, "{") ||
        !glyph_diag_writer_printf(writer,
                                  "\"total\":%" PRIu64 ",\"emitted\":%" PRIu64 ",\"suppressed\":%" PRIu64
                                  ",\"groups_started\":%" PRIu64 ",\"groups_finished\":%" PRIu64
                                  ",\"write_errors\":%" PRIu64 ",\"by_severity\":{",
                                  counters->total,
                                  counters->emitted,
                                  counters->suppressed,
                                  counters->groups_started,
                                  counters->groups_finished,
                                  counters->write_errors)) {
        return 0;
    }
    for (i = 0u; i < GLYPH_DIAG_SEVERITY_COUNT; i++) {
        if (i != 0u && !glyph_diag_writer_puts(writer, ",")) {
            return 0;
        }
        if (!glyph_diag_writer_printf(writer,
                                      "\"%s\":%" PRIu64,
                                      glyph_diag_severity_name((GlyphDiagSeverity)i),
                                      counters->by_severity[i])) {
            return 0;
        }
    }
    return glyph_diag_writer_puts(writer, "}}");
}
