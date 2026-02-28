#include "interp.h"
#include "decoder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <math.h>

/* ---- NaN-boxing helpers ---- */

/* Single-precision values in f[] are NaN-boxed: upper 32 bits = 0xFFFFFFFF.
 * If upper bits aren't all 1s, the value is treated as canonical NaN. */
static inline float fp_unbox_s(uint64_t v) {
    if ((v >> 32) != 0xFFFFFFFF) {
        /* Not properly NaN-boxed: return canonical NaN */
        float nan;
        uint32_t cn = 0x7FC00000;
        memcpy(&nan, &cn, 4);
        return nan;
    }
    float f;
    uint32_t lo = (uint32_t)v;
    memcpy(&f, &lo, 4);
    return f;
}

static inline uint64_t fp_box_s(float f) {
    uint32_t bits;
    memcpy(&bits, &f, 4);
    return 0xFFFFFFFF00000000ULL | bits;
}

static inline double fp_unbox_d(uint64_t v) {
    double d;
    memcpy(&d, &v, 8);
    return d;
}

static inline uint64_t fp_box_d(double d) {
    uint64_t bits;
    memcpy(&bits, &d, 8);
    return bits;
}

/* Memory access helpers — little-endian */
static inline uint32_t mem_read32(rv32_binary_t *bin, uint32_t addr) {
    if (addr + 4 > bin->memory_size) {
        fprintf(stderr, "rv32-run: read32 out of bounds at 0x%08X\n", addr);
        exit(1);
    }
    uint32_t val;
    memcpy(&val, bin->memory + addr, 4);
    return val;
}

static inline uint16_t mem_read16(rv32_binary_t *bin, uint32_t addr) {
    if (addr + 2 > bin->memory_size) {
        fprintf(stderr, "rv32-run: read16 out of bounds at 0x%08X\n", addr);
        exit(1);
    }
    uint16_t val;
    memcpy(&val, bin->memory + addr, 2);
    return val;
}

static inline uint8_t mem_read8(rv32_binary_t *bin, uint32_t addr) {
    if (addr >= bin->memory_size) {
        fprintf(stderr, "rv32-run: read8 out of bounds at 0x%08X\n", addr);
        exit(1);
    }
    return bin->memory[addr];
}

static inline void mem_write32(rv32_binary_t *bin, uint32_t addr, uint32_t val) {
    if (addr + 4 > bin->memory_size) {
        fprintf(stderr, "rv32-run: write32 out of bounds at 0x%08X\n", addr);
        exit(1);
    }
    memcpy(bin->memory + addr, &val, 4);
}

static inline void mem_write16(rv32_binary_t *bin, uint32_t addr, uint16_t val) {
    if (addr + 2 > bin->memory_size) {
        fprintf(stderr, "rv32-run: write16 out of bounds at 0x%08X\n", addr);
        exit(1);
    }
    memcpy(bin->memory + addr, &val, 2);
}

static inline void mem_write8(rv32_binary_t *bin, uint32_t addr, uint8_t val) {
    if (addr >= bin->memory_size) {
        fprintf(stderr, "rv32-run: write8 out of bounds at 0x%08X\n", addr);
        exit(1);
    }
    bin->memory[addr] = val;
}

/* ECALL handler — minimal: just exit and basic I/O for now */
static int handle_ecall(rv32_state_t *s, rv32_binary_t *bin) {
    uint32_t syscall_num = s->x[17];  /* a7 = syscall number */
    (void)bin;

    switch (syscall_num) {
    case 93:  /* exit */
        return (int)(int32_t)s->x[10];  /* a0 = exit code */

    case 64:  /* write(fd, buf, len) */
        {
            uint32_t fd  = s->x[10];
            uint32_t buf = s->x[11];
            uint32_t len = s->x[12];
            if (buf + len > bin->memory_size) {
                s->x[10] = (uint32_t)-1;
                break;
            }
            ssize_t written = write(fd, bin->memory + buf, len);
            s->x[10] = (uint32_t)(int32_t)written;
        }
        break;

    case 63:  /* read(fd, buf, len) */
        {
            uint32_t fd  = s->x[10];
            uint32_t buf = s->x[11];
            uint32_t len = s->x[12];
            if (buf + len > bin->memory_size) {
                s->x[10] = (uint32_t)-1;
                break;
            }
            ssize_t nread = read(fd, bin->memory + buf, len);
            s->x[10] = (uint32_t)(int32_t)nread;
        }
        break;

    case 56:  /* openat(dirfd, pathname, flags, mode) */
        {
            int32_t dirfd = (int32_t)s->x[10];
            uint32_t path_addr = s->x[11];
            int flags = (int)s->x[12];
            int mode = (int)s->x[13];
            if (path_addr >= bin->memory_size) {
                s->x[10] = (uint32_t)-1;
                break;
            }
            const char *pathname = (const char *)(bin->memory + path_addr);
            int result = openat(dirfd, pathname, flags, mode);
            s->x[10] = (uint32_t)(int32_t)result;
        }
        break;

    case 57:  /* close(fd) */
        {
            int fd = (int)s->x[10];
            if (fd <= 2) { s->x[10] = 0; break; }
            int result = close(fd);
            s->x[10] = (uint32_t)(int32_t)result;
        }
        break;

    case 62:  /* lseek(fd, offset, whence) */
        {
            int fd = (int)s->x[10];
            off_t offset = (off_t)(int32_t)s->x[11];
            int whence = (int)s->x[12];
            off_t result = lseek(fd, offset, whence);
            s->x[10] = (uint32_t)(int32_t)result;
        }
        break;

    case 35:  /* unlinkat(dirfd, pathname, flags) */
        {
            uint32_t path_addr = s->x[11];
            if (path_addr >= bin->memory_size) {
                s->x[10] = (uint32_t)-1;
                break;
            }
            const char *pathname = (const char *)(bin->memory + path_addr);
            int result = unlink(pathname);
            s->x[10] = (uint32_t)(int32_t)result;
        }
        break;

    case 46:  /* ftruncate(fd, length) */
        {
            int fd = (int)s->x[10];
            off_t length = (off_t)(int32_t)s->x[11];
            int result = ftruncate(fd, length);
            s->x[10] = (uint32_t)(int32_t)result;
        }
        break;

    case 80:  /* fstat — stub */
        s->x[10] = (uint32_t)-1;
        break;

    case 214: /* brk — not needed */
        s->x[10] = 0;
        break;

    case 403: /* clock_gettime(clockid, tp_addr) */
        {
            uint32_t tp_addr = s->x[11];
            if (tp_addr + 8 > bin->memory_size) {
                s->x[10] = (uint32_t)-1;
                break;
            }
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            uint32_t sec = (uint32_t)ts.tv_sec;
            uint32_t nsec = (uint32_t)ts.tv_nsec;
            memcpy(bin->memory + tp_addr, &sec, 4);
            memcpy(bin->memory + tp_addr + 4, &nsec, 4);
            s->x[10] = 0;
        }
        break;

    case 404: /* get_cpu_clock() — returns host clock() value */
        s->x[10] = (uint32_t)clock();
        break;

    case 500: /* term_setraw(mode) — set terminal raw/cooked mode */
        {
            int mode = (int)s->x[10];
            struct termios t;
            if (tcgetattr(STDIN_FILENO, &t) < 0) {
                s->x[10] = (uint32_t)-1;
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
            s->x[10] = (tcsetattr(STDIN_FILENO, TCSAFLUSH, &t) == 0) ? 0 : (uint32_t)-1;
        }
        break;

    case 501: /* term_getsize(buf) — get terminal dimensions */
        {
            uint32_t buf_addr = s->x[10];
            if (buf_addr + 8 > bin->memory_size) {
                s->x[10] = (uint32_t)-1;
                break;
            }
            struct winsize ws;
            if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
                uint32_t rows = ws.ws_row;
                uint32_t cols = ws.ws_col;
                memcpy(bin->memory + buf_addr, &rows, 4);
                memcpy(bin->memory + buf_addr + 4, &cols, 4);
                s->x[10] = 0;
            } else {
                s->x[10] = (uint32_t)-1;
            }
        }
        break;

    case 502: /* term_kbhit() — non-blocking key check */
        {
            struct pollfd pfd;
            pfd.fd = STDIN_FILENO;
            pfd.events = POLLIN;
            int ret = poll(&pfd, 1, 0);
            s->x[10] = (ret > 0 && (pfd.revents & POLLIN)) ? 1 : 0;
        }
        break;

    default:
        fprintf(stderr, "rv32-run: unhandled ecall %u at PC=0x%08X\n",
                syscall_num, s->pc - 4);
        return -1;
    }

    return -2;  /* continue execution */
}

int rv32_interp_run(rv32_state_t *state, rv32_binary_t *bin) {
    rv32_insn_t insn;

    for (;;) {
        /* Fetch */
        uint32_t word = mem_read32(bin, state->pc);

        /* Decode */
        rv32_decode(word, &insn);

        uint32_t next_pc = state->pc + 4;
        state->insn_count++;

        /* Execute */
        switch (insn.opcode) {

        case OP_LUI:
            if (insn.rd) state->x[insn.rd] = (uint32_t)insn.imm;
            break;

        case OP_AUIPC:
            if (insn.rd) state->x[insn.rd] = state->pc + (uint32_t)insn.imm;
            break;

        case OP_JAL:
            if (insn.rd) state->x[insn.rd] = next_pc;
            next_pc = state->pc + insn.imm;
            break;

        case OP_JALR:
            { uint32_t target = (state->x[insn.rs1] + insn.imm) & ~1u;
              if (insn.rd) state->x[insn.rd] = next_pc;
              next_pc = target; }
            break;

        case OP_BRANCH:
            { uint32_t a = state->x[insn.rs1];
              uint32_t b = state->x[insn.rs2];
              int taken = 0;
              switch (insn.funct3) {
              case BR_BEQ:  taken = (a == b); break;
              case BR_BNE:  taken = (a != b); break;
              case BR_BLT:  taken = ((int32_t)a < (int32_t)b); break;
              case BR_BGE:  taken = ((int32_t)a >= (int32_t)b); break;
              case BR_BLTU: taken = (a < b); break;
              case BR_BGEU: taken = (a >= b); break;
              default:
                  fprintf(stderr, "rv32-run: illegal branch funct3=%d at 0x%08X\n",
                          insn.funct3, state->pc);
                  return -1;
              }
              if (taken) next_pc = state->pc + insn.imm;
            }
            break;

        case OP_LOAD:
            { uint32_t addr = state->x[insn.rs1] + insn.imm;
              uint32_t val = 0;
              switch (insn.funct3) {
              case LD_LB:  val = (uint32_t)(int32_t)(int8_t)mem_read8(bin, addr); break;
              case LD_LH:  val = (uint32_t)(int32_t)(int16_t)mem_read16(bin, addr); break;
              case LD_LW:  val = mem_read32(bin, addr); break;
              case LD_LBU: val = mem_read8(bin, addr); break;
              case LD_LHU: val = mem_read16(bin, addr); break;
              default:
                  fprintf(stderr, "rv32-run: illegal load funct3=%d at 0x%08X\n",
                          insn.funct3, state->pc);
                  return -1;
              }
              if (insn.rd) state->x[insn.rd] = val;
            }
            break;

        case OP_STORE:
            { uint32_t addr = state->x[insn.rs1] + insn.imm;
              switch (insn.funct3) {
              case ST_SB: mem_write8(bin, addr, (uint8_t)state->x[insn.rs2]); break;
              case ST_SH: mem_write16(bin, addr, (uint16_t)state->x[insn.rs2]); break;
              case ST_SW: mem_write32(bin, addr, state->x[insn.rs2]); break;
              default:
                  fprintf(stderr, "rv32-run: illegal store funct3=%d at 0x%08X\n",
                          insn.funct3, state->pc);
                  return -1;
              }
            }
            break;

        case OP_IMM:
            { uint32_t src = state->x[insn.rs1];
              uint32_t result = 0;
              switch (insn.funct3) {
              case ALU_ADDI:  result = src + (uint32_t)insn.imm; break;
              case ALU_SLTI:  result = ((int32_t)src < insn.imm) ? 1 : 0; break;
              case ALU_SLTIU: result = (src < (uint32_t)insn.imm) ? 1 : 0; break;
              case ALU_XORI:  result = src ^ (uint32_t)insn.imm; break;
              case ALU_ORI:   result = src | (uint32_t)insn.imm; break;
              case ALU_ANDI:  result = src & (uint32_t)insn.imm; break;
              case ALU_SLLI:  result = src << (insn.imm & 0x1F); break;
              case ALU_SRLI:
                  if (insn.funct7 & 0x20)
                      result = (uint32_t)((int32_t)src >> (insn.imm & 0x1F));  /* SRAI */
                  else
                      result = src >> (insn.imm & 0x1F);  /* SRLI */
                  break;
              }
              if (insn.rd) state->x[insn.rd] = result;
            }
            break;

        case OP_REG:
            { uint32_t a = state->x[insn.rs1];
              uint32_t b = state->x[insn.rs2];
              uint32_t result = 0;

              if (insn.funct7 == 0x01) {
                  /* M extension */
                  switch (insn.funct3) {
                  case 0: /* MUL */
                      result = a * b;
                      break;
                  case 1: /* MULH (signed × signed, high 32) */
                      result = (uint32_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b >> 32);
                      break;
                  case 2: /* MULHSU (signed × unsigned, high 32) */
                      result = (uint32_t)((int64_t)(int32_t)a * (uint64_t)b >> 32);
                      break;
                  case 3: /* MULHU (unsigned × unsigned, high 32) */
                      result = (uint32_t)((uint64_t)a * (uint64_t)b >> 32);
                      break;
                  case 4: /* DIV */
                      if (b == 0)
                          result = (uint32_t)-1;
                      else if ((int32_t)a == INT32_MIN && (int32_t)b == -1)
                          result = (uint32_t)INT32_MIN;  /* overflow */
                      else
                          result = (uint32_t)((int32_t)a / (int32_t)b);
                      break;
                  case 5: /* DIVU */
                      result = b == 0 ? UINT32_MAX : a / b;
                      break;
                  case 6: /* REM */
                      if (b == 0)
                          result = a;
                      else if ((int32_t)a == INT32_MIN && (int32_t)b == -1)
                          result = 0;
                      else
                          result = (uint32_t)((int32_t)a % (int32_t)b);
                      break;
                  case 7: /* REMU */
                      result = b == 0 ? a : a % b;
                      break;
                  }
              } else {
                  /* Base I extension */
                  switch (insn.funct3) {
                  case ALU_ADD:
                      result = (insn.funct7 & 0x20) ? a - b : a + b;
                      break;
                  case ALU_SLL:
                      result = a << (b & 0x1F);
                      break;
                  case ALU_SLT:
                      result = ((int32_t)a < (int32_t)b) ? 1 : 0;
                      break;
                  case ALU_SLTU:
                      result = (a < b) ? 1 : 0;
                      break;
                  case ALU_XOR:
                      result = a ^ b;
                      break;
                  case ALU_SRL:
                      if (insn.funct7 & 0x20)
                          result = (uint32_t)((int32_t)a >> (b & 0x1F));  /* SRA */
                      else
                          result = a >> (b & 0x1F);
                      break;
                  case ALU_OR:
                      result = a | b;
                      break;
                  case ALU_AND:
                      result = a & b;
                      break;
                  }
              }
              if (insn.rd) state->x[insn.rd] = result;
            }
            break;

        case OP_FENCE:
            /* No-op in single-threaded microcontroller mode */
            break;

        case OP_SYSTEM:
            if (insn.imm == 0 && insn.funct3 == 0) {
                /* ECALL */
                state->pc = next_pc;
                int rc = handle_ecall(state, bin);
                if (rc != -2) return rc;
                state->x[0] = 0;
                continue;
            } else if (insn.imm == 1 && insn.funct3 == 0) {
                /* EBREAK — treat as breakpoint/halt */
                fprintf(stderr, "rv32-run: EBREAK at 0x%08X (%llu instructions)\n",
                        state->pc, (unsigned long long)state->insn_count);
                return -1;
            } else if (insn.funct3 >= 1 && insn.funct3 <= 3) {
                /* CSR read/write: CSRRW(1), CSRRS(2), CSRRC(3) */
                uint32_t csr_addr = insn.imm & 0xFFF;
                uint32_t csr_val = 0;
                switch (csr_addr) {
                case 0x001: csr_val = state->fcsr & 0x1F; break;        /* fflags */
                case 0x002: csr_val = (state->fcsr >> 5) & 0x7; break;  /* frm */
                case 0x003: csr_val = state->fcsr & 0xFF; break;        /* fcsr */
                default:
                    fprintf(stderr, "rv32-run: unsupported CSR 0x%03X at 0x%08X\n",
                            csr_addr, state->pc);
                    return -1;
                }
                uint32_t new_val = csr_val;
                uint32_t rs1_val = state->x[insn.rs1];
                switch (insn.funct3) {
                case 1: new_val = rs1_val; break;                /* CSRRW */
                case 2: new_val = csr_val | rs1_val; break;      /* CSRRS */
                case 3: new_val = csr_val & ~rs1_val; break;     /* CSRRC */
                }
                if (insn.rd) state->x[insn.rd] = csr_val;
                switch (csr_addr) {
                case 0x001: state->fcsr = (state->fcsr & ~0x1Fu) | (new_val & 0x1F); break;
                case 0x002: state->fcsr = (state->fcsr & ~0xE0u) | ((new_val & 0x7) << 5); break;
                case 0x003: state->fcsr = new_val & 0xFF; break;
                }
            } else if (insn.funct3 >= 5 && insn.funct3 <= 7) {
                /* CSRRWI(5), CSRRSI(6), CSRRCI(7) — use rs1 field as zimm */
                uint32_t csr_addr = insn.imm & 0xFFF;
                uint32_t csr_val = 0;
                switch (csr_addr) {
                case 0x001: csr_val = state->fcsr & 0x1F; break;
                case 0x002: csr_val = (state->fcsr >> 5) & 0x7; break;
                case 0x003: csr_val = state->fcsr & 0xFF; break;
                default:
                    fprintf(stderr, "rv32-run: unsupported CSR 0x%03X at 0x%08X\n",
                            csr_addr, state->pc);
                    return -1;
                }
                uint32_t new_val = csr_val;
                uint32_t zimm = insn.rs1;  /* 5-bit immediate in rs1 field */
                switch (insn.funct3) {
                case 5: new_val = zimm; break;                   /* CSRRWI */
                case 6: new_val = csr_val | zimm; break;         /* CSRRSI */
                case 7: new_val = csr_val & ~zimm; break;        /* CSRRCI */
                }
                if (insn.rd) state->x[insn.rd] = csr_val;
                switch (csr_addr) {
                case 0x001: state->fcsr = (state->fcsr & ~0x1Fu) | (new_val & 0x1F); break;
                case 0x002: state->fcsr = (state->fcsr & ~0xE0u) | ((new_val & 0x7) << 5); break;
                case 0x003: state->fcsr = new_val & 0xFF; break;
                }
            } else {
                fprintf(stderr, "rv32-run: illegal SYSTEM funct3=%d at 0x%08X\n",
                        insn.funct3, state->pc);
                return -1;
            }
            break;

        /* ---- RV32F/D floating-point instructions ---- */

        case OP_FP_LOAD: {
            uint32_t addr = state->x[insn.rs1] + insn.imm;
            if (insn.funct3 == 2) {
                /* FLW */
                uint32_t val = mem_read32(bin, addr);
                state->f[insn.rd] = 0xFFFFFFFF00000000ULL | val;
            } else if (insn.funct3 == 3) {
                /* FLD */
                uint32_t lo = mem_read32(bin, addr);
                uint32_t hi = mem_read32(bin, addr + 4);
                state->f[insn.rd] = ((uint64_t)hi << 32) | lo;
            } else {
                fprintf(stderr, "rv32-run: illegal FP load funct3=%d at 0x%08X\n",
                        insn.funct3, state->pc);
                return -1;
            }
            break;
        }

        case OP_FP_STORE: {
            uint32_t addr = state->x[insn.rs1] + insn.imm;
            if (insn.funct3 == 2) {
                /* FSW */
                mem_write32(bin, addr, (uint32_t)state->f[insn.rs2]);
            } else if (insn.funct3 == 3) {
                /* FSD */
                uint64_t val = state->f[insn.rs2];
                mem_write32(bin, addr, (uint32_t)val);
                mem_write32(bin, addr + 4, (uint32_t)(val >> 32));
            } else {
                fprintf(stderr, "rv32-run: illegal FP store funct3=%d at 0x%08X\n",
                        insn.funct3, state->pc);
                return -1;
            }
            break;
        }

        case OP_FMADD: case OP_FMSUB: case OP_FNMSUB: case OP_FNMADD: {
            int fmt = insn.funct7 & 3; /* 0=S, 1=D */
            if (fmt == 0) {
                /* Single-precision FMA */
                float a = fp_unbox_s(state->f[insn.rs1]);
                float b = fp_unbox_s(state->f[insn.rs2]);
                float c = fp_unbox_s(state->f[insn.rs3]);
                float r;
                switch (insn.opcode) {
                case OP_FMADD:  r =  a * b + c; break;
                case OP_FMSUB:  r =  a * b - c; break;
                case OP_FNMSUB: r = -a * b + c; break;
                case OP_FNMADD: r = -a * b - c; break;
                default: r = 0; break;
                }
                state->f[insn.rd] = fp_box_s(r);
            } else if (fmt == 1) {
                /* Double-precision FMA */
                double a = fp_unbox_d(state->f[insn.rs1]);
                double b = fp_unbox_d(state->f[insn.rs2]);
                double c = fp_unbox_d(state->f[insn.rs3]);
                double r;
                switch (insn.opcode) {
                case OP_FMADD:  r =  a * b + c; break;
                case OP_FMSUB:  r =  a * b - c; break;
                case OP_FNMSUB: r = -a * b + c; break;
                case OP_FNMADD: r = -a * b - c; break;
                default: r = 0; break;
                }
                state->f[insn.rd] = fp_box_d(r);
            } else {
                fprintf(stderr, "rv32-run: illegal FMA fmt=%d at 0x%08X\n", fmt, state->pc);
                return -1;
            }
            break;
        }

        case OP_FP: {
            int funct5 = insn.funct7 >> 2;
            int fmt = insn.funct7 & 3; /* 0=S, 1=D */

            switch (funct5) {
            case 0x00: /* FADD */
                if (fmt == 0) {
                    float a = fp_unbox_s(state->f[insn.rs1]);
                    float b = fp_unbox_s(state->f[insn.rs2]);
                    state->f[insn.rd] = fp_box_s(a + b);
                } else {
                    double a = fp_unbox_d(state->f[insn.rs1]);
                    double b = fp_unbox_d(state->f[insn.rs2]);
                    state->f[insn.rd] = fp_box_d(a + b);
                }
                break;
            case 0x01: /* FSUB */
                if (fmt == 0) {
                    float a = fp_unbox_s(state->f[insn.rs1]);
                    float b = fp_unbox_s(state->f[insn.rs2]);
                    state->f[insn.rd] = fp_box_s(a - b);
                } else {
                    double a = fp_unbox_d(state->f[insn.rs1]);
                    double b = fp_unbox_d(state->f[insn.rs2]);
                    state->f[insn.rd] = fp_box_d(a - b);
                }
                break;
            case 0x02: /* FMUL */
                if (fmt == 0) {
                    float a = fp_unbox_s(state->f[insn.rs1]);
                    float b = fp_unbox_s(state->f[insn.rs2]);
                    state->f[insn.rd] = fp_box_s(a * b);
                } else {
                    double a = fp_unbox_d(state->f[insn.rs1]);
                    double b = fp_unbox_d(state->f[insn.rs2]);
                    state->f[insn.rd] = fp_box_d(a * b);
                }
                break;
            case 0x03: /* FDIV */
                if (fmt == 0) {
                    float a = fp_unbox_s(state->f[insn.rs1]);
                    float b = fp_unbox_s(state->f[insn.rs2]);
                    state->f[insn.rd] = fp_box_s(a / b);
                } else {
                    double a = fp_unbox_d(state->f[insn.rs1]);
                    double b = fp_unbox_d(state->f[insn.rs2]);
                    state->f[insn.rd] = fp_box_d(a / b);
                }
                break;
            case 0x0B: /* FSQRT */
                if (fmt == 0) {
                    float a = fp_unbox_s(state->f[insn.rs1]);
                    state->f[insn.rd] = fp_box_s(sqrtf(a));
                } else {
                    double a = fp_unbox_d(state->f[insn.rs1]);
                    state->f[insn.rd] = fp_box_d(sqrt(a));
                }
                break;
            case 0x04: /* FSGNJ / FSGNJN / FSGNJX */
                if (fmt == 0) {
                    uint32_t a, b;
                    memcpy(&a, &state->f[insn.rs1], 4);
                    memcpy(&b, &state->f[insn.rs2], 4);
                    uint32_t r;
                    switch (insn.funct3) {
                    case 0: r = (a & 0x7FFFFFFF) | (b & 0x80000000); break;            /* FSGNJ */
                    case 1: r = (a & 0x7FFFFFFF) | ((~b) & 0x80000000); break;         /* FSGNJN */
                    case 2: r = (a & 0x7FFFFFFF) | ((a ^ b) & 0x80000000); break;      /* FSGNJX */
                    default: r = a; break;
                    }
                    state->f[insn.rd] = 0xFFFFFFFF00000000ULL | r;
                } else {
                    uint64_t a = state->f[insn.rs1];
                    uint64_t b = state->f[insn.rs2];
                    uint64_t r;
                    switch (insn.funct3) {
                    case 0: r = (a & 0x7FFFFFFFFFFFFFFFULL) | (b & 0x8000000000000000ULL); break;
                    case 1: r = (a & 0x7FFFFFFFFFFFFFFFULL) | ((~b) & 0x8000000000000000ULL); break;
                    case 2: r = (a & 0x7FFFFFFFFFFFFFFFULL) | ((a ^ b) & 0x8000000000000000ULL); break;
                    default: r = a; break;
                    }
                    state->f[insn.rd] = r;
                }
                break;
            case 0x05: /* FMIN / FMAX */
                if (fmt == 0) {
                    float a = fp_unbox_s(state->f[insn.rs1]);
                    float b = fp_unbox_s(state->f[insn.rs2]);
                    float r;
                    if (insn.funct3 == 0) { /* FMIN */
                        if (isnan(a) && isnan(b)) { uint32_t cn = 0x7FC00000; memcpy(&r, &cn, 4); }
                        else if (isnan(a)) r = b;
                        else if (isnan(b)) r = a;
                        else if (a == 0 && b == 0) {
                            /* -0 < +0 */
                            uint32_t sa, sb;
                            memcpy(&sa, &a, 4); memcpy(&sb, &b, 4);
                            r = (sa & 0x80000000) ? a : b;
                        }
                        else r = (a < b) ? a : b;
                    } else { /* FMAX */
                        if (isnan(a) && isnan(b)) { uint32_t cn = 0x7FC00000; memcpy(&r, &cn, 4); }
                        else if (isnan(a)) r = b;
                        else if (isnan(b)) r = a;
                        else if (a == 0 && b == 0) {
                            uint32_t sa, sb;
                            memcpy(&sa, &a, 4); memcpy(&sb, &b, 4);
                            r = (sa & 0x80000000) ? b : a;
                        }
                        else r = (a > b) ? a : b;
                    }
                    state->f[insn.rd] = fp_box_s(r);
                } else {
                    double a = fp_unbox_d(state->f[insn.rs1]);
                    double b = fp_unbox_d(state->f[insn.rs2]);
                    double r;
                    if (insn.funct3 == 0) { /* FMIN */
                        if (isnan(a) && isnan(b)) { uint64_t cn = 0x7FF8000000000000ULL; memcpy(&r, &cn, 8); }
                        else if (isnan(a)) r = b;
                        else if (isnan(b)) r = a;
                        else if (a == 0 && b == 0) {
                            uint64_t sa, sb;
                            memcpy(&sa, &a, 8); memcpy(&sb, &b, 8);
                            r = (sa & 0x8000000000000000ULL) ? a : b;
                        }
                        else r = (a < b) ? a : b;
                    } else { /* FMAX */
                        if (isnan(a) && isnan(b)) { uint64_t cn = 0x7FF8000000000000ULL; memcpy(&r, &cn, 8); }
                        else if (isnan(a)) r = b;
                        else if (isnan(b)) r = a;
                        else if (a == 0 && b == 0) {
                            uint64_t sa, sb;
                            memcpy(&sa, &a, 8); memcpy(&sb, &b, 8);
                            r = (sa & 0x8000000000000000ULL) ? b : a;
                        }
                        else r = (a > b) ? a : b;
                    }
                    state->f[insn.rd] = fp_box_d(r);
                }
                break;
            case 0x14: /* FEQ / FLT / FLE (compare → integer rd) */
                if (fmt == 0) {
                    float a = fp_unbox_s(state->f[insn.rs1]);
                    float b = fp_unbox_s(state->f[insn.rs2]);
                    uint32_t r = 0;
                    switch (insn.funct3) {
                    case 2: r = (!isnan(a) && !isnan(b) && a == b) ? 1 : 0; break; /* FEQ */
                    case 1: r = (!isnan(a) && !isnan(b) && a < b)  ? 1 : 0; break; /* FLT */
                    case 0: r = (!isnan(a) && !isnan(b) && a <= b) ? 1 : 0; break; /* FLE */
                    }
                    if (insn.rd) state->x[insn.rd] = r;
                } else {
                    double a = fp_unbox_d(state->f[insn.rs1]);
                    double b = fp_unbox_d(state->f[insn.rs2]);
                    uint32_t r = 0;
                    switch (insn.funct3) {
                    case 2: r = (!isnan(a) && !isnan(b) && a == b) ? 1 : 0; break;
                    case 1: r = (!isnan(a) && !isnan(b) && a < b)  ? 1 : 0; break;
                    case 0: r = (!isnan(a) && !isnan(b) && a <= b) ? 1 : 0; break;
                    }
                    if (insn.rd) state->x[insn.rd] = r;
                }
                break;
            case 0x18: /* FCVT.W.S / FCVT.WU.S / FCVT.W.D / FCVT.WU.D */
                if (fmt == 0) {
                    float a = fp_unbox_s(state->f[insn.rs1]);
                    int32_t r;
                    if (insn.rs2 == 0) {
                        /* FCVT.W.S — float to signed int32 */
                        if (isnan(a)) r = INT32_MAX;
                        else if (a >= 2147483648.0f) r = INT32_MAX;
                        else if (a < -2147483648.0f) r = INT32_MIN;
                        else r = (int32_t)a;
                    } else {
                        /* FCVT.WU.S — float to unsigned int32 */
                        if (isnan(a) || a < 0.0f) r = 0;
                        else if (a >= 4294967296.0f) r = (int32_t)UINT32_MAX;
                        else r = (int32_t)(uint32_t)a;
                    }
                    if (insn.rd) state->x[insn.rd] = (uint32_t)r;
                } else {
                    double a = fp_unbox_d(state->f[insn.rs1]);
                    int32_t r;
                    if (insn.rs2 == 0) {
                        /* FCVT.W.D — double to signed int32 */
                        if (isnan(a)) r = INT32_MAX;
                        else if (a >= 2147483648.0) r = INT32_MAX;
                        else if (a < -2147483648.0) r = INT32_MIN;
                        else r = (int32_t)a;
                    } else {
                        /* FCVT.WU.D — double to unsigned int32 */
                        if (isnan(a) || a < 0.0) r = 0;
                        else if (a >= 4294967296.0) r = (int32_t)UINT32_MAX;
                        else r = (int32_t)(uint32_t)a;
                    }
                    if (insn.rd) state->x[insn.rd] = (uint32_t)r;
                }
                break;
            case 0x1A: /* FCVT.S.W / FCVT.S.WU / FCVT.D.W / FCVT.D.WU */
                if (fmt == 0) {
                    if (insn.rs2 == 0) {
                        /* FCVT.S.W — signed int32 to float */
                        state->f[insn.rd] = fp_box_s((float)(int32_t)state->x[insn.rs1]);
                    } else {
                        /* FCVT.S.WU — unsigned int32 to float */
                        state->f[insn.rd] = fp_box_s((float)state->x[insn.rs1]);
                    }
                } else {
                    if (insn.rs2 == 0) {
                        /* FCVT.D.W — signed int32 to double */
                        state->f[insn.rd] = fp_box_d((double)(int32_t)state->x[insn.rs1]);
                    } else {
                        /* FCVT.D.WU — unsigned int32 to double */
                        state->f[insn.rd] = fp_box_d((double)state->x[insn.rs1]);
                    }
                }
                break;
            case 0x08: /* FCVT.S.D / FCVT.D.S */
                if (fmt == 0) {
                    /* FCVT.S.D — double to float */
                    double a = fp_unbox_d(state->f[insn.rs1]);
                    state->f[insn.rd] = fp_box_s((float)a);
                } else {
                    /* FCVT.D.S — float to double */
                    float a = fp_unbox_s(state->f[insn.rs1]);
                    state->f[insn.rd] = fp_box_d((double)a);
                }
                break;
            case 0x1C: /* FMV.X.W / FCLASS */
                if (fmt == 0) {
                    if (insn.funct3 == 0) {
                        /* FMV.X.W — move FP bits to integer */
                        if (insn.rd) state->x[insn.rd] = (uint32_t)state->f[insn.rs1];
                    } else if (insn.funct3 == 1) {
                        /* FCLASS.S — classify float → 10-bit mask */
                        float a = fp_unbox_s(state->f[insn.rs1]);
                        uint32_t bits;
                        memcpy(&bits, &a, 4);
                        uint32_t sign = bits >> 31;
                        uint32_t exp = (bits >> 23) & 0xFF;
                        uint32_t frac = bits & 0x7FFFFF;
                        uint32_t cls = 0;
                        if (exp == 0xFF && frac != 0) {
                            cls = (frac & 0x400000) ? (1 << 9) : (1 << 8); /* qNaN : sNaN */
                        } else if (exp == 0xFF) {
                            cls = sign ? (1 << 0) : (1 << 7); /* -inf : +inf */
                        } else if (exp == 0 && frac == 0) {
                            cls = sign ? (1 << 3) : (1 << 4); /* -0 : +0 */
                        } else if (exp == 0) {
                            cls = sign ? (1 << 2) : (1 << 5); /* -subnormal : +subnormal */
                        } else {
                            cls = sign ? (1 << 1) : (1 << 6); /* -normal : +normal */
                        }
                        if (insn.rd) state->x[insn.rd] = cls;
                    }
                } else if (fmt == 1 && insn.funct3 == 1) {
                    /* FCLASS.D — classify double */
                    double a = fp_unbox_d(state->f[insn.rs1]);
                    uint64_t bits;
                    memcpy(&bits, &a, 8);
                    uint32_t sign = (uint32_t)(bits >> 63);
                    uint32_t exp = (uint32_t)((bits >> 52) & 0x7FF);
                    uint64_t frac = bits & 0xFFFFFFFFFFFFFULL;
                    uint32_t cls = 0;
                    if (exp == 0x7FF && frac != 0) {
                        cls = (frac & 0x8000000000000ULL) ? (1 << 9) : (1 << 8);
                    } else if (exp == 0x7FF) {
                        cls = sign ? (1 << 0) : (1 << 7);
                    } else if (exp == 0 && frac == 0) {
                        cls = sign ? (1 << 3) : (1 << 4);
                    } else if (exp == 0) {
                        cls = sign ? (1 << 2) : (1 << 5);
                    } else {
                        cls = sign ? (1 << 1) : (1 << 6);
                    }
                    if (insn.rd) state->x[insn.rd] = cls;
                }
                break;
            case 0x1E: /* FMV.W.X */
                if (fmt == 0 && insn.funct3 == 0) {
                    /* FMV.W.X — move integer bits to FP */
                    state->f[insn.rd] = 0xFFFFFFFF00000000ULL | state->x[insn.rs1];
                }
                break;
            default:
                fprintf(stderr, "rv32-run: unhandled FP funct5=0x%02X fmt=%d at 0x%08X\n",
                        funct5, fmt, state->pc);
                return -1;
            }
            break;
        }

        default:
            fprintf(stderr, "rv32-run: illegal opcode 0x%02X at 0x%08X\n",
                    insn.opcode, state->pc);
            return -1;
        }

        state->x[0] = 0;  /* x0 is always zero */
        state->pc = next_pc;
    }
}
