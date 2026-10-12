#!/data/data/com.termux/files/usr/bin/bash
cd "$(dirname "$0")"

echo ">>> rebuild rápido (kernel + bootdisk.img)"
rm -f kernel.o kernel.elf kernel.bin bootdisk.img kernel.vmdk
make quick 2>&1 | grep -E "error|warning: [^']" | head -10
[ -f bootdisk.img ] && echo "OK: bootdisk.img ($(stat -c%s bootdisk.img) bytes)" || echo "FALHOU"
