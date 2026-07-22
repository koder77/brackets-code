#!/bin/bash
# Performance benchmark test for brackets-code

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BRACKETS_CODE="$SCRIPT_DIR/brackets-code"
LOG_FILE="$SCRIPT_DIR/benchmark.log"

echo "=========================================="
echo "Brackets-Code Performance Benchmark"
echo "=========================================="
echo ""

# Configuration
TARGET_BLOCKS=20
echo "Target: $TARGET_BLOCKS blocks"
echo ""

# Run code generation
echo "Running performance test..."
"$BRACKETS_CODE" "$TARGET_BLOCKS"

# Calculate metrics
echo ""
echo "=========================================="
echo "Results"
echo "=========================================="

# Extract time from output
GEN_TIME=$(echo "$@" | grep "^[0-9.]*")

if [ -z "$GEN_TIME" ]; then
    echo "ERROR: Could not extract generation time"
    exit 1
fi

echo "Generated: $TARGET_BLOCKS blocks"
echo "Time: ${GEN_TIME}s"
echo "Blocks/sec: $(echo "$GEN_TIME" | awk '{print int(1000/$GEN_TIME)}')"
echo "Target: ~${TARGET_BLOCKS*1000/20}ms/block"

# Check if we met the target
if [ $(echo "$GEN_TIME <= 15000" | bc -l) -eq 1 ]; then
    echo ""
    echo "✓ PERFORMANCE TARGET MET!"
    echo "✓ Code generation is within acceptable range"
    exit 0
else
    echo ""
    echo "✗ PERFORMANCE TARGET NOT MET"
    exit 1
fi
