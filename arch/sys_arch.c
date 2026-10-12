#include "lwip/opt.h"
#include "arch/sys_arch.h"

extern unsigned long long kernel_tsc(void);
extern unsigned long long kernel_tsc_per_ms;

u32_t sys_now(void) {
    unsigned long long t = kernel_tsc();
    if (kernel_tsc_per_ms == 0) return (u32_t)(t / 1000000ULL);
    return (u32_t)(t / kernel_tsc_per_ms);
}

u32_t sys_jiffies(void) { return sys_now(); }
