#include "lwip/tcp.h"
#include "lwip/ip_addr.h"
#include "lwip/sys.h"
#include "conntrack.h"
#include <stdint.h>
#include <stddef.h>
#include "httpd.h"

extern void  *memcpy(void*, const void*, size_t);
extern void  *memset(void*, int, size_t);
extern int    memcmp(const void*, const void*, size_t);
extern size_t strlen(const char*);
extern int    strcmp(const char*, const char*);
extern int    strncmp(const char*, const char*, size_t);
extern int    tolower(int);

static const char *my_strstr(const char *h, const char *n){
    if (!*n) return h;
    size_t nlen = strlen(n);
    while (*h){ if (strncmp(h,n,nlen)==0) return h; h++; }
    return 0;
}
static int ci_ncmp(const char *a, const char *b, size_t n){
    for (size_t i=0;i<n;i++){
        int ca=tolower((unsigned char)a[i]), cb=tolower((unsigned char)b[i]);
        if (ca!=cb) return ca-cb;
        if (!ca) return 0;
    }
    return 0;
}
static int hexval(int c){
    if (c>='0'&&c<='9') return c-'0';
    if (c>='a'&&c<='f') return c-'a'+10;
    if (c>='A'&&c<='F') return c-'A'+10;
    return -1;
}

#define USER_MAX 31
#define PASS_MAX 31

typedef struct {
    char user[USER_MAX+1];
    char pass[PASS_MAX+1];
    int  auth, port, req_count, auth_fail, up;
} httpd_cfg_t;

static httpd_cfg_t g_cfg;
static struct tcp_pcb *g_listen;
static httpd_log_fn g_log;
static httpd_ping_cb g_ping_fn;
static httpd_dns_cb2 g_dns_fn;
static void (*g_reboot_fn)(void);

void httpd_set_logger(httpd_log_fn fn){ g_log = fn; }
static void logmsg(const char *s){ if (g_log) g_log(s); }
void httpd_set_ping_handler(httpd_ping_cb fn){ g_ping_fn = fn; }
void httpd_set_dns_handler(httpd_dns_cb2 fn){ g_dns_fn = fn; }
void httpd_set_reboot_handler(void (*fn)(void)){ g_reboot_fn = fn; }

typedef struct { char *buf; int cap, len; } ob_t;
static void ob_putc(ob_t *o, char c){ if (o->len < o->cap-1) o->buf[o->len++]=c; }
static void ob_puts(ob_t *o, const char *s){ while (*s) ob_putc(o, *s++); }
static void ob_putdec(ob_t *o, int v){
    char t[12]; int i=0, neg=0;
    if (v<0){ neg=1; v=-v; }
    if (v==0){ ob_putc(o,'0'); return; }
    while (v){ t[i++]=(char)('0'+v%10); v/=10; }
    if (neg) ob_putc(o,'-');
    while (i--) ob_putc(o, t[i]);
}
static void ob_puthex(ob_t *o, uint32_t v, int d){
    static const char h[]="0123456789abcdef";
    for (int i=(d-1)*4;i>=0;i-=4) ob_putc(o, h[(v>>i)&0xF]);
}
static void ob_html(ob_t *o, const char *s){
    for (; *s; s++){
        if (*s=='<') ob_puts(o,"&lt;");
        else if (*s=='>') ob_puts(o,"&gt;");
        else if (*s=='&') ob_puts(o,"&amp;");
        else if (*s=='"') ob_puts(o,"&quot;");
        else ob_putc(o,*s);
    }
}

static int b64_decode(const char *in, int inlen, char *out, int outcap){
    static const char tbl[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int i=0,o=0; unsigned val=0; int bits=0;
    while (i<inlen && in[i]!='=' && o<outcap-1){
        int c=(unsigned char)in[i++]; int idx=-1;
        for (int j=0;j<64;j++) if (tbl[j]==c){ idx=j; break; }
        if (idx<0) continue;
        val=(val<<6)|(unsigned)idx; bits+=6;
        if (bits>=8){ bits-=8; out[o++]=(char)((val>>bits)&0xFF); }
    }
    out[o]=0; return o;
}
static void url_decode(const char *src, int srclen, char *dst, int dstcap){
    int i=0,o=0;
    while (i<srclen && o<dstcap-1){
        char c=src[i++];
        if (c=='+'){ dst[o++]=' '; continue; }
        if (c=='%' && i+1<srclen){
            int hi=hexval(src[i]), lo=hexval(src[i+1]);
            if (hi>=0 && lo>=0){ dst[o++]=(char)((hi<<4)|lo); i+=2; continue; }
        }
        dst[o++]=c;
    }
    dst[o]=0;
}
static int form_get(const char *body, int bodylen, const char *key, char *out, int outcap){
    int klen=(int)strlen(key), i=0;
    while (i<bodylen){
        if (ci_ncmp(body+i,key,(size_t)klen)==0 && body[i+klen]=='='){
            int end=i+klen+1;
            while (end<bodylen && body[end]!='&') end++;
            url_decode(body+i+klen+1, end-(i+klen+1), out, outcap);
            return 1;
        }
        while (i<bodylen && body[i]!='&') i++;
        if (i<bodylen && body[i]=='&') i++;
    }
    return 0;
}
static const char *query_val(const char *q, const char *key, char *out, int outcap){
    if (!q) return 0;
    int klen=(int)strlen(key);
    while (*q){
        if (strncmp(q,key,(size_t)klen)==0 && q[klen]=='='){
            const char *v=q+klen+1; int i=0;
            while (v[i] && v[i]!='&' && i<outcap-1){ out[i]=v[i]; i++; }
            out[i]=0;
            /* url decode in place approximation */
            char tmp[256]; url_decode(out, i, tmp, sizeof(tmp));
            int k=0; while (tmp[k] && k<outcap-1){ out[k]=tmp[k]; k++; }
            out[k]=0;
            return out;
        }
        while (*q && *q!='&') q++;
        if (*q=='&') q++;
    }
    return 0;
}

#define MAX_CONN 4
#define REQBUF_SZ 4096

typedef struct {
    int in_use;
    struct tcp_pcb *pcb;
    char buf[REQBUF_SZ];
    int  len, headers_done, content_length, handled;
} conn_t;

static conn_t g_conns[MAX_CONN];
static int     g_conn_count;

static conn_t *conn_find(struct tcp_pcb *pcb){
    for (int i=0;i<MAX_CONN;i++)
        if (g_conns[i].in_use && g_conns[i].pcb==pcb) return &g_conns[i];
    return 0;
}
static conn_t *conn_alloc(struct tcp_pcb *pcb){
    for (int i=0;i<MAX_CONN;i++) if (!g_conns[i].in_use){
        g_conns[i].in_use=1; g_conns[i].pcb=pcb;
        g_conns[i].len=0; g_conns[i].headers_done=0;
        g_conns[i].content_length=0; g_conns[i].handled=0;
        tcp_arg(pcb, &g_conns[i]);
        g_conn_count++;
        return &g_conns[i];
    }
    return 0;
}
static void conn_free(struct tcp_pcb *pcb){
    conn_t *c = conn_find(pcb);
    if (c){ c->in_use=0; c->pcb=0; if (g_conn_count>0) g_conn_count--; }
}

/* ---- HTML ---- */
static const char CSS[] =
    "<style>"
    "body{font-family:monospace;background:#0b0b0b;color:#d0d0d0;margin:0;padding:20px;max-width:1000px}"
    "h1{color:#0ff;border-bottom:1px solid #0ff;padding-bottom:8px}"
    "h2{color:#ff0;margin-top:24px}"
    "a{color:#0ff;text-decoration:none}a:hover{text-decoration:underline}"
    "pre,code{background:#000;color:#0f0;padding:6px 10px;border-radius:4px}"
    "pre{overflow-x:auto}"
    "table{border-collapse:collapse;margin:8px 0;width:100%}"
    "td,th{border:1px solid #333;padding:5px 12px;text-align:left}"
    "th{background:#111;color:#0ff}"
    "input,button{font-family:monospace;background:#111;color:#0f0;border:1px solid #333;padding:6px 10px;border-radius:3px}"
    "input:focus{outline:1px solid #0ff;border-color:#0ff}"
    "button{cursor:pointer}button:hover{background:#1a1a1a}"
    ".ok{color:#0f0;background:#012;padding:6px 12px;border-left:3px solid #0f0;margin:10px 0}"
    ".err{color:#f55;background:#200;padding:6px 12px;border-left:3px solid #f55;margin:10px 0}"
    ".warn{color:#fa0;background:#210;padding:6px 12px;border-left:3px solid #fa0;margin:10px 0}"
    ".nav a{margin-right:16px}"
    ".ok-t{color:#0f0}.err-t{color:#f55}.dim{color:#666}"
    "</style>";

static void page_head(ob_t *o, const char *title){
    ob_puts(o, "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>");
    ob_puts(o, title); ob_puts(o, "</title>"); ob_puts(o, CSS);
    ob_puts(o, "</head><body>");
}
static void page_nav(ob_t *o){
    ob_puts(o, "<div class=\"nav\">"
               "<a href=\"/\">dashboard</a>"
               "<a href=\"/status\">status</a>"
               "<a href=\"/connections\">conexoes</a>"
               "<a href=\"/tools\">ferramentas</a>"
               "<a href=\"/config\">config</a>"
               "<a href=\"/help\">help</a>"
               "</div><hr>");
}
static void page_foot(ob_t *o){ ob_puts(o, "</body></html>"); }

static void http_head(ob_t *o, int code, const char *ct){
    const char *cs="200 OK";
    switch(code){
        case 200: cs="200 OK"; break;
        case 303: cs="303 See Other"; break;
        case 400: cs="400 Bad Request"; break;
        case 401: cs="401 Unauthorized"; break;
        case 404: cs="404 Not Found"; break;
        case 405: cs="405 Method Not Allowed"; break;
        case 500: cs="500 Internal Server Error"; break;
    }
    ob_puts(o, "HTTP/1.1 "); ob_puts(o, cs); ob_puts(o, "\r\n");
    if (ct){ ob_puts(o, "Content-Type: "); ob_puts(o, ct); ob_puts(o, "\r\n"); }
    ob_puts(o, "Connection: close\r\n");
}

static void serve_401(ob_t *o){
    http_head(o, 401, "text/html; charset=utf-8");
    ob_puts(o, "WWW-Authenticate: Basic realm=\"kernel-web\"\r\n\r\n");
    page_head(o, "401");
    ob_puts(o, "<h1>401</h1><p>Login requerido.</p>");
    page_foot(o);
}
static void serve_404(ob_t *o){
    http_head(o, 404, "text/html; charset=utf-8"); ob_puts(o, "\r\n");
    page_head(o, "404"); page_nav(o);
    ob_puts(o, "<h1>404</h1><p>Rota nao encontrada.</p>");
    page_foot(o);
}
static void serve_index(ob_t *o){
    http_head(o, 200, "text/html; charset=utf-8"); ob_puts(o, "\r\n");
    page_head(o, "Kernel Web"); page_nav(o);
    ob_puts(o, "<h1>Kernel Web</h1>");
    ob_puts(o, "<p>Logado como <b>"); ob_puts(o, g_cfg.user); ob_puts(o, "</b>.</p>");

    ob_puts(o, "<h2>Uptime</h2><p>");
    ob_putdec(o, (int)sys_now()); ob_puts(o, " ms</p>");

    ob_puts(o, "<h2>Resumo</h2><table>");
    ob_puts(o, "<tr><th>item</th><th>valor</th></tr>");
    ob_puts(o, "<tr><td>auth</td><td>"); ob_puts(o, g_cfg.auth?"on":"off"); ob_puts(o, "</td></tr>");
    ob_puts(o, "<tr><td>porta</td><td>"); ob_putdec(o, g_cfg.port); ob_puts(o, "</td></tr>");
    ob_puts(o, "<tr><td>requests</td><td>"); ob_putdec(o, g_cfg.req_count); ob_puts(o, "</td></tr>");
    ob_puts(o, "<tr><td>falhas de login</td><td>"); ob_putdec(o, g_cfg.auth_fail); ob_puts(o, "</td></tr>");
    ob_puts(o, "<tr><td>conexoes recentes</td><td>"); ob_putdec(o, conntrack_count()); ob_puts(o, "</td></tr>");
    ob_puts(o, "</table>");
    page_foot(o);
}

static void serve_status(ob_t *o){
    http_head(o, 200, "text/html; charset=utf-8"); ob_puts(o, "\r\n");
    page_head(o, "Status"); page_nav(o);
    ob_puts(o, "<h1>Status</h1><table>");
    ob_puts(o, "<tr><th>item</th><th>valor</th></tr>");
    ob_puts(o, "<tr><td>uptime</td><td>"); ob_putdec(o, (int)sys_now()); ob_puts(o, " ms</td></tr>");
    extern uint8_t rtl_mac[6];
    ob_puts(o, "<tr><td>MAC</td><td>");
    for (int i=0;i<6;i++){ ob_puthex(o, rtl_mac[i],2); if (i<5) ob_putc(o,':'); }
    ob_puts(o, "</td></tr>");
    ob_puts(o, "<tr><td>auth</td><td>"); ob_puts(o, g_cfg.auth?"on":"off"); ob_puts(o, "</td></tr>");
    ob_puts(o, "<tr><td>usuario</td><td>"); ob_puts(o, g_cfg.user); ob_puts(o, "</td></tr>");
    ob_puts(o, "<tr><td>porta</td><td>"); ob_putdec(o, g_cfg.port); ob_puts(o, "</td></tr>");
    ob_puts(o, "<tr><td>requests</td><td>"); ob_putdec(o, g_cfg.req_count); ob_puts(o, "</td></tr>");
    ob_puts(o, "<tr><td>falhas de login</td><td>"); ob_putdec(o, g_cfg.auth_fail); ob_puts(o, "</td></tr>");
    ob_puts(o, "<tr><td>conexoes ativas</td><td>"); ob_putdec(o, g_conn_count); ob_puts(o, "</td></tr>");
    ob_puts(o, "</table>");
    page_foot(o);
}

static void serve_connections(ob_t *o){
    http_head(o, 200, "text/html; charset=utf-8"); ob_puts(o, "\r\n");
    page_head(o, "Conexoes"); page_nav(o);
    ob_puts(o, "<h1>Conexoes recentes</h1>");
    ob_puts(o, "<table><tr><th>#</th><th>proto</th><th>local</th><th>remoto</th><th>desc</th><th>aberta</th><th>dur</th><th>status</th></tr>");

    int any=0;
    for (int i=0;i<CONNTRACK_MAX;i++){
        conntrack_entry_t *e = conntrack_get(i);
        if (!e || !e->active) continue;
        any=1;
        ob_puts(o, "<tr><td>"); ob_putdec(o, i); ob_puts(o, "</td>");
        ob_puts(o, "<td>"); ob_puts(o, conntrack_proto_name(e->proto)); ob_puts(o, "</td>");

        ob_puts(o, "<td>:");
        ob_putdec(o, e->lport); ob_puts(o, "</td>");

        ob_puts(o, "<td>");
        if (e->proto == 1 || e->remote.addr){
            /* imprime IP */
            uint32_t a = lwip_ntohl(e->remote.addr);
            ob_putdec(o, (int)((a>>24)&0xFF)); ob_putc(o,'.');
            ob_putdec(o, (int)((a>>16)&0xFF)); ob_putc(o,'.');
            ob_putdec(o, (int)((a>>8)&0xFF)); ob_putc(o,'.');
            ob_putdec(o, (int)(a&0xFF));
            ob_putc(o, ':'); ob_putdec(o, e->rport);
        }
        ob_puts(o, "</td>");

        ob_puts(o, "<td>"); ob_html(o, e->desc); ob_puts(o, "</td>");
        ob_puts(o, "<td>"); ob_putdec(o, (int)e->opened_ms); ob_puts(o, " ms</td>");

        if (e->closed_ms){
            ob_puts(o, "<td>"); ob_putdec(o, (int)(e->closed_ms - e->opened_ms)); ob_puts(o, " ms</td>");
            ob_puts(o, e->success ? "<td class=\"ok-t\">ok</td>" : "<td class=\"err-t\">falhou</td>");
        } else {
            ob_puts(o, "<td class=\"dim\">em curso</td><td class=\"dim\">-</td>");
        }
        ob_puts(o, "</tr>");
    }
    if (!any) ob_puts(o, "<tr><td colspan=\"8\" class=\"dim\">(nenhuma conexao registrada)</td></tr>");
    ob_puts(o, "</table>");
    ob_puts(o, "<p class=\"dim\">Ultimas "); ob_putdec(o, CONNTRACK_MAX); ob_puts(o, " conexoes.</p>");
    page_foot(o);
}

static void serve_tools(ob_t *o, const char *ping_host, const char *ping_res,
                        const char *dns_name, const char *dns_res)
{
    http_head(o, 200, "text/html; charset=utf-8"); ob_puts(o, "\r\n");
    page_head(o, "Ferramentas"); page_nav(o);
    ob_puts(o, "<h1>Ferramentas</h1>");

    /* PING */
    ob_puts(o, "<h2>Ping</h2>");
    ob_puts(o, "<form method=\"GET\" action=\"/tools\">"
               "<input type=\"text\" name=\"ping\" placeholder=\"host ou IP\" ");
    if (ping_host) { ob_puts(o, "value=\""); ob_html(o, ping_host); ob_puts(o, "\""); }
    ob_puts(o, "> <button type=\"submit\">pingar</button></form>");
    if (ping_res){
        ob_puts(o, "<pre>"); ob_html(o, ping_res); ob_puts(o, "</pre>");
    }

    /* DNS */
    ob_puts(o, "<h2>DNS</h2>");
    ob_puts(o, "<form method=\"GET\" action=\"/tools\">"
               "<input type=\"text\" name=\"dns\" placeholder=\"nome\" ");
    if (dns_name) { ob_puts(o, "value=\""); ob_html(o, dns_name); ob_puts(o, "\""); }
    ob_puts(o, "> <button type=\"submit\">resolver</button></form>");
    if (dns_res){
        ob_puts(o, "<pre>"); ob_html(o, dns_res); ob_puts(o, "</pre>");
    }

    /* REBOOT */
    ob_puts(o, "<h2>Reiniciar</h2>");
    ob_puts(o, "<form method=\"POST\" action=\"/reboot\" "
               "onsubmit=\"return confirm('Confirma reboot?')\">"
               "<button type=\"submit\">reboot</button></form>");
    ob_puts(o, "<p class=\"warn\">O reboot derruba todas as conexoes.</p>");

    page_foot(o);
}

static void serve_config(ob_t *o, const char *ok){
    http_head(o, 200, "text/html; charset=utf-8"); ob_puts(o, "\r\n");
    page_head(o, "Config"); page_nav(o);
    ob_puts(o, "<h1>Configuracao</h1>");
    if (ok && *ok){
        ob_puts(o, "<div class=\"ok\">");
        if (strcmp(ok,"user")==0) ob_puts(o, "Usuario alterado.");
        else if (strcmp(ok,"pass")==0) ob_puts(o, "Senha alterada.");
        else if (strcmp(ok,"auth")==0) ob_puts(o, "Auth alterado.");
        else ob_puts(o, "Aplicado.");
        ob_puts(o, "</div>");
    }
    ob_puts(o, "<h2>Usuario</h2><form method=\"POST\" action=\"/config/user\">"
               "<input type=\"text\" name=\"user\" placeholder=\"novo usuario\" maxlength=\"31\" required> "
               "<button>salvar</button></form>");
    ob_puts(o, "<h2>Senha</h2><form method=\"POST\" action=\"/config/pass\">"
               "<input type=\"password\" name=\"pass\" placeholder=\"nova senha\" maxlength=\"31\" required> "
               "<button>salvar</button></form>");
    ob_puts(o, "<h2>Autenticacao</h2><p>Atual: <b>");
    ob_puts(o, g_cfg.auth?"ON":"OFF"); ob_puts(o, "</b></p>"
               "<form method=\"POST\" action=\"/config/auth\">"
               "<button name=\"state\" value=\"on\">ativar</button> "
               "<button name=\"state\" value=\"off\">desativar</button></form>");
    page_foot(o);
}

static void serve_help(ob_t *o){
    http_head(o, 200, "text/html; charset=utf-8"); ob_puts(o, "\r\n");
    page_head(o, "Help"); page_nav(o);
    ob_puts(o, "<h1>Rotas</h1><pre>"
               "GET  /                    dashboard\n"
               "GET  /status              estado\n"
               "GET  /connections         conexoes recentes\n"
               "GET  /tools               ping + dns + reboot\n"
               "GET  /tools?ping=8.8.8.8  pingar\n"
               "GET  /tools?dns=g.com     resolver DNS\n"
               "POST /reboot              reiniciar\n"
               "GET  /config              config\n"
               "POST /config/user         mudar usuario\n"
               "POST /config/pass         mudar senha\n"
               "POST /config/auth         on/off\n"
               "GET  /help                esta pagina\n"
               "</pre>");
    page_foot(o);
}

static int check_auth(const char *headers, int hlen){
    if (!g_cfg.auth) return 1;
    for (int i=0;i+20<hlen;i++){
        if (ci_ncmp(headers+i, "Authorization:", 14)!=0) continue;
        int j = i + 14;
        while (j<hlen && (headers[j]==' '||headers[j]=='\t')) j++;
        if (j+6>=hlen || ci_ncmp(headers+j, "Basic", 5)!=0) return 0;
        j += 5;
        while (j<hlen && (headers[j]==' '||headers[j]=='\t')) j++;
        int b64start = j;
        while (j<hlen && headers[j]!='\r' && headers[j]!='\n') j++;
        char dec[128];
        int n = b64_decode(headers+b64start, j-b64start, dec, sizeof(dec));
        if (n <= 0) return 0;
        char *colon = 0;
        for (int k=0;k<n;k++) if (dec[k]==':'){ colon=&dec[k]; break; }
        if (!colon) return 0;
        *colon = 0;
        if (strcmp(dec, g_cfg.user)!=0) return 0;
        if (strcmp(colon+1, g_cfg.pass)!=0) return 0;
        return 1;
    }
    return 0;
}

static void send_and_close(conn_t *c, ob_t *o){
    if (o->len > 0){
        tcp_write(c->pcb, o->buf, (u16_t)o->len, TCP_WRITE_FLAG_COPY);
        tcp_output(c->pcb);
    }
    tcp_close(c->pcb);
    conn_free(c->pcb);
}

static void handle(conn_t *c){
    c->handled = 1;
    const char *req = c->buf;
    int hdr_end = c->headers_done;

    char method[8]={0};
    int i=0, j=0;
    while (req[i] && req[i]!=' ' && j<7) method[j++] = req[i++];
    while (req[i]==' ') i++;
    char fullpath[512]={0};
    j=0;
    while (req[i] && req[i]!=' ' && req[i]!='\r' && req[i]!='\n' && j<511)
        fullpath[j++] = req[i++];

    char path[256]; const char *query=0;
    j=0;
    for (int k=0; fullpath[k] && j<255; k++){
        if (fullpath[k]=='?'){ query = &fullpath[k+1]; break; }
        path[j++] = fullpath[k];
    }
    path[j]=0;

    g_cfg.req_count++;
    logmsg(path);

    int is_get = strcmp(method,"GET")==0;
    int is_post = strcmp(method,"POST")==0;

    /* auth em tudo */
    if (!check_auth(req, hdr_end)){
        g_cfg.auth_fail++;
        static char out[2048]; ob_t o = { out, sizeof(out), 0 };
        serve_401(&o);
        send_and_close(c, &o);
        return;
    }

    const char *body = c->buf + hdr_end;
    int bodylen = c->len - hdr_end; if (bodylen<0) bodylen=0;

    static char out[16384];
    ob_t o = { out, sizeof(out), 0 };

    if (is_get){
        if      (strcmp(path,"/")==0)             serve_index(&o);
        else if (strcmp(path,"/status")==0)       serve_status(&o);
        else if (strcmp(path,"/connections")==0)  serve_connections(&o);
        else if (strcmp(path,"/help")==0)         serve_help(&o);
        else if (strcmp(path,"/tools")==0){
            char hostbuf[128]={0}, resbuf[1024]={0};
            const char *host = query_val(query, "ping", hostbuf, sizeof(hostbuf));
            const char *result = 0;
            if (host && *host && g_ping_fn){
                if (g_ping_fn(host, resbuf, sizeof(resbuf))) result = resbuf;
                else result = "ping falhou";
            }
            char dnsbuf[128]={0}, dnsres[512]={0};
            const char *dnsname = query_val(query, "dns", dnsbuf, sizeof(dnsbuf));
            const char *dnsresult = 0;
            if (dnsname && *dnsname && g_dns_fn){
                if (g_dns_fn(dnsname, dnsres, sizeof(dnsres))) dnsresult = dnsres;
                else dnsresult = "resolucao falhou";
            }
            serve_tools(&o, host, result, dnsname, dnsresult);
        }
        else if (strcmp(path,"/config")==0){
            char ok[16]={0};
            char okbuf[16]={0};
            if (query_val(query, "ok", okbuf, sizeof(okbuf))) {
                int k=0; while (okbuf[k] && k<15){ ok[k]=okbuf[k]; k++; } ok[k]=0;
            }
            serve_config(&o, ok);
        }
        else serve_404(&o);
    }
    else if (is_post){
        char v[64];
        if (strcmp(path,"/config/user")==0){
            if (form_get(body,bodylen,"user",v,sizeof(v)) && v[0]){
                httpd_set_credentials(v,0);
                http_head(&o, 303, 0); ob_puts(&o, "Location: /config?ok=user\r\n\r\n");
            } else { http_head(&o,400,"text/plain"); ob_puts(&o,"\r\nvazio\n"); }
        }
        else if (strcmp(path,"/config/pass")==0){
            if (form_get(body,bodylen,"pass",v,sizeof(v)) && v[0]){
                httpd_set_credentials(0,v);
                http_head(&o, 303, 0); ob_puts(&o, "Location: /config?ok=pass\r\n\r\n");
            } else { http_head(&o,400,"text/plain"); ob_puts(&o,"\r\nvazio\n"); }
        }
        else if (strcmp(path,"/config/auth")==0){
            if (form_get(body,bodylen,"state",v,sizeof(v))){
                if (strcmp(v,"on")==0)  httpd_set_auth(1);
                if (strcmp(v,"off")==0) httpd_set_auth(0);
                http_head(&o, 303, 0); ob_puts(&o, "Location: /config?ok=auth\r\n\r\n");
            } else { http_head(&o,400,"text/plain"); ob_puts(&o,"\r\nstate ausente\n"); }
        }
        else if (strcmp(path,"/reboot")==0){
            http_head(&o, 200, "text/html; charset=utf-8");
            ob_puts(&o, "\r\n");
            page_head(&o, "Reboot");
            ob_puts(&o, "<h1>Reiniciando...</h1><p>O sistema vai reiniciar em instantes.</p>");
            page_foot(&o);
            send_and_close(c, &o);
            /* pequeno delay via busy-wait para o pacote sair */
            for (volatile int k=0;k<50000000;k++) __asm__ volatile("pause");
            if (g_reboot_fn) g_reboot_fn();
            return;
        }
        else serve_404(&o);
    }
    else {
        http_head(&o, 405, "text/plain"); ob_puts(&o, "\r\nmetodo nao suportado\n");
    }

    send_and_close(c, &o);
}

static void on_err(void *arg, err_t err){
    (void)err;
    struct tcp_pcb *pcb = (struct tcp_pcb*)arg;
    if (pcb) conn_free(pcb);
}

static err_t on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err){
    (void)arg;
    if (err != ERR_OK){ if (p) pbuf_free(p); return err; }
    if (!p){
        conn_free(pcb); tcp_arg(pcb,0); tcp_recv(pcb,0); tcp_close(pcb);
        return ERR_OK;
    }
    conn_t *c = conn_find(pcb);
    if (!c){ pbuf_free(p); return ERR_MEM; }
    if (c->handled){ pbuf_free(p); return ERR_OK; }

    int space = REQBUF_SZ - c->len - 1;
    int n = p->tot_len;
    if (n > space) n = space;
    if (n > 0){
        pbuf_copy_partial(p, c->buf+c->len, (u16_t)n, 0);
        c->len += n;
        c->buf[c->len]=0;
    }
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);

    if (!c->headers_done){
        const char *e = my_strstr(c->buf, "\r\n\r\n");
        if (e){
            c->headers_done = (int)(e-c->buf)+4;
            c->content_length = 0;
            for (int i=0;i<c->headers_done-2;i++){
                if (ci_ncmp(c->buf+i, "Content-Length:", 15)==0){
                    int jj=i+15;
                    while (jj<c->headers_done && (c->buf[jj]==' '||c->buf[jj]=='\t')) jj++;
                    int v=0;
                    while (jj<c->headers_done && c->buf[jj]>='0' && c->buf[jj]<='9'){
                        v=v*10+(c->buf[jj]-'0'); jj++;
                    }
                    c->content_length = v; break;
                }
            }
        } else return ERR_OK;
    }

    if (c->len >= c->headers_done + c->content_length)
        handle(c);

    return ERR_OK;
}

static err_t on_accept(void *arg, struct tcp_pcb *newpcb, err_t err){
    (void)arg;
    if (err != ERR_OK) return err;
    if (!conn_alloc(newpcb)) return ERR_MEM;
    tcp_recv(newpcb, on_recv);
    tcp_err(newpcb, on_err);
    return ERR_OK;
}

err_t httpd_start(unsigned short port){
    if (g_cfg.up) return ERR_ALREADY;
    g_listen = tcp_new();
    if (!g_listen) return ERR_MEM;
    ip_addr_t any = *IP_ADDR_ANY;
    err_t e = tcp_bind(g_listen, &any, port);
    if (e != ERR_OK){ tcp_close(g_listen); g_listen=0; return e; }
    g_listen = tcp_listen(g_listen);
    if (!g_listen) return ERR_MEM;
    tcp_accept(g_listen, on_accept);
    g_cfg.up = 1; g_cfg.port = (int)port;
    logmsg("http started");
    return ERR_OK;
}
void httpd_stop(void){
    if (!g_cfg.up) return;
    tcp_close(g_listen); g_listen=0; g_cfg.up=0;
    logmsg("http stopped");
}
int httpd_running(void){ return g_cfg.up; }
void httpd_set_auth(int e){ g_cfg.auth = e?1:0; }
int  httpd_get_auth(void){ return g_cfg.auth; }
int  httpd_set_credentials(const char *u, const char *p){
    if (u && *u){ int i=0; while (u[i] && i<USER_MAX){ g_cfg.user[i]=u[i]; i++; } g_cfg.user[i]=0; }
    if (p && *p){ int i=0; while (p[i] && i<PASS_MAX){ g_cfg.pass[i]=p[i]; i++; } g_cfg.pass[i]=0; }
    return 0;
}
const char *httpd_get_username(void){ return g_cfg.user; }
int httpd_get_port(void){ return g_cfg.port; }
int httpd_get_request_count(void){ return g_cfg.req_count; }
int httpd_get_auth_fail_count(void){ return g_cfg.auth_fail; }
int httpd_get_conn_count(void){ return g_conn_count; }

void httpd_init(void){
    memset(&g_cfg, 0, sizeof(g_cfg));
    const char *u="admin", *p="admin";
    int i=0; while (u[i]){ g_cfg.user[i]=u[i]; i++; } g_cfg.user[i]=0;
    i=0; while (p[i]){ g_cfg.pass[i]=p[i]; i++; } g_cfg.pass[i]=0;
    g_cfg.auth=1; g_cfg.up=0; g_cfg.port=80;
    memset(g_conns, 0, sizeof(g_conns));
    g_conn_count=0;
    g_ping_fn = 0; g_dns_fn = 0; g_reboot_fn = 0;
}
