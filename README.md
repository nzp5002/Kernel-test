<div align="center">

# ⚙️ Kernel i386

### Um projeto de kernel bare-metal focado em redes

Kernel monolítico para x86 (i386), com drivers de rede e uma pilha TCP/IP integrada — inspirado em unikernels e sistemas operacionais de rede.

<p>
  <img src="https://img.shields.io/badge/Architecture-x86%20%7C%20i386-2563eb?style=for-the-badge" alt="Arquitetura x86 i386">
  <img src="https://img.shields.io/badge/Language-C%20%2B%20Assembly-64748b?style=for-the-badge" alt="C e Assembly">
  <img src="https://img.shields.io/badge/Networking-lwIP-0f766e?style=for-the-badge" alt="Rede lwIP">
</p>

</div>

---

## 🧭 Visão geral

O **Kernel i386** é um projeto experimental de sistema operacional executado diretamente no hardware virtual ou compatível, sem depender de um sistema operacional hospedeiro durante sua execução.

O objetivo é reunir inicialização, acesso a dispositivos, rede e serviços básicos em um ambiente compacto.

## ✨ Recursos

<table>
  <tr>
    <td width="50%">
      <h3>🖥️ Sistema</h3>
      <ul>
        <li>Inicialização via GRUB/ISO ou Multiboot</li>
        <li>Saída de texto VGA 80×25</li>
        <li>Entrada por teclado PS/2</li>
      </ul>
    </td>
    <td width="50%">
      <h3>🌐 Rede</h3>
      <ul>
        <li>Pilha TCP/IP baseada em <a href="https://github.com/lwip-tcpip/lwip">lwIP</a></li>
        <li>Driver Intel PRO/1000 (e1000)</li>
        <li>Driver Realtek RTL8139</li>
        <li>Detecção de interface de rede</li>
      </ul>
    </td>
  </tr>
  <tr>
    <td>
      <h3>💾 Armazenamento</h3>
      <ul>
        <li>Filesystem FAT16</li>
        <li>Operações de leitura e escrita</li>
      </ul>
    </td>
    <td>
      <h3>🛠️ Serviços</h3>
      <ul>
        <li>Servidor HTTP com autenticação Basic</li>
        <li>Shell remoto via TCP</li>
        <li>Comandos de rede e arquivos</li>
      </ul>
    </td>
  </tr>
</table>

### Comandos disponíveis

`ping4` · `dns` · `nmap` · `http` · `ls` · `cat` · `write`

## 🚀 Compilação

### Requisitos

- Clang
- LLD (`ld.lld`)
- xorriso
- QEMU para x86
- Git

### Android com Termux (ARM64)

Instale as ferramentas necessárias:

```sh
pkg update
pkg install clang lld qemu-system-i386 xorriso git
```

Clone o projeto e inicialize os submódulos:

```sh
git clone https://github.com/nzp5002/Kernel-test.git
cd Kernel-test
git submodule update --init --recursive
```

Gere a imagem ISO:

```sh
./mkiso.sh
```

O script de compilação gera `kernel.iso`, conforme a configuração atual do projeto.

## 🧪 Execução e testes

Use o QEMU para experimentar o kernel em uma máquina virtual, sem precisar inicializá-lo diretamente no computador físico. Consulte os scripts do repositório para ver as opções de execução disponíveis.

> **Dica:** faça testes em um ambiente descartável e evite expor serviços de rede experimentais a redes públicas.

## 🗂️ Estrutura do projeto

| Arquivo / diretório | Finalidade |
|---|---|
| `boot.S` | Código de inicialização em Assembly |
| `kernel.c` | Código principal do kernel |
| `ata.c` / `ata.h` | Interface de armazenamento ATA |
| `fat.c` / `fat.h` | Suporte ao filesystem FAT |
| `e1000.c` | Driver Intel PRO/1000 |
| `pcnet.c` | Driver PCnet |
| `nic.c` / `nic.h` | Camada de interface de rede |
| `httpd.c` / `httpd.h` | Servidor HTTP |
| `conntrack.c` / `conntrack.h` | Rastreamento de conexões |
| `lwipopts.h` | Configuração da pilha lwIP |
| `Makefile`, `build.sh`, `mkiso.sh` | Ferramentas de compilação |

## 🔐 Nota de segurança

Este é um projeto de desenvolvimento. Antes de usar em uma rede real, revise especialmente a autenticação HTTP Basic, o shell remoto, a validação de entradas e os limites de memória. Não coloque serviços experimentais diretamente na Internet.

## 📌 Estado do projeto

O projeto está em desenvolvimento. Recursos, compatibilidade de hardware e procedimentos de compilação podem mudar.

## 🤝 Contribuições

Sugestões, relatos de bugs e melhorias são bem-vindos. Abra uma *Issue* com detalhes do problema ou envie um *Pull Request* com sua proposta.

---

<div align="center">

**Feito para explorar sistemas operacionais e redes em baixo nível.**

<sub>Kernel i386 · C · Assembly · lwIP</sub>

</div>
