#ifndef GLYPH_DIAGNOSTICS_H
#define GLYPH_DIAGNOSTICS_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GLYPH_DIAG_SEVERITY_COUNT 7u
#define GLYPH_DIAG_NO_LINE 0u
#define GLYPH_DIAG_NO_COLUMN 0u
#define GLYPH_DIAG_NO_OFFSET ((size_t)-1)

typedef enum {
    GLYPH_DIAG_TRACE = 0,
    GLYPH_DIAG_DEBUG = 1,
    GLYPH_DIAG_INFO = 2,
    GLYPH_DIAG_NOTE = 3,
    GLYPH_DIAG_WARNING = 4,
    GLYPH_DIAG_ERROR = 5,
    GLYPH_DIAG_FATAL = 6
} GlyphDiagSeverity;

typedef enum {
    GLYPH_DIAG_FORMAT_TEXT = 0,
    GLYPH_DIAG_FORMAT_JSON = 1
} GlyphDiagFormat;

typedef enum {
    GLYPH_DIAG_COLOR_NEVER = 0,
    GLYPH_DIAG_COLOR_ALWAYS = 1
} GlyphDiagColorMode;

enum {
    GLYPH_DIAG_SINK_SHOW_SOURCE = 1 << 0,
    GLYPH_DIAG_SINK_SHOW_GROUPS = 1 << 1,
    GLYPH_DIAG_SINK_SHOW_COUNTERS = 1 << 2,
    GLYPH_DIAG_SINK_COLOR = 1 << 3
};

enum {
    GLYPH_DIAG_REPORT_SHOW_COUNTERS = 1 << 0,
    GLYPH_DIAG_REPORT_JSON_ARRAY = 1 << 1
};

typedef struct {
    const char *path;
    size_t offset;
    uint32_t line;
    uint32_t column;
} GlyphDiagPosition;

typedef struct {
    GlyphDiagPosition start;
    GlyphDiagPosition end;
} GlyphDiagRange;

typedef struct {
    GlyphDiagSeverity severity;
    const char *code;
    const char *message;
    const char *detail;
    GlyphDiagRange range;
} GlyphDiagMessage;

typedef struct {
    uint64_t total;
    uint64_t by_severity[GLYPH_DIAG_SEVERITY_COUNT];
    uint64_t emitted;
    uint64_t suppressed;
    uint64_t groups_started;
    uint64_t groups_finished;
    uint64_t write_errors;
} GlyphDiagCounters;

typedef int (*GlyphDiagWriteFn)(void *user_data, const char *data, size_t size);

typedef struct {
    GlyphDiagWriteFn write;
    void *user_data;
    uint64_t bytes_written;
    int failed;
} GlyphDiagWriter;

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
    int owns_buffer;
    int truncated;
    int failed;
} GlyphDiagMemoryWriter;

typedef struct {
    char *buffer;
    size_t length;
    size_t capacity;
    int truncated;
} GlyphDiagStringWriter;

typedef struct {
    FILE *fp;
    int close_on_free;
    int failed;
} GlyphDiagFileWriter;

typedef struct {
    const char *title;
    const char *code;
    const char *detail;
    GlyphDiagRange range;
} GlyphDiagGroup;

typedef struct {
    GlyphDiagWriter writer;
    GlyphDiagSeverity min_severity;
    GlyphDiagFormat format;
    GlyphDiagColorMode color_mode;
    unsigned flags;
    const char *name;
    GlyphDiagCounters counters;
    int json_started;
    int json_needs_comma;
} GlyphDiagSink;

typedef struct {
    GlyphDiagSink **sinks;
    size_t sink_count;
    size_t sink_capacity;
    GlyphDiagCounters counters;
    GlyphDiagSeverity min_severity;
    unsigned flags;
    GlyphDiagGroup *groups;
    size_t group_count;
    size_t group_capacity;
    int failed;
} GlyphDiagReporter;

const char *glyph_diag_severity_name(GlyphDiagSeverity severity);
const char *glyph_diag_severity_label(GlyphDiagSeverity severity);
int glyph_diag_severity_from_name(const char *name, GlyphDiagSeverity *severity);
int glyph_diag_severity_is_valid(GlyphDiagSeverity severity);
int glyph_diag_severity_at_least(GlyphDiagSeverity severity, GlyphDiagSeverity threshold);

GlyphDiagPosition glyph_diag_position(const char *path, uint32_t line, uint32_t column);
GlyphDiagPosition glyph_diag_position_offset(const char *path, size_t offset, uint32_t line, uint32_t column);
GlyphDiagRange glyph_diag_range(GlyphDiagPosition start, GlyphDiagPosition end);
GlyphDiagRange glyph_diag_range_line(const char *path, uint32_t line, uint32_t first_column, uint32_t last_column);
GlyphDiagRange glyph_diag_range_none(void);
int glyph_diag_position_has_location(const GlyphDiagPosition *position);
int glyph_diag_range_has_location(const GlyphDiagRange *range);

void glyph_diag_message_init(GlyphDiagMessage *message);
GlyphDiagMessage glyph_diag_message(GlyphDiagSeverity severity,
                                    const char *code,
                                    const char *message,
                                    GlyphDiagRange range);
GlyphDiagMessage glyph_diag_message_detail(GlyphDiagSeverity severity,
                                           const char *code,
                                           const char *message,
                                           const char *detail,
                                           GlyphDiagRange range);

void glyph_diag_counters_init(GlyphDiagCounters *counters);
void glyph_diag_counters_add(GlyphDiagCounters *counters, GlyphDiagSeverity severity);
void glyph_diag_counters_merge(GlyphDiagCounters *dst, const GlyphDiagCounters *src);
uint64_t glyph_diag_counters_severity(const GlyphDiagCounters *counters, GlyphDiagSeverity severity);
uint64_t glyph_diag_counters_errors(const GlyphDiagCounters *counters);
int glyph_diag_counters_has_errors(const GlyphDiagCounters *counters);

void glyph_diag_writer_init(GlyphDiagWriter *writer, GlyphDiagWriteFn write, void *user_data);
int glyph_diag_writer_write(GlyphDiagWriter *writer, const char *data, size_t size);
int glyph_diag_writer_puts(GlyphDiagWriter *writer, const char *text);
int glyph_diag_writer_printf(GlyphDiagWriter *writer, const char *format, ...);
int glyph_diag_writer_vprintf(GlyphDiagWriter *writer, const char *format, va_list args);

void glyph_diag_memory_writer_init(GlyphDiagMemoryWriter *memory);
void glyph_diag_memory_writer_init_with_capacity(GlyphDiagMemoryWriter *memory, size_t capacity);
void glyph_diag_memory_writer_init_buffer(GlyphDiagMemoryWriter *memory, char *buffer, size_t capacity);
void glyph_diag_memory_writer_free(GlyphDiagMemoryWriter *memory);
void glyph_diag_memory_writer_clear(GlyphDiagMemoryWriter *memory);
const char *glyph_diag_memory_writer_data(GlyphDiagMemoryWriter *memory);
size_t glyph_diag_memory_writer_length(const GlyphDiagMemoryWriter *memory);
char *glyph_diag_memory_writer_take(GlyphDiagMemoryWriter *memory, size_t *length);
GlyphDiagWriter glyph_diag_memory_writer(GlyphDiagMemoryWriter *memory);

void glyph_diag_string_writer_init(GlyphDiagStringWriter *string, char *buffer, size_t capacity);
void glyph_diag_string_writer_clear(GlyphDiagStringWriter *string);
const char *glyph_diag_string_writer_data(GlyphDiagStringWriter *string);
size_t glyph_diag_string_writer_length(const GlyphDiagStringWriter *string);
GlyphDiagWriter glyph_diag_string_writer(GlyphDiagStringWriter *string);

void glyph_diag_file_writer_init(GlyphDiagFileWriter *file, FILE *fp, int close_on_free);
int glyph_diag_file_writer_open(GlyphDiagFileWriter *file, const char *path);
void glyph_diag_file_writer_free(GlyphDiagFileWriter *file);
GlyphDiagWriter glyph_diag_file_writer(GlyphDiagFileWriter *file);

void glyph_diag_sink_init(GlyphDiagSink *sink,
                          GlyphDiagWriter writer,
                          GlyphDiagSeverity min_severity,
                          GlyphDiagFormat format);
void glyph_diag_sink_set_name(GlyphDiagSink *sink, const char *name);
void glyph_diag_sink_set_flags(GlyphDiagSink *sink, unsigned flags);
void glyph_diag_sink_set_color_mode(GlyphDiagSink *sink, GlyphDiagColorMode mode);
int glyph_diag_sink_begin(GlyphDiagSink *sink);
int glyph_diag_sink_finish(GlyphDiagSink *sink);
int glyph_diag_sink_emit(GlyphDiagSink *sink, const GlyphDiagMessage *message, const GlyphDiagGroup *groups, size_t group_count);
int glyph_diag_sink_group_begin(GlyphDiagSink *sink, const GlyphDiagGroup *group, size_t depth);
int glyph_diag_sink_group_end(GlyphDiagSink *sink, const GlyphDiagGroup *group, size_t depth);
int glyph_diag_sink_write_counters(GlyphDiagSink *sink, const GlyphDiagCounters *counters);

void glyph_diag_reporter_init(GlyphDiagReporter *reporter);
void glyph_diag_reporter_free(GlyphDiagReporter *reporter);
int glyph_diag_reporter_add_sink(GlyphDiagReporter *reporter, GlyphDiagSink *sink);
void glyph_diag_reporter_clear_sinks(GlyphDiagReporter *reporter);
void glyph_diag_reporter_set_min_severity(GlyphDiagReporter *reporter, GlyphDiagSeverity min_severity);
void glyph_diag_reporter_set_flags(GlyphDiagReporter *reporter, unsigned flags);
const GlyphDiagCounters *glyph_diag_reporter_counters(const GlyphDiagReporter *reporter);
int glyph_diag_reporter_begin(GlyphDiagReporter *reporter);
int glyph_diag_reporter_finish(GlyphDiagReporter *reporter);
int glyph_diag_reporter_push_group(GlyphDiagReporter *reporter, const GlyphDiagGroup *group);
int glyph_diag_reporter_pop_group(GlyphDiagReporter *reporter);
int glyph_diag_reporter_emit(GlyphDiagReporter *reporter, const GlyphDiagMessage *message);
int glyph_diag_reporter_emitf(GlyphDiagReporter *reporter,
                              GlyphDiagSeverity severity,
                              const char *code,
                              GlyphDiagRange range,
                              const char *format,
                              ...);
int glyph_diag_reporter_vemitf(GlyphDiagReporter *reporter,
                               GlyphDiagSeverity severity,
                               const char *code,
                               GlyphDiagRange range,
                               const char *format,
                               va_list args);

int glyph_diag_write_escaped_text(GlyphDiagWriter *writer, const char *text);
int glyph_diag_write_escaped_json(GlyphDiagWriter *writer, const char *text);
int glyph_diag_write_text_message(GlyphDiagWriter *writer, const GlyphDiagMessage *message);
int glyph_diag_write_json_message(GlyphDiagWriter *writer,
                                  const GlyphDiagMessage *message,
                                  const GlyphDiagGroup *groups,
                                  size_t group_count);
int glyph_diag_write_text_counters(GlyphDiagWriter *writer, const GlyphDiagCounters *counters);
int glyph_diag_write_json_counters(GlyphDiagWriter *writer, const GlyphDiagCounters *counters);

#ifdef __cplusplus
}
#endif

#endif
