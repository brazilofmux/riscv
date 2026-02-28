#include "dbt.h"
#include "decoder.h"
#include "emit_x64.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <poll.h>

/* Max guest instructions per translated block */
#define MAX_BLOCK_INSNS  64

/* ---- Block cache ---- */

static inline uint32_t cache_hash(uint32_t pc) {
    return (pc >> 2) & BLOCK_CACHE_MASK;
}

static block_entry_t *cache_lookup(dbt_state_t *dbt, uint32_t pc) {
    block_entry_t *e = &dbt->cache[cache_hash(pc)];
    if (e->guest_pc == pc) {
        dbt->cache_hits++;
        return e;
    }
    dbt->cache_misses++;
    return NULL;
}

static void cache_insert(dbt_state_t *dbt, uint32_t pc, uint8_t *code) {
    block_entry_t *e = &dbt->cache[cache_hash(pc)];
    e->guest_pc = pc;
    e->native_code = code;
}

/* ---- ECALL handler ---- */

static int handle_ecall(dbt_state_t *dbt) {
    rv32_ctx_t *ctx = &dbt->ctx;
    uint32_t syscall_num = ctx->x[17];

    switch (syscall_num) {
    case 93:  /* exit */
        return (int)(int32_t)ctx->x[10];

    case 64:  /* write(fd, buf, len) */
        {
            uint32_t fd  = ctx->x[10];
            uint32_t buf = ctx->x[11];
            uint32_t len = ctx->x[12];
            if (buf + len > dbt->bin->memory_size) {
                ctx->x[10] = (uint32_t)-1;
                break;
            }
            ssize_t written = write(fd, dbt->bin->memory + buf, len);
            ctx->x[10] = (uint32_t)(int32_t)written;
        }
        break;

    case 63:  /* read(fd, buf, len) */
        {
            uint32_t fd  = ctx->x[10];
            uint32_t buf = ctx->x[11];
            uint32_t len = ctx->x[12];
            if (buf + len > dbt->bin->memory_size) {
                ctx->x[10] = (uint32_t)-1;
                break;
            }
            ssize_t nread = read(fd, dbt->bin->memory + buf, len);
            ctx->x[10] = (uint32_t)(int32_t)nread;
        }
        break;

    case 56:  /* openat(dirfd, pathname, flags, mode) */
        {
            int32_t dirfd = (int32_t)ctx->x[10];
            uint32_t path_addr = ctx->x[11];
            int flags = (int)ctx->x[12];
            int mode = (int)ctx->x[13];
            if (path_addr >= dbt->bin->memory_size) {
                ctx->x[10] = (uint32_t)-1;
                break;
            }
            const char *pathname = (const char *)(dbt->bin->memory + path_addr);
            int result = openat(dirfd, pathname, flags, mode);
            ctx->x[10] = (uint32_t)(int32_t)result;
        }
        break;

    case 57:  /* close(fd) */
        {
            int fd = (int)ctx->x[10];
            /* Don't close stdin/stdout/stderr */
            if (fd <= 2) { ctx->x[10] = 0; break; }
            int result = close(fd);
            ctx->x[10] = (uint32_t)(int32_t)result;
        }
        break;

    case 62:  /* lseek(fd, offset, whence) */
        {
            int fd = (int)ctx->x[10];
            off_t offset = (off_t)(int32_t)ctx->x[11];
            int whence = (int)ctx->x[12];
            off_t result = lseek(fd, offset, whence);
            ctx->x[10] = (uint32_t)(int32_t)result;
        }
        break;

    case 35:  /* unlinkat(dirfd, pathname, flags) */
        {
            uint32_t path_addr = ctx->x[11];
            if (path_addr >= dbt->bin->memory_size) {
                ctx->x[10] = (uint32_t)-1;
                break;
            }
            const char *pathname = (const char *)(dbt->bin->memory + path_addr);
            int result = unlink(pathname);
            ctx->x[10] = (uint32_t)(int32_t)result;
        }
        break;

    case 80:  /* fstat — stub, return -1 */
        ctx->x[10] = (uint32_t)-1;
        break;

    case 214: /* brk — not needed, return 0 */
        ctx->x[10] = 0;
        break;

    case 403: /* clock_gettime(clockid, tp_addr) */
        {
            uint32_t tp_addr = ctx->x[11];
            if (tp_addr + 8 > dbt->bin->memory_size) {
                ctx->x[10] = (uint32_t)-1;
                break;
            }
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            uint32_t sec = (uint32_t)ts.tv_sec;
            uint32_t nsec = (uint32_t)ts.tv_nsec;
            memcpy(dbt->bin->memory + tp_addr, &sec, 4);
            memcpy(dbt->bin->memory + tp_addr + 4, &nsec, 4);
            ctx->x[10] = 0;
        }
        break;

    case 404: /* get_cpu_clock() — returns host clock() value */
        ctx->x[10] = (uint32_t)clock();
        break;

    case 500: /* term_setraw(mode) — set terminal raw/cooked mode */
        {
            int mode = (int)ctx->x[10];
            struct termios t;
            if (tcgetattr(STDIN_FILENO, &t) < 0) {
                ctx->x[10] = (uint32_t)-1;
                break;
            }
            if (mode) {
                t.c_iflag &= ~(unsigned)(ICRNL | IXON | BRKINT | INPCK | ISTRIP);
                t.c_oflag &= ~(unsigned)(OPOST);
                t.c_lflag &= ~(unsigned)(ICANON | ECHO | ISIG | IEXTEN);
                t.c_cflag |= CS8;
                t.c_cc[VMIN] = 1;
                t.c_cc[VTIME] = 0;
            } else {
                t.c_iflag |= ICRNL | IXON;
                t.c_oflag |= OPOST;
                t.c_lflag |= ICANON | ECHO | ISIG | IEXTEN;
            }
            ctx->x[10] = (tcsetattr(STDIN_FILENO, TCSAFLUSH, &t) == 0) ? 0 : (uint32_t)-1;
        }
        break;

    case 501: /* term_getsize(buf) — get terminal dimensions */
        {
            uint32_t buf_addr = ctx->x[10];
            if (buf_addr + 8 > dbt->bin->memory_size) {
                ctx->x[10] = (uint32_t)-1;
                break;
            }
            struct winsize ws;
            if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
                uint32_t rows = ws.ws_row;
                uint32_t cols = ws.ws_col;
                memcpy(dbt->bin->memory + buf_addr, &rows, 4);
                memcpy(dbt->bin->memory + buf_addr + 4, &cols, 4);
                ctx->x[10] = 0;
            } else {
                ctx->x[10] = (uint32_t)-1;
            }
        }
        break;

    case 502: /* term_kbhit() — non-blocking key check */
        {
            struct pollfd pfd;
            pfd.fd = STDIN_FILENO;
            pfd.events = POLLIN;
            int ret = poll(&pfd, 1, 0);
            ctx->x[10] = (ret > 0 && (pfd.revents & POLLIN)) ? 1 : 0;
        }
        break;

    default:
        fprintf(stderr, "rv32-run: unhandled ecall %u at PC=0x%08X\n",
                syscall_num, ctx->next_pc - 4);
        return -1;
    }

    return -2;
}

/* ---- Chained exit: inline cache probe ---- */

/*
 * Emit a chained exit for a KNOWN target PC (direct branches, JAL).
 * R13 = cache base.
 *
 *   mov eax, hash(target_pc) * 16    ; cache entry offset
 *   cmp dword [r13 + rax], target_pc ; check guest_pc
 *   jne miss
 *   jmp [r13 + rax + 8]              ; jump to native code
 * miss:
 *   mov dword [rbx + CTX_NEXT_PC_OFF], target_pc
 *   ret
 */
static void emit_exit_chained(emit_t *e, uint32_t target_pc) {
    uint32_t offset = cache_hash(target_pc) * 16;

    /* mov eax, offset */
    emit_mov_r32_imm32(e, X64_RAX, offset);

    /* lea rcx, [r13 + rax] — compute entry pointer
     * Note: R13 (reg 5) as SIB base with mod=00 means [disp32 + index],
     * NOT [R13 + index].  Must use mod=01 with disp8=0. */
    emit_byte(e, rex(1, reg_hi(X64_RCX), reg_hi(X64_RAX), reg_hi(X64_R13)));
    emit_byte(e, 0x8D);  /* LEA */
    emit_byte(e, modrm(0x01, X64_RCX, 0x04));  /* SIB follows, mod=01 for R13 */
    emit_byte(e, (uint8_t)((reg_lo(X64_RAX) << 3) | reg_lo(X64_R13)));  /* SIB: base=R13, index=RAX */
    emit_byte(e, 0);  /* disp8=0 */

    /* cmp dword [rcx], target_pc */
    emit_byte(e, 0x81);
    emit_byte(e, modrm(0x00, 7, X64_RCX));
    emit_u32(e, target_pc);

    /* jne miss */
    emit_byte(e, 0x75);
    uint32_t miss_patch = emit_pos(e);
    emit_byte(e, 0);  /* rel8 placeholder */

    /* jmp [rcx + 8] (native_code pointer) */
    emit_byte(e, 0xFF);
    emit_byte(e, modrm(0x01, 4, X64_RCX));  /* jmp [rcx+disp8] */
    emit_byte(e, 8);

    /* miss: store next_pc and return to dispatch */
    e->buf[miss_patch] = (uint8_t)(emit_pos(e) - miss_patch - 1);
    emit_exit_with_pc(e, target_pc);
}

/*
 * Emit a chained exit for an INDIRECT target (JALR).
 * Target PC is in EAX at runtime. R13 = cache base.
 *
 *   mov [rbx + CTX_NEXT_PC_OFF], eax  ; always store (needed for ecall check)
 *   mov ecx, eax
 *   shr ecx, 2
 *   and ecx, CACHE_MASK
 *   shl rcx, 4                        ; * 16 byte entries
 *   cmp dword [r13 + rcx], eax
 *   jne miss
 *   jmp [r13 + rcx + 8]
 * miss:
 *   ret
 */
static void emit_exit_indirect(emit_t *e) {
    /* Store target PC (always needed for ecall check) */
    emit_byte(e, 0x89);  /* mov [rbx+disp32], eax */
    emit_byte(e, modrm(0x02, X64_RAX, X64_RBX));
    emit_u32(e, CTX_NEXT_PC_OFF);

    /* Hash: ecx = (eax >> 2) & MASK */
    emit_mov_rr(e, X64_RCX, X64_RAX);
    emit_shr_r_imm(e, X64_RCX, 2);
    emit_and_r_imm(e, X64_RCX, BLOCK_CACHE_MASK);

    /* Scale: rcx *= 16 (shl rcx, 4) — need 64-bit for pointer math */
    emit_byte(e, rex(1, 0, 0, 0));
    emit_byte(e, 0xC1);
    emit_byte(e, modrm(0x03, 4, X64_RCX));
    emit_byte(e, 4);

    /* lea rdx, [r13 + rcx] — same R13 mod=01 workaround */
    emit_byte(e, rex(1, reg_hi(X64_RDX), reg_hi(X64_RCX), reg_hi(X64_R13)));
    emit_byte(e, 0x8D);
    emit_byte(e, modrm(0x01, X64_RDX, 0x04));  /* mod=01 for R13 base */
    emit_byte(e, (uint8_t)((reg_lo(X64_RCX) << 3) | reg_lo(X64_R13)));
    emit_byte(e, 0);  /* disp8=0 */

    /* cmp [rdx], eax */
    emit_byte(e, 0x39);
    emit_byte(e, modrm(0x00, X64_RAX, X64_RDX));

    /* jne miss */
    emit_byte(e, 0x75);
    uint32_t miss_patch = emit_pos(e);
    emit_byte(e, 0);

    /* jmp [rdx + 8] */
    emit_byte(e, 0xFF);
    emit_byte(e, modrm(0x01, 4, X64_RDX));
    emit_byte(e, 8);

    /* miss: ret */
    e->buf[miss_patch] = (uint8_t)(emit_pos(e) - miss_patch - 1);
    emit_ret(e);
}

/* ---- RAS (Return Address Stack) ---- */

/*
 * Emit RAS push: ras[ras_top] = return_pc; ras_top = (ras_top + 1) & RAS_MASK
 * Uses ECX as scratch (not a register-cache host reg).
 */
static void emit_ras_push(emit_t *e, uint32_t return_pc) {
    /* mov ecx, [rbx + CTX_RAS_TOP_OFF] */
    emit_byte(e, 0x8B);
    emit_byte(e, modrm(0x02, X64_RCX, X64_RBX));
    emit_u32(e, CTX_RAS_TOP_OFF);

    /* mov dword [rbx + rcx*4 + CTX_RAS_OFF], return_pc */
    emit_byte(e, 0xC7);
    emit_byte(e, 0x84);  /* ModRM: mod=10, /0, rm=100(SIB) */
    emit_byte(e, 0x8B);  /* SIB: scale=10(4x), index=001(ECX), base=011(EBX) */
    emit_u32(e, CTX_RAS_OFF);
    emit_u32(e, return_pc);

    /* inc ecx */
    emit_byte(e, 0xFF);
    emit_byte(e, modrm(0x03, 0, X64_RCX));

    /* and ecx, RAS_MASK */
    emit_and_r_imm(e, X64_RCX, RAS_MASK);

    /* mov [rbx + CTX_RAS_TOP_OFF], ecx */
    emit_byte(e, 0x89);
    emit_byte(e, modrm(0x02, X64_RCX, X64_RBX));
    emit_u32(e, CTX_RAS_TOP_OFF);
}

/*
 * Emit RAS-predicted return exit.
 * EAX = actual target PC (already computed).
 * Pops RAS prediction, compares with actual target.
 * On hit: inline cache probe (skipping next_pc store).
 * On miss: falls through to full emit_exit_indirect.
 */
static void emit_exit_ras_return(emit_t *e) {
    /* RAS pop: ras_top = (ras_top - 1) & RAS_MASK */

    /* mov ecx, [rbx + CTX_RAS_TOP_OFF] */
    emit_byte(e, 0x8B);
    emit_byte(e, modrm(0x02, X64_RCX, X64_RBX));
    emit_u32(e, CTX_RAS_TOP_OFF);

    /* dec ecx */
    emit_byte(e, 0xFF);
    emit_byte(e, modrm(0x03, 1, X64_RCX));

    /* and ecx, RAS_MASK */
    emit_and_r_imm(e, X64_RCX, RAS_MASK);

    /* mov [rbx + CTX_RAS_TOP_OFF], ecx */
    emit_byte(e, 0x89);
    emit_byte(e, modrm(0x02, X64_RCX, X64_RBX));
    emit_u32(e, CTX_RAS_TOP_OFF);

    /* cmp eax, [rbx + rcx*4 + CTX_RAS_OFF] */
    emit_byte(e, 0x3B);
    emit_byte(e, 0x84);  /* ModRM: mod=10, reg=000(EAX), rm=100(SIB) */
    emit_byte(e, 0x8B);  /* SIB: scale=10(4x), index=001(ECX), base=011(EBX) */
    emit_u32(e, CTX_RAS_OFF);

    /* jne ras_miss (rel32) */
    emit_byte(e, 0x0F);
    emit_byte(e, 0x85);
    uint32_t miss_patch = emit_pos(e);
    emit_u32(e, 0);

    /* --- RAS HIT: inline cache probe without storing next_pc --- */

    /* Hash: ecx = (eax >> 2) & MASK */
    emit_mov_rr(e, X64_RCX, X64_RAX);
    emit_shr_r_imm(e, X64_RCX, 2);
    emit_and_r_imm(e, X64_RCX, BLOCK_CACHE_MASK);

    /* Scale: shl rcx, 4 (REX.W for 64-bit) */
    emit_byte(e, rex(1, 0, 0, 0));
    emit_byte(e, 0xC1);
    emit_byte(e, modrm(0x03, 4, X64_RCX));
    emit_byte(e, 4);

    /* lea rdx, [r13 + rcx] (R13 mod=01 workaround) */
    emit_byte(e, rex(1, reg_hi(X64_RDX), reg_hi(X64_RCX), reg_hi(X64_R13)));
    emit_byte(e, 0x8D);
    emit_byte(e, modrm(0x01, X64_RDX, 0x04));
    emit_byte(e, (uint8_t)((reg_lo(X64_RCX) << 3) | reg_lo(X64_R13)));
    emit_byte(e, 0);

    /* cmp [rdx], eax */
    emit_byte(e, 0x39);
    emit_byte(e, modrm(0x00, X64_RAX, X64_RDX));

    /* jne hit_cache_miss (rel8) */
    emit_byte(e, 0x75);
    uint32_t hcm_patch = emit_pos(e);
    emit_byte(e, 0);

    /* Cache HIT: jmp [rdx + 8] */
    emit_byte(e, 0xFF);
    emit_byte(e, modrm(0x01, 4, X64_RDX));
    emit_byte(e, 8);

    /* hit_cache_miss: store next_pc and return to dispatch */
    e->buf[hcm_patch] = (uint8_t)(emit_pos(e) - hcm_patch - 1);
    emit_byte(e, 0x89);  /* mov [rbx + CTX_NEXT_PC_OFF], eax */
    emit_byte(e, modrm(0x02, X64_RAX, X64_RBX));
    emit_u32(e, CTX_NEXT_PC_OFF);
    emit_ret(e);

    /* --- RAS MISS: full emit_exit_indirect --- */
    emit_patch_rel32(e, miss_patch, emit_pos(e));
    emit_exit_indirect(e);
}

/* ---- Register cache ---- */

/*
 * Within a translated block, guest registers are cached in host registers.
 * Loads from cached registers become register-to-register moves (or nothing).
 * Stores write to the cache; actual memory writeback is deferred to block exit.
 *
 * Available cache registers (not used by emitter or reserved):
 *   RSI, RDI, R8, R9, R10, R11, R14, R15
 * Reserved: RBX=ctx, R12=mem, R13=cache, RAX/RCX/RDX=scratch, RSP/RBP=stack
 */

#define RC_NUM_SLOTS  8

static const int rc_host_regs[RC_NUM_SLOTS] = {
    X64_RSI, X64_RDI, X64_R8, X64_R9,
    X64_R10, X64_R11, X64_R14, X64_R15
};

typedef struct {
    int guest_reg;  /* -1 = free */
    int dirty;
    int last_use;
} rc_slot_t;

typedef struct {
    rc_slot_t slots[RC_NUM_SLOTS];
    int clock;
} reg_cache_t;

static void rc_init(reg_cache_t *rc) {
    for (int i = 0; i < RC_NUM_SLOTS; i++) {
        rc->slots[i].guest_reg = -1;
        rc->slots[i].dirty = 0;
        rc->slots[i].last_use = 0;
    }
    rc->clock = 0;
}

static int rc_find(reg_cache_t *rc, int guest_reg) {
    for (int i = 0; i < RC_NUM_SLOTS; i++)
        if (rc->slots[i].guest_reg == guest_reg) return i;
    return -1;
}

static int rc_alloc(reg_cache_t *rc, emit_t *e) {
    for (int i = 0; i < RC_NUM_SLOTS; i++)
        if (rc->slots[i].guest_reg == -1) return i;
    /* Evict LRU */
    int lru = 0;
    for (int i = 1; i < RC_NUM_SLOTS; i++)
        if (rc->slots[i].last_use < rc->slots[lru].last_use) lru = i;
    if (rc->slots[lru].dirty)
        emit_store_guest(e, rc->slots[lru].guest_reg, rc_host_regs[lru]);
    rc->slots[lru].guest_reg = -1;
    rc->slots[lru].dirty = 0;
    return lru;
}

/* Return host register holding guest_reg's value.  Loads from memory on miss. */
static int rc_read(emit_t *e, reg_cache_t *rc, int guest_reg) {
    if (guest_reg == 0) {
        /* x0 is always zero — use RAX as scratch, caller must consume immediately */
        emit_xor_rr(e, X64_RAX, X64_RAX);
        return X64_RAX;
    }
    int slot = rc_find(rc, guest_reg);
    if (slot >= 0) {
        rc->slots[slot].last_use = ++rc->clock;
        return rc_host_regs[slot];
    }
    slot = rc_alloc(rc, e);
    emit_load_guest(e, rc_host_regs[slot], guest_reg);
    rc->slots[slot].guest_reg = guest_reg;
    rc->slots[slot].dirty = 0;
    rc->slots[slot].last_use = ++rc->clock;
    return rc_host_regs[slot];
}

/* Return host register allocated for writing guest_reg.  Marks dirty. */
static int rc_write(emit_t *e, reg_cache_t *rc, int guest_reg) {
    if (guest_reg == 0) return X64_RAX; /* writes to x0 are discarded */
    int slot = rc_find(rc, guest_reg);
    if (slot < 0) slot = rc_alloc(rc, e);
    rc->slots[slot].guest_reg = guest_reg;
    rc->slots[slot].dirty = 1;
    rc->slots[slot].last_use = ++rc->clock;
    return rc_host_regs[slot];
}

/* Legacy wrappers for instructions that need values in specific registers (mul/div). */
static void rc_load(emit_t *e, reg_cache_t *rc, int host_dst, int guest_reg) {
    int hr = rc_read(e, rc, guest_reg);
    emit_mov_rr(e, host_dst, hr);
}

static void rc_store(emit_t *e, reg_cache_t *rc, int guest_reg, int host_src) {
    if (guest_reg == 0) return;
    int hr = rc_write(e, rc, guest_reg);
    emit_mov_rr(e, hr, host_src);
}

/* Flush all dirty cached registers to memory.  Does NOT clear dirty flags
 * so the same flush can be emitted for multiple exit paths. */
static void rc_flush(emit_t *e, reg_cache_t *rc) {
    for (int i = 0; i < RC_NUM_SLOTS; i++)
        if (rc->slots[i].guest_reg >= 0 && rc->slots[i].dirty)
            emit_store_guest(e, rc->slots[i].guest_reg, rc_host_regs[i]);
}

/* ---- Superblock side exits ---- */

typedef struct {
    uint32_t jcc_patch;                /* offset of jcc rel32 displacement */
    uint32_t target_pc;                /* guest PC for taken path */
    rc_slot_t snapshot[RC_NUM_SLOTS];  /* cache state at branch point */
} side_exit_t;

#define MAX_SIDE_EXITS 8

/* ---- Diamond merge ---- */

/* Check if the fall-through path from start to target contains only
 * straight-line instructions (no branches, jumps, calls, system). */
static int can_diamond_merge(dbt_state_t *dbt, uint32_t start, uint32_t target) {
    for (uint32_t pc = start; pc < target; pc += 4) {
        if (pc + 4 > dbt->bin->code_end) return 0;
        uint32_t w;
        memcpy(&w, dbt->bin->memory + pc, 4);
        rv32_insn_t si;
        rv32_decode(w, &si);
        switch (si.opcode) {
        case OP_LUI: case OP_AUIPC: case OP_LOAD: case OP_STORE:
        case OP_IMM: case OP_REG: case OP_FENCE:
            break;
        default:
            return 0;
        }
    }
    return 1;
}

/* Translate a single straight-line instruction (no control flow).
 * Returns 0 on success, -1 if the opcode cannot be handled inline. */
static int translate_one(dbt_state_t *dbt __attribute__((unused)),
                         emit_t *e, reg_cache_t *rc,
                         rv32_insn_t *insn, uint32_t pc) {
    switch (insn->opcode) {

    case OP_LUI:
        if (insn->rd) {
            int rd = rc_write(e, rc, insn->rd);
            emit_mov_r32_imm32(e, rd, (uint32_t)insn->imm);
        }
        return 0;

    case OP_AUIPC:
        if (insn->rd) {
            int rd = rc_write(e, rc, insn->rd);
            emit_mov_r32_imm32(e, rd, pc + (uint32_t)insn->imm);
        }
        return 0;

    case OP_LOAD: {
        int rs1 = rc_read(e, rc, insn->rs1);
        int rd = insn->rd ? rc_write(e, rc, insn->rd) : X64_RCX;
        switch (insn->funct3) {
        case LD_LW:  emit_load_mem32(e, rd, rs1, insn->imm);  break;
        case LD_LH:  emit_load_mem16s(e, rd, rs1, insn->imm); break;
        case LD_LHU: emit_load_mem16u(e, rd, rs1, insn->imm); break;
        case LD_LB:  emit_load_mem8s(e, rd, rs1, insn->imm);  break;
        case LD_LBU: emit_load_mem8u(e, rd, rs1, insn->imm);  break;
        default: return -1;
        }
        return 0;
    }

    case OP_STORE: {
        int rs1 = rc_read(e, rc, insn->rs1);
        int rs2;
        if (insn->rs2 == 0) {
            emit_xor_rr(e, X64_RCX, X64_RCX);
            rs2 = X64_RCX;
        } else {
            rs2 = rc_read(e, rc, insn->rs2);
        }
        switch (insn->funct3) {
        case ST_SW: emit_store_mem32(e, rs1, rs2, insn->imm); break;
        case ST_SH: emit_store_mem16(e, rs1, rs2, insn->imm); break;
        case ST_SB: emit_store_mem8(e, rs1, rs2, insn->imm); break;
        default: return -1;
        }
        return 0;
    }

    case OP_IMM: {
        int rs1 = rc_read(e, rc, insn->rs1);
        int rd = insn->rd ? rc_write(e, rc, insn->rd) : X64_RAX;
        if (rd != rs1) emit_mov_rr(e, rd, rs1);
        switch (insn->funct3) {
        case ALU_ADDI: emit_add_r_imm(e, rd, insn->imm); break;
        case ALU_SLTI:
            emit_cmp_r_imm(e, rd, insn->imm);
            emit_setcc(e, SETCC_L, X64_RAX);
            emit_movzx_r32_r8(e, rd, X64_RAX);
            break;
        case ALU_SLTIU:
            emit_cmp_r_imm(e, rd, insn->imm);
            emit_setcc(e, SETCC_B, X64_RAX);
            emit_movzx_r32_r8(e, rd, X64_RAX);
            break;
        case ALU_XORI: emit_xor_r_imm(e, rd, insn->imm); break;
        case ALU_ORI:  emit_or_r_imm(e, rd, insn->imm); break;
        case ALU_ANDI: emit_and_r_imm(e, rd, insn->imm); break;
        case ALU_SLLI: emit_shl_r_imm(e, rd, insn->imm & 0x1F); break;
        case ALU_SRLI:
            if (insn->funct7 & 0x20)
                emit_sar_r_imm(e, rd, insn->imm & 0x1F);
            else
                emit_shr_r_imm(e, rd, insn->imm & 0x1F);
            break;
        }
        return 0;
    }

    case OP_REG: {
        if (insn->funct7 == 0x01) {
            rc_load(e, rc, X64_RAX, insn->rs1);
            rc_load(e, rc, X64_RCX, insn->rs2);
            switch (insn->funct3) {
            case 0: /* MUL */
                emit_imul_rr(e, X64_RAX, X64_RCX);
                break;
            case 1: /* MULH */
                emit_imul_1(e, X64_RCX);
                emit_mov_rr(e, X64_RAX, X64_RDX);
                break;
            case 2: /* MULHSU */
                emit_byte(e, rex(1, 0, 0, 0)); emit_byte(e, 0x63);
                emit_byte(e, modrm(0x03, X64_RAX, X64_RAX));
                emit_mov_rr(e, X64_RCX, X64_RCX);
                emit_byte(e, rex(1, 0, 0, 0)); emit_byte(e, 0x0F); emit_byte(e, 0xAF);
                emit_byte(e, modrm(0x03, X64_RAX, X64_RCX));
                emit_byte(e, rex(1, 0, 0, 0));
                emit_byte(e, 0xC1); emit_byte(e, modrm(0x03, 5, X64_RAX));
                emit_byte(e, 32);
                break;
            case 3: /* MULHU */
                emit_mul(e, X64_RCX);
                emit_mov_rr(e, X64_RAX, X64_RDX);
                break;
            case 4: /* DIV */
                emit_test_rr(e, X64_RCX, X64_RCX);
                emit_jcc_rel32(e, JCC_NE);
                { uint32_t p1 = emit_pos(e) - 4;
                  emit_mov_r32_imm32(e, X64_RAX, (uint32_t)-1);
                  emit_jmp_rel32(e);
                  uint32_t p_end1 = emit_pos(e) - 4;
                  emit_patch_rel32(e, p1, emit_pos(e));
                  emit_cmp_r_imm(e, X64_RAX, (int32_t)0x80000000u);
                  emit_jcc_rel32(e, JCC_NE);
                  uint32_t p2 = emit_pos(e) - 4;
                  emit_cmp_r_imm(e, X64_RCX, -1);
                  emit_jcc_rel32(e, JCC_NE);
                  uint32_t p3 = emit_pos(e) - 4;
                  emit_mov_r32_imm32(e, X64_RAX, 0x80000000u);
                  emit_jmp_rel32(e);
                  uint32_t p_end2 = emit_pos(e) - 4;
                  emit_patch_rel32(e, p2, emit_pos(e));
                  emit_patch_rel32(e, p3, emit_pos(e));
                  emit_cdq(e);
                  emit_idiv(e, X64_RCX);
                  emit_patch_rel32(e, p_end1, emit_pos(e));
                  emit_patch_rel32(e, p_end2, emit_pos(e));
                }
                break;
            case 5: /* DIVU */
                emit_test_rr(e, X64_RCX, X64_RCX);
                emit_jcc_rel32(e, JCC_NE);
                { uint32_t p1 = emit_pos(e) - 4;
                  emit_mov_r32_imm32(e, X64_RAX, UINT32_MAX);
                  emit_jmp_rel32(e);
                  uint32_t p_end = emit_pos(e) - 4;
                  emit_patch_rel32(e, p1, emit_pos(e));
                  emit_xor_rr(e, X64_RDX, X64_RDX);
                  emit_div(e, X64_RCX);
                  emit_patch_rel32(e, p_end, emit_pos(e));
                }
                break;
            case 6: /* REM */
                emit_test_rr(e, X64_RCX, X64_RCX);
                emit_jcc_rel32(e, JCC_NE);
                { uint32_t p1 = emit_pos(e) - 4;
                  emit_jmp_rel32(e);
                  uint32_t p_end1 = emit_pos(e) - 4;
                  emit_patch_rel32(e, p1, emit_pos(e));
                  emit_cmp_r_imm(e, X64_RAX, (int32_t)0x80000000u);
                  emit_jcc_rel32(e, JCC_NE);
                  uint32_t p2 = emit_pos(e) - 4;
                  emit_cmp_r_imm(e, X64_RCX, -1);
                  emit_jcc_rel32(e, JCC_NE);
                  uint32_t p3 = emit_pos(e) - 4;
                  emit_xor_rr(e, X64_RAX, X64_RAX);
                  emit_jmp_rel32(e);
                  uint32_t p_end2 = emit_pos(e) - 4;
                  emit_patch_rel32(e, p2, emit_pos(e));
                  emit_patch_rel32(e, p3, emit_pos(e));
                  emit_cdq(e);
                  emit_idiv(e, X64_RCX);
                  emit_mov_rr(e, X64_RAX, X64_RDX);
                  emit_patch_rel32(e, p_end1, emit_pos(e));
                  emit_patch_rel32(e, p_end2, emit_pos(e));
                }
                break;
            case 7: /* REMU */
                emit_test_rr(e, X64_RCX, X64_RCX);
                emit_jcc_rel32(e, JCC_NE);
                { uint32_t p1 = emit_pos(e) - 4;
                  emit_jmp_rel32(e);
                  uint32_t p_end = emit_pos(e) - 4;
                  emit_patch_rel32(e, p1, emit_pos(e));
                  emit_xor_rr(e, X64_RDX, X64_RDX);
                  emit_div(e, X64_RCX);
                  emit_mov_rr(e, X64_RAX, X64_RDX);
                  emit_patch_rel32(e, p_end, emit_pos(e));
                }
                break;
            }
            if (insn->rd)
                rc_store(e, rc, insn->rd, X64_RAX);
        } else {
            int rs1 = rc_read(e, rc, insn->rs1);
            int rs2 = rc_read(e, rc, insn->rs2);
            int rd = insn->rd ? rc_write(e, rc, insn->rd) : X64_RAX;

            switch (insn->funct3) {
            case ALU_ADD:
                if (rd == rs1) {
                    if (insn->funct7 & 0x20) emit_sub_rr(e, rd, rs2);
                    else emit_add_rr(e, rd, rs2);
                } else if (rd == rs2) {
                    if (insn->funct7 & 0x20) {
                        emit_neg(e, rd);
                        emit_add_rr(e, rd, rs1);
                    } else {
                        emit_add_rr(e, rd, rs1);
                    }
                } else {
                    emit_mov_rr(e, rd, rs1);
                    if (insn->funct7 & 0x20) emit_sub_rr(e, rd, rs2);
                    else emit_add_rr(e, rd, rs2);
                }
                break;
            case ALU_SLL:
                emit_mov_rr(e, X64_RCX, rs2);
                if (rd != rs1) emit_mov_rr(e, rd, rs1);
                emit_shl_r_cl(e, rd);
                break;
            case ALU_SLT:
                emit_cmp_rr(e, rs1, rs2);
                emit_setcc(e, SETCC_L, X64_RAX);
                emit_movzx_r32_r8(e, rd, X64_RAX);
                break;
            case ALU_SLTU:
                emit_cmp_rr(e, rs1, rs2);
                emit_setcc(e, SETCC_B, X64_RAX);
                emit_movzx_r32_r8(e, rd, X64_RAX);
                break;
            case ALU_XOR:
                if (rd == rs1) emit_xor_rr_op(e, rd, rs2);
                else if (rd == rs2) emit_xor_rr_op(e, rd, rs1);
                else { emit_mov_rr(e, rd, rs1); emit_xor_rr_op(e, rd, rs2); }
                break;
            case ALU_SRL:
                emit_mov_rr(e, X64_RCX, rs2);
                if (rd != rs1) emit_mov_rr(e, rd, rs1);
                if (insn->funct7 & 0x20) emit_sar_r_cl(e, rd);
                else emit_shr_r_cl(e, rd);
                break;
            case ALU_OR:
                if (rd == rs1) emit_or_rr(e, rd, rs2);
                else if (rd == rs2) emit_or_rr(e, rd, rs1);
                else { emit_mov_rr(e, rd, rs1); emit_or_rr(e, rd, rs2); }
                break;
            case ALU_AND:
                if (rd == rs1) emit_and_rr(e, rd, rs2);
                else if (rd == rs2) emit_and_rr(e, rd, rs1);
                else { emit_mov_rr(e, rd, rs1); emit_and_rr(e, rd, rs2); }
                break;
            }
        }
        return 0;
    }

    case OP_FENCE:
        return 0;

    default:
        return -1;
    }
}

/* ---- Intrinsic stubs ---- */

/*
 * Emit a native memcpy/memmove stub that replaces the guest implementation.
 * Loads guest args (a0=dest, a1=src, a2=len), calls host function, returns via ra.
 */
static uint8_t *emit_memcpy_stub(dbt_state_t *dbt, uint32_t guest_pc, int is_memmove) {
    (void)guest_pc;
    emit_t e;
    e.buf = dbt->code_buf + dbt->code_used;
    e.offset = 0;
    e.capacity = CODE_BUF_SIZE - dbt->code_used;

    /* Load guest args: a0(x10)=dest, a1(x11)=src, a2(x12)=len */
    emit_load_guest(&e, X64_RDI, 10);
    emit_load_guest(&e, X64_RSI, 11);
    emit_load_guest(&e, X64_RDX, 12);

    /* Convert guest addresses to host pointers */
    emit_add_r64_r64(&e, X64_RDI, X64_R12);
    emit_add_r64_r64(&e, X64_RSI, X64_R12);

    /* Align stack: RSP is 8-mod-16 in translated blocks, need 16-aligned before CALL */
    emit_byte(&e, rex(1, 0, 0, 0));
    emit_byte(&e, 0x83);
    emit_byte(&e, modrm(0x03, 5, X64_RSP));  /* sub rsp, 8 */
    emit_byte(&e, 8);

    /* Call host memcpy/memmove */
    emit_mov_r64_imm64(&e, X64_RAX, (uint64_t)(uintptr_t)(is_memmove ? memmove : memcpy));
    emit_call_rax(&e);

    /* Restore stack */
    emit_byte(&e, rex(1, 0, 0, 0));
    emit_byte(&e, 0x83);
    emit_byte(&e, modrm(0x03, 0, X64_RSP));  /* add rsp, 8 */
    emit_byte(&e, 8);

    /* a0 (x[10]) already has correct guest dest addr in context (untouched) */

    /* Return via ra: load x[1], use RAS-predicted exit */
    emit_load_guest(&e, X64_RAX, 1);
    emit_exit_ras_return(&e);

    uint8_t *code = dbt->code_buf + dbt->code_used;
    dbt->code_used += e.offset;
    dbt->blocks_translated++;
    return code;
}

/*
 * Emit a native memset stub. Loads a0=ptr, a1=val, a2=len.
 */
static uint8_t *emit_memset_stub(dbt_state_t *dbt, uint32_t guest_pc) {
    (void)guest_pc;
    emit_t e;
    e.buf = dbt->code_buf + dbt->code_used;
    e.offset = 0;
    e.capacity = CODE_BUF_SIZE - dbt->code_used;

    /* Load guest args: a0(x10)=ptr, a1(x11)=value, a2(x12)=len */
    emit_load_guest(&e, X64_RDI, 10);
    emit_load_guest(&e, X64_RSI, 11);
    emit_load_guest(&e, X64_RDX, 12);

    /* Convert ptr to host pointer */
    emit_add_r64_r64(&e, X64_RDI, X64_R12);

    /* Align stack */
    emit_byte(&e, rex(1, 0, 0, 0));
    emit_byte(&e, 0x83);
    emit_byte(&e, modrm(0x03, 5, X64_RSP));
    emit_byte(&e, 8);

    /* Call host memset */
    emit_mov_r64_imm64(&e, X64_RAX, (uint64_t)(uintptr_t)memset);
    emit_call_rax(&e);

    /* Restore stack */
    emit_byte(&e, rex(1, 0, 0, 0));
    emit_byte(&e, 0x83);
    emit_byte(&e, modrm(0x03, 0, X64_RSP));
    emit_byte(&e, 8);

    /* a0 (x[10]) already has correct guest ptr addr in context */

    /* Return via ra */
    emit_load_guest(&e, X64_RAX, 1);
    emit_exit_ras_return(&e);

    uint8_t *code = dbt->code_buf + dbt->code_used;
    dbt->code_used += e.offset;
    dbt->blocks_translated++;
    return code;
}

/*
 * Emit a native strlen stub. Loads a0=str, returns length in a0.
 */
static uint8_t *emit_strlen_stub(dbt_state_t *dbt, uint32_t guest_pc) {
    (void)guest_pc;
    emit_t e;
    e.buf = dbt->code_buf + dbt->code_used;
    e.offset = 0;
    e.capacity = CODE_BUF_SIZE - dbt->code_used;

    /* Load guest arg: a0(x10)=str */
    emit_load_guest(&e, X64_RDI, 10);

    /* Convert to host pointer */
    emit_add_r64_r64(&e, X64_RDI, X64_R12);

    /* Align stack */
    emit_byte(&e, rex(1, 0, 0, 0));
    emit_byte(&e, 0x83);
    emit_byte(&e, modrm(0x03, 5, X64_RSP));
    emit_byte(&e, 8);

    /* Call host strlen */
    emit_mov_r64_imm64(&e, X64_RAX, (uint64_t)(uintptr_t)strlen);
    emit_call_rax(&e);

    /* Restore stack */
    emit_byte(&e, rex(1, 0, 0, 0));
    emit_byte(&e, 0x83);
    emit_byte(&e, modrm(0x03, 0, X64_RSP));
    emit_byte(&e, 8);

    /* Store return value: a0 = strlen result (in EAX) */
    emit_store_guest(&e, 10, X64_RAX);

    /* Return via ra */
    emit_load_guest(&e, X64_RAX, 1);
    emit_exit_ras_return(&e);

    uint8_t *code = dbt->code_buf + dbt->code_used;
    dbt->code_used += e.offset;
    dbt->blocks_translated++;
    return code;
}

/* ---- Symbol lookup for intrinsics ---- */

static uint32_t find_symbol(rv32_binary_t *bin, const char *name) {
    if (!bin->symtab || !bin->strtab) return 0;
    for (uint32_t i = 0; i < bin->symtab_count; i++) {
        if ((bin->symtab[i].st_info & 0x0F) == 2 /* STT_FUNC */
            && strcmp(bin->strtab + bin->symtab[i].st_name, name) == 0)
            return bin->symtab[i].st_value;
    }
    return 0;
}

/* ---- Translator: RV32IM → x86-64 ---- */

static uint8_t *translate_block(dbt_state_t *dbt, uint32_t guest_pc) {
    /* Intercept intrinsic functions with native stubs */
    if (dbt->intrinsic_memcpy  && guest_pc == dbt->intrinsic_memcpy)
        return emit_memcpy_stub(dbt, guest_pc, 0);
    if (dbt->intrinsic_memmove && guest_pc == dbt->intrinsic_memmove)
        return emit_memcpy_stub(dbt, guest_pc, 1);
    if (dbt->intrinsic_memset  && guest_pc == dbt->intrinsic_memset)
        return emit_memset_stub(dbt, guest_pc);
    if (dbt->intrinsic_strlen  && guest_pc == dbt->intrinsic_strlen)
        return emit_strlen_stub(dbt, guest_pc);

    emit_t e;
    e.buf = dbt->code_buf + dbt->code_used;
    e.offset = 0;
    e.capacity = CODE_BUF_SIZE - dbt->code_used;

    reg_cache_t rc;
    rc_init(&rc);

    /* Self-loop detection: pre-scan to find if any branch targets start_pc.
     * If so, pre-warm the register cache and record a warm_entry point.
     * The back-edge jumps to warm_entry, skipping the cold loads.
     *
     * Extended scan: follows superblock-like fall-through past forward
     * branches and unconditional jumps to detect back-edges through
     * forward branches (common in loops with if-then bodies).
     * Register usage is only collected from the prefix before the first
     * control-flow instruction (same as original) to keep the warm_entry
     * register mapping stable. */
    uint32_t warm_entry = 0;
    int self_loop = 0;
    {
        uint32_t scan_pc = guest_pc;
        int used[32] = {0};
        int past_first_branch = 0;  /* stop updating used[] after first branch */
        int scan_depth = 0;         /* count of branches bypassed */
        for (int i = 0; i < MAX_BLOCK_INSNS && scan_pc + 4 <= dbt->bin->code_end; i++) {
            uint32_t w;
            memcpy(&w, dbt->bin->memory + scan_pc, 4);
            rv32_insn_t si;
            rv32_decode(w, &si);
            if (!past_first_branch) {
                if (si.rs1) used[si.rs1] = 1;
                if ((si.opcode == OP_REG || si.opcode == OP_BRANCH || si.opcode == OP_STORE) && si.rs2)
                    used[si.rs2] = 1;
            }
            if (si.opcode == OP_BRANCH) {
                uint32_t target = scan_pc + si.imm;
                if (target == guest_pc) {
                    self_loop = 1;
                    break;
                }
                if (si.imm < 0)
                    break;  /* backward branch elsewhere — stop */
                /* Forward branch: continue scanning fall-through */
                past_first_branch = 1;
                if (++scan_depth >= MAX_SIDE_EXITS)
                    break;
                scan_pc += 4;
                continue;
            }
            if (si.opcode == OP_JAL) {
                if (si.rd != 0)
                    break;  /* call (JAL ra) — stop */
                uint32_t target = scan_pc + si.imm;
                if (target == guest_pc) {
                    self_loop = 1;
                    break;
                }
                if (si.imm > 0 && target + 4 <= dbt->bin->code_end) {
                    /* Forward unconditional jump: follow target */
                    past_first_branch = 1;
                    if (++scan_depth >= MAX_SIDE_EXITS)
                        break;
                    scan_pc = target;
                    continue;
                }
                break;  /* backward or out-of-range jump — stop */
            }
            if (si.opcode == OP_JALR || si.opcode == OP_SYSTEM)
                break;
            scan_pc += 4;
        }
        if (self_loop && past_first_branch) {
            /* Deep self-loop: don't enable warm_entry because the
             * 8-slot register cache fills up during translation of
             * the extended loop body.  Evictions remap host registers
             * to different guest registers, so warm_entry's assumed
             * mapping diverges from the actual mapping at the back-edge.
             * Let the branch be handled normally instead. */
            self_loop = 0;
        }
        if (self_loop) {
            int loaded = 0;
            for (int r = 1; r < 32 && loaded < RC_NUM_SLOTS; r++) {
                if (used[r]) { rc_read(&e, &rc, r); loaded++; }
            }
            warm_entry = emit_pos(&e);
        }
    }

    side_exit_t side_exits[MAX_SIDE_EXITS];
    int num_side_exits = 0;

    uint32_t pc = guest_pc;
    int count = 0;
    uint8_t branch_jcc = 0;
    int32_t branch_imm = 0;

    while (count < MAX_BLOCK_INSNS) {
        if (pc + 4 > dbt->bin->code_end) {
            rc_flush(&e, &rc);
            emit_exit_chained(&e, pc);
            break;
        }

        uint32_t word;
        memcpy(&word, dbt->bin->memory + pc, 4);

        rv32_insn_t insn;
        rv32_decode(word, &insn);
        count++;

        /* LUI+ADDI / AUIPC+ADDI fusion: collapse the two-instruction
         * "load constant" or "load address" idiom into a single mov. */
        if ((insn.opcode == OP_LUI || insn.opcode == OP_AUIPC)
            && insn.rd && pc + 8 <= dbt->bin->code_end) {
            uint32_t nw;
            memcpy(&nw, dbt->bin->memory + pc + 4, 4);
            rv32_insn_t ni;
            rv32_decode(nw, &ni);
            if (ni.opcode == OP_IMM && ni.funct3 == ALU_ADDI
                && ni.rs1 == insn.rd && ni.rd == insn.rd) {
                uint32_t base = (insn.opcode == OP_AUIPC) ? pc : 0;
                int rd = rc_write(&e, &rc, insn.rd);
                emit_mov_r32_imm32(&e, rd, base + (uint32_t)(insn.imm + ni.imm));
                pc += 8;
                count++;
                continue;
            }
            /* AUIPC+JALR / LUI+JALR fusion: resolve indirect call/jump to
             * direct when the target is a compile-time constant. */
            if (ni.opcode == OP_JALR && ni.rs1 == insn.rd) {
                uint32_t base = (insn.opcode == OP_AUIPC) ? pc : 0;
                uint32_t target = (base + (uint32_t)(insn.imm + ni.imm)) & ~1u;
                uint32_t return_pc = pc + 8;

                /* Materialize AUIPC/LUI result if not overwritten by JALR.rd */
                if (insn.rd != ni.rd && insn.rd != 0) {
                    int auipc_rd = rc_write(&e, &rc, insn.rd);
                    emit_mov_r32_imm32(&e, auipc_rd, base + (uint32_t)insn.imm);
                }

                /* Write return address to JALR.rd */
                if (ni.rd) {
                    int jalr_rd = rc_write(&e, &rc, ni.rd);
                    emit_mov_r32_imm32(&e, jalr_rd, return_pc);
                }

                /* RAS push for calls (JALR rd=ra) */
                if (ni.rd == 1)
                    emit_ras_push(&e, return_pc);

                /* Tail call: inline jump like JAL rd=0 */
                if (ni.rd == 0
                    && !(target >= guest_pc && target <= pc + 4)
                    && count < MAX_BLOCK_INSNS - 4) {
                    pc = target;
                    count++;
                    continue;
                }

                rc_flush(&e, &rc);
                emit_exit_chained(&e, target);
                count++;
                goto done;
            }
            /* AUIPC+load / LUI+load fusion: when the address is fully known
             * at translate time, use direct [R12+disp] addressing. */
            if (ni.opcode == OP_LOAD && ni.rs1 == insn.rd) {
                uint32_t base = (insn.opcode == OP_AUIPC) ? pc : 0;
                int32_t addr = (int32_t)(base + (uint32_t)(insn.imm + ni.imm));
                /* Materialize AUIPC/LUI result if load doesn't overwrite it */
                if (insn.rd != ni.rd && insn.rd != 0) {
                    int auipc_rd = rc_write(&e, &rc, insn.rd);
                    emit_mov_r32_imm32(&e, auipc_rd, base + (uint32_t)insn.imm);
                }
                int rd = ni.rd ? rc_write(&e, &rc, ni.rd) : X64_RCX;
                switch (ni.funct3) {
                case LD_LW:  emit_load_abs32(&e, rd, addr);  break;
                case LD_LH:  emit_load_abs16s(&e, rd, addr); break;
                case LD_LHU: emit_load_abs16u(&e, rd, addr); break;
                case LD_LB:  emit_load_abs8s(&e, rd, addr);  break;
                case LD_LBU: emit_load_abs8u(&e, rd, addr);  break;
                default: goto no_fusion;
                }
                pc += 8;
                count++;
                continue;
            }
            /* AUIPC+store / LUI+store fusion: direct [R12+disp] addressing. */
            if (ni.opcode == OP_STORE && ni.rs1 == insn.rd) {
                uint32_t base = (insn.opcode == OP_AUIPC) ? pc : 0;
                int32_t addr = (int32_t)(base + (uint32_t)(insn.imm + ni.imm));
                /* Always materialize AUIPC/LUI result (store doesn't write rd) */
                if (insn.rd != 0) {
                    int auipc_rd = rc_write(&e, &rc, insn.rd);
                    emit_mov_r32_imm32(&e, auipc_rd, base + (uint32_t)insn.imm);
                }
                int rs2;
                if (ni.rs2 == 0) {
                    emit_xor_rr(&e, X64_RCX, X64_RCX);
                    rs2 = X64_RCX;
                } else {
                    rs2 = rc_read(&e, &rc, ni.rs2);
                }
                switch (ni.funct3) {
                case ST_SW: emit_store_abs32(&e, rs2, addr); break;
                case ST_SH: emit_store_abs16(&e, rs2, addr); break;
                case ST_SB: emit_store_abs8(&e, rs2, addr);  break;
                default: goto no_fusion;
                }
                pc += 8;
                count++;
                continue;
            }
        }
        no_fusion:

        /* SLT+branch / SLTU+branch fusion: when SLT(U)/SLTI(U) is followed
         * by BEQ/BNE against x0, fuse into cmp+jcc (skip redundant test). */
        if ((insn.opcode == OP_REG && insn.funct7 == 0
             && (insn.funct3 == ALU_SLT || insn.funct3 == ALU_SLTU))
            || (insn.opcode == OP_IMM
                && (insn.funct3 == ALU_SLTI || insn.funct3 == ALU_SLTIU))) {
            if (pc + 8 <= dbt->bin->code_end) {
                uint32_t nw;
                memcpy(&nw, dbt->bin->memory + pc + 4, 4);
                rv32_insn_t ni;
                rv32_decode(nw, &ni);
                /* Match BEQ/BNE where one operand is SLT.rd and the other is x0 */
                if (ni.opcode == OP_BRANCH
                    && (ni.funct3 == BR_BEQ || ni.funct3 == BR_BNE)
                    && ((ni.rs1 == insn.rd && ni.rs2 == 0)
                        || (ni.rs2 == insn.rd && ni.rs1 == 0))) {
                    int is_signed = (insn.opcode == OP_REG)
                        ? (insn.funct3 == ALU_SLT) : (insn.funct3 == ALU_SLTI);
                    /* Emit the comparison from the SLT */
                    if (insn.opcode == OP_REG) {
                        int rs1 = rc_read(&e, &rc, insn.rs1);
                        int rs2 = rc_read(&e, &rc, insn.rs2);
                        emit_cmp_rr(&e, rs1, rs2);
                    } else {
                        int rs1 = rc_read(&e, &rc, insn.rs1);
                        emit_cmp_r_imm(&e, rs1, insn.imm);
                    }
                    /* Materialize SLT result — setcc/movzx don't clobber flags */
                    if (insn.rd) {
                        emit_setcc(&e, is_signed ? SETCC_L : SETCC_B, X64_RAX);
                        int rd = rc_write(&e, &rc, insn.rd);
                        emit_movzx_r32_r8(&e, rd, X64_RAX);
                    }
                    /* BNE rd, x0 = branch if SLT was true → use < condition
                     * BEQ rd, x0 = branch if SLT was false → use >= condition */
                    if (ni.funct3 == BR_BNE)
                        branch_jcc = is_signed ? JCC_L : JCC_B;
                    else
                        branch_jcc = is_signed ? JCC_GE : JCC_AE;
                    branch_imm = ni.imm;
                    pc += 4;  /* advance past SLT to branch PC */
                    count++;
                    goto emit_branch;

                    /* Shared branch emission — also used by OP_BRANCH below.
                     * Expects: branch_jcc, branch_imm, pc = branch insn PC.
                     * Uses all branch paths: self-loop, diamond, superblock, normal. */
                    { emit_branch:;
                        if (self_loop && pc + branch_imm == guest_pc) {
                            emit_jcc_rel32(&e, branch_jcc);
                            emit_patch_rel32(&e, emit_pos(&e) - 4, warm_entry);
                            rc_flush(&e, &rc);
                            emit_exit_chained(&e, pc + 4);
                            goto done;
                        }
                        if (branch_imm > 0 && branch_imm <= 16 &&
                            can_diamond_merge(dbt, pc + 4, (uint32_t)(pc + branch_imm))) {
                            rc_flush(&e, &rc);
                            rc_init(&rc);
                            emit_jcc_rel32(&e, branch_jcc);
                            uint32_t merge_patch = emit_pos(&e) - 4;
                            uint32_t merge_target = (uint32_t)(pc + branch_imm);
                            pc += 4;
                            while (pc < merge_target) {
                                uint32_t fw;
                                memcpy(&fw, dbt->bin->memory + pc, 4);
                                rv32_insn_t fi;
                                rv32_decode(fw, &fi);
                                translate_one(dbt, &e, &rc, &fi, pc);
                                pc += 4;
                                count++;
                            }
                            rc_flush(&e, &rc);
                            rc_init(&rc);
                            emit_patch_rel32(&e, merge_patch, emit_pos(&e));
                            dbt->diamond_merges++;
                            break;
                        }
                        if (num_side_exits < MAX_SIDE_EXITS && count < MAX_BLOCK_INSNS - 4) {
                            emit_jcc_rel32(&e, branch_jcc);
                            side_exits[num_side_exits].jcc_patch = emit_pos(&e) - 4;
                            side_exits[num_side_exits].target_pc = (uint32_t)(pc + branch_imm);
                            memcpy(side_exits[num_side_exits].snapshot, rc.slots, sizeof(rc.slots));
                            num_side_exits++;
                            pc += 4;
                            break;
                        }
                        emit_jcc_rel32(&e, branch_jcc);
                        uint32_t patch_taken = emit_pos(&e) - 4;
                        rc_flush(&e, &rc);
                        emit_exit_chained(&e, pc + 4);
                        emit_patch_rel32(&e, patch_taken, emit_pos(&e));
                        rc_flush(&e, &rc);
                        emit_exit_chained(&e, pc + branch_imm);
                        goto done;
                    }
                }
            }
        }

        switch (insn.opcode) {

        case OP_LUI: case OP_AUIPC: case OP_LOAD: case OP_STORE:
        case OP_IMM: case OP_REG: case OP_FENCE:
            if (translate_one(dbt, &e, &rc, &insn, pc) < 0) {
                rc_flush(&e, &rc);
                emit_exit_with_pc(&e, pc);
                goto done;
            }
            pc += 4;
            break;

        case OP_JAL: {
            uint32_t target = (uint32_t)(pc + insn.imm);
            /* Inline unconditional jumps (j pseudo = JAL x0) when safe.
             * Don't inline if target is within already-translated range
             * (would re-translate and potentially loop forever). */
            if (insn.rd == 0 && !(target >= guest_pc && target <= pc)
                && count < MAX_BLOCK_INSNS - 4) {
                pc = target;
                break;
            }
            if (insn.rd) {
                int rd = rc_write(&e, &rc, insn.rd);
                emit_mov_r32_imm32(&e, rd, pc + 4);
            }
            /* RAS push for calls (JAL ra, target) */
            if (insn.rd == 1)
                emit_ras_push(&e, pc + 4);
            rc_flush(&e, &rc);
            emit_exit_chained(&e, target);
            goto done;
        }

        case OP_JALR: {
            int rs1 = rc_read(&e, &rc, insn.rs1);
            emit_mov_rr(&e, X64_RAX, rs1);
            emit_add_r_imm(&e, X64_RAX, insn.imm);
            emit_and_r_imm(&e, X64_RAX, ~1);
            if (insn.rd) {
                int rd = rc_write(&e, &rc, insn.rd);
                emit_mov_r32_imm32(&e, rd, pc + 4);
            }
            /* RAS push for indirect calls (JALR rd=ra) */
            if (insn.rd == 1)
                emit_ras_push(&e, pc + 4);
            rc_flush(&e, &rc);
            /* RAS predict for returns (JALR x0, x1, 0 = ret) */
            if (insn.rs1 == 1 && insn.rd == 0 && insn.imm == 0)
                emit_exit_ras_return(&e);
            else
                emit_exit_indirect(&e);
            goto done;
        }

        case OP_BRANCH: {
            int rs1 = rc_read(&e, &rc, insn.rs1);
            int rs2 = rc_read(&e, &rc, insn.rs2);
            emit_cmp_rr(&e, rs1, rs2);

            switch (insn.funct3) {
            case BR_BEQ:  branch_jcc = JCC_E;  break;
            case BR_BNE:  branch_jcc = JCC_NE; break;
            case BR_BLT:  branch_jcc = JCC_L;  break;
            case BR_BGE:  branch_jcc = JCC_GE; break;
            case BR_BLTU: branch_jcc = JCC_B;  break;
            case BR_BGEU: branch_jcc = JCC_AE; break;
            default:
                rc_flush(&e, &rc);
                emit_exit_with_pc(&e, pc);
                goto done;
            }
            branch_imm = insn.imm;
            goto emit_branch;
        }

        case OP_SYSTEM:
            pc += 4;
            if (insn.imm == 0) {
                rc_flush(&e, &rc);
                emit_exit_with_pc(&e, pc | 1);
            } else {
                rc_flush(&e, &rc);
                emit_exit_with_pc(&e, pc | 2);
            }
            goto done;

        default:
            rc_flush(&e, &rc);
            emit_exit_with_pc(&e, pc);
            goto done;
        }
    }

    rc_flush(&e, &rc);
    emit_exit_chained(&e, pc);

done:
    /* Emit cold stubs for superblock side exits.
     * Each stub: flush dirty regs (from snapshot at branch point),
     * then chained exit to the taken-path target. */
    for (int i = 0; i < num_side_exits; i++) {
        emit_patch_rel32(&e, side_exits[i].jcc_patch, emit_pos(&e));
        for (int j = 0; j < RC_NUM_SLOTS; j++) {
            if (side_exits[i].snapshot[j].guest_reg >= 0 &&
                side_exits[i].snapshot[j].dirty) {
                emit_store_guest(&e, side_exits[i].snapshot[j].guest_reg,
                                 rc_host_regs[j]);
            }
        }
        emit_exit_chained(&e, side_exits[i].target_pc);
    }

    if (e.offset > e.capacity) {
        fprintf(stderr, "rv32-run: JIT code buffer exhausted\n");
        exit(1);
    }

    uint8_t *code = dbt->code_buf + dbt->code_used;
    dbt->code_used += e.offset;
    dbt->blocks_translated++;
    if (num_side_exits > 0) {
        dbt->superblock_count++;
        dbt->side_exits_total += num_side_exits;
    }

    return code;
}

/* ---- Trampoline ---- */

/*
 * trampoline(rv32_ctx_t *ctx=RDI, uint8_t *mem=RSI, void *block=RDX, void *cache=RCX)
 *
 * Sets up:
 *   RBX = ctx pointer
 *   R12 = guest memory base
 *   R13 = block cache base
 */
static void emit_trampoline(dbt_state_t *dbt) {
    emit_t e;
    e.buf = dbt->code_buf;
    e.offset = 0;
    e.capacity = 256;

    emit_push(&e, X64_RBX);
    emit_push(&e, X64_R12);
    emit_push(&e, X64_R13);
    emit_push(&e, X64_R14);
    emit_push(&e, X64_R15);

    /* mov rbx, rdi */
    emit_byte(&e, rex(1, 0, 0, 0));
    emit_byte(&e, 0x89);
    emit_byte(&e, modrm(0x03, X64_RDI, X64_RBX));

    /* mov r12, rsi */
    emit_byte(&e, rex(1, 0, 0, 1));
    emit_byte(&e, 0x89);
    emit_byte(&e, modrm(0x03, X64_RSI, reg_lo(X64_R12)));

    /* mov r13, rcx */
    emit_byte(&e, rex(1, 0, 0, 1));
    emit_byte(&e, 0x89);
    emit_byte(&e, modrm(0x03, X64_RCX, reg_lo(X64_R13)));

    /* call rdx */
    emit_byte(&e, 0xFF);
    emit_byte(&e, modrm(0x03, 2, X64_RDX));

    emit_pop(&e, X64_R15);
    emit_pop(&e, X64_R14);
    emit_pop(&e, X64_R13);
    emit_pop(&e, X64_R12);
    emit_pop(&e, X64_RBX);
    emit_ret(&e);

    dbt->code_used = e.offset;
}

/* ---- Public API ---- */

int dbt_init(dbt_state_t *dbt, rv32_binary_t *bin) {
    memset(dbt, 0, sizeof(*dbt));
    dbt->bin = bin;

    dbt->code_buf = mmap(NULL, CODE_BUF_SIZE,
                         PROT_READ | PROT_WRITE | PROT_EXEC,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (dbt->code_buf == MAP_FAILED) {
        fprintf(stderr, "rv32-run: cannot allocate JIT code buffer\n");
        return -1;
    }

    emit_trampoline(dbt);

    /* Look up intrinsic function addresses in symbol table */
    dbt->intrinsic_memcpy  = find_symbol(bin, "memcpy");
    dbt->intrinsic_memset  = find_symbol(bin, "memset");
    dbt->intrinsic_memmove = find_symbol(bin, "memmove");
    dbt->intrinsic_strlen  = find_symbol(bin, "strlen");

    return 0;
}

int dbt_run(dbt_state_t *dbt) {
    typedef void (*trampoline_fn_t)(rv32_ctx_t *ctx, uint8_t *mem,
                                     void *block, void *cache);
    trampoline_fn_t trampoline = (trampoline_fn_t)(void *)dbt->code_buf;

    for (;;) {
        uint32_t pc = dbt->ctx.next_pc;

        if (dbt->trace) {
            uint32_t rpc = pc & ~3u;
            fprintf(stderr, "[dbt] pc=0x%08X\n", rpc);
        }

        if (pc & 1) {
            dbt->ctx.next_pc = pc & ~3u;
            int rc = handle_ecall(dbt);
            if (rc != -2) return rc;
            dbt->ctx.x[0] = 0;
            continue;
        }
        if (pc & 2) {
            fprintf(stderr, "rv32-run: EBREAK at 0x%08X\n", pc & ~3u);
            return -1;
        }

        block_entry_t *be = cache_lookup(dbt, pc);
        uint8_t *code;
        if (be) {
            code = be->native_code;
        } else {
            code = translate_block(dbt, pc);
            cache_insert(dbt, pc, code);
        }

        trampoline(&dbt->ctx, dbt->bin->memory, code, dbt->cache);

        dbt->ctx.x[0] = 0;
    }
}

void dbt_cleanup(dbt_state_t *dbt) {
    if (dbt->code_buf && dbt->code_buf != MAP_FAILED) {
        munmap(dbt->code_buf, CODE_BUF_SIZE);
        dbt->code_buf = NULL;
    }
}
