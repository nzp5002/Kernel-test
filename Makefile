# Makefile — build incremental i386 + lwIP

CC      := clang
LD      := ld.lld
TARGET  := i386-unknown-none-elf
LWIP    := $(HOME)/pj/lwip

CFLAGS  := --target=$(TARGET) -ffreestanding -nostdlib -fno-pie \
           -fno-stack-protector -fno-builtin -mno-sse -mno-sse2 \
           -mno-mmx -mno-avx -mgeneral-regs-only -Wall -Wextra -O2 \
           -nostdinc -Isysinclude -Iarch -I. -I$(LWIP)/src/include

LDFLAGS := -m elf_i386 -T linker.ld -nostdlib

PROJ_SRC := kernel.c arch/sys_arch.c netif/rtl8139_lwip.c \
            httpd.c conntrack.c ata.c fat.c \
            nic.c e1000.c pcnet.c

PROJ_OBJ := $(PROJ_SRC:.c=.o)

LWIP_SRC := \
  $(LWIP)/src/core/init.c $(LWIP)/src/core/def.c \
  $(LWIP)/src/core/dns.c $(LWIP)/src/core/inet_chksum.c \
  $(LWIP)/src/core/ip.c $(LWIP)/src/core/mem.c \
  $(LWIP)/src/core/memp.c $(LWIP)/src/core/netif.c \
  $(LWIP)/src/core/pbuf.c $(LWIP)/src/core/raw.c \
  $(LWIP)/src/core/stats.c $(LWIP)/src/core/sys.c \
  $(LWIP)/src/core/tcp.c $(LWIP)/src/core/tcp_in.c \
  $(LWIP)/src/core/tcp_out.c $(LWIP)/src/core/timeouts.c \
  $(LWIP)/src/core/udp.c \
  $(LWIP)/src/core/ipv4/etharp.c $(LWIP)/src/core/ipv4/icmp.c \
  $(LWIP)/src/core/ipv4/ip4.c $(LWIP)/src/core/ipv4/ip4_addr.c \
  $(LWIP)/src/core/ipv4/ip4_frag.c \
  $(LWIP)/src/netif/ethernet.c

LWIP_OBJ := $(notdir $(LWIP_SRC:.c=.o))
ALL_OBJ := $(PROJ_OBJ) $(LWIP_OBJ)

all: kernel.elf

%.o: %.c
	@echo CC $<
	@$(CC) $(CFLAGS) -c $< -o $@

%.o: $(LWIP)/src/core/%.c
	@echo CC lwip/$*.c
	@$(CC) $(CFLAGS) -c $< -o $@
%.o: $(LWIP)/src/core/ipv4/%.c
	@echo CC lwip/ipv4/$*.c
	@$(CC) $(CFLAGS) -c $< -o $@
%.o: $(LWIP)/src/netif/%.c
	@echo CC lwip/netif/$*.c
	@$(CC) $(CFLAGS) -c $< -o $@

kernel.elf: $(ALL_OBJ) linker.ld
	@echo LD kernel.elf
	@$(LD) $(LDFLAGS) $(ALL_OBJ) -o $@
	@echo OK: $$(stat -c%s kernel.elf) bytes

boot.o: boot.S
	@echo AS boot.S
	@$(CC) --target=$(TARGET) -ffreestanding -nostdlib -c $< -o $@

boot.bin: boot.o
	@echo LD boot.bin
	@$(LD) -m elf_i386 -Ttext=0x7C00 --image-base=0 -nostdlib \
	       --oformat binary -s boot.o -o $@
	@test $$(stat -c%s boot.bin) -eq 512 || (echo ERRO: boot.bin != 512 bytes && exit 1)

kernel.bin: kernel.elf
	@echo BIN kernel.bin
	@objcopy -O binary kernel.elf kernel.bin

bootdisk.img: boot.bin kernel.bin
	@echo IMG bootdisk.img
	@dd if=/dev/zero of=$@ bs=512 count=131072 2>/dev/null
	@dd if=boot.bin of=$@ bs=512 count=1 conv=notrunc 2>/dev/null
	@dd if=kernel.bin of=$@ bs=512 seek=1 conv=notrunc 2>/dev/null

kernel.vmdk: bootdisk.img
	@echo VMDK kernel.vmdk
	@qemu-img convert -f raw -O vmdk bootdisk.img kernel.vmdk

quick: bootdisk.img
vbox: kernel.vmdk

run: bootdisk.img
	qemu-system-i386 -drive format=raw,file=bootdisk.img -vnc :1

debug: bootdisk.img
	qemu-system-i386 -drive format=raw,file=bootdisk.img \
	  -vnc :1 -d int,cpu_reset -no-reboot 2>&1 | tee /tmp/qemu.log

clean:
	@rm -f $(PROJ_OBJ) boot.o boot.bin kernel.bin kernel.elf bootdisk.img kernel.vmdk

distclean: clean
	@rm -f $(LWIP_OBJ)

rebuild: distclean
	@$(MAKE) quick

.PHONY: all clean distclean rebuild run debug quick vbox
