#include "nic.h"
#include <stddef.h>

extern nic_ops_t e1000_ops;
extern nic_ops_t rtl8139_ops;
extern nic_ops_t pcnet_ops;

static nic_ops_t *nic_list[] = {
    &e1000_ops,     /* Intel — VirtualBox default, QEMU, PCs Intel */
    &rtl8139_ops,   /* Realtek — QEMU padrão, PCs antigos */
    &pcnet_ops,     /* AMD — VirtualBox alternativa, VMware */
    0
};

static nic_ops_t *g_active;

static inline void     outl(uint16_t p, uint32_t v){ __asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p)); }
static inline uint32_t inl (uint16_t p)            { uint32_t v; __asm__ volatile("inl %1,%0":"=a"(v):"Nd"(p)); return v; }

uint32_t nic_pci_read32(uint8_t b, uint8_t s, uint8_t f, uint8_t o){
    outl(0xCF8, 0x80000000u|((uint32_t)b<<16)|((uint32_t)s<<11)|((uint32_t)f<<8)|(o&0xFC));
    return inl(0xCFC);
}
void nic_pci_write32(uint8_t b, uint8_t s, uint8_t f, uint8_t o, uint32_t v){
    outl(0xCF8, 0x80000000u|((uint32_t)b<<16)|((uint32_t)s<<11)|((uint32_t)f<<8)|(o&0xFC));
    outl(0xCFC, v);
}

int nic_init(void){
    g_active = 0;
    for (int i=0; nic_list[i]; i++){
        nic_ops_t *n = nic_list[i];
        if (!n->probe || !n->probe()) continue;
        if (n->init && n->init() != 0) continue;
        g_active = n;
        return 0;
    }
    return -1;
}
int nic_present(void){ return g_active != 0; }
const char *nic_name(void){ return g_active ? g_active->name : "(nenhuma)"; }
int nic_send(const void *data, int len){
    if (!g_active || !g_active->send) return -1;
    return g_active->send(data, len);
}
int nic_recv(uint8_t *out, int maxlen){
    if (!g_active || !g_active->recv) return -1;
    return g_active->recv(out, maxlen);
}
void nic_mac(uint8_t out[6]){
    if (!g_active || !g_active->mac){ for (int i=0;i<6;i++) out[i]=0; return; }
    g_active->mac(out);
}
