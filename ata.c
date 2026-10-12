#include "ata.h"

static inline void     outb(uint16_t p, uint8_t  v){ __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static inline uint8_t  inb (uint16_t p)            { uint8_t  v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline void     outw(uint16_t p, uint16_t v){ __asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p)); }
static inline uint16_t inw (uint16_t p)            { uint16_t v; __asm__ volatile("inw %1,%0":"=a"(v):"Nd"(p)); return v; }

#define IO_BASE     0x1F0
#define REG_DATA    0
#define REG_ERR     1
#define REG_FEAT    1
#define REG_SECCNT  2
#define REG_LBA0    3
#define REG_LBA1    4
#define REG_LBA2    5
#define REG_DRIVE   6
#define REG_STATUS  7
#define REG_CMD     7

#define CMD_READ    0x20
#define CMD_WRITE   0x30
#define CMD_FLUSH   0xE7
#define CMD_IDENT   0xEC

#define SR_BSY      0x80
#define SR_DRQ      0x08
#define SR_ERR      0x01

static int g_present;
static uint32_t g_sectors;

static void wait_ready(void){
    int t = 1000000;
    while ((inb(IO_BASE+REG_STATUS) & SR_BSY) && t--) { }
}

static int identify(void){
    outb(IO_BASE+REG_DRIVE, 0xA0);  /* master */
    outb(IO_BASE+REG_SECCNT, 0);
    outb(IO_BASE+REG_LBA0, 0);
    outb(IO_BASE+REG_LBA1, 0);
    outb(IO_BASE+REG_LBA2, 0);
    outb(IO_BASE+REG_CMD, CMD_IDENT);
    uint8_t st = inb(IO_BASE+REG_STATUS);
    if (st == 0) return 0;
    wait_ready();
    if (inb(IO_BASE+REG_LBA1) || inb(IO_BASE+REG_LBA2)) return 0; /* ATAPI */
    int t = 100000;
    while (!(inb(IO_BASE+REG_STATUS) & SR_DRQ) && t--) { }
    if (t <= 0) return 0;

    uint16_t buf[256];
    for (int i=0;i<256;i++) buf[i] = inw(IO_BASE+REG_DATA);

    /* sectors = words 60-61 (LBA28) ou 100-103 (LBA48) */
    uint32_t s28 = ((uint32_t)buf[61]<<16) | buf[60];
    if (s28) { g_sectors = s28; }
    else {
        /* LBA48 */
        g_sectors = (uint32_t)buf[100] | ((uint32_t)buf[101]<<16);
    }
    return 1;
}

int ata_init(void){
    g_present = 0;
    g_sectors = 0;
    if (!identify()) return 0;
    g_present = 1;
    return 1;
}

int ata_present(void){ return g_present; }
uint32_t ata_sectors(void){ return g_sectors; }

static int rw(uint32_t lba, uint8_t count, void *buf, int write){
    if (!g_present) return -1;
    wait_ready();
    outb(IO_BASE+REG_DRIVE,   0xE0 | ((lba>>24)&0x0F));
    outb(IO_BASE+REG_FEAT,    0x00);
    outb(IO_BASE+REG_SECCNT,  count);
    outb(IO_BASE+REG_LBA0,   (uint8_t)( lba       & 0xFF));
    outb(IO_BASE+REG_LBA1,   (uint8_t)((lba>> 8) & 0xFF));
    outb(IO_BASE+REG_LBA2,   (uint8_t)((lba>>16) & 0xFF));
    outb(IO_BASE+REG_CMD,    write ? CMD_WRITE : CMD_READ);

    uint16_t *p = (uint16_t*)buf;
    for (int s=0;s<count;s++){
        wait_ready();
        int t = 1000000;
        while (!(inb(IO_BASE+REG_STATUS) & SR_DRQ) && t--) { }
        if (t <= 0) return -2;
        if (inb(IO_BASE+REG_STATUS) & SR_ERR) return -3;
        for (int i=0;i<256;i++){
            if (write) outw(IO_BASE+REG_DATA, p[i]);
            else       p[i] = inw(IO_BASE+REG_DATA);
        }
        p += 256;
    }
    if (write) outb(IO_BASE+REG_CMD, CMD_FLUSH);
    return 0;
}

int ata_read (uint32_t lba, uint8_t n, void *b){ return rw(lba, n, b, 0); }
int ata_write(uint32_t lba, uint8_t n, void *b){ return rw(lba, n, b, 1); }
int ata_flush(void){
    if (!g_present) return -1;
    outb(IO_BASE+REG_CMD, CMD_FLUSH);
    wait_ready();
    return 0;
}
