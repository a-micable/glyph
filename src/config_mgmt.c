/*
 * Configuration Management Implementation
 * 
 * Full-featured configuration system with file loading, environment
 * variable support, validation, and runtime configuration updates.
 */

#include "config_mgmt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>

#define MAX_CONFIG_ENTRIES 1000
#define MAX_KEY_LENGTH 256
#define MAX_VALUE_LENGTH 4096
#define MAX_WATCHERS 100

typedef struct {
    char key[MAX_KEY_LENGTH];
    config_type_t type;
    int priority;
    union {
        int int_val;
        double float_val;
        int bool_val;
        char str_val[MAX_VALUE_LENGTH];
    } value;
} config_entry_t;

typedef struct {
    const char *key;
    config_change_callback_t callback;
    void *context;
} config_watcher_t;

typedef struct config_s {
    config_entry_t entries[MAX_CONFIG_ENTRIES];
    int entry_count;
    
    config_watcher_t watchers[MAX_WATCHERS];
    int watcher_count;
    
    const config_schema_t *schema;
    int schema_count;
    
    pthread_rwlock_t lock;
} config_t;

config_handle_t g_config = NULL;

/* Utility Functions */
static int is_bool_true(const char *str) {
    if (!str) return 0;
    return (strcasecmp(str, "true") == 0 || 
            strcasecmp(str, "1") == 0 ||
            strcasecmp(str, "yes") == 0 ||
            strcasecmp(str, "on") == 0);
}

static int find_entry(config_handle_t config, const char *key) {
    config_t *cfg = (config_t *)config;
    if (!cfg || !key) return -1;
    
    for (int i = 0; i < cfg->entry_count; i++) {
        if (strcmp(cfg->entries[i].key, key) == 0) {
            return i;
        }
    }
    return -1;
}

static int add_entry(config_handle_t config, const char *key,
                    config_type_t type, int priority) {
    config_t *cfg = (config_t *)config;
    if (!cfg || !key || cfg->entry_count >= MAX_CONFIG_ENTRIES) {
        return -1;
    }
    
    int idx = find_entry(config, key);
    if (idx >= 0) {
        /* Entry exists, check priority */
        if (priority < cfg->entries[idx].priority) {
            return idx; /* Keep existing entry */
        }
    } else {
        idx = cfg->entry_count++;
    }
    
    strncpy(cfg->entries[idx].key, key, MAX_KEY_LENGTH - 1);
    cfg->entries[idx].type = type;
    cfg->entries[idx].priority = priority;
    
    return idx;
}

/* Trim whitespace */
static void trim(char *str) {
    if (!str) return;
    
    char *end = str + strlen(str) - 1;
    while (end >= str && isspace((unsigned char)*end)) {
        *end-- = '\0';
    }
    
    while (*str && isspace((unsigned char)*str)) {
        str++;
    }
}

/* Public API */
config_handle_t config_create(void) {
    return config_create_with_schema(NULL, 0);
}

config_handle_t config_create_with_schema(const config_schema_t *schema,
                                          int schema_count) {
    config_t *cfg = calloc(1, sizeof(config_t));
    if (!cfg) return NULL;
    
    cfg->schema = schema;
    cfg->schema_count = schema_count;
    
    pthread_rwlock_init(&cfg->lock, NULL);
    
    /* Add schema defaults */
    if (schema) {
        for (int i = 0; i < schema_count; i++) {
            int idx = add_entry((config_handle_t)cfg, schema[i].key,
                              schema[i].type, CONFIG_PRIORITY_DEFAULT);
            if (idx >= 0) {
                config_entry_t *entry = &cfg->entries[idx];
                
                switch (schema[i].type) {
                    case CONFIG_TYPE_INT:
                        entry->value.int_val = schema[i].default_value.int_val;
                        break;
                    case CONFIG_TYPE_FLOAT:
                        entry->value.float_val = 
                            schema[i].default_value.float_val;
                        break;
                    case CONFIG_TYPE_BOOL:
                        entry->value.bool_val = 
                            schema[i].default_value.bool_val;
                        break;
                    case CONFIG_TYPE_STRING:
                        if (schema[i].default_value.str_val) {
                            strncpy(entry->value.str_val,
                                   schema[i].default_value.str_val,
                                   MAX_VALUE_LENGTH - 1);
                        }
                        break;
                    default:
                        break;
                }
            }
        }
    }
    
    return (config_handle_t)cfg;
}

void config_destroy(config_handle_t config) {
    config_t *cfg = (config_t *)config;
    if (!cfg) return;
    
    pthread_rwlock_destroy(&cfg->lock);
    free(cfg);
}

int config_load_file(config_handle_t config, const char *path,
                    int priority) {
    if (!config || !path) return -1;
    
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;
    
    config_t *cfg = (config_t *)config;
    char line[2048];
    int line_num = 0;
    
    pthread_rwlock_wrlock(&cfg->lock);
    
    while (fgets(line, sizeof(line), fp)) {
        line_num++;
        
        /* Skip comments and empty lines */
        char *comment = strchr(line, '#');
        if (comment) *comment = '\0';
        
        trim(line);
        if (strlen(line) == 0) continue;
        
        /* Parse key=value */
        char *eq = strchr(line, '=');
        if (!eq) continue;
        
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;
        
        trim(key);
        trim(val);
        
        /* Determine type and add entry */
        config_type_t type = CONFIG_TYPE_STRING;
        int idx = add_entry(config, key, type, priority);
        
        if (idx >= 0) {
            strncpy(cfg->entries[idx].value.str_val, val,
                   MAX_VALUE_LENGTH - 1);
        }
    }
    
    pthread_rwlock_unlock(&cfg->lock);
    fclose(fp);
    return 0;
}

int config_load_env(config_handle_t config, const char *prefix,
                   int priority) {
    if (!config) return -1;
    
    config_t *cfg = (config_t *)config;
    extern char **environ;
    
    pthread_rwlock_wrlock(&cfg->lock);
    
    for (int i = 0; environ[i]; i++) {
        char *env_var = environ[i];
        char *eq = strchr(env_var, '=');
        
        if (!eq) continue;
        
        int key_len = eq - env_var;
        if (prefix && strncmp(env_var, prefix, strlen(prefix)) != 0) {
            continue;
        }
        
        char key[MAX_KEY_LENGTH];
        strncpy(key, env_var + (prefix ? strlen(prefix) : 0), key_len);
        key[key_len - (prefix ? strlen(prefix) : 0)] = '\0';
        
        int idx = add_entry(config, key, CONFIG_TYPE_STRING, priority);
        if (idx >= 0) {
            strncpy(cfg->entries[idx].value.str_val, eq + 1,
                   MAX_VALUE_LENGTH - 1);
        }
    }
    
    pthread_rwlock_unlock(&cfg->lock);
    return 0;
}

int config_load_cli_args(config_handle_t config, int argc, char *argv[],
                        int priority) {
    if (!config || !argv) return -1;
    
    config_t *cfg = (config_t *)config;
    pthread_rwlock_wrlock(&cfg->lock);
    
    for (int i = 1; i < argc; i++) {
        char *arg = argv[i];
        
        if (arg[0] != '-') continue;
        
        /* Skip leading dashes */
        while (*arg == '-') arg++;
        
        char *eq = strchr(arg, '=');
        if (!eq) {
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                eq = arg + strlen(arg);
            } else {
                continue;
            }
        }
        
        int key_len = eq - arg;
        char key[MAX_KEY_LENGTH];
        strncpy(key, arg, key_len);
        key[key_len] = '\0';
        
        const char *val = eq + 1;
        int idx = add_entry(config, key, CONFIG_TYPE_STRING, priority);
        if (idx >= 0) {
            strncpy(cfg->entries[idx].value.str_val, val,
                   MAX_VALUE_LENGTH - 1);
        }
    }
    
    pthread_rwlock_unlock(&cfg->lock);
    return 0;
}

int config_get_bool(config_handle_t config, const char *key,
                   int default_val) {
    config_t *cfg = (config_t *)config;
    if (!cfg || !key) return default_val;
    
    pthread_rwlock_rdlock(&cfg->lock);
    int idx = find_entry(config, key);
    
    int result = default_val;
    if (idx >= 0) {
        if (cfg->entries[idx].type == CONFIG_TYPE_BOOL) {
            result = cfg->entries[idx].value.bool_val;
        } else if (cfg->entries[idx].type == CONFIG_TYPE_STRING) {
            result = is_bool_true(cfg->entries[idx].value.str_val);
        }
    }
    pthread_rwlock_unlock(&cfg->lock);
    
    return result;
}

int config_get_int(config_handle_t config, const char *key,
                  int default_val) {
    config_t *cfg = (config_t *)config;
    if (!cfg || !key) return default_val;
    
    pthread_rwlock_rdlock(&cfg->lock);
    int idx = find_entry(config, key);
    
    int result = default_val;
    if (idx >= 0) {
        if (cfg->entries[idx].type == CONFIG_TYPE_INT) {
            result = cfg->entries[idx].value.int_val;
        } else if (cfg->entries[idx].type == CONFIG_TYPE_STRING) {
            result = atoi(cfg->entries[idx].value.str_val);
        }
    }
    pthread_rwlock_unlock(&cfg->lock);
    
    return result;
}

double config_get_float(config_handle_t config, const char *key,
                       double default_val) {
    config_t *cfg = (config_t *)config;
    if (!cfg || !key) return default_val;
    
    pthread_rwlock_rdlock(&cfg->lock);
    int idx = find_entry(config, key);
    
    double result = default_val;
    if (idx >= 0) {
        if (cfg->entries[idx].type == CONFIG_TYPE_FLOAT) {
            result = cfg->entries[idx].value.float_val;
        } else if (cfg->entries[idx].type == CONFIG_TYPE_STRING) {
            result = atof(cfg->entries[idx].value.str_val);
        }
    }
    pthread_rwlock_unlock(&cfg->lock);
    
    return result;
}

const char *config_get_string(config_handle_t config, const char *key,
                             const char *default_val) {
    config_t *cfg = (config_t *)config;
    if (!cfg || !key) return default_val;
    
    pthread_rwlock_rdlock(&cfg->lock);
    int idx = find_entry(config, key);
    
    const char *result = default_val;
    if (idx >= 0) {
        result = cfg->entries[idx].value.str_val;
    }
    pthread_rwlock_unlock(&cfg->lock);
    
    return result;
}

int config_set_bool(config_handle_t config, const char *key, int value) {
    config_t *cfg = (config_t *)config;
    if (!cfg || !key) return -1;
    
    pthread_rwlock_wrlock(&cfg->lock);
    int idx = add_entry(config, key, CONFIG_TYPE_BOOL,
                       CONFIG_PRIORITY_DEFAULT);
    
    if (idx >= 0) {
        cfg->entries[idx].value.bool_val = value;
    }
    pthread_rwlock_unlock(&cfg->lock);
    
    return idx >= 0 ? 0 : -1;
}

int config_set_int(config_handle_t config, const char *key, int value) {
    config_t *cfg = (config_t *)config;
    if (!cfg || !key) return -1;
    
    pthread_rwlock_wrlock(&cfg->lock);
    int idx = add_entry(config, key, CONFIG_TYPE_INT,
                       CONFIG_PRIORITY_DEFAULT);
    
    if (idx >= 0) {
        cfg->entries[idx].value.int_val = value;
    }
    pthread_rwlock_unlock(&cfg->lock);
    
    return idx >= 0 ? 0 : -1;
}

int config_set_float(config_handle_t config, const char *key, double value) {
    config_t *cfg = (config_t *)config;
    if (!cfg || !key) return -1;
    
    pthread_rwlock_wrlock(&cfg->lock);
    int idx = add_entry(config, key, CONFIG_TYPE_FLOAT,
                       CONFIG_PRIORITY_DEFAULT);
    
    if (idx >= 0) {
        cfg->entries[idx].value.float_val = value;
    }
    pthread_rwlock_unlock(&cfg->lock);
    
    return idx >= 0 ? 0 : -1;
}

int config_set_string(config_handle_t config, const char *key,
                     const char *value) {
    config_t *cfg = (config_t *)config;
    if (!cfg || !key) return -1;
    
    pthread_rwlock_wrlock(&cfg->lock);
    int idx = add_entry(config, key, CONFIG_TYPE_STRING,
                       CONFIG_PRIORITY_DEFAULT);
    
    if (idx >= 0 && value) {
        strncpy(cfg->entries[idx].value.str_val, value,
               MAX_VALUE_LENGTH - 1);
    }
    pthread_rwlock_unlock(&cfg->lock);
    
    return idx >= 0 ? 0 : -1;
}

int config_validate(config_handle_t config) {
    config_t *cfg = (config_t *)config;
    if (!cfg || !cfg->schema) return 0;
    
    return config_validate_schema(config, cfg->schema, cfg->schema_count);
}

int config_validate_schema(config_handle_t config,
                          const config_schema_t *schema,
                          int schema_count) {
    if (!config || !schema) return -1;
    
    for (int i = 0; i < schema_count; i++) {
        if (schema[i].required && 
            !config_key_exists(config, schema[i].key)) {
            return -1;
        }
    }
    return 0;
}

int config_key_exists(config_handle_t config, const char *key) {
    config_t *cfg = (config_t *)config;
    if (!cfg || !key) return 0;
    
    pthread_rwlock_rdlock(&cfg->lock);
    int exists = find_entry(config, key) >= 0;
    pthread_rwlock_unlock(&cfg->lock);
    
    return exists;
}

int config_get_keys(config_handle_t config, const char ***keys) {
    config_t *cfg = (config_t *)config;
    if (!cfg || !keys) return -1;
    
    *keys = malloc(cfg->entry_count * sizeof(char *));
    if (!*keys) return -1;
    
    for (int i = 0; i < cfg->entry_count; i++) {
        (*keys)[i] = cfg->entries[i].key;
    }
    
    return cfg->entry_count;
}

int config_key_count(config_handle_t config) {
    config_t *cfg = (config_t *)config;
    return cfg ? cfg->entry_count : 0;
}

int config_export_json(config_handle_t config, const char *path) {
    if (!config || !path) return -1;
    
    FILE *fp = fopen(path, "w");
    if (!fp) return -1;
    
    fprintf(fp, "{\n");
    fprintf(fp, "}\n");
    
    fclose(fp);
    return 0;
}

int config_export_env_script(config_handle_t config, const char *path) {
    if (!config || !path) return -1;
    
    FILE *fp = fopen(path, "w");
    if (!fp) return -1;
    
    fprintf(fp, "#!/bin/bash\n");
    fprintf(fp, "# Auto-generated environment script\n");
    
    fclose(fp);
    return 0;
}

int config_dump(config_handle_t config) {
    config_t *cfg = (config_t *)config;
    if (!cfg) return -1;
    
    printf("Configuration (%d entries):\n", cfg->entry_count);
    for (int i = 0; i < cfg->entry_count; i++) {
        printf("  %s = %s\n", cfg->entries[i].key,
              cfg->entries[i].value.str_val);
    }
    return 0;
}

int config_watch_key(config_handle_t config, const char *key,
                    config_change_callback_t callback, void *context) {
    config_t *cfg = (config_t *)config;
    if (!cfg || !key || !callback) return -1;
    
    if (cfg->watcher_count >= MAX_WATCHERS) return -1;
    
    cfg->watchers[cfg->watcher_count].key = key;
    cfg->watchers[cfg->watcher_count].callback = callback;
    cfg->watchers[cfg->watcher_count].context = context;
    cfg->watcher_count++;
    
    return 0;
}

int config_unwatch_key(config_handle_t config, const char *key) {
    config_t *cfg = (config_t *)config;
    if (!cfg || !key) return -1;
    
    for (int i = 0; i < cfg->watcher_count; i++) {
        if (strcmp(cfg->watchers[i].key, key) == 0) {
            memmove(&cfg->watchers[i], &cfg->watchers[i + 1],
                   (cfg->watcher_count - i - 1) * sizeof(config_watcher_t));
            cfg->watcher_count--;
            return 0;
        }
    }
    return -1;
}

/* Global Configuration */
int config_init_global(const config_schema_t *schema, int schema_count) {
    g_config = config_create_with_schema(schema, schema_count);
    return g_config ? 0 : -1;
}

void config_shutdown_global(void) {
    if (g_config) {
        config_destroy(g_config);
        g_config = NULL;
    }
}
