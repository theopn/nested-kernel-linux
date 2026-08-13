#include <linux/kernel.h>
#include <linux/memblock.h>
#include <asm/paravirt.h>
#include <asm/pgtable.h>
#include <asm/set_memory.h>
#include <asm/tlbflush.h>
#include <asm/processor-flags.h>
#include <asm/tlb.h>
#include <asm/cpufeature.h>

#ifndef X86_FEATURE_PKS
#define X86_FEATURE_PKS (16*32 + 31)
#endif

phys_addr_t nk_base_phys;
/* For testing only - remove before production use */
EXPORT_SYMBOL_GPL(nk_base_phys);
phys_addr_t nk_size = 16 * 1024 * 1024; /* 16MB for NK */

static bool nk_locked_down;   /* false until nk_protect_memory() completes */

#ifdef CONFIG_PARAVIRT_XXL
void (*nk_orig_set_pte)(pte_t *ptep, pte_t pteval);
void (*nk_orig_set_pmd)(pmd_t *pmdp, pmd_t pmdval);
#endif

void nk_declare(void);
void nk_alloc(void);
void nk_free(void);
void nk_write_PTE(pte_t *ptep, pte_t pteval);
void nk_write_PMD(pmd_t *pmdp, pmd_t pmdval);
void nk_write(void *dest, const void *src, size_t size);
void nk_init(void);
void nk_protect_memory(void);
void nk_protect_citadel_pks(void);

extern void nk_enter(void *payload, void *arg1, void *arg2);
extern void nk_exit(void);

void nk_declare(void) {}
void nk_alloc(void) {}
void nk_free(void) {}

static bool nk_is_citadel_vaddr(unsigned long addr)
{
    unsigned long base;
    if (!nk_base_phys)
        return false;
    base = (unsigned long)__va(nk_base_phys);
    return addr >= base && addr < base + nk_size;
}

static bool nk_pte_maps_citadel(pte_t pte)
{
    phys_addr_t pa;
    if (!nk_base_phys || !pte_present(pte))
        return false;
    pa = (phys_addr_t)pte_pfn(pte) << PAGE_SHIFT;
    return pa >= nk_base_phys && pa < nk_base_phys + nk_size;
}

static bool nk_pmd_maps_citadel(pmd_t pmd)
{
    phys_addr_t pa;
    if (!nk_base_phys || !pmd_present(pmd))
        return false;
    pa = (phys_addr_t)pmd_pfn(pmd) << PAGE_SHIFT;
    return !(pa + PMD_SIZE <= nk_base_phys ||
             pa >= nk_base_phys + nk_size);
}

static void payload_set_pte(pte_t *ptep, pte_t *pteval_ptr) {
    pte_t val = *pteval_ptr;

    if (nk_locked_down) {
        if (nk_is_citadel_vaddr((unsigned long)ptep)) {
            pr_warn_ratelimited("NK: rejected PTE write to Citadel PTE %px\n", ptep);
            return;
        }
        if (nk_pte_maps_citadel(val)) {
            pr_warn_ratelimited("NK: rejected PTE mapping Citadel phys\n");
            return;
        }
    }

#ifdef CONFIG_PARAVIRT_XXL
    if (nk_orig_set_pte) {
        nk_orig_set_pte(ptep, val);
        return;
    }
#endif
    native_set_pte(ptep, val);
}

void nk_write_PTE(pte_t *ptep, pte_t pteval) {
    nk_enter((void *)payload_set_pte, ptep, &pteval);
}

static void payload_set_pmd(pmd_t *pmdp, pmd_t *pmdval_ptr) {
    pmd_t val = *pmdval_ptr;

    if (nk_locked_down) {
        if (nk_is_citadel_vaddr((unsigned long)pmdp)) {
            pr_warn_ratelimited("NK: rejected PMD write to Citadel PMD %px\n", pmdp);
            return;
        }
        if (nk_pmd_maps_citadel(val)) {
            pr_warn_ratelimited("NK: rejected PMD mapping Citadel phys\n");
            return;
        }
    }

#ifdef CONFIG_PARAVIRT_XXL
    if (nk_orig_set_pmd) {
        nk_orig_set_pmd(pmdp, val);
        return;
    }
#endif
    native_set_pmd(pmdp, val);
}

void nk_write_PMD(pmd_t *pmdp, pmd_t pmdval) {
    nk_enter((void *)payload_set_pmd, pmdp, &pmdval);
}

void nk_write(void *dest, const void *src, size_t size) {}

void nk_init(void) {
    nk_base_phys = memblock_phys_alloc_range(nk_size, PMD_SIZE, 0, ULLONG_MAX);
    if (!nk_base_phys) {
        panic("Nested Kernel: Failed to allocate physical memory!\n");
    }
    pr_info("Nested Kernel: Reserved %llu bytes at phys 0x%llx\n", 
            (unsigned long long)nk_size, (unsigned long long)nk_base_phys);

#ifdef CONFIG_PARAVIRT_XXL
    nk_orig_set_pte = pv_ops.mmu.set_pte;
    pv_ops.mmu.set_pte = nk_write_PTE;

    nk_orig_set_pmd = pv_ops.mmu.set_pmd;
    pv_ops.mmu.set_pmd = nk_write_PMD;
#endif
}

extern char __start_nk_rodata[];
extern char __stop_nk_rodata[];

void nk_protect_memory(void) {
    unsigned long nk_base_virt;
    unsigned long npages;
    if (!nk_base_phys) return;

    nk_base_virt = (unsigned long)__va(nk_base_phys);
    

    /* Map statically compiled NK rodata as read-only */
    npages = PAGE_ALIGN(__stop_nk_rodata - __start_nk_rodata) >> PAGE_SHIFT;
    if (npages > 0) {
        int err = set_memory_ro((unsigned long)__start_nk_rodata, npages);
        pr_info("Nested Kernel: set_memory_ro returned %d for nk_rodata (addr=%px, pages=%lu)\n", err, __start_nk_rodata, npages);
    }
    
    unsigned long citadel_pages = nk_size >> PAGE_SHIFT;
    int err2 = set_memory_ro(nk_base_virt, citadel_pages);
    pr_info("Nested Kernel: set_memory_ro on Citadel returned %d "
            "(base=%lx, pages=%lu)\n", err2, nk_base_virt, citadel_pages);
    if (err2)
        pr_err("Nested Kernel: FAILED to write-protect Citadel!\n");

    pr_info("Nested Kernel: Protected NK memory and PTPs\n");
    nk_protect_citadel_pks();

    nk_locked_down = true;
    pr_info("Nested Kernel: Lockdown engaged.\n");
}

#define _PAGE_PKEY_SHIFT _PAGE_BIT_PKEY_BIT0

static void nk_raw_set_pte(pte_t *ptep, pte_t pte)
{
    /* NK-internal only. Bypasses pv_ops interception so the NK can
     * modify Citadel PTEs during its own initialization without
     * recursing through nk_write_PTE. Must never be exported or
     * called from outside this file. */
    native_set_pte(ptep, pte);
}

static int apply_supervisor_pkey(unsigned long addr, int pkey) {
    unsigned int level; pte_t *pte; pte_t new_pte; unsigned long pteval;
    pte = lookup_address(addr, &level);
    if (!pte) return -ENOENT;
    if (level != PG_LEVEL_4K) return -EINVAL;
    pteval = pte_val(*pte);
    pteval &= ~_PAGE_PKEY_MASK;
    pteval |= ((unsigned long)(pkey & 0xF)) << _PAGE_PKEY_SHIFT;
    new_pte = __pte(pteval);
    nk_raw_set_pte(pte, new_pte);
    return 0;
}

static void nk_init_cpu_pks(void *info) {
    if (!cpu_feature_enabled(X86_FEATURE_PKS))
        return;

    /* Enable PKS in CR4 on this specific CPU */
    cr4_set_bits(X86_CR4_PKS);
    
    /* Globally lock the Citadel (Key 1) by default on this CPU
     * 0x8 = Access Disable (AD) and Write Disable (WD) for PKEY 1
     */
#ifndef MSR_IA32_PKRS
#define MSR_IA32_PKRS 0x6E1
#endif
    wrmsrl(MSR_IA32_PKRS, 0x8);
}

void nk_protect_citadel_pks(void) {
    unsigned long addr;
    unsigned long nk_base_virt = (unsigned long)__va(nk_base_phys);
    unsigned long end = nk_base_virt + nk_size;
    int success = 0, invalid = 0;

    if (!cpu_feature_enabled(X86_FEATURE_PKS)) {
        printk(KERN_INFO "Nested Kernel: PKS not supported by hardware. Bypassing PKS protection.\n");
        return;
    }

    int split_ret = set_memory_4k(nk_base_virt, nk_size >> PAGE_SHIFT);
    pr_info("Nested Kernel: set_memory_4k on Citadel returned %d\n", split_ret);

    for (addr = nk_base_virt; addr < end; addr += PAGE_SIZE) {
        int ret = apply_supervisor_pkey(addr, 1);
        if (ret == 0)
            success++;
        else if (ret == -EINVAL)
            invalid++;
    }
    __flush_tlb_all();
    
    printk(KERN_INFO "Nested Kernel: PKS tagging complete - %d pages tagged, %d pages invalid (huge page leak).\n", success, invalid);
    
    /* Broadcast CR4 and PKRS initialization to ALL CPUs simultaneously */
    on_each_cpu(nk_init_cpu_pks, NULL, 1);
    
    printk(KERN_INFO "Nested Kernel: PKS Key 1 applied and locked on all CPUs.\n");
}
