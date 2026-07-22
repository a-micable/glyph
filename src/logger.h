/*
 * Logger Module - Advanced Logging Framework
 * 
 * Provides comprehensive logging with multiple backends, log levels,
 * filtering, formatting, and performance tracking capabilities.
 * 
 * Company-grade logging infrastructure for enterprise deployments.
 */

#ifndef GLYPH_LOGGER_H
#define GLYPH_LOGGER_H

#include <time.h>
#include <stdint.h>
#include <stdarg.h>

/* Log Levels - Hierarchical importance */
typedef enum {
    LOG_TRACE   = 0,   /* Detailed trace-level information */
    LOG_DEBUG   = 1,   /* Debug information */
    LOG_INFO    = 2,   /* Informational messages */
    LOG_WARN    = 3,   /* Warning messages */
    LOG_ERROR   = 4,   /* Error messages */
    LOG_FATAL   = 5,   /* Fatal errors requiring termination */
    LOG_LEVEL_COUNT = 6
} log_level_t;

/* Backend Types */
typedef enum {
    LOG_BACKEND_STDERR  = 1 << 0,  /* Standard error output */
    LOG_BACKEND_STDOUT  = 1 << 1,  /* Standard output */
    LOG_BACKEND_FILE    = 1 << 2,  /* File logging */
    LOG_BACKEND_SYSLOG  = 1 << 3,  /* System logger */
    LOG_BACKEND_MEMORY  = 1 << 4   /* In-memory buffer */
} log_backend_t;

/* Log Entry Structure */
typedef struct {
    time_t timestamp;
    log_level_t level;
    const char *module;
    const char *function;
    uint32_t line;
    const char *message;
    uint64_t sequence_num;
    uint64_t thread_id;
    int64_t elapsed_us;  /* Microseconds since last log */
} log_entry_t;

/* Logger Configuration */
typedef struct {
    log_level_t min_level;
    log_backend_t backends;
    int max_file_size;
    int max_files;
    const char *file_path;
    int format_flags;
    int enable_colors;
    int buffered;
    int buffer_size;
} logger_config_t;

/* Format Flags */
#define LOG_FMT_TIMESTAMP  (1 << 0)
#define LOG_FMT_LEVEL      (1 << 1)
#define LOG_FMT_MODULE     (1 << 2)
#define LOG_FMT_FUNCTION   (1 << 3)
#define LOG_FMT_LINE       (1 << 4)
#define LOG_FMT_THREAD     (1 << 5)
#define LOG_FMT_SEQUENCE   (1 << 6)
#define LOG_FMT_ELAPSED    (1 << 7)
#define LOG_FMT_ALL        (LOG_FMT_TIMESTAMP | LOG_FMT_LEVEL | LOG_FMT_MODULE | \
                            LOG_FMT_FUNCTION | LOG_FMT_LINE | LOG_FMT_THREAD | \
                            LOG_FMT_SEQUENCE | LOG_FMT_ELAPSED)

/* Logger Handle */
typedef struct logger_s *logger_handle_t;

/* API Functions */
logger_handle_t logger_create(const logger_config_t *config);
void logger_destroy(logger_handle_t logger);

void logger_log(logger_handle_t logger, log_level_t level,
                const char *module, const char *function, uint32_t line,
                const char *fmt, ...);

void logger_vlog(logger_handle_t logger, log_level_t level,
                 const char *module, const char *function, uint32_t line,
                 const char *fmt, va_list args);

void logger_set_level(logger_handle_t logger, log_level_t level);
log_level_t logger_get_level(logger_handle_t logger);

int logger_set_backends(logger_handle_t logger, log_backend_t backends);
int logger_add_backend(logger_handle_t logger, log_backend_t backend);
int logger_remove_backend(logger_handle_t logger, log_backend_t backend);

int logger_flush(logger_handle_t logger);
int logger_rotate_files(logger_handle_t logger);

/* Statistics */
typedef struct {
    uint64_t total_logs;
    uint64_t logs_by_level[LOG_LEVEL_COUNT];
    uint64_t bytes_written;
    uint64_t errors;
} logger_stats_t;

void logger_get_stats(logger_handle_t logger, logger_stats_t *stats);
void logger_reset_stats(logger_handle_t logger);

/* Convenience Macros */
#define LOG_TRACE(logger, ...) \
    logger_log(logger, LOG_TRACE, __FILE__, __func__, __LINE__, __VA_ARGS__)

#define LOG_DEBUG(logger, ...) \
    logger_log(logger, LOG_DEBUG, __FILE__, __func__, __LINE__, __VA_ARGS__)

#define LOG_INFO(logger, ...) \
    logger_log(logger, LOG_INFO, __FILE__, __func__, __LINE__, __VA_ARGS__)

#define LOG_WARN(logger, ...) \
    logger_log(logger, LOG_WARN, __FILE__, __func__, __LINE__, __VA_ARGS__)

#define LOG_ERROR(logger, ...) \
    logger_log(logger, LOG_ERROR, __FILE__, __func__, __LINE__, __VA_ARGS__)

#define LOG_FATAL(logger, ...) \
    logger_log(logger, LOG_FATAL, __FILE__, __func__, __LINE__, __VA_ARGS__)

/* Global Logger Instance */
extern logger_handle_t g_logger;

int logger_init_global(const logger_config_t *config);
void logger_shutdown_global(void);

#endif /* GLYPH_LOGGER_H */
