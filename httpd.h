#ifndef HTTPD_H
#define HTTPD_H
#include "lwip/err.h"

void httpd_init(void);
err_t httpd_start(unsigned short port);
void  httpd_stop(void);
int   httpd_running(void);

void        httpd_set_auth(int enabled);
int         httpd_get_auth(void);
int         httpd_set_credentials(const char *user, const char *pass);
const char *httpd_get_username(void);

int         httpd_get_port(void);
int         httpd_get_request_count(void);
int         httpd_get_auth_fail_count(void);
int         httpd_get_conn_count(void);

typedef void (*httpd_log_fn)(const char *msg);
void httpd_set_logger(httpd_log_fn fn);

/* callbacks para ping/dns vindos do kernel.c */
typedef int (*httpd_ping_cb)(const char *host, char *out, int outcap);
typedef int (*httpd_dns_cb2)(const char *name, char *out, int outcap);
void httpd_set_ping_handler(httpd_ping_cb fn);
void httpd_set_dns_handler(httpd_dns_cb2 fn);
void httpd_request_reboot(void);
void httpd_set_reboot_handler(void (*fn)(void));

#endif
