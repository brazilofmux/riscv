/* dbt_common.c — Architecture-neutral DBT machinery.
 *
 * Block cache lookup/insert, ECALL handler (host service layer), the run
 * loop, and the init/cleanup plumbing that doesn't care which host ISA the
 * code buffer was filled with. The arch-specific code emission lives in
 * dbt_x64.c / dbt_a64.c and is reached through three hooks:
 *
 *   uint8_t *dbt_translate_block(dbt_state_t *, uint32_t guest_pc);
 *   void     dbt_emit_trampoline(dbt_state_t *);
 *   int      dbt_jit_available(void);
 */
#include "dbt.h"
#include "decoder.h"
#include "shadow.h"
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
#include <sys/stat.h>
#include <math.h>
#include <dirent.h>
#include <errno.h>

/* Host-side directory handle table for the opendir/readdir/closedir
 * ECALLs. Slot index is what the guest sees as a "DIR handle"; the real
 * host DIR* lives here. Single-threaded guests, so no locking. */
#define DBT_DIR_TABLE_SIZE 16
static DIR *g_dir_table[DBT_DIR_TABLE_SIZE];

static int dbt_dir_alloc(DIR *d) {
    for (int i = 0; i < DBT_DIR_TABLE_SIZE; i++) {
        if (!g_dir_table[i]) { g_dir_table[i] = d; return i; }
    }
    return -1;
}

/* libm intrinsic table — see libm_id_t in dbt.h. The order MUST match the
 * enum so the per-arch dispatcher can index by id. */
const libm_info_t libm_table[LIBM_COUNT] = {
    [LIBM_SIN]   = {"sin",   (void*)sin,   0},
    [LIBM_COS]   = {"cos",   (void*)cos,   0},
    [LIBM_TAN]   = {"tan",   (void*)tan,   0},
    [LIBM_ASIN]  = {"asin",  (void*)asin,  0},
    [LIBM_ACOS]  = {"acos",  (void*)acos,  0},
    [LIBM_ATAN]  = {"atan",  (void*)atan,  0},
    [LIBM_SINH]  = {"sinh",  (void*)sinh,  0},
    [LIBM_COSH]  = {"cosh",  (void*)cosh,  0},
    [LIBM_TANH]  = {"tanh",  (void*)tanh,  0},
    [LIBM_EXP]   = {"exp",   (void*)exp,   0},
    [LIBM_LOG]   = {"log",   (void*)log,   0},
    [LIBM_LOG10] = {"log10", (void*)log10, 0},
    [LIBM_LOG2]  = {"log2",  (void*)log2,  0},
    [LIBM_SQRT]  = {"sqrt",  (void*)sqrt,  0},
    [LIBM_FABS]  = {"fabs",  (void*)fabs,  0},
    [LIBM_FLOOR] = {"floor", (void*)floor, 0},
    [LIBM_CEIL]  = {"ceil",  (void*)ceil,  0},
    [LIBM_POW]   = {"pow",   (void*)pow,   1},
    [LIBM_ATAN2] = {"atan2", (void*)atan2, 1},
    [LIBM_FMOD]  = {"fmod",  (void*)fmod,  1},
};

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

    case 46:  /* ftruncate(fd, length) */
        {
            int fd = (int)ctx->x[10];
            off_t length = (off_t)(int32_t)ctx->x[11];
            int result = ftruncate(fd, length);
            ctx->x[10] = (uint32_t)(int32_t)result;
        }
        break;

    case 34:  /* mkdirat(dirfd, pathname, mode) */
        {
            uint32_t path_addr = ctx->x[11];
            if (path_addr >= dbt->bin->memory_size) {
                ctx->x[10] = (uint32_t)-1;
                break;
            }
            const char *pathname = (const char *)(dbt->bin->memory + path_addr);
            int mode = (int)ctx->x[12];
            int result = mkdir(pathname, mode);
            ctx->x[10] = (uint32_t)(int32_t)result;
        }
        break;

    case 79:  /* fstatat(dirfd, pathname, statbuf, flags) */
        {
            int32_t dirfd = (int32_t)ctx->x[10];
            uint32_t path_addr = ctx->x[11];
            uint32_t buf_addr = ctx->x[12];
            int flags = (int)ctx->x[13];
            if (path_addr >= dbt->bin->memory_size || buf_addr + 80 > dbt->bin->memory_size) {
                ctx->x[10] = (uint32_t)-1;
                break;
            }
            const char *pathname = (const char *)(dbt->bin->memory + path_addr);
            if (dirfd == -100) dirfd = AT_FDCWD;
            struct stat host_st;
            int result = fstatat(dirfd, pathname, &host_st, flags);
            if (result == 0) {
                /* Marshal host stat to guest (ILP32: mode@16, size@40) */
                uint8_t *dst = dbt->bin->memory + buf_addr;
                memset(dst, 0, 80);
                uint32_t mode32 = (uint32_t)host_st.st_mode;
                int64_t size64 = (int64_t)host_st.st_size;
                memcpy(dst + 16, &mode32, 4);  /* st_mode */
                memcpy(dst + 40, &size64, 8);  /* st_size */
            }
            ctx->x[10] = (uint32_t)(int32_t)result;
        }
        break;

    case 80:  /* fstat — stub, return -1 */
        ctx->x[10] = (uint32_t)-1;
        break;

    case 90:  /* opendir(path) → handle (>=0) or -1 */
        {
            uint32_t path_addr = ctx->x[10];
            if (path_addr >= dbt->bin->memory_size) {
                ctx->x[10] = (uint32_t)-1;
                break;
            }
            const char *pathname = (const char *)(dbt->bin->memory + path_addr);
            DIR *d = opendir(pathname);
            if (!d) { ctx->x[10] = (uint32_t)-1; break; }
            int slot = dbt_dir_alloc(d);
            if (slot < 0) {
                closedir(d);
                ctx->x[10] = (uint32_t)-1;
                break;
            }
            ctx->x[10] = (uint32_t)slot;
        }
        break;

    case 91:  /* readdir(handle, guest_dirent_addr) → 0 success / -1 EOF/err.
               * Guest struct dirent: 8-byte d_ino at +0, 1-byte d_type
               * at +8, 256-byte d_name at +9. Total written = 265 bytes;
               * sizeof on the guest is 272 (8-aligned tail padding). */
        {
            int slot = (int)(int32_t)ctx->x[10];
            uint32_t buf_addr = ctx->x[11];
            if (slot < 0 || slot >= DBT_DIR_TABLE_SIZE || !g_dir_table[slot]
                || buf_addr + 272 > dbt->bin->memory_size) {
                ctx->x[10] = (uint32_t)-1;
                break;
            }
            errno = 0;
            struct dirent *e = readdir(g_dir_table[slot]);
            if (!e) {
                ctx->x[10] = (errno == 0) ? (uint32_t)-1 : (uint32_t)-1;
                break;
            }
            uint8_t *dst = dbt->bin->memory + buf_addr;
            memset(dst, 0, 265);
            uint64_t ino = (uint64_t)e->d_ino;
            memcpy(dst + 0, &ino, 8);
            uint8_t type = 0;
#ifdef DT_REG
            type = e->d_type;  /* POSIX d_type when available */
#endif
            dst[8] = type;
            size_t nlen = strlen(e->d_name);
            if (nlen > 255) nlen = 255;
            memcpy(dst + 9, e->d_name, nlen);
            dst[9 + nlen] = '\0';
            ctx->x[10] = 0;
        }
        break;

    case 92:  /* closedir(handle) */
        {
            int slot = (int)(int32_t)ctx->x[10];
            if (slot < 0 || slot >= DBT_DIR_TABLE_SIZE || !g_dir_table[slot]) {
                ctx->x[10] = (uint32_t)-1;
                break;
            }
            int rc = closedir(g_dir_table[slot]);
            g_dir_table[slot] = NULL;
            ctx->x[10] = (uint32_t)(int32_t)rc;
        }
        break;

    case 101: /* nanosleep(req_ts_addr, rem_ts_addr).
               * Guest struct timespec: 4-byte tv_sec, 4-byte tv_nsec
               * (long is 32-bit on ILP32). */
        {
            uint32_t req_addr = ctx->x[10];
            uint32_t rem_addr = ctx->x[11];
            if (req_addr + 8 > dbt->bin->memory_size) {
                ctx->x[10] = (uint32_t)-1;
                break;
            }
            uint32_t s32 = 0, ns32 = 0;
            memcpy(&s32,  dbt->bin->memory + req_addr,     4);
            memcpy(&ns32, dbt->bin->memory + req_addr + 4, 4);
            struct timespec req = { (time_t)s32, (long)ns32 };
            struct timespec rem = { 0, 0 };
            int rc = nanosleep(&req, &rem);
            if (rem_addr && rem_addr + 8 <= dbt->bin->memory_size) {
                uint32_t rs = (uint32_t)rem.tv_sec;
                uint32_t rn = (uint32_t)rem.tv_nsec;
                memcpy(dbt->bin->memory + rem_addr,     &rs, 4);
                memcpy(dbt->bin->memory + rem_addr + 4, &rn, 4);
            }
            ctx->x[10] = (uint32_t)(int32_t)rc;
        }
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

/* ---- Diamond-merge eligibility check (arch-neutral, decodes guest insns) ---- */

int dbt_can_diamond_merge(dbt_state_t *dbt, uint32_t start, uint32_t target) {
    for (uint32_t pc = start; pc < target; pc += 4) {
        if (pc + 4 > dbt->bin->code_end) return 0;
        uint32_t w;
        memcpy(&w, dbt->bin->memory + pc, 4);
        rv32_insn_t si;
        rv32_decode(w, &si);
        switch (si.opcode) {
        case OP_LUI: case OP_AUIPC: case OP_LOAD: case OP_STORE:
        case OP_IMM: case OP_REG: case OP_FENCE:
        case OP_FP_LOAD: case OP_FP_STORE: case OP_FP:
        case OP_FMADD: case OP_FMSUB: case OP_FNMSUB: case OP_FNMADD:
            break;
        default:
            return 0;
        }
    }
    return 1;
}

/* ---- Symbol lookup for intrinsic interception ---- */

uint32_t dbt_find_symbol(rv32_binary_t *bin, const char *name) {
    if (!bin->symtab || !bin->strtab) return 0;
    for (uint32_t i = 0; i < bin->symtab_count; i++) {
        if ((bin->symtab[i].st_info & 0x0F) == 2 /* STT_FUNC */
            && strcmp(bin->strtab + bin->symtab[i].st_name, name) == 0)
            return bin->symtab[i].st_value;
    }
    return 0;
}

/* ---- Public API ---- */

int dbt_init(dbt_state_t *dbt, rv32_binary_t *bin) {
    memset(dbt, 0, sizeof(*dbt));
    dbt->bin = bin;

    dbt->code_buf = mmap(NULL, CODE_BUF_SIZE,
                         PROT_READ | PROT_WRITE | PROT_EXEC,
                         DBT_JIT_MMAP_FLAGS, -1, 0);
    if (dbt->code_buf == MAP_FAILED) {
        fprintf(stderr, "rv32-run: cannot allocate JIT code buffer\n");
        return -1;
    }

    dbt_jit_writable_begin();
    dbt_emit_trampoline(dbt);
    dbt_jit_writable_end();

    /* Look up intrinsic function addresses in symbol table */
    dbt->intrinsic_memcpy  = dbt_find_symbol(bin, "memcpy");
    dbt->intrinsic_memset  = dbt_find_symbol(bin, "memset");
    dbt->intrinsic_memmove = dbt_find_symbol(bin, "memmove");
    dbt->intrinsic_strlen  = dbt_find_symbol(bin, "strlen");
    for (int i = 0; i < LIBM_COUNT; i++)
        dbt->intrinsic_libm[i] = dbt_find_symbol(bin, libm_table[i].name);

    return 0;
}

int dbt_run(dbt_state_t *dbt) {
    typedef void (*trampoline_fn_t)(rv32_ctx_t *ctx, uint8_t *mem,
                                     void *block, void *cache);
    trampoline_fn_t trampoline = (trampoline_fn_t)(void *)dbt->code_buf;

    /* Lazy shadow alloc — dbt->verify is set by main.c after dbt_init. */
    if (dbt->verify && !dbt->shadow) {
        shadow_state_t *sh = calloc(1, sizeof(*sh));
        if (!sh) {
            fprintf(stderr, "rv32-run: cannot allocate shadow state\n");
            return -1;
        }
        shadow_init(sh, dbt);
        dbt->shadow = sh;
    }

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
            dbt_jit_writable_begin();
            code = dbt_translate_block(dbt, pc);
            dbt_jit_writable_end();
            cache_insert(dbt, pc, code);
        }

        /* Verify mode: snapshot, run shadow, run JIT, compare. The
         * chained-exit helpers in the backend short-circuit when verify
         * is on, so each block round-trips through here. */
        shadow_state_t *sh = (shadow_state_t *)dbt->shadow;
        int do_verify = 0;
        if (sh) {
            shadow_snapshot(sh, dbt);
            do_verify = shadow_pre_execute(sh, pc);
        }

        trampoline(&dbt->ctx, dbt->bin->memory, code, dbt->cache);

        if (sh && do_verify) shadow_verify(sh, dbt, pc);

        dbt->ctx.x[0] = 0;
    }
}

void dbt_cleanup(dbt_state_t *dbt) {
    if (dbt->code_buf && dbt->code_buf != MAP_FAILED) {
        munmap(dbt->code_buf, CODE_BUF_SIZE);
        dbt->code_buf = NULL;
    }
    if (dbt->shadow) {
        free(dbt->shadow);
        dbt->shadow = NULL;
    }
}
