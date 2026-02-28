#!/bin/bash
# dBASE III clone test suite for RV32IM
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DBASE_DIR="$(dirname "$SCRIPT_DIR")"
ROOT_DIR="$(dirname "$DBASE_DIR")"
RUNNER="$ROOT_DIR/dbt/rv32-run"
BINARY="$DBASE_DIR/dbase.elf"

if [ ! -f "$BINARY" ]; then
    echo "Error: dbase.elf not found. Run 'make -C dbase' first."
    exit 1
fi

if [ ! -f "$RUNNER" ]; then
    echo "Error: rv32-run not found. Run 'make -C dbt' first."
    exit 1
fi

PASS=0
FAIL=0
TOTAL=0

# Clean up any leftover files from previous runs
rm -f "$SCRIPT_DIR"/*.DBF "$SCRIPT_DIR"/*.DBT "$SCRIPT_DIR"/*.FRM \
    "$SCRIPT_DIR"/*.LBL "$SCRIPT_DIR"/*.NDX \
    "$SCRIPT_DIR"/testfile.txt "$SCRIPT_DIR"/ALT*.TXT

for testfile in "$SCRIPT_DIR"/*.txt; do
    [ -f "$testfile" ] || continue
    name=$(basename "$testfile" .txt)
    expected="$SCRIPT_DIR/expected/$name.expected"
    TOTAL=$((TOTAL + 1))

    # Clean files before each test
    rm -f "$SCRIPT_DIR"/*.DBF "$SCRIPT_DIR"/*.DBT "$SCRIPT_DIR"/*.FRM \
        "$SCRIPT_DIR"/*.LBL "$SCRIPT_DIR"/*.NDX \
        "$SCRIPT_DIR"/testfile.txt

    # Run test from tests/ directory so .DBF files are created there
    # Strip trailing dot-prompt lines left by the main loop after stdin EOF
    # (SLOW-32's emulator filter incidentally removed these; rv32-run doesn't)
    actual=$( cd "$SCRIPT_DIR" && cat "$testfile" | "$RUNNER" "$BINARY" 2>/dev/null \
        | awk '{ lines[NR]=$0 } END { n=NR; while(n>0 && lines[n] ~ /^[. ]*$/) n--; for(i=1;i<=n;i++) print lines[i] }' ) || true

    if [ -f "$expected" ]; then
        exp=$(cat "$expected")
        if [ "$actual" = "$exp" ]; then
            echo "  PASS: $name"
            PASS=$((PASS + 1))
        else
            echo "  FAIL: $name"
            echo "    Expected:"
            echo "$exp" | head -5 | sed 's/^/      /'
            echo "    Got:"
            echo "$actual" | head -5 | sed 's/^/      /'
            FAIL=$((FAIL + 1))
        fi
    else
        # No expected file — just check it doesn't error
        if echo "$actual" | grep -qi "^error"; then
            echo "  FAIL: $name (error in output)"
            echo "$actual" | grep -i "error" | head -3 | sed 's/^/      /'
            FAIL=$((FAIL + 1))
        else
            echo "  PASS: $name (no .expected file, no errors)"
            PASS=$((PASS + 1))
        fi
    fi
done

# Final cleanup
rm -f "$SCRIPT_DIR"/*.DBF "$SCRIPT_DIR"/*.DBT "$SCRIPT_DIR"/*.FRM \
    "$SCRIPT_DIR"/*.LBL "$SCRIPT_DIR"/*.NDX \
    "$SCRIPT_DIR"/testfile.txt "$SCRIPT_DIR"/ALT*.TXT

echo ""
echo "$PASS/$TOTAL passed, $FAIL failed"
exit $FAIL
