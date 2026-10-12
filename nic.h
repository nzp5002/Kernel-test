#ifndef NIC_H
#define NIC_H
#include <stdint.h>

typedef struct {
    const char *name;
    int  (*probe)(void);
    int  (*init)(void);
    int  (*send)(const void *data, int len);
    int  (*recv)(uint8_t *out, int maxlen);
    void (*mac)(uint8_t out[6]);
} nic_ops_t;

int  nic_init(void);
int  nic_present(void);
const char *nic_name(void);
int  nic_send(const void *data, int len);
int  nic_recv(uint8_t *out, int maxlen);
void nic_mac(uint8_t out[6]);

uint32_t nic_pci_read32 (uint8_t b, uint8_t s, uint8_t f, uint8_t o);
void     nic_pci_write32(uint8_t b, uint8_t s, uint8_t f, uint8_t o, uint32_t v);

#endif
