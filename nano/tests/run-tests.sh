#!/bin/bash
# nano test runner for RV32IM
# Usage: bash nano/tests/run-tests.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
NANO_DIR="$(dirname "$SCRIPT_DIR")"
ROOT_DIR="$(dirname "$NANO_DIR")"
RUNNER="$ROOT_DIR/dbt/rv32-run"
BINARY="$NANO_DIR/nano.elf"

if [ ! -f "$BINARY" ]; then
    echo "ERROR: $BINARY not found. Run 'make -C nano' first."
    exit 1
fi

if [ ! -f "$RUNNER" ]; then
    echo "ERROR: $RUNNER not found. Run 'make -C dbt' first."
    exit 1
fi

echo "=== nano unit tests ==="

# Run built-in unit tests (--test flag, no terminal needed)
"$RUNNER" "$BINARY" --test
