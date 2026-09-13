# Kael OS - Makefile (GRUB/Multiboot edition)
AS = nasm
CC = gcc
LD = ld

CFLAGS = -m32 -ffreestanding -nostdlib -nostdinc -I kernel -fno-builtin \
         -fno-stack-protector -nostartfiles -nodefaultlibs \
         -fno-pic -fno-pie -mno-red-zone -Wall -Wextra -O2 -c
ASFLAGS = -f elf32

FLOPPY_IMG = kael.img
ISO_IMG = kael.iso
C_SRCS = kernel/kernel.c kernel/keyboard.c kernel/vga.c kernel/font.c \
         kernel/mouse.c kernel/desktop.c kernel/prog_cube.c kernel/prog_notepad.c \
         kernel/prog_fm.c kernel/prog_calc.c
ASM_SRCS = kernel/entry.asm kernel/idt.asm
C_OBJS = $(C_SRCS:.c=.o)
ASM_OBJS = $(ASM_SRCS:.asm=.o)
ALL_OBJS = $(ASM_OBJS) $(C_OBJS)

.PHONY: all clean run iso

all: $(FLOPPY_IMG)

iso: $(ISO_IMG)

kernel/%.o: kernel/%.asm
	$(AS) $(ASFLAGS) $< -o $@

kernel/%.o: kernel/%.c kernel/types.h
	$(CC) $(CFLAGS) $< -o $@

kernel/kael.elf: $(ALL_OBJS) kernel/linker.ld
	$(LD) -m elf_i386 -T kernel/linker.ld -o $@ $(ALL_OBJS)

kernel/kael.bin: kernel/kael.elf
	objcopy -O binary $< $@

$(FLOPPY_IMG): kernel/kael.bin
	cat boot/boot.bin kernel/kael.bin > $@
	truncate -s 1474560 $@

# GRUB ISO targets
iso_dir/boot/grub:
	mkdir -p iso/boot/grub

iso/boot/grub/grub.cfg: iso_dir/boot/grub
	echo 'menuentry "Kael OS" { multiboot /boot/kael.elf boot }' > $@

iso/boot/kael.elf: kernel/kael.elf iso_dir/boot/grub
	mkdir -p iso/boot
	cp kernel/kael.elf iso/boot/kael.elf

$(ISO_IMG): iso/boot/kael.elf iso/boot/grub/grub.cfg
	grub-mkrescue -o $@ iso/ 2>/dev/null || xorriso -as mkisofs -R -b boot/grub/grub.cfg -no-emul-boot -boot-load-size 4 -boot-info-table -o $@ iso/

run: $(FLOPPY_IMG)
	qemu-system-i386 -fda $(FLOPPY_IMG)

run-iso: $(ISO_IMG)
	qemu-system-i386 -cdrom $(ISO_IMG) -boot d

clean:
	rm -f boot/boot.bin kernel/*.o kernel/kael.elf kernel/kael.bin $(FLOPPY_IMG) $(ISO_IMG)
	rm -rf iso/
