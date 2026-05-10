#!/bin/bash
# Apples-to-apples bench against qemu-riscv32 user-mode emulator.
#
# qemu-user-mode only runs on Linux, so on macOS we run BOTH rv32-run and
# qemu-riscv32 inside a podman container — same VM overhead on both, so
# the ratio is meaningful even from an Apple Silicon host.
#
# Usage from the repo root:
#   podman run --rm -v "$PWD:/riscv" ubuntu:latest bash /riscv/bench-vs-qemu.sh
#
# Requires podman (or docker; substitute the runner). The container
# installs build-essential + qemu-user, builds rv32-run for its native
# host arch, and times the same set of guest workloads on each.
set -e
echo "=== Setup ==="
apt-get -qq update >/dev/null 2>&1
DEBIAN_FRONTEND=noninteractive apt-get -qq install -y build-essential qemu-user time >/dev/null 2>&1
qemu-riscv32 --version 2>&1 | head -1
gcc --version | head -1

echo "=== Build rv32-run ==="
cd /riscv/dbt
make clean >/dev/null 2>&1
make 2>&1 | tail -2

echo
echo "=== Sanity: outputs match? ==="
cd /riscv/lisp/tests
diff <(/riscv/dbt/rv32-run ../lisp.elf < 16-minint.lisp 2>/dev/null) \
     <(qemu-riscv32          ../lisp.elf < 16-minint.lisp 2>/dev/null) \
   && echo "  match"

# Time a command silently, print "<label> <secs> s"
run() {
    local label="$1"; shift
    /usr/bin/time -f "%e" -o /tmp/t "$@" >/dev/null 2>/dev/null
    printf "  %-30s  %s s\n" "$label" "$(cat /tmp/t)"
}

# Like run, but takes a shell command (so we can sequence multiple invocations).
runsh() {
    local label="$1"; shift
    /usr/bin/time -f "%e" -o /tmp/t bash -c "$1" >/dev/null 2>/dev/null
    printf "  %-30s  %s s\n" "$label" "$(cat /tmp/t)"
}

echo
echo "=== lisp 17-stress (CPU-bound interpreter loop) ==="
cd /riscv/lisp/tests
for i in 1 2 3; do
    runsh "rv32-run #$i" '/riscv/dbt/rv32-run ../lisp.elf < 17-stress.lisp'
done
for i in 1 2 3; do
    runsh "qemu     #$i" 'qemu-riscv32          ../lisp.elf < 17-stress.lisp'
done

echo
echo "=== lua full sweep (11 tests, mixed) ==="
cd /riscv/lua/tests
for i in 1 2 3; do
    runsh "rv32-run #$i" 'for t in *.lua; do /riscv/dbt/rv32-run ../lua.elf < "$t" > /dev/null; done'
done
for i in 1 2 3; do
    runsh "qemu     #$i" 'for t in *.lua; do qemu-riscv32          ../lua.elf < "$t" > /dev/null; done'
done

echo
echo "=== startup latency: empty.lua x N ==="
cd /riscv/lua/tests
runsh "rv32-run x100"  'for i in $(seq 100); do /riscv/dbt/rv32-run ../lua.elf < empty.lua > /dev/null; done'
runsh "qemu     x100"  'for i in $(seq 100); do qemu-riscv32          ../lua.elf < empty.lua > /dev/null; done'

echo
echo "=== sbasic stress (interpreter, integer + string) ==="
cd /riscv/sbasic/tests
runsh "rv32-run sbasic suite" 'EMU=/riscv/dbt/rv32-run bash ./run-tests.sh'
runsh "qemu     sbasic suite" 'EMU=qemu-riscv32          bash ./run-tests.sh'

echo
echo "=== dbase test sweep (102 tests, I/O-heavy) ==="
cd /riscv/dbase/tests
runsh "rv32-run dbase 102 tests" 'EMU=/riscv/dbt/rv32-run bash ./run-tests.sh'
runsh "qemu     dbase 102 tests" 'EMU=qemu-riscv32          bash ./run-tests.sh'
