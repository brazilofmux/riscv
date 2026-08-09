/* CSR (fcsr/fflags/frm) semantics — including the csrrw rd==rs1 swap,
 * which regressed in the JIT backends when rd was written before rs1
 * was read. The interpreter is the reference. */
#include <stdio.h>
#include <stdint.h>

static int failures;

static void check(const char *name, uint32_t got, uint32_t want) {
    if (got == want) {
        printf("PASS %s\n", name);
    } else {
        printf("FAIL %s: got 0x%02x want 0x%02x\n", name, got, want);
        failures++;
    }
}

int main(void) {
    uint32_t v;

    /* csrw / csrr roundtrip on fcsr */
    v = 0x2A;
    __asm__ volatile("csrw fcsr, %0" :: "r"(v));
    __asm__ volatile("csrr %0, fcsr" : "=r"(v));
    check("fcsr roundtrip", v, 0x2A);

    /* fflags is fcsr[4:0] */
    __asm__ volatile("csrr %0, fflags" : "=r"(v));
    check("fflags field", v, 0x0A);

    /* frm is fcsr[7:5] */
    __asm__ volatile("csrr %0, frm" : "=r"(v));
    check("frm field", v, 0x1);

    /* csrrw rd==rs1: swap. t0=0x15 in, old fcsr (0x2A) out, fcsr=0x15. */
    {
        register uint32_t t __asm__("t0") = 0x15;
        __asm__ volatile("csrrw t0, fcsr, t0" : "+r"(t));
        check("csrrw swap old", t, 0x2A);
        __asm__ volatile("csrr %0, fcsr" : "=r"(v));
        check("csrrw swap new", v, 0x15);
    }

    /* csrrs: set bits, return old */
    {
        uint32_t bits = 0x02, old;
        __asm__ volatile("csrrs %0, fcsr, %1" : "=r"(old) : "r"(bits));
        check("csrrs old", old, 0x15);
        __asm__ volatile("csrr %0, fcsr" : "=r"(v));
        check("csrrs new", v, 0x17);
    }

    /* csrrc: clear bits, return old */
    {
        uint32_t bits = 0x05, old;
        __asm__ volatile("csrrc %0, fcsr, %1" : "=r"(old) : "r"(bits));
        check("csrrc old", old, 0x17);
        __asm__ volatile("csrr %0, fcsr" : "=r"(v));
        check("csrrc new", v, 0x12);
    }

    /* csrrs with rs1=x0 must not write */
    {
        uint32_t old;
        __asm__ volatile("csrrs %0, fcsr, x0" : "=r"(old));
        check("csrrs x0 read", old, 0x12);
        __asm__ volatile("csrr %0, fcsr" : "=r"(v));
        check("csrrs x0 nowrite", v, 0x12);
    }

    /* Immediate forms */
    {
        uint32_t old;
        __asm__ volatile("csrrwi %0, fflags, 0x1F" : "=r"(old));
        check("csrrwi old", old, 0x12);
        __asm__ volatile("csrrci %0, fflags, 0x0C" : "=r"(old));
        check("csrrci old", old, 0x1F);
        __asm__ volatile("csrrsi %0, fflags, 0x01" : "=r"(old));
        check("csrrsi old", old, 0x13);
        __asm__ volatile("csrr %0, fcsr" : "=r"(v));
        check("imm forms fcsr", v, 0x13);
    }

    /* frm write leaves fflags alone */
    {
        uint32_t three = 3;
        __asm__ volatile("csrw frm, %0" :: "r"(three));
        __asm__ volatile("csrr %0, fcsr" : "=r"(v));
        check("frm write merged", v, 0x73);
        __asm__ volatile("csrr %0, fflags" : "=r"(v));
        check("fflags preserved", v, 0x13);
    }

    if (failures == 0)
        printf("=== test-csr: all pass ===\n");
    else
        printf("=== test-csr: %d FAILURES ===\n", failures);
    return failures ? 1 : 0;
}
