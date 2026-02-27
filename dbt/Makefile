CC = gcc
CFLAGS = -Wall -Wextra -O2 -g
TARGET = rv32-run

SRCS = main.c elf_loader.c interp.c dbt.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

# Dependencies
main.o: main.c elf_loader.h interp.h dbt.h
elf_loader.o: elf_loader.c elf_loader.h
interp.o: interp.c interp.h decoder.h elf_loader.h
dbt.o: dbt.c dbt.h decoder.h emit_x64.h elf_loader.h
