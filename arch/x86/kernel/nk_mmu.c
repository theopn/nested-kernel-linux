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
#define X86_FEATURE_PKS (16 * 32 + 31)
#endif

phys_addr_t nk_base_phys;
/* For testing only - remove before production use */
EXPORT_SYMBOL_GPL(nk_base_phys);
phys_addr_t nk_size = 16 * 1024 * 1024; /* 16MB for NK */

static bool nk_locked_down; /* false until nk_protect_memory() completes */

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

void nk_report_ptes(unsigned long vaddr);
static void nk_analyze_pud_page(unsigned long vaddr);
static void nk_analyze_pmd_page(unsigned long vaddr);
static void nk_count_citadel_ptpages(void);

extern void nk_enter(void *payload, void *arg1, void *arg2);
extern void nk_exit(void);

void nk_declare(void)
{
}
void nk_alloc(void)
{
}
void nk_free(void)
{
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
	return !(pa + PMD_SIZE <= nk_base_phys || pa >= nk_base_phys + nk_size);
}

static atomic_t nk_pte_calls = ATOMIC_INIT(0);

static void payload_set_pte(pte_t *ptep, pte_t *pteval_ptr)
{
	pte_t val = *pteval_ptr;

	if (atomic_inc_return(&nk_pte_calls) % 10000 == 0) {
		pr_info("NK-DBG: payload_set_pte heartbeat\n");
	}

	if (nk_locked_down) {
		pte_t old = *ptep;
		bool old_hits = nk_pte_maps_citadel(old);
		bool new_hits = nk_pte_maps_citadel(val);

		/* TEMPORARY: log any write where either check is relevant,
         * plus a periodic heartbeat so I know the payload runs */
		if (old_hits || new_hits || pte_val(old) != 0) {
			pr_warn_ratelimited(
				"NK-DBG: ptep=%px old=0x%lx new=0x%lx old_hits=%d new_hits=%d "
				"base=0x%llx size=0x%llx\n",
				ptep, pte_val(old), pte_val(val), old_hits,
				new_hits, (u64)nk_base_phys, (u64)nk_size);
		}

		if (old_hits) {
			pr_warn_ratelimited(
				"NK: rejected write to PTE mapping Citadel (old=0x%lx new=0x%lx)\n",
				pte_val(old), pte_val(val));
			return;
		}
		if (new_hits) {
			pr_warn_ratelimited(
				"NK: rejected PTE aliasing Citadel phys\n");
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

void nk_write_PTE(pte_t *ptep, pte_t pteval)
{
	nk_enter((void *)payload_set_pte, ptep, &pteval);
}

static void payload_set_pmd(pmd_t *pmdp, pmd_t *pmdval_ptr)
{
	pmd_t val = *pmdval_ptr;

	if (nk_locked_down) {
		pmd_t old = *pmdp;
		if (nk_pmd_maps_citadel(old)) {
			pr_warn_ratelimited(
				"NK: rejected write to PMD mapping Citadel (old=0x%lx new=0x%lx)\n",
				pmd_val(old), pmd_val(val));
			return;
		}
		if (nk_pmd_maps_citadel(val)) {
			pr_warn_ratelimited(
				"NK: rejected PMD aliasing Citadel phys\n");
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

void nk_write_PMD(pmd_t *pmdp, pmd_t pmdval)
{
	nk_enter((void *)payload_set_pmd, pmdp, &pmdval);
}

void nk_write(void *dest, const void *src, size_t size)
{
}

void nk_init(void)
{
	nk_base_phys =
		memblock_phys_alloc_range(nk_size, PUD_SIZE, 0, ULLONG_MAX);
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

void nk_protect_memory(void)
{
	unsigned long nk_base_virt;
	unsigned long npages;
	if (!nk_base_phys)
		return;

	nk_base_virt = (unsigned long)__va(nk_base_phys);

	/* Map statically compiled NK rodata as read-only */
	npages = PAGE_ALIGN(__stop_nk_rodata - __start_nk_rodata) >> PAGE_SHIFT;
	if (npages > 0) {
		int err =
			set_memory_ro((unsigned long)__start_nk_rodata, npages);
		pr_info("Nested Kernel: set_memory_ro returned %d for nk_rodata (addr=%px, pages=%lu)\n",
			err, __start_nk_rodata, npages);
	}

	unsigned long citadel_pages = nk_size >> PAGE_SHIFT;
	int err2 = set_memory_ro(nk_base_virt, citadel_pages);
	pr_info("Nested Kernel: set_memory_ro on Citadel returned %d "
		"(base=%lx, pages=%lu)\n",
		err2, nk_base_virt, citadel_pages);
	if (err2)
		pr_err("Nested Kernel: FAILED to write-protect Citadel!\n");

	int split_ret = set_memory_4k(nk_base_virt, nk_size >> PAGE_SHIFT);
	pr_info("Nested Kernel: set_memory_4k on Citadel returned %d\n",
		split_ret);

	pr_info("Nested Kernel: Protected NK memory and PTPs\n");
	nk_protect_citadel_pks();

	nk_report_ptes(nk_base_virt);
	nk_analyze_pud_page(nk_base_virt);
	nk_analyze_pmd_page(nk_base_virt);
	nk_count_citadel_ptpages();

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

void nk_report_ptes(unsigned long vaddr)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	pgd = pgd_offset_k(vaddr);
	pr_info("NK-PTW: PGD at %px (val=%lx)\n", pgd, pgd_val(*pgd));
	if (pgd_none(*pgd) || pgd_bad(*pgd))
		return;

	p4d = p4d_offset(pgd, vaddr);
	pr_info("NK-PTW: P4D at %px (val=%lx)\n", p4d, p4d_val(*p4d));
	if (p4d_none(*p4d) || p4d_bad(*p4d))
		return;

	pud = pud_offset(p4d, vaddr);
	pr_info("NK-PTW: PUD at %px (val=%lx)\n", pud, pud_val(*pud));
	if (pud_none(*pud) || pud_bad(*pud))
		return;

	pmd = pmd_offset(pud, vaddr);
	pr_info("NK-PTW: PMD at %px (val=%lx)\n", pmd, pmd_val(*pmd));
	if (pmd_none(*pmd) || pmd_bad(*pmd))
		return;

	if (pmd_leaf(*pmd)) {
		pr_info("NK-PTW: PMD is leaf (huge page), no PTE page.\n");
		return;
	}

	pte = pte_offset_kernel(pmd, vaddr);
	pr_info("NK-PTW: PTE at %px (val=%lx)\n", pte, pte_val(*pte));
}
EXPORT_SYMBOL_GPL(nk_report_ptes);

static void nk_analyze_pud_page(unsigned long vaddr)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud_page, *pudp;
	unsigned long nk_base_virt = (unsigned long)__va(nk_base_phys);
	unsigned long nk_end_virt = nk_base_virt + nk_size;
	unsigned long base_va, entry_va;
	int i, citadel = 0, foreign = 0, empty = 0;

	pgd = pgd_offset_k(vaddr);
	if (pgd_none(*pgd) || pgd_bad(*pgd))
		return;
	p4d = p4d_offset(pgd, vaddr);
	if (p4d_none(*p4d) || p4d_bad(*p4d))
		return;

	pud_page = (pud_t *)((unsigned long)pud_offset(p4d, vaddr) & PAGE_MASK);
	base_va = vaddr & ~((unsigned long)PTRS_PER_PUD * PUD_SIZE - 1UL);

	pr_info("NK-PTC: Analyzing PUD page at %px (covers VA %lx - %lx)\n",
		pud_page, base_va,
		base_va + ((unsigned long)PTRS_PER_PUD * PUD_SIZE));

	for (i = 0; i < PTRS_PER_PUD; i++) {
		pudp = pud_page + i;
		if (!pud_present(*pudp)) {
			empty++;
		} else {
			entry_va = base_va + i * PUD_SIZE;
			if (entry_va < nk_end_virt &&
			    (entry_va + PUD_SIZE) > nk_base_virt)
				citadel++;
			else
				foreign++;
		}
	}
	pr_info("NK-PTC: PUD page counts - Citadel: %d, Foreign: %d, Empty: %d. %s\n",
		citadel, foreign, empty,
		foreign == 0 ? "CITADEL-EXCLUSIVE" : "SHARED");
}

static void nk_analyze_pmd_page(unsigned long vaddr)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd_page, *pmdp;
	unsigned long nk_base_virt = (unsigned long)__va(nk_base_phys);
	unsigned long nk_end_virt = nk_base_virt + nk_size;
	unsigned long base_va, entry_va;
	int i, citadel = 0, foreign = 0, empty = 0;

	pgd = pgd_offset_k(vaddr);
	if (pgd_none(*pgd) || pgd_bad(*pgd))
		return;
	p4d = p4d_offset(pgd, vaddr);
	if (p4d_none(*p4d) || p4d_bad(*p4d))
		return;
	pud = pud_offset(p4d, vaddr);
	if (pud_none(*pud) || pud_bad(*pud))
		return;

	pmd_page = (pmd_t *)((unsigned long)pmd_offset(pud, vaddr) & PAGE_MASK);
	base_va = vaddr & ~((unsigned long)PTRS_PER_PMD * PMD_SIZE - 1UL);

	pr_info("NK-PTC: Analyzing PMD page at %px (covers VA %lx - %lx)\n",
		pmd_page, base_va,
		base_va + ((unsigned long)PTRS_PER_PMD * PMD_SIZE));

	for (i = 0; i < PTRS_PER_PMD; i++) {
		pmdp = pmd_page + i;
		if (!pmd_present(*pmdp)) {
			empty++;
		} else {
			entry_va = base_va + i * PMD_SIZE;
			if (entry_va < nk_end_virt &&
			    (entry_va + PMD_SIZE) > nk_base_virt)
				citadel++;
			else
				foreign++;
		}
	}
	pr_info("NK-PTC: PMD page counts - Citadel: %d, Foreign: %d, Empty: %d. %s\n",
		citadel, foreign, empty,
		foreign == 0 ? "CITADEL-EXCLUSIVE" : "SHARED");
}

static void nk_count_citadel_ptpages(void)
{
	unsigned long addr;
	unsigned long nk_base_virt = (unsigned long)__va(nk_base_phys);
	unsigned long end = nk_base_virt + nk_size;
	pte_t *last_pte_page = NULL;
	int pte_page_count = 0;

	pr_info("NK-PTC: Analyzing PTE pages for Citadel range %lx - %lx\n",
		nk_base_virt, end);

	for (addr = nk_base_virt; addr < end; addr += PAGE_SIZE) {
		unsigned int level;
		pte_t *pte = lookup_address(addr, &level);
		pte_t *pte_page;
		int i, citadel, foreign, empty;

		if (!pte)
			continue;

		pte_page = (pte_t *)((unsigned long)pte & PAGE_MASK);
		if (pte_page == last_pte_page)
			continue;

		citadel = 0;
		foreign = 0;
		empty = 0;

		for (i = 0; i < PTRS_PER_PTE; i++) {
			pte_t *p = pte_page + i;
			if (!pte_present(*p))
				empty++;
			else if (nk_pte_maps_citadel(*p))
				citadel++;
			else
				foreign++;
		}

		pte_page_count++;
		pr_info("NK-PTC: Found PTE page %d at %px. Citadel: %d, Foreign: %d, Empty: %d. %s\n",
			pte_page_count, pte_page, citadel, foreign, empty,
			foreign == 0 ? "CITADEL-EXCLUSIVE" : "SHARED");

		last_pte_page = pte_page;
	}
	pr_info("NK-PTC: Total distinct PTE pages for Citadel: %d\n",
		pte_page_count);
}

static int apply_supervisor_pkey(unsigned long addr, int pkey)
{
	unsigned int level;
	pte_t *pte;
	pte_t new_pte;
	unsigned long pteval;
	pte = lookup_address(addr, &level);
	if (!pte)
		return -ENOENT;
	if (level != PG_LEVEL_4K)
		return -EINVAL;
	pteval = pte_val(*pte);
	pteval &= ~_PAGE_PKEY_MASK;
	pteval |= ((unsigned long)(pkey & 0xF)) << _PAGE_PKEY_SHIFT;
	new_pte = __pte(pteval);
	nk_raw_set_pte(pte, new_pte);
	return 0;
}

static void nk_init_cpu_pks(void *info)
{
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

void nk_protect_citadel_pks(void)
{
	unsigned long addr;
	unsigned long nk_base_virt = (unsigned long)__va(nk_base_phys);
	unsigned long end = nk_base_virt + nk_size;
	int success = 0, invalid = 0;

	if (!cpu_feature_enabled(X86_FEATURE_PKS)) {
		printk(KERN_INFO
		       "Nested Kernel: PKS not supported by hardware. Bypassing PKS protection.\n");
		return;
	}

	for (addr = nk_base_virt; addr < end; addr += PAGE_SIZE) {
		int ret = apply_supervisor_pkey(addr, 1);
		if (ret == 0)
			success++;
		else if (ret == -EINVAL)
			invalid++;
	}
	__flush_tlb_all();

	printk(KERN_INFO
	       "Nested Kernel: PKS tagging complete - %d pages tagged, %d pages invalid (huge page leak).\n",
	       success, invalid);

	/* Broadcast CR4 and PKRS initialization to ALL CPUs simultaneously */
	on_each_cpu(nk_init_cpu_pks, NULL, 1);

	printk(KERN_INFO
	       "Nested Kernel: PKS Key 1 applied and locked on all CPUs.\n");
}
