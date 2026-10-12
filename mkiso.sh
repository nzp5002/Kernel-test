#!/data/data/com.termux/files/usr/bin/bash
# mkiso.sh — gera kernel.iso bootável no Termux
set -e
cd "$(dirname "$0")"

echo ">>> [1/5] verificando ferramentas"
for tool in xorriso wget ar tar; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "Falta: $tool"
        echo "Instale: pkg install $tool"
        exit 1
    }
done

mkdir -p "$HOME/.cache"

WORK="$HOME/.grub-i386"
if [ ! -f "$WORK/i386-pc/normal.mod" ]; then
    echo ">>> [2/5] baixando módulos do GRUB (só na 1ª vez)"
    rm -rf "$WORK" "$WORK-tmp"
    mkdir -p "$WORK-tmp"
    cd "$WORK-tmp"

    # Pega a lista de .deb disponíveis no pool do Debian
    echo "    consultando mirror..."
    wget -qO- "http://ftp.debian.org/debian/pool/main/g/grub2/" \
      | grep -oE 'grub-(pc-bin|common)_[0-9][^"]*_amd64\.deb' \
      | sort -u > $HOME/.cache/grub-list.txt

    # escolhe as versões mais recentes
    PC_DEB=$(grep '^grub-pc-bin'    $HOME/.cache/grub-list.txt | sort -V | tail -1)
    CM_DEB=$(grep '^grub-common'    $HOME/.cache/grub-list.txt | sort -V | tail -1)

    if [ -z "$PC_DEB" ] || [ -z "$CM_DEB" ]; then
        echo "ERRO: não consegui listar .deb do pool"
        exit 1
    fi
    echo "    pc-bin: $PC_DEB"
    echo "    common: $CM_DEB"

    wget -q "http://ftp.debian.org/debian/pool/main/g/grub2/$PC_DEB"
    wget -q "http://ftp.debian.org/debian/pool/main/g/grub2/$CM_DEB"

    # extrai cada .deb separadamente
    for deb in *.deb; do
        mkdir -p "x-$deb"
        ( cd "x-$deb" && ar x "../$deb" && \
          for f in data.tar.*; do tar xf "$f" 2>/dev/null; done )
    done

    # copia os módulos i386-pc
    mkdir -p "$WORK"
    for d in x-*/usr/lib/grub/i386-pc; do
        [ -d "$d" ] && cp -r "$d"/* "$WORK"/ 2>/dev/null || true
    done

    cd "$OLDPWD"
    rm -rf "$WORK-tmp"
    echo "    módulos em: $WORK"
fi

echo ">>> [3/5] montando esqueleto da ISO"
rm -rf iso
mkdir -p iso/boot/grub/i386-pc
cp "$WORK"/* iso/boot/grub/i386-pc/
cp kernel.elf iso/boot/kernel.elf

cat > iso/boot/grub/grub.cfg << 'GRUBEOF'
set timeout=5
set default=0

menuentry "Kernel i386 + lwIP" {
    multiboot /boot/kernel.elf
    boot
}

menuentry "Kernel (com log para VGA)" {
    multiboot /boot/kernel.elf
    boot
}
GRUBEOF

echo ">>> [4/5] gerando kernel.iso"
if [ -f iso/boot/grub/i386-pc/eltorito.img ]; then
    BOOT_IMG=boot/grub/i386-pc/eltorito.img
elif [ -f iso/boot/grub/i386-pc/boot_hybrid.img ]; then
    BOOT_IMG=boot/grub/i386-pc/boot_hybrid.img
else
    echo "ERRO: nem eltorito.img nem boot_hybrid.img encontrados"
    ls iso/boot/grub/i386-pc/ | head -20
    exit 1
fi

xorriso -as mkisofs \
    -o kernel.iso \
    -V "KERNEL" \
    -b "$BOOT_IMG" \
    -no-emul-boot \
    -boot-load-size 4 \
    -boot-info-table \
    -isohybrid-mbr iso/boot/grub/i386-pc/boot_hybrid.img \
    iso/ 2>&1 | tail -5

echo ">>> [5/5] resultado"
ls -la kernel.iso
file kernel.iso
echo ""
echo "Pronto! Copie kernel.iso para o VirtualBox e inicialize por CD."
