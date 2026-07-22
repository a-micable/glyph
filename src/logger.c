/*
 * Logger Implementation - Advanced Logging Infrastructure
 * 
 * Multi-backend logging with support for file rotation, filtering,
 * performance tracking, and statistics collection.
 */

#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stddef.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>

#define MAX_LOG_MESSAGE_SIZE 4096
#define MAX_BUFFER_ENTRIES 10000
#define MAX_LOG_FILES 10

typedef struct {
    log_entry_t *entries;
    int capacity;
    int count;
    int write_index;
} log_buffer_t;

typedef struct logger_s {
    logger_config_t config;
    log_level_t current_level;
    log_backend_t active_backends;
    FILE *log_file;
    char log_file_path[512];
    int current_file_size;
    int file_rotation_count;
    
    log_buffer_t buffer;
    
    logger_stats_t stats;
    
    pthread_mutex_t lock;
    time_t last_log_time;
} logger_t;

logger_handle_t g_logger = NULL;

/* Helper Functions */
static const char *level_to_string(log_level_t level) {
    static const char *level_names[] = {
        "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
    };
    if (level >= 0 && level < LOG_LEVEL_COUNT) {
        return level_names[level];
    }
    return "UNKNOWN";
}

static const char *get_color_code(log_level_t level) {
    switch (level) {
        case LOG_TRACE:  return "\033[36m";  /* Cyan */
        case LOG_DEBUG:  return "\033[34m";  /* Blue */
        case LOG_INFO:   return "\033[32m";  /* Green */
        case LOG_WARN:   return "\033[33m";  /* Yellow */
        case LOG_ERROR:  return "\033[31m";  /* Red */
        case LOG_FATAL:  return "\033[1;31m"; /* Bold Red */
        default:         return "";
    }
}

static const char *get_reset_code(void) {
    return "\033[0m";
}

static int format_log_entry(char *buffer, size_t size,
                            const logger_t *logger,
                            const log_entry_t *entry) {
    int len = 0;
    const char *color = logger->config.enable_colors ? 
                       get_color_code(entry->level) : "";
    const char *reset = logger->config.enable_colors ? 
                       get_reset_code() : "";
    
    len += snprintf(buffer + len, size - len, "%s", color);
    
    if (logger->config.format_flags & LOG_FMT_TIMESTAMP) {
        char time_buf[32];
        struct tm *tm_info = localtime(&entry->timestamp);
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);
        len += snprintf(buffer + len, size - len, "[%s]", time_buf);
    }
    
    if (logger->config.format_flags & LOG_FMT_LEVEL) {
        len += snprintf(buffer + len, size - len, "[%-5s]", 
                       level_to_string(entry->level));
    }
    
    if (logger->config.format_flags & LOG_FMT_MODULE) {
        len += snprintf(buffer + len, size - len, "[%s]", entry->module);
    }
    
    if (logger->config.format_flags & LOG_FMT_FUNCTION) {
        len += snprintf(buffer + len, size - len, "[%s]", entry->function);
    }
    
    if (logger->config.format_flags & LOG_FMT_LINE) {
        len += snprintf(buffer + len, size - len, ":%u", entry->line);
    }
    
    if (logger->config.format_flags & LOG_FMT_THREAD) {
        len += snprintf(buffer + len, size - len, "[tid:%lx]", 
                       (unsigned long)entry->thread_id);
    }
    
    if (logger->config.format_flags & LOG_FMT_SEQUENCE) {
        len += snprintf(buffer + len, size - len, "[seq:%lu]", 
                       (unsigned long)entry->sequence_num);
    }
    
    if (logger->config.format_flags & LOG_FMT_ELAPSED) {
        if (entry->elapsed_us >= 0) {
            len += snprintf(buffer + len, size - len, "[+%ldμs]", 
                           (long)entry->elapsed_us);
        }
    }
    
    len += snprintf(buffer + len, size - len, " ");
    len += snprintf(buffer + len, size - len, "%s%s", entry->message, reset);
    
    return len;
}

static int open_log_file(logger_t *logger) {
    if (!logger->config.file_path) return 0;
    
    logger->log_file = fopen(logger->config.file_path, "a");
    if (!logger->log_file) {
        return -1;
    }
    
    fseek(logger->log_file, 0, SEEK_END);
    logger->current_file_size = ftell(logger->log_file);
    
    return 0;
}

static int close_log_file(logger_t *logger) {
    if (logger->log_file) {
        fclose(logger->log_file);
        logger->log_file = NULL;
        logger->current_file_size = 0;
        return 0;
    }
    return -1;
}

static int rotate_log_file(logger_t *logger) {
    if (!logger->config.file_path) return 0;
    
    close_log_file(logger);
    
    /* Create rotated filename */
    char rotated_path[512];
    snprintf(rotated_path, sizeof(rotated_path), "%s.%d",
            logger->config.file_path, logger->file_rotation_count);
    
    rename(logger->config.file_path, rotated_path);
    
    logger->file_rotation_count++;
    if (logger->file_rotation_count > logger->config.max_files) {
        logger->file_rotation_count = 1;
    }
    
    return open_log_file(logger);
}

static int write_to_backends(logger_t *logger, const log_entry_t *entry) {
    char formatted[MAX_LOG_MESSAGE_SIZE];
    int formatted_len = format_log_entry(formatted, sizeof(formatted),
                                         logger, entry);
    
    if (formatted_len <= 0) return -1;
    
    /* Write to stderr */
    if (logger->active_backends & LOG_BACKEND_STDERR) {
        fprintf(stderr, "%s\n", formatted);
        fflush(stderr);
    }
    
    /* Write to stdout */
    if (logger->active_backends & LOG_BACKEND_STDOUT) {
        fprintf(stdout, "%s\n", formatted);
        fflush(stdout);
    }
    
    /* Write to file */
    if ((logger->active_backends & LOG_BACKEND_FILE) && logger->log_file) {
        int written = fprintf(logger->log_file, "%s\n", formatted);
        if (written > 0) {
            logger->current_file_size += written + 1;
            
            if (logger->config.max_file_size > 0 &&
                logger->current_file_size > logger->config.max_file_size) {
                rotate_log_file(logger);
            }
        }
        
        if (!logger->config.buffered) {
            fflush(logger->log_file);
        }
    }
    
    /* Write to buffer */
    if (logger->active_backends & LOG_BACKEND_MEMORY) {
        log_buffer_t *buf = &logger->buffer;
        if (buf->count < buf->capacity) {
            memcpy(&buf->entries[buf->write_index], entry, sizeof(*entry));
            buf->write_index = (buf->write_index + 1) % buf->capacity;
            if (buf->count < buf->capacity) buf->count++;
        }
    }
    
    return 0;
}

/* Public API */
logger_handle_t logger_create(const logger_config_t *config) {
    logger_t *logger = calloc(1, sizeof(logger_t));
    if (!logger) return NULL;
    
    /* Copy configuration */
    if (config) {
        memcpy(&logger->config, config, sizeof(logger_config_t));
    } else {
        logger->config.min_level = LOG_INFO;
        logger->config.backends = LOG_BACKEND_STDERR;
        logger->config.format_flags = LOG_FMT_ALL;
        logger->config.enable_colors = 1;
        logger->config.buffer_size = 1000;
        logger->config.max_file_size = 10 * 1024 * 1024; /* 10MB */
        logger->config.max_files = 5;
    }
    
    logger->current_level = logger->config.min_level;
    logger->active_backends = logger->config.backends;
    
    /* Initialize threading */
    pthread_mutex_init(&logger->lock, NULL);
    
    /* Initialize buffer */
    if (logger->config.backends & LOG_BACKEND_MEMORY) {
        int buf_size = logger->config.buffer_size > 0 ? 
                      logger->config.buffer_size : MAX_BUFFER_ENTRIES;
        logger->buffer.capacity = buf_size;
        logger->buffer.entries = calloc(buf_size, sizeof(log_entry_t));
        if (!logger->buffer.entries) {
            free(logger);
            return NULL;
        }
    }
    
    /* Open log file if configured */
    if (logger->config.backends & LOG_BACKEND_FILE) {
        if (logger->config.file_path) {
            strncpy(logger->log_file_path, logger->config.file_path,
                   sizeof(logger->log_file_path) - 1);
            if (open_log_file(logger) != 0) {
                logger->active_backends &= ~LOG_BACKEND_FILE;
            }
        }
    }
    
    logger->last_log_time = time(NULL);
    
    return logger;
}

void logger_destroy(logger_handle_t handle) {
    logger_t *logger = (logger_t *)handle;
    if (!logger) return;
    
    logger_flush(logger);
    close_log_file(logger);
    
    if (logger->buffer.entries) {
        free(logger->buffer.entries);
    }
    
    pthread_mutex_destroy(&logger->lock);
    free(logger);
}

void logger_log(logger_handle_t handle, log_level_t level,
                const char *module, const char *function, uint32_t line,
                const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    logger_vlog(handle, level, module, function, line, fmt, args);
    va_end(args);
}

void logger_vlog(logger_handle_t handle, log_level_t level,
                 const char *module, const char *function, uint32_t line,
                 const char *fmt, va_list args) {
    logger_t *logger = (logger_t *)handle;
    if (!logger || level < logger->current_level) return;
    
    pthread_mutex_lock(&logger->lock);
    
    log_entry_t entry = {0};
    entry.timestamp = time(NULL);
    entry.level = level;
    entry.module = module;
    entry.function = function;
    entry.line = line;
    entry.thread_id = (uint64_t)pthread_self();
    entry.sequence_num = logger->stats.total_logs;
    
    /* Calculate elapsed time */
    time_t now = entry.timestamp;
    entry.elapsed_us = (now - logger->last_log_time) * 1000000;
    logger->last_log_time = now;
    
    /* Format message */
    char message[MAX_LOG_MESSAGE_SIZE];
    vsnprintf(message, sizeof(message), fmt, args);
    entry.message = message;
    
    /* Write to backends */
    write_to_backends(logger, &entry);
    
    /* Update statistics */
    logger->stats.total_logs++;
    logger->stats.logs_by_level[level]++;
    logger->stats.bytes_written += strlen(message);
    
    pthread_mutex_unlock(&logger->lock);
}

void logger_set_level(logger_handle_t handle, log_level_t level) {
    logger_t *logger = (logger_t *)handle;
    if (!logger) return;
    pthread_mutex_lock(&logger->lock);
    logger->current_level = level;
    pthread_mutex_unlock(&logger->lock);
}

log_level_t logger_get_level(logger_handle_t handle) {
    logger_t *logger = (logger_t *)handle;
    return logger ? logger->current_level : LOG_INFO;
}

int logger_set_backends(logger_handle_t handle, log_backend_t backends) {
    logger_t *logger = (logger_t *)handle;
    if (!logger) return -1;
    
    pthread_mutex_lock(&logger->lock);
    logger->active_backends = backends;
    pthread_mutex_unlock(&logger->lock);
    return 0;
}

int logger_add_backend(logger_handle_t handle, log_backend_t backend) {
    logger_t *logger = (logger_t *)handle;
    if (!logger) return -1;
    
    pthread_mutex_lock(&logger->lock);
    logger->active_backends |= backend;
    pthread_mutex_unlock(&logger->lock);
    return 0;
}

int logger_remove_backend(logger_handle_t handle, log_backend_t backend) {
    logger_t *logger = (logger_t *)handle;
    if (!logger) return -1;
    
    pthread_mutex_lock(&logger->lock);
    logger->active_backends &= ~backend;
    pthread_mutex_unlock(&logger->lock);
    return 0;
}

int logger_flush(logger_handle_t handle) {
    logger_t *logger = (logger_t *)handle;
    if (!logger) return -1;
    
    pthread_mutex_lock(&logger->lock);
    if (logger->log_file) {
        fflush(logger->log_file);
    }
    pthread_mutex_unlock(&logger->lock);
    return 0;
}

int logger_rotate_files(logger_handle_t handle) {
    logger_t *logger = (logger_t *)handle;
    if (!logger) return -1;
    
    pthread_mutex_lock(&logger->lock);
    int result = rotate_log_file(logger);
    pthread_mutex_unlock(&logger->lock);
    return result;
}

void logger_get_stats(logger_handle_t handle, logger_stats_t *stats) {
    logger_t *logger = (logger_t *)handle;
    if (!logger || !stats) return;
    
    pthread_mutex_lock(&logger->lock);
    memcpy(stats, &logger->stats, sizeof(logger_stats_t));
    pthread_mutex_unlock(&logger->lock);
}

void logger_reset_stats(logger_handle_t handle) {
    logger_t *logger = (logger_t *)handle;
    if (!logger) return;
    
    pthread_mutex_lock(&logger->lock);
    memset(&logger->stats, 0, sizeof(logger_stats_t));
    pthread_mutex_unlock(&logger->lock);
}

/* Global Logger */
int logger_init_global(const logger_config_t *config) {
    g_logger = logger_create(config);
    return g_logger ? 0 : -1;
}

void logger_shutdown_global(void) {
    if (g_logger) {
        logger_destroy(g_logger);
        g_logger = NULL;
    }
}
