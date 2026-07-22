#!/bin/bash
# test_sort_algorithms.sh - Test all sort algorithms
# Tests bubble sort (ascending/descending), selection sort, and insertion sort

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
L1VM_ROOT="${L1VM_ROOT:-$HOME/l1vm/}"
L1VM_BIN="${L1VM_ROOT}bin"
INCLUDE_DIR="${L1VM_ROOT}include/"
PATH="$L1VM_BIN:$PATH"

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

PASS=0
FAIL=0

run_test() {
    local name="$1"
    local file="$2"
    local expected="$3"
    
    echo -n "  Testing $name ... "
    
    # Preprocess
    if ! l1pre "$file" "$TMPDIR/${name}_pp.l1com" "$INCLUDE_DIR" 2>/dev/null; then
        echo "FAIL (preprocess)"
        FAIL=$((FAIL + 1))
        return
    fi
    
    # Compile
    if ! l1com "$TMPDIR/${name}_pp.l1com" 2>/dev/null; then
        echo "FAIL (compile)"
        FAIL=$((FAIL + 1))
        return
    fi
    
    # Run and capture output
    local output
    output=$(l1vm "$TMPDIR/${name}_pp" 2>&1 | grep -E "^[0-9]+$" | tr '\n' ' ')
    
    # Compare with expected
    if [ "$output" = "$expected" ]; then
        echo "PASS (got: $output)"
        PASS=$((PASS + 1))
    else
        echo "FAIL (expected: $expected, got: $output)"
        FAIL=$((FAIL + 1))
    fi
}

echo "========================================"
echo "  Sort Algorithm Tests"
echo "========================================"
echo ""

echo "--- Bubble Sort Ascending ---"
run_test "bubble_asc" "$SCRIPT_DIR/test_bubble_sort_asc.l1com" "1 2 3 4 5 "

echo ""
echo "--- Bubble Sort Descending ---"
run_test "bubble_desc" "$SCRIPT_DIR/test_bubble_sort_desc.l1com" "5 4 3 2 1 "

echo ""
echo "--- Selection Sort Ascending ---"
run_test "selection_asc" "$SCRIPT_DIR/test_selection_sort.l1com" "1 2 3 4 5 "

echo ""
echo "--- Insertion Sort Ascending ---"
run_test "insertion_asc" "$SCRIPT_DIR/test_insertion_sort.l1com" "1 2 3 4 5 "

echo ""
echo "========================================"
echo "  Results: $PASS passed, $FAIL failed"
echo "========================================"

[ "$FAIL" -eq 0 ]
