/* e1000.c — Intel PRO/1000 (82540EM, 82545EM, 82574L, I217-LM) */
#include "nic.h"
#include <stdint.h>
#include <stddef.h>

extern void *memcpy(void*, const void*, size_t);
extern void *memset(void*, int, size_t);

static const uint16_t e1000_devs[] = {
    0x100E,  /* 82540EM  — QEMU default */
    0x100F,  /* 82545EM  — VirtualBox default */
    0x1004,  /* 82543GC  */
    0x10D3,  /* 82574L   */
    0x153A,  /* I217-LM  */
    0
};

#define E1000_CTRL    0x0000
#define E1000_STATUS  0x0008
#define E1000_EERD    0x0014
#define E1000_ICR     0x00C0
#define E1000_IMC     0x00D8
#define E1000_RCTL    0x0100
#define E1000_TCTL    0x0400
#define E1000_TIPG    0x0410
#define E1000_RDBAL   0x2800
#define E1000_RDBAH   0x2804
#define E1000_RDLEN   0x2808
#define E1000_RDH     0x2810
#define E1000_RDT     0x2818
#define E1000_TDBAL   0x3800
#define E1000_TDBAH   0x3804
#define E1000_TDLEN   0x3808
#define E1000_TDH     0x3810
#define E1000_TDT     0x3818
#define E1000_MTA     0x5200
#define E1000_RAL     0x5400
#define E1000_RAH     0x5404

#define CTRL_FD      (1u<<0)
#define CTRL_ASDE    (1u<<5)
#define CTRL_SLU     (1u<<6)
#define CTRL_RST     (1u<<26)

#define RCTL_EN      (1u<<1)
#define RCTL_SBP     (1u<<2)
#define RCTL_UPE     (1u<<3)
#define RCTL_MPE     (1u<<4)
#define RCTL_BAM     (1u<<15)
#define RCTL_SECRC   (1u<<26)
#define RCTL_BSIZE_2048 (0u<<16)

#define TCTL_EN      (1u<<1)
#define TCTL_PSP     (1u<<3)
#define TCTL_CT_SHIFT 4
#define TCTL_COLD_SHIFT 12

#define NUM_RX 32
#define NUM_TX 8
#define RX_SZ  2048
#define TX_SZ  2048

typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} rx_desc_t;

typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} tx_desc_t;

static uint32_t      g_mmio;
static uint8_t       g_mac[6];
static rx_desc_t    *g_rx_desc;
static tx_desc_t    *g_tx_desc;
static uint8_t      *g_rx_bufs;
static uint8_t      *g_tx_bufs;
static uint32_t      g_rx_cur;
static uint32_t      g_tx_cur;

static uint8_t rx_area[sizeof(rx_desc_t)*NUM_RX] __attribute__((aligned(16)));
static uint8_t tx_area[sizeof(tx_desc_t)*NUM_TX] __attribute__((aligned(16)));
static uint8_t rx_buf_area[NUM_RX*RX_SZ]         __attribute__((aligned(16)));
static uint8_t tx_buf_area[NUM_TX*TX_SZ]         __attribute__((aligned(16)));

static inline uint32_t io_r(uint32_t r){ return *(volatile uint32_t*)(g_mmio + r); }
static inline void     io_w(uint32_t r, uint32_t v){ *(volatile uint32_t*)(g_mmio + r) = v; }

static int eeprom_read(uint8_t addr, uint16_t *out){
    io_w(E1000_EERD, ((uint32_t)addr << 8) | 1u);
    for (int t=0;t<100000;t++){
        uint32_t v = io_r(E1000_EERD);
        if (v & (1u<<4)){ *out = (uint16_t)(v>>16); return 0; }
    }
    return -1;
}

static int e1000_probe(void){
    for (int b=0;b<256;b++) for (int s=0;s<32;s++){
        uint32_t id = nic_pci_read32((uint8_t)b,(uint8_t)s,0,0);
        if ((id&0xFFFF)==0xFFFF) continue;
        if ((id&0xFFFF)!=0x8086) continue;
        uint16_t dev = id>>16;
        int match = 0;
        for (int i=0; e1000_devs[i]; i++)
            if (e1000_devs[i] == dev){ match = 1; break; }
        if (!match) continue;

        uint32_t bar0 = nic_pci_read32((uint8_t)b,(uint8_t)s,0,0x10);
        if (bar0 & 1) continue;
        g_mmio = bar0 & 0xFFFFFFF0u;

        uint32_t cmd = nic_pci_read32((uint8_t)b,(uint8_t)s,0,0x04);
        nic_pci_write32((uint8_t)b,(uint8_t)s,0,0x04, cmd | 0x06);
        return 1;
    }
    return 0;
}

static int e1000_init(void){
    /* reset */
    uint32_t c = io_r(E1000_CTRL);
    io_w(E1000_CTRL, c | CTRL_RST);
    for (int t=0;t<100000;t++) if (!(io_r(E1000_CTRL) & CTRL_RST)) break;

    io_w(E1000_IMC, 0xFFFFFFFF);
    io_r(E1000_ICR);

    /* MAC via EEPROM */
    for (int i=0;i<3;i++){
        uint16_t w;
        if (eeprom_read((uint8_t)i, &w) != 0) return -1;
        g_mac[2*i]   = (uint8_t)(w & 0xFF);
        g_mac[2*i+1] = (uint8_t)(w >> 8);
    }

    uint32_t ral = (uint32_t)g_mac[0] | ((uint32_t)g_mac[1]<<8)
                 | ((uint32_t)g_mac[2]<<16) | ((uint32_t)g_mac[3]<<24);
    uint32_t rah = (uint32_t)g_mac[4] | ((uint32_t)g_mac[5]<<8) | (1u<<31);
    io_w(E1000_RAL, ral);
    io_w(E1000_RAH, rah);
    for (int i=0;i<128;i++) io_w(E1000_MTA + i*4, 0);

    /* RX ring */
    g_rx_desc = (rx_desc_t*)rx_area;
    g_rx_bufs = rx_buf_area;
    memset(rx_area, 0, sizeof(rx_area));
    for (int i=0;i<NUM_RX;i++)
        g_rx_desc[i].addr = (uint64_t)(uintptr_t)(g_rx_bufs + i*RX_SZ);

    io_w(E1000_RDBAL, (uint32_t)(uintptr_t)g_rx_desc);
    io_w(E1000_RDBAH, 0);
    io_w(E1000_RDLEN, sizeof(rx_area));
    io_w(E1000_RDH, 0);
    io_w(E1000_RDT, NUM_RX-1);
    g_rx_cur = 0;

    io_w(E1000_RCTL, RCTL_EN|RCTL_SBP|RCTL_UPE|RCTL_MPE|RCTL_BAM|RCTL_SECRC|RCTL_BSIZE_2048);

    /* TX ring */
    g_tx_desc = (tx_desc_t*)tx_area;
    g_tx_bufs = tx_buf_area;
    memset(tx_area, 0, sizeof(tx_area));
    for (int i=0;i<NUM_TX;i++){
        g_tx_desc[i].addr = (uint64_t)(uintptr_t)(g_tx_bufs + i*TX_SZ);
        g_tx_desc[i].status = 1;
    }
    io_w(E1000_TDBAL, (uint32_t)(uintptr_t)g_tx_desc);
    io_w(E1000_TDBAH, 0);
    io_w(E1000_TDLEN, sizeof(tx_area));
    io_w(E1000_TDH, 0);
    io_w(E1000_TDT, 0);
    g_tx_cur = 0;

    io_w(E1000_TCTL, TCTL_EN|TCTL_PSP|(15u<<TCTL_CT_SHIFT)|(64u<<TCTL_COLD_SHIFT));
    io_w(E1000_TIPG, 10 | (8u<<10) | (6u<<20));

    io_w(E1000_CTRL, io_r(E1000_CTRL) | CTRL_SLU | CTRL_ASDE | CTRL_FD);
    return 0;
}

static int e1000_send(const void *data, int len){
    if (len > TX_SZ) return -1;
    uint32_t i = g_tx_cur;
    for (int t=0;t<1000000 && !(g_tx_desc[i].status & 1); t++) { }
    if (!(g_tx_desc[i].status & 1)) return -2;

    memcpy(g_tx_bufs + i*TX_SZ, data, len);
    g_tx_desc[i].length   = (uint16_t)len;
    g_tx_desc[i].cmd      = 0x0B;   /* EOP|IFCS|RS */
    g_tx_desc[i].status   = 0;
    g_tx_desc[i].cso      = 0;
    g_tx_desc[i].css      = 0;
    g_tx_desc[i].special  = 0;

    io_w(E1000_TDT, (i+1) % NUM_TX);
    g_tx_cur = (i+1) % NUM_TX;
    return 0;
}

static int e1000_recv(uint8_t *out, int maxlen){
    uint32_t i = g_rx_cur;
    if (!(g_rx_desc[i].status & 1)) return -1;
    int len = g_rx_desc[i].length;
    if (len > maxlen) len = maxlen;
    memcpy(out, g_rx_bufs + i*RX_SZ, len);

    g_rx_desc[i].status = 0;
    io_w(E1000_RDT, i);
    g_rx_cur = (i+1) % NUM_RX;
    return len;
}

static void e1000_mac(uint8_t out[6]){ for (int i=0;i<6;i++) out[i] = g_mac[i]; }

nic_ops_t e1000_ops = {
    .name  = "e1000",
    .probe = e1000_probe,
    .init  = e1000_init,
    .send  = e1000_send,
    .recv  = e1000_recv,
    .mac   = e1000_mac,
};
