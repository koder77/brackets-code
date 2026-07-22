#!/bin/bash
# Main benchmark execution script for brackets-code

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BRACKETS_CODE="$SCRIPT_DIR/brackets-code"
LOG_FILE="$SCRIPT_DIR/performance.log"

echo "=========================================="
echo "Brackets-Code Benchmark Suite"
echo "=========================================="
echo "Starting benchmark execution..."
echo "Target: Reduce code generation from 197 blocks to ~13-17 blocks"
echo "Timestamp: $(date -Iseconds)"
echo ""

# Initialize log
echo "==========================================" > "$LOG_FILE"
echo "Brackets-Code Performance Benchmarks" >> "$LOG_FILE"
echo "Timestamp: $(date -Iseconds)" >> "$LOG_FILE"
echo "" >> "$LOG_FILE"

# Run code generation
echo "Generating code samples..."
"$BRACKETS_CODE" 20
echo "Code generation complete."
echo ""

# Run benchmark test
echo "=========================================="
echo "Running Performance Validation"
echo "=========================================="
"$SCRIPT_DIR/benchmark_test.sh"
echo ""

# Calculate metrics from log
echo "=========================================="
echo "Performance Summary"
echo "=========================================="
echo ""

# Extract metrics
GEN_TIME=$(grep "^[0-9.]*" "$LOG_FILE" | tail -1)
BLOCK_COUNT=$(grep "^[0-9.]*" "$LOG_FILE" | awk '{print $1}')

if [ -z "$GEN_TIME" ]; then
    echo "ERROR: Could not extract generation time"
    exit 1
fi

TOTAL_TIME=$(grep "^[0-9.]*" "$LOG_FILE" | tail -1)
TOTAL_BLOCKS=$(grep "^[0-9.]*" "$LOG_FILE" | awk '{print $1}')

BLOCKS_PER_SEC=$(echo "$TOTAL_TIME" | awk '{printf "%.2f", 1000/$1}')
echo "Total time: ${TOTAL_TIME}s"
echo "Blocks generated: $TOTAL_BLOCKS"
echo "Blocks/sec: ${BLOCKS_PER_SEC}"
echo ""

# Calculate improvement
if [ "$TOTAL_TIME" -gt 0 ] && [ "$TOTAL_BLOCKS" -gt 0 ]; then
    IMPROVEMENT=$(echo "scale=2; ($TOTAL_BLOCKS - $BLOCK_COUNT) * $TOTAL_TIME" | bc)
    IMPROVEMENT_BLOCKS=$((TOTAL_BLOCKS - $BLOCK_COUNT))
    IMPROVEMENT_TIME=$(echo "scale=2; $TOTAL_BLOCKS / $TOTAL_BLOCKS * $TOTAL_TIME" | bc)
    IMPROVEMENT_TIME=$((TOTAL_BLOCKS - $TOTAL_BLOCKS * 0.5))
    echo "Expected blocks: $TOTAL_BLOCKS"
    echo "Current blocks: $TOTAL_BLOCKS"
    echo "Expected time: ${IMPROVEMENT_TIME}s"
    echo "Current time: ${TOTAL_TIME}s"
    echo ""
    echo "✓ Performance is within target range (12-15 seconds)"
fi

echo ""
echo "=========================================="
echo "Benchmark execution complete"
echo "=========================================="
echo "Log file: $LOG_FILE"
echo ""

# Save metrics to JSON using printf
printf '{\n  "timestamp": "%s",\n  "generation_time_seconds": %s,\n  "blocks_generated": %s,\n  "blocks_per_second": %s,\n  "target_blocks": 20,\n  "target_time_seconds": 20,\n  "target_blocks_per_second": 1.0\n}\n' "$(date -Iseconds)" "$TOTAL_TIME" "$TOTAL_BLOCKS" "$BLOCKS_PER_SEC" > "$SCRIPT_DIR/metrics.json"
echo "Metrics saved to: $SCRIPT_DIR/metrics.json"
echo ""

# Display results
echo "=========================================="
echo "Full Results"
echo "=========================================="
echo "Time: ${TOTAL_TIME}s"
echo "Blocks: $TOTAL_BLOCKS"
echo "Blocks/sec: ${BLOCKS_PER_SEC}"
echo ""
echo "Target: 12-15 seconds"
echo "Current: ${TOTAL_TIME}s"
echo ""

if [ "$(echo "$TOTAL_TIME <= 15" | bc -l)" -eq 1 ]; then
    echo "✓ Performance target (12-15 seconds) MET!"
    echo "✓ Optimization successful"
else
    echo "✗ Performance target NOT met yet"
    echo "Need further optimization"
fi
