/*
 * Configuration Management - Production-Grade Config System
 * 
 * Supports multiple config sources (files, environment, CLI),
 * schema validation, defaults, and runtime configuration updates.
 */

#ifndef GLYPH_CONFIG_H
#define GLYPH_CONFIG_H

#include <stdint.h>

typedef enum {
    CONFIG_TYPE_BOOL,
    CONFIG_TYPE_INT,
    CONFIG_TYPE_FLOAT,
    CONFIG_TYPE_STRING,
    CONFIG_TYPE_ARRAY,
    CONFIG_TYPE_OBJECT
} config_type_t;

typedef struct {
    const char *key;
    config_type_t type;
    int required;
    const char *description;
    union {
        int int_val;
        double float_val;
        int bool_val;
        char *str_val;
    } default_value;
} config_schema_t;

typedef struct config_s *config_handle_t;

/* Config Source Priorities */
#define CONFIG_PRIORITY_DEFAULT    1
#define CONFIG_PRIORITY_FILE       5
#define CONFIG_PRIORITY_ENV        8
#define CONFIG_PRIORITY_CLI        10

/* API Functions */
config_handle_t config_create(void);
config_handle_t config_create_with_schema(const config_schema_t *schema,
                                          int schema_count);
void config_destroy(config_handle_t config);

/* Loading Configuration */
int config_load_file(config_handle_t config, const char *path,
                    int priority);
int config_load_env(config_handle_t config, const char *prefix,
                   int priority);
int config_load_cli_args(config_handle_t config, int argc, char *argv[],
                        int priority);

/* Getting Configuration Values */
int config_get_bool(config_handle_t config, const char *key,
                   int default_val);
int config_get_int(config_handle_t config, const char *key,
                  int default_val);
double config_get_float(config_handle_t config, const char *key,
                       double default_val);
const char *config_get_string(config_handle_t config, const char *key,
                             const char *default_val);

/* Setting Configuration Values */
int config_set_bool(config_handle_t config, const char *key, int value);
int config_set_int(config_handle_t config, const char *key, int value);
int config_set_float(config_handle_t config, const char *key, double value);
int config_set_string(config_handle_t config, const char *key,
                     const char *value);

/* Validation */
int config_validate(config_handle_t config);
int config_validate_schema(config_handle_t config,
                          const config_schema_t *schema,
                          int schema_count);

/* Introspection */
int config_key_exists(config_handle_t config, const char *key);
int config_get_keys(config_handle_t config, const char ***keys);
int config_key_count(config_handle_t config);

/* Export/Import */
int config_export_json(config_handle_t config, const char *path);
int config_export_env_script(config_handle_t config, const char *path);
int config_dump(config_handle_t config);

/* Watchers for Configuration Changes */
typedef void (*config_change_callback_t)(const char *key,
                                         config_type_t old_type,
                                         config_type_t new_type,
                                         void *context);

int config_watch_key(config_handle_t config, const char *key,
                    config_change_callback_t callback, void *context);
int config_unwatch_key(config_handle_t config, const char *key);

/* Global Configuration Instance */
extern config_handle_t g_config;

int config_init_global(const config_schema_t *schema, int schema_count);
void config_shutdown_global(void);

#endif /* GLYPH_CONFIG_H */
