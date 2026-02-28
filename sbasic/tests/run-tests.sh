#!/bin/bash
# SLOW BASIC test runner for RV32IM
# Usage: bash sbasic/tests/run-tests.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SBASIC_DIR="$(dirname "$SCRIPT_DIR")"
ROOT_DIR="$(dirname "$SBASIC_DIR")"
RUNNER="$ROOT_DIR/dbt/rv32-run"
BINARY="$SBASIC_DIR/sbasic.elf"

# Tests to skip (need filesystem ops we don't support yet)
SKIP="compat5"

if [ ! -f "$BINARY" ]; then
    echo "ERROR: $BINARY not found. Run 'make -C sbasic' first."
    exit 1
fi

if [ ! -f "$RUNNER" ]; then
    echo "ERROR: $RUNNER not found. Run 'make -C dbt' first."
    exit 1
fi

PASS=0
FAIL=0
SKIPPED=0
ERRORS=""

for testfile in "$SCRIPT_DIR"/*.bas; do
    testname=$(basename "$testfile" .bas)
    expected="$SCRIPT_DIR/$testname.expected"

    if [ ! -f "$expected" ]; then
        continue
    fi

    # Check skip list
    skip_this=0
    for s in $SKIP; do
        if [ "$testname" = "$s" ]; then
            skip_this=1
            break
        fi
    done
    if [ "$skip_this" = "1" ]; then
        echo "  SKIP  $testname"
        SKIPPED=$((SKIPPED + 1))
        continue
    fi

    # Run test: feed program + RUN command, run from sbasic dir for $INCLUDE paths
    # Merge stderr (LPRINT goes there) and filter out rv32-run loader lines
    actual=$( (cat "$testfile"; echo "RUN") | (cd "$SBASIC_DIR" && "$RUNNER" "$BINARY") 2>&1 | grep -v "^rv32-run:\|^  entry:\|^  code:\|^  data:\|^  stack:\|^  symbols:" ) || true

    expected_content=$(cat "$expected")

    if [ "$actual" = "$expected_content" ]; then
        echo "  PASS  $testname"
        PASS=$((PASS + 1))
    else
        echo "  FAIL  $testname"
        FAIL=$((FAIL + 1))
        ERRORS="$ERRORS\n--- $testname ---\nExpected:\n$(echo "$expected_content" | head -5)\nActual:\n$(echo "$actual" | head -5)\n"
    fi
done

echo ""
echo "Results: $PASS passed, $FAIL failed, $SKIPPED skipped out of $((PASS + FAIL + SKIPPED)) tests"

if [ $FAIL -gt 0 ]; then
    echo ""
    echo "Failures:"
    echo -e "$ERRORS"
    exit 1
fi
