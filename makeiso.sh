#!/bin/sh
# makeiso.sh — gera kernel.iso com GRUB (rodar no PC Linux)
set -e

if ! command -v grub-mkrescue >/dev/null 2>&1; then
    echo "Instale: sudo apt install grub-pc-bin xorriso mtools"
    exit 1
fi

rm -rf /tmp/kernel-iso
mkdir -p /tmp/kernel-iso/boot/grub
cp kernel.elf /tmp/kernel-iso/boot/

cat > /tmp/kernel-iso/boot/grub/grub.cfg << 'GRUBEOF'
set timeout=3
set default=0

menuentry "Kernel i386 + lwIP" {
    multiboot /boot/kernel.elf
    boot
}

menuentry "Kernel (serial debug)" {
    multiboot /boot/kernel.elf
    boot
}
GRUBEOF

grub-mkrescue -o kernel.iso /tmp/kernel-iso/ 2>&1 | tail -3
echo "OK: kernel.iso ($(stat -c%s kernel.iso) bytes)"
