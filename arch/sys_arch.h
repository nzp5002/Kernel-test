#ifndef LWIP_ARCH_SYS_ARCH_H
#define LWIP_ARCH_SYS_ARCH_H

#include "lwip/opt.h"
#include "lwip/err.h"

typedef int sys_prot_t;
typedef int sys_mutex_t;
typedef int sys_sem_t;

u32_t sys_now(void);
u32_t sys_jiffies(void);

#endif
