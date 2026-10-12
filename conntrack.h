#ifndef CONNTRACK_H
#define CONNTRACK_H
#include "lwip/ip_addr.h"
#include <stdint.h>

#define CONNTRACK_MAX 16

typedef struct {
    int        active;
    int        proto;        /* 6=TCP, 17=UDP, 1=ICMP */
    ip_addr_t  remote;
    uint16_t   rport;
    uint16_t   lport;
    uint32_t   opened_ms;
    uint32_t   closed_ms;
    char       desc[24];
    int        success;
} conntrack_entry_t;

void conntrack_init(void);
int  conntrack_open(int proto, const ip_addr_t *remote,
                    uint16_t rport, uint16_t lport, const char *desc);
void conntrack_close(int handle, int success);
int  conntrack_count(void);
const char *conntrack_proto_name(int proto);
conntrack_entry_t *conntrack_get(int idx);   /* 0..CONNTRACK_MAX-1 */

#endif
