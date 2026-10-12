#ifndef LWIP_ARCH_CC_H
#define LWIP_ARCH_CC_H

#include <stdint.h>
#include <stddef.h>

typedef uint8_t   u8_t;
typedef int8_t    s8_t;
typedef uint16_t  u16_t;
typedef int16_t   s16_t;
typedef uint32_t  u32_t;
typedef int32_t   s32_t;
typedef uintptr_t mem_ptr_t;

#define U16_F  "hu"
#define S16_F  "hd"
#define X16_F  "hx"
#define U32_F  "u"
#define S32_F  "d"
#define X32_F  "x"
#define SZT_F  "zu"

#define LWIP_PLATFORM_DIAG(x)   do { } while(0)
#define LWIP_PLATFORM_ASSERT(x) do { } while(0)

void *memcpy(void *d, const void *s, size_t n);
void *memset(void *d, int c, size_t n);
int   memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);

#define MEMCPY(d,s,n)  memcpy(d,s,n)
#define SMEMCPY(d,s,n) memcpy(d,s,n)
#define MEMCMP(a,b,n)  memcmp(a,b,n)

#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_END
#define PACK_STRUCT_FIELD(x) x
#define PACK_STRUCT_STRUCT __attribute__((packed))

#endif
