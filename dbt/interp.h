#ifndef INTERP_H
#define INTERP_H

#include "elf_loader.h"

/* Interpreter state */
typedef struct {
    uint32_t x[32];     /* general-purpose registers (x[0] always 0) */
    uint32_t pc;        /* program counter */
    uint64_t insn_count;/* instructions executed */
} rv32_state_t;

/*
 * Run the interpreter until exit.
 * Returns the exit code (value in a0 at ECALL exit).
 */
int rv32_interp_run(rv32_state_t *state, rv32_binary_t *bin);

#endif /* INTERP_H */
