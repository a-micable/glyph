#include "script.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    GLYPH_SCRIPT_TOKEN_EOF = 0,
    GLYPH_SCRIPT_TOKEN_EOL = 1,
    GLYPH_SCRIPT_TOKEN_IDENTIFIER = 2,
    GLYPH_SCRIPT_TOKEN_INTEGER = 3,
    GLYPH_SCRIPT_TOKEN_COMMA = 4,
    GLYPH_SCRIPT_TOKEN_SEMICOLON = 5,
    GLYPH_SCRIPT_TOKEN_EQUAL = 6,
    GLYPH_SCRIPT_TOKEN_PLUS_EQUAL = 7,
    /* TODO: document tooling utilities */
    GLYPH_SCRIPT_TOKEN_MINUS_EQUAL = 8
} GlyphScriptTokenKind;

typedef struct {
    GlyphScriptTokenKind kind;
    uint32_t line;
    uint32_t column;
    char text[64];
    uint32_t u32;
    int32_t i32;
} GlyphScriptToken;

typedef struct {
    const char *text;
    size_t pos;
    uint32_t line;
    uint32_t column;
    GlyphScriptToken current;
    GlyphScriptDiagnostics *diagnostics;
    int failed;
} GlyphScriptLexer;

static int glyph_script_is_eol(GlyphScriptTokenKind kind) {
    return kind == GLYPH_SCRIPT_TOKEN_EOL ||
           kind == GLYPH_SCRIPT_TOKEN_SEMICOLON ||
           kind == GLYPH_SCRIPT_TOKEN_EOF;
}

static int glyph_script_string_eq(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}

static void glyph_script_copy_message(char *dst, size_t dst_cap, const char *message) {
    size_t len;
    if (!dst || dst_cap == 0) {
        return;
    }
    if (!message) {
        message = "";
    }
    len = strlen(message);
    if (len >= dst_cap) {
        len = dst_cap - 1u;
    }
    memcpy(dst, message, len);
    dst[len] = '\0';
}

void glyph_script_diagnostics_init(GlyphScriptDiagnostics *diagnostics) {
    if (diagnostics) {
        memset(diagnostics, 0, sizeof(*diagnostics));
    }
}

void glyph_script_diagnostics_free(GlyphScriptDiagnostics *diagnostics) {
    if (!diagnostics) {
        return;
    }
    free(diagnostics->items);
    memset(diagnostics, 0, sizeof(*diagnostics));
}

static int glyph_script_diagnostics_reserve(GlyphScriptDiagnostics *diagnostics, uint32_t need) {
    GlyphScriptDiagnostic *items;
    uint32_t capacity;

    if (!diagnostics) {
        return 1;
    }
    if (need <= diagnostics->capacity) {
        return 1;
    }
    capacity = diagnostics->capacity ? diagnostics->capacity : 8u;
    while (capacity < need) {
        if (capacity > UINT32_MAX / 2u) {
            return 0;
        }
        capacity *= 2u;
    }
    items = (GlyphScriptDiagnostic *)realloc(diagnostics->items, (size_t)capacity * sizeof(*items));
    if (!items) {
        return 0;
    }
    diagnostics->items = items;
    diagnostics->capacity = capacity;
    return 1;
}

int glyph_script_diagnostics_add(GlyphScriptDiagnostics *diagnostics,
                                 GlyphScriptDiagnosticSeverity severity,
                                 GlyphScriptDiagnosticCode code,
                                 uint32_t line,
                                 uint32_t column,
                                 const char *message) {
    GlyphScriptDiagnostic *diag;

    if (!diagnostics) {
        return 1;
    }
    if (!glyph_script_diagnostics_reserve(diagnostics, diagnostics->count + 1u)) {
        return 0;
    }
    diag = &diagnostics->items[diagnostics->count++];
    memset(diag, 0, sizeof(*diag));
    diag->severity = severity;
    diag->code = code;
    diag->line = line;
    diag->column = column;
    glyph_script_copy_message(diag->message, sizeof(diag->message), message);
    return 1;
}

const char *glyph_script_diagnostic_severity_name(GlyphScriptDiagnosticSeverity severity) {
    switch (severity) {
    case GLYPH_SCRIPT_DIAG_INFO:
        return "info";
    case GLYPH_SCRIPT_DIAG_WARNING:
        return "warning";
    case GLYPH_SCRIPT_DIAG_ERROR:
        return "error";
    default:
        return "unknown";
    }
}

const char *glyph_script_diagnostic_code_name(GlyphScriptDiagnosticCode code) {
    switch (code) {
    case GLYPH_SCRIPT_DIAG_PARSE:
        return "parse";
    case GLYPH_SCRIPT_DIAG_ARGUMENT:
        return "argument";
    case GLYPH_SCRIPT_DIAG_MEMORY:
        return "memory";
    case GLYPH_SCRIPT_DIAG_VALIDATION:
        return "validation";
    case GLYPH_SCRIPT_DIAG_PLANNING:
        return "planning";
    case GLYPH_SCRIPT_DIAG_EXECUTION:
        return "execution";
    default:
        return "unknown";
    }
}

int glyph_script_diagnostics_write(FILE *fp, const GlyphScriptDiagnostics *diagnostics) {
    uint32_t i;

    if (!fp) {
        return 0;
    }
    if (!diagnostics || diagnostics->count == 0) {
        fprintf(fp, "glyph-script: no diagnostics\n");
        return ferror(fp) == 0;
    }
    for (i = 0; i < diagnostics->count; i++) {
        const GlyphScriptDiagnostic *diag = &diagnostics->items[i];
        fprintf(fp, "%u:%u: %s[%s]: %s\n",
                diag->line,
                diag->column,
                glyph_script_diagnostic_severity_name(diag->severity),
                glyph_script_diagnostic_code_name(diag->code),
                diag->message);
    }
    return ferror(fp) == 0;
}

void glyph_script_init(GlyphScript *script) {
    if (script) {
        memset(script, 0, sizeof(*script));
    }
}

static void glyph_script_command_free(GlyphScriptCommand *command) {
    if (!command) {
        return;
    }
    if (command->type == GLYPH_SCRIPT_COMMAND_SUBSET) {
        free(command->as.subset.ids);
        command->as.subset.ids = NULL;
        command->as.subset.count = 0;
    }
}

void glyph_script_free(GlyphScript *script) {
    uint32_t i;

    if (!script) {
        return;
    }
    for (i = 0; i < script->count; i++) {
        glyph_script_command_free(&script->commands[i]);
    }
    free(script->commands);
    memset(script, 0, sizeof(*script));
}

static int glyph_script_reserve_commands(GlyphScript *script, uint32_t need) {
    GlyphScriptCommand *commands;
    uint32_t capacity;

    if (!script) {
        return 0;
    }
    if (need <= script->capacity) {
        return 1;
    }
    capacity = script->capacity ? script->capacity : 8u;
    while (capacity < need) {
        if (capacity > UINT32_MAX / 2u) {
            return 0;
        }
        capacity *= 2u;
    }
    commands = (GlyphScriptCommand *)realloc(script->commands, (size_t)capacity * sizeof(*commands));
    if (!commands) {
        return 0;
    }
    script->commands = commands;
    script->capacity = capacity;
    return 1;
}

static int glyph_script_append_command(GlyphScript *script, const GlyphScriptCommand *command) {
    if (!glyph_script_reserve_commands(script, script->count + 1u)) {
        return 0;
    }
    script->commands[script->count++] = *command;
    return 1;
}

static int glyph_script_append_u32(uint32_t **values, uint32_t *count, uint32_t *capacity, uint32_t value) {
    uint32_t *next;
    uint32_t next_capacity;

    if (!values || !count || !capacity) {
        return 0;
    }
    if (*count >= *capacity) {
        next_capacity = *capacity ? *capacity * 2u : 8u;
        if (next_capacity < *capacity || next_capacity > UINT32_MAX / sizeof(**values)) {
            return 0;
        }
        next = (uint32_t *)realloc(*values, (size_t)next_capacity * sizeof(**values));
        if (!next) {
            return 0;
        }
        *values = next;
        *capacity = next_capacity;
    }
    (*values)[(*count)++] = value;
    return 1;
}

static int glyph_script_append_remap(GlyphScriptExecutionPlan *plan, uint32_t old_id, uint32_t new_id) {
    GlyphIdRemap *next;

    if (!plan || plan->remap_count == UINT32_MAX) {
        return 0;
    }
    next = (GlyphIdRemap *)realloc(plan->remaps, (size_t)(plan->remap_count + 1u) * sizeof(*next));
    if (!next) {
        return 0;
    }
    plan->remaps = next;
    plan->remaps[plan->remap_count].old_id = old_id;
    plan->remaps[plan->remap_count].new_id = new_id;
    plan->remap_count++;
    return 1;
}

static int glyph_script_append_subset_id(GlyphScriptExecutionPlan *plan, uint32_t id) {
    uint32_t *next;

    if (!plan || plan->subset_id_count == UINT32_MAX) {
        return 0;
    }
    next = (uint32_t *)realloc(plan->subset_ids, (size_t)(plan->subset_id_count + 1u) * sizeof(*next));
    if (!next) {
        return 0;
    }
    plan->subset_ids = next;
    plan->subset_ids[plan->subset_id_count++] = id;
    return 1;
}

static int glyph_script_append_advance(GlyphScriptExecutionPlan *plan,
                                       uint32_t glyph_id,
                                       GlyphEditAdvanceMode mode,
                                       int16_t value) {
    GlyphAdvanceAdjustment *next;

    if (!plan || plan->advance_count == UINT32_MAX) {
        return 0;
    }
    next = (GlyphAdvanceAdjustment *)realloc(plan->advances,
                                             (size_t)(plan->advance_count + 1u) * sizeof(*next));
    if (!next) {
        return 0;
    }
    plan->advances = next;
    plan->advances[plan->advance_count].glyph_id = glyph_id;
    plan->advances[plan->advance_count].mode = mode;
    plan->advances[plan->advance_count].value = value;
    plan->advance_count++;
    return 1;
}

static int glyph_script_append_translation(GlyphScriptExecutionPlan *plan,
                                           uint32_t glyph_id,
                                           int16_t dx,
                                           int16_t dy) {
    GlyphBoundsTranslation *next;

    if (!plan || plan->translate_count == UINT32_MAX) {
        return 0;
    }
    next = (GlyphBoundsTranslation *)realloc(plan->translations,
                                             (size_t)(plan->translate_count + 1u) * sizeof(*next));
    if (!next) {
        return 0;
    }
    plan->translations = next;
    plan->translations[plan->translate_count].glyph_id = glyph_id;
    plan->translations[plan->translate_count].dx = dx;
    plan->translations[plan->translate_count].dy = dy;
    plan->translate_count++;
    return 1;
}

static char glyph_script_lexer_peek(const GlyphScriptLexer *lexer) {
    if (!lexer || !lexer->text) {
        return '\0';
    }
    return lexer->text[lexer->pos];
}

static char glyph_script_lexer_peek_next(const GlyphScriptLexer *lexer) {
    if (!lexer || !lexer->text || lexer->text[lexer->pos] == '\0') {
        return '\0';
    }
    return lexer->text[lexer->pos + 1u];
}

static char glyph_script_lexer_take(GlyphScriptLexer *lexer) {
    char c;

    c = glyph_script_lexer_peek(lexer);
    if (c == '\0') {
        return c;
    }
    lexer->pos++;
    if (c == '\n') {
        lexer->line++;
        lexer->column = 1u;
    } else {
        lexer->column++;
    }
    return c;
}

static void glyph_script_lexer_init(GlyphScriptLexer *lexer,
                                    const char *text,
                                    GlyphScriptDiagnostics *diagnostics) {
    memset(lexer, 0, sizeof(*lexer));
    lexer->text = text ? text : "";
    lexer->line = 1u;
    lexer->column = 1u;
    lexer->diagnostics = diagnostics;
}

static void glyph_script_lexer_error(GlyphScriptLexer *lexer, const char *message) {
    if (!lexer) {
        return;
    }
    lexer->failed = 1;
    (void)glyph_script_diagnostics_add(lexer->diagnostics,
                                       GLYPH_SCRIPT_DIAG_ERROR,
                                       GLYPH_SCRIPT_DIAG_PARSE,
                                       lexer->line,
                                       lexer->column,
                                       message);
}

static void glyph_script_lexer_skip_space(GlyphScriptLexer *lexer) {
    int again = 1;

    while (again) {
        char c;
        again = 0;
        while ((c = glyph_script_lexer_peek(lexer)) != '\0' &&
               c != '\n' &&
               isspace((unsigned char)c)) {
            (void)glyph_script_lexer_take(lexer);
        }
        if (glyph_script_lexer_peek(lexer) == '#') {
            while (glyph_script_lexer_peek(lexer) != '\0' &&
                   glyph_script_lexer_peek(lexer) != '\n') {
                (void)glyph_script_lexer_take(lexer);
            }
            again = 1;
        } else if (glyph_script_lexer_peek(lexer) == '/' &&
                   glyph_script_lexer_peek_next(lexer) == '/') {
            while (glyph_script_lexer_peek(lexer) != '\0' &&
                   glyph_script_lexer_peek(lexer) != '\n') {
                (void)glyph_script_lexer_take(lexer);
            }
            again = 1;
        }
    }
}

static int glyph_script_parse_number_text(const char *text, uint32_t *u32, int32_t *i32) {
    char *end = NULL;
    long long signed_value;
    unsigned long long unsigned_value;

    if (!text || !u32 || !i32) {
        return 0;
    }
    if (text[0] == '-') {
        errno = 0;
        signed_value = strtoll(text, &end, 0);
        if (errno == 0 && end && *end == '\0' &&
            signed_value >= INT32_MIN && signed_value <= INT32_MAX) {
            *i32 = (int32_t)signed_value;
            *u32 = 0;
            return 1;
        }
        return 0;
    }

    errno = 0;
    signed_value = strtoll(text, &end, 0);
    if (errno == 0 && end && *end == '\0' &&
        signed_value >= 0 && signed_value <= INT32_MAX) {
        *i32 = (int32_t)signed_value;
    } else {
        *i32 = INT32_MAX;
    }

    if (text[0] == '+') {
        text++;
    }
    errno = 0;
    end = NULL;
    unsigned_value = strtoull(text, &end, 0);
    if (errno == 0 && end && *end == '\0' && unsigned_value <= UINT32_MAX) {
        *u32 = (uint32_t)unsigned_value;
        return 1;
    }
    return 0;
}

static void glyph_script_lexer_next(GlyphScriptLexer *lexer) {
    GlyphScriptToken token;
    char c;
    size_t n = 0;

    memset(&token, 0, sizeof(token));
    glyph_script_lexer_skip_space(lexer);
    token.line = lexer->line;
    token.column = lexer->column;
    c = glyph_script_lexer_peek(lexer);
    if (c == '\0') {
        token.kind = GLYPH_SCRIPT_TOKEN_EOF;
        lexer->current = token;
        return;
    }
    if (c == '\n') {
        (void)glyph_script_lexer_take(lexer);
        token.kind = GLYPH_SCRIPT_TOKEN_EOL;
        lexer->current = token;
        return;
    }
    if (c == ',') {
        (void)glyph_script_lexer_take(lexer);
        token.kind = GLYPH_SCRIPT_TOKEN_COMMA;
        lexer->current = token;
        return;
    }
    if (c == ';') {
        (void)glyph_script_lexer_take(lexer);
        token.kind = GLYPH_SCRIPT_TOKEN_SEMICOLON;
        lexer->current = token;
        return;
    }
    if (c == '=') {
        (void)glyph_script_lexer_take(lexer);
        token.kind = GLYPH_SCRIPT_TOKEN_EQUAL;
        lexer->current = token;
        return;
    }
    if (c == '+' && glyph_script_lexer_peek_next(lexer) == '=') {
        (void)glyph_script_lexer_take(lexer);
        (void)glyph_script_lexer_take(lexer);
        token.kind = GLYPH_SCRIPT_TOKEN_PLUS_EQUAL;
        lexer->current = token;
        return;
    }
    if (c == '-' && glyph_script_lexer_peek_next(lexer) == '=') {
        (void)glyph_script_lexer_take(lexer);
        (void)glyph_script_lexer_take(lexer);
        token.kind = GLYPH_SCRIPT_TOKEN_MINUS_EQUAL;
        lexer->current = token;
        return;
    }
    if (isalpha((unsigned char)c) || c == '_') {
        while ((c = glyph_script_lexer_peek(lexer)) != '\0' &&
               (isalnum((unsigned char)c) || c == '_' || c == '-')) {
            if (n + 1u < sizeof(token.text)) {
                token.text[n++] = c;
            }
            (void)glyph_script_lexer_take(lexer);
        }
        token.text[n] = '\0';
        token.kind = GLYPH_SCRIPT_TOKEN_IDENTIFIER;
        lexer->current = token;
        return;
    }
    if (isdigit((unsigned char)c) ||
        ((c == '-' || c == '+') && isdigit((unsigned char)glyph_script_lexer_peek_next(lexer)))) {
        if (c == '-' || c == '+') {
            token.text[n++] = c;
            (void)glyph_script_lexer_take(lexer);
        }
        while ((c = glyph_script_lexer_peek(lexer)) != '\0' &&
               (isalnum((unsigned char)c) || c == 'x' || c == 'X')) {
            if (n + 1u < sizeof(token.text)) {
                token.text[n++] = c;
            }
            (void)glyph_script_lexer_take(lexer);
        }
        token.text[n] = '\0';
        token.kind = GLYPH_SCRIPT_TOKEN_INTEGER;
        if (!glyph_script_parse_number_text(token.text, &token.u32, &token.i32)) {
            glyph_script_lexer_error(lexer, "integer literal is out of range or malformed");
        }
        lexer->current = token;
        return;
    }
    (void)glyph_script_lexer_take(lexer);
    glyph_script_lexer_error(lexer, "unexpected character");
    token.kind = GLYPH_SCRIPT_TOKEN_EOL;
    lexer->current = token;
}

static void glyph_script_parser_diag(GlyphScriptLexer *lexer,
                                     GlyphScriptDiagnosticCode code,
                                     const char *message) {
    if (!lexer) {
        return;
    }
    lexer->failed = 1;
    (void)glyph_script_diagnostics_add(lexer->diagnostics,
                                       GLYPH_SCRIPT_DIAG_ERROR,
                                       code,
                                       lexer->current.line,
                                       lexer->current.column,
                                       message);
}

static void glyph_script_skip_statement(GlyphScriptLexer *lexer) {
    while (!glyph_script_is_eol(lexer->current.kind)) {
        glyph_script_lexer_next(lexer);
    }
    if (lexer->current.kind != GLYPH_SCRIPT_TOKEN_EOF) {
        glyph_script_lexer_next(lexer);
    }
}

static int glyph_script_take_u32(GlyphScriptLexer *lexer, uint32_t *out, const char *what) {
    if (lexer->current.kind != GLYPH_SCRIPT_TOKEN_INTEGER || lexer->current.text[0] == '-') {
        glyph_script_parser_diag(lexer, GLYPH_SCRIPT_DIAG_ARGUMENT, what);
        return 0;
    }
    if (out) {
        *out = lexer->current.u32;
    }
    glyph_script_lexer_next(lexer);
    return 1;
}

static int glyph_script_take_i16(GlyphScriptLexer *lexer, int16_t *out, const char *what) {
    int32_t value;

    if (lexer->current.kind != GLYPH_SCRIPT_TOKEN_INTEGER) {
        glyph_script_parser_diag(lexer, GLYPH_SCRIPT_DIAG_ARGUMENT, what);
        return 0;
    }
    value = lexer->current.i32;
    if (value < INT16_MIN || value > INT16_MAX) {
        glyph_script_parser_diag(lexer, GLYPH_SCRIPT_DIAG_ARGUMENT, "signed 16-bit value is out of range");
        return 0;
    }
    if (out) {
        *out = (int16_t)value;
    }
    glyph_script_lexer_next(lexer);
    return 1;
}

static int glyph_script_expect_end(GlyphScriptLexer *lexer) {
    if (!glyph_script_is_eol(lexer->current.kind)) {
        glyph_script_parser_diag(lexer, GLYPH_SCRIPT_DIAG_PARSE, "unexpected trailing tokens");
        glyph_script_skip_statement(lexer);
        return 0;
    }
    if (lexer->current.kind != GLYPH_SCRIPT_TOKEN_EOF) {
        glyph_script_lexer_next(lexer);
    }
    return 1;
}

static int glyph_script_parse_remap(GlyphScriptLexer *lexer, GlyphScript *script, uint32_t line, uint32_t column) {
    GlyphScriptCommand command;

    memset(&command, 0, sizeof(command));
    command.type = GLYPH_SCRIPT_COMMAND_REMAP;
    command.line = line;
    command.column = column;
    if (!glyph_script_take_u32(lexer, &command.as.remap.old_id, "expected remap source glyph id") ||
        !glyph_script_take_u32(lexer, &command.as.remap.new_id, "expected remap target glyph id")) {
        glyph_script_skip_statement(lexer);
        return 0;
    }
    if (!glyph_script_expect_end(lexer)) {
        return 0;
    }
    if (!glyph_script_append_command(script, &command)) {
        glyph_script_parser_diag(lexer, GLYPH_SCRIPT_DIAG_MEMORY, "failed to append remap command");
        return 0;
    }
    return 1;
}

static int glyph_script_parse_subset(GlyphScriptLexer *lexer, GlyphScript *script, uint32_t line, uint32_t column) {
    GlyphScriptCommand command;
    uint32_t capacity = 0;

    memset(&command, 0, sizeof(command));
    command.type = GLYPH_SCRIPT_COMMAND_SUBSET;
    command.line = line;
    command.column = column;
    while (!glyph_script_is_eol(lexer->current.kind)) {
        uint32_t id;
        if (lexer->current.kind == GLYPH_SCRIPT_TOKEN_COMMA) {
            glyph_script_lexer_next(lexer);
            continue;
        }
        if (!glyph_script_take_u32(lexer, &id, "expected subset glyph id")) {
            glyph_script_skip_statement(lexer);
            glyph_script_command_free(&command);
            return 0;
        }
        if (!glyph_script_append_u32(&command.as.subset.ids, &command.as.subset.count, &capacity, id)) {
            glyph_script_parser_diag(lexer, GLYPH_SCRIPT_DIAG_MEMORY, "failed to append subset id");
            glyph_script_command_free(&command);
            glyph_script_skip_statement(lexer);
            return 0;
        }
    }
    if (command.as.subset.count == 0) {
        glyph_script_parser_diag(lexer, GLYPH_SCRIPT_DIAG_ARGUMENT, "subset requires at least one glyph id");
        glyph_script_skip_statement(lexer);
        glyph_script_command_free(&command);
        return 0;
    }
    if (!glyph_script_expect_end(lexer)) {
        glyph_script_command_free(&command);
        return 0;
    }
    if (!glyph_script_append_command(script, &command)) {
        glyph_script_parser_diag(lexer, GLYPH_SCRIPT_DIAG_MEMORY, "failed to append subset command");
        glyph_script_command_free(&command);
        return 0;
    }
    return 1;
}

static int glyph_script_parse_advance(GlyphScriptLexer *lexer, GlyphScript *script, uint32_t line, uint32_t column) {
    GlyphScriptCommand command;
    int sign = 1;

    memset(&command, 0, sizeof(command));
    command.type = GLYPH_SCRIPT_COMMAND_ADVANCE;
    command.line = line;
    command.column = column;
    command.as.advance.mode = GLYPH_EDIT_ADVANCE_SET;
    if (!glyph_script_take_u32(lexer, &command.as.advance.glyph_id, "expected advance glyph id")) {
        glyph_script_skip_statement(lexer);
        return 0;
    }
    if (lexer->current.kind == GLYPH_SCRIPT_TOKEN_IDENTIFIER) {
        if (glyph_script_string_eq(lexer->current.text, "set")) {
            command.as.advance.mode = GLYPH_EDIT_ADVANCE_SET;
            glyph_script_lexer_next(lexer);
        } else if (glyph_script_string_eq(lexer->current.text, "add")) {
            command.as.advance.mode = GLYPH_EDIT_ADVANCE_ADD;
            glyph_script_lexer_next(lexer);
        } else {
            glyph_script_parser_diag(lexer, GLYPH_SCRIPT_DIAG_ARGUMENT, "expected set or add");
            glyph_script_skip_statement(lexer);
            return 0;
        }
    } else if (lexer->current.kind == GLYPH_SCRIPT_TOKEN_EQUAL) {
        command.as.advance.mode = GLYPH_EDIT_ADVANCE_SET;
        glyph_script_lexer_next(lexer);
    } else if (lexer->current.kind == GLYPH_SCRIPT_TOKEN_PLUS_EQUAL) {
        command.as.advance.mode = GLYPH_EDIT_ADVANCE_ADD;
        glyph_script_lexer_next(lexer);
    } else if (lexer->current.kind == GLYPH_SCRIPT_TOKEN_MINUS_EQUAL) {
        command.as.advance.mode = GLYPH_EDIT_ADVANCE_ADD;
        sign = -1;
        glyph_script_lexer_next(lexer);
    }
    if (!glyph_script_take_i16(lexer, &command.as.advance.value, "expected advance value")) {
        glyph_script_skip_statement(lexer);
        return 0;
    }
    if (sign < 0) {
        if (command.as.advance.value == INT16_MIN) {
            glyph_script_parser_diag(lexer, GLYPH_SCRIPT_DIAG_ARGUMENT, "advance -= value is out of range");
            glyph_script_skip_statement(lexer);
            return 0;
        }
        command.as.advance.value = (int16_t)-command.as.advance.value;
    }
    if (!glyph_script_expect_end(lexer)) {
        return 0;
    }
    if (!glyph_script_append_command(script, &command)) {
        glyph_script_parser_diag(lexer, GLYPH_SCRIPT_DIAG_MEMORY, "failed to append advance command");
        return 0;
    }
    return 1;
}

static int glyph_script_parse_translate(GlyphScriptLexer *lexer, GlyphScript *script, uint32_t line, uint32_t column) {
    GlyphScriptCommand command;

    memset(&command, 0, sizeof(command));
    command.type = GLYPH_SCRIPT_COMMAND_TRANSLATE;
    command.line = line;
    command.column = column;
    if (!glyph_script_take_u32(lexer, &command.as.translate.glyph_id, "expected translate glyph id") ||
        !glyph_script_take_i16(lexer, &command.as.translate.dx, "expected translate dx") ||
        !glyph_script_take_i16(lexer, &command.as.translate.dy, "expected translate dy")) {
        glyph_script_skip_statement(lexer);
        return 0;
    }
    if (!glyph_script_expect_end(lexer)) {
        return 0;
    }
    if (!glyph_script_append_command(script, &command)) {
        glyph_script_parser_diag(lexer, GLYPH_SCRIPT_DIAG_MEMORY, "failed to append translate command");
        return 0;
    }
    return 1;
}

static int glyph_script_parse_validate(GlyphScriptLexer *lexer, GlyphScript *script, uint32_t line, uint32_t column) {
    GlyphScriptCommand command;

    memset(&command, 0, sizeof(command));
    command.type = GLYPH_SCRIPT_COMMAND_VALIDATE;
    command.line = line;
    command.column = column;
    command.as.validate.require_unique_glyph_ids = 1;
    command.as.validate.require_subset_ids_exist = 1;
    while (!glyph_script_is_eol(lexer->current.kind)) {
        if (lexer->current.kind != GLYPH_SCRIPT_TOKEN_IDENTIFIER) {
            glyph_script_parser_diag(lexer, GLYPH_SCRIPT_DIAG_ARGUMENT, "expected validate option");
            glyph_script_skip_statement(lexer);
            return 0;
        }
        if (glyph_script_string_eq(lexer->current.text, "unique")) {
            command.as.validate.require_unique_glyph_ids = 1;
        } else if (glyph_script_string_eq(lexer->current.text, "allow-duplicate-ids")) {
            command.as.validate.require_unique_glyph_ids = 0;
        } else if (glyph_script_string_eq(lexer->current.text, "subset-exists")) {
            command.as.validate.require_subset_ids_exist = 1;
        } else if (glyph_script_string_eq(lexer->current.text, "allow-missing-subset")) {
            command.as.validate.require_subset_ids_exist = 0;
        } else {
            glyph_script_parser_diag(lexer, GLYPH_SCRIPT_DIAG_ARGUMENT, "unknown validate option");
            glyph_script_skip_statement(lexer);
            return 0;
        }
        glyph_script_lexer_next(lexer);
    }
    if (!glyph_script_expect_end(lexer)) {
        return 0;
    }
    if (!glyph_script_append_command(script, &command)) {
        glyph_script_parser_diag(lexer, GLYPH_SCRIPT_DIAG_MEMORY, "failed to append validate command");
        return 0;
    }
    return 1;
}

static int glyph_script_parse_report(GlyphScriptLexer *lexer, GlyphScript *script, uint32_t line, uint32_t column) {
    GlyphScriptCommand command;
    int saw_option = 0;

    memset(&command, 0, sizeof(command));
    command.type = GLYPH_SCRIPT_COMMAND_REPORT;
    command.line = line;
    command.column = column;
    command.as.report.include_summary = 1;
    command.as.report.include_validation = 1;
    command.as.report.include_plan = 0;
    while (!glyph_script_is_eol(lexer->current.kind)) {
        if (lexer->current.kind == GLYPH_SCRIPT_TOKEN_COMMA) {
            glyph_script_lexer_next(lexer);
            continue;
        }
        if (lexer->current.kind != GLYPH_SCRIPT_TOKEN_IDENTIFIER) {
            glyph_script_parser_diag(lexer, GLYPH_SCRIPT_DIAG_ARGUMENT, "expected report option");
            glyph_script_skip_statement(lexer);
            return 0;
        }
        if (!saw_option) {
            command.as.report.include_summary = 0;
            command.as.report.include_validation = 0;
            command.as.report.include_plan = 0;
            saw_option = 1;
        }
        if (glyph_script_string_eq(lexer->current.text, "all")) {
            command.as.report.include_summary = 1;
            command.as.report.include_validation = 1;
            command.as.report.include_plan = 1;
        } else if (glyph_script_string_eq(lexer->current.text, "summary")) {
            command.as.report.include_summary = 1;
        } else if (glyph_script_string_eq(lexer->current.text, "validation")) {
            command.as.report.include_validation = 1;
        } else if (glyph_script_string_eq(lexer->current.text, "plan")) {
            command.as.report.include_plan = 1;
        } else {
            glyph_script_parser_diag(lexer, GLYPH_SCRIPT_DIAG_ARGUMENT, "unknown report option");
            glyph_script_skip_statement(lexer);
            return 0;
        }
        glyph_script_lexer_next(lexer);
    }
    if (!glyph_script_expect_end(lexer)) {
        return 0;
    }
    if (!glyph_script_append_command(script, &command)) {
        glyph_script_parser_diag(lexer, GLYPH_SCRIPT_DIAG_MEMORY, "failed to append report command");
        return 0;
    }
    return 1;
}

int glyph_script_parse(const char *text, GlyphScript *script, GlyphScriptDiagnostics *diagnostics) {
    GlyphScriptLexer lexer;

    if (!script) {
        return 0;
    }
    glyph_script_free(script);
    glyph_script_init(script);
    glyph_script_lexer_init(&lexer, text, diagnostics);
    glyph_script_lexer_next(&lexer);
    while (lexer.current.kind != GLYPH_SCRIPT_TOKEN_EOF) {
        uint32_t line;
        uint32_t column;
        char command[64];

        if (lexer.current.kind == GLYPH_SCRIPT_TOKEN_EOL ||
            lexer.current.kind == GLYPH_SCRIPT_TOKEN_SEMICOLON) {
            glyph_script_lexer_next(&lexer);
            continue;
        }
        if (lexer.current.kind != GLYPH_SCRIPT_TOKEN_IDENTIFIER) {
            glyph_script_parser_diag(&lexer, GLYPH_SCRIPT_DIAG_PARSE, "expected command");
            glyph_script_skip_statement(&lexer);
            continue;
        }
        line = lexer.current.line;
        column = lexer.current.column;
        glyph_script_copy_message(command, sizeof(command), lexer.current.text);
        glyph_script_lexer_next(&lexer);

        if (glyph_script_string_eq(command, "remap")) {
            (void)glyph_script_parse_remap(&lexer, script, line, column);
        } else if (glyph_script_string_eq(command, "subset")) {
            (void)glyph_script_parse_subset(&lexer, script, line, column);
        } else if (glyph_script_string_eq(command, "advance")) {
            (void)glyph_script_parse_advance(&lexer, script, line, column);
        } else if (glyph_script_string_eq(command, "translate")) {
            (void)glyph_script_parse_translate(&lexer, script, line, column);
        } else if (glyph_script_string_eq(command, "validate")) {
            (void)glyph_script_parse_validate(&lexer, script, line, column);
        } else if (glyph_script_string_eq(command, "report")) {
            (void)glyph_script_parse_report(&lexer, script, line, column);
        } else {
            glyph_script_parser_diag(&lexer, GLYPH_SCRIPT_DIAG_PARSE, "unknown command");
            glyph_script_skip_statement(&lexer);
        }
    }
    if (lexer.failed) {
        glyph_script_free(script);
        glyph_script_init(script);
        return 0;
    }
    return 1;
}

static const char *glyph_script_advance_mode_name(GlyphEditAdvanceMode mode) {
    return mode == GLYPH_EDIT_ADVANCE_ADD ? "add" : "set";
}

int glyph_script_serialize(FILE *fp, const GlyphScript *script) {
    uint32_t i;

    if (!fp || !script || (script->count && !script->commands)) {
        return 0;
    }
    for (i = 0; i < script->count; i++) {
        const GlyphScriptCommand *command = &script->commands[i];
        uint32_t j;
        switch (command->type) {
        case GLYPH_SCRIPT_COMMAND_REMAP:
            fprintf(fp, "remap %u %u\n", command->as.remap.old_id, command->as.remap.new_id);
            break;
        case GLYPH_SCRIPT_COMMAND_SUBSET:
            fprintf(fp, "subset");
            for (j = 0; j < command->as.subset.count; j++) {
                fprintf(fp, " %u", command->as.subset.ids[j]);
            }
            fprintf(fp, "\n");
            break;
        case GLYPH_SCRIPT_COMMAND_ADVANCE:
            fprintf(fp, "advance %u %s %d\n",
                    command->as.advance.glyph_id,
                    glyph_script_advance_mode_name(command->as.advance.mode),
                    (int)command->as.advance.value);
            break;
        case GLYPH_SCRIPT_COMMAND_TRANSLATE:
            fprintf(fp, "translate %u %d %d\n",
                    command->as.translate.glyph_id,
                    (int)command->as.translate.dx,
                    (int)command->as.translate.dy);
            break;
        case GLYPH_SCRIPT_COMMAND_VALIDATE:
            fprintf(fp, "validate %s %s\n",
                    command->as.validate.require_unique_glyph_ids ? "unique" : "allow-duplicate-ids",
                    command->as.validate.require_subset_ids_exist ? "subset-exists" : "allow-missing-subset");
            break;
        case GLYPH_SCRIPT_COMMAND_REPORT:
            fprintf(fp, "report");
            if (command->as.report.include_summary) {
                fprintf(fp, " summary");
            }
            if (command->as.report.include_validation) {
                fprintf(fp, " validation");
            }
            if (command->as.report.include_plan) {
                fprintf(fp, " plan");
            }
            fprintf(fp, "\n");
            break;
        default:
            return 0;
        }
    }
    return ferror(fp) == 0;
}

void glyph_script_execution_plan_init(GlyphScriptExecutionPlan *plan) {
    if (plan) {
        memset(plan, 0, sizeof(*plan));
        plan->edit.require_unique_glyph_ids = 1;
        plan->edit.require_subset_ids_exist = 1;
    }
}

void glyph_script_execution_plan_free(GlyphScriptExecutionPlan *plan) {
    if (!plan) {
        return;
    }
    free(plan->remaps);
    free(plan->subset_ids);
    free(plan->advances);
    free(plan->translations);
    memset(plan, 0, sizeof(*plan));
}

static int glyph_script_plan_memory_error(GlyphScriptDiagnostics *diagnostics) {
    return glyph_script_diagnostics_add(diagnostics,
                                        GLYPH_SCRIPT_DIAG_ERROR,
                                        GLYPH_SCRIPT_DIAG_MEMORY,
                                        0,
                                        0,
                                        "failed to allocate execution plan");
}

static void glyph_script_finish_plan(GlyphScriptExecutionPlan *plan) {
    plan->edit.id_remaps = plan->remaps;
    plan->edit.id_remap_count = plan->remap_count;
    plan->edit.subset_ids = plan->subset_ids;
    plan->edit.subset_id_count = plan->subset_id_count;
    plan->edit.advance_adjustments = plan->advances;
    plan->edit.advance_adjustment_count = plan->advance_count;
    plan->edit.bounds_translations = plan->translations;
    plan->edit.bounds_translation_count = plan->translate_count;
}

int glyph_script_build_execution_plan(const GlyphScript *script,
                                      const GlyphFile *file,
                                      GlyphScriptExecutionPlan *plan,
                                      GlyphScriptDiagnostics *diagnostics) {
    uint32_t i;
    GlyphEditValidationReport validation;

    (void)file;
    if (!script || !plan || (script->count && !script->commands)) {
        (void)glyph_script_diagnostics_add(diagnostics,
                                           GLYPH_SCRIPT_DIAG_ERROR,
                                           GLYPH_SCRIPT_DIAG_PLANNING,
                                           0,
                                           0,
                                           "script or plan is missing");
        return 0;
    }
    glyph_script_execution_plan_free(plan);
    glyph_script_execution_plan_init(plan);
    plan->command_count = script->count;

    for (i = 0; i < script->count; i++) {
        const GlyphScriptCommand *command = &script->commands[i];
        uint32_t j;
        switch (command->type) {
        case GLYPH_SCRIPT_COMMAND_REMAP:
            if (!glyph_script_append_remap(plan, command->as.remap.old_id, command->as.remap.new_id)) {
                (void)glyph_script_plan_memory_error(diagnostics);
                glyph_script_execution_plan_free(plan);
                return 0;
            }
            break;
        case GLYPH_SCRIPT_COMMAND_SUBSET:
            for (j = 0; j < command->as.subset.count; j++) {
                if (!glyph_script_append_subset_id(plan, command->as.subset.ids[j])) {
                    (void)glyph_script_plan_memory_error(diagnostics);
                    glyph_script_execution_plan_free(plan);
                    return 0;
                }
            }
            break;
        case GLYPH_SCRIPT_COMMAND_ADVANCE:
            if (!glyph_script_append_advance(plan,
                                             command->as.advance.glyph_id,
                                             command->as.advance.mode,
                                             command->as.advance.value)) {
                (void)glyph_script_plan_memory_error(diagnostics);
                glyph_script_execution_plan_free(plan);
                return 0;
            }
            break;
        case GLYPH_SCRIPT_COMMAND_TRANSLATE:
            if (!glyph_script_append_translation(plan,
                                                 command->as.translate.glyph_id,
                                                 command->as.translate.dx,
                                                 command->as.translate.dy)) {
                (void)glyph_script_plan_memory_error(diagnostics);
                glyph_script_execution_plan_free(plan);
                return 0;
            }
            break;
        case GLYPH_SCRIPT_COMMAND_VALIDATE:
            plan->wants_validation = 1;
            plan->edit.require_unique_glyph_ids = command->as.validate.require_unique_glyph_ids;
            plan->edit.require_subset_ids_exist = command->as.validate.require_subset_ids_exist;
            break;
        case GLYPH_SCRIPT_COMMAND_REPORT:
            plan->wants_report = 1;
            if (command->as.report.include_summary) {
                plan->report_summary = 1;
            }
            if (command->as.report.include_validation) {
                plan->wants_validation = 1;
                plan->report_validation = 1;
            }
            if (command->as.report.include_plan) {
                plan->report_plan = 1;
            }
            break;
        default:
            (void)glyph_script_diagnostics_add(diagnostics,
                                               GLYPH_SCRIPT_DIAG_ERROR,
                                               GLYPH_SCRIPT_DIAG_PLANNING,
                                               command->line,
                                               command->column,
                                               "unknown command type in script");
            glyph_script_execution_plan_free(plan);
            return 0;
        }
    }
    glyph_script_finish_plan(plan);

    if (file) {
        glyph_edit_validation_report_init(&validation);
        if (!glyph_edit_validate_script(file, &plan->edit, &validation)) {
            (void)glyph_script_diagnostics_add(diagnostics,
                                               GLYPH_SCRIPT_DIAG_ERROR,
                                               GLYPH_SCRIPT_DIAG_VALIDATION,
                                               0,
                                               0,
                                               validation.message[0] ? validation.message : "script validation failed");
            return 0;
        }
    }
    return 1;
}

void glyph_script_execution_result_init(GlyphScriptExecutionResult *result) {
    if (result) {
        memset(result, 0, sizeof(*result));
        glyph_edit_summary_init(&result->summary);
        glyph_edit_validation_report_init(&result->validation);
        glyph_edit_table_diff_init(&result->diff);
    }
}

void glyph_script_execution_result_free(GlyphScriptExecutionResult *result) {
    if (!result) {
        return;
    }
    glyph_table_free(&result->glyphs);
    kerning_table_free(&result->kerning);
    hints_free(&result->hints);
    memset(result, 0, sizeof(*result));
}

int glyph_script_validate(const GlyphScript *script,
                          const GlyphFile *file,
                          GlyphScriptDiagnostics *diagnostics,
                          GlyphEditValidationReport *report) {
    GlyphScriptExecutionPlan plan;
    GlyphEditValidationReport local;
    int ok;

    if (!report) {
        report = &local;
    }
    glyph_edit_validation_report_init(report);
    glyph_script_execution_plan_init(&plan);
    ok = glyph_script_build_execution_plan(script, file, &plan, diagnostics);
    if (ok && file) {
        ok = glyph_edit_validate_script(file, &plan.edit, report);
        if (!ok) {
            (void)glyph_script_diagnostics_add(diagnostics,
                                               GLYPH_SCRIPT_DIAG_ERROR,
                                               GLYPH_SCRIPT_DIAG_VALIDATION,
                                               0,
                                               0,
                                               report->message[0] ? report->message : "script validation failed");
        }
    }
    glyph_script_execution_plan_free(&plan);
    return ok;
}

static int glyph_script_copy_glyph_table(const GlyphTable *src, GlyphTable *dst) {
    if (!src || !dst || (src->count && !src->entries)) {
        return 0;
    }
    memset(dst, 0, sizeof(*dst));
    if (!glyph_table_alloc(dst, src->count)) {
        return 0;
    }
    if (src->count) {
        memcpy(dst->entries, src->entries, (size_t)src->count * sizeof(*src->entries));
    }
    return 1;
}

static int glyph_script_copy_kerning_table(const KerningTable *src, KerningTable *dst) {
    if (!src || !dst || (src->count && !src->pairs)) {
        return 0;
    }
    memset(dst, 0, sizeof(*dst));
    if (!kerning_table_alloc(dst, src->count)) {
        return 0;
    }
    if (src->count) {
        memcpy(dst->pairs, src->pairs, (size_t)src->count * sizeof(*src->pairs));
    }
    return 1;
}

static void glyph_script_result_error(GlyphScriptDiagnostics *diagnostics, const char *message) {
    (void)glyph_script_diagnostics_add(diagnostics,
                                       GLYPH_SCRIPT_DIAG_ERROR,
                                       GLYPH_SCRIPT_DIAG_EXECUTION,
                                       0,
                                       0,
                                       message);
}

static int glyph_script_execute_subset(const GlyphScriptExecutionPlan *plan,
                                       const GlyphFile *file,
                                       GlyphScriptExecutionResult *result,
                                       GlyphScriptDiagnostics *diagnostics) {
    uint32_t *old_to_new = NULL;
    uint32_t new_count = 0;

    if (!glyph_edit_compute_subset_remap(&file->glyphs,
                                         plan->subset_ids,
                                         plan->subset_id_count,
                                         &old_to_new,
                                         &new_count)) {
        glyph_script_result_error(diagnostics, "failed to compute subset index remap");
        return 0;
    }
    (void)new_count;
    if (!glyph_edit_copy_subset_glyph_table(&file->glyphs,
                                            old_to_new,
                                            file->glyphs.count,
                                            &result->glyphs,
                                            &result->summary)) {
        free(old_to_new);
        glyph_script_result_error(diagnostics, "failed to copy subset glyph table");
        return 0;
    }
    if (!glyph_edit_filter_kerning(&file->kerning,
                                   old_to_new,
                                   file->glyphs.count,
                                   &result->kerning,
                                   &result->summary)) {
        free(old_to_new);
        glyph_script_result_error(diagnostics, "failed to filter kerning table");
        return 0;
    }
    if (!glyph_edit_filter_hints(&file->hints,
                                 old_to_new,
                                 file->glyphs.count,
                                 &result->hints,
                                 &result->summary)) {
        free(old_to_new);
        glyph_script_result_error(diagnostics, "failed to filter hint table");
        return 0;
    }
    free(old_to_new);
    return 1;
}

static int glyph_script_execute_copy_all(const GlyphFile *file,
                                         GlyphScriptExecutionResult *result,
                                         GlyphScriptDiagnostics *diagnostics) {
    if (!glyph_script_copy_glyph_table(&file->glyphs, &result->glyphs)) {
        glyph_script_result_error(diagnostics, "failed to copy glyph table");
        return 0;
    }
    if (!glyph_script_copy_kerning_table(&file->kerning, &result->kerning)) {
        glyph_script_result_error(diagnostics, "failed to copy kerning table");
        return 0;
    }
    if (!glyph_edit_copy_hints(&file->hints, &result->hints)) {
        glyph_script_result_error(diagnostics, "failed to copy hint table");
        return 0;
    }
    result->summary.glyphs_seen += file->glyphs.count;
    result->summary.glyphs_kept += file->glyphs.count;
    result->summary.kerning_seen += file->kerning.count;
    result->summary.kerning_kept += file->kerning.count;
    result->summary.hints_seen += file->hints.count;
    return 1;
}

int glyph_script_execute_plan(const GlyphScriptExecutionPlan *plan,
                              const GlyphFile *file,
                              GlyphScriptExecutionResult *result,
                              GlyphScriptDiagnostics *diagnostics) {
    if (!plan || !file || !result) {
        glyph_script_result_error(diagnostics, "execution plan, file, or result is missing");
        return 0;
    }
    glyph_script_execution_result_free(result);
    glyph_script_execution_result_init(result);

    glyph_edit_validation_report_init(&result->validation);
    if (!glyph_edit_validate_script(file, &plan->edit, &result->validation)) {
        glyph_script_result_error(diagnostics,
                                  result->validation.message[0] ?
                                  result->validation.message :
                                  "script validation failed before execution");
        return 0;
    }
    if (plan->subset_id_count) {
        if (!glyph_script_execute_subset(plan, file, result, diagnostics)) {
            glyph_script_execution_result_free(result);
            return 0;
        }
    } else if (!glyph_script_execute_copy_all(file, result, diagnostics)) {
        glyph_script_execution_result_free(result);
        return 0;
    }

    if (!glyph_edit_apply_id_remap(&result->glyphs,
                                   plan->remaps,
                                   plan->remap_count,
                                   &result->summary)) {
        glyph_script_result_error(diagnostics, "failed to apply id remaps");
        glyph_script_execution_result_free(result);
        return 0;
    }
    if (!glyph_edit_apply_advance_adjustments(&result->glyphs,
                                              plan->advances,
                                              plan->advance_count,
                                              &result->summary)) {
        glyph_script_result_error(diagnostics, "failed to apply advance adjustments");
        glyph_script_execution_result_free(result);
        return 0;
    }
    if (!glyph_edit_translate_bounds(&result->glyphs,
                                     plan->translations,
                                     plan->translate_count,
                                     &result->summary)) {
        glyph_script_result_error(diagnostics, "failed to apply bounds translations");
        glyph_script_execution_result_free(result);
        return 0;
    }
    if (!glyph_edit_compare_tables(&file->glyphs, &result->glyphs, &result->diff)) {
        glyph_script_result_error(diagnostics, "failed to compare glyph tables");
        glyph_script_execution_result_free(result);
        return 0;
    }
    return 1;
}

int glyph_script_report(FILE *fp,
                        const GlyphScriptExecutionPlan *plan,
                        const GlyphScriptExecutionResult *result) {
    if (!fp) {
        return 0;
    }
    if (plan && plan->report_plan) {
        fprintf(fp, "Glyph script plan\n");
        fprintf(fp, "commands=%u remaps=%u subset-ids=%u advances=%u translations=%u\n",
                plan->command_count,
                plan->remap_count,
                plan->subset_id_count,
                plan->advance_count,
                plan->translate_count);
        fprintf(fp, "validate=%s report=%s require-unique=%s require-subset-exists=%s\n",
                plan->wants_validation ? "yes" : "no",
                plan->wants_report ? "yes" : "no",
                plan->edit.require_unique_glyph_ids ? "yes" : "no",
                plan->edit.require_subset_ids_exist ? "yes" : "no");
    }
    if (result) {
        const GlyphEditSummary *summary = &result->summary;
        const GlyphEditValidationReport *validation = &result->validation;
        const GlyphTableDiff *diff = &result->diff;
        if (plan && plan->wants_report) {
            summary = plan->report_summary ? &result->summary : NULL;
            validation = plan->report_validation ? &result->validation : NULL;
            diff = plan->report_plan ? &result->diff : NULL;
        }
        if (!glyph_edit_write_summary_report(fp,
                                             summary,
                                             validation,
                                             diff)) {
            return 0;
        }
    }
    return ferror(fp) == 0;
}
