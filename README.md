# Kernel i386 — Bare-metal Network Appliance

Kernel monolítico i386 com pilha de rede completa, inspirado em unikernels
e RTOS de rede (MirageOS, IncludeOS, NuttX, ClickOS).

## Features

- Boot via GRUB (ISO) ou Multiboot (`-kernel`)
- VGA modo texto 80x25 + teclado PS/2
- Driver e1000 (Intel PRO/1000) + RTL8139 (Realtek) + auto-detecção
- Pilha TCP/IP completa via [lwIP](https://github.com/lwip-tcpip/lwip)
- Filesystem FAT16 (leitura/escrita)
- Servidor HTTP com autenticação Basic
- Shell remoto via TCP
- Comandos: `ping4`, `dns`, `nmap`, `http`, `ls`, `cat`, `write`

## Como compilar

Requer: `clang`, `ld.lld`, `xorriso`, `qemu-system-i386`.

### Termux (Android ARM64)
```sh
pkg install clang lld qemu-system-i386 xorriso
git submodule update --init --recursive
./mkiso.sh          # gera kernel.iso

