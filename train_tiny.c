/*
 * This file train_tiny.c is part of L1vm.
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

/* train_tiny.c — Standalone training tool for the tiny transformer. */
/* Reads DSL files, trains a classification model, saves weights. */

#include "tiny_transformer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void usage(const char *prog) {
    printf("Usage: %s [options]\n", prog);
    printf("Options:\n");
    printf("  --dsl-dir <path>     DSL directory (default: dsl/)\n");
    printf("  --model <path>       Output model file (default: tiny_model.tiny)\n");
    printf("  --epochs <n>         Training epochs (default: 50)\n");
    printf("  --lr <float>         Learning rate (default: 0.01)\n");
    printf("  --predict <prompt>   Predict emitter for a prompt (requires trained model)\n");
    printf("  --help               Show this help\n");
}

static const char *emitter_names[] = {
    "math","input_loop","loop","for_sum","print_even","find_max",
    "countdown","fib_seq","input_sort","median","string_cat",
    "string_compare","array_assign","array_reverse","array_find",
    "input_fact","array_vmath","read_file","write_file","string_to_num",
    "timer","factorial","fizzbuzz","primes","even_odd","power",
    "mult_table","guess","gcd","hello_name","random","array_min_max",
    "bool_demo","bit_check","fann_create","fann_train","fann_run",
    "average","selection_sort","palindrome","lcm","collatz",
    "sum_of_digits","reverse_string","armstrong","perfect_number",
    "count_vowels","anagram_check","string_to_upper","string_to_lower",
    "caesar_cipher","palindrome_string","bubble_sort","binary_search",
    "square_root","prime_factorization","standard_deviation",
    "compound_interest","decimal_to_binary","dice_roll","double_math",
    "double_circle_area","double_average","double_compound_interest",
    "double_pythagoras","double_temp_convert","double_sqrt","function",
    "string_length","stack","queue","insertion_sort","calculator",
    "unit_converter","rock_paper_scissors","pyramid","temp_converter_menu",
    "sort_stats","string_analyzer","number_analyzer","filter_numbers",
    "random_generator","math_menu","quiz_game","bmi_calculator",
    "statistics_suite","linked_list","binary_search_tree","tree_traversal",
    "graph_bfs_dfs","n_queens","sudoku","levenshtein","maze_generator",
    "maze_solver","monte_carlo","matrix_mul","matrix_transpose",
    "numerical_integration","complex_numbers","linear_regression",
    "base_converter","freq_analysis","shuffle","weighted_random",
    "ascii_table","bignum_math","password_card","chess_problem",
    "shell_repl","webserver","sdl_window","sdl_button","thread",
    "scheduler","shell_exec","json","crypto","bluetooth_ble",
    "serial_rs232","gpio","gps","timer_date","sdl_sound","sdl_joystick",
    "sdl_mouse","fractal","cluster_3x1","reload","coordinate_grid",
    "turmite","crossword","linter","double_power","double_volume_sphere",
    "double_discount","double_simple_interest","double_bmi",
    "double_standard_deviation","double_kinetic_energy","hello_world",
    "string_find","string_split","switch_demo","type_convert",
    "iterative_factorial","random_walk","bar_chart","hanoi_tower",
    "ascii_art","number_to_words","temperature_table","loop_demo",
    "pointer","struct","hex_binary","shell_args","time"
};
#define NUM_EMITTER_NAMES (sizeof(emitter_names) / sizeof(emitter_names[0]))

int main(int argc, char **argv) {
    const char *dsl_dir = "dsl";
    const char *model_path = "tiny_model.tiny";
    int epochs = 50;
    float lr = 0.01f;
    const char *predict_prompt = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--dsl-dir") == 0 && i + 1 < argc) {
            dsl_dir = argv[++i];
        } else if (strcmp(argv[i], "--model") == 0 && i + 1 < argc) {
            model_path = argv[++i];
        } else if (strcmp(argv[i], "--epochs") == 0 && i + 1 < argc) {
            epochs = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--lr") == 0 && i + 1 < argc) {
            lr = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--predict") == 0 && i + 1 < argc) {
            predict_prompt = argv[++i];
        } else {
            printf("Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    /* Predict mode */
    if (predict_prompt) {
        printf("Loading model from %s...\n", model_path);
        if (tt_init(model_path) != 0) {
            /* tt_init returns 1 if no model found, but still initializes */
        }
        float score = 0;
        int emitter = tt_predict(predict_prompt, &score);
        if (emitter >= 0 && emitter < (int)NUM_EMITTER_NAMES) {
            printf("Prompt: \"%s\"\n", predict_prompt);
            printf("Predicted emitter: %s (confidence: %.2f%%)\n",
                   emitter_names[emitter], score * 100.0f);
        } else {
            printf("No prediction available for: \"%s\"\n", predict_prompt);
        }
        return 0;
    }

    /* Training mode */
    printf("=== Tiny Transformer Trainer ===\n");
    printf("DSL directory: %s\n", dsl_dir);
    printf("Model output:  %s\n", model_path);
    printf("Epochs:        %d\n", epochs);
    printf("Learning rate: %.4f\n", lr);
    printf("\n");

    int result = tt_train(dsl_dir, model_path, epochs, lr);

    if (result == 0) {
        printf("\nTraining complete! Model saved to %s\n", model_path);
        printf("Use --predict \"your prompt\" to test.\n");
    } else {
        printf("\nTraining failed!\n");
    }

    return result;
}
