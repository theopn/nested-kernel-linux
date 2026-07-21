#include <linux/kernel.h>
#include <linux/memblock.h>
#include <asm/paravirt.h>
#include <asm/pgtable.h>
#include <asm/set_memory.h>
#include <asm/tlbflush.h>
#include <asm/processor-flags.h>
#include <asm/tlb.h>
phys_addr_t nk_base_phys;
// Providing the test suite the base address to attack
//EXPORT_SYMBOL_GPL(nk_base_phys);
phys_addr_t nk_size = 16 * 1024 * 1024; /* 16MB for NK */

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

static void payload_set_pte(pte_t *ptep, pte_t *pteval_ptr) {
#ifdef CONFIG_PARAVIRT_XXL
    if (nk_orig_set_pte) {
        nk_orig_set_pte(ptep, *pteval_ptr);
        return;
    }
#endif
    native_set_pte(ptep, *pteval_ptr);
}

void nk_write_PTE(pte_t *ptep, pte_t pteval) {
    nk_enter((void *)payload_set_pte, ptep, &pteval);
}

static void payload_set_pmd(pmd_t *pmdp, pmd_t *pmdval_ptr) {
#ifdef CONFIG_PARAVIRT_XXL
    if (nk_orig_set_pmd) {
        nk_orig_set_pmd(pmdp, *pmdval_ptr);
        return;
    }
#endif
    native_set_pmd(pmdp, *pmdval_ptr);
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
    
    pr_info("Nested Kernel: Protected NK memory and PTPs\n");
    nk_protect_citadel_pks();
}

#define _PAGE_PKEY_SHIFT _PAGE_BIT_PKEY_BIT0

static int apply_supervisor_pkey(unsigned long addr, int pkey) {
    unsigned int level; pte_t *pte; pte_t new_pte; unsigned long pte_val;
    pte = lookup_address(addr, &level);
    if (!pte) return -ENOENT;
    pte_val = pte_val(*pte);
    pte_val &= ~_PAGE_PKEY_MASK;
    pte_val |= ((unsigned long)(pkey & 0xF)) << _PAGE_PKEY_SHIFT;
    new_pte = __pte(pte_val);
    set_pte_atomic(pte, new_pte);
    return 0;
}

static void nk_init_cpu_pks(void *info) {
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
    for (addr = nk_base_virt; addr < end; addr += PAGE_SIZE) {
        apply_supervisor_pkey(addr, 1);
    }
    __flush_tlb_all();
    
    /* Broadcast CR4 and PKRS initialization to ALL CPUs simultaneously */
    on_each_cpu(nk_init_cpu_pks, NULL, 1);
    
    printk(KERN_INFO "Nested Kernel: PKS Key 1 applied and locked on all CPUs.\n");
}
