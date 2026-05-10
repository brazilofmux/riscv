/*
 * stage00 / rv32-emu — minimal RV32IMFD interpreter, the trust root.
 *
 * A single translation unit that drops the pure C reference interpreter
 * out of the dbt/ tree (no JIT, no shadow, no MAP_JIT plumbing) into a
 * standalone binary. Builds with stock C11 + POSIX:
 *
 *     cc -O2 -std=c11 selfhost/stage00/rv32-emu.c -o rv32-emu
 *
 * The dbt/ sources are #include'd by relative path so this file stays
 * the only thing you need to hand to a compiler — the .c files referenced
 * here have no JIT dependency. The Makefile alongside swaps in a symlink
 * to `dbt/rv32-run` when one exists, so the same `rv32-emu` filename
 * reaches the fast path automatically once the JIT is built.
 */

#include "../../dbt/elf_loader.c"
#include "../../dbt/interp.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Linux-ABI argv layout: same as dbt/main.c (kept here independently so
 * stage00 has no link to that translation unit). */
static uint32_t setup_guest_args(rv32_binary_t *bin, int argc, char **argv) {
    uint32_t sp = bin->stack_top;
    if (argc == 0) {
        sp -= 12;
        sp &= ~15u;
        uint32_t zero = 0;
        memcpy(bin->memory + sp,     &zero, 4);
        memcpy(bin->memory + sp + 4, &zero, 4);
        memcpy(bin->memory + sp + 8, &zero, 4);
        return sp;
    }

    uint32_t str_addrs[128];
    if (argc > 128) argc = 128;
    for (int i = argc - 1; i >= 0; i--) {
        size_t len = strlen(argv[i]) + 1;
        sp -= (uint32_t)len;
        memcpy(bin->memory + sp, argv[i], len);
        str_addrs[i] = sp;
    }
    sp &= ~3u;
    uint32_t frame_words = 1u + (uint32_t)argc + 1u + 1u;
    sp -= frame_words * 4;
    sp &= ~15u;
    uint32_t argc32 = (uint32_t)argc;
    memcpy(bin->memory + sp, &argc32, 4);
    for (int i = 0; i < argc; i++) {
        uint32_t a = str_addrs[i];
        memcpy(bin->memory + sp + 4 + i * 4, &a, 4);
    }
    uint32_t z = 0;
    memcpy(bin->memory + sp + 4 + argc * 4, &z, 4);
    memcpy(bin->memory + sp + 4 + (argc + 1) * 4, &z, 4);
    return sp;
}

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s program.elf [guest-args...]\n"
        "\n"
        "Stage-0 RV32IMFD interpreter (trust root). No JIT — slow but\n"
        "auditable in ~1500 lines of C across this file plus dbt/interp.c\n"
        "and dbt/elf_loader.c. The Makefile prefers dbt/rv32-run when\n"
        "available; this binary is the fallback.\n",
        prog);
}

int main(int argc, char **argv) {
    if (argc < 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        usage(argv[0]);
        return argc < 2 ? 1 : 0;
    }

    rv32_binary_t bin;
    if (rv32_load_elf(argv[1], &bin) != 0)
        return 1;

    int guest_argc = argc - 1;
    char **guest_argv = argv + 1;
    uint32_t sp = setup_guest_args(&bin, guest_argc, guest_argv);

    rv32_state_t state;
    memset(&state, 0, sizeof(state));
    state.pc = bin.entry_point;
    state.x[2] = sp;

    int rc = rv32_interp_run(&state, &bin);
    rv32_free_binary(&bin);
    return rc;
}
