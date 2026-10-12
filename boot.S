/* boot.S — MBR bootloader com debug e DAP em 0x0600 */

.set KERNEL_LBA,      1
.set KERNEL_SECTORS,  1024
.set KERNEL_ENTRY,    0x100010

.code16
.section .text
.global _boot

_boot:
    cli
    xorw    %ax, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movw    $0x7C00, %sp
    movb    %dl, boot_drive

    movb    $'A', %al
    call    putc

    movw    $msg_ok, %si
    call    print_str

    movb    $'B', %al
    call    putc

    movw    $0x1000, %ax
    movw    %ax, %es
    xorw    %di, %di
    movl    $KERNEL_LBA, %esi
    movw    $KERNEL_SECTORS, %cx

.load_loop:
    testw   %cx, %cx
    jz      .load_done

    cmpw    $0xF000, %di
    jb      .no_seg_adv
    movw    $0, %di
    movw    %es, %ax
    addw    $0x1000, %ax
    movw    %ax, %es
.no_seg_adv:

    movw    $8, %ax
    cmpw    %ax, %cx
    jae     .chunk_ok
    movw    %cx, %ax
.chunk_ok:
    movw    %ax, 0x0620

    /* ---- DAP em 0x0600 ---- */
    movb    $0x10, 0x0600
    movb    $0x00, 0x0601
    movw    %ax,   0x0602
    movw    %di,   0x0604
    movw    %es,   0x0606
    movl    %esi,  0x0608
    movl    $0,    0x060C

    /* --- print '1' antes do int 0x13 --- */
    movb    $'1', %al
    call    putc

    movw    $0x0600, %si
    movb    $0x42, %ah
    movb    boot_drive, %dl
    int     $0x13
    jc      disk_err
    testb   %ah, %ah
    jnz     disk_err

    /* --- print '2' depois do int 0x13 --- */
    movb    $'2', %al
    call    putc

    /* progresso: ponto a cada 128 setores */
    testw   $0x7F, %cx
    jnz     .no_dot
    movb    $'.', %al
    call    putc
.no_dot:

    movw    0x0620, %ax
    movzwl  %ax, %eax
    addl    %eax, %esi
    movw    0x0620, %ax
    subw    %ax, %cx

    movw    0x0620, %ax
    shlw    $9, %ax
    addw    %ax, %di
    jmp     .load_loop

.load_done:
    movb    $'C', %al
    call    putc

    /* A20 via port 0x92 */
    inb     $0x92, %al
    andb    $0xFE, %al
    orb     $0x02, %al
    outb    %al, $0x92

    movb    $'D', %al
    call    putc

    lgdt    gdt_desc

    movb    $'E', %al
    call    putc

    movl    %cr0, %eax
    orl     $1, %eax
    movl    %eax, %cr0
    ljmp    $0x08, $pm_entry

disk_err:
    movb    $'!', %al
    call    putc
    movw    $msg_err, %si
    call    print_str
1:  hlt
    jmp     1b

putc:
    movb    $0x0E, %ah
    movb    $0x00, %bh
    movb    $0x07, %bl
    int     $0x10
    ret

print_str:
    lodsb
    testb   %al, %al
    jz      1f
    movb    $0x0E, %ah
    movb    $0x00, %bh
    movb    $0x07, %bl
    int     $0x10
    jmp     print_str
1:  ret

.code32
pm_entry:
    movw    $0x10, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs
    movw    %ax, %ss
    movl    $0x90000, %esp

    movl    $0x10000, %esi
    movl    $0x100000, %edi
    movl    $131072, %ecx
    cld
    rep movsl

    movl    $0x9000, %edi
    movl    $0x00000001, 0(%edi)
    movl    $640, 4(%edi)
    movl    $32768, 8(%edi)

    movl    $0x2BADB002, %eax
    movl    $0x9000, %ebx
    movl    $KERNEL_ENTRY, %ecx
    jmp     *%ecx

.align 8
gdt:
    .quad 0
    .quad 0x00CF9A000000FFFF
    .quad 0x00CF92000000FFFF
gdt_desc:
    .word   23
    .long   gdt

msg_ok:   .asciz "Booting kernel...\r\n"
msg_err:  .asciz "Disk error.\r\n"
boot_drive: .byte 0

.org 510
.byte 0x55, 0xAA
