#ifndef ELF_LOADER_H
#define ELF_LOADER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ELF32 header constants */
#define EI_NIDENT    16
#define ELFMAG0      0x7f
#define ELFMAG1      'E'
#define ELFMAG2      'L'
#define ELFMAG3      'F'
#define ELFCLASS32   1
#define ELFDATA2LSB  1
#define ET_EXEC      2
#define EM_RISCV     243

/* ELF flags for RISC-V */
#define EF_RISCV_RVC              0x0001
#define EF_RISCV_FLOAT_ABI_SOFT   0x0000
#define EF_RISCV_FLOAT_ABI_SINGLE 0x0002
#define EF_RISCV_FLOAT_ABI_DOUBLE 0x0004
#define EF_RISCV_FLOAT_ABI_QUAD   0x0006

/* Program header types */
#define PT_NULL    0
#define PT_LOAD    1

/* Program header flags */
#define PF_X 0x1
#define PF_W 0x2
#define PF_R 0x4

/* Section header types */
#define SHT_NULL     0
#define SHT_PROGBITS 1
#define SHT_SYMTAB   2
#define SHT_STRTAB   3
#define SHT_NOBITS   8

/* ELF32 structures */
typedef struct {
    uint8_t  e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf32_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} Elf32_Phdr;

typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint32_t sh_flags;
    uint32_t sh_addr;
    uint32_t sh_offset;
    uint32_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint32_t sh_addralign;
    uint32_t sh_entsize;
} Elf32_Shdr;

typedef struct {
    uint32_t st_name;
    uint32_t st_value;
    uint32_t st_size;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
} Elf32_Sym;

/* Loaded binary representation */
typedef struct {
    uint8_t *memory;        /* flat memory image */
    uint32_t memory_size;   /* total allocated memory */
    uint32_t entry_point;   /* e_entry from ELF header */

    /* Segment boundaries (from PT_LOAD) */
    uint32_t code_start;
    uint32_t code_end;      /* exclusive */
    uint32_t data_start;
    uint32_t data_end;      /* exclusive, includes BSS */

    /* Stack */
    uint32_t stack_top;     /* initial SP value */

    /* MMIO (to be configured) */
    uint32_t mmio_base;
    uint32_t mmio_size;

    /* Symbol table (optional, for debugging/intrinsics) */
    Elf32_Sym *symtab;
    uint32_t   symtab_count;
    const char *strtab;

    /* ELF flags */
    uint32_t elf_flags;
    bool has_compressed;    /* RVC extension present */
    int float_abi;          /* 0=soft, 2=single, 4=double */
} rv32_binary_t;

/*
 * Load an ELF binary and validate it as an RV32IM microcontroller binary.
 * Returns 0 on success, -1 on error (prints diagnostics to stderr).
 */
int rv32_load_elf(const char *filename, rv32_binary_t *bin);

/*
 * Free resources allocated by rv32_load_elf.
 */
void rv32_free_binary(rv32_binary_t *bin);

#endif /* ELF_LOADER_H */
