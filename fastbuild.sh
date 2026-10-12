#!/data/data/com.termux/files/usr/bin/bash
# fastbuild.sh — compila só o kernel e roda com -kernel (que funciona)
cd "$(dirname "$0")"

echo ">>> compilando kernel.elf (incremental)..."
make kernel.elf 2>&1 | tail -3

if [ ! -f kernel.elf ]; then
    echo "FALHOU: kernel.elf não gerado"
    exit 1
fi

echo ">>> rodando QEMU com -kernel  (VNC: 127.0.0.1:5901)"
exec qemu-system-i386 -kernel kernel.elf \
  -netdev user,id=n0,hostfwd=tcp::8080-:80 \
  -device e1000,netdev=n0 \
  -vnc :1
