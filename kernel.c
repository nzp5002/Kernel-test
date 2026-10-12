/* ============================================================
 * kernel.c — Kernel i386 + lwIP + HTTP + DNS + conntrack + reboot
 *
 *   - Boot Multiboot com stack própria
 *   - VGA modo texto 80x25
 *   - Teclado PS/2 (polling)
 *   - Driver RTL8139
 *   - lwIP (NO_SYS=1, polling)
 *   - Shell interativo
 *   - Ping com resolução DNS
 *   - Tabela de conexões (conntrack)
 *   - Reboot via 8042 / triple fault
 * ============================================================ */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "lwip/timeouts.h"
#include "lwip/dns.h"
#include "lwip/raw.h"
#include "lwip/icmp.h"
#include "lwip/inet_chksum.h"
#include "lwip/sys.h"
#include "ata.h"
#include "fat.h"
#include "nic.h"
#include "netif/ethernet.h"
#include "httpd.h"
#include "conntrack.h"

/* ============================================================
 * Multiboot header
 * ============================================================ */
__attribute__((section(".multiboot"), used, aligned(4)))
static const uint32_t mb_header[] = {
    0x1BADB002, 0x00000000, (uint32_t)(-(0x1BADB002))
};

/* ============================================================
 * _start — configura stack e chama kernel_main
 * ============================================================ */
extern char __stack_top[];
__attribute__((naked, noreturn, section(".text.entry")))
extern char __bss_start[], __bss_end[];
void _start(void){
    __asm__ volatile(
        /* zero BSS */
        "movl $__bss_start, %edi\n"
        "movl $__bss_end, %ecx\n"
        "subl %edi, %ecx\n"
        "xorl %eax, %eax\n"
        "cld\n"
        "rep stosb\n"
        /* stack */
        "movl $__stack_top, %esp\n"
        "movl %esp, %ebp\n"
        "call kernel_main\n"
        "1: hlt\n"
        "jmp 1b\n"
    );
}

/* ============================================================
 * Port I/O
 * ============================================================ */
static inline void outb(uint16_t p, uint8_t v){ __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static inline uint8_t inb(uint16_t p){ uint8_t v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline void outw(uint16_t p, uint16_t v){ __asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p)); }
static inline uint16_t inw(uint16_t p){ uint16_t v; __asm__ volatile("inw %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline void outl(uint16_t p, uint32_t v){ __asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p)); }
static inline uint32_t inl(uint16_t p){ uint32_t v; __asm__ volatile("inl %1,%0":"=a"(v):"Nd"(p)); return v; }

uint64_t kernel_tsc(void){ uint32_t lo,hi; __asm__ volatile("rdtsc":"=a"(lo),"=d"(hi)); return ((uint64_t)hi<<32)|lo; }
uint64_t kernel_tsc_per_ms = 1000000000ULL;

/* ============================================================
 * Libc mínima
 * ============================================================ */
void *memcpy(void *d, const void *s, size_t n){
    uint8_t *dd=d; const uint8_t *ss=s;
    for (size_t i=0;i<n;i++) dd[i]=ss[i];
    return d;
}
void *memset(void *d, int c, size_t n){
    uint8_t *dd=d;
    for (size_t i=0;i<n;i++) dd[i]=(uint8_t)c;
    return d;
}
int memcmp(const void *a, const void *b, size_t n){
    const uint8_t *x=a,*y=b;
    for (size_t i=0;i<n;i++) if (x[i]!=y[i]) return (int)x[i]-(int)y[i];
    return 0;
}
void *memmove(void *d, const void *s, size_t n){
    uint8_t *dd=d; const uint8_t *ss=s;
    if (dd<ss) for (size_t i=0;i<n;i++) dd[i]=ss[i];
    else       for (size_t i=n;i>0;i--) dd[i-1]=ss[i-1];
    return d;
}
size_t strlen(const char *s){ size_t n=0; while(s[n]) n++; return n; }
int strcmp(const char *a, const char *b){ while(*a&&*a==*b){a++;b++;} return (int)(uint8_t)*a-(int)(uint8_t)*b; }
int strncmp(const char *a, const char *b, size_t n){
    for (size_t i=0;i<n;i++){ if (a[i]!=b[i]) return (int)(uint8_t)a[i]-(int)(uint8_t)b[i]; if(!a[i]) return 0; }
    return 0;
}
char *strcpy(char *d, const char *s){ char *r=d; while((*d++=*s++)) { } return r; }
char *strncpy(char *d, const char *s, size_t n){
    size_t i=0; for(;i<n&&s[i];i++) d[i]=s[i]; for(;i<n;i++) d[i]=0; return d;
}

int isdigit(int c){ return c>='0'&&c<='9'; }
int isxdigit(int c){ return isdigit(c)||(c>='a'&&c<='f')||(c>='A'&&c<='F'); }
int isalpha(int c){ return (c>='a'&&c<='z')||(c>='A'&&c<='Z'); }
int isalnum(int c){ return isalpha(c)||isdigit(c); }
int isspace(int c){ return c==' '||c=='\t'||c=='\n'||c=='\r'; }
int isupper(int c){ return c>='A'&&c<='Z'; }
int islower(int c){ return c>='a'&&c<='z'; }
int isprint(int c){ return c>=32&&c<127; }
int ispunct(int c){ return isprint(c)&&!isalnum(c)&&c!=' '; }
int iscntrl(int c){ return c<32||c==127; }
int toupper(int c){ return islower(c)?c-32:c; }
int tolower(int c){ return isupper(c)?c+32:c; }

unsigned long strtoul(const char *s, char **end, int base){
    while (isspace((unsigned char)*s)) s++;
    unsigned long v=0; int neg=0;
    if (*s=='-'){ neg=1; s++; } else if (*s=='+') s++;
    if (base==0){
        if (s[0]=='0'&&(s[1]=='x'||s[1]=='X')){ base=16; s+=2; }
        else if (s[0]=='0'){ base=8; s++; }
        else base=10;
    }
    while (*s){
        int c=*s, d;
        if (c>='0'&&c<='9') d=c-'0';
        else if (c>='a'&&c<='f') d=c-'a'+10;
        else if (c>='A'&&c<='F') d=c-'A'+10;
        else break;
        if (d>=base) break;
        v = v*(unsigned long)base + (unsigned long)d;
        s++;
    }
    if (end) *end=(char*)s;
    return neg ? (unsigned long)(-(long)v) : v;
}
int atoi(const char *s){ return (int)strtoul(s, 0, 10); }

int errno;

/* ---- Runtime 64-bit ---- */
uint64_t __udivdi3(uint64_t num, uint64_t den){
    if (den == 0) return 0;
    uint64_t quot = 0, qbit = 1;
    while ((int64_t)den >= 0){ den <<= 1; qbit <<= 1; }
    while (qbit){
        if (den <= num){ num -= den; quot += qbit; }
        den >>= 1; qbit >>= 1;
    }
    return quot;
}
uint64_t __umoddi3(uint64_t num, uint64_t den){
    if (den == 0) return 0;
    uint64_t qbit = 1;
    while ((int64_t)den >= 0){ den <<= 1; qbit <<= 1; }
    while (qbit){
        if (den <= num) num -= den;
        den >>= 1; qbit >>= 1;
    }
    return num;
}
int64_t __divdi3(int64_t a, int64_t b){
    int neg = 0;
    if (a < 0){ a = -a; neg ^= 1; }
    if (b < 0){ b = -b; neg ^= 1; }
    int64_t r = (int64_t)__udivdi3((uint64_t)a, (uint64_t)b);
    return neg ? -r : r;
}
int64_t __moddi3(int64_t a, int64_t b){
    int neg = 0;
    if (a < 0){ a = -a; neg ^= 1; }
    if (b < 0){ b = -b; }
    int64_t r = (int64_t)__umoddi3((uint64_t)a, (uint64_t)b);
    return neg ? -r : r;
}

/* ============================================================
 * VGA 80x25
 * ============================================================ */
#define VGA ((volatile uint16_t*)0xB8000)
#define VGA_W 80
#define VGA_H 25
static int vga_pos;

static void vga_scroll(void){
    for (int i=0;i<(VGA_H-1)*VGA_W;i++) VGA[i]=VGA[i+VGA_W];
    for (int i=(VGA_H-1)*VGA_W;i<VGA_H*VGA_W;i++) VGA[i]=0x0F20;
    vga_pos=(VGA_H-1)*VGA_W;
}
static void vga_clear(void){ for(int i=0;i<VGA_W*VGA_H;i++) VGA[i]=0x0F20; vga_pos=0; }
static void vga_putc(char c){
    if (c=='\n') vga_pos += VGA_W - (vga_pos % VGA_W);
    else if (c=='\r') vga_pos -= vga_pos % VGA_W;
    else if (c=='\b'){ if (vga_pos>0){ vga_pos--; VGA[vga_pos]=0x0F20; } }
    else VGA[vga_pos++] = (uint16_t)(uint8_t)c | 0x0F00;
    if (vga_pos >= VGA_W*VGA_H) vga_scroll();
}
static void vga_puts(const char *s){ while(*s) vga_putc(*s++); }
static void vga_hex(uint32_t v, int d){
    static const char h[]="0123456789abcdef";
    for (int i=(d-1)*4;i>=0;i-=4) vga_putc(h[(v>>i)&0xF]);
}
static void vga_dec(int v){
    char t[12]; int i=0,neg=0;
    if(v<0){neg=1;v=-v;}
    if(v==0){vga_putc('0');return;}
    while(v){t[i++]=(char)('0'+v%10);v/=10;}
    if(neg)vga_putc('-');
    while(i--)vga_putc(t[i]);
}

static void snprintf_ip4(char *b, const ip4_addr_t *a){
    int o=0;
    for (int i=0;i<4;i++){
        uint32_t v=(lwip_ntohl(a->addr)>>(24-8*i))&0xFF;
        char t[4]; int k=0;
        if(v==0)t[k++]='0';
        while(v){t[k++]=(char)('0'+v%10);v/=10;}
        while(k--) b[o++]=t[k];
        if(i<3) b[o++]='.';
    }
    b[o]=0;
}
static void print_ip4(const ip4_addr_t *a){ char b[20]; snprintf_ip4(b,a); vga_puts(b); }

/* ============================================================
 * Teclado PS/2
 * ============================================================ */
static const char kbd_map[128]={
    0,27,'1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,'\\','z','x','c','v','b','n','m',',','.','/',0,'*',0,' '
};
static int kbd_shift;
static int kbd_getc(void){
    if (!(inb(0x64)&1)) return -1;
    uint8_t sc = inb(0x60);
    if (sc==0x2A||sc==0x36){ kbd_shift=1; return -1; }
    if (sc==0xAA||sc==0xB6){ kbd_shift=0; return -1; }
    if (sc&0x80) return -1;
    if (sc>=128) return -1;
    char c = kbd_map[sc];
    if (kbd_shift && c>='a' && c<='z') c-=32;
    return (uint8_t)c;
}

/* ============================================================
 * PCI + RTL8139
 * ============================================================ */
static uint32_t pci_read32(uint8_t b,uint8_t s,uint8_t f,uint8_t o){
    outl(0xCF8, 0x80000000u|((uint32_t)b<<16)|((uint32_t)s<<11)|((uint32_t)f<<8)|(o&0xFC));
    return inl(0xCFC);
}
static void pci_write32(uint8_t b,uint8_t s,uint8_t f,uint8_t o,uint32_t v){
    outl(0xCF8, 0x80000000u|((uint32_t)b<<16)|((uint32_t)s<<11)|((uint32_t)f<<8)|(o&0xFC));
    outl(0xCFC, v);
}

#define RX_BUF_SZ (8192+16+1500)
#define NUM_TX 4
#define TX_SZ 2048

static uint16_t rtl_io;
uint8_t rtl_mac[6];
static uint8_t rx_buf[RX_BUF_SZ] __attribute__((aligned(16)));
static uint8_t tx_buf[NUM_TX][TX_SZ] __attribute__((aligned(16)));
static int rx_ptr, tx_cur;

#define R_IDR0    0x00
#define R_TSDR    0x04
#define R_CMD     0x37
#define R_IMR     0x3C
#define R_ISR     0x3E
#define R_RCR     0x44
#define R_TCR     0x40
#define R_RBSTART 0x30
#define R_CAPR    0x38
#define R_CONFIG1 0x52

static int rtl8139_find(void){
    for (int b=0;b<256;b++) for (int s=0;s<32;s++){
        uint32_t id = pci_read32((uint8_t)b,(uint8_t)s,0,0);
        if ((id&0xFFFF)==0xFFFF) continue;
        if ((id&0xFFFF)!=0x10EC || (id>>16)!=0x8139) continue;
        uint32_t bar = pci_read32((uint8_t)b,(uint8_t)s,0,0x10);
        if (!(bar&1)) continue;
        rtl_io = (uint16_t)(bar & 0xFFFC);
        uint32_t cmd = pci_read32((uint8_t)b,(uint8_t)s,0,0x04);
        pci_write32((uint8_t)b,(uint8_t)s,0,0x04, cmd|0x05);
        return 1;
    }
    return 0;
}

static void rtl8139_init(void){
    outb(rtl_io+R_CONFIG1, 0x00);
    outb(rtl_io+R_CMD, 0x10);
    while (inb(rtl_io+R_CMD)&0x10) { }
    for (int i=0;i<6;i++) rtl_mac[i]=inb(rtl_io+R_IDR0+i);
    outl(rtl_io+R_RBSTART, (uint32_t)(uintptr_t)rx_buf);
    outw(rtl_io+R_IMR, 0x0000);
    outw(rtl_io+R_ISR, 0xFFFF);
    outl(rtl_io+R_RCR, 0x0000000F | (1<<7));
    outl(rtl_io+R_TCR, 0x03000700);
    outb(rtl_io+R_CMD, 0x0C);
    rx_ptr = 0; tx_cur = 0;
}

int rtl8139_send_frame(const void *data, int len){
    if (len > TX_SZ) return -1;
    for (int i=0;i<len;i++) tx_buf[tx_cur][i]=((const uint8_t*)data)[i];
    if (len<60) for (int i=len;i<60;i++) tx_buf[tx_cur][i]=0;
    int total = len<60?60:len;
    outl(rtl_io+0x20+tx_cur*4, (uint32_t)(uintptr_t)tx_buf[tx_cur]);
    outl(rtl_io+0x10+tx_cur*4, (uint32_t)total);
    int spin = 1000000;
    while (!(inb(rtl_io+R_TSDR)&(1<<tx_cur)) && spin--) { }
    outb(rtl_io+R_TSDR, 1<<tx_cur);
    tx_cur = (tx_cur+1) % NUM_TX;
    return 0;
}

int rtl8139_recv_frame(uint8_t *out, int maxlen){
    if (inb(rtl_io+R_CMD)&0x01) return -1;
    uint16_t status = *(volatile uint16_t*)(rx_buf + rx_ptr);
    uint16_t length = *(volatile uint16_t*)(rx_buf + rx_ptr + 2);
    if (!(status&0x01) || length<60 || length>1518){
        outb(rtl_io+R_CMD, 0x01);
        rx_ptr = 0;
        return -1;
    }
    int pkt_off = rx_ptr + 4;
    int n = length > maxlen ? maxlen : length;
    for (int i=0;i<n;i++) out[i]=rx_buf[pkt_off+i];
    rx_ptr = (rx_ptr + 4 + length + 3) & ~3;
    rx_ptr %= 8192;
    outw(rtl_io+R_CAPR, (uint16_t)((rx_ptr-16)&0xFFFF));
    return length;
}

/* ============================================================
 * Adaptador RTL8139 para a NIC HAL
 * ============================================================ */
static int rtl_ops_probe(void){ return rtl8139_find(); }
static int rtl_ops_init (void){ rtl8139_init(); return 0; }
static int rtl_ops_send (const void *d, int l){ return rtl8139_send_frame(d, l); }
static int rtl_ops_recv (uint8_t *o, int m){ return rtl8139_recv_frame(o, m); }
static void rtl_ops_mac (uint8_t m[6]){ for (int i=0;i<6;i++) m[i]=rtl_mac[i]; }

nic_ops_t rtl8139_ops = {
    .name="rtl8139", .probe=rtl_ops_probe, .init=rtl_ops_init,
    .send=rtl_ops_send, .recv=rtl_ops_recv, .mac=rtl_ops_mac,
};

/* ============================================================
 * lwIP netif
 * ============================================================ */
extern err_t rtl_low_level_init(struct netif *netif);
extern void  rtl_poll_lwip(struct netif *netif);

static struct netif g_netif;

/* ============================================================
 * Estado global do shell
 * ============================================================ */
static char line_buf[128];
static int  line_len;
static ip4_addr_t cfg_ip, cfg_mask, cfg_gw;

/* ============================================================
 * Resolução de nome (IP literal ou DNS)
 * ============================================================ */
static volatile int dns_done;
static ip_addr_t dns_result;

static void dns_resolve_cb(const char *name, const ip_addr_t *ip, void *arg){
    (void)name; (void)arg;
    if (ip){ dns_result = *ip; dns_done = 1; }
    else    { dns_done = 2; }
}

/* Retorna 1 em sucesso, 0 em falha. Aceita IP literal. */
static int resolve_host(const char *host, ip_addr_t *out){
    if (ipaddr_aton(host, out)) return 1;
    dns_done = 0;
    err_t e = dns_gethostbyname(host, out, dns_resolve_cb, NULL);
    if (e == ERR_OK) return 1;
    if (e != ERR_INPROGRESS) return 0;
    uint32_t t0 = (uint32_t)sys_now();
    while ((uint32_t)sys_now() - t0 < 8000){
        rtl_poll_lwip(&g_netif);
        sys_check_timeouts();
        if (dns_done) break;
    }
    if (dns_done == 1){ *out = dns_result; return 1; }
    return 0;
}

/* ============================================================
 * Ping (ICMP echo) com suporte a hostname
 * ============================================================ */
static volatile int ping_got;
static ip_addr_t ping_src;
static uint16_t ping_id_ctr = 0x1000;

static uint8_t ping_recv(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr){
    (void)arg; (void)pcb; (void)addr;
    struct ip_hdr *iph = (struct ip_hdr*)p->payload;
    uint16_t iph_len = IPH_HL(iph)*4;
    if (p->len < iph_len + 8) { pbuf_free(p); return 1; }
    struct icmp_echo_hdr *icmp = (struct icmp_echo_hdr*)((uint8_t*)p->payload + iph_len);
    if (ICMPH_TYPE(icmp) == ICMP_ER){
        ping_got = 1;
        ping_src = *addr;
    }
    pbuf_free(p);
    return 1;
}

/* Executa ping e devolve resumo em 'out'. Retorna nº de replies. */
static int do_ping(const char *host, char *out, int outcap, int verbose){
    int o = 0;
    #define PO(s) do { const char *_s=(s); while (*_s && o<outcap-1) out[o++]=*_s++; } while(0)
    #define PO_DEC(v) do { \
        char _t[12]; int _i=0,_n=0; long _v=(long)(v); \
        if(_v<0){_n=1;_v=-_v;} \
        if(!_v)_t[_i++]='0'; \
        while(_v){_t[_i++]=(char)('0'+_v%10);_v/=10;} \
        if(_n)_t[_i++]='-'; \
        while(_i--&&o<outcap-1) out[o++]=_t[_i]; \
    } while(0)

    ip_addr_t dst;
    if (!resolve_host(host, &dst)){
        PO("nao resolve: "); PO(host); PO("\n");
        out[o]=0;
        return 0;
    }

    if (verbose){
        PO("PING "); PO(host);
        /* mostra IP resolvido se for hostname */
        int is_literal = 1;
        for (const char *q=host; *q; q++)
            if (!(isdigit((unsigned char)*q)||*q=='.')) { is_literal = 0; break; }
        if (!is_literal){
            char ipbuf[20]; snprintf_ip4(ipbuf, ip_2_ip4(&dst));
            PO(" ("); PO(ipbuf); PO(")");
        }
        PO("\n");
    }

    struct raw_pcb *pcb = raw_new(IP_PROTO_ICMP);
    if (!pcb){ PO("raw_new falhou\n"); out[o]=0; return 0; }
    raw_recv(pcb, ping_recv, NULL);
    raw_bind(pcb, IP_ADDR_ANY);

    int replies = 0;
    for (int i=1;i<=4;i++){
        struct pbuf *p = pbuf_alloc(PBUF_IP, sizeof(struct icmp_echo_hdr)+16, PBUF_RAM);
        if (!p){ PO("pbuf_alloc falhou\n"); break; }
        struct icmp_echo_hdr *ie = (struct icmp_echo_hdr*)p->payload;
        ICMPH_TYPE_SET(ie, ICMP_ECHO);
        ICMPH_CODE_SET(ie, 0);
        ie->id = htons(ping_id_ctr);
        ie->seqno = htons((uint16_t)i);
        ie->chksum = 0;
        for (int k=0;k<16;k++) ((uint8_t*)p->payload)[8+k] = (uint8_t)k;
        ie->chksum = inet_chksum(ie, p->len);

        /* registra conexão */
        int h = conntrack_open(1, &dst, 0, 0, host);

        ping_got = 0;
        raw_sendto(pcb, p, &dst);
        pbuf_free(p);

        uint32_t t0 = (uint32_t)sys_now();
        while ((uint32_t)sys_now() - t0 < 2000){
            rtl_poll_lwip(&g_netif);
            sys_check_timeouts();
            if (ping_got) break;
        }
        PO("  seq="); PO_DEC(i);
        if (ping_got){
            PO(" reply de ");
            char ipbuf[20]; snprintf_ip4(ipbuf, ip_2_ip4(&ping_src)); PO(ipbuf);
            PO(" tempo="); PO_DEC((int)((uint32_t)sys_now()-t0)); PO(" ms\n");
            replies++;
            conntrack_close(h, 1);
        } else {
            PO(" timeout\n");
            conntrack_close(h, 0);
        }
    }
    raw_remove(pcb);
    PO("  "); PO_DEC(replies); PO("/4 pacotes recebidos\n");
    out[o]=0;
    #undef PO
    #undef PO_DEC
    return replies;
}

/* ============================================================
 * Handlers expostos ao HTTP
 * ============================================================ */
static int http_ping_handler(const char *host, char *out, int outcap){
    return do_ping(host, out, outcap, 1) > 0;
}

static int http_dns_handler(const char *name, char *out, int outcap){
    int o=0;
    #define PO(s) do { const char *_s=(s); while (*_s && o<outcap-1) out[o++]=*_s++; } while(0)
    ip_addr_t a;
    int h = conntrack_open(17, 0, 53, 0, name);
    if (!resolve_host(name, &a)){
        PO("falha: "); PO(name); PO("\n");
        conntrack_close(h, 0);
        out[o]=0; return 0;
    }
    PO(name); PO(" -> ");
    char ipbuf[20]; snprintf_ip4(ipbuf, ip_2_ip4(&a)); PO(ipbuf);
    PO("\n");
    conntrack_close(h, 1);
    out[o]=0;
    #undef PO
    return 1;
}

static void http_reboot_handler(void){
    __asm__ volatile ("cli");
    /* espera teclado ocioso */
    while (inb(0x64) & 0x02) { }
    outb(0x64, 0xFE);   /* reset via controlador 8042 */
    /* fallback: triple fault */
    struct { uint16_t limit; uint32_t base; } __attribute__((packed)) null_idt = {0, 0};
    __asm__ volatile("lidt %0" : : "m"(null_idt));
    __asm__ volatile("int $0x03");
    for(;;) __asm__ volatile("hlt");
}

static void http_log_cb(const char *msg){
    vga_puts("[http] "); vga_puts(msg); vga_putc('\n');
}

/* ============================================================
 * Banner (CP437)
 * ============================================================ */
static void cmd_banner(void){
    vga_puts("             \xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC             \n");
    vga_puts("         \xDC\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDC         \n");
    vga_puts("       \xDC\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDC       \n");
    vga_puts("      \xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB      \n");
    vga_puts("     \xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB     \n");
    vga_puts("    \xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB    \n");
    vga_puts("    \xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB  \xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB  \xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB    \n");
    vga_puts("    \xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB   \xDB\xDB\xDB   \xDB\xDB   \xDB\xDB\xDB   \xDB\xDB\xDB\xDB\xDB\xDB\xDB    \n");
    vga_puts("    \xDB\xDB\xDB\xDB\xDB    \xDB\xDB\xDB\xDB\xDB   \xDB\xDB   \xDB\xDB\xDB\xDB\xDB    \xDB\xDB\xDB\xDB    \n");
    vga_puts("     \xDB\xDB\xDB\xDB\xDB     \xDB\xDB     \xDB\xDB     \xDB\xDB\xDB\xDB\xDB    \n");
    vga_puts("      \xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB     \n");
    vga_puts("       \xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB      \n");
    vga_puts("        \xDB\xDB\xDB\xDB\xDB  \xDB\xDB\xDB\xDB\xDB\xDB  \xDB\xDB\xDB\xDB\xDB       \n");
    vga_puts("        \xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB        \n");
    vga_puts("         \xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB           \n");
    vga_puts("          \xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB              \n");
    vga_puts("\n");
    vga_puts("      [ K E R N E L ]  v1.0\n");
    vga_puts("      >> acesso restrito :: nao autorizado <<\n\n");
}

/* ============================================================
 * Comandos
 * ============================================================ */
static void cmd_help(void){
    vga_puts("Comandos:\n");
    vga_puts("  help                     - esta ajuda\n");
    vga_puts("  clear                    - limpa tela\n");
    vga_puts("  mac                      - MAC da NIC\n");
    vga_puts("  ip show                  - IPv4 atual\n");
    vga_puts("  ip set <a>/<pfx>         - define IPv4\n");
    vga_puts("  dns show                 - lista servidores DNS\n");
    vga_puts("  dns set <ip>             - define DNS\n");
    vga_puts("  dns clear                - apaga todos os DNS\n");
    vga_puts("  dns query <nome>         - resolve nome (A record)\n");
    vga_puts("  ping4 <ip|host>          - ping IPv4 (aceita hostname)\n");
    vga_puts("  conn                     - conexoes recentes\n");
    vga_puts("  conn clear               - limpa tabela de conexoes\n");
    vga_puts("  http start [porta]       - inicia servidor web\n");
    vga_puts("  http stop                - para o servidor web\n");
    vga_puts("  http status              - estado do httpd\n");
    vga_puts("  http auth on|off         - liga/desliga login\n");
    vga_puts("  http user <nome>         - muda usuario\n");
    vga_puts("  http pass <senha>        - muda senha\n");
    vga_puts("  http set <u> <s>         - muda ambos\n");
    vga_puts("  http reset               - volta para admin/admin\n");
    vga_puts("  http config              - mostra config atual\n");
    vga_puts("  reboot                   - reinicia o sistema\n");
}

static void cmd_ip(const char *sub, const char *val){
    if (!sub || strcmp(sub,"show")==0){
        vga_puts("IPv4: "); print_ip4(&cfg_ip);
        vga_puts("/");     print_ip4(&cfg_mask);
        vga_puts("  gw="); print_ip4(&cfg_gw);
        vga_putc('\n');
        return;
    }
    if (strcmp(sub,"set")==0 && val){
        char tmp[32]; int i=0;
        while (val[i] && val[i]!='/' && i<31){ tmp[i]=val[i]; i++; }
        tmp[i]=0;
        ip4_addr_t a;
        if (!ip4addr_aton(tmp, &a)){ vga_puts("IP invalido\n"); return; }
        cfg_ip = a;
        if (val[i]=='/'){
            int pfx=0, j=i+1;
            while (val[j]>='0'&&val[j]<='9'){ pfx=pfx*10+(val[j]-'0'); j++; }
            uint32_t m = pfx==0?0:(0xFFFFFFFFu << (32-pfx));
            ip4_addr_set_u32(&cfg_mask, lwip_htonl(m));
        }
        netif_set_ipaddr(&g_netif, &cfg_ip);
        netif_set_netmask(&g_netif, &cfg_mask);
        netif_set_gw(&g_netif, &cfg_gw);
        vga_puts("IPv4 atualizado\n");
        return;
    }
    vga_puts("uso: ip show | ip set <a>/<p>\n");
}

static void cmd_dns(const char *sub, const char *val){
    if (!sub || strcmp(sub,"show")==0){
        vga_puts("DNS servers:\n");
        for (int i=0;i<DNS_MAX_SERVERS;i++){
            const ip_addr_t *a = dns_getserver((uint8_t)i);
            if (a && !ip_addr_isany(a)){
                vga_puts("  "); print_ip4(ip_2_ip4(a)); vga_putc('\n');
            }
        }
        return;
    }
    if (strcmp(sub,"set")==0 && val){
        ip_addr_t a;
        if (!ipaddr_aton(val, &a)){ vga_puts("IP invalido\n"); return; }
        dns_setserver(0, &a);
        vga_puts("DNS definido\n");
        return;
    }
    if (strcmp(sub,"clear")==0){
        ip_addr_t any; IP_ADDR4(&any, 0, 0, 0, 0);
        for (int i=0;i<DNS_MAX_SERVERS;i++) dns_setserver((uint8_t)i, &any);
        vga_puts("DNS limpos\n");
        return;
    }
    if (strcmp(sub,"query")==0 && val){
        ip_addr_t a;
        vga_puts("Resolvendo "); vga_puts(val); vga_puts("...\n");
        int h = conntrack_open(17, 0, 53, 0, val);
        if (resolve_host(val, &a)){
            vga_puts("  A = "); print_ip4(ip_2_ip4(&a)); vga_putc('\n');
            conntrack_close(h, 1);
        } else {
            vga_puts("  falha\n");
            conntrack_close(h, 0);
        }
        return;
    }
    vga_puts("uso: dns show|set|clear|query\n");
}

static void cmd_conn(const char *sub){
    if (sub && strcmp(sub,"clear")==0){
        conntrack_init();
        vga_puts("conexoes limpas\n");
        return;
    }
    vga_puts("Conexoes recentes:\n");
    vga_puts("  #  proto  local      remoto              desc              dur     status\n");
    int any = 0;
    for (int i=0;i<CONNTRACK_MAX;i++){
        conntrack_entry_t *e = conntrack_get(i);
        if (!e || !e->active) continue;
        any = 1;
        vga_putc(' '); if(i<10) vga_putc(' '); vga_dec(i); vga_puts("  ");
        vga_puts(conntrack_proto_name(e->proto));
        /* padding fixo */
        int pn = (int)strlen(conntrack_proto_name(e->proto));
        for (int k=pn;k<7;k++) vga_putc(' ');
        vga_putc(':'); vga_dec(e->lport);
        for (int k=0;k<3;k++) vga_putc(' ');
        uint32_t a = lwip_ntohl(e->remote.addr);
        vga_dec((int)((a>>24)&0xFF)); vga_putc('.');
        vga_dec((int)((a>>16)&0xFF)); vga_putc('.');
        vga_dec((int)((a>>8)&0xFF));  vga_putc('.');
        vga_dec((int)(a&0xFF));
        vga_putc(':'); vga_dec(e->rport);
        for (int k=0;k<2;k++) vga_putc(' ');
        vga_puts(e->desc);
        int dl = (int)strlen(e->desc);
        for (int k=dl;k<16;k++) vga_putc(' ');
        if (e->closed_ms){
            vga_dec((int)(e->closed_ms - e->opened_ms));
            vga_puts("ms  ");
            vga_puts(e->success ? "ok\n" : "falhou\n");
        } else {
            vga_puts("em curso ");
            vga_puts("-\n");
        }
    }
    if (!any) vga_puts("  (vazio)\n");
}

static void cmd_http(int argc, char **argv){
    if (argc < 2){
        vga_puts("uso: http start|stop|status|auth|user|pass|set|reset|config\n");
        return;
    }
    if (strcmp(argv[1],"start")==0){
        unsigned short port = 80;
        if (argc > 2){
            int pp=0;
            for (int k=0; argv[2][k]; k++)
                if (argv[2][k]>='0' && argv[2][k]<='9')
                    pp = pp*10 + (argv[2][k]-'0');
            if (pp > 0) port = (unsigned short)pp;
        }
        err_t e = httpd_start(port);
        if (e == ERR_OK){
            vga_puts("HTTP: rodando em porta ");
            vga_dec(port);
            vga_puts("  -> http://10.0.2.15/\n");
        } else if (e == ERR_ALREADY){
            vga_puts("HTTP: ja rodando\n");
        } else {
            vga_puts("HTTP: falha ("); vga_dec((int)e); vga_puts(")\n");
        }
    }
    else if (strcmp(argv[1],"stop")==0){
        httpd_stop();
        vga_puts("HTTP: parado\n");
    }
    else if (strcmp(argv[1],"status")==0){
        vga_puts("HTTP status:\n");
        vga_puts("  rodando: "); vga_puts(httpd_running()?"sim\n":"nao\n");
        vga_puts("  porta:   "); vga_dec(httpd_get_port()); vga_putc('\n');
        vga_puts("  auth:    "); vga_puts(httpd_get_auth()?"on\n":"off\n");
        vga_puts("  user:    "); vga_puts(httpd_get_username()); vga_putc('\n');
        vga_puts("  reqs:    "); vga_dec(httpd_get_request_count()); vga_putc('\n');
        vga_puts("  falhas:  "); vga_dec(httpd_get_auth_fail_count()); vga_putc('\n');
        vga_puts("  conexoes:"); vga_dec(httpd_get_conn_count()); vga_putc('\n');
    }
    else if (strcmp(argv[1],"auth")==0){
        if (argc < 3){ vga_puts("uso: http auth on|off\n"); }
        else if (strcmp(argv[2],"on")==0){ httpd_set_auth(1); vga_puts("HTTP auth: ON\n"); }
        else if (strcmp(argv[2],"off")==0){ httpd_set_auth(0); vga_puts("HTTP auth: OFF\n"); }
        else vga_puts("uso: http auth on|off\n");
    }
    else if (strcmp(argv[1],"user")==0 && argc>2){
        httpd_set_credentials(argv[2], 0);
        vga_puts("HTTP usuario: "); vga_puts(httpd_get_username()); vga_putc('\n');
    }
    else if (strcmp(argv[1],"pass")==0 && argc>2){
        httpd_set_credentials(0, argv[2]);
        vga_puts("HTTP senha alterada\n");
    }
    else if (strcmp(argv[1],"set")==0 && argc>3){
        httpd_set_credentials(argv[2], argv[3]);
        vga_puts("HTTP credenciais atualizadas\n");
    }
    else if (strcmp(argv[1],"reset")==0){
        httpd_init();
        vga_puts("HTTP reset: user=admin pass=admin auth=on\n");
    }
    else if (strcmp(argv[1],"config")==0){
        vga_puts("HTTP config:\n");
        vga_puts("  user: "); vga_puts(httpd_get_username()); vga_putc('\n');
        vga_puts("  pass: (oculta)\n");
        vga_puts("  auth: "); vga_puts(httpd_get_auth()?"on":"off"); vga_putc('\n');
        vga_puts("  porta:"); vga_dec(httpd_get_port()); vga_putc('\n');
    }
    else {
        vga_puts("uso: http start|stop|status|auth|user|pass|set|reset|config\n");
    }
}

/* ============================================================
 * Dispatcher de comandos
 * ============================================================ */
static void run_command(char *line){
    char *argv[8]; int argc=0;
    char *p = line;
    while (*p && argc<8){
        while (*p==' ') p++;
        if (!*p) break;
        argv[argc++]=p;
        while (*p && *p!=' ') p++;
        if (*p) *p++=0;
    }
    if (argc==0) return;

    if      (strcmp(argv[0],"help")==0)  cmd_help();
    else if (strcmp(argv[0],"clear")==0) { vga_clear(); return; }
    else if (strcmp(argv[0],"mac")==0){
        for (int i=0;i<6;i++){ vga_hex(rtl_mac[i],2); if (i<5) vga_putc(':'); }
        vga_putc('\n');
    }
    else if (strcmp(argv[0],"reboot")==0){
        vga_puts("reiniciando...\n");
        http_reboot_handler();
    }
    else if (strcmp(argv[0],"ip")==0)
        cmd_ip(argc>1?argv[1]:0, argc>2?argv[2]:0);
    else if (strcmp(argv[0],"dns")==0)
        cmd_dns(argc>1?argv[1]:0, argc>2?argv[2]:0);
    else if (strcmp(argv[0],"ping4")==0 && argc>1){
        char buf[1024];
        do_ping(argv[1], buf, sizeof(buf), 1);
        vga_puts(buf);
    }
    else if (strcmp(argv[0],"conn")==0)
        cmd_conn(argc>1?argv[1]:0);
    else if (strcmp(argv[0],"http")==0)
        cmd_http(argc, argv);
    else {
        vga_puts("comando desconhecido: ");
        vga_puts(argv[0]);
        vga_putc('\n');
    }
}

/* ============================================================
 * kernel_main
 * ============================================================ */
/* callback do shell FS */
void ls_cb_print(const fat_dirent_t *e, void *user){
    (void)user;
    vga_puts(e->name);
    int n = (int)strlen(e->name);
    for (int i=n;i<16;i++) vga_putc(' ');
    vga_dec((int)e->size);
    for (int i=0;i<6;i++) vga_putc(' ');
    vga_puts(e->is_dir ? "<DIR>" : "file");
    vga_putc('\n');
}

void kernel_main(void){
    /* MARCADOR — escreve direto em 0xB8000, sem passar por vga_clear */
    volatile unsigned short *dbg = (unsigned short *)0xB8000;
    dbg[0]  = 0x1B4B;  /* 'K' fundo azul */
    dbg[1]  = 0x1B58;  /* 'X' fundo azul */
    dbg[80] = 0x1B4B;
    dbg[81] = 0x1B21;  /* '!' */
    __asm__ volatile ("cli");
    vga_clear();
    cmd_banner();

    vga_puts("[1] TSC fixo\n");
    kernel_tsc_per_ms = 1000000000ULL;

    vga_puts("[2] procurando NIC\n");
    if (nic_init() != 0){
        vga_puts("Nenhuma NIC compativel encontrada\n");
        for(;;) __asm__ volatile("hlt");
    }
    vga_puts("[3] NIC ativa: "); vga_puts(nic_name()); vga_putc('\n');

    nic_mac(rtl_mac);   /* reusa o buffer global rtl_mac */
    vga_puts("[4] MAC: ");
    for (int i=0;i<6;i++){ vga_hex(rtl_mac[i],2); if (i<5) vga_putc(':'); }
    vga_putc('\n');

    vga_puts("[5] lwip_init\n");
    lwip_init();

    /* HTTP daemon: inicializa defaults e registra callbacks */
    httpd_init();
    httpd_set_logger(http_log_cb);
    httpd_set_ping_handler(http_ping_handler);
    httpd_set_dns_handler(http_dns_handler);
    httpd_set_reboot_handler(http_reboot_handler);

    /* Tabela de conexões */
    conntrack_init();

    vga_puts("[6] netif_add\n");
    IP4_ADDR(&cfg_ip,   10, 0, 2, 15);
    IP4_ADDR(&cfg_mask, 255,255,255, 0);
    IP4_ADDR(&cfg_gw,   10, 0, 2,  2);

    netif_add(&g_netif, &cfg_ip, &cfg_mask, &cfg_gw, NULL,
              rtl_low_level_init, ethernet_input);
    netif_set_default(&g_netif);
    netif_set_up(&g_netif);

    ip_addr_t dns_srv;
    IP_ADDR4(&dns_srv, 10, 0, 2, 3);
    dns_setserver(0, &dns_srv);

    vga_puts("[7] ATA/FAT\n");
    if (ata_init() && fat_mount() == 0){
        uint32_t tk=0, fk=0;
        fat_df(&tk, &fk);
        vga_puts("FS: FAT16 montado (");
        vga_dec((int)tk); vga_puts(" KB, ");
        vga_dec((int)fk); vga_puts(" KB livres)\n");
    } else {
        vga_puts("FS: disco ausente\n");
    }
    vga_puts("[8] pronto\n");
    vga_puts("IPv4: "); print_ip4(&cfg_ip); vga_putc('\n');
    vga_puts("gw  : "); print_ip4(&cfg_gw); vga_putc('\n');
    vga_puts("dns : "); print_ip4(ip_2_ip4(&dns_srv)); vga_putc('\n');
    vga_puts("\nDigite 'help'.\n\n> ");

    /* ============================================================
     * Loop principal: polling de rede + teclado
     * ============================================================ */
    for (;;){
        rtl_poll_lwip(&g_netif);
        sys_check_timeouts();

        int c = kbd_getc();
        if (c >= 0){
            if (c == '\n'){
                vga_putc('\n');
                line_buf[line_len] = 0;
                run_command(line_buf);
                line_len = 0;
                vga_puts("> ");
            } else if (c == '\b'){
                if (line_len > 0){ line_len--; vga_putc('\b'); }
            } else if (c >= 32 && c < 127 && line_len < 120){
                line_buf[line_len++] = (char)c;
                vga_putc((char)c);
            }
        }
    }
}
