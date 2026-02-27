#!/bin/bash
# RV32IM Runtime Regression Tests
# Runs each test-*.elf under the DBT, compares stdout against expected.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
EMULATOR="$ROOT_DIR/dbt/rv32-run"

# Allow override: run-tests.sh -i for interpreter mode
MODE=""
if [ "$1" = "-i" ]; then
    MODE="-i"
fi

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

TOTAL=0
PASSED=0
FAILED=0

echo "RV32IM Runtime Regression Tests"
echo "================================"
echo ""

for test_elf in "$SCRIPT_DIR"/test-*.elf; do
    [ -f "$test_elf" ] || continue
    test_name=$(basename "$test_elf" .elf)
    expected="$SCRIPT_DIR/expected/${test_name}.expected"

    if [ ! -f "$expected" ]; then
        printf "%-30s ${RED}SKIP${NC} (no expected file)\n" "$test_name:"
        continue
    fi

    TOTAL=$((TOTAL + 1))

    actual=$(timeout 5 "$EMULATOR" $MODE "$test_elf" 2>/dev/null || true)
    expected_content=$(cat "$expected")

    if [ "$actual" = "$expected_content" ]; then
        printf "%-30s ${GREEN}PASS${NC}\n" "$test_name:"
        PASSED=$((PASSED + 1))
    else
        printf "%-30s ${RED}FAIL${NC}\n" "$test_name:"
        FAILED=$((FAILED + 1))
        diff --color=auto <(echo "$actual") <(echo "$expected_content") | head -20
        echo ""
    fi
done

echo ""
echo "================================"
echo "Results: $PASSED/$TOTAL passed, $FAILED failed"
if [ $FAILED -eq 0 ]; then
    echo -e "${GREEN}All tests passed!${NC}"
else
    echo -e "${RED}Some tests failed.${NC}"
    exit 1
fi
