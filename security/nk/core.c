#include <linux/printk.h>
#include <linux/nk.h>

void nk_init(void)
{
	pr_info("[NK] Nested Kernel initialized.\n");
}
