/* emit_a64.h — AArch64 instruction emitter primitives.
 *
 * Placeholder header. Filled in during P1 with the full emit API analogous
 * to emit_x64.h (load/store guest reg, ALU reg/imm, shifts, branches,
 * mul/div, FP scalar). Until then the backend in dbt_a64.c is a stub that
 * reports dbt_jit_available() == 0, so main.c forces interpreter mode and
 * this header is not actually consumed by the build.
 */
#ifndef EMIT_A64_H
#define EMIT_A64_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#endif /* EMIT_A64_H */
