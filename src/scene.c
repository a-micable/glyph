#include "scene.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    SCENE_NODE_ROOT = 1,
    SCENE_NODE_GROUP = 2,
    SCENE_NODE_GLYPH = 3,
    SCENE_NODE_LAYER = 4
} SceneNodeType;

typedef struct SceneNode SceneNode;

struct SceneNode {
    uint32_t id;
    uint32_t parent_id;
    SceneNodeType type;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    char label[32];
    SceneNode **children;
    uint32_t child_count;
    uint32_t child_capacity;
};

typedef struct {
    uint32_t node_index;
    uint32_t revision;
} SceneExportItem;

typedef struct {
    SceneExportItem *items;
    uint32_t count;
    uint32_t capacity;
} SceneExportPlan;

typedef enum {
    SCENE_HANDLER_ALPHA = 1,
    SCENE_HANDLER_BETA = 2,
    SCENE_HANDLER_GAMMA = 3
} SceneHandlerType;

typedef enum {
    SCENE_HANDLER_FREE = 0,
    SCENE_HANDLER_ACTIVE = 1,
    SCENE_HANDLER_PENDING_FREE = 2
} SceneHandlerState;

typedef struct {
    uint32_t kind;
    char name[24];
    uint32_t weights[12];
    uint32_t salt;
} SceneAlphaHandler;

typedef struct {
    uint32_t kind;
    uint32_t flags;
    char tag[8];
} SceneBetaHandler;

typedef struct {
    uint16_t kind;
    uint16_t flags;
    char code[4];
} SceneGammaHandler;

typedef struct {
    uint32_t id;
    SceneHandlerType type;
    SceneHandlerState state;
    void *handler;
} SceneHandlerSlot;

typedef struct {
    uint32_t id;
    uint32_t slot_index;
    SceneHandlerType expected_type;
    int valid;
} SceneHandlerRoute;

typedef struct {
    uint32_t id;
    uint32_t slot_index;
    SceneHandlerType expected_type;
    uint32_t generation;
} SceneHandlerAuditItem;

typedef struct {
    SceneHandlerAuditItem *items;
    uint32_t count;
    uint32_t capacity;
} SceneHandlerAuditPlan;

typedef struct {
    SceneHandlerSlot *slots;
    uint32_t slot_count;
    uint32_t slot_capacity;
    SceneHandlerRoute *routes;
    uint32_t route_count;
    uint32_t route_capacity;
    SceneHandlerAuditPlan audit;
    uint32_t schema_generation;
} SceneHandlerRegistry;

struct GlyphScene {
    SceneNode **nodes;
    uint32_t node_count;
    uint32_t node_capacity;
    SceneNode *root;
    SceneExportPlan export_plan;
    SceneHandlerRegistry handlers;
    uint32_t viewport_width;
    uint32_t viewport_height;
    uint32_t revision;
};

typedef struct {
    const char *data;
    size_t size;
    size_t pos;
} SceneParser;

static void scene_export_plan_clear(SceneExportPlan *plan) {
    free(plan->items);
    memset(plan, 0, sizeof(*plan));
}

static void scene_node_free(SceneNode *node) {
    if (!node) {
        return;
    }
    free(node->children);
    free(node);
}

static void scene_handler_release(SceneHandlerSlot *slot) {
    if (!slot) {
        return;
    }
    free(slot->handler);
    memset(slot, 0, sizeof(*slot));
}

static void scene_handler_registry_free(SceneHandlerRegistry *registry) {
    if (!registry) {
        return;
    }
    for (uint32_t i = 0; i < registry->slot_count; i++) {
        scene_handler_release(&registry->slots[i]);
    }
    free(registry->slots);
    free(registry->routes);
    free(registry->audit.items);
    memset(registry, 0, sizeof(*registry));
}

static void glyph_scene_free(GlyphScene *scene) {
    if (!scene) {
        return;
    }
    for (uint32_t i = 0; i < scene->node_count; i++) {
        scene_node_free(scene->nodes[i]);
    }
    free(scene->nodes);
    scene_export_plan_clear(&scene->export_plan);
    scene_handler_registry_free(&scene->handlers);
    memset(scene, 0, sizeof(*scene));
}

static int scene_skip_ws(SceneParser *parser) {
    while (parser->pos < parser->size && isspace((unsigned char)parser->data[parser->pos])) {
        parser->pos++;
    }
    return parser->pos < parser->size;
}

static int scene_read_word(SceneParser *parser, char *out, size_t cap) {
    size_t start;
    size_t len;

    if (!scene_skip_ws(parser) || cap == 0) {
        return 0;
    }
    start = parser->pos;
    while (parser->pos < parser->size) {
        unsigned char c = (unsigned char)parser->data[parser->pos];
        if (isspace(c) || c == '"' || c == '{' || c == '}') {
            break;
        }
        parser->pos++;
    }
    len = parser->pos - start;
    if (len == 0 || len >= cap) {
        return 0;
    }
    memcpy(out, parser->data + start, len);
    out[len] = '\0';
    return 1;
}

static int scene_read_string(SceneParser *parser, char *out, size_t cap) {
    size_t len = 0;

    if (!scene_skip_ws(parser) || parser->data[parser->pos] != '"' || cap == 0) {
        return 0;
    }
    parser->pos++;
    while (parser->pos < parser->size && parser->data[parser->pos] != '"') {
        if (len + 1 >= cap || (unsigned char)parser->data[parser->pos] < 0x20) {
            return 0;
        }
        out[len++] = parser->data[parser->pos++];
    }
    if (parser->pos >= parser->size || parser->data[parser->pos] != '"') {
        return 0;
    }
    parser->pos++;
    out[len] = '\0';
    return 1;
}

static int scene_read_label(SceneParser *parser, char *out, size_t cap) {
    if (!scene_skip_ws(parser)) {
        return 0;
    }
    if (parser->data[parser->pos] == '"') {
        return scene_read_string(parser, out, cap);
    }
    return scene_read_word(parser, out, cap);
}

static int scene_read_u32(SceneParser *parser, uint32_t *value) {
    char word[32];
    char *end = NULL;
    unsigned long parsed;

    if (!scene_read_word(parser, word, sizeof(word))) {
        return 0;
    }
    parsed = strtoul(word, &end, 10);
    if (!end || *end || parsed > UINT32_MAX) {
        return 0;
    }
    *value = (uint32_t)parsed;
    return 1;
}

static int scene_read_i32(SceneParser *parser, int32_t *value) {
    char word[32];
    char *end = NULL;
    long parsed;

    if (!scene_read_word(parser, word, sizeof(word))) {
        return 0;
    }
    parsed = strtol(word, &end, 10);
    if (!end || *end || parsed < -32768 || parsed > 32767) {
        return 0;
    }
    *value = (int32_t)parsed;
    return 1;
}

static int scene_expect(SceneParser *parser, const char *expected) {
    char word[32];

    return scene_read_word(parser, word, sizeof(word)) && strcmp(word, expected) == 0;
}

static SceneNodeType scene_parse_type(const char *word) {
    if (strcmp(word, "root") == 0) {
        return SCENE_NODE_ROOT;
    }
    if (strcmp(word, "group") == 0) {
        return SCENE_NODE_GROUP;
    }
    if (strcmp(word, "glyph") == 0) {
        return SCENE_NODE_GLYPH;
    }
    if (strcmp(word, "layer") == 0) {
        return SCENE_NODE_LAYER;
    }
    return 0;
}

static SceneNode *scene_find_node(const GlyphScene *scene, uint32_t id) {
    for (uint32_t i = 0; i < scene->node_count; i++) {
        if (scene->nodes[i] && scene->nodes[i]->id == id) {
            return scene->nodes[i];
        }
    }
    return NULL;
}

static uint32_t scene_find_node_index(const GlyphScene *scene, const SceneNode *node) {
    for (uint32_t i = 0; i < scene->node_count; i++) {
        if (scene->nodes[i] == node) {
            return i;
        }
    }
    return UINT32_MAX;
}

static int scene_node_add_child(SceneNode *parent, SceneNode *child) {
    SceneNode **next;
    uint32_t next_capacity;

    if (parent->child_count == parent->child_capacity) {
        next_capacity = parent->child_capacity ? parent->child_capacity * 2 : 4;
        next = (SceneNode **)realloc(parent->children, (size_t)next_capacity * sizeof(*next));
        if (!next) {
            return 0;
        }
        parent->children = next;
        parent->child_capacity = next_capacity;
    }
    parent->children[parent->child_count++] = child;
    return 1;
}

static int scene_add_node(GlyphScene *scene, SceneNode *node) {
    SceneNode **next;
    uint32_t next_capacity;

    if (scene_find_node(scene, node->id)) {
        return 0;
    }
    if (node->type == SCENE_NODE_ROOT && scene->root) {
        return 0;
    }
    if (scene->node_count == scene->node_capacity) {
        next_capacity = scene->node_capacity ? scene->node_capacity * 2 : 8;
        next = (SceneNode **)realloc(scene->nodes, (size_t)next_capacity * sizeof(*next));
        if (!next) {
            return 0;
        }
        scene->nodes = next;
        scene->node_capacity = next_capacity;
    }
    scene->nodes[scene->node_count++] = node;
    if (node->type == SCENE_NODE_ROOT) {
        scene->root = node;
    }
    return 1;
}

static void scene_remove_node_ref(GlyphScene *scene, SceneNode *node) {
    for (uint32_t i = 0; i < scene->node_count; i++) {
        if (scene->nodes[i] == node) {
            memmove(&scene->nodes[i], &scene->nodes[i + 1], (size_t)(scene->node_count - i - 1) * sizeof(scene->nodes[i]));
            scene->node_count--;
            break;
        }
    }
    if (scene->root == node) {
        scene->root = NULL;
    }
}

static void scene_detach_child(SceneNode *parent, const SceneNode *child) {
    if (!parent || !child) {
        return;
    }
    for (uint32_t i = 0; i < parent->child_count; i++) {
        if (parent->children[i] == child) {
            memmove(&parent->children[i],
                    &parent->children[i + 1],
                    (size_t)(parent->child_count - i - 1) * sizeof(parent->children[i]));
            parent->child_count--;
            return;
        }
    }
}

static int scene_parse_node(SceneParser *parser, GlyphScene *scene) {
    SceneNode *node;
    SceneNode *parent;
    char type_name[16];

    node = (SceneNode *)calloc(1, sizeof(*node));
    if (!node) {
        return 0;
    }
    if (!scene_read_u32(parser, &node->id) ||
        !scene_read_word(parser, type_name, sizeof(type_name)) ||
        !scene_read_u32(parser, &node->parent_id) ||
        !scene_read_i32(parser, &node->x) ||
        !scene_read_i32(parser, &node->y) ||
        !scene_read_u32(parser, &node->width) ||
        !scene_read_u32(parser, &node->height) ||
        !scene_read_string(parser, node->label, sizeof(node->label))) {
        scene_node_free(node);
        return 0;
    }
    node->type = scene_parse_type(type_name);
    if (!node->type || node->width > 4096 || node->height > 4096) {
        scene_node_free(node);
        return 0;
    }
    if (node->type == SCENE_NODE_ROOT && node->parent_id != 0) {
        scene_node_free(node);
        return 0;
    }
    if (node->parent_id != 0) {
        parent = scene_find_node(scene, node->parent_id);
        if (!parent || parent->type == SCENE_NODE_GLYPH) {
            scene_node_free(node);
            return 0;
        }
    }
    if (!scene_add_node(scene, node)) {
        scene_node_free(node);
        return 0;
    }
    if (node->parent_id != 0 && !scene_node_add_child(parent, node)) {
        scene_remove_node_ref(scene, node);
        scene_node_free(node);
        return 0;
    }
    return 1;
}

static int scene_parse_header(SceneParser *parser, GlyphScene *scene) {
    uint32_t node_count;

    if (!scene_expect(parser, "scene") ||
        !scene_read_u32(parser, &scene->viewport_width) ||
        !scene_read_u32(parser, &scene->viewport_height) ||
        !scene_expect(parser, "nodes") ||
        !scene_read_u32(parser, &node_count) ||
        node_count == 0 ||
        node_count > 256) {
        return 0;
    }
    for (uint32_t i = 0; i < node_count; i++) {
        if (!scene_expect(parser, "node") || !scene_parse_node(parser, scene)) {
            return 0;
        }
    }
    return scene->root != NULL;
}

static int scene_export_plan_append(GlyphScene *scene, SceneExportPlan *plan, SceneNode *node) {
    SceneExportItem *next;
    uint32_t next_capacity;
    uint32_t node_index = scene_find_node_index(scene, node);

    if (node_index == UINT32_MAX) {
        return 0;
    }

    if (plan->count == plan->capacity) {
        next_capacity = plan->capacity ? plan->capacity * 2 : 8;
        next = (SceneExportItem *)realloc(plan->items, (size_t)next_capacity * sizeof(*next));
        if (!next) {
            return 0;
        }
        plan->items = next;
        plan->capacity = next_capacity;
    }
    plan->items[plan->count].node_index = node_index;
    plan->items[plan->count].revision = scene->revision;
    plan->count++;
    return 1;
}

static int scene_export_plan_visit(GlyphScene *scene, SceneExportPlan *plan, SceneNode *node) {
    if (!scene_export_plan_append(scene, plan, node)) {
        return 0;
    }
    for (uint32_t i = 0; i < node->child_count; i++) {
        if (!scene_export_plan_visit(scene, plan, node->children[i])) {
            return 0;
        }
    }
    return 1;
}

static int scene_export_refresh_plan(GlyphScene *scene) {
    SceneExportPlan next;

    memset(&next, 0, sizeof(next));
    if (!scene_export_plan_visit(scene, &next, scene->root)) {
        scene_export_plan_clear(&next);
        return 0;
    }
    scene_export_plan_clear(&scene->export_plan);
    scene->export_plan = next;
    return 1;
}

static int scene_move_node(GlyphScene *scene, uint32_t id, int32_t dx, int32_t dy) {
    SceneNode *node = scene_find_node(scene, id);

    if (!node) {
        return 0;
    }
    node->x += dx;
    node->y += dy;
    scene->revision++;
    return 1;
}

static int scene_delete_node(GlyphScene *scene, uint32_t id) {
    SceneNode *node = scene_find_node(scene, id);
    SceneNode *parent;
    uint8_t remove[256];
    uint32_t out = 0;
    int changed;

    if (!node || node == scene->root) {
        return 0;
    }
    memset(remove, 0, sizeof(remove));
    for (uint32_t i = 0; i < scene->node_count; i++) {
        if (scene->nodes[i] == node) {
            remove[i] = 1;
            break;
        }
    }
    do {
        changed = 0;
        for (uint32_t i = 0; i < scene->node_count; i++) {
            if (!remove[i] && scene->nodes[i]) {
                for (uint32_t j = 0; j < scene->node_count; j++) {
                    if (remove[j] && scene->nodes[j] && scene->nodes[i]->parent_id == scene->nodes[j]->id) {
                        remove[i] = 1;
                        changed = 1;
                        break;
                    }
                }
            }
        }
    } while (changed);

    parent = scene_find_node(scene, node->parent_id);
    scene_detach_child(parent, node);
    for (uint32_t i = 0; i < scene->node_count; i++) {
        if (remove[i]) {
            scene_node_free(scene->nodes[i]);
        } else {
            scene->nodes[out++] = scene->nodes[i];
        }
    }
    scene->node_count = out;
    scene->revision++;
    return 1;
}

static SceneHandlerType scene_parse_handler_type(const char *word) {
    if (strcmp(word, "alpha") == 0) {
        return SCENE_HANDLER_ALPHA;
    }
    if (strcmp(word, "beta") == 0) {
        return SCENE_HANDLER_BETA;
    }
    if (strcmp(word, "gamma") == 0) {
        return SCENE_HANDLER_GAMMA;
    }
    return 0;
}

static int scene_handler_registry_grow_slots(SceneHandlerRegistry *registry) {
    SceneHandlerSlot *next;
    uint32_t next_capacity = registry->slot_capacity ? registry->slot_capacity * 2 : 4;

    next = (SceneHandlerSlot *)realloc(registry->slots, (size_t)next_capacity * sizeof(*next));
    if (!next) {
        return 0;
    }
    memset(next + registry->slot_capacity, 0, (size_t)(next_capacity - registry->slot_capacity) * sizeof(*next));
    registry->slots = next;
    registry->slot_capacity = next_capacity;
    return 1;
}

static int scene_handler_registry_grow_routes(SceneHandlerRegistry *registry) {
    SceneHandlerRoute *next;
    uint32_t next_capacity = registry->route_capacity ? registry->route_capacity * 2 : 8;

    next = (SceneHandlerRoute *)realloc(registry->routes, (size_t)next_capacity * sizeof(*next));
    if (!next) {
        return 0;
    }
    memset(next + registry->route_capacity, 0, (size_t)(next_capacity - registry->route_capacity) * sizeof(*next));
    registry->routes = next;
    registry->route_capacity = next_capacity;
    return 1;
}

static int scene_handler_audit_grow(SceneHandlerAuditPlan *audit) {
    SceneHandlerAuditItem *next;
    uint32_t next_capacity = audit->capacity ? audit->capacity * 2 : 8;

    next = (SceneHandlerAuditItem *)realloc(audit->items, (size_t)next_capacity * sizeof(*next));
    if (!next) {
        return 0;
    }
    audit->items = next;
    audit->capacity = next_capacity;
    return 1;
}

static SceneHandlerSlot *scene_handler_find_active_slot(SceneHandlerRegistry *registry, uint32_t id, uint32_t *slot_index) {
    for (uint32_t i = 0; i < registry->slot_count; i++) {
        SceneHandlerSlot *slot = &registry->slots[i];
        if (slot->state == SCENE_HANDLER_ACTIVE && slot->id == id) {
            if (slot_index) {
                *slot_index = i;
            }
            return slot;
        }
    }
    return NULL;
}

static SceneHandlerRoute *scene_handler_find_route(SceneHandlerRegistry *registry, uint32_t id) {
    for (uint32_t i = 0; i < registry->route_count; i++) {
        if (registry->routes[i].valid && registry->routes[i].id == id) {
            return &registry->routes[i];
        }
    }
    return NULL;
}

static int scene_handler_cache_route(SceneHandlerRegistry *registry,
                                     uint32_t id,
                                     uint32_t slot_index,
                                     SceneHandlerType type) {
    SceneHandlerRoute *route = scene_handler_find_route(registry, id);

    if (!route) {
        if (registry->route_count == registry->route_capacity && !scene_handler_registry_grow_routes(registry)) {
            return 0;
        }
        route = &registry->routes[registry->route_count++];
    }
    route->id = id;
    route->slot_index = slot_index;
    route->expected_type = type;
    route->valid = 1;
    return 1;
}

static void scene_handler_invalidate_route(SceneHandlerRegistry *registry, uint32_t id) {
    SceneHandlerRoute *route = scene_handler_find_route(registry, id);

    if (route) {
        route->valid = 0;
    }
}

static void *scene_handler_alloc(SceneHandlerType type, const char *label, uint32_t a, uint32_t b) {
    SceneAlphaHandler *alpha;
    SceneBetaHandler *beta;
    SceneGammaHandler *gamma;

    if (type == SCENE_HANDLER_ALPHA) {
        alpha = (SceneAlphaHandler *)calloc(1, sizeof(*alpha));
        if (!alpha) {
            return NULL;
        }
        alpha->kind = 0xA1A1A1A1u;
        strncpy(alpha->name, label, sizeof(alpha->name) - 1);
        for (uint32_t i = 0; i < 12; i++) {
            alpha->weights[i] = a + b + i;
        }
        alpha->salt = a ^ (b << 8);
        return alpha;
    }
    if (type == SCENE_HANDLER_BETA) {
        beta = (SceneBetaHandler *)calloc(1, sizeof(*beta));
        if (!beta) {
            return NULL;
        }
        beta->kind = 0xB2B2B2B2u;
        beta->flags = a ^ b;
        strncpy(beta->tag, label, sizeof(beta->tag) - 1);
        return beta;
    }
    if (type == SCENE_HANDLER_GAMMA) {
        gamma = (SceneGammaHandler *)calloc(1, sizeof(*gamma));
        if (!gamma) {
            return NULL;
        }
        gamma->kind = 0xC3C3u;
        gamma->flags = (uint16_t)(a ^ b);
        strncpy(gamma->code, label, sizeof(gamma->code) - 1);
        return gamma;
    }
    return NULL;
}

static int scene_handler_claim_slot(SceneHandlerRegistry *registry,
                                    uint32_t id,
                                    SceneHandlerType type,
                                    void *handler) {
    SceneHandlerSlot *slot = NULL;

    if (scene_handler_find_active_slot(registry, id, NULL)) {
        return 0;
    }
    for (uint32_t i = 0; i < registry->slot_count; i++) {
        if (registry->slots[i].state == SCENE_HANDLER_FREE) {
            slot = &registry->slots[i];
            break;
        }
    }
    if (!slot) {
        if (registry->slot_count == registry->slot_capacity && !scene_handler_registry_grow_slots(registry)) {
            return 0;
        }
        slot = &registry->slots[registry->slot_count++];
    }
    slot->id = id;
    slot->type = type;
    slot->state = SCENE_HANDLER_ACTIVE;
    slot->handler = handler;
    return 1;
}

static int scene_handler_register(SceneHandlerRegistry *registry,
                                  uint32_t id,
                                  SceneHandlerType type,
                                  const char *label,
                                  uint32_t a,
                                  uint32_t b) {
    void *handler;

    if (!type || id == 0) {
        return 0;
    }
    handler = scene_handler_alloc(type, label, a, b);
    if (!handler) {
        return 0;
    }
    if (!scene_handler_claim_slot(registry, id, type, handler)) {
        free(handler);
        return 0;
    }
    return 1;
}

static int scene_handler_deregister(SceneHandlerRegistry *registry, uint32_t id) {
    SceneHandlerSlot *slot = scene_handler_find_active_slot(registry, id, NULL);

    if (!slot) {
        return 0;
    }
    scene_handler_invalidate_route(registry, id);
    slot->state = SCENE_HANDLER_PENDING_FREE;
    return 1;
}

static int scene_handler_schema_promote(SceneHandlerRegistry *registry) {
    for (uint32_t i = 0; i < registry->slot_count; i++) {
        if (registry->slots[i].state == SCENE_HANDLER_PENDING_FREE) {
            scene_handler_release(&registry->slots[i]);
        }
    }
    registry->schema_generation++;
    return 1;
}

static uint32_t scene_handler_alpha_apply(const SceneAlphaHandler *handler, uint32_t value) {
    uint32_t out = value ^ handler->kind;

    for (uint32_t i = 0; i < 12; i++) {
        out += handler->weights[i] ^ handler->salt;
    }
    return out + (uint32_t)strlen(handler->name);
}

static uint32_t scene_handler_beta_apply(const SceneBetaHandler *handler, uint32_t value) {
    return value + handler->kind + handler->flags + (uint32_t)strlen(handler->tag);
}

static uint32_t scene_handler_gamma_apply(const SceneGammaHandler *handler, uint32_t value) {
    return value + handler->kind + handler->flags + (uint32_t)handler->code[0];
}

static uint32_t scene_handler_apply_as(const SceneHandlerSlot *slot,
                                       SceneHandlerType expected_type,
                                       uint32_t value) {
    if (expected_type == SCENE_HANDLER_ALPHA) {
        return scene_handler_alpha_apply((const SceneAlphaHandler *)slot->handler, value);
    }
    if (expected_type == SCENE_HANDLER_BETA) {
        return scene_handler_beta_apply((const SceneBetaHandler *)slot->handler, value);
    }
    if (expected_type == SCENE_HANDLER_GAMMA) {
        return scene_handler_gamma_apply((const SceneGammaHandler *)slot->handler, value);
    }
    return value;
}

static int scene_handler_dispatch_cached(SceneHandlerRegistry *registry,
                                         SceneHandlerRoute *route,
                                         uint32_t value,
                                         uint32_t *out) {
    SceneHandlerSlot *slot;

    if (!route || route->slot_index >= registry->slot_count) {
        return 0;
    }
    slot = &registry->slots[route->slot_index];
    if (slot->state != SCENE_HANDLER_ACTIVE || !slot->handler) {
        return 0;
    }
    if (slot->id != route->id || slot->type != route->expected_type) {
        route->valid = 0;
        return 0;
    }
    *out = scene_handler_apply_as(slot, route->expected_type, value);
    return 1;
}

static int scene_handler_dispatch(SceneHandlerRegistry *registry, uint32_t id, uint32_t value, uint32_t *out) {
    SceneHandlerRoute *route = scene_handler_find_route(registry, id);
    SceneHandlerSlot *slot;
    uint32_t slot_index = 0;

    if (route) {
        return scene_handler_dispatch_cached(registry, route, value, out);
    }
    slot = scene_handler_find_active_slot(registry, id, &slot_index);
    if (!slot) {
        return 0;
    }
    if (!scene_handler_cache_route(registry, id, slot_index, slot->type)) {
        return 0;
    }
    *out = scene_handler_apply_as(slot, slot->type, value);
    return 1;
}

static int scene_handler_audit_capture(SceneHandlerRegistry *registry, uint32_t id) {
    SceneHandlerSlot *slot;
    SceneHandlerAuditItem *item;
    uint32_t slot_index = 0;

    slot = scene_handler_find_active_slot(registry, id, &slot_index);
    if (!slot) {
        return 0;
    }
    if (registry->audit.count == registry->audit.capacity && !scene_handler_audit_grow(&registry->audit)) {
        return 0;
    }
    item = &registry->audit.items[registry->audit.count++];
    item->id = id;
    item->slot_index = slot_index;
    item->expected_type = slot->type;
    item->generation = registry->schema_generation;
    return 1;
}

static int scene_handler_audit_replay_item(SceneHandlerRegistry *registry,
                                           const SceneHandlerAuditItem *item,
                                           uint32_t value,
                                           uint32_t *out) {
    SceneHandlerSlot *slot;

    if (item->slot_index >= registry->slot_count) {
        return 0;
    }
    slot = &registry->slots[item->slot_index];
    if (slot->state != SCENE_HANDLER_ACTIVE || !slot->handler) {
        return 0;
    }
    if (slot->id != item->id ||
        slot->type != item->expected_type ||
        item->generation != registry->schema_generation) {
        return 0;
    }
    *out ^= scene_handler_apply_as(slot, item->expected_type, value + item->id + item->generation);
    return 1;
}

static int scene_handler_audit_replay(SceneHandlerRegistry *registry, uint32_t value, uint32_t *out) {
    if (!registry->audit.count) {
        return 0;
    }
    for (uint32_t i = 0; i < registry->audit.count; i++) {
        if (!scene_handler_audit_replay_item(registry, &registry->audit.items[i], value + i, out)) {
            return 0;
        }
    }
    return 1;
}

static size_t scene_node_measure_text(const SceneNode *node) {
    size_t len = strlen(node->label);

    if (node->type == SCENE_NODE_GLYPH) {
        len += node->width + node->height;
    } else {
        len += node->child_count;
    }
    return len;
}

static int scene_node_write_record(const SceneNode *node, uint32_t depth, uint32_t *checksum) {
    size_t weight = scene_node_measure_text(node);

    *checksum ^= node->id * 2654435761u;
    *checksum += (uint32_t)(weight + depth + (uint32_t)node->x + (uint32_t)node->y);
    return 1;
}

static int scene_export_write_node(const SceneNode *node, uint32_t depth, uint32_t *checksum) {
    return scene_node_write_record(node, depth, checksum);
}

static SceneNode *scene_export_resolve_entry(const GlyphScene *scene, const SceneExportItem *item) {
    if (item->revision != scene->revision || item->node_index >= scene->node_count) {
        return NULL;
    }
    return scene->nodes[item->node_index];
}

static int scene_export_write_entry(const GlyphScene *scene,
                                    const SceneExportItem *item,
                                    uint32_t depth,
                                    uint32_t *checksum) {
    SceneNode *node = scene_export_resolve_entry(scene, item);

    if (!node) {
        return 0;
    }
    return scene_export_write_node(node, depth, checksum);
}

static int scene_export_visit_all(const GlyphScene *scene, const SceneExportPlan *plan, uint32_t *checksum) {
    for (uint32_t i = 0; i < plan->count; i++) {
        if (!scene_export_write_entry(scene, &plan->items[i], i, checksum)) {
            return 0;
        }
    }
    return 1;
}

static int scene_export_emit_cached(const GlyphScene *scene, uint32_t *checksum) {
    if (scene->export_plan.count == 0) {
        return 0;
    }
    return scene_export_visit_all(scene, &scene->export_plan, checksum);
}

static int scene_export_emit(const GlyphScene *scene) {
    uint32_t checksum = scene->revision ^ scene->viewport_width ^ scene->viewport_height;

    return scene_export_emit_cached(scene, &checksum) && checksum != 0;
}

static int scene_apply_snapshot(GlyphScene *scene) {
    return scene_export_refresh_plan(scene);
}

static int scene_apply_serialize(GlyphScene *scene) {
    if (scene->export_plan.count == 0 ||
        scene->export_plan.items[0].revision != scene->revision) {
        if (!scene_export_refresh_plan(scene)) {
            return 0;
        }
    }
    return scene_export_emit(scene);
}

static int scene_dispatch_command(SceneParser *parser, GlyphScene *scene, const char *op) {
    uint32_t id;
    uint32_t a;
    uint32_t b;
    uint32_t out;
    int32_t dx;
    int32_t dy;
    char type_name[16];
    char label[32];
    SceneHandlerType type;

    if (strcmp(op, "snapshot") == 0) {
        return scene_apply_snapshot(scene);
    }
    if (strcmp(op, "serialize") == 0) {
        return scene_apply_serialize(scene);
    }
    if (strcmp(op, "delete") == 0) {
        return scene_read_u32(parser, &id) && scene_delete_node(scene, id);
    }
    if (strcmp(op, "move") == 0) {
        return scene_read_u32(parser, &id) &&
               scene_read_i32(parser, &dx) &&
               scene_read_i32(parser, &dy) &&
               scene_move_node(scene, id, dx, dy);
    }
    if (strcmp(op, "register") == 0) {
        if (!scene_read_u32(parser, &id) ||
            !scene_read_word(parser, type_name, sizeof(type_name)) ||
            !scene_read_label(parser, label, sizeof(label)) ||
            !scene_read_u32(parser, &a) ||
            !scene_read_u32(parser, &b)) {
            return 0;
        }
        type = scene_parse_handler_type(type_name);
        return scene_handler_register(&scene->handlers, id, type, label, a, b);
    }
    if (strcmp(op, "dispatch") == 0) {
        if (!scene_read_u32(parser, &id) ||
            !scene_read_u32(parser, &a) ||
            !scene_handler_dispatch(&scene->handlers, id, a, &out)) {
            return 0;
        }
        scene->revision ^= out;
        return 1;
    }
    if (strcmp(op, "audit") == 0) {
        return scene_read_u32(parser, &id) && scene_handler_audit_capture(&scene->handlers, id);
    }
    if (strcmp(op, "replay") == 0) {
        if (!scene_read_u32(parser, &a) ||
            !scene_handler_audit_replay(&scene->handlers, a, &out)) {
            return 0;
        }
        scene->revision ^= out;
        return 1;
    }
    if (strcmp(op, "deregister") == 0) {
        return scene_read_u32(parser, &id) && scene_handler_deregister(&scene->handlers, id);
    }
    if (strcmp(op, "promote") == 0) {
        return scene_handler_schema_promote(&scene->handlers);
    }
    return 0;
}

static int scene_execute_program(SceneParser *parser, GlyphScene *scene) {
    uint32_t op_count;
    char op[32];

    if (!scene_expect(parser, "ops") || !scene_read_u32(parser, &op_count) || op_count > 128) {
        return 0;
    }
    for (uint32_t i = 0; i < op_count; i++) {
        if (!scene_expect(parser, "op") ||
            !scene_read_word(parser, op, sizeof(op)) ||
            !scene_dispatch_command(parser, scene, op)) {
            return 0;
        }
    }
    return 1;
}

static int scene_document_parse(SceneParser *parser, GlyphScene *scene) {
    memset(scene, 0, sizeof(*scene));
    if (!scene_parse_header(parser, scene)) {
        return 0;
    }
    return scene_execute_program(parser, scene);
}

GlyphSceneResult glyph_scene_run_document(const uint8_t *data, size_t size) {
    SceneParser parser;
    GlyphScene scene;
    int ok;

    if (!data || size == 0 || size > 65536) {
        return GLYPH_SCENE_RESULT_REJECT;
    }
    parser.data = (const char *)data;
    parser.size = size;
    parser.pos = 0;
    ok = scene_document_parse(&parser, &scene);
    glyph_scene_free(&scene);
    return ok ? GLYPH_SCENE_RESULT_OK : GLYPH_SCENE_RESULT_REJECT;
}
