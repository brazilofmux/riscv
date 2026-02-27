#include "elf_loader.h"
#include "interp.h"
#include "dbt.h"
#include <stdio.h>
#include <string.h>

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s [options] program.elf\n", prog);
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  -i       Use interpreter (default: DBT)\n");
    fprintf(stderr, "  -s       Show stats on exit\n");
    fprintf(stderr, "  -h       Show this help\n");
    fprintf(stderr, "\nRV32IM microcontroller binary executor.\n");
    fprintf(stderr, "Accepts standard ELF32 RISC-V executables (RV32IM, no RVC).\n");
}

int main(int argc, char *argv[]) {
    int show_stats = 0;
    int use_interp = 0;
    const char *elf_file = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0) {
            use_interp = 1;
        } else if (strcmp(argv[i], "-s") == 0) {
            show_stats = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "rv32-run: unknown option '%s'\n", argv[i]);
            usage(argv[0]);
            return 1;
        } else {
            elf_file = argv[i];
        }
    }

    if (!elf_file) {
        usage(argv[0]);
        return 1;
    }

    /* Load ELF */
    rv32_binary_t bin;
    if (rv32_load_elf(elf_file, &bin) != 0) {
        return 1;
    }

    int exit_code;

    if (use_interp) {
        /* Interpreter mode */
        rv32_state_t state;
        memset(&state, 0, sizeof(state));
        state.pc = bin.entry_point;
        state.x[2] = bin.stack_top;

        exit_code = rv32_interp_run(&state, &bin);

        if (show_stats) {
            fprintf(stderr, "rv32-run: %llu instructions (interpreter)\n",
                    (unsigned long long)state.insn_count);
        }
    } else {
        /* DBT mode */
        static dbt_state_t dbt;  /* static — too large for stack */
        if (dbt_init(&dbt, &bin) != 0) {
            rv32_free_binary(&bin);
            return 1;
        }

        dbt.ctx.next_pc = bin.entry_point;
        dbt.ctx.x[2] = bin.stack_top;

        exit_code = dbt_run(&dbt);

        if (show_stats) {
            fprintf(stderr, "  blocks translated: %llu\n",
                    (unsigned long long)dbt.blocks_translated);
            fprintf(stderr, "  cache hits: %llu, misses: %llu\n",
                    (unsigned long long)dbt.cache_hits,
                    (unsigned long long)dbt.cache_misses);
            fprintf(stderr, "  code buffer used: %u bytes\n", dbt.code_used);
            if (dbt.intrinsic_memcpy || dbt.intrinsic_memset ||
                dbt.intrinsic_memmove || dbt.intrinsic_strlen) {
                fprintf(stderr, "  intrinsics:");
                if (dbt.intrinsic_memcpy)  fprintf(stderr, " memcpy@0x%x", dbt.intrinsic_memcpy);
                if (dbt.intrinsic_memmove) fprintf(stderr, " memmove@0x%x", dbt.intrinsic_memmove);
                if (dbt.intrinsic_memset)  fprintf(stderr, " memset@0x%x", dbt.intrinsic_memset);
                if (dbt.intrinsic_strlen)  fprintf(stderr, " strlen@0x%x", dbt.intrinsic_strlen);
                fprintf(stderr, "\n");
            }
        }

        dbt_cleanup(&dbt);
    }

    rv32_free_binary(&bin);
    return exit_code;
}
