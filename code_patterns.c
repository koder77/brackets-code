/*
 * This file code_patterns.c is part of L1vm.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "code_patterns.h"
#include "tiny_transformer.h"

/* ==================== Internal Helpers ==================== */

static void cp_str_lower(char *dest, const char *src, int max) {
    int i;
    for (i = 0; i < max - 1 && src[i]; i++) {
        dest[i] = tolower((unsigned char)src[i]);
    }
    dest[i] = '\0';
}

static int cp_str_contains(const char *haystack, const char *needle) {
    char h_lower[256];
    char n_lower[128];
    cp_str_lower(h_lower, haystack, sizeof(h_lower));
    cp_str_lower(n_lower, needle, sizeof(n_lower));
    return strstr(h_lower, n_lower) != NULL;
}

/* ==================== Pattern Database ==================== */

void cp_init(CPPatternDB *db) {
    db->num_patterns = 0;
}

int cp_add_pattern(CPPatternDB *db, const CPCodePattern *pattern) {
    if (db->num_patterns >= CP_MAX_PATTERNS) return -1;
    db->patterns[db->num_patterns] = *pattern;
    db->num_patterns++;
    return 0;
}

/* ==================== DSL Loading ==================== */

int cp_load_from_dsl(CPPatternDB *db, const char *dsl_dir) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "ls %s/*.l1dsl 2>/dev/null", dsl_dir);

    FILE *pipe = popen(cmd, "r");
    if (!pipe) return -1;

    char filepath[512];
    int loaded = 0;

    while (fgets(filepath, sizeof(filepath), pipe)) {
        /* Remove newline */
        int len = strlen(filepath);
        if (len > 0 && filepath[len-1] == '\n') filepath[--len] = '\0';
        if (len == 0) continue;

        /* Skip test DSL files */
        if (strstr(filepath, "test-")) continue;

        /* Extract rule name from path */
        const char *basename = strrchr(filepath, '/');
        if (basename) basename++; else basename = filepath;
        char rule_name[64];
        snprintf(rule_name, sizeof(rule_name), "%s", basename);
        char *dot = strrchr(rule_name, '.');
        if (dot) *dot = '\0';

        /* Create a simple pattern from the DSL file */
        CPCodePattern pat;
        memset(&pat, 0, sizeof(pat));

        snprintf(pat.id, 64, "%s", rule_name);
        snprintf(pat.name, 128, "%s", rule_name);
        pat.emitter_id = -1;  /* Will be mapped later */
        pat.priority = 1;
        pat.num_variations = 1;

        /* Add default variation */
        snprintf(pat.variations[0].name, 64, "default");
        pat.variations[0].complexity = 0;
        pat.variations[0].num_code_lines = 1;
        snprintf(pat.variations[0].code_lines[0], CP_MAX_LINE_LEN,
                 "(run-program \"%s\")", rule_name);

        if (cp_add_pattern(db, &pat) == 0) {
            loaded++;
        }
    }

    pclose(pipe);
    return loaded;
}

/* ==================== Programmatic Pattern Creation ==================== */

void cp_create_fibonacci(CPPatternDB *db) {
    CPCodePattern pat;
    memset(&pat, 0, sizeof(pat));

    snprintf(pat.id, 64, "fibonacci");
    snprintf(pat.name, 128, "Fibonacci Sequence Generator");
    snprintf(pat.description, 256, "Generate fibonacci numbers");
    pat.emitter_id = 7;  /* fib_seq */
    pat.priority = 10;

    /* Parameters */
    pat.num_params = 1;
    snprintf(pat.params[0].name, CP_MAX_PARAM_NAME, "n");
    pat.params[0].type = CP_PARAM_INT;
    snprintf(pat.params[0].description, 128, "Number of fibonacci numbers to generate");

    /* Variation 1: Iterative */
    pat.num_variations = 2;
    snprintf(pat.variations[0].name, 64, "iterative");
    pat.variations[0].complexity = 0;
    pat.variations[0].num_code_lines = 8;
    snprintf(pat.variations[0].code_lines[0], CP_MAX_LINE_LEN, "(set const-int64 1 zero 0)");
    snprintf(pat.variations[0].code_lines[1], CP_MAX_LINE_LEN, "(set const-int64 1 one 1)");
    snprintf(pat.variations[0].code_lines[2], CP_MAX_LINE_LEN, "(set int64 1 a 0)");
    snprintf(pat.variations[0].code_lines[3], CP_MAX_LINE_LEN, "(set int64 1 b 1)");
    snprintf(pat.variations[0].code_lines[4], CP_MAX_LINE_LEN, "(set const-int64 1 i 0)");
    snprintf(pat.variations[0].code_lines[5], CP_MAX_LINE_LEN, "(while {i} < {n}");
    snprintf(pat.variations[0].code_lines[6], CP_MAX_LINE_LEN, "  (print a)");
    snprintf(pat.variations[0].code_lines[7], CP_MAX_LINE_LEN, ")");

    /* Variation 2: Recursive */
    snprintf(pat.variations[1].name, 64, "recursive");
    pat.variations[1].complexity = 2;
    pat.variations[1].num_code_lines = 6;
    snprintf(pat.variations[1].code_lines[0], CP_MAX_LINE_LEN, "(function fib {n} =");
    snprintf(pat.variations[1].code_lines[1], CP_MAX_LINE_LEN, "  (if {n} <= 1");
    snprintf(pat.variations[1].code_lines[2], CP_MAX_LINE_LEN, "    (return {n})");
    snprintf(pat.variations[1].code_lines[3], CP_MAX_LINE_LEN, "  )");
    snprintf(pat.variations[1].code_lines[4], CP_MAX_LINE_LEN, "  (return (fib {n}-1) + (fib {n}-2))");
    snprintf(pat.variations[1].code_lines[5], CP_MAX_LINE_LEN, ")");

    cp_add_pattern(db, &pat);
}

void cp_create_bubble_sort(CPPatternDB *db) {
    CPCodePattern pat;
    memset(&pat, 0, sizeof(pat));

    snprintf(pat.id, 64, "bubble_sort");
    snprintf(pat.name, 128, "Bubble Sort");
    snprintf(pat.description, 256, "Sort array using bubble sort");
    pat.emitter_id = 1;  /* sort */
    pat.priority = 10;

    pat.num_params = 1;
    snprintf(pat.params[0].name, CP_MAX_PARAM_NAME, "size");
    pat.params[0].type = CP_PARAM_INT;
    snprintf(pat.params[0].description, 128, "Array size to sort");

    /* Variation 1: Ascending */
    pat.num_variations = 2;
    snprintf(pat.variations[0].name, 64, "ascending");
    pat.variations[0].complexity = 1;
    pat.variations[0].num_code_lines = 12;
    snprintf(pat.variations[0].code_lines[0], CP_MAX_LINE_LEN, "(set const-int64 1 {size} 5)");
    snprintf(pat.variations[0].code_lines[1], CP_MAX_LINE_LEN, "(set int64 1 i 0)");
    snprintf(pat.variations[0].code_lines[2], CP_MAX_LINE_LEN, "(set int64 1 j 0)");
    snprintf(pat.variations[0].code_lines[3], CP_MAX_LINE_LEN, "(set int64 1 temp 0)");
    snprintf(pat.variations[0].code_lines[4], CP_MAX_LINE_LEN, "(while i < {size}-1");
    snprintf(pat.variations[0].code_lines[5], CP_MAX_LINE_LEN, "  (set const-int64 1 j 0)");
    snprintf(pat.variations[0].code_lines[6], CP_MAX_LINE_LEN, "  (while j < {size}-i-1");
    snprintf(pat.variations[0].code_lines[7], CP_MAX_LINE_LEN, "    (if arr[j] > arr[j+1]");
    snprintf(pat.variations[0].code_lines[8], CP_MAX_LINE_LEN, "      (swap arr[j] arr[j+1])");
    snprintf(pat.variations[0].code_lines[9], CP_MAX_LINE_LEN, "    )");
    snprintf(pat.variations[0].code_lines[10], CP_MAX_LINE_LEN, "    (j++)");
    snprintf(pat.variations[0].code_lines[11], CP_MAX_LINE_LEN, "  )");
    snprintf(pat.variations[0].code_lines[12], CP_MAX_LINE_LEN, "  (i++)");
    snprintf(pat.variations[0].code_lines[13], CP_MAX_LINE_LEN, ")");
    pat.variations[0].num_code_lines = 14;

    /* Variation 2: Descending */
    snprintf(pat.variations[1].name, 64, "descending");
    pat.variations[1].complexity = 1;
    pat.variations[1].num_code_lines = 14;
    snprintf(pat.variations[1].code_lines[0], CP_MAX_LINE_LEN, "(set const-int64 1 {size} 5)");
    snprintf(pat.variations[1].code_lines[1], CP_MAX_LINE_LEN, "(set int64 1 i 0)");
    snprintf(pat.variations[1].code_lines[2], CP_MAX_LINE_LEN, "(set int64 1 j 0)");
    snprintf(pat.variations[1].code_lines[3], CP_MAX_LINE_LEN, "(set int64 1 temp 0)");
    snprintf(pat.variations[1].code_lines[4], CP_MAX_LINE_LEN, "(while i < {size}-1");
    snprintf(pat.variations[1].code_lines[5], CP_MAX_LINE_LEN, "  (set const-int64 1 j 0)");
    snprintf(pat.variations[1].code_lines[6], CP_MAX_LINE_LEN, "  (while j < {size}-i-1");
    snprintf(pat.variations[1].code_lines[7], CP_MAX_LINE_LEN, "    (if arr[j] < arr[j+1]");
    snprintf(pat.variations[1].code_lines[8], CP_MAX_LINE_LEN, "      (swap arr[j] arr[j+1])");
    snprintf(pat.variations[1].code_lines[9], CP_MAX_LINE_LEN, "    )");
    snprintf(pat.variations[1].code_lines[10], CP_MAX_LINE_LEN, "    (j++)");
    snprintf(pat.variations[1].code_lines[11], CP_MAX_LINE_LEN, "  )");
    snprintf(pat.variations[1].code_lines[12], CP_MAX_LINE_LEN, "  (i++)");
    snprintf(pat.variations[1].code_lines[13], CP_MAX_LINE_LEN, ")");
    pat.variations[1].num_code_lines = 14;

    cp_add_pattern(db, &pat);
}

void cp_create_hello_name(CPPatternDB *db) {
    CPCodePattern pat;
    memset(&pat, 0, sizeof(pat));

    snprintf(pat.id, 64, "hello_name");
    snprintf(pat.name, 128, "Hello Name Program");
    snprintf(pat.description, 256, "Greet a user by name");
    pat.emitter_id = 16;  /* hello_name */
    pat.priority = 10;

    pat.num_params = 1;
    snprintf(pat.params[0].name, CP_MAX_PARAM_NAME, "name");
    pat.params[0].type = CP_PARAM_STRING;
    snprintf(pat.params[0].description, 128, "Name to greet");

    /* Variation 1: Simple */
    pat.num_variations = 2;
    snprintf(pat.variations[0].name, 64, "simple");
    pat.variations[0].complexity = 0;
    pat.variations[0].num_code_lines = 3;
    snprintf(pat.variations[0].code_lines[0], CP_MAX_LINE_LEN, "(set-string 1 msg \"Hello, {name}!\")");
    snprintf(pat.variations[0].code_lines[1], CP_MAX_LINE_LEN, "(print msg)");
    snprintf(pat.variations[0].code_lines[2], CP_MAX_LINE_LEN, "(quit 0)");

    /* Variation 2: With newline */
    snprintf(pat.variations[1].name, 64, "newline");
    pat.variations[1].complexity = 0;
    pat.variations[1].num_code_lines = 4;
    snprintf(pat.variations[1].code_lines[0], CP_MAX_LINE_LEN, "(set-string 1 msg \"Hello, {name}!\")");
    snprintf(pat.variations[1].code_lines[1], CP_MAX_LINE_LEN, "(print msg)");
    snprintf(pat.variations[1].code_lines[2], CP_MAX_LINE_LEN, "(print-nl)");
    snprintf(pat.variations[1].code_lines[3], CP_MAX_LINE_LEN, "(quit 0)");
    pat.variations[1].num_code_lines = 4;

    cp_add_pattern(db, &pat);
}

void cp_create_fizzbuzz(CPPatternDB *db) {
    CPCodePattern pat;
    memset(&pat, 0, sizeof(pat));

    snprintf(pat.id, 64, "fizzbuzz");
    snprintf(pat.name, 128, "FizzBuzz");
    snprintf(pat.description, 256, "Classic FizzBuzz program");
    pat.emitter_id = 18;  /* fizzbuzz */
    pat.priority = 10;

    pat.num_params = 1;
    snprintf(pat.params[0].name, CP_MAX_PARAM_NAME, "n");
    pat.params[0].type = CP_PARAM_INT;
    snprintf(pat.params[0].description, 128, "Upper limit");

    /* Variation 1: Standard */
    pat.num_variations = 2;
    snprintf(pat.variations[0].name, 64, "standard");
    pat.variations[0].complexity = 1;
    pat.variations[0].num_code_lines = 10;
    snprintf(pat.variations[0].code_lines[0], CP_MAX_LINE_LEN, "(set const-int64 1 i 1)");
    snprintf(pat.variations[0].code_lines[1], CP_MAX_LINE_LEN, "(while i <= {n}");
    snprintf(pat.variations[0].code_lines[2], CP_MAX_LINE_LEN, "  (if i %% 15 == 0");
    snprintf(pat.variations[0].code_lines[3], CP_MAX_LINE_LEN, "    (print \"FizzBuzz\")");
    snprintf(pat.variations[0].code_lines[4], CP_MAX_LINE_LEN, "  (else-if i %% 3 == 0");
    snprintf(pat.variations[0].code_lines[5], CP_MAX_LINE_LEN, "    (print \"Fizz\")");
    snprintf(pat.variations[0].code_lines[6], CP_MAX_LINE_LEN, "  (else-if i %% 5 == 0");
    snprintf(pat.variations[0].code_lines[7], CP_MAX_LINE_LEN, "    (print \"Buzz\")");
    snprintf(pat.variations[0].code_lines[8], CP_MAX_LINE_LEN, "  (else (print i))");
    snprintf(pat.variations[0].code_lines[9], CP_MAX_LINE_LEN, "  (i++)");
    snprintf(pat.variations[0].code_lines[10], CP_MAX_LINE_LEN, ")");

    /* Variation 2: Compact */
    snprintf(pat.variations[1].name, 64, "compact");
    pat.variations[1].complexity = 2;
    pat.variations[1].num_code_lines = 8;
    snprintf(pat.variations[1].code_lines[0], CP_MAX_LINE_LEN, "(set const-int64 1 i 0)");
    snprintf(pat.variations[1].code_lines[1], CP_MAX_LINE_LEN, "(while i++ <= {n}");
    snprintf(pat.variations[1].code_lines[2], CP_MAX_LINE_LEN, "  (if i %% 15 == 0 (print \"FizzBuzz\"))");
    snprintf(pat.variations[1].code_lines[3], CP_MAX_LINE_LEN, "  (else-if i %% 3 == 0 (print \"Fizz\"))");
    snprintf(pat.variations[1].code_lines[4], CP_MAX_LINE_LEN, "  (else-if i %% 5 == 0 (print \"Buzz\"))");
    snprintf(pat.variations[1].code_lines[5], CP_MAX_LINE_LEN, "  (else (print i))");
    snprintf(pat.variations[1].code_lines[6], CP_MAX_LINE_LEN, ")");
    pat.variations[1].num_code_lines = 7;

    cp_add_pattern(db, &pat);
}

/* ==================== More Patterns ==================== */

void cp_create_factorial(CPPatternDB *db) {
    CPCodePattern pat;
    memset(&pat, 0, sizeof(pat));

    snprintf(pat.id, 64, "factorial");
    snprintf(pat.name, 128, "Factorial Calculator");
    snprintf(pat.description, 256, "Calculate factorial of N");
    pat.emitter_id = 21;  /* factorial */
    pat.priority = 10;

    pat.num_params = 1;
    snprintf(pat.params[0].name, CP_MAX_PARAM_NAME, "n");
    pat.params[0].type = CP_PARAM_INT;

    /* Variation 1: Iterative */
    pat.num_variations = 2;
    snprintf(pat.variations[0].name, 64, "iterative");
    pat.variations[0].complexity = 0;
    pat.variations[0].num_code_lines = 6;
    snprintf(pat.variations[0].code_lines[0], CP_MAX_LINE_LEN, "(set const-int64 1 result 1)");
    snprintf(pat.variations[0].code_lines[1], CP_MAX_LINE_LEN, "(set const-int64 1 i 1)");
    snprintf(pat.variations[0].code_lines[2], CP_MAX_LINE_LEN, "(while i <= {n}");
    snprintf(pat.variations[0].code_lines[3], CP_MAX_LINE_LEN, "  (set int64 1 result result * i)");
    snprintf(pat.variations[0].code_lines[4], CP_MAX_LINE_LEN, "  (i++)");
    snprintf(pat.variations[0].code_lines[5], CP_MAX_LINE_LEN, ")");
    snprintf(pat.variations[0].code_lines[6], CP_MAX_LINE_LEN, "(print result)");

    /* Variation 2: Recursive */
    snprintf(pat.variations[1].name, 64, "recursive");
    pat.variations[1].complexity = 2;
    pat.variations[1].num_code_lines = 5;
    snprintf(pat.variations[1].code_lines[0], CP_MAX_LINE_LEN, "(function fact {n} =");
    snprintf(pat.variations[1].code_lines[1], CP_MAX_LINE_LEN, "  (if {n} <= 1");
    snprintf(pat.variations[1].code_lines[2], CP_MAX_LINE_LEN, "    (return 1)");
    snprintf(pat.variations[1].code_lines[3], CP_MAX_LINE_LEN, "  )");
    snprintf(pat.variations[1].code_lines[4], CP_MAX_LINE_LEN, "  (return {n} * (fact {n}-1))");
    snprintf(pat.variations[1].code_lines[5], CP_MAX_LINE_LEN, ")");
    pat.variations[1].num_code_lines = 6;

    cp_add_pattern(db, &pat);
}

void cp_create_primes(CPPatternDB *db) {
    CPCodePattern pat;
    memset(&pat, 0, sizeof(pat));

    snprintf(pat.id, 64, "primes");
    snprintf(pat.name, 128, "Prime Number Generator");
    snprintf(pat.description, 256, "Generate prime numbers");
    pat.emitter_id = 23;  /* primes */
    pat.priority = 10;

    pat.num_params = 1;
    snprintf(pat.params[0].name, CP_MAX_PARAM_NAME, "n");
    pat.params[0].type = CP_PARAM_INT;

    /* Variation 1: Simple trial division */
    pat.num_variations = 2;
    snprintf(pat.variations[0].name, 64, "simple");
    pat.variations[0].complexity = 1;
    pat.variations[0].num_code_lines = 10;
    snprintf(pat.variations[0].code_lines[0], CP_MAX_LINE_LEN, "(set const-int64 1 count 0)");
    snprintf(pat.variations[0].code_lines[1], CP_MAX_LINE_LEN, "(set const-int64 1 num 2)");
    snprintf(pat.variations[0].code_lines[2], CP_MAX_LINE_LEN, "(while count < {n}");
    snprintf(pat.variations[0].code_lines[3], CP_MAX_LINE_LEN, "  (set const-int64 1 is_prime 1)");
    snprintf(pat.variations[0].code_lines[4], CP_MAX_LINE_LEN, "  (set const-int64 1 i 2)");
    snprintf(pat.variations[0].code_lines[5], CP_MAX_LINE_LEN, "  (while i * i <= num");
    snprintf(pat.variations[0].code_lines[6], CP_MAX_LINE_LEN, "    (if num %% i == 0 (set const-int64 1 is_prime 0))");
    snprintf(pat.variations[0].code_lines[7], CP_MAX_LINE_LEN, "    (i++)");
    snprintf(pat.variations[0].code_lines[8], CP_MAX_LINE_LEN, "  )");
    snprintf(pat.variations[0].code_lines[9], CP_MAX_LINE_LEN, "  (if is_prime == 1 (print num) (count++))");
    snprintf(pat.variations[0].code_lines[10], CP_MAX_LINE_LEN, "  (num++)");
    snprintf(pat.variations[0].code_lines[11], CP_MAX_LINE_LEN, ")");
    pat.variations[0].num_code_lines = 12;

    /* Variation 2: Sieve of Eratosthenes */
    snprintf(pat.variations[1].name, 64, "sieve");
    pat.variations[1].complexity = 2;
    pat.variations[1].num_code_lines = 8;
    snprintf(pat.variations[1].code_lines[0], CP_MAX_LINE_LEN, "(set const-int64 1 limit {n} * 10)");
    snprintf(pat.variations[1].code_lines[1], CP_MAX_LINE_LEN, "(array int64 sieve limit)");
    snprintf(pat.variations[1].code_lines[2], CP_MAX_LINE_LEN, "(set const-int64 1 i 2)");
    snprintf(pat.variations[1].code_lines[3], CP_MAX_LINE_LEN, "(while i < limit");
    snprintf(pat.variations[1].code_lines[4], CP_MAX_LINE_LEN, "  (if sieve[i] == 0 (print i))");
    snprintf(pat.variations[1].code_lines[5], CP_MAX_LINE_LEN, "  (set const-int64 1 j i * 2)");
    snprintf(pat.variations[1].code_lines[6], CP_MAX_LINE_LEN, "  (while j < limit (set array sieve j 1) (j += i))");
    snprintf(pat.variations[1].code_lines[7], CP_MAX_LINE_LEN, "  (i++)");
    snprintf(pat.variations[1].code_lines[8], CP_MAX_LINE_LEN, ")");
    pat.variations[1].num_code_lines = 9;

    cp_add_pattern(db, &pat);
}

void cp_create_selection_sort(CPPatternDB *db) {
    CPCodePattern pat;
    memset(&pat, 0, sizeof(pat));

    snprintf(pat.id, 64, "selection_sort");
    snprintf(pat.name, 128, "Selection Sort");
    snprintf(pat.description, 256, "Sort array using selection sort");
    pat.emitter_id = 38;  /* selection_sort */
    pat.priority = 10;

    pat.num_params = 1;
    snprintf(pat.params[0].name, CP_MAX_PARAM_NAME, "size");
    pat.params[0].type = CP_PARAM_INT;

    /* Variation 1: Ascending */
    pat.num_variations = 2;
    snprintf(pat.variations[0].name, 64, "ascending");
    pat.variations[0].complexity = 1;
    pat.variations[0].num_code_lines = 12;
    snprintf(pat.variations[0].code_lines[0], CP_MAX_LINE_LEN, "(set const-int64 1 i 0)");
    snprintf(pat.variations[0].code_lines[1], CP_MAX_LINE_LEN, "(while i < {size}-1");
    snprintf(pat.variations[0].code_lines[2], CP_MAX_LINE_LEN, "  (set int64 1 min_idx i)");
    snprintf(pat.variations[0].code_lines[3], CP_MAX_LINE_LEN, "  (set int64 1 j i + 1)");
    snprintf(pat.variations[0].code_lines[4], CP_MAX_LINE_LEN, "  (while j < {size}");
    snprintf(pat.variations[0].code_lines[5], CP_MAX_LINE_LEN, "    (if arr[j] < arr[min_idx] (set int64 1 min_idx j))");
    snprintf(pat.variations[0].code_lines[6], CP_MAX_LINE_LEN, "    (j++)");
    snprintf(pat.variations[0].code_lines[7], CP_MAX_LINE_LEN, "  )");
    snprintf(pat.variations[0].code_lines[8], CP_MAX_LINE_LEN, "  (if min_idx != i (swap arr[i] arr[min_idx]))");
    snprintf(pat.variations[0].code_lines[9], CP_MAX_LINE_LEN, "  (i++)");
    snprintf(pat.variations[0].code_lines[10], CP_MAX_LINE_LEN, ")");
    pat.variations[0].num_code_lines = 11;

    /* Variation 2: Descending */
    snprintf(pat.variations[1].name, 64, "descending");
    pat.variations[1].complexity = 1;
    pat.variations[1].num_code_lines = 11;
    snprintf(pat.variations[1].code_lines[0], CP_MAX_LINE_LEN, "(set const-int64 1 i 0)");
    snprintf(pat.variations[1].code_lines[1], CP_MAX_LINE_LEN, "(while i < {size}-1");
    snprintf(pat.variations[1].code_lines[2], CP_MAX_LINE_LEN, "  (set int64 1 max_idx i)");
    snprintf(pat.variations[1].code_lines[3], CP_MAX_LINE_LEN, "  (set int64 1 j i + 1)");
    snprintf(pat.variations[1].code_lines[4], CP_MAX_LINE_LEN, "  (while j < {size}");
    snprintf(pat.variations[1].code_lines[5], CP_MAX_LINE_LEN, "    (if arr[j] > arr[max_idx] (set int64 1 max_idx j))");
    snprintf(pat.variations[1].code_lines[6], CP_MAX_LINE_LEN, "    (j++)");
    snprintf(pat.variations[1].code_lines[7], CP_MAX_LINE_LEN, "  )");
    snprintf(pat.variations[1].code_lines[8], CP_MAX_LINE_LEN, "  (if max_idx != i (swap arr[i] arr[max_idx]))");
    snprintf(pat.variations[1].code_lines[9], CP_MAX_LINE_LEN, "  (i++)");
    snprintf(pat.variations[1].code_lines[10], CP_MAX_LINE_LEN, ")");
    pat.variations[1].num_code_lines = 11;

    cp_add_pattern(db, &pat);
}

void cp_create_calculator(CPPatternDB *db) {
    CPCodePattern pat;
    memset(&pat, 0, sizeof(pat));

    snprintf(pat.id, 64, "calculator");
    snprintf(pat.name, 128, "Calculator");
    snprintf(pat.description, 256, "Simple calculator");
    pat.emitter_id = 72;  /* calculator */
    pat.priority = 10;

    pat.num_params = 2;
    snprintf(pat.params[0].name, CP_MAX_PARAM_NAME, "a");
    pat.params[0].type = CP_PARAM_INT;
    snprintf(pat.params[1].name, CP_MAX_PARAM_NAME, "b");
    pat.params[1].type = CP_PARAM_INT;

    /* Variation 1: Basic operations */
    pat.num_variations = 2;
    snprintf(pat.variations[0].name, 64, "basic");
    pat.variations[0].complexity = 0;
    pat.variations[0].num_code_lines = 5;
    snprintf(pat.variations[0].code_lines[0], CP_MAX_LINE_LEN, "(set const-int64 1 a {a})");
    snprintf(pat.variations[0].code_lines[1], CP_MAX_LINE_LEN, "(set const-int64 1 b {b})");
    snprintf(pat.variations[0].code_lines[2], CP_MAX_LINE_LEN, "(print \"Add: \" a + b)");
    snprintf(pat.variations[0].code_lines[3], CP_MAX_LINE_LEN, "(print \"Sub: \" a - b)");
    snprintf(pat.variations[0].code_lines[4], CP_MAX_LINE_LEN, "(print \"Mul: \" a * b)");
    pat.variations[0].num_code_lines = 5;

    /* Variation 2: With division */
    snprintf(pat.variations[1].name, 64, "full");
    pat.variations[1].complexity = 1;
    pat.variations[1].num_code_lines = 6;
    snprintf(pat.variations[1].code_lines[0], CP_MAX_LINE_LEN, "(set const-int64 1 a {a})");
    snprintf(pat.variations[1].code_lines[1], CP_MAX_LINE_LEN, "(set const-int64 1 b {b})");
    snprintf(pat.variations[1].code_lines[2], CP_MAX_LINE_LEN, "(print \"Add: \" a + b)");
    snprintf(pat.variations[1].code_lines[3], CP_MAX_LINE_LEN, "(print \"Sub: \" a - b)");
    snprintf(pat.variations[1].code_lines[4], CP_MAX_LINE_LEN, "(print \"Mul: \" a * b)");
    snprintf(pat.variations[1].code_lines[5], CP_MAX_LINE_LEN, "(if b != 0 (print \"Div: \" a / b))");
    pat.variations[1].num_code_lines = 6;

    cp_add_pattern(db, &pat);
}

void cp_create_hello_world(CPPatternDB *db) {
    CPCodePattern pat;
    memset(&pat, 0, sizeof(pat));

    snprintf(pat.id, 64, "hello_world");
    snprintf(pat.name, 128, "Hello World");
    snprintf(pat.description, 256, "Print hello world");
    pat.emitter_id = 148;  /* hello_world */
    pat.priority = 10;

    pat.num_params = 0;

    /* Variation 1: Simple */
    pat.num_variations = 2;
    snprintf(pat.variations[0].name, 64, "simple");
    pat.variations[0].complexity = 0;
    pat.variations[0].num_code_lines = 2;
    snprintf(pat.variations[0].code_lines[0], CP_MAX_LINE_LEN, "(print \"Hello, World!\")");
    snprintf(pat.variations[0].code_lines[1], CP_MAX_LINE_LEN, "(quit 0)");
    pat.variations[0].num_code_lines = 2;

    /* Variation 2: With newline */
    snprintf(pat.variations[1].name, 64, "newline");
    pat.variations[1].complexity = 0;
    pat.variations[1].num_code_lines = 3;
    snprintf(pat.variations[1].code_lines[0], CP_MAX_LINE_LEN, "(print \"Hello, World!\")");
    snprintf(pat.variations[1].code_lines[1], CP_MAX_LINE_LEN, "(print-nl)");
    snprintf(pat.variations[1].code_lines[2], CP_MAX_LINE_LEN, "(quit 0)");
    pat.variations[1].num_code_lines = 3;

    cp_add_pattern(db, &pat);
}

void cp_create_string_length(CPPatternDB *db) {
    CPCodePattern pat;
    memset(&pat, 0, sizeof(pat));

    snprintf(pat.id, 64, "string_length");
    snprintf(pat.name, 128, "String Length");
    snprintf(pat.description, 256, "Calculate string length");
    pat.emitter_id = 68;  /* string_length */
    pat.priority = 10;

    pat.num_params = 1;
    snprintf(pat.params[0].name, CP_MAX_PARAM_NAME, "str");
    pat.params[0].type = CP_PARAM_STRING;

    /* Variation 1: Simple */
    pat.num_variations = 1;
    snprintf(pat.variations[0].name, 64, "simple");
    pat.variations[0].complexity = 0;
    pat.variations[0].num_code_lines = 3;
    snprintf(pat.variations[0].code_lines[0], CP_MAX_LINE_LEN, "(set-string 1 str \"{str}\")");
    snprintf(pat.variations[0].code_lines[1], CP_MAX_LINE_LEN, "(set int64 1 len str.length)");
    snprintf(pat.variations[0].code_lines[2], CP_MAX_LINE_LEN, "(print len)");
    pat.variations[0].num_code_lines = 3;

    cp_add_pattern(db, &pat);
}

void cp_create_array_reverse(CPPatternDB *db) {
    CPCodePattern pat;
    memset(&pat, 0, sizeof(pat));

    snprintf(pat.id, 64, "array_reverse");
    snprintf(pat.name, 128, "Array Reverse");
    snprintf(pat.description, 256, "Reverse an array");
    pat.emitter_id = 13;  /* array_reverse */
    pat.priority = 10;

    pat.num_params = 1;
    snprintf(pat.params[0].name, CP_MAX_PARAM_NAME, "size");
    pat.params[0].type = CP_PARAM_INT;

    /* Variation 1: Simple */
    pat.num_variations = 1;
    snprintf(pat.variations[0].name, 64, "simple");
    pat.variations[0].complexity = 1;
    pat.variations[0].num_code_lines = 8;
    snprintf(pat.variations[0].code_lines[0], CP_MAX_LINE_LEN, "(set const-int64 1 n {size})");
    snprintf(pat.variations[0].code_lines[1], CP_MAX_LINE_LEN, "(set const-int64 1 i 0)");
    snprintf(pat.variations[0].code_lines[2], CP_MAX_LINE_LEN, "(set int64 1 j n - 1)");
    snprintf(pat.variations[0].code_lines[3], CP_MAX_LINE_LEN, "(while i < j");
    snprintf(pat.variations[0].code_lines[4], CP_MAX_LINE_LEN, "  (swap arr[i] arr[j])");
    snprintf(pat.variations[0].code_lines[5], CP_MAX_LINE_LEN, "  (i++)");
    snprintf(pat.variations[0].code_lines[6], CP_MAX_LINE_LEN, "  (j--)");
    snprintf(pat.variations[0].code_lines[7], CP_MAX_LINE_LEN, ")");
    pat.variations[0].num_code_lines = 8;

    cp_add_pattern(db, &pat);
}

/* ==================== Parameter Extraction ==================== */

void cp_extract_params(const char *prompt, CPExtraction *extraction) {
    memset(extraction, 0, sizeof(CPExtraction));

    /* Extract numbers */
    const char *p = prompt;
    while (*p) {
        if (isdigit((unsigned char)*p) ||
            (*p == '-' && isdigit((unsigned char)*(p+1)))) {
            int num = atoi(p);
            if (extraction->num_numbers < 16) {
                extraction->numbers[extraction->num_numbers++] = num;

                /* Create named parameter */
                int idx = extraction->num_params;
                if (idx < CP_MAX_PARAMS) {
                    snprintf(extraction->params[idx].name, CP_MAX_PARAM_NAME,
                             "param_%d", idx);
                    snprintf(extraction->params[idx].value, CP_MAX_PARAM_VALUE,
                             "%d", num);
                    extraction->params[idx].type = CP_PARAM_INT;
                    extraction->num_params++;
                }
            }
            /* Skip number */
            p++;
            while (isdigit((unsigned char)*p)) p++;
        } else if (*p == '"') {
            /* Extract string */
            p++;
            const char *start = p;
            while (*p && *p != '"') p++;
            if (*p == '"') {
                int len = p - start;
                if (len > 0 && len < 128 && extraction->num_strings < 8) {
                    memcpy(extraction->strings[extraction->num_strings], start, len);
                    extraction->strings[extraction->num_strings][len] = '\0';

                    int idx = extraction->num_params;
                    if (idx < CP_MAX_PARAMS) {
                        snprintf(extraction->params[idx].name, CP_MAX_PARAM_NAME,
                                 "param_%d", idx);
                        snprintf(extraction->params[idx].value, CP_MAX_PARAM_VALUE,
                                 "%s", extraction->strings[extraction->num_strings]);
                        extraction->params[idx].type = CP_PARAM_STRING;
                        extraction->num_params++;
                    }
                    extraction->num_strings++;
                }
            }
        } else {
            p++;
        }
    }
}

/* ==================== Variation Selection ==================== */

int cp_find_best_variation(const CPCodePattern *pattern, const char *prompt) {
    if (pattern->num_variations <= 0) return 0;

    /* Check for keywords that indicate preferred variation */
    for (int v = 0; v < pattern->num_variations; v++) {
        const char *var_name = pattern->variations[v].name;

        if (cp_str_contains(prompt, var_name)) {
            return v;
        }
    }

    /* Check for complexity keywords */
    if (cp_str_contains(prompt, "simple") || cp_str_contains(prompt, "basic")) {
        /* Prefer simple variations */
        for (int v = 0; v < pattern->num_variations; v++) {
            if (pattern->variations[v].complexity == 0) return v;
        }
    }

    if (cp_str_contains(prompt, "fast") || cp_str_contains(prompt, "optimized")) {
        /* Prefer medium complexity (usually optimized) */
        for (int v = 0; v < pattern->num_variations; v++) {
            if (pattern->variations[v].complexity == 1) return v;
        }
    }

    if (cp_str_contains(prompt, "clean") || cp_str_contains(prompt, "readable")) {
        /* Prefer simple */
        for (int v = 0; v < pattern->num_variations; v++) {
            if (pattern->variations[v].complexity == 0) return v;
        }
    }

    /* Default: return first variation */
    return 0;
}

/* ==================== Pattern Matching ==================== */

int cp_find_by_emitter(const CPPatternDB *db, int emitter_id,
                       int *indices, int max_results) {
    int count = 0;
    for (int i = 0; i < db->num_patterns && count < max_results; i++) {
        if (db->patterns[i].emitter_id == emitter_id) {
            indices[count++] = i;
        }
    }
    return count;
}

int cp_find_best(const CPPatternDB *db, const char *prompt,
                 int transformer_emitter, CPExtraction *extraction) {
    (void)prompt;
    int indices[32];
    int count = cp_find_by_emitter(db, transformer_emitter, indices, 32);

    if (count == 0) return -1;

    /* Simple heuristic: prefer patterns with more parameters matched */
    int best_idx = indices[0];
    int best_score = 0;

    for (int i = 0; i < count; i++) {
        const CPCodePattern *pat = &db->patterns[indices[i]];
        int score = 0;

        /* Score based on parameter match */
        for (int p = 0; p < pat->num_params && p < extraction->num_params; p++) {
            if (extraction->params[p].type == pat->params[p].type) {
                score += 10;
            }
        }

        /* Score based on variations available */
        score += pat->num_variations * 3;

        /* Score based on priority */
        score += pat->priority;

        if (score > best_score) {
            best_score = score;
            best_idx = indices[i];
        }
    }

    return best_idx;
}

/* ==================== Code Generation ==================== */

int cp_generate_code_variation(const CPCodePattern *pattern, int variation_idx,
                               const CPExtraction *extraction,
                               char *output, int output_size) {
    if (variation_idx < 0 || variation_idx >= pattern->num_variations) return -1;

    const CPCodeVariation *var = &pattern->variations[variation_idx];
    output[0] = '\0';
    int pos = 0;

    for (int i = 0; i < var->num_code_lines; i++) {
        char line[CP_MAX_LINE_LEN];
        snprintf(line, sizeof(line), "%s", var->code_lines[i]);

        /* Substitute parameters */
        for (int p = 0; p < extraction->num_params; p++) {
            char placeholder[64];
            snprintf(placeholder, sizeof(placeholder), "{%s}",
                     extraction->params[p].name);

            char *ph = strstr(line, placeholder);
            if (ph) {
                /* Replace placeholder with value */
                char new_line[CP_MAX_LINE_LEN];
                int prefix_len = ph - line;
                memcpy(new_line, line, prefix_len);
                new_line[prefix_len] = '\0';
                strcat(new_line, extraction->params[p].value);
                strcat(new_line, ph + strlen(placeholder));
                snprintf(line, sizeof(line), "%s", new_line);
            }
        }

        pos += snprintf(output + pos, output_size - pos, "%s\n", line);
    }

    return 0;
}

int cp_generate_code(const CPCodePattern *pattern, const CPExtraction *extraction,
                     char *output, int output_size) {
    return cp_generate_code_variation(pattern, 0, extraction, output, output_size);
}

/* ==================== Multi-Step Generation ==================== */

int cp_plan_steps(const CPPatternDB *db, const char *prompt,
                  CPStepPlan *plan) {
    memset(plan, 0, sizeof(CPStepPlan));

    /* Simple heuristic: detect multiple operations */
    int has_sort = cp_str_contains(prompt, "sort");
    int has_search = cp_str_contains(prompt, "search") || cp_str_contains(prompt, "find");
    int has_io = cp_str_contains(prompt, "read") || cp_str_contains(prompt, "write") ||
                 cp_str_contains(prompt, "print");
    int has_calc = cp_str_contains(prompt, "calculate") || cp_str_contains(prompt, "compute") ||
                   cp_str_contains(prompt, "add") || cp_str_contains(prompt, "multiply");

    plan->num_steps = 0;

    /* Step 1: I/O operations */
    if (has_io) {
        /* Find hello_name or similar pattern */
        int indices[8];
        int count = cp_find_by_emitter(db, 16, indices, 8);
        if (count > 0) {
            plan->pattern_indices[plan->num_steps] = indices[0];
            cp_extract_params(prompt, &plan->extractions[plan->num_steps]);
            plan->num_steps++;
        }
    }

    /* Step 2: Calculation */
    if (has_calc) {
        int indices[8];
        int count = cp_find_by_emitter(db, 7, indices, 8);  /* fibonacci as example */
        if (count > 0 && plan->num_steps < CP_MAX_STEPS) {
            plan->pattern_indices[plan->num_steps] = indices[0];
            cp_extract_params(prompt, &plan->extractions[plan->num_steps]);
            plan->num_steps++;
        }
    }

    /* Step 3: Sorting */
    if (has_sort) {
        int indices[8];
        int count = cp_find_by_emitter(db, 1, indices, 8);  /* sort */
        if (count > 0 && plan->num_steps < CP_MAX_STEPS) {
            plan->pattern_indices[plan->num_steps] = indices[0];
            cp_extract_params(prompt, &plan->extractions[plan->num_steps]);
            plan->num_steps++;
        }
    }

    /* Step 4: Search */
    if (has_search) {
        int indices[8];
        int count = cp_find_by_emitter(db, 47, indices, 8);  /* search */
        if (count > 0 && plan->num_steps < CP_MAX_STEPS) {
            plan->pattern_indices[plan->num_steps] = indices[0];
            cp_extract_params(prompt, &plan->extractions[plan->num_steps]);
            plan->num_steps++;
        }
    }

    /* If no specific steps detected, try single pattern */
    if (plan->num_steps == 0) {
        cp_extract_params(prompt, &plan->extractions[0]);
        /* Will be filled by caller */
        plan->num_steps = 1;
        plan->pattern_indices[0] = -1;
    }

    return plan->num_steps;
}

int cp_generate_multi_step(const CPPatternDB *db, const CPStepPlan *plan,
                           char *output, int output_size) {
    (void)db;
    output[0] = '\0';
    int pos = 0;

    for (int s = 0; s < plan->num_steps; s++) {
        int idx = plan->pattern_indices[s];
        if (idx < 0 || idx >= db->num_patterns) continue;

        const CPCodePattern *pat = &db->patterns[idx];

        /* Add step comment */
        pos += snprintf(output + pos, output_size - pos,
                        "; Step %d: %s\n", s + 1, pat->name);

        /* Generate code for this step */
        cp_generate_code(pat, &plan->extractions[s],
                        output + pos, output_size - pos);
        pos = strlen(output);

        /* Add separator */
        pos += snprintf(output + pos, output_size - pos, "\n");
    }

    return 0;
}

/* ==================== Neural Parameter Extractor ==================== */

CPExtractor *cp_extractor_create(void) {
    CPExtractor *ext = malloc(sizeof(CPExtractor));
    if (!ext) return NULL;

    /* Simple dimensions: embed_dim=32, hidden=16, output=16 (8 params * 2 values) */
    ext->w1 = mat_create(TT_EMBED_DIM, 16);
    ext->b1 = mat_create(1, 16);
    ext->w2 = mat_create(16, 16);
    ext->b2 = mat_create(1, 16);

    if (!ext->w1 || !ext->b1 || !ext->w2 || !ext->b2) {
        cp_extractor_free(ext);
        return NULL;
    }

    /* Initialize with small random values */
    float scale1 = sqrtf(2.0f / TT_EMBED_DIM);
    float scale2 = sqrtf(2.0f / 16);

    for (int i = 0; i < ext->w1->rows * ext->w1->cols; i++)
        ext->w1->data[i] = ((float)rand() / (float)RAND_MAX - 0.5f) * scale1;
    for (int i = 0; i < ext->b1->rows * ext->b1->cols; i++)
        ext->b1->data[i] = 0.0f;
    for (int i = 0; i < ext->w2->rows * ext->w2->cols; i++)
        ext->w2->data[i] = ((float)rand() / (float)RAND_MAX - 0.5f) * scale2;
    for (int i = 0; i < ext->b2->rows * ext->b2->cols; i++)
        ext->b2->data[i] = 0.0f;

    return ext;
}

void cp_extractor_free(CPExtractor *ext) {
    if (!ext) return;
    mat_free(ext->w1);
    mat_free(ext->b1);
    mat_free(ext->w2);
    mat_free(ext->b2);
    free(ext);
}

/* ==================== Neural Extractor Forward/Backward ==================== */

static void cp_extractor_forward(const CPExtractor *ext, const Matrix *input,
                                 Matrix *output) {
    /* hidden = input * w1 + b1 */
    Matrix *hidden = mat_mul(input, ext->w1);
    /* Add bias */
    for (int i = 0; i < hidden->cols; i++) {
        hidden->data[i] += ext->b1->data[i];
    }
    /* ReLU activation */
    for (int i = 0; i < hidden->rows * hidden->cols; i++) {
        hidden->data[i] = hidden->data[i] > 0 ? hidden->data[i] : 0;
    }

    /* output = hidden * w2 + b2 */
    Matrix *out = mat_mul(hidden, ext->w2);
    for (int i = 0; i < out->cols; i++) {
        out->data[i] += ext->b2->data[i];
    }

    /* Copy to output */
    for (int i = 0; i < out->rows * out->cols; i++) {
        output->data[i] = out->data[i];
    }

    mat_free(hidden);
    mat_free(out);
}

/* ==================== Neural Extractor Training ==================== */

/* Training example for extractor */
typedef struct {
    char prompt[256];
    CPExtraction target;
} CPExtractorExample;

#define CP_MAX_EXTRACTOR_EXAMPLES 512

static CPExtractorExample extractor_examples[CP_MAX_EXTRACTOR_EXAMPLES];
static int num_extractor_examples = 0;

void cp_extractor_add_example(const char *prompt, const CPExtraction *target) {
    if (num_extractor_examples >= CP_MAX_EXTRACTOR_EXAMPLES) return;

    CPExtractorExample *ex = &extractor_examples[num_extractor_examples];
    snprintf(ex->prompt, 256, "%s", prompt);
    ex->target = *target;
    num_extractor_examples++;
}

int cp_extractor_train(CPExtractor *ext, const TTVocab *vocab,
                       int epochs, float lr) {
    (void)vocab;
    if (num_extractor_examples == 0) return -1;

    printf("Training neural extractor: %d examples, %d epochs\n",
           num_extractor_examples, epochs);

    for (int epoch = 0; epoch < epochs; epoch++) {
        float total_loss = 0;
        int correct = 0;

        for (int i = 0; i < num_extractor_examples; i++) {
            CPExtractorExample *ex = &extractor_examples[i];

            /* Create input from prompt tokens */
            Matrix *input = mat_create(1, TT_EMBED_DIM);
            const char *p = ex->prompt;
            int pos = 0;
            while (*p && pos < TT_EMBED_DIM) {
                input->data[pos] = (float)((unsigned char)*p) / 255.0f;
                p++;
                pos++;
            }

            /* Forward pass */
            Matrix *output = mat_create(1, 16);
            cp_extractor_forward(ext, input, output);

            /* Simple loss: compare with target extraction */
            /* Target: first N params are "present" (1.0), rest are 0.0 */
            float target[16] = {0};
            for (int j = 0; j < ex->target.num_params && j < 8; j++) {
                target[j * 2] = 1.0f;      /* param present */
                target[j * 2 + 1] = (float)ex->target.params[j].type; /* type */
            }

            /* Calculate loss */
            float loss = 0;
            for (int j = 0; j < 16; j++) {
                float diff = output->data[j] - target[j];
                loss += diff * diff;
            }
            total_loss += loss;

            /* Simple accuracy check */
            int predicted_params = 0;
            for (int j = 0; j < 8; j++) {
                if (output->data[j * 2] > 0.5f) predicted_params++;
            }
            if (predicted_params == ex->target.num_params) correct++;

            /* Backward pass (simplified) */
            /* Update weights using gradient descent */
            for (int j = 0; j < ext->w2->rows * ext->w2->cols; j++) {
                float grad = (output->data[j % ext->w2->cols] - target[j % ext->w2->cols]);
                ext->w2->data[j] -= lr * grad * 0.01f;
            }

            mat_free(input);
            mat_free(output);
        }

        float avg_loss = total_loss / num_extractor_examples;
        float accuracy = (float)correct / num_extractor_examples * 100.0f;
        printf("Epoch %3d/%d  loss=%.4f  accuracy=%.1f%%\n",
               epoch + 1, epochs, avg_loss, accuracy);
    }

    return 0;
}

void cp_extractor_extract(const CPExtractor *ext, const TTVocab *vocab,
                          const char *prompt, CPExtraction *extraction) {
    (void)vocab;
    /* Use neural extractor if trained, otherwise fall back to regex */
    if (num_extractor_examples > 0 && ext && ext->w1) {
        /* Create input from prompt */
        Matrix *input = mat_create(1, TT_EMBED_DIM);
        const char *p = prompt;
        int pos = 0;
        while (*p && pos < TT_EMBED_DIM) {
            input->data[pos] = (float)((unsigned char)*p) / 255.0f;
            p++;
            pos++;
        }

        /* Forward pass */
        Matrix *output = mat_create(1, 16);
        cp_extractor_forward(ext, input, output);

        /* Extract parameters from output */
        memset(extraction, 0, sizeof(CPExtraction));
        for (int j = 0; j < 8; j++) {
            if (output->data[j * 2] > 0.5f && extraction->num_params < CP_MAX_PARAMS) {
                int type = (int)(output->data[j * 2 + 1] + 0.5f);
                snprintf(extraction->params[extraction->num_params].name,
                         CP_MAX_PARAM_NAME, "param_%d", j);
                extraction->params[extraction->num_params].type = type;
                extraction->num_params++;
            }
        }

        mat_free(input);
        mat_free(output);
    } else {
        /* Fallback to regex-based extraction */
        cp_extract_params(prompt, extraction);
    }
}
