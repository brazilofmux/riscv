#include "elf_loader.h"
#include "interp.h"
#include "dbt.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>

/* Terminal state preservation for crash recovery */
static struct termios orig_termios;
static int termios_saved = 0;

static void restore_terminal(void) {
    if (termios_saved)
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

static void signal_handler(int sig) {
    /* Only async-signal-safe functions here */
    if (termios_saved)
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    /* Re-raise to get default behavior (core dump, exit, etc.) */
    signal(sig, SIG_DFL);
    raise(sig);
}

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s [options] program.elf [guest-args...]\n", prog);
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  -i       Use interpreter (default: DBT)\n");
    fprintf(stderr, "  -s       Show stats on exit\n");
    fprintf(stderr, "  -h       Show this help\n");
    fprintf(stderr, "\nRV32IM microcontroller binary executor.\n");
    fprintf(stderr, "Accepts standard ELF32 RISC-V executables (RV32IM, no RVC).\n");
    fprintf(stderr, "Arguments after the ELF file are passed to the guest program.\n");
}

/*
 * Set up argc/argv on the guest stack.  Layout (growing downward):
 *
 *   [arg strings, packed]     <-- string area
 *   [argv[argc] = NULL]
 *   [argv[argc-1]]           <-- pointers into string area (guest addrs)
 *   ...
 *   [argv[0]]                <-- argv pointer (guest addr)
 *                             <-- new SP (16-byte aligned)
 *
 * Returns the new stack_top, sets *out_argc and *out_argv (guest addresses).
 */
static uint32_t setup_guest_args(rv32_binary_t *bin, int guest_argc,
                                  char **guest_argv, uint32_t *out_argc,
                                  uint32_t *out_argv)
{
    uint32_t sp = bin->stack_top;

    if (guest_argc == 0) {
        *out_argc = 0;
        *out_argv = 0;
        return sp;
    }

    /* Write arg strings to top of stack, track their guest addresses */
    uint32_t str_addrs[128];  /* max 128 guest args */
    if (guest_argc > 128) guest_argc = 128;

    for (int i = guest_argc - 1; i >= 0; i--) {
        size_t len = strlen(guest_argv[i]) + 1;  /* include NUL */
        sp -= len;
        memcpy(bin->memory + sp, guest_argv[i], len);
        str_addrs[i] = sp;
    }

    /* Align to 4 bytes */
    sp &= ~3u;

    /* Write argv array (NULL-terminated) */
    sp -= (guest_argc + 1) * 4;
    uint32_t argv_base = sp;
    for (int i = 0; i < guest_argc; i++) {
        uint32_t addr = str_addrs[i];
        memcpy(bin->memory + argv_base + i * 4, &addr, 4);
    }
    uint32_t null_val = 0;
    memcpy(bin->memory + argv_base + guest_argc * 4, &null_val, 4);

    /* 16-byte align SP */
    sp &= ~15u;

    *out_argc = guest_argc;
    *out_argv = argv_base;
    return sp;
}

int main(int argc, char *argv[]) {
    int show_stats = 0;
    int use_interp = 0;
    const char *elf_file = NULL;
    int elf_arg_index = 0;

    int trace = 0;
    for (int i = 1; i < argc; i++) {
        if (elf_file) {
            /* Everything after the ELF file is a guest arg — stop parsing */
            break;
        }
        if (strcmp(argv[i], "-i") == 0) {
            use_interp = 1;
        } else if (strcmp(argv[i], "-s") == 0) {
            show_stats = 1;
        } else if (strcmp(argv[i], "-t") == 0) {
            trace = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "rv32-run: unknown option '%s'\n", argv[i]);
            usage(argv[0]);
            return 1;
        } else {
            elf_file = argv[i];
            elf_arg_index = i;
        }
    }

    if (!elf_file) {
        usage(argv[0]);
        return 1;
    }

    /* Guest args: argv[elf_arg_index .. argc-1] */
    int guest_argc = argc - elf_arg_index;
    char **guest_argv = argv + elf_arg_index;

    /* Load ELF */
    rv32_binary_t bin;
    if (rv32_load_elf(elf_file, &bin) != 0) {
        return 1;
    }

    /* Set up guest argc/argv on the guest stack */
    uint32_t g_argc, g_argv;
    uint32_t new_sp = setup_guest_args(&bin, guest_argc, guest_argv, &g_argc, &g_argv);

    /* Save terminal state for crash recovery */
    if (tcgetattr(STDIN_FILENO, &orig_termios) == 0) {
        termios_saved = 1;
        atexit(restore_terminal);
        struct sigaction sa;
        sa.sa_handler = signal_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGINT,  &sa, NULL);
        sigaction(SIGTERM, &sa, NULL);
        sigaction(SIGQUIT, &sa, NULL);
        sigaction(SIGSEGV, &sa, NULL);
        sigaction(SIGABRT, &sa, NULL);
    }

    int exit_code;

    if (use_interp) {
        /* Interpreter mode */
        rv32_state_t state;
        memset(&state, 0, sizeof(state));
        state.pc = bin.entry_point;
        state.x[2] = new_sp;
        state.x[10] = g_argc;   /* a0 = argc */
        state.x[11] = g_argv;   /* a1 = argv */

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
        dbt.ctx.x[2] = new_sp;
        dbt.ctx.x[10] = g_argc;   /* a0 = argc */
        dbt.ctx.x[11] = g_argv;   /* a1 = argv */

        dbt.trace = trace;
        exit_code = dbt_run(&dbt);

        if (show_stats) {
            fprintf(stderr, "  blocks translated: %llu\n",
                    (unsigned long long)dbt.blocks_translated);
            fprintf(stderr, "  cache hits: %llu, misses: %llu\n",
                    (unsigned long long)dbt.cache_hits,
                    (unsigned long long)dbt.cache_misses);
            fprintf(stderr, "  code buffer used: %u bytes\n", dbt.code_used);
            fprintf(stderr, "  superblocks: %llu (%llu side exits, %llu diamond merges)\n",
                    (unsigned long long)dbt.superblock_count,
                    (unsigned long long)dbt.side_exits_total,
                    (unsigned long long)dbt.diamond_merges);
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
