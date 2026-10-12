#ifndef _STDIO_H
#define _STDIO_H
#include <stddef.h>
typedef struct _FILE FILE;
extern FILE *stdout, *stderr, *stdin;
int printf(const char *fmt, ...);
int snprintf(char *buf, size_t n, const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);
#define NULL ((void*)0)
#endif
