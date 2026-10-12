#include "conntrack.h"
#include <string.h>

static conntrack_entry_t g_tab[CONNTRACK_MAX];
static int g_next;

extern uint32_t sys_now(void);

void conntrack_init(void){ memset(g_tab, 0, sizeof(g_tab)); g_next = 0; }

int conntrack_open(int proto, const ip_addr_t *remote,
                   uint16_t rport, uint16_t lport, const char *desc)
{
    for (int i=0;i<CONNTRACK_MAX;i++){
        int idx = (g_next + i) % CONNTRACK_MAX;
        if (!g_tab[idx].active){
            g_tab[idx].active = 1;
            g_tab[idx].proto = proto;
            if (remote) g_tab[idx].remote = *remote;
            g_tab[idx].rport = rport;
            g_tab[idx].lport = lport;
            g_tab[idx].opened_ms = sys_now();
            g_tab[idx].closed_ms = 0;
            g_tab[idx].success = 0;
            int k=0;
            if (desc) while (desc[k] && k<23){ g_tab[idx].desc[k]=desc[k]; k++; }
            g_tab[idx].desc[k]=0;
            g_next = (idx+1) % CONNTRACK_MAX;
            return idx;
        }
    }
    /* tabela cheia: recicla o mais antigo */
    int oldest = 0;
    for (int i=1;i<CONNTRACK_MAX;i++)
        if (g_tab[i].opened_ms < g_tab[oldest].opened_ms) oldest = i;
    g_tab[oldest].active = 1;
    g_tab[oldest].proto = proto;
    if (remote) g_tab[oldest].remote = *remote;
    g_tab[oldest].rport = rport;
    g_tab[oldest].lport = lport;
    g_tab[oldest].opened_ms = sys_now();
    g_tab[oldest].closed_ms = 0;
    g_tab[oldest].success = 0;
    int k=0;
    if (desc) while (desc[k] && k<23){ g_tab[oldest].desc[k]=desc[k]; k++; }
    g_tab[oldest].desc[k]=0;
    return oldest;
}

void conntrack_close(int handle, int success){
    if (handle < 0 || handle >= CONNTRACK_MAX) return;
    g_tab[handle].closed_ms = sys_now();
    g_tab[handle].success = success;
    /* mantém active=1 para aparecer no histórico até ser reciclado */
}

int conntrack_count(void){
    int n=0;
    for (int i=0;i<CONNTRACK_MAX;i++) if (g_tab[i].active) n++;
    return n;
}

const char *conntrack_proto_name(int proto){
    switch(proto){
        case 1:  return "ICMP";
        case 6:  return "TCP";
        case 17: return "UDP";
    }
    return "?";
}

conntrack_entry_t *conntrack_get(int idx){
    if (idx < 0 || idx >= CONNTRACK_MAX) return 0;
    return &g_tab[idx];
}
