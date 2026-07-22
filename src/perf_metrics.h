/*
 * Performance Metrics Module - Comprehensive Performance Tracking
 * 
 * Tracks performance metrics including operation timing, throughput,
 * resource usage, and provides statistical analysis capabilities.
 */

#ifndef GLYPH_METRICS_H
#define GLYPH_METRICS_H

#include <time.h>
#include <stdint.h>

/* Metric Types */
typedef enum {
    METRIC_COUNTER,      /* Simple counter */
    METRIC_GAUGE,        /* Point-in-time value */
    METRIC_HISTOGRAM,    /* Distribution of values */
    METRIC_TIMER         /* Time-based measurement */
} metric_type_t;

/* Metric Flags */
#define METRIC_FLAG_ENABLED    (1 << 0)
#define METRIC_FLAG_AGGREGATE  (1 << 1)
#define METRIC_FLAG_PERSISTENT (1 << 2)

typedef struct {
    const char *name;
    metric_type_t type;
    double value;
    double min;
    double max;
    double sum;
    double sum_sq;
    uint64_t count;
    int flags;
} metric_t;

/* Histogram Bucket */
typedef struct {
    double boundary;
    uint64_t count;
} histogram_bucket_t;

typedef struct {
    histogram_bucket_t *buckets;
    int bucket_count;
    double sum;
    uint64_t total_count;
} histogram_t;

/* Timer Context */
typedef struct {
    struct timespec start_time;
    struct timespec end_time;
    uint64_t elapsed_us;
} timer_context_t;

/* Metrics Registry Handle */
typedef struct metrics_registry_s *metrics_registry_t;

/* API Functions */
metrics_registry_t metrics_registry_create(int initial_capacity);
void metrics_registry_destroy(metrics_registry_t registry);

/* Counter Operations */
void metrics_counter_inc(metrics_registry_t registry, const char *name);
void metrics_counter_add(metrics_registry_t registry, const char *name, 
                        double value);
double metrics_counter_get(metrics_registry_t registry, const char *name);

/* Gauge Operations */
void metrics_gauge_set(metrics_registry_t registry, const char *name, 
                       double value);
double metrics_gauge_get(metrics_registry_t registry, const char *name);

/* Histogram Operations */
void metrics_histogram_record(metrics_registry_t registry, const char *name,
                              double value);
histogram_t *metrics_histogram_get(metrics_registry_t registry, 
                                   const char *name);

/* Timer Operations */
timer_context_t *metrics_timer_start(metrics_registry_t registry,
                                      const char *name);
void metrics_timer_end(timer_context_t *ctx);
void metrics_timer_record(metrics_registry_t registry, const char *name,
                          uint64_t elapsed_us);

/* Statistics */
typedef struct {
    double mean;
    double median;
    double stddev;
    double percentile_95;
    double percentile_99;
    double min;
    double max;
} metric_statistics_t;

void metrics_get_statistics(histogram_t *histogram, 
                            metric_statistics_t *stats);

/* Registry Operations */
int metrics_register(metrics_registry_t registry, const char *name,
                     metric_type_t type, int flags);

void metrics_enable_all(metrics_registry_t registry);
void metrics_disable_all(metrics_registry_t registry);

int metrics_export_json(metrics_registry_t registry, const char *path);
int metrics_export_csv(metrics_registry_t registry, const char *path);

/* Convenience Macros */
#define METRICS_TIMER_START(registry, name) \
    metrics_timer_start(registry, name)

#define METRICS_TIMER_END(ctx) \
    do { if (ctx) { metrics_timer_end(ctx); free(ctx); } } while(0)

#define METRICS_SCOPED_TIMER(registry, name) \
    timer_context_t *__timer = METRICS_TIMER_START(registry, name); \
    defer_cleanup(METRICS_TIMER_END, __timer)

/* Global Registry */
extern metrics_registry_t g_metrics;

int metrics_init_global(int initial_capacity);
void metrics_shutdown_global(void);

#endif /* GLYPH_METRICS_H */
