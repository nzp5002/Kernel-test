#include "lwip/opt.h"
#include "lwip/netif.h"
#include "lwip/etharp.h"
#include "lwip/ethip6.h"
#include "lwip/pbuf.h"
#include "netif/ethernet.h"

extern unsigned char rtl_mac[6];
extern int nic_send(const void *data, int len);
extern int nic_recv(unsigned char *out, int maxlen);

static err_t rtl_low_level_output(struct netif *netif, struct pbuf *p) {
    (void)netif;
    static unsigned char buf[1600];
    if (p->tot_len > sizeof(buf)) return ERR_MEM;

    unsigned char *q = buf;
    for (struct pbuf *cur = p; cur; cur = cur->next) {
        for (u16_t i = 0; i < cur->len; i++)
            *q++ = ((unsigned char*)cur->payload)[i];
    }
    if (nic_send(buf, p->tot_len) < 0) return ERR_IF;
    return ERR_OK;
}

err_t rtl_low_level_init(struct netif *netif) {
    netif->name[0] = 'e';
    netif->name[1] = '0';
    netif->hwaddr_len = 6;
    for (int i = 0; i < 6; i++) netif->hwaddr[i] = rtl_mac[i];
    netif->mtu = 1500;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP |
                   NETIF_FLAG_ETHERNET | NETIF_FLAG_LINK_UP;
#if LWIP_IPV6
    netif->flags |= NETIF_FLAG_MLD6;
#endif
    netif->output     = etharp_output;
#if LWIP_IPV6
    netif->output_ip6 = ethip6_output;
#endif
    netif->linkoutput = rtl_low_level_output;
    return ERR_OK;
}

void rtl_poll_lwip(struct netif *netif) {
    static unsigned char buf[1600];
    int n = nic_recv(buf, sizeof(buf));
    if (n <= 0) return;

    struct pbuf *p = pbuf_alloc(PBUF_RAW, n, PBUF_POOL);
    if (!p) return;
    if (pbuf_take(p, buf, n) != ERR_OK) { pbuf_free(p); return; }
    if (netif->input(p, netif) != ERR_OK) pbuf_free(p);
}
