/*
 * Rendering Backend System - Pluggable Rendering Infrastructure
 * 
 * Support for multiple rendering backends including CPU, SIMD,
 * GPU acceleration, and specialized rendering strategies.
 */

#ifndef GLYPH_RENDER_BACKENDS_H
#define GLYPH_RENDER_BACKENDS_H

#include <stdint.h>

/* Rendering Quality Levels */
typedef enum {
    RENDER_QUALITY_FAST,
    RENDER_QUALITY_NORMAL,
    RENDER_QUALITY_HIGH,
    RENDER_QUALITY_ULTRA
} render_quality_t;

/* Render Target Format */
typedef enum {
    RENDER_FORMAT_GRAYSCALE_8,
    RENDER_FORMAT_RGB_8,
    RENDER_FORMAT_RGBA_8,
    RENDER_FORMAT_GRAYSCALE_16,
    RENDER_FORMAT_FLOAT_32
} render_format_t;

/* Antialiasing Modes */
typedef enum {
    AA_NONE,
    AA_2X,
    AA_4X,
    AA_8X,
    AA_ADAPTIVE
} antialiasing_mode_t;

/* Rendering Backend Interface */
typedef struct {
    const char *name;
    const char *version;
    int capabilities;
    int max_width;
    int max_height;
    
    /* Initialize backend */
    int (*init)(void **context);
    
    /* Render glyph */
    int (*render_glyph)(void *context, uint32_t glyph_id,
                       void *target, int width, int height,
                       render_format_t format);
    
    /* Render text */
    int (*render_text)(void *context, const char *text,
                      void *target, int width, int height,
                      render_format_t format);
    
    /* Get glyph metrics */
    int (*get_metrics)(void *context, uint32_t glyph_id,
                      int *width, int *height, int *advance);
    
    /* Shutdown backend */
    int (*shutdown)(void *context);
} render_backend_t;

/* Backend Capabilities */
#define BACKEND_CAP_BASIC          (1 << 0)
#define BACKEND_CAP_ANTIALIASING   (1 << 1)
#define BACKEND_CAP_KERNING        (1 << 2)
#define BACKEND_CAP_LIGATURES      (1 << 3)
#define BACKEND_CAP_COLOR          (1 << 4)
#define BACKEND_CAP_EMOJI          (1 << 5)
#define BACKEND_CAP_FALLBACK_FONTS (1 << 6)
#define BACKEND_CAP_SDF            (1 << 7)
#define BACKEND_CAP_SUBPIXEL       (1 << 8)
#define BACKEND_CAP_HINTING        (1 << 9)

/* Rendering Options */
typedef struct {
    render_quality_t quality;
    antialiasing_mode_t antialiasing;
    render_format_t format;
    int enable_kerning;
    int enable_ligatures;
    int enable_hinting;
    float gamma;
    int dpi;
    float scale;
} render_options_t;

/* Backend Registry */
typedef struct render_registry_s *render_registry_t;

/* API */
render_registry_t render_registry_create(void);
void render_registry_destroy(render_registry_t registry);

int render_register_backend(render_registry_t registry,
                           const render_backend_t *backend);
int render_unregister_backend(render_registry_t registry,
                             const char *backend_name);

render_backend_t *render_get_backend(render_registry_t registry,
                                     const char *name);
const char **render_list_backends(render_registry_t registry, int *count);

/* Rendering with backend */
int render_glyph(render_registry_t registry, const char *backend_name,
                uint32_t glyph_id, void *target, int width, int height,
                const render_options_t *options);

int render_text(render_registry_t registry, const char *backend_name,
               const char *text, void *target, int width, int height,
               const render_options_t *options);

/* CPU-based rendering */
int render_backend_cpu_init(void);
render_backend_t *render_backend_cpu_get(void);

/* SIMD-optimized rendering */
int render_backend_simd_init(void);
render_backend_t *render_backend_simd_get(void);

/* Signed Distance Field rendering */
int render_backend_sdf_init(void);
render_backend_t *render_backend_sdf_get(void);

/* Subpixel rendering */
int render_backend_subpixel_init(void);
render_backend_t *render_backend_subpixel_get(void);

/* Global registry */
extern render_registry_t g_render_registry;

int render_init_global(void);
void render_shutdown_global(void);

#endif /* GLYPH_RENDER_BACKENDS_H */
