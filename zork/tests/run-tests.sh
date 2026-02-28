#!/bin/bash
# MojoZork test runner for RV32IM
# Usage: bash zork/tests/run-tests.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ZORK_DIR="$(dirname "$SCRIPT_DIR")"
ROOT_DIR="$(dirname "$ZORK_DIR")"
RUNNER="$ROOT_DIR/dbt/rv32-run"
BINARY="$ZORK_DIR/zork.elf"
STORY="$ZORK_DIR/stories/minizork.z3"

if [ ! -f "$BINARY" ]; then
    echo "ERROR: $BINARY not found. Run 'make -C zork' first."
    exit 1
fi

if [ ! -f "$RUNNER" ]; then
    echo "ERROR: $RUNNER not found. Run 'make -C dbt' first."
    exit 1
fi

if [ ! -f "$STORY" ]; then
    echo "ERROR: $STORY not found."
    exit 1
fi

PASS=0
FAIL=0
TOTAL=0

run_test() {
    local name="$1"
    local input="$2"
    local expected_file="$SCRIPT_DIR/$name.expected"
    TOTAL=$((TOTAL + 1))

    if [ ! -f "$expected_file" ]; then
        echo "  SKIP  $name (no expected file)"
        return
    fi

    local actual
    actual=$(printf '%s' "$input" | "$RUNNER" "$BINARY" "$STORY" 2>/dev/null) || true

    local expected
    expected=$(cat "$expected_file")

    if [ "$actual" = "$expected" ]; then
        echo "  PASS  $name"
        PASS=$((PASS + 1))
    else
        echo "  FAIL  $name"
        FAIL=$((FAIL + 1))
        echo "  Expected:"
        echo "$expected" | head -5 | sed 's/^/    /'
        echo "  Got:"
        echo "$actual" | head -5 | sed 's/^/    /'
    fi
}

# Test 1: Startup and quit
run_test "startup" "$(printf 'quit\ny\n')"

# Test 2: Look around
run_test "look" "$(printf 'look\nquit\ny\n')"

# Test 3: Explore
run_test "explore" "$(printf 'open mailbox\nread leaflet\ngo north\nquit\ny\n')"

echo ""
echo "Results: $PASS/$TOTAL passed, $FAIL failed"

if [ $FAIL -gt 0 ]; then
    exit 1
fi
