#ifndef GLYPH_SCRIPT_H
#define GLYPH_SCRIPT_H

#include "atlas.h"
#include "edit.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GLYPH_SCRIPT_DIAG_INFO = 0,
    GLYPH_SCRIPT_DIAG_WARNING = 1,
    GLYPH_SCRIPT_DIAG_ERROR = 2
} GlyphScriptDiagnosticSeverity;

typedef enum {
    GLYPH_SCRIPT_DIAG_PARSE = 0,
    GLYPH_SCRIPT_DIAG_ARGUMENT = 1,
    GLYPH_SCRIPT_DIAG_MEMORY = 2,
    GLYPH_SCRIPT_DIAG_VALIDATION = 3,
    GLYPH_SCRIPT_DIAG_PLANNING = 4,
    GLYPH_SCRIPT_DIAG_EXECUTION = 5
} GlyphScriptDiagnosticCode;

typedef struct {
    GlyphScriptDiagnosticSeverity severity;
    GlyphScriptDiagnosticCode code;
    uint32_t line;
    uint32_t column;
    char message[160];
} GlyphScriptDiagnostic;

typedef struct {
    uint32_t count;
    uint32_t capacity;
    GlyphScriptDiagnostic *items;
} GlyphScriptDiagnostics;

typedef enum {
    GLYPH_SCRIPT_COMMAND_REMAP = 0,
    GLYPH_SCRIPT_COMMAND_SUBSET = 1,
    GLYPH_SCRIPT_COMMAND_ADVANCE = 2,
    GLYPH_SCRIPT_COMMAND_TRANSLATE = 3,
    GLYPH_SCRIPT_COMMAND_VALIDATE = 4,
    GLYPH_SCRIPT_COMMAND_REPORT = 5
} GlyphScriptCommandType;

typedef struct {
    uint32_t old_id;
    uint32_t new_id;
} GlyphScriptRemapCommand;

typedef struct {
    uint32_t count;
    uint32_t *ids;
} GlyphScriptSubsetCommand;

typedef struct {
    uint32_t glyph_id;
    GlyphEditAdvanceMode mode;
    int16_t value;
} GlyphScriptAdvanceCommand;

typedef struct {
    uint32_t glyph_id;
    int16_t dx;
    int16_t dy;
} GlyphScriptTranslateCommand;

typedef struct {
    int require_unique_glyph_ids;
    int require_subset_ids_exist;
} GlyphScriptValidateCommand;

typedef struct {
    int include_summary;
    int include_validation;
    int include_plan;
} GlyphScriptReportCommand;

typedef struct {
    GlyphScriptCommandType type;
    uint32_t line;
    uint32_t column;
    union {
        GlyphScriptRemapCommand remap;
        GlyphScriptSubsetCommand subset;
        GlyphScriptAdvanceCommand advance;
        GlyphScriptTranslateCommand translate;
        GlyphScriptValidateCommand validate;
        GlyphScriptReportCommand report;
    } as;
} GlyphScriptCommand;

typedef struct {
    uint32_t count;
    uint32_t capacity;
    GlyphScriptCommand *commands;
} GlyphScript;

typedef struct {
    uint32_t command_count;
    uint32_t remap_count;
    uint32_t subset_id_count;
    uint32_t advance_count;
    uint32_t translate_count;
    int wants_validation;
    int wants_report;
    int report_summary;
    int report_validation;
    int report_plan;
    GlyphEditScript edit;
    GlyphIdRemap *remaps;
    uint32_t *subset_ids;
    GlyphAdvanceAdjustment *advances;
    GlyphBoundsTranslation *translations;
} GlyphScriptExecutionPlan;

typedef struct {
    GlyphTable glyphs;
    KerningTable kerning;
    HintTable hints;
    GlyphEditSummary summary;
    GlyphEditValidationReport validation;
    GlyphTableDiff diff;
} GlyphScriptExecutionResult;

void glyph_script_diagnostics_init(GlyphScriptDiagnostics *diagnostics);
void glyph_script_diagnostics_free(GlyphScriptDiagnostics *diagnostics);
int glyph_script_diagnostics_add(GlyphScriptDiagnostics *diagnostics,
                                 GlyphScriptDiagnosticSeverity severity,
                                 GlyphScriptDiagnosticCode code,
                                 uint32_t line,
                                 uint32_t column,
                                 const char *message);
const char *glyph_script_diagnostic_severity_name(GlyphScriptDiagnosticSeverity severity);
const char *glyph_script_diagnostic_code_name(GlyphScriptDiagnosticCode code);
int glyph_script_diagnostics_write(FILE *fp, const GlyphScriptDiagnostics *diagnostics);

void glyph_script_init(GlyphScript *script);
void glyph_script_free(GlyphScript *script);
int glyph_script_parse(const char *text, GlyphScript *script, GlyphScriptDiagnostics *diagnostics);
int glyph_script_serialize(FILE *fp, const GlyphScript *script);

void glyph_script_execution_plan_init(GlyphScriptExecutionPlan *plan);
void glyph_script_execution_plan_free(GlyphScriptExecutionPlan *plan);
int glyph_script_build_execution_plan(const GlyphScript *script,
                                      const GlyphFile *file,
                                      GlyphScriptExecutionPlan *plan,
                                      GlyphScriptDiagnostics *diagnostics);

void glyph_script_execution_result_init(GlyphScriptExecutionResult *result);
void glyph_script_execution_result_free(GlyphScriptExecutionResult *result);
int glyph_script_validate(const GlyphScript *script,
                          const GlyphFile *file,
                          GlyphScriptDiagnostics *diagnostics,
                          GlyphEditValidationReport *report);
int glyph_script_execute_plan(const GlyphScriptExecutionPlan *plan,
                              const GlyphFile *file,
                              GlyphScriptExecutionResult *result,
                              GlyphScriptDiagnostics *diagnostics);
int glyph_script_report(FILE *fp,
                        const GlyphScriptExecutionPlan *plan,
                        const GlyphScriptExecutionResult *result);

#ifdef __cplusplus
}
#endif

#endif
