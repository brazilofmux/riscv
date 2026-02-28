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
            if (insn.imm == 0) {
                /* ECALL */
                state->pc = next_pc;
                int rc = handle_ecall(state, bin);
                if (rc != -2) return rc;
                state->x[0] = 0;
                continue;
            } else if (insn.imm == 1) {
                /* EBREAK — treat as breakpoint/halt */
                fprintf(stderr, "rv32-run: EBREAK at 0x%08X (%llu instructions)\n",
                        state->pc, (unsigned long long)state->insn_count);
                return -1;
            } else {
                /* CSR instructions — reject for microcontroller profile */
                fprintf(stderr, "rv32-run: CSR instruction at 0x%08X (not supported)\n",
                        state->pc);
                return -1;
            }
            break;

        default:
            fprintf(stderr, "rv32-run: illegal opcode 0x%02X at 0x%08X\n",
                    insn.opcode, state->pc);
            return -1;
        }

        state->x[0] = 0;  /* x0 is always zero */
        state->pc = next_pc;
    }
}
