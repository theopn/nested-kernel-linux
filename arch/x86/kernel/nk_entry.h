#ifndef _NK_ENTRY_H
#define _NK_ENTRY_H

#include <asm/msr-index.h>

#ifndef MSR_IA32_PKRS
#define MSR_IA32_PKRS 0x6E1
#endif

#define NK_PKEY 1
#define PKRS_UNLOCKED_VAL 0x0

static __always_inline void nk_switch_gate_enter(void) {
    u32 verification_val;
    asm volatile(
        "cli\n\t" 
        "movl %[unlock_val], %%eax\n\t"
        "xorl %%edx, %%edx\n\t"
        "movl %[msr_pkrs], %%ecx\n\t"
        "wrmsr\n\t"
        "rdmsr\n\t"
        "cmpl %[unlock_val], %%eax\n\t"
        "je 1f\n\t"
        "ud2\n\t" 
        "1:\n\t"
        : "=a" (verification_val)
        : [unlock_val] "i" (PKRS_UNLOCKED_VAL), [msr_pkrs] "i" (MSR_IA32_PKRS)
        : "ecx", "edx", "memory"
    );
}

#endif
