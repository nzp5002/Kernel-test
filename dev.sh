#!/data/data/com.termux/files/usr/bin/bash
# dev.sh — build + run em um comando
cd "$(dirname "$0")"

# limpa só o kernel (não o lwIP)
rm -f kernel.o kernel.elf kernel.bin bootdisk.img kernel.vmdk

echo ">>> compilando..."
make quick 2>&1 | grep -vE "^(clang|ld.lld|objcopy|dd|qemu-img)" | head -20

[ -f bootdisk.img ] || { echo "FALHOU: bootdisk.img não gerado"; exit 1; }

echo ""
echo ">>> rodando QEMU  (VNC: 127.0.0.1:5901)"
exec qemu-system-i386 -drive format=raw,file=bootdisk.img -vnc :1
