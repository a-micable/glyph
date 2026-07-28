/*
 * Performance Metrics Implementation
 * 
 * Multi-threaded metrics collection with support for counters,
 * gauges, histograms, and timers with statistical analysis.
 */

#include "perf_metrics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <time.h>

#define METRICS_INITIAL_CAPACITY 100
#define HISTOGRAM_BUCKETS 32

typedef struct metric_node_s {
    char name[256];
    metric_type_t type;
    int flags;
    
    double current_value;
    double min_value;
    double max_value;
    double sum;
    double sum_squared;
    uint64_t count;
    
    histogram_t histogram;
    
    pthread_mutex_t lock;
    struct metric_node_s *next;
} metric_node_t;

typedef struct metrics_registry_s {
    metric_node_t **buckets;
    int bucket_count;
    int metric_count;
    pthread_rwlock_t lock;
} metrics_registry_t_impl;

metrics_registry_t g_metrics = NULL;

/* Hash function for metric names */
static uint32_t hash_metric_name(const char *name, int bucket_count) {
    uint32_t hash = 5381;
    int c;
    while ((c = *name++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash % bucket_count;
}

/* Find or create metric */
static metric_node_t *find_metric(metrics_registry_t registry,
                                   const char *name,
                                   int create_if_missing) {
    metrics_registry_t_impl *impl = (metrics_registry_t_impl *)registry;
    if (!impl || !name) return NULL;
    
    uint32_t idx = hash_metric_name(name, impl->bucket_count);
    metric_node_t *node = impl->buckets[idx];
    
    while (node) {
        if (strcmp(node->name, name) == 0) {
            return node;
        }
        node = node->next;
    }
    
    if (!create_if_missing) return NULL;
    
    /* Create new metric */
    node = calloc(1, sizeof(metric_node_t));
    if (!node) return NULL;
    
    strncpy(node->name, name, sizeof(node->name) - 1);
    node->min_value = DBL_MAX;
    node->max_value = DBL_MIN;
    node->flags = METRIC_FLAG_ENABLED;
    pthread_mutex_init(&node->lock, NULL);
    node->next = impl->buckets[idx];
    impl->buckets[idx] = node;
    impl->metric_count++;
    
    return node;
}

/* Public API Implementation */
metrics_registry_t metrics_registry_create(int initial_capacity) {
    if (initial_capacity <= 0) {
        initial_capacity = METRICS_INITIAL_CAPACITY;
    }
    
    metrics_registry_t_impl *impl = calloc(1, sizeof(metrics_registry_t_impl));
    if (!impl) return NULL;
    
    impl->bucket_count = initial_capacity;
    impl->buckets = calloc(initial_capacity, sizeof(metric_node_t *));
    if (!impl->buckets) {
        free(impl);
        return NULL;
    }
    
    pthread_rwlock_init(&impl->lock, NULL);
    
    return (metrics_registry_t)impl;
}

void metrics_registry_destroy(metrics_registry_t registry) {
    metrics_registry_t_impl *impl = (metrics_registry_t_impl *)registry;
    if (!impl) return;
    
    for (int i = 0; i < impl->bucket_count; i++) {
        metric_node_t *node = impl->buckets[i];
        while (node) {
            metric_node_t *next = node->next;
            if (node->histogram.buckets) {
                free(node->histogram.buckets);
            }
            pthread_mutex_destroy(&node->lock);
            free(node);
            node = next;
        }
    }
    
    free(impl->buckets);
    pthread_rwlock_destroy(&impl->lock);
    free(impl);
}

void metrics_counter_inc(metrics_registry_t registry, const char *name) {
    metrics_counter_add(registry, name, 1.0);
}

void metrics_counter_add(metrics_registry_t registry, const char *name,
                        double value) {
    metrics_registry_t_impl *impl = (metrics_registry_t_impl *)registry;
    if (!impl) return;
    
    pthread_rwlock_rdlock(&impl->lock);
    metric_node_t *node = find_metric(registry, name, 1);
    
    if (node) {
        pthread_mutex_lock(&node->lock);
        node->current_value += value;
        node->sum += value;
        node->count++;
        pthread_mutex_unlock(&node->lock);
    }
    pthread_rwlock_unlock(&impl->lock);
}

double metrics_counter_get(metrics_registry_t registry, const char *name) {
    metric_node_t *node = find_metric(registry, name, 0);
    if (!node) return 0.0;
    
    pthread_mutex_lock(&node->lock);
    double value = node->current_value;
    pthread_mutex_unlock(&node->lock);
    
    return value;
}

void metrics_gauge_set(metrics_registry_t registry, const char *name,
                       double value) {
    metric_node_t *node = find_metric(registry, name, 1);
    if (!node) return;
    
    pthread_mutex_lock(&node->lock);
    node->current_value = value;
    node->min_value = fmin(node->min_value, value);
    node->max_value = fmax(node->max_value, value);
    node->sum += value;
    node->count++;
    pthread_mutex_unlock(&node->lock);
}

double metrics_gauge_get(metrics_registry_t registry, const char *name) {
    return metrics_counter_get(registry, name);
}

void metrics_histogram_record(metrics_registry_t registry, const char *name,
                              double value) {
    metric_node_t *node = find_metric(registry, name, 1);
    if (!node) return;
    
    pthread_mutex_lock(&node->lock);
    
    node->min_value = fmin(node->min_value, value);
    node->max_value = fmax(node->max_value, value);
    node->sum += value;
    node->sum_squared += value * value;
    node->count++;
    
    if (!node->histogram.buckets && node->histogram.bucket_count == 0) {
        node->histogram.bucket_count = HISTOGRAM_BUCKETS;
        node->histogram.buckets = calloc(HISTOGRAM_BUCKETS,
                                         sizeof(histogram_bucket_t));
        if (node->histogram.buckets) {
            for (int i = 0; i < HISTOGRAM_BUCKETS; i++) {
                node->histogram.buckets[i].boundary = 
                    1.0 * (i + 1) / HISTOGRAM_BUCKETS * 100.0;
            }
        }
    }
    
    if (node->histogram.buckets) {
        node->histogram.total_count++;
        node->histogram.sum += value;
        
        for (int i = 0; i < node->histogram.bucket_count; i++) {
            if (value <= node->histogram.buckets[i].boundary) {
                node->histogram.buckets[i].count++;
                break;
            }
        }
    }
    
    pthread_mutex_unlock(&node->lock);
}

histogram_t *metrics_histogram_get(metrics_registry_t registry,
                                   const char *name) {
    metric_node_t *node = find_metric(registry, name, 0);
    if (!node) return NULL;
    return &node->histogram;
}

timer_context_t *metrics_timer_start(metrics_registry_t registry,
                                      const char *name) {
    (void)registry;
    (void)name;
    
    timer_context_t *ctx = calloc(1, sizeof(timer_context_t));
    if (!ctx) return NULL;
    
    clock_gettime(CLOCK_MONOTONIC, &ctx->start_time);
    return ctx;
}

void metrics_timer_end(timer_context_t *ctx) {
    if (!ctx) return;
    
    clock_gettime(CLOCK_MONOTONIC, &ctx->end_time);
    
    uint64_t start_us = ctx->start_time.tv_sec * 1000000ULL +
                       ctx->start_time.tv_nsec / 1000;
    uint64_t end_us = ctx->end_time.tv_sec * 1000000ULL +
                     ctx->end_time.tv_nsec / 1000;
    
    ctx->elapsed_us = end_us - start_us;
}

void metrics_timer_record(metrics_registry_t registry, const char *name,
                          uint64_t elapsed_us) {
    metrics_histogram_record(registry, name, (double)elapsed_us);
}

void metrics_get_statistics(histogram_t *histogram,
                            metric_statistics_t *stats) {
    if (!histogram || !stats) return;
    
    memset(stats, 0, sizeof(metric_statistics_t));
    
    if (histogram->total_count == 0) return;
    
    stats->mean = histogram->sum / histogram->total_count;
    stats->min = histogram->buckets[0].boundary;
    stats->max = histogram->buckets[histogram->bucket_count - 1].boundary;
    
    /* Percentile calculations */
    uint64_t cumulative = 0;
    for (int i = 0; i < histogram->bucket_count; i++) {
        cumulative += histogram->buckets[i].count;
        
        if (cumulative >= histogram->total_count * 0.95) {
            stats->percentile_95 = histogram->buckets[i].boundary;
        }
        if (cumulative >= histogram->total_count * 0.99) {
            stats->percentile_99 = histogram->buckets[i].boundary;
        }
    }
}

int metrics_register(metrics_registry_t registry, const char *name,
                     metric_type_t type, int flags) {
    metric_node_t *node = find_metric(registry, name, 1);
    if (!node) return -1;
    
    node->type = type;
    node->flags = flags;
    return 0;
}

void metrics_enable_all(metrics_registry_t registry) {
    metrics_registry_t_impl *impl = (metrics_registry_t_impl *)registry;
    if (!impl) return;
    
    for (int i = 0; i < impl->bucket_count; i++) {
        metric_node_t *node = impl->buckets[i];
        while (node) {
            node->flags |= METRIC_FLAG_ENABLED;
            node = node->next;
        }
    }
}

void metrics_disable_all(metrics_registry_t registry) {
    metrics_registry_t_impl *impl = (metrics_registry_t_impl *)registry;
    if (!impl) return;
    
    for (int i = 0; i < impl->bucket_count; i++) {
        metric_node_t *node = impl->buckets[i];
        while (node) {
            node->flags &= ~METRIC_FLAG_ENABLED;
            node = node->next;
        }
    }
}

int metrics_export_json(metrics_registry_t registry, const char *path) {
    FILE *fp = fopen(path, "w");
    if (!fp) return -1;
    
    fprintf(fp, "{\n");
    fprintf(fp, "  \"metrics\": [\n");
    fprintf(fp, "  ]\n");
    fprintf(fp, "}\n");
    
    fclose(fp);
    return 0;
}

int metrics_export_csv(metrics_registry_t registry, const char *path) {
    FILE *fp = fopen(path, "w");
    if (!fp) return -1;
    
    fprintf(fp, "metric_name,count,sum,mean,min,max\n");
    
    fclose(fp);
    return 0;
}

/* Global Metrics Registry */
int metrics_init_global(int initial_capacity) {
    g_metrics = metrics_registry_create(initial_capacity);
    return g_metrics ? 0 : -1;
}

void metrics_shutdown_global(void) {
    if (g_metrics) {
        metrics_registry_destroy(g_metrics);
        g_metrics = NULL;
    }
}
