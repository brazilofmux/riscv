#!/bin/bash
# Lisp test runner for RV32IM
# Usage: bash lisp/tests/run-tests.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
LISP_DIR="$(dirname "$SCRIPT_DIR")"
ROOT_DIR="$(dirname "$LISP_DIR")"
RUNNER="$ROOT_DIR/dbt/rv32-run"
BINARY="$LISP_DIR/lisp.elf"

if [ ! -f "$BINARY" ]; then
    echo "ERROR: $BINARY not found. Run 'make -C lisp' first."
    exit 1
fi

if [ ! -f "$RUNNER" ]; then
    echo "ERROR: $RUNNER not found. Run 'make -C dbt' first."
    exit 1
fi

PASS=0
FAIL=0
ERRORS=""

for testfile in "$SCRIPT_DIR"/*.lisp; do
    testname=$(basename "$testfile" .lisp)
    expected="$SCRIPT_DIR/$testname.expected"

    if [ ! -f "$expected" ]; then
        echo "  SKIP  $testname (no .expected file)"
        continue
    fi

    # Run test: pipe .lisp file to rv32-run lisp.elf, capture stdout
    actual=$("$RUNNER" "$BINARY" < "$testfile" 2>/dev/null) || true

    expected_content=$(cat "$expected")

    if [ "$actual" = "$expected_content" ]; then
        echo "  PASS  $testname"
        PASS=$((PASS + 1))
    else
        echo "  FAIL  $testname"
        FAIL=$((FAIL + 1))
        ERRORS="$ERRORS\n--- $testname ---\nExpected:\n$expected_content\nActual:\n$actual\n"
    fi
done

echo ""
echo "Results: $PASS passed, $FAIL failed out of $((PASS + FAIL)) tests"

if [ $FAIL -gt 0 ]; then
    echo ""
    echo "Failures:"
    echo -e "$ERRORS"
    exit 1
fi
