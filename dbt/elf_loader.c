#include "elf_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define DEFAULT_MEMORY_SIZE  (256 * 1024 * 1024)  /* 256 MB */
#define DEFAULT_STACK_SIZE   (128 * 1024)          /* 128 KB */

int rv32_load_elf(const char *filename, rv32_binary_t *bin) {
    memset(bin, 0, sizeof(*bin));

    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "rv32-run: cannot open '%s'\n", filename);
        return -1;
    }

    /* Read ELF header */
    Elf32_Ehdr ehdr;
    if (fread(&ehdr, sizeof(ehdr), 1, f) != 1) {
        fprintf(stderr, "rv32-run: cannot read ELF header\n");
        fclose(f);
        return -1;
    }

    /* Validate ELF magic */
    if (ehdr.e_ident[0] != ELFMAG0 || ehdr.e_ident[1] != ELFMAG1 ||
        ehdr.e_ident[2] != ELFMAG2 || ehdr.e_ident[3] != ELFMAG3) {
        fprintf(stderr, "rv32-run: not an ELF file\n");
        fclose(f);
        return -1;
    }

    /* Must be 32-bit little-endian */
    if (ehdr.e_ident[4] != ELFCLASS32) {
        fprintf(stderr, "rv32-run: not a 32-bit ELF (class=%d)\n", ehdr.e_ident[4]);
        fclose(f);
        return -1;
    }
    if (ehdr.e_ident[5] != ELFDATA2LSB) {
        fprintf(stderr, "rv32-run: not little-endian\n");
        fclose(f);
        return -1;
    }

    /* Must be executable */
    if (ehdr.e_type != ET_EXEC) {
        fprintf(stderr, "rv32-run: not an executable (type=%d)\n", ehdr.e_type);
        fclose(f);
        return -1;
    }

    /* Must be RISC-V */
    if (ehdr.e_machine != EM_RISCV) {
        fprintf(stderr, "rv32-run: not a RISC-V binary (machine=%d)\n", ehdr.e_machine);
        fclose(f);
        return -1;
    }

    /* Check ELF flags */
    bin->elf_flags = ehdr.e_flags;
    bin->has_compressed = (ehdr.e_flags & EF_RISCV_RVC) != 0;
    bin->float_abi = ehdr.e_flags & 0x0006;

    if (bin->has_compressed) {
        fprintf(stderr, "rv32-run: compressed (RVC) instructions not supported\n");
        fclose(f);
        return -1;
    }

    bin->entry_point = ehdr.e_entry;

    /* Allocate flat memory */
    bin->memory_size = DEFAULT_MEMORY_SIZE;
    bin->memory = mmap(NULL, bin->memory_size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (bin->memory == MAP_FAILED) {
        fprintf(stderr, "rv32-run: cannot allocate %u bytes of memory\n", bin->memory_size);
        fclose(f);
        return -1;
    }

    /* Read program headers and load segments */
    if (ehdr.e_phnum == 0) {
        fprintf(stderr, "rv32-run: no program headers\n");
        rv32_free_binary(bin);
        fclose(f);
        return -1;
    }

    bin->code_start = UINT32_MAX;
    bin->data_start = UINT32_MAX;

    for (int i = 0; i < ehdr.e_phnum; i++) {
        Elf32_Phdr phdr;
        fseek(f, ehdr.e_phoff + i * ehdr.e_phentsize, SEEK_SET);
        if (fread(&phdr, sizeof(phdr), 1, f) != 1) {
            fprintf(stderr, "rv32-run: cannot read program header %d\n", i);
            rv32_free_binary(bin);
            fclose(f);
            return -1;
        }

        if (phdr.p_type != PT_LOAD) continue;

        /* Bounds check */
        if (phdr.p_vaddr + phdr.p_memsz > bin->memory_size) {
            fprintf(stderr, "rv32-run: segment at 0x%08X + 0x%X exceeds memory\n",
                    phdr.p_vaddr, phdr.p_memsz);
            rv32_free_binary(bin);
            fclose(f);
            return -1;
        }

        /* Load file contents */
        if (phdr.p_filesz > 0) {
            fseek(f, phdr.p_offset, SEEK_SET);
            if (fread(bin->memory + phdr.p_vaddr, 1, phdr.p_filesz, f) != phdr.p_filesz) {
                fprintf(stderr, "rv32-run: cannot read segment data\n");
                rv32_free_binary(bin);
                fclose(f);
                return -1;
            }
        }

        /* BSS: zero-filled (already zeroed by mmap) */

        /* Track segment boundaries */
        if (phdr.p_flags & PF_X) {
            /* Code segment */
            if (phdr.p_vaddr < bin->code_start)
                bin->code_start = phdr.p_vaddr;
            uint32_t end = phdr.p_vaddr + phdr.p_memsz;
            if (end > bin->code_end)
                bin->code_end = end;
        } else {
            /* Data/BSS segment */
            if (phdr.p_vaddr < bin->data_start)
                bin->data_start = phdr.p_vaddr;
            uint32_t end = phdr.p_vaddr + phdr.p_memsz;
            if (end > bin->data_end)
                bin->data_end = end;
        }
    }

    /* Set up stack */
    bin->stack_top = bin->memory_size - 16;  /* aligned, below top of memory */

    /* Try to load symbol table (optional, for intrinsic hooking) */
    if (ehdr.e_shnum > 0 && ehdr.e_shoff != 0) {
        /* Find .symtab and .strtab */
        for (int i = 0; i < ehdr.e_shnum; i++) {
            Elf32_Shdr shdr;
            fseek(f, ehdr.e_shoff + i * ehdr.e_shentsize, SEEK_SET);
            if (fread(&shdr, sizeof(shdr), 1, f) != 1) break;

            if (shdr.sh_type == SHT_SYMTAB && shdr.sh_size > 0) {
                bin->symtab_count = shdr.sh_size / sizeof(Elf32_Sym);
                bin->symtab = malloc(shdr.sh_size);
                if (bin->symtab) {
                    fseek(f, shdr.sh_offset, SEEK_SET);
                    if (fread(bin->symtab, 1, shdr.sh_size, f) != shdr.sh_size) {
                        free(bin->symtab);
                        bin->symtab = NULL;
                        bin->symtab_count = 0;
                    }
                }

                /* Load associated string table */
                if (bin->symtab && shdr.sh_link < ehdr.e_shnum) {
                    Elf32_Shdr str_shdr;
                    fseek(f, ehdr.e_shoff + shdr.sh_link * ehdr.e_shentsize, SEEK_SET);
                    if (fread(&str_shdr, sizeof(str_shdr), 1, f) == 1 && str_shdr.sh_size > 0) {
                        char *strbuf = malloc(str_shdr.sh_size);
                        if (strbuf) {
                            fseek(f, str_shdr.sh_offset, SEEK_SET);
                            if (fread(strbuf, 1, str_shdr.sh_size, f) == str_shdr.sh_size) {
                                bin->strtab = strbuf;
                            } else {
                                free(strbuf);
                            }
                        }
                    }
                }
                break;  /* only need first symtab */
            }
        }
    }

    fclose(f);

    fprintf(stderr, "rv32-run: loaded '%s'\n", filename);
    fprintf(stderr, "  entry:  0x%08X\n", bin->entry_point);
    fprintf(stderr, "  code:   0x%08X - 0x%08X (%u bytes)\n",
            bin->code_start, bin->code_end, bin->code_end - bin->code_start);
    if (bin->data_start != UINT32_MAX)
        fprintf(stderr, "  data:   0x%08X - 0x%08X (%u bytes)\n",
                bin->data_start, bin->data_end, bin->data_end - bin->data_start);
    fprintf(stderr, "  stack:  0x%08X\n", bin->stack_top);
    if (bin->symtab)
        fprintf(stderr, "  symbols: %u\n", bin->symtab_count);

    return 0;
}

void rv32_free_binary(rv32_binary_t *bin) {
    if (bin->memory && bin->memory != MAP_FAILED) {
        munmap(bin->memory, bin->memory_size);
        bin->memory = NULL;
    }
    free(bin->symtab);
    bin->symtab = NULL;
    free((void *)bin->strtab);
    bin->strtab = NULL;
}
