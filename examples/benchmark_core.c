// CPU-bound benchmark kernels for RV32IM.
//
// Build:
//   riscv64-unknown-elf-gcc -march=rv32im -mabi=ilp32 -nostdlib \
//       -T runtime/link.ld -O2 examples/benchmark_core.c -o examples/benchmark_core.elf
//
// Run:
//   time ./dbt/rv32-run -s examples/benchmark_core.elf

#include <stdint.h>

#ifndef BENCH_ITERS
#define BENCH_ITERS 100000000u
#endif

#define NOINLINE __attribute__((noinline))

/* Bare-metal I/O via ECALL */
static inline long sys_write(int fd, const void *buf, unsigned long len) {
    register long a0 __asm__("a0") = fd;
    register long a1 __asm__("a1") = (long)buf;
    register long a2 __asm__("a2") = (long)len;
    register long a7 __asm__("a7") = 64;
    __asm__ volatile ("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7) : "memory");
    return a0;
}

static inline void sys_exit(int code) {
    register long a0 __asm__("a0") = code;
    register long a7 __asm__("a7") = 93;
    __asm__ volatile ("ecall" : : "r"(a0), "r"(a7));
    __builtin_unreachable();
}

static void print_str(const char *s) {
    const char *p = s;
    while (*p) p++;
    sys_write(1, s, (unsigned)(p - s));
}

static void print_hex(uint32_t val) {
    char buf[11];
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 9; i >= 2; i--) {
        int nib = val & 0xF;
        buf[i] = nib < 10 ? '0' + nib : 'a' + nib - 10;
        val >>= 4;
    }
    buf[10] = '\n';
    sys_write(1, buf, 11);
}

/* Benchmark kernels — identical to SLOW-32 */
static uint32_t NOINLINE bench_arith(uint32_t iterations) {
    uint32_t acc = 0x12345678u;
    uint32_t x = 0x9e3779b9u;

    for (uint32_t i = 0; i < iterations; ++i) {
        acc += x;
        acc ^= acc << 7;
        acc ^= acc >> 5;
        x += 0x3c6ef372u;
    }

    return acc ^ x;
}

static uint32_t NOINLINE bench_branch(uint32_t iterations) {
    uint32_t acc = 0;
    uint32_t toggle = 0;

    for (uint32_t i = 0; i < iterations; ++i) {
        if ((toggle & 1u) == 0) {
            acc += i ^ (toggle << 3);
        } else {
            acc ^= i + (toggle << 1);
        }

        if ((acc & 0x10u) == 0) {
            acc = (acc << 1) | 1u;
        }

        toggle = (toggle + 3u) & 0x7u;
    }

    return acc ^ toggle;
}

static uint32_t NOINLINE bench_mem(uint32_t iterations) {
    uint32_t buf[64];
    for (uint32_t i = 0; i < 64; ++i) {
        buf[i] = i * 0x01010101u;
    }

    uint32_t acc = 0;
    uint32_t idx = 0;

    for (uint32_t i = 0; i < iterations; ++i) {
        idx = (idx + 7u) & 63u;
        acc ^= buf[idx] + i;
        buf[idx] = acc ^ (i << 4);
    }

    for (uint32_t i = 0; i < 64; ++i) {
        acc ^= buf[i];
    }

    return acc;
}

void _start(void) {
    const uint32_t arith_iters = BENCH_ITERS;
    const uint32_t branch_iters = BENCH_ITERS;
    const uint32_t mem_iters = (BENCH_ITERS / 4u) + 1u;

    uint32_t arith = bench_arith(arith_iters);
    uint32_t branch = bench_branch(branch_iters);
    uint32_t mem = bench_mem(mem_iters);

    uint32_t checksum = arith ^ branch ^ mem ^ BENCH_ITERS;

    print_str("arith:  "); print_hex(arith);
    print_str("branch: "); print_hex(branch);
    print_str("mem:    "); print_hex(mem);
    print_str("check:  "); print_hex(checksum);

    sys_exit((int)(checksum & 0xFF));
}
