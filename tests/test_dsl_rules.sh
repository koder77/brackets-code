#!/bin/bash
# test_dsl_rules.sh - Test pipeline for .l1DSL rules
# Tests each DSL rule by generating, preprocessing, and compiling

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
L1VM_ROOT="${L1VM_ROOT:-$HOME/l1vm/}"
INCLUDE_DIR="${L1VM_ROOT}/include/"
PATH="${L1VM_ROOT}bin:${PATH}"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

PASSED=0
FAILED=0
SKIPPED=0
TMP=$(mktemp -d)
trap "rm -rf $TMP" EXIT

declare -A KNOWN_FAILURES
KNOWN_FAILURES[chess-problem]="missing label :mp_math_init (external L1VM lib)"
KNOWN_FAILURES[fann-create]="missing label :fann_create (external L1VM lib)"
KNOWN_FAILURES[fann-run]="missing label :fann_run (external L1VM lib)"
KNOWN_FAILURES[fann-train]="missing label :fann_train (external L1VM lib)"
KNOWN_FAILURES[read-file]="missing label :fopen (external L1VM lib)"
KNOWN_FAILURES[write-file]="missing label :fopen (external L1VM lib)"

declare -A SKIP_TESTS
SKIP_TESTS[input-loop]="interactive"
SKIP_TESTS[input-sort]="interactive"
SKIP_TESTS[input-factorial]="interactive"
SKIP_TESTS[shell-repl]="interactive"
SKIP_TESTS[quiz-game]="interactive"
SKIP_TESTS[guess-number]="interactive"
SKIP_TESTS[math-menu]="interactive"
SKIP_TESTS[user-input]="interactive"
SKIP_TESTS[calculator]="interactive"
SKIP_TESTS[base-converter]="interactive"
SKIP_TESTS[unit-converter]="interactive"
SKIP_TESTS[dice-roll]="interactive"
SKIP_TESTS[shuffle]="interactive"

test_dsl_rule() {
    local dsl_file="$1"
    local rule_name
    rule_name=$(basename "$dsl_file" .l1dsl)

    # Check skip
    for skip in "${!SKIP_TESTS[@]}"; do
        if [[ "$rule_name" == *"$skip"* ]]; then
            echo -e "  [${YELLOW}SKIP${NC}] $rule_name (${SKIP_TESTS[$skip]})"
            SKIPPED=$((SKIPPED + 1))
            return
        fi
    done

    # Check known failure
    local known_fail=""
    for kf in "${!KNOWN_FAILURES[@]}"; do
        if [[ "$rule_name" == *"$kf"* ]]; then
            known_fail="${KNOWN_FAILURES[$kf]}"
            break
        fi
    done

    # Extract prompt from parser line
    local parser_line
    parser_line=$(grep "^parser:" "$dsl_file" | head -1)
    local prompt
    prompt=$(echo "$parser_line" | sed 's/parser: "//; s/"//' | cut -d',' -f1 | xargs 2>/dev/null)

    if [[ -z "$prompt" ]]; then
        echo -e "  [${YELLOW}SKIP${NC}] $rule_name (no parser keywords)"
        SKIPPED=$((SKIPPED + 1))
        return
    fi

    # Generate
    local outfile="$TMP/${rule_name}_test.l1com"
    "$PROJECT_DIR/brackets-code" "$prompt" "$outfile" > /dev/null 2>&1
    local gen_ok=$?

    # Preprocess
    local pp_file="$TMP/${rule_name}_pp.l1com"
    l1pre "$outfile" "$pp_file" "$INCLUDE_DIR" > /dev/null 2>&1
    local pre_ok=$?

    # Compile (non-interactive: pipe /dev/null to stdin)
    timeout 3 l1com "$TMP/${rule_name}_pp" < /dev/null > /dev/null 2>&1
    local com_ok=$?

    if [[ $com_ok -eq 0 ]]; then
        echo -e "  [${GREEN}OK${NC}] $rule_name"
        PASSED=$((PASSED + 1))
    elif [[ -n "$known_fail" ]]; then
        echo -e "  [${YELLOW}FAIL${NC}] $rule_name (known: $known_fail)"
        FAILED=$((FAILED + 1))
    else
        echo -e "  [${RED}FAIL${NC}] $rule_name"
        FAILED=$((FAILED + 1))
    fi
}

echo "========================================"
echo "  DSL Rule Test Pipeline"
echo "========================================"
echo ""
echo "Testing $(ls dsl/*.l1dsl | wc -l) DSL rules..."
echo ""

for dsl_file in "$PROJECT_DIR"/dsl/*.l1dsl; do
    test_dsl_rule "$dsl_file"
done

echo ""
echo "========================================"
echo "  Results: $PASSED passed, $FAILED failed, $SKIPPED skipped"
echo "========================================"

[ "$FAILED" -eq 0 ]
