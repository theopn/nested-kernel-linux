#include <linux/kernel.h>
#include <linux/memblock.h>
#include <asm/paravirt.h>
#include <asm/pgtable.h>
#include <asm/set_memory.h>

phys_addr_t nk_base_phys;
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
    nk_base_phys = memblock_phys_alloc_range(nk_size, PAGE_SIZE, 0, ULLONG_MAX);
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

void nk_protect_memory(void) {
    unsigned long nk_base_virt;
    if (!nk_base_phys) return;

    nk_base_virt = (unsigned long)__va(nk_base_phys);
    
    /* Map NK memory as read-only to OK */
    set_memory_ro(nk_base_virt, nk_size >> PAGE_SHIFT);
    
    pr_info("Nested Kernel: Protected NK memory and PTPs\n");
}
