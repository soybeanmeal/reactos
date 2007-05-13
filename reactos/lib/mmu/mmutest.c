#include "mmu.h"
#include "mmuutil.h"
#include "mmuobject.h"

int mmunitest()
{
    int ret;
    int (*fun)(int ret) = (void *)0x80000000;
    ppc_map_info_t info = { 0 };
    volatile int oldmsr, msr = 0x2030;
    __asm__("mfmsr 0\n\tstw 0,0(%0)" : : "r" (&oldmsr));
    mmusetvsid(8, 9, 0);
    info.flags = MMU_ALL_RW;
    info.phys =  0x1000000;
    mmuaddpage(fun, &info, 1);
    __asm__("mtmsr %0" : : "r" (msr));
    __asm__("mtsdr1 %0" : : "r" (HTABORG));
    *((int *)fun) = 0x4e800020;
    ret = fun(3);
    __asm__("mtmsr %0" : : "r" (oldmsr));
    return ret != 3;
}
