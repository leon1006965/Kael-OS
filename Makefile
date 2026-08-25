# Kael OS - Makefile
AS = nasm
CC = gcc
LD = ld

CFLAGS = -m32 -ffreestanding -nostdlib -nostdinc -I kernel -fno-builtin \
         -fno-stack-protector -nostartfiles -nodefaultlibs \
         -fno-pic -fno-pie -mno-red-zone          -Wall -Wextra -O2 -fno-tree-fre -fno-inline-functions -fno-inline-small-functions -c
ASFLAGS = -f elf32

FLOPPY_IMG = kael.img
C_SRCS = kernel/kernel.c kernel/keyboard.c kernel/vga.c kernel/font.c kernel/mouse.c kernel/desktop.c kernel/prog_cube.c kernel/prog_notepad.c kernel/prog_fm.c kernel/prog_calc.c
ASM_SRCS = kernel/entry.asm kernel/idt.asm
C_OBJS = $(C_SRCS:.c=.o)
ASM_OBJS = $(ASM_SRCS:.asm=.o)
ALL_OBJS = $(ASM_OBJS) $(C_OBJS)

.PHONY: all clean run

all: $(FLOPPY_IMG)

kernel/%.o: kernel/%.asm
	$(AS) $(ASFLAGS) $< -o $@

kernel/%.o: kernel/%.c kernel/types.h
	$(CC) $(CFLAGS) $< -o $@

boot/boot.bin: boot/boot.asm
	$(AS) -f bin $< -o $@

kernel/kael.bin: $(ALL_OBJS) kernel/linker.ld
	$(LD) -m elf_i386 -T kernel/linker.ld --oformat binary --no-gc-sections -o $@ $(ALL_OBJS)

$(FLOPPY_IMG): boot/boot.bin kernel/kael.bin
	cat boot/boot.bin kernel/kael.bin > $@
	truncate -s 1474560 $@

run: $(FLOPPY_IMG)
	qemu-system-i386 -fda $(FLOPPY_IMG)

clean:
	rm -f boot/boot.bin kernel/*.o kernel/kael.bin $(FLOPPY_IMG)
