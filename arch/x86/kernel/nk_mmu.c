#include <linux/printk.h>
#include <linux/compiler.h>
#include <asm/special_insns.h>
#include <asm/processor.h>
#include <asm/nk_mmu.h>

void nk_write_cr0(unsigned long val)
{
	/* Invariant I8: The WP-bit in CR0 is never disabled by OK code.
	 * X86_CR0_WP is defined in asm/processor-flags.h
	 */
	if (unlikely((val & X86_CR0_WP) != X86_CR0_WP)) {
		pr_emerg("[NK] Blocked OK attempt to disable CR0 WP bit!\n");

		/* Force the WP bit to remain set */
		val |= X86_CR0_WP;
	}

	/* Safe execution using raw assembly since we bypassed native_write_cr0 */
	asm volatile("mov %0,%%cr0" : "+r"(val) : : "memory");
}

void nk_write_cr3(unsigned long val)
{
	/* Future: Validate physical address of the new Page Directory */
	asm volatile("mov %0,%%cr3" : : "r"(val) : "memory");
}

void nk_write_cr4(unsigned long val)
{
	/* Future: Prevent SMEP/SMAP from being disabled */
	asm volatile("mov %0,%%cr4" : : "r"(val) : "memory");
}

void nk_set_pte(pte_t *ptep, pte_t pte)
{
	static bool first_pte = true;

	if (unlikely(first_pte)) {
		pr_info("[NK] PTE assignment hijacked.\n");
		first_pte = false;
	}

	/* Future: Validate that 'pte' does not map Nested Kernel memory as writable.
	 * For now, passively pass the assignment through.
	 */
	WRITE_ONCE(*ptep, pte);
}
