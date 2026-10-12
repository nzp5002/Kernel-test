#ifndef _STDLIB_H
#define _STDLIB_H
#include <stddef.h>
void *malloc(size_t n);
void  free(void *p);
void *realloc(void *p, size_t n);
void *calloc(size_t n, size_t sz);
int   atoi(const char *s);
unsigned long strtoul(const char *s, char **end, int base);
#define NULL ((void*)0)
#endif
