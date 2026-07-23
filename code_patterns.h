/*
 * This file code_patterns.h is part of L1vm.
 *
 * (c) Copyright Stefan Pietzonke (info@midnight-coding.de), 2026
 *
 * L1vm is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * L1vm is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with L1vm.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef CODE_PATTERNS_H
#define CODE_PATTERNS_H

#include "brackets-code.h"
#include "tiny_transformer.h"

/* ==================== Configuration ==================== */

#define CP_MAX_PATTERNS     512
#define CP_MAX_VARIATIONS     4
#define CP_MAX_CODE_LINES    64
#define CP_MAX_LINE_LEN     512
#define CP_MAX_PARAMS         8
#define CP_MAX_PARAM_NAME    32
#define CP_MAX_PARAM_VALUE   64
#define CP_MAX_INCLUDES       4
#define CP_MAX_VAR_NAME      32
#define CP_MAX_VAR_TYPE      16
#define CP_MAX_STEPS          8

/* ==================== Parameter Types ==================== */

typedef enum {
    CP_PARAM_INT,        /* Integer number */
    CP_PARAM_DOUBLE,     /* Floating point */
    CP_PARAM_STRING,     /* String literal */
    CP_PARAM_VARIABLE,   /* Variable name */
    CP_PARAM_EXPRESSION  /* L1VM expression */
} CPParamType;

typedef struct {
    char name[CP_MAX_PARAM_NAME];
    CPParamType type;
    char default_value[CP_MAX_PARAM_VALUE];
    char description[128];
} CPParam;

/* ==================== Code Pattern ==================== */

typedef struct {
    char name[64];
    char code_lines[CP_MAX_CODE_LINES][CP_MAX_LINE_LEN];
    int num_code_lines;
    int complexity;
} CPCodeVariation;

typedef struct {
    char id[64];
    char name[128];
    char description[256];
    int emitter_id;

    /* Parameters this pattern accepts */
    CPParam params[CP_MAX_PARAMS];
    int num_params;

    /* Includes needed */
    char includes[CP_MAX_INCLUDES][64];
    int num_includes;

    /* Variable declarations */
    struct {
        char type[CP_MAX_VAR_TYPE];
        char name[CP_MAX_VAR_NAME];
        char default_value[CP_MAX_PARAM_VALUE];
        int is_array;
        int array_size;
    } vars[16];
    int num_vars;

    /* Code variations (e.g., iterative vs recursive) */
    CPCodeVariation variations[CP_MAX_VARIATIONS];
    int num_variations;

    /* Metadata */
    int priority;
} CPCodePattern;

/* ==================== Extracted Parameters ==================== */

typedef struct {
    char name[CP_MAX_PARAM_NAME];
    char value[CP_MAX_PARAM_VALUE];
    CPParamType type;
} CPExtractedParam;

typedef struct {
    CPExtractedParam params[CP_MAX_PARAMS];
    int num_params;
    int numbers[16];
    int num_numbers;
    char strings[8][128];
    int num_strings;
} CPExtraction;

/* ==================== Multi-Step Generation ==================== */

typedef struct {
    int pattern_indices[CP_MAX_STEPS];
    CPExtraction extractions[CP_MAX_STEPS];
    int num_steps;
    int total_lines;
} CPStepPlan;

/* ==================== Pattern Database ==================== */

typedef struct {
    CPCodePattern patterns[CP_MAX_PATTERNS];
    int num_patterns;
} CPPatternDB;

/* ==================== API ==================== */

/* Initialize pattern database */
void cp_init(CPPatternDB *db);

/* Load patterns from DSL directory */
int cp_load_from_dsl(CPPatternDB *db, const char *dsl_dir);

/* Add a pattern programmatically */
int cp_add_pattern(CPPatternDB *db, const CPCodePattern *pattern);

/* Pre-built patterns for common emitters */
void cp_create_fibonacci(CPPatternDB *db);
void cp_create_bubble_sort(CPPatternDB *db);
void cp_create_hello_name(CPPatternDB *db);
void cp_create_fizzbuzz(CPPatternDB *db);
void cp_create_factorial(CPPatternDB *db);
void cp_create_primes(CPPatternDB *db);
void cp_create_selection_sort(CPPatternDB *db);
void cp_create_calculator(CPPatternDB *db);
void cp_create_hello_world(CPPatternDB *db);
void cp_create_string_length(CPPatternDB *db);
void cp_create_array_reverse(CPPatternDB *db);

/* Find patterns matching an emitter */
int cp_find_by_emitter(const CPPatternDB *db, int emitter_id,
                       int *indices, int max_results);

/* Find best pattern for a prompt (uses transformer + parameter extraction) */
int cp_find_best(const CPPatternDB *db, const char *prompt,
                 int transformer_emitter, CPExtraction *extraction);

/* Find best variation for a pattern based on prompt keywords */
int cp_find_best_variation(const CPCodePattern *pattern, const char *prompt);

/* Extract parameters from a prompt */
void cp_extract_params(const char *prompt, CPExtraction *extraction);

/* Generate code from a pattern with extracted parameters */
int cp_generate_code(const CPCodePattern *pattern, const CPExtraction *extraction,
                     char *output, int output_size);

/* Generate code from a specific variation */
int cp_generate_code_variation(const CPCodePattern *pattern, int variation_idx,
                               const CPExtraction *extraction,
                               char *output, int output_size);

/* Multi-step generation: combine multiple patterns */
int cp_generate_multi_step(const CPPatternDB *db, const CPStepPlan *plan,
                           char *output, int output_size);

/* Plan a multi-step solution from a complex prompt */
int cp_plan_steps(const CPPatternDB *db, const char *prompt,
                  CPStepPlan *plan);

/* Print pattern info (for debugging) */
void cp_print_pattern(const CPCodePattern *pattern);

/* ==================== Neural Parameter Extractor ==================== */

/* Small neural network for extracting numbers/strings from prompts */
typedef struct {
    /* Simple feed-forward: input (embed_dim) → hidden → output (max_params * 2) */
    Matrix *w1;    /* [embed_dim, hidden_dim] */
    Matrix *b1;    /* [1, hidden_dim] */
    Matrix *w2;    /* [hidden_dim, max_outputs] */
    Matrix *b2;    /* [1, max_outputs] */
} CPExtractor;

CPExtractor *cp_extractor_create(void);
void         cp_extractor_free(CPExtractor *ext);
void         cp_extractor_extract(const CPExtractor *ext, const TTVocab *vocab,
                                  const char *prompt, CPExtraction *extraction);
void         cp_extractor_add_example(const char *prompt, const CPExtraction *target);
int          cp_extractor_train(CPExtractor *ext, const TTVocab *vocab,
                                int epochs, float lr);

#endif /* CODE_PATTERNS_H */
