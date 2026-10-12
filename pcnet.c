/* pcnet.c — AMD PCnet-FAST III (stub — não implementado) */
#include "nic.h"
#include <stddef.h>

static int pcnet_probe(void){ return 0; }
static int pcnet_init (void){ return -1; }
static int pcnet_send (const void *d, int l){ (void)d; (void)l; return -1; }
static int pcnet_recv (uint8_t *o, int m){ (void)o; (void)m; return -1; }
static void pcnet_mac (uint8_t m[6]){ for (int i=0;i<6;i++) m[i]=0; }

nic_ops_t pcnet_ops = {
    .name="pcnet", .probe=pcnet_probe, .init=pcnet_init,
    .send=pcnet_send, .recv=pcnet_recv, .mac=pcnet_mac,
};
