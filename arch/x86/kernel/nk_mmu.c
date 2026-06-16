#include <linux/printk.h>
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
